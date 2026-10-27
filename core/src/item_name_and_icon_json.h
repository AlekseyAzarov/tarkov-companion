// One wire form for tarkov.dev responses, results, favourites and their sync.
#pragma once

#include <string>
#include <string_view>

#include "contract_generated.h"
#include "json.h"
#include "model.h"
#include "tarkov_api.h"

namespace tarkov_companion {

// Our payloads kept tarkov.dev's names, so one reader serves both.
static_assert(std::string_view(contract::payload::kFieldName) == tarkov_api::kFieldName);
static_assert(std::string_view(contract::payload::kFieldShortName) == tarkov_api::kFieldShortName);
static_assert(std::string_view(contract::payload::kFieldIconLink) == tarkov_api::kFieldIconLink);
static_assert(std::string_view(contract::payload::kFieldName) == contract::favorites_sync::kFieldName);
static_assert(std::string_view(contract::payload::kFieldShortName) == contract::favorites_sync::kFieldShortName);
static_assert(std::string_view(contract::payload::kFieldIconLink) == contract::favorites_sync::kFieldIconLink);

inline ItemNameAndIcon nameAndIconFromJson(const JsonValue& json) {
  return ItemNameAndIcon{std::string(json[contract::payload::kFieldName].asString()),
                         std::string(json[contract::payload::kFieldShortName].asString()),
                         std::string(json[contract::payload::kFieldIconLink].asString())};
}

// Writes the three keys into the object the writer is in.
inline JsonWriter& writeNameAndIconKeys(JsonWriter& writer, const ItemNameAndIcon& nameAndIcon) {
  return writer.key(contract::payload::kFieldName)
      .value(nameAndIcon.name)
      .key(contract::payload::kFieldShortName)
      .value(nameAndIcon.shortName)
      .key(contract::payload::kFieldIconLink)
      .value(nameAndIcon.iconLink);
}

}  // namespace tarkov_companion
