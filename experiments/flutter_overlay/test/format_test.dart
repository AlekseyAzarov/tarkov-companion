import 'package:flutter_overlay/format.dart';
import 'package:flutter_overlay/models.dart';
import 'package:flutter_test/flutter_test.dart';

Recommendation recommendation({
  Verdict verdict = Verdict.sellFlea,
  int bestNetRubles = 0,
  TraderOffer? bestTrader,
  FleaOffer? flea,
  List<KeepReason> keepReasons = const [],
}) {
  return Recommendation(
    itemName: 'Salewa first aid kit',
    verdict: verdict,
    bestNetRubles: bestNetRubles,
    bestNetPerSlotRubles: bestNetRubles,
    bestTrader: bestTrader,
    flea: flea,
    keepReasons: keepReasons,
    pricesAreStale: false,
    fromOfflineCache: false,
  );
}

void main() {
  test('formatRoubles groups thousands with thin spaces and uses a real minus sign', () {
    expect(formatRoubles(0), '0 ₽');
    expect(formatRoubles(999), '999 ₽');
    expect(formatRoubles(1234567), '1 234 567 ₽');
    expect(formatRoubles(-15000), '−15 000 ₽');
  });

  test('verdictHeadline matches the web flow', () {
    expect(verdictHeadline(recommendation(verdict: Verdict.sellFlea, bestNetRubles: 21500)), '21 500 ₽ after fees');
    expect(
      verdictHeadline(
        recommendation(
          verdict: Verdict.sellTrader,
          bestNetRubles: 8000,
          bestTrader: const TraderOffer(vendorName: 'Therapist', priceRubles: 8000),
        ),
      ),
      'Therapist pays 8 000 ₽',
    );
    expect(
      verdictHeadline(
        recommendation(
          verdict: Verdict.keep,
          keepReasons: const [KeepReason(kind: KeepReasonKind.task, label: 'Shortage', count: 3)],
        ),
      ),
      'Needed for Shortage',
    );
    expect(
      verdictHeadline(
        recommendation(
          verdict: Verdict.keep,
          keepReasons: const [
            KeepReason(kind: KeepReasonKind.task, label: 'Shortage', count: 3),
            KeepReason(kind: KeepReasonKind.hideout, label: 'Medstation 1', count: 2),
          ],
        ),
      ),
      'Needed in 2 places',
    );
  });

  test('fleaUnavailableNote explains why the flea is out, and stays silent when it is available', () {
    FleaOffer flea({required bool available, String? reason}) =>
        FleaOffer(available: available, unavailableReason: reason, listPriceRubles: 0, feeRubles: 0, netRubles: 0);

    expect(fleaUnavailableNote(recommendation()), isNull);
    expect(fleaUnavailableNote(recommendation(flea: flea(available: true))), isNull);
    expect(
      fleaUnavailableNote(recommendation(flea: flea(available: false, reason: 'flea_locked'))),
      'Flea market unlocks at level 15',
    );
    expect(
      fleaUnavailableNote(recommendation(flea: flea(available: false, reason: 'something_new'))),
      'Flea market unavailable',
    );
  });

  test('only transient errors offer a retry', () {
    expect(isWorthRetrying('network'), isTrue);
    expect(isWorthRetrying('api'), isTrue);
    expect(isWorthRetrying('not_found'), isFalse);
    expect(messageForErrorCode('native_unavailable'), 'Something went wrong.');
  });
}
