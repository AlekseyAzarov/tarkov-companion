// tarkov.dev's GraphQL names (contracts/queries/). Not ours to choose, so not
// in contracts/; scripts/contract-check.mjs checks them nightly.
#pragma once

namespace tarkov_companion::tarkov_api {

inline constexpr const char* kRequestFieldQuery = "query";
inline constexpr const char* kRequestFieldVariables = "variables";

inline constexpr const char* kVariableLanguage = "lang";
inline constexpr const char* kVariableGameMode = "gameMode";
inline constexpr const char* kVariableName = "name";
inline constexpr const char* kVariableLimit = "limit";
inline constexpr const char* kVariableIds = "ids";

inline constexpr const char* kGameModeRegular = "regular";
inline constexpr const char* kGameModePve = "pve";

inline constexpr const char* kFieldData = "data";
inline constexpr const char* kFieldErrors = "errors";
inline constexpr const char* kFieldItems = "items";
inline constexpr const char* kFieldHideoutStations = "hideoutStations";

inline constexpr const char* kFieldId = "id";
inline constexpr const char* kFieldName = "name";
inline constexpr const char* kFieldShortName = "shortName";
inline constexpr const char* kFieldIconLink = "iconLink";
inline constexpr const char* kFieldBasePrice = "basePrice";
inline constexpr const char* kFieldAveragePriceLast24Hours = "avg24hPrice";
inline constexpr const char* kFieldLastLowPrice = "lastLowPrice";
inline constexpr const char* kFieldWidth = "width";
inline constexpr const char* kFieldHeight = "height";
inline constexpr const char* kFieldTypes = "types";
inline constexpr const char* kFieldSellFor = "sellFor";
inline constexpr const char* kFieldVendor = "vendor";
inline constexpr const char* kFieldNormalizedName = "normalizedName";
inline constexpr const char* kFieldPriceRubles = "priceRUB";
inline constexpr const char* kFieldUsedInTasks = "usedInTasks";
inline constexpr const char* kFieldMinimumPlayerLevel = "minPlayerLevel";
inline constexpr const char* kFieldLevels = "levels";
inline constexpr const char* kFieldLevel = "level";
inline constexpr const char* kFieldItemRequirements = "itemRequirements";
inline constexpr const char* kFieldItem = "item";
inline constexpr const char* kFieldCount = "count";

inline constexpr const char* kItemTypeBannedFromFlea = "noFlea";

// Paths in the static catalog, under <base>/<game mode>/<language>/.
inline constexpr const char* kCatalogSearchDirectory = "search/";
inline constexpr const char* kCatalogItemsDirectory = "items/";
inline constexpr const char* kCatalogHideoutFile = "hideout.json";
inline constexpr const char* kCatalogFileExtension = ".json";

}  // namespace tarkov_companion::tarkov_api
