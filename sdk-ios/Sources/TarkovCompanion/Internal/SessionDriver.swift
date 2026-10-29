import Foundation

/// Runs the core session's actions with the host's HttpTransport and the
/// SDK's stores. All tarkov.dev logic is in the core.
///
/// Calls into the core and its actions run on one serial queue, in the order
/// the core produced them; only HTTP leaves it, and its completion comes back
/// to it. A result reaches its caller once, unless the caller's task was
/// cancelled first. Every action that expects an answer gets one, even when a
/// store fails or the action can't be read.
final class SessionDriver: @unchecked Sendable {
    // tarkov_companion_session_complete_http with no HTTP response at all.
    private static let statusCodeNoResponse: Int32 = 0

    private let openSession: () throws -> NativeSession
    private let httpTransport: HttpTransport
    private let cacheStore: KeyValueStore
    private let dataStoreKeptUntilDeleted: KeyValueStore
    private let log: Log
    private let analytics: (@Sendable (String, [String: String]) -> Void)?
    private let currentProfile: @Sendable () -> PlayerProfile
    private let coreQueue = DispatchQueue(label: "com.tarkovcompanion.core", qos: .utility)

    // Only touched on coreQueue.
    private var openedSession: NativeSession?
    private var profileLastSentToCore: PlayerProfile?

    private let deliveriesLock = NSLock()
    private var lastRequestId: UInt64 = 0
    private var deliveriesByRequestId: [UInt64: Delivery] = [:]

    init(
        openSession: @escaping () throws -> NativeSession,
        httpTransport: HttpTransport,
        cacheStore: KeyValueStore,
        dataStoreKeptUntilDeleted: KeyValueStore,
        log: Log,
        analytics: (@Sendable (String, [String: String]) -> Void)?,
        currentProfile: @escaping @Sendable () -> PlayerProfile
    ) {
        self.openSession = openSession
        self.httpTransport = httpTransport
        self.cacheStore = cacheStore
        self.dataStoreKeptUntilDeleted = dataStoreKeptUntilDeleted
        self.log = log
        self.analytics = analytics
        self.currentProfile = currentProfile
    }

    /// The result document of a search (tarkov_companion.h).
    func search(_ query: String) async throws -> Data {
        try await runCommand { session, requestId in
            try session.search(requestId: requestId, query: query)
        }
    }

    /// The result document of a recommendation (tarkov_companion.h).
    func recommend(_ itemId: String) async throws -> Data {
        try await runCommand { session, requestId in
            try session.recommend(requestId: requestId, itemId: itemId)
        }
    }

    private func runCommand(_ command: @escaping (NativeSession, UInt64) throws -> Void) async throws -> Data {
        let delivery = Delivery()
        let requestId = register(delivery)

        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                delivery.attach(continuation)
                coreQueue.async {
                    self.startCommand(requestId, command)
                }
            }
        } onCancel: {
            if delivery.finish(with: .failure(CancellationError())) {
                _ = self.unregister(requestId)
                self.coreQueue.async {
                    self.openedSession?.cancel(requestId: requestId)
                }
            }
        }
    }

    // On coreQueue.
    private func startCommand(_ requestId: UInt64, _ command: (NativeSession, UInt64) throws -> Void) {
        guard let delivery = registeredDelivery(requestId) else {
            // Cancelled before it reached the core.
            return
        }

        let session: NativeSession
        do {
            session = try openSessionIfNeeded()
            try sendProfileToCoreIfChanged(session)
            try command(session, requestId)
        } catch {
            // Only failures of the command itself are this request's; a
            // failing action below may belong to any request.
            _ = unregister(requestId)
            delivery.finish(with: .failure(Self.companionError(error)))
            return
        }

        runActionsUntilIdle(session)
    }

    private func openSessionIfNeeded() throws -> NativeSession {
        if let openedSession {
            return openedSession
        }

        let session = try openSession()
        openedSession = session

        return session
    }

    private func sendProfileToCoreIfChanged(_ session: NativeSession) throws {
        let profile = currentProfile()
        if profile != profileLastSentToCore {
            try session.setProfile(CoreSettings.profileJson(profile))
            profileLastSentToCore = profile
        }
    }

    // On coreQueue only: that is what keeps the actions in order.
    private func runActionsUntilIdle(_ session: NativeSession) {
        while true {
            let action: SessionAction
            do {
                guard let nextAction = try session.nextActionOrNilWhenIdle() else {
                    return
                }

                action = nextAction
            } catch {
                // The core already handed it out, e.g. copying it ran out of memory.
                log.error("lost an action of the core session: \(error)")
                continue
            }

            run(action, session)
        }
    }

    private func run(_ action: SessionAction, _ session: NativeSession) {
        switch action {
        case let .http(actionId, request):
            // Only the request leaves the core queue; its completion comes back to it.
            Task.detached(priority: .utility) {
                let (statusCode, body) = await self.executeHttp(request)
                self.coreQueue.async {
                    session.completeHttp(
                        actionId: actionId,
                        statusCodeOrZeroIfNoResponse: statusCode,
                        bodyOrFailureReason: body
                    )
                    self.runActionsUntilIdle(session)
                }
            }
        case let .cacheRead(actionId, key):
            let value = readOrNilIfFailed("cache") { try cacheStore.value(forKey: key) }
            session.completeRead(actionId: actionId, valueOrNilIfMissing: value)
        case let .cacheWrite(key, value):
            writeLoggingFailure("cache") { try cacheStore.store(value, forKey: key) }
        case let .dataRead(actionId, key):
            let value = readOrNilIfFailed("data") { try dataStoreKeptUntilDeleted.value(forKey: key) }
            session.completeRead(actionId: actionId, valueOrNilIfMissing: value)
        case let .dataWrite(key, value):
            writeLoggingFailure("data") { try dataStoreKeptUntilDeleted.store(value, forKey: key) }
        case let .dataDelete(key):
            writeLoggingFailure("data") { try dataStoreKeptUntilDeleted.deleteValue(forKey: key) }
        case let .secretRead(actionId, _):
            // Only sign-in keeps secrets, and it is not configured on this platform.
            session.completeRead(actionId: actionId, valueOrNilIfMissing: nil)
        case let .secretWrite(key), let .secretDelete(key):
            log.warning("no secret store on this platform; dropping a change to '\(key)'")
        case let .result(requestId, outcome):
            unregister(requestId)?.finish(with: outcome.mapError { failure -> Error in failure })
        case let .log(level, message):
            log.log(level, message)
        case let .track(event, properties):
            analytics?(event, properties)
        case let .notUsedOnThisPlatform(type):
            log.debug("skipping a '\(type)' action: not used on this platform yet")
        case let .unknownFromNewerCore(json):
            log.warning("skipping an action this SDK does not know: \(json)")
        case let .unreadable(json, reason, readActionIdToAnswerNotFound, httpActionIdToAnswerNoResponse):
            log.error("could not read an action of the core session (\(reason)): \(json)")
            if let actionId = readActionIdToAnswerNotFound {
                session.completeRead(actionId: actionId, valueOrNilIfMissing: nil)
            }
            if let actionId = httpActionIdToAnswerNoResponse {
                let failureReason = Data("unreadable http action: \(reason)".utf8)
                session.completeHttp(
                    actionId: actionId,
                    statusCodeOrZeroIfNoResponse: Self.statusCodeNoResponse,
                    bodyOrFailureReason: failureReason
                )
            }
        }
    }

    // Anything the host transport throws must still complete the action, or
    // the request waiting on it would never get a result.
    private func executeHttp(_ request: HttpRequest) async -> (Int32, Data) {
        do {
            let response = try await httpTransport.execute(request)

            return (Int32(clamping: response.statusCode), response.body)
        } catch {
            return (Self.statusCodeNoResponse, Data(String(describing: error).utf8))
        }
    }

    // A store that fails reads as "not there": the core falls back as it would for a missing entry.
    private func readOrNilIfFailed(_ storeName: String, _ read: () throws -> Data?) -> Data? {
        do {
            return try read()
        } catch {
            log.error("\(storeName) store read failed: \(error)")
            return nil
        }
    }

    // A full disk must not stop the actions queued after this one.
    private func writeLoggingFailure(_ storeName: String, _ write: () throws -> Void) {
        do {
            try write()
        } catch {
            log.error("\(storeName) store write failed: \(error)")
        }
    }

    private func register(_ delivery: Delivery) -> UInt64 {
        deliveriesLock.lock()
        defer { deliveriesLock.unlock() }

        lastRequestId += 1
        deliveriesByRequestId[lastRequestId] = delivery

        return lastRequestId
    }

    private func registeredDelivery(_ requestId: UInt64) -> Delivery? {
        deliveriesLock.lock()
        defer { deliveriesLock.unlock() }

        return deliveriesByRequestId[requestId]
    }

    private func unregister(_ requestId: UInt64) -> Delivery? {
        deliveriesLock.lock()
        defer { deliveriesLock.unlock() }

        return deliveriesByRequestId.removeValue(forKey: requestId)
    }

    static func companionError(_ error: Error) -> CompanionError {
        error as? CompanionError ?? CompanionError(.internalError, String(describing: error))
    }
}

/// One request's way back to its caller. Whoever finishes it first wins: the
/// core's result or the caller's cancellation.
private final class Delivery: @unchecked Sendable {
    private let lock = NSLock()
    private var continuation: CheckedContinuation<Data, Error>?
    private var outcomeWaitingForContinuation: Result<Data, Error>?
    private var finished = false

    // The task may be cancelled before its continuation exists; the outcome then waits here.
    func attach(_ continuation: CheckedContinuation<Data, Error>) {
        lock.lock()
        guard let outcome = outcomeWaitingForContinuation else {
            self.continuation = continuation
            lock.unlock()
            return
        }

        outcomeWaitingForContinuation = nil
        lock.unlock()
        continuation.resume(with: outcome)
    }

    @discardableResult
    func finish(with outcome: Result<Data, Error>) -> Bool {
        lock.lock()
        if finished {
            lock.unlock()
            return false
        }

        finished = true
        let waitingContinuation = continuation
        continuation = nil
        if waitingContinuation == nil {
            outcomeWaitingForContinuation = outcome
        }
        lock.unlock()

        waitingContinuation?.resume(with: outcome)

        return true
    }
}
