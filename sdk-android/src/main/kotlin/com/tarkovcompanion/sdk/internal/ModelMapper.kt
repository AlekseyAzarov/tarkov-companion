package com.tarkovcompanion.sdk.internal

import com.tarkovcompanion.sdk.internal.generated.GeneratedContract as Contract
import com.tarkovcompanion.sdk.model.FleaOffer
import com.tarkovcompanion.sdk.model.ItemSummary
import com.tarkovcompanion.sdk.model.KeepReason
import com.tarkovcompanion.sdk.model.Recommendation
import com.tarkovcompanion.sdk.model.TraderOffer
import com.tarkovcompanion.sdk.model.Verdict
import org.json.JSONArray
import org.json.JSONObject

/** Core results (tarkov_companion.h) onto the public model, for the headless API. */
internal object ModelMapper {
    fun itemSummariesFromSearchResult(searchResult: ByteArray): List<ItemSummary> =
        JSONObject(searchResult.decodeToString()).getJSONArray(Contract.PAYLOAD_FIELD_ITEMS).objects().map { summary ->
            ItemSummary(
                id = summary.getString(Contract.PAYLOAD_FIELD_ID),
                name = summary.getString(Contract.PAYLOAD_FIELD_NAME),
                shortName = summary.optString(Contract.PAYLOAD_FIELD_SHORT_NAME),
                iconUrl = summary.optString(Contract.PAYLOAD_FIELD_ICON_LINK),
                slots = summary.optInt(Contract.PAYLOAD_FIELD_SLOTS, 1),
                averagePriceLast24HoursRubles = summary.optLongOrNull(Contract.PAYLOAD_FIELD_AVERAGE_PRICE_LAST24_HOURS),
            )
        }

    fun recommendationFromResult(recommendResult: ByteArray): Recommendation {
        val result = JSONObject(recommendResult.decodeToString())
        val recommendation = result.getJSONObject(Contract.PAYLOAD_FIELD_RECOMMENDATION)

        val bestTrader = recommendation.optJSONObject(Contract.PAYLOAD_FIELD_BEST_TRADER)?.let { trader ->
            TraderOffer(
                trader.getString(Contract.PAYLOAD_FIELD_VENDOR_ID),
                trader.getString(Contract.PAYLOAD_FIELD_VENDOR_NAME),
                trader.getLong(Contract.PAYLOAD_FIELD_PRICE_RUBLES),
            )
        }
        val flea = recommendation.optJSONObject(Contract.PAYLOAD_FIELD_FLEA)?.let { fleaOffer ->
            FleaOffer(
                available = fleaOffer.getBoolean(Contract.PAYLOAD_FIELD_AVAILABLE),
                unavailableReason = fleaOffer.optString(Contract.PAYLOAD_FIELD_UNAVAILABLE_REASON).ifEmpty { null },
                listPriceRubles = fleaOffer.getLong(Contract.PAYLOAD_FIELD_LIST_PRICE_RUBLES),
                feeRubles = fleaOffer.getLong(Contract.PAYLOAD_FIELD_FEE_RUBLES),
                netRubles = fleaOffer.getLong(Contract.PAYLOAD_FIELD_NET_RUBLES),
            )
        }
        val keepReasons = recommendation.getJSONArray(Contract.PAYLOAD_FIELD_KEEP_REASONS).objects().map { reason ->
            KeepReason(
                kind = KEEP_REASON_KINDS_BY_WIRE_NAME[reason.getString(Contract.PAYLOAD_FIELD_KIND)]
                    ?: KeepReason.Kind.HIDEOUT,
                label = reason.getString(Contract.PAYLOAD_FIELD_LABEL),
                count = reason.optInt(Contract.PAYLOAD_FIELD_COUNT, 1),
            )
        }

        return Recommendation(
            itemId = recommendation.getString(Contract.PAYLOAD_FIELD_ITEM_ID),
            itemName = recommendation.getString(Contract.PAYLOAD_FIELD_ITEM_NAME),
            verdict = verdictFromWireName(recommendation.getString(Contract.PAYLOAD_FIELD_VERDICT)),
            bestNetRubles = recommendation.getLong(Contract.PAYLOAD_FIELD_BEST_NET_RUBLES),
            bestNetPerSlotRubles = recommendation.getLong(Contract.PAYLOAD_FIELD_BEST_NET_PER_SLOT_RUBLES),
            bestTrader = bestTrader,
            flea = flea,
            keepReasons = keepReasons,
            pricesAreStale = recommendation.optBoolean(Contract.PAYLOAD_FIELD_PRICES_ARE_STALE),
            fromOfflineCache = result.optBoolean(Contract.PAYLOAD_FIELD_FROM_CACHE),
        )
    }

    private fun JSONArray.objects(): List<JSONObject> = (0 until length()).map { index -> getJSONObject(index) }

    private fun JSONObject.optLongOrNull(name: String): Long? = if (isNull(name)) null else getLong(name)

    private val VERDICTS_BY_WIRE_NAME = mapOf(
        Contract.PAYLOAD_VERDICT_KEEP to Verdict.KEEP,
        Contract.PAYLOAD_VERDICT_SELL_FLEA to Verdict.SELL_FLEA,
        Contract.PAYLOAD_VERDICT_SELL_TRADER to Verdict.SELL_TRADER,
        Contract.PAYLOAD_VERDICT_NO_BUYER to Verdict.NO_BUYER,
    )

    private val KEEP_REASON_KINDS_BY_WIRE_NAME = mapOf(
        Contract.PAYLOAD_KEEP_REASON_KIND_TASK to KeepReason.Kind.TASK,
        Contract.PAYLOAD_KEEP_REASON_KIND_HIDEOUT to KeepReason.Kind.HIDEOUT,
    )

    private fun verdictFromWireName(name: String): Verdict = VERDICTS_BY_WIRE_NAME[name] ?: Verdict.NO_BUYER
}
