#pragma once

#include <string>
#include <vector>

#include "model.h"

namespace tarkov_companion {

// Pure: no I/O, no clock, no globals.
Recommendation recommend(const Item& item, const HideoutRequirementsByItemId* hideout, const PlayerContext& context);

std::string recommendationToJson(const Recommendation& recommendation);
std::string summariesToJson(const std::vector<Item>& items);

}  // namespace tarkov_companion
