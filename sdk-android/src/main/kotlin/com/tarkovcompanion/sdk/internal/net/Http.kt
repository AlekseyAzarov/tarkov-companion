package com.tarkovcompanion.sdk.internal.net

// RFC 9110 names; the core's are in core/src/http.h.
internal object HttpStatus {
    const val OK = 200
    const val FORBIDDEN = 403
    const val NOT_FOUND = 404
    const val BAD_GATEWAY = 502

    const val FIRST_ERROR = 400
    val SUCCESS_RANGE = 200..299
}

internal object HttpNames {
    const val METHOD_GET = "GET"
    const val HEADER_USER_AGENT = "User-Agent"
    const val HEADER_CACHE_CONTROL = "Cache-Control"
    const val HEADER_CONTENT_SECURITY_POLICY = "Content-Security-Policy"
    const val HEADER_CONTENT_TYPE_OPTIONS = "X-Content-Type-Options"
    const val HEADER_VALUE_SEPARATOR = ","
}
