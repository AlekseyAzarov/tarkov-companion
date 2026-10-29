import 'package:flutter/services.dart';

import 'models.dart';

/// Lowercase names of the SDK's ErrorCode, plus "superseded" for a search
/// replaced by a newer one.
class CompanionError implements Exception {
  const CompanionError(this.code);

  final String code;

  static const superseded = 'superseded';

  @override
  String toString() => 'CompanionError($code)';
}

abstract interface class Companion {
  Future<List<ItemSummary>> searchItems(String query);

  Future<Recommendation> getRecommendation(String itemId);
}

/// The SDK's headless API, through the host's handler (host-game, withFlutter flavor).
class MethodChannelCompanion implements Companion {
  static const _channel = MethodChannel('tarkov_companion/overlay');

  @override
  Future<List<ItemSummary>> searchItems(String query) async {
    final items = await _invoke<List<Object?>>('search', {'query': query});

    return [for (final item in items) ItemSummary.fromMap(item! as Map<Object?, Object?>)];
  }

  @override
  Future<Recommendation> getRecommendation(String itemId) async {
    final recommendation = await _invoke<Map<Object?, Object?>>('recommend', {'itemId': itemId});

    return Recommendation.fromMap(recommendation);
  }

  Future<T> _invoke<T>(String method, Map<String, Object?> arguments) async {
    try {
      final result = await _channel.invokeMethod<T>(method, arguments);
      if (result == null) {
        throw const CompanionError('internal');
      }

      return result;
    } on PlatformException catch (exception) {
      throw CompanionError(exception.code);
    }
  }
}
