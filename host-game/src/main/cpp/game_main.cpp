// Game loop on android_native_app_glue. The game never links against the SDK:
// it asks its platform layer (GameActivity) to open the overlay.
#include <EGL/egl.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <algorithm>
#include <chrono>

#include "renderer.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "RaidSim", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "RaidSim", __VA_ARGS__)

namespace {

using SteadyClock = std::chrono::steady_clock;

// ALooper_pollOnce timeouts.
constexpr int kPollWithoutWaiting = 0;
constexpr int kPollUntilEvent = -1;

// A long hitch (debugger, backgrounding) must not teleport the crates.
constexpr float kMaxFrameDeltaSeconds = 0.1f;
// Frame-time stats are logged every this many frames (~5 s at 60 fps).
constexpr int kFramesPerStatsReport = 300;

constexpr EGLint kColorChannelBits = 8;
constexpr EGLint kGlesVersion = 2;

struct Game {
  // Frame-time stats: show the game pausing while the overlay is up and
  // recovering after it closes. First, being the 8-byte fields.
  SteadyClock::time_point lastFrame{};
  double accumulatedFrameMs = 0;
  double worstFrameMs = 0;
  int framesSinceReport = 0;

  android_app* app = nullptr;
  JNIEnv* env = nullptr;  // game thread's JNI env, attached once
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLSurface surface = EGL_NO_SURFACE;
  EGLContext context = EGL_NO_CONTEXT;
  Renderer renderer;
  bool resumed = false;
  bool focused = false;

  bool animating() const { return resumed && focused && surface != EGL_NO_SURFACE; }
};

void resizeToSurface(Game& game) {
  EGLint width = 0;
  EGLint height = 0;
  eglQuerySurface(game.display, game.surface, EGL_WIDTH, &width);
  eglQuerySurface(game.display, game.surface, EGL_HEIGHT, &height);
  game.renderer.resize(width, height);
}

bool initDisplay(Game& game) {
  game.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  eglInitialize(game.display, nullptr, nullptr);
  const EGLint configAttributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                     EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
                                     EGL_RED_SIZE,        kColorChannelBits,
                                     EGL_GREEN_SIZE,      kColorChannelBits,
                                     EGL_BLUE_SIZE,       kColorChannelBits,
                                     EGL_NONE};
  EGLConfig config;
  EGLint configCount = 0;
  eglChooseConfig(game.display, configAttributes, &config, 1, &configCount);
  if (configCount == 0) {
    return false;
  }

  game.surface = eglCreateWindowSurface(game.display, config, game.app->window, nullptr);
  const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, kGlesVersion, EGL_NONE};
  game.context = eglCreateContext(game.display, config, EGL_NO_CONTEXT, contextAttributes);
  if (eglMakeCurrent(game.display, game.surface, game.surface, game.context) == EGL_FALSE) {
    return false;
  }

  if (!game.renderer.init()) {
    return false;
  }

  resizeToSurface(game);
  game.lastFrame = SteadyClock::now();
  LOGI("display ready");

  return true;
}

void termDisplay(Game& game) {
  if (game.display != EGL_NO_DISPLAY) {
    game.renderer.release();
    eglMakeCurrent(game.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (game.context != EGL_NO_CONTEXT) {
      eglDestroyContext(game.display, game.context);
    }

    if (game.surface != EGL_NO_SURFACE) {
      eglDestroySurface(game.display, game.surface);
    }

    eglTerminate(game.display);
  }

  game.display = EGL_NO_DISPLAY;
  game.surface = EGL_NO_SURFACE;
  game.context = EGL_NO_CONTEXT;
}

// Calls GameActivity.openCompanionFromNative(). GetObjectClass on the activity
// instance resolves our app class; FindClass from this native thread would
// search the system class loader and fail.
void openCompanion(Game& game) {
  JNIEnv* env = game.env;
  jobject activity = game.app->activity->clazz;
  jclass activityClass = env->GetObjectClass(activity);
  jmethodID openMethod = env->GetMethodID(activityClass, "openCompanionFromNative", "()V");
  if (openMethod) {
    env->CallVoidMethod(activity, openMethod);
  }

  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    LOGW("openCompanionFromNative failed (R8 keep rule missing?)");
  }

  env->DeleteLocalRef(activityClass);
}

void onCommand(android_app* app, int32_t command) {
  auto& game = *static_cast<Game*>(app->userData);
  switch (command) {
    case APP_CMD_INIT_WINDOW:
      if (app->window && !initDisplay(game)) {
        LOGW("EGL init failed");
      }

      break;
    case APP_CMD_TERM_WINDOW:
      termDisplay(game);
      break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
      if (game.surface != EGL_NO_SURFACE) {
        resizeToSurface(game);
      }

      break;
    case APP_CMD_GAINED_FOCUS:
      game.focused = true;
      game.lastFrame = SteadyClock::now();  // don't count the paused time as a frame
      LOGI("focus gained: rendering");
      break;
    case APP_CMD_LOST_FOCUS:
      game.focused = false;
      LOGI("focus lost: render loop idle");
      break;
    case APP_CMD_RESUME:
      game.resumed = true;
      break;
    case APP_CMD_PAUSE:
      game.resumed = false;
      break;
    default:
      break;
  }
}

int32_t onInput(android_app* app, AInputEvent* event) {
  constexpr int32_t kHandled = 1;
  constexpr int32_t kNotHandled = 0;
  constexpr size_t kFirstPointer = 0;

  auto& game = *static_cast<Game*>(app->userData);
  if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
    return kNotHandled;
  }

  if ((AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) != AMOTION_EVENT_ACTION_UP) {
    return kHandled;
  }

  float x = AMotionEvent_getX(event, kFirstPointer);
  float y = AMotionEvent_getY(event, kFirstPointer);
  if (game.renderer.companionButtonHit(x, y)) {
    game.renderer.pressButton();
    openCompanion(game);
  }

  return kHandled;
}

void recordFrame(Game& game, double frameMs) {
  game.accumulatedFrameMs += frameMs;
  game.worstFrameMs = frameMs > game.worstFrameMs ? frameMs : game.worstFrameMs;
  if (++game.framesSinceReport >= kFramesPerStatsReport) {
    LOGI("frames: avg %.2f ms, worst %.2f ms", game.accumulatedFrameMs / game.framesSinceReport, game.worstFrameMs);
    game.accumulatedFrameMs = 0;
    game.framesSinceReport = 0;
    game.worstFrameMs = 0;
  }
}

}  // namespace

void android_main(android_app* app) {
  Game game;
  game.app = app;
  app->userData = &game;
  app->onAppCmd = onCommand;
  app->onInputEvent = onInput;
  app->activity->vm->AttachCurrentThread(&game.env, nullptr);

  while (true) {
    int events = 0;
    android_poll_source* source = nullptr;
    // Block while paused: an idle game must not burn battery under the overlay.
    int timeout = game.animating() ? kPollWithoutWaiting : kPollUntilEvent;
    while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
      if (source) {
        source->process(app, source);
      }

      if (app->destroyRequested) {
        termDisplay(game);
        app->activity->vm->DetachCurrentThread();

        return;
      }

      timeout = game.animating() ? kPollWithoutWaiting : kPollUntilEvent;
    }

    if (game.animating()) {
      auto now = SteadyClock::now();
      std::chrono::duration<float> deltaSeconds = now - game.lastFrame;
      std::chrono::duration<double, std::milli> frameMs = now - game.lastFrame;
      game.lastFrame = now;
      game.renderer.update(std::min(deltaSeconds.count(), kMaxFrameDeltaSeconds));
      game.renderer.draw();
      eglSwapBuffers(game.display, game.surface);
      recordFrame(game, frameMs.count());
    }
  }
}
