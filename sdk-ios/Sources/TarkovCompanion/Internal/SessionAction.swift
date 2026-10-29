import Foundation

private typealias Contract = GeneratedContract

/// An action of the core session (tarkov_companion_session_next_action).
enum SessionAction {
    case http(actionId: UInt64, request: HttpRequest)
    case cacheRead(actionId: UInt64, key: String)
    case cacheWrite(key: String, value: Data)
    case dataRead(actionId: UInt64, key: String)
    case dataWrite(key: String, value: Data)
    case dataDelete(key: String)
    case secretRead(actionId: UInt64, key: String)
    case secretWrite(key: String)
    case secretDelete(key: String)
    case result(requestId: UInt64, outcome: Result<Data, CompanionError>)
    case log(level: LogLevel, message: String)
    case track(event: String, properties: [String: String])

    // Known types this SDK has no use for yet: the web UI's bridge, sign-in, favourites, remote flags.
    case notUsedOnThisPlatform(type: String)
    case unknownFromNewerCore(json: String)

    // A known action this SDK could not read (a field missing or of the wrong
    // type). If it expected an answer, it still gets one, so the request
    // waiting on it finishes.
    case unreadable(
        json: String,
        reason: String,
        readActionIdToAnswerNotFound: UInt64?,
        httpActionIdToAnswerNoResponse: UInt64?
    )

    private static let readActionTypes: Set<String> = [
        Contract.actionTypeCacheRead,
        Contract.actionTypeSecretRead,
        Contract.actionTypeDataRead,
    ]

    private static let notUsedActionTypes: Set<String> = [
        Contract.actionTypeFlags,
        Contract.actionTypeOpenBrowser,
        Contract.actionTypeAccount,
        Contract.actionTypeFavorites,
        Contract.actionTypeBridgeSend,
        Contract.actionTypeBridgeState,
        Contract.actionTypeBridgeClose,
        Contract.actionTypeBridgeSignIn,
    ]

    private static let errorCodesByWireName: [String: CompanionError.Code] = [
        Contract.errorCodeInvalidArgument: .invalidArgument,
        Contract.errorCodeNetwork: .network,
        Contract.errorCodeApi: .api,
        Contract.errorCodeSchemaMismatch: .schemaMismatch,
        Contract.errorCodeNotFound: .notFound,
        Contract.errorCodeInternal: .internalError,
    ]

    private static let logLevelsByWireName: [String: LogLevel] = [
        Contract.actionLogLevelDebug: .debug,
        Contract.actionLogLevelInfo: .info,
        Contract.actionLogLevelWarn: .warning,
        Contract.actionLogLevelError: .error,
    ]

    static func from(json: String, payload: Data?) -> SessionAction {
        do {
            return try read(json: json, payload: payload)
        } catch {
            return unreadable(json: json, reason: "\(error)")
        }
    }

    // As much as can still be read: the type and the id the answer needs.
    private static func unreadable(json: String, reason: String) -> SessionAction {
        let object = try? JsonObject(text: json)
        let type = (try? object?.string(Contract.actionFieldType)) ?? ""
        let actionId = try? object?.unsignedInteger(Contract.actionFieldId)

        return .unreadable(
            json: json,
            reason: reason,
            readActionIdToAnswerNotFound: readActionTypes.contains(type) ? actionId : nil,
            httpActionIdToAnswerNoResponse: type == Contract.actionTypeHttp ? actionId : nil
        )
    }

    private static func read(json: String, payload: Data?) throws -> SessionAction {
        let object = try JsonObject(text: json)
        let payloadOrEmpty = payload ?? Data()
        let type = try object.string(Contract.actionFieldType)

        switch type {
        case Contract.actionTypeHttp:
            let request = HttpRequest(
                url: try object.string(Contract.actionFieldUrl),
                method: try object.string(Contract.actionFieldMethod),
                headers: try object.stringMap(Contract.actionFieldHeaders),
                body: payload,
                timeoutMilliseconds: Int(try object.unsignedInteger(Contract.actionFieldTimeoutMilliseconds))
            )

            return .http(actionId: try actionId(object), request: request)
        case Contract.actionTypeCacheRead:
            return .cacheRead(actionId: try actionId(object), key: try object.string(Contract.actionFieldKey))
        case Contract.actionTypeCacheWrite:
            return .cacheWrite(key: try object.string(Contract.actionFieldKey), value: payloadOrEmpty)
        case Contract.actionTypeDataRead:
            return .dataRead(actionId: try actionId(object), key: try object.string(Contract.actionFieldKey))
        case Contract.actionTypeDataWrite:
            return .dataWrite(key: try object.string(Contract.actionFieldKey), value: payloadOrEmpty)
        case Contract.actionTypeDataDelete:
            return .dataDelete(key: try object.string(Contract.actionFieldKey))
        case Contract.actionTypeSecretRead:
            return .secretRead(actionId: try actionId(object), key: try object.string(Contract.actionFieldKey))
        case Contract.actionTypeSecretWrite:
            return .secretWrite(key: try object.string(Contract.actionFieldKey))
        case Contract.actionTypeSecretDelete:
            return .secretDelete(key: try object.string(Contract.actionFieldKey))
        case Contract.actionTypeResult:
            return try result(object, payload: payloadOrEmpty)
        case Contract.actionTypeLog:
            let level = logLevelsByWireName[try object.string(Contract.actionFieldLevel)] ?? .error

            return .log(level: level, message: try object.string(Contract.actionFieldMessage))
        case Contract.actionTypeTrack:
            return .track(
                event: try object.string(Contract.actionFieldEvent),
                properties: try object.stringMap(Contract.actionFieldProperties)
            )
        case _ where notUsedActionTypes.contains(type):
            return .notUsedOnThisPlatform(type: type)
        default:
            return .unknownFromNewerCore(json: json)
        }
    }

    private static func actionId(_ object: JsonObject) throws -> UInt64 {
        try object.unsignedInteger(Contract.actionFieldId)
    }

    private static func result(_ object: JsonObject, payload: Data) throws -> SessionAction {
        let requestId = try object.unsignedInteger(Contract.actionFieldRequest)
        guard let error = try object.optionalObject(Contract.actionFieldError) else {
            return .result(requestId: requestId, outcome: .success(payload))
        }

        let code = errorCodesByWireName[try error.string(Contract.actionFieldCode)] ?? .internalError
        let failure = CompanionError(code, try error.string(Contract.actionFieldMessage))

        return .result(requestId: requestId, outcome: .failure(failure))
    }
}
