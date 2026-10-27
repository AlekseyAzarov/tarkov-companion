// Executes a session's actions the way a platform layer does, synchronously
// and in memory: shared by the session and bridge tests.
#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json.h"
#include "session.h"
#include "test_util.h"
#include "http.h"

namespace tarkov_companion::test {

inline constexpr int kTransportFailure = 0;

struct HttpReply {
  int statusCode = http::kStatusOk;
  std::string body;
};

struct Result {
  bool ok = false;
  JsonValue payload;
  std::string errorCode;
  std::string errorMessage;
};

// "query ItemSearch($name: ..." -> "ItemSearch"
inline std::string operationName(std::string_view query) {
  constexpr std::string_view kPrefix = "query ";
  query.remove_prefix(kPrefix.size());

  return std::string(query.substr(0, query.find_first_of("( ")));
}

// The analytics catalog (contracts/analytics-events.json), read once.
inline const JsonValue& analyticsCatalog() {
  static const JsonValue catalog = parseJson(readFixture("../analytics-events.json")).value;
  return catalog;
}

class FakePlatform {
 public:
  // An HTTP request as the session asked for it.
  struct SentRequest {
    ActionId id;
    std::string method;
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
  };

  std::map<std::string, std::string> store;    // the "disk", survives sessions
  std::map<std::string, std::string> secrets;  // the secret store, survives sessions
  std::map<std::string, std::string> data;     // the data store, survives sessions
  bool offline = false;
  // When set, HTTP actions wait in `heldHttp` until releaseHttp().
  bool holdHttp = false;
  // GraphQL POSTs are answered by operation name, GETs by URL (404 when absent).
  std::function<HttpReply(const std::string& operation)> handler = [](const std::string& operation) {
    if (operation == "Hideout") {
      return HttpReply{http::kStatusOk, readFixture("hideout.json")};
    }

    return HttpReply{http::kStatusOk, readFixture("item_search.json")};
  };
  std::map<std::string, HttpReply> getReplies;
  // Tried first for every request: answers it, or leaves it to the defaults above.
  std::function<std::optional<HttpReply>(const SentRequest&)> server;

  std::vector<std::string> operations;
  std::vector<std::string> gets;  // URLs, in order
  std::vector<bool> launchAllowed;  // every "flags" action
  std::vector<JsonValue> variables;
  std::vector<std::pair<std::string, std::string>> logs;  // level, message
  std::map<RequestId, Result> results;
  int cacheReads = 0;
  std::vector<std::pair<std::string, std::map<std::string, std::string>>> tracked;  // event, props
  std::map<BridgeId, std::vector<std::string>> sentToWeb;  // protocol messages, in order
  std::map<BridgeId, JsonValue> bridgeStates;               // the last "bridge_state" action
  std::vector<BridgeId> closedScreens;
  std::vector<std::string> openedUrls;     // "open_browser" actions
  std::vector<JsonValue> accounts;         // every "account" action
  std::vector<BridgeId> signInsFromPages;  // "bridge_sign_in" actions
  std::vector<JsonValue> favoriteLists;   // every "favorites" action's list

  void drain(Session& session) {
    while (auto action = session.nextAction()) {
      JsonParseResult parsed = parseJson(action->json);
      ASSERT_TRUE(parsed.ok) << action->json;
      const JsonValue& json = parsed.value;
      std::string_view type = json["type"].asString();
      if (type == "http") {
        SentRequest request{static_cast<ActionId>(json["id"].asInteger()), std::string(json["method"].asString()),
                            std::string(json["url"].asString()), {}, action->payload.value_or("")};
        for (const auto& [name, value] : json["headers"].members()) request.headers[name] = std::string(value.asString());
        m_heldHttp.push_back(std::move(request));
        if (!holdHttp) {
          releaseHttp(session);
        }
      } else if (type == "cache_read" || type == "secret_read" || type == "data_read") {
        auto& source = type == "cache_read" ? store : type == "secret_read" ? secrets : data;
        if (type == "cache_read") {
          ++cacheReads;
        }

        auto found = source.find(std::string(json["key"].asString()));
        ActionId id = static_cast<ActionId>(json["id"].asInteger());
        if (found == source.end()) {
          session.completeStoredValueRead(id, std::nullopt);
        } else {
          session.completeStoredValueRead(id, std::string_view(found->second));
        }
      } else if (type == "secret_write") {
        secrets[std::string(json["key"].asString())] = action->payload.value_or("");
      } else if (type == "secret_delete") {
        secrets.erase(std::string(json["key"].asString()));
      } else if (type == "data_write") {
        data[std::string(json["key"].asString())] = action->payload.value_or("");
      } else if (type == "data_delete") {
        data.erase(std::string(json["key"].asString()));
      } else if (type == "favorites") {
        favoriteLists.push_back(parseJson(action->payload.value_or("")).value);
      } else if (type == "open_browser") {
        openedUrls.emplace_back(json["url"].asString());
      } else if (type == "account") {
        accounts.push_back(json);
      } else if (type == "bridge_sign_in") {
        signInsFromPages.push_back(bridgeOf(json));
      } else if (type == "cache_write") {
        store[std::string(json["key"].asString())] = action->payload.value_or("");
      } else if (type == "result") {
        recordResult(json, action->payload);
      } else if (type == "log") {
        logs.emplace_back(json["level"].asString(), json["message"].asString());
      } else if (type == "track") {
        std::map<std::string, std::string> props;
        for (const auto& [name, value] : json["properties"].members()) props[name] = std::string(value.asString());
        expectInCatalog(std::string(json["event"].asString()), props);
        tracked.emplace_back(json["event"].asString(), std::move(props));
      } else if (type == "bridge_send") {
        sentToWeb[bridgeOf(json)].push_back(action->payload.value_or(""));
      } else if (type == "bridge_state") {
        bridgeStates[bridgeOf(json)] = json;
      } else if (type == "bridge_close") {
        closedScreens.push_back(bridgeOf(json));
      } else if (type == "flags") {
        launchAllowed.push_back(json["launchAllowed"].asBool());
      } else {
        ADD_FAILURE() << "unknown action " << action->json;
      }
    }
  }

  void releaseHttp(Session& session) {
    auto held = std::move(m_heldHttp);
    m_heldHttp.clear();
    for (const SentRequest& request : held) {
      sent.push_back(request);
      if (offline) {
        // The request never reaches a server: nothing there changes state.
        JsonParseResult graphql = parseJson(request.body);
        if (request.method == "GET") {
          gets.push_back(request.url);
        } else if (graphql.ok && graphql.value.hasKey("query")) {
          operations.push_back(operationName(graphql.value["query"].asString()));
          variables.push_back(graphql.value["variables"]);
        }

        session.completeHttp(request.id, kTransportFailure, "airplane mode");
        continue;
      }

      HttpReply reply;
      std::optional<HttpReply> answered = server ? server(request) : std::nullopt;
      if (answered) {
        reply = *answered;
      } else if (request.method == "GET") {
        gets.push_back(request.url);
        auto found = getReplies.find(request.url);
        reply = found != getReplies.end() ? found->second : HttpReply{http::kStatusNotFound, ""};
      } else {
        JsonParseResult body = parseJson(request.body);
        ASSERT_TRUE(body.ok) << request.body;
        operations.push_back(operationName(body.value["query"].asString()));
        variables.push_back(body.value["variables"]);
        reply = handler(operations.back());
      }

      session.completeHttp(request.id, reply.statusCode, reply.body);
    }
  }

  size_t heldHttpCount() const { return m_heldHttp.size(); }

  std::vector<SentRequest> sent;  // every HTTP request, as released

  bool logged(std::string_view level, std::string_view fragment) const {
    for (const auto& [lineLevel, message] : logs) {
      if (lineLevel == level && message.find(fragment) != std::string::npos) {
        return true;
      }
    }

    return false;
  }

  // AnalyticsProperties of the only `event` tracked so far.
  std::map<std::string, std::string> trackedOnce(std::string_view event) const {
    std::vector<std::map<std::string, std::string>> matches;
    for (const auto& [name, props] : tracked) {
      if (name == event) {
        matches.push_back(props);
      }
    }

    EXPECT_EQ(matches.size(), 1u) << event;

    return matches.empty() ? std::map<std::string, std::string>{} : matches.front();
  }

 private:
  static BridgeId bridgeOf(const JsonValue& json) { return static_cast<BridgeId>(json["bridge"].asInteger()); }

  // Every event the core sends must be in the catalog, with only its listed
  // properties. Web events (web_*) are free-form.
  static void expectInCatalog(const std::string& event, const std::map<std::string, std::string>& props) {
    const JsonValue& catalog = analyticsCatalog();
    if (event.rfind(std::string(catalog["webEventPrefix"].asString()), 0) == 0) {
      return;
    }

    const JsonValue& spec = catalog["events"][event];
    ASSERT_TRUE(spec.isObject()) << event << " is not in contracts/analytics-events.json";
    EXPECT_EQ(spec["emittedBy"].asString(), "core") << event;
    for (const auto& prop : props) {
      const auto& allowed = spec["properties"].items();
      bool listed = std::any_of(allowed.begin(), allowed.end(),
                                [&](const JsonValue& name) { return name.asString() == prop.first; });
      EXPECT_TRUE(listed) << event << "." << prop.first << " is not in contracts/analytics-events.json";
    }
  }

  void recordResult(const JsonValue& json, const std::optional<std::string>& payload) {
    Result result;
    result.ok = json["ok"].asBool();
    if (result.ok) {
      JsonParseResult parsed = parseJson(payload.value_or(""));
      EXPECT_TRUE(parsed.ok) << payload.value_or("<no payload>");
      result.payload = parsed.value;
    } else {
      result.errorCode = std::string(json["error"]["code"].asString());
      result.errorMessage = std::string(json["error"]["message"].asString());
    }

    RequestId request = static_cast<RequestId>(json["request"].asInteger());
    EXPECT_EQ(results.count(request), 0u) << "two results for request " << request;
    results[request] = result;
  }

  std::vector<SentRequest> m_heldHttp;
};

}  // namespace tarkov_companion::test
