# Data Model: Harmonic Physics (122)

**Date**: 2026-03-06 | **Spec**: spec.md

## Existing Entities (Reused)

### Krate::DSP::Partial
**Location**: `dsp/include/krate/dsp/processors/harmonic_types.h:35-44`

```cpp
struct Partial {
    int harmonicIndex = 0;           // 1-based harmonic number
    float frequency = 0.0f;          // Hz
    float amplitude = 0.0f;          // Linear amplitude (PRIMARY field modified by physics)
    float phase = 0.0f;              // Radians [-pi, pi]
    float relativeFrequency = 0.0f;  // frequency / F0
    float inharmonicDeviation = 0.0f;
    float stability = 0.0f;          // Tracking confidence [0, 1]
    int age = 0;                     // Frames since track birth
};
```

**Physics interaction**: Only `amplitude` is modified. All other fields pass through unchanged (FR-010).

### Krate::DSP::HarmonicFrame
**Location**: `dsp/include/krate/dsp/processors/harmonic_types.h:51-60`

```cpp
struct HarmonicFrame {
    float f0 = 0.0f;
    float f0Confidence = 0.0f;
    std::array<Partial, kMaxPartials> partials{};  // kMaxPartials = 48
    int numPartials = 0;                           // Active count [0, 48]
    float spectralCentroid = 0.0f;
    float brightness = 0.0f;
    float noisiness = 0.0f;
    float globalAmplitude = 0.0f;   // Energy budget source for dynamics
};
```

**Physics interaction**: `morphedFrame_` is modified in-place. `globalAmplitude` is read by dynamics for energy budgeting.

### Krate::DSP::OnePoleSmoother
**Location**: `dsp/include/krate/dsp/primitives/smoother.h:133-292`

Key API used:
- `configure(smoothTimeMs, sampleRate)` -- set smoothing time
- `setTarget(value)` -- set target (NaN/Inf safe)
- `getCurrentValue()` -- read current smoothed value
- `advanceSamples(numSamples)` -- O(1) block advancement
- `snapTo(value)` -- immediate set (for state restore)
- `reset()` -- zero both current and target

Default smoothing time: `kDefaultSmoothingTimeMs = 5.0f`

## New Entities

### Innexus::AgentState
**Location**: Defined within `HarmonicPhysics` class (not a standalone struct)

```cpp
// Per-partial state for the dynamics agent system (A3)
// Stored as parallel arrays for cache efficiency
struct AgentState {
    std::array<float, Krate::DSP::kMaxPartials> amplitude{};    // Smoothed output amplitude
    std::array<float, Krate::DSP::kMaxPartials> velocity{};     // Rate of change tracking
    std::array<float, Krate::DSP::kMaxPartials> persistence{};  // Stability-over-time [0, 1]
    std::array<float, Krate::DSP::kMaxPartials> energyShare{};  // Energy budget allocation
};
```

**Size**: 48 * 4 * 4 bytes = 768 bytes total. Fits in L1 cache.

**Lifecycle**: Initialized to zero on construction. Reset via `reset()`. First frame after reset copies input amplitudes to agent amplitudes (no ramp-from-zero).

### Innexus::HarmonicPhysics
**Location**: `plugins/innexus/src/dsp/harmonic_physics.h`
**Namespace**: `Innexus`

```cpp
class HarmonicPhysics {
public:
    void prepare(double sampleRate, int hopSize) noexcept;
    void reset() noexcept;
    void processFrame(Krate::DSP::HarmonicFrame& frame) noexcept;

    // Parameter setters (called from processParameterChanges)
    void setWarmth(float warmth) noexcept;     // [0, 1]
    void setCoupling(float coupling) noexcept; // [0, 1]
    void setStability(float stability) noexcept; // [0, 1]
    void setEntropy(float entropy) noexcept;   // [0, 1]

private:
    // Stateless transforms
    void applyWarmth(Krate::DSP::HarmonicFrame& frame) noexcept;
    void applyCoupling(Krate::DSP::HarmonicFrame& frame) noexcept;

    // Stateful transform
    void applyDynamics(Krate::DSP::HarmonicFrame& frame) noexcept;

    // Parameters (set atomically from processor, read in processFrame)
    float warmth_ = 0.0f;
    float coupling_ = 0.0f;
    float stability_ = 0.0f;
    float entropy_ = 0.0f;

    // Dynamics agent state
    AgentState agents_;
    bool firstFrame_ = true;

    // Timing constants (computed in prepare())
    float persistenceGrowthRate_ = 0.05f;  // 1/20
    float persistenceDecayFactor_ = 0.5f;
    float persistenceThreshold_ = 0.01f;
};
```

## New Parameters

| Name | ID | Range | Default | Type | Display |
|------|----|-------|---------|------|---------|
| Warmth | `kWarmthId` (700) | 0.0-1.0 | 0.0 | `RangeParameter` | "%" |
| Coupling | `kCouplingId` (701) | 0.0-1.0 | 0.0 | `RangeParameter` | "%" |
| Stability | `kStabilityId` (702) | 0.0-1.0 | 0.0 | `RangeParameter` | "%" |
| Entropy | `kEntropyId` (703) | 0.0-1.0 | 0.0 | `RangeParameter` | "%" |

All parameters are:
- Normalized 0-1 at VST boundary (no denormalization needed)
- Defaulting to 0.0 (full bypass)
- Smoothed via `OnePoleSmoother`
- Saved/restored in processor state

## State Persistence

### State Version
Current version: **6** (M6: creative extensions)
New version: **7** (M7: harmonic physics)

### Serialization Order (appended to v6 state)
```
// v7 additions
streamer.writeFloat(warmth_)      // 0.0-1.0
streamer.writeFloat(coupling_)    // 0.0-1.0
streamer.writeFloat(stability_)   // 0.0-1.0
streamer.writeFloat(entropy_)     // 0.0-1.0
```

### Backward Compatibility
- v6 states loaded into v7 code: 4 new params default to 0.0 (bypass)
- v7 states loaded into v6 code: extra bytes ignored by old code (standard VST3 pattern)

## Relationships

```
Processor
  |-- HarmonicPhysics (new, instantiated as member)
  |     |-- AgentState (internal, dynamics state)
  |     |-- warmth_, coupling_, stability_, entropy_ (parameters)
  |
  |-- OnePoleSmoother x4 (new, for parameter smoothing)
  |     |-- warmthSmoother_
  |     |-- couplingSmoother_
  |     |-- stabilitySmoother_
  |     |-- entropySmoother_
  |
  |-- morphedFrame_ (existing, modified in-place by physics)
  |-- oscillatorBank_ (existing, receives processed frame)
```

## Processing Flow

```
morphedFrame_ (after morph/blend/filter/modulator)
     |
     v
 [Coupling] -- neighbor energy sharing, energy-conserving normalization
     |
     v
 [Warmth] -- tanh soft saturation per partial
     |
     v
 [Dynamics] -- inertia (stability), decay (entropy), energy budget
     |
     v
 oscillatorBank_.loadFrame(morphedFrame_, targetPitch)
```

Each step modifies `morphedFrame_.partials[i].amplitude` in-place. Only `amplitude` is touched; frequency, phase, and all other fields pass through unchanged.
