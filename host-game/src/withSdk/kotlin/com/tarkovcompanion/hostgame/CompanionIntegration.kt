package com.tarkovcompanion.hostgame

import android.app.Activity
import android.app.Application
import android.util.Log
import android.widget.Toast
import com.tarkovcompanion.sdk.CompanionConfig
import com.tarkovcompanion.sdk.LaunchResult
import com.tarkovcompanion.sdk.PlayerProfile
import com.tarkovcompanion.sdk.SignInConfig
import com.tarkovcompanion.sdk.TarkovCompanion
import com.tarkovcompanion.sdk.host.LogLevel

// The whole SDK integration. The `baseline` flavor has a no-op twin for footprint measurements.
object CompanionIntegration {
    fun initialize(application: Application) {
        val config = CompanionConfig.Builder()
            .logger { level, message, throwable ->
                val stackTrace = throwable?.let { "\n" + Log.getStackTraceString(throwable) } ?: ""
                Log.println(logcatPriority(level), "RaidSim/Companion", message + stackTrace)
            }
            .analytics { event, properties -> Log.i("RaidSim/Analytics", "$event $properties") }
        DemoSupport.transport(application)?.let(config::httpTransport)
        DemoSupport.remoteFlagsUrl()?.let(config::remoteFlagsUrl)
        DemoSupport.signInIssuer()?.let { issuer ->
            config.signIn(SignInConfig.Builder().issuer(issuer).clientId(DemoSupport.SIGN_IN_CLIENT_ID).build())
        }
        DemoSupport.favoritesSyncUrl()?.let(config::favoritesSyncUrl)
        DemoSupport.catalogUrl()?.let(config::catalogUrl)

        TarkovCompanion.initialize(application, config.build())

        if (DemoSupport.signInIssuer() != null) {
            TarkovCompanion.addAccountListener { account ->
                Log.i("RaidSim", "account: ${account?.displayName ?: "signed out"}")
            }
            TarkovCompanion.addFavoritesListener { favorites ->
                Log.i("RaidSim", "favourites: ${favorites.map { favorite -> favorite.shortName }}")
            }
        }

        TarkovCompanion.setPlayerProfile(PlayerProfile.Builder().level(22).intelligenceCenterLevel(1).build())
    }

    // Loads the overlay's WebView engine ahead of the first tap on the companion button.
    fun prewarm() = TarkovCompanion.prewarm()

    fun open(activity: Activity) {
        when (TarkovCompanion.showItemLookup(activity)) {
            LaunchResult.LAUNCHED -> Unit
            LaunchResult.DISABLED_REMOTELY -> {
                Toast.makeText(activity, "Item lookup is temporarily unavailable", Toast.LENGTH_SHORT).show()
            }
            LaunchResult.NOT_INITIALIZED -> Log.e("RaidSim", "TarkovCompanion not initialized")
        }
    }

    private fun logcatPriority(level: LogLevel) = when (level) {
        LogLevel.DEBUG -> Log.DEBUG
        LogLevel.INFO -> Log.INFO
        LogLevel.WARNING -> Log.WARN
        LogLevel.ERROR -> Log.ERROR
    }
}
