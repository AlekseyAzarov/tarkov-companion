package com.tarkovcompanion.sdk.internal.net

import com.tarkovcompanion.sdk.host.HttpRequest
import com.tarkovcompanion.sdk.host.HttpResponse
import com.tarkovcompanion.sdk.host.HttpTransport
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

/** Used when the host provides no transport (ADR 0003). */
internal class DefaultHttpTransport(
    private val maximumResponseBytes: Int = DEFAULT_MAXIMUM_RESPONSE_BYTES,
) : HttpTransport {
    override fun execute(request: HttpRequest): HttpResponse {
        val connection = URL(request.url).openConnection() as HttpURLConnection
        try {
            connection.requestMethod = request.method
            connection.connectTimeout = request.timeoutMilliseconds
            connection.readTimeout = request.timeoutMilliseconds
            connection.instanceFollowRedirects = false
            request.headers.forEach { (name, value) -> connection.setRequestProperty(name, value) }

            request.body?.let { body ->
                connection.doOutput = true
                connection.setFixedLengthStreamingMode(body.size)
                connection.outputStream.use { output -> output.write(body) }
            }

            val statusCode = connection.responseCode
            // HttpURLConnection serves 4xx/5xx bodies from errorStream.
            val stream = if (statusCode >= HttpStatus.FIRST_ERROR) connection.errorStream else connection.inputStream
            val body = stream?.use(::readAtMostMaximumResponseBytes) ?: ByteArray(0)
            val headers = connection.headerFields
                .filterKeys { name -> name != null }
                .mapValues { (_, values) -> values.joinToString(HttpNames.HEADER_VALUE_SEPARATOR) }

            return HttpResponse(statusCode, headers, body)
        } finally {
            connection.disconnect()
        }
    }

    private fun readAtMostMaximumResponseBytes(input: InputStream): ByteArray {
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(READ_BUFFER_BYTES)
        while (true) {
            val bytesRead = input.read(buffer)
            if (bytesRead < 0) {
                break
            }

            if (output.size() + bytesRead > maximumResponseBytes) {
                throw IOException("response exceeds $maximumResponseBytes bytes")
            }

            output.write(buffer, 0, bytesRead)
        }

        return output.toByteArray()
    }

    private companion object {
        // A full hideout response is ~100 KB; anything near this limit is not tarkov.dev.
        const val DEFAULT_MAXIMUM_RESPONSE_BYTES = 8 * 1024 * 1024
        const val READ_BUFFER_BYTES = 16 * 1024
    }
}
