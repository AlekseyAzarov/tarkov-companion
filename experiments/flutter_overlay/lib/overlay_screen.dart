import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'common_widgets.dart';
import 'companion_channel.dart';
import 'item_view.dart';
import 'models.dart';
import 'overlay_theme.dart';
import 'search_view.dart';

class OverlayApp extends StatelessWidget {
  const OverlayApp({super.key, required this.companion});

  final Companion companion;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Item lookup',
      debugShowCheckedModeBanner: false,
      color: Colors.transparent,
      theme: overlayTheme(),
      home: OverlayScreen(companion: companion),
    );
  }
}

/// The panel over the game: search, or one item's verdict. Back on the item
/// returns to search; back on search, the close button or a tap outside the
/// panel closes the activity.
class OverlayScreen extends StatefulWidget {
  const OverlayScreen({super.key, required this.companion});

  final Companion companion;

  @override
  State<OverlayScreen> createState() => _OverlayScreenState();
}

class _OverlayScreenState extends State<OverlayScreen> {
  final _queryController = TextEditingController();
  ItemSummary? _openedItem;

  @override
  void dispose() {
    _queryController.dispose();
    super.dispose();
  }

  void _closeOverlay() => SystemNavigator.pop();

  void _backToSearch() => setState(() => _openedItem = null);

  @override
  Widget build(BuildContext context) {
    final openedItem = _openedItem;

    return PopScope(
      canPop: openedItem == null,
      onPopInvokedWithResult: (didPop, result) {
        if (!didPop) {
          _backToSearch();
        }
      },
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: _closeOverlay,
        child: ColoredBox(
          color: OverlayColors.scrim,
          child: SafeArea(
            child: Padding(
              padding: EdgeInsets.only(bottom: MediaQuery.viewInsetsOf(context).bottom),
              child: Center(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  // Swallows taps on the panel so they don't close it.
                  child: GestureDetector(
                    onTap: () {},
                    child: ConstrainedBox(
                      constraints: const BoxConstraints(maxWidth: 520),
                      child: _buildPanel(openedItem),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildPanel(ItemSummary? openedItem) {
    return Material(
      color: OverlayColors.background,
      elevation: 12,
      shadowColor: Colors.black,
      clipBehavior: Clip.antiAlias,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.all(overlayCornerRadius),
        side: BorderSide(color: OverlayColors.line),
      ),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 20),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            SizedBox(
              height: 44,
              child: Row(
                children: [
                  if (openedItem == null)
                    const Text(
                      'SELL OR KEEP?',
                      style: TextStyle(
                        color: OverlayColors.accent,
                        fontSize: 13,
                        fontWeight: FontWeight.w700,
                        letterSpacing: 0.5,
                      ),
                    )
                  else
                    GlyphButton(glyph: '‹', semanticLabel: 'Back to search', onPressed: _backToSearch),
                  const Spacer(),
                  GlyphButton(glyph: '×', semanticLabel: 'Close', onPressed: _closeOverlay),
                ],
              ),
            ),
            Flexible(
              child: openedItem == null
                  ? SearchView(
                      companion: widget.companion,
                      queryController: _queryController,
                      onItemPicked: (item) => setState(() => _openedItem = item),
                    )
                  : ItemView(key: ValueKey(openedItem.id), companion: widget.companion, item: openedItem),
            ),
          ],
        ),
      ),
    );
  }
}
