// Favourites: local first, merged into the account on sign-in, synced across
// devices through a scripted backend that settles conflicts like
// bff/src/server.mjs (per item, the later change wins; removals are kept).
#include "favorites.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fake_identity_server.h"
#include "fake_platform.h"
#include "json.h"
#include "session.h"
#include "test_util.h"
#include "http.h"

namespace tarkov_companion {
namespace {

using std::chrono::seconds;
using test::FakeIdentityServer;
using test::HttpReply;
using http::kStatusOk;
using test::Result;
using test::Sent;
using test::SystemTime;

constexpr const char* kSyncUrl = "https://api.example/favorites/sync";
constexpr const char* kSalewaId = "544fb45d4bdc2dee738b4568";
constexpr const char* kGraphicsCardId = "57347ca924597744596b4e71";

class FakeFavoritesServer {
 public:
  bool down = false;
  int syncs = 0;

  std::optional<HttpReply> handle(const Sent& request) {
    if (request.url != kSyncUrl) {
      return std::nullopt;
    }

    if (down) {
      return HttpReply{http::kStatusServiceUnavailable, ""};
    }

    auto authorization = request.headers.find("Authorization");
    if (authorization == request.headers.end() || authorization->second.rfind("Bearer access-", 0) != 0) {
      return HttpReply{http::kStatusUnauthorized, ""};
    }

    ++syncs;
    JsonValue body = parseJson(request.body).value;
    std::vector<std::string> touched;
    for (const JsonValue& change : body["changes"].items()) {
      std::string itemId(change["itemId"].asString());
      touched.push_back(itemId);
      auto existing = m_records.find(itemId);
      if (existing != m_records.end() && existing->second.changedAt >= change["changedAt"].asInteger()) {
        continue;
      }

      m_records[itemId] = Record{change["favorite"].asBool(), change["changedAt"].asInteger(),
                                std::string(change["name"].asString()), ++m_sequence};
    }

    JsonWriter answer;
    answer.beginObject().key("changes").beginArray();
    for (const auto& [itemId, record] : m_records) {
      bool wanted = record.sequence > body["since"].asInteger() ||
                    std::find(touched.begin(), touched.end(), itemId) != touched.end();
      if (!wanted) {
        continue;
      }

      answer.beginObject()
          .key("itemId").value(itemId)
          .key("favorite").value(record.favorite)
          .key("changedAt").value(record.changedAt)
          .key("name").value(record.name)
          .endObject();
    }

    answer.endArray().key("cursor").value(m_sequence).endObject();

    return HttpReply{http::kStatusOk, answer.takeText()};
  }

  bool favorite(const std::string& itemId) const {
    auto found = m_records.find(itemId);
    return found != m_records.end() && found->second.favorite;
  }

 private:
  struct Record {
    bool favorite;
    int64_t changedAt;
    std::string name;
    int64_t sequence;
  };
  std::map<std::string, Record> m_records;
  int64_t m_sequence = 0;
};

// One device: its own storage and session, the shared servers.
struct Device {
  test::FakePlatform platform;
  std::unique_ptr<Session> session;
  RequestId nextRequest = 1;
  int entropy = 0;
};

class FavoritesTest : public ::testing::Test {
 protected:
  void SetUp() override { start(m_phone); }

  void start(Device& device, bool withAuth = true) {
    device.platform.server = [this](const Sent& request) {
      if (auto reply = m_favoritesServer.handle(request)) {
        return reply;
      }

      return m_identity.handle(request);
    };
    std::string config = withAuth ? std::string(R"({"signInIssuer":")") + test::kIssuer + R"(","signInClientId":")" +
                                        test::kClientId + R"(","signInRedirectUri":")" + test::kRedirectUri +
                                        R"(","favoritesSyncUrl":")" + kSyncUrl + R"("})"
                                  : "";
    SessionClocks clocks;
    clocks.wall = [this] { return m_now; };
    std::string error;
    device.session = Session::create(config, &error, clocks);
    ASSERT_NE(device.session, nullptr) << error;
    device.platform.drain(*device.session);
  }

  Result run(Device& device, const std::function<bool(Session&, RequestId)>& command) {
    RequestId request = device.nextRequest++;
    EXPECT_TRUE(command(*device.session, request));
    device.platform.drain(*device.session);

    return device.platform.results[request];
  }

  void search(Device& device) {
    ASSERT_TRUE(run(device, [](Session& session, RequestId request) { return session.search(request, "salewa"); }).ok);
  }

  Result setFavorite(Device& device, const std::string& itemId, bool favorite) {
    m_now += seconds(1);

    return run(device, [&](Session& session, RequestId request) {
      return session.setFavorite(request, itemId, favorite);
    });
  }

  std::vector<std::string> favoriteIds(Device& device) {
    Result result = run(device, [](Session& session, RequestId request) { return session.listFavorites(request); });
    std::vector<std::string> ids;
    for (const JsonValue& item : result.payload["items"].items()) ids.emplace_back(item["id"].asString());

    return ids;
  }

  void signIn(Device& device) {
    RequestId request = device.nextRequest++;
    std::vector<uint8_t> bytes(Authentication::kSignInEntropyBytes);
    for (auto& byte : bytes) byte = static_cast<uint8_t>(++device.entropy);
    ASSERT_TRUE(device.session->startSignIn(request, bytes.data(), bytes.size()));
    device.platform.drain(*device.session);
    device.session->signInRedirect(m_identity.approve(device.platform.openedUrls.back()));
    device.platform.drain(*device.session);
    ASSERT_TRUE(device.platform.results[request].ok);
  }

  SystemTime m_now = SystemTime(std::chrono::hours(24 * 365 * 50));
  FakeIdentityServer m_identity{m_now};
  FakeFavoritesServer m_favoritesServer;
  Device m_phone;
  Device m_tablet;
};

TEST_F(FavoritesTest, SignedOutTheListStaysOnTheDevice) {
  search(m_phone);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);
  EXPECT_EQ(favoriteIds(m_phone), std::vector<std::string>{kSalewaId});
  EXPECT_EQ(m_phone.platform.favoriteLists.back().items()[0]["name"].asString(), "Salewa first aid kit");
  EXPECT_EQ(m_favoritesServer.syncs, 0);
  EXPECT_EQ(m_phone.platform.trackedOnce("favorite_toggled").at("signed_in"), "false");

  start(m_phone);  // a restart: the data store survives, the catalog doesn't
  EXPECT_EQ(favoriteIds(m_phone), std::vector<std::string>{kSalewaId});
}

TEST_F(FavoritesTest, NamesComeFromTheCatalogNotTheCaller) {
  EXPECT_EQ(setFavorite(m_phone, kSalewaId, true).errorCode, "not_found") << "nothing searched yet";
  EXPECT_EQ(setFavorite(m_phone, "../x", true).errorCode, "invalid_argument");
}

TEST_F(FavoritesTest, NewestFirstAndRemovalsDisappear) {
  search(m_phone);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);
  ASSERT_TRUE(setFavorite(m_phone, kGraphicsCardId, true).ok);
  EXPECT_EQ(favoriteIds(m_phone), (std::vector<std::string>{kGraphicsCardId, kSalewaId}));

  size_t notices = m_phone.platform.favoriteLists.size();
  ASSERT_TRUE(setFavorite(m_phone, kGraphicsCardId, true).ok);
  EXPECT_EQ(m_phone.platform.favoriteLists.size(), notices) << "already a favourite: nothing changes";

  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, false).ok);
  EXPECT_EQ(favoriteIds(m_phone), std::vector<std::string>{kGraphicsCardId});
}

TEST_F(FavoritesTest, TheListIsCappedAtTheContractLimit) {
  constexpr int kItems = static_cast<int>(contract::kFavoritesMaximum) + 1;
  m_phone.platform.handler = [](const std::string&) {
    JsonWriter items;
    items.beginObject().key("data").beginObject().key("items").beginArray();
    for (int index = 0; index < kItems; ++index) {
      items.beginObject().key("id").value("item" + std::to_string(index)).key("name").value("Item").endObject();
    }

    items.endArray().endObject().endObject();

    return HttpReply{http::kStatusOk, items.takeText()};
  };
  search(m_phone);
  for (int index = 0; index < kItems - 1; ++index) {
    ASSERT_TRUE(setFavorite(m_phone, "item" + std::to_string(index), true).ok);
  }

  EXPECT_EQ(setFavorite(m_phone, "item" + std::to_string(kItems - 1), true).errorCode, "invalid_argument");
}

TEST_F(FavoritesTest, SigningInMergesThisDevicesListIntoTheAccount) {
  search(m_phone);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);
  signIn(m_phone);

  EXPECT_TRUE(m_favoritesServer.favorite(kSalewaId)) << "the local favourite reached the account";
  EXPECT_EQ(favoriteIds(m_phone), std::vector<std::string>{kSalewaId});
  EXPECT_EQ(m_phone.platform.data.count("favorites|local"), 0u) << "merged, not kept twice";
  EXPECT_EQ(m_phone.platform.data.count("favorites|account|acc_1"), 1u);
}

TEST_F(FavoritesTest, AnotherDeviceGetsTheList) {
  signIn(m_phone);
  search(m_phone);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);

  start(m_tablet);
  signIn(m_tablet);
  EXPECT_EQ(favoriteIds(m_tablet), std::vector<std::string>{kSalewaId});
  EXPECT_EQ(m_tablet.platform.favoriteLists.back().items().size(), 1u) << "the platform heard about it";
}

TEST_F(FavoritesTest, ARemovalReachesTheOtherDevice) {
  signIn(m_phone);
  search(m_phone);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);
  start(m_tablet);
  signIn(m_tablet);
  ASSERT_EQ(favoriteIds(m_tablet).size(), 1u);

  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, false).ok);
  start(m_tablet);  // the tablet restarts and syncs
  EXPECT_TRUE(favoriteIds(m_tablet).empty());
}

TEST_F(FavoritesTest, TheLaterChangeWinsWhateverOrderTheSyncsArriveIn) {
  signIn(m_phone);
  search(m_phone);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);
  start(m_tablet);
  signIn(m_tablet);

  // The tablet removes it while offline; later the phone stars it again online.
  m_favoritesServer.down = true;
  ASSERT_TRUE(setFavorite(m_tablet, kSalewaId, false).ok);
  m_favoritesServer.down = false;
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, false).ok);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);

  start(m_tablet);  // its older removal goes out now, and loses
  EXPECT_EQ(favoriteIds(m_tablet), std::vector<std::string>{kSalewaId});
  EXPECT_TRUE(m_favoritesServer.favorite(kSalewaId));
}

TEST_F(FavoritesTest, ChangesMadeOfflineAreSentLater) {
  signIn(m_phone);
  search(m_phone);
  m_favoritesServer.down = true;
  ASSERT_TRUE(setFavorite(m_phone, kGraphicsCardId, true).ok);
  EXPECT_EQ(favoriteIds(m_phone), std::vector<std::string>{kGraphicsCardId}) << "shown at once";
  EXPECT_FALSE(m_favoritesServer.favorite(kGraphicsCardId));

  m_favoritesServer.down = false;
  start(m_phone);  // the queue was stored with the list
  EXPECT_TRUE(m_favoritesServer.favorite(kGraphicsCardId));
}

TEST_F(FavoritesTest, ChangesMadeOfflineGoWhenTheOverlayOpensAgain) {
  signIn(m_phone);
  search(m_phone);
  m_favoritesServer.down = true;
  ASSERT_TRUE(setFavorite(m_phone, kGraphicsCardId, true).ok);
  m_favoritesServer.down = false;

  std::string error;
  ASSERT_TRUE(m_phone.session->openBridge("", &error).has_value()) << error;
  m_phone.platform.drain(*m_phone.session);
  EXPECT_TRUE(m_favoritesServer.favorite(kGraphicsCardId)) << "no restart and no other change needed";
}

TEST_F(FavoritesTest, AnExpiredAccessTokenIsRefreshedForTheSync) {
  signIn(m_phone);
  search(m_phone);
  m_now += seconds(m_identity.accessLifetimeSeconds);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);
  EXPECT_TRUE(m_favoritesServer.favorite(kSalewaId));
  EXPECT_EQ(m_identity.refreshRequests, 1);
}

TEST_F(FavoritesTest, SigningOutTakesTheAccountsListOffTheDevice) {
  signIn(m_phone);
  search(m_phone);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);
  ASSERT_TRUE(run(m_phone, [](Session& session, RequestId request) { return session.signOut(request); }).ok);

  EXPECT_TRUE(favoriteIds(m_phone).empty());
  EXPECT_EQ(m_phone.platform.data.count("favorites|account|acc_1"), 0u);
  EXPECT_TRUE(m_phone.platform.favoriteLists.back().items().empty()) << "the page and the host were told";

  signIn(m_phone);
  EXPECT_EQ(favoriteIds(m_phone), std::vector<std::string>{kSalewaId}) << "back from the account";
}

TEST_F(FavoritesTest, WithoutSyncUrlSignedInFavouritesStayLocal) {
  start(m_phone, false);
  search(m_phone);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);
  EXPECT_EQ(m_favoritesServer.syncs, 0);
  EXPECT_EQ(favoriteIds(m_phone), std::vector<std::string>{kSalewaId});
}

TEST_F(FavoritesTest, ThePageSeesChangesFromOtherDevices) {
  signIn(m_phone);
  start(m_tablet);
  signIn(m_tablet);
  std::string error;
  auto bridge = m_tablet.session->openBridge("", &error);
  ASSERT_TRUE(bridge.has_value()) << error;
  ASSERT_TRUE(m_tablet.session->bridgeReceive(
      *bridge, R"({"version":1,"id":"h","type":"hello","payload":{"protocol":1,"minimumNativeProtocol":1,"bundleVersion":"0.1.0"}})"));
  m_tablet.platform.drain(*m_tablet.session);

  search(m_phone);
  ASSERT_TRUE(setFavorite(m_phone, kSalewaId, true).ok);
  search(m_tablet);
  ASSERT_TRUE(setFavorite(m_tablet, kGraphicsCardId, true).ok);  // syncs, and brings the phone's change

  JsonValue last = parseJson(m_tablet.platform.sentToWeb[*bridge].back()).value;
  EXPECT_EQ(last["event"].asString(), "favorites_changed");
  EXPECT_EQ(last["payload"]["items"].items().size(), 2u);
}

}  // namespace
}  // namespace tarkov_companion
