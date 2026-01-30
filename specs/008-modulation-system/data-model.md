# Data Model: Modulation System

**Feature**: 008-modulation-system
**Date**: 2026-01-29

---

## Entity Definitions

### 1. ModSource (Layer 0 - Enum)

**Location**: `dsp/include/krate/dsp/core/modulation_types.h`

```cpp
enum class ModSource : uint8_t {
    None = 0,
    LFO1 = 1,
    LFO2 = 2,
    EnvFollower = 3,
    Random = 4,
    Macro1 = 5,
    Macro2 = 6,
    Macro3 = 7,
    Macro4 = 8,
    Chaos = 9,
    SampleHold = 10,
    PitchFollower = 11,
    Transient = 12
};
inline constexpr uint8_t kModSourceCount = 13;
```

**Validation**: Values 0-12 valid. Invalid values treated as None.

---

### 2. ModCurve (Layer 0 - Enum)

**Location**: `dsp/include/krate/dsp/core/modulation_types.h`

```cpp
enum class ModCurve : uint8_t {
    Linear = 0,
    Exponential = 1,
    SCurve = 2,
    Stepped = 3
};
inline constexpr uint8_t kModCurveCount = 4;
```

**Formulas** (applied to absolute value x in [0, 1]):
| Curve | Formula | Output at x=0.5 |
|-------|---------|-----------------|
| Linear | `y = x` | 0.5 |
| Exponential | `y = x * x` | 0.25 |
| SCurve | `y = x * x * (3 - 2 * x)` | 0.5 |
| Stepped | `y = floor(x * 4) / 3` | 0.667 |

---

### 3. ModRouting (Layer 0 - Struct)

**Location**: `dsp/include/krate/dsp/core/modulation_types.h`

```cpp
struct ModRouting {
    ModSource source = ModSource::None;    // Which source
    uint32_t destParamId = 0;              // Destination VST parameter ID
    float amount = 0.0f;                   // Bipolar amount [-1.0, +1.0]
    ModCurve curve = ModCurve::Linear;     // Response curve
    bool active = false;                   // Whether this slot is in use
};
inline constexpr size_t kMaxModRoutings = 32;
```

**Validation Rules**:
- `amount` clamped to [-1.0, +1.0]
- `source` must be valid ModSource (0-12)
- `destParamId` must be a registered modulatable parameter ID
- `curve` must be valid ModCurve (0-3)

**Processing Formula**:
```
rawSource = source.getCurrentValue()            // [-1, +1] or [0, +1]
absSource = abs(rawSource)                      // [0, +1]
curvedSource = applyModCurve(curve, absSource)  // [0, +1] shaped
output = curvedSource * amount                  // amount carries sign
```

---

### 4. MacroConfig (Layer 0 - Struct)

**Location**: `dsp/include/krate/dsp/core/modulation_types.h`

```cpp
struct MacroConfig {
    float value = 0.0f;                    // Current knob position [0, 1]
    float minOutput = 0.0f;                // Minimum output range [0, 1]
    float maxOutput = 1.0f;                // Maximum output range [0, 1]
    ModCurve curve = ModCurve::Linear;     // Response curve
};
inline constexpr size_t kMaxMacros = 4;
```

**Processing Formula**:
```
mapped = min + value * (max - min)    // FR-028: Min/Max mapping FIRST
output = applyModCurve(curve, mapped) // FR-029: Curve applied AFTER mapping
```

**Output Range**: [0, +1] (unipolar) per FR-029a.

---

### 5. EnvFollowerSourceType (Layer 0 - Enum)

**Location**: `dsp/include/krate/dsp/core/modulation_types.h`

```cpp
enum class EnvFollowerSourceType : uint8_t {
    InputL = 0,     // Left channel
    InputR = 1,     // Right channel
    InputSum = 2,   // L + R (default)
    Mid = 3,        // (L + R) / 2
    Side = 4        // (L - R) / 2
};
```

---

### 6. SampleHoldInputType (Layer 0 - Enum)

**Location**: `dsp/include/krate/dsp/core/modulation_types.h`

```cpp
enum class SampleHoldInputType : uint8_t {
    Random = 0,     // White noise [-1, +1]
    LFO1 = 1,       // Current LFO 1 output
    LFO2 = 2,       // Current LFO 2 output
    External = 3    // Input audio amplitude [0, +1]
};
```

---

### 7. ChaosModSource (Layer 2 - Class)

**Location**: `dsp/include/krate/dsp/processors/chaos_mod_source.h`
**Implements**: `Krate::DSP::ModulationSource` (from Layer 0)

**Fields**:
| Field | Type | Default | Description |
|-------|------|---------|-------------|
| model_ | ChaosModel | Lorenz | Attractor model (enum from chaos_waveshaper.h) |
| speed_ | float | 1.0 | Integration speed [0.05, 20.0] |
| coupling_ | float | 0.0 | Audio coupling [0.0, 1.0] |
| state_ | {x, y, z} | model-specific | Attractor state variables |
| normalizedOutput_ | float | 0.0 | Current output [-1, +1] |
| sampleRate_ | double | 44100.0 | Sample rate |
| samplesUntilUpdate_ | int | 32 | Control-rate counter |

**Key Methods**:
- `prepare(double sampleRate)` -- Initialize attractor state
- `process()` -- Update attractor at control rate, output normalized X
- `setModel(ChaosModel)` -- Switch attractor model (resets state)
- `setSpeed(float)` -- Set integration speed multiplier
- `setCoupling(float)` -- Set audio coupling amount
- `setInputLevel(float)` -- Set current audio input level for coupling
- `getCurrentValue() const noexcept` -- Return normalized output
- `getSourceRange() const noexcept` -- Return {-1.0f, 1.0f}

**Normalization**: `output = tanh(state_.x / scale)` where scale = {Lorenz: 20, Rossler: 10, Chua: 2, Henon: 1.5}

**State Transitions**: None (continuous output).

---

### 8. SampleHoldSource (Layer 2 - Class)

**Location**: `dsp/include/krate/dsp/processors/sample_hold_source.h`
**Implements**: `Krate::DSP::ModulationSource`

**Fields**:
| Field | Type | Default | Description |
|-------|------|---------|-------------|
| inputType_ | SampleHoldInputType | Random | Which input to sample |
| rate_ | float | 4.0 | Sampling rate [0.1, 50.0] Hz |
| phase_ | float | 0.0 | Timer phase [0, 1) |
| heldValue_ | float | 0.0 | Current held value |
| outputSmoother_ | OnePoleSmoother | -- | Slew limiter for transitions |
| slewMs_ | float | 0.0 | Slew time [0, 500] ms |
| rng_ | Xorshift32 | seed | Random number generator |
| lfo1Ptr_ | LFO* | nullptr | Pointer to LFO 1 (set by engine) |
| lfo2Ptr_ | LFO* | nullptr | Pointer to LFO 2 (set by engine) |
| externalLevel_ | float | 0.0 | Current external input level |

**Key Methods**:
- `prepare(double sampleRate)`
- `process()` -- Advance timer; sample on trigger; apply slew
- `setInputType(SampleHoldInputType)`
- `setRate(float hz)`
- `setSlewTime(float ms)`
- `setExternalLevel(float level)` -- Called by engine with audio envelope
- `setLFOPointers(LFO* lfo1, LFO* lfo2)` -- Set by engine during init

**Output Range**: [-1, +1] for Random/LFO sources; [0, +1] for External.

---

### 9. PitchFollowerSource (Layer 2 - Class)

**Location**: `dsp/include/krate/dsp/processors/pitch_follower_source.h`
**Implements**: `Krate::DSP::ModulationSource`

**Fields**:
| Field | Type | Default | Description |
|-------|------|---------|-------------|
| detector_ | PitchDetector | -- | Autocorrelation pitch detector |
| minHz_ | float | 80.0 | Min frequency range [20, 500] |
| maxHz_ | float | 2000.0 | Max frequency range [200, 5000] |
| confidenceThreshold_ | float | 0.5 | Min confidence to accept [0, 1] |
| outputSmoother_ | OnePoleSmoother | -- | Tracking speed smoother |
| trackingSpeedMs_ | float | 50.0 | Smoothing time [10, 300] ms |
| lastValidValue_ | float | 0.0 | Held when confidence low |

**Key Methods**:
- `prepare(double sampleRate)`
- `pushSample(float sample)` -- Feed audio to pitch detector
- `process()` -- Map detected frequency to [0, 1] via log mapping
- `setMinHz(float)`, `setMaxHz(float)`, `setConfidenceThreshold(float)`, `setTrackingSpeed(float)`

**Mapping Formula**:
```
midiNote = 69 + 12 * log2(freq / 440)
minMidi = 69 + 12 * log2(minHz / 440)
maxMidi = 69 + 12 * log2(maxHz / 440)
modValue = clamp((midiNote - minMidi) / (maxMidi - minMidi), 0, 1)
```

**Output Range**: [0, +1] (unipolar).

---

### 10. TransientDetector (Layer 2 - Class)

**Location**: `dsp/include/krate/dsp/processors/transient_detector.h`
**Implements**: `Krate::DSP::ModulationSource`

**Fields**:
| Field | Type | Default | Description |
|-------|------|---------|-------------|
| sensitivity_ | float | 0.5 | Detection sensitivity [0, 1] |
| attackMs_ | float | 2.0 | Attack time [0.5, 10] ms |
| decayMs_ | float | 50.0 | Decay time [20, 200] ms |
| envelope_ | float | 0.0 | Current output envelope [0, 1] |
| prevAmplitude_ | float | 0.0 | Previous amplitude for delta calc |
| state_ | enum | Idle | Current state (Idle/Attack/Decay) |
| attackIncrement_ | float | 0.0 | Per-sample attack ramp increment |
| decayCoeff_ | float | 0.0 | Exponential decay coefficient |

**State Machine**:
```
Idle -> Attack:  When both amplitude > ampThresh AND delta > rateThresh
Attack -> Decay: When envelope reaches 1.0
Attack -> Attack: Retrigger from current level (restart ramp toward 1.0)
Decay -> Attack:  Retrigger from current level (transition to attack)
Decay -> Idle:    When envelope < 0.001
```

**Threshold Formulas** (FR-050):
```
ampThresh = 0.5 * (1.0 - sensitivity)
rateThresh = 0.1 * (1.0 - sensitivity)
```

**Output Range**: [0, +1] (unipolar).

---

### 11. RandomSource (Layer 2 - Class)

**Location**: `dsp/include/krate/dsp/processors/random_source.h`
**Implements**: `Krate::DSP::ModulationSource`

**Fields**:
| Field | Type | Default | Description |
|-------|------|---------|-------------|
| rate_ | float | 4.0 | New value rate [0.1, 50.0] Hz |
| smoothness_ | float | 0.0 | Output smoothing [0, 1] |
| phase_ | float | 0.0 | Timer phase [0, 1) |
| currentValue_ | float | 0.0 | Current random target |
| rng_ | Xorshift32 | seed | Random number generator |
| outputSmoother_ | OnePoleSmoother | -- | Smoothness filter |
| tempoSync_ | bool | false | Tempo sync enabled |

**Key Methods**:
- `prepare(double sampleRate)`
- `process()` -- Advance timer; generate on trigger; apply smoothing
- `setRate(float hz)`, `setSmoothness(float)`, `setTempoSync(bool)`, `setTempo(float bpm)`

**Output Range**: [-1, +1] (bipolar).

---

### 12. ModulationEngine (Layer 3 - Class)

**Location**: `dsp/include/krate/dsp/systems/modulation_engine.h`

**Fields**:
| Field | Type | Description |
|-------|------|-------------|
| lfo1_ | LFO | LFO 1 modulation source |
| lfo2_ | LFO | LFO 2 modulation source |
| envFollower_ | EnvelopeFollower | Envelope follower source |
| random_ | RandomSource | Random modulation source |
| macros_ | array<MacroConfig, 4> | Macro parameter configs |
| chaos_ | ChaosModSource | Chaos attractor source |
| sampleHold_ | SampleHoldSource | Sample & Hold source |
| pitchFollower_ | PitchFollowerSource | Pitch follower source |
| transient_ | TransientDetector | Transient detector source |
| routings_ | array<ModRouting, 32> | Routing slots |
| modOffsets_ | array<float, kMaxModDestinations> | Per-destination modulation offsets (array for real-time safety; no allocations) |
| destActive_ | array<bool, kMaxModDestinations> | Tracks which destinations have active modulation |
| amountSmoothers_ | array<OnePoleSmoother, 32> | Per-routing amount smoothing |
| sampleRate_ | double | Current sample rate |

**Key Methods**:
- `prepare(double sampleRate, size_t maxBlockSize)` -- Initialize all sources
- `reset()` -- Reset all sources and routing state
- `process(const BlockContext& ctx, const float* inputL, const float* inputR, size_t numSamples)` -- Process one block
- `getModulationOffset(uint32_t paramId) const` -- Get modulation offset for a parameter
- `getModulatedValue(uint32_t paramId, float baseNormalized) const` -- Get final modulated parameter value
- `setRouting(size_t index, const ModRouting& routing)` -- Set routing slot
- Source parameter setters (delegate to individual sources)

**Processing Order**:
1. Update tempo from BlockContext (LFO sync)
2. Process audio-dependent sources per-sample (EnvFollower, PitchFollower, Transient)
3. Process per-sample sources (LFO1, LFO2)
4. Process control-rate sources (Chaos, Random, S&H)
5. Compute macro outputs
6. For each active routing: get source value, apply abs+curve, multiply by amount, accumulate to destination
7. Clamp each destination offset to [-1, +1]
8. Apply offsets to base parameter values, clamp to [0, 1]

---

## Relationships

```
ModulationEngine (Layer 3)
  |-- owns --> LFO x2 (Layer 1)
  |-- owns --> EnvelopeFollower (Layer 2)
  |-- owns --> RandomSource (Layer 2)
  |-- owns --> ChaosModSource (Layer 2)
  |-- owns --> SampleHoldSource (Layer 2)
  |-- owns --> PitchFollowerSource (Layer 2)
  |-- owns --> TransientDetector (Layer 2)
  |-- owns --> MacroConfig x4 (Layer 0, value type)
  |-- owns --> ModRouting x32 (Layer 0, value type)
  |-- uses --> applyModCurve() (Layer 0)
  |-- uses --> BlockContext (Layer 0)

SampleHoldSource (Layer 2)
  |-- uses --> Xorshift32 (Layer 0)
  |-- uses --> OnePoleSmoother (Layer 1)
  |-- references --> LFO* x2 (set by engine)

PitchFollowerSource (Layer 2)
  |-- owns --> PitchDetector (Layer 1)
  |-- owns --> OnePoleSmoother (Layer 1)

ChaosModSource (Layer 2)
  |-- uses --> ChaosModel enum (from chaos_waveshaper.h, Layer 1)

TransientDetector (Layer 2)
  |-- standalone (no KrateDSP dependencies beyond stdlib)

RandomSource (Layer 2)
  |-- uses --> Xorshift32 (Layer 0)
  |-- uses --> OnePoleSmoother (Layer 1)
```

---

## Parameter ID Allocation

| Range | Assignment | Count |
|-------|-----------|-------|
| 200-206 | LFO 1 (Rate, Shape, Phase, Sync, NoteValue, Unipolar, Retrigger) | 7 |
| 220-226 | LFO 2 (same layout) | 7 |
| 240-243 | Envelope Follower (Attack, Release, Sensitivity, Source) | 4 |
| 260-262 | Random (Rate, Smoothness, Sync) | 3 |
| 280-282 | Chaos (Model, Speed, Coupling) | 3 |
| 285-287 | Sample & Hold (Source, Rate, Slew) | 3 |
| 290-293 | Pitch Follower (MinHz, MaxHz, Confidence, TrackingSpeed) | 4 |
| 295-297 | Transient Detector (Sensitivity, Attack, Decay) | 3 |
| 300-427 | Routing (32 x 4: Source, Dest, Amount, Curve) | 128 |
| 430-445 | Macros (4 x 4: Value, Min, Max, Curve) | 16 |
| **Total** | | **178** |
