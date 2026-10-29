/// Why an operation failed.
public struct CompanionError: Error, Equatable, Sendable {
    /// New codes are added as the SDK grows (that is not a breaking change):
    /// keep a `default` case when you switch over them.
    public struct Code: RawRepresentable, Hashable, Sendable, CustomStringConvertible {
        public let rawValue: String

        public init(rawValue: String) {
            self.rawValue = rawValue
        }

        public var description: String { rawValue }

        public static let notInitialized = Code(rawValue: "not_initialized")
        public static let invalidArgument = Code(rawValue: GeneratedContract.errorCodeInvalidArgument)

        /// Transport failure and nothing usable in the offline cache.
        public static let network = Code(rawValue: GeneratedContract.errorCodeNetwork)

        /// tarkov.dev answered with an error (outage, rate limit).
        public static let api = Code(rawValue: GeneratedContract.errorCodeApi)

        /// The response no longer matches the expected schema: the upstream API changed.
        public static let schemaMismatch = Code(rawValue: GeneratedContract.errorCodeSchemaMismatch)
        public static let notFound = Code(rawValue: GeneratedContract.errorCodeNotFound)
        public static let internalError = Code(rawValue: GeneratedContract.errorCodeInternal)
    }

    public let code: Code
    public let message: String

    init(_ code: Code, _ message: String) {
        self.code = code
        self.message = message
    }
}
