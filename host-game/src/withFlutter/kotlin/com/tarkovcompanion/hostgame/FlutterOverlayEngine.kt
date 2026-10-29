package com.tarkovcompanion.hostgame

import android.content.Context
import android.os.SystemClock
import android.util.Log
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.embedding.engine.dart.DartExecutor
import io.flutter.plugin.common.MethodChannel

/**
 * One engine for the process, started by prewarm() or the first open and then
 * kept, as Chromium stays loaded after the WebView overlay closes. The Dart
 * screen keeps its state between opens because of that.
 */
object FlutterOverlayEngine {
    private const val CHANNEL_NAME = "tarkov_companion/overlay"
    private const val LOG_TAG = "RaidSim/FlutterOverlay"

    private var engine: FlutterEngine? = null

    val isStarted: Boolean
        get() = engine != null

    // Main thread.
    fun obtain(context: Context): FlutterEngine {
        engine?.let { startedEngine -> return startedEngine }

        val startedAtElapsedMilliseconds = SystemClock.elapsedRealtime()
        val startedEngine = FlutterEngine(context.applicationContext)
        MethodChannel(startedEngine.dartExecutor.binaryMessenger, CHANNEL_NAME)
            .setMethodCallHandler(HeadlessCompanionHandler())
        startedEngine.dartExecutor.executeDartEntrypoint(DartExecutor.DartEntrypoint.createDefault())
        engine = startedEngine

        val mainThreadMilliseconds = SystemClock.elapsedRealtime() - startedAtElapsedMilliseconds
        Log.i(LOG_TAG, "engine started in $mainThreadMilliseconds ms on the main thread")

        return startedEngine
    }
}
