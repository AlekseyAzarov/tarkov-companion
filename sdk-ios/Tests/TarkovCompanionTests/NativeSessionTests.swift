import TarkovCompanionCore
import XCTest
@testable import TarkovCompanion

// The core library built for this platform, through its C API: strings, payloads, error reporting.
final class NativeSessionTests: XCTestCase {
    private static let httpOk: Int32 = 200

    private func openSession() throws -> NativeSession {
        try NativeSession(
            configurationJson: CoreSettings.sessionConfigurationJson(CompanionConfiguration(), userAgent: "test")
        )
    }

    private func answerFromFixturesUntilResult(
        _ session: NativeSession,
        _ requestId: UInt64
    ) throws -> Result<Data, CompanionError> {
        while let action = try session.nextActionOrNilWhenIdle() {
            switch action {
            case let .http(actionId, request):
                let body = try Fixtures.responseBody(for: request)
                session.completeHttp(
                    actionId: actionId,
                    statusCodeOrZeroIfNoResponse: Self.httpOk,
                    bodyOrFailureReason: body
                )
            case let .cacheRead(actionId, _), let .dataRead(actionId, _):
                session.completeRead(actionId: actionId, valueOrNilIfMissing: nil)
            case let .result(resultRequestId, outcome) where resultRequestId == requestId:
                return outcome
            default:
                continue
            }
        }

        throw CompanionError(.internalError, "the core went idle without a result for request \(requestId)")
    }

    func testRecommendationMatchesTheGoldenContract() throws {
        let session = try openSession()
        try session.search(requestId: 1, query: "salewa")
        _ = try answerFromFixturesUntilResult(session, 1).get()

        try session.recommend(requestId: 2, itemId: "544fb45d4bdc2dee738b4568")
        let payload = String(decoding: try answerFromFixturesUntilResult(session, 2).get(), as: UTF8.self)

        // Bytes, not parsed documents: the golden file pins the exact output, the same as on Android.
        let golden = try Fixtures.text("core_recommendation.json").trimmingCharacters(in: .whitespacesAndNewlines)
        XCTAssertEqual(payload, #"{"recommendation":\#(golden),"fromCache":false,"hideoutMissing":false}"#)
    }

    func testNonBmpCharactersAndNulBytesSurviveTheBoundary() throws {
        let session = try openSession()
        try session.search(requestId: 1, query: "аптечка 😀")

        var httpActionId: UInt64?
        while httpActionId == nil, let action = try session.nextActionOrNilWhenIdle() {
            switch action {
            case let .dataRead(actionId, _), let .cacheRead(actionId, _):
                session.completeRead(actionId: actionId, valueOrNilIfMissing: nil)
            case let .http(actionId, request):
                let variables = try JsonObject(data: request.body ?? Data()).object("variables")
                XCTAssertEqual(try variables.string("name"), "аптечка 😀")
                httpActionId = actionId
            default:
                continue
            }
        }

        let response = #"{"data":{"items":[{"id":"x1","name":"Аптечка 😀 \u0000ok","shortName":"А"}]}}"#
        session.completeHttp(
            actionId: try XCTUnwrap(httpActionId),
            statusCodeOrZeroIfNoResponse: Self.httpOk,
            bodyOrFailureReason: Data(response.utf8)
        )

        let items = try ModelMapper.itemSummaries(fromSearchResult: try answerFromFixturesUntilResult(session, 1).get())
        XCTAssertEqual(items.first?.name, "Аптечка 😀 \u{0000}ok")
    }

    func testErrorsAreReportedInResults() throws {
        let session = try openSession()
        try session.search(requestId: 1, query: "x")

        guard case let .failure(error) = try answerFromFixturesUntilResult(session, 1) else {
            return XCTFail("a one-letter search succeeded")
        }

        XCTAssertEqual(error.code, .invalidArgument)
    }

    func testInvalidConfigurationIsRejectedWithAReason() {
        XCTAssertThrowsError(try NativeSession(configurationJson: #"{"language":"english"}"#)) { error in
            XCTAssertTrue(String(describing: error).contains("language"), String(describing: error))
        }
    }

    func testCoreVersionComesFromTheSameFileAsTheSdk() {
        XCTAssertEqual(String(cString: tarkov_companion_version()), GeneratedContract.sdkVersion)
        XCTAssertEqual(Int(tarkov_companion_abi_version()), GeneratedContract.nativeAbiVersion)
    }
}
