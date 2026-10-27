# ADR 0001: Business logic in a C++ core behind a C API

**Status:** accepted · 2026-10 · API surface refined by [ADR 0006](0006-sans-io-session.md)

## Context

The SDK runs inside games. Most are C++; their platform layers differ (NativeActivity, GameActivity, Unity, Unreal), and the product should exist on iOS too. The logic to share is small but has to agree across platforms to the rouble: parsing tarkov.dev responses, the flea-fee formula, the keep-or-sell decision, and the bounded catalog cache.

## Decision

- Logic lives in `core/` (C++17), built as a static library.
- The only public surface is a flat C API (`core/include/tarkov_companion/tarkov_companion.h`): opaque handle, status codes, UTF-8 JSON in and out, caller-frees strings via `tarkov_companion_free`.
- Settings are JSON documents whose keys come from `contracts/config-limits.json`; unknown keys are ignored and reported, so an older caller works with a newer core ([ADR 0006](0006-sans-io-session.md)).
- Android talks to it through a thin JNI layer (`jni_bridge.cpp`) using `RegisterNatives`, byte arrays instead of `jstring`, and status codes instead of thrown Java exceptions.
- Built with `-fno-exceptions -fno-rtti`, static libc++, symbols hidden.

## Why not ...

- **Kotlin Multiplatform:** it covers Android and iOS but not a game engine calling in from C++, and adds a Kotlin/Native runtime to the iOS binary.
- **Logic in Kotlin, port to Swift later:** two implementations of the fee formula drift.
- **C++ API (classes) across the boundary:** C++ ABI isn't stable across compilers/STLs; a game built with a different libc++ couldn't link it.
- **`jstring` for JSON:** JNI's "modified UTF-8" mangles characters outside the BMP. `NativeSessionInstrumentedTest.nonBmpCharactersSurviveTheJniBoundary` guards this.

## Consequences

- Two languages in the SDK repo; contributors need CMake and the NDK. Mitigated by tests at both levels and CI on every PR.
- JSON at the boundary costs a parse per call. Fine for a lookup per tap; would need revisiting for per-frame calls. Large bodies (HTTP responses, cache values) cross as raw bytes beside the JSON, never inside it (ADR 0006).
- The core's output format is a contract with the web UI. It's pinned by a golden file (`contracts/fixtures/core_recommendation.json`) tested from both C++ and TypeScript.
- Static libc++ pulls in libc++abi extras; the 118 KB demangler is replaced by a stub ([footprint.md](../footprint.md)).
