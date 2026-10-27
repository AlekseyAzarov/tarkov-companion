plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

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
}
