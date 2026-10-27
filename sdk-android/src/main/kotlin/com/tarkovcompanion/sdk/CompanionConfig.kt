package com.tarkovcompanion.sdk

import com.tarkovcompanion.sdk.host.AnalyticsSink
import com.tarkovcompanion.sdk.host.CompanionLogger
import com.tarkovcompanion.sdk.host.HttpTransport
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import java.util.concurrent.Executor

/** Immutable SDK configuration. A builder, not a data class, so new options stay binary-compatible. */
public class CompanionConfig private constructor(builder: Builder) {
    public val graphqlEndpoint: String = builder.graphqlEndpoint
    public val language: String = builder.language
    public val httpTransport: HttpTransport? = builder.httpTransport
    public val logger: CompanionLogger? = builder.logger
    public val analytics: AnalyticsSink? = builder.analytics
    public val callbackExecutor: Executor? = builder.callbackExecutor
    public val remoteFlagsUrl: String? = builder.remoteFlagsUrl
    public val signIn: SignInConfig? = builder.signIn
    public val favoritesSyncUrl: String? = builder.favoritesSyncUrl
    public val catalogUrl: String? = builder.catalogUrl
    public val itemCacheCapacity: Int = builder.itemCacheCapacity
    public val priceTimeToLiveSeconds: Int = builder.priceTimeToLiveSeconds
    public val webContentsDebuggingEnabled: Boolean? = builder.webContentsDebuggingEnabled

    public class Builder {
        internal var graphqlEndpoint: String = DEFAULT_GRAPHQL_ENDPOINT
        internal var language: String = GeneratedContract.LANGUAGE_DEFAULT
        internal var httpTransport: HttpTransport? = null
        internal var logger: CompanionLogger? = null
        internal var analytics: AnalyticsSink? = null
        internal var callbackExecutor: Executor? = null
        internal var remoteFlagsUrl: String? = null
        internal var signIn: SignInConfig? = null
        internal var favoritesSyncUrl: String? = null
        internal var catalogUrl: String? = null
        internal var itemCacheCapacity: Int = DEFAULT_ITEM_CACHE_CAPACITY
        internal var priceTimeToLiveSeconds: Int = DEFAULT_PRICE_TIME_TO_LIVE_SECONDS
        internal var webContentsDebuggingEnabled: Boolean? = null

        /** GraphQL endpoint. Point it at your own caching proxy in production. */
        public fun graphqlEndpoint(url: String): Builder = apply {
            require(url.startsWith(GeneratedContract.ENDPOINT_REQUIRED_PREFIX)) {
                "graphqlEndpoint must use https: $url"
            }
            graphqlEndpoint = url
        }

        /** tarkov.dev language code for item names, e.g. "en", "ru", "de". */
        public fun language(code: String): Builder = apply {
            require(code.matches(LANGUAGE_CODE)) {
                "language must be a ${GeneratedContract.LANGUAGE_LENGTH}-letter code: $code"
            }
            language = code
        }

        public fun httpTransport(transport: HttpTransport): Builder = apply { httpTransport = transport }

        public fun logger(logger: CompanionLogger): Builder = apply { this.logger = logger }

        public fun analytics(sink: AnalyticsSink): Builder = apply { analytics = sink }

        /** Where callbacks are delivered. Defaults to the main thread. */
        public fun callbackExecutor(executor: Executor): Builder = apply { callbackExecutor = executor }

        /** Optional JSON document with remote feature flags / kill switch. See docs/remote-flags.md. */
        public fun remoteFlagsUrl(url: String): Builder = apply {
            require(url.startsWith(GeneratedContract.REMOTE_FLAGS_URL_REQUIRED_PREFIX)) {
                "remoteFlagsUrl must use https: $url"
            }
            remoteFlagsUrl = url
        }

        /** Enables sign-in ([TarkovCompanion.signIn]) against your identity server. See docs/auth.md. */
        public fun signIn(config: SignInConfig): Builder = apply { signIn = config }

        /**
         * Favourites sync endpoint (docs/favorites.md); needs [signIn]. https, or
         * http to this machine in debuggable builds.
         */
        public fun favoritesSyncUrl(url: String): Builder = apply {
            val allowed =
                url.startsWith(GeneratedContract.FAVORITES_SYNC_URL_REQUIRED_PREFIX) || DEVELOPMENT_HTTP.matches(url)
            require(allowed) {
                "favoritesSyncUrl must use https: $url"
            }
            favoritesSyncUrl = url
        }

        /**
         * Base URL of the static catalog read when tarkov.dev's GraphQL API is
         * down (docs/catalog.md). https, or http to this machine in debuggable builds.
         */
        public fun catalogUrl(url: String): Builder = apply {
            require(url.startsWith(GeneratedContract.CATALOG_URL_REQUIRED_PREFIX) || DEVELOPMENT_HTTP.matches(url)) {
                "catalogUrl must use https: $url"
            }
            catalogUrl = url
        }

        public fun itemCacheCapacity(capacity: Int): Builder = apply {
            require(capacity in ITEM_CACHE_CAPACITY_RANGE) { "itemCacheCapacity out of range: $capacity" }
            itemCacheCapacity = capacity
        }

        /** After this, cached prices are still shown but flagged as stale. */
        public fun priceTimeToLiveSeconds(seconds: Int): Builder = apply {
            require(seconds >= GeneratedContract.PRICE_TIME_TO_LIVE_SECONDS_MINIMUM) { "priceTimeToLiveSeconds must be >= 0" }
            priceTimeToLiveSeconds = seconds
        }

        /** Force WebView remote debugging on/off. By default it follows the host's debuggable flag. */
        public fun webContentsDebuggingEnabled(enabled: Boolean): Builder = apply {
            webContentsDebuggingEnabled = enabled
        }

        public fun build(): CompanionConfig = CompanionConfig(this)
    }

    // From contracts/config-limits.json; the core checks again.
    public companion object {
        public const val DEFAULT_GRAPHQL_ENDPOINT: String = GeneratedContract.ENDPOINT_DEFAULT

        private const val DEFAULT_ITEM_CACHE_CAPACITY = GeneratedContract.ITEM_CACHE_CAPACITY_DEFAULT
        private val ITEM_CACHE_CAPACITY_RANGE =
            GeneratedContract.ITEM_CACHE_CAPACITY_MINIMUM..GeneratedContract.ITEM_CACHE_CAPACITY_MAXIMUM

        private const val DEFAULT_PRICE_TIME_TO_LIVE_SECONDS = GeneratedContract.PRICE_TIME_TO_LIVE_SECONDS_DEFAULT

        // The core accepts it only in debuggable apps.
        private val DEVELOPMENT_HTTP = Regex("""http://(127\.0\.0\.1|localhost|10\.0\.2\.2)([:/].*)?""")

        // ISO 639-1, as tarkov.dev's `lang` takes.
        private val LANGUAGE_CODE = Regex("[a-z]{${GeneratedContract.LANGUAGE_LENGTH}}")
    }
}
