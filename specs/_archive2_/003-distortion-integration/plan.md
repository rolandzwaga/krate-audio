# Implementation Plan: Distortion Integration

**Spec ID**: 003-distortion-integration
**Version**: 1.1
**Date**: 2026-01-28
**Author**: Claude Opus 4.5

**Source Documents Consulted**:
- `specs/Disrumpo/specs-overview.md` - Core requirements (FR-DIST-*)
- `specs/Disrumpo/plans-overview.md` - System architecture, signal flow diagrams
- `specs/Disrumpo/roadmap.md` - Task details T3.1-T3.13, M2 milestone
- `specs/Disrumpo/dsp-details.md` - Parameter structures, DSP algorithms

---

## 1. Architecture Overview

### 1.1 Component Relationships

```
+-------------------+
|   BandProcessor   |  <-- Per-band processing context (from 002-band-management)
+-------------------+
        |
        v
+-------------------+     +---------------------+
| DistortionAdapter |---->| KrateDSP Processors |
+-------------------+     +---------------------+
        |                       |
        |                       +-- SaturationProcessor
        |                       +-- TubeStage
        |                       +-- TapeSaturator
        |                       +-- FuzzProcessor
        |                       +-- WavefolderProcessor
        |                       +-- BitcrusherProcessor
        |                       +-- SpectralDistortion
        |                       +-- (... 19 more types)
        |
        v
+-------------------+     +-------------------+
| Oversampler<N,1>  |     |    DCBlocker      |
+-------------------+     +-------------------+
```

### 1.2 Data Flow per Band

```
Band Input
    |
    v
+-------------------+
| Sweep Intensity   |  <- Placeholder for 005-sweep-system
| Multiply          |     (input *= sweepIntensity, default 1.0)
+-------------------+
    |
    v
+-------------------------------------------------------+
| Drive Gate (drive == 0.0?)                            |
|   YES --> bypass directly to Mix Stage (passthrough)  |
|   NO  --> continue to oversampling path               |
+-------------------------------------------------------+
    |
    v
+-------------------+
| Oversampler Up    |  Factor: 1x/2x/4x/8x (type-dependent)
+-------------------+
    |
    v
+-------------------+
| DistortionAdapter |  Process at oversampled rate
| (processRaw)      |  Block-based types use ring buffers
+-------------------+
    |
    v
+-------------------+
| DC Blocker        |  Only for asymmetric types
+-------------------+  (D06, D10, D11, D17)
    |
    v
+-------------------+
| Oversampler Down  |  Back to original rate
+-------------------+
    |
    v
+-------------------+
| Tone Filter       |  OnePoleLP on wet signal
+-------------------+
    |
    v
+-------------------+
| Mix Stage         |  dry*(1-mix) + wet*mix
+-------------------+
    |
    v
Band Output
```

### 1.3 Namespace Structure

All new code lives in `namespace Disrumpo`:

```cpp
namespace Disrumpo {
    enum class DistortionType;          // D01-D26
    enum class DistortionCategory;      // 7 categories
    struct DistortionCommonParams;      // drive, mix, toneHz
    struct DistortionParams;            // All type-specific params
    class DistortionAdapter;            // Unified adapter
}
```

KrateDSP components remain in `namespace Krate::DSP`.

---

## 2. File Structure

### 2.1 New Files

```
plugins/Disrumpo/
├── src/
│   └── dsp/
│       ├── distortion_types.h          # Enums, category mappings, helper functions
│       ├── distortion_adapter.h        # DistortionAdapter class declaration
│       └── distortion_adapter.cpp      # DistortionAdapter implementation
└── tests/
    └── unit/
        └── distortion_adapter_test.cpp # Catch2 tests
```

### 2.2 Modified Files

```
plugins/Disrumpo/
├── src/
│   └── dsp/
│       └── band_processor.h/.cpp       # Add DistortionAdapter + Oversampler integration
├── CMakeLists.txt                      # Add new source files
```

---

## 3. KrateDSP Component Interfaces

All processors follow consistent patterns. Documented here for implementation reference.

### 3.1 Standard Processor Pattern

```cpp
// Lifecycle
void prepare(double sampleRate, size_t maxBlockSize) noexcept;
void reset() noexcept;

// Processing
void process(float* buffer, size_t numSamples) noexcept;
float processSample(float input) noexcept;  // Optional

// Parameters
void setParameter(float value) noexcept;
[[nodiscard]] float getParameter() const noexcept;
```

### 3.2 Verified Component Interfaces

| Component | Location | Key Methods |
|-----------|----------|-------------|
| **SaturationProcessor** | `processors/saturation_processor.h` | `prepare(sr, blockSize)`, `process(buf, n)`, `setType(SaturationType)`, `setInputGain(dB)`, `setMix(ratio)` |
| **TubeStage** | `processors/tube_stage.h` | `prepare(sr, blockSize)`, `process(buf, n)`, `setInputGain(dB)`, `setBias(f)`, `setSaturationAmount(f)` |
| **TapeSaturator** | `processors/tape_saturator.h` | `prepare(sr, blockSize)`, `process(buf, n)`, `setModel(TapeModel)`, `setDrive(dB)`, `setSaturation(f)` |
| **FuzzProcessor** | `processors/fuzz_processor.h` | `prepare(sr, blockSize)`, `process(buf, n)`, `setFuzzType(Germanium/Silicon)`, `setFuzz(f)`, `setBias(f)`, `setTone(f)` |
| **WavefolderProcessor** | `processors/wavefolder_processor.h` | `prepare(sr, blockSize)`, `process(buf, n)`, `setModel(Simple/Serge/Buchla259/Lockhart)`, `setFoldAmount(f)`, `setSymmetry(f)` |
| **BitcrusherProcessor** | `processors/bitcrusher_processor.h` | `prepare(sr, blockSize)`, `process(buf, n)`, `setBitDepth(bits)`, `setReductionFactor(f)` |
| **SpectralDistortion** | `processors/spectral_distortion.h` | `prepare(sr, fftSize)`, `processBlock(in, out, n)`, `setMode(Mode)`, `setDrive(f)` |
| **DCBlocker** | `primitives/dc_blocker.h` | `prepare(sr, cutoffHz=10.0f)`, `process(x)`, `processBlock(buf, n)` |
| **OnePoleLP** | `primitives/one_pole.h` | `prepare(sr)`, `setCutoff(hz)`, `process(x)` |
| **Oversampler<F,C>** | `primitives/oversampler.h` | `prepare(sr, blockSize, quality, mode)`, `process(buf, n, callback)`, `getLatency()` |
| **CrossoverLR4** | `processors/crossover_filter.h` | `prepare(sr)`, `setCrossoverFrequency(hz)`, `process(x) -> {low, high}` |

### 3.3 Oversampler Usage Pattern

```cpp
// Per-band mono oversampler
Krate::DSP::Oversampler<4, 1> oversampler_;  // 4x, mono

// In prepare():
oversampler_.prepare(sampleRate, maxBlockSize,
    Krate::DSP::OversamplingQuality::Economy,
    Krate::DSP::OversamplingMode::ZeroLatency);

// In process():
oversampler_.process(buffer, numSamples, [this](float* osBuf, size_t osN) {
    // Process at oversampled rate
    distortionAdapter_.processBlock(osBuf, osBuf, osN);
});
```

### 3.4 Existing Primitives for Experimental Types

| Type | Primitive/Processor | Notes |
|------|---------------------|-------|
| D20 Chaos | `primitives/chaos_waveshaper.h` | Lorenz/Rossler attractors |
| D21 Formant | `processors/formant_distortion.h` | Vowel filtering + saturation |
| D22 Granular | `processors/granular_distortion.h` | Grain-based processing |
| D23 Spectral | `processors/spectral_distortion.h` | FFT-domain distortion |
| D24 Fractal | `processors/fractal_distortion.h` | Iterative waveshaping |
| D25 Stochastic | `primitives/stochastic_shaper.h` | Noise-modulated coefficients |
| D26 Allpass | `processors/allpass_saturator.h` | Resonant saturation |
| D16 Ring | `primitives/ring_saturation.h` | Ring modulation + saturation |
| D13 SampleReduce | `primitives/sample_rate_reducer.h` | Sample rate reduction |
| D19 Bitwise | `primitives/bitwise_mangler.h` | Bit rotation/XOR |

---

## 4. DistortionAdapter Implementation

### 4.1 Class Structure

```cpp
// distortion_adapter.h
#pragma once

#include "distortion_types.h"
#include <krate/dsp/processors/saturation_processor.h>
#include <krate/dsp/processors/tube_stage.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/fuzz_processor.h>
#include <krate/dsp/processors/wavefolder_processor.h>
#include <krate/dsp/processors/bitcrusher_processor.h>
#include <krate/dsp/processors/temporal_distortion.h>
#include <krate/dsp/processors/feedback_distortion.h>
#include <krate/dsp/processors/aliasing_effect.h>
#include <krate/dsp/processors/spectral_distortion.h>
#include <krate/dsp/processors/fractal_distortion.h>
#include <krate/dsp/processors/formant_distortion.h>
#include <krate/dsp/processors/granular_distortion.h>
#include <krate/dsp/processors/allpass_saturator.h>
#include <krate/dsp/primitives/sample_rate_reducer.h>
#include <krate/dsp/primitives/ring_saturation.h>
#include <krate/dsp/primitives/bitwise_mangler.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>
#include <krate/dsp/primitives/stochastic_shaper.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/one_pole.h>

#include <array>

namespace Disrumpo {

class DistortionAdapter {
public:
    void prepare(double sampleRate, int maxBlockSize = 512) noexcept;
    void reset() noexcept;

    void setType(DistortionType type) noexcept;
    [[nodiscard]] DistortionType getType() const noexcept;

    void setCommonParams(const DistortionCommonParams& params) noexcept;
    void setParams(const DistortionParams& params) noexcept;

    [[nodiscard]] float process(float input) noexcept;
    void processBlock(const float* input, float* output, int numSamples) noexcept;

    [[nodiscard]] int getProcessingLatency() const noexcept;

private:
    [[nodiscard]] float processRaw(float input) noexcept;
    [[nodiscard]] float applyTone(float wet) noexcept;
    void updateDCBlockerState() noexcept;
    void routeParamsToProcessor() noexcept;

    // State
    double sampleRate_ = 44100.0;
    DistortionType currentType_ = DistortionType::SoftClip;
    DistortionCommonParams commonParams_;
    DistortionParams typeParams_;
    bool needsDCBlock_ = false;

    // Common processing
    Krate::DSP::OnePoleLP toneFilter_;
    Krate::DSP::DCBlocker dcBlocker_;

    // All processor instances (pre-allocated)
    Krate::DSP::SaturationProcessor saturation_;
    Krate::DSP::TubeStage tube_;
    Krate::DSP::TapeSaturator tape_;
    Krate::DSP::FuzzProcessor fuzz_;
    Krate::DSP::WavefolderProcessor wavefolder_;
    Krate::DSP::BitcrusherProcessor bitcrusher_;
    Krate::DSP::SampleRateReducer srReducer_;
    Krate::DSP::TemporalDistortion temporal_;
    Krate::DSP::RingSaturation ringSaturation_;
    Krate::DSP::FeedbackDistortion feedbackDist_;
    Krate::DSP::AliasingEffect aliasing_;
    Krate::DSP::BitwiseMangler bitwiseMangler_;
    Krate::DSP::ChaosWaveshaper chaos_;
    Krate::DSP::FormantDistortion formant_;
    Krate::DSP::GranularDistortion granular_;
    Krate::DSP::SpectralDistortion spectral_;
    Krate::DSP::FractalDistortion fractal_;
    Krate::DSP::StochasticShaper stochastic_;
    Krate::DSP::AllpassSaturator allpassSaturator_;

    // Block-based buffering for latency-introducing types
    std::array<float, 4096> inputRingBuffer_{};
    std::array<float, 4096> outputRingBuffer_{};
    int ringWritePos_ = 0;
    int ringReadPos_ = 0;
    int blockLatency_ = 0;
    bool isBlockBased_ = false;
};

} // namespace Disrumpo
```

### 4.2 Type-to-Processor Mapping

```cpp
[[nodiscard]] float DistortionAdapter::processRaw(float input) noexcept {
    switch (currentType_) {
        // Saturation (D01-D06)
        case DistortionType::SoftClip:
            saturation_.setType(Krate::DSP::SaturationType::Tape);
            return saturation_.processSample(input);
        case DistortionType::HardClip:
            saturation_.setType(Krate::DSP::SaturationType::Digital);
            return saturation_.processSample(input);
        case DistortionType::Tube:
            return tube_.processSample(input);
        case DistortionType::Tape:
            return tape_.processSample(input);
        case DistortionType::Fuzz:
            fuzz_.setFuzzType(Krate::DSP::FuzzType::Germanium);
            return fuzz_.processSample(input);
        case DistortionType::AsymmetricFuzz:
            fuzz_.setFuzzType(Krate::DSP::FuzzType::Silicon);
            fuzz_.setBias(typeParams_.bias);
            return fuzz_.processSample(input);

        // Wavefold (D07-D09)
        case DistortionType::SineFold:
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Serge);
            return wavefolder_.processSample(input);
        case DistortionType::TriangleFold:
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Simple);
            return wavefolder_.processSample(input);
        case DistortionType::SergeFold:
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Lockhart);
            return wavefolder_.processSample(input);

        // Rectify (D10-D11) - inline implementations
        case DistortionType::FullRectify:
            return std::abs(input);
        case DistortionType::HalfRectify:
            return std::max(0.0f, input);

        // Digital (D12-D14)
        case DistortionType::Bitcrush:
            return bitcrusher_.processSample(input);
        case DistortionType::SampleReduce:
            return srReducer_.process(input);
        case DistortionType::Quantize:
            bitcrusher_.setBitDepth(typeParams_.bitDepth);
            return bitcrusher_.processSample(input);

        // Digital continued (D18-D19)
        case DistortionType::Aliasing:
            return aliasing_.processSample(input);
        case DistortionType::BitwiseMangler:
            return bitwiseMangler_.process(input);

        // Dynamic (D15)
        case DistortionType::Temporal:
            return temporal_.processSample(input);

        // Hybrid (D16-D17, D26)
        case DistortionType::RingSaturation:
            return ringSaturation_.process(input);
        case DistortionType::FeedbackDist:
            return feedbackDist_.processSample(input);
        case DistortionType::AllpassResonant:
            return allpassSaturator_.processSample(input);

        // Experimental (D20-D25)
        case DistortionType::Chaos:
            return chaos_.process(input);
        case DistortionType::Formant:
            return formant_.processSample(input);
        case DistortionType::Granular:
            // Block-based: handled via ring buffer
            return input;
        case DistortionType::Spectral:
            // Block-based: handled via ring buffer
            return input;
        case DistortionType::Fractal:
            return fractal_.processSample(input);
        case DistortionType::Stochastic:
            return stochastic_.process(input);

        default:
            return input;
    }
}
```

### 4.3 DC Blocker State Management

```cpp
void DistortionAdapter::updateDCBlockerState() noexcept {
    // DC blocking required for asymmetric types
    needsDCBlock_ = (currentType_ == DistortionType::AsymmetricFuzz ||
                     currentType_ == DistortionType::FullRectify ||
                     currentType_ == DistortionType::HalfRectify ||
                     currentType_ == DistortionType::FeedbackDist);
}
```

### 4.4 Block-Based Processing with Ring Buffers

```cpp
void DistortionAdapter::processBlock(const float* input, float* output,
                                      int numSamples) noexcept {
    if (!isBlockBased_) {
        // Sample-accurate types: direct processing
        for (int i = 0; i < numSamples; ++i) {
            float wet = processRaw(input[i] * commonParams_.drive);
            if (needsDCBlock_) {
                wet = dcBlocker_.process(wet);
            }
            wet = applyTone(wet);
            output[i] = input[i] * (1.0f - commonParams_.mix) +
                        wet * commonParams_.mix;
        }
    } else {
        // Block-based types (Spectral, Granular): use ring buffers
        // Accumulate input, process when buffer full, drain output
        for (int i = 0; i < numSamples; ++i) {
            // Feed input ring buffer
            inputRingBuffer_[ringWritePos_] = input[i] * commonParams_.drive;
            ringWritePos_ = (ringWritePos_ + 1) % inputRingBuffer_.size();

            // Drain output ring buffer (with latency)
            float wet = outputRingBuffer_[ringReadPos_];
            ringReadPos_ = (ringReadPos_ + 1) % outputRingBuffer_.size();

            if (needsDCBlock_) {
                wet = dcBlocker_.process(wet);
            }
            wet = applyTone(wet);
            output[i] = input[i] * (1.0f - commonParams_.mix) +
                        wet * commonParams_.mix;
        }

        // Process full blocks when accumulated
        // (Spectral/Granular have their own internal block processing)
    }
}
```

---

## 5. Integration with BandProcessor

### 5.1 BandProcessor Structure Update

```cpp
// band_processor.h
#include "distortion_adapter.h"
#include <krate/dsp/primitives/oversampler.h>

class BandProcessor {
public:
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    // Distortion controls
    void setDistortionType(DistortionType type) noexcept;
    void setDistortionCommonParams(const DistortionCommonParams& params) noexcept;
    void setDistortionParams(const DistortionParams& params) noexcept;
    void setOversampleFactor(int factor) noexcept;  // 1, 2, 4, or 8

    // Sweep intensity hook (for 005-sweep-system integration)
    // Default 1.0 = no effect. Set by SweepProcessor based on band frequency.
    void setSweepIntensity(float intensity) noexcept { sweepIntensity_ = intensity; }

private:
    DistortionAdapter distortion_;

    // Oversampler variants (only one active at a time for 2x/4x)
    Krate::DSP::Oversampler<2, 1> oversampler2x_;
    Krate::DSP::Oversampler<4, 1> oversampler4x_;
    // For 8x: cascade 4x + 2x stages (4 * 2 = 8)
    Krate::DSP::Oversampler<2, 1> oversampler8xInner_;  // Inner 2x stage

    int currentOversampleFactor_ = 2;
    int maxOversampleFactor_ = 8;  // Global limit parameter

    // Sweep system hook point (005-sweep-system)
    float sweepIntensity_ = 1.0f;  // Applied before distortion

    std::vector<float> oversampleBuffer_;
};
```

### 5.2 BandProcessor::process Implementation

```cpp
void BandProcessor::process(float* buffer, size_t numSamples) noexcept {
    // Apply sweep intensity (hook for 005-sweep-system)
    // Default 1.0 = no effect
    if (sweepIntensity_ != 1.0f) {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] *= sweepIntensity_;
        }
    }

    const float drive = distortion_.getCommonParams().drive;

    // Drive Gate: bypass distortion path entirely if drive == 0
    if (drive < 0.0001f) {
        // Passthrough: no processing needed
        return;
    }

    // Determine effective oversample factor
    const int effectiveFactor = std::min(currentOversampleFactor_, maxOversampleFactor_);

    if (effectiveFactor == 1) {
        // No oversampling
        distortion_.processBlock(buffer, buffer, static_cast<int>(numSamples));
    } else if (effectiveFactor == 2) {
        oversampler2x_.process(buffer, numSamples,
            [this](float* osBuf, size_t osN) {
                distortion_.processBlock(osBuf, osBuf, static_cast<int>(osN));
            });
    } else if (effectiveFactor == 4) {
        oversampler4x_.process(buffer, numSamples,
            [this](float* osBuf, size_t osN) {
                distortion_.processBlock(osBuf, osBuf, static_cast<int>(osN));
            });
    } else if (effectiveFactor == 8) {
        // 8x: cascade 4x outer * 2x inner = 8x total
        oversampler4x_.process(buffer, numSamples,
            [this](float* os4Buf, size_t os4N) {
                // At 4x rate, apply inner 2x stage for 8x total
                oversampler8xInner_.process(os4Buf, os4N,
                    [this](float* os8Buf, size_t os8N) {
                        distortion_.processBlock(os8Buf, os8Buf, static_cast<int>(os8N));
                    });
            });
    }
}
```

---

## 6. Oversampling Strategy

### 6.1 Per-Type Recommendations (from spec)

| Type | Factor | Rationale |
|------|--------|-----------|
| D01 Soft Clip | 2x | Mild harmonics |
| D02 Hard Clip | 4x | Strong harmonics |
| D03 Tube | 2x | Moderate harmonics |
| D04 Tape | 2x | Moderate harmonics |
| D05-D06 Fuzz | 4x | Many harmonics |
| D07-D09 Wavefolders | 4x | Many harmonics |
| D10-D11 Rectifiers | 4x | Frequency doubling |
| D12-D14 Digital | 1x | Aliasing intentional |
| D15 Temporal | 2x | Moderate harmonics |
| D16 Ring Saturation | 4x | Inharmonic sidebands |
| D17 Feedback | 2x | Controlled by limiter |
| D18-D19 Digital | 1x | Artifacts are the effect |
| D20-D22 Experimental | 2x | Varies |
| D23 Spectral | 1x | FFT domain |
| D24-D25 Experimental | 2x | Varies |
| D26 Allpass Resonant | 4x | Self-oscillation |

### 6.2 Implementation: Auto-Select Based on Type

```cpp
// distortion_types.h
constexpr int getRecommendedOversample(DistortionType type) noexcept {
    switch (type) {
        // 4x types
        case DistortionType::HardClip:
        case DistortionType::Fuzz:
        case DistortionType::AsymmetricFuzz:
        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
        case DistortionType::RingSaturation:
        case DistortionType::AllpassResonant:
            return 4;

        // 1x types (aliasing is the effect)
        case DistortionType::Bitcrush:
        case DistortionType::SampleReduce:
        case DistortionType::Quantize:
        case DistortionType::Aliasing:
        case DistortionType::BitwiseMangler:
        case DistortionType::Spectral:
            return 1;

        // 2x types (default)
        default:
            return 2;
    }
}
```

---

## 7. Testing Strategy

### 7.1 Unit Tests (distortion_adapter_test.cpp)

```cpp
// Test categories based on spec requirements

// UT-DI-001: All 26 types process audio
TEST_CASE("All distortion types produce non-zero output", "[distortion][unit]") {
    DistortionAdapter adapter;
    adapter.prepare(44100.0, 512);

    for (int i = 0; i < static_cast<int>(DistortionType::COUNT); ++i) {
        auto type = static_cast<DistortionType>(i);
        adapter.setType(type);

        float testSignal = 0.5f;
        float output = adapter.process(testSignal);

        CAPTURE(getTypeName(type));
        REQUIRE(output != 0.0f);
    }
}

// UT-DI-002: Type switching
TEST_CASE("Type switching activates correct processor", "[distortion][unit]") {
    DistortionAdapter adapter;
    adapter.prepare(44100.0, 512);

    // Saturation types should sound different
    adapter.setType(DistortionType::SoftClip);
    float softOutput = adapter.process(0.8f);

    adapter.setType(DistortionType::HardClip);
    float hardOutput = adapter.process(0.8f);

    REQUIRE(softOutput != hardOutput);
}

// UT-DI-003: Drive parameter
TEST_CASE("Drive parameter affects output level", "[distortion][unit]") {
    DistortionAdapter adapter;
    adapter.prepare(44100.0, 512);
    adapter.setType(DistortionType::SoftClip);

    DistortionCommonParams params;
    params.drive = 1.0f;
    params.mix = 1.0f;
    adapter.setCommonParams(params);
    float lowDriveOut = adapter.process(0.5f);

    params.drive = 5.0f;
    adapter.setCommonParams(params);
    float highDriveOut = adapter.process(0.5f);

    // Higher drive = more saturation = different output
    REQUIRE(std::abs(highDriveOut) != std::abs(lowDriveOut));
}

// UT-DI-004: Mix parameter
TEST_CASE("Mix parameter blends dry/wet correctly", "[distortion][unit]") {
    DistortionAdapter adapter;
    adapter.prepare(44100.0, 512);
    adapter.setType(DistortionType::SoftClip);

    float input = 0.5f;

    DistortionCommonParams params;
    params.drive = 5.0f;
    params.mix = 0.0f;  // Full dry
    adapter.setCommonParams(params);
    float dryOut = adapter.process(input);
    REQUIRE(dryOut == Catch::Approx(input).margin(0.001f));

    params.mix = 1.0f;  // Full wet
    adapter.setCommonParams(params);
    float wetOut = adapter.process(input);
    REQUIRE(wetOut != Catch::Approx(input).margin(0.001f));
}

// UT-DI-009: Drive=0 passthrough
TEST_CASE("Drive=0 produces passthrough", "[distortion][unit]") {
    DistortionAdapter adapter;
    adapter.prepare(44100.0, 512);
    adapter.setType(DistortionType::SoftClip);

    DistortionCommonParams params;
    params.drive = 0.0f;
    params.mix = 1.0f;
    adapter.setCommonParams(params);

    float input = 0.7f;
    float output = adapter.process(input);

    REQUIRE(output == Catch::Approx(input).margin(0.001f));
}

// UT-DI-010: Block-based latency reporting
TEST_CASE("Block-based types report correct latency", "[distortion][unit]") {
    DistortionAdapter adapter;
    adapter.prepare(44100.0, 2048);

    // Sample-accurate types: 0 latency
    adapter.setType(DistortionType::SoftClip);
    REQUIRE(adapter.getProcessingLatency() == 0);

    // Spectral: FFT size latency
    adapter.setType(DistortionType::Spectral);
    REQUIRE(adapter.getProcessingLatency() > 0);
}
```

### 7.2 Integration Tests

```cpp
// IT-DI-001: Full signal path
TEST_CASE("Audio flows through bands with distortion", "[distortion][integration]") {
    // Create full Disrumpo processor
    // Send test signal through 4 bands
    // Verify non-zero output
}

// IT-DI-002: Band type independence
TEST_CASE("Different types per band work independently", "[distortion][integration]") {
    // Set Band 1: SoftClip
    // Set Band 2: HardClip
    // Verify each band has distinct character
}
```

### 7.3 Performance Tests

```cpp
// PT-DI-002: CPU target
TEST_CASE("CPU usage under 5% with 4 bands 4x OS", "[distortion][performance]") {
    // Setup: 4 bands, 4x oversampling, single type per band
    // Measure: Process 10 seconds of audio
    // Target: < 5% CPU @ 44.1kHz

    auto start = std::chrono::high_resolution_clock::now();
    // ... process ...
    auto end = std::chrono::high_resolution_clock::now();

    double realTimeMs = 10000.0;  // 10 seconds
    double processingMs = std::chrono::duration<double, std::milli>(end - start).count();
    double cpuPercent = (processingMs / realTimeMs) * 100.0;

    REQUIRE(cpuPercent < 5.0);
}
```

---

## 8. Build Integration

### 8.1 CMakeLists.txt Updates

```cmake
# plugins/Disrumpo/CMakeLists.txt

target_sources(${DISRUMPO_TARGET} PRIVATE
    # Existing files...

    # New distortion files
    src/dsp/distortion_types.h
    src/dsp/distortion_adapter.h
    src/dsp/distortion_adapter.cpp
)

# Link KrateDSP
target_link_libraries(${DISRUMPO_TARGET} PRIVATE
    KrateDSP
)

# Tests
if(BUILD_TESTING)
    add_executable(disrumpo_distortion_tests
        tests/unit/distortion_adapter_test.cpp
    )
    target_link_libraries(disrumpo_distortion_tests PRIVATE
        Catch2::Catch2WithMain
        KrateDSP
    )
    add_test(NAME DisrumpoDistortionTests COMMAND disrumpo_distortion_tests)
endif()
```

---

## 9. Implementation Tasks Summary

| Task | Priority | Effort | Dependencies |
|------|----------|--------|--------------|
| T3.1: DistortionAdapter interface | P0 | 8h | 002 complete |
| T3.2: DistortionType enum | P0 | 4h | T3.1 |
| T3.3: Saturation types D01-D06 | P0 | 8h | T3.1, T3.2 |
| T3.4: Wavefold types D07-D09 | P0 | 4h | T3.1, T3.2 |
| T3.5: Rectify types D10-D11 | P0 | 2h | T3.1, T3.2 |
| T3.6: Digital types D12-D14 | P0 | 4h | T3.1, T3.2 |
| T3.7: Digital types D18-D19 | P0 | 4h | T3.1, T3.2 |
| T3.8: Dynamic type D15 | P0 | 2h | T3.1, T3.2 |
| T3.9: Hybrid types D16-D17, D26 | P0 | 6h | T3.1, T3.2 |
| T3.10: Experimental D20-D25 | P0 | 8h | T3.1, T3.2 |
| T3.11: Common parameter mapping | P0 | 4h | T3.3-T3.10 |
| T3.12: Oversampler integration | P0 | 6h | T3.11 |
| T3.13: Band wiring | P0 | 4h | T3.12 |

**Total Estimated Effort**: 64 hours

---

## 10. Constitution Compliance

### 10.1 Pre-Design Check

| Principle | Status | Evidence |
|-----------|--------|----------|
| I. VST3 Architecture | N/A | DSP layer only, no processor/controller changes |
| II. Real-Time Safety | Compliant | All `process()` methods noexcept, pre-allocated buffers |
| III. Modern C++ | Compliant | C++20, RAII, smart pointers where applicable |
| IV. SIMD/DSP | Compliant | Sequential memory access, minimal branching |
| V. VSTGUI | N/A | No UI in this spec |
| VI. Cross-Platform | Compliant | No platform-specific code |
| IX. Layered DSP | Compliant | Adapter wraps Layer 1-2 components |
| X. DSP Constraints | Compliant | DC blocking, oversampling, feedback limiting |
| XV. ODR Prevention | Compliant | Unique `Disrumpo` namespace |
| XVI. Honest Completion | Pending | Final verification at implementation |

### 10.2 Post-Design Verification

To be completed after implementation. Must verify:
- [ ] All 26 types process audio (UT-DI-001)
- [ ] CPU < 5% target met (PT-DI-002)
- [ ] No allocations in process paths
- [ ] All tests pass

---

## 11. Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-28 | Claude Opus 4.5 | Initial plan creation |
| 1.1 | 2026-01-28 | Claude Opus 4.5 | Added sweep intensity multiply to signal flow (per plans-overview.md), added source documents reference, added sweepIntensity_ hook in BandProcessor for 005-sweep-system integration |
