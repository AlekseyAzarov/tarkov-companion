import 'package:flutter/material.dart';

import 'format.dart';
import 'overlay_theme.dart';

/// A 44×44 round button showing a text glyph (‹, ×), as in the web flow.
class GlyphButton extends StatelessWidget {
  const GlyphButton({super.key, required this.glyph, required this.semanticLabel, required this.onPressed});

  final String glyph;
  final String semanticLabel;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      label: semanticLabel,
      excludeSemantics: true,
      child: SizedBox.square(
        dimension: 44,
        child: InkResponse(
          onTap: onPressed,
          radius: 22,
          highlightColor: OverlayColors.surfaceRaised,
          child: Center(child: Text(glyph, style: const TextStyle(fontSize: 26, height: 1))),
        ),
      ),
    );
  }
}

class ItemIcon extends StatelessWidget {
  const ItemIcon({super.key, required this.url, this.size = 48});

  final String url;
  final double size;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: size,
      height: size,
      decoration: const BoxDecoration(color: OverlayColors.surface, borderRadius: BorderRadius.all(Radius.circular(8))),
      clipBehavior: Clip.antiAlias,
      child: Image.network(
        url,
        fit: BoxFit.contain,
        cacheWidth: (size * MediaQuery.devicePixelRatioOf(context)).round(),
        errorBuilder: (context, error, stackTrace) => const SizedBox.shrink(),
      ),
    );
  }
}

class HintText extends StatelessWidget {
  const HintText(this.text, {super.key});

  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 2),
      child: Text(text, style: const TextStyle(color: OverlayColors.muted)),
    );
  }
}

class OfflineBadge extends StatelessWidget {
  const OfflineBadge(this.text, {super.key});

  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: 12),
      child: Align(
        alignment: Alignment.centerLeft,
        child: DecoratedBox(
          decoration: BoxDecoration(
            color: OverlayColors.keep.withValues(alpha: 0.15),
            borderRadius: const BorderRadius.all(Radius.circular(999)),
          ),
          child: Padding(
            padding: const EdgeInsets.symmetric(vertical: 4, horizontal: 10),
            child: Text(text, style: const TextStyle(color: OverlayColors.keep, fontSize: 13)),
          ),
        ),
      ),
    );
  }
}

class ErrorNote extends StatelessWidget {
  const ErrorNote({super.key, required this.code, required this.onRetry});

  final String code;
  final VoidCallback onRetry;

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.symmetric(vertical: 16),
      padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 14),
      decoration: BoxDecoration(
        color: OverlayColors.danger.withValues(alpha: 0.12),
        borderRadius: const BorderRadius.all(Radius.circular(10)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(messageForErrorCode(code), style: const TextStyle(color: OverlayColors.danger)),
          if (isWorthRetrying(code))
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: OutlinedButton(
                onPressed: onRetry,
                style: OutlinedButton.styleFrom(
                  minimumSize: const Size(0, 44),
                  foregroundColor: OverlayColors.text,
                  backgroundColor: OverlayColors.surfaceRaised,
                  side: const BorderSide(color: OverlayColors.line),
                  shape: const RoundedRectangleBorder(borderRadius: BorderRadius.all(Radius.circular(10))),
                ),
                child: const Text('Try again'),
              ),
            ),
        ],
      ),
    );
  }
}
