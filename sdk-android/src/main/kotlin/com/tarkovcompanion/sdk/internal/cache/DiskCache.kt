package com.tarkovcompanion.sdk.internal.cache

import java.io.File
import java.security.MessageDigest

internal interface KeyValueStore {
    fun get(key: String): ByteArray?

    fun put(key: String, value: ByteArray)

    fun delete(key: String)
}

/**
 * Opaque values by key; the core stamps its own entries. Writes go through a
 * temporary file and a rename, so a killed process leaves the old or the new
 * entry, never half of one.
 */
internal class DiskCache(
    private val directory: File,
    private val maximumEntryCountOrNullForUnlimited: Int?,
) : KeyValueStore {
    @Synchronized
    override fun get(key: String): ByteArray? {
        val file = fileFor(key)
        if (!file.isFile) {
            return null
        }

        return try {
            file.readBytes()
        } catch (_: Exception) {
            // Unreadable entry (e.g. a disk error): drop it.
            file.delete()
            null
        }
    }

    @Synchronized
    override fun put(key: String, value: ByteArray) {
        if (!directory.isDirectory && !directory.mkdirs()) {
            return
        }

        val target = fileFor(key)
        val temporary = File(directory, target.name + TEMPORARY_FILE_SUFFIX)
        temporary.writeBytes(value)
        if (!temporary.renameTo(target)) {
            target.delete()
            temporary.renameTo(target)
        }

        deleteOldestEntriesOverLimit()
    }

    @Synchronized
    override fun delete(key: String) {
        fileFor(key).delete()
    }

    private fun deleteOldestEntriesOverLimit() {
        val maximumEntryCount = maximumEntryCountOrNullForUnlimited ?: return
        val entries = directory.listFiles { file -> file.name.endsWith(ENTRY_FILE_SUFFIX) } ?: return
        if (entries.size <= maximumEntryCount) {
            return
        }

        entries.sortedBy(File::lastModified).take(entries.size - maximumEntryCount).forEach(File::delete)
    }

    // Keys are hashed: file names stay short and don't leak search queries.
    private fun fileFor(key: String): File {
        val digest = MessageDigest.getInstance("SHA-256").digest(key.encodeToByteArray())
        val name = digest.take(FILE_NAME_BYTES).joinToString("") { byte -> "%02x".format(byte) }

        return File(directory, name + ENTRY_FILE_SUFFIX)
    }

    private companion object {
        const val ENTRY_FILE_SUFFIX = ".bin"
        const val TEMPORARY_FILE_SUFFIX = ".tmp"

        // 128 bits of SHA-256: collision-free for any realistic number of entries.
        const val FILE_NAME_BYTES = 16
    }
}
