import Foundation

private typealias Contract = GeneratedContract

/// Builds the session's settings documents; key names are generated from contracts/config-limits.json.
enum CoreSettings {
    // Sign-in, favourites sync and remote flags are left out: this SDK has no use for them yet.
    static func sessionConfigurationJson(_ configuration: CompanionConfiguration, userAgent: String) -> String {
        jsonText([
            Contract.keyEndpoint: configuration.graphqlEndpoint,
            Contract.keyLanguage: configuration.language,
            Contract.keyUserAgent: userAgent,
            Contract.keyItemCacheCapacity: configuration.itemCacheCapacity,
            Contract.keyPriceTimeToLiveSeconds: configuration.priceTimeToLiveSeconds,
            Contract.keyCatalogUrl: configuration.catalogUrl ?? NSNull(),
        ])
    }

    // Progress the host didn't pass is sent as null, which the core reads as absent.
    static func profileJson(_ profile: PlayerProfile) -> String {
        let completedTaskIds: Any = profile.completedTaskIds.map { taskIds in taskIds.sorted() } ?? NSNull()
        let hideoutLevels: Any = profile.hideoutLevels ?? NSNull()

        return jsonText([
            Contract.keyPlayerLevel: profile.level,
            Contract.keyFleaUnlocked: profile.fleaUnlocked,
            Contract.keyCompletedTaskIds: completedTaskIds,
            Contract.keyHideoutLevels: hideoutLevels,
            Contract.keyGameMode: profile.gameMode.rawValue,
            Contract.keyIgnoreKeepReasons: profile.ignoreKeepReasons,
            Contract.keyFleaMarginPercent: profile.fleaMarginPercent,
        ])
    }

    // Only ever given strings, numbers, booleans, NSNull, string arrays and dictionaries of numbers, which always
    // serialize.
    private static func jsonText(_ fields: [String: Any]) -> String {
        let data = try! JSONSerialization.data(withJSONObject: fields, options: [.sortedKeys])

        return String(decoding: data, as: UTF8.self)
    }
}
