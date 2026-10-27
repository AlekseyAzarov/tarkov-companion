// contracts/fixtures/bridge_transcript.json through a real session: the
// protocol as the web bundle sees it. web-ui/test/transcript.test.ts checks
// the same file from the other side, so the two implementations can't drift
// apart without one of the suites failing.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fake_platform.h"
#include "json.h"
#include "session.h"
#include "test_util.h"
#include "http.h"

namespace tarkov_companion {
namespace {

constexpr std::string_view kAnyValue = "*";

// Serializes a parsed fixture value back to text, to send it as a message.
void write(JsonWriter& writer, const JsonValue& value) {
  switch (value.type()) {
    case JsonValue::Type::Null: writer.null(); break;
    case JsonValue::Type::Bool: writer.value(value.asBool()); break;
    case JsonValue::Type::Number: writer.value(value.asNumber()); break;
    case JsonValue::Type::String: writer.value(value.asString()); break;
    case JsonValue::Type::Array:
      writer.beginArray();
      for (const auto& item : value.items()) write(writer, item);
      writer.endArray();
      break;
    case JsonValue::Type::Object:
      writer.beginObject();
      for (const auto& [key, member] : value.members()) {
        writer.key(key);
        write(writer, member);
      }

      writer.endObject();
      break;
  }
}

std::string toText(const JsonValue& value) {
  JsonWriter writer;
  write(writer, value);

  return writer.takeText();
}

// `expected` matches if every field it has is in `actual` with a matching
// value; "*" matches anything; arrays must match element by element.
bool matches(const JsonValue& expected, const JsonValue& actual, const std::string& path, std::string* mismatch) {
  if (expected.isString() && expected.asString() == kAnyValue) {
    return true;
  }

  if (expected.type() != actual.type()) {
    *mismatch = path + ": different type";
    return false;
  }

  switch (expected.type()) {
    case JsonValue::Type::Object:
      for (const auto& [key, member] : expected.members()) {
        if (!actual.hasKey(key)) {
          *mismatch = path + "." + key + ": missing";
          return false;
        }

        if (!matches(member, actual[key], path + "." + key, mismatch)) {
          return false;
        }
      }

      return true;
    case JsonValue::Type::Array:
      if (expected.items().size() != actual.items().size()) {
        *mismatch = path + ": different length";
        return false;
      }

      for (size_t index = 0; index < expected.items().size(); ++index) {
        if (!matches(expected.items()[index], actual.items()[index], path + "[" + std::to_string(index) + "]",
                     mismatch)) {
          return false;
        }
      }

      return true;
    default:
      if (toText(expected) != toText(actual)) {
        *mismatch = path + ": " + toText(expected) + " != " + toText(actual);
        return false;
      }

      return true;
  }
}

void expectEventsInCatalog(const std::vector<std::pair<std::string, std::map<std::string, std::string>>>& tracked);

TEST(BridgeTranscript, SessionAnswersAsDocumented) {
  JsonParseResult transcript = parseJson(test::readFixture("bridge_transcript.json"));
  ASSERT_TRUE(transcript.ok);

  const JsonValue& flags = transcript.value["remoteFlags"];
  test::FakePlatform platform;
  platform.getReplies[std::string(flags["url"].asString())] = {http::kStatusOk, toText(flags["document"])};

  std::string error;
  JsonWriter config;
  config.beginObject().key("remoteFlagsUrl").value(flags["url"].asString()).endObject();
  auto session = Session::create(config.text(), &error);
  ASSERT_NE(session, nullptr) << error;
  platform.drain(*session);
  ASSERT_EQ(platform.launchAllowed, std::vector<bool>{true});
  auto bridge = session->openBridge(toText(transcript.value["bridgeOptions"]), &error);
  ASSERT_TRUE(bridge.has_value()) << error;

  size_t answered = 0;
  for (const JsonValue& exchange : transcript.value["exchanges"].items()) {
    std::string about(exchange["about"].asString());
    const JsonValue& web = exchange["web"];
    ASSERT_TRUE(session->bridgeReceive(*bridge, web.isString() ? std::string(web.asString()) : toText(web)));
    platform.drain(*session);

    std::vector<std::string>& sent = platform.sentToWeb[*bridge];
    const auto& expected = exchange["native"].items();
    ASSERT_EQ(sent.size() - answered, expected.size()) << about;
    for (const JsonValue& message : expected) {
      std::string mismatch;
      JsonParseResult actual = parseJson(sent[answered]);
      ASSERT_TRUE(actual.ok) << sent[answered];
      EXPECT_TRUE(matches(message, actual.value, "message", &mismatch))
          << about << ": " << mismatch << "\n  actual: " << sent[answered];
      ++answered;
    }
  }

  EXPECT_EQ(platform.closedScreens, std::vector<BridgeId>{*bridge}) << "the last exchange closes the screen";
  EXPECT_EQ(platform.bridgeStates[*bridge]["itemId"].asString(), "544fb45d4bdc2dee738b4568");

  expectEventsInCatalog(platform.tracked);
}

// Every event the bridge sends is exercised by the transcript. (That each
// one is in contracts/analytics-events.json, with only its listed properties,
// FakePlatform checks for every test.)
void expectEventsInCatalog(const std::vector<std::pair<std::string, std::map<std::string, std::string>>>& tracked) {
  // Sign-in events come from the authentication flow, not from the bridge.
  const std::set<std::string> signInEvents = {contract::kEventSignInCompleted, contract::kEventSignedOut};
  std::set<std::string> seen;
  for (const auto& entry : tracked) {
    seen.insert(entry.first);
  }

  for (const auto& [event, spec] : test::analyticsCatalog()["events"].members()) {
    bool bridgeEvent = spec["emittedBy"].asString() == "core" && signInEvents.count(event) == 0;
    if (bridgeEvent) {
      EXPECT_EQ(seen.count(event), 1u) << event << " never sent";
    }
  }
}

}  // namespace
}  // namespace tarkov_companion
