package com.tarkovcompanion.sdk.ui

import android.content.res.AssetManager
import android.os.Build
import android.util.LruCache
import android.webkit.RenderProcessGoneDetail
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import com.tarkovcompanion.sdk.internal.Logger
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import com.tarkovcompanion.sdk.internal.net.HttpNames
import com.tarkovcompanion.sdk.internal.net.HttpStatus
import java.io.ByteArrayInputStream
import java.io.FileNotFoundException
import java.util.concurrent.TimeUnit

/**
 * Serves the bundled web UI under a real https origin, which postWebMessage's
 * origin check needs, and refuses everything else (ADR 0004). Icons go through
 * /companion/icon, so the host's HttpTransport applies to them too.
 */
internal class CompanionWebViewClient(
    private val assets: AssetManager,
    private val fetchIcon: (String) -> ByteArray,
    private val logger: Logger,
    private val onPageReady: () -> Unit,
    private val onRendererGone: () -> Unit,
) : WebViewClient() {
    private val iconCache = object : LruCache<String, ByteArray>(ICON_CACHE_BYTES) {
        override fun sizeOf(key: String, value: ByteArray): Int = value.size
    }

    override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
        val url = request.url
        if (url.scheme != SCHEME_HTTPS || url.host != HOST) {
            return forbidden()
        }

        val path = url.path ?: return notFound()
        if (!path.startsWith(PATH_PREFIX)) {
            return notFound()
        }

        if (path == GeneratedContract.ICON_PROXY_PATH) {
            return proxiedIcon(url.getQueryParameter(GeneratedContract.ICON_PROXY_URL_PARAMETER))
        }

        val assetPath = path.removePrefix(PATH_PREFIX).ifEmpty { ENTRY_FILE }
        val escapesAssetRoot = assetPath.split(PATH_SEPARATOR).any { segment ->
            segment == PARENT_DIRECTORY || segment.isEmpty()
        }
        if (escapesAssetRoot) {
            return notFound()
        }

        return try {
            WebResourceResponse(
                mimeType(assetPath),
                UTF_8,
                HttpStatus.OK,
                REASON_OK,
                SECURITY_HEADERS,
                assets.open("$ASSET_DIRECTORY/$assetPath"),
            )
        } catch (_: FileNotFoundException) {
            notFound()
        }
    }

    // The overlay never navigates. Links, redirects, window.location: all refused.
    override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean = true

    override fun onPageFinished(view: WebView, url: String) {
        if (url.startsWith(ENTRY_URL)) {
            onPageReady()
        }
    }

    // Without this override a renderer crash kills the host process, the game included.
    override fun onRenderProcessGone(view: WebView, detail: RenderProcessGoneDetail): Boolean {
        val crashed = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) detail.didCrash() else true
        logger.error("WebView renderer gone (crashed=$crashed); closing companion")
        onRendererGone()

        return true
    }

    private fun proxiedIcon(iconUrl: String?): WebResourceResponse {
        if (iconUrl == null || !iconUrl.startsWith(ICON_ORIGIN)) {
            return forbidden()
        }

        val cached = iconCache.get(iconUrl)
        if (cached != null) {
            return image(iconUrl, cached)
        }

        return try {
            val bytes = fetchIcon(iconUrl)
            iconCache.put(iconUrl, bytes)
            image(iconUrl, bytes)
        } catch (failure: Exception) {
            logger.debug("icon fetch failed: ${failure.message}")
            emptyResponse(HttpStatus.BAD_GATEWAY, REASON_BAD_GATEWAY)
        }
    }

    private fun image(url: String, bytes: ByteArray) =
        WebResourceResponse(mimeType(url), null, HttpStatus.OK, REASON_OK, ICON_HEADERS, ByteArrayInputStream(bytes))

    private fun forbidden() = emptyResponse(HttpStatus.FORBIDDEN, REASON_FORBIDDEN)

    private fun notFound() = emptyResponse(HttpStatus.NOT_FOUND, REASON_NOT_FOUND)

    private fun emptyResponse(statusCode: Int, reasonPhrase: String) = WebResourceResponse(
        MIME_TYPE_PLAIN_TEXT,
        UTF_8,
        statusCode,
        reasonPhrase,
        SECURITY_HEADERS,
        ByteArrayInputStream(ByteArray(0)),
    )

    companion object {
        const val HOST = "appassets.androidplatform.net"
        const val ORIGIN = "https://$HOST"
        const val PATH_PREFIX = "/companion/"
        private const val ENTRY_FILE = "index.html"
        const val ENTRY_URL = "$ORIGIN$PATH_PREFIX$ENTRY_FILE"

        // Where Gradle puts the web bundle inside the AAR's assets (see BuildWebUiTask).
        private const val ASSET_DIRECTORY = "companion"
        private const val ICON_ORIGIN = "https://assets.tarkov.dev/"
        private const val ICON_CACHE_BYTES = 4 * 1024 * 1024
        private val ICON_CACHE_MAXIMUM_AGE_SECONDS = TimeUnit.DAYS.toSeconds(1)

        private const val SCHEME_HTTPS = "https"
        private const val PATH_SEPARATOR = '/'
        private const val PARENT_DIRECTORY = ".."
        private const val UTF_8 = "utf-8"
        private const val MIME_TYPE_PLAIN_TEXT = "text/plain"
        private const val REASON_OK = "OK"
        private const val REASON_FORBIDDEN = "Forbidden"
        private const val REASON_NOT_FOUND = "Not Found"
        private const val REASON_BAD_GATEWAY = "Bad Gateway"

        private val SECURITY_HEADERS = mapOf(
            HttpNames.HEADER_CONTENT_SECURITY_POLICY to
                "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self'; " +
                "connect-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
            HttpNames.HEADER_CONTENT_TYPE_OPTIONS to "nosniff",
            HttpNames.HEADER_CACHE_CONTROL to "no-store",
        )
        private val ICON_HEADERS = mapOf(
            HttpNames.HEADER_CACHE_CONTROL to "max-age=$ICON_CACHE_MAXIMUM_AGE_SECONDS",
            HttpNames.HEADER_CONTENT_TYPE_OPTIONS to "nosniff",
        )

        fun mimeType(path: String): String = when (path.substringAfterLast('.').lowercase()) {
            "html" -> "text/html"
            "js", "mjs" -> "text/javascript"
            "css" -> "text/css"
            "json" -> "application/json"
            "svg" -> "image/svg+xml"
            "webp" -> "image/webp"
            "png" -> "image/png"
            "jpg", "jpeg" -> "image/jpeg"
            "woff2" -> "font/woff2"
            else -> "application/octet-stream"
        }
    }
}
