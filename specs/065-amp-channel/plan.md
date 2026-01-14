# Implementation Plan: AmpChannel (Layer 3 System)

**Feature Branch**: `065-amp-channel`
**Created**: 2026-01-14
**Spec**: [spec.md](spec.md)

---

## Technical Context

### Existing Dependencies (Verified)

| Component | Location | Status | Relevance |
|-----------|----------|--------|-----------|
| TubeStage | `dsp/include/krate/dsp/processors/tube_stage.h` | Complete | Compose 3 instances for preamp + 1 for poweramp |
| Oversampler | `dsp/include/krate/dsp/primitives/oversampler.h` | Complete | Template-based 2x/4x mono oversampling |
| Biquad | `dsp/include/krate/dsp/primitives/biquad.h` | Complete | LowShelf, HighShelf, Peak filter types for tone stack |
| DCBlocker | `dsp/include/krate/dsp/primitives/dc_blocker.h` | Complete | Inter-stage DC blocking |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | Complete | Parameter smoothing (5ms default) |
| dbToGain | `dsp/include/krate/dsp/core/db_utils.h` | Complete | Gain conversions |

### API Review Summary

**TubeStage** (Layer 2):
- `prepare(double sampleRate, size_t maxBlockSize)` - configure for processing
- `reset()` - clear state without reallocation
- `process(float* buffer, size_t numSamples)` - in-place mono processing
- `setInputGain(float dB)` - drive control [-24, +24] dB
- `setOutputGain(float dB)` - makeup [-24, +24] dB
- `setBias(float bias)` - asymmetry [-1, +1]
- `setSaturationAmount(float amount)` - wet/dry [0, 1]
- Internal: 5ms parameter smoothing, 10Hz DC blocking

**Oversampler** (Layer 1):
- Template `Oversampler<Factor, NumChannels>` where Factor = 2 or 4, NumChannels = 1 or 2
- `prepare(sampleRate, maxBlockSize, quality, mode)` - allocates buffers
- `process(float* buffer, size_t numSamples, MonoCallback callback)` - upsample, process, downsample
- `getLatency()` - returns samples of latency based on filter type
- `reset()` - clears filter state

**Biquad** (Layer 1):
- `configure(FilterType, frequency, Q, gainDb, sampleRate)` - set filter parameters
- `process(float input)` - single sample
- `processBlock(float* buffer, size_t numSamples)` - block processing
- Filter types: `LowShelf`, `HighShelf`, `Peak` needed for Baxandall tone stack
- `reset()` - clears state

---

## Constitution Check

### Pre-Design Validation

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | COMPLIANT | All components use noexcept, pre-allocated buffers |
| III. Modern C++ | COMPLIANT | C++20, RAII, smart pointers not needed (value semantics) |
| IX. Layer Architecture | COMPLIANT | Layer 3 depends only on Layers 0-2 |
| X. DSP Constraints | COMPLIANT | Oversampling for saturation, DC blocking between stages |
| XI. Performance Budget | COMPLIANT | Target < 1% CPU for Layer 3 system |
| XIV. ODR Prevention | COMPLIANT | No existing AmpChannel, ToneStack, or BrightCapFilter classes |
| XV. Pre-Implementation Research | COMPLIANT | Searched codebase, no conflicts found |

---

## Phase 0: Research

### Research Tasks

#### R1: Baxandall Tone Stack Implementation

**Decision**: Use three independent Biquad filters (LowShelf + Peak + HighShelf)

**Rationale**:
- Baxandall topology provides independent bass/treble control (adjusting one does not significantly affect the other)
- Existing Biquad primitive supports all required filter types
- Three filters allows precise control: bass shelf (100Hz), mid parametric (800Hz), treble shelf (3kHz)

**Alternatives Considered**:
- TMB (Fender-style) tone stack: Rejected - bass/mid/treble interact heavily
- Parametric-only EQ: Rejected - shelving filters better match amp behavior
- Analog modeling approach: Rejected - complexity not justified for this use case

**Frequency/Q Design** (standard Baxandall values):
| Control | Filter Type | Center Freq | Q | Gain Range |
|---------|-------------|-------------|---|------------|
| Bass | LowShelf | 100 Hz | 0.707 | +/-12 dB |
| Mid | Peak | 800 Hz | 1.0 | +/-12 dB |
| Treble | HighShelf | 3000 Hz | 0.707 | +/-12 dB |
| Presence | HighShelf | 5000 Hz | 0.5 | +/-6 dB |

**Mapping**: Normalized [0, 1] -> [-12, +12] dB via `(value - 0.5f) * 24.0f`

---

#### R2: Bright Cap Filter Design

**Decision**: High-shelf filter with gain-dependent attenuation

**Rationale**:
- Bright cap in real amps bypasses high frequencies around volume pot at low gain settings
- Effect diminishes as gain increases (pot wiper moves toward signal)
- Simple first-order or Biquad high-shelf achieves this effect

**Implementation**:
```
brightBoostDb = 6.0f * (1.0f - clamp((inputGainDb + 24.0f) / 36.0f, 0.0f, 1.0f))
```

At inputGain = -24dB: boost = 6dB
At inputGain = +12dB: boost = 0dB
Linear interpolation between these points.

**Filter Parameters**:
- Type: HighShelf
- Frequency: 3000 Hz
- Q: 0.707 (Butterworth)
- Gain: 0 to +6 dB based on input gain

---

#### R3: Oversampling Factor Change Deferral

**Decision**: Store pending factor, apply on reset()/prepare()

**Rationale**:
- Changing oversampler factor requires reallocation/reconfiguration
- Deferred change avoids real-time allocation
- Matches spec requirement FR-027

**Implementation Pattern**:
```cpp
void setOversamplingFactor(int factor) noexcept {
    pendingOversamplingFactor_ = factor;  // Store for later
}

void reset() noexcept {
    if (pendingOversamplingFactor_ != currentOversamplingFactor_) {
        currentOversamplingFactor_ = pendingOversamplingFactor_;
        configureOversampler();  // Reconfigure with new factor
    }
    // ... reset other state
}
```

---

#### R4: Multi-Stage Preamp Architecture

**Decision**: Fixed array of 3 TubeStage instances with active count parameter

**Rationale**:
- Pre-allocate all 3 stages for real-time safety
- `activePreampStages_` (1-3) controls how many are processed
- Each stage can have independent bias for character variation
- Interstage DC blockers prevent DC accumulation

**Stage Configuration** (defaults):
| Stage | Input Gain | Output Gain | Bias | Saturation |
|-------|------------|-------------|------|------------|
| Preamp 1 | From preampGain_ | 0 dB | 0.0 | 1.0 |
| Preamp 2 | 0 dB | 0 dB | 0.1 | 1.0 |
| Preamp 3 | 0 dB | 0 dB | 0.05 | 1.0 |
| Poweramp | From powerampGain_ | 0 dB | 0.0 | 1.0 |

Slight bias variations between stages add harmonic complexity (empirical tuning values based on typical tube amp characteristics).

---

#### R5: Signal Flow Architecture

**Decision**: Linear signal chain with optional oversampling wrapper

**Signal Flow**:
```
Input
  |
  v
[Input Gain Smoother] -> Apply input gain
  |
  v
[Bright Cap Filter] (if enabled, gain-dependent)
  |
  v
[Oversampling Wrapper] (if factor > 1)
  |  |
  |  v  (upsampled signal)
  |  [Preamp Stage 1] -> [DC Blocker 1]
  |    |
  |    v  (if activeStages >= 2)
  |  [Preamp Stage 2] -> [DC Blocker 2]
  |    |
  |    v  (if activeStages >= 3)
  |  [Preamp Stage 3] -> [DC Blocker 3]
  |  |
  |  v
  |  (downsample)
  |
  v
[Tone Stack] (pre or post position)
  |
  v
[Poweramp Stage] -> [DC Blocker 4]
  |
  v
[Master Volume Smoother] -> Apply master gain
  |
  v
Output
```

**Notes**:
- Tone stack position (Pre/Post) determines whether it goes before preamp stages (Pre) or after poweramp (Post, default).
- Bright cap is applied after input gain but before preamp stages for authentic vintage amp behavior.
- Post-position tone stack is applied after the oversampler (downsampled) for efficiency.

---

## Phase 1: Data Model & Contracts

### Entity Definitions

#### AmpChannel Class

```cpp
namespace Krate {
namespace DSP {

/// Tone stack position relative to distortion stages
enum class ToneStackPosition : uint8_t {
    Pre = 0,   ///< Before preamp stages (EQ drives into distortion)
    Post = 1   ///< After poweramp stage (EQ shapes distorted tone)
};

/// @brief Layer 3 System - Guitar amp channel with gain staging and tone shaping
class AmpChannel {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = +24.0f;
    static constexpr float kMinMasterDb = -60.0f;
    static constexpr float kMaxMasterDb = +6.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr int kMinPreampStages = 1;
    static constexpr int kMaxPreampStages = 3;
    static constexpr int kDefaultPreampStages = 2;

    // Tone stack frequencies
    static constexpr float kBassFreqHz = 100.0f;
    static constexpr float kMidFreqHz = 800.0f;
    static constexpr float kTrebleFreqHz = 3000.0f;
    static constexpr float kPresenceFreqHz = 5000.0f;

    // Bright cap parameters
    static constexpr float kBrightCapFreqHz = 3000.0f;
    static constexpr float kBrightCapMaxBoostDb = 6.0f;

    // Filter Q values
    static constexpr float kButterworthQ = 0.707f;  // Butterworth Q for shelving filters
    static constexpr float kMidQ = 1.0f;            // Q for parametric mid
    static constexpr float kPresenceQ = 0.5f;       // Wider Q for presence

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    AmpChannel() noexcept = default;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Gain Staging (FR-004 to FR-008)
    // =========================================================================

    void setInputGain(float dB) noexcept;      // [-24, +24] dB
    void setPreampGain(float dB) noexcept;     // [-24, +24] dB
    void setPowerampGain(float dB) noexcept;   // [-24, +24] dB
    void setMasterVolume(float dB) noexcept;   // [-60, +6] dB

    [[nodiscard]] float getInputGain() const noexcept;
    [[nodiscard]] float getPreampGain() const noexcept;
    [[nodiscard]] float getPowerampGain() const noexcept;
    [[nodiscard]] float getMasterVolume() const noexcept;

    // =========================================================================
    // Preamp Configuration (FR-009 to FR-013)
    // =========================================================================

    void setPreampStages(int count) noexcept;  // [1, 3]
    [[nodiscard]] int getPreampStages() const noexcept;

    // =========================================================================
    // Tone Stack (FR-014 to FR-021)
    // =========================================================================

    void setToneStackPosition(ToneStackPosition pos) noexcept;
    void setBass(float value) noexcept;        // [0, 1] -> +/-12dB
    void setMid(float value) noexcept;         // [0, 1] -> +/-12dB
    void setTreble(float value) noexcept;      // [0, 1] -> +/-12dB
    void setPresence(float value) noexcept;    // [0, 1] -> +/-6dB

    [[nodiscard]] ToneStackPosition getToneStackPosition() const noexcept;
    [[nodiscard]] float getBass() const noexcept;
    [[nodiscard]] float getMid() const noexcept;
    [[nodiscard]] float getTreble() const noexcept;
    [[nodiscard]] float getPresence() const noexcept;

    // =========================================================================
    // Character Controls (FR-022 to FR-025)
    // =========================================================================

    void setBrightCap(bool enabled) noexcept;
    [[nodiscard]] bool getBrightCap() const noexcept;

    // =========================================================================
    // Oversampling (FR-026 to FR-030)
    // =========================================================================

    void setOversamplingFactor(int factor) noexcept;  // 1, 2, or 4
    [[nodiscard]] int getOversamplingFactor() const noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;

    // =========================================================================
    // Processing (FR-031 to FR-034)
    // =========================================================================

    void process(float* buffer, size_t numSamples) noexcept;

private:
    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;

    // Gain parameters (stored in dB)
    float inputGainDb_ = 0.0f;
    float preampGainDb_ = 0.0f;
    float powerampGainDb_ = 0.0f;
    float masterVolumeDb_ = 0.0f;

    // Preamp configuration
    int activePreampStages_ = kDefaultPreampStages;

    // Tone stack parameters (stored as normalized 0-1)
    ToneStackPosition toneStackPosition_ = ToneStackPosition::Post;
    float bassValue_ = 0.5f;
    float midValue_ = 0.5f;
    float trebleValue_ = 0.5f;
    float presenceValue_ = 0.5f;

    // Character controls
    bool brightCapEnabled_ = false;

    // Oversampling
    int currentOversamplingFactor_ = 1;
    int pendingOversamplingFactor_ = 1;

    // =========================================================================
    // DSP Components
    // =========================================================================

    // Parameter smoothers
    OnePoleSmoother inputGainSmoother_;
    OnePoleSmoother masterVolumeSmoother_;

    // Preamp stages (fixed array, active count controlled by parameter)
    std::array<TubeStage, kMaxPreampStages> preampStages_;
    std::array<DCBlocker, kMaxPreampStages> preampDCBlockers_;

    // Poweramp stage
    TubeStage powerampStage_;
    DCBlocker powerampDCBlocker_;

    // Tone stack (Baxandall style)
    Biquad bassFilter_;        // LowShelf @ 100Hz
    Biquad midFilter_;         // Peak @ 800Hz
    Biquad trebleFilter_;      // HighShelf @ 3kHz
    Biquad presenceFilter_;    // HighShelf @ 5kHz

    // Bright cap filter
    Biquad brightCapFilter_;   // HighShelf @ 3kHz, gain-dependent

    // Oversamplers (one for each factor, only one active)
    Oversampler<2, 1> oversampler2x_;
    Oversampler<4, 1> oversampler4x_;

    // Work buffer for oversampled processing
    std::vector<float> oversampledBuffer_;

    // =========================================================================
    // Private Methods
    // =========================================================================

    void updateToneStack() noexcept;
    void updateBrightCap() noexcept;
    void configureOversampler() noexcept;
    void processPreampStages(float* buffer, size_t numSamples) noexcept;
    void processToneStack(float* buffer, size_t numSamples) noexcept;
};

} // namespace DSP
} // namespace Krate
```

### File Structure

```
dsp/include/krate/dsp/systems/
    amp_channel.h           # AmpChannel class definition and inline implementation

dsp/tests/systems/
    amp_channel_tests.cpp   # Unit tests for AmpChannel

specs/_architecture_/
    layer-3-systems.md      # Update with AmpChannel documentation
```

---

## Phase 2: Implementation Plan

### Task Breakdown

#### Task 1: Create AmpChannel Header (FR-001 to FR-037)

**File**: `dsp/include/krate/dsp/systems/amp_channel.h`

**Subtasks**:
1. Add file header with constitution compliance notes
2. Define `ToneStackPosition` enum
3. Implement `AmpChannel` class with all constants
4. Implement `prepare()` - configure all components
5. Implement `reset()` - clear state, apply pending oversampling factor
6. Implement all setters with parameter clamping
7. Implement all getters
8. Implement `process()` with signal flow
9. Implement private helper methods

**Estimated Complexity**: Medium-High (composing many components)

---

#### Task 2: Implement prepare() and reset()

**Key Implementation Details**:

```cpp
void prepare(double sampleRate, size_t maxBlockSize) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Configure smoothers (5ms default)
    const float sr = static_cast<float>(sampleRate);
    inputGainSmoother_.configure(kDefaultSmoothingMs, sr);
    masterVolumeSmoother_.configure(kDefaultSmoothingMs, sr);

    // Snap to initial values
    inputGainSmoother_.setTarget(dbToGain(inputGainDb_));
    inputGainSmoother_.snapToTarget();
    masterVolumeSmoother_.setTarget(dbToGain(masterVolumeDb_));
    masterVolumeSmoother_.snapToTarget();

    // Prepare all preamp stages
    for (auto& stage : preampStages_) {
        stage.prepare(sampleRate, maxBlockSize);
    }
    for (auto& blocker : preampDCBlockers_) {
        blocker.prepare(sampleRate, 10.0f);  // 10Hz cutoff
    }

    // Prepare poweramp
    powerampStage_.prepare(sampleRate, maxBlockSize);
    powerampDCBlocker_.prepare(sampleRate, 10.0f);

    // Configure tone stack
    updateToneStack();

    // Configure bright cap
    updateBrightCap();

    // Configure oversampler
    configureOversampler();

    // Allocate work buffer (pre-allocate for max 4x factor to ensure real-time safety)
    oversampledBuffer_.resize(maxBlockSize * 4);  // Max 4x oversampling; Oversampler allocates internally on prepare()

    reset();
}
```

---

#### Task 3: Implement Signal Processing

**process() Implementation Strategy**:

```cpp
void process(float* buffer, size_t numSamples) noexcept {
    // FR-032, FR-033: Handle edge cases
    if (numSamples == 0 || buffer == nullptr) {
        return;
    }

    // Apply input gain with smoothing
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= inputGainSmoother_.process();
    }

    // Apply bright cap (if enabled, before distortion)
    if (brightCapEnabled_) {
        updateBrightCap();  // Update gain based on current input gain
        brightCapFilter_.processBlock(buffer, numSamples);
    }

    // Tone stack in Pre position
    if (toneStackPosition_ == ToneStackPosition::Pre) {
        processToneStack(buffer, numSamples);
    }

    // Process through preamp and poweramp with oversampling
    if (currentOversamplingFactor_ == 1) {
        // No oversampling - process directly
        processPreampStages(buffer, numSamples);
        powerampStage_.process(buffer, numSamples);
        powerampDCBlocker_.processBlock(buffer, numSamples);
    } else if (currentOversamplingFactor_ == 2) {
        oversampler2x_.process(buffer, numSamples,
            [this](float* os, size_t n) {
                processPreampStages(os, n);
                powerampStage_.process(os, n);
                powerampDCBlocker_.processBlock(os, n);
            });
    } else {  // 4x
        oversampler4x_.process(buffer, numSamples,
            [this](float* os, size_t n) {
                processPreampStages(os, n);
                powerampStage_.process(os, n);
                powerampDCBlocker_.processBlock(os, n);
            });
    }

    // Tone stack in Post position (default)
    if (toneStackPosition_ == ToneStackPosition::Post) {
        processToneStack(buffer, numSamples);
    }

    // Apply master volume with smoothing
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= masterVolumeSmoother_.process();
    }
}
```

---

#### Task 4: Implement Tone Stack

**updateToneStack() Implementation**:

```cpp
void updateToneStack() noexcept {
    const float sr = static_cast<float>(sampleRate_);

    // Map [0, 1] -> [-12, +12] dB (or [-6, +6] for presence)
    const float bassDb = (bassValue_ - 0.5f) * 24.0f;
    const float midDb = (midValue_ - 0.5f) * 24.0f;
    const float trebleDb = (trebleValue_ - 0.5f) * 24.0f;
    const float presenceDb = (presenceValue_ - 0.5f) * 12.0f;

    // Configure filters (FilterType enum from Biquad API)
    bassFilter_.configure(FilterType::LowShelf, kBassFreqHz, kButterworthQ, bassDb, sr);
    midFilter_.configure(FilterType::Peak, kMidFreqHz, kMidQ, midDb, sr);
    trebleFilter_.configure(FilterType::HighShelf, kTrebleFreqHz, kButterworthQ, trebleDb, sr);
    presenceFilter_.configure(FilterType::HighShelf, kPresenceFreqHz, kPresenceQ, presenceDb, sr);
}

void processToneStack(float* buffer, size_t numSamples) noexcept {
    bassFilter_.processBlock(buffer, numSamples);
    midFilter_.processBlock(buffer, numSamples);
    trebleFilter_.processBlock(buffer, numSamples);
    presenceFilter_.processBlock(buffer, numSamples);
}
```

---

#### Task 5: Implement Bright Cap

**updateBrightCap() Implementation**:

```cpp
void updateBrightCap() noexcept {
    // Calculate gain-dependent boost
    // At -24dB input: +6dB boost
    // At +12dB input: 0dB boost
    // Linear interpolation between
    const float normalizedGain = (inputGainDb_ + 24.0f) / 36.0f;  // [0, 1] range
    const float clampedNorm = std::clamp(normalizedGain, 0.0f, 1.0f);
    const float boostDb = kBrightCapMaxBoostDb * (1.0f - clampedNorm);

    // FilterType enum from Biquad API
    brightCapFilter_.configure(
        FilterType::HighShelf,
        kBrightCapFreqHz,
        kButterworthQ,
        boostDb,
        static_cast<float>(sampleRate_)
    );
}
```

---

#### Task 6: Implement Oversampling Management

**configureOversampler() Implementation**:

```cpp
void configureOversampler() noexcept {
    currentOversamplingFactor_ = pendingOversamplingFactor_;

    if (currentOversamplingFactor_ == 2) {
        oversampler2x_.prepare(sampleRate_, maxBlockSize_,
            OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
    } else if (currentOversamplingFactor_ == 4) {
        oversampler4x_.prepare(sampleRate_, maxBlockSize_,
            OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
    }
    // Factor 1 needs no configuration (bypass)
}

size_t getLatency() const noexcept {
    if (currentOversamplingFactor_ == 2) {
        return oversampler2x_.getLatency();
    } else if (currentOversamplingFactor_ == 4) {
        return oversampler4x_.getLatency();
    }
    return 0;  // No oversampling = no latency
}
```

---

### Test Strategy

#### Unit Tests per User Story

**US1: Basic Amp Channel Processing**
```cpp
TEST_CASE("AmpChannel basic processing") {
    AmpChannel amp;
    amp.prepare(44100.0, 512);

    SECTION("SC-001: +12dB preamp produces 3rd harmonic THD > 1%") {
        amp.setPreampGain(12.0f);
        std::vector<float> sine = generateSine(1000.0f, 44100.0, 4096);
        amp.process(sine.data(), sine.size());
        float thd = measureTHD(sine);
        REQUIRE(thd > 0.01f);  // > 1%
    }

    SECTION("SC-009: Default params produce near-unity gain") {
        // All defaults: 0dB gains, 0.5 tones
        std::vector<float> buffer(512, 0.5f);
        amp.process(buffer.data(), buffer.size());
        // After settling, output should be close to input
        float rms = calculateRMS(buffer);
        REQUIRE(rms == Approx(0.5f).margin(0.05f));
    }
}
```

**US2: Tone Stack Shaping**
```cpp
TEST_CASE("AmpChannel tone stack") {
    AmpChannel amp;
    amp.prepare(44100.0, 512);

    SECTION("SC-006: Bass boost increases low frequency energy") {
        amp.setBass(1.0f);  // Max boost
        std::vector<float> noise = generatePinkNoise(4096);
        amp.process(noise.data(), noise.size());
        float lowEnergy = measureBandEnergy(noise, 50, 200);

        amp.reset();
        amp.setBass(0.0f);  // Max cut
        std::vector<float> noise2 = generatePinkNoise(4096);
        amp.process(noise2.data(), noise2.size());
        float lowEnergy2 = measureBandEnergy(noise2, 50, 200);

        REQUIRE(lowEnergy > lowEnergy2 * 4.0f);  // ~12dB difference
    }

    SECTION("FR-020: Baxandall independence - bass doesn't affect treble") {
        amp.setBass(1.0f);  // Max bass
        amp.setTreble(0.5f);  // Neutral treble
        std::vector<float> noise = generateWhiteNoise(4096);
        amp.process(noise.data(), noise.size());
        float highEnergy1 = measureBandEnergy(noise, 4000, 8000);

        amp.reset();
        amp.setBass(0.0f);  // Min bass
        amp.setTreble(0.5f);  // Still neutral treble
        std::vector<float> noise2 = generateWhiteNoise(4096);
        amp.process(noise2.data(), noise2.size());
        float highEnergy2 = measureBandEnergy(noise2, 4000, 8000);

        // High frequencies should be similar regardless of bass setting
        REQUIRE(highEnergy1 == Approx(highEnergy2).margin(highEnergy2 * 0.2f));
    }
}
```

**US3: Oversampling**
```cpp
TEST_CASE("AmpChannel oversampling") {
    SECTION("SC-003: 4x reduces aliasing by 40dB") {
        AmpChannel amp1x, amp4x;
        amp1x.prepare(44100.0, 512);
        amp4x.prepare(44100.0, 512);

        amp1x.setOversamplingFactor(1);
        amp4x.setOversamplingFactor(4);
        amp4x.reset();  // Apply pending factor

        amp1x.setPreampGain(18.0f);
        amp4x.setPreampGain(18.0f);

        // 15kHz sine will alias without oversampling
        auto sine1x = generateSine(15000.0f, 44100.0, 4096);
        auto sine4x = generateSine(15000.0f, 44100.0, 4096);

        amp1x.process(sine1x.data(), sine1x.size());
        amp4x.process(sine4x.data(), sine4x.size());

        float alias1x = measureAliasingEnergy(sine1x, 15000.0f);
        float alias4x = measureAliasingEnergy(sine4x, 15000.0f);

        float reductionDb = 20.0f * std::log10(alias1x / alias4x);
        REQUIRE(reductionDb >= 40.0f);
    }

    SECTION("SC-010: Oversampling change deferred until reset()") {
        AmpChannel amp;
        amp.prepare(44100.0, 512);
        amp.setOversamplingFactor(1);
        amp.reset();

        REQUIRE(amp.getOversamplingFactor() == 1);

        amp.setOversamplingFactor(4);  // Set pending
        REQUIRE(amp.getOversamplingFactor() == 1);  // Still 1x

        amp.reset();  // Apply change
        REQUIRE(amp.getOversamplingFactor() == 4);  // Now 4x
    }
}
```

**US4: Bright Cap**
```cpp
TEST_CASE("AmpChannel bright cap") {
    SECTION("SC-007: +6dB at 3kHz when input at -24dB") {
        AmpChannel amp;
        amp.prepare(44100.0, 512);
        amp.setInputGain(-24.0f);
        amp.setBrightCap(true);

        // Generate 3kHz test tone
        auto tone = generateSine(3000.0f, 44100.0, 4096);
        float inputLevel = calculateRMS(tone);
        amp.process(tone.data(), tone.size());
        float outputLevel = calculateRMS(tone);

        float boostDb = 20.0f * std::log10(outputLevel / inputLevel);
        REQUIRE(boostDb == Approx(6.0f).margin(1.0f));
    }

    SECTION("SC-007: 0dB at 3kHz when input at +12dB") {
        AmpChannel amp;
        amp.prepare(44100.0, 512);
        amp.setInputGain(12.0f);
        amp.setBrightCap(true);

        auto tone = generateSine(3000.0f, 44100.0, 4096);
        float inputLevel = calculateRMS(tone);
        amp.process(tone.data(), tone.size());
        float outputLevel = calculateRMS(tone);

        // Note: Output will be affected by saturation, so compare at low level
        // This test should use a level measurement that accounts for gain
        float boostDb = 20.0f * std::log10(outputLevel / inputLevel);
        // Bright cap should not add boost (within saturation effects)
        REQUIRE(boostDb < 1.0f);
    }
}
```

**US5: Configurable Preamp Stages**
```cpp
TEST_CASE("AmpChannel preamp stages") {
    SECTION("SC-011: Different stage counts produce different harmonics") {
        AmpChannel amp1, amp3;
        amp1.prepare(44100.0, 512);
        amp3.prepare(44100.0, 512);

        amp1.setPreampStages(1);
        amp3.setPreampStages(3);
        amp1.setPreampGain(12.0f);
        amp3.setPreampGain(12.0f);

        auto sine1 = generateSine(1000.0f, 44100.0, 4096);
        auto sine3 = generateSine(1000.0f, 44100.0, 4096);

        amp1.process(sine1.data(), sine1.size());
        amp3.process(sine3.data(), sine3.size());

        float thd1 = measureTHD(sine1);
        float thd3 = measureTHD(sine3);

        // 3 stages should have more harmonic content
        REQUIRE(thd3 > thd1);
    }

    SECTION("FR-013: Default is 2 preamp stages") {
        AmpChannel amp;
        amp.prepare(44100.0, 512);
        REQUIRE(amp.getPreampStages() == 2);
    }
}
```

**Edge Cases**
```cpp
TEST_CASE("AmpChannel edge cases") {
    SECTION("FR-032: Handle n=0 gracefully") {
        AmpChannel amp;
        amp.prepare(44100.0, 512);
        float dummy = 0.0f;
        amp.process(&dummy, 0);  // Should not crash
    }

    SECTION("FR-033: Handle nullptr gracefully") {
        AmpChannel amp;
        amp.prepare(44100.0, 512);
        amp.process(nullptr, 100);  // Should not crash
    }

    SECTION("SC-005: Stability over 10 seconds") {
        AmpChannel amp;
        amp.prepare(44100.0, 512);
        amp.setPreampGain(18.0f);
        amp.setPowerampGain(12.0f);

        std::vector<float> buffer(512);
        for (int block = 0; block < 44100 * 10 / 512; ++block) {
            // Generate pink noise
            for (auto& s : buffer) {
                s = generatePinkNoiseSample();
            }
            amp.process(buffer.data(), buffer.size());

            // Check for NaN/Inf
            for (auto s : buffer) {
                REQUIRE(std::isfinite(s));
            }

            // Check for DC drift
            float dc = std::accumulate(buffer.begin(), buffer.end(), 0.0f) / buffer.size();
            REQUIRE(std::abs(dc) < 0.1f);
        }
    }

    SECTION("SC-008: Works at 192kHz") {
        AmpChannel amp;
        amp.prepare(192000.0, 512);
        amp.setPreampGain(12.0f);

        std::vector<float> buffer = generateSine(1000.0f, 192000.0, 512);
        amp.process(buffer.data(), buffer.size());

        // Should produce output without crashing
        float rms = calculateRMS(buffer);
        REQUIRE(rms > 0.0f);
        REQUIRE(std::isfinite(rms));
    }
}
```

---

## Implementation Order

1. **Create amp_channel.h** with class skeleton and constants
2. **Implement prepare() and reset()** - configure all components
3. **Implement gain setters/getters** with parameter smoothing
4. **Implement preamp stage management** - setPreampStages, processPreampStages
5. **Implement tone stack** - Baxandall filters, updateToneStack
6. **Implement bright cap** - gain-dependent high shelf
7. **Implement oversampling** - deferred factor change pattern
8. **Implement process()** - full signal chain
9. **Write unit tests** for each user story
10. **Update layer-3-systems.md** documentation

---

## Post-Design Constitution Re-Check

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | COMPLIANT | All allocation in prepare(), noexcept throughout |
| III. Modern C++ | COMPLIANT | C++20, value semantics, std::array for fixed components |
| IX. Layer Architecture | COMPLIANT | Layer 3 using only Layer 0-2 components |
| X. DSP Constraints | COMPLIANT | Oversampling, DC blocking, parameter smoothing |
| XI. Performance Budget | COMPLIANT | 4 TubeStages + filters + oversampler < 1% target |
| XIV. ODR Prevention | COMPLIANT | Unique class name AmpChannel in Krate::DSP |
| XVI. Honest Completion | READY | Clear success criteria defined for all requirements |

---

## Artifacts Generated

- `specs/065-amp-channel/plan.md` (this file)

## Files to Create During Implementation

- `dsp/include/krate/dsp/systems/amp_channel.h`
- `dsp/tests/systems/amp_channel_tests.cpp`
- Update `specs/_architecture_/layer-3-systems.md`
