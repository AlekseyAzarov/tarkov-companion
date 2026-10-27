import { useEffect, useState } from 'preact/hooks';
import type { BridgeApi } from '../bridge/bridge';
import { parseFavoritesChangedEvent, type FavoriteView } from '../bridge/protocol';
import { BridgeEvent, BridgeFeature, BridgeRequest } from '../generated/contract';

export interface Favorites {
  // An SDK without favourites: hide the star and the list.
  supportedByNative: boolean;
  items: FavoriteView[];
  isFavorite(itemId: string): boolean;
  setFavorite(itemId: string, favorite: boolean): void;
}

/** Kept current by favorites_changed, which native sends after every change, synced ones included. */
export function useFavorites(bridge: BridgeApi): Favorites {
  const supportedByNative = bridge.hello.features.includes(BridgeFeature.Favorites);
  const [items, setItems] = useState<FavoriteView[]>([]);

  useEffect(() => {
    if (!supportedByNative) {
      return;
    }

    bridge.onEvent(BridgeEvent.FavoritesChanged, (payload) => {
      const changedItems = parseFavoritesChangedEvent(payload);
      if (changedItems) {
        setItems(changedItems);
      }
    });

    bridge
      .request(BridgeRequest.FavoritesList, {})
      .then((result) => setItems(result.items))
      .catch(() => {
        // No list to show; the star still works.
      });
  }, []);

  return {
    supportedByNative,
    items,
    isFavorite: (itemId) => items.some((item) => item.id === itemId),
    setFavorite: (itemId, favorite) => bridge.fireAndForget(BridgeRequest.FavoriteSet, { itemId, favorite }),
  };
}
