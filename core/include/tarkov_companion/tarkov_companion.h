/*
 * Tarkov Companion core: the flat C API, the only surface of the core library.
 * Platform SDK layers call it; host apps use the platform SDK instead.
 *
 * A session is a sans-IO state machine (docs/adr/0006-sans-io-session.md):
 *
 *   platform ──commands──►  session  ──actions──►  platform
 *   (search, recommend,               (do this HTTP request, read/write this
 *    cancel, set profile)              cache key, deliver this result, log)
 *            ◄──completions── (HTTP response, cache value)
 *
 * The platform runs each action with the host's HTTP stack, storage and threads.
 *
 * Rules:
 *   - Settings cross the boundary as JSON objects. Keys, limits and defaults
 *     are listed in contracts/config-limits.json. Unknown keys are ignored
 *     and reported with a "warn" log action.
 *   - Strings are UTF-8. Strings returned via char** are owned by the caller
 *     and released with tarkov_companion_free().
 *   - Every function is thread-safe for a given session. Nothing here calls
 *     back into the platform, so there is no re-entrancy across the boundary.
 *   - No C++ exception crosses this boundary. Built with exceptions, a call
 *     that fails inside (out of memory) returns TARKOV_COMPANION_ERROR_INTERNAL
 *     and may have been partly applied. Built without them (Android), running
 *     out of memory aborts the process.
 */
#ifndef TARKOV_COMPANION_H
#define TARKOV_COMPANION_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define TARKOV_COMPANION_API
#else
#  define TARKOV_COMPANION_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped on incompatible changes to this header. Must equal "nativeAbiVersion"
 * in contracts/config-limits.json, which the platform SDKs check it against. */
#define TARKOV_COMPANION_ABI_VERSION 2u

typedef enum tarkov_companion_status {
  TARKOV_COMPANION_OK = 0,
  /* bad pointer, bad settings, unknown action/bridge id, request id in use */
  TARKOV_COMPANION_ERROR_INVALID_ARGUMENT = 1,
  /* tarkov_companion_session_next_action: nothing to do right now */
  TARKOV_COMPANION_NO_ACTION = 2,
  /* out of memory, or another failure inside the core; see the rules above */
  TARKOV_COMPANION_ERROR_INTERNAL = 3
} tarkov_companion_status;

TARKOV_COMPANION_API const char* tarkov_companion_version(void);
TARKOV_COMPANION_API uint32_t tarkov_companion_abi_version(void);
TARKOV_COMPANION_API const char* tarkov_companion_status_string(tarkov_companion_status status);
TARKOV_COMPANION_API void tarkov_companion_free(char* text);

typedef struct tarkov_companion_session tarkov_companion_session;

/* Caller-chosen id of a search/recommend command; echoed in its "result" action.
 * Ids with the top bit set are reserved for the session's own use. */
typedef uint64_t tarkov_companion_request_id;
/* Session-chosen id of an action that expects a completion. */
typedef uint64_t tarkov_companion_action_id;
/* Session-chosen id of a bridge to a web UI. */
typedef uint64_t tarkov_companion_bridge_id;

/*
 * Creates a session. `configuration_json` may be NULL for defaults:
 *
 *   { "endpoint": "https://api.tarkov.dev/graphql", "language": "en",
 *     "userAgent": "TarkovCompanionSDK/0.1.0 (Android 36)",
 *     "itemCacheCapacity": 512, "priceTimeToLiveSeconds": 900,
 *     "remoteFlagsUrl": "https://config.example.com/companion/flags.json",
 *     "signInIssuer": "https://id.example.com", "signInClientId": "game-123",
 *     "signInRedirectUri": "com.example.game://tarkov-companion-auth", "developmentMode": false,
 *     "favoritesSyncUrl": "https://api.example.com/favorites/sync",
 *     "catalogUrl": "https://owner.github.io/repository/catalog" }
 *
 * With "remoteFlagsUrl" the session starts by loading the remote flags
 * (docs/remote-flags.md): the cached document first, then the network.
 * With "signInIssuer" it starts by restoring the last sign-in from the secret
 * store (docs/auth.md). "developmentMode", for debug builds only, also
 * allows http URLs to this machine (127.0.0.1, localhost, 10.0.2.2).
 * With "catalogUrl", reads the GraphQL endpoint fails are retried there
 * with plain GETs (docs/catalog.md), before falling back to the cache.
 *
 * On invalid settings returns TARKOV_COMPANION_ERROR_INVALID_ARGUMENT and, if `out_error` is
 * not NULL, a message saying which key is wrong.
 */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_create(const char* configuration_json, size_t configuration_length,
                                tarkov_companion_session** out_session, char** out_error);
TARKOV_COMPANION_API void tarkov_companion_session_destroy(tarkov_companion_session* session);

/*
 * Player context for subsequent commands. "fleaUnlocked" defaults to
 * playerLevel >= 15; "gameMode" also selects the data set and cache entries.
 *
 *   { "playerLevel": 22, "fleaUnlocked": true, "intelligenceCenterLevel": 1,
 *     "gameMode": "regular" | "pve", "ignoreKeepReasons": false,
 *     "fleaMarginPercent": 5 }
 */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_set_profile(tarkov_companion_session* session, const char* profile_json, size_t profile_length,
                                     char** out_error);

/*
 * Commands. Each produces exactly one "result" action for `request_id`, unless
 * cancelled; invalid input is reported in that result, not here. Returns
 * TARKOV_COMPANION_ERROR_INVALID_ARGUMENT only for a NULL argument, a request id in use or a reserved one.
 */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_search(tarkov_companion_session* session, tarkov_companion_request_id request_id,
                                const char* query, size_t query_length);
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_recommend(tarkov_companion_session* session, tarkov_companion_request_id request_id,
                                   const char* item_id, size_t item_id_length);

/*
 * No "result" is produced for `request_id` after this returns, including one
 * already queued. Network work already started still completes and still
 * fills the offline cache.
 */
TARKOV_COMPANION_API void
tarkov_companion_session_cancel(tarkov_companion_session* session, tarkov_companion_request_id request_id);

/*
 * Bridges to the web UI (docs/bridge-protocol.md). The session runs the
 * protocol: it reads every message the web flow sends, answers it, and runs
 * the searches and recommendations it asks for. The platform only carries
 * messages between the session and the page, unread. Open one bridge per
 * page load:
 *
 *   { "initialState": { "route": "search" | "item", "query": "gpu", "itemId": "..." },
 *     "openedMillisecondsAgo": 120 }
 *
 * "initialState" is what the last "bridge_state" action reported, so the web
 * flow reopens where the player left it after process death. "openedMillisecondsAgo" is
 * how long ago the player asked for the screen, so time-to-first-verdict
 * covers the whole wait. The handshake applies the remote flags in force when
 * it happens.
 */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_open_bridge(tarkov_companion_session* session, const char* options_json, size_t options_length,
                                     tarkov_companion_bridge_id* out_bridge, char** out_error);

/* A message from the page, as received. Invalid messages are answered, not reported here. */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_bridge_receive(tarkov_companion_session* session, tarkov_companion_bridge_id bridge,
                                        const char* message, size_t message_length);

/* An event for the page, e.g. "navigate_back" when the system back button is pressed. */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_bridge_event(tarkov_companion_session* session, tarkov_companion_bridge_id bridge,
                                      const char* name, size_t name_length);

/*
 * The page is gone. Its requests are cancelled, and no action is produced for
 * this bridge afterwards, including one already queued.
 */
TARKOV_COMPANION_API void
tarkov_companion_session_close_bridge(tarkov_companion_session* session, tarkov_companion_bridge_id bridge);

/*
 * Sign-in: OAuth 2.0 authorization code with PKCE in the system browser
 * (docs/auth.md). `random_bytes` are at least 48 fresh bytes from a secure
 * random source; the core has no entropy of its own. The session answers
 * with an "open_browser" action, and the request's result is
 * {"account":{"id":"...","displayName":"..."}} or an auth error.
 */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_sign_in(tarkov_companion_session* session, tarkov_companion_request_id request_id,
                                 const uint8_t* random_bytes, size_t random_length);

/* The URI the browser returned to the app with. `uri_length` 0: the player came back without one (closed the page). */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_sign_in_redirect(tarkov_companion_session* session, const char* uri, size_t uri_length);

/* Signs out at once; revoking the token on the server is best effort. The result payload is {}. */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_sign_out(tarkov_companion_session* session, tarkov_companion_request_id request_id);

/*
 * Favourites (docs/favorites.md): this device's list when signed out, the
 * account's (synced through "favoritesSyncUrl") when signed in. Results:
 * {"items":[{"id","name","shortName","iconLink"}...]} and {}. A favourite
 * needs the item in the catalog (searched or looked at): its name comes from
 * there, not from the caller.
 */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_favorites_list(tarkov_companion_session* session, tarkov_companion_request_id request_id);
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_favorite_set(tarkov_companion_session* session, tarkov_companion_request_id request_id,
                                      const char* item_id, size_t item_id_length, int32_t favorite);

/*
 * Takes the next action, or returns TARKOV_COMPANION_NO_ACTION. The platform drains actions
 * after every command and every completion.
 *
 * `*out_action_json` describes the action. Bulky content travels separately
 * as raw bytes in `*out_payload` (NULL when the action has none), so a large
 * response is never escaped into a JSON string. Every type and field name
 * below is listed in contracts/session-actions.json, from which the core and
 * the platform SDKs generate their constants:
 *
 *   {"type":"http", "id":7, "method":"POST", "url":"...", "headers":{...}, "timeoutMilliseconds":10000}
 *       payload: request body          → answer with tarkov_companion_session_complete_http(7, ...)
 *   {"type":"cache_read", "id":8, "key":"search|regular|en|salewa"}
 *                                      → answer with tarkov_companion_session_complete_cache_read(8, ...)
 *   {"type":"cache_write", "key":"..."}
 *       payload: value to store        → fire and forget; the store is a dumb
 *                                        key-value store, the session stamps its own values
 *   {"type":"result", "request":3, "ok":true}
 *       payload: result document       → deliver to whoever issued request 3
 *   {"type":"result", "request":3, "ok":false, "error":{"code":"network", "message":"..."}}
 *   {"type":"log", "level":"debug"|"info"|"warn"|"error", "message":"..."}
 *   {"type":"track", "event":"verdict_shown", "properties":{"verdict":"keep", ...}}
 *                                      → analytics; property values are strings
 *   {"type":"bridge_send", "bridge":1}
 *       payload: protocol message      → post to that bridge's page as is
 *   {"type":"bridge_state", "bridge":1, "route":"item", "query":"gpu", "itemId":"..."}
 *                                      → keep for "initialState" after process death
 *   {"type":"bridge_close", "bridge":1}
 *                                      → the page asked to close the screen
 *   {"type":"secret_read", "id":9, "key":"auth"}
 *                                      → the secret store; answer with
 *                                        tarkov_companion_session_complete_cache_read(9, ...)
 *   {"type":"secret_write", "key":"auth"}
 *       payload: value                 → encrypt at rest, keep out of backups
 *   {"type":"secret_delete", "key":"auth"}
 *   {"type":"open_browser", "request":3, "url":"https://id.example.com/authorize?..."}
 *                                      → the system browser (never a WebView); pass where
 *                                        it returns to tarkov_companion_session_sign_in_redirect
 *   {"type":"account", "signedIn":true, "id":"...", "displayName":"..."}
 *                                      → the sign-in state changed
 *   {"type":"bridge_sign_in", "bridge":1}
 *                                      → the page asked to sign in: start one as for the host
 *   {"type":"data_read", "id":10, "key":"favorites|local"}
 *                                      → the data store; answer with tarkov_companion_session_complete_cache_read(10,
 * ...)
 *   {"type":"data_write", "key":"..."}
 *       payload: value                 → keep until deleted (not a cache: never evicted)
 *   {"type":"data_delete", "key":"..."}
 *   {"type":"favorites"}
 *       payload: [{"id","name","shortName","iconLink"}...]
 *                                      → the favourites changed (here or on another device)
 *   {"type":"flags", "launchAllowed":false}
 *                                      → remote flags changed; keep the kill switch's
 *                                        verdict for checks that must answer synchronously
 *
 * Analytics event names and properties are listed in contracts/analytics-events.json.
 *
 * Result documents are what the web UI's bridge protocol carries:
 *   search:    {"items":[...summaries], "fromCache":false, "cacheAgeSeconds":0}
 *   recommend: {"recommendation":{...}, "fromCache":false, "hideoutMissing":false}
 * Error codes: invalid_argument, network, api, schema_mismatch, not_found, internal,
 * sign_in_cancelled, sign_in_failed, not_signed_in.
 */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_next_action(tarkov_companion_session* session, char** out_action_json, char** out_payload,
                                     size_t* out_payload_length);

/* `status_code` 0 means the request never got an HTTP response; `body` then holds the reason. */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_complete_http(tarkov_companion_session* session, tarkov_companion_action_id action_id,
                                       int32_t status_code, const char* body, size_t body_length);

/* `found` 0: the key is not in the store, `value` is ignored. */
TARKOV_COMPANION_API tarkov_companion_status
tarkov_companion_session_complete_cache_read(tarkov_companion_session* session, tarkov_companion_action_id action_id,
                                             int32_t found, const char* value, size_t value_length);

#ifdef __cplusplus
}
#endif

#endif /* TARKOV_COMPANION_H */
