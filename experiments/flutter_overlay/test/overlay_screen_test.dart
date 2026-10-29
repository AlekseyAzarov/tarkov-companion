import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_overlay/companion_channel.dart';
import 'package:flutter_overlay/models.dart';
import 'package:flutter_overlay/overlay_screen.dart';
import 'package:flutter_test/flutter_test.dart';

const salewa = ItemSummary(
  id: '544fb45d4bdc2dee738b4568',
  name: 'Salewa first aid kit',
  shortName: 'Salewa',
  iconUrl: 'https://assets.tarkov.dev/544fb45d4bdc2dee738b4568-icon.webp',
  slots: 2,
  averagePriceLast24HoursRubles: 21500,
);

const salewaRecommendation = Recommendation(
  itemName: 'Salewa first aid kit',
  verdict: Verdict.keep,
  bestNetRubles: 19000,
  bestNetPerSlotRubles: 9500,
  bestTrader: TraderOffer(vendorName: 'Therapist', priceRubles: 12000),
  flea: FleaOffer(available: true, unavailableReason: null, listPriceRubles: 21500, feeRubles: 2500, netRubles: 19000),
  keepReasons: [KeepReason(kind: KeepReasonKind.hideout, label: 'Medstation 1', count: 2)],
  pricesAreStale: false,
  fromOfflineCache: false,
);

class FakeCompanion implements Companion {
  final searchedQueries = <String>[];
  final searchResults = <Completer<List<ItemSummary>>>[];
  Object? recommendationError;

  @override
  Future<List<ItemSummary>> searchItems(String query) {
    searchedQueries.add(query);
    final result = Completer<List<ItemSummary>>();
    searchResults.add(result);

    return result.future;
  }

  @override
  Future<Recommendation> getRecommendation(String itemId) async {
    final error = recommendationError;
    if (error != null) {
      throw error;
    }

    return salewaRecommendation;
  }
}

// The answer's continuation runs as a microtask after the first frame; the second frame shows it.
Future<void> pumpAnswer(WidgetTester tester) async {
  await tester.pump();
  await tester.pump();
}

void main() {
  testWidgets('searches once the typing pauses, then shows the verdict for the picked item', (tester) async {
    final companion = FakeCompanion();
    await tester.pumpWidget(OverlayApp(companion: companion));

    expect(find.text('Type at least 2 letters.'), findsOneWidget);

    await tester.enterText(find.byType(TextField), 's');
    await tester.pump(const Duration(milliseconds: 300));
    expect(companion.searchedQueries, isEmpty);

    await tester.enterText(find.byType(TextField), 'sal');
    await tester.pump(const Duration(milliseconds: 100));
    await tester.enterText(find.byType(TextField), 'salewa ');
    await tester.pump(const Duration(milliseconds: 300));
    expect(companion.searchedQueries, ['salewa']);
    expect(find.text('Searching…'), findsOneWidget);

    companion.searchResults.single.complete([salewa]);
    await pumpAnswer(tester);
    expect(find.text('2 slots · ~21 500 ₽'), findsOneWidget);

    await tester.tap(find.text('Salewa first aid kit'));
    await pumpAnswer(tester);
    expect(find.text('Keep it'), findsOneWidget);
    expect(find.text('Needed for Medstation 1'), findsOneWidget);
    expect(find.text('Medstation 1 ×2'), findsOneWidget);
    expect(find.text('12 000 ₽ · Therapist'), findsOneWidget);
    expect(find.text('list 21 500 ₽, fee 2 500 ₽'), findsOneWidget);

    await tester.tap(find.bySemanticsLabel('Back to search'));
    await tester.pump();
    expect(find.byType(TextField), findsOneWidget);
  });

  testWidgets('drops the result of a search that a newer query replaced', (tester) async {
    final companion = FakeCompanion();
    await tester.pumpWidget(OverlayApp(companion: companion));

    await tester.enterText(find.byType(TextField), 'salewa');
    await tester.pump(const Duration(milliseconds: 300));
    await tester.enterText(find.byType(TextField), 'grizzly');
    await tester.pump(const Duration(milliseconds: 300));
    expect(companion.searchedQueries, ['salewa', 'grizzly']);

    companion.searchResults.first.complete([salewa]);
    await pumpAnswer(tester);
    expect(find.text('Salewa first aid kit'), findsNothing);

    companion.searchResults.last.complete([]);
    await pumpAnswer(tester);
    expect(find.text('Nothing found for “grizzly”.'), findsOneWidget);
  });

  testWidgets('offers a retry for a network error only', (tester) async {
    final companion = FakeCompanion()..recommendationError = const CompanionError('not_found');
    await tester.pumpWidget(OverlayApp(companion: companion));

    await tester.enterText(find.byType(TextField), 'salewa');
    await tester.pump(const Duration(milliseconds: 300));
    companion.searchResults.single.complete([salewa]);
    await pumpAnswer(tester);
    await tester.tap(find.text('Salewa first aid kit'));
    await pumpAnswer(tester);
    expect(find.text('This item no longer exists.'), findsOneWidget);
    expect(find.text('Try again'), findsNothing);

    await tester.tap(find.bySemanticsLabel('Back to search'));
    await tester.pump();
    companion.recommendationError = const CompanionError('network');
    await tester.pump(const Duration(milliseconds: 300));
    companion.searchResults.last.complete([salewa]);
    await pumpAnswer(tester);
    await tester.tap(find.text('Salewa first aid kit'));
    await pumpAnswer(tester);
    expect(find.text('No connection, and nothing cached for this yet.'), findsOneWidget);

    companion.recommendationError = null;
    await tester.tap(find.text('Try again'));
    await pumpAnswer(tester);
    expect(find.text('Keep it'), findsOneWidget);
  });
}
