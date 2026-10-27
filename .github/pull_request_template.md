## What and why

## Checklist
- [ ] Public API change? `./gradlew :sdk-android:updateKotlinAbi`, commit `sdk-android/api/`, note it in CHANGELOG.md
- [ ] Bridge protocol change? Updated docs/bridge-protocol.md, both sides (C++ core + TS), `contracts/fixtures/bridge_transcript.json`, and considered older/newer bundles
- [ ] Analytics change? `contracts/analytics-events.json` and docs/analytics.md updated (names are a compatibility promise)
- [ ] Core output change? `contracts/fixtures/core_recommendation.json` updated, web validators still pass
- [ ] Footprint delta in the CI summary looks expected
