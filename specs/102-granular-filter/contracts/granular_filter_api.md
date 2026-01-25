# API Contract: GranularFilter

**Date**: 2026-01-25 | **Spec**: [../spec.md](../spec.md) | **Layer**: 3 (Systems)

## Header Location

```
dsp/include/krate/dsp/systems/granular_filter.h
```

## Include Dependencies

```cpp
#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/grain_pool.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/grain_processor.h>
#include <krate/dsp/processors/grain_scheduler.h>

#include <array>
#include <cmath>
```

---

## Public API

### Constants

```cpp
static constexpr float kDefaultMaxDelaySeconds = 2.0f;
static constexpr float kDefaultSmoothTimeMs = 20.0f;
static constexpr float kFreezeCrossfadeMs = 50.0f;
static constexpr float kMinCutoffHz = 20.0f;
static constexpr float kMinQ = 0.5f;
static constexpr float kMaxQ = 20.0f;
static constexpr float kMaxRandomizationOctaves = 4.0f;
```

### Lifecycle

#### prepare
```cpp
/// @brief Prepare the filter engine for processing.
/// @param sampleRate Sample rate in Hz (typically 44100-192000)
/// @param maxDelaySeconds Maximum delay buffer size in seconds (default 2.0)
/// @note Must be called before process()
/// @note All filter instances are prepared with the sample rate
void prepare(double sampleRate, float maxDelaySeconds = kDefaultMaxDelaySeconds) noexcept;
```

#### reset
```cpp
/// @brief Reset all state to initial values.
/// @note Clears delay buffers, releases all grains, resets all filter states
/// @note Does NOT reset parameter values (cutoff, Q, etc.)
void reset() noexcept;
```

---

### Granular Parameters

All inherited from GranularEngine interface.

#### setGrainSize
```cpp
/// @brief Set grain duration.
/// @param ms Grain size in milliseconds (clamped to 10-500ms)
void setGrainSize(float ms) noexcept;
```

#### setDensity
```cpp
/// @brief Set grain trigger rate.
/// @param grainsPerSecond Density in grains per second (clamped to 1-100)
void setDensity(float grainsPerSecond) noexcept;
```

#### setPitch
```cpp
/// @brief Set base pitch shift.
/// @param semitones Pitch shift in semitones (clamped to -24 to +24)
void setPitch(float semitones) noexcept;
```

#### setPitchSpray
```cpp
/// @brief Set pitch randomization amount.
/// @param amount Normalized amount 0-1 (0=none, 1=+/-24 semitones)
void setPitchSpray(float amount) noexcept;
```

#### setPosition
```cpp
/// @brief Set base read position in delay buffer.
/// @param ms Position in milliseconds (clamped to 0-2000ms)
void setPosition(float ms) noexcept;
```

#### setPositionSpray
```cpp
/// @brief Set position randomization amount.
/// @param amount Normalized amount 0-1 (0=none, 1=full range)
void setPositionSpray(float amount) noexcept;
```

#### setReverseProbability
```cpp
/// @brief Set probability of reverse playback.
/// @param probability Probability 0-1 (0=never, 1=always)
void setReverseProbability(float probability) noexcept;
```

#### setPanSpray
```cpp
/// @brief Set stereo pan randomization.
/// @param amount Normalized amount 0-1 (0=center, 1=full L-R spread)
void setPanSpray(float amount) noexcept;
```

#### setJitter
```cpp
/// @brief Set timing jitter.
/// @param amount Normalized amount 0-1 (0=regular, 1=+/-50% timing variation)
void setJitter(float amount) noexcept;
```

#### setEnvelopeType
```cpp
/// @brief Set grain envelope shape.
/// @param type Envelope type (Hann, Trapezoid, Sine, Blackman, Linear, Exponential)
void setEnvelopeType(GrainEnvelopeType type) noexcept;
```

#### setPitchQuantMode
```cpp
/// @brief Set pitch quantization mode.
/// @param mode Quantization mode (Off, Semitone, Scale)
void setPitchQuantMode(PitchQuantMode mode) noexcept;
```

#### setTexture
```cpp
/// @brief Set grain amplitude variation (texture/chaos).
/// @param amount Normalized amount 0-1 (0=uniform, 1=maximum variation)
void setTexture(float amount) noexcept;
```

#### setFreeze
```cpp
/// @brief Enable/disable freeze mode (stop writing to delay buffer).
/// @param frozen true to freeze, false to unfreeze
void setFreeze(bool frozen) noexcept;
```

---

### Filter Parameters (NEW)

#### setFilterEnabled
```cpp
/// @brief Enable or disable per-grain filtering.
/// @param enabled true to enable filtering, false to bypass
/// @note When disabled, grains currently active complete with their filter state
/// @note New grains will bypass filtering entirely
void setFilterEnabled(bool enabled) noexcept;
```

#### setFilterCutoff
```cpp
/// @brief Set base filter cutoff frequency.
/// @param hz Cutoff frequency in Hz (clamped to 20Hz - sampleRate*0.495)
/// @note This is the center frequency for randomization
/// @note Does NOT update cutoff of currently active grains
void setFilterCutoff(float hz) noexcept;
```

#### setFilterResonance
```cpp
/// @brief Set filter resonance (Q factor).
/// @param q Q value (clamped to 0.5-20.0)
/// @note 0.7071 = Butterworth (maximally flat)
/// @note Higher Q = more resonant peak at cutoff
/// @note Updates ALL active grain filters immediately (Q is global)
void setFilterResonance(float q) noexcept;
```

#### setFilterType
```cpp
/// @brief Set filter type.
/// @param mode Filter type (Lowpass, Highpass, Bandpass, Notch)
/// @note Updates ALL active grain filters immediately (type is global)
void setFilterType(SVFMode mode) noexcept;
```

#### setCutoffRandomization
```cpp
/// @brief Set cutoff frequency randomization range in octaves.
/// @param octaves Randomization range (clamped to 0-4 octaves)
/// @note 0 = no randomization, all grains use base cutoff
/// @note 2 octaves at 1kHz = 250Hz to 4kHz range
/// @note Randomization is applied when each grain is triggered
void setCutoffRandomization(float octaves) noexcept;
```

---

### Getters

```cpp
[[nodiscard]] PitchQuantMode getPitchQuantMode() const noexcept;
[[nodiscard]] float getTexture() const noexcept;
[[nodiscard]] bool isFrozen() const noexcept;
[[nodiscard]] size_t activeGrainCount() const noexcept;

// Filter getters (NEW)
[[nodiscard]] bool isFilterEnabled() const noexcept;
[[nodiscard]] float getFilterCutoff() const noexcept;
[[nodiscard]] float getFilterResonance() const noexcept;
[[nodiscard]] SVFMode getFilterType() const noexcept;
[[nodiscard]] float getCutoffRandomization() const noexcept;
```

---

### Processing

#### process
```cpp
/// @brief Process stereo audio sample.
/// @param inputL Left channel input sample
/// @param inputR Right channel input sample
/// @param outputL Reference to left channel output (modified)
/// @param outputR Reference to right channel output (modified)
/// @note Real-time safe: noexcept, no allocations
/// @note Signal flow per grain: read -> pitch -> envelope -> filter -> pan
void process(float inputL, float inputR, float& outputL, float& outputR) noexcept;
```

---

### Deterministic Seeding

#### seed
```cpp
/// @brief Seed random number generators for reproducible output.
/// @param seedValue Seed value (non-zero)
/// @note Seeds both grain scheduling RNG and cutoff randomization RNG
/// @note Use for testing or pattern-based effects
void seed(uint32_t seedValue) noexcept;
```

---

## Usage Example

```cpp
#include <krate/dsp/systems/granular_filter.h>

using namespace Krate::DSP;

GranularFilter gf;

// Prepare for 48kHz sample rate
gf.prepare(48000.0);

// Configure granular parameters
gf.setGrainSize(100.0f);      // 100ms grains
gf.setDensity(20.0f);         // 20 grains/sec
gf.setPitch(0.0f);            // No pitch shift
gf.setPosition(500.0f);       // 500ms delay
gf.setPositionSpray(0.2f);    // 20% position variation

// Configure filter
gf.setFilterEnabled(true);
gf.setFilterType(SVFMode::Lowpass);
gf.setFilterCutoff(2000.0f);  // 2kHz base cutoff
gf.setFilterResonance(2.0f);  // Moderate resonance
gf.setCutoffRandomization(2.0f);  // +/- 2 octaves (500Hz to 8kHz)

// Seed for reproducible output (optional)
gf.seed(12345);

// Process audio
float inputL = ..., inputR = ...;
float outputL, outputR;
gf.process(inputL, inputR, outputL, outputR);
```

---

## Thread Safety

- **NOT thread-safe**: Create separate instances for each audio thread
- All methods are `noexcept` and real-time safe
- No allocations in `process()` - all memory allocated in `prepare()`

---

## Error Handling

- All parameters are silently clamped to valid ranges
- No exceptions thrown (all methods are `noexcept`)
- Calling `process()` before `prepare()` produces silent output
- Invalid filter modes are not possible (enum enforced)
