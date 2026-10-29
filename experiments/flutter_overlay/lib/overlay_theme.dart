import 'package:flutter/material.dart';

import 'models.dart';

// The web flow's palette (web-ui/src/styles.css).
abstract final class OverlayColors {
  // The SDK overlay's window background (sdk-android tarkov_companion_scrim).
  static const scrim = Color(0xB3000000);
  static const background = Color(0xFF15171A);
  static const surface = Color(0xFF1F2226);
  static const surfaceRaised = Color(0xFF2A2E33);
  static const text = Color(0xFFE9E6DF);
  static const muted = Color(0xFF9A9890);
  static const line = Color(0xFF3A3F45);
  static const accent = Color(0xFFC9B37E);
  static const keep = Color(0xFFE0A43B);
  static const flea = Color(0xFF5FB37A);
  static const trader = Color(0xFF6FA3D9);
  static const noBuyer = Color(0xFF8B8B8B);
  static const danger = Color(0xFFE06C5C);

  static Color forVerdict(Verdict verdict) {
    return switch (verdict) {
      Verdict.keep => keep,
      Verdict.sellFlea => flea,
      Verdict.sellTrader => trader,
      Verdict.noBuyer => noBuyer,
    };
  }
}

const overlayCornerRadius = Radius.circular(12);

ThemeData overlayTheme() {
  final base = ThemeData(
    brightness: Brightness.dark,
    colorScheme: const ColorScheme.dark(
      primary: OverlayColors.accent,
      surface: OverlayColors.background,
      onSurface: OverlayColors.text,
      error: OverlayColors.danger,
    ),
    scaffoldBackgroundColor: Colors.transparent,
    canvasColor: Colors.transparent,
  );

  return base.copyWith(
    textTheme: base.textTheme.apply(bodyColor: OverlayColors.text, displayColor: OverlayColors.text),
  );
}
