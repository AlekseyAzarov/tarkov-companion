// Web side of the bridge protocol (docs/bridge-protocol.md). Everything from
// native is validated: the bundle and the SDK are versioned independently.

import {
  BridgeRequest,
  BridgeRoute,
  PayloadKeepReasonKind,
  PayloadVerdict,
  type BridgeRouteName,
  type PayloadKeepReasonKindName,
  type PayloadVerdictName,
} from '../generated/contract';

export const PROTOCOL_VERSION = 1;
export const OLDEST_SUPPORTED_NATIVE_PROTOCOL = 1;
export const PORT_HANDSHAKE = 'tarkov-companion-bridge-port';

export type Route = BridgeRouteName;

export interface InitialState {
  route: Route;
  query?: string;
  itemId?: string;
}

// All the page ever learns about the player's sign-in: tokens stay in native.
export interface AccountView {
  signedIn: boolean;
  displayName?: string;
}

export interface HelloResult {
  protocol: number;
  sdkVersion: string;
  features: string[];
  iconProxy?: string;
  initialState: InitialState;
  // Present when the host configured sign-in (features include "sign_in").
  account?: AccountView;
}

export interface ItemSummary {
  id: string;
  name: string;
  shortName: string;
  iconLink: string;
  slots: number;
  averagePriceLast24Hours: number | null;
}

export interface SearchResult {
  items: ItemSummary[];
  fromCache: boolean;
  cacheAgeSeconds: number;
}

export type Verdict = PayloadVerdictName;

export interface Recommendation {
  itemId: string;
  itemName: string;
  verdict: Verdict;
  bestNetRubles: number;
  bestNetPerSlotRubles: number;
  pricesAreStale: boolean;
  bestTrader: { vendorId: string; vendorName: string; priceRubles: number } | null;
  flea: {
    available: boolean;
    unavailableReason?: string;
    listPriceRubles: number;
    feeRubles: number;
    netRubles: number;
  } | null;
  keepReasons: { kind: PayloadKeepReasonKindName; label: string; count: number }[];
}

// Names come from native's catalog, never from the page.
export interface FavoriteView {
  id: string;
  name: string;
  shortName: string;
  iconLink: string;
}

export interface RecommendResult {
  recommendation: Recommendation;
  fromCache: boolean;
  hideoutMissing: boolean;
}

export interface Requests {
  [BridgeRequest.Hello]: { protocol: number; minimumNativeProtocol: number; bundleVersion: string };
  [BridgeRequest.Search]: { query: string };
  [BridgeRequest.Recommend]: { itemId: string };
  [BridgeRequest.Track]: { event: string; properties?: Record<string, string | number | boolean> };
  [BridgeRequest.State]: { route: Route; query?: string; itemId?: string };
  [BridgeRequest.Close]: Record<string, never>;
  [BridgeRequest.SignIn]: Record<string, never>;
  [BridgeRequest.SignOut]: Record<string, never>;
  [BridgeRequest.FavoritesList]: Record<string, never>;
  [BridgeRequest.FavoriteSet]: { itemId: string; favorite: boolean };
}

export interface Responses {
  [BridgeRequest.Hello]: HelloResult;
  [BridgeRequest.Search]: SearchResult;
  [BridgeRequest.Recommend]: RecommendResult;
  [BridgeRequest.Track]: Record<string, never>;
  [BridgeRequest.State]: Record<string, never>;
  [BridgeRequest.Close]: never;
  // Only "started": the outcome arrives as an account_changed event.
  [BridgeRequest.SignIn]: Record<string, never>;
  [BridgeRequest.SignOut]: Record<string, never>;
  [BridgeRequest.FavoritesList]: { items: FavoriteView[] };
  [BridgeRequest.FavoriteSet]: Record<string, never>;
}

export type RequestType = keyof Requests;

export type IncomingMessage =
  | { kind: 'result'; replyTo: string; payload: unknown }
  | { kind: 'error'; replyTo: string | null; code: string; message: string }
  | { kind: 'event'; event: string; payload: unknown };

// --- validation -----------------------------------------------------------

type JsonObject = Record<string, unknown>;

// Native never sends anything close to this; a bigger message is not ours.
const MAXIMUM_INCOMING_MESSAGE_LENGTH = 1_000_000;
// Every protocol version so far shares the v1 envelope.
const OLDEST_ENVELOPE_VERSION = 1;
const VERDICTS: readonly string[] = Object.values(PayloadVerdict);
const KEEP_REASON_KINDS: readonly unknown[] = Object.values(PayloadKeepReasonKind);
const ROUTES: readonly unknown[] = Object.values(BridgeRoute);

const isObject = (value: unknown): value is JsonObject =>
  typeof value === 'object' && value !== null && !Array.isArray(value);
const isString = (value: unknown): value is string => typeof value === 'string';
const isNumber = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);
const isBoolean = (value: unknown): value is boolean => typeof value === 'boolean';
const isRoute = (value: unknown): value is Route => ROUTES.includes(value);

export function parseIncomingMessage(data: unknown): IncomingMessage | null {
  if (!isString(data) || data.length > MAXIMUM_INCOMING_MESSAGE_LENGTH) {
    return null;
  }

  let message: unknown;
  try {
    message = JSON.parse(data);
  } catch {
    return null;
  }

  if (!isObject(message) || !isNumber(message.version) || message.version < OLDEST_ENVELOPE_VERSION) {
    return null;
  }

  if (isString(message.event)) {
    return { kind: 'event', event: message.event, payload: message.payload };
  }

  if (message.ok === true && isString(message.replyTo)) {
    return { kind: 'result', replyTo: message.replyTo, payload: message.payload };
  }

  if (message.ok === false && isObject(message.error) && isString(message.error.code)) {
    return {
      kind: 'error',
      replyTo: isString(message.replyTo) ? message.replyTo : null,
      code: message.error.code,
      message: isString(message.error.message) ? message.error.message : '',
    };
  }

  return null;
}

function isItemSummary(value: unknown): value is ItemSummary {
  return (
    isObject(value) &&
    isString(value.id) &&
    isString(value.name) &&
    isString(value.shortName) &&
    isString(value.iconLink) &&
    isNumber(value.slots) &&
    (value.averagePriceLast24Hours === null || isNumber(value.averagePriceLast24Hours))
  );
}

function isKeepReason(value: unknown): boolean {
  return (
    isObject(value) &&
    KEEP_REASON_KINDS.includes(value.kind) &&
    isString(value.label) &&
    isNumber(value.count)
  );
}

function isTraderOfferOrNull(value: unknown): boolean {
  return value === null || (isObject(value) && isString(value.vendorName) && isNumber(value.priceRubles));
}

function isFleaOfferOrNull(value: unknown): boolean {
  return (
    value === null ||
    (isObject(value) &&
      isBoolean(value.available) &&
      isNumber(value.listPriceRubles) &&
      isNumber(value.feeRubles) &&
      isNumber(value.netRubles))
  );
}

function isRecommendation(value: unknown): value is Recommendation {
  if (!isObject(value)) {
    return false;
  }

  return (
    isString(value.itemId) &&
    isString(value.itemName) &&
    isString(value.verdict) &&
    VERDICTS.includes(value.verdict) &&
    isNumber(value.bestNetRubles) &&
    isNumber(value.bestNetPerSlotRubles) &&
    isBoolean(value.pricesAreStale) &&
    isTraderOfferOrNull(value.bestTrader) &&
    isFleaOfferOrNull(value.flea) &&
    Array.isArray(value.keepReasons) &&
    value.keepReasons.every(isKeepReason)
  );
}

const isEmptyPayload = (value: unknown): value is Record<string, never> => isObject(value);

export const isAccountView = (value: unknown): value is AccountView =>
  isObject(value) && isBoolean(value.signedIn) && (value.displayName === undefined || isString(value.displayName));

const isFavoriteView = (value: unknown): value is FavoriteView =>
  isObject(value) &&
  isString(value.id) &&
  isString(value.name) &&
  isString(value.shortName) &&
  isString(value.iconLink);

const isFavoriteList = (value: unknown): value is FavoriteView[] =>
  Array.isArray(value) && value.every(isFavoriteView);

export function parseFavoritesChangedEvent(payload: unknown): FavoriteView[] | null {
  return isObject(payload) && isFavoriteList(payload.items) ? payload.items : null;
}

export function parseAccountChangedEvent(payload: unknown): AccountView | null {
  return isObject(payload) && isAccountView(payload.account) ? payload.account : null;
}

export const responseValidators: {
  [Type in RequestType]: (payload: unknown) => payload is Responses[Type];
} = {
  [BridgeRequest.Hello]: (payload): payload is HelloResult =>
    isObject(payload) &&
    isNumber(payload.protocol) &&
    isString(payload.sdkVersion) &&
    Array.isArray(payload.features) &&
    payload.features.every(isString) &&
    (payload.iconProxy === undefined || isString(payload.iconProxy)) &&
    isObject(payload.initialState) &&
    isRoute(payload.initialState.route) &&
    (payload.account === undefined || isAccountView(payload.account)),
  [BridgeRequest.Search]: (payload): payload is SearchResult =>
    isObject(payload) &&
    Array.isArray(payload.items) &&
    payload.items.every(isItemSummary) &&
    isBoolean(payload.fromCache) &&
    isNumber(payload.cacheAgeSeconds),
  [BridgeRequest.Recommend]: (payload): payload is RecommendResult =>
    isObject(payload) &&
    isRecommendation(payload.recommendation) &&
    isBoolean(payload.fromCache) &&
    isBoolean(payload.hideoutMissing),
  [BridgeRequest.Track]: isEmptyPayload,
  [BridgeRequest.State]: isEmptyPayload,
  // `close` never gets an answer: the screen goes away.
  [BridgeRequest.Close]: (_payload): _payload is never => false,
  [BridgeRequest.SignIn]: isEmptyPayload,
  [BridgeRequest.SignOut]: isEmptyPayload,
  [BridgeRequest.FavoritesList]: (payload): payload is { items: FavoriteView[] } =>
    isObject(payload) && isFavoriteList(payload.items),
  [BridgeRequest.FavoriteSet]: isEmptyPayload,
};
