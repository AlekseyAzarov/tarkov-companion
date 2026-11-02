#include "tarkov_parser.h"

#include "item_name_and_icon_json.h"
#include "json.h"
#include "tarkov_dev_schema.h"

namespace tarkov_companion {
namespace {

constexpr int kDefaultItemDimension = 1;
constexpr int kDefaultRequirementCount = 1;

std::optional<int64_t> positivePriceOrNothing(const JsonValue& field) {
  // tarkov.dev reports "no data" as null or 0; both mean we can't price it.
  if (!field.isNumber()) {
    return std::nullopt;
  }

  int64_t price = field.asInteger();
  if (price <= 0) {
    return std::nullopt;
  }

  return price;
}

ParseStatus findGraphqlDataObject(const JsonParseResult& document, const JsonValue** data) {
  if (!document.ok) {
    return ParseStatus::InvalidJson;
  }

  const JsonValue& root = document.value;
  if (!root.isObject()) {
    return ParseStatus::Schema;
  }

  const JsonValue& payload = root[tarkov_dev::kFieldData];
  if (payload.isNull() && root[tarkov_dev::kFieldErrors].isArray()) {
    return ParseStatus::Api;
  }
  if (!payload.isObject()) {
    return ParseStatus::Schema;
  }

  *data = &payload;

  return ParseStatus::Ok;
}

Item parseItem(const JsonValue& json) {
  Item item;
  item.id = std::string(json[tarkov_dev::kFieldId].asString());
  item.nameAndIcon = nameAndIconFromJson(json);
  item.basePrice = json[tarkov_dev::kFieldBasePrice].asInteger();
  item.averagePriceLast24Hours = positivePriceOrNothing(json[tarkov_dev::kFieldAveragePriceLast24Hours]);
  item.lastLowPrice = positivePriceOrNothing(json[tarkov_dev::kFieldLastLowPrice]);
  item.width = static_cast<int>(json[tarkov_dev::kFieldWidth].asInteger(kDefaultItemDimension));
  item.height = static_cast<int>(json[tarkov_dev::kFieldHeight].asInteger(kDefaultItemDimension));

  for (const auto& type : json[tarkov_dev::kFieldTypes].items()) {
    if (type.asString() == tarkov_dev::kItemTypeBannedFromFlea) {
      item.bannedFromFlea = true;
    }
  }

  for (const auto& offerJson : json[tarkov_dev::kFieldSellFor].items()) {
    SellOffer offer;
    offer.vendorId = std::string(offerJson[tarkov_dev::kFieldVendor][tarkov_dev::kFieldNormalizedName].asString());
    const JsonValue& vendor = offerJson[tarkov_dev::kFieldVendor];
    offer.vendorDisplayName = std::string(vendor[tarkov_dev::kFieldName].asString(offer.vendorId));
    offer.priceRubles = offerJson[tarkov_dev::kFieldPriceRubles].asInteger();
    if (!offer.vendorId.empty() && offer.priceRubles > 0) {
      item.sellFor.push_back(std::move(offer));
    }
  }

  for (const auto& taskJson : json[tarkov_dev::kFieldUsedInTasks].items()) {
    TaskReference task;
    task.id = std::string(taskJson[tarkov_dev::kFieldId].asString());
    task.name = std::string(taskJson[tarkov_dev::kFieldName].asString());
    if (!task.id.empty()) {
      item.usedInTasks.push_back(std::move(task));
    }
  }

  return item;
}

}  // namespace

ItemsParseResult parseItemsResponse(std::string_view json) {
  ItemsParseResult result;
  JsonParseResult document = parseJson(json);
  const JsonValue* data = nullptr;
  result.status = findGraphqlDataObject(document, &data);
  if (result.status != ParseStatus::Ok) {
    return result;
  }

  const JsonValue& items = (*data)[tarkov_dev::kFieldItems];
  if (!items.isArray()) {
    result.status = ParseStatus::Schema;
    return result;
  }

  result.items.reserve(items.items().size());
  for (const auto& itemJson : items.items()) {
    bool hasIdAndName = itemJson.isObject() && itemJson[tarkov_dev::kFieldId].isString() &&
                        itemJson[tarkov_dev::kFieldName].isString();
    if (!hasIdAndName) {
      ++result.skippedWithoutIdOrName;
      continue;
    }
    result.items.push_back(parseItem(itemJson));
  }

  return result;
}

HideoutParseResult parseHideoutResponse(std::string_view json) {
  HideoutParseResult result;
  JsonParseResult document = parseJson(json);
  const JsonValue* data = nullptr;
  result.status = findGraphqlDataObject(document, &data);
  if (result.status != ParseStatus::Ok) {
    return result;
  }

  const JsonValue& stations = (*data)[tarkov_dev::kFieldHideoutStations];
  if (!stations.isArray()) {
    result.status = ParseStatus::Schema;
    return result;
  }

  for (const auto& station : stations.items()) {
    std::string stationName(station[tarkov_dev::kFieldName].asString());
    for (const auto& level : station[tarkov_dev::kFieldLevels].items()) {
      int levelNumber = static_cast<int>(level[tarkov_dev::kFieldLevel].asInteger());
      for (const auto& requirement : level[tarkov_dev::kFieldItemRequirements].items()) {
        std::string_view itemId = requirement[tarkov_dev::kFieldItem][tarkov_dev::kFieldId].asString();
        int count = static_cast<int>(requirement[tarkov_dev::kFieldCount].asInteger(kDefaultRequirementCount));
        if (itemId.empty() || count <= 0) {
          continue;
        }
        result.requirementsByItemId[std::string(itemId)].push_back({stationName, levelNumber, count});
      }
    }
  }

  return result;
}

}  // namespace tarkov_companion
