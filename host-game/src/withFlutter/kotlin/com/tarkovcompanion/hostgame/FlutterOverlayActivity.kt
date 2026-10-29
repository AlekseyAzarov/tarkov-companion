package com.tarkovcompanion.hostgame

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.SystemClock
import android.util.Log
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.android.FlutterActivityLaunchConfigs.BackgroundMode
import io.flutter.embedding.engine.FlutterEngine

/**
 * The Flutter counterpart of the SDK's CompanionActivity: a translucent
 * activity over the game, on the process-wide engine. Logs how long the
 * screen took to appear, for docs/footprint.md.
 */
class FlutterOverlayActivity : FlutterActivity() {
    private var createdAtElapsedMilliseconds = 0L
    private var engineWasStarted = false

    override fun onCreate(savedInstanceState: Bundle?) {
        createdAtElapsedMilliseconds = SystemClock.elapsedRealtime()
        engineWasStarted = FlutterOverlayEngine.isStarted
        super.onCreate(savedInstanceState)
    }

    override fun provideFlutterEngine(context: Context): FlutterEngine = FlutterOverlayEngine.obtain(context)

    override fun shouldDestroyEngineWithHost(): Boolean = false

    override fun getBackgroundMode(): BackgroundMode = BackgroundMode.transparent

    override fun onFlutterUiDisplayed() {
        super.onFlutterUiDisplayed()
        val millisecondsSinceCreated = SystemClock.elapsedRealtime() - createdAtElapsedMilliseconds
        val engineState = if (engineWasStarted) "started" else "cold"
        Log.i(LOG_TAG, "first frame $millisecondsSinceCreated ms after onCreate, engine $engineState")
    }

    companion object {
        private const val LOG_TAG = "RaidSim/FlutterOverlay"

        fun intent(context: Context): Intent = Intent(context, FlutterOverlayActivity::class.java)
    }
}
