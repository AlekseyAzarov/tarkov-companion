package com.tarkovcompanion.sdk

import android.app.Activity
import android.content.Context
import android.os.Looper
import android.os.Trace
import android.util.Log
import com.tarkovcompanion.sdk.internal.CompanionRuntime
import com.tarkovcompanion.sdk.internal.ModelMapper
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import com.tarkovcompanion.sdk.internal.session.SessionAction
import com.tarkovcompanion.sdk.model.FavoriteItem
import com.tarkovcompanion.sdk.model.ItemSummary
import com.tarkovcompanion.sdk.model.Recommendation
import com.tarkovcompanion.sdk.ui.CompanionActivity
import org.json.JSONObject

/**
 * Entry point of the Tarkov Companion SDK.
 *
 * ```
 * // Application.onCreate
 * TarkovCompanion.initialize(this, CompanionConfig.Builder().build())
 * // anywhere with an Activity, e.g. from a pause-menu button
 * TarkovCompanion.showItemLookup(activity)
 * ```
 *
 * Methods are safe from any thread unless noted, and static (@JvmStatic) for
 * callers through JNI.
 *
 * Nothing here throws. Calls with a callback report problems through it
 * ([ErrorCode.NOT_INITIALIZED], [ErrorCode.INVALID_ARGUMENT]); [showItemLookup]
 * returns a [LaunchResult]. A profile or listeners given before [initialize]
 * take effect when it runs; [prewarm] before it does nothing.
 */
public object TarkovCompanion {
    @JvmStatic
    public val sdkVersion: String
        get() = GeneratedContract.SDK_VERSION

    /** No disk, network or native-library work: safe in Application.onCreate. Calling it again does nothing. */
    @JvmStatic
    public fun initialize(context: Context, config: CompanionConfig) {
        Trace.beginSection("TarkovCompanion.initialize")
        try {
            synchronized(stateGivenBeforeInitializeLock) {
                if (!CompanionRuntime.initializeOrReturnFalseIfAlreadyInitialized(context, config)) {
                    CompanionRuntime.get()?.logger?.warn("initialize() called twice; keeping the first config")
                    return
                }

                CompanionRuntime.get()?.let(::handOverStateGivenBeforeInitialize)
            }
        } finally {
            Trace.endSection()
        }
    }

    @JvmStatic
    public fun isInitialized(): Boolean = CompanionRuntime.get() != null

    /**
     * Starts the WebView engine and the native library ahead of the first
     * [showItemLookup], saving about 200 ms of it. Costs a pause of the main
     * thread of about 200 ms and about 45 MB for the rest of the process, so
     * call it when the player is likely to open the companion, not at game
     * start. Calling it again does nothing.
     */
    @JvmStatic
    public fun prewarm() {
        val runtime = CompanionRuntime.get()
        if (runtime == null) {
            Log.w(LOG_TAG, "prewarm() called before initialize(); ignored")
            return
        }
        if (Looper.myLooper() != Looper.getMainLooper()) {
            CompanionRuntime.MainThreadExecutor.execute { prewarm() }
            return
        }

        Trace.beginSection("TarkovCompanion.prewarm")
        try {
            runtime.prewarm()
        } finally {
            Trace.endSection()
        }
    }

    /** Updates the player context used for subsequent verdicts. Before [initialize], it is kept until then. */
    @JvmStatic
    public fun setPlayerProfile(profile: PlayerProfile) {
        synchronized(stateGivenBeforeInitializeLock) {
            val runtime = CompanionRuntime.get()
            if (runtime == null) {
                profileGivenBeforeInitialize = profile
            } else {
                runtime.profile = profile
            }
        }
    }

    /** Opens the overlay above [activity]. Main thread. The activity receives onPause, as for a dialog. */
    @JvmStatic
    @JvmOverloads
    public fun showItemLookup(activity: Activity, initialQuery: String? = null): LaunchResult {
        val runtime = CompanionRuntime.get() ?: return LaunchResult.NOT_INITIALIZED
        if (!runtime.session.launchAllowedByKillSwitch) {
            runtime.analytics.track(
                GeneratedContract.EVENT_COMPANION_LAUNCH_BLOCKED,
                GeneratedContract.PROPERTY_REASON to "remote_kill_switch",
            )

            return LaunchResult.DISABLED_REMOTELY
        }

        activity.startActivity(CompanionActivity.intent(activity, initialQuery))

        return LaunchResult.LAUNCHED
    }

    /** Headless search, for hosts with their own UI. */
    @JvmStatic
    public fun searchItems(query: String, callback: CompanionCallback<List<ItemSummary>>): Cancellable {
        val runtime = CompanionRuntime.get() ?: return failNotInitialized(callback)
        return runtime.session.search(
            query,
            runtime.callbackExecutor,
            callback,
            ModelMapper::itemSummariesFromSearchResult,
        )
    }

    /** Headless verdict for one item id (as returned by [searchItems]). */
    @JvmStatic
    public fun getRecommendation(itemId: String, callback: CompanionCallback<Recommendation>): Cancellable {
        val runtime = CompanionRuntime.get() ?: return failNotInitialized(callback)
        return runtime.session.recommend(
            itemId,
            runtime.callbackExecutor,
            callback,
            ModelMapper::recommendationFromResult,
        )
    }

    /**
     * Signs the player in, in the system browser over [activity]. Main thread.
     * Errors: [ErrorCode.SIGN_IN_CANCELLED], [ErrorCode.SIGN_IN_FAILED],
     * [ErrorCode.NETWORK], and [ErrorCode.INVALID_ARGUMENT] when sign-in is not
     * configured or can't work in this app. Kept across restarts.
     */
    @JvmStatic
    public fun signIn(activity: Activity, callback: CompanionCallback<Account>): Cancellable {
        val runtime = CompanionRuntime.get() ?: return failNotInitialized(callback)

        return runtime.signIn(activity, runtime.callbackExecutor, callback)
    }

    /** Signs out at once, offline too; revoking on the server is best effort. */
    @JvmStatic
    public fun signOut(callback: CompanionCallback<Unit>): Cancellable {
        val runtime = CompanionRuntime.get() ?: return failNotInitialized(callback)
        return runtime.session.signOut(runtime.callbackExecutor, callback)
    }

    /** Null until the stored sign-in is read after a cold start; [addAccountListener] tells when. */
    @JvmStatic
    public val currentAccount: Account?
        get() = CompanionRuntime.get()?.session?.lastReportedAccount

    /** [listener] is called on the main thread whenever [currentAccount] changes. */
    @JvmStatic
    public fun addAccountListener(listener: AccountListener) {
        synchronized(stateGivenBeforeInitializeLock) {
            val runtime = CompanionRuntime.get()
            if (runtime == null) {
                accountListenersGivenBeforeInitialize += listener
            } else {
                runtime.session.addAccountListener(listener)
            }
        }
    }

    @JvmStatic
    public fun removeAccountListener(listener: AccountListener) {
        synchronized(stateGivenBeforeInitializeLock) {
            accountListenersGivenBeforeInitialize -= listener
            CompanionRuntime.get()?.session?.removeAccountListener(listener)
        }
    }

    /** Most recently added first: the device's list when signed out, the account's when signed in. */
    @JvmStatic
    public fun getFavorites(callback: CompanionCallback<List<FavoriteItem>>): Cancellable {
        val runtime = CompanionRuntime.get() ?: return failNotInitialized(callback)
        return runtime.session.favorites(runtime.callbackExecutor, callback) { payload ->
            val items = JSONObject(payload.decodeToString()).getJSONArray(GeneratedContract.PAYLOAD_FIELD_ITEMS)
            SessionAction.favoriteItemsFromJson(items)
        }
    }

    /**
     * The item must have been searched or looked at in this session, otherwise
     * [ErrorCode.NOT_FOUND]. [ErrorCode.INVALID_ARGUMENT] when the list is full.
     */
    @JvmStatic
    public fun setFavorite(itemId: String, favorite: Boolean, callback: CompanionCallback<Unit>): Cancellable {
        val runtime = CompanionRuntime.get() ?: return failNotInitialized(callback)
        return runtime.session.setFavorite(itemId, favorite, runtime.callbackExecutor, callback)
    }

    /** [listener] gets the whole list on the main thread whenever it changes. */
    @JvmStatic
    public fun addFavoritesListener(listener: FavoritesListener) {
        synchronized(stateGivenBeforeInitializeLock) {
            val runtime = CompanionRuntime.get()
            if (runtime == null) {
                favoritesListenersGivenBeforeInitialize += listener
            } else {
                runtime.session.addFavoritesListener(listener)
            }
        }
    }

    @JvmStatic
    public fun removeFavoritesListener(listener: FavoritesListener) {
        synchronized(stateGivenBeforeInitializeLock) {
            favoritesListenersGivenBeforeInitialize -= listener
            CompanionRuntime.get()?.session?.removeFavoritesListener(listener)
        }
    }

    private const val NOT_INITIALIZED_MESSAGE = "TarkovCompanion.initialize() has not been called"
    private const val LOG_TAG = "TarkovCompanion"

    private val stateGivenBeforeInitializeLock = Any()
    private var profileGivenBeforeInitialize: PlayerProfile? = null
    private val accountListenersGivenBeforeInitialize = mutableSetOf<AccountListener>()
    private val favoritesListenersGivenBeforeInitialize = mutableSetOf<FavoritesListener>()

    private fun handOverStateGivenBeforeInitialize(runtime: CompanionRuntime) {
        profileGivenBeforeInitialize?.let { profile -> runtime.profile = profile }
        profileGivenBeforeInitialize = null
        accountListenersGivenBeforeInitialize.forEach(runtime.session::addAccountListener)
        accountListenersGivenBeforeInitialize.clear()
        favoritesListenersGivenBeforeInitialize.forEach(runtime.session::addFavoritesListener)
        favoritesListenersGivenBeforeInitialize.clear()
    }

    private fun <T> failNotInitialized(callback: CompanionCallback<T>): Cancellable {
        callback.onError(CompanionException(ErrorCode.NOT_INITIALIZED, NOT_INITIALIZED_MESSAGE))
        val nothingIsRunning = Cancellable { }

        return nothingIsRunning
    }
}
