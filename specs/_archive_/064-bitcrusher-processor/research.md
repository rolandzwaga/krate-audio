# Research: BitcrusherProcessor

**Feature**: 064-bitcrusher-processor | **Date**: 2026-01-14

## Summary

Research findings for implementing BitcrusherProcessor as a Layer 2 processor composing existing Layer 1 primitives. All technical unknowns have been resolved through codebase analysis.

---

## Research Tasks

### Task 1: Existing Primitive API Verification

**Question**: Do the existing BitCrusher and SampleRateReducer primitives provide all needed functionality?

**Findings**:

1. **BitCrusher** (`dsp/include/krate/dsp/primitives/bit_crusher.h`):
   - Provides bit depth reduction [4, 16] bits - matches FR-001
   - Provides TPDF dither [0, 1] - matches FR-003
   - Has `setSeed()` for RNG state - useful for stereo decorrelation
   - Sample-by-sample processing via `process(float)` - compatible with per-sample dither gating
   - Constants: `kMinBitDepth=4`, `kMaxBitDepth=16`, `kDefaultBitDepth=16`

2. **SampleRateReducer** (`dsp/include/krate/dsp/primitives/sample_rate_reducer.h`):
   - Provides sample rate reduction factor [1, 8] - matches FR-002
   - Uses sample-and-hold technique with fractional support
   - Constants: `kMinReductionFactor=1`, `kMaxReductionFactor=8`

**Decision**: Existing primitives are sufficient. No modifications needed.

**Rationale**: Both primitives provide the exact functionality specified in FR-001 through FR-003.

**Alternatives considered**: Creating new primitives - rejected as existing ones fully meet requirements.

---

### Task 2: Dither Gating Implementation Pattern

**Question**: How to implement dither gating at -60dB threshold using envelope detection?

**Findings**:

1. **EnvelopeFollower** (`dsp/include/krate/dsp/processors/envelope_follower.h`):
   - Layer 2 processor - can be used within BitcrusherProcessor (Layer 2)
   - Provides `DetectionMode::Amplitude` for full-wave rectification
   - Configurable attack/release times for smooth gating
   - `processSample()` returns envelope value per sample

2. **-60dB Threshold**:
   - Linear value: `dbToGain(-60.0f)` = 0.001
   - When envelope < 0.001, disable dither (set to 0)
   - When envelope >= 0.001, restore dither amount

3. **Attack/Release for Gate**:
   - Fast attack (1-5ms): Quickly enable dither when signal present
   - Slower release (10-20ms): Prevent pumping on transients (FR-003b)

**Decision**: Use EnvelopeFollower with Amplitude mode for dither gate signal detection.

**Rationale**: EnvelopeFollower already provides signal envelope tracking with configurable attack/release to avoid pumping artifacts.

**Alternatives considered**:
- Simple peak detection: Rejected - would cause pumping on transients
- Moving average: Rejected - more complex, EnvelopeFollower already exists

---

### Task 3: Processing Order Implementation

**Question**: How to implement configurable processing order (BitCrushFirst vs SampleReduceFirst)?

**Findings**:

1. **Spec Requirements**:
   - FR-004a: ProcessingOrder enum with BitCrushFirst (default) and SampleReduceFirst
   - FR-004b: Immediate switch (no crossfade)
   - FR-004c: Default is BitCrushFirst

2. **Implementation Pattern** (from TapeSaturator):
   - TapeSaturator uses model switching with 10ms crossfade
   - For BitcrusherProcessor, spec explicitly states NO crossfade (FR-004b)
   - Simple enum check in process loop is sufficient

3. **Processing Order Effects**:
   - BitCrushFirst: Quantization noise is then aliased by sample reduction
   - SampleReduceFirst: Sample-and-hold creates stairstep, then quantized

**Decision**: Implement as simple enum switch with no crossfade, checked per sample.

**Rationale**: Matches spec requirement FR-004b for immediate switching.

**Alternatives considered**:
- Crossfade between orders: Rejected - spec explicitly requires immediate switch
- Separate processing functions: Accepted - cleaner code organization

---

### Task 4: Gain Staging Pattern

**Question**: What is the established pattern for pre/post gain staging in Layer 2 processors?

**Findings**:

1. **TapeSaturator Pattern** (`dsp/include/krate/dsp/processors/tape_saturator.h`):
   - Drive parameter in dB [-24, +24]
   - Converted to linear using `dbToGain()`
   - Smoothed with OnePoleSmoother (5ms)
   - Applied before saturation

2. **SaturationProcessor Pattern** (`dsp/include/krate/dsp/processors/saturation_processor.h`):
   - inputGainDb_ and outputGainDb_ [-24, +24]
   - Converted to linear, smoothed, applied
   - Dry buffer stored for mix blending

3. **Mix Implementation**:
   - Store dry signal before processing
   - Blend: `output = dry * (1-mix) + wet * mix`
   - Smooth mix parameter to prevent clicks

**Decision**: Follow SaturationProcessor pattern with pre-gain, post-gain, and smoothed mix.

**Rationale**: Established pattern in codebase, matches FR-005 through FR-011.

**Alternatives considered**: None - pattern is well-established and matches spec.

---

### Task 5: DC Blocking Configuration

**Question**: What DCBlocker configuration is appropriate for bitcrushing artifacts?

**Findings**:

1. **Spec Requirements**:
   - FR-012: DC blocking after processing
   - FR-013: ~10Hz cutoff

2. **DCBlocker API**:
   - `prepare(double sampleRate, float cutoffHz = 10.0f)` - default 10Hz
   - First-order highpass: 3 ops/sample
   - Settling time: ~40ms at 10Hz cutoff

3. **Codebase Precedent**:
   - TapeSaturator: `kDCBlockerCutoffHz = 10.0f`
   - SaturationProcessor: `kDCBlockerCutoffHz = 10.0f`
   - DiodeClipper: Uses DCBlocker at 10Hz

**Decision**: Use DCBlocker with 10Hz cutoff (default).

**Rationale**: Standard configuration used across all Layer 2 processors.

**Alternatives considered**:
- DCBlocker2 (2nd-order): Rejected - overkill for bitcrushing DC, which is mild
- Lower cutoff (5Hz): Rejected - slower settling, 10Hz is standard

---

### Task 6: Parameter Smoothing Requirements

**Question**: Which parameters need smoothing and which are immediate?

**Findings**:

1. **Smoothed Parameters** (cause clicks if changed abruptly):
   - Pre-gain (FR-008): Amplitude change causes click
   - Post-gain (FR-009): Amplitude change causes click
   - Mix (FR-010): Wet/dry blend change causes click
   - Dither amount (implicit): May cause audible change

2. **Immediate Parameters** (per spec clarifications):
   - Bit depth (FR-001a): Integer quantization levels - interpolation undefined
   - Sample rate factor (FR-002a): Discrete sample-and-hold state
   - Processing order (FR-004b): Consistent with other discrete parameters

3. **Smoothing Time**:
   - FR-011: 5ms (standard for most parameters)
   - OnePoleSmoother default: `kDefaultSmoothingTimeMs = 5.0f`

**Decision**: Smooth pre-gain, post-gain, mix; immediate bit depth, factor, order.

**Rationale**: Matches spec clarifications and established patterns.

**Alternatives considered**: None - spec explicitly defines which are smoothed vs immediate.

---

### Task 7: Bypass Optimization

**Question**: What bypass optimizations should be implemented?

**Findings**:

1. **Spec Requirements**:
   - FR-020: mix=0% bypasses wet processing
   - FR-021: bitDepth=16, factor=1 minimal processing

2. **SaturationProcessor Pattern**:
   ```cpp
   if (currentMix < 0.0001f) {
       // Still advance smoothers
       return;  // Buffer unchanged
   }
   ```

3. **Minimal Processing Detection**:
   - At bit depth 16, quantization is effectively transparent
   - At factor 1, no sample rate reduction
   - Could skip BitCrusher/SampleRateReducer processing

**Decision**: Implement mix=0 bypass; consider bitDepth=16 && factor=1 optimization.

**Rationale**: Matches established patterns, improves efficiency.

**Alternatives considered**:
- No optimizations: Rejected - unnecessary CPU usage
- More granular bypass: Rejected - adds complexity for minimal gain

---

## Resolved Clarifications

All technical unknowns from the spec have been resolved:

| Unknown | Resolution |
|---------|------------|
| Processing order implementation | Simple enum switch, no crossfade |
| Dither gate detection | EnvelopeFollower with Amplitude mode |
| Gain staging pattern | SaturationProcessor pattern (pre/post/mix) |
| DC blocker configuration | 10Hz cutoff (standard) |
| Smoothing configuration | 5ms for gains/mix, immediate for discrete |
| Bypass optimization | Mix=0 early return, buffer unchanged |

---

## Technical Decisions Summary

| Decision | Rationale |
|----------|-----------|
| Reuse EnvelopeFollower for dither gate | Existing Layer 2 component, no duplication |
| ProcessingOrder enum in processor | Scoped to this processor's needs |
| 5ms smoothing time | Standard for Layer 2 processors |
| 10Hz DC blocker | Standard configuration across codebase |
| No crossfade on order switch | Per spec FR-004b requirement |
| First-order DCBlocker | Sufficient for mild bitcrushing DC |
