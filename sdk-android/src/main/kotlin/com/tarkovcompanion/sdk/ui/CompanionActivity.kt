package com.tarkovcompanion.sdk.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.ViewGroup
import android.view.WindowInsets
import android.webkit.ConsoleMessage
import android.webkit.WebChromeClient
import android.webkit.WebMessage
import android.webkit.WebMessagePort
import android.webkit.WebSettings
import android.webkit.WebView
import android.widget.FrameLayout
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import com.tarkovcompanion.sdk.Account
import com.tarkovcompanion.sdk.CompanionCallback
import com.tarkovcompanion.sdk.CompanionException
import com.tarkovcompanion.sdk.internal.CompanionRuntime
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import com.tarkovcompanion.sdk.internal.session.BridgeHost
import com.tarkovcompanion.sdk.internal.session.CoreSettings
import com.tarkovcompanion.sdk.internal.session.SessionDriver
import com.tarkovcompanion.sdk.internal.session.UiState

/**
 * Translucent overlay hosting the web flow (ADR 0012). It carries messages
 * between the page and the core and owns the WebView's lifecycle.
 * - configChanges: the WebView and its page survive rotation and resizes.
 * - Process death: the route, query and item return in the bridge handshake.
 * - screenOrientation="behind": a translucent activity with a fixed
 *   orientation rotates the game below it, and crashes on API 26.
 */
internal class CompanionActivity : Activity(), BridgeHost {
    private lateinit var runtime: CompanionRuntime
    private var webView: WebView? = null
    private var nativeMessagePort: WebMessagePort? = null
    private var bridgeConnection: SessionDriver.BridgeConnection? = null
    private var registeredBackCallback: Any? = null
    private var state = UiState(UiState.ROUTE_SEARCH, null, null)
    private var openedAtElapsedMilliseconds = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        openedAtElapsedMilliseconds = SystemClock.elapsedRealtime()

        val initializedRuntime = CompanionRuntime.get()
        if (initializedRuntime == null) {
            // Process was restored straight into this screen and the host
            // initializes the SDK lazily. See docs/troubleshooting.md.
            Log.w("TarkovCompanion", "CompanionActivity started before TarkovCompanion.initialize(); closing")
            finish()
            return
        }

        runtime = initializedRuntime
        state = savedInstanceState?.let(::restoreState)
            ?: UiState(UiState.ROUTE_SEARCH, intent.getStringExtra(EXTRA_INITIAL_QUERY), null)

        val createdWebView = try {
            WebView(this)
        } catch (failure: Exception) {
            // Missing, disabled or mid-update WebView provider.
            runtime.logger.error("WebView unavailable", failure)
            runtime.analytics.track(
                GeneratedContract.EVENT_COMPANION_LAUNCH_FAILED,
                GeneratedContract.PROPERTY_REASON to "webview_unavailable",
            )
            finish()
            return
        }
        configure(createdWebView)
        webView = createdWebView

        val container = FrameLayout(this)
        container.addView(
            createdWebView,
            FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT),
        )
        container.setOnApplyWindowInsetsListener { view, insets ->
            applyInsets(view as ViewGroup, insets)
            insets
        }
        setContentView(container)
        registerBackHandler()

        runtime.analytics.track(
            GeneratedContract.EVENT_COMPANION_OPENED,
            GeneratedContract.PROPERTY_RESTORED to (savedInstanceState != null),
        )
        createdWebView.loadUrl(CompanionWebViewClient.ENTRY_URL)
    }

    private fun configure(view: WebView) {
        view.setBackgroundColor(Color.TRANSPARENT)
        view.settings.apply {
            javaScriptEnabled = true
            allowFileAccess = false
            allowContentAccess = false
            domStorageEnabled = false
            mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
            setGeolocationEnabled(false)
            setSupportMultipleWindows(false)
            mediaPlaybackRequiresUserGesture = true
            userAgentString = "$userAgentString TarkovCompanionSDK/${runtime.sdkVersion}"
        }

        // Process-global switch: only ever turn it on, never off, or we'd break
        // debugging of the host's own WebViews.
        val debuggableHostBuild = applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        if (runtime.config.webContentsDebuggingEnabled ?: debuggableHostBuild) {
            WebView.setWebContentsDebuggingEnabled(true)
        }

        view.webViewClient = CompanionWebViewClient(
            assets = assets,
            fetchIcon = runtime::fetchWithHttpGet,
            logger = runtime.logger,
            onPageReady = ::connectBridge,
            onRendererGone = {
                runtime.analytics.track(GeneratedContract.EVENT_COMPANION_RENDERER_GONE)
                // Already dead: destroy() would throw.
                webView = null
                finish()
            },
        )
        view.webChromeClient = object : WebChromeClient() {
            override fun onConsoleMessage(message: ConsoleMessage): Boolean {
                runtime.logger.debug("web: ${message.message()} (${message.sourceId()}:${message.lineNumber()})")

                return true
            }
        }
    }

    // postWebMessage delivers only to the page's origin, so a page that navigated elsewhere never gets the port.
    private fun connectBridge() {
        val view = webView ?: return
        // Reloads call this again.
        disconnectBridge()

        val (nativeEnd, webEnd) = view.createWebMessageChannel()
        val millisecondsSinceOpened = SystemClock.elapsedRealtime() - openedAtElapsedMilliseconds
        val options = CoreSettings.bridgeOptionsJson(state, millisecondsSinceOpened)
        val connection = runtime.session.openBridge(
            options,
            host = this,
            hostCallbackExecutor = CompanionRuntime.MainThreadExecutor,
        )
        nativeEnd.setWebMessageCallback(
            object : WebMessagePort.WebMessageCallback() {
                override fun onMessage(port: WebMessagePort, message: WebMessage) {
                    message.data?.let(connection::receiveFromPage)
                }
            },
            Handler(Looper.getMainLooper()),
        )

        nativeMessagePort = nativeEnd
        bridgeConnection = connection
        view.postWebMessage(WebMessage(PORT_HANDSHAKE, arrayOf(webEnd)), Uri.parse(CompanionWebViewClient.ORIGIN))
    }

    private fun disconnectBridge() {
        bridgeConnection?.close()
        bridgeConnection = null
        nativeMessagePort?.close()
        nativeMessagePort = null
    }

    override fun postProtocolMessageToPage(message: String) {
        nativeMessagePort?.postMessage(WebMessage(message))
    }

    override fun onPageRequestedClose() = finish()

    override fun onStateChanged(state: UiState) {
        this.state = state
    }

    // The result reaches the page as an account_changed event.
    override fun onPageRequestedSignIn() {
        runtime.signIn(this, CompanionRuntime.MainThreadExecutor, IgnoredResult)
    }

    override fun onCoreUnreachable(error: CompanionException) {
        runtime.analytics.track(
            GeneratedContract.EVENT_COMPANION_LAUNCH_FAILED,
            GeneratedContract.PROPERTY_REASON to error.code.name.lowercase(),
        )
        finish()
    }

    private fun handleBack() {
        val connection = bridgeConnection
        if (state.route == UiState.ROUTE_ITEM && connection != null) {
            connection.sendEventToPage(GeneratedContract.BRIDGE_EVENT_NAVIGATE_BACK)
        } else {
            finish()
        }
    }

    private fun registerBackHandler() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val callback = OnBackInvokedCallback { handleBack() }
            onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback)
            registeredBackCallback = callback
        }
    }

    @Deprecated("Pre-API 33 back handling")
    @Suppress("DEPRECATION")
    override fun onBackPressed() = handleBack()

    @Suppress("DEPRECATION")
    private fun applyInsets(container: ViewGroup, insets: WindowInsets) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val padding = insets.getInsets(
                WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout() or WindowInsets.Type.ime(),
            )
            container.setPadding(padding.left, padding.top, padding.right, padding.bottom)
        } else {
            container.setPadding(
                insets.systemWindowInsetLeft,
                insets.systemWindowInsetTop,
                insets.systemWindowInsetRight,
                insets.systemWindowInsetBottom,
            )
        }
    }

    override fun onResume() {
        super.onResume()
        webView?.onResume()
    }

    override fun onPause() {
        webView?.onPause()
        super.onPause()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putString(KEY_ROUTE, state.route)
        outState.putString(KEY_QUERY, state.query)
        outState.putString(KEY_ITEM_ID, state.itemId)
    }

    override fun onDestroy() {
        if (::runtime.isInitialized) {
            runtime.analytics.track(
                GeneratedContract.EVENT_COMPANION_CLOSED,
                GeneratedContract.PROPERTY_DURATION_MILLISECONDS to SystemClock.elapsedRealtime() - openedAtElapsedMilliseconds,
            )
        }

        disconnectBridge()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val callback = registeredBackCallback as? OnBackInvokedCallback
            callback?.let(onBackInvokedDispatcher::unregisterOnBackInvokedCallback)
        }

        webView?.let { view ->
            (view.parent as? ViewGroup)?.removeView(view)
            view.stopLoading()
            view.destroy()
        }
        webView = null

        super.onDestroy()
    }

    private fun restoreState(saved: Bundle): UiState = UiState(
        saved.getString(KEY_ROUTE) ?: UiState.ROUTE_SEARCH,
        saved.getString(KEY_QUERY),
        saved.getString(KEY_ITEM_ID),
    )

    private object IgnoredResult : CompanionCallback<Account> {
        override fun onSuccess(result: Account) = Unit

        override fun onError(error: CompanionException) = Unit
    }

    companion object {
        private const val EXTRA_INITIAL_QUERY = "com.tarkovcompanion.sdk.QUERY"
        private const val KEY_ROUTE = "tarkov_companion.route"
        private const val KEY_QUERY = "tarkov_companion.query"
        private const val KEY_ITEM_ID = "tarkov_companion.item"
        const val PORT_HANDSHAKE = "tarkov-companion-bridge-port"

        fun intent(context: Context, initialQuery: String?): Intent =
            Intent(context, CompanionActivity::class.java)
                .putExtra(EXTRA_INITIAL_QUERY, initialQuery?.take(GeneratedContract.SEARCH_QUERY_MAXIMUM_LENGTH))
    }
}
