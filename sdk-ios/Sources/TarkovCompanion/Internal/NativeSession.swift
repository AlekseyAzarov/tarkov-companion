import Foundation
import TarkovCompanionCore

/// A core session (tarkov_companion.h). Thread-safe, as the C API is.
final class NativeSession: @unchecked Sendable {
    private let handle: OpaquePointer

    init(configurationJson: String) throws {
        let coreAbiVersion = Int(tarkov_companion_abi_version())
        if coreAbiVersion != GeneratedContract.nativeAbiVersion {
            throw CompanionError(
                .internalError,
                "core ABI \(coreAbiVersion), SDK built for \(GeneratedContract.nativeAbiVersion)"
            )
        }

        var createdHandle: OpaquePointer?
        var error: UnsafeMutablePointer<CChar>?
        let status = configurationJson.withUTF8Pointer { pointer, length in
            tarkov_companion_session_create(pointer, length, &createdHandle, &error)
        }

        guard status == TARKOV_COMPANION_OK, let createdHandle else {
            throw CompanionError(.internalError, "core rejected the configuration: \(Self.takeMessage(error))")
        }

        handle = createdHandle
    }

    deinit {
        tarkov_companion_session_destroy(handle)
    }

    func setProfile(_ profileJson: String) throws {
        var error: UnsafeMutablePointer<CChar>?
        let status = profileJson.withUTF8Pointer { pointer, length in
            tarkov_companion_session_set_profile(handle, pointer, length, &error)
        }

        if status != TARKOV_COMPANION_OK {
            throw CompanionError(.internalError, "core rejected the profile: \(Self.takeMessage(error))")
        }
    }

    func search(requestId: UInt64, query: String) throws {
        let status = query.withUTF8Pointer { pointer, length in
            tarkov_companion_session_search(handle, requestId, pointer, length)
        }
        try Self.checkCommandAccepted(status, requestId)
    }

    func recommend(requestId: UInt64, itemId: String) throws {
        let status = itemId.withUTF8Pointer { pointer, length in
            tarkov_companion_session_recommend(handle, requestId, pointer, length)
        }
        try Self.checkCommandAccepted(status, requestId)
    }

    func cancel(requestId: UInt64) {
        tarkov_companion_session_cancel(handle, requestId)
    }

    func nextActionOrNilWhenIdle() throws -> SessionAction? {
        var actionJson: UnsafeMutablePointer<CChar>?
        var payload: UnsafeMutablePointer<CChar>?
        var payloadLength = 0
        let status = tarkov_companion_session_next_action(handle, &actionJson, &payload, &payloadLength)
        defer {
            tarkov_companion_free(actionJson)
            tarkov_companion_free(payload)
        }

        if status == TARKOV_COMPANION_NO_ACTION {
            return nil
        }

        guard status == TARKOV_COMPANION_OK, let actionJson else {
            let statusName = String(cString: tarkov_companion_status_string(status))
            throw CompanionError(.internalError, "tarkov_companion_session_next_action failed: \(statusName)")
        }

        let payloadBytes = payload.map { pointer in Data(bytes: pointer, count: payloadLength) }

        return SessionAction.from(json: String(cString: actionJson), payload: payloadBytes)
    }

    func completeHttp(actionId: UInt64, statusCodeOrZeroIfNoResponse: Int32, bodyOrFailureReason: Data) {
        bodyOrFailureReason.withCharPointer { pointer, length in
            _ = tarkov_companion_session_complete_http(handle, actionId, statusCodeOrZeroIfNoResponse, pointer, length)
        }
    }

    func completeRead(actionId: UInt64, valueOrNilIfMissing: Data?) {
        guard let value = valueOrNilIfMissing else {
            _ = tarkov_companion_session_complete_cache_read(handle, actionId, 0, nil, 0)
            return
        }

        value.withCharPointer { pointer, length in
            _ = tarkov_companion_session_complete_cache_read(handle, actionId, 1, pointer, length)
        }
    }

    // Commands are only rejected for a request id already in use: an SDK bug.
    private static func checkCommandAccepted(_ status: tarkov_companion_status, _ requestId: UInt64) throws {
        if status != TARKOV_COMPANION_OK {
            throw CompanionError(.internalError, "core rejected request \(requestId) (status \(status.rawValue))")
        }
    }

    private static func takeMessage(_ message: UnsafeMutablePointer<CChar>?) -> String {
        guard let message else {
            return "no details"
        }

        defer { tarkov_companion_free(message) }

        return String(cString: message)
    }
}

private extension String {
    // The length goes along, so the core never relies on the terminating NUL.
    func withUTF8Pointer<Result>(_ body: (UnsafePointer<CChar>, Int) -> Result) -> Result {
        let length = utf8.count

        return withCString { pointer in body(pointer, length) }
    }
}

private extension Data {
    // Never a NULL pointer, even for no bytes: the C API reads NULL as "absent".
    func withCharPointer<Result>(_ body: (UnsafePointer<CChar>, Int) -> Result) -> Result {
        if isEmpty {
            return "".withCString { emptyText in body(emptyText, 0) }
        }

        return withUnsafeBytes { bytes in
            body(bytes.bindMemory(to: CChar.self).baseAddress!, count)
        }
    }
}
