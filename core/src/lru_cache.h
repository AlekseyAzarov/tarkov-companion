// Not thread-safe; Engine guards it with a mutex.
#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

#include "clock.h"

namespace tarkov_companion {

template <typename Key, typename Value>
class LruCache {
 public:
  struct LookupResult {
    const Value* value = nullptr;
    bool expired = false;
  };

  static constexpr std::chrono::seconds kNoExpiry{0};

  LruCache(size_t capacity, std::chrono::seconds timeToLive, SteadyClock clock = realSteadyClock())
      : m_clock(std::move(clock)), m_timeToLive(timeToLive), m_capacity(capacity == 0 ? 1 : capacity) {}

  void put(const Key& key, Value value) {
    auto existing = m_entryByKey.find(key);
    if (existing != m_entryByKey.end()) {
      existing->second->value = std::move(value);
      existing->second->insertedAt = m_clock();
      m_entriesMostRecentFirst.splice(m_entriesMostRecentFirst.begin(), m_entriesMostRecentFirst, existing->second);
      return;
    }

    m_entriesMostRecentFirst.push_front(Entry{key, std::move(value), m_clock()});
    m_entryByKey.emplace(key, m_entriesMostRecentFirst.begin());

    if (m_entriesMostRecentFirst.size() > m_capacity) {
      m_entryByKey.erase(m_entriesMostRecentFirst.back().key);
      m_entriesMostRecentFirst.pop_back();
      ++m_evictionCount;
    }
  }

  // Expired entries are still returned (flagged) rather than dropped: stale
  // prices are better than no answer when the network is down.
  LookupResult get(const Key& key) {
    auto found = m_entryByKey.find(key);
    if (found == m_entryByKey.end()) {
      return {};
    }

    m_entriesMostRecentFirst.splice(m_entriesMostRecentFirst.begin(), m_entriesMostRecentFirst, found->second);
    const Entry& entry = *found->second;
    bool expired = m_timeToLive > kNoExpiry && m_clock() - entry.insertedAt > m_timeToLive;

    return {&entry.value, expired};
  }

  bool contains(const Key& key) const { return m_entryByKey.count(key) != 0; }
  size_t size() const { return m_entriesMostRecentFirst.size(); }
  size_t capacity() const { return m_capacity; }
  size_t evictionCount() const { return m_evictionCount; }

 private:
  struct Entry {
    Key key;
    Value value;
    SteadyTime insertedAt;
  };

  SteadyClock m_clock;
  std::chrono::seconds m_timeToLive;
  size_t m_capacity;
  std::list<Entry> m_entriesMostRecentFirst;
  std::unordered_map<Key, typename std::list<Entry>::iterator> m_entryByKey;
  size_t m_evictionCount = 0;
};

}  // namespace tarkov_companion
