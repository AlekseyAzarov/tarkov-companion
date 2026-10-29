import 'dart:async';

import 'package:flutter/material.dart';

import 'common_widgets.dart';
import 'companion_channel.dart';
import 'format.dart';
import 'models.dart';
import 'overlay_theme.dart';

const minimumQueryLength = 2;
const maximumQueryLength = 64;
const _debounceDuration = Duration(milliseconds: 250);

enum _SearchStatus { idle, loading, done, error }

class SearchView extends StatefulWidget {
  const SearchView({super.key, required this.companion, required this.queryController, required this.onItemPicked});

  final Companion companion;
  final TextEditingController queryController;
  final ValueChanged<ItemSummary> onItemPicked;

  @override
  State<SearchView> createState() => _SearchViewState();
}

class _SearchViewState extends State<SearchView> {
  _SearchStatus _status = _SearchStatus.idle;
  List<ItemSummary> _items = const [];
  String _errorCode = '';
  String _searchedQuery = '';
  Timer? _debounce;

  // Results of a search that a newer keystroke replaced are dropped.
  int _latestSearchNumber = 0;

  @override
  void initState() {
    super.initState();
    widget.queryController.addListener(_onQueryChanged);
    _onQueryChanged();
  }

  @override
  void dispose() {
    widget.queryController.removeListener(_onQueryChanged);
    _debounce?.cancel();
    super.dispose();
  }

  void _onQueryChanged() {
    final trimmedQuery = widget.queryController.text.trim();
    if (trimmedQuery == _searchedQuery && _status != _SearchStatus.idle) {
      return;
    }

    _debounce?.cancel();
    _latestSearchNumber++;
    _searchedQuery = trimmedQuery;

    if (trimmedQuery.length < minimumQueryLength) {
      setState(() => _status = _SearchStatus.idle);
      return;
    }

    _debounce = Timer(_debounceDuration, _search);
  }

  Future<void> _search() async {
    final searchNumber = _latestSearchNumber;
    setState(() => _status = _SearchStatus.loading);

    try {
      final items = await widget.companion.searchItems(_searchedQuery);
      if (mounted && searchNumber == _latestSearchNumber) {
        setState(() {
          _status = _SearchStatus.done;
          _items = items;
        });
      }
    } on CompanionError catch (error) {
      if (mounted && searchNumber == _latestSearchNumber && error.code != CompanionError.superseded) {
        setState(() {
          _status = _SearchStatus.error;
          _errorCode = error.code;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        TextField(
          controller: widget.queryController,
          autofocus: true,
          maxLength: maximumQueryLength,
          textInputAction: TextInputAction.search,
          decoration: const InputDecoration(
            hintText: 'Item name, e.g. Salewa',
            hintStyle: TextStyle(color: OverlayColors.muted),
            counterText: '',
            filled: true,
            fillColor: OverlayColors.surface,
            contentPadding: EdgeInsets.symmetric(horizontal: 14, vertical: 14),
            enabledBorder: OutlineInputBorder(
              borderSide: BorderSide(color: OverlayColors.line),
              borderRadius: BorderRadius.all(Radius.circular(10)),
            ),
            focusedBorder: OutlineInputBorder(
              borderSide: BorderSide(color: OverlayColors.accent, width: 2),
              borderRadius: BorderRadius.all(Radius.circular(10)),
            ),
          ),
        ),
        Flexible(child: _buildBelowInput()),
      ],
    );
  }

  Widget _buildBelowInput() {
    switch (_status) {
      case _SearchStatus.idle:
        return const HintText('Type at least $minimumQueryLength letters.');
      case _SearchStatus.loading:
        return const HintText('Searching…');
      case _SearchStatus.error:
        return ErrorNote(code: _errorCode, onRetry: _search);
      case _SearchStatus.done:
        if (_items.isEmpty) {
          return HintText('Nothing found for “$_searchedQuery”.');
        }

        return ListView.builder(
          shrinkWrap: true,
          padding: const EdgeInsets.only(top: 12),
          itemCount: _items.length,
          itemBuilder: (context, index) => _ResultRow(item: _items[index], onTap: widget.onItemPicked),
        );
    }
  }
}

class _ResultRow extends StatelessWidget {
  const _ResultRow({required this.item, required this.onTap});

  final ItemSummary item;
  final ValueChanged<ItemSummary> onTap;

  @override
  Widget build(BuildContext context) {
    final slotsText = item.slots == 1 ? '1 slot' : '${item.slots} slots';
    final averagePrice = item.averagePriceLast24HoursRubles;
    final details = averagePrice == null ? slotsText : '$slotsText · ~${formatRoubles(averagePrice)}';

    return InkWell(
      onTap: () => onTap(item),
      borderRadius: const BorderRadius.all(Radius.circular(10)),
      highlightColor: OverlayColors.surfaceRaised,
      child: ConstrainedBox(
        constraints: const BoxConstraints(minHeight: 64),
        child: Padding(
          padding: const EdgeInsets.all(8),
          child: Row(
            children: [
              ItemIcon(url: item.iconUrl),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(item.name),
                    const SizedBox(height: 2),
                    Text(details, style: const TextStyle(color: OverlayColors.muted, fontSize: 13)),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
