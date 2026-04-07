# Research: Innexus ADSR Envelope Detection

**Branch**: `124-adsr-envelope-detection` | **Date**: 2026-03-08

## R1: Envelope Detection Algorithm Design

### Decision: Peak-finding + O(1) Rolling Least-Squares Steady-State Detection

**Rationale**: The spec mandates a specific algorithm (FR-002): find peak amplitude frame, detect steady-state using rolling statistics. The O(1) rolling least-squares approach avoids per-frame recomputation and is analysis-thread safe (no real-time constraints, but should be efficient).

**Algorithm Overview**:
1. Read `globalAmplitude` from each `HarmonicFrame` in the `SampleAnalysis.frames` vector
2. Find the peak frame index (max `globalAmplitude`)
3. Attack time = peak frame index * `hopTimeSec` (in seconds, convert to ms)
4. Starting from peak frame, slide an 8-20 frame window forward:
   - Compute rolling least-squares slope and variance using Welford's algorithm variant
   - Steady-state detected when BOTH `|slope| < 0.0005/frame` AND `variance < 0.002`
5. Sustain level = mean `globalAmplitude` over steady-state region, relative to peak (ratio 0-1)
6. Release = time from last steady-state frame to contour end, or default 100ms if no clear decay

**Rolling Least-Squares O(1) Algorithm**:
Uses the formulas for online linear regression:
- Maintain running sums: `sum_x`, `sum_y`, `sum_xy`, `sum_x2`, `n`
- When window slides: subtract oldest element, add newest
- Slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x^2)
- Variance computed via Welford's online algorithm (running mean and M2)

**Alternatives Considered**:
- Machine learning envelope fitting: Rejected per spec -- too complex for the use case
- Simple threshold-based detection: Less robust for diverse samples
- Hilbert transform envelope: Adds DSP complexity, `globalAmplitude` already provides smoothed RMS

## R2: Where to Place the Envelope Detector

### Decision: `plugins/innexus/src/dsp/envelope_detector.h` (plugin-local DSP)

**Rationale**: The envelope detection is analysis-time only (not audio-thread), specific to Innexus's `SampleAnalysis` structure, and tightly coupled to the `HarmonicFrame` concept. Placing it in `plugins/innexus/src/dsp/` follows the existing pattern for plugin-local DSP (e.g., `sample_analyzer.h`, `evolution_engine.h`, `spectral_decay_envelope.h`).

**Alternatives Considered**:
- `dsp/include/krate/dsp/core/`: Could be shared, but the algorithm is analysis-specific, not a general utility. Only one consumer currently. Constitution says wait for 2+ consumers before extracting.
- `dsp/include/krate/dsp/processors/`: Wrong layer -- this is analysis code, not a real-time processor.

## R3: ADSREnvelope DSP Class Integration

### Decision: Reuse `Krate::DSP::ADSREnvelope` directly as the audio-thread envelope generator

**Rationale**: The existing class at `dsp/include/krate/dsp/primitives/adsr_envelope.h` has the exact API needed:
- `prepare(sampleRate)` -- called in `setupProcessing()`
- `setAttack(ms)`, `setDecay(ms)`, `setSustain(level)`, `setRelease(ms)` -- parameter update
- `setAttackCurve(float)`, `setDecayCurve(float)`, `setReleaseCurve(float)` -- curve amount (-1 to +1)
- `gate(true)` / `gate(false)` -- note-on / note-off
- `process()` -- returns envelope level per sample
- `getOutput()`, `getStage()` -- for UI playback dot

The `setRetriggerMode(RetriggerMode::Hard)` option matches FR-012 (hard retrigger on note-on).

No API changes or extensions needed.

## R4: ADSRDisplay UI Component Integration

### Decision: Reuse `Krate::Plugins::ADSRDisplay` with parameter wiring

**Rationale**: The shared component at `plugins/shared/src/ui/adsr_display.h` provides:
- Draggable control points for Attack, Decay, Sustain, Release
- Draggable curve segments wired to curve amount parameters
- `setAdsrBaseParamId()` + `setCurveBaseParamId()` for consecutive parameter ID wiring
- `setPlaybackStatePointers()` for playback dot animation
- Already registered as a VSTGUI custom view via `ADSRDisplayCreator`

The controller's `createCustomView()` will instantiate and wire this view. No bezier mode is needed (use simple curve amounts only).

## R5: MemorySlot Extension Strategy

### Decision: Add 9 ADSR fields directly to `Krate::DSP::MemorySlot` struct

**Rationale**: The struct is minimal (just `HarmonicSnapshot snapshot` + `bool occupied`). Adding 9 floats is clean and follows the existing flat-struct pattern. No inheritance or wrapper needed.

**Fields to add**:
```cpp
float adsrAttackMs = 10.0f;
float adsrDecayMs = 100.0f;
float adsrSustainLevel = 1.0f;
float adsrReleaseMs = 100.0f;
float adsrAmount = 0.0f;
float adsrTimeScale = 1.0f;
float adsrAttackCurve = 0.0f;
float adsrDecayCurve = 0.0f;
float adsrReleaseCurve = 0.0f;
```

**Impact assessment**: `MemorySlot` is used by:
- `Innexus::Processor::memorySlots_` (array of 8)
- `Innexus::EvolutionEngine::getInterpolatedFrame()` (reads slots)
- `Innexus::HarmonicBlender::blend()` (reads slots)
- State serialization (processor_state.cpp)

All consumers receive the struct by reference or const reference, so adding fields has no ABI impact within the same build. Default values ensure backward compatibility.

## R6: Morph/Evolution Interpolation Strategy

### Decision: Geometric mean for time params, linear for levels/curves

**Rationale**: Per spec clarification, logarithmic (geometric mean) interpolation for Attack, Decay, and Release times produces perceptually uniform transitions (e.g., midpoint of 10ms and 500ms is ~71ms, not 255ms). Linear interpolation for Sustain level, Envelope Amount, and curve amounts is appropriate since they are ratios, not times.

**Implementation**:
```cpp
// Geometric mean interpolation for time parameters
float interpolatedTime = std::sqrt(timeA * timeB);
// Or equivalently for arbitrary t: exp(lerp(log(a), log(b), t))
float interpolatedTime = std::exp((1.0f - t) * std::log(timeA) + t * std::log(timeB));

// Linear interpolation for levels/amounts
float interpolatedLevel = (1.0f - t) * levelA + t * levelB;
```

**Where applied**:
1. `EvolutionEngine::getInterpolatedFrame()` -- extend to output ADSR params alongside harmonic data
2. `Processor` morph position handling -- interpolate between recalled slots
3. `HarmonicBlender::blend()` -- possibly extend for multi-source blend (lower priority)

## R7: State Serialization v9

### Decision: Increment to version 9, add ADSR block after version 8 data

**Rationale**: Current version is 8 (analysis feedback loop). The deserialization code already uses `if (version >= N)` blocks for backward compatibility. Version 9 adds 9 global ADSR floats plus 9 per-slot ADSR floats (8 slots * 9 = 72 floats).

**Backward compatibility**:
- v1-v8 states: `version < 9` branch skips ADSR reads, defaults apply (Amount=0.0, curves=0.0)
- Default Amount=0.0 means bit-exact bypass (FR-009, SC-003)

## R8: Parameter ID Allocation

### Decision: Use IDs 720-728 in the "ADSR Envelope" block

**Rationale**: The current highest parameter ID block is 710-711 (Analysis Feedback). Using 720 starts a clean block with room for future expansion.

**Parameter IDs**:
```
kAdsrAttackId = 720,       // 1-5000ms, default 10ms
kAdsrDecayId = 721,        // 1-5000ms, default 100ms
kAdsrSustainId = 722,      // 0.0-1.0, default 1.0
kAdsrReleaseId = 723,      // 1-5000ms, default 100ms
kAdsrAmountId = 724,       // 0.0-1.0, default 0.0
kAdsrTimeScaleId = 725,    // 0.25-4.0, default 1.0
kAdsrAttackCurveId = 726,  // -1.0 to +1.0, default 0.0
kAdsrDecayCurveId = 727,   // -1.0 to +1.0, default 0.0
kAdsrReleaseCurveId = 728, // -1.0 to +1.0, default 0.0
```

Note: These are distinct from the existing `kReleaseTimeId = 200` which controls oscillator release fade time.

## R9: Envelope Amount Blending for Bit-Exact Bypass

### Decision: Conditional multiply -- skip ADSR processing entirely when Amount == 0.0

**Rationale**: FR-009 requires bit-exact bypass at Amount=0.0. The simplest approach: check the smoothed Amount value, and if it's exactly 0.0 (after smoothing settles), skip the ADSR multiply entirely. During transitions from/to 0.0, use `gain = lerp(1.0, adsrOutput, smoothedAmount)` which produces 1.0 when Amount=0.0.

**Implementation**:
```cpp
float adsrGain = 1.0f;
if (smoothedAmount > 0.0f) {
    float rawGain = adsr_.process();
    adsrGain = 1.0f + smoothedAmount * (rawGain - 1.0f);  // lerp(1.0, rawGain, amount)
}
// Apply: output *= adsrGain;
```

This ensures smooth transitions (FR-023) and exact bypass (FR-009).

## R10: ADSRDisplay Parameter Wiring via setAdsrBaseParamId

### Decision: Use consecutive parameter IDs so `setAdsrBaseParamId(kAdsrAttackId)` auto-wires Attack, Decay, Sustain, Release

**Rationale**: `ADSRDisplay::setAdsrBaseParamId(baseId)` assigns:
- `attackParamId_ = baseId` (720)
- `decayParamId_ = baseId + 1` (721)
- `sustainParamId_ = baseId + 2` (722)
- `releaseParamId_ = baseId + 3` (723)

Similarly `setCurveBaseParamId(kAdsrAttackCurveId)` assigns:
- `attackCurveParamId_ = baseId` (726)
- `decayCurveParamId_ = baseId + 1` (727)
- `releaseCurveParamId_ = baseId + 2` (728)

This requires Attack/Decay/Sustain/Release IDs to be consecutive (720-723) and curve IDs consecutive (726-728). The chosen ID scheme satisfies this.
