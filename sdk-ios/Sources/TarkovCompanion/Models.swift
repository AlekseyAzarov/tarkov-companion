// Initializers are internal, so fields can be added without breaking hosts.

public struct ItemSummary: Equatable, Sendable {
    public let id: String
    public let name: String
    public let shortName: String
    public let iconUrl: String
    public let slots: Int
    public let averagePriceLast24HoursRubles: Int64?
}

/// New verdicts may be added in minor versions.
public enum Verdict: Equatable, Sendable {
    case keep
    case sellFlea
    case sellTrader
    case noBuyer
}

public struct TraderOffer: Equatable, Sendable {
    public let vendorId: String
    public let vendorName: String
    public let priceRubles: Int64
}

public struct FleaOffer: Equatable, Sendable {
    public let available: Bool

    /// "flea_locked", "no_flea" or "no_price" when `available` is false.
    public let unavailableReason: String?
    public let listPriceRubles: Int64
    public let feeRubles: Int64
    public let netRubles: Int64
}

public struct KeepReason: Equatable, Sendable {
    public enum Kind: Equatable, Sendable {
        case task
        case hideout
    }

    public let kind: Kind
    public let label: String
    public let count: Int
}

public struct Recommendation: Equatable, Sendable {
    public let itemId: String
    public let itemName: String
    public let verdict: Verdict

    /// Roubles in hand following the recommended sell path (or what selling would give, for `keep`).
    public let bestNetRubles: Int64
    public let bestNetPerSlotRubles: Int64
    public let bestTrader: TraderOffer?
    public let flea: FleaOffer?
    public let keepReasons: [KeepReason]

    /// Prices are older than the configured time to live.
    public let pricesAreStale: Bool

    /// Served from the offline cache because the network was unavailable.
    public let fromOfflineCache: Bool
}
