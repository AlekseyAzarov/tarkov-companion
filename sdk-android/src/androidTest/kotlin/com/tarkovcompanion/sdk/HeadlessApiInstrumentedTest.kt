package com.tarkovcompanion.sdk

import android.os.Looper
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.tarkovcompanion.sdk.host.HttpResponse
import com.tarkovcompanion.sdk.host.HttpTransport
import com.tarkovcompanion.sdk.model.ItemSummary
import com.tarkovcompanion.sdk.model.Recommendation
import com.tarkovcompanion.sdk.model.Verdict
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.BeforeClass
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * The public API end to end on a device: initialize -> search -> verdict,
 * through the session executor, disk cache, native core and main-thread delivery.
 * The network is a host-provided HttpTransport serving recorded fixtures.
 */
@RunWith(AndroidJUnit4::class)
class HeadlessApiInstrumentedTest {
    companion object {
        private const val CALLBACK_TIMEOUT_SECONDS = 10L
        private val assets = InstrumentationRegistry.getInstrumentation().context.assets

        private fun fixture(name: String) = assets.open(name).bufferedReader().use { reader -> reader.readText() }

        @JvmStatic
        @BeforeClass
        fun initializeSdkWithFixtureTransport() {
            val transport = HttpTransport { request ->
                val query = JSONObject(request.body!!.decodeToString()).getString("query")
                val body = if (query.startsWith("query Hideout")) fixture("hideout.json") else fixture("item_search.json")
                HttpResponse(200, emptyMap(), body.encodeToByteArray())
            }

            TarkovCompanion.initialize(
                InstrumentationRegistry.getInstrumentation().targetContext,
                CompanionConfig.Builder().httpTransport(transport).build(),
            )
        }
    }

    private fun <T> awaitSuccess(call: (CompanionCallback<T>) -> Cancellable): T {
        val delivered = CountDownLatch(1)
        val resultOrError = AtomicReference<Any>()
        val deliveredOnMainThread = AtomicReference<Boolean>()

        call(object : CompanionCallback<T> {
            override fun onSuccess(result: T) = record(result)

            override fun onError(error: CompanionException) = record(error)

            private fun record(value: Any?) {
                deliveredOnMainThread.set(Looper.myLooper() == Looper.getMainLooper())
                resultOrError.set(value)
                delivered.countDown()
            }
        })

        assertTrue("callback timed out", delivered.await(CALLBACK_TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertTrue("callbacks are delivered on the main thread by default", deliveredOnMainThread.get())

        val error = resultOrError.get() as? CompanionException
        if (error != null) {
            throw AssertionError("failed: ${error.code}", error)
        }

        @Suppress("UNCHECKED_CAST")
        return resultOrError.get() as T
    }

    @Test
    fun searchThenVerdict() {
        val items: List<ItemSummary> = awaitSuccess { callback -> TarkovCompanion.searchItems("salewa", callback) }
        assertEquals("Salewa first aid kit", items.first().name)

        TarkovCompanion.setPlayerProfile(PlayerProfile.Builder().level(30).build())
        val graphicsCard: Recommendation = awaitSuccess { callback ->
            TarkovCompanion.getRecommendation("57347ca924597744596b4e71", callback)
        }

        assertEquals(Verdict.KEEP, graphicsCard.verdict)
        assertEquals("Bitcoin farm L2", graphicsCard.keepReasons.single().label)
        assertTrue(graphicsCard.flea!!.netRubles > graphicsCard.bestTrader!!.priceRubles)
    }

    @Test
    fun prewarmFromAnyThreadIsHarmlessAndOnlyOnce() {
        // From the test thread it is posted to the main thread.
        TarkovCompanion.prewarm()
        InstrumentationRegistry.getInstrumentation().runOnMainSync {
            TarkovCompanion.prewarm()
            TarkovCompanion.prewarm()
        }
        InstrumentationRegistry.getInstrumentation().waitForIdleSync()

        // The SDK still works after it, the native core included.
        val items: List<ItemSummary> = awaitSuccess { callback -> TarkovCompanion.searchItems("salewa", callback) }
        assertEquals("Salewa first aid kit", items.first().name)
    }

    @Test
    fun invalidInputIsReportedNotThrown() {
        val delivered = CountDownLatch(1)
        var errorCode: ErrorCode? = null

        TarkovCompanion.searchItems("x", object : CompanionCallback<List<ItemSummary>> {
            override fun onSuccess(result: List<ItemSummary>) = delivered.countDown()

            override fun onError(error: CompanionException) {
                errorCode = error.code
                delivered.countDown()
            }
        })

        assertTrue(delivered.await(CALLBACK_TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertEquals(ErrorCode.INVALID_ARGUMENT, errorCode)
    }
}
