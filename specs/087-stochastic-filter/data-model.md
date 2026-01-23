# Data Model: Stochastic Filter

**Feature**: 087-stochastic-filter
**Date**: 2026-01-23
**Layer**: 2 (DSP Processors)

---

## Entities

### 1. RandomMode (Enumeration)

Defines the four random modulation algorithms.

```cpp
enum class RandomMode : uint8_t {
    Walk = 0,   ///< Brownian motion (smooth drift)
    Jump,       ///< Discrete random jumps
    Lorenz,     ///< Chaotic attractor (deterministic chaos)
    Perlin      ///< Coherent noise (smooth random)
};
```

**Relationships**: Used by StochasticFilter to select modulation algorithm.

---

### 2. StochasticFilter (Main Processor Class)

Layer 2 processor composing SVF with stochastic modulation.

```cpp
class StochasticFilter {
public:
    // === Constants ===
    static constexpr float kMinChangeRate = 0.01f;   // Hz
    static constexpr float kMaxChangeRate = 100.0f;  // Hz
    static constexpr float kDefaultChangeRate = 1.0f;

    static constexpr float kMinSmoothing = 0.0f;     // ms
    static constexpr float kMaxSmoothing = 1000.0f;  // ms
    static constexpr float kDefaultSmoothing = 50.0f;

    static constexpr float kMinOctaveRange = 0.0f;
    static constexpr float kMaxOctaveRange = 8.0f;
    static constexpr float kDefaultOctaveRange = 2.0f;

    static constexpr float kMinQRange = 0.0f;
    static constexpr float kMaxQRange = 1.0f;        // Normalized
    static constexpr float kDefaultQRange = 0.5f;

    static constexpr size_t kControlRateInterval = 32;  // samples

    // === Lifecycle ===
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // === Processing ===
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // === Mode Selection ===
    void setMode(RandomMode mode) noexcept;
    [[nodiscard]] RandomMode getMode() const noexcept;

    // === Randomization Enable ===
    void setCutoffRandomEnabled(bool enabled) noexcept;
    void setResonanceRandomEnabled(bool enabled) noexcept;
    void setTypeRandomEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isCutoffRandomEnabled() const noexcept;
    [[nodiscard]] bool isResonanceRandomEnabled() const noexcept;
    [[nodiscard]] bool isTypeRandomEnabled() const noexcept;

    // === Base Parameters ===
    void setBaseCutoff(float hz) noexcept;           // Center frequency
    void setBaseResonance(float q) noexcept;         // Center Q
    void setBaseFilterType(SVFMode type) noexcept;   // Default type
    [[nodiscard]] float getBaseCutoff() const noexcept;
    [[nodiscard]] float getBaseResonance() const noexcept;
    [[nodiscard]] SVFMode getBaseFilterType() const noexcept;

    // === Randomization Ranges ===
    void setCutoffOctaveRange(float octaves) noexcept;  // +/- octaves
    void setResonanceRange(float range) noexcept;       // 0-1 normalized
    void setEnabledFilterTypes(uint8_t typeMask) noexcept;  // Bitmask
    [[nodiscard]] float getCutoffOctaveRange() const noexcept;
    [[nodiscard]] float getResonanceRange() const noexcept;
    [[nodiscard]] uint8_t getEnabledFilterTypes() const noexcept;

    // === Control Parameters ===
    void setChangeRate(float hz) noexcept;           // Modulation speed
    void setSmoothingTime(float ms) noexcept;        // Transition smoothing
    void setSeed(uint32_t seed) noexcept;            // For reproducibility
    [[nodiscard]] float getChangeRate() const noexcept;
    [[nodiscard]] float getSmoothingTime() const noexcept;
    [[nodiscard]] uint32_t getSeed() const noexcept;

private:
    // === Filter Instances (for type crossfade) ===
    SVF filterA_;
    SVF filterB_;

    // === Random Generator ===
    Xorshift32 rng_{1};
    uint32_t seed_ = 1;

    // === Mode State ===
    RandomMode mode_ = RandomMode::Walk;

    // === Walk Mode State ===
    float walkValue_ = 0.0f;

    // === Jump Mode State ===
    float jumpValue_ = 0.0f;
    float samplesUntilNextJump_ = 0.0f;

    // === Lorenz Mode State ===
    float lorenzX_ = 0.1f;
    float lorenzY_ = 0.0f;
    float lorenzZ_ = 25.0f;

    // === Perlin Mode State ===
    float perlinTime_ = 0.0f;
    uint32_t perlinSeed_ = 1;

    // === Parameter Smoothers ===
    OnePoleSmoother cutoffSmoother_;
    OnePoleSmoother resonanceSmoother_;
    OnePoleSmoother crossfadeSmoother_;

    // === Type Transition State ===
    SVFMode currentTypeA_ = SVFMode::Lowpass;
    SVFMode currentTypeB_ = SVFMode::Lowpass;
    bool isTransitioning_ = false;

    // === Configuration ===
    double sampleRate_ = 44100.0;
    float baseCutoffHz_ = 1000.0f;
    float baseResonance_ = 0.707f;
    SVFMode baseFilterType_ = SVFMode::Lowpass;
    float cutoffOctaveRange_ = 2.0f;
    float resonanceRange_ = 0.5f;
    uint8_t enabledTypesMask_ = 0x07;  // LP, HP, BP by default
    float changeRateHz_ = 1.0f;
    float smoothingTimeMs_ = 50.0f;
    bool cutoffRandomEnabled_ = true;
    bool resonanceRandomEnabled_ = false;
    bool typeRandomEnabled_ = false;

    // === Control Rate State ===
    int samplesUntilUpdate_ = 0;

    // === Internal Methods ===
    void updateModulation() noexcept;
    float calculateWalkValue() noexcept;
    float calculateJumpValue() noexcept;
    float calculateLorenzValue() noexcept;
    float calculatePerlinValue() noexcept;
    void initializeLorenzState() noexcept;
    float perlin1D(float t) const noexcept;
    float gradientAt(int i) const noexcept;
    void updateFilterParameters() noexcept;
    SVFMode selectRandomType() noexcept;
};
```

---

### 3. Filter Type Bitmask

For enabling/disabling filter types in random selection.

```cpp
namespace FilterTypeMask {
    constexpr uint8_t Lowpass   = 1 << 0;  // 0x01
    constexpr uint8_t Highpass  = 1 << 1;  // 0x02
    constexpr uint8_t Bandpass  = 1 << 2;  // 0x04
    constexpr uint8_t Notch     = 1 << 3;  // 0x08
    constexpr uint8_t Allpass   = 1 << 4;  // 0x10
    constexpr uint8_t Peak      = 1 << 5;  // 0x20
    constexpr uint8_t LowShelf  = 1 << 6;  // 0x40
    constexpr uint8_t HighShelf = 1 << 7;  // 0x80
    constexpr uint8_t All       = 0xFF;
}
```

---

## State Transitions

### Random Mode State Machine

```
[Any Mode] --setMode(Walk)--> [Walk Mode]
[Any Mode] --setMode(Jump)--> [Jump Mode]
[Any Mode] --setMode(Lorenz)--> [Lorenz Mode]
[Any Mode] --setMode(Perlin)--> [Perlin Mode]
```

Mode changes take effect immediately at next control-rate update.

### Filter Type Transition State Machine

```
[Stable: filterA active] --setFilterType(newType)--> [Transitioning]
    |
    v
[Transitioning: both filters active, crossfading]
    |
    | (crossfade complete)
    v
[Stable: filterA = newType]
```

---

## Validation Rules

### Parameter Constraints

| Parameter | Min | Max | Default | Unit |
|-----------|-----|-----|---------|------|
| changeRateHz | 0.01 | 100.0 | 1.0 | Hz |
| smoothingTimeMs | 0.0 | 1000.0 | 50.0 | ms |
| baseCutoffHz | 1.0 | sampleRate * 0.495 | 1000.0 | Hz |
| baseResonance | 0.1 | 30.0 | 0.707 | Q |
| cutoffOctaveRange | 0.0 | 8.0 | 2.0 | octaves |
| resonanceRange | 0.0 | 1.0 | 0.5 | normalized |
| seed | 1 | UINT32_MAX | 1 | - |

### Invariants

1. At least one filter type must be enabled in the bitmask (default to LP if all disabled)
2. Smoothing time of 0ms in Jump mode may cause clicks (warn user or enforce minimum)
3. Lorenz state must not contain NaN/Inf (reset on detection)
4. Perlin time advances monotonically (no negative values)

---

## Memory Layout

```
StochasticFilter (estimated size: ~600 bytes)
├── filterA_ (SVF)           ~60 bytes
├── filterB_ (SVF)           ~60 bytes
├── rng_ (Xorshift32)        ~4 bytes
├── seed_ (uint32_t)         ~4 bytes
├── mode_ (RandomMode)       ~1 byte
├── [padding]                ~3 bytes
├── walkValue_ (float)       ~4 bytes
├── jumpValue_ (float)       ~4 bytes
├── samplesUntilNextJump_    ~4 bytes
├── lorenzX/Y/Z_ (3 floats)  ~12 bytes
├── perlinTime_ (float)      ~4 bytes
├── perlinSeed_ (uint32_t)   ~4 bytes
├── cutoffSmoother_          ~20 bytes
├── resonanceSmoother_       ~20 bytes
├── crossfadeSmoother_       ~20 bytes
├── currentTypeA/B_          ~2 bytes
├── isTransitioning_         ~1 byte
├── [padding]                ~1 byte
├── sampleRate_ (double)     ~8 bytes
├── baseCutoffHz_ (float)    ~4 bytes
├── baseResonance_ (float)   ~4 bytes
├── baseFilterType_          ~1 byte
├── [padding]                ~3 bytes
├── cutoffOctaveRange_       ~4 bytes
├── resonanceRange_          ~4 bytes
├── enabledTypesMask_        ~1 byte
├── [padding]                ~3 bytes
├── changeRateHz_ (float)    ~4 bytes
├── smoothingTimeMs_ (float) ~4 bytes
├── cutoffRandomEnabled_     ~1 byte
├── resonanceRandomEnabled_  ~1 byte
├── typeRandomEnabled_       ~1 byte
├── [padding]                ~1 byte
└── samplesUntilUpdate_      ~4 bytes
```

**Note**: All members are stack-allocated. No heap allocations in audio path.

---

## Relationships

```
StochasticFilter
    ├── composes 2x SVF (Layer 1) - for type crossfade
    ├── composes 1x Xorshift32 (Layer 0) - for random generation
    ├── composes 3x OnePoleSmoother (Layer 1) - for parameter smoothing
    └── uses SVFMode (Layer 1) - for filter type selection

RandomMode
    └── used by StochasticFilter - to select modulation algorithm

FilterTypeMask
    └── used by StochasticFilter - to enable/disable types for random selection
```

---

## File Location

```
dsp/include/krate/dsp/processors/stochastic_filter.h
```

Single header implementation following project conventions.
