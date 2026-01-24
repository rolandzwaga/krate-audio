# Implementation Plan: Audio-Rate Filter FM

**Branch**: `095-audio-rate-filter-fm` | **Date**: 2026-01-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/095-audio-rate-filter-fm/spec.md`

## Summary

Audio-rate filter FM processor (Layer 2) that modulates SVF filter cutoff at audio rates (20Hz-20kHz) using a wavetable oscillator with linear interpolation. Creates metallic, bell-like, ring modulation-style, and aggressive timbres. Supports internal oscillator, external modulator, and self-modulation (feedback FM) sources with configurable oversampling (1x/2x/4x) for anti-aliasing.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: SVF (Layer 1), Oversampler (Layer 1), math_constants.h, db_utils.h (Layer 0)
**Storage**: N/A (stateless configuration, filter state only)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform VST3
**Project Type**: DSP library component (header-only, Layer 2 processor)
**Performance Goals**: 512-sample block at 4x oversampling < 2ms (SC-010)
**Constraints**: Real-time safe (no allocations in process), noexcept processing
**Scale/Scope**: Single-channel mono processor, users create separate instances per channel

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check

**Principle II (Real-Time Safety):**
- [x] All processing methods will be noexcept
- [x] No allocations in process() - wavetable and oversampler buffers pre-allocated in prepare()
- [x] No locks, mutexes, or blocking operations in audio path
- [x] No exceptions, no I/O in process methods

**Principle IX (Layered Architecture):**
- [x] Layer 2 processor - depends only on Layer 0 (core) and Layer 1 (primitives)
- [x] Will use SVF from primitives/svf.h (Layer 1)
- [x] Will use Oversampler from primitives/oversampler.h (Layer 1)
- [x] Will use math_constants.h and db_utils.h from core/ (Layer 0)

**Principle X (DSP Constraints):**
- [x] Oversampling for nonlinear filter modulation (anti-aliasing)
- [x] Denormal flushing after filter state updates
- [x] Feedback safety for self-modulation (hard-clip to [-1, +1])

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

### Post-Design Re-Check

- [x] All decisions align with constitution
- [x] No violations requiring complexity tracking

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| AudioRateFilterFM | `grep -r "class AudioRateFilterFM" dsp/ plugins/` | No | Create New |
| FMModSource | `grep -r "FMModSource" dsp/ plugins/` | No | Create New |
| FMFilterType | `grep -r "FMFilterType" dsp/ plugins/` | No | Create New |
| FMWaveform | `grep -r "FMWaveform" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | core/db_utils.h | Reuse |
| isNaN | `grep -r "isNaN" dsp/` | Yes | core/db_utils.h | Reuse |
| isInf | `grep -r "isInf" dsp/` | Yes | core/db_utils.h | Reuse |
| kPi, kTwoPi | `grep -r "kTwoPi" dsp/` | Yes | core/math_constants.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SVF | primitives/svf.h | 1 | Carrier filter - audio-rate cutoff modulation |
| SVFMode | primitives/svf.h | 1 | Map FMFilterType to SVF mode internally |
| Oversampler | primitives/oversampler.h | 1 | Anti-aliasing for high-frequency modulation |
| Oversampler2xMono | primitives/oversampler.h | 1 | Type alias for 2x mono oversampling |
| Oversampler4xMono | primitives/oversampler.h | 1 | Type alias for 4x mono oversampling |
| kPi, kTwoPi | core/math_constants.h | 0 | Phase increment calculations for oscillator |
| detail::flushDenormal() | core/db_utils.h | 0 | Flush denormals in filter state |
| detail::isNaN() | core/db_utils.h | 0 | Input validation |
| detail::isInf() | core/db_utils.h | 0 | Input validation |
| LFO wavetable pattern | primitives/lfo.h | 1 | Reference for wavetable oscillator design |
| Waveform enum | primitives/lfo.h | 1 | Reference only (will define separate FMWaveform) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 primitives (SVF, Oversampler to reuse)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no AudioRateFilterFM exists)
- [x] `specs/_architecture_/` - Component inventory (no FM filter listed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (AudioRateFilterFM, FMModSource, FMFilterType, FMWaveform) are unique and not found in codebase. The only potential confusion is with the existing Waveform enum in LFO, but spec explicitly requires separate FMWaveform enum to avoid confusion.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | kMaxCutoffRatio | `static constexpr float kMaxCutoffRatio = 0.495f` | Yes |
| Oversampler | prepare | `void prepare(double sampleRate, size_t maxBlockSize, OversamplingQuality quality, OversamplingMode mode) noexcept` | Yes |
| Oversampler | reset | `void reset() noexcept` | Yes |
| Oversampler | getLatency | `[[nodiscard]] size_t getLatency() const noexcept` | Yes |
| Oversampler | upsample | `void upsample(const float* input, float* output, size_t numSamples, size_t channel) noexcept` | Yes |
| Oversampler | downsample | `void downsample(const float* input, float* output, size_t numSamples, size_t channel) noexcept` | Yes |
| detail::flushDenormal | - | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | - | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | - | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| kPi | - | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |
| kTwoPi | - | `inline constexpr float kTwoPi = 2.0f * kPi` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class, SVFMode enum
- [x] `dsp/include/krate/dsp/primitives/oversampler.h` - Oversampler template, type aliases
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - Wavetable pattern reference
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SVF | setCutoff clamps to `[1 Hz, sampleRate * 0.495]` | Use SVF::kMaxCutoffRatio for max |
| SVF | Must call prepare() before processing | Check `isPrepared()` or handle in process |
| Oversampler | prepare() allocates memory (NOT real-time safe) | Call only in prepare(), never in process() |
| Oversampler | Type aliases: Oversampler2xMono = Oversampler<2, 1> | Use template for runtime factor selection |
| SVF | prepare() takes double sampleRate, not float | Pass oversampled rate as double |
| LFO wavetable | Uses kTableSize = 2048, with linear interpolation | Similar approach for internal oscillator |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Wavetable generation | AudioRateFilterFM-specific, inline in class |
| Phase increment calculation | Simple formula, tied to oscillator state |
| FM cutoff calculation | Feature-specific exponential mapping |

**Decision**: No new Layer 0 utilities needed. All required utilities already exist in db_utils.h and math_constants.h.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from FLT-ROADMAP.md):
- Phase 16.3: frequency_shifter.h - May use similar wavetable oscillator
- Phase 16.4: cross_mod_filter.h - May share modulation patterns
- Phase 16.2: comb_filter_bank.h - Different concept, unlikely to share

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Wavetable oscillator | MEDIUM | frequency_shifter | Keep local, extract after 2nd use |
| FM depth exponential mapping | LOW | Unique to FM | Keep local |
| Oversampling wrapper pattern | HIGH | Already in Oversampler | Reuse existing |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep wavetable inline | First implementation, patterns not established. If frequency_shifter needs similar oscillator, extract to audio_oscillator.h |
| Use existing Oversampler | Proven, well-tested, handles 2x/4x with quality settings |

### Review Trigger

After implementing **frequency_shifter.h** (Phase 16.3), review this section:
- [ ] Does frequency_shifter need similar wavetable oscillator? -> Extract to shared audio_oscillator.h
- [ ] Does it use same FM modulation pattern? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/095-audio-rate-filter-fm/
├── plan.md              # This file
├── research.md          # Phase 0 output - research findings
├── data-model.md        # Phase 1 output - class/struct definitions
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - API contracts
│   └── audio_rate_filter_fm.api.md
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── audio_rate_filter_fm.h    # Header-only implementation
└── tests/
    └── unit/
        └── processors/
            └── audio_rate_filter_fm_test.cpp  # Catch2 tests
```

**Structure Decision**: Single header file in processors/ following existing Layer 2 patterns (envelope_filter.h, multimode_filter.h).

## Complexity Tracking

> No constitution violations requiring justification.

---

## Research Findings (Phase 0)

### Wavetable Oscillator Implementation

**Decision**: Use 2048-sample wavetable with linear interpolation (per spec clarification)

**Rationale**:
- LFO.h already uses this pattern successfully with kTableSize = 2048
- Linear interpolation provides adequate quality for modulator (not an audible output)
- Pre-computed tables avoid runtime trig calls
- 2048 samples provides sufficient resolution for audio-rate frequencies

**Wavetable Size Analysis**:
- At 20kHz modulator and 44.1kHz sample rate: phase increment = 0.453
- With 2048 samples, step through ~927 samples per cycle
- Linear interpolation error < 0.1% for sine

**Implementation Pattern** (from LFO.h):
```cpp
// Pre-computed tables for Sine, Triangle, Sawtooth, Square
static constexpr size_t kWavetableSize = 2048;
std::array<float, kWavetableSize> sineTable_;
std::array<float, kWavetableSize> triangleTable_;
std::array<float, kWavetableSize> sawTable_;
std::array<float, kWavetableSize> squareTable_;

// Linear interpolation read
float readWavetable(const float* table, double phase) {
    double scaledPhase = phase * kWavetableSize;
    size_t index0 = static_cast<size_t>(scaledPhase);
    size_t index1 = (index0 + 1) % kWavetableSize;
    float frac = static_cast<float>(scaledPhase - index0);
    return table[index0] + frac * (table[index1] - table[index0]);
}
```

### SVF Integration at Oversampled Rate

**Decision**: Call SVF.prepare() with oversampled rate (sampleRate * oversamplingFactor)

**Rationale** (per spec clarification):
- SVF coefficients depend on sample rate (g = tan(pi * fc / fs))
- Running SVF at 4x rate means fs is 4x higher
- Correct coefficients require prepare(sampleRate * 4) for 4x oversampling

**Implementation**:
```cpp
void prepare(double sampleRate, size_t maxBlockSize) {
    baseSampleRate_ = sampleRate;
    updateSVFForOversampling();  // Reconfigure SVF with oversampled rate
}

void setOversamplingFactor(int factor) {
    oversamplingFactor_ = clampFactor(factor);
    updateSVFForOversampling();
}

void updateSVFForOversampling() {
    const double oversampledRate = baseSampleRate_ * oversamplingFactor_;
    svf_.prepare(oversampledRate);
}
```

### Self-Modulation Stability

**Decision**: Hard-clip filter output to [-1, +1] before using as modulator

**Rationale** (per spec clarification):
- Without clipping, feedback can cause exponential frequency excursions
- `modulatedCutoff = carrierCutoff * 2^(modulator * fmDepth)`
- If modulator exceeds [-1, +1], frequency grows without bound
- Hard clipping is simpler than soft limiting and sufficient for stability

**Implementation**:
```cpp
// In process() for self-modulation mode
float selfModValue = std::clamp(previousOutput_, -1.0f, 1.0f);
float modulatedCutoff = calculateModulatedCutoff(selfModValue);
```

### Oversampling Factor Clamping

**Decision**: Clamp invalid factors to nearest valid value (≤1→1, 3→2, ≥5→4)

**Rationale** (per spec clarification):
- Only 1, 2, 4 are valid (Oversampler only supports 2x and 4x)
- Factor 1 means "no oversampling" (process directly without up/downsample)
- Clamping rule: values ≤1 become 1, values 2-4 become 2, values ≥5 become 4
- This provides predictable behavior for invalid input

**Implementation**:
```cpp
void setOversamplingFactor(int factor) {
    if (factor <= 1) {
        oversamplingFactor_ = 1;
    } else if (factor <= 3) {
        // 2 and 3 both map to 2x oversampling
        oversamplingFactor_ = 2;
    } else {
        // 4 and higher map to 4x oversampling
        oversamplingFactor_ = 4;
    }
    // Reconfigure if prepared
    if (prepared_) {
        updateSVFForOversampling();
    }
}
```

### Waveform-Specific THD Requirements

**Decision**: Test sine <0.1% THD, triangle <1% THD, no limits for saw/square

**Rationale** (per spec clarification):
- Sine and triangle are expected to be "clean" waveforms
- Sawtooth and square inherently contain harmonics (not distortion)
- THD measurement only meaningful for waveforms intended to be pure

**Test Strategy**:
```cpp
// SC-002: Waveform quality tests
TEST_CASE("Internal oscillator waveform quality") {
    SECTION("Sine wave THD < 0.1%") {
        // Measure THD using FFT, verify < 0.001
    }
    SECTION("Triangle wave THD < 1%") {
        // Measure THD, verify < 0.01
    }
    SECTION("Sawtooth produces stable bounded output") {
        // No THD check, verify no NaN/Inf
    }
    SECTION("Square produces stable bounded output") {
        // No THD check, verify no NaN/Inf
    }
}
```

### FM Depth Calculation

**Decision**: Exponential frequency mapping: `modulatedCutoff = carrierCutoff * 2^(modulator * fmDepth)`

**Rationale**:
- Per spec FR-013
- 1 octave up = 2x frequency, 1 octave down = 0.5x frequency
- Modulator range [-1, +1] maps to [-fmDepth, +fmDepth] octaves
- At fmDepth=1, modulator=+1 -> 2x carrier, modulator=-1 -> 0.5x carrier

**Implementation**:
```cpp
float calculateModulatedCutoff(float modulator) {
    // modulator in [-1, +1], fmDepth in octaves [0, 6]
    float octaveOffset = modulator * fmDepth_;
    float frequencyMultiplier = std::pow(2.0f, octaveOffset);
    float modulatedFreq = carrierCutoff_ * frequencyMultiplier;

    // Clamp to safe range
    const float maxFreq = static_cast<float>(oversampledRate_) * SVF::kMaxCutoffRatio;
    return std::clamp(modulatedFreq, 20.0f, maxFreq);
}
```

### Latency Reporting

**Decision**: Latency = Oversampler latency (0 for 1x/Economy, varies for FIR modes)

**Rationale**:
- SVF has zero latency (IIR filter)
- Internal oscillator has zero latency
- Only Oversampler adds latency (when using FIR mode)
- For Economy/ZeroLatency mode, latency = 0

**Implementation**:
```cpp
[[nodiscard]] size_t getLatency() const noexcept {
    if (oversamplingFactor_ == 1) {
        return 0;
    }
    return oversampler_.getLatency();
}
```

---

## Data Model (Phase 1)

### Enumerations

```cpp
/// @brief Modulation source selection
enum class FMModSource : uint8_t {
    Internal = 0,  ///< Built-in wavetable oscillator
    External = 1,  ///< External modulator input (sidechain)
    Self = 2       ///< Filter output feedback (self-modulation)
};

/// @brief Filter type selection (maps to SVFMode)
enum class FMFilterType : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass
    Highpass = 1,  ///< 12 dB/oct highpass
    Bandpass = 2,  ///< Constant 0 dB peak bandpass
    Notch = 3      ///< Band-reject filter
};

/// @brief Internal oscillator waveform
enum class FMWaveform : uint8_t {
    Sine = 0,      ///< Pure sine wave (lowest THD)
    Triangle = 1,  ///< Triangle wave (low THD)
    Sawtooth = 2,  ///< Sawtooth wave (bright, harmonic-rich)
    Square = 3     ///< Square wave (hollow, odd harmonics)
};
```

### Class Members

```cpp
class AudioRateFilterFM {
private:
    // Configuration
    double baseSampleRate_ = 44100.0;
    double oversampledRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    int oversamplingFactor_ = 1;
    bool prepared_ = false;

    // Carrier filter parameters
    float carrierCutoff_ = 1000.0f;  // Carrier cutoff in Hz [20, sr*0.495*factor]
    float carrierQ_ = SVF::kButterworthQ;  // Q [0.5, 20.0]
    FMFilterType filterType_ = FMFilterType::Lowpass;

    // Modulator parameters
    FMModSource modSource_ = FMModSource::Internal;
    float modulatorFreq_ = 440.0f;   // Hz [0.1, 20000]
    FMWaveform waveform_ = FMWaveform::Sine;
    float fmDepth_ = 1.0f;           // Octaves [0, 6]

    // Internal oscillator state
    double phase_ = 0.0;             // [0, 1)
    double phaseIncrement_ = 0.0;    // per sample

    // Wavetables (pre-computed in prepare)
    static constexpr size_t kWavetableSize = 2048;
    std::array<float, kWavetableSize> sineTable_;
    std::array<float, kWavetableSize> triangleTable_;
    std::array<float, kWavetableSize> sawTable_;
    std::array<float, kWavetableSize> squareTable_;

    // Self-modulation state
    float previousOutput_ = 0.0f;

    // Composed components
    SVF svf_;
    Oversampler<2, 1> oversampler2x_;
    Oversampler<4, 1> oversampler4x_;

    // Pre-allocated buffers
    std::vector<float> oversampledBuffer_;
    std::vector<float> modulatorBuffer_;
};
```

### API Contract

```cpp
// Lifecycle
void prepare(double sampleRate, size_t maxBlockSize);
void reset() noexcept;

// Carrier configuration
void setCarrierCutoff(float hz);
void setCarrierQ(float q);
void setFilterType(FMFilterType type);

// Modulator configuration
void setModulatorSource(FMModSource source);
void setModulatorFrequency(float hz);
void setModulatorWaveform(FMWaveform waveform);

// FM depth
void setFMDepth(float octaves);

// Oversampling
void setOversamplingFactor(int factor);
[[nodiscard]] size_t getLatency() const noexcept;

// Processing
[[nodiscard]] float process(float input, float externalModulator = 0.0f) noexcept;
void processBlock(float* buffer, const float* modulator, size_t numSamples) noexcept;
void processBlock(float* buffer, size_t numSamples) noexcept;

// Query
[[nodiscard]] bool isPrepared() const noexcept;
[[nodiscard]] float getCarrierCutoff() const noexcept;
[[nodiscard]] float getCarrierQ() const noexcept;
[[nodiscard]] FMFilterType getFilterType() const noexcept;
[[nodiscard]] FMModSource getModulatorSource() const noexcept;
[[nodiscard]] float getModulatorFrequency() const noexcept;
[[nodiscard]] FMWaveform getModulatorWaveform() const noexcept;
[[nodiscard]] float getFMDepth() const noexcept;
[[nodiscard]] int getOversamplingFactor() const noexcept;
```

---

## Testing Strategy

### Test Categories

1. **Foundational Tests** (enum values, constants, lifecycle)
2. **Parameter Tests** (setters/getters, clamping, validation)
3. **Processing Tests** (FM algorithm, filter behavior)
4. **Modulation Source Tests** (Internal, External, Self)
5. **Oversampling Tests** (aliasing reduction, latency)
6. **Edge Cases** (NaN/Inf, denormals, extreme parameters)
7. **Performance Tests** (SC-010: 512 samples @ 4x in < 2ms)
8. **Success Criteria Tests** (SC-001 through SC-012)

### Key Test Cases

| Test | Requirement | Measurement |
|------|-------------|-------------|
| FM depth 0 = static filter | SC-001 | Output matches SVF within 0.01dB |
| Sine oscillator THD | SC-002 | FFT analysis, THD < 0.1% |
| Triangle oscillator THD | SC-002 | FFT analysis, THD < 1% |
| 2x oversampling aliasing | SC-003 | 10kHz mod, aliases < -20dB |
| 4x oversampling aliasing | SC-004 | 10kHz mod, aliases < -40dB |
| External mod +1.0 = 2x cutoff | SC-005 | Verify cutoff = 2 * carrier (1%) |
| External mod -1.0 = 0.5x cutoff | SC-006 | Verify cutoff = 0.5 * carrier (1%) |
| Self-mod stability | SC-007 | 10s processing, no NaN/runaway |
| Lowpass attenuation | SC-008 | 10kHz @ 1kHz cutoff, > 22dB down |
| Bandpass peak gain | SC-009 | Peak within 1dB of unity |
| Performance | SC-010 | 512 @ 4x < 2ms |
| Latency accuracy | SC-011 | Reported matches measured +/- 1 |
| Test coverage | SC-012 | 100% public method coverage |

---

## Implementation Order

### Phase 1: Enumerations and Class Skeleton
1. Define FMModSource, FMFilterType, FMWaveform enums
2. Define AudioRateFilterFM class with all members
3. Implement lifecycle (prepare, reset)
4. Implement getters/setters with clamping

### Phase 2: Wavetable Oscillator
1. Implement wavetable generation in prepare()
2. Implement linear interpolation read
3. Implement phase increment calculation
4. Test oscillator waveform quality (SC-002)

### Phase 3: FM Algorithm (No Oversampling)
1. Implement FM cutoff calculation
2. Implement process() for Internal source
3. Implement process() for External source
4. Test FM depth behavior (SC-001, SC-005, SC-006)

### Phase 4: Self-Modulation
1. Implement self-modulation with hard clipping
2. Test stability (SC-007)
3. Test decay to silence

### Phase 5: Oversampling Integration
1. Integrate Oversampler for 2x/4x
2. Implement factor clamping
3. Test aliasing reduction (SC-003, SC-004)
4. Test latency reporting (SC-011)

### Phase 6: Filter Types and Edge Cases
1. Test all filter types (SC-008, SC-009)
2. Implement edge case handling (NaN, Inf, denormals)
3. Performance testing (SC-010)
4. Final test coverage verification (SC-012)

---

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/audio_rate_filter_fm.h` | Header-only implementation |
| `dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp` | Catch2 test file |
| `specs/_architecture_/layer-2-processors.md` | Update with new component |

---

## Quick Reference

### Include Pattern
```cpp
#include <krate/dsp/processors/audio_rate_filter_fm.h>
```

### Usage Example
```cpp
AudioRateFilterFM fm;
fm.prepare(44100.0, 512);
fm.setCarrierCutoff(1000.0f);
fm.setCarrierQ(8.0f);
fm.setFilterType(FMFilterType::Lowpass);
fm.setModulatorSource(FMModSource::Internal);
fm.setModulatorFrequency(440.0f);
fm.setModulatorWaveform(FMWaveform::Sine);
fm.setFMDepth(2.0f);  // 2 octaves
fm.setOversamplingFactor(2);

// Process
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = fm.process(input[i]);
}
```

### External Modulation Example
```cpp
fm.setModulatorSource(FMModSource::External);
fm.setFMDepth(1.0f);

// Process with external modulator (e.g., another oscillator)
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = fm.process(input[i], modulator[i]);
}
```

### Self-Modulation Example
```cpp
fm.setModulatorSource(FMModSource::Self);
fm.setFMDepth(2.0f);

// Process - filter output feeds back to modulate cutoff
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = fm.process(input[i]);
}
```
