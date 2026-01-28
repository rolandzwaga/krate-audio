# Implementation Spec: Distortion Integration

**Spec ID**: 003-distortion-integration
**Version**: 1.1
**Date**: 2026-01-28
**Status**: Ready for Implementation
**Prerequisites**: 001-plugin-skeleton (complete), 002-band-management (complete)

**Source Documents Consulted**:
- spec.md: FR-DIST-001/002/003, distortion types D01-D26, success criteria
- plan.md: System architecture, signal flow, per-band processing diagram
- tasks.md: Week 3 task breakdown (T3.1-T3.6 condensed)
- roadmap.md: Detailed tasks T3.1-T3.13, M2 milestone criteria
- dsp-details.md: DistortionAdapter interface, DistortionType enum, parameter structures

---

## 1. Executive Summary

This spec covers the integration of all 26 distortion types from KrateDSP into the Disrumpo plugin. The DistortionAdapter provides a unified interface to the various KrateDSP processors, enabling each frequency band to apply any distortion type with common parameters (Drive, Mix, Tone) plus type-specific parameters.

### Scope

| Included | Excluded |
|----------|----------|
| DistortionAdapter unified interface | Morph system (004-morph-system) |
| DistortionType enum and category mapping | Sweep system (005-sweep-system) |
| All 26 distortion types (D01-D26) | UI (004-vstgui-infrastructure) |
| Common parameter mapping (Drive, Mix, Tone) | Modulation system (later spec) |
| Oversampler integration per band | |
| Wiring distortion to bands (single type) | |

### Success Criteria (M2 Milestone)

From roadmap.md:
- [ ] All 26 distortion types process audio correctly
- [ ] Common parameters (Drive, Mix, Tone) work
- [ ] Oversampling works (1x, 2x, 4x, 8x)
- [ ] CPU < 5% @ 4 bands, 4x OS, single type

---

## 2. Prerequisites

### Required Completions

| Spec | Status | Required For |
|------|--------|--------------|
| 001-plugin-skeleton | Complete | Processor/Controller scaffolding, parameter system |
| 002-band-management | Complete | CrossoverNetwork, BandState, per-band routing |

### KrateDSP Components Available

All required processors exist in `dsp/include/krate/dsp/processors/`:

| Type ID | KrateDSP Component | Header |
|---------|-------------------|--------|
| D01-D02 | SaturationProcessor | saturation_processor.h |
| D03 | TubeStage | tube_stage.h |
| D04 | TapeSaturator | tape_saturator.h |
| D05-D06 | FuzzProcessor | fuzz_processor.h |
| D07-D09 | WavefolderProcessor | wavefolder_processor.h |
| D12-D14 | BitcrusherProcessor | bitcrusher_processor.h |
| D13 | SampleRateReducer | sample_rate_reducer.h (primitives) |
| D15 | TemporalDistortion | temporal_distortion.h |
| D16 | RingSaturation | ring_saturation.h (primitives) |
| D17 | FeedbackDistortion | feedback_distortion.h |
| D18 | AliasingEffect | aliasing_effect.h |
| D19 | BitwiseMangler | bitwise_mangler.h (primitives) |
| D20 | ChaosWaveshaper | chaos_waveshaper.h (primitives) |
| D21 | FormantDistortion | formant_distortion.h |
| D22 | GranularDistortion | granular_distortion.h |
| D23 | SpectralDistortion | spectral_distortion.h |
| D24 | FractalDistortion | fractal_distortion.h |
| D25 | StochasticShaper | stochastic_shaper.h (primitives) |
| D26 | AllpassSaturator | allpass_saturator.h |

Primitives available in `dsp/include/krate/dsp/primitives/`:
- Oversampler: oversampler.h
- DCBlocker: dc_blocker.h

---

## 3. Functional Requirements

### FR-DI-001: DistortionType Enum

The system MUST define a DistortionType enum covering all 26 types with category groupings.

**Categories**:
| Category | Types | Count |
|----------|-------|-------|
| Saturation | D01-D06 | 6 |
| Wavefold | D07-D09 | 3 |
| Rectify | D10-D11 | 2 |
| Digital | D12-D14, D18-D19 | 5 |
| Dynamic | D15 | 1 |
| Hybrid | D16-D17, D26 | 3 |
| Experimental | D20-D25 | 6 |

### FR-DI-002: DistortionAdapter Interface

The system MUST provide a unified DistortionAdapter class that:

- Accepts any DistortionType via `setType()`
- Provides common interface `process(float input)` for all types
- Prepares all internal processors during `prepare(sampleRate)`
- Resets all internal state via `reset()`
- Operates without allocations after `prepare()` (real-time safe)

### FR-DI-003: Common Parameters

The system MUST implement these common parameters for ALL distortion types:

| Parameter | Range | Default | VST Parameter |
|-----------|-------|---------|---------------|
| Drive | 0.0 - 10.0 | 1.0 | Yes |
| Mix | 0% - 100% | 100% | Yes |
| Tone | 200 Hz - 8000 Hz | 4000 Hz | Yes |

**Behavior**:
- Drive: Scales input level before distortion
- Mix: Blends dry/wet output (parallel processing)
- Tone: Post-distortion tone filter (lowpass, affects only wet signal)

### FR-DI-004: Type-Specific Parameters

The system MUST implement all type-specific parameters defined in spec.md FR-DIST-003 and Appendix A. These are delivered to the adapter via a single `setParams(const DistortionParams&)` call. The `DistortionParams` struct (section 4.3) is the authoritative parameter definition covering all categories:

| Category | Parameters |
|----------|------------|
| Saturation | Bias (-1.0 to +1.0), Sag (0.0 to 1.0) |
| Wavefold | Folds (1-8), Shape (0.0-1.0), Symmetry (0.0-1.0) |
| Digital | Bit Depth (1.0-16.0), Sample Rate Ratio (1.0-32.0), Smoothness (0.0-1.0) |
| Dynamic | Sensitivity (0.0-1.0), Attack (1-100ms), Release (10-500ms), Mode (Envelope/Inverse/Derivative) |
| Hybrid | Feedback (0.0-1.5), Delay (1-100ms), Stages (1-4), Mod Depth (0.0-1.0) |
| Aliasing | Frequency Shift (-1000 to +1000 Hz) |
| Bitwise | Rotate Amount (-16 to +16), XOR Pattern (0x0000-0xFFFF) |
| Experimental | Chaos Amount (0.0-1.0), Attractor Speed (0.1-10.0), Grain Size (5-100ms), Formant Shift (-12 to +12 st) |
| Spectral | FFT Size (512-4096), Magnitude Bits (1-16) |
| Fractal | Iterations (1-8), Scale Factor (0.3-0.9), Frequency Decay (0-1) |
| Stochastic | Jitter Amount (0-1), Jitter Rate (0.1-100 Hz), Coefficient Noise (0-1) |
| Allpass Resonant | Resonant Frequency (20-2000 Hz), Allpass Feedback (0-0.99), Decay Time (0.01-10 s) |

### FR-DI-005: Per-Band Oversampler

The system MUST integrate an Oversampler per band with:

- Factor selection: 1x, 2x, 4x, 8x
- Global maximum limit parameter
- Intelligent factor selection based on distortion type (per FR-OS-003 in spec.md)

**Oversampling Recommendations** (from spec.md FR-OS-003):

| Type | Factor | Rationale |
|------|--------|-----------|
| Soft Clip (D01) | 2x | Mild harmonics |
| Hard Clip, Fuzz (D02, D05-D06) | 4x | Strong harmonics |
| Tube, Tape (D03-D04) | 2x | Moderate harmonics |
| Wavefolders (D07-D09) | 4x | Many harmonics |
| Rectifiers (D10-D11) | 4x | Frequency doubling |
| Digital (D12-D14) | 1x | Aliasing intentional |
| Temporal (D15) | 2x | Moderate harmonics |
| Ring Saturation (D16) | 4x | Inharmonic sidebands |
| Feedback (D17) | 2x | Controlled by limiter |
| Aliasing (D18), Bitwise (D19) | 1x | Artifacts are the effect |
| Chaos (D20), Formant (D21), Granular (D22) | 2x | Moderate, varies |
| Spectral (D23) | 1x | FFT domain processing |
| Fractal (D24), Stochastic (D25) | 2x | Varies by settings |
| Allpass Resonant (D26) | 4x | Self-oscillation potential |

### FR-DI-006: Band Distortion Wiring

The system MUST wire the DistortionAdapter to each band such that:

- Each band has one DistortionAdapter instance
- Band input flows through: Input -> Oversampler Up -> Distortion -> Oversampler Down -> Band Output
- Distortion type is configurable per band via parameter

### FR-DI-007: DC Blocking

The system MUST include DC blocking after asymmetric distortion types:
- D06 (Asymmetric Fuzz)
- D10-D11 (Rectifiers)
- D17 (Feedback Distortion)

Use DCBlocker from KrateDSP primitives with ~5Hz corner frequency.

---

## 4. Technical Design

### 4.1 File Structure

```
plugins/Disrumpo/
├── src/
│   └── dsp/
│       ├── distortion_types.h      # DistortionType enum, constants
│       ├── distortion_adapter.h    # DistortionAdapter class declaration
│       ├── distortion_adapter.cpp  # DistortionAdapter implementation
│       └── band_processor.h/.cpp   # Updated to include distortion
└── tests/
    └── unit/
        └── distortion_adapter_test.cpp
```

### 4.2 DistortionType Enum

```cpp
// plugins/Disrumpo/src/dsp/distortion_types.h
#pragma once

#include <cstdint>

namespace Disrumpo {

enum class DistortionType : uint8_t {
    // Saturation (D01-D06)
    SoftClip = 0,       // D01
    HardClip,           // D02
    Tube,               // D03
    Tape,               // D04
    Fuzz,               // D05
    AsymmetricFuzz,     // D06

    // Wavefold (D07-D09)
    SineFold,           // D07
    TriangleFold,       // D08
    SergeFold,          // D09

    // Rectify (D10-D11)
    FullRectify,        // D10
    HalfRectify,        // D11

    // Digital (D12-D14, D18-D19)
    Bitcrush,           // D12
    SampleReduce,       // D13
    Quantize,           // D14
    Aliasing,           // D18
    BitwiseMangler,     // D19

    // Dynamic (D15)
    Temporal,           // D15

    // Hybrid (D16-D17, D26)
    RingSaturation,     // D16
    FeedbackDist,       // D17
    AllpassResonant,    // D26

    // Experimental (D20-D25)
    Chaos,              // D20
    Formant,            // D21
    Granular,           // D22
    Spectral,           // D23
    Fractal,            // D24
    Stochastic,         // D25

    COUNT               // Total count for iteration
};

/// @brief Get the number of distortion types
constexpr int kDistortionTypeCount = static_cast<int>(DistortionType::COUNT);

/// @brief Category groupings for UI/morphing
enum class DistortionCategory : uint8_t {
    Saturation = 0,
    Wavefold,
    Rectify,
    Digital,
    Dynamic,
    Hybrid,
    Experimental
};

/// @brief Get category for a distortion type
constexpr DistortionCategory getCategory(DistortionType type) noexcept {
    switch (type) {
        case DistortionType::SoftClip:
        case DistortionType::HardClip:
        case DistortionType::Tube:
        case DistortionType::Tape:
        case DistortionType::Fuzz:
        case DistortionType::AsymmetricFuzz:
            return DistortionCategory::Saturation;

        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
            return DistortionCategory::Wavefold;

        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
            return DistortionCategory::Rectify;

        case DistortionType::Bitcrush:
        case DistortionType::SampleReduce:
        case DistortionType::Quantize:
        case DistortionType::Aliasing:
        case DistortionType::BitwiseMangler:
            return DistortionCategory::Digital;

        case DistortionType::Temporal:
            return DistortionCategory::Dynamic;

        case DistortionType::RingSaturation:
        case DistortionType::FeedbackDist:
        case DistortionType::AllpassResonant:
            return DistortionCategory::Hybrid;

        case DistortionType::Chaos:
        case DistortionType::Formant:
        case DistortionType::Granular:
        case DistortionType::Spectral:
        case DistortionType::Fractal:
        case DistortionType::Stochastic:
            return DistortionCategory::Experimental;

        default:
            return DistortionCategory::Saturation;
    }
}

/// @brief Get recommended oversampling factor for a distortion type
constexpr int getRecommendedOversample(DistortionType type) noexcept {
    switch (type) {
        case DistortionType::SoftClip:
        case DistortionType::Tube:
        case DistortionType::Tape:
        case DistortionType::Temporal:
        case DistortionType::FeedbackDist:
        case DistortionType::Chaos:
        case DistortionType::Formant:
        case DistortionType::Granular:
        case DistortionType::Fractal:
        case DistortionType::Stochastic:
            return 2;

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

        case DistortionType::Bitcrush:
        case DistortionType::SampleReduce:
        case DistortionType::Quantize:
        case DistortionType::Aliasing:
        case DistortionType::BitwiseMangler:
        case DistortionType::Spectral:
            return 1;

        default:
            return 2;
    }
}

/// @brief Get display name for a distortion type
constexpr const char* getTypeName(DistortionType type) noexcept {
    switch (type) {
        case DistortionType::SoftClip: return "Soft Clip";
        case DistortionType::HardClip: return "Hard Clip";
        case DistortionType::Tube: return "Tube";
        case DistortionType::Tape: return "Tape";
        case DistortionType::Fuzz: return "Fuzz";
        case DistortionType::AsymmetricFuzz: return "Asymmetric Fuzz";
        case DistortionType::SineFold: return "Sine Fold";
        case DistortionType::TriangleFold: return "Triangle Fold";
        case DistortionType::SergeFold: return "Serge Fold";
        case DistortionType::FullRectify: return "Full Rectify";
        case DistortionType::HalfRectify: return "Half Rectify";
        case DistortionType::Bitcrush: return "Bitcrush";
        case DistortionType::SampleReduce: return "Sample Reduce";
        case DistortionType::Quantize: return "Quantize";
        case DistortionType::Aliasing: return "Aliasing";
        case DistortionType::BitwiseMangler: return "Bitwise Mangler";
        case DistortionType::Temporal: return "Temporal";
        case DistortionType::RingSaturation: return "Ring Saturation";
        case DistortionType::FeedbackDist: return "Feedback";
        case DistortionType::AllpassResonant: return "Allpass Resonant";
        case DistortionType::Chaos: return "Chaos";
        case DistortionType::Formant: return "Formant";
        case DistortionType::Granular: return "Granular";
        case DistortionType::Spectral: return "Spectral";
        case DistortionType::Fractal: return "Fractal";
        case DistortionType::Stochastic: return "Stochastic";
        default: return "Unknown";
    }
}

} // namespace Disrumpo
```

### 4.3 DistortionAdapter Class

```cpp
// plugins/Disrumpo/src/dsp/distortion_adapter.h
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

namespace Disrumpo {

/// @brief Common parameters applicable to all distortion types.
struct DistortionCommonParams {
    float drive = 1.0f;         ///< Input drive [0, 10]. 0.0 = passthrough (bypass distortion).
    float mix = 1.0f;           ///< Wet/dry mix [0, 1]
    float toneHz = 4000.0f;     ///< Tone filter frequency [200, 8000] Hz
};

/// @brief All type-specific parameters in a single struct.
/// The adapter ignores fields not applicable to the active type.
/// Covers all categories defined in spec.md FR-DIST-003 and Appendix A.
struct DistortionParams {
    // Saturation (D01-D06)
    float bias = 0.0f;              ///< Asymmetry [-1, 1]
    float sag = 0.0f;               ///< Tube sag [0, 1]

    // Wavefold (D07-D09)
    float folds = 1.0f;             ///< Fold count [1, 8]
    float shape = 0.0f;             ///< Fold curve shape [0, 1]
    float symmetry = 0.5f;          ///< Fold symmetry [0, 1]

    // Digital (D12-D14)
    float bitDepth = 16.0f;         ///< Bit depth [1, 16]
    float sampleRateRatio = 1.0f;   ///< Downsample ratio [1, 32]
    float smoothness = 0.0f;        ///< Anti-alias smoothing [0, 1]

    // Dynamic (D15)
    float sensitivity = 0.5f;       ///< Envelope sensitivity [0, 1]
    float attackMs = 10.0f;         ///< Attack time [1, 100] ms
    float releaseMs = 100.0f;       ///< Release time [10, 500] ms
    int dynamicMode = 0;            ///< Mode: 0=Envelope, 1=Inverse, 2=Derivative

    // Hybrid (D16-D17, D26)
    float feedback = 0.5f;          ///< Feedback amount [0, 1.5]
    float delayMs = 10.0f;          ///< Delay time [1, 100] ms
    int stages = 1;                 ///< Allpass/filter stages [1, 4]
    float modDepth = 0.5f;          ///< Modulation depth [0, 1]

    // Aliasing (D18)
    float freqShift = 0.0f;         ///< Frequency shift [-1000, 1000] Hz

    // Bitwise (D19)
    int rotateAmount = 0;           ///< Bit rotation [-16, 16]
    uint16_t xorPattern = 0xAAAA;   ///< XOR mask [0x0000, 0xFFFF]

    // Experimental (D20-D25)
    float chaosAmount = 0.5f;       ///< Attractor influence [0, 1]
    float attractorSpeed = 1.0f;    ///< Attractor evolution rate [0.1, 10]
    float grainSizeMs = 50.0f;      ///< Granular grain size [5, 100] ms
    float formantShift = 0.0f;      ///< Formant shift [-12, 12] semitones

    // Spectral (D23)
    int fftSize = 2048;             ///< FFT window size [512, 4096]
    int magnitudeBits = 16;         ///< Spectral quantization [1, 16]

    // Fractal (D24)
    int iterations = 4;             ///< Fractal recursion depth [1, 8]
    float scaleFactor = 0.5f;       ///< Fractal scale [0.3, 0.9]
    float frequencyDecay = 0.5f;    ///< Harmonic decay [0, 1]

    // Stochastic (D25)
    float jitterAmount = 0.2f;      ///< Sample jitter depth [0, 1]
    float jitterRate = 10.0f;       ///< Jitter frequency [0.1, 100] Hz
    float coefficientNoise = 0.1f;  ///< Filter coefficient noise [0, 1]

    // Allpass Resonant (D26)
    float resonantFreq = 440.0f;    ///< Resonant frequency [20, 2000] Hz
    float allpassFeedback = 0.7f;   ///< Allpass feedback [0, 0.99]
    float decayTimeS = 1.0f;        ///< Decay time [0.01, 10] s
};

/// @brief Unified interface for all 26 distortion types.
/// Real-time safe: no allocations after prepare().
class DistortionAdapter {
public:
    /// @brief Prepare all processors for the given sample rate.
    /// @param sampleRate Processing sample rate (after any oversampling)
    /// @param maxBlockSize Maximum block size for block-based processors
    void prepare(double sampleRate, int maxBlockSize = 512) noexcept;

    /// @brief Reset all internal state.
    void reset() noexcept;

    /// @brief Set the active distortion type.
    /// @param type The distortion type to use
    void setType(DistortionType type) noexcept;

    /// @brief Get the current distortion type.
    [[nodiscard]] DistortionType getType() const noexcept { return currentType_; }

    /// @brief Set common parameters.
    void setCommonParams(const DistortionCommonParams& params) noexcept;

    /// @brief Get current common parameters.
    [[nodiscard]] DistortionCommonParams getCommonParams() const noexcept { return params_; }

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Processed output sample (with mix applied)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples.
    /// @param input Input buffer
    /// @param output Output buffer
    /// @param numSamples Number of samples to process
    void processBlock(const float* input, float* output, int numSamples) noexcept;

    /// @brief Set all type-specific parameters in one call.
    /// The adapter internally routes each field to the active processor.
    /// Fields irrelevant to the current type are ignored at zero cost.
    void setParams(const DistortionParams& params) noexcept;

    /// @brief Query fixed processing latency introduced by block-based types.
    /// Returns 0 for sample-accurate types (Saturation, Wavefold, Rectify, etc.).
    /// Returns the internal ring-buffer size for block-based types:
    ///   Spectral  -> FFT size (default 2048 samples)
    ///   Granular  -> grain buffer size (derived from grainSizeMs)
    ///   Feedback  -> delay line length (derived from delayMs)
    ///   Allpass   -> allpass chain length (derived from decayTimeS)
    /// @return Latency in samples at the current (potentially oversampled) rate
    [[nodiscard]] int getProcessingLatency() const noexcept;

private:
    // Internal processing (raw, before mix/tone)
    [[nodiscard]] float processRaw(float input) noexcept;

    // Apply tone filter (on wet signal only)
    [[nodiscard]] float applyTone(float wet) noexcept;

    double sampleRate_ = 44100.0;
    DistortionType currentType_ = DistortionType::SoftClip;
    DistortionCommonParams params_;

    // Tone filter (one-pole lowpass)
    Krate::DSP::OnePole toneFilter_;

    // DC blocker for asymmetric types
    Krate::DSP::DCBlocker dcBlocker_;
    bool needsDCBlock_ = false;

    // Current type-specific parameters
    DistortionParams typeParams_;

    // Processor instances (all pre-allocated)
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

    // Ring buffers for block-based processors.
    // Sized during prepare() based on maxBlockSize and type-specific requirements.
    // process(float) accumulates into inputRingBuffer_ and drains from outputRingBuffer_.
    // Sample-accurate types bypass these buffers entirely (latency = 0).
    std::array<float, 4096> inputRingBuffer_{};   ///< Max FFT size for Spectral
    std::array<float, 4096> outputRingBuffer_{};
    int ringWritePos_ = 0;                        ///< Current write position
    int ringReadPos_ = 0;                         ///< Current read position
    int blockLatency_ = 0;                        ///< Latency in samples for current type
    bool isBlockBased_ = false;                   ///< True if current type needs buffering
};

} // namespace Disrumpo
```

### 4.4 Processing Signal Flow

Per-band signal flow with distortion:

```
Band Input (from CrossoverNetwork)
    |
    v
+-------------------+
| Sweep Intensity   |  <- Placeholder for 005-sweep-system
| Multiply          |     (input *= sweepIntensity, default 1.0)
+-------------------+
    |
    v
+-------------------+
| Drive Gate        |  <- If drive == 0.0: bypass to Mix Stage (passthrough)
+-------------------+    Otherwise: scale input by drive, continue below
    |
    v
+-------------------+
| Oversampler Up    |  <- Upsample by factor (1/2/4/8x)
+-------------------+
    |
    v
+-------------------+
| DistortionAdapter |  <- Process at oversampled rate
| (processRaw)      |  <- Block-based types (Spectral, Granular, Feedback,
+-------------------+      Allpass) buffer internally; output drains with
                           fixed per-type latency
    |
    v
+-------------------+
| DC Blocker        |  <- Only if type needs it
+-------------------+
    |
    v
+-------------------+
| Oversampler Down  |  <- Downsample back to original rate
+-------------------+
    |
    v
+-------------------+
| Tone Filter       |  <- Apply tone to wet signal only
+-------------------+
    |
    v
+-------------------+
| Mix Stage         |  <- dry*(1-mix) + wet*mix
+-------------------+
    |
    v
+-------------------+
| Band Gain/Pan     |  <- From 002-band-management
+-------------------+
    |
    v
Band Output (to summation)
```

---

## 5. Implementation Tasks

Based on roadmap.md T3.1-T3.13:

### T3.1: Create DistortionAdapter Unified Interface

**Priority**: P0 (Critical Path)
**Effort**: 8h
**Dependencies**: 002-band-management complete

**Deliverables**:
- `distortion_adapter.h` with class declaration
- `distortion_adapter.cpp` with implementation
- `prepare()`, `reset()`, `setType()`, `process()` methods
- All processor instances as member variables

**Acceptance Criteria**:
- Compiles without warnings
- All 26 processor types instantiated
- prepare() calls prepare() on all child processors
- reset() calls reset() on all child processors

### T3.2: Create DistortionType Enum and Category Mapping

**Priority**: P0 (Critical Path)
**Effort**: 4h
**Dependencies**: T3.1

**Deliverables**:
- `distortion_types.h` with DistortionType enum
- `DistortionCategory` enum
- `getCategory()` function
- `getRecommendedOversample()` function
- `getTypeName()` function

**Acceptance Criteria**:
- All 26 types enumerated (D01-D26)
- Category mapping matches spec.md FR-DIST-001
- Oversampling recommendations match spec.md FR-OS-003

### T3.3: Integrate Saturation Types (D01-D06)

**Priority**: P0 (Critical Path)
**Effort**: 8h
**Dependencies**: T3.1, T3.2

**Deliverables**:
- SoftClip via SaturationProcessor::Type::Tape
- HardClip via SaturationProcessor::Type::Digital
- Tube via TubeStage
- Tape via TapeSaturator
- Fuzz via FuzzProcessor::Type::Germanium
- AsymmetricFuzz via FuzzProcessor with bias control

**Acceptance Criteria**:
- Each type produces distinct harmonic character
- Bias parameter affects asymmetry for D06
- Unit tests verify expected harmonic content

### T3.4: Integrate Wavefold Types (D07-D09)

**Priority**: P0 (Critical Path)
**Effort**: 4h
**Dependencies**: T3.1, T3.2

**Deliverables**:
- SineFold via WavefolderProcessor (Serge model)
- TriangleFold via WavefolderProcessor (Simple model)
- SergeFold via WavefolderProcessor (Lockhart model)

**Acceptance Criteria**:
- Folds parameter controls number of folds (1-8)
- Shape parameter adjusts fold curve
- Unit tests verify fold behavior

### T3.5: Integrate Rectify Types (D10-D11)

**Priority**: P0 (Critical Path)
**Effort**: 2h
**Dependencies**: T3.1, T3.2

**Deliverables**:
- FullRectify: `std::abs(input)`
- HalfRectify: `std::max(0.0f, input)`
- DC blocking enabled for both types

**Acceptance Criteria**:
- Full rectify doubles frequency
- Half rectify removes negative half
- DC offset removed by DC blocker

### T3.6: Integrate Digital Types (D12-D14)

**Priority**: P0 (Critical Path)
**Effort**: 4h
**Dependencies**: T3.1, T3.2

**Deliverables**:
- Bitcrush via BitcrusherProcessor
- SampleReduce via SampleRateReducer
- Quantize via BitcrusherProcessor (quantization mode)

**Acceptance Criteria**:
- Bit depth parameter reduces resolution
- Sample rate ratio reduces effective sample rate
- Unit tests verify quantization levels

### T3.7: Integrate Digital Types (D18-D19)

**Priority**: P0 (Critical Path)
**Effort**: 4h
**Dependencies**: T3.1, T3.2

**Deliverables**:
- Aliasing via AliasingEffect
- BitwiseMangler via BitwiseMangler

**Acceptance Criteria**:
- Aliasing creates intentional spectral folding
- Bitwise operations produce expected digital artifacts

### T3.8: Integrate Dynamic Type (D15)

**Priority**: P0 (Critical Path)
**Effort**: 2h
**Dependencies**: T3.1, T3.2

**Deliverables**:
- Temporal via TemporalDistortion

**Acceptance Criteria**:
- Sensitivity parameter affects envelope response
- Attack/Release parameters control envelope timing
- Louder input = more distortion (EnvelopeFollow mode)

### T3.9: Integrate Hybrid Types (D16-D17, D26)

**Priority**: P0 (Critical Path)
**Effort**: 6h
**Dependencies**: T3.1, T3.2

**Deliverables**:
- RingSaturation via RingSaturation
- FeedbackDist via FeedbackDistortion
- AllpassResonant via AllpassSaturator

**Acceptance Criteria**:
- RingSaturation creates metallic inharmonic content
- FeedbackDist produces sustained distortion with feedback limiting
- AllpassResonant creates resonant pitched distortion

### T3.10: Integrate Experimental Types (D20-D25)

**Priority**: P0 (Critical Path)
**Effort**: 8h
**Dependencies**: T3.1, T3.2

**Deliverables**:
- Chaos via ChaosWaveshaper
- Formant via FormantDistortion
- Granular via GranularDistortion
- Spectral via SpectralDistortion
- Fractal via FractalDistortion
- Stochastic via StochasticShaper

**Acceptance Criteria**:
- Each type produces unique character
- Chaos amount controls attractor influence
- Grain size controls granular texture
- Unit tests verify processing occurs

### T3.11: Implement Common Parameter Mapping

**Priority**: P0 (Critical Path)
**Effort**: 4h
**Dependencies**: T3.3-T3.10

**Deliverables**:
- Drive parameter scales input level
- Mix parameter blends dry/wet
- Tone parameter applies lowpass filter to wet

**Acceptance Criteria**:
- Drive 0.0 = passthrough: the Drive Gate bypasses the entire distortion path (Oversampler, processRaw, DC Blocker) and routes the unmodified band input directly to the Mix Stage. No zeros at output.
- Drive 10.0 = maximum distortion
- Mix 0.0 = fully dry
- Mix 1.0 = fully wet
- Tone filter audibly affects high frequencies

### T3.12: Integrate Oversampler Per Band

**Priority**: P0 (Critical Path)
**Effort**: 6h
**Dependencies**: T3.11

**Deliverables**:
- Oversampler instance per band
- Factor selection based on distortion type
- Global maximum limit parameter
- Integration into band processing chain

**Acceptance Criteria**:
- Oversampling reduces aliasing
- Factor matches recommendations per type
- CPU usage acceptable with 4x oversampling

### T3.13: Wire Distortion to Bands

**Priority**: P0 (Critical Path)
**Effort**: 4h
**Dependencies**: T3.12

**Deliverables**:
- Each band processes through its DistortionAdapter
- Distortion type parameter per band
- Signal flow as specified in section 4.4

**Acceptance Criteria**:
- Each band can have different distortion type
- Changing band type changes the character
- All bands sum correctly at output

---

## 6. Testing Requirements

### Unit Tests

| Test ID | Description | Criteria |
|---------|-------------|----------|
| UT-DI-001 | All 26 types process audio | Non-zero output for all types |
| UT-DI-002 | Type switching works | Correct processor activated per type |
| UT-DI-003 | Drive parameter affects output | Higher drive = more distortion |
| UT-DI-004 | Mix parameter blends correctly | Mix 0 = dry, Mix 1 = wet |
| UT-DI-005 | Tone filter works | Lower tone = less high frequencies |
| UT-DI-006 | DC blocker removes DC | DC component < 0.01 after blocker |
| UT-DI-007 | Oversampling reduces aliasing | Less aliasing at higher factors |
| UT-DI-008 | Real-time safety | No allocations in process() |
| UT-DI-009 | Drive=0 passthrough | Output equals input (no distortion path exercised) |
| UT-DI-010 | Block-based latency reporting | getProcessingLatency() returns correct value per type; sample-accurate types return 0 |
| UT-DI-011 | setParams covers all type-specific fields | Each category's parameters reach the active processor; round-trip via getParams matches |

### Integration Tests

| Test ID | Description | Criteria |
|---------|-------------|----------|
| IT-DI-001 | Full signal path | Audio through bands + distortion |
| IT-DI-002 | Band type independence | Different types per band |
| IT-DI-003 | State save/load | Distortion types restored correctly |

### Performance Tests

| Test ID | Description | Target | Milestone |
|---------|-------------|--------|-----------|
| PT-DI-001 | CPU 1 band, 1x OS, single type | < 2% | M2 |
| PT-DI-002 | CPU 4 bands, 4x OS, single type per band | < 5% | M2 |
| PT-DI-003 | CPU 8 bands, 4x OS, single type per band | < 10% | M2 |
| PT-DI-004 | CPU 4 bands, 4x OS, 2-type cross-family morph | < 15% | M4 (deferred -- exercised by 004-morph-system) |

---

## 7. Success Criteria Verification

### M2 Milestone Criteria (from roadmap.md)

| Criterion | Verification Method |
|-----------|---------------------|
| All 26 distortion types process audio correctly | UT-DI-001: Pass all types test |
| Common parameters (Drive, Mix, Tone) work | UT-DI-003/004/005: Pass parameter tests |
| Oversampling works (1x, 2x, 4x, 8x) | UT-DI-007: Verify aliasing reduction |
| CPU < 5% @ 4 bands, 4x OS, single type | PT-DI-002: Performance test |

---

## 8. Dependencies Summary

### KrateDSP Components Used

| Component | Include Path | Layer |
|-----------|--------------|-------|
| SaturationProcessor | `<krate/dsp/processors/saturation_processor.h>` | 2 |
| TubeStage | `<krate/dsp/processors/tube_stage.h>` | 2 |
| TapeSaturator | `<krate/dsp/processors/tape_saturator.h>` | 2 |
| FuzzProcessor | `<krate/dsp/processors/fuzz_processor.h>` | 2 |
| WavefolderProcessor | `<krate/dsp/processors/wavefolder_processor.h>` | 2 |
| BitcrusherProcessor | `<krate/dsp/processors/bitcrusher_processor.h>` | 2 |
| TemporalDistortion | `<krate/dsp/processors/temporal_distortion.h>` | 2 |
| RingSaturation | `<krate/dsp/primitives/ring_saturation.h>` | 1 |
| FeedbackDistortion | `<krate/dsp/processors/feedback_distortion.h>` | 2 |
| AliasingEffect | `<krate/dsp/processors/aliasing_effect.h>` | 2 |
| BitwiseMangler | `<krate/dsp/primitives/bitwise_mangler.h>` | 1 |
| ChaosWaveshaper | `<krate/dsp/primitives/chaos_waveshaper.h>` | 1 |
| FormantDistortion | `<krate/dsp/processors/formant_distortion.h>` | 2 |
| GranularDistortion | `<krate/dsp/processors/granular_distortion.h>` | 2 |
| SpectralDistortion | `<krate/dsp/processors/spectral_distortion.h>` | 2 |
| FractalDistortion | `<krate/dsp/processors/fractal_distortion.h>` | 2 |
| StochasticShaper | `<krate/dsp/primitives/stochastic_shaper.h>` | 1 |
| AllpassSaturator | `<krate/dsp/processors/allpass_saturator.h>` | 2 |
| SampleRateReducer | `<krate/dsp/primitives/sample_rate_reducer.h>` | 1 |
| DCBlocker | `<krate/dsp/primitives/dc_blocker.h>` | 1 |
| OnePole | `<krate/dsp/primitives/one_pole.h>` | 1 |
| Oversampler | `<krate/dsp/primitives/oversampler.h>` | 1 |

---

## 9. Clarifications

### Session 2026-01-28

- Q: Drive=0 behavior -- passthrough or silence? Signal flow showed `input * drive` (silence at 0), but T3.11 stated "Drive 0.0 = no distortion (passthrough)." --> A: Drive=0 means passthrough. The Drive Gate conditionally bypasses the entire distortion path (Oversampler up, processRaw, DC Blocker, Oversampler down). The unmodified band signal routes directly to the Mix Stage. Signal flow diagram updated accordingly.

- Q: Missing type-specific parameters -- individual setters or generic struct? The adapter exposed per-category setters but omitted Spectral (FFT Size, Magnitude Bits), Fractal (Iterations, Scale Factor, Frequency Decay), Stochastic (Jitter Amount, Jitter Rate, Coefficient Noise), Allpass Resonant (Resonant Frequency, Allpass Feedback, Decay Time), Aliasing (Frequency Shift), and Bitwise (Rotate Amount, XOR Pattern) from spec.md Appendix A. --> A: Replace all individual type-specific setters with a single `setParams(const DistortionParams&)` call. The `DistortionParams` struct now covers every parameter from spec.md Appendix A. The adapter routes each field internally to the correct KrateDSP processor.

- Q: CPU target scope -- PT-DI-002 said <5% with no "single type" qualifier, conflicting with spec.md's <15% for the full system. --> A: PT-DI-002 is scoped to single-type-per-band (M2 milestone) at <5%. Added PT-DI-004 for 2-type cross-family morph at <15%, deferred to 004-morph-system spec (M4 milestone). All performance test entries now carry explicit milestone and type-count annotations.

- Q: How do block-based processors (Spectral, Granular, Feedback, Allpass) operate via the single-sample `process()` interface? --> A: Internal ring-buffer accumulation. Each block-based processor maintains a pre-allocated input ring buffer (sized to its minimum processing window during `prepare()`). Calls to `process(float)` accumulate samples; once the buffer is full the processor runs its algorithm and output samples drain one at a time from an output ring buffer. This introduces a fixed, type-specific latency queryable via `getProcessingLatency()`. Latency is reported to the host via `getTail()`. Sample-accurate types (Saturation, Wavefold, Rectify, Digital) bypass the ring buffers entirely (latency = 0).

- Q: Namespace conflict -- `Disrumpo` (in 003) vs `Krate::DSP` (in dsp-details.md) for DistortionAdapter and DistortionParams. --> A: `namespace Disrumpo` is authoritative. DistortionAdapter and DistortionParams are plugin-specific glue wrapping shared `Krate::DSP` processors; they do not belong in the shared library. The dsp-details.md namespace reference is an error to be corrected in that document. All code in this spec correctly uses `namespace Disrumpo`.

---

## 10. Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-28 | Claude | Initial spec creation |
| 1.1 | 2026-01-28 | Claude | Clarification session: Drive=0 passthrough, DistortionParams struct replaces individual setters, CPU target phasing (PT-DI-002/004), block-based processor ring-buffer buffering with getProcessingLatency(), namespace confirmed as Disrumpo |
