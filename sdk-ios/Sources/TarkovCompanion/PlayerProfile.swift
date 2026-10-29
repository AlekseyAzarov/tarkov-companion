public enum GameMode: String, Sendable {
    case regular
    case pve
}

/// What verdicts depend on: the flea market unlocks at level 15, Intelligence Center level 3 lowers fees.
public struct PlayerProfile: Equatable, Sendable {
    public let level: Int
    public let fleaUnlocked: Bool
    public let intelligenceCenterLevel: Int
    public let gameMode: GameMode

    /// Recommend selling even items needed for quests or the hideout.
    public let ignoreKeepReasons: Bool

    /// How much more, in percent, the flea must pay than a trader to be worth listing.
    public let fleaMarginPercent: Int

    // From contracts/config-limits.json.
    public static let fleaUnlockLevel = GeneratedContract.fleaUnlockLevel
    public static let defaultLevel = GeneratedContract.playerLevelDefault
    public static let defaultIntelligenceCenterLevel = GeneratedContract.intelligenceCenterLevelDefault
    public static let defaultFleaMarginPercent = GeneratedContract.fleaMarginPercentDefault

    public static let `default` = PlayerProfile(
        validatedLevel: defaultLevel,
        fleaUnlocked: defaultLevel >= fleaUnlockLevel,
        intelligenceCenterLevel: defaultIntelligenceCenterLevel,
        gameMode: GameMode(rawValue: GeneratedContract.gameModeDefault) ?? .regular,
        ignoreKeepReasons: false,
        fleaMarginPercent: defaultFleaMarginPercent
    )

    /// `fleaUnlocked` defaults to `level >= fleaUnlockLevel`. Throws
    /// `CompanionError` with `.invalidArgument` for a value out of range.
    public init(
        level: Int = PlayerProfile.defaultLevel,
        fleaUnlocked: Bool? = nil,
        intelligenceCenterLevel: Int = PlayerProfile.defaultIntelligenceCenterLevel,
        gameMode: GameMode = .regular,
        ignoreKeepReasons: Bool = false,
        fleaMarginPercent: Int = PlayerProfile.defaultFleaMarginPercent
    ) throws {
        try Self.requireInRange(
            level,
            GeneratedContract.playerLevelMinimum...GeneratedContract.playerLevelMaximum,
            "level"
        )
        try Self.requireInRange(
            intelligenceCenterLevel,
            GeneratedContract.intelligenceCenterLevelMinimum...GeneratedContract.intelligenceCenterLevelMaximum,
            "intelligenceCenterLevel"
        )
        try Self.requireInRange(
            fleaMarginPercent,
            GeneratedContract.fleaMarginPercentMinimum...GeneratedContract.fleaMarginPercentMaximum,
            "fleaMarginPercent"
        )

        self.init(
            validatedLevel: level,
            fleaUnlocked: fleaUnlocked ?? (level >= Self.fleaUnlockLevel),
            intelligenceCenterLevel: intelligenceCenterLevel,
            gameMode: gameMode,
            ignoreKeepReasons: ignoreKeepReasons,
            fleaMarginPercent: fleaMarginPercent
        )
    }

    private init(
        validatedLevel: Int,
        fleaUnlocked: Bool,
        intelligenceCenterLevel: Int,
        gameMode: GameMode,
        ignoreKeepReasons: Bool,
        fleaMarginPercent: Int
    ) {
        self.level = validatedLevel
        self.fleaUnlocked = fleaUnlocked
        self.intelligenceCenterLevel = intelligenceCenterLevel
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
