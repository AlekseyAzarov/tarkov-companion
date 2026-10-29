plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

val flutterOverlayBuilt = rootProject.file("experiments/flutter_overlay/build/host/outputs/repo").isDirectory

android {
    namespace = "com.tarkovcompanion.hostgame"
    compileSdk = 36
    ndkVersion = "29.0.13846066"

    defaultConfig {
        applicationId = "com.tarkovcompanion.hostgame"
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = rootProject.file("VERSION").readText().trim()
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    // Two otherwise identical games: the difference between their APKs is the
    // SDK's footprint. See `./gradlew footprintReport` and docs/footprint.md.
    flavorDimensions += "integration"
    productFlavors {
        create("withSdk") {
            dimension = "integration"
        }
        create("baseline") {
            dimension = "integration"
            applicationIdSuffix = ".baseline"
        }
        // withSdk with a Flutter screen instead of the WebView overlay (ADR 0002). Only
        // when its AARs are built, so the usual build and CI don't need Flutter.
        if (flutterOverlayBuilt) {
            create("withFlutter") {
                dimension = "integration"
                applicationIdSuffix = ".flutter"
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            // Demo app: release builds are signed with the debug key so they can be installed and measured.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets {
        // Recorded API responses for the debug-only fixture fallback (DemoSupport).
        getByName("debug").assets.srcDir(rootProject.file("contracts"))

        // Variant source sets are made after this script runs; their Implementation configurations are needed below.
        if (flutterOverlayBuilt) {
            maybeCreate("withFlutterDebug")
            maybeCreate("withFlutterRelease")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    "withSdkImplementation"(project(":sdk-android"))

    if (flutterOverlayBuilt) {
        "withFlutterImplementation"(project(":sdk-android"))
        "withFlutterDebugImplementation"("com.tarkovcompanion.flutter_overlay:flutter_debug:1.0")
        "withFlutterReleaseImplementation"("com.tarkovcompanion.flutter_overlay:flutter_release:1.0")
    }
}
