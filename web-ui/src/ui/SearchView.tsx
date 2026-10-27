import { useEffect, useState } from 'preact/hooks';
import { BridgeError, type BridgeApi } from '../bridge/bridge';
import type { SearchResult } from '../bridge/protocol';
import { BridgeErrorCode, BridgeRequest } from '../generated/contract';
import type { OpenedItem } from './App';
import { ErrorNote } from './ErrorNote';
import type { Favorites } from './favorites';
import { formatAge, formatRoubles } from './format';

type SearchState =
  | { status: 'idle' }
  | { status: 'loading' }
  | { status: 'done'; result: SearchResult }
  | { status: 'error'; code: string };

const DEBOUNCE_MILLISECONDS = 250;
const MINIMUM_QUERY_LENGTH = 2;
export const MAXIMUM_QUERY_LENGTH = 64;
const ICON_SIZE_PIXELS = 48;

export function SearchView(props: {
  bridge: BridgeApi;
  query: string;
  onQueryChange: (query: string) => void;
  onItemPicked: (item: OpenedItem) => void;
  iconSourceFor: (iconLink: string) => string;
  favorites: Favorites;
}) {
  const { bridge, query } = props;
  const [state, setState] = useState<SearchState>({ status: 'idle' });
  const [retryCount, setRetryCount] = useState(0);

  useEffect(() => {
    const trimmedQuery = query.trim();
    if (trimmedQuery.length < MINIMUM_QUERY_LENGTH) {
      setState({ status: 'idle' });
      return;
    }

    let stillTheLatestQuery = true;
    const debounce = setTimeout(() => {
      setState({ status: 'loading' });
      bridge
        .request(BridgeRequest.Search, { query: trimmedQuery })
        .then((result) => {
          if (stillTheLatestQuery) {
            setState({ status: 'done', result });
          }
        })
        .catch((error: BridgeError) => {
          // A newer keystroke replaced this search; its own result is coming.
          if (stillTheLatestQuery && error.code !== BridgeErrorCode.Superseded) {
            setState({ status: 'error', code: error.code });
          }
        });
    }, DEBOUNCE_MILLISECONDS);

    return () => {
      stillTheLatestQuery = false;
      clearTimeout(debounce);
    };
  }, [query, retryCount]);

  const itemIcon = (iconLink: string) => (
    <img
      class="icon"
      src={props.iconSourceFor(iconLink)}
      alt=""
      width={ICON_SIZE_PIXELS}
      height={ICON_SIZE_PIXELS}
      loading="lazy"
    />
  );

  return (
    <section class="search">
      <input
        class="search-input"
        type="search"
        placeholder="Item name, e.g. Salewa"
        aria-label="Item name"
        autoFocus
        maxLength={MAXIMUM_QUERY_LENGTH}
        value={query}
        onInput={(event) => props.onQueryChange((event.target as HTMLInputElement).value)}
      />
      {state.status === 'idle' &&
        (props.favorites.items.length > 0 ? (
          <>
            <h2 class="section-title">Favourites</h2>
            <ul class="results">
              {props.favorites.items.map((item) => (
                <li key={item.id}>
                  <button class="result" onClick={() => props.onItemPicked(item)}>
                    {itemIcon(item.iconLink)}
                    <span class="result-name">
                      {item.name}
                      <small>{item.shortName}</small>
                    </span>
                  </button>
                </li>
              ))}
            </ul>
          </>
        ) : (
          <p class="hint">Type at least {MINIMUM_QUERY_LENGTH} letters.</p>
        ))}
      {state.status === 'loading' && (
        <p class="hint" aria-live="polite">
          Searching…
        </p>
      )}
      {state.status === 'error' && (
        <ErrorNote code={state.code} onRetry={() => setRetryCount((count) => count + 1)} />
      )}
      {state.status === 'done' && (
        <>
          {state.result.fromCache && (
            <p class="badge offline">Offline · results from {formatAge(state.result.cacheAgeSeconds)}</p>
          )}
          {state.result.items.length === 0 ? (
            <p class="hint">Nothing found for “{query.trim()}”.</p>
          ) : (
            <ul class="results">
              {state.result.items.map((item) => (
                <li key={item.id}>
                  <button class="result" onClick={() => props.onItemPicked(item)}>
                    {itemIcon(item.iconLink)}
                    <span class="result-name">
                      {item.name}
                      <small>
                        {item.slots} slot{item.slots === 1 ? '' : 's'}
                        {item.averagePriceLast24Hours !== null && ` · ~${formatRoubles(item.averagePriceLast24Hours)}`}
                      </small>
                    </span>
                  </button>
                </li>
              ))}
            </ul>
          )}
        </>
      )}
    </section>
  );
}
