package com.tarkovcompanion.sdk

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.tarkovcompanion.sdk.internal.secure.KeystoreSecretStore
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

// The real Android Keystore: encryption at rest, tamper detection, files bound to their key.
@RunWith(AndroidJUnit4::class)
class KeystoreSecretStoreInstrumentedTest {
    private val directory = File(
        InstrumentationRegistry.getInstrumentation().targetContext.noBackupFilesDir,
        "secret-store-test",
    )
    private val droppedUnreadableKeys = mutableListOf<String>()
    private val store = KeystoreSecretStore(directory) { key, _ -> droppedUnreadableKeys += key }

    @After
    fun deleteStoreDirectory() {
        directory.deleteRecursively()
    }

    private fun onlyFile() = directory.listFiles()!!.single()

    @Test
    fun roundTripsAndKeepsNoPlaintextOnDisk() {
        val secret = """{"refreshToken":"refresh-123"}""".encodeToByteArray()

        store.put("auth", secret)

        assertArrayEquals(secret, store.get("auth"))
        assertFalse("ciphertext only", String(onlyFile().readBytes(), Charsets.ISO_8859_1).contains("refresh-123"))

        store.delete("auth")

        assertNull(store.get("auth"))
    }

    @Test
    fun aTamperedFileReadsAsAbsentAndIsDropped() {
        store.put("auth", "secret".encodeToByteArray())
        val file = onlyFile()
        val bytes = file.readBytes()
        bytes[bytes.size - 1] = (bytes[bytes.size - 1].toInt() xor 1).toByte()

        file.writeBytes(bytes)

        assertNull(store.get("auth"))
        assertEquals(listOf("auth"), droppedUnreadableKeys)
        assertFalse(file.exists())
    }

    @Test
    fun aFileCopiedUnderAnotherKeyDoesNotDecrypt() {
        store.put("auth", "secret".encodeToByteArray())
        val original = onlyFile()
        store.put("other", "x".encodeToByteArray())
        val otherFile = directory.listFiles()!!.single { file -> file != original }

        original.copyTo(otherFile, overwrite = true)

        assertNull("the key name is authenticated data", store.get("other"))
        assertArrayEquals("secret".encodeToByteArray(), store.get("auth"))
    }
}
