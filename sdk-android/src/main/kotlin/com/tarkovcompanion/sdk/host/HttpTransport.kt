package com.tarkovcompanion.sdk.host

import com.tarkovcompanion.sdk.internal.net.HttpStatus
import java.io.IOException

/** The SDK's only way onto the network. Called on an SDK background thread; blocking is expected. */
public fun interface HttpTransport {
    /** Throw [IOException] for transport failures. Non-2xx responses are returned, not thrown. */
    @Throws(IOException::class)
    public fun execute(request: HttpRequest): HttpResponse
}

public class HttpRequest(
    public val url: String,
    public val method: String,
    public val headers: Map<String, String>,
    public val body: ByteArray?,
    public val timeoutMilliseconds: Int,
) {
    override fun toString(): String = "HttpRequest($method $url)"
}

public class HttpResponse(
    public val statusCode: Int,
    public val headers: Map<String, String>,
    public val body: ByteArray,
) {
    public val isSuccessful: Boolean get() = statusCode in HttpStatus.SUCCESS_RANGE

    override fun toString(): String = "HttpResponse($statusCode, ${body.size} bytes)"
}
