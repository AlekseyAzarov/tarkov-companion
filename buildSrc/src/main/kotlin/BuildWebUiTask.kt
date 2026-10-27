import org.gradle.api.DefaultTask
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.Internal
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction
import org.gradle.process.ExecOperations
import javax.inject.Inject

/** Bundles `web-ui/` with esbuild into a generated assets directory of the AAR. */
@CacheableTask
abstract class BuildWebUiTask : DefaultTask() {
    @get:Inject
    abstract val execOperations: ExecOperations

    @get:Internal
    abstract val webUiDirectory: DirectoryProperty

    // Everything that affects the bundle; node_modules is covered by the lockfile.
    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val sources: ConfigurableFileCollection

    @get:OutputDirectory
    abstract val outputDirectory: DirectoryProperty

    @TaskAction
    fun build() {
        val webUi = webUiDirectory.get().asFile
        val runningOnWindows = System.getProperty("os.name").lowercase().contains("windows")
        if (!webUi.resolve("node_modules").isDirectory) {
            execOperations.exec {
                workingDir = webUi
                commandLine(if (runningOnWindows) listOf("cmd", "/c", "npm", "ci") else listOf("npm", "ci"))
            }
        }

        val output = outputDirectory.get().asFile
        output.deleteRecursively()
        execOperations.exec {
            workingDir = webUi
            commandLine("node", "build.mjs", "--outdir", output.resolve("companion").absolutePath)
        }
    }
}
