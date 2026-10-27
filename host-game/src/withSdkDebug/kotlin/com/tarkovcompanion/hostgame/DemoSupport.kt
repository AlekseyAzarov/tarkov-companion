package com.tarkovcompanion.hostgame

import android.content.Context
import android.util.Log
import com.tarkovcompanion.sdk.CompanionConfig
import com.tarkovcompanion.sdk.host.HttpRequest
import com.tarkovcompanion.sdk.host.HttpResponse
import com.tarkovcompanion.sdk.host.HttpTransport
import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/**
 * Debug builds only: a host HttpTransport that falls back to contracts/fixtures
 * when there is no network at all. Errors from tarkov.dev pass through, so the
 * SDK's own catalog fallback is what runs.
 */
object DemoSupport {
    /**
     * Remote flags for the demo: served by the transport below from
     * files/demo-flags.json, so the kill switch can be tried without a server:
     *
     *   adb shell "run-as com.tarkovcompanion.hostgame sh -c \
     *     'echo {\"schema\":1,\"webFlowEnabled\":false} > files/demo-flags.json'"
     */
    const val REMOTE_FLAGS_URL = "https://demo.invalid/companion/flags.json"

    // Registered for com.tarkovcompanion.hostgame://tarkov-companion-auth in bff/src/server.mjs.
    const val SIGN_IN_CLIENT_ID = "tarkov-companion-demo"

    // The development backend in bff/ (`node bff/src/main.mjs`), as the emulator
    // sees the computer. http is allowed only because this is a debuggable
    // build; see docs/auth.md.
    private const val DEVELOPMENT_BACKEND = "http://10.0.2.2:8787"

    fun remoteFlagsUrl(): String? = REMOTE_FLAGS_URL

    fun signInIssuer(): String? = DEVELOPMENT_BACKEND

    fun favoritesSyncUrl(): String? = "$DEVELOPMENT_BACKEND/favorites/sync"

    // Built by scripts/build-catalog.mjs.
    fun catalogUrl(): String? = "$DEVELOPMENT_BACKEND/catalog"

    fun transport(context: Context): HttpTransport = FixtureFallbackTransport(context.applicationContext)
}

private class FixtureFallbackTransport(private val context: Context) : HttpTransport {
    override fun execute(request: HttpRequest): HttpResponse {
        if (request.url == DemoSupport.REMOTE_FLAGS_URL) {
            return demoFlags()
        }

        try {
            return sendOverNetwork(request)
        } catch (offline: IOException) {
            val fixture = fixtureFor(request) ?: throw offline
            Log.w("RaidSim/Demo", "no network (${offline.message}); serving a recorded fixture")

            return HttpResponse(HttpURLConnection.HTTP_OK, JSON_HEADERS, fixture.encodeToByteArray())
        }
    }

    private fun sendOverNetwork(request: HttpRequest): HttpResponse {
        val connection = URL(request.url).openConnection() as HttpURLConnection
        try {
            connection.requestMethod = request.method
            connection.connectTimeout = request.timeoutMilliseconds
            connection.readTimeout = request.timeoutMilliseconds
            request.headers.forEach { (name, value) -> connection.setRequestProperty(name, value) }

            request.body?.let { body ->
                connection.doOutput = true
                connection.outputStream.use { output -> output.write(body) }
            }

            val failed = connection.responseCode >= HttpURLConnection.HTTP_BAD_REQUEST
            val stream = if (failed) connection.errorStream else connection.inputStream
            val body = stream?.use { input -> input.readBytes() } ?: ByteArray(0)

            return HttpResponse(connection.responseCode, emptyMap(), body)
        } finally {
            connection.disconnect()
        }
    }

    private fun fixtureFor(request: HttpRequest): String? {
        // Only tarkov.dev has recorded fixtures; the sign-in server's answers pass through as they are.
        if (!request.url.startsWith(CompanionConfig.DEFAULT_GRAPHQL_ENDPOINT)) {
            return null
        }

        val body = request.body?.decodeToString() ?: return null
        val graphqlRequest = JSONObject(body)
        val variables = graphqlRequest.optJSONObject(GRAPHQL_VARIABLES) ?: JSONObject()
        val query = graphqlRequest.getString(GRAPHQL_QUERY)

        return when {
            query.startsWith(HIDEOUT_QUERY_START) -> asset(HIDEOUT_FIXTURE)
            query.startsWith(ITEM_SEARCH_QUERY_START) -> itemsMatching { item ->
                val searchedName = variables.optString(VARIABLE_NAME)
                item.getString(FIELD_NAME).contains(searchedName, ignoreCase = true) ||
                    item.optString(FIELD_SHORT_NAME).contains(searchedName, ignoreCase = true)
            }
            query.startsWith(ITEM_BY_ID_QUERY_START) -> {
                val requestedIds = variables.optJSONArray(VARIABLE_IDS) ?: JSONArray()
                val wanted = (0 until requestedIds.length()).map { index -> requestedIds.getString(index) }.toSet()
                itemsMatching { item -> item.getString(FIELD_ID) in wanted }
            }
            else -> null
        }
    }

    private fun itemsMatching(shouldInclude: (JSONObject) -> Boolean): String {
        val allItems = JSONObject(asset(ITEM_SEARCH_FIXTURE)).getJSONObject(FIELD_DATA).getJSONArray(FIELD_ITEMS)
        val matchingItems = JSONArray()
        for (index in 0 until allItems.length()) {
            val item = allItems.getJSONObject(index)
            if (shouldInclude(item)) {
                matchingItems.put(item)
            }
        }

        return JSONObject().put(FIELD_DATA, JSONObject().put(FIELD_ITEMS, matchingItems)).toString()
    }

    private fun demoFlags(): HttpResponse {
        val file = context.filesDir.resolve(DEMO_FLAGS_FILE)
        val document = if (file.isFile) file.readText() else DEFAULT_DEMO_FLAGS

        return HttpResponse(HttpURLConnection.HTTP_OK, JSON_HEADERS, document.encodeToByteArray())
    }

    private fun asset(name: String) =
        context.assets.open("$FIXTURES_DIRECTORY/$name").bufferedReader().use { reader -> reader.readText() }

    private companion object {
        val JSON_HEADERS = mapOf("Content-Type" to "application/json")
        const val FIXTURES_DIRECTORY = "fixtures"
        const val ITEM_SEARCH_FIXTURE = "item_search.json"
        const val HIDEOUT_FIXTURE = "hideout.json"
        const val DEMO_FLAGS_FILE = "demo-flags.json"
        const val DEFAULT_DEMO_FLAGS = """{"schema":1,"webFlowEnabled":true}"""

        // tarkov.dev's GraphQL request and response (contracts/queries/).
        const val GRAPHQL_QUERY = "query"
        const val GRAPHQL_VARIABLES = "variables"
        const val HIDEOUT_QUERY_START = "query Hideout"
        const val ITEM_SEARCH_QUERY_START = "query ItemSearch"
        const val ITEM_BY_ID_QUERY_START = "query ItemById"
        const val VARIABLE_NAME = "name"
        const val VARIABLE_IDS = "ids"
        const val FIELD_DATA = "data"
        const val FIELD_ITEMS = "items"
        const val FIELD_ID = "id"
        const val FIELD_NAME = "name"
        const val FIELD_SHORT_NAME = "shortName"
    }
}
