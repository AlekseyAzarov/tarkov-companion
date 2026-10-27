import { describe, expect, it } from 'vitest';
import type { Recommendation } from '../src/bridge/protocol';
import { fleaUnavailableNote, formatAge, formatRoubles, verdictHeadline } from '../src/ui/format';

const sellToTrader: Recommendation = {
  itemId: 'a',
  itemName: 'A',
  verdict: 'sell_trader',
  bestNetRubles: 9450,
  bestNetPerSlotRubles: 9450,
  pricesAreStale: false,
  bestTrader: { vendorId: 'mechanic', vendorName: 'Mechanic', priceRubles: 9450 },
  flea: { available: false, unavailableReason: 'flea_locked', listPriceRubles: 16999, feeRubles: 959, netRubles: 16040 },
  keepReasons: [],
};

describe('format', () => {
  it('groups roubles with thin spaces', () => {
    expect(formatRoubles(0)).toBe('0 ₽');
    expect(formatRoubles(1234567)).toBe('1 234 567 ₽');
    expect(formatRoubles(-950)).toBe('−950 ₽');
  });

  it('humanises cache age', () => {
    expect(formatAge(30)).toBe('just now');
    expect(formatAge(600)).toBe('10 min ago');
    expect(formatAge(7200)).toBe('2 h ago');
    expect(formatAge(3 * 86400)).toBe('3 d ago');
  });

  it('builds a one-line answer per verdict', () => {
    expect(verdictHeadline(sellToTrader)).toBe('Mechanic pays 9 450 ₽');

    const keepForOneTask: Recommendation = {
      ...sellToTrader,
      verdict: 'keep',
      keepReasons: [{ kind: 'task', label: 'Shortage', count: 1 }],
    };
    expect(verdictHeadline(keepForOneTask)).toBe('Needed for Shortage');

    const keepForTwoReasons: Recommendation = {
      ...sellToTrader,
      verdict: 'keep',
      keepReasons: [
        { kind: 'task', label: 'Shortage', count: 1 },
        { kind: 'hideout', label: 'Medstation L1', count: 2 },
      ],
    };
    expect(verdictHeadline(keepForTwoReasons)).toBe('Needed in 2 places');
  });

  it('explains why the flea is not an option', () => {
    expect(fleaUnavailableNote(sellToTrader)).toBe('Flea market unlocks at level 15');
    expect(fleaUnavailableNote({ ...sellToTrader, flea: { ...sellToTrader.flea!, available: true } })).toBeNull();
  });
});
