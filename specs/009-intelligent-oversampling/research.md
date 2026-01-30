# Research: Intelligent Per-Band Oversampling

**Feature**: 009-intelligent-oversampling
**Date**: 2026-01-30

## Research Questions & Findings

### RQ-1: Existing Oversampling Infrastructure in BandProcessor

**Decision**: Extend `BandProcessor` in-place rather than replacing it.

**Rationale**: `BandProcessor` (at `plugins/disrumpo/src/dsp/band_processor.h`) already contains:
- `Oversampler<2, 2> oversampler2x_` and `Oversampler<4, 2> oversampler4x_` instances
- `int currentOversampleFactor_` and `int maxOversampleFactor_` members
- `setMaxOversampleFactor()` with clamping logic
- `setDistortionType()` that calls `getRecommendedOversample()` and sets `currentOversampleFactor_`
- `processBlock()` routing to 1x, 2x, 4x, or 8x paths

What is **missing** and needs to be added:
1. Morph-weighted factor computation (FR-003, FR-004)
2. 8ms crossfade transitions between oversampling paths (FR-010, FR-011)
3. Proper 1x direct bypass path (FR-012, FR-020)
4. Hysteresis to prevent unnecessary transitions during continuous morphing (FR-017)

**Alternatives considered**:
- Creating a separate `DynamicOversamplerAdapter` class wrapping oversamplers: Rejected because BandProcessor already owns the oversamplers and the routing logic. A separate adapter would require extracting the distortion/morph processing callback, adding unnecessary indirection. The crossfade logic belongs inside BandProcessor.

### RQ-2: Oversampling Profile Table (FR-001, FR-014)

**Decision**: Reuse existing `getRecommendedOversample()` in `distortion_types.h`.

**Rationale**: The function already exists at `plugins/disrumpo/src/dsp/distortion_types.h`. It maps all 26 `DistortionType` values to factors 1, 2, or 4 exactly as specified in FR-014:
- 4x: HardClip, Fuzz, AsymmetricFuzz, SineFold, TriangleFold, SergeFold, FullRectify, HalfRectify, RingSaturation, AllpassResonant (10 types)
- 1x: Bitcrush, SampleReduce, Quantize, Aliasing, BitwiseMangler, Spectral (6 types)
- 2x: SoftClip, Tube, Tape, Temporal, FeedbackDist, Chaos, Formant, Granular, Fractal, Stochastic (10 types, default)

**Alternatives considered**:
- Separate lookup table or `constexpr std::array`: Rejected. The switch-based function is already in use, is `constexpr`, and matches the spec exactly. No change needed.

### RQ-3: Morph-Weighted Factor Computation (FR-003, FR-004)

**Decision**: Add a new free function `calculateMorphOversampleFactor()` in a new header `oversampling_utils.h` inside the Disrumpo plugin DSP folder.

**Rationale**: The function needs to:
1. Accept morph weights (`std::array<float, kMaxMorphNodes>`) and nodes (for type info)
2. Compute weighted average: `sum(weight_i * getRecommendedOversample(node_i.type))` for active nodes
3. Round up to nearest power of 2 (1, 2, or 4) per FR-004
4. Clamp to global limit per FR-007, FR-008

This is a pure function (stateless, `constexpr`-capable) that does not belong to any class. Placing it in `oversampling_utils.h` keeps it testable independently and follows the project pattern of utility headers.

**Alternatives considered**:
- Adding the function as a method on MorphEngine: Rejected. MorphEngine should not have oversampling awareness -- separation of concerns.
- Adding to BandProcessor: Rejected. A member function would be harder to unit test independently.
- Adding to `distortion_types.h`: Rejected. That file is about type definitions and per-type queries; morph-weighted computation requires knowledge of the morph node structure.

### RQ-4: Crossfade Strategy for Factor Transitions (FR-010, FR-011)

**Decision**: Implement dual-path crossfade directly in BandProcessor's `processBlock()` method, using `crossfadeIncrement()` and `equalPowerGains()` from `crossfade_utils.h` (Layer 0).

**Rationale**: The spec requires:
- Fixed 8ms transition duration
- Equal-power crossfade: old path `cos(pi/2 * t)`, new path `sin(pi/2 * t)`
- Dual-path processing: both old and new oversamplers run simultaneously during transition
- Abort-and-restart: if factor changes mid-transition, restart from current state

The existing `crossfade_utils.h` provides exactly the math needed:
```cpp
// From crossfade_utils.h (Layer 0)
inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept;
[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept;
```

Implementation approach:
- BandProcessor stores `crossfadeProgress_` (0.0 to 1.0), `crossfadeIncrement_` (per-sample), `crossfadeActive_` flag
- `crossfadeOldFactor_` tracks which oversampler to fade out
- `crossfadeNewFactor_` tracks which oversampler to fade in
- During crossfade, `processBlock()` runs both old and new paths and blends per-sample
- When factor changes trigger, set new target and compute increment: `crossfadeIncrement(8.0f, sampleRate_)`

**Alternatives considered**:
- Using `LinearRamp` for crossfade progress: Considered but rejected in favor of a raw float accumulator. LinearRamp adds overhead (NaN checks, denormal flushing) that is unnecessary for a simple 0-to-1 ramp. The crossfade increment from `crossfade_utils.h` is simpler and purpose-built.
- Using `OnePoleSmoother` for crossfade: Rejected. The spec requires a fixed 8ms duration (constant rate), not exponential approach. OnePoleSmoother would give unpredictable duration.
- Separate CrossfadeManager class: Rejected. The crossfade state is tightly coupled to BandProcessor's oversampler instances and processing paths. Extracting it would require passing oversamplers and callbacks through an abstraction layer for minimal benefit.

### RQ-5: 1x Direct Bypass Path (FR-012, FR-020)

**Decision**: When factor is 1x, skip oversampler entirely and process distortion/morph directly in `processBlock()`.

**Rationale**: The existing code in `band_processor.h` already has a 1x path:
```cpp
if (bypassDistortion || currentOversampleFactor_ == 1) {
    for (size_t i = 0; i < numSamples; ++i) {
        process(left[i], right[i]);
    }
}
```
This path does not use oversampling and processes directly. For FR-012 (bit-transparent bypass when band is bypassed), the band bypass flag needs to be checked before any processing occurs, and input must be passed through unchanged.

For transitions involving 1x (e.g., 1x to 4x or 4x to 1x per FR-020), the crossfade blends the direct path output with the oversampled path output. Both paths run during the 8ms transition.

### RQ-6: Global Oversampling Limit Parameter (FR-005, FR-006, FR-015)

**Decision**: Use the existing `kOversampleMaxId = 0x0F04` parameter. Wire it through the processor to call `setMaxOversampleFactor()` on all band processors.

**Rationale**: The parameter ID is already defined in `plugin_ids.h`:
```cpp
kOversampleMaxId = 0x0F04,  // 3844 - Max oversample factor
```
And `GlobalParamType::kGlobalOversample = 0x04`. The Controller already registers this as a StringListParameter with options "1x", "2x", "4x", "8x".

The Processor needs to:
1. Handle the parameter change in `processParameterChanges()`
2. Map normalized value [0, 1] to discrete values {1, 2, 4, 8}
3. Call `setMaxOversampleFactor()` on all 8 band processors
4. Each BandProcessor re-clamps its current factor and triggers crossfade if needed

### RQ-7: Latency Reporting (FR-018, FR-019)

**Decision**: Report 0 latency always (IIR mode).

**Rationale**: The existing `BandProcessor::getLatency()` already returns 0. The Oversampler in Economy/ZeroLatency mode (`OversamplingQuality::Economy`, `OversamplingMode::ZeroLatency`) produces 0 latency. Since all oversamplers are prepared with these defaults in `band_processor.h`, the system inherently satisfies FR-018 and FR-019.

No changes needed for latency reporting.

### RQ-8: Pre-Allocation Strategy (FR-009)

**Decision**: All oversamplers are already pre-allocated in `BandProcessor::prepare()`.

**Rationale**: The current code already prepares both `oversampler2x_` and `oversampler4x_` (and `oversampler8xInner_`) regardless of the current factor. They remain in memory and are simply not invoked when not needed. This satisfies FR-009 without any changes.

### RQ-9: Crossfade Buffer Strategy for Dual-Path Processing

**Decision**: Allocate temporary crossfade buffers in `BandProcessor::prepare()` for the "old path" output during transitions.

**Rationale**: During an 8ms crossfade, both the old and new oversampling paths must produce output simultaneously. The "new" path can write to the original left/right buffers, but the "old" path needs its own buffers. Since we must not allocate on the audio thread, these buffers must be pre-allocated.

Required additional members:
- `std::array<float, kMaxBlockSize> crossfadeOldLeft_` -- old path left output
- `std::array<float, kMaxBlockSize> crossfadeOldRight_` -- old path right output

At `kMaxBlockSize = 2048` and 4 bytes per float, this adds 16KB per BandProcessor, or 128KB total for 8 bands. This is acceptable per the spec's memory budget analysis (256KB budgeted for oversampler buffers).

### RQ-10: Abort-and-Restart Transition Behavior (FR-010)

**Decision**: When a new factor change occurs mid-transition, capture the current blended state and start a new crossfade.

**Rationale**: The spec explicitly states: "If a new factor change is requested while a transition is already in progress, the current transition MUST be immediately aborted and a new 8ms transition MUST begin from the current crossfade state to the new target factor."

Implementation:
1. If crossfade is active and a new target factor is set:
   a. The current "blended" output becomes the new "old" path conceptually
   b. Reset `crossfadeProgress_` to 0.0
   c. Set `crossfadeOldFactor_` to the currently-being-blended effective factor
   d. Set `crossfadeNewFactor_` to the new target
2. In practice, since we process per-block not per-sample for oversamplers, the "old" path during an abort is whichever oversampler was being faded in (it was partially active). We can simplify by:
   a. Making the new "old" factor = the current `crossfadeNewFactor_` (what was being faded in)
   b. Starting a fresh crossfade from 0.0 to the new target

This avoids complex mid-crossfade state capture while still honoring the spec's intent.

## Dependency Best Practices

### Oversampler Template (Krate::DSP)

The `Oversampler<Factor, NumChannels>` is a compile-time template. Key usage patterns:
- Factor is a template parameter (2 or 4), not a runtime variable
- `prepare()` must be called before `process()` (allocates internal buffers)
- `reset()` clears filter states without deallocating
- `process()` takes a callback that receives oversampled buffers and length
- Non-copyable, movable
- `getLatency()` returns 0 for Economy/ZeroLatency mode

### crossfade_utils.h (Krate::DSP, Layer 0)

- `equalPowerGains(position, fadeOut, fadeIn)` -- does NOT clamp position; caller must keep in [0, 1]
- `crossfadeIncrement(durationMs, sampleRate)` -- returns per-sample increment; returns 1.0 if duration is 0
- Both are `inline` free functions, safe for audio thread

### Smoother (Krate::DSP, Layer 1)

- Not used directly for crossfade progress (see RQ-4), but `OnePoleSmoother` is used elsewhere in BandProcessor for gain/pan/mute smoothing
- API: `configure(timeMs, sampleRate)`, `setTarget(value)`, `process()`, `snapTo(value)`, `isComplete()`

## Integration Patterns

### BandProcessor Integration Points

1. **Factor recalculation trigger**: `setDistortionType()` already calls `getRecommendedOversample()`. Need to also trigger from morph position/node changes.

2. **Morph-aware recalculation**: After `setMorphPosition()` or `setMorphNodes()`, query `MorphEngine::getWeights()` and call `calculateMorphOversampleFactor()` to get new target. Only trigger crossfade if target differs from current.

3. **Global limit change**: `setMaxOversampleFactor()` already clamps `currentOversampleFactor_`. Need to add crossfade trigger when the clamped value differs from the pre-clamp value.

4. **processBlock() modifications**: Add crossfade path selection between existing 1x/2x/4x/8x paths. During crossfade, run both old and new paths and blend.

### Processor Integration Points

1. **Parameter handling**: `processParameterChanges()` already handles global parameters. Need to add `kOversampleMaxId` handling to map normalized value to {1, 2, 4, 8} and propagate to all band processors.

2. **Morph position forwarding**: Already handled via existing morph parameter routing. The new morph-weighted oversampling recalculation piggybacks on existing morph position updates.
