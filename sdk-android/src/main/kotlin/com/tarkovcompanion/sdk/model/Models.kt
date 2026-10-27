package com.tarkovcompanion.sdk.model

// Internal constructors, so fields can be added without breaking hosts.
public class ItemSummary internal constructor(
    public val id: String,
    public val name: String,
    public val shortName: String,
    public val iconUrl: String,
    public val slots: Int,
    public val averagePriceLast24HoursRubles: Long?,
) {
    override fun toString(): String = "ItemSummary($shortName, id=$id)"
}

public class FavoriteItem internal constructor(
    public val id: String,
    public val name: String,
    public val shortName: String,
    public val iconUrl: String,
) {
    override fun equals(other: Any?): Boolean = other is FavoriteItem && other.id == id && other.name == name

    override fun hashCode(): Int = id.hashCode()

    override fun toString(): String = "FavoriteItem($shortName, id=$id)"
}

/** New verdicts may be added in minor versions: keep an `else` branch. */
public enum class Verdict { KEEP, SELL_FLEA, SELL_TRADER, NO_BUYER }

public class TraderOffer internal constructor(
    public val vendorId: String,
    public val vendorName: String,
    public val priceRubles: Long,
)

public class FleaOffer internal constructor(
    public val available: Boolean,
    /** "flea_locked", "no_flea" or "no_price" when [available] is false. */
    public val unavailableReason: String?,
    public val listPriceRubles: Long,
    public val feeRubles: Long,
    public val netRubles: Long,
)

public class KeepReason internal constructor(
    public val kind: Kind,
    public val label: String,
    public val count: Int,
) {
    public enum class Kind { TASK, HIDEOUT }
}

public class Recommendation internal constructor(
    public val itemId: String,
    public val itemName: String,
    public val verdict: Verdict,
    /** Roubles in hand following the recommended sell path (or what selling would give, for KEEP). */
    public val bestNetRubles: Long,
    public val bestNetPerSlotRubles: Long,
    public val bestTrader: TraderOffer?,
    public val flea: FleaOffer?,
    public val keepReasons: List<KeepReason>,
    /** Prices are older than the configured TTL. */
    public val pricesAreStale: Boolean,
    /** Served from the offline cache because the network was unavailable. */
    public val fromOfflineCache: Boolean,
) {
    override fun toString(): String = "Recommendation($itemName: $verdict, $bestNetRubles RUB)"
}
