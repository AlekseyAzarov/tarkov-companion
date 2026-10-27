package com.tarkovcompanion.sdk.internal.session

import com.tarkovcompanion.sdk.Account
import com.tarkovcompanion.sdk.Cancellable
import com.tarkovcompanion.sdk.CompanionCallback
import com.tarkovcompanion.sdk.CompanionException
import com.tarkovcompanion.sdk.ErrorCode
import com.tarkovcompanion.sdk.PlayerProfile
import com.tarkovcompanion.sdk.host.HttpRequest
import com.tarkovcompanion.sdk.host.HttpResponse
import com.tarkovcompanion.sdk.host.HttpTransport
import com.tarkovcompanion.sdk.host.LogLevel
import com.tarkovcompanion.sdk.internal.cache.KeyValueStore
import com.tarkovcompanion.sdk.internal.net.HttpStatus
import com.tarkovcompanion.sdk.internal.secure.SecretStore
import com.tarkovcompanion.sdk.model.FavoriteItem
import com.tarkovcompanion.sdk.testing.FakeSession
import com.tarkovcompanion.sdk.testing.MemorySecretStore
import com.tarkovcompanion.sdk.testing.MemoryStore
import com.tarkovcompanion.sdk.testing.RecordingAnalytics
import com.tarkovcompanion.sdk.testing.RecordingLogger
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.IOException
import java.security.KeyStoreException
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * The generic executor, against a scripted session. What the actions mean is
 * the core's business (core/tests/session_test.cpp); here it is only about
 * running them faithfully and delivering results once.
 */
class SessionDriverTest {
    private val runImmediately = Executor { task -> task.run() }
    private val session = FakeSession()
    private val cacheStore = MemoryStore()
    private val secretStore = MemorySecretStore()
    private val dataStore = MemoryStore()
    private val recordingLogger = RecordingLogger()
    private val recordingAnalytics = RecordingAnalytics()
    private val sentHttpRequests = mutableListOf<HttpRequest>()
    private var httpTransport = HttpTransport { request ->
        sentHttpRequests += request
        HttpResponse(HttpStatus.OK, emptyMap(), "response".encodeToByteArray())
    }
    private var profile = PlayerProfile.DEFAULT
    private var openSession: () -> CoreSession = { session }

    private fun createDriver(
        coreExecutor: Executor = runImmediately,
        httpExecutor: Executor = runImmediately,
    ) = SessionDriver(
        openSession = { openSession() },
        httpTransport = { request -> httpTransport.execute(request) },
        cacheStore = { cacheStore },
        secretStore = { secretStore },
        dataStoreKeptUntilDeleted = { dataStore },
        coreExecutor = coreExecutor,
        httpExecutor = httpExecutor,
        listenerExecutor = runImmediately,
        logger = recordingLogger.logger,
        analytics = recordingAnalytics.analytics,
        currentProfile = { profile },
    )

    private class RecordingCallback<T> : CompanionCallback<T> {
        val results = mutableListOf<Any?>()

        override fun onSuccess(result: T) {
            results += result
        }

        override fun onError(error: CompanionException) {
            results += error.code
        }
    }

    private fun search(
        driver: SessionDriver,
        deliverOn: Executor = runImmediately,
    ): Pair<RecordingCallback<String>, Cancellable> {
        val callback = RecordingCallback<String>()
        val cancellable = driver.search("salewa", deliverOn, callback) { payload -> payload.decodeToString() }

        return callback to cancellable
    }

    @Test
    fun `runs an http action and delivers the result it leads to`() {
        val request = HttpRequest(
            "https://api.example/graphql",
            "POST",
            mapOf("User-Agent" to "UA"),
            "body".encodeToByteArray(),
            5_000,
        )
        session.onSearch = { _, _ -> session.actions += SessionAction.Http(1, request) }
        session.onHttpCompleted = { _, _, body ->
            session.actions += SessionAction.CacheWrite("key", body)
            session.queueResult(1, """{"items":[]}""")
        }

        val (callback, _) = search(createDriver())

        assertEquals("body", sentHttpRequests.single().body!!.decodeToString())
        assertEquals(Triple(1L, HttpStatus.OK, "response"), session.httpCompletions.single())
        assertEquals("response", cacheStore.entries.getValue("key").decodeToString())
        assertEquals(listOf<Any>("""{"items":[]}"""), callback.results)
    }

    @Test
    fun `transport failures complete the action with status 0`() {
        session.onSearch = { _, _ -> session.actions += httpAction(7) }
        httpTransport = HttpTransport { throw IOException("airplane mode") }

        search(createDriver())

        assertEquals(Triple(7L, 0, "airplane mode"), session.httpCompletions.single())
    }

    @Test
    fun `a transport that throws something else still completes the action`() {
        session.onSearch = { _, _ -> session.actions += httpAction(7) }
        httpTransport = HttpTransport { throw IllegalStateException("host bug") }

        search(createDriver())

        assertEquals(0, session.httpCompletions.single().second)
        assertTrue(recordingLogger.has(LogLevel.ERROR, "HttpTransport threw"))
    }

    @Test
    fun `cache reads are answered from the store`() {
        cacheStore.put("present", "cached".encodeToByteArray())
        session.onSearch = { _, _ ->
            session.actions += SessionAction.CacheRead(1, "present")
            session.actions += SessionAction.CacheRead(2, "absent")
        }

        search(createDriver())

        assertEquals(listOf(1L to "cached", 2L to null), session.cacheReadCompletions)
    }

    @Test
    fun `log actions reach the logger`() {
        session.onSearch = { _, _ -> session.actions += SessionAction.Log(LogLevel.WARNING, "fetch failed") }

        search(createDriver())

        assertTrue(recordingLogger.has(LogLevel.WARNING, "fetch failed"))
    }

    @Test
    fun `error results keep their code`() {
        session.onSearch = { requestId, _ ->
            val error = CompanionException(ErrorCode.SCHEMA_MISMATCH, "shape")
            session.actions += SessionAction.Result(requestId, null, error)
        }

        val (callback, _) = search(createDriver())

        assertEquals(listOf<Any>(ErrorCode.SCHEMA_MISMATCH), callback.results)
    }

    @Test
    fun `cancel before delivery suppresses the callback and tells the session`() {
        val queuedDeliveries = mutableListOf<Runnable>()
        session.onSearch = { requestId, _ -> session.queueResult(requestId, "{}") }
        val (callback, cancellable) = search(createDriver(), deliverOn = { task -> queuedDeliveries += task })

        // E.g. Activity.onDestroy, before the main thread ran the delivery.
        cancellable.cancel()
        queuedDeliveries.forEach(Runnable::run)

        assertTrue(callback.results.isEmpty())
        assertEquals(listOf(1L), session.cancelledRequestIds)
    }

    @Test
    fun `each result is delivered once`() {
        session.onSearch = { requestId, _ ->
            session.queueResult(requestId, "first")
            session.queueResult(requestId, "second")
        }

        val (callback, _) = search(createDriver())

        assertEquals(listOf<Any>("first"), callback.results)
    }

    @Test
    fun `the profile is pushed before the first command and again when it changes`() {
        session.onSearch = { requestId, _ -> session.queueResult(requestId, "{}") }
        val driver = createDriver()

        search(driver)
        search(driver)
        profile = PlayerProfile.Builder().level(30).build()
        search(driver)

        assertEquals(2, session.profiles.size)
        assertTrue(session.profiles.last().contains("\"playerLevel\":30"))
    }

    @Test
    fun `a missing native library is reported per call`() {
        openSession = { throw CompanionException(ErrorCode.NATIVE_UNAVAILABLE, "no .so") }

        val (callback, _) = search(createDriver())

        assertEquals(listOf<Any>(ErrorCode.NATIVE_UNAVAILABLE), callback.results)
    }

    @Test
    fun `a payload the caller can't read is an INTERNAL error`() {
        session.onSearch = { requestId, _ -> session.queueResult(requestId, "{}") }
        val callback = RecordingCallback<String>()

        createDriver().search("salewa", runImmediately, callback) { error("unreadable") }

        assertEquals(listOf<Any>(ErrorCode.INTERNAL), callback.results)
    }

    @Test
    fun `actions from a newer core are skipped, not fatal`() {
        session.onSearch = { requestId, _ ->
            session.actions += SessionAction.UnknownFromNewerCore("""{"type":"telemetry"}""")
            session.queueResult(requestId, "{}")
        }

        val (callback, _) = search(createDriver())

        assertTrue(recordingLogger.has(LogLevel.WARNING, "telemetry"))
        assertEquals(1, callback.results.size)
    }

    private class RecordingBridgeHost : BridgeHost {
        val messagesPostedToPage = mutableListOf<String>()
        val states = mutableListOf<UiState>()
        var closeRequested = false
        var coreUnreachableError: CompanionException? = null
        var signInRequestCount = 0

        override fun postProtocolMessageToPage(message: String) {
            messagesPostedToPage += message
        }

        override fun onPageRequestedClose() {
            closeRequested = true
        }

        override fun onStateChanged(state: UiState) {
            states += state
        }

        override fun onCoreUnreachable(error: CompanionException) {
            coreUnreachableError = error
        }

        override fun onPageRequestedSignIn() {
            signInRequestCount++
        }
    }

    @Test
    fun `sign-in opens the page the core asks for and delivers the account`() {
        val openedUrls = mutableListOf<String>()
        val reportedAccounts = mutableListOf<Account?>()
        val driver = createDriver()
        driver.addAccountListener { account -> reportedAccounts += account }
        val callback = RecordingCallback<Account>()

        driver.signIn(runImmediately, callback) { url -> openedUrls += url }

        val (requestId, secureRandomBytes) = session.signIns.single()
        assertEquals(CoreSession.SIGN_IN_ENTROPY_BYTES, secureRandomBytes.size)
        assertTrue("random, not zeros", secureRandomBytes.any { byte -> byte != 0.toByte() })

        session.actions += SessionAction.OpenSignInPageInBrowser(requestId, "https://id.example/authorize?x")
        session.actions += SessionAction.SecretWrite("auth", "tokens".encodeToByteArray())
        session.actions += SessionAction.AccountChanged(Account("acc_1", "Nikita"))
        session.queueResult(requestId, """{"account":{"id":"acc_1","displayName":"Nikita"}}""")
        driver.signInRedirect("com.example.game://tarkov-companion-auth?code=1&state=2")

        assertEquals(listOf("https://id.example/authorize?x"), openedUrls)
        assertEquals(listOf<String?>("com.example.game://tarkov-companion-auth?code=1&state=2"), session.redirects)
        assertEquals(listOf<Any>(Account("acc_1", "Nikita")), callback.results)
        assertEquals(listOf<Account?>(Account("acc_1", "Nikita")), reportedAccounts)
        assertEquals(Account("acc_1", "Nikita"), driver.lastReportedAccount)
        assertEquals("tokens", secretStore.entries.getValue("auth").decodeToString())
    }

    @Test
    fun `secrets are read from and deleted in the secret store, not the cache`() {
        secretStore.put("auth", "stored".encodeToByteArray())
        session.onSearch = { requestId, _ ->
            session.actions += SessionAction.SecretRead(7, "auth")
            session.actions += SessionAction.SecretDelete("auth")
            session.queueResult(requestId, "{}")
        }

        search(createDriver())

        assertEquals(listOf(7L to "stored"), session.cacheReadCompletions)
        assertTrue(secretStore.entries.isEmpty())
        assertTrue(cacheStore.entries.isEmpty())
    }

    @Test
    fun `favourites use the data store, and their listeners hear about changes`() {
        val reportedLists = mutableListOf<List<FavoriteItem>>()
        val driver = createDriver()
        driver.addFavoritesListener { favorites -> reportedLists += favorites }
        session.onFavoritesList = { requestId ->
            session.actions += SessionAction.DataRead(3, "favorites|local")
            session.actions += SessionAction.DataWrite("favorites|local", "list".encodeToByteArray())
            session.actions += SessionAction.FavoritesChanged(listOf(FavoriteItem("a", "A", "a", "")))
            session.queueResult(requestId, """{"items":[{"id":"a","name":"A","shortName":"a","iconLink":""}]}""")
        }
        val callback = RecordingCallback<List<FavoriteItem>>()

        driver.favorites(runImmediately, callback) { payload ->
            SessionAction.favoriteItemsFromJson(JSONObject(payload.decodeToString()).getJSONArray("items"))
        }

        assertEquals(listOf(3L to null), session.cacheReadCompletions)
        assertEquals("list", dataStore.entries.getValue("favorites|local").decodeToString())
        assertTrue("not the cache", cacheStore.entries.isEmpty())
        assertEquals(listOf(listOf(FavoriteItem("a", "A", "a", ""))), reportedLists)
        assertEquals(listOf<Any>(listOf(FavoriteItem("a", "A", "a", ""))), callback.results)

        session.actions += SessionAction.DataDelete("favorites|local")
        driver.setFavorite("gpu", true, runImmediately, RecordingCallback())

        assertEquals(Triple(2L, "gpu", true), session.favoriteSets.single())
        assertTrue(dataStore.entries.isEmpty())
    }

    @Test
    fun `a sign-in the page asks for goes to the screen`() {
        val host = RecordingBridgeHost()
        session.onBridgeMessage = { bridgeId, _ -> session.actions += SessionAction.PageRequestedSignIn(bridgeId) }

        createDriver().openBridge("{}", host, runImmediately).receiveFromPage("sign_in")

        assertEquals(1, host.signInRequestCount)
    }

    @Test
    fun `bridge messages go to the core unread and its actions come back to the host`() {
        val host = RecordingBridgeHost()
        session.onBridgeMessage = { bridgeId, message ->
            session.actions += SessionAction.BridgeSend(bridgeId, "reply to $message")
            session.actions += SessionAction.BridgeState(bridgeId, UiState(UiState.ROUTE_ITEM, null, "abc"))
            session.actions += SessionAction.Track("verdict_shown", mapOf("verdict" to "keep"))
            session.actions += SessionAction.BridgeClose(bridgeId)
        }

        val connection = createDriver().openBridge("""{"initialState":{"route":"search"}}""", host, runImmediately)
        connection.receiveFromPage("""{"version":1,"id":"h"}""")

        assertEquals(listOf("""{"initialState":{"route":"search"}}"""), session.bridgeOptions)
        assertEquals(listOf(1L to """{"version":1,"id":"h"}"""), session.bridgeMessages)
        assertEquals(listOf("""reply to {"version":1,"id":"h"}"""), host.messagesPostedToPage)
        assertEquals("abc", host.states.single().itemId)
        assertEquals(mapOf("verdict" to "keep"), recordingAnalytics.propertiesOfEventsNamed("verdict_shown").single())
        assertTrue(host.closeRequested)
    }

    @Test
    fun `the bridge is open before the first message, however the pool schedules`() {
        val poolTasks = mutableListOf<Runnable>()
        val driver = createDriver(coreExecutor = { task -> poolTasks += task })

        val connection = driver.openBridge("{}", RecordingBridgeHost(), runImmediately)
        connection.receiveFromPage("first")
        connection.sendEventToPage("navigate_back")
        // Run whatever the pool was given, most recent first: the serial queue keeps the order anyway.
        while (poolTasks.isNotEmpty()) {
            poolTasks.removeAt(poolTasks.lastIndex).run()
        }

        assertEquals(1, session.bridgeOptions.size)
        assertEquals(listOf(1L to "first"), session.bridgeMessages)
        assertEquals(listOf(1L to "navigate_back"), session.bridgeEvents)
    }

    @Test
    fun `nothing reaches the host after close`() {
        val queuedDeliveries = mutableListOf<Runnable>()
        val host = RecordingBridgeHost()
        session.onBridgeMessage = { bridgeId, _ -> session.actions += SessionAction.BridgeSend(bridgeId, "late") }
        val connection = createDriver().openBridge("{}", host, hostCallbackExecutor = { task -> queuedDeliveries += task })
        connection.receiveFromPage("hello")

        // Activity.onDestroy, before the main thread ran the delivery.
        connection.close()
        queuedDeliveries.forEach(Runnable::run)
        connection.receiveFromPage("after close")

        assertTrue(host.messagesPostedToPage.isEmpty())
        assertEquals(listOf(1L), session.closedBridgeIds)
        assertEquals(1, session.bridgeMessages.size)
    }

    @Test
    fun `a core that can't be reached is reported to the screen`() {
        openSession = { throw CompanionException(ErrorCode.NATIVE_UNAVAILABLE, "no .so") }
        val host = RecordingBridgeHost()

        createDriver().openBridge("{}", host, runImmediately)

        assertEquals(ErrorCode.NATIVE_UNAVAILABLE, host.coreUnreachableError?.code)
        assertFalse(host.closeRequested)
    }

    @Test
    fun `the kill switch verdict comes from the core`() {
        session.onSearch = { requestId, _ ->
            session.actions += SessionAction.RemoteFlagsChanged(launchAllowed = false)
            session.queueResult(requestId, "{}")
        }
        val driver = createDriver()
        assertTrue("allowed until the core says otherwise", driver.launchAllowedByKillSwitch)

        search(driver)

        assertFalse(driver.launchAllowedByKillSwitch)
    }

    @Test
    fun `opening the session in the background runs what it queued, e g loading remote flags`() {
        session.actions += SessionAction.CacheRead(1, "remote-flags")
        session.actions += SessionAction.RemoteFlagsChanged(launchAllowed = false)
        val driver = createDriver()

        driver.openSessionInBackground()

        assertEquals(listOf(1L to null), session.cacheReadCompletions)
        assertFalse(driver.launchAllowedByKillSwitch)
    }

    @Test
    fun `actions run in the order the core produced them, even on a pool of threads`() {
        val producedWrites = Collections.synchronizedList(mutableListOf<String>())
        val performedWrites = Collections.synchronizedList(mutableListOf<String>())
        val slowStore = object : KeyValueStore by dataStore {
            override fun put(key: String, value: ByteArray) {
                // Uneven, so writes taken by different threads would finish out of order.
                Thread.sleep((value.size % 3).toLong())
                performedWrites += value.decodeToString()
            }
        }
        session.onSearch = { requestId, _ ->
            for (write in listOf("$requestId-written", "$requestId-written-again-later")) {
                producedWrites += write
                session.actions += SessionAction.DataWrite("favorites|local", write.encodeToByteArray())
            }
            session.queueResult(requestId, "{}")
        }
        val searchCount = 20
        val everyResultDelivered = CountDownLatch(searchCount)
        val pool = Executors.newFixedThreadPool(4)
        val driver = SessionDriver(
            openSession = { session },
            httpTransport = { request -> httpTransport.execute(request) },
            cacheStore = { cacheStore },
            secretStore = { secretStore },
            dataStoreKeptUntilDeleted = { slowStore },
            coreExecutor = pool,
            httpExecutor = pool,
            listenerExecutor = runImmediately,
            logger = recordingLogger.logger,
            analytics = recordingAnalytics.analytics,
            currentProfile = { profile },
        )

        repeat(searchCount) {
            val callback = object : CompanionCallback<String> {
                override fun onSuccess(result: String) = everyResultDelivered.countDown()

                override fun onError(error: CompanionException) = everyResultDelivered.countDown()
            }
            driver.search("salewa", runImmediately, callback) { payload -> payload.decodeToString() }
        }

        assertTrue(everyResultDelivered.await(10, TimeUnit.SECONDS))
        pool.shutdown()
        assertEquals(producedWrites.toList(), performedWrites.toList())
    }

    @Test
    fun `a request still in flight does not hold up the page's messages or cache reads`() {
        val blockedHttpTasks = mutableListOf<Runnable>()
        session.onSearch = { _, _ -> session.actions += httpAction(1) }
        session.onBridgeMessage = { _, _ -> session.actions += SessionAction.CacheRead(2, "present") }
        cacheStore.put("present", "cached".encodeToByteArray())
        val driver = createDriver(httpExecutor = { task -> blockedHttpTasks += task })

        search(driver)
        driver.openBridge("{}", RecordingBridgeHost(), runImmediately).receiveFromPage("search gpu")

        assertEquals(1, blockedHttpTasks.size)
        assertEquals(listOf(1L to "search gpu"), session.bridgeMessages)
        assertEquals(listOf(2L to "cached"), session.cacheReadCompletions)
        assertTrue("the request is still waiting", session.httpCompletions.isEmpty())
    }

    @Test
    fun `a failing store reads as missing and does not fail the request or stop later actions`() {
        val brokenSecretStore = object : SecretStore {
            override fun get(key: String): ByteArray? = throw KeyStoreException("Keystore unavailable")

            override fun put(key: String, value: ByteArray) = throw KeyStoreException("Keystore unavailable")

            override fun delete(key: String) = throw KeyStoreException("Keystore unavailable")
        }
        val fullDisk = object : KeyValueStore by cacheStore {
            override fun put(key: String, value: ByteArray) = throw IOException("no space left")
        }
        session.onSearch = { requestId, _ ->
            session.actions += SessionAction.SecretRead(4, "auth")
            session.actions += SessionAction.SecretWrite("auth", "tokens".encodeToByteArray())
            session.actions += SessionAction.CacheWrite("search|salewa", "body".encodeToByteArray())
            session.actions += SessionAction.Log(LogLevel.INFO, "after the failures")
            session.queueResult(requestId, "found")
        }
        val callback = RecordingCallback<String>()

        SessionDriver(
            openSession = { session },
            httpTransport = { request -> httpTransport.execute(request) },
            cacheStore = { fullDisk },
            secretStore = { brokenSecretStore },
            dataStoreKeptUntilDeleted = { dataStore },
            coreExecutor = runImmediately,
            httpExecutor = runImmediately,
            listenerExecutor = runImmediately,
            logger = recordingLogger.logger,
            analytics = recordingAnalytics.analytics,
            currentProfile = { profile },
        ).search("salewa", runImmediately, callback) { payload -> payload.decodeToString() }

        assertEquals(listOf(4L to null), session.cacheReadCompletions)
        assertTrue(recordingLogger.has(LogLevel.ERROR, "secret store write failed"))
        assertTrue(recordingLogger.has(LogLevel.ERROR, "cache store write failed"))
        assertTrue(recordingLogger.has(LogLevel.INFO, "after the failures"))
        assertEquals(listOf<Any>("found"), callback.results)
    }

    @Test
    fun `an action that can't be read is still answered`() {
        session.onSearch = { _, _ ->
            // Both lack a field this SDK needs.
            session.actions += SessionAction.fromJson("""{"type":"cache_read","id":5}""", null)
            session.actions += SessionAction.fromJson("""{"type":"http","id":6,"url":"https://x"}""", null)
        }

        search(createDriver())

        assertEquals(listOf(5L to null), session.cacheReadCompletions)
        assertEquals(6L, session.httpCompletions.single().first)
        assertEquals(0, session.httpCompletions.single().second)
        assertTrue(recordingLogger.has(LogLevel.ERROR, "could not read an action"))
    }

    private fun httpAction(actionId: Long) =
        SessionAction.Http(actionId, HttpRequest("https://api.example/graphql", "POST", emptyMap(), ByteArray(0), 5_000))
}
