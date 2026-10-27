# Rules applied to the host app's R8 run.

# JNI_OnLoad registers natives on this class by name.
-keep,includedescriptorclasses class com.tarkovcompanion.sdk.internal.session.NativeBridge {
    native <methods>;
}

# Engine plugins (Unity, Unreal, custom C++) commonly reach the SDK through
# JNI reflection, which R8 cannot see. Keep the static entry points stable.
-keep class com.tarkovcompanion.sdk.TarkovCompanion {
    public static *** showItemLookup(...);
    public static *** isInitialized();
}
