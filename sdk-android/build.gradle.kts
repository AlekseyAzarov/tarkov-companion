plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    `maven-publish`
}

val sdkVersion = rootProject.file("VERSION").readText().trim()
group = providers.gradleProperty("GROUP").get()
version = sdkVersion

android {
    namespace = "com.tarkovcompanion.sdk"
    resourcePrefix = "tarkov_companion_"
    compileSdk = 36
    ndkVersion = "29.0.13846066"

    defaultConfig {
        minSdk = 24
        consumerProguardFiles("consumer-rules.pro")
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {
            // Must match what the host ships; see docs/troubleshooting.md#unsatisfiedlinkerror
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                // Static libc++ keeps us out of the host's libc++_shared.so version;
                // the version script keeps our copy of it private.
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false // consumers run R8 with consumer-rules.pro
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    testOptions {
        unitTests.all {
            it.systemProperty("tarkov_companion.fixtures", rootProject.file("contracts/fixtures").absolutePath)
        }
    }

    sourceSets {
        // Instrumented tests use the same recorded responses as every other suite.
        getByName("androidTest").assets.srcDir(rootProject.file("contracts/fixtures"))
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
        }
    }
}

kotlin {
    explicitApi()
    // Older language, API and stdlib levels, so hosts on an older Kotlin toolchain can use the SDK.
    coreLibrariesVersion = "2.2.21"
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        apiVersion.set(org.jetbrains.kotlin.gradle.dsl.KotlinVersion.KOTLIN_2_2)
        languageVersion.set(org.jetbrains.kotlin.gradle.dsl.KotlinVersion.KOTLIN_2_2)
        allWarningsAsErrors.set(true)
    }
    // Public API snapshot in sdk-android/api/. `check` fails when the API
    // changes without an updated dump, so every API change shows up in review.
    @OptIn(org.jetbrains.kotlin.gradle.dsl.abi.ExperimentalAbiValidation::class)
    abiValidation {
        referenceDumpDir.set(layout.projectDirectory.dir("api"))
    }
}

val generateContractSources = tasks.register<GenerateContractSourcesTask>("generateContractSources") {
    limitsFile.set(rootProject.layout.projectDirectory.file("contracts/config-limits.json"))
    analyticsFile.set(rootProject.layout.projectDirectory.file("contracts/analytics-events.json"))
    contractFiles.from(rootProject.fileTree("contracts") { include("*.json") })
    versionFile.set(rootProject.layout.projectDirectory.file("VERSION"))
    packageName.set("com.tarkovcompanion.sdk.internal.generated")
    outputDirectory.set(layout.buildDirectory.dir("generated/contract"))
}

val webUi = rootProject.layout.projectDirectory.dir("web-ui")
val buildWebUi = tasks.register<BuildWebUiTask>("buildWebUi") {
    webUiDirectory.set(webUi)
    sources.from(
        webUi.dir("src"),
        webUi.file("build.mjs"),
        webUi.file("package.json"),
        webUi.file("package-lock.json"),
        webUi.file("tsconfig.json"),
        rootProject.layout.projectDirectory.dir("contracts"),
        rootProject.layout.projectDirectory.file("scripts/generate-web-contract.mjs"),
    )
    outputDirectory.set(layout.buildDirectory.dir("generated/webui"))
}

androidComponents {
    onVariants { variant ->
        variant.sources.kotlin?.addGeneratedSourceDirectory(
            generateContractSources,
            GenerateContractSourcesTask::outputDirectory,
        )
        variant.sources.assets?.addGeneratedSourceDirectory(buildWebUi, BuildWebUiTask::outputDirectory)
    }
}

dependencies {
    // Deliberately no runtime dependencies. See docs/adr/0003-zero-runtime-dependencies.md.
    testImplementation(libs.junit)
    testImplementation(libs.org.json)

    androidTestImplementation(libs.junit)
    androidTestImplementation(libs.androidx.test.runner)
    androidTestImplementation(libs.androidx.test.core)
    androidTestImplementation(libs.androidx.test.ext.junit)
}

publishing {
    publications {
        register<MavenPublication>("release") {
            groupId = project.group.toString()
            artifactId = providers.gradleProperty("POM_ARTIFACT_ID").get()
            version = sdkVersion
            afterEvaluate { from(components["release"]) }
            pom {
                name.set("Tarkov Companion SDK")
                description.set("Embeddable item-lookup companion for Android games: C++ core, WebView UI.")
                licenses {
                    license {
                        name.set("MIT")
                        url.set("https://opensource.org/licenses/MIT")
                    }
                }
            }
        }
    }
    repositories {
        maven {
            name = "GitHubPackages"
            url = uri("https://maven.pkg.github.com/${System.getenv("GITHUB_REPOSITORY") ?: "AlekseyAzarov/tarkov-companion"}")
            credentials {
                username = System.getenv("GITHUB_ACTOR")
                password = System.getenv("GITHUB_TOKEN")
            }
        }
        maven {
            name = "LocalRepo"
            url = uri(rootProject.layout.buildDirectory.dir("local-maven"))
        }
    }
}
