pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()

        // The Flutter overlay experiment, once `flutter build aar` has run (experiments/flutter_overlay/README.md).
        val flutterOverlayRepository = file("experiments/flutter_overlay/build/host/outputs/repo")
        if (flutterOverlayRepository.isDirectory) {
            maven {
                url = flutterOverlayRepository.toURI()
                content { includeGroup("com.tarkovcompanion.flutter_overlay") }
            }
            maven {
                url = uri("https://storage.googleapis.com/download.flutter.io")
                content { includeGroup("io.flutter") }
            }
        }
    }
}

rootProject.name = "tarkov-companion"

include(":sdk-android")
include(":host-game")
