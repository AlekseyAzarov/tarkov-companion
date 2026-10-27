// Fake native side for `npm run dev`: lets the web flow be developed in a
// desktop browser with hot reload, no device, no Gradle. Stripped from
// production bundles (__DEV__ is false there).
import type { Transport } from './bridge';
import { PROTOCOL_VERSION, type FavoriteView, type ItemSummary, type Recommendation } from './protocol';
import {
  BridgeEvent,
  BridgeFeature,
  BridgeRequest,
  BridgeRoute,
  ErrorCode,
  PayloadFleaUnavailableReason,
  PayloadKeepReasonKind,
  PayloadVerdict,
} from '../generated/contract';

const iconLinkFor = (itemId: string) => `https://assets.tarkov.dev/${itemId}-icon.webp`;

// Built inside a function so esbuild can drop all of it from production bundles
// (top-level calls like iconLinkFor() would count as side effects and be kept).
function createMockCatalog() {
  const items: ItemSummary[] = [
    {
      id: '544fb45d4bdc2dee738b4568',
      name: 'Salewa first aid kit',
      shortName: 'Salewa',
      iconLink: iconLinkFor('544fb45d4bdc2dee738b4568'),
      slots: 2,
      averagePriceLast24Hours: 26412,
    },
    {
      id: '57347ca924597744596b4e71',
      name: 'Graphics card',
      shortName: 'GPU',
      iconLink: iconLinkFor('57347ca924597744596b4e71'),
      slots: 2,
      averagePriceLast24Hours: 512000,
    },
    {
      id: '59e35cbb86f7741778269d83',
      name: 'Corrugated hose',
      shortName: 'Hose',
      iconLink: iconLinkFor('59e35cbb86f7741778269d83'),
      slots: 1,
      averagePriceLast24Hours: 17800,
    },
    {
      id: '5c093ca986f7740a1867ab12',
      name: 'Secure container Kappa',
      shortName: 'Kappa',
      iconLink: iconLinkFor('5c093ca986f7740a1867ab12'),
      slots: 9,
      averagePriceLast24Hours: null,
    },
  ];

  const recommendationsByItemId: Record<string, Recommendation> = {
    '544fb45d4bdc2dee738b4568': {
      itemId: '544fb45d4bdc2dee738b4568',
      itemName: 'Salewa first aid kit',
      verdict: PayloadVerdict.Keep,
      bestNetRubles: 23615,
      bestNetPerSlotRubles: 11807,
      pricesAreStale: false,
      bestTrader: { vendorId: 'therapist', vendorName: 'Therapist', priceRubles: 11760 },
      flea: { available: true, listPriceRubles: 24999, feeRubles: 1384, netRubles: 23615 },
      keepReasons: [{ kind: PayloadKeepReasonKind.Task, label: 'Shortage', count: 1 }],
    },
    '57347ca924597744596b4e71': {
      itemId: '57347ca924597744596b4e71',
      itemName: 'Graphics card',
      verdict: PayloadVerdict.SellFlea,
      bestNetRubles: 469651,
      bestNetPerSlotRubles: 234825,
      pricesAreStale: false,
      bestTrader: { vendorId: 'therapist', vendorName: 'Therapist', priceRubles: 138000 },
      flea: { available: true, listPriceRubles: 497999, feeRubles: 28348, netRubles: 469651 },
      keepReasons: [],
    },
    '59e35cbb86f7741778269d83': {
      itemId: '59e35cbb86f7741778269d83',
      itemName: 'Corrugated hose',
      verdict: PayloadVerdict.SellTrader,
      bestNetRubles: 9450,
      bestNetPerSlotRubles: 9450,
      pricesAreStale: true,
      bestTrader: { vendorId: 'mechanic', vendorName: 'Mechanic', priceRubles: 9450 },
      flea: { available: false, unavailableReason: PayloadFleaUnavailableReason.FleaLocked, listPriceRubles: 16999, feeRubles: 959, netRubles: 16040 },
      keepReasons: [],
    },
    '5c093ca986f7740a1867ab12': {
      itemId: '5c093ca986f7740a1867ab12',
      itemName: 'Secure container Kappa',
      verdict: PayloadVerdict.NoBuyer,
      bestNetRubles: 0,
      bestNetPerSlotRubles: 0,
      pricesAreStale: false,
      bestTrader: null,
      flea: { available: false, unavailableReason: PayloadFleaUnavailableReason.NoFlea, listPriceRubles: 0, feeRubles: 0, netRubles: 0 },
      keepReasons: [],
    },
  };

  return { items, recommendationsByItemId };
}

// Feels like a real round trip without slowing development down.
const DEFAULT_LATENCY_MILLISECONDS = 250;
// Searching for "offline" shows the stale-cache banner, two hours old.
const OFFLINE_DEMO_QUERY = 'offline';
const OFFLINE_DEMO_CACHE_AGE_SECONDS = 2 * 60 * 60;

export function createMockTransport(latencyMilliseconds = DEFAULT_LATENCY_MILLISECONDS): Transport {
  const { items, recommendationsByItemId } = createMockCatalog();
  const favorites: FavoriteView[] = [];
  let deliverToPage: (data: unknown) => void = () => {};
  const replyAfterLatency = (body: object) =>
    setTimeout(() => deliverToPage(JSON.stringify({ version: PROTOCOL_VERSION, ...body })), latencyMilliseconds);

  return {
    listen: (handler) => {
      deliverToPage = handler;
    },
    post: (rawMessage) => {
      const request = JSON.parse(rawMessage) as { id: string; type: string; payload: Record<string, unknown> };
      const succeed = (payload: object) => replyAfterLatency({ replyTo: request.id, ok: true, payload });

      switch (request.type) {
        case BridgeRequest.Hello:
          return succeed({
            protocol: PROTOCOL_VERSION,
            sdkVersion: 'mock',
            features: [BridgeFeature.SignIn, BridgeFeature.Favorites],
            initialState: { route: BridgeRoute.Search },
            account: { signedIn: false },
          });
        case BridgeRequest.SignIn:
          // In the app the system browser opens here; the mock signs in at once.
          succeed({});
          return replyAfterLatency({
            event: BridgeEvent.AccountChanged,
            payload: { account: { signedIn: true, displayName: 'Mock player' } },
          });
        case BridgeRequest.SignOut:
          succeed({});
          return replyAfterLatency({ event: BridgeEvent.AccountChanged, payload: { account: { signedIn: false } } });
        case BridgeRequest.Search: {
          const query = String(request.payload.query ?? '').toLowerCase();
          const matchingItems = items.filter(
            (item) => item.name.toLowerCase().includes(query) || item.shortName.toLowerCase().includes(query),
          );
          const fromCache = query === OFFLINE_DEMO_QUERY;

          return succeed({
            items: matchingItems,
            fromCache,
            cacheAgeSeconds: fromCache ? OFFLINE_DEMO_CACHE_AGE_SECONDS : 0,
          });
        }
        case BridgeRequest.Recommend: {
          const recommendation = recommendationsByItemId[String(request.payload.itemId ?? '')];
          if (!recommendation) {
            return replyAfterLatency({
              replyTo: request.id,
              ok: false,
              error: { code: ErrorCode.NotFound, message: 'unknown item' },
            });
          }

          return succeed({ recommendation, fromCache: false, hideoutMissing: false });
        }
        case BridgeRequest.FavoritesList:
          return succeed({ items: favorites });
        case BridgeRequest.FavoriteSet: {
          const itemId = String(request.payload.itemId);
          const favorite = request.payload.favorite === true;
          const favoriteIndex = favorites.findIndex((existing) => existing.id === itemId);
          const catalogItem = items.find((item) => item.id === itemId);

          if (favorite && favoriteIndex < 0 && catalogItem) {
            const { id, name, shortName, iconLink } = catalogItem;
            favorites.unshift({ id, name, shortName, iconLink });
          }
          if (!favorite && favoriteIndex >= 0) {
            favorites.splice(favoriteIndex, 1);
          }

          replyAfterLatency({ event: BridgeEvent.FavoritesChanged, payload: { items: [...favorites] } });

          return succeed({});
        }
        case BridgeRequest.Close:
          console.info('[mock] close requested');
          return;
        default:
          return succeed({});
      }
    },
  };
}
