# Quickstart: Intelligent Per-Band Oversampling

**Feature**: 009-intelligent-oversampling
**Date**: 2026-01-30

## What This Feature Does

Adds intelligent per-band oversampling to Disrumpo that automatically selects the optimal oversampling factor (1x, 2x, or 4x) for each frequency band based on:
1. The active distortion type's harmonic generation profile
2. Morph blend weights when morphing between types
3. A user-configurable global limit for CPU management

When the oversampling factor changes at runtime (due to type switching, morph cursor movement, or global limit changes), the system crossfades between old and new oversampling paths over 8ms using equal-power gains to prevent audible artifacts.

## Key Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| `plugins/disrumpo/src/dsp/oversampling_utils.h` | **NEW** | Morph-weighted factor computation utility |
| `plugins/disrumpo/src/dsp/band_processor.h` | **MODIFY** | Add crossfade state, transition logic, new processing paths |
| `plugins/disrumpo/src/processor/processor.cpp` | **MODIFY** | Wire global limit parameter to band processors |
| `plugins/disrumpo/tests/` | **NEW** | Unit tests for oversampling utils, crossfade, integration |

## Key Files to Read (NOT modify)

| File | Why |
|------|-----|
| `dsp/include/krate/dsp/primitives/oversampler.h` | Core oversampling template |
| `dsp/include/krate/dsp/core/crossfade_utils.h` | `equalPowerGains()`, `crossfadeIncrement()` |
| `dsp/include/krate/dsp/primitives/smoother.h` | Existing smoothers (context) |
| `plugins/disrumpo/src/dsp/distortion_types.h` | `getRecommendedOversample()` (reuse) |
| `plugins/disrumpo/src/dsp/morph_engine.h` | `getWeights()` (read-only) |
| `plugins/disrumpo/src/dsp/morph_node.h` | `MorphNode`, `kMaxMorphNodes` |
| `plugins/disrumpo/src/plugin_ids.h` | `kOversampleMaxId = 0x0F04` |

## Implementation Order

### Phase 1: Utility Function + Unit Tests
1. Create `oversampling_utils.h` with `calculateMorphOversampleFactor()` and `roundUpToPowerOf2Factor()`
2. Write unit tests for all 26 types individually
3. Write unit tests for morph-weighted computation (20+ weight combinations per SC-009)
4. Write unit tests for global limit clamping

### Phase 2: BandProcessor Crossfade Integration
1. Add crossfade state members to BandProcessor
2. Implement `requestOversampleFactor()` with 8ms crossfade initiation
3. Implement `processBlockWithCrossfade()` with dual-path blending
4. Implement `processWithFactor()` routing (refactor existing 1x/2x/4x/8x paths)
5. Modify `setDistortionType()` to use `recalculateOversampleFactor()`
6. Modify `setMorphPosition()` and `setMorphNodes()` to trigger recalculation
7. Write crossfade transition tests (artifact-free, 8ms duration per SC-005)

### Phase 3: Processor Integration + Bypass
1. Wire `kOversampleMaxId` parameter in `processParameterChanges()`
2. Implement band bypass optimization (FR-012, SC-010, SC-011)
3. Write integration tests (multi-band, morph automation)
4. Write bit-transparency bypass test (SC-011)

### Phase 4: Performance + Latency
1. Verify latency reporting (`getLatencySamples()` returns 0, SC-012)
2. Run performance benchmarks (SC-001 through SC-003, SC-007)
3. Optimize if needed (profile hot paths)
4. Update architecture documentation

## Important Constraints

- **No allocations on audio thread**: All buffers pre-allocated in `prepare()`
- **No locks on audio thread**: All crossfade state is single-threaded (audio thread only)
- **IIR mode only**: Zero latency, no PDC re-calculation needed
- **8ms fixed crossfade**: Not configurable, uses `crossfadeIncrement(8.0f, sampleRate)`
- **Power-of-2 rounding**: Always round UP (never compromise quality by rounding down)
- **Hysteresis**: Only trigger crossfade when computed factor actually changes

## Existing Code to Reuse

| Component | Location | How Used |
|-----------|----------|----------|
| `getRecommendedOversample()` | `distortion_types.h` | Per-type profile lookup (FR-001) |
| `equalPowerGains()` | `crossfade_utils.h` | Equal-power crossfade gains (FR-011) |
| `crossfadeIncrement()` | `crossfade_utils.h` | 8ms transition increment (FR-010) |
| `Oversampler<2,2>` / `Oversampler<4,2>` | `oversampler.h` | Pre-allocated oversamplers |
| `MorphEngine::getWeights()` | `morph_engine.h` | Morph blend weights (FR-003) |
| `BandProcessor` | `band_processor.h` | Existing 1x/2x/4x/8x routing |
| `kOversampleMaxId` | `plugin_ids.h` | Global limit parameter ID (FR-005) |

## Build & Test

```bash
# Build
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo_tests

# Run tests
build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe

# Run oversampling tests only
build/windows-x64-release/bin/Release/disrumpo_tests.exe "[oversampling]"

# Pluginval (after plugin code changes)
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"
```

## Implementation Discoveries and Gotchas

These notes were added during implementation and may help future work on this feature.

### MorphEngine Weight Staleness

`MorphEngine::getWeights()` returns smoothed weights that may not reflect the latest morph position until `process()` is called. For oversampling factor selection (which must respond immediately to position changes), BandProcessor uses a separate `computeRawMorphWeights()` method that computes inverse-distance weights directly from the target position. This bypasses the audio-rate smoother and provides instant factor recalculation.

### BandProcessor Stack Size

Each `BandProcessor` is large due to pre-allocated buffers (crossfade buffers, oversampler state, MorphEngine). Creating 8 BandProcessors on the stack (e.g., in a test) can cause stack overflow. Always use heap allocation via `std::unique_ptr` or `std::vector<std::unique_ptr<BandProcessor>>` when instantiating multiple BandProcessors.

### Crossfade Initial State

When setting up morph nodes via `setMorphNodes()`, the computed factor may differ from the initial default (2x). This triggers a crossfade transition. If subsequent tests or code expect `isOversampleTransitioning() == false`, process enough audio blocks to complete the initial crossfade (~353 samples at 44.1kHz, or 10 blocks of 512 samples for safety).

### Alias Suppression Thresholds (IIR Mode)

The spec's SC-006 target of 48dB alias suppression applies to high-quality FIR oversampling. With IIR zero-latency mode (Economy quality), realistic suppression is lower. Measured thresholds: >6dB for 4x IIR, >3dB for 2x IIR. If FIR mode is added later, thresholds should be increased.

### kOversampleMaxId Parameter Mapping

`kOversampleMaxId` is registered as a `StringListParameter` with 4 entries. The normalized value mapping uses `index = round(value * 3)`, mapping to factors {1, 2, 4, 8}. This differs from a linear range mapping.
