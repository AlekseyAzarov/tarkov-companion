package com.tarkovcompanion.sdk.internal.session

import com.tarkovcompanion.sdk.CompanionException
import com.tarkovcompanion.sdk.ErrorCode
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract

/** Bound by RegisterNatives: names and signatures must match kMethods in jni_bridge.cpp. */
internal object NativeBridge {
    // tarkov_companion_status values.
    const val STATUS_OK = 0
    const val STATUS_NO_ACTION = 2

    const val OUTPUT_SLOT_ACTION_OR_ERROR = 0
    const val OUTPUT_SLOT_PAYLOAD = 1
    const val OUTPUT_SLOT_COUNT = 2

    @JvmStatic external fun nativeVersion(): String

    @JvmStatic external fun nativeAbiVersion(): Int

    @JvmStatic external fun createSession(configJson: ByteArray, errorOutput: Array<ByteArray?>): Long

    @JvmStatic external fun destroySession(sessionHandle: Long)

    @JvmStatic external fun setProfile(sessionHandle: Long, profileJson: ByteArray, errorOutput: Array<ByteArray?>): Int

    @JvmStatic external fun search(sessionHandle: Long, requestId: Long, query: ByteArray): Int

    @JvmStatic external fun recommend(sessionHandle: Long, requestId: Long, itemId: ByteArray): Int

    @JvmStatic external fun cancel(sessionHandle: Long, requestId: Long)

    @JvmStatic external fun nextAction(sessionHandle: Long, actionAndPayloadOutput: Array<ByteArray?>): Int

    @JvmStatic external fun completeHttp(sessionHandle: Long, actionId: Long, statusCode: Int, body: ByteArray): Int

    @JvmStatic external fun completeCacheRead(
        sessionHandle: Long,
        actionId: Long,
        found: Boolean,
        value: ByteArray?,
    ): Int

    @JvmStatic external fun openBridge(
        sessionHandle: Long,
        optionsJson: ByteArray,
        errorOutput: Array<ByteArray?>,
    ): Long

    @JvmStatic external fun bridgeReceive(sessionHandle: Long, bridgeId: Long, message: ByteArray): Int

    @JvmStatic external fun bridgeEvent(sessionHandle: Long, bridgeId: Long, eventName: ByteArray): Int

    @JvmStatic external fun closeBridge(sessionHandle: Long, bridgeId: Long)

    @JvmStatic external fun signIn(sessionHandle: Long, requestId: Long, secureRandomBytes: ByteArray): Int

    @JvmStatic external fun signInRedirect(sessionHandle: Long, redirectUriOrNullIfClosed: ByteArray?): Int

    @JvmStatic external fun signOut(sessionHandle: Long, requestId: Long): Int

    @JvmStatic external fun favoritesList(sessionHandle: Long, requestId: Long): Int

    @JvmStatic external fun favoriteSet(sessionHandle: Long, requestId: Long, itemId: ByteArray, favorite: Boolean): Int
}

/** Loads the library on first use, not at SDK init. */
internal object NativeLibrary {
    const val EXPECTED_ABI_VERSION = GeneratedContract.NATIVE_ABI_VERSION
    private const val LIBRARY_NAME = "tarkovcompanion"

    @Volatile private var rememberedLoadFailure: Throwable? = null

    @Volatile private var loaded = false

    @Synchronized
    fun ensureLoaded() {
        if (loaded) {
            return
        }

        rememberedLoadFailure?.let { previousFailure -> throw nativeUnavailableException(previousFailure) }

        try {
            System.loadLibrary(LIBRARY_NAME)
            val coreAbiVersion = NativeBridge.nativeAbiVersion()
            check(coreAbiVersion == EXPECTED_ABI_VERSION) {
                "core ABI $coreAbiVersion, SDK expects $EXPECTED_ABI_VERSION"
            }
            loaded = true
        } catch (failure: Throwable) {
            // UnsatisfiedLinkError: the host APK lacks this device's ABI, or a split install is broken.
            rememberedLoadFailure = failure
            throw nativeUnavailableException(failure)
        }
    }

    private fun nativeUnavailableException(cause: Throwable) =
        CompanionException(ErrorCode.NATIVE_UNAVAILABLE, "Native core unavailable: ${cause.message}", cause)
}
