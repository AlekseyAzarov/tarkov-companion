import Foundation

private typealias Contract = GeneratedContract

/// Core results (tarkov_companion.h) onto the public model, for the headless API.
enum ModelMapper {
    private static let verdictsByWireName: [String: Verdict] = [
        Contract.payloadVerdictKeep: .keep,
        Contract.payloadVerdictSellFlea: .sellFlea,
        Contract.payloadVerdictSellTrader: .sellTrader,
        Contract.payloadVerdictNoBuyer: .noBuyer,
    ]

    private static let keepReasonKindsByWireName: [String: KeepReason.Kind] = [
        Contract.payloadKeepReasonKindTask: .task,
        Contract.payloadKeepReasonKindHideout: .hideout,
    ]

    static func itemSummaries(fromSearchResult searchResult: Data) throws -> [ItemSummary] {
        try JsonObject(data: searchResult).objects(Contract.payloadFieldItems).map { summary in
            ItemSummary(
                id: try summary.string(Contract.payloadFieldId),
                name: try summary.string(Contract.payloadFieldName),
                shortName: summary.optionalString(Contract.payloadFieldShortName) ?? "",
                iconUrl: summary.optionalString(Contract.payloadFieldIconLink) ?? "",
                slots: Int(summary.optionalInteger(Contract.payloadFieldSlots) ?? 1),
                averagePriceLast24HoursRubles: summary.optionalInteger(Contract.payloadFieldAveragePriceLast24Hours)
            )
        }
    }

    static func recommendation(fromResult recommendResult: Data) throws -> Recommendation {
        let result = try JsonObject(data: recommendResult)
        let recommendation = try result.object(Contract.payloadFieldRecommendation)

        let bestTrader = try recommendation.optionalObject(Contract.payloadFieldBestTrader).map { trader in
            TraderOffer(
                vendorId: try trader.string(Contract.payloadFieldVendorId),
                vendorName: try trader.string(Contract.payloadFieldVendorName),
                priceRubles: try trader.integer(Contract.payloadFieldPriceRubles)
            )
        }
        let flea = try recommendation.optionalObject(Contract.payloadFieldFlea).map { fleaOffer in
            FleaOffer(
                available: try fleaOffer.boolean(Contract.payloadFieldAvailable),
                unavailableReason: fleaOffer.optionalString(Contract.payloadFieldUnavailableReason),
                listPriceRubles: try fleaOffer.integer(Contract.payloadFieldListPriceRubles),
                feeRubles: try fleaOffer.integer(Contract.payloadFieldFeeRubles),
                netRubles: try fleaOffer.integer(Contract.payloadFieldNetRubles)
            )
        }
        let keepReasons = try recommendation.objects(Contract.payloadFieldKeepReasons).map { reason in
            KeepReason(
                kind: keepReasonKindsByWireName[try reason.string(Contract.payloadFieldKind)] ?? .hideout,
                label: try reason.string(Contract.payloadFieldLabel),
                count: Int(reason.optionalInteger(Contract.payloadFieldCount) ?? 1)
            )
        }

        return Recommendation(
            itemId: try recommendation.string(Contract.payloadFieldItemId),
            itemName: try recommendation.string(Contract.payloadFieldItemName),
            verdict: verdictsByWireName[try recommendation.string(Contract.payloadFieldVerdict)] ?? .noBuyer,
            bestNetRubles: try recommendation.integer(Contract.payloadFieldBestNetRubles),
            bestNetPerSlotRubles: try recommendation.integer(Contract.payloadFieldBestNetPerSlotRubles),
            bestTrader: bestTrader,
            flea: flea,
            keepReasons: keepReasons,
            pricesAreStale: recommendation.optionalBoolean(Contract.payloadFieldPricesAreStale) ?? false,
            fromOfflineCache: result.optionalBoolean(Contract.payloadFieldFromCache) ?? false
        )
    }
}
