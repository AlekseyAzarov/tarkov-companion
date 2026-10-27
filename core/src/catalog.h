// The static catalog (docs/catalog.md). Search words and shard choice must
// match scripts/catalog/catalog.mjs; both test contracts/fixtures/catalog_search.json.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tarkov_companion::catalog {

// Words are runs of ASCII letters and digits, accented Latin and Cyrillic, with
// ё folded into е. Not full Unicode: the generator applies the same rule.
std::vector<std::string> lowercaseSearchWords(std::string_view text);

std::optional<std::string> searchShardFileName(std::string_view query);

bool everyQueryWordStartsAWordOf(const std::vector<std::string>& queryWords, std::string_view name,
                                 std::string_view shortName);

// What tarkov.dev's search would have answered: the matching items of the
// shard, in shard order, as an items response.
std::optional<std::string> searchResponseFromShard(std::string_view shardJson, std::string_view query,
                                                   size_t maximumItems);

// The catalog has English and Russian; other languages read English.
const char* catalogLanguageFor(std::string_view configuredLanguage);

}  // namespace tarkov_companion::catalog
