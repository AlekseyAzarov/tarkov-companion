import { useEffect, useState } from 'preact/hooks';
import { BridgeError, type BridgeApi } from '../bridge/bridge';
import type { RecommendResult } from '../bridge/protocol';
import { BridgeRequest, PayloadKeepReasonKind } from '../generated/contract';
import type { OpenedItem } from './App';
import { ErrorNote } from './ErrorNote';
import type { Favorites } from './favorites';
import { VERDICT_TITLES, fleaUnavailableNote, verdictHeadline, formatRoubles } from './format';

const ICON_SIZE_PIXELS = 64;

type RecommendationState =
  | { status: 'loading' }
  | { status: 'done'; result: RecommendResult }
  | { status: 'error'; code: string };

export function ItemView(props: {
  bridge: BridgeApi;
  item: OpenedItem;
  iconSourceFor: (iconLink: string) => string;
  favorites: Favorites;
}) {
  const { bridge, item, favorites } = props;
  const [state, setState] = useState<RecommendationState>({ status: 'loading' });
  const [retryCount, setRetryCount] = useState(0);

  useEffect(() => {
    let stillShowingThisItem = true;
    setState({ status: 'loading' });

    bridge
      .request(BridgeRequest.Recommend, { itemId: item.id })
      .then((result) => {
        if (stillShowingThisItem) {
          setState({ status: 'done', result });
        }
      })
      .catch((error: BridgeError) => {
        if (stillShowingThisItem) {
          setState({ status: 'error', code: error.code });
        }
      });

    return () => {
      stillShowingThisItem = false;
    };
  }, [item.id, retryCount]);

  const isFavorite = favorites.isFavorite(item.id);

  return (
    <section class="item">
      <div class="item-head">
        {item.iconLink && (
          <img
            class="icon large"
            src={props.iconSourceFor(item.iconLink)}
            alt=""
            width={ICON_SIZE_PIXELS}
            height={ICON_SIZE_PIXELS}
          />
        )}
        <h1>{state.status === 'done' ? state.result.recommendation.itemName : (item.name ?? 'Loading…')}</h1>
        {/* After the verdict: native names favourites from its catalog, which now has this item. */}
        {favorites.supportedByNative && state.status === 'done' && (
          <button
            class={`star${isFavorite ? ' on' : ''}`}
            aria-pressed={isFavorite}
            aria-label={isFavorite ? 'Remove from favourites' : 'Add to favourites'}
            onClick={() => favorites.setFavorite(item.id, !isFavorite)}
          >
            {isFavorite ? '★' : '☆'}
          </button>
        )}
      </div>
      {state.status === 'loading' && <div class="verdict skeleton" aria-busy="true" />}
      {state.status === 'error' && <ErrorNote code={state.code} onRetry={() => setRetryCount((count) => count + 1)} />}
      {state.status === 'done' && <VerdictDetails result={state.result} />}
    </section>
  );
}

function VerdictDetails({ result }: { result: RecommendResult }) {
  const recommendation = result.recommendation;
  const { bestTrader, flea } = recommendation;
  const fleaPriced = flea !== null && flea.listPriceRubles > 0;
  const unavailableNote = fleaUnavailableNote(recommendation);
  const pricesOutdated = result.fromCache || recommendation.pricesAreStale;

  return (
    <>
      <div class={`verdict v-${recommendation.verdict}`} role="status" aria-live="polite">
        <span class="verdict-title">{VERDICT_TITLES[recommendation.verdict]}</span>
        <span class="verdict-line">{verdictHeadline(recommendation)}</span>
      </div>

      {(pricesOutdated || result.hideoutMissing) && (
        <p class="badge offline">{pricesOutdated ? 'Prices may be outdated' : 'Hideout data unavailable'}</p>
      )}

      {recommendation.keepReasons.length > 0 && (
        <ul class="reasons" aria-label="Needed for">
          {recommendation.keepReasons.map((reason) => (
            <li key={reason.kind + reason.label}>
              <span class={`tag ${reason.kind}`}>{reason.kind === PayloadKeepReasonKind.Task ? 'Task' : 'Hideout'}</span>
              {reason.label}
              {reason.count > 1 && <span class="count"> ×{reason.count}</span>}
            </li>
          ))}
        </ul>
      )}

      <dl class="numbers">
        <div>
          <dt>Best trader</dt>
          <dd>{bestTrader ? `${formatRoubles(bestTrader.priceRubles)} · ${bestTrader.vendorName}` : '—'}</dd>
        </div>
        <div>
          <dt>Flea, after fee</dt>
          <dd>
            {fleaPriced ? formatRoubles(flea.netRubles) : '—'}
            {fleaPriced && (
              <small>
                list {formatRoubles(flea.listPriceRubles)}, fee {formatRoubles(flea.feeRubles)}
              </small>
            )}
            {unavailableNote && <small class="warn">{unavailableNote}</small>}
          </dd>
        </div>
        <div>
          <dt>Per slot</dt>
          <dd>{formatRoubles(recommendation.bestNetPerSlotRubles)}</dd>
        </div>
      </dl>
    </>
  );
}
