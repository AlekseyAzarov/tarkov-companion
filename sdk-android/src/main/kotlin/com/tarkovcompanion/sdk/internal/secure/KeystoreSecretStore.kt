package com.tarkovcompanion.sdk.internal.secure

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.io.File
import java.security.KeyStore
import java.security.MessageDigest
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

internal interface SecretStore {
    fun get(key: String): ByteArray?

    fun put(key: String, value: ByteArray)

    fun delete(key: String)
}

/**
 * AES-GCM under a non-exportable Android Keystore key. [directory] belongs in
 * noBackupFilesDir: the key never leaves the device. Anything that can't be
 * decrypted reads as absent, so the player signs in again.
 *
 * Format: version byte, 12-byte IV, ciphertext with the GCM tag. The entry's
 * name is the associated data, so a file moved to another name fails.
 */
internal class KeystoreSecretStore(
    private val directory: File,
    private val onUnreadableSecretDropped: (key: String, failure: Exception) -> Unit,
) : SecretStore {
    @Synchronized
    override fun get(key: String): ByteArray? {
        val file = fileFor(key)
        if (!file.isFile) {
            return null
        }

        return try {
            val stored = file.readBytes()
            require(stored.size > HEADER_BYTES && stored[0] == FORMAT_VERSION) { "unknown format" }

            val cipher = Cipher.getInstance(TRANSFORMATION)
            val parameters = GCMParameterSpec(AUTHENTICATION_TAG_BITS, stored, 1, INITIALIZATION_VECTOR_BYTES)
            cipher.init(Cipher.DECRYPT_MODE, keystoreSecretKey(), parameters)
            cipher.updateAAD(key.encodeToByteArray())
            cipher.doFinal(stored, HEADER_BYTES, stored.size - HEADER_BYTES)
        } catch (failure: Exception) {
            // GeneralSecurityException (bad tag, invalidated key), IOException, or the format check.
            onUnreadableSecretDropped(key, failure)
            file.delete()
            null
        }
    }

    @Synchronized
    override fun put(key: String, value: ByteArray) {
        if (!directory.isDirectory && !directory.mkdirs()) {
            return
        }

        val cipher = Cipher.getInstance(TRANSFORMATION)
        // The Keystore picks a fresh initialization vector.
        cipher.init(Cipher.ENCRYPT_MODE, keystoreSecretKey())
        cipher.updateAAD(key.encodeToByteArray())
        val encrypted = cipher.doFinal(value)
        val initializationVector = cipher.iv
        check(initializationVector.size == INITIALIZATION_VECTOR_BYTES) {
            "unexpected IV size ${initializationVector.size}"
        }

        val target = fileFor(key)
        val temporary = File(directory, target.name + TEMPORARY_FILE_SUFFIX)
        temporary.writeBytes(byteArrayOf(FORMAT_VERSION) + initializationVector + encrypted)
        if (!temporary.renameTo(target)) {
            target.delete()
            temporary.renameTo(target)
        }
    }

    @Synchronized
    override fun delete(key: String) {
        fileFor(key).delete()
    }

    private fun keystoreSecretKey(): SecretKey {
        val keyStore = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        val existingEntry = keyStore.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry
        if (existingEntry != null) {
            return existingEntry.secretKey
        }

        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
        generator.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(KEY_BITS)
                .build(),
        )

        return generator.generateKey()
    }

    // Hashed names, like DiskCache: they don't say what the file holds.
    private fun fileFor(key: String): File {
        val digest = MessageDigest.getInstance("SHA-256").digest(key.encodeToByteArray())
        val name = digest.take(FILE_NAME_BYTES).joinToString("") { byte -> "%02x".format(byte) }

        return File(directory, name + ENTRY_FILE_SUFFIX)
    }

    private companion object {
        const val ANDROID_KEYSTORE = "AndroidKeyStore"
        const val KEY_ALIAS = "tarkov_companion_secrets"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val KEY_BITS = 256
        const val AUTHENTICATION_TAG_BITS = 128
        const val INITIALIZATION_VECTOR_BYTES = 12
        const val FORMAT_VERSION: Byte = 1
        const val HEADER_BYTES = 1 + INITIALIZATION_VECTOR_BYTES
        const val ENTRY_FILE_SUFFIX = ".sec"
        const val TEMPORARY_FILE_SUFFIX = ".tmp"
        const val FILE_NAME_BYTES = 16
    }
}
