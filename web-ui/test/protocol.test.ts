import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import {
  parseAccountChangedEvent,
  parseFavoritesChangedEvent,
  parseIncomingMessage,
  responseValidators,
} from '../src/bridge/protocol';

const recommendation = {
  itemId: 'a',
  itemName: 'A',
  verdict: 'sell_flea',
  bestNetRubles: 100,
  bestNetPerSlotRubles: 50,
  pricesAreStale: false,
  bestTrader: { vendorId: 'therapist', vendorName: 'Therapist', priceRubles: 80 },
  flea: { available: true, listPriceRubles: 110, feeRubles: 10, netRubles: 100 },
  keepReasons: [{ kind: 'task', label: 'Shortage', count: 1 }],
};

describe('parseIncomingMessage', () => {
  it('classifies results, errors and events', () => {
    expect(parseIncomingMessage('{"version":1,"replyTo":"w1","ok":true,"payload":{}}')).toEqual({
      kind: 'result',
      replyTo: 'w1',
      payload: {},
    });
    expect(
      parseIncomingMessage('{"version":1,"replyTo":null,"ok":false,"error":{"code":"invalid_message","message":"m"}}'),
    ).toEqual({
      kind: 'error',
      replyTo: null,
      code: 'invalid_message',
      message: 'm',
    });
    expect(parseIncomingMessage('{"version":1,"event":"navigate_back","payload":{}}')).toMatchObject({ kind: 'event' });
  });

  it('accepts messages from a newer protocol version (forward compatible envelope)', () => {
    expect(parseIncomingMessage('{"version":7,"replyTo":"w1","ok":true,"payload":{},"extra":1}')).not.toBeNull();
  });

  it.each([42, null, '', '{', '[]', '{"replyTo":"w1","ok":true}', '{"version":1,"ok":true}', '{"version":1,"ok":false,"error":{}}'])(
    'rejects %j',
    (input) => expect(parseIncomingMessage(input)).toBeNull(),
  );
});

describe('responseValidators', () => {
  it('accept a well-formed recommendation and tolerate extra fields', () => {
    const payload = { recommendation: { ...recommendation, newField: 1 }, fromCache: false, hideoutMissing: false };

    expect(responseValidators.recommend(payload)).toBe(true);
  });

  it('reject unknown verdicts', () => {
    const payload = { recommendation: { ...recommendation, verdict: 'yolo' }, fromCache: false, hideoutMissing: false };

    expect(responseValidators.recommend(payload)).toBe(false);
  });

  it('reject wrong types deep in the tree', () => {
    const payload = {
      recommendation: { ...recommendation, keepReasons: [{ kind: 'task', label: 1, count: 1 }] },
      fromCache: false,
      hideoutMissing: false,
    };

    expect(responseValidators.recommend(payload)).toBe(false);
  });

  it('accept null trader and null average price', () => {
    const withoutTrader = { recommendation: { ...recommendation, bestTrader: null }, fromCache: true, hideoutMissing: true };
    const withoutAveragePrice = {
      items: [{ id: 'x', name: 'X', shortName: 'X', iconLink: '', slots: 1, averagePriceLast24Hours: null }],
      fromCache: false,
      cacheAgeSeconds: 0,
    };

    expect(responseValidators.recommend(withoutTrader)).toBe(true);
    expect(responseValidators.search(withoutAveragePrice)).toBe(true);
  });
});

// The C++ core's output is the input of these validators. Keep the TS types and
// core/src/recommender.cpp in lockstep by checking a document of the exact
// shape the core emits (see RecommenderTest.MatchesGoldenContract).
describe('contract with the C++ core', () => {
  it('validates the fixture-derived recommendation', () => {
    const fixtureUrl = new URL('../../contracts/fixtures/core_recommendation.json', import.meta.url);
    const fixture = JSON.parse(readFileSync(fixtureUrl, 'utf8'));

    expect(responseValidators.recommend({ recommendation: fixture, fromCache: false, hideoutMissing: false })).toBe(true);
  });
});

describe('account', () => {
  const hello = { protocol: 1, sdkVersion: '0.1.0', features: ['sign_in'], initialState: { route: 'search' } };

  it('hello may carry the account, and older SDKs leave it out', () => {
    expect(responseValidators.hello({ ...hello, account: { signedIn: true, displayName: 'Nikita' } })).toBe(true);
    expect(responseValidators.hello(hello)).toBe(true);
    expect(responseValidators.hello({ ...hello, account: { signedIn: 'yes' } })).toBe(false);
  });

  it('reads account_changed and nothing else', () => {
    expect(parseAccountChangedEvent({ account: { signedIn: false } })).toEqual({ signedIn: false });
    expect(parseAccountChangedEvent({ account: { signedIn: true, displayName: 'Nikita' } })?.displayName).toBe('Nikita');
    expect(parseAccountChangedEvent({ account: { signedIn: true, displayName: 7 } })).toBeNull();
    expect(parseAccountChangedEvent(null)).toBeNull();
  });
});

describe('favourites', () => {
  const salewa = { id: 'a', name: 'Salewa first aid kit', shortName: 'Salewa', iconLink: 'https://x/a.webp' };

  it('accepts the list and the change event', () => {
    expect(responseValidators.favorites_list({ items: [salewa] })).toBe(true);
    expect(responseValidators.favorites_list({ items: [] })).toBe(true);
    expect(parseFavoritesChangedEvent({ items: [salewa] })).toEqual([salewa]);
  });

  it('rejects items without the fields a list needs', () => {
    expect(responseValidators.favorites_list({ items: [{ id: 'a' }] })).toBe(false);
    expect(parseFavoritesChangedEvent({ items: 'a' })).toBeNull();
  });
});
