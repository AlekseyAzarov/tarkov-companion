package com.tarkovcompanion.sdk

import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import com.tarkovcompanion.sdk.internal.session.CoreSettings
import com.tarkovcompanion.sdk.model.FavoriteItem

/** The signed-in player, as the identity server knows them. */
public class Account internal constructor(
    /** Stable id from the identity server. */
    public val id: String,
    public val displayName: String,
) {
    override fun equals(other: Any?): Boolean = other is Account && other.id == id && other.displayName == displayName

    override fun hashCode(): Int = id.hashCode() * HASH_MULTIPLIER + displayName.hashCode()

    override fun toString(): String = "Account($id, $displayName)"

    private companion object {
        const val HASH_MULTIPLIER = 31
    }
}

/** Called on the main thread with the whole list whenever it changes, here or (when signed in) on another device. */
public fun interface FavoritesListener {
    public fun onFavoritesChanged(favorites: List<FavoriteItem>)
}

/** Called on the main thread when the player signs in or out, or the session expires. */
public fun interface AccountListener {
    public fun onAccountChanged(account: Account?)
}

/**
 * OAuth 2.0 with PKCE in the system browser (docs/auth.md). Endpoints: `issuer`
 * + `/authorize`, `/token`, `/revoke`, `/me`. Register
 * `<scheme>://tarkov-companion-auth` for [clientId]; the scheme is the
 * applicationId unless [Builder.redirectScheme] is set.
 */
public class SignInConfig private constructor(builder: Builder) {
    public val issuer: String = builder.issuer ?: throw IllegalArgumentException("issuer is required")
    public val clientId: String = builder.clientId ?: throw IllegalArgumentException("clientId is required")
    public val redirectScheme: String? = builder.redirectScheme

    public class Builder {
        internal var issuer: String? = null
        internal var clientId: String? = null
        internal var redirectScheme: String? = null

        /** https, or http to 127.0.0.1, localhost or 10.0.2.2 in debuggable builds. */
        public fun issuer(url: String): Builder = apply {
            val allowed =
                url.startsWith(GeneratedContract.SIGN_IN_ISSUER_REQUIRED_PREFIX) || isDevelopmentServerOnThisMachine(url)
            require(allowed) {
                "issuer must use https: $url"
            }
            issuer = url
        }

        public fun clientId(id: String): Builder = apply {
            require(id.isNotBlank()) { "clientId must not be blank" }
            clientId = id
        }

        /**
         * For an applicationId that can't be a URI scheme (uppercase letters,
         * `_`). Declare the scheme for the SDK's redirect activity (docs/auth.md).
         */
        public fun redirectScheme(scheme: String): Builder = apply {
            require(CoreSettings.isUsableRedirectScheme(scheme)) {
                "redirectScheme must be lowercase letters, digits, '+', '-' or '.', starting with a letter: $scheme"
            }
            redirectScheme = scheme
        }

        public fun build(): SignInConfig = SignInConfig(this)
    }

    private companion object {
        val DEVELOPMENT_SERVER_ON_THIS_MACHINE = Regex("""http://(127\.0\.0\.1|localhost|10\.0\.2\.2)([:/].*)?""")

        fun isDevelopmentServerOnThisMachine(url: String): Boolean = DEVELOPMENT_SERVER_ON_THIS_MACHINE.matches(url)
    }
}
