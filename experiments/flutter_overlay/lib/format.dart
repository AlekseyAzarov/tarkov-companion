// A port of web-ui/src/ui/format.ts and ErrorNote.tsx, so both screens say the same things.

import 'models.dart';

const _thinSpace = ' ';
const _minusSign = '−';
final _thousandsBoundary = RegExp(r'\B(?=(\d{3})+(?!\d))');

// 1234567 -> "1 234 567 ₽".
String formatRoubles(int value) {
  final sign = value < 0 ? _minusSign : '';
  final grouped = value.abs().toString().replaceAll(_thousandsBoundary, _thinSpace);

  return '$sign$grouped ₽';
}

String verdictTitle(Verdict verdict) {
  return switch (verdict) {
    Verdict.keep => 'Keep it',
    Verdict.sellFlea => 'Sell on the Flea',
    Verdict.sellTrader => 'Sell to a trader',
    Verdict.noBuyer => 'Nobody buys this',
  };
}

String verdictHeadline(Recommendation recommendation) {
  switch (recommendation.verdict) {
    case Verdict.keep:
      final reasons = recommendation.keepReasons;
      if (reasons.length == 1) {
        return 'Needed for ${reasons.single.label}';
      }

      return 'Needed in ${reasons.length} places';
    case Verdict.sellFlea:
      return '${formatRoubles(recommendation.bestNetRubles)} after fees';
    case Verdict.sellTrader:
      final vendorName = recommendation.bestTrader?.vendorName ?? 'Trader';

      return '$vendorName pays ${formatRoubles(recommendation.bestNetRubles)}';
    case Verdict.noBuyer:
      return 'No trader buys it and it cannot be listed';
  }
}

const _fleaUnavailableReasons = {
  'flea_locked': 'Flea market unlocks at level 15',
  'no_flea': 'Banned from the flea market',
  'no_price': 'No recent flea price',
};

String? fleaUnavailableNote(Recommendation recommendation) {
  final flea = recommendation.flea;
  if (flea == null || flea.available) {
    return null;
  }

  return _fleaUnavailableReasons[flea.unavailableReason] ?? 'Flea market unavailable';
}

const _messagesByErrorCode = {
  'network': 'No connection, and nothing cached for this yet.',
  'api': 'tarkov.dev is having trouble. Try again in a minute.',
  'schema_mismatch': 'The price source changed format. A game update will fix this.',
  'not_found': 'This item no longer exists.',
};

const _transientErrorCodesWorthRetrying = {'network', 'api'};

String messageForErrorCode(String code) => _messagesByErrorCode[code] ?? 'Something went wrong.';

bool isWorthRetrying(String code) => _transientErrorCodesWorthRetrying.contains(code);
