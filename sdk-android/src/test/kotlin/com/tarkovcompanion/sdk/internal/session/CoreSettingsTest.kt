package com.tarkovcompanion.sdk.internal.session

import com.tarkovcompanion.sdk.Account
import com.tarkovcompanion.sdk.CompanionConfig
import com.tarkovcompanion.sdk.ErrorCode
import com.tarkovcompanion.sdk.GameMode
import com.tarkovcompanion.sdk.PlayerProfile
import com.tarkovcompanion.sdk.SignInConfig
import com.tarkovcompanion.sdk.host.LogLevel
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CoreSettingsTest {
    @Test
    fun `config document sends every key the core knows`() {
        val signInRedirectUri = CoreSettings.signInRedirectUri("com.example.game")

        val json = JSONObject(
            CoreSettings.sessionConfigJson(CompanionConfig.Builder().build(), "UA/1", signInRedirectUri, false),
        )

        assertEquals(GeneratedContract.CONFIG_KEYS, json.keys().asSequence().toSet())
    }

    @Test
    fun `profile document sends every key the core knows`() {
        val json = JSONObject(CoreSettings.profileJson(PlayerProfile.DEFAULT))

        assertEquals(GeneratedContract.PROFILE_KEYS, json.keys().asSequence().toSet())
    }

    @Test
    fun `bridge options send every key the core knows`() {
        val restoredState = UiState(UiState.ROUTE_ITEM, "gpu", "57347ca924597744596b4e71")

        val json = JSONObject(CoreSettings.bridgeOptionsJson(restoredState, millisecondsSinceScreenOpened = 120))

        assertEquals(GeneratedContract.BRIDGE_OPTIONS_KEYS, json.keys().asSequence().toSet())
        assertEquals("item", json.getJSONObject("initialState").getString("route"))
        assertEquals(120, json.getInt("openedMillisecondsAgo"))
    }

    @Test
    fun `values reach the documents`() {
        val companionConfig = CompanionConfig.Builder()
            .graphqlEndpoint("https://proxy.example/gql")
            .language("de")
            .itemCacheCapacity(64)
            .remoteFlagsUrl("https://flags.example/f.json")
            .signIn(SignInConfig.Builder().issuer("https://id.example").clientId("game-123").build())
            .favoritesSyncUrl("https://api.example/favorites/sync")
            .catalogUrl("https://catalog.example/companion")
            .build()
        val signInRedirectUri = CoreSettings.signInRedirectUri("com.example.game")

        val config = JSONObject(CoreSettings.sessionConfigJson(companionConfig, "UA/1", signInRedirectUri, true))

        assertEquals("https://proxy.example/gql", config.getString("endpoint"))
        assertEquals("de", config.getString("language"))
        assertEquals("UA/1", config.getString("userAgent"))
        assertEquals(64, config.getInt("itemCacheCapacity"))
        assertEquals("https://flags.example/f.json", config.getString("remoteFlagsUrl"))
        assertEquals("https://id.example", config.getString("signInIssuer"))
        assertEquals("game-123", config.getString("signInClientId"))
        assertEquals("com.example.game://tarkov-companion-auth", config.getString("signInRedirectUri"))
        assertTrue(config.getBoolean("developmentMode"))
        assertEquals("https://api.example/favorites/sync", config.getString("favoritesSyncUrl"))
        assertEquals("https://catalog.example/companion", config.getString("catalogUrl"))

        val defaults = JSONObject(
            CoreSettings.sessionConfigJson(
                CompanionConfig.Builder().build(),
                "UA",
                CoreSettings.signInRedirectUri("game"),
                false,
            ),
        )
        assertTrue("no flags URL is sent as null, which the core reads as absent", defaults.isNull("remoteFlagsUrl"))

        val playerProfile = PlayerProfile.Builder()
            .level(10)
            .fleaUnlocked(true)
            .gameMode(GameMode.PVE)
            .fleaMarginPercent(12)
            .build()
        val profile = JSONObject(CoreSettings.profileJson(playerProfile))
        assertEquals(10, profile.getInt("playerLevel"))
        assertTrue(profile.getBoolean("fleaUnlocked"))
        assertEquals("pve", profile.getString("gameMode"))
        assertEquals(12, profile.getInt("fleaMarginPercent"))
    }

    @Test
    fun `the redirect scheme is the applicationId only when that can be a scheme`() {
        val signIn = SignInConfig.Builder().issuer("https://id.example").clientId("game-123")
        val withDefaultScheme = CompanionConfig.Builder().signIn(signIn.build()).build()
        val withOwnScheme = CompanionConfig.Builder().signIn(signIn.redirectScheme("studio-game").build()).build()

        assertEquals("com.studio.game", CoreSettings.signInRedirectSchemeOrNull(withDefaultScheme, "com.studio.game"))
        assertNull("uppercase", CoreSettings.signInRedirectSchemeOrNull(withDefaultScheme, "com.Studio.Game"))
        assertNull("underscore", CoreSettings.signInRedirectSchemeOrNull(withDefaultScheme, "com.studio.my_game"))
        assertEquals("studio-game", CoreSettings.signInRedirectSchemeOrNull(withOwnScheme, "com.Studio.Game"))
        assertNull(CoreSettings.signInRedirectSchemeOrNull(CompanionConfig.Builder().build(), "com.studio.game"))
    }

    @Test
    fun `without a usable redirect sign-in is left out of the config`() {
        val companionConfig = CompanionConfig.Builder()
            .signIn(SignInConfig.Builder().issuer("https://id.example").clientId("game-123").build())
            .build()

        val config = JSONObject(CoreSettings.sessionConfigJson(companionConfig, "UA", null, false))

        assertTrue(config.isNull("signInIssuer"))
        assertTrue(config.isNull("signInClientId"))
        assertTrue(config.isNull("signInRedirectUri"))
    }

    @Test
    fun `builder defaults are the contract defaults`() {
        val config = CompanionConfig.Builder().build()

        assertEquals(GeneratedContract.ENDPOINT_DEFAULT, config.graphqlEndpoint)
        assertEquals(GeneratedContract.ITEM_CACHE_CAPACITY_DEFAULT, config.itemCacheCapacity)
        assertEquals(GeneratedContract.PRICE_TIME_TO_LIVE_SECONDS_DEFAULT, config.priceTimeToLiveSeconds)
        assertEquals(GeneratedContract.PLAYER_LEVEL_DEFAULT, PlayerProfile.DEFAULT.level)
        assertEquals(GeneratedContract.GAME_MODE_DEFAULT, PlayerProfile.DEFAULT.gameMode.apiValue)
    }
}

// The action format documented in core/include/tarkov_companion/tarkov_companion.h, as the executor reads it.
class SessionActionTest {
    @Test
    fun `parses http actions with the body taken from the payload`() {
        val actionJson =
            """{"type":"http","id":7,"method":"POST","url":"https://x/graphql",""" +
                """"headers":{"Accept":"application/json"},"timeoutMilliseconds":10000}"""

        val action = SessionAction.fromJson(actionJson, "body".encodeToByteArray()) as SessionAction.Http

        assertEquals(7L, action.actionId)
        assertEquals("POST", action.request.method)
        assertEquals("application/json", action.request.headers["Accept"])
        assertEquals("body", action.request.body!!.decodeToString())
        assertEquals(10_000, action.request.timeoutMilliseconds)
    }

    @Test
    fun `parses results, errors and logs`() {
        val succeeded = SessionAction.fromJson(
            """{"type":"result","request":3,"ok":true}""",
            "{}".encodeToByteArray(),
        ) as SessionAction.Result
        assertEquals("{}", succeeded.payload!!.decodeToString())
        assertNull(succeeded.error)

        val failed = SessionAction.fromJson(
            """{"type":"result","request":3,"ok":false,"error":{"code":"schema_mismatch","message":"shape"}}""",
            null,
        ) as SessionAction.Result
        assertNull(failed.payload)
        assertEquals(ErrorCode.SCHEMA_MISMATCH, failed.error!!.code)

        val log = SessionAction.fromJson("""{"type":"log","level":"warn","message":"m"}""", null) as SessionAction.Log
        assertEquals(LogLevel.WARNING, log.level)
    }

    @Test
    fun `unknown error codes and action types degrade gracefully`() {
        val result = SessionAction.fromJson(
            """{"type":"result","request":1,"ok":false,"error":{"code":"from_the_future","message":"?"}}""",
            null,
        ) as SessionAction.Result

        assertEquals(ErrorCode.INTERNAL, result.error!!.code)
        assertTrue(SessionAction.fromJson("""{"type":"telemetry"}""", null) is SessionAction.UnknownFromNewerCore)
    }

    @Test
    fun `parses bridge and analytics actions`() {
        val send = SessionAction.fromJson("""{"type":"bridge_send","bridge":2}""", "{\"version\":1}".encodeToByteArray())
            as SessionAction.BridgeSend
        assertEquals(2L, send.bridgeId)
        assertEquals("""{"version":1}""", send.message)

        val state = SessionAction.fromJson("""{"type":"bridge_state","bridge":2,"route":"item","itemId":"abc"}""", null)
            as SessionAction.BridgeState
        assertEquals("item", state.state.route)
        assertNull(state.state.query)
        assertEquals("abc", state.state.itemId)

        assertTrue(SessionAction.fromJson("""{"type":"bridge_close","bridge":2}""", null) is SessionAction.BridgeClose)

        val flags = SessionAction.fromJson("""{"type":"flags","launchAllowed":false}""", null)
            as SessionAction.RemoteFlagsChanged
        assertEquals(false, flags.launchAllowed)
    }

    @Test
    fun `parses sign-in actions`() {
        val open = SessionAction.fromJson("""{"type":"open_browser","request":3,"url":"https://id.example/authorize"}""", null)
            as SessionAction.OpenSignInPageInBrowser
        assertEquals(3L, open.requestId)

        val signedIn = SessionAction.fromJson(
            """{"type":"account","signedIn":true,"id":"acc_1","displayName":"Nikita"}""",
            null,
        ) as SessionAction.AccountChanged
        assertEquals(Account("acc_1", "Nikita"), signedIn.accountOrNullIfSignedOut)

        val signedOut = SessionAction.fromJson("""{"type":"account","signedIn":false}""", null)
            as SessionAction.AccountChanged
        assertNull(signedOut.accountOrNullIfSignedOut)

        val secretRead = SessionAction.fromJson("""{"type":"secret_read","id":9,"key":"auth"}""", null)
            as SessionAction.SecretRead
        assertEquals("auth", secretRead.key)

        val secretWrite = SessionAction.fromJson("""{"type":"secret_write","key":"auth"}""", "v".encodeToByteArray())
            as SessionAction.SecretWrite
        assertEquals("v", secretWrite.value.decodeToString())

        assertTrue(SessionAction.fromJson("""{"type":"secret_delete","key":"auth"}""", null) is SessionAction.SecretDelete)
        assertTrue(
            SessionAction.fromJson("""{"type":"bridge_sign_in","bridge":1}""", null) is SessionAction.PageRequestedSignIn,
        )
    }

    @Test
    fun `parses favourites and data store actions`() {
        val favorites = SessionAction.fromJson(
            """{"type":"favorites"}""",
            """[{"id":"gpu","name":"Graphics card","shortName":"GPU","iconLink":"https://x/gpu.webp"}]""".encodeToByteArray(),
        ) as SessionAction.FavoritesChanged
        assertEquals("GPU", favorites.items.single().shortName)
        assertEquals("https://x/gpu.webp", favorites.items.single().iconUrl)

        val dataRead = SessionAction.fromJson("""{"type":"data_read","id":10,"key":"k"}""", null) as SessionAction.DataRead
        assertEquals(10L, dataRead.actionId)

        val dataWrite = SessionAction.fromJson("""{"type":"data_write","key":"k"}""", "v".encodeToByteArray())
            as SessionAction.DataWrite
        assertEquals("v", dataWrite.value.decodeToString())

        assertTrue(SessionAction.fromJson("""{"type":"data_delete","key":"k"}""", null) is SessionAction.DataDelete)

        val failed = SessionAction.fromJson(
            """{"type":"result","request":1,"ok":false,"error":{"code":"sign_in_cancelled","message":"closed"}}""",
            null,
        ) as SessionAction.Result
        assertEquals(ErrorCode.SIGN_IN_CANCELLED, failed.error!!.code)

        val track = SessionAction.fromJson("""{"type":"track","event":"verdict_shown","properties":{"verdict":"keep"}}""", null)
            as SessionAction.Track
        assertEquals(mapOf("verdict" to "keep"), track.properties)
    }
}
