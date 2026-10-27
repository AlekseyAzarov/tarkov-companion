package com.tarkovcompanion.sdk.ui

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import com.tarkovcompanion.sdk.internal.CompanionRuntime

/**
 * Holds one sign-in while the system browser shows the identity server's page
 * (RFC 8252: never the SDK's WebView). No UI of its own; AppAuth's pattern.
 */
internal class SignInActivity : Activity() {
    private var browserOpened = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        browserOpened = savedInstanceState?.getBoolean(KEY_BROWSER_OPENED) ?: false
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        // The redirect, picked up in onResume.
        setIntent(intent)
    }

    override fun onResume() {
        super.onResume()

        val runtime = CompanionRuntime.get()
        if (runtime == null) {
            // Process restored into this screen without the SDK: nothing to resume.
            finish()
            return
        }

        val signInPageUrl = intent.getStringExtra(EXTRA_SIGN_IN_PAGE_URL)
        if (!browserOpened && signInPageUrl != null) {
            browserOpened = true
            openBrowser(runtime, signInPageUrl)
            return
        }

        // Back from the browser: with the redirect, or without it if the
        // player closed the page. Either way the core decides what it means.
        runtime.session.signInRedirect(intent.data?.toString())
        finish()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putBoolean(KEY_BROWSER_OPENED, browserOpened)
    }

    private fun openBrowser(runtime: CompanionRuntime, signInPageUrl: String) {
        val viewIntent = Intent(Intent.ACTION_VIEW, Uri.parse(signInPageUrl))
            .addCategory(Intent.CATEGORY_BROWSABLE)
            // Asks for a Custom Tab (the page slides over the game instead of
            // switching apps) without the androidx.browser dependency: browsers
            // that support it look for this extra; others open normally.
            .putExtras(Bundle().apply { putBinder(EXTRA_CUSTOM_TABS_SESSION, null) })

        try {
            startActivity(viewIntent)
        } catch (failure: ActivityNotFoundException) {
            runtime.logger.error("no browser to sign in with", failure)
            runtime.session.signInRedirect(null)
            finish()
        }
    }

    companion object {
        private const val EXTRA_SIGN_IN_PAGE_URL = "com.tarkovcompanion.sdk.SIGN_IN_URL"
        private const val KEY_BROWSER_OPENED = "tarkov_companion.browserOpened"
        private const val EXTRA_CUSTOM_TABS_SESSION = "android.support.customtabs.extra.SESSION"

        fun intent(context: Context, signInPageUrl: String): Intent =
            Intent(context, SignInActivity::class.java).putExtra(EXTRA_SIGN_IN_PAGE_URL, signInPageUrl)
    }
}

/** Receives the browser's redirect and hands it to the waiting [SignInActivity]. */
internal class RedirectActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        startActivity(
            Intent(this, SignInActivity::class.java)
                .setData(intent.data)
                .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP),
        )
        finish()
    }
}
