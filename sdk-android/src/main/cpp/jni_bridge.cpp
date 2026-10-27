// JNI glue between NativeBridge and the C API.
//   - UTF-8 byte[], never jstring: JNI's "modified UTF-8" mangles characters outside the BMP.
//   - Natives return a status and leave no Java exception pending, so Kotlin maps failures in one place.
//   - Bytes are copied: a critical array must not be held while the core waits for its lock.
//   - Handles are tarkov_companion_session* in a jlong; Kotlin owns their lifetime.
#include <jni.h>

#include <string>

#include "tarkov_companion/tarkov_companion.h"

namespace {

constexpr const char* kBridgeClass = "com/tarkovcompanion/sdk/internal/session/NativeBridge";

enum OutSlot : jsize {
  kSlotMain = 0,     // action JSON, or the error message of create/setProfile
  kSlotPayload = 1,  // the action's payload, left null when it has none
};

tarkov_companion_session* fromHandle(jlong handle) { return reinterpret_cast<tarkov_companion_session*>(handle); }

std::string copyBytes(JNIEnv* env, jbyteArray array) {
  std::string bytes;
  if (!array) {
    return bytes;
  }

  jsize length = env->GetArrayLength(array);
  bytes.resize(static_cast<size_t>(length));
  env->GetByteArrayRegion(array, 0, length, reinterpret_cast<jbyte*>(bytes.data()));

  return bytes;
}

// Copies `length` bytes into out[slot] as a byte[]. False if the JVM is out of
// memory; the OutOfMemoryError is cleared, the status reports it.
bool storeBytes(JNIEnv* env, jobjectArray out, OutSlot slot, const char* data, size_t length) {
  jbyteArray bytes = env->NewByteArray(static_cast<jsize>(length));
  if (!bytes) {
    env->ExceptionClear();
    return false;
  }

  env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(length), reinterpret_cast<const jbyte*>(data));
  env->SetObjectArrayElement(out, slot, bytes);
  env->DeleteLocalRef(bytes);

  return true;
}

// Moves a core-owned C string into out[slot] and frees it.
void storeAndFree(JNIEnv* env, jobjectArray out, OutSlot slot, char* text) {
  if (!text) {
    return;
  }

  if (out) {
    storeBytes(env, out, slot, text, std::char_traits<char>::length(text));
  }

  tarkov_companion_free(text);
}

jstring nativeVersion(JNIEnv* env, jclass) { return env->NewStringUTF(tarkov_companion_version()); }

jint nativeAbiVersion(JNIEnv*, jclass) { return static_cast<jint>(tarkov_companion_abi_version()); }

jlong createSession(JNIEnv* env, jclass, jbyteArray configJson, jobjectArray outError) {
  std::string config = copyBytes(env, configJson);
  tarkov_companion_session* session = nullptr;
  char* error = nullptr;
  tarkov_companion_session_create(config.data(), config.size(), &session, &error);
  storeAndFree(env, outError, kSlotMain, error);

  return reinterpret_cast<jlong>(session);
}

void destroySession(JNIEnv*, jclass, jlong handle) { tarkov_companion_session_destroy(fromHandle(handle)); }

jint setProfile(JNIEnv* env, jclass, jlong handle, jbyteArray profileJson, jobjectArray outError) {
  std::string profile = copyBytes(env, profileJson);
  char* error = nullptr;
  tarkov_companion_status status =
      tarkov_companion_session_set_profile(fromHandle(handle), profile.data(), profile.size(), &error);
  storeAndFree(env, outError, kSlotMain, error);

  return status;
}

jint search(JNIEnv* env, jclass, jlong handle, jlong requestId, jbyteArray query) {
  if (!query) {
    return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
  }

  std::string text = copyBytes(env, query);

  return tarkov_companion_session_search(fromHandle(handle), static_cast<tarkov_companion_request_id>(requestId),
                                         text.data(), text.size());
}

jint recommend(JNIEnv* env, jclass, jlong handle, jlong requestId, jbyteArray itemId) {
  if (!itemId) {
    return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
  }

  std::string id = copyBytes(env, itemId);

  return tarkov_companion_session_recommend(fromHandle(handle), static_cast<tarkov_companion_request_id>(requestId),
                                            id.data(), id.size());
}

void cancel(JNIEnv*, jclass, jlong handle, jlong requestId) {
  tarkov_companion_session_cancel(fromHandle(handle), static_cast<tarkov_companion_request_id>(requestId));
}

jint nextAction(JNIEnv* env, jclass, jlong handle, jobjectArray out) {
  char* actionJson = nullptr;
  char* payload = nullptr;
  size_t payloadLength = 0;
  tarkov_companion_status status =
      tarkov_companion_session_next_action(fromHandle(handle), &actionJson, &payload, &payloadLength);
  if (status != TARKOV_COMPANION_OK) {
    return status;
  }

  bool stored = storeBytes(env, out, kSlotMain, actionJson, std::char_traits<char>::length(actionJson));
  if (stored && payload) {
    stored = storeBytes(env, out, kSlotPayload, payload, payloadLength);
  }

  tarkov_companion_free(actionJson);
  tarkov_companion_free(payload);

  // The action is already taken from the session and lost: say so, so the
  // platform logs it instead of reading a half-filled output.
  return stored ? TARKOV_COMPANION_OK : TARKOV_COMPANION_ERROR_INTERNAL;
}

jint completeHttp(JNIEnv* env, jclass, jlong handle, jlong actionId, jint statusCode, jbyteArray body) {
  std::string bytes = copyBytes(env, body);

  return tarkov_companion_session_complete_http(fromHandle(handle), static_cast<tarkov_companion_action_id>(actionId),
                                                statusCode, bytes.data(), bytes.size());
}

jint completeCacheRead(JNIEnv* env, jclass, jlong handle, jlong actionId, jboolean found, jbyteArray value) {
  std::string bytes = copyBytes(env, value);

  return tarkov_companion_session_complete_cache_read(
      fromHandle(handle), static_cast<tarkov_companion_action_id>(actionId), found ? 1 : 0, bytes.data(), bytes.size());
}

jlong openBridge(JNIEnv* env, jclass, jlong handle, jbyteArray optionsJson, jobjectArray outError) {
  std::string options = copyBytes(env, optionsJson);
  tarkov_companion_bridge_id bridge = 0;
  char* error = nullptr;
  tarkov_companion_session_open_bridge(fromHandle(handle), options.data(), options.size(), &bridge, &error);
  storeAndFree(env, outError, kSlotMain, error);

  return static_cast<jlong>(bridge);  // ids start at 1: 0 means failure
}

jint bridgeReceive(JNIEnv* env, jclass, jlong handle, jlong bridge, jbyteArray message) {
  if (!message) {
    return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
  }

  std::string bytes = copyBytes(env, message);

  return tarkov_companion_session_bridge_receive(fromHandle(handle), static_cast<tarkov_companion_bridge_id>(bridge),
                                                 bytes.data(), bytes.size());
}

jint bridgeEvent(JNIEnv* env, jclass, jlong handle, jlong bridge, jbyteArray name) {
  if (!name) {
    return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
  }

  std::string bytes = copyBytes(env, name);

  return tarkov_companion_session_bridge_event(fromHandle(handle), static_cast<tarkov_companion_bridge_id>(bridge),
                                               bytes.data(), bytes.size());
}

void closeBridge(JNIEnv*, jclass, jlong handle, jlong bridge) {
  tarkov_companion_session_close_bridge(fromHandle(handle), static_cast<tarkov_companion_bridge_id>(bridge));
}

jint signIn(JNIEnv* env, jclass, jlong handle, jlong requestId, jbyteArray randomBytes) {
  if (!randomBytes) {
    return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
  }

  std::string bytes = copyBytes(env, randomBytes);

  return tarkov_companion_session_sign_in(fromHandle(handle), static_cast<tarkov_companion_request_id>(requestId),
                                          reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

// A null uri: the player came back without a redirect.
jint signInRedirect(JNIEnv* env, jclass, jlong handle, jbyteArray uri) {
  std::string bytes = copyBytes(env, uri);
  return tarkov_companion_session_sign_in_redirect(fromHandle(handle), bytes.data(), bytes.size());
}

jint signOut(JNIEnv*, jclass, jlong handle, jlong requestId) {
  return tarkov_companion_session_sign_out(fromHandle(handle), static_cast<tarkov_companion_request_id>(requestId));
}

jint favoritesList(JNIEnv*, jclass, jlong handle, jlong requestId) {
  return tarkov_companion_session_favorites_list(fromHandle(handle),
                                                 static_cast<tarkov_companion_request_id>(requestId));
}

jint favoriteSet(JNIEnv* env, jclass, jlong handle, jlong requestId, jbyteArray itemId, jboolean favorite) {
  if (!itemId) {
    return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
  }

  std::string id = copyBytes(env, itemId);

  return tarkov_companion_session_favorite_set(fromHandle(handle), static_cast<tarkov_companion_request_id>(requestId),
                                               id.data(), id.size(), favorite ? 1 : 0);
}

const JNINativeMethod kMethods[] = {
    {"nativeVersion", "()Ljava/lang/String;", reinterpret_cast<void*>(nativeVersion)},
    {"nativeAbiVersion", "()I", reinterpret_cast<void*>(nativeAbiVersion)},
    {"createSession", "([B[[B)J", reinterpret_cast<void*>(createSession)},
    {"destroySession", "(J)V", reinterpret_cast<void*>(destroySession)},
    {"setProfile", "(J[B[[B)I", reinterpret_cast<void*>(setProfile)},
    {"search", "(JJ[B)I", reinterpret_cast<void*>(search)},
    {"recommend", "(JJ[B)I", reinterpret_cast<void*>(recommend)},
    {"cancel", "(JJ)V", reinterpret_cast<void*>(cancel)},
    {"nextAction", "(J[[B)I", reinterpret_cast<void*>(nextAction)},
    {"completeHttp", "(JJI[B)I", reinterpret_cast<void*>(completeHttp)},
    {"completeCacheRead", "(JJZ[B)I", reinterpret_cast<void*>(completeCacheRead)},
    {"openBridge", "(J[B[[B)J", reinterpret_cast<void*>(openBridge)},
    {"bridgeReceive", "(JJ[B)I", reinterpret_cast<void*>(bridgeReceive)},
    {"bridgeEvent", "(JJ[B)I", reinterpret_cast<void*>(bridgeEvent)},
    {"closeBridge", "(JJ)V", reinterpret_cast<void*>(closeBridge)},
    {"signIn", "(JJ[B)I", reinterpret_cast<void*>(signIn)},
    {"signInRedirect", "(J[B)I", reinterpret_cast<void*>(signInRedirect)},
    {"signOut", "(JJ)I", reinterpret_cast<void*>(signOut)},
    {"favoritesList", "(JJ)I", reinterpret_cast<void*>(favoritesList)},
    {"favoriteSet", "(JJ[BZ)I", reinterpret_cast<void*>(favoriteSet)},
};

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  jclass bridgeClass = env->FindClass(kBridgeClass);
  // R8 renamed it: consumer-rules.pro is missing from the host build
  if (!bridgeClass) {
    return JNI_ERR;
  }

  jint result = env->RegisterNatives(bridgeClass, kMethods, sizeof(kMethods) / sizeof(kMethods[0]));
  env->DeleteLocalRef(bridgeClass);

  return result == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}
