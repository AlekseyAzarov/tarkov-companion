package com.tarkovcompanion.sdk.internal.session

import com.tarkovcompanion.sdk.CompanionConfig
import com.tarkovcompanion.sdk.PlayerProfile
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import org.json.JSONObject

/** Builds the session's settings documents; key names are generated from contracts/config-limits.json. */
internal object CoreSettings {
    // Must match the intent filter in the SDK's manifest.
    const val SIGN_IN_REDIRECT_HOST = "tarkov-companion-auth"

    // Without a usable redirect URI sign-in is left out, which turns it off in
    // the core: the page then never offers it.
    fun sessionConfigJson(
        config: CompanionConfig,
        userAgent: String,
        signInRedirectUriOrNullIfUnusable: String?,
        debuggableHostBuild: Boolean,
    ): String {
        val usableSignIn = config.signIn?.takeIf { signInRedirectUriOrNullIfUnusable != null }

        return JSONObject()
            .put(GeneratedContract.KEY_ENDPOINT, config.graphqlEndpoint)
            .put(GeneratedContract.KEY_LANGUAGE, config.language)
            .put(GeneratedContract.KEY_USER_AGENT, userAgent)
            .put(GeneratedContract.KEY_ITEM_CACHE_CAPACITY, config.itemCacheCapacity)
            .put(GeneratedContract.KEY_PRICE_TIME_TO_LIVE_SECONDS, config.priceTimeToLiveSeconds)
            .put(GeneratedContract.KEY_REMOTE_FLAGS_URL, config.remoteFlagsUrl ?: JSONObject.NULL)
            .put(GeneratedContract.KEY_SIGN_IN_ISSUER, usableSignIn?.issuer ?: JSONObject.NULL)
            .put(GeneratedContract.KEY_SIGN_IN_CLIENT_ID, usableSignIn?.clientId ?: JSONObject.NULL)
            .put(GeneratedContract.KEY_SIGN_IN_REDIRECT_URI, signInRedirectUriOrNullIfUnusable ?: JSONObject.NULL)
            .put(GeneratedContract.KEY_DEVELOPMENT_MODE, debuggableHostBuild)
            .put(GeneratedContract.KEY_FAVORITES_SYNC_URL, config.favoritesSyncUrl ?: JSONObject.NULL)
            .put(GeneratedContract.KEY_CATALOG_URL, config.catalogUrl ?: JSONObject.NULL)
            .toString()
    }

    fun signInRedirectUri(scheme: String): String = "$scheme://$SIGN_IN_REDIRECT_HOST"

    // The host's own scheme, else the applicationId the manifest registers, if it can be a scheme.
    fun signInRedirectSchemeOrNull(config: CompanionConfig, applicationId: String): String? {
        val signIn = config.signIn ?: return null

        return signIn.redirectScheme ?: applicationId.takeIf(::isUsableRedirectScheme)
    }

    // RFC 3986 scheme in lowercase: Android matches intent-filter schemes
    // case-sensitively, while browsers lowercase the scheme they redirect to.
    fun isUsableRedirectScheme(scheme: String): Boolean = USABLE_REDIRECT_SCHEME.matches(scheme)

    private val USABLE_REDIRECT_SCHEME = Regex("[a-z][a-z0-9+.-]*")

    fun profileJson(profile: PlayerProfile): String = JSONObject()
        .put(GeneratedContract.KEY_PLAYER_LEVEL, profile.level)
        .put(GeneratedContract.KEY_FLEA_UNLOCKED, profile.fleaUnlocked)
        .put(GeneratedContract.KEY_INTELLIGENCE_CENTER_LEVEL, profile.intelligenceCenterLevel)
        .put(GeneratedContract.KEY_GAME_MODE, profile.gameMode.apiValue)
        .put(GeneratedContract.KEY_IGNORE_KEEP_REASONS, profile.ignoreKeepReasons)
        .put(GeneratedContract.KEY_FLEA_MARGIN_PERCENT, profile.fleaMarginPercent)
        .toString()

    fun bridgeOptionsJson(stateRestoredAfterProcessDeath: UiState, millisecondsSinceScreenOpened: Long): String {
        val state = JSONObject().put(GeneratedContract.BRIDGE_FIELD_ROUTE, stateRestoredAfterProcessDeath.route)
        stateRestoredAfterProcessDeath.query?.let { query -> state.put(GeneratedContract.BRIDGE_FIELD_QUERY, query) }
        stateRestoredAfterProcessDeath.itemId?.let { itemId ->
            state.put(GeneratedContract.BRIDGE_FIELD_ITEM_ID, itemId)
        }

        return JSONObject()
            .put(GeneratedContract.KEY_INITIAL_STATE, state)
            .put(GeneratedContract.KEY_OPENED_MILLISECONDS_AGO, millisecondsSinceScreenOpened.coerceAtLeast(0))
            .toString()
    }
}
