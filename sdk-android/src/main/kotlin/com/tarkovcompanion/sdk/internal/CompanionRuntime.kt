package com.tarkovcompanion.sdk.internal

import android.app.Activity
import android.content.Context
import android.content.pm.ApplicationInfo
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.os.SystemClock
import android.webkit.WebSettings
import com.tarkovcompanion.sdk.Account
import com.tarkovcompanion.sdk.Cancellable
import com.tarkovcompanion.sdk.CompanionCallback
import com.tarkovcompanion.sdk.CompanionConfig
import com.tarkovcompanion.sdk.CompanionException
import com.tarkovcompanion.sdk.ErrorCode
import com.tarkovcompanion.sdk.PlayerProfile
import com.tarkovcompanion.sdk.host.HttpRequest
import com.tarkovcompanion.sdk.host.HttpTransport
import com.tarkovcompanion.sdk.internal.cache.DiskCache
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import com.tarkovcompanion.sdk.internal.net.DefaultHttpTransport
import com.tarkovcompanion.sdk.internal.net.HttpNames
import com.tarkovcompanion.sdk.internal.secure.KeystoreSecretStore
import com.tarkovcompanion.sdk.internal.session.CoreSettings
import com.tarkovcompanion.sdk.internal.session.NativeSession
import com.tarkovcompanion.sdk.internal.session.SessionDriver
import com.tarkovcompanion.sdk.ui.SignInActivity
import java.io.File
import java.io.IOException
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import java.util.concurrent.ThreadFactory
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/** Process-wide SDK state. Construction touches no disk, network or native library: it runs on the host's cold start. */
internal class CompanionRuntime private constructor(
    val applicationContext: Context,
    val config: CompanionConfig,
) {
    val logger = Logger(config.logger ?: LogcatLogger)
    val analytics = Analytics(config.analytics, logger)
    val sdkVersion: String = GeneratedContract.SDK_VERSION
    private val userAgent = "TarkovCompanionSDK/$sdkVersion (Android ${Build.VERSION.SDK_INT})"
    private val httpTransport: HttpTransport = config.httpTransport ?: DefaultHttpTransport()

    // One thread runs the core and its actions in order; HTTP blocks on threads of its own.
    private val coreExecutor = Executors.newSingleThreadExecutor(BackgroundThreadFactory)
    private val httpExecutor = Executors.newFixedThreadPool(HTTP_THREAD_COUNT, BackgroundThreadFactory)
    val callbackExecutor: Executor = config.callbackExecutor ?: MainThreadExecutor

    @Volatile var profile: PlayerProfile = PlayerProfile.DEFAULT

    // Lazy, and first touched on a background thread: it creates directories.
    private val cacheStore by lazy {
        DiskCache(
            File(applicationContext.cacheDir, CACHE_DIRECTORY),
            maximumEntryCountOrNullForUnlimited = CACHE_MAXIMUM_ENTRIES,
        )
    }

    // The player's own data (favourites): filesDir, never evicted; backed up with the app.
    private val dataStore by lazy {
        DiskCache(File(applicationContext.filesDir, DATA_DIRECTORY), maximumEntryCountOrNullForUnlimited = null)
    }

    // Sign-in tokens. noBackupFilesDir: a backup restored elsewhere couldn't be decrypted anyway.
    private val secretStore by lazy {
        KeystoreSecretStore(File(applicationContext.noBackupFilesDir, SECRETS_DIRECTORY)) { key, failure ->
            val failureName = failure.javaClass.simpleName
            logger.warn("dropping an unreadable secret '$key' ($failureName); the player signs in again")
        }
    }

    // Debuggable host builds may use a development server over http on this machine.
    private val debuggableHostBuild = applicationContext.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0

    private val signInRedirectSchemeOrNull =
        CoreSettings.signInRedirectSchemeOrNull(config, applicationContext.packageName)

    // Why sign-in can't work with this config, said once at initialize() and to every signIn() caller.
    private val signInConfigurationProblemOrNull: String? = when {
        config.signIn == null -> "sign-in is not configured: set CompanionConfig.Builder.signIn()"
        signInRedirectSchemeOrNull == null ->
            "sign-in is off: the applicationId '${applicationContext.packageName}' can't be a URI scheme " +
                "(lowercase letters, digits, '+', '-', '.'); set SignInConfig.Builder.redirectScheme() and " +
                "declare it in the manifest (docs/auth.md)"
        else -> null
    }

    val session = SessionDriver(
        openSession = {
            val signInRedirectUri = signInRedirectSchemeOrNull?.let(CoreSettings::signInRedirectUri)
            NativeSession.loadLibraryAndCreate(
                CoreSettings.sessionConfigJson(config, userAgent, signInRedirectUri, debuggableHostBuild),
            )
        },
        httpTransport = httpTransport,
        cacheStore = { cacheStore },
        secretStore = { secretStore },
        dataStoreKeptUntilDeleted = { dataStore },
        coreExecutor = coreExecutor,
        httpExecutor = httpExecutor,
        listenerExecutor = MainThreadExecutor,
        logger = logger,
        analytics = analytics,
        currentProfile = { profile },
    )

    private val webViewEngineLoaded = AtomicBoolean(false)

    init {
        if (config.signIn != null) {
            signInConfigurationProblemOrNull?.let { problem -> logger.error(problem) }
        }
    }

    // For the host's signIn() and for the page's sign-in button alike.
    fun signIn(activity: Activity, deliverOn: Executor, callback: CompanionCallback<Account>): Cancellable {
        val problem = signInConfigurationProblemOrNull
        if (problem != null) {
            deliverOn.execute { callback.onError(CompanionException(ErrorCode.INVALID_ARGUMENT, problem)) }
            val nothingIsRunning = Cancellable { }

            return nothingIsRunning
        }

        return session.signIn(deliverOn, callback) { url ->
            activity.startActivity(SignInActivity.intent(activity, url))
        }
    }

    // Main thread: WebView may only be touched there.
    fun prewarm() {
        session.openSessionInBackground()

        if (!webViewEngineLoaded.compareAndSet(false, true)) {
            return
        }

        val startedAt = SystemClock.elapsedRealtime()
        try {
            // Loads the WebView provider and starts Chromium in this process, the
            // bulk of the first WebView's cost, without creating a WebView or a
            // renderer process.
            WebSettings.getDefaultUserAgent(applicationContext)
            logger.debug("WebView engine loaded in ${SystemClock.elapsedRealtime() - startedAt} ms")
        } catch (failure: RuntimeException) {
            // Missing, disabled or updating provider: the overlay reports it when opened.
            logger.warn("could not prewarm WebView: ${failure.javaClass.simpleName}")
        }
    }

    // Item icons go through the host's transport too. Blocking.
    fun fetchWithHttpGet(url: String): ByteArray {
        val headers = mapOf(HttpNames.HEADER_USER_AGENT to userAgent)
        val request = HttpRequest(url, HttpNames.METHOD_GET, headers, null, FETCH_TIMEOUT_MILLISECONDS)
        val response = httpTransport.execute(request)
        if (!response.isSuccessful) {
            throw IOException("HTTP ${response.statusCode} for $url")
        }

        return response.body
    }

    internal object MainThreadExecutor : Executor {
        private val mainThreadHandler = Handler(Looper.getMainLooper())

        override fun execute(command: Runnable) {
            mainThreadHandler.post(command)
        }
    }

    // Background priority: SDK work must never compete with the game's render thread.
    private object BackgroundThreadFactory : ThreadFactory {
        private val createdThreadCount = AtomicInteger()

        override fun newThread(task: Runnable): Thread = Thread(
            {
                Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
                task.run()
            },
            "tarkov-companion-${createdThreadCount.incrementAndGet()}",
        ).apply { isDaemon = true }
    }

    companion object {
        // A lookup's GraphQL request, its catalog fallback, the hideout and a
        // sync can be in flight together; a host transport that ignores its
        // timeout then only holds up other HTTP, never the core's queue.
        private const val HTTP_THREAD_COUNT = 4

        // Versioned so a future format change can simply start a new directory.
        private const val CACHE_DIRECTORY = "tarkov-companion/cache-v1"
        private const val SECRETS_DIRECTORY = "tarkov-companion/secrets"
        private const val DATA_DIRECTORY = "tarkov-companion/data"
        private const val CACHE_MAXIMUM_ENTRIES = 64
        private const val FETCH_TIMEOUT_MILLISECONDS = 10_000

        @Volatile private var instance: CompanionRuntime? = null

        fun get(): CompanionRuntime? = instance

        @Synchronized
        fun initializeOrReturnFalseIfAlreadyInitialized(context: Context, config: CompanionConfig): Boolean {
            if (instance != null) {
                return false
            }

            val runtime = CompanionRuntime(context.applicationContext, config)
            instance = runtime

            // So the kill switch and the signed-in account are known before the
            // first lookup; on first idle, after the host's own startup work.
            if (config.remoteFlagsUrl != null || config.signIn != null) {
                Looper.getMainLooper().queue.addIdleHandler {
                    runtime.session.openSessionInBackground()
                    val runAgainOnNextIdle = false
                    runAgainOnNextIdle
                }
            }

            return true
        }
    }
}
