package com.tarkovcompanion.sdk

import com.tarkovcompanion.sdk.internal.generated.GeneratedContract

public enum class GameMode(internal val apiValue: String) {
    REGULAR("regular"),
    PVE("pve"),
}

/** What verdicts depend on: the flea market unlocks at level 15, Intelligence Center level 3 lowers fees. */
public class PlayerProfile private constructor(builder: Builder) {
    public val level: Int = builder.level
    public val fleaUnlocked: Boolean = builder.fleaUnlocked ?: (builder.level >= FLEA_UNLOCK_LEVEL)
    public val intelligenceCenterLevel: Int = builder.intelligenceCenterLevel
    public val gameMode: GameMode = builder.gameMode
    public val ignoreKeepReasons: Boolean = builder.ignoreKeepReasons
    public val fleaMarginPercent: Int = builder.fleaMarginPercent

    public class Builder {
        internal var level: Int = GeneratedContract.PLAYER_LEVEL_DEFAULT
        internal var fleaUnlocked: Boolean? = null
        internal var intelligenceCenterLevel: Int = GeneratedContract.INTELLIGENCE_CENTER_LEVEL_DEFAULT
        internal var gameMode: GameMode =
            GameMode.entries.first { mode -> mode.apiValue == GeneratedContract.GAME_MODE_DEFAULT }
        internal var ignoreKeepReasons: Boolean = false
        internal var fleaMarginPercent: Int = DEFAULT_FLEA_MARGIN_PERCENT

        public fun level(level: Int): Builder = apply {
            require(level in PLAYER_LEVEL_RANGE) { "level out of range: $level" }
            this.level = level
        }

        /** Overrides the level-based default (flea unlocks at level 15). */
        public fun fleaUnlocked(unlocked: Boolean): Builder = apply { fleaUnlocked = unlocked }

        public fun intelligenceCenterLevel(level: Int): Builder = apply {
            require(level in INTELLIGENCE_CENTER_LEVEL_RANGE) { "intelligenceCenterLevel out of range: $level" }
            intelligenceCenterLevel = level
        }

        public fun gameMode(mode: GameMode): Builder = apply { gameMode = mode }

        /** Recommend selling even items needed for quests or the hideout. */
        public fun ignoreKeepReasons(ignore: Boolean): Builder = apply { ignoreKeepReasons = ignore }

        /** How much more, in percent, the flea must pay than a trader to be worth listing. */
        public fun fleaMarginPercent(percent: Int): Builder = apply {
            require(percent in FLEA_MARGIN_PERCENT_RANGE) { "fleaMarginPercent out of range: $percent" }
            fleaMarginPercent = percent
        }

        public fun build(): PlayerProfile = PlayerProfile(this)
    }

    // From contracts/config-limits.json.
    public companion object {
        public const val FLEA_UNLOCK_LEVEL: Int = GeneratedContract.FLEA_UNLOCK_LEVEL

        private const val DEFAULT_FLEA_MARGIN_PERCENT = GeneratedContract.FLEA_MARGIN_PERCENT_DEFAULT
        private val PLAYER_LEVEL_RANGE = GeneratedContract.PLAYER_LEVEL_MINIMUM..GeneratedContract.PLAYER_LEVEL_MAXIMUM
        private val INTELLIGENCE_CENTER_LEVEL_RANGE =
            GeneratedContract.INTELLIGENCE_CENTER_LEVEL_MINIMUM..GeneratedContract.INTELLIGENCE_CENTER_LEVEL_MAXIMUM
        private val FLEA_MARGIN_PERCENT_RANGE =
            GeneratedContract.FLEA_MARGIN_PERCENT_MINIMUM..GeneratedContract.FLEA_MARGIN_PERCENT_MAXIMUM

        @JvmField
        public val DEFAULT: PlayerProfile = Builder().build()
    }
}
