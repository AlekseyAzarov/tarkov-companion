plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.android) apply false
}

tasks.register<FootprintReportTask>("footprintReport") {
    group = "verification"
    description = "Measures what the SDK adds to a release build of the host game."
    dependsOn(":host-game:assembleWithSdkRelease", ":host-game:assembleBaselineRelease", ":sdk-android:assembleRelease")
    withSdkApkDirectory.set(file("host-game/build/outputs/apk/withSdk/release"))
    baselineApkDirectory.set(file("host-game/build/outputs/apk/baseline/release"))
    aar.set(file("sdk-android/build/outputs/aar/sdk-android-release.aar"))
    report.set(layout.buildDirectory.file("reports/footprint.md"))
}
