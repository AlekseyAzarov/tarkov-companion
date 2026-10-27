package com.tarkovcompanion.sdk

/** Result callback. Exactly one method is called, at most once, unless the call was cancelled. */
public interface CompanionCallback<in T> {
    public fun onSuccess(result: T)

    public fun onError(error: CompanionException)
}

public fun interface Cancellable {
    /** After this returns, no callback for the operation will be delivered. Idempotent. */
    public fun cancel()
}

/**
 * Why an operation failed. New codes are added as the SDK grows (that is not
 * a breaking change): keep an `else` branch when you `when` over them.
 */
public enum class ErrorCode {
    NOT_INITIALIZED,
    INVALID_ARGUMENT,

    /** Transport failure and nothing usable in the offline cache. */
    NETWORK,

    /** tarkov.dev answered with an error (outage, rate limit). */
    API,

    /** The response no longer matches the expected schema: the upstream API changed. */
    SCHEMA_MISMATCH,
    NOT_FOUND,

    /** The native core could not be loaded, usually an ABI missing from the host APK. */
    NATIVE_UNAVAILABLE,
    INTERNAL,

    /** The player closed or declined the sign-in page, or started another sign-in. */
    SIGN_IN_CANCELLED,

    /** The identity server refused the sign-in or answered something unusable. */
    SIGN_IN_FAILED,

    /** The operation needs a signed-in player, or the session expired. */
    NOT_SIGNED_IN,
}

public class CompanionException internal constructor(
    public val code: ErrorCode,
    message: String,
    cause: Throwable? = null,
) : Exception(message, cause)

public enum class LaunchResult {
    LAUNCHED,
    NOT_INITIALIZED,

    /** Turned off by the remote kill switch. The host should hide its entry point. */
    DISABLED_REMOTELY,
}
