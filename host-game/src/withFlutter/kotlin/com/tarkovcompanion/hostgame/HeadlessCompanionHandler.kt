package com.tarkovcompanion.hostgame

import com.tarkovcompanion.sdk.Cancellable
import com.tarkovcompanion.sdk.CompanionCallback
import com.tarkovcompanion.sdk.CompanionException
import com.tarkovcompanion.sdk.TarkovCompanion
import com.tarkovcompanion.sdk.model.ItemSummary
import com.tarkovcompanion.sdk.model.KeepReason
import com.tarkovcompanion.sdk.model.Recommendation
import com.tarkovcompanion.sdk.model.Verdict
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

/**
 * Answers the Flutter screen (experiments/flutter_overlay/lib/companion_channel.dart)
 * with the SDK's headless API. Main thread, where the SDK also calls back.
 */
internal class HeadlessCompanionHandler : MethodChannel.MethodCallHandler {
    private var pendingSearch: Cancellable? = null
    private var pendingSearchResult: MethodChannel.Result? = null

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "search" -> search(call.argument<String>("query").orEmpty(), result)
            "recommend" -> recommend(call.argument<String>("itemId").orEmpty(), result)
            else -> result.notImplemented()
        }
    }

    // A cancelled call never calls back, so the replaced search is answered here.
    private fun search(query: String, result: MethodChannel.Result) {
        pendingSearch?.cancel()
        pendingSearchResult?.error(SUPERSEDED, "A newer search replaced this one", null)
        pendingSearchResult = result

        pendingSearch = TarkovCompanion.searchItems(
            query,
            object : CompanionCallback<List<ItemSummary>> {
                override fun onSuccess(result: List<ItemSummary>) {
                    finishSearch()?.success(result.map(::itemSummaryToMap))
                }

                override fun onError(error: CompanionException) {
                    finishSearch()?.error(error.code.name.lowercase(), error.message, null)
                }
            },
        )
    }

    private fun finishSearch(): MethodChannel.Result? {
        val result = pendingSearchResult
        pendingSearch = null
        pendingSearchResult = null

        return result
    }

    private fun recommend(itemId: String, result: MethodChannel.Result) {
        TarkovCompanion.getRecommendation(
            itemId,
            object : CompanionCallback<Recommendation> {
                override fun onSuccess(recommendation: Recommendation) {
                    result.success(recommendationToMap(recommendation))
                }

                override fun onError(error: CompanionException) {
                    result.error(error.code.name.lowercase(), error.message, null)
                }
            },
        )
    }

    private fun itemSummaryToMap(item: ItemSummary): Map<String, Any?> = mapOf(
        "id" to item.id,
        "name" to item.name,
        "shortName" to item.shortName,
        "iconUrl" to item.iconUrl,
        "slots" to item.slots,
        "averagePriceLast24HoursRubles" to item.averagePriceLast24HoursRubles,
    )

    private fun recommendationToMap(recommendation: Recommendation): Map<String, Any?> = mapOf(
        "itemName" to recommendation.itemName,
        "verdict" to verdictWireName(recommendation.verdict),
        "bestNetRubles" to recommendation.bestNetRubles,
        "bestNetPerSlotRubles" to recommendation.bestNetPerSlotRubles,
        "bestTrader" to recommendation.bestTrader?.let { trader ->
            mapOf("vendorName" to trader.vendorName, "priceRubles" to trader.priceRubles)
        },
        "flea" to recommendation.flea?.let { flea ->
            mapOf(
                "available" to flea.available,
                "unavailableReason" to flea.unavailableReason,
                "listPriceRubles" to flea.listPriceRubles,
                "feeRubles" to flea.feeRubles,
                "netRubles" to flea.netRubles,
            )
        },
        "keepReasons" to recommendation.keepReasons.map { reason ->
            mapOf(
                "kind" to if (reason.kind == KeepReason.Kind.TASK) "task" else "hideout",
                "label" to reason.label,
                "count" to reason.count,
            )
        },
        "pricesAreStale" to recommendation.pricesAreStale,
        "fromOfflineCache" to recommendation.fromOfflineCache,
    )

    private fun verdictWireName(verdict: Verdict): String = when (verdict) {
        Verdict.KEEP -> "keep"
        Verdict.SELL_FLEA -> "sell_flea"
        Verdict.SELL_TRADER -> "sell_trader"
        else -> "no_buyer"
    }

    private companion object {
        const val SUPERSEDED = "superseded"
    }
}
