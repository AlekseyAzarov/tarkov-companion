package com.tarkovcompanion.sdk.internal

import com.tarkovcompanion.sdk.host.AnalyticsSink
import com.tarkovcompanion.sdk.host.CompanionLogger
import com.tarkovcompanion.sdk.host.LogLevel
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract

// Never lets a host logger/analytics bug take the SDK (or the game) down.
internal class Logger(private val hostLogger: CompanionLogger) {
    fun debug(message: String) = log(LogLevel.DEBUG, message, null)

    fun info(message: String) = log(LogLevel.INFO, message, null)

    fun warn(message: String, throwable: Throwable? = null) = log(LogLevel.WARNING, message, throwable)

    fun error(message: String, throwable: Throwable? = null) = log(LogLevel.ERROR, message, throwable)

    fun log(level: LogLevel, message: String, throwable: Throwable? = null) {
        try {
            hostLogger.log(level, message, throwable)
        } catch (_: Throwable) {
        }
    }
}

internal class Analytics(private val hostAnalyticsSink: AnalyticsSink?, internal val logger: Logger) {
    fun track(event: String, vararg properties: Pair<String, Any?>) {
        val nonNullProperties = properties
            .filter { (_, value) -> value != null }
            .associate { (name, value) -> name to value.toString() }

        track(event, nonNullProperties)
    }

    fun track(event: String, properties: Map<String, String>) {
        logErrorIfNotInCatalog(event, properties)

        val sink = hostAnalyticsSink ?: return
        try {
            sink.track(event, properties)
        } catch (failure: Throwable) {
            logger.warn("analytics sink threw on '$event'", failure)
        }
    }
}

/** An event outside contracts/analytics-events.json is delivered but logged as an error; web events are free-form. */
private fun Analytics.logErrorIfNotInCatalog(event: String, properties: Map<String, String>) {
    if (event.startsWith(GeneratedContract.WEB_EVENT_PREFIX)) {
        return
    }

    val allowedProperties = GeneratedContract.EVENT_PROPERTIES[event]
    if (allowedProperties == null) {
        logger.error("analytics event '$event' is not in contracts/analytics-events.json")
        return
    }

    val unknownProperties = properties.keys - allowedProperties
    if (unknownProperties.isNotEmpty()) {
        logger.error("analytics event '$event' property $unknownProperties is not in contracts/analytics-events.json")
    }
}

internal object LogcatLogger : CompanionLogger {
    private const val TAG = "TarkovCompanion"

    override fun log(level: LogLevel, message: String, throwable: Throwable?) {
        when (level) {
            LogLevel.DEBUG -> android.util.Log.d(TAG, message, throwable)
            LogLevel.INFO -> android.util.Log.i(TAG, message, throwable)
            LogLevel.WARNING -> android.util.Log.w(TAG, message, throwable)
            LogLevel.ERROR -> android.util.Log.e(TAG, message, throwable)
        }
    }
}
