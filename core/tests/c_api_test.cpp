// The C boundary itself: ownership of returned strings, payload bytes, error
// reporting and null handling. Behaviour is covered by session_test.cpp.
#include "tarkov_companion/tarkov_companion.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

#include "json.h"
#include "test_util.h"

namespace {

struct SessionDeleter {
  void operator()(tarkov_companion_session* session) const { tarkov_companion_session_destroy(session); }
};
using SessionPtr = std::unique_ptr<tarkov_companion_session, SessionDeleter>;

// Owns a string returned by the C API.
struct CString {
  char* pointer = nullptr;
  ~CString() { tarkov_companion_free(pointer); }
  std::string text() const { return pointer ? pointer : ""; }
};

struct TakenAction {
  tarkov_companion_status status = TARKOV_COMPANION_NO_ACTION;
  tarkov_companion::JsonValue json;
  std::string payload;
  bool hasPayload = false;
};

TakenAction takeAction(tarkov_companion_session* session) {
  CString json;
  CString payload;
  size_t payloadLength = 0;
  TakenAction action;
  action.status = tarkov_companion_session_next_action(session, &json.pointer, &payload.pointer, &payloadLength);
  if (action.status != TARKOV_COMPANION_OK) {
    return action;
  }

  action.json = tarkov_companion::parseJson(json.text()).value;
  action.hasPayload = payload.pointer != nullptr;
  if (payload.pointer) {
    action.payload.assign(payload.pointer, payloadLength);
  }

  return action;
}

// A session past its start-up reads (the favourites list, from an empty store).
SessionPtr createSession(const std::string& config = "") {
  tarkov_companion_session* session = nullptr;
  EXPECT_EQ(tarkov_companion_session_create(config.data(), config.size(), &session, nullptr), TARKOV_COMPANION_OK);
  for (TakenAction action = takeAction(session); action.status == TARKOV_COMPANION_OK; action = takeAction(session)) {
    if (action.json["type"].asString() == "data_read") {
      tarkov_companion_session_complete_cache_read(
          session, static_cast<tarkov_companion_action_id>(action.json["id"].asInteger()), 0, nullptr, 0);
    }
  }

  return SessionPtr(session);
}

TEST(CApi, VersionMatchesRepository) {
  EXPECT_EQ(tarkov_companion_abi_version(), TARKOV_COMPANION_ABI_VERSION);
  EXPECT_GT(std::strlen(tarkov_companion_version()), 0u);
  EXPECT_STREQ(tarkov_companion_status_string(TARKOV_COMPANION_NO_ACTION), "no_action");
  EXPECT_STREQ(tarkov_companion_status_string(TARKOV_COMPANION_ERROR_INTERNAL), "internal");
}

TEST(CApi, InvalidConfigIsExplained) {
  const std::string config = R"({"language":"english"})";
  tarkov_companion_session* session = nullptr;
  CString error;
  EXPECT_EQ(tarkov_companion_session_create(config.data(), config.size(), &session, &error.pointer),
            TARKOV_COMPANION_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(session, nullptr);
  EXPECT_NE(error.text().find("language"), std::string::npos) << error.text();

  SessionPtr valid = createSession();
  const std::string profile = R"({"playerLevel":0})";
  CString profileError;
  EXPECT_EQ(tarkov_companion_session_set_profile(valid.get(), profile.data(), profile.size(), &profileError.pointer),
            TARKOV_COMPANION_ERROR_INVALID_ARGUMENT);
  EXPECT_NE(profileError.text().find("playerLevel"), std::string::npos);
}

TEST(CApi, NullConfigMeansDefaults) {
  tarkov_companion_session* session = nullptr;
  ASSERT_EQ(tarkov_companion_session_create(nullptr, 0, &session, nullptr), TARKOV_COMPANION_OK);
  tarkov_companion_session_destroy(session);
}

TEST(CApi, SearchRoundTripCarriesBodiesAsPayloadBytes) {
  SessionPtr session = createSession();
  const std::string query = "salewa";
  ASSERT_EQ(tarkov_companion_session_search(session.get(), 7, query.data(), query.size()), TARKOV_COMPANION_OK);

  TakenAction http = takeAction(session.get());
  ASSERT_EQ(http.status, TARKOV_COMPANION_OK);
  EXPECT_EQ(http.json["type"].asString(), "http");
  ASSERT_TRUE(http.hasPayload);
  EXPECT_EQ(http.payload.rfind(R"({"query":"query ItemSearch)", 0), 0u) << http.payload;

  std::string response = tarkov_companion::test::readFixture("item_search.json");
  ASSERT_EQ(tarkov_companion_session_complete_http(session.get(),
                                                   static_cast<tarkov_companion_action_id>(http.json["id"].asInteger()),
                                                   200, response.data(), response.size()),
            TARKOV_COMPANION_OK);

  TakenAction write = takeAction(session.get());
  EXPECT_EQ(write.json["type"].asString(), "cache_write");
  EXPECT_NE(write.payload.find(response), std::string::npos) << "the response is stored verbatim";

  TakenAction result = takeAction(session.get());
  EXPECT_EQ(result.json["type"].asString(), "result");
  EXPECT_EQ(result.json["request"].asInteger(), 7);
  ASSERT_TRUE(result.hasPayload);
  EXPECT_EQ(tarkov_companion::parseJson(result.payload).value["items"].items().size(), 4u);

  EXPECT_EQ(takeAction(session.get()).status, TARKOV_COMPANION_NO_ACTION);
}

TEST(CApi, PayloadsMayContainNulBytes) {
  SessionPtr session = createSession();
  const std::string query("sale\0wa", 7);
  ASSERT_EQ(tarkov_companion_session_search(session.get(), 1, query.data(), query.size()), TARKOV_COMPANION_OK);
  TakenAction http = takeAction(session.get());
  EXPECT_NE(http.payload.find("sale\\u0000wa"), std::string::npos) << "escaped inside the GraphQL body";
}

TEST(CApi, RejectsNullArgumentsAndUnknownActions) {
  SessionPtr session = createSession();
  char* json = nullptr;
  char* payload = nullptr;
  size_t length = 0;
  EXPECT_EQ(tarkov_companion_session_create(nullptr, 0, nullptr, nullptr), TARKOV_COMPANION_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(tarkov_companion_session_search(nullptr, 1, "salewa", 6), TARKOV_COMPANION_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(tarkov_companion_session_search(session.get(), 1, nullptr, 0), TARKOV_COMPANION_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(tarkov_companion_session_recommend(session.get(), 1, nullptr, 0), TARKOV_COMPANION_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(tarkov_companion_session_next_action(session.get(), nullptr, &payload, &length),
            TARKOV_COMPANION_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(tarkov_companion_session_next_action(session.get(), &json, &payload, &length), TARKOV_COMPANION_NO_ACTION);
  EXPECT_EQ(json, nullptr);
  EXPECT_EQ(tarkov_companion_session_complete_http(session.get(), 99, 200, "{}", 2),
            TARKOV_COMPANION_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(tarkov_companion_session_complete_cache_read(session.get(), 99, 0, nullptr, 0),
            TARKOV_COMPANION_ERROR_INVALID_ARGUMENT);
  tarkov_companion_session_cancel(nullptr, 1);  // must be a no-op
  tarkov_companion_session_destroy(nullptr);
  tarkov_companion_free(nullptr);
}

TEST(CApi, InvalidInputIsReportedInTheResult) {
  SessionPtr session = createSession();
  ASSERT_EQ(tarkov_companion_session_search(session.get(), 3, "x", 1), TARKOV_COMPANION_OK);
  TakenAction result = takeAction(session.get());
  EXPECT_FALSE(result.json["ok"].asBool());
  EXPECT_EQ(result.json["error"]["code"].asString(), "invalid_argument");
  EXPECT_FALSE(result.hasPayload);
}

}  // namespace
