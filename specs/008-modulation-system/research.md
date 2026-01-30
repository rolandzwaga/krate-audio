# Research: Modulation System

**Feature**: 008-modulation-system
**Date**: 2026-01-29

---

## 1. Existing ModulationMatrix Reuse Analysis

### Decision: Compose with existing ModulationMatrix, do not subclass or replace

**Rationale**: The existing `ModulationMatrix` class at `dsp/include/krate/dsp/systems/modulation_matrix.h` already provides:
- `ModulationSource` abstract interface (`getCurrentValue()`, `getSourceRange()`)
- Source/destination registration (16 sources, 16 destinations)
- Route creation and management (up to 32 routes)
- Per-route depth smoothing via `OnePoleSmoother` (20ms)
- Multi-source summation to same destination with clamping
- Bipolar/Unipolar mode per route
- NaN handling (FR-018 in the matrix spec)
- Real-time safe: noexcept, no allocations in `process()`

The new `ModulationEngine` will compose the `ModulationMatrix` internally and add:
1. **ModCurve application** -- The matrix applies depth but not response curves. The engine post-processes matrix output with curve shaping.
2. **ModSource enum dispatch** -- Maps the 13-value `ModSource` enum to concrete source instances.
3. **Source ownership** -- Owns and manages lifecycle of all modulation source objects.
4. **Block-level source processing** -- Calls each source's `process()` before the matrix routes values.
5. **Bipolar amount handling** -- Matrix depth is [0,1]; engine applies sign from routing amount.
6. **Macro Min/Max/Curve** -- Pre-processes macro values before they enter the routing.

**Alternatives Considered**:
- Subclass ModulationMatrix: Rejected because ModulationMatrix is a concrete class not designed for inheritance (no virtual methods on process path).
- Replace ModulationMatrix entirely: Rejected because it would duplicate ~400 LOC of tested routing infrastructure.
- Use ModulationMatrix directly without wrapping: Rejected because it lacks curve application and ModSource dispatch.

### Adaptation Strategy

The `ModulationEngine` will hold a `ModulationMatrix` member and wrap its API:

```
ModulationEngine::process(blockContext, numSamples)
  1. Update source parameters from BlockContext (tempo for LFO sync)
  2. Process each source for the block (update internal state)
  3. Call matrix_.process(numSamples) (routes + smoothes + sums)
  4. For each destination, apply modulation curves post-routing
```

**Key insight**: The existing `ModulationMatrix::process()` gets source values via `getCurrentValue()` on the `ModulationSource*` pointers. So all new sources just implement this interface and the matrix handles routing. The engine adds curve application as a post-processing step on the matrix's raw modulation sums.

However, there is a design tension: the matrix applies depth (amount magnitude) during routing, but curves need to be applied to the source value BEFORE scaling by amount. The spec says: "Curve is applied to absolute value of modulation source output, then multiplied by the routing amount." This means the curve must be applied before the matrix's depth multiplication, not after.

**Resolution**: Instead of using the matrix's built-in depth handling, the new sources will internally apply curves before exposing their value via `getCurrentValue()`. Each source adapter wraps the raw source and applies the routing's curve. Alternatively, the ModulationEngine can bypass the matrix's built-in route processing and implement its own routing loop that applies curves correctly. Given the spec's curve-before-depth requirement, the engine will implement custom routing logic that:
1. Gets raw source value
2. Applies `abs()` and curve
3. Multiplies by signed amount
4. Sums to destination

This means the ModulationEngine will NOT delegate to `ModulationMatrix::process()` for the curve-aware routing. Instead, it will use its own loop that correctly orders curve application. The `ModulationMatrix` infrastructure (smoothers, sums, source registration) can still be referenced as a pattern, but the engine needs its own implementation to get the curve ordering right.

**Final Decision**: Build ModulationEngine with its own routing loop (borrowing patterns from ModulationMatrix) to ensure correct curve application order. Reuse `ModulationSource` interface for all sources.

---

## 2. ChaosModSource vs ChaosWaveshaper

### Decision: Create separate ChaosModSource class; do NOT reuse ChaosWaveshaper

**Rationale**: The existing `ChaosWaveshaper` at Layer 1 tightly couples chaos attractor state with audio waveshaping. Its `process()` method:
1. Updates attractor state (at control rate, every 32 samples)
2. Normalizes X to `[-1, 1]` via `state_.x / normalizationFactor_`
3. Uses normalized X to modulate waveshaping drive
4. Applies `tanh(input * drive)` to audio signal
5. Mixes dry/wet

For modulation, we only need step 1 (attractor update) and step 2 (normalized output). We do NOT want waveshaping applied to any signal. The spec also requires `tanh(x / scale)` soft-limiting for normalization (FR-034), which differs from the waveshaper's `clamp(x / normFactor, -1, 1)`.

**Implementation**: Extract the attractor update logic (Lorenz, Rossler, Chua, Henon equations) into `ChaosModSource`. Reference `ChaosWaveshaper::updateLorenz()` etc. for the equations, but implement independently. Key differences:
- Normalization: `tanh(x / scale)` instead of linear clamp
- No waveshaping pipeline
- No oversampling
- Speed range: 0.05-20.0 (spec) vs 0.01-100.0 (waveshaper)
- Coupling: Uses audio envelope to perturb state (same concept, but ChaosModSource receives the audio level as a float parameter, not the audio buffer directly)

**Alternatives Considered**:
- Reuse ChaosWaveshaper with chaosAmount=0: Still processes audio through the pipeline unnecessarily. Does not output raw attractor value.
- Subclass ChaosWaveshaper: Not designed for inheritance. Would expose unnecessary waveshaping API.
- Extract shared attractor base class: Over-engineering for 2 consumers. The equations are ~20 lines each; duplication is acceptable.

---

## 3. Modulation Curve Implementation

### Decision: Pure functions at Layer 0 in `modulation_curves.h`

**Rationale**: The 4 curves (Linear, Exponential, S-Curve, Stepped) are stateless pure math functions with no dependencies:

```cpp
float applyModCurve(ModCurve curve, float x) noexcept {
    // x is assumed to be in [0, 1] (absolute value of source output)
    switch (curve) {
        case ModCurve::Linear:      return x;
        case ModCurve::Exponential: return x * x;
        case ModCurve::SCurve:      return x * x * (3.0f - 2.0f * x);
        case ModCurve::Stepped:     return std::floor(x * 4.0f) / 3.0f;
    }
    return x;
}
```

**Bipolar handling**: Per spec FR-059, curves are applied to `abs(sourceValue)`, then sign from amount is applied:
```cpp
float curvedValue = applyModCurve(routing.curve, std::abs(sourceValue));
float output = curvedValue * routing.amount;  // amount carries the sign
```

This ensures symmetrical bipolar behavior: negative source values produce the same curve shape as positive, with the sign determined by the routing amount.

**Stepped curve verification**: `floor(x * 4) / 3` produces:
- x in [0, 0.25): floor(0..0.99) / 3 = 0/3 = 0.0
- x in [0.25, 0.5): floor(1..1.99) / 3 = 1/3 = 0.333
- x in [0.5, 0.75): floor(2..2.99) / 3 = 2/3 = 0.667
- x in [0.75, 1.0]: floor(3..4) / 3 = 3/3 = 1.0

Confirmed: exactly 4 discrete levels.

---

## 4. Modulation Source Layering

### Decision: New sources at Layer 2 (processors), implementing existing ModulationSource interface

**Rationale**:
- `ModulationSource` interface is defined at Layer 3 in `modulation_matrix.h`. However, implementing an interface defined at a higher layer from a lower layer is fine in C++ -- the implementation just needs to include the header. Actually, this creates a dependency issue: Layer 2 cannot depend on Layer 3.

**Resolution**: The `ModulationSource` interface needs to be at Layer 0 or Layer 1 (below the implementing classes). Since it is currently at Layer 3 (in `modulation_matrix.h`), the plan must account for this.

**Options**:
1. Move `ModulationSource` interface to Layer 0 `dsp/core/modulation_source.h`
2. Keep sources at Layer 3 alongside ModulationEngine
3. Define a separate interface at Layer 1 that Layer 3 ModulationSource adapts

**Decision**: Option 1 is cleanest. Extract the `ModulationSource` abstract class to Layer 0 `dsp/core/modulation_source.h` (it has no dependencies -- only stdlib types). Update `modulation_matrix.h` to include from Layer 0 instead of defining it locally. This is a non-breaking refactor since the class stays in `Krate::DSP` namespace.

Wait -- actually, `ModulationSource` uses `std::pair` and `virtual`, which are stdlib. But virtual functions are discouraged in tight audio loops per Principle IV. The existing ModulationMatrix already uses it this way, so the pattern is established. Layer 0 can contain interfaces if they have no dependencies beyond stdlib.

**Revised decision**: Keep new source classes at Layer 2 (`processors/`) to match their dependency requirements (they use Layer 1 primitives like LFO, Smoother, PitchDetector). Extract `ModulationSource` interface to Layer 0. This is the correct layering.

---

## 5. Transient Detector Algorithm

### Decision: Implement envelope derivative analysis per spec

**Rationale**: The spec (FR-049) defines:
- Compute amplitude envelope of input signal
- Compute rate-of-change (derivative) of envelope
- Transient detected when BOTH: amplitude > amplitudeThreshold AND delta > rateThreshold
- Sensitivity parameter adjusts both thresholds (FR-050):
  - `ampThresh = 0.5 * (1 - sensitivity)`
  - `rateThresh = 0.1 * (1 - sensitivity)`
- Output is attack-decay envelope triggered by detection (FR-051, FR-052, FR-053)

**Implementation approach**:
1. Compute running amplitude envelope using simple one-pole follower (fast attack ~1ms for derivative accuracy)
2. Compute delta as `currentEnvelope - previousEnvelope`
3. When both thresholds exceeded: start/restart attack ramp
4. Attack: linear ramp from current level to 1.0 over attack time (0.5-10ms)
5. Decay: exponential fall from current level over decay time (20-200ms)
6. Retrigger: from current level, not zero (FR-053)

**State machine**:
```
enum State { Idle, Attack, Decay };
- Idle: output = 0, waiting for transient
- Attack: linear ramp toward 1.0, check for new transient (restart from current)
- Decay: exponential fall, check for new transient (transition to Attack from current)
```

---

## 6. Sample & Hold Source Design

### Decision: Internal timer-based sampling with selectable input

**Rationale**: Per spec FR-037, the S&H has 4 input sources:
- **Random**: Generate new random value from Xorshift32
- **LFO 1**: Sample current output of LFO 1 (requires pointer to LFO 1)
- **LFO 2**: Sample current output of LFO 2 (requires pointer to LFO 2)
- **External**: Sample input audio amplitude (requires envelope value)

**Timer mechanism**: Increment phase counter at sample rate; when phase >= 1.0, sample the selected input:
```
phase += rate / sampleRate;
if (phase >= 1.0) {
    phase -= 1.0;
    heldValue = sampleCurrentInput();
}
```

**Output smoothing**: OnePoleSmoother with configurable slew (0-500ms). When slew=0ms, output is stepped (immediate transitions). When slew>0, transitions are exponentially smoothed.

**LFO access**: SampleHoldSource needs pointers to LFO 1 and LFO 2 instances. These will be set during ModulationEngine initialization (the engine owns all sources).

---

## 7. Pitch Follower Logarithmic Mapping

### Decision: Use semitone-based logarithmic mapping per spec

**Formula** (from dsp-details.md Section 10.3):
```cpp
float midiNote = 69.0f + 12.0f * std::log2(freq / 440.0f);
float minMidi = 69.0f + 12.0f * std::log2(minHz / 440.0f);
float maxMidi = 69.0f + 12.0f * std::log2(maxHz / 440.0f);
float modValue = (midiNote - minMidi) / (maxMidi - minMidi);
modValue = std::clamp(modValue, 0.0f, 1.0f);
```

**Confidence handling**: When PitchDetector confidence < threshold, hold last valid modulation value. This prevents erratic behavior on noise or silence.

**Tracking speed**: OnePoleSmoother applied to the output modulation value (10-300ms configurable).

---

## 8. Existing ModulationMatrix Destination Limit

### Decision: ModulationMatrix supports 16 destinations; spec needs up to ~60+ (all modulatable params)

**Problem**: The existing `ModulationMatrix` has `kMaxModulationDestinations = 16`. The spec lists many more modulatable destinations (6 global + 8 bands * 6 per-band = 54 destinations per FR-063).

**Resolution**: The ModulationEngine will NOT use the ModulationMatrix's destination registration system directly for all parameters. Instead, the engine's routing output is an array of modulation offsets indexed by VST parameter ID. The engine applies modulation as a post-processing step that maps routing results to parameter offsets. The existing ModulationMatrix's 16-destination limit applies to Iterum's simpler modulation needs; for Disrumpo, the engine implements its own destination handling.

This reinforces the decision from Research item 1: ModulationEngine implements its own routing loop rather than delegating to ModulationMatrix.process(). The engine:
1. Iterates 32 routing slots
2. For each active routing: gets source value, applies abs+curve, multiplies by amount
3. Accumulates per-destination offset in a map/array indexed by destination param ID
4. Clamps each destination's total offset to [-1, +1]
5. Applies offset to base normalized parameter value, clamped to [0, 1]

---

## 9. Performance Budget Analysis

### Decision: Per-block processing, not per-sample for most sources

**Analysis**: SC-011 requires <1% CPU for 32 routings with all 12 sources at 44.1kHz/512 samples.

**Per-source cost estimates**:
- LFO: ~5 ops/sample (wavetable read + interpolation) = 2 LFOs * 512 = ~5K ops
- EnvelopeFollower: ~10 ops/sample (rectify + asymmetric smooth) = 512 * 10 = ~5K ops
- Random: ~5 ops/trigger + smoother = ~100 ops/block (rate << sample rate)
- Macros: 4 * ~5 ops = 20 ops/block (parameter reads)
- Chaos: ~30 ops/update * (512/32 control-rate updates) = ~480 ops/block
- Sample & Hold: ~5 ops/sample (timer + smoother) = ~2.5K ops
- Pitch Follower: PitchDetector does autocorrelation ~every 64 samples, ~256*256 ops/detection = heavy but amortized
- Transient Detector: ~10 ops/sample = ~5K ops

**Total routing processing**: 32 routings * ~10 ops each = 320 ops/block

**Total estimate**: ~20K-30K ops/block. At 44.1kHz/512 samples, one buffer = ~11.6ms. Modern CPU does ~10B ops/s. 30K ops = 3us, well under 0.03% CPU. Performance budget easily met.

**Decision**: Process LFOs and EnvelopeFollower per-sample (they need it for accuracy). Process Chaos at control rate (every 32 samples, already established in ChaosWaveshaper). Process Random, S&H at their respective rates. Process routing per-block (once per block, using end-of-block source values).

---

## 10. EnvelopeFollower Source Selector

### Decision: Pre-mix input channels in ModulationEngine before feeding EnvelopeFollower

**Rationale**: FR-020a requires the envelope follower to support 5 input source modes: Input L, Input R, Input Sum (L+R), Mid (L+R)/2, Side (L-R)/2.

The existing `EnvelopeFollower` processes a single float stream. The source selection is handled by the `ModulationEngine` during block processing:

```cpp
// In ModulationEngine::process(), before envelope follower:
float envInput = 0.0f;
switch (envFollowerSource_) {
    case EnvSource::InputL:  envInput = inputL[i]; break;
    case EnvSource::InputR:  envInput = inputR[i]; break;
    case EnvSource::Sum:     envInput = inputL[i] + inputR[i]; break;
    case EnvSource::Mid:     envInput = (inputL[i] + inputR[i]) * 0.5f; break;
    case EnvSource::Side:    envInput = (inputL[i] - inputR[i]) * 0.5f; break;
}
envFollower_.processSample(envInput);
```

This keeps the EnvelopeFollower class unchanged (Layer 2) and handles channel mixing at the integration level (ModulationEngine).

---

## 11. Lock-Free Modulation Visualization Buffer

### Decision: Follow SweepPositionBuffer pattern for modulation activity display

**Rationale**: The SweepPositionBuffer from 007-sweep-system provides a proven lock-free SPSC pattern for audio-to-UI data. For modulation visualization, a similar buffer can carry:
- Per-source current values (12 floats)
- Per-destination total modulation offset (for active routing indicators)
- Sample position for sync

**Deferred**: This is a UI visualization concern, not core modulation DSP. The data structures for visualization can be defined during the UI implementation phase (T9.22-T9.24). The core engine design does not need to account for this in Phase 1.

---

## Summary of Open Items

None. All NEEDS CLARIFICATION items from the Technical Context have been resolved through this research:

1. Existing ModulationMatrix reuse strategy -- resolved: compose but implement own routing loop
2. ChaosModSource vs ChaosWaveshaper -- resolved: separate class extracting attractor math
3. Modulation curve implementation -- resolved: pure functions at Layer 0
4. Source layering -- resolved: Layer 2 with interface extracted to Layer 0
5. Transient detector algorithm -- resolved: envelope derivative with attack/decay state machine
6. Sample & Hold design -- resolved: timer-based with pointer to LFO instances
7. Pitch follower mapping -- resolved: semitone-based log mapping
8. Destination limit -- resolved: engine implements own routing with param ID indexing
9. Performance budget -- resolved: easily within 1% CPU budget
10. EnvelopeFollower source selector -- resolved: pre-mix in engine
11. Modulation visualization -- resolved: deferred to UI phase
