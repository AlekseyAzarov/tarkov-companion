import Foundation

/// Where the SDK keeps its files. Versioned, so a future format change can simply start a new directory.
struct StorageDirectories {
    let cache: URL
    let dataKeptUntilDeleted: URL

    static var standard: StorageDirectories {
        let fileManager = FileManager.default
        let caches = fileManager.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        let applicationSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]

        return StorageDirectories(
            cache: caches.appendingPathComponent("tarkov-companion/cache-v1", isDirectory: true),
            dataKeptUntilDeleted: applicationSupport.appendingPathComponent("tarkov-companion/data", isDirectory: true)
        )
    }
}

/// Process-wide SDK state. Creating it touches no disk or network and doesn't start the core.
final class CompanionRuntime: @unchecked Sendable {
    private static let cacheMaximumEntries = 64

    let log: Log
    let driver: SessionDriver
    private let lockedProfile: LockedProfile

    init(configuration: CompanionConfiguration, profile: PlayerProfile, storageDirectories: StorageDirectories) {
        let log = Log(hostLogger: configuration.logger)
        let lockedProfile = LockedProfile(profile)
        let sessionConfigurationJson = CoreSettings.sessionConfigurationJson(configuration, userAgent: Self.userAgent())

        self.log = log
        self.lockedProfile = lockedProfile
        self.driver = SessionDriver(
            openSession: { try NativeSession(configurationJson: sessionConfigurationJson) },
            httpTransport: configuration.httpTransport ?? URLSessionHttpTransport(),
            cacheStore: DiskStore(
                directory: storageDirectories.cache,
                maximumEntryCountOrNilForUnlimited: Self.cacheMaximumEntries
            ),
            dataStoreKeptUntilDeleted: DiskStore(
                directory: storageDirectories.dataKeptUntilDeleted,
                maximumEntryCountOrNilForUnlimited: nil
            ),
            log: log,
            analytics: configuration.analytics,
            currentProfile: { lockedProfile.value }
        )
    }

    var profile: PlayerProfile {
        get { lockedProfile.value }
        set { lockedProfile.value = newValue }
    }

    func searchItems(_ query: String) async throws -> [ItemSummary] {
        let result = try await driver.search(query)

        return try convert(result, ModelMapper.itemSummaries(fromSearchResult:))
    }

    func recommendation(forItemId itemId: String) async throws -> Recommendation {
        let result = try await driver.recommend(itemId)

        return try convert(result, ModelMapper.recommendation(fromResult:))
    }

    private func convert<Model>(_ result: Data, _ mapper: (Data) throws -> Model) throws -> Model {
        do {
            return try mapper(result)
        } catch {
            log.error("bad result payload: \(error)")
            throw CompanionError(.internalError, "bad result payload: \(error)")
        }
    }

    private static func userAgent() -> String {
        let version = ProcessInfo.processInfo.operatingSystemVersion
        #if os(iOS)
        let platform = "iOS"
        #else
        let platform = "macOS"
        #endif

        return "TarkovCompanionSDK/\(GeneratedContract.sdkVersion) (\(platform) \(version.majorVersion).\(version.minorVersion))"
    }
}

private final class LockedProfile: @unchecked Sendable {
    private let lock = NSLock()
    private var storedValue: PlayerProfile

    init(_ value: PlayerProfile) {
        storedValue = value
    }

    var value: PlayerProfile {
        get {
            lock.lock()
            defer { lock.unlock() }

            return storedValue
        }
        set {
            lock.lock()
            defer { lock.unlock() }

            storedValue = newValue
        }
    }
}
