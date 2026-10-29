// C API shim over Session: no C++ exception or type may cross it. guarded()
// turns an exception into TARKOV_COMPANION_ERROR_INTERNAL where exceptions are
// on; built without them (Android), a failed allocation aborts.
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "contract_generated.h"
#include "session.h"
#include "tarkov_companion/tarkov_companion.h"

static_assert(TARKOV_COMPANION_ABI_VERSION == tarkov_companion::contract::kNativeAbiVersion,
              "contracts/config-limits.json shared.nativeAbiVersion is what the platform SDKs expect");

#ifndef TARKOV_COMPANION_VERSION_STRING
#  define TARKOV_COMPANION_VERSION_STRING "0.0.0-dev"
#endif

struct tarkov_companion_session {
  std::unique_ptr<tarkov_companion::Session> implementation;
};

namespace {

// Released by the caller with tarkov_companion_free(). Always NUL-terminated,
// so text results can be used as C strings.
char* copyToMallocatedCString(std::string_view text) {
  char* copy = static_cast<char*>(std::malloc(text.size() + 1));
  if (!copy) {
    return nullptr;
  }
  std::memcpy(copy, text.data(), text.size());
  copy[text.size()] = '\0';

  return copy;
}

std::string_view viewOrEmpty(const char* data, size_t length) {
  return data ? std::string_view(data, length) : std::string_view();
}

void storeErrorIfWanted(char** out_error, const std::string& message) {
  if (out_error) {
    *out_error = copyToMallocatedCString(message);
  }
}

tarkov_companion_status okOrInvalidArgument(bool succeeded) {
  return succeeded ? TARKOV_COMPANION_OK : TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
}

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#  define TARKOV_COMPANION_EXCEPTIONS_ENABLED 1
#endif

template <typename Body>
tarkov_companion_status guarded(Body&& body) noexcept {
#ifdef TARKOV_COMPANION_EXCEPTIONS_ENABLED
  try {
    return body();
  } catch (...) {
    return TARKOV_COMPANION_ERROR_INTERNAL;
  }
#else
  return body();
#endif
}

// For the functions that return nothing: a failure is dropped, not thrown.
template <typename Body>
void guardedWithoutStatus(Body&& body) noexcept {
#ifdef TARKOV_COMPANION_EXCEPTIONS_ENABLED
  try {
    body();
  } catch (...) {
  }
#else
  body();
#endif
}

}  // namespace

extern "C" {

const char* tarkov_companion_version(void) { return TARKOV_COMPANION_VERSION_STRING; }

uint32_t tarkov_companion_abi_version(void) { return TARKOV_COMPANION_ABI_VERSION; }

const char* tarkov_companion_status_string(tarkov_companion_status status) {
  switch (status) {
    case TARKOV_COMPANION_OK: return "ok";
    case TARKOV_COMPANION_ERROR_INVALID_ARGUMENT: return "invalid_argument";
    case TARKOV_COMPANION_NO_ACTION: return "no_action";
    case TARKOV_COMPANION_ERROR_INTERNAL: return "internal";
  }

  return "unknown";
}

void tarkov_companion_free(char* text) { std::free(text); }

tarkov_companion_status tarkov_companion_session_create(const char* configuration_json, size_t configuration_length,
                                                        tarkov_companion_session** out_session, char** out_error) {
  return guarded([&]() -> tarkov_companion_status {
    if (!out_session) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }
    *out_session = nullptr;

    std::string error;
    auto implementation =
        tarkov_companion::Session::create(viewOrEmpty(configuration_json, configuration_length), &error);
    if (!implementation) {
      storeErrorIfWanted(out_error, error);
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    *out_session = new tarkov_companion_session{std::move(implementation)};

    return TARKOV_COMPANION_OK;
  });
}

void tarkov_companion_session_destroy(tarkov_companion_session* session) {
  guardedWithoutStatus([&] { delete session; });
}

tarkov_companion_status tarkov_companion_session_set_profile(tarkov_companion_session* session,
                                                             const char* profile_json, size_t profile_length,
                                                             char** out_error) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    std::string error;
    if (!session->implementation->setProfile(viewOrEmpty(profile_json, profile_length), &error)) {
      storeErrorIfWanted(out_error, error);
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    return TARKOV_COMPANION_OK;
  });
}

tarkov_companion_status tarkov_companion_session_search(tarkov_companion_session* session,
                                                        tarkov_companion_request_id request_id, const char* query,
                                                        size_t query_length) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session || !query) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    return okOrInvalidArgument(session->implementation->search(request_id, std::string_view(query, query_length)));
  });
}

tarkov_companion_status tarkov_companion_session_recommend(tarkov_companion_session* session,
                                                           tarkov_companion_request_id request_id, const char* item_id,
                                                           size_t item_id_length) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session || !item_id) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    return okOrInvalidArgument(
        session->implementation->recommend(request_id, std::string_view(item_id, item_id_length)));
  });
}

void tarkov_companion_session_cancel(tarkov_companion_session* session, tarkov_companion_request_id request_id) {
  guardedWithoutStatus([&] {
    if (session) {
      session->implementation->cancel(request_id);
    }
  });
}

tarkov_companion_status tarkov_companion_session_open_bridge(tarkov_companion_session* session,
                                                             const char* options_json, size_t options_length,
                                                             tarkov_companion_bridge_id* out_bridge, char** out_error) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session || !out_bridge) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    std::string error;
    auto bridge = session->implementation->openBridge(viewOrEmpty(options_json, options_length), &error);
    if (!bridge) {
      storeErrorIfWanted(out_error, error);
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }
    *out_bridge = *bridge;

    return TARKOV_COMPANION_OK;
  });
}

tarkov_companion_status tarkov_companion_session_bridge_receive(tarkov_companion_session* session,
                                                                tarkov_companion_bridge_id bridge, const char* message,
                                                                size_t message_length) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session || !message) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    return okOrInvalidArgument(
        session->implementation->bridgeReceive(bridge, std::string_view(message, message_length)));
  });
}

tarkov_companion_status tarkov_companion_session_bridge_event(tarkov_companion_session* session,
                                                              tarkov_companion_bridge_id bridge, const char* name,
                                                              size_t name_length) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session || !name) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    return okOrInvalidArgument(session->implementation->bridgeEvent(bridge, std::string_view(name, name_length)));
  });
}

void tarkov_companion_session_close_bridge(tarkov_companion_session* session, tarkov_companion_bridge_id bridge) {
  guardedWithoutStatus([&] {
    if (session) {
      session->implementation->closeBridge(bridge);
    }
  });
}

tarkov_companion_status tarkov_companion_session_sign_in(tarkov_companion_session* session,
                                                         tarkov_companion_request_id request_id,
                                                         const uint8_t* random_bytes, size_t random_length) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session || !random_bytes) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    return okOrInvalidArgument(session->implementation->startSignIn(request_id, random_bytes, random_length));
  });
}

tarkov_companion_status tarkov_companion_session_sign_in_redirect(tarkov_companion_session* session, const char* uri,
                                                               size_t uri_length) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }
    session->implementation->signInRedirect(viewOrEmpty(uri, uri_length));

    return TARKOV_COMPANION_OK;
  });
}

tarkov_companion_status tarkov_companion_session_sign_out(tarkov_companion_session* session,
                                                          tarkov_companion_request_id request_id) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    return okOrInvalidArgument(session->implementation->signOut(request_id));
  });
}

tarkov_companion_status tarkov_companion_session_favorites_list(tarkov_companion_session* session,
                                                                tarkov_companion_request_id request_id) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    return okOrInvalidArgument(session->implementation->listFavorites(request_id));
  });
}

tarkov_companion_status tarkov_companion_session_favorite_set(tarkov_companion_session* session,
                                                              tarkov_companion_request_id request_id,
                                                              const char* item_id, size_t item_id_length,
                                                              int32_t favorite) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session || !item_id) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    std::string_view itemId(item_id, item_id_length);

    return okOrInvalidArgument(session->implementation->setFavorite(request_id, itemId, favorite != 0));
  });
}

tarkov_companion_status tarkov_companion_session_next_action(tarkov_companion_session* session, char** out_action_json,
                                                             char** out_payload, size_t* out_payload_length) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session || !out_action_json || !out_payload || !out_payload_length) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    *out_action_json = nullptr;
    *out_payload = nullptr;
    *out_payload_length = 0;

    auto action = session->implementation->nextAction();
    if (!action) {
      return TARKOV_COMPANION_NO_ACTION;
    }

    *out_action_json = copyToMallocatedCString(action->json);
    if (action->payload) {
      *out_payload = copyToMallocatedCString(*action->payload);
      *out_payload_length = action->payload->size();
    }
    bool copyFailed = !*out_action_json || (action->payload && !*out_payload);
    if (copyFailed) {
      std::free(*out_action_json);
      std::free(*out_payload);
      *out_action_json = nullptr;
      *out_payload = nullptr;
      *out_payload_length = 0;

      return TARKOV_COMPANION_ERROR_INTERNAL;
    }

    return TARKOV_COMPANION_OK;
  });
}

tarkov_companion_status tarkov_companion_session_complete_http(tarkov_companion_session* session,
                                                               tarkov_companion_action_id action_id,
                                                               int32_t status_code, const char* body,
                                                               size_t body_length) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    return okOrInvalidArgument(
        session->implementation->completeHttp(action_id, status_code, viewOrEmpty(body, body_length)));
  });
}

tarkov_companion_status tarkov_companion_session_complete_cache_read(tarkov_companion_session* session,
                                                                     tarkov_companion_action_id action_id,
                                                                     int32_t found, const char* value,
                                                                     size_t value_length) {
  return guarded([&]() -> tarkov_companion_status {
    if (!session) {
      return TARKOV_COMPANION_ERROR_INVALID_ARGUMENT;
    }

    std::optional<std::string_view> storedValue;
    if (found) {
      storedValue = viewOrEmpty(value, value_length);
    }

    return okOrInvalidArgument(session->implementation->completeStoredValueRead(action_id, storedValue));
  });
}

}  // extern "C"
