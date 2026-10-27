package com.tarkovcompanion.sdk

import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import com.tarkovcompanion.sdk.internal.session.CoreSession
import com.tarkovcompanion.sdk.internal.session.CoreSettings
import com.tarkovcompanion.sdk.internal.session.NativeBridge
import com.tarkovcompanion.sdk.internal.session.NativeSession
import com.tarkovcompanion.sdk.internal.session.SessionAction
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith

// The real .so on a real ABI: JNI registration, byte[] marshalling, payloads, error reporting.
@RunWith(AndroidJUnit4::class)
class NativeSessionInstrumentedTest {
    private fun fixture(name: String) = InstrumentationRegistry.getInstrumentation().context.assets
        .open(name)
        .bufferedReader()
        .use { reader -> reader.readText() }

    private fun openSession(config: CompanionConfig = CompanionConfig.Builder().build()): NativeSession {
        val signInRedirectUri = CoreSettings.signInRedirectUri("com.example.game")

        return NativeSession.loadLibraryAndCreate(
            CoreSettings.sessionConfigJson(config, "test", signInRedirectUri, false),
        )
    }

    private fun answerHttpFromFixturesAndCacheFromNothingUntilResult(
        session: NativeSession,
        requestId: Long,
    ): SessionAction.Result {
        while (true) {
            when (val action = session.nextActionOrNullWhenIdle() ?: fail("no result for $requestId")) {
                is SessionAction.Http -> {
                    val query = JSONObject(action.request.body!!.decodeToString()).getString("query")
                    val fixtureName = if (query.startsWith("query Hideout")) "hideout.json" else "item_search.json"
                    session.completeHttp(action.actionId, HTTP_OK, fixture(fixtureName).encodeToByteArray())
                }
                is SessionAction.CacheRead -> session.completeCacheRead(action.actionId, null)
                is SessionAction.Result -> {
                    if (action.requestId == requestId) {
                        return action
                    }
                }
                else -> Unit
            }
        }
    }

    @Test
    fun recommendationMatchesTheGoldenContract() {
        val session = openSession()
        session.search(1, "salewa")
        answerHttpFromFixturesAndCacheFromNothingUntilResult(session, 1)

        session.recommend(2, "544fb45d4bdc2dee738b4568")
        val payload = answerHttpFromFixturesAndCacheFromNothingUntilResult(session, 2).payload!!.decodeToString()

        // Bytes, not parsed documents: the golden file pins the exact output.
        val golden = fixture("core_recommendation.json").trim()
        assertEquals("""{"recommendation":$golden,"fromCache":false,"hideoutMissing":false}""", payload)
    }

    @Test
    fun nonBmpCharactersSurviveTheJniBoundary() {
        // jstring would mangle these (modified UTF-8); the bridge passes byte[].
        val session = openSession()
        session.search(1, "аптечка 😀")

        // Past the start-up read of the favourites, to the search's request.
        val http = generateSequence { session.nextActionOrNullWhenIdle() }
            .onEach { action ->
                if (action is SessionAction.DataRead) {
                    session.completeCacheRead(action.actionId, null)
                }
            }
            .filterIsInstance<SessionAction.Http>()
            .first()
        val variables = JSONObject(http.request.body!!.decodeToString()).getJSONObject("variables")
        assertEquals("аптечка 😀", variables.getString("name"))

        val response = """{"data":{"items":[{"id":"x1","name":"Аптечка 😀 \u0000ok","shortName":"А"}]}}"""
        session.completeHttp(http.actionId, HTTP_OK, response.encodeToByteArray())
        val result = answerHttpFromFixturesAndCacheFromNothingUntilResult(session, 1)
        val item = JSONObject(result.payload!!.decodeToString()).getJSONArray("items").getJSONObject(0)
        assertEquals("Аптечка 😀 \u0000ok", item.getString("name"))
    }

    @Test
    fun errorsAreReportedInResults() {
        val session = openSession()

        session.search(1, "x")
        val result = answerHttpFromFixturesAndCacheFromNothingUntilResult(session, 1)

        assertEquals(ErrorCode.INVALID_ARGUMENT, result.error!!.code)
    }

    @Test
    fun invalidConfigIsRejectedWithAReason() {
        try {
            NativeSession.loadLibraryAndCreate("""{"language":"english"}""")
            fail("expected the core to reject the config")
        } catch (error: CompanionException) {
            assertTrue(error.message!!, error.message!!.contains("language"))
        }
    }

    @Test
    fun profileReachesTheCore() {
        val session = openSession()
        session.search(1, "hose")
        answerHttpFromFixturesAndCacheFromNothingUntilResult(session, 1)
        val hoseItemId = "59e35cbb86f7741778269d83"

        // The water collector needs hoses: without ignoreKeepReasons the verdict is always "keep".
        session.setProfile(CoreSettings.profileJson(PlayerProfile.Builder().level(5).ignoreKeepReasons(true).build()))
        session.recommend(2, hoseItemId)
        val payload = answerHttpFromFixturesAndCacheFromNothingUntilResult(session, 2).payload!!.decodeToString()

        val recommendation = JSONObject(payload).getJSONObject("recommendation")
        assertEquals("sell_trader", recommendation.getString("verdict"))
        assertEquals("flea_locked", recommendation.getJSONObject("flea").getString("unavailableReason"))
    }

    @Test
    fun bridgeAnswersTheHandshakeAndASearch() {
        val session = openSession()
        val bridgeId = session.openBridgeAndGetId("""{"initialState":{"route":"search"}}""")
        val messagesSentToPage = mutableListOf<String>()

        fun receiveFromPage(message: String) {
            session.bridgeReceive(bridgeId, message)
            while (true) {
                when (val action = session.nextActionOrNullWhenIdle() ?: break) {
                    is SessionAction.Http -> {
                        session.completeHttp(action.actionId, HTTP_OK, fixture("item_search.json").encodeToByteArray())
                    }
                    is SessionAction.CacheRead -> session.completeCacheRead(action.actionId, null)
                    is SessionAction.BridgeSend -> messagesSentToPage += action.message
                    else -> Unit
                }
            }
        }

        receiveFromPage(
            """{"version":1,"id":"h","type":"hello",""" +
                """"payload":{"protocol":1,"minimumNativeProtocol":1,"bundleVersion":"0.1.0"}}""",
        )
        val hello = JSONObject(messagesSentToPage.single())
        assertEquals(GeneratedContract.SDK_VERSION, hello.getJSONObject("payload").getString("sdkVersion"))

        receiveFromPage("""{"version":1,"id":"w1","type":"search","payload":{"query":"аптечка 😀"}}""")
        val reply = JSONObject(messagesSentToPage.last())
        assertEquals("w1", reply.getString("replyTo"))
        assertEquals(4, reply.getJSONObject("payload").getJSONArray("items").length())

        session.closeBridge(bridgeId)
    }

    @Test
    fun remoteFlagsAreFetchedWithGetAndReportTheKillSwitch() {
        val remoteFlagsUrl = "https://flags.example/companion.json"
        val session = openSession(CompanionConfig.Builder().remoteFlagsUrl(remoteFlagsUrl).build())
        var launchAllowed: Boolean? = null

        while (true) {
            when (val action = session.nextActionOrNullWhenIdle() ?: break) {
                is SessionAction.CacheRead -> session.completeCacheRead(action.actionId, null)
                is SessionAction.Http -> {
                    assertEquals("GET", action.request.method)
                    assertEquals(remoteFlagsUrl, action.request.url)
                    val flags = """{"schema":1,"webFlowEnabled":false}"""
                    session.completeHttp(action.actionId, HTTP_OK, flags.encodeToByteArray())
                }
                is SessionAction.RemoteFlagsChanged -> launchAllowed = action.launchAllowed
                else -> Unit
            }
        }

        assertEquals(false, launchAllowed)
    }

    @Test
    fun signInOpensTheBrowserWithAPkceChallenge() {
        val config = CompanionConfig.Builder()
            .signIn(SignInConfig.Builder().issuer("https://id.example").clientId("game-123").build())
            .build()
        val session = openSession(config)
        var signInPageUrl: String? = null

        session.signIn(1, ByteArray(CoreSession.SIGN_IN_ENTROPY_BYTES) { index -> index.toByte() })
        while (true) {
            when (val action = session.nextActionOrNullWhenIdle() ?: break) {
                is SessionAction.SecretRead -> session.completeCacheRead(action.actionId, null)
                is SessionAction.OpenSignInPageInBrowser -> signInPageUrl = action.url
                else -> Unit
            }
        }

        val page = Uri.parse(signInPageUrl)
        assertEquals("/authorize", page.path)
        assertEquals("S256", page.getQueryParameter("code_challenge_method"))
        assertEquals("com.example.game://tarkov-companion-auth", page.getQueryParameter("redirect_uri"))
        assertEquals(43, page.getQueryParameter("code_challenge")!!.length)
    }

    @Test
    fun favouritesKeepNamesFromTheCatalogAndSurviveARestart() {
        val dataStore = mutableMapOf<String, ByteArray>()

        fun runUntilResult(session: NativeSession, requestId: Long): SessionAction.Result {
            while (true) {
                when (val action = session.nextActionOrNullWhenIdle() ?: fail("no result for $requestId")) {
                    is SessionAction.Http -> {
                        session.completeHttp(action.actionId, HTTP_OK, fixture("item_search.json").encodeToByteArray())
                    }
                    is SessionAction.CacheRead -> session.completeCacheRead(action.actionId, null)
                    is SessionAction.DataRead -> session.completeCacheRead(action.actionId, dataStore[action.key])
                    is SessionAction.DataWrite -> dataStore[action.key] = action.value
                    is SessionAction.Result -> {
                        if (action.requestId == requestId) {
                            return action
                        }
                    }
                    else -> Unit
                }
            }
        }

        val session = openSession()
        session.search(1, "gpu")
        runUntilResult(session, 1)
        session.favoriteSet(2, "57347ca924597744596b4e71", true)
        assertNull(runUntilResult(session, 2).error)

        val restartedSession = openSession()
        restartedSession.favoritesList(3)
        val favorites = JSONObject(runUntilResult(restartedSession, 3).payload!!.decodeToString()).getJSONArray("items")
        assertEquals("Graphics card", favorites.getJSONObject(0).getString("name"))
    }

    @Test
    fun versionComesFromTheSameFileAsTheSdk() {
        openSession()

        assertEquals(GeneratedContract.SDK_VERSION, NativeBridge.nativeVersion())
        assertNotNull(NativeBridge.nativeAbiVersion())
    }

    private companion object {
        const val HTTP_OK = 200
    }
}
