import XCTest
@testable import TarkovCompanion

/// The headless API end to end: search -> verdict through the session driver,
/// the disk stores and the core. The network is a host transport serving
/// recorded fixtures.
final class HeadlessApiTests: XCTestCase {
    private static let graphicsCardId = "57347ca924597744596b4e71"

    private func makeRuntime(
        transport: HttpTransport,
        storageDirectories: StorageDirectories = Fixtures.temporaryStorageDirectories(),
        priceTimeToLiveSeconds: Int = GeneratedContract.priceTimeToLiveSecondsDefault
    ) -> CompanionRuntime {
        var configuration = CompanionConfiguration()
        configuration.httpTransport = transport
        configuration.priceTimeToLiveSeconds = priceTimeToLiveSeconds

        return CompanionRuntime(configuration: configuration, profile: .default, storageDirectories: storageDirectories)
    }

    private func assertThrowsCompanionError<Value>(
        _ code: CompanionError.Code,
        _ operation: () async throws -> Value,
        file: StaticString = #filePath,
        line: UInt = #line
    ) async {
        do {
            _ = try await operation()
            XCTFail("expected \(code)", file: file, line: line)
        } catch let error as CompanionError {
            XCTAssertEqual(error.code, code, error.message, file: file, line: line)
        } catch {
            XCTFail("expected \(code), got \(error)", file: file, line: line)
        }
    }

    func testSearchThenVerdict() async throws {
        let runtime = makeRuntime(transport: FixtureTransport())

        let items = try await runtime.searchItems("salewa")
        XCTAssertEqual(items.first?.name, "Salewa first aid kit")

        runtime.profile = try PlayerProfile(level: 30)
        let graphicsCard = try await runtime.recommendation(forItemId: Self.graphicsCardId)

        XCTAssertEqual(graphicsCard.verdict, .keep)
        XCTAssertEqual(graphicsCard.keepReasons.map(\.label), ["Bitcoin farm L2"])
        let flea = try XCTUnwrap(graphicsCard.flea)
        let bestTrader = try XCTUnwrap(graphicsCard.bestTrader)
        XCTAssertGreaterThan(flea.netRubles, bestTrader.priceRubles)
        XCTAssertFalse(graphicsCard.fromOfflineCache)
    }

    func testInvalidInputIsReportedAsAnError() async {
        let runtime = makeRuntime(transport: FixtureTransport())

        await assertThrowsCompanionError(.invalidArgument) {
            try await runtime.searchItems("x")
        }
    }

    func testWithoutNetworkOrCacheTheErrorIsNetwork() async {
        let transport = FixtureTransport()
        transport.offline = true
        let runtime = makeRuntime(transport: transport)

        await assertThrowsCompanionError(.network) {
            try await runtime.searchItems("salewa")
        }
    }

    // After a restart the item is only on disk under its search, so the player searches again, as in the UI.
    func testSearchAndVerdictComeFromTheDiskCacheAfterARestartWithoutNetwork() async throws {
        let storageDirectories = Fixtures.temporaryStorageDirectories()
        let online = makeRuntime(transport: FixtureTransport(), storageDirectories: storageDirectories)
        _ = try await online.searchItems("graphics")
        let fresh = try await online.recommendation(forItemId: Self.graphicsCardId)

        let offlineTransport = FixtureTransport()
        offlineTransport.offline = true
        let restarted = makeRuntime(
            transport: offlineTransport,
            storageDirectories: storageDirectories,
            priceTimeToLiveSeconds: 0
        )
        let cachedItems = try await restarted.searchItems("graphics")
        XCTAssertTrue(cachedItems.contains { item in item.id == Self.graphicsCardId })
        let cached = try await restarted.recommendation(forItemId: Self.graphicsCardId)

        XCTAssertFalse(offlineTransport.receivedRequests.isEmpty, "the restarted SDK tried the network first")
        XCTAssertTrue(cached.fromOfflineCache)
        XCTAssertEqual(cached.verdict, fresh.verdict)
        XCTAssertEqual(cached.bestNetRubles, fresh.bestNetRubles)
    }

    func testCancellingTheTaskThrowsCancellationErrorAndDropsTheResult() async throws {
        let transport = HeldTransport()
        let runtime = makeRuntime(transport: transport)

        let search = Task {
            try await runtime.searchItems("salewa")
        }
        while transport.receivedRequestCount == 0 {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        search.cancel()

        do {
            _ = try await search.value
            XCTFail("a cancelled search delivered its result")
        } catch {
            XCTAssertTrue(error is CancellationError, "\(error)")
        }

        // The request still completes, and the next lookup works.
        transport.release()
        let items = try await runtime.searchItems("salewa")
        XCTAssertEqual(items.first?.name, "Salewa first aid kit")
    }

    func testProfileChangesReachTheCore() async throws {
        let runtime = makeRuntime(transport: FixtureTransport())
        _ = try await runtime.searchItems("graphics")

        runtime.profile = try PlayerProfile(level: 30, ignoreKeepReasons: true)
        let ignoringKeepReasons = try await runtime.recommendation(forItemId: Self.graphicsCardId)

        XCTAssertNotEqual(ignoringKeepReasons.verdict, .keep)
    }

    func testBeforeInitializeLookupsFailWithNotInitialized() async {
        if TarkovCompanion.isInitialized {
            return
        }

        await assertThrowsCompanionError(.notInitialized) {
            try await TarkovCompanion.searchItems("salewa")
        }
    }
}
