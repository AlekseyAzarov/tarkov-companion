#include "remote_flags.h"

#include <gtest/gtest.h>

#include "fake_platform.h"
#include "session.h"
#include "http.h"

namespace tarkov_companion {
namespace {

constexpr const char* kRemoteFlagsUrl = "https://flags.example/companion.json";
constexpr const char* kKillSwitch = R"({"schema":1,"webFlowEnabled":false})";
constexpr const char* kAllClear = R"({"schema":1,"webFlowEnabled":true})";

TEST(RemoteFlags, ParsesTheKillSwitchDocument) {
  auto flags = parseRemoteFlags(R"({"schema":1,"webFlowEnabled":true,"disabledSdkVersions":["0.1.0"],
    "disabledBundleVersions":["0.0.9",7],"minimumWebProtocol":1,"future":{}})");
  ASSERT_TRUE(flags.has_value());
  EXPECT_FALSE(flags->killSwitchAllowsLaunch("0.1.0"));
  EXPECT_TRUE(flags->killSwitchAllowsLaunch("0.1.1"));
  EXPECT_TRUE(flags->bundleDisabled("0.0.9"));
  EXPECT_EQ(flags->disabledBundleVersions.size(), 1u) << "non-strings are skipped";
  EXPECT_EQ(flags->minimumWebProtocol, 1);
}

TEST(RemoteFlags, GlobalSwitchDisablesEveryVersion) {
  EXPECT_FALSE(parseRemoteFlags(kKillSwitch)->killSwitchAllowsLaunch("9.9.9"));
}

TEST(RemoteFlags, UnknownSchemaAndMalformedDocumentsAreIgnored) {
  EXPECT_FALSE(parseRemoteFlags(R"({"schema":2,"webFlowEnabled":false})").has_value());
  EXPECT_FALSE(parseRemoteFlags(R"({"webFlowEnabled":false})").has_value());
  EXPECT_FALSE(parseRemoteFlags("not json").has_value());
  EXPECT_FALSE(parseRemoteFlags("[]").has_value());
  EXPECT_TRUE(RemoteFlags{}.killSwitchAllowsLaunch("0.1.0")) << "defaults allow everything";
}

TEST(RemoteFlags, WrongTypesKeepDefaults) {
  auto flags = parseRemoteFlags(R"({"schema":1,"webFlowEnabled":"no","minimumWebProtocol":"2"})");
  ASSERT_TRUE(flags.has_value());
  EXPECT_TRUE(flags->webFlowEnabled);
  EXPECT_EQ(flags->minimumWebProtocol, 0);
}

// The session fetches, keeps and applies the document.
class SessionFlagsTest : public ::testing::Test {
 protected:
  std::unique_ptr<Session> start() {
    std::string error;
    auto session = Session::create(std::string(R"({"remoteFlagsUrl":")") + kRemoteFlagsUrl + R"("})", &error);
    EXPECT_NE(session, nullptr) << error;
    m_platform.drain(*session);

    return session;
  }

  test::FakePlatform m_platform;
};

TEST_F(SessionFlagsTest, FetchesTheDocumentAndReportsTheKillSwitch) {
  m_platform.getReplies[kRemoteFlagsUrl] = {http::kStatusOk, kKillSwitch};
  start();
  EXPECT_EQ(m_platform.gets, std::vector<std::string>{kRemoteFlagsUrl});
  EXPECT_EQ(m_platform.launchAllowed, std::vector<bool>{false});
}

TEST_F(SessionFlagsTest, AKillSwitchSurvivesAColdStartWithoutNetwork) {
  m_platform.getReplies[kRemoteFlagsUrl] = {http::kStatusOk, kKillSwitch};
  start();
  m_platform.offline = true;
  m_platform.launchAllowed.clear();
  start();
  EXPECT_EQ(m_platform.launchAllowed, std::vector<bool>{false}) << "the cached document, then nothing from the network";
  EXPECT_TRUE(m_platform.logged("warn", "airplane mode"));
}

TEST_F(SessionFlagsTest, CachedFlagsApplyFirstAndTheNetworkWins) {
  m_platform.getReplies[kRemoteFlagsUrl] = {http::kStatusOk, kKillSwitch};
  start();
  m_platform.getReplies[kRemoteFlagsUrl] = {http::kStatusOk, kAllClear};
  m_platform.launchAllowed.clear();
  start();
  EXPECT_EQ(m_platform.launchAllowed, (std::vector<bool>{false, true}));
}

TEST_F(SessionFlagsTest, ABadPushKeepsTheLastGoodDocument) {
  m_platform.getReplies[kRemoteFlagsUrl] = {http::kStatusOk, kKillSwitch};
  start();
  m_platform.getReplies[kRemoteFlagsUrl] = {http::kStatusOk, "{oops"};
  m_platform.launchAllowed.clear();
  start();
  EXPECT_EQ(m_platform.launchAllowed, std::vector<bool>{false});
  EXPECT_TRUE(m_platform.logged("warn", "ignoring malformed remote flags"));

  m_platform.launchAllowed.clear();
  start();
  EXPECT_EQ(m_platform.launchAllowed, std::vector<bool>{false}) << "the bad push was not cached";
}

TEST_F(SessionFlagsTest, ServerErrorsAreLoggedNotApplied) {
  start();  // no reply registered: 404
  EXPECT_TRUE(m_platform.launchAllowed.empty());
  EXPECT_TRUE(m_platform.logged("warn", "HTTP 404"));
}

TEST_F(SessionFlagsTest, NoRemoteFlagsUrlMeansNoRequests) {
  std::string error;
  auto session = Session::create("", &error);
  m_platform.drain(*session);
  EXPECT_TRUE(m_platform.gets.empty());
  EXPECT_EQ(m_platform.cacheReads, 0);
}

TEST_F(SessionFlagsTest, InsecureRemoteFlagsUrlIsRejected) {
  std::string error;
  EXPECT_EQ(Session::create(R"({"remoteFlagsUrl":"http://flags.example"})", &error), nullptr);
  EXPECT_NE(error.find("remoteFlagsUrl"), std::string::npos);
}

// The handshake reads the flags in force when it happens, so a page opened
// before the document arrived is still subject to it.
TEST_F(SessionFlagsTest, BridgeHandshakeUsesFlagsThatArrivedAfterTheBridgeOpened) {
  m_platform.holdHttp = true;
  auto session = start();
  std::string error;
  auto bridge = session->openBridge("", &error);
  ASSERT_TRUE(bridge.has_value()) << error;

  m_platform.getReplies[kRemoteFlagsUrl] = {http::kStatusOk, R"({"schema":1,"disabledBundleVersions":["0.1.0"]})"};
  m_platform.releaseHttp(*session);
  m_platform.drain(*session);
  ASSERT_TRUE(session->bridgeReceive(
      *bridge, R"({"version":1,"id":"h","type":"hello","payload":{"protocol":1,"minimumNativeProtocol":1,"bundleVersion":"0.1.0"}})"));
  m_platform.drain(*session);
  JsonValue reply = parseJson(m_platform.sentToWeb[*bridge].back()).value;
  EXPECT_EQ(reply["error"]["code"].asString(), "bundle_disabled");
}

}  // namespace
}  // namespace tarkov_companion
