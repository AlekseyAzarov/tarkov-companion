import type { Recommendation, Verdict } from '../bridge/protocol';
import { PayloadFleaUnavailableReason, PayloadVerdict } from '../generated/contract';

const SECONDS_PER_MINUTE = 60;
const SECONDS_PER_HOUR = 60 * SECONDS_PER_MINUTE;
const SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;

// Switch units at 1.5× so we never say "1 min ago" for 89 seconds.
const JUST_NOW_LIMIT_SECONDS = 1.5 * SECONDS_PER_MINUTE;
const MINUTES_LIMIT_SECONDS = 1.5 * SECONDS_PER_HOUR;
const HOURS_LIMIT_SECONDS = 1.5 * SECONDS_PER_DAY;

const THIN_SPACE = ' ';
const MINUS_SIGN = '−';

// 1234567 -> "1 234 567 ₽". Hand-rolled: Intl is inconsistent across old WebViews.
export function formatRoubles(value: number): string {
  const sign = value < 0 ? MINUS_SIGN : '';
  const digits = Math.abs(Math.round(value)).toString();
  const grouped = digits.replace(/\B(?=(\d{3})+(?!\d))/g, THIN_SPACE);

  return `${sign}${grouped} ₽`;
}

export function formatAge(seconds: number): string {
  if (seconds < JUST_NOW_LIMIT_SECONDS) {
    return 'just now';
  }

  if (seconds < MINUTES_LIMIT_SECONDS) {
    return `${Math.round(seconds / SECONDS_PER_MINUTE)} min ago`;
  }

  if (seconds < HOURS_LIMIT_SECONDS) {
    return `${Math.round(seconds / SECONDS_PER_HOUR)} h ago`;
  }

  return `${Math.round(seconds / SECONDS_PER_DAY)} d ago`;
}

export const VERDICT_TITLES: Record<Verdict, string> = {
  [PayloadVerdict.Keep]: 'Keep it',
  [PayloadVerdict.SellFlea]: 'Sell on the Flea',
  [PayloadVerdict.SellTrader]: 'Sell to a trader',
  [PayloadVerdict.NoBuyer]: 'Nobody buys this',
};

export function verdictHeadline(recommendation: Recommendation): string {
  switch (recommendation.verdict) {
    case PayloadVerdict.Keep: {
      const reasons = recommendation.keepReasons;

      return reasons.length === 1 ? `Needed for ${reasons[0]!.label}` : `Needed in ${reasons.length} places`;
    }
    case PayloadVerdict.SellFlea:
      return `${formatRoubles(recommendation.bestNetRubles)} after fees`;
    case PayloadVerdict.SellTrader:
      return `${recommendation.bestTrader?.vendorName ?? 'Trader'} pays ${formatRoubles(recommendation.bestNetRubles)}`;
    case PayloadVerdict.NoBuyer:
      return 'No trader buys it and it cannot be listed';
  }
}

const FLEA_UNAVAILABLE_REASONS: Record<string, string> = {
  [PayloadFleaUnavailableReason.FleaLocked]: 'Flea market unlocks at level 15',
  [PayloadFleaUnavailableReason.NoFlea]: 'Banned from the flea market',
  [PayloadFleaUnavailableReason.NoPrice]: 'No recent flea price',
};

export function fleaUnavailableNote(recommendation: Recommendation): string | null {
  const flea = recommendation.flea;
  if (!flea || flea.available) {
    return null;
  }

  return FLEA_UNAVAILABLE_REASONS[flea.unavailableReason ?? ''] ?? 'Flea market unavailable';
}
