# ADR 0005: Classic Kotlin plugin instead of AGP 9 built-in Kotlin (for now)

**Status:** accepted, temporary · 2026-10

## Context

AGP 9 compiles Kotlin itself ("built-in Kotlin") and deprecates applying `org.jetbrains.kotlin.android`. Public API checking is a hard requirement for this SDK (every API change visible in review, CI fails otherwise).

What we found, in order:

1. `binary-compatibility-validator` 0.18 hooks into the Kotlin plugin ids. With built-in Kotlin it registers no tasks (`apiDump` not found).
2. KGP's own ABI validation (`kotlin { abiValidation { } }`) isn't on the extension type AGP 9.4.1 exposes with its bundled KGP 2.2.10.
3. Pinning KGP 2.4.10 makes `checkKotlinAbi` / `updateKotlinAbi` appear, but the dump is empty: the ABI tasks don't see built-in-Kotlin compilations.

## Decision

`android.builtInKotlin=false` and `android.newDsl=false` in `gradle.properties`, apply `org.jetbrains.kotlin.android` 2.4.10, and use KGP's ABI validation (`checkKotlinAbi`, dump in `sdk-android/api/`).

## Consequences

- Deprecation warnings at configuration time; AGP 10 removes the opt-out.
- Revisit when KGP's ABI validation supports AGP built-in Kotlin. The migration is: delete the two properties and the plugin, keep the `abiValidation` block, check the dump is unchanged.
