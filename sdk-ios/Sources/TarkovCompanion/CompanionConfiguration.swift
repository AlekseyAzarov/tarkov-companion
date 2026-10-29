import Foundation

public enum LogLevel: Sendable {
    case debug
    case info
    case warning
    case error
}

/// SDK configuration; set what you need on a default value.
public struct CompanionConfiguration: Sendable {
    /// GraphQL endpoint. Point it at your own caching proxy in production.
    public var graphqlEndpoint: String = GeneratedContract.endpointDefault
    public var language: String = GeneratedContract.languageDefault

    /// Defaults to URLSession.
    public var httpTransport: HttpTransport?

    /// Defaults to the unified logging system (os_log).
    public var logger: (@Sendable (LogLevel, String) -> Void)?

    /// Event names and properties are listed in contracts/analytics-events.json.
    public var analytics: (@Sendable (String, [String: String]) -> Void)?

    /// Static copies of tarkov.dev's data, read when its GraphQL API fails (docs/catalog.md).
    public var catalogUrl: String?
    public var itemCacheCapacity: Int = GeneratedContract.itemCacheCapacityDefault
    public var priceTimeToLiveSeconds: Int = GeneratedContract.priceTimeToLiveSecondsDefault

    public init() {}
}

/// The SDK's only way onto the network.
public protocol HttpTransport: Sendable {
    /// Throw for transport failures. Non-2xx responses are returned, not thrown.
    func execute(_ request: HttpRequest) async throws -> HttpResponse
}

public struct HttpRequest: Sendable, CustomStringConvertible {
    public let url: String
    public let method: String
    public let headers: [String: String]
    public let body: Data?
    public let timeoutMilliseconds: Int

    public var description: String { "HttpRequest(\(method) \(url))" }
}

public struct HttpResponse: Sendable, CustomStringConvertible {
    public let statusCode: Int
    public let headers: [String: String]
    public let body: Data

    public init(statusCode: Int, headers: [String: String] = [:], body: Data) {
        self.statusCode = statusCode
        self.headers = headers
        self.body = body
    }

    public var description: String { "HttpResponse(\(statusCode), \(body.count) bytes)" }
}
