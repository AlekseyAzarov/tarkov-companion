package com.tarkovcompanion.hostgame

import android.app.Activity
import android.app.Application

// Baseline build: same game, no SDK. Exists only so the SDK's footprint can be measured as a diff.
object CompanionIntegration {
    fun initialize(application: Application) = Unit

    fun prewarm() = Unit

    fun open(activity: Activity) = Unit
}
