import XCTest
@testable import TarkovCompanion

final class DiskStoreTests: XCTestCase {
    private func makeStore(maximumEntryCount: Int?) -> DiskStore {
        DiskStore(
            directory: Fixtures.temporaryStorageDirectories().cache,
            maximumEntryCountOrNilForUnlimited: maximumEntryCount
        )
    }

    func testStoresReadsAndDeletes() throws {
        let store = makeStore(maximumEntryCount: nil)
        XCTAssertNil(try store.value(forKey: "search|regular|en|salewa"))

        try store.store(Data("first".utf8), forKey: "search|regular|en|salewa")
        try store.store(Data("second".utf8), forKey: "search|regular|en|salewa")
        XCTAssertEqual(try store.value(forKey: "search|regular|en|salewa"), Data("second".utf8))

        try store.deleteValue(forKey: "search|regular|en|salewa")
        XCTAssertNil(try store.value(forKey: "search|regular|en|salewa"))
        try store.deleteValue(forKey: "never stored")
    }

    func testKeepsAnEmptyValueApartFromAMissingOne() throws {
        let store = makeStore(maximumEntryCount: nil)
        try store.store(Data(), forKey: "empty")

        XCTAssertEqual(try store.value(forKey: "empty"), Data())
    }

    func testEvictsTheOldestEntriesOverTheLimit() throws {
        let store = makeStore(maximumEntryCount: 2)
        try store.store(Data("1".utf8), forKey: "first")
        Thread.sleep(forTimeInterval: 0.05)
        try store.store(Data("2".utf8), forKey: "second")
        Thread.sleep(forTimeInterval: 0.05)
        try store.store(Data("3".utf8), forKey: "third")

        XCTAssertNil(try store.value(forKey: "first"))
        XCTAssertNotNil(try store.value(forKey: "second"))
        XCTAssertNotNil(try store.value(forKey: "third"))
    }
}

final class PlayerProfileTests: XCTestCase {
    func testFleaUnlocksAtLevel15UnlessOverridden() throws {
        XCTAssertFalse(try PlayerProfile(level: 14).fleaUnlocked)
        XCTAssertTrue(try PlayerProfile(level: 15).fleaUnlocked)
        XCTAssertTrue(try PlayerProfile(level: 5, fleaUnlocked: true).fleaUnlocked)
        XCTAssertEqual(PlayerProfile.default.level, GeneratedContract.playerLevelDefault)
    }

    func testValuesOutOfRangeAreRejected() {
        for makeProfile in [
            { try PlayerProfile(level: 0) },
            { try PlayerProfile(intelligenceCenterLevel: 4) },
            { try PlayerProfile(fleaMarginPercent: 101) },
        ] {
            XCTAssertThrowsError(try makeProfile()) { error in
                XCTAssertEqual((error as? CompanionError)?.code, .invalidArgument)
            }
        }
    }

    func testProfileJsonUsesTheContractKeys() throws {
        let json = try JsonObject(text: CoreSettings.profileJson(try PlayerProfile(level: 22, gameMode: .pve)))

        XCTAssertEqual(try json.integer(GeneratedContract.keyPlayerLevel), 22)
        XCTAssertEqual(try json.string(GeneratedContract.keyGameMode), "pve")
        XCTAssertEqual(try json.boolean(GeneratedContract.keyFleaUnlocked), true)
    }
}
