public enum GameMode: String, Sendable {
    case regular
    case pve
}

/// What verdicts depend on: the flea market unlocks at level 15, Intelligence Center level 3 lowers fees,
/// and quests and hideout levels the player has finished no longer keep an item.
public struct PlayerProfile: Equatable, Sendable {
    public let level: Int
    public let fleaUnlocked: Bool

    /// tarkov.dev ids of the quests the player has completed; `nil` counts every quest as still to do.
    public let completedTaskIds: Set<String>?

    /// Built level of each hideout station by tarkov.dev normalizedName, e.g. `["bitcoin-farm": 2]`. A station
    /// left out is not built; `nil` counts every hideout level as still to build. "intelligence-center" sets the
    /// flea fee discount.
    public let hideoutLevels: [String: Int]?

    public let gameMode: GameMode

    /// Recommend selling even items needed for quests or the hideout.
    public let ignoreKeepReasons: Bool

    /// How much more, in percent, the flea must pay than a trader to be worth listing.
    public let fleaMarginPercent: Int

    // From contracts/config-limits.json.
    public static let fleaUnlockLevel = GeneratedContract.fleaUnlockLevel
    public static let defaultLevel = GeneratedContract.playerLevelDefault
    public static let defaultFleaMarginPercent = GeneratedContract.fleaMarginPercentDefault

    public static let `default` = PlayerProfile(
        validatedLevel: defaultLevel,
        fleaUnlocked: defaultLevel >= fleaUnlockLevel,
        completedTaskIds: nil,
        hideoutLevels: nil,
        gameMode: GameMode(rawValue: GeneratedContract.gameModeDefault) ?? .regular,
        ignoreKeepReasons: false,
        fleaMarginPercent: defaultFleaMarginPercent
    )

    /// `fleaUnlocked` defaults to `level >= fleaUnlockLevel`. Throws
    /// `CompanionError` with `.invalidArgument` for a value out of range.
    public init(
        level: Int = PlayerProfile.defaultLevel,
        fleaUnlocked: Bool? = nil,
        completedTaskIds: Set<String>? = nil,
        hideoutLevels: [String: Int]? = nil,
        gameMode: GameMode = .regular,
        ignoreKeepReasons: Bool = false,
        fleaMarginPercent: Int = PlayerProfile.defaultFleaMarginPercent
    ) throws {
        try Self.requireInRange(
            level,
            GeneratedContract.playerLevelMinimum...GeneratedContract.playerLevelMaximum,
            "level"
        )
        if let completedTaskIds {
            if completedTaskIds.count > GeneratedContract.completedTaskIdsMaximumCount {
                throw CompanionError(
                    .invalidArgument,
                    "more than \(GeneratedContract.completedTaskIdsMaximumCount) task ids"
                )
            }
            if completedTaskIds.contains("") {
                throw CompanionError(.invalidArgument, "empty task id")
            }
        }
        for (station, stationLevel) in hideoutLevels ?? [:] {
            try Self.requireInRange(
                stationLevel,
                GeneratedContract.hideoutLevelsMinimum...GeneratedContract.hideoutLevelsMaximum,
                "hideout level of \(station)"
            )
        }
        try Self.requireInRange(
            fleaMarginPercent,
            GeneratedContract.fleaMarginPercentMinimum...GeneratedContract.fleaMarginPercentMaximum,
            "fleaMarginPercent"
        )

        self.init(
            validatedLevel: level,
            fleaUnlocked: fleaUnlocked ?? (level >= Self.fleaUnlockLevel),
            completedTaskIds: completedTaskIds,
            hideoutLevels: hideoutLevels,
            gameMode: gameMode,
            ignoreKeepReasons: ignoreKeepReasons,
            fleaMarginPercent: fleaMarginPercent
        )
    }

    private init(
        validatedLevel: Int,
        fleaUnlocked: Bool,
        completedTaskIds: Set<String>?,
        hideoutLevels: [String: Int]?,
        gameMode: GameMode,
        ignoreKeepReasons: Bool,
        fleaMarginPercent: Int
    ) {
        self.level = validatedLevel
        self.fleaUnlocked = fleaUnlocked
        self.completedTaskIds = completedTaskIds
        self.hideoutLevels = hideoutLevels
        self.gameMode = gameMode
        self.ignoreKeepReasons = ignoreKeepReasons
        self.fleaMarginPercent = fleaMarginPercent
    }

    private static func requireInRange(_ value: Int, _ range: ClosedRange<Int>, _ name: String) throws {
        if !range.contains(value) {
            throw CompanionError(.invalidArgument, "\(name) out of range: \(value)")
        }
    }
}
