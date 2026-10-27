package com.tarkovcompanion.hostgame

import android.content.Context
import com.tarkovcompanion.sdk.host.HttpTransport

/** Release builds use the SDK's default transport and the published catalog; no remote flags, no sign-in. */
object DemoSupport {
    fun remoteFlagsUrl(): String? = null

    fun signInIssuer(): String? = null

    fun favoritesSyncUrl(): String? = null

    fun catalogUrl(): String? = "https://alekseyazarov.github.io/tarkov-companion/catalog"

    const val SIGN_IN_CLIENT_ID = "tarkov-companion-demo"

    fun transport(context: Context): HttpTransport? = null
}
