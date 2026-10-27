package com.tarkovcompanion.sdk.testing

import com.tarkovcompanion.sdk.host.LogLevel
import com.tarkovcompanion.sdk.internal.Analytics
import com.tarkovcompanion.sdk.internal.Logger
import com.tarkovcompanion.sdk.internal.cache.KeyValueStore
import com.tarkovcompanion.sdk.internal.secure.SecretStore
import com.tarkovcompanion.sdk.internal.session.CoreSession
import com.tarkovcompanion.sdk.internal.session.SessionAction
import java.io.File
import java.util.concurrent.ConcurrentLinkedQueue

object Fixtures {
    private val directory = File(System.getProperty("tarkov_companion.fixtures") ?: "../contracts/fixtures")

    fun read(name: String): String = File(directory, name).readText()
}

internal class RecordingLogger {
    val lines = mutableListOf<String>()
    val logger = Logger { level, message, _ -> lines += "$level $message" }

    fun has(level: LogLevel, fragment: String) = lines.any { line -> line.startsWith(level.name) && fragment in line }
}

internal class RecordingAnalytics {
    val events = mutableListOf<Pair<String, Map<String, String>>>()
    val analytics = Analytics({ name, properties -> events += name to properties }, RecordingLogger().logger)

    fun propertiesOfEventsNamed(name: String) =
        events.filter { (eventName, _) -> eventName == name }.map { (_, properties) -> properties }
}

/**
 * Scripted stand-in for the core session (no .so on the JVM). Tests decide
 * which actions it hands out in reaction to commands and completions, and
 * read back what the executor reported.
 */
internal class FakeSession : CoreSession {
    val actions = ConcurrentLinkedQueue<SessionAction>()
    val profiles = mutableListOf<String>()
    val cancelledRequestIds = mutableListOf<Long>()
    val httpCompletions = mutableListOf<Triple<Long, Int, String>>()
    val cacheReadCompletions = mutableListOf<Pair<Long, String?>>()
    val bridgeOptions = mutableListOf<String>()
    val bridgeMessages = mutableListOf<Pair<Long, String>>()
    val bridgeEvents = mutableListOf<Pair<Long, String>>()
    val closedBridgeIds = mutableListOf<Long>()
    val signIns = mutableListOf<Pair<Long, ByteArray>>()
    val redirects = mutableListOf<String?>()
    val signOutRequestIds = mutableListOf<Long>()
    val favoriteSets = mutableListOf<Triple<Long, String, Boolean>>()

    var onFavoritesList: (requestId: Long) -> Unit = { }
    var onSearch: (requestId: Long, query: String) -> Unit = { _, _ -> }
    var onRecommend: (requestId: Long, itemId: String) -> Unit = { _, _ -> }
    var onHttpCompleted: (actionId: Long, statusCode: Int, body: ByteArray) -> Unit = { _, _, _ -> }
    var onCacheReadCompleted: (actionId: Long, value: ByteArray?) -> Unit = { _, _ -> }
    var onBridgeMessage: (bridgeId: Long, message: String) -> Unit = { _, _ -> }

    override fun setProfile(profileJson: String) {
        profiles += profileJson
    }

    override fun search(requestId: Long, query: String) = onSearch(requestId, query)

    override fun recommend(requestId: Long, itemId: String) = onRecommend(requestId, itemId)

    override fun cancel(requestId: Long) {
        cancelledRequestIds += requestId
    }

    override fun nextActionOrNullWhenIdle(): SessionAction? = actions.poll()

    override fun completeHttp(actionId: Long, statusCodeOrZeroIfNoResponse: Int, bodyOrFailureReason: ByteArray) {
        httpCompletions += Triple(actionId, statusCodeOrZeroIfNoResponse, bodyOrFailureReason.decodeToString())
        onHttpCompleted(actionId, statusCodeOrZeroIfNoResponse, bodyOrFailureReason)
    }

    override fun completeCacheRead(actionId: Long, valueOrNullIfMissing: ByteArray?) {
        cacheReadCompletions += actionId to valueOrNullIfMissing?.decodeToString()
        onCacheReadCompleted(actionId, valueOrNullIfMissing)
    }

    override fun openBridgeAndGetId(optionsJson: String): Long {
        bridgeOptions += optionsJson

        return bridgeOptions.size.toLong()
    }

    override fun bridgeReceive(bridgeId: Long, message: String) {
        bridgeMessages += bridgeId to message
        onBridgeMessage(bridgeId, message)
    }

    override fun bridgeEvent(bridgeId: Long, eventName: String) {
        bridgeEvents += bridgeId to eventName
    }

    override fun closeBridge(bridgeId: Long) {
        closedBridgeIds += bridgeId
    }

    override fun signIn(requestId: Long, secureRandomBytes: ByteArray) {
        signIns += requestId to secureRandomBytes
    }

    override fun signInRedirect(redirectUriOrNullIfClosed: String?) {
        redirects += redirectUriOrNullIfClosed
    }

    override fun signOut(requestId: Long) {
        signOutRequestIds += requestId
    }

    override fun favoritesList(requestId: Long) = onFavoritesList(requestId)

    override fun favoriteSet(requestId: Long, itemId: String, favorite: Boolean) {
        favoriteSets += Triple(requestId, itemId, favorite)
        queueResult(requestId, "{}")
    }

    fun queueResult(requestId: Long, payload: String) {
        actions += SessionAction.Result(requestId, payload.encodeToByteArray(), null)
    }
}

internal class MemoryStore : KeyValueStore {
    val entries = mutableMapOf<String, ByteArray>()

    override fun get(key: String): ByteArray? = entries[key]

    override fun put(key: String, value: ByteArray) {
        entries[key] = value
    }

    override fun delete(key: String) {
        entries.remove(key)
    }
}

internal class MemorySecretStore : SecretStore {
    val entries = mutableMapOf<String, ByteArray>()

    override fun get(key: String): ByteArray? = entries[key]

    override fun put(key: String, value: ByteArray) {
        entries[key] = value
    }

    override fun delete(key: String) {
        entries.remove(key)
    }
}
