import Foundation

/// The SDK's entry point. Lookups throw `CompanionError`, or `CancellationError`
/// when the calling task is cancelled; nothing else throws.
public enum TarkovCompanion {
    public static let sdkVersion = GeneratedContract.sdkVersion

    private static let stateLock = NSLock()
    private static var runtime: CompanionRuntime?
    private static var profileGivenBeforeInitialize: PlayerProfile?

    /// Does no I/O and doesn't start the core: that happens on the first
    /// lookup. Calling it again does nothing.
    public static func initialize(_ configuration: CompanionConfiguration) {
        stateLock.lock()
        defer { stateLock.unlock() }

        if let runtime {
            runtime.log.warning("initialize() called again; the first configuration stays")
            return
        }

        runtime = CompanionRuntime(
            configuration: configuration,
            profile: profileGivenBeforeInitialize ?? .default,
            storageDirectories: .standard
        )
        profileGivenBeforeInitialize = nil
    }

    public static var isInitialized: Bool {
        stateLock.lock()
        defer { stateLock.unlock() }

        return runtime != nil
    }

    /// Updates the player context used for subsequent verdicts. Before `initialize`, it is kept until then.
    public static func setPlayerProfile(_ profile: PlayerProfile) {
        stateLock.lock()
        defer { stateLock.unlock() }

        if let runtime {
            runtime.profile = profile
        } else {
            profileGivenBeforeInitialize = profile
        }
    }

    /// Items whose name matches `query` (2 to 64 characters).
    public static func searchItems(_ query: String) async throws -> [ItemSummary] {
        try await initializedRuntime().searchItems(query)
    }

    /// The verdict for one item id, as returned by `searchItems`.
    public static func recommendation(forItemId itemId: String) async throws -> Recommendation {
        try await initializedRuntime().recommendation(forItemId: itemId)
    }

    private static func initializedRuntime() throws -> CompanionRuntime {
        stateLock.lock()
        defer { stateLock.unlock() }

        guard let runtime else {
            throw CompanionError(.notInitialized, "TarkovCompanion.initialize() has not been called")
        }

        return runtime
    }
}
