// ==============================================================================
// Distortion Adapter for Disrumpo Plugin
// ==============================================================================
// Unified interface for all 26 distortion types, wrapping KrateDSP processors.
// Real-time safe: no allocations after prepare().
//
// Namespace: Disrumpo (plugin-specific glue layer)
//
// Reference: specs/003-distortion-integration/spec.md FR-DI-002
// ==============================================================================

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
#include <krate/dsp/primitives/waveshaper.h>

#include <array>
#include <cmath>
#include <algorithm>

namespace Disrumpo {

// =============================================================================
// Parameter Structures (FR-DI-003, FR-DI-004)
// =============================================================================

/// @brief Common parameters applicable to all distortion types.
///
/// These parameters are applied around the distortion algorithm itself:
/// - Drive: Scales input before distortion (0 = passthrough bypass)
/// - Mix: Blends dry/wet after distortion
/// - Tone: Lowpass filter on wet signal
struct DistortionCommonParams {
    float drive = 1.0f;         ///< Input drive [0, 10]. 0.0 = passthrough (bypass distortion).
    float mix = 1.0f;           ///< Wet/dry mix [0, 1]
    float toneHz = 4000.0f;     ///< Tone filter frequency [200, 8000] Hz
};

/// @brief All type-specific parameters in a single struct.
///
/// The adapter ignores fields not applicable to the active type.
/// This approach enables efficient parameter passing without virtual calls.
///
/// Covers all categories defined in spec.md FR-DI-004:
/// - Saturation: bias, sag
/// - Wavefold: folds, shape, symmetry
/// - Digital: bitDepth, sampleRateRatio, smoothness
/// - Dynamic: sensitivity, attackMs, releaseMs, dynamicMode
/// - Hybrid: feedback, delayMs, stages, modDepth
/// - Aliasing: freqShift
/// - Bitwise: rotateAmount, xorPattern
/// - Experimental: chaosAmount, attractorSpeed, grainSizeMs, formantShift
/// - Spectral: fftSize, magnitudeBits
/// - Fractal: iterations, scaleFactor, frequencyDecay
/// - Stochastic: jitterAmount, jitterRate, coefficientNoise
/// - Allpass Resonant: resonantFreq, allpassFeedback, decayTimeS
struct DistortionParams {
    // Saturation (D01-D06)
    float bias = 0.0f;              ///< Asymmetry [-1, 1]
    float sag = 0.0f;               ///< Tube sag [0, 1]
    float curve = 0.5f;             ///< SoftClip curve shape [0, 1]
    float knee = 0.5f;              ///< SoftClip knee [0, 1]
    float threshold = 0.7f;         ///< Hard clip threshold [0, 1]
    float ceiling = 1.0f;           ///< Hard clip ceiling [0, 1]
    int satStage = 0;               ///< Tube/tape stage select [0-3]
    float speed = 0.5f;             ///< Tape speed [0, 1]
    int tapeModel = 0;              ///< Tape model select
    float hfRoll = 0.5f;            ///< Tape HF rolloff [0, 1]
    float flutter = 0.2f;           ///< Tape flutter [0, 1]
    float gate = 0.0f;              ///< Fuzz gate [0, 1]
    int transistor = 0;             ///< Fuzz transistor type [0-1]
    float octave = 0.0f;            ///< Fuzz octave [0, 1]
    float sustain = 0.5f;           ///< Fuzz sustain [0, 1]
    float asymmetry = 0.3f;         ///< Asym fuzz asymmetry [0, 1]
    float body = 0.5f;              ///< Asym fuzz body [0, 1]

    // Wavefold (D07-D09)
    float folds = 1.0f;             ///< Fold count [1, 8]
    float shape = 0.0f;             ///< Fold curve shape [0, 1]
    float symmetry = 0.5f;          ///< Fold symmetry [0, 1]
    float angle = 0.5f;             ///< TriFold angle [0, 1]
    int foldModel = 0;              ///< SergeFold model select

    // Rectify (D10-D11)
    bool dcBlock = true;            ///< DC blocking toggle

    // Digital (D12-D14)
    float bitDepth = 16.0f;         ///< Bit depth [1, 16]
    float sampleRateRatio = 1.0f;   ///< Downsample ratio [1, 32]
    float smoothness = 0.0f;        ///< Anti-alias smoothing [0, 1]
    float dither = 0.0f;            ///< Bitcrush/quantize dither [0, 1]
    int bitcrushMode = 0;           ///< Bitcrush processing order
    float jitter = 0.0f;            ///< Digital jitter [0, 1]
    int sampleMode = 0;             ///< Sample reduce mode
    float quantLevels = 0.5f;       ///< Quantize levels [0, 1]
    float quantOffset = 0.0f;       ///< Quantize offset [0, 1]
    bool preFilter = false;         ///< Aliasing pre-filter toggle
    float resonance = 0.0f;         ///< Aliasing resonance [0, 1]
    int bitwiseOp = 0;              ///< Bitwise operation select
    float bitwiseIntensity = 0.5f;  ///< Bitwise intensity [0, 1]
    float bitwisePattern = 0.0f;    ///< Bitwise pattern [0, 1]
    float bitwiseBits = 0.5f;       ///< Bitwise bits slider [0, 1]

    // Dynamic (D15)
    float sensitivity = 0.5f;       ///< Envelope sensitivity [0, 1]
    float attackMs = 10.0f;         ///< Attack time [1, 100] ms
    float releaseMs = 100.0f;       ///< Release time [10, 500] ms
    int dynamicMode = 0;            ///< Mode: 0=Envelope, 1=Inverse, 2=Derivative
    float dynamicCurve = 0.5f;      ///< Temporal waveshape curve [0, 1]
    float dynamicDepth = 0.5f;      ///< Temporal depth [0, 1]
    int lookAhead = 0;              ///< Temporal look-ahead mode
    float hold = 0.0f;              ///< Temporal hold [0, 1]

    // Hybrid (D16-D17, D26)
    float feedback = 0.5f;          ///< Feedback amount [0, 1.5]
    float delayMs = 10.0f;          ///< Delay time [1, 100] ms
    int stages = 1;                 ///< Allpass/filter stages [1, 4]
    float modDepth = 0.5f;          ///< Modulation depth [0, 1]
    float rsCurve = 0.5f;           ///< Ring sat curve [0, 1]
    int carrierType = 0;            ///< Ring sat carrier select
    int rsFreqSelect = 0;           ///< Ring sat frequency select
    float fbCurve = 0.5f;           ///< Feedback curve [0, 1]
    int filterType = 0;             ///< Feedback filter select
    float filterFreq = 0.5f;        ///< Feedback filter freq [0, 1]
    bool limiter = true;            ///< Feedback limiter toggle
    float limThreshold = 0.8f;      ///< Feedback limiter threshold [0, 1]

    // Aliasing (D18)
    float freqShift = 0.0f;         ///< Frequency shift [-1000, 1000] Hz

    // Bitwise (D19)
    int rotateAmount = 0;           ///< Bit rotation [-16, 16]
    uint16_t xorPattern = 0xAAAA;   ///< XOR mask [0x0000, 0xFFFF]

    // Experimental (D20-D25)
    float chaosAmount = 0.5f;       ///< Attractor influence [0, 1]
    float attractorSpeed = 1.0f;    ///< Attractor evolution rate [0.1, 10]
    int chaosAttractor = 0;         ///< Chaos attractor model [0-3]
    float chaosCoupling = 0.5f;     ///< Chaos input coupling [0, 1]
    float chaosXDrive = 0.5f;       ///< Chaos X drive [0, 1]
    float chaosYDrive = 0.5f;       ///< Chaos Y drive [0, 1]
    int vowelSelect = 0;            ///< Formant vowel select [0-4]
    float formantShift = 0.0f;      ///< Formant shift [-12, 12] semitones
    float formantCurve = 0.5f;      ///< Formant curve [0, 1]
    float formantReso = 0.5f;       ///< Formant resonance [0, 1]
    float formantBW = 0.5f;         ///< Formant bandwidth [0, 1]
    int formantCount = 0;           ///< Formant count select
    float formantGender = 0.5f;     ///< Formant gender [0, 1]
    float formantBlend = 0.5f;      ///< Formant blend [0, 1]
    float grainSizeMs = 50.0f;      ///< Granular grain size [5, 100] ms
    float grainDensity = 0.5f;      ///< Granular density [0, 1]
    float grainPVar = 0.0f;         ///< Granular pitch variance [0, 1]
    float grainDVar = 0.0f;         ///< Granular density variance [0, 1]
    float grainPos = 0.0f;          ///< Granular position [0, 1]
    float grainCurve = 0.5f;        ///< Granular curve [0, 1]
    int grainEnvType = 0;           ///< Granular envelope select
    int grainSpread = 0;            ///< Granular spread select
    bool grainFreeze = false;       ///< Granular freeze toggle

    // Spectral (D23)
    int spectralMode = 0;           ///< Spectral processing mode
    int fftSize = 2048;             ///< FFT window size [512, 4096]
    float spectralCurve = 0.5f;     ///< Spectral curve [0, 1]
    float spectralTilt = 0.5f;      ///< Spectral tilt [0, 1]
    float spectralThreshold = 0.0f; ///< Spectral threshold [0, 1]
    int magnitudeBits = 16;         ///< Spectral quantization [1, 16]
    int spectralMagMode = 0;        ///< Spectral magnitude mode
    float spectralFreq = 0.5f;      ///< Spectral frequency [0, 1]
    int spectralPhase = 0;          ///< Spectral phase mode

    // Fractal (D24)
    int fractalMode = 0;            ///< Fractal mode select
    int iterations = 4;             ///< Fractal recursion depth [1, 8]
    float scaleFactor = 0.5f;       ///< Fractal scale [0.3, 0.9]
    float frequencyDecay = 0.5f;    ///< Harmonic decay [0, 1]
    float fractalCurve = 0.5f;      ///< Fractal curve [0, 1]
    float fractalFB = 0.0f;         ///< Fractal feedback [0, 1]
    int fractalBlend = 0;           ///< Fractal blend select
    float fractalDepth = 0.5f;      ///< Fractal depth [0, 1]

    // Stochastic (D25)
    int stochasticCurve = 0;        ///< Stochastic curve select
    float jitterAmount = 0.2f;      ///< Sample jitter depth [0, 1]
    float jitterRate = 10.0f;       ///< Jitter frequency [0.1, 100] Hz
    float coefficientNoise = 0.1f;  ///< Filter coefficient noise [0, 1]
    float stochasticDrift = 0.0f;   ///< Stochastic drift [0, 1]
    int stochasticCorr = 0;         ///< Stochastic correlation select
    float stochasticSmooth = 0.5f;  ///< Stochastic smoothing [0, 1]

    // Allpass Resonant (D26)
    int allpassTopo = 0;            ///< Allpass topology select
    float resonantFreq = 440.0f;    ///< Resonant frequency [20, 2000] Hz
    float allpassFeedback = 0.7f;   ///< Allpass feedback [0, 0.99]
    float decayTimeS = 1.0f;        ///< Decay time [0.01, 10] s
    float allpassCurve = 0.5f;      ///< Allpass curve [0, 1]
    bool allpassPitch = false;      ///< Allpass pitch tracking
    float allpassDamp = 0.3f;       ///< Allpass damping [0, 1]
};

// =============================================================================
// DistortionAdapter Class (FR-DI-002)
// =============================================================================

/// @brief Unified interface for all 26 distortion types.
///
/// Wraps KrateDSP processors (which remain in Krate::DSP namespace) with a
/// plugin-specific adapter that provides:
/// - Type switching via setType()
/// - Common parameter handling (drive, mix, tone)
/// - Type-specific parameter routing via setParams()
/// - DC blocking for asymmetric types
/// - Block-based latency reporting for Spectral/Granular types
///
/// Real-time safe: no allocations after prepare().
///
/// @par Signal Flow
/// Input -> [Drive Scale] -> [processRaw] -> [DC Block (if needed)] ->
/// [Tone Filter] -> [Mix Blend] -> Output
///
/// @par Drive Gate
/// When drive == 0.0, the entire distortion path is bypassed and the input
/// is returned directly to the mix stage (passthrough).
///
/// @par Block-Based Types
/// Spectral (D23) and Granular (D22) use internal ring buffers and introduce
/// fixed latency. Query via getProcessingLatency(). Sample-accurate types
/// return 0 latency.
class DistortionAdapter {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare all processors for the given sample rate.
    /// @param sampleRate Processing sample rate (after any oversampling)
    /// @param maxBlockSize Maximum block size for block-based processors
    void prepare(double sampleRate, int maxBlockSize = 512) noexcept;

    /// @brief Reset all internal state.
    void reset() noexcept;

    // =========================================================================
    // Type Selection
    // =========================================================================

    /// @brief Set the active distortion type.
    /// @param type The distortion type to use
    void setType(DistortionType type) noexcept;

    /// @brief Get the current distortion type.
    [[nodiscard]] DistortionType getType() const noexcept { return currentType_; }

    // =========================================================================
    // Parameter Control
    // =========================================================================

    /// @brief Set common parameters.
    void setCommonParams(const DistortionCommonParams& params) noexcept;

    /// @brief Get current common parameters.
    [[nodiscard]] const DistortionCommonParams& getCommonParams() const noexcept {
        return commonParams_;
    }

    /// @brief Set all type-specific parameters in one call.
    ///
    /// The adapter internally routes each field to the active processor.
    /// Fields irrelevant to the current type are ignored at zero cost.
    void setParams(const DistortionParams& params) noexcept;

    /// @brief Get current type-specific parameters.
    [[nodiscard]] const DistortionParams& getParams() const noexcept {
        return typeParams_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// Applies drive scaling, distortion, DC blocking, tone filter, and mix.
    /// Drive=0 bypasses the distortion path entirely (passthrough).
    ///
    /// @param input Input sample
    /// @return Processed output sample (with mix applied)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples.
    /// @param input Input buffer
    /// @param output Output buffer (can be same as input for in-place)
    /// @param numSamples Number of samples to process
    void processBlock(const float* input, float* output, int numSamples) noexcept;

    // =========================================================================
    // Latency Query
    // =========================================================================

    /// @brief Query fixed processing latency introduced by block-based types.
    ///
    /// Returns 0 for sample-accurate types (Saturation, Wavefold, Rectify, etc.).
    /// Returns the internal ring-buffer size for block-based types:
    /// - Spectral: FFT size (default 2048 samples)
    /// - Granular: grain buffer size (derived from grainSizeMs)
    ///
    /// @return Latency in samples at the current (potentially oversampled) rate
    [[nodiscard]] int getProcessingLatency() const noexcept;

private:
    // =========================================================================
    // Internal Processing
    // =========================================================================

    /// @brief Raw processing for the current type (no mix/tone/DC).
    [[nodiscard]] float processRaw(float input) noexcept;

    /// @brief Apply tone filter to wet signal.
    [[nodiscard]] float applyTone(float wet) noexcept;

    /// @brief Update DC blocker state based on current type.
    void updateDCBlockerState() noexcept;

    /// @brief Route type-specific parameters to the active processor.
    void routeParamsToProcessor() noexcept;

    // =========================================================================
    // State
    // =========================================================================

    double sampleRate_ = 44100.0;
    DistortionType currentType_ = DistortionType::SoftClip;
    DistortionCommonParams commonParams_;
    DistortionParams typeParams_;

    // =========================================================================
    // Common Processing Components
    // =========================================================================

    /// Tone filter (one-pole lowpass on wet signal)
    Krate::DSP::OnePoleLP toneFilter_;

    /// DC blocker for asymmetric types
    Krate::DSP::DCBlocker dcBlocker_;

    /// Whether current type needs DC blocking
    bool needsDCBlock_ = false;

    // =========================================================================
    // KrateDSP Processor Instances (all pre-allocated)
    // =========================================================================

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

    // =========================================================================
    // Primitive Waveshapers for Direct Single-Sample Processing
    // =========================================================================
    // These are used instead of the block-based processors when we need
    // sample-by-sample processing without the overhead of smoothing.

    /// Waveshaper for Tube type (D03)
    Krate::DSP::Waveshaper tubeShaper_;

    /// Waveshaper for Tape type (D04) - uses Sigmoid::tanh with drive
    Krate::DSP::Waveshaper tapeShaper_;

    /// Single-sample buffer for block processors that only have block processing
    float singleSampleBuffer_[1] = {0.0f};

    // =========================================================================
    // Block-Based Processing State
    // =========================================================================

    /// Ring buffers for block-based processors (Spectral, Granular)
    static constexpr size_t kMaxBlockBufferSize = 4096;
    std::array<float, kMaxBlockBufferSize> inputRingBuffer_{};
    std::array<float, kMaxBlockBufferSize> outputRingBuffer_{};
    int ringWritePos_ = 0;
    int ringReadPos_ = 0;
    int blockLatency_ = 0;
    bool isBlockBased_ = false;
};

} // namespace Disrumpo
