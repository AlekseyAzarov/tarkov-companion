/// The SDK's public models (sdk-android model/Models.kt) as the host's
/// MethodChannel handler sends them.
library;

class ItemSummary {
  const ItemSummary({
    required this.id,
    required this.name,
    required this.shortName,
    required this.iconUrl,
    required this.slots,
    required this.averagePriceLast24HoursRubles,
  });

  factory ItemSummary.fromMap(Map<Object?, Object?> map) {
    return ItemSummary(
      id: map['id']! as String,
      name: map['name']! as String,
      shortName: map['shortName']! as String,
      iconUrl: map['iconUrl']! as String,
      slots: map['slots']! as int,
      averagePriceLast24HoursRubles: map['averagePriceLast24HoursRubles'] as int?,
    );
  }

  final String id;
  final String name;
  final String shortName;
  final String iconUrl;
  final int slots;
  final int? averagePriceLast24HoursRubles;
}

enum Verdict { keep, sellFlea, sellTrader, noBuyer }

enum KeepReasonKind { task, hideout }

class TraderOffer {
  const TraderOffer({required this.vendorName, required this.priceRubles});

  factory TraderOffer.fromMap(Map<Object?, Object?> map) {
    return TraderOffer(vendorName: map['vendorName']! as String, priceRubles: map['priceRubles']! as int);
  }

  final String vendorName;
  final int priceRubles;
}

class FleaOffer {
  const FleaOffer({
    required this.available,
    required this.unavailableReason,
    required this.listPriceRubles,
    required this.feeRubles,
    required this.netRubles,
  });

  factory FleaOffer.fromMap(Map<Object?, Object?> map) {
    return FleaOffer(
      available: map['available']! as bool,
      unavailableReason: map['unavailableReason'] as String?,
      listPriceRubles: map['listPriceRubles']! as int,
      feeRubles: map['feeRubles']! as int,
      netRubles: map['netRubles']! as int,
    );
  }

  final bool available;
  final String? unavailableReason;
  final int listPriceRubles;
  final int feeRubles;
  final int netRubles;
}

class KeepReason {
  const KeepReason({required this.kind, required this.label, required this.count});

  factory KeepReason.fromMap(Map<Object?, Object?> map) {
    return KeepReason(
      kind: map['kind'] == 'task' ? KeepReasonKind.task : KeepReasonKind.hideout,
      label: map['label']! as String,
      count: map['count']! as int,
    );
  }

  final KeepReasonKind kind;
  final String label;
  final int count;
}

class Recommendation {
  const Recommendation({
    required this.itemName,
    required this.verdict,
    required this.bestNetRubles,
    required this.bestNetPerSlotRubles,
    required this.bestTrader,
    required this.flea,
    required this.keepReasons,
    required this.pricesAreStale,
    required this.fromOfflineCache,
  });

  factory Recommendation.fromMap(Map<Object?, Object?> map) {
    final bestTrader = map['bestTrader'] as Map<Object?, Object?>?;
    final flea = map['flea'] as Map<Object?, Object?>?;
    final keepReasons = map['keepReasons']! as List<Object?>;

    return Recommendation(
      itemName: map['itemName']! as String,
      verdict: _verdictsByWireName[map['verdict']] ?? Verdict.noBuyer,
      bestNetRubles: map['bestNetRubles']! as int,
      bestNetPerSlotRubles: map['bestNetPerSlotRubles']! as int,
      bestTrader: bestTrader == null ? null : TraderOffer.fromMap(bestTrader),
      flea: flea == null ? null : FleaOffer.fromMap(flea),
      keepReasons: [for (final reason in keepReasons) KeepReason.fromMap(reason! as Map<Object?, Object?>)],
      pricesAreStale: map['pricesAreStale']! as bool,
      fromOfflineCache: map['fromOfflineCache']! as bool,
    );
  }

  static const _verdictsByWireName = {
    'keep': Verdict.keep,
    'sell_flea': Verdict.sellFlea,
    'sell_trader': Verdict.sellTrader,
    'no_buyer': Verdict.noBuyer,
  };

  final String itemName;
  final Verdict verdict;
  final int bestNetRubles;
  final int bestNetPerSlotRubles;
  final TraderOffer? bestTrader;
  final FleaOffer? flea;
  final List<KeepReason> keepReasons;
  final bool pricesAreStale;
  final bool fromOfflineCache;
}
