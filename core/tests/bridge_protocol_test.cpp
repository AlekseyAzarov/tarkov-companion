#include "bridge_protocol.h"

#include <gtest/gtest.h>

#include <string>

#include "json.h"

namespace tarkov_companion::bridge {
namespace {

std::string message(const std::string& type, const std::string& payload = "{}", const std::string& id = "w1",
                    int version = 1) {
  return R"({"version":)" + std::to_string(version) + R"(,"id":")" + id + R"(","type":")" + type +
         R"(","payload":)" + payload + "}";
}

Request ok(const std::string& raw) {
  ParsedMessage parsed = parseMessage(raw);
  EXPECT_TRUE(parsed.validRequest.has_value()) << raw << " -> " << parsed.errorMessage;

  return parsed.validRequest.value_or(Request{});
}

ParsedMessage invalid(const std::string& raw) {
  ParsedMessage parsed = parseMessage(raw);
  EXPECT_FALSE(parsed.validRequest.has_value()) << raw;

  return parsed;
}

TEST(BridgeProtocol, ParsesEveryRequestType) {
  Request hello = ok(message("hello", R"({"protocol":1,"minimumNativeProtocol":1,"bundleVersion":"0.1.0"})"));
  EXPECT_EQ(hello.type, RequestType::Hello);
  EXPECT_EQ(hello.helloBundleVersion, "0.1.0");
  EXPECT_EQ(ok(message("search", R"({"query":"  salewa "})")).trimmedSearchQuery, "salewa");
  EXPECT_EQ(ok(message("recommend", R"({"itemId":"544fb45d4bdc2dee738b4568"})")).type, RequestType::Recommend);

  Request track = ok(message("track", R"({"event":"item_opened","properties":{"source":"search","n":3,"x":1.5,"b":true}})"));
  EXPECT_EQ(track.trackProperties, (AnalyticsProperties{{"source", "search"}, {"n", "3"}, {"x", "1.5"}, {"b", "true"}}));

  Request state = ok(message("state", R"({"route":"item","itemId":"abc"})"));
  EXPECT_EQ(state.reportedState.route, "item");
  EXPECT_TRUE(state.reportedState.query.empty());
  EXPECT_EQ(ok(message("close")).type, RequestType::Close);
}

TEST(BridgeProtocol, ToleratesUnknownFieldsAndNewerEnvelopeVersions) {
  ok(R"({"version":3,"id":"w1","type":"search","payload":{"query":"gpu","future":1},"trace":"x"})");
}

TEST(BridgeProtocol, UnknownTypeIsAnsweredNotDropped) {
  ParsedMessage rejection = invalid(message("teleport"));
  EXPECT_EQ(rejection.idToAnswerWithError, "w1");
  EXPECT_EQ(rejection.errorCode, contract::kBridgeErrorCodeUnsupportedType);
}

TEST(BridgeProtocol, RejectsMalformedEnvelopesWithoutEchoingAnId) {
  for (const std::string& raw : {std::string(""), std::string("nope"), std::string("[]"),
                                 std::string(R"({"type":"search"})"), message("search", "{}", "has space"),
                                 message("search", "{}", std::string(33, 'x')), std::string(R"({"version":1,"id":7})")}) {
    ParsedMessage rejection = invalid(raw);
    EXPECT_FALSE(rejection.idToAnswerWithError.has_value()) << raw;
    EXPECT_EQ(rejection.errorCode, contract::kBridgeErrorCodeInvalidMessage);
  }

  EXPECT_EQ(invalid(message("search", "{}", "w1", 0)).idToAnswerWithError, "w1");
}

TEST(BridgeProtocol, EnforcesPayloadLimits) {
  std::string elevenProps;
  for (int index = 1; index <= 11; ++index) elevenProps += (index > 1 ? "," : "") + ("\"k" + std::to_string(index) + "\":1");
  for (const std::string& raw : {
           message("search", R"({"query":"a"})"),
           message("search", R"({"query":")" + std::string(65, 'a') + R"("})"),
           message("search", R"({"query":42})"),
           message("recommend", R"({"itemId":"../../x"})"),
           message("state", R"({"route":"settings"})"),
           message("track", R"({"event":"Bad-Name"})"),
           message("track", R"({"event":"ok","properties":{"nested":{"a":1}}})"),
           message("track", R"({"event":"ok","properties":{"version":")" + std::string(101, 'x') + R"("}})"),
           message("track", R"({"event":"ok","properties":{)" + elevenProps + "}}"),
           message("hello", R"({"protocol":1,"minimumNativeProtocol":1,"bundleVersion":"<script>"})"),
           message("hello", R"({"protocol":1.5,"minimumNativeProtocol":1,"bundleVersion":"0.1.0"})"),
       }) {
    ParsedMessage rejection = invalid(raw);
    EXPECT_EQ(rejection.errorCode, contract::kBridgeErrorCodeInvalidMessage) << raw;
    EXPECT_EQ(rejection.idToAnswerWithError, "w1") << raw;
  }
}

TEST(BridgeProtocol, CapsMessageSizeBeforeParsing) {
  ParsedMessage rejection = invalid(message("search", R"({"query":")" + std::string(kMaximumMessageBytes, 'a') + R"("})"));
  EXPECT_FALSE(rejection.idToAnswerWithError.has_value());
  EXPECT_EQ(rejection.errorMessage, "message too large");
}

TEST(BridgeProtocol, QueryLimitsCountCharactersNotBytes) {
  std::string cyrillic;
  for (int letter = 0; letter < 40; ++letter) cyrillic += "а";  // 80 bytes, 40 characters
  EXPECT_EQ(ok(message("search", R"({"query":")" + cyrillic + R"("})")).trimmedSearchQuery, cyrillic);
}

TEST(BridgeProtocol, EncodesResponsesAndEventsInTheDocumentedEnvelope) {
  JsonValue success = parseJson(okMessage("w1", R"({"a":1})")).value;
  EXPECT_EQ(success["version"].asInteger(), kNativeProtocolVersion);
  EXPECT_EQ(success["replyTo"].asString(), "w1");
  EXPECT_TRUE(success["ok"].asBool());
  EXPECT_EQ(success["payload"]["a"].asInteger(), 1);

  JsonValue failure = parseJson(errorMessage(std::nullopt, contract::kBridgeErrorCodeInvalidMessage, "m")).value;
  EXPECT_TRUE(failure.hasKey("replyTo"));
  EXPECT_TRUE(failure["replyTo"].isNull());
  EXPECT_EQ(failure["error"]["code"].asString(), "invalid_message");

  EXPECT_EQ(parseJson(eventMessage("navigate_back")).value["event"].asString(), "navigate_back");
}

}  // namespace
}  // namespace tarkov_companion::bridge
