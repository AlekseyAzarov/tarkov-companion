package com.tarkovcompanion.sdk.host

public enum class LogLevel { DEBUG, INFO, WARNING, ERROR }

/** Route SDK logs into the host's logging pipeline. Defaults to logcat. */
public fun interface CompanionLogger {
    public fun log(level: LogLevel, message: String, throwable: Throwable?)
}

/** Product analytics events (docs/analytics.md); names and properties are part of the compatibility promise. */
public fun interface AnalyticsSink {
    public fun track(event: String, properties: Map<String, String>)
}
