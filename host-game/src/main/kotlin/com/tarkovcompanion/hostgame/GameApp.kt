package com.tarkovcompanion.hostgame

import android.app.Application

class GameApp : Application() {
    override fun onCreate() {
        super.onCreate()
        CompanionIntegration.initialize(this)
    }
}
