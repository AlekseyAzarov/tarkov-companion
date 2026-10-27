package com.tarkovcompanion.sdk.internal

import com.tarkovcompanion.sdk.CompanionConfig
import com.tarkovcompanion.sdk.PlayerProfile
import com.tarkovcompanion.sdk.SignInConfig
import com.tarkovcompanion.sdk.host.LogLevel
import com.tarkovcompanion.sdk.internal.cache.DiskCache
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import com.tarkovcompanion.sdk.model.KeepReason
import com.tarkovcompanion.sdk.model.Verdict
import com.tarkovcompanion.sdk.testing.Fixtures
import com.tarkovcompanion.sdk.testing.RecordingLogger
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class DiskCacheTest {
    @get:Rule val temporaryFolder = TemporaryFolder()

    @Test
    fun `round trips values byte for byte`() {
        val cache = DiskCache(temporaryFolder.root, 8)
        val value = byteArrayOf(0, 10, -1) + "{\"a\":\"line1\nline2\"}".encodeToByteArray()

        cache.put("key", value)

        assertArrayEquals(value, cache.get("key"))
        assertNull(cache.get("other"))
    }

    @Test
    fun `trims to max entries`() {
        val cache = DiskCache(temporaryFolder.root, 3)

        for (index in 1..10) {
            cache.put("key$index", "value$index".encodeToByteArray())
        }

        assertEquals(3, temporaryFolder.root.listFiles { file: File -> file.name.endsWith(".bin") }!!.size)
    }

    @Test
    fun `file names do not leak keys`() {
        DiskCache(temporaryFolder.root, 3).put("search|regular|en|my secret query", "value".encodeToByteArray())

        assertFalse(temporaryFolder.root.listFiles()!!.single().name.contains("secret"))
    }
}

class AnalyticsTest {
    private val recordingLogger = RecordingLogger()
    private val deliveredEventNames = mutableListOf<String>()
    private val analytics = Analytics({ name, _ -> deliveredEventNames += name }, recordingLogger.logger)

    @Test
    fun `catalogued events go out quietly`() {
        analytics.track(GeneratedContract.EVENT_COMPANION_OPENED, GeneratedContract.PROPERTY_RESTORED to false)
        analytics.track("web_item_opened", "anything" to 1)

        assertEquals(listOf("companion_opened", "web_item_opened"), deliveredEventNames)
        assertTrue(recordingLogger.lines.isEmpty())
    }

    @Test
    fun `events outside the catalog are delivered but reported`() {
        analytics.track("companion_teleported")
        analytics.track(GeneratedContract.EVENT_COMPANION_OPENED, "colour" to "red")

        assertEquals(2, deliveredEventNames.size)
        assertTrue(
            recordingLogger.has(LogLevel.ERROR, "'companion_teleported' is not in contracts/analytics-events.json"),
        )
        assertTrue(recordingLogger.has(LogLevel.ERROR, "property [colour]"))
    }

    @Test
    fun `every event has its name constant`() {
        assertEquals(
            setOf(
                GeneratedContract.EVENT_COMPANION_OPENED, GeneratedContract.EVENT_COMPANION_CLOSED,
                GeneratedContract.EVENT_COMPANION_LAUNCH_FAILED, GeneratedContract.EVENT_COMPANION_LAUNCH_BLOCKED,
                GeneratedContract.EVENT_COMPANION_RENDERER_GONE, GeneratedContract.EVENT_BRIDGE_HANDSHAKE,
                GeneratedContract.EVENT_BRIDGE_ERROR, GeneratedContract.EVENT_BRIDGE_REQUEST_FAILED,
                GeneratedContract.EVENT_SEARCH_COMPLETED, GeneratedContract.EVENT_VERDICT_SHOWN,
                GeneratedContract.EVENT_SIGN_IN_COMPLETED, GeneratedContract.EVENT_SIGNED_OUT,
                GeneratedContract.EVENT_FAVORITE_TOGGLED,
            ),
            GeneratedContract.EVENT_PROPERTIES.keys,
        )
    }
}

class ModelMapperTest {
    @Test
    fun `maps the core's golden output onto the public model`() {
        val recommendationJson = Fixtures.read("core_recommendation.json")
        val payload = """{"recommendation":$recommendationJson,"fromCache":true,"hideoutMissing":false}"""

        val recommendation = ModelMapper.recommendationFromResult(payload.encodeToByteArray())

        assertEquals(Verdict.KEEP, recommendation.verdict)
        assertEquals(23615, recommendation.bestNetRubles)
        assertEquals("Therapist", recommendation.bestTrader!!.vendorName)
        assertEquals(1384, recommendation.flea!!.feeRubles)
        assertNull(recommendation.flea.unavailableReason)
        assertEquals(KeepReason.Kind.TASK, recommendation.keepReasons.single().kind)
        assertTrue(recommendation.fromOfflineCache)
    }
}

class ConfigTest {
    @Test
    fun `builders validate input eagerly`() {
        assertEachThrowsIllegalArgument(
            { CompanionConfig.Builder().graphqlEndpoint("http://insecure") },
            { CompanionConfig.Builder().language("english") },
            { CompanionConfig.Builder().itemCacheCapacity(1) },
            { PlayerProfile.Builder().intelligenceCenterLevel(4) },
            { PlayerProfile.Builder().level(0) },
        )
    }

    @Test
    fun `auth config takes https, or http to this machine for development`() {
        SignInConfig.Builder().issuer("https://id.example").clientId("client").build()
        SignInConfig.Builder().issuer("http://10.0.2.2:8787").clientId("client").build()
        SignInConfig.Builder().issuer("http://localhost").clientId("client").build()

        assertEachThrowsIllegalArgument(
            { SignInConfig.Builder().issuer("http://id.example") },
            { SignInConfig.Builder().issuer("http://10.0.2.2.evil.example") },
            { SignInConfig.Builder().clientId(" ") },
            { SignInConfig.Builder().issuer("https://id.example").build() },
            { SignInConfig.Builder().redirectScheme("com.Studio.Game") },
            { SignInConfig.Builder().redirectScheme("my_game") },
            { SignInConfig.Builder().redirectScheme("1game") },
        )
    }

    @Test
    fun `favourites sync url takes https, or http to this machine`() {
        CompanionConfig.Builder().favoritesSyncUrl("https://api.example/favorites/sync")
        CompanionConfig.Builder().favoritesSyncUrl("http://10.0.2.2:8787/favorites/sync")

        assertEachThrowsIllegalArgument({ CompanionConfig.Builder().favoritesSyncUrl("http://api.example/favorites/sync") })
    }

    @Test
    fun `catalog url takes https, or http to this machine`() {
        CompanionConfig.Builder().catalogUrl("https://example.github.io/companion/catalog")
        CompanionConfig.Builder().catalogUrl("http://10.0.2.2:8787/catalog")

        assertEachThrowsIllegalArgument({ CompanionConfig.Builder().catalogUrl("http://catalog.example") })
    }

    @Test
    fun `flea unlock follows level unless overridden`() {
        assertFalse(PlayerProfile.Builder().level(10).build().fleaUnlocked)
        assertTrue(PlayerProfile.Builder().level(15).build().fleaUnlocked)
        assertTrue(PlayerProfile.Builder().level(10).fleaUnlocked(true).build().fleaUnlocked)
    }

    private fun assertEachThrowsIllegalArgument(vararg calls: () -> Unit) {
        for (call in calls) {
            try {
                call()
                fail()
            } catch (_: IllegalArgumentException) {
            }
        }
    }
}
