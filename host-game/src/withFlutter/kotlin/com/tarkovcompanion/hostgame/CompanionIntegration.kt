package com.tarkovcompanion.hostgame

import android.app.Activity
import android.app.Application
import android.util.Log
import com.tarkovcompanion.sdk.CompanionConfig
import com.tarkovcompanion.sdk.PlayerProfile
import com.tarkovcompanion.sdk.TarkovCompanion
import com.tarkovcompanion.sdk.host.LogLevel

/**
 * The Flutter overlay experiment (experiments/flutter_overlay, ADR 0002): the
 * same SDK with the release configuration, and a Flutter screen on its
 * headless API in place of the WebView overlay.
 */
object CompanionIntegration {
    private lateinit var application: Application

    fun initialize(application: Application) {
        this.application = application

        val config = CompanionConfig.Builder()
            .logger { level, message, throwable ->
                val stackTrace = throwable?.let { "\n" + Log.getStackTraceString(throwable) } ?: ""
                Log.println(logcatPriority(level), "RaidSim/Companion", message + stackTrace)
            }
            .analytics { event, properties -> Log.i("RaidSim/Analytics", "$event $properties") }
            .catalogUrl("https://alekseyazarov.github.io/tarkov-companion/catalog")
            .build()
        TarkovCompanion.initialize(application, config)
        TarkovCompanion.setPlayerProfile(PlayerProfile.Builder().level(22).intelligenceCenterLevel(1).build())
    }

    // Starts the Flutter engine and the Dart screen; TarkovCompanion.prewarm() would load the WebView, which this build never shows.
    fun prewarm() {
        FlutterOverlayEngine.obtain(application)
    }

    fun open(activity: Activity) {
        activity.startActivity(FlutterOverlayActivity.intent(activity))
    }

    private fun logcatPriority(level: LogLevel) = when (level) {
        LogLevel.DEBUG -> Log.DEBUG
        LogLevel.INFO -> Log.INFO
        LogLevel.WARNING -> Log.WARN
        LogLevel.ERROR -> Log.ERROR
    }
}
