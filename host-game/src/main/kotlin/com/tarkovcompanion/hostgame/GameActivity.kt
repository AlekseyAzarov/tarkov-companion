package com.tarkovcompanion.hostgame

import android.app.NativeActivity

/** The platform layer of a C++ game (src/main/cpp/game_main.cpp) on NativeActivity. */
class GameActivity : NativeActivity() {
    private var companionPrewarmed = false

    /**
     * The button is always on screen, so prewarm now; a game with the button in
     * a menu would prewarm as the menu opens. Rendering is on its own thread,
     * so the main thread's pause drops no frames.
     */
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus && !companionPrewarmed) {
            companionPrewarmed = true
            CompanionIntegration.prewarm()
        }
    }

    /** Called from the game thread via JNI when the in-game Companion button is tapped. */
    fun openCompanionFromNative() {
        runOnUiThread { CompanionIntegration.open(this) }
    }
}
