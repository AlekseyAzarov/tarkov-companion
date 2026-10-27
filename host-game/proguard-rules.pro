# The C++ game loop calls this through JNI (game_main.cpp: openCompanion).
-keep class com.tarkovcompanion.hostgame.GameActivity {
    public void openCompanionFromNative();
}
