package com.tarkovcompanion.sdk.internal.session

import com.tarkovcompanion.sdk.Account
import com.tarkovcompanion.sdk.AccountListener
import com.tarkovcompanion.sdk.Cancellable
import com.tarkovcompanion.sdk.CompanionCallback
import com.tarkovcompanion.sdk.CompanionException
import com.tarkovcompanion.sdk.ErrorCode
import com.tarkovcompanion.sdk.FavoritesListener
import com.tarkovcompanion.sdk.PlayerProfile
import com.tarkovcompanion.sdk.host.HttpTransport
import com.tarkovcompanion.sdk.internal.Analytics
import com.tarkovcompanion.sdk.internal.Logger
import com.tarkovcompanion.sdk.internal.cache.KeyValueStore
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract as Contract
import com.tarkovcompanion.sdk.internal.secure.SecretStore
import org.json.JSONObject
import java.io.IOException
import java.security.SecureRandom
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArraySet
import java.util.concurrent.Executor
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * Runs the core session's actions with the host's [HttpTransport], the SDK's
 * stores and threads. All tarkov.dev and protocol logic is in the core.
 *
 * Calls into the core and its actions run on one serial queue, in the order
 * the core produced them; only the blocking part of HTTP runs on [httpExecutor].
 * A result is delivered once, on the caller's executor, unless cancelled;
 * cancellation is checked on the delivery thread, so no callback follows a
 * cancel() made there. Every action that expects an answer gets one, even
 * when a store fails or the action can't be read.
 */
internal class SessionDriver(
    openSession: () -> CoreSession,
    private val httpTransport: HttpTransport,
    private val cacheStore: () -> KeyValueStore,
    private val secretStore: () -> SecretStore,
    private val dataStoreKeptUntilDeleted: () -> KeyValueStore,
    coreExecutor: Executor,
    private val httpExecutor: Executor,
    private val listenerExecutor: Executor,
    private val logger: Logger,
    private val analytics: Analytics,
    private val currentProfile: () -> PlayerProfile,
) {
    // Created on first use, on a background thread: opening it loads the native library.
    private val lazySession = lazy(openSession)
    private val coreQueue = SerialExecutor(coreExecutor)
    private val lastRequestId = AtomicLong()
    private val deliveriesByRequestId = ConcurrentHashMap<Long, Delivery<*>>()
    private val bridgeConnectionsById = ConcurrentHashMap<Long, BridgeConnection>()
    private val profileLock = Any()
    private var profileLastSentToCore: PlayerProfile? = null
    private val signInBrowserOpenersByRequestId = ConcurrentHashMap<Long, SignInBrowserOpener>()
    private val accountListeners = CopyOnWriteArraySet<AccountListener>()
    private val favoritesListeners = CopyOnWriteArraySet<FavoritesListener>()
    private val secureRandom = SecureRandom()

    @Volatile var lastReportedAccount: Account? = null
        private set

    // Read on the main thread without waiting on the core.
    @Volatile var launchAllowedByKillSwitch: Boolean = true
        private set

    // So the remote flags and a stored sign-in are known before the first lookup.
    fun openSessionInBackground() {
        coreQueue.execute {
            runLoggingFailures {
                runActionsUntilIdle(lazySession.value)
            }
        }
    }

    fun <T> search(
        query: String,
        deliverOn: Executor,
        callback: CompanionCallback<T>,
        convertPayload: (ByteArray) -> T,
    ): Cancellable = submitCommand(Delivery(callback, deliverOn, convertPayload)) { session, requestId ->
        session.search(requestId, query)
    }

    fun <T> recommend(
        itemId: String,
        deliverOn: Executor,
        callback: CompanionCallback<T>,
        convertPayload: (ByteArray) -> T,
    ): Cancellable = submitCommand(Delivery(callback, deliverOn, convertPayload)) { session, requestId ->
        session.recommend(requestId, itemId)
    }

    // Randomness for PKCE comes from here: the core has none of its own.
    fun signIn(
        deliverOn: Executor,
        callback: CompanionCallback<Account>,
        openSignInPageInBrowser: (String) -> Unit,
    ): Cancellable {
        val secureRandomBytes = ByteArray(CoreSession.SIGN_IN_ENTROPY_BYTES).also(secureRandom::nextBytes)
        val delivery = Delivery(callback, deliverOn) { payload ->
            accountFromSignInResult(JSONObject(payload.decodeToString()))
        }

        return submitCommand(
            delivery,
            onRequestIdAssigned = { requestId ->
                signInBrowserOpenersByRequestId[requestId] = SignInBrowserOpener(deliverOn, openSignInPageInBrowser)
            },
        ) { session, requestId ->
            session.signIn(requestId, secureRandomBytes)
        }
    }

    fun signInRedirect(redirectUriOrNullIfClosed: String?) {
        coreQueue.execute {
            runLoggingFailures {
                val session = lazySession.value
                session.signInRedirect(redirectUriOrNullIfClosed)
                runActionsUntilIdle(session)
            }
        }
    }

    fun signOut(deliverOn: Executor, callback: CompanionCallback<Unit>): Cancellable =
        submitCommand(Delivery(callback, deliverOn) { }) { session, requestId ->
            session.signOut(requestId)
        }

    fun <T> favorites(
        deliverOn: Executor,
        callback: CompanionCallback<T>,
        convertPayload: (ByteArray) -> T,
    ): Cancellable =
        submitCommand(Delivery(callback, deliverOn, convertPayload)) { session, requestId ->
            session.favoritesList(requestId)
        }

    fun setFavorite(
        itemId: String,
        favorite: Boolean,
        deliverOn: Executor,
        callback: CompanionCallback<Unit>,
    ): Cancellable =
        submitCommand(Delivery(callback, deliverOn) { }) { session, requestId ->
            session.favoriteSet(requestId, itemId, favorite)
        }

    fun addFavoritesListener(listener: FavoritesListener) {
        favoritesListeners += listener
    }

    fun removeFavoritesListener(listener: FavoritesListener) {
        favoritesListeners -= listener
    }

    fun addAccountListener(listener: AccountListener) {
        accountListeners += listener
    }

    fun removeAccountListener(listener: AccountListener) {
        accountListeners -= listener
    }

    fun openBridge(optionsJson: String, host: BridgeHost, hostCallbackExecutor: Executor): BridgeConnection {
        val connection = BridgeConnection(host, hostCallbackExecutor)
        connection.runOnSerialQueue { session ->
            val bridgeId = session.openBridgeAndGetId(optionsJson)
            connection.bridgeId = bridgeId
            bridgeConnectionsById[bridgeId] = connection
        }

        return connection
    }

    private fun submitCommand(
        delivery: Delivery<*>,
        onRequestIdAssigned: (Long) -> Unit = {},
        command: (CoreSession, Long) -> Unit,
    ): Cancellable {
        val requestId = lastRequestId.incrementAndGet()
        deliveriesByRequestId[requestId] = delivery
        onRequestIdAssigned(requestId)

        coreQueue.execute {
            val session = try {
                val openedSession = lazySession.value
                sendProfileToCoreIfChanged(openedSession)
                command(openedSession, requestId)
                openedSession
            } catch (failure: Exception) {
                // Only failures of the command itself are this request's; a
                // failing action below may belong to any request.
                deliveriesByRequestId.remove(requestId)?.fail(failure, logger)
                return@execute
            }

            runActionsUntilIdle(session)
        }

        return Cancellable {
            if (delivery.claimIfFirst()) {
                deliveriesByRequestId.remove(requestId)
                coreQueue.execute {
                    if (lazySession.isInitialized()) {
                        runLoggingFailures { lazySession.value.cancel(requestId) }
                    }
                }
            }
        }
    }

    private fun sendProfileToCoreIfChanged(session: CoreSession) = synchronized(profileLock) {
        val profile = currentProfile()
        if (profile !== profileLastSentToCore) {
            session.setProfile(CoreSettings.profileJson(profile))
            profileLastSentToCore = profile
        }
    }

    // On the core queue only: that is what keeps the actions in order.
    private fun runActionsUntilIdle(session: CoreSession) {
        while (true) {
            val action = try {
                session.nextActionOrNullWhenIdle() ?: return
            } catch (failure: Exception) {
                // The core already handed it out, e.g. the JVM ran out of memory copying it.
                logger.error("lost an action of the core session", failure)
                continue
            }

            try {
                runAction(session, action)
            } catch (failure: Exception) {
                logger.error("action ${action.javaClass.simpleName} failed", failure)
            }
        }
    }

    private fun runAction(session: CoreSession, action: SessionAction) {
        when (action) {
            // Only the blocking request leaves the core queue; its completion comes back to it.
            is SessionAction.Http -> httpExecutor.execute { executeHttpAndComplete(session, action) }
            is SessionAction.CacheRead -> {
                val value = readOrNullIfFailed(CACHE_STORE_NAME) { cacheStore().get(action.key) }
                session.completeCacheRead(action.actionId, value)
            }
            is SessionAction.CacheWrite -> writeLoggingFailure(CACHE_STORE_NAME) {
                cacheStore().put(action.key, action.value)
            }
            is SessionAction.Result -> {
                signInBrowserOpenersByRequestId.remove(action.requestId)
                deliveriesByRequestId.remove(action.requestId)?.completeWith(action, logger)
            }
            is SessionAction.Log -> logger.log(action.level, action.message)
            is SessionAction.Track -> analytics.track(action.event, action.properties)
            is SessionAction.BridgeSend -> bridgeConnectionsById[action.bridgeId]?.deliverToHost {
                postProtocolMessageToPage(action.message)
            }
            is SessionAction.BridgeState -> bridgeConnectionsById[action.bridgeId]?.deliverToHost {
                onStateChanged(action.state)
            }
            is SessionAction.BridgeClose -> bridgeConnectionsById[action.bridgeId]?.deliverToHost {
                onPageRequestedClose()
            }
            is SessionAction.RemoteFlagsChanged -> launchAllowedByKillSwitch = action.launchAllowed
            is SessionAction.SecretRead -> {
                val value = readOrNullIfFailed(SECRET_STORE_NAME) { secretStore().get(action.key) }
                session.completeCacheRead(action.actionId, value)
            }
            is SessionAction.SecretWrite -> writeLoggingFailure(SECRET_STORE_NAME) {
                secretStore().put(action.key, action.value)
            }
            is SessionAction.SecretDelete -> writeLoggingFailure(SECRET_STORE_NAME) {
                secretStore().delete(action.key)
            }
            is SessionAction.OpenSignInPageInBrowser -> {
                signInBrowserOpenersByRequestId.remove(action.requestId)?.let { opener ->
                    opener.executor.execute { opener.openInBrowser(action.url) }
                }
            }
            is SessionAction.AccountChanged -> {
                lastReportedAccount = action.accountOrNullIfSignedOut
                val account = action.accountOrNullIfSignedOut
                listenerExecutor.execute {
                    accountListeners.forEach { listener -> listener.onAccountChanged(account) }
                }
            }
            is SessionAction.PageRequestedSignIn -> bridgeConnectionsById[action.bridgeId]?.deliverToHost {
                onPageRequestedSignIn()
            }
            is SessionAction.DataRead -> {
                val value = readOrNullIfFailed(DATA_STORE_NAME) { dataStoreKeptUntilDeleted().get(action.key) }
                session.completeCacheRead(action.actionId, value)
            }
            is SessionAction.DataWrite -> writeLoggingFailure(DATA_STORE_NAME) {
                dataStoreKeptUntilDeleted().put(action.key, action.value)
            }
            is SessionAction.DataDelete -> writeLoggingFailure(DATA_STORE_NAME) {
                dataStoreKeptUntilDeleted().delete(action.key)
            }
            is SessionAction.FavoritesChanged -> listenerExecutor.execute {
                favoritesListeners.forEach { listener -> listener.onFavoritesChanged(action.items) }
            }
            is SessionAction.UnknownFromNewerCore -> {
                logger.warn("skipping an action this SDK does not know: ${action.json}")
            }
            is SessionAction.Unreadable -> answerUnreadableAction(session, action)
        }
    }

    // A store that fails reads as "not there": the core falls back as it would for a missing entry.
    private fun readOrNullIfFailed(storeName: String, read: () -> ByteArray?): ByteArray? = try {
        read()
    } catch (failure: Exception) {
        logger.error("$storeName store read failed", failure)
        null
    }

    // A full disk or a broken Keystore must not stop the actions queued after this one.
    private fun writeLoggingFailure(storeName: String, write: () -> Unit) {
        try {
            write()
        } catch (failure: Exception) {
            logger.error("$storeName store write failed", failure)
        }
    }

    private fun answerUnreadableAction(session: CoreSession, action: SessionAction.Unreadable) {
        logger.error("could not read an action of the core session: ${action.json}", action.failure)
        action.readActionIdToAnswerNotFound?.let { actionId -> session.completeCacheRead(actionId, null) }
        action.httpActionIdToAnswerNoResponse?.let { actionId ->
            val reason = "unreadable http action: ${action.failure.message}"
            session.completeHttp(actionId, STATUS_CODE_NO_RESPONSE, reason.encodeToByteArray())
        }
    }

    // On the HTTP executor: blocks for as long as the host's transport takes.
    private fun executeHttpAndComplete(session: CoreSession, action: SessionAction.Http) {
        val (statusCode, body) = try {
            val response = httpTransport.execute(action.request)
            response.statusCode to response.body
        } catch (failure: Exception) {
            // Anything the host transport throws must still complete the
            // action, or the request waiting on it would never get a result.
            if (failure !is IOException) {
                logger.error("HttpTransport threw instead of returning", failure)
            }
            STATUS_CODE_NO_RESPONSE to (failure.message ?: failure.javaClass.name).encodeToByteArray()
        }

        coreQueue.execute {
            runLoggingFailures {
                session.completeHttp(action.actionId, statusCode, body)
                runActionsUntilIdle(session)
            }
        }
    }

    private fun runLoggingFailures(block: () -> Unit) {
        try {
            block()
        } catch (failure: Exception) {
            logger.error("session executor failed", failure)
        }
    }

    /** One page load's bridge. After [close] returns on the host callback thread, the host is not called again. */
    inner class BridgeConnection internal constructor(
        private val host: BridgeHost,
        private val hostCallbackExecutor: Executor,
    ) {
        @Volatile internal var bridgeId: Long? = null

        @Volatile private var closed = false

        fun receiveFromPage(message: String) = runOnSerialQueue { session ->
            bridgeId?.let { openedBridgeId -> session.bridgeReceive(openedBridgeId, message) }
        }

        fun sendEventToPage(eventName: String) = runOnSerialQueue { session ->
            bridgeId?.let { openedBridgeId -> session.bridgeEvent(openedBridgeId, eventName) }
        }

        fun close() {
            closed = true
            coreQueue.execute {
                val openedBridgeId = bridgeId ?: return@execute
                bridgeConnectionsById.remove(openedBridgeId)

                if (lazySession.isInitialized()) {
                    runLoggingFailures { lazySession.value.closeBridge(openedBridgeId) }
                }
            }
        }

        internal fun runOnSerialQueue(command: (CoreSession) -> Unit) {
            if (closed) {
                return
            }

            coreQueue.execute {
                val session = try {
                    val openedSession = lazySession.value
                    command(openedSession)
                    openedSession
                } catch (failure: Exception) {
                    logger.error("bridge call failed", failure)
                    val error = failure as? CompanionException
                        ?: CompanionException(ErrorCode.INTERNAL, failure.message ?: failure.javaClass.name, failure)
                    deliverToHost { onCoreUnreachable(error) }
                    return@execute
                }

                runActionsUntilIdle(session)
            }
        }

        internal fun deliverToHost(call: BridgeHost.() -> Unit) {
            hostCallbackExecutor.execute {
                if (!closed) {
                    host.call()
                }
            }
        }
    }

    private class SignInBrowserOpener(val executor: Executor, val openInBrowser: (String) -> Unit)

    private class Delivery<T>(
        private val callback: CompanionCallback<T>,
        private val executor: Executor,
        private val convertPayload: (ByteArray) -> T,
    ) {
        private val claimed = AtomicBoolean(false)

        // Whoever claims first delivers the result, or cancels it.
        fun claimIfFirst(): Boolean = !claimed.getAndSet(true)

        fun completeWith(result: SessionAction.Result, logger: Logger) {
            val outcome = when {
                result.error != null -> Result.failure(result.error)
                else -> runCatching { convertPayload(result.payload!!) }
            }

            val conversionFailure = outcome.exceptionOrNull()
            if (conversionFailure != null && conversionFailure !is CompanionException) {
                logger.error("bad result payload", conversionFailure)
            }

            deliver(outcome)
        }

        fun fail(failure: Exception, logger: Logger) {
            if (failure !is CompanionException) {
                logger.error("session call failed", failure)
            }

            deliver(Result.failure(failure))
        }

        private fun deliver(outcome: Result<T>) {
            executor.execute {
                if (!claimIfFirst()) {
                    return@execute
                }

                outcome.fold(
                    onSuccess = { value -> callback.onSuccess(value) },
                    onFailure = { failure -> callback.onError(failure.toCompanionException()) },
                )
            }
        }

        private fun Throwable.toCompanionException(): CompanionException =
            this as? CompanionException ?: CompanionException(ErrorCode.INTERNAL, message ?: javaClass.name, this)
    }

    private companion object {
        // tarkov_companion_session_complete_http with no HTTP response at all.
        const val STATUS_CODE_NO_RESPONSE = 0
        const val CACHE_STORE_NAME = "cache"
        const val SECRET_STORE_NAME = "secret"
        const val DATA_STORE_NAME = "data"

        fun accountFromSignInResult(result: JSONObject): Account {
            val account = result.getJSONObject(Contract.PAYLOAD_FIELD_ACCOUNT)

            return Account(
                account.getString(Contract.PAYLOAD_FIELD_ID),
                account.getString(Contract.PAYLOAD_FIELD_DISPLAY_NAME),
            )
        }
    }
}
