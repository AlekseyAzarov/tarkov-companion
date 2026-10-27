import { useEffect, useState } from 'preact/hooks';
import type { BridgeApi } from '../bridge/bridge';
import type { ItemSummary } from '../bridge/protocol';
import { BridgeEvent, BridgeRequest, BridgeRoute } from '../generated/contract';
import { AccountChip } from './AccountChip';
import { useFavorites } from './favorites';
import { ItemView } from './ItemView';
import { MAXIMUM_QUERY_LENGTH, SearchView } from './SearchView';

export type OpenedItem = Pick<ItemSummary, 'id'> & Partial<ItemSummary>;

// The page's own analytics events reach the host as web_<event> (contracts/analytics-events.json).
const ANALYTICS_EVENT_ITEM_OPENED = 'item_opened';
const ANALYTICS_PROPERTY_SOURCE = 'source';
const ITEM_OPENED_FROM_SEARCH = 'search';

export function App({ bridge }: { bridge: BridgeApi }) {
  const initialState = bridge.hello.initialState;
  const [query, setQuery] = useState(initialState.query ?? '');
  const [openedItem, setOpenedItem] = useState<OpenedItem | null>(
    initialState.route === BridgeRoute.Item && initialState.itemId ? { id: initialState.itemId } : null,
  );

  // Native keeps this to restore the screen after process death.
  useEffect(() => {
    bridge.fireAndForget(BridgeRequest.State, {
      route: openedItem ? BridgeRoute.Item : BridgeRoute.Search,
      ...(query ? { query: query.slice(0, MAXIMUM_QUERY_LENGTH) } : {}),
      ...(openedItem ? { itemId: openedItem.id } : {}),
    });
  }, [query, openedItem?.id]);

  useEffect(() => bridge.onEvent(BridgeEvent.NavigateBack, () => setOpenedItem(null)), []);
  const favorites = useFavorites(bridge);

  const closeScreen = () => bridge.fireAndForget(BridgeRequest.Close, {});
  const iconSourceFor = (iconLink: string) =>
    bridge.hello.iconProxy ? bridge.hello.iconProxy + encodeURIComponent(iconLink) : iconLink;

  const closeOnBackdropClick = (event: MouseEvent) => {
    if (event.target === event.currentTarget) {
      closeScreen();
    }
  };

  const openPickedItem = (pickedItem: OpenedItem) => {
    bridge.fireAndForget(BridgeRequest.Track, {
      event: ANALYTICS_EVENT_ITEM_OPENED,
      properties: { [ANALYTICS_PROPERTY_SOURCE]: ITEM_OPENED_FROM_SEARCH },
    });
    setOpenedItem(pickedItem);
  };

  return (
    <div class="backdrop" onClick={closeOnBackdropClick}>
      <main class="panel" role="dialog" aria-modal="true" aria-label="Item lookup">
        <header class="bar">
          {openedItem ? (
            <button class="icon-btn" onClick={() => setOpenedItem(null)} aria-label="Back to search">
              ‹
            </button>
          ) : (
            <span class="brand">Sell or keep?</span>
          )}
          <span class="bar-end">
            <AccountChip bridge={bridge} />
            <button class="icon-btn" onClick={closeScreen} aria-label="Close">
              ×
            </button>
          </span>
        </header>
        {openedItem ? (
          <ItemView bridge={bridge} item={openedItem} iconSourceFor={iconSourceFor} favorites={favorites} />
        ) : (
          <SearchView
            bridge={bridge}
            query={query}
            onQueryChange={setQuery}
            onItemPicked={openPickedItem}
            iconSourceFor={iconSourceFor}
            favorites={favorites}
          />
        )}
      </main>
    </div>
  );
}
