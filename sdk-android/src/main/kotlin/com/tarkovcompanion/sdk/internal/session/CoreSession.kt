package com.tarkovcompanion.sdk.internal.session

import com.tarkovcompanion.sdk.Account
import com.tarkovcompanion.sdk.CompanionException
import com.tarkovcompanion.sdk.ErrorCode
import com.tarkovcompanion.sdk.host.HttpRequest
import com.tarkovcompanion.sdk.host.LogLevel
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract as Contract
import com.tarkovcompanion.sdk.model.FavoriteItem
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/** A core session (tarkov_companion.h); an interface so the executor is testable on the JVM. Thread-safe. */
internal interface CoreSession {
    fun setProfile(profileJson: String)

    fun search(requestId: Long, query: String)

    fun recommend(requestId: Long, itemId: String)

    fun cancel(requestId: Long)

    fun nextActionOrNullWhenIdle(): SessionAction?

    fun completeHttp(actionId: Long, statusCodeOrZeroIfNoResponse: Int, bodyOrFailureReason: ByteArray)

    fun completeCacheRead(actionId: Long, valueOrNullIfMissing: ByteArray?)

    fun openBridgeAndGetId(optionsJson: String): Long

    fun bridgeReceive(bridgeId: Long, message: String)

    fun bridgeEvent(bridgeId: Long, eventName: String)

    fun closeBridge(bridgeId: Long)

    fun signIn(requestId: Long, secureRandomBytes: ByteArray)

    fun signInRedirect(redirectUriOrNullIfClosed: String?)

    fun signOut(requestId: Long)

    fun favoritesList(requestId: Long)

    fun favoriteSet(requestId: Long, itemId: String, favorite: Boolean)

    companion object {
        // For the PKCE verifier and the state.
        const val SIGN_IN_ENTROPY_BYTES = Contract.SIGN_IN_ENTROPY_BYTES
    }
}

internal sealed class SessionAction {
    class Http(val actionId: Long, val request: HttpRequest) : SessionAction()

    class CacheRead(val actionId: Long, val key: String) : SessionAction()

    class CacheWrite(val key: String, val value: ByteArray) : SessionAction()

    // Exactly one of payload and error is set.
    class Result(val requestId: Long, val payload: ByteArray?, val error: CompanionException?) : SessionAction()

    class Log(val level: LogLevel, val message: String) : SessionAction()

    class Track(val event: String, val properties: Map<String, String>) : SessionAction()

    class BridgeSend(val bridgeId: Long, val message: String) : SessionAction()

    class BridgeState(val bridgeId: Long, val state: UiState) : SessionAction()

    class BridgeClose(val bridgeId: Long) : SessionAction()

    class RemoteFlagsChanged(val launchAllowed: Boolean) : SessionAction()

    // Secret, cache and data reads are all answered with completeCacheRead.
    class SecretRead(val actionId: Long, val key: String) : SessionAction()

    class SecretWrite(val key: String, val value: ByteArray) : SessionAction()

    class SecretDelete(val key: String) : SessionAction()

    class OpenSignInPageInBrowser(val requestId: Long, val url: String) : SessionAction()

    class AccountChanged(val accountOrNullIfSignedOut: Account?) : SessionAction()

    class PageRequestedSignIn(val bridgeId: Long) : SessionAction()

    // The data store keeps entries until they are deleted, it never evicts.
    class DataRead(val actionId: Long, val key: String) : SessionAction()

    class DataWrite(val key: String, val value: ByteArray) : SessionAction()

    class DataDelete(val key: String) : SessionAction()

    class FavoritesChanged(val items: List<FavoriteItem>) : SessionAction()

    class UnknownFromNewerCore(val json: String) : SessionAction()

    // A known action this SDK could not read (a field missing or of the wrong
    // type). If it expected an answer, it still gets one, so the request
    // waiting on it finishes.
    class Unreadable(
        val json: String,
        val failure: Exception,
        val readActionIdToAnswerNotFound: Long?,
        val httpActionIdToAnswerNoResponse: Long?,
    ) : SessionAction()

    companion object {
        private val ERROR_CODES_BY_WIRE_NAME = mapOf(
            Contract.ERROR_CODE_INVALID_ARGUMENT to ErrorCode.INVALID_ARGUMENT,
            Contract.ERROR_CODE_NETWORK to ErrorCode.NETWORK,
            Contract.ERROR_CODE_API to ErrorCode.API,
            Contract.ERROR_CODE_SCHEMA_MISMATCH to ErrorCode.SCHEMA_MISMATCH,
            Contract.ERROR_CODE_NOT_FOUND to ErrorCode.NOT_FOUND,
            Contract.ERROR_CODE_INTERNAL to ErrorCode.INTERNAL,
            Contract.ERROR_CODE_SIGN_IN_CANCELLED to ErrorCode.SIGN_IN_CANCELLED,
            Contract.ERROR_CODE_SIGN_IN_FAILED to ErrorCode.SIGN_IN_FAILED,
            Contract.ERROR_CODE_NOT_SIGNED_IN to ErrorCode.NOT_SIGNED_IN,
        )

        private val LOG_LEVELS_BY_WIRE_NAME = mapOf(
            Contract.ACTION_LOG_LEVEL_DEBUG to LogLevel.DEBUG,
            Contract.ACTION_LOG_LEVEL_INFO to LogLevel.INFO,
            Contract.ACTION_LOG_LEVEL_WARN to LogLevel.WARNING,
            Contract.ACTION_LOG_LEVEL_ERROR to LogLevel.ERROR,
        )

        private val READ_ACTION_TYPES = setOf(
            Contract.ACTION_TYPE_CACHE_READ,
            Contract.ACTION_TYPE_SECRET_READ,
            Contract.ACTION_TYPE_DATA_READ,
        )

        fun fromJson(actionJson: String, payload: ByteArray?): SessionAction = try {
            readFromJson(actionJson, payload)
        } catch (failure: JSONException) {
            unreadable(actionJson, failure)
        }

        // As much as can still be read: the type and the id the answer needs.
        private fun unreadable(actionJson: String, failure: JSONException): Unreadable {
            val json = try {
                JSONObject(actionJson)
            } catch (_: JSONException) {
                null
            }
            val type = json?.optString(Contract.ACTION_FIELD_TYPE).orEmpty()
            val actionId = json?.optLong(Contract.ACTION_FIELD_ID, NO_ACTION_ID)?.takeIf { id -> id != NO_ACTION_ID }

            return Unreadable(
                actionJson,
                failure,
                readActionIdToAnswerNotFound = actionId?.takeIf { type in READ_ACTION_TYPES },
                httpActionIdToAnswerNoResponse = actionId?.takeIf { type == Contract.ACTION_TYPE_HTTP },
            )
        }

        private fun readFromJson(actionJson: String, payload: ByteArray?): SessionAction {
            val json = JSONObject(actionJson)
            val payloadOrEmpty = payload ?: ByteArray(0)

            return when (json.getString(Contract.ACTION_FIELD_TYPE)) {
                Contract.ACTION_TYPE_HTTP -> httpFromJson(json, payload)
                Contract.ACTION_TYPE_CACHE_READ -> CacheRead(json.actionId(), json.key())
                Contract.ACTION_TYPE_CACHE_WRITE -> CacheWrite(json.key(), payloadOrEmpty)
                Contract.ACTION_TYPE_RESULT -> resultFromJson(json, payload)
                Contract.ACTION_TYPE_LOG -> Log(
                    LOG_LEVELS_BY_WIRE_NAME[json.getString(Contract.ACTION_FIELD_LEVEL)] ?: LogLevel.ERROR,
                    json.getString(Contract.ACTION_FIELD_MESSAGE),
                )
                Contract.ACTION_TYPE_TRACK -> Track(
                    json.getString(Contract.ACTION_FIELD_EVENT),
                    stringMapFromJson(json.getJSONObject(Contract.ACTION_FIELD_PROPERTIES)),
                )
                Contract.ACTION_TYPE_BRIDGE_SEND -> BridgeSend(json.bridgeId(), payload?.decodeToString().orEmpty())
                Contract.ACTION_TYPE_BRIDGE_STATE -> BridgeState(json.bridgeId(), uiStateFromJson(json))
                Contract.ACTION_TYPE_BRIDGE_CLOSE -> BridgeClose(json.bridgeId())
                Contract.ACTION_TYPE_FLAGS -> RemoteFlagsChanged(json.getBoolean(Contract.ACTION_FIELD_LAUNCH_ALLOWED))
                Contract.ACTION_TYPE_SECRET_READ -> SecretRead(json.actionId(), json.key())
                Contract.ACTION_TYPE_SECRET_WRITE -> SecretWrite(json.key(), payloadOrEmpty)
                Contract.ACTION_TYPE_SECRET_DELETE -> SecretDelete(json.key())
                Contract.ACTION_TYPE_OPEN_BROWSER -> OpenSignInPageInBrowser(
                    json.getLong(Contract.ACTION_FIELD_REQUEST),
                    json.getString(Contract.ACTION_FIELD_URL),
                )
                Contract.ACTION_TYPE_ACCOUNT -> AccountChanged(accountFromJsonOrNullIfSignedOut(json))
                Contract.ACTION_TYPE_BRIDGE_SIGN_IN -> PageRequestedSignIn(json.bridgeId())
                Contract.ACTION_TYPE_DATA_READ -> DataRead(json.actionId(), json.key())
                Contract.ACTION_TYPE_DATA_WRITE -> DataWrite(json.key(), payloadOrEmpty)
                Contract.ACTION_TYPE_DATA_DELETE -> DataDelete(json.key())
                Contract.ACTION_TYPE_FAVORITES -> FavoritesChanged(
                    favoriteItemsFromJson(JSONArray(payload?.decodeToString() ?: EMPTY_JSON_ARRAY)),
                )
                else -> UnknownFromNewerCore(actionJson)
            }
        }

        // Shared by the core's "favorites" action and the favorites_list result.
        fun favoriteItemsFromJson(items: JSONArray): List<FavoriteItem> = (0 until items.length()).map { index ->
            val item = items.getJSONObject(index)
            FavoriteItem(
                item.getString(Contract.PAYLOAD_FIELD_ID),
                item.getString(Contract.PAYLOAD_FIELD_NAME),
                item.optString(Contract.PAYLOAD_FIELD_SHORT_NAME),
                item.optString(Contract.PAYLOAD_FIELD_ICON_LINK),
            )
        }

        private const val EMPTY_JSON_ARRAY = "[]"
        private const val NO_ACTION_ID = -1L

        private fun JSONObject.actionId(): Long = getLong(Contract.ACTION_FIELD_ID)

        private fun JSONObject.bridgeId(): Long = getLong(Contract.ACTION_FIELD_BRIDGE)

        private fun JSONObject.key(): String = getString(Contract.ACTION_FIELD_KEY)

        private fun httpFromJson(json: JSONObject, payload: ByteArray?) = Http(
            actionId = json.actionId(),
            request = HttpRequest(
                url = json.getString(Contract.ACTION_FIELD_URL),
                method = json.getString(Contract.ACTION_FIELD_METHOD),
                headers = stringMapFromJson(json.getJSONObject(Contract.ACTION_FIELD_HEADERS)),
                body = payload,
                timeoutMilliseconds = json.getInt(Contract.ACTION_FIELD_TIMEOUT_MILLISECONDS),
            ),
        )

        private fun uiStateFromJson(json: JSONObject) = UiState(
            json.getString(Contract.ACTION_FIELD_ROUTE),
            json.optString(Contract.ACTION_FIELD_QUERY).ifEmpty { null },
            json.optString(Contract.ACTION_FIELD_ITEM_ID).ifEmpty { null },
        )

        private fun resultFromJson(json: JSONObject, payload: ByteArray?): Result {
            val error = json.optJSONObject(Contract.ACTION_FIELD_ERROR)?.let { errorJson ->
                CompanionException(
                    ERROR_CODES_BY_WIRE_NAME[errorJson.getString(Contract.ACTION_FIELD_CODE)] ?: ErrorCode.INTERNAL,
                    errorJson.getString(Contract.ACTION_FIELD_MESSAGE),
                )
            }
            val payloadOnSuccess = if (error == null) payload ?: ByteArray(0) else null

            return Result(json.getLong(Contract.ACTION_FIELD_REQUEST), payloadOnSuccess, error)
        }

        private fun accountFromJsonOrNullIfSignedOut(json: JSONObject): Account? {
            if (!json.getBoolean(Contract.ACTION_FIELD_SIGNED_IN)) {
                return null
            }

            return Account(json.getString(Contract.ACTION_FIELD_ID), json.getString(Contract.ACTION_FIELD_DISPLAY_NAME))
        }

        private fun stringMapFromJson(json: JSONObject): Map<String, String> =
            json.keys().asSequence().associateWith(json::getString)
    }
}

/** Lives as long as the process, so there is no destroy() to race with calls in flight. */
internal class NativeSession private constructor(private val sessionHandle: Long) : CoreSession {
    override fun setProfile(profileJson: String) {
        val errorOutput = arrayOfNulls<ByteArray>(1)
        val status = NativeBridge.setProfile(sessionHandle, profileJson.encodeToByteArray(), errorOutput)
        check(status == NativeBridge.STATUS_OK) {
            "core rejected the profile: ${errorOutput.errorMessageOrNoDetails()}"
        }
    }

    override fun search(requestId: Long, query: String) {
        checkCommandAccepted(NativeBridge.search(sessionHandle, requestId, query.encodeToByteArray()), requestId)
    }

    override fun recommend(requestId: Long, itemId: String) {
        checkCommandAccepted(NativeBridge.recommend(sessionHandle, requestId, itemId.encodeToByteArray()), requestId)
    }

    override fun cancel(requestId: Long) = NativeBridge.cancel(sessionHandle, requestId)

    override fun nextActionOrNullWhenIdle(): SessionAction? {
        val actionAndPayload = arrayOfNulls<ByteArray>(NativeBridge.OUTPUT_SLOT_COUNT)

        return when (val status = NativeBridge.nextAction(sessionHandle, actionAndPayload)) {
            NativeBridge.STATUS_NO_ACTION -> null
            NativeBridge.STATUS_OK -> SessionAction.fromJson(
                actionAndPayload[NativeBridge.OUTPUT_SLOT_ACTION_OR_ERROR]!!.decodeToString(),
                actionAndPayload[NativeBridge.OUTPUT_SLOT_PAYLOAD],
            )
            else -> error("tarkov_companion_session_next_action failed with status $status")
        }
    }

    override fun completeHttp(actionId: Long, statusCodeOrZeroIfNoResponse: Int, bodyOrFailureReason: ByteArray) {
        NativeBridge.completeHttp(sessionHandle, actionId, statusCodeOrZeroIfNoResponse, bodyOrFailureReason)
    }

    override fun completeCacheRead(actionId: Long, valueOrNullIfMissing: ByteArray?) {
        NativeBridge.completeCacheRead(sessionHandle, actionId, valueOrNullIfMissing != null, valueOrNullIfMissing)
    }

    override fun openBridgeAndGetId(optionsJson: String): Long {
        val errorOutput = arrayOfNulls<ByteArray>(1)
        val bridgeId = NativeBridge.openBridge(sessionHandle, optionsJson.encodeToByteArray(), errorOutput)
        check(bridgeId != 0L) { "core rejected the bridge options: ${errorOutput.errorMessageOrNoDetails()}" }

        return bridgeId
    }

    override fun bridgeReceive(bridgeId: Long, message: String) {
        NativeBridge.bridgeReceive(sessionHandle, bridgeId, message.encodeToByteArray())
    }

    override fun bridgeEvent(bridgeId: Long, eventName: String) {
        NativeBridge.bridgeEvent(sessionHandle, bridgeId, eventName.encodeToByteArray())
    }

    override fun closeBridge(bridgeId: Long) = NativeBridge.closeBridge(sessionHandle, bridgeId)

    override fun signIn(requestId: Long, secureRandomBytes: ByteArray) {
        checkCommandAccepted(NativeBridge.signIn(sessionHandle, requestId, secureRandomBytes), requestId)
    }

    override fun signInRedirect(redirectUriOrNullIfClosed: String?) {
        NativeBridge.signInRedirect(sessionHandle, redirectUriOrNullIfClosed?.encodeToByteArray())
    }

    override fun signOut(requestId: Long) {
        checkCommandAccepted(NativeBridge.signOut(sessionHandle, requestId), requestId)
    }

    override fun favoritesList(requestId: Long) {
        checkCommandAccepted(NativeBridge.favoritesList(sessionHandle, requestId), requestId)
    }

    override fun favoriteSet(requestId: Long, itemId: String, favorite: Boolean) {
        val status = NativeBridge.favoriteSet(sessionHandle, requestId, itemId.encodeToByteArray(), favorite)
        checkCommandAccepted(status, requestId)
    }

    // Commands are only rejected for a request id already in use: an SDK bug.
    private fun checkCommandAccepted(status: Int, requestId: Long) =
        check(status == NativeBridge.STATUS_OK) { "core rejected request $requestId (status $status)" }

    companion object {
        fun loadLibraryAndCreate(configJson: String): NativeSession {
            NativeLibrary.ensureLoaded()

            val errorOutput = arrayOfNulls<ByteArray>(1)
            val sessionHandle = NativeBridge.createSession(configJson.encodeToByteArray(), errorOutput)
            if (sessionHandle == 0L) {
                throw CompanionException(
                    ErrorCode.INTERNAL,
                    "core rejected the config: ${errorOutput.errorMessageOrNoDetails()}",
                )
            }

            return NativeSession(sessionHandle)
        }

        private fun Array<ByteArray?>.errorMessageOrNoDetails(): String =
            this[NativeBridge.OUTPUT_SLOT_ACTION_OR_ERROR]?.decodeToString() ?: "no details"
    }
}
