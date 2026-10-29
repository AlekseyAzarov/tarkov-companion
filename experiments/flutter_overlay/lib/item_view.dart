import 'package:flutter/material.dart';

import 'common_widgets.dart';
import 'companion_channel.dart';
import 'format.dart';
import 'models.dart';
import 'overlay_theme.dart';

enum _RecommendationStatus { loading, done, error }

class ItemView extends StatefulWidget {
  const ItemView({super.key, required this.companion, required this.item});

  final Companion companion;
  final ItemSummary item;

  @override
  State<ItemView> createState() => _ItemViewState();
}

class _ItemViewState extends State<ItemView> {
  _RecommendationStatus _status = _RecommendationStatus.loading;
  Recommendation? _recommendation;
  String _errorCode = '';

  @override
  void initState() {
    super.initState();
    _loadRecommendation();
  }

  Future<void> _loadRecommendation() async {
    setState(() => _status = _RecommendationStatus.loading);

    try {
      final recommendation = await widget.companion.getRecommendation(widget.item.id);
      if (mounted) {
        setState(() {
          _status = _RecommendationStatus.done;
          _recommendation = recommendation;
        });
      }
    } on CompanionError catch (error) {
      if (mounted) {
        setState(() {
          _status = _RecommendationStatus.error;
          _errorCode = error.code;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final recommendation = _recommendation;
    final title = _status == _RecommendationStatus.done ? recommendation!.itemName : widget.item.name;

    return SingleChildScrollView(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.only(top: 4, bottom: 16),
            child: Row(
              children: [
                ItemIcon(url: widget.item.iconUrl, size: 64),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(title, style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w700)),
                ),
              ],
            ),
          ),
          switch (_status) {
            _RecommendationStatus.loading => const _VerdictSkeleton(),
            _RecommendationStatus.error => ErrorNote(code: _errorCode, onRetry: _loadRecommendation),
            _RecommendationStatus.done => _VerdictDetails(recommendation: recommendation!),
          },
        ],
      ),
    );
  }
}

class _VerdictSkeleton extends StatefulWidget {
  const _VerdictSkeleton();

  @override
  State<_VerdictSkeleton> createState() => _VerdictSkeletonState();
}

class _VerdictSkeletonState extends State<_VerdictSkeleton> with SingleTickerProviderStateMixin {
  late final AnimationController _pulse = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 600),
    lowerBound: 0.5,
  )..repeat(reverse: true);

  @override
  void dispose() {
    _pulse.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return FadeTransition(
      opacity: _pulse,
      child: Container(
        height: 92,
        decoration: const BoxDecoration(
          color: OverlayColors.surface,
          borderRadius: BorderRadius.all(overlayCornerRadius),
        ),
      ),
    );
  }
}

class _VerdictDetails extends StatelessWidget {
  const _VerdictDetails({required this.recommendation});

  final Recommendation recommendation;

  @override
  Widget build(BuildContext context) {
    final verdictColor = OverlayColors.forVerdict(recommendation.verdict);
    final bestTrader = recommendation.bestTrader;
    final flea = recommendation.flea;
    final fleaPriced = flea != null && flea.listPriceRubles > 0;
    final unavailableNote = fleaUnavailableNote(recommendation);
    final pricesOutdated = recommendation.fromOfflineCache || recommendation.pricesAreStale;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Container(
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            color: OverlayColors.surface,
            borderRadius: const BorderRadius.all(overlayCornerRadius),
            border: Border(left: BorderSide(color: verdictColor, width: 6)),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                verdictTitle(recommendation.verdict),
                style: TextStyle(fontSize: 24, fontWeight: FontWeight.w800, color: verdictColor),
              ),
              const SizedBox(height: 4),
              Text(verdictHeadline(recommendation), style: const TextStyle(color: OverlayColors.muted)),
            ],
          ),
        ),
        if (pricesOutdated) const OfflineBadge('Prices may be outdated'),
        if (recommendation.keepReasons.isNotEmpty)
          Padding(
            padding: const EdgeInsets.only(top: 16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                for (final reason in recommendation.keepReasons)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 8),
                    child: _KeepReasonRow(reason: reason),
                  ),
              ],
            ),
          ),
        const SizedBox(height: 20),
        _NumberRow(
          label: 'Best trader',
          value: bestTrader == null ? '—' : '${formatRoubles(bestTrader.priceRubles)} · ${bestTrader.vendorName}',
        ),
        _NumberRow(
          label: 'Flea, after fee',
          value: fleaPriced ? formatRoubles(flea.netRubles) : '—',
          details: [
            if (fleaPriced)
              Text(
                'list ${formatRoubles(flea.listPriceRubles)}, fee ${formatRoubles(flea.feeRubles)}',
                style: const TextStyle(color: OverlayColors.muted, fontSize: 13),
              ),
            if (unavailableNote != null)
              Text(unavailableNote, style: const TextStyle(color: OverlayColors.keep, fontSize: 13)),
          ],
        ),
        _NumberRow(label: 'Per slot', value: formatRoubles(recommendation.bestNetPerSlotRubles)),
      ],
    );
  }
}

class _KeepReasonRow extends StatelessWidget {
  const _KeepReasonRow({required this.reason});

  final KeepReason reason;

  @override
  Widget build(BuildContext context) {
    final isTask = reason.kind == KeepReasonKind.task;

    return Row(
      children: [
        Container(
          constraints: const BoxConstraints(minWidth: 64),
          margin: const EdgeInsets.only(right: 8),
          padding: const EdgeInsets.symmetric(vertical: 2, horizontal: 8),
          decoration: const BoxDecoration(
            color: OverlayColors.surfaceRaised,
            borderRadius: BorderRadius.all(Radius.circular(6)),
          ),
          child: Text(
            isTask ? 'Task' : 'Hideout',
            textAlign: TextAlign.center,
            style: TextStyle(fontSize: 12, color: isTask ? OverlayColors.keep : OverlayColors.trader),
          ),
        ),
        Expanded(child: Text(reason.count > 1 ? '${reason.label} ×${reason.count}' : reason.label)),
      ],
    );
  }
}

class _NumberRow extends StatelessWidget {
  const _NumberRow({required this.label, required this.value, this.details = const []});

  final String label;
  final String value;
  final List<Widget> details;

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.only(bottom: 10),
      decoration: const BoxDecoration(
        border: Border(bottom: BorderSide(color: OverlayColors.line)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: const TextStyle(color: OverlayColors.muted)),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.end,
              children: [
                Text(value, style: const TextStyle(fontFeatures: [FontFeature.tabularFigures()])),
                ...details,
              ],
            ),
          ),
        ],
      ),
    );
  }
}
