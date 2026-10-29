import Foundation
@testable import TarkovCompanion

/// The recorded tarkov.dev responses in contracts/fixtures, shared with the Android tests.
enum Fixtures {
    static let repositoryRoot = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()

    static func text(_ name: String) throws -> String {
        try String(contentsOf: repositoryRoot.appendingPathComponent("contracts/fixtures/\(name)"), encoding: .utf8)
    }

    /// The fixture tarkov.dev would answer this GraphQL request with.
    static func responseBody(for request: HttpRequest) throws -> Data {
        let query = try JsonObject(data: request.body ?? Data()).string("query")
        let fixtureName = query.hasPrefix("query Hideout") ? "hideout.json" : "item_search.json"

        return Data(try text(fixtureName).utf8)
    }

    static func temporaryStorageDirectories() -> StorageDirectories {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)

        return StorageDirectories(
            cache: root.appendingPathComponent("cache", isDirectory: true),
            dataKeptUntilDeleted: root.appendingPathComponent("data", isDirectory: true)
        )
    }
}

/// Answers every request from the fixtures, or fails them all while `offline` is set.
final class FixtureTransport: HttpTransport, @unchecked Sendable {
    private let lock = NSLock()
    private var isOffline = false
    private var requests: [HttpRequest] = []

    var offline: Bool {
        get {
            lock.lock()
            defer { lock.unlock() }

            return isOffline
        }
        set {
            lock.lock()
            defer { lock.unlock() }

            isOffline = newValue
        }
    }

    var receivedRequests: [HttpRequest] {
        lock.lock()
        defer { lock.unlock() }

        return requests
    }

    func execute(_ request: HttpRequest) async throws -> HttpResponse {
        lock.lock()
        requests.append(request)
        let failRequest = isOffline
        lock.unlock()

        if failRequest {
            throw URLError(.notConnectedToInternet)
        }

        return HttpResponse(statusCode: 200, body: try Fixtures.responseBody(for: request))
    }
}

/// Holds every request until `release()`, so a test can act while one is in flight.
final class HeldTransport: HttpTransport, @unchecked Sendable {
    private let lock = NSLock()
    private var waitingRequests: [CheckedContinuation<Void, Never>] = []
    private var released = false
    private var requestCount = 0

    var receivedRequestCount: Int {
        lock.lock()
        defer { lock.unlock() }

        return requestCount
    }

    func execute(_ request: HttpRequest) async throws -> HttpResponse {
        await withCheckedContinuation { continuation in
            lock.lock()
            requestCount += 1
            if released {
                lock.unlock()
                continuation.resume()
                return
            }

            waitingRequests.append(continuation)
            lock.unlock()
        }

        return HttpResponse(statusCode: 200, body: try Fixtures.responseBody(for: request))
    }

    func release() {
        lock.lock()
        released = true
        let continuations = waitingRequests
        waitingRequests = []
        lock.unlock()

        continuations.forEach { continuation in continuation.resume() }
    }
}
