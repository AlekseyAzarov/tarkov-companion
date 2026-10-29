import Foundation

/// Used when the host provides no transport (ADR 0003). Doesn't follow
/// redirects and keeps no HTTP cache: the core decides what to cache.
final class URLSessionHttpTransport: HttpTransport {
    // A full hideout response is ~100 KB; anything near this limit is not tarkov.dev.
    private static let maximumResponseBytes = 8 * 1024 * 1024
    private static let millisecondsPerSecond = 1000.0

    private let session = URLSession(
        configuration: .ephemeral,
        delegate: RedirectRefusingDelegate(),
        delegateQueue: nil
    )

    func execute(_ request: HttpRequest) async throws -> HttpResponse {
        guard let url = URL(string: request.url) else {
            throw URLError(.badURL)
        }

        var urlRequest = URLRequest(
            url: url,
            cachePolicy: .reloadIgnoringLocalCacheData,
            timeoutInterval: Double(request.timeoutMilliseconds) / Self.millisecondsPerSecond
        )
        urlRequest.httpMethod = request.method
        urlRequest.httpBody = request.body
        for (name, value) in request.headers {
            urlRequest.setValue(value, forHTTPHeaderField: name)
        }

        let (body, response) = try await session.data(for: urlRequest)
        guard let httpResponse = response as? HTTPURLResponse else {
            throw URLError(.badServerResponse)
        }

        if body.count > Self.maximumResponseBytes {
            throw URLError(.dataLengthExceedsMaximum)
        }

        var headers: [String: String] = [:]
        for (name, value) in httpResponse.allHeaderFields {
            if let name = name as? String, let value = value as? String {
                headers[name] = value
            }
        }

        return HttpResponse(statusCode: httpResponse.statusCode, headers: headers, body: body)
    }
}

private final class RedirectRefusingDelegate: NSObject, URLSessionTaskDelegate {
    func urlSession(
        _ session: URLSession,
        task: URLSessionTask,
        willPerformHTTPRedirection response: HTTPURLResponse,
        newRequest request: URLRequest,
        completionHandler: @escaping (URLRequest?) -> Void
    ) {
        completionHandler(nil)
    }
}
