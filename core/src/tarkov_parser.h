// Lenient about additions (unknown fields are ignored), strict about shape
// (data.items must be an array): what the contract check pins down.
#pragma once

#include <string_view>
#include <vector>

#include "model.h"

namespace tarkov_companion {

enum class ParseStatus { Ok, InvalidJson, Schema, Api };

struct ItemsParseResult {
  std::vector<Item> items;
  ParseStatus status = ParseStatus::Ok;
  int skippedWithoutIdOrName = 0;
};

struct HideoutParseResult {
  ParseStatus status = ParseStatus::Ok;
  HideoutRequirementsByItemId requirementsByItemId;
};

ItemsParseResult parseItemsResponse(std::string_view json);
HideoutParseResult parseHideoutResponse(std::string_view json);

}  // namespace tarkov_companion
