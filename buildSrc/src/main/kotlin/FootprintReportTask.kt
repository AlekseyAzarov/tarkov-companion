import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction
import org.gradle.work.DisableCachingByDefault
import java.io.File
import java.util.Locale
import java.util.zip.ZipFile

/** Compares release APKs of the host with and without the SDK and breaks the delta down, as Markdown. */
@DisableCachingByDefault(because = "Cheap; output is a report")
abstract class FootprintReportTask : DefaultTask() {
    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val withSdkApkDirectory: DirectoryProperty

    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val baselineApkDirectory: DirectoryProperty

    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val aar: RegularFileProperty

    @get:OutputFile
    abstract val report: RegularFileProperty

    private class ApkEntry(val name: String, val compressedBytes: Long, val uncompressedBytes: Long)

    @TaskAction
    fun run() {
        val withSdkApk = findApk(withSdkApkDirectory.get().asFile)
        val baselineApk = findApk(baselineApkDirectory.get().asFile)
        val withSdkEntries = entriesByName(withSdkApk)
        val baselineEntries = entriesByName(baselineApk)

        val compressedDeltaByPart = (withSdkEntries.keys + baselineEntries.keys)
            .groupBy(::partOf)
            .map { (part, names) ->
                val delta = names.sumOf { name ->
                    (withSdkEntries[name]?.compressedBytes ?: 0) - (baselineEntries[name]?.compressedBytes ?: 0)
                }
                part to delta
            }
            .filter { (_, delta) -> delta != 0L }
            .sortedByDescending { (_, delta) -> delta }

        val sdkLibraries = withSdkEntries.values
            .filter { entry -> entry.name.startsWith(NATIVE_LIBRARY_DIRECTORY) && entry.name.endsWith(SDK_LIBRARY) }
            .sortedBy(ApkEntry::name)

        val apkDelta = withSdkApk.length() - baselineApk.length()
        val markdown = buildString {
            appendLine("## SDK footprint")
            appendLine()
            appendLine("| | Size |")
            appendLine("|---|---:|")
            appendLine("| Host APK without SDK (release, R8) | ${kilobytes(baselineApk.length())} |")
            appendLine("| Host APK with SDK (release, R8) | ${kilobytes(withSdkApk.length())} |")
            appendLine("| **Delta (universal APK, 3 ABIs)** | **${signedKilobytes(apkDelta)}** |")
            appendLine("| SDK .aar | ${kilobytes(aar.get().asFile.length())} |")
            appendLine()
            appendLine("Delta by content (compressed size in APK):")
            appendLine()
            appendLine("| Part | Delta |")
            appendLine("|---|---:|")
            compressedDeltaByPart.forEach { (part, delta) -> appendLine("| $part | ${signedKilobytes(delta)} |") }
            appendLine()
            appendLine("Per-ABI native library (what one device actually downloads from an App Bundle):")
            appendLine()
            appendLine("| ABI | Download (compressed) | On disk |")
            appendLine("|---|---:|---:|")
            sdkLibraries.forEach { library ->
                val abi = library.name.removePrefix(NATIVE_LIBRARY_DIRECTORY).substringBefore('/')
                appendLine("| $abi | ${kilobytes(library.compressedBytes)} | ${kilobytes(library.uncompressedBytes)} |")
            }
        }

        report.get().asFile.apply { parentFile.mkdirs() }.writeText(markdown)
        logger.lifecycle(markdown)
    }

    private fun partOf(entryName: String): String = when {
        entryName.startsWith(NATIVE_LIBRARY_DIRECTORY) -> {
            "native: " + entryName.removePrefix(NATIVE_LIBRARY_DIRECTORY).substringBefore('/')
        }
        entryName.endsWith(".dex") -> "dex (Kotlin/Java)"
        entryName.startsWith("assets/companion/") -> "web UI assets"
        entryName.startsWith("res/") || entryName == "resources.arsc" -> "resources"
        else -> "other"
    }

    private fun findApk(directory: File): File =
        directory.walkTopDown().firstOrNull { file -> file.extension == "apk" } ?: error("no APK in $directory")

    private fun entriesByName(apk: File): Map<String, ApkEntry> = ZipFile(apk).use { archive ->
        archive.entries().asSequence()
            .filter { entry -> !entry.isDirectory }
            .associate { entry -> entry.name to ApkEntry(entry.name, entry.compressedSize, entry.size) }
    }

    private fun kilobytes(bytes: Long): String = "%.1f KB".format(Locale.ROOT, bytes / BYTES_PER_KILOBYTE)

    private fun signedKilobytes(bytes: Long): String = "%+.1f KB".format(Locale.ROOT, bytes / BYTES_PER_KILOBYTE)

    private companion object {
        const val NATIVE_LIBRARY_DIRECTORY = "lib/"
        const val SDK_LIBRARY = "libtarkovcompanion.so"
        const val BYTES_PER_KILOBYTE = 1024.0
    }
}
