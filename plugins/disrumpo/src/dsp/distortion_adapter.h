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

#include <krate/dsp/processors/tube_stage.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/fuzz_processor.h>
#include <krate/dsp/processors/wavefolder_processor.h>
#include <krate/dsp/primitives/bit_crusher.h>
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
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/waveshaper.h>

#include <array>
#include <atomic>
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
/// - Bitwise: xorPattern
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
    int transistor = 0;             ///< Fuzz transistor type [0-1]
    float octave = 0.0f;            ///< Fuzz octave [0, 1]
    float sustain = 0.5f;           ///< Fuzz sustain [0, 1]
    float asymmetry = 0.3f;         ///< Asym fuzz asymmetry [0, 1]
    float body = 0.5f;              ///< Asym fuzz body [0, 1]

    // Wavefold (D07-D09)
    float folds = 1.0f;             ///< Fold count [1, 12] (the folders clamp at 10)
    float shape = 0.0f;             ///< Fold curve shape [0, 1]
    float symmetry = 0.5f;          ///< Fold symmetry [0, 1]
    float angle = 0.5f;             ///< TriFold angle [0, 1]
    int foldModel = 0;              ///< SergeFold model select

    // Rectify (D10-D11)
    bool dcBlock = true;            ///< DC blocking toggle

    // Digital (D12-D14)
    float bitDepth = 16.0f;         ///< Bit depth; Bitcrush maps its Bits slot to [4, 16] (BitCrusher's range)
    float sampleRateRatio = 1.0f;   ///< Downsample ratio [1, 32]
    float smoothness = 0.0f;        ///< Anti-alias smoothing [0, 1]
    float dither = 0.0f;            ///< Bitcrush/quantize dither [0, 1]
    int bitcrushMode = 0;           ///< Bitcrush processing order
    float jitter = 0.0f;            ///< Digital jitter [0, 1]
    int sampleMode = 0;             ///< Sample reduce mode
    float quantLevels = 0.5f;       ///< Quantize levels [0, 1]
    float quantOffset = 0.0f;       ///< Quantize offset [0, 1]
    int bitwiseOp = 0;              ///< Bitwise operation select
    float bitwiseIntensity = 0.5f;  ///< Bitwise intensity [0, 1]
    float bitwisePattern = 0.0f;    ///< Bitwise pattern [0, 1]

    // Dynamic (D15)
    float sensitivity = 0.5f;       ///< Envelope sensitivity [0, 1]
    float attackMs = 10.0f;         ///< Attack time [1, 500] ms
    float releaseMs = 100.0f;       ///< Release time [10, 5000] ms
    int dynamicMode = 0;            ///< Mode: 0=EnvelopeFollow, 1=InverseEnvelope, 2=Derivative, 3=Hysteresis (menu: Follow, Inverse, Transient, Hyst)
    float dynamicCurve = 0.5f;      ///< Temporal waveshape curve [0, 1]
    float dynamicDepth = 0.5f;      ///< Temporal depth [0, 1]
    float hold = 0.0f;              ///< Temporal hold [0, 1]

    // Hybrid (D16-D17, D26)
    float feedback = 0.5f;          ///< Feedback amount [0, 1.5]
    float delayMs = 10.0f;          ///< Delay time [1, 100] ms
    int stages = 1;                 ///< Allpass/filter stages [1, 4]
    float modDepth = 0.5f;          ///< Modulation depth [0, 1]
    float rsCurve = 0.5f;           ///< Ring sat curve [0, 1]
    int carrierType = 0;            ///< Ring sat carrier select
    int rsFreqSelect = 0;           ///< Ring sat frequency select
    float rsCarrierFreq = 0.5f;     ///< Ring sat carrier freq param [0, 1]
    float fbCurve = 0.5f;           ///< Feedback curve [0, 1]
    int filterType = 0;             ///< Feedback filter select
    float filterFreq = 0.5f;        ///< Feedback filter freq [0, 1]
    bool limiter = true;            ///< Feedback limiter toggle
    float limThreshold = 0.8f;      ///< Feedback limiter threshold [0, 1]
    float fbDecay = 0.0f;           ///< Feedback decay amount [0, 1] (0=off)

    // Aliasing (D18)
    float freqShift = 0.0f;         ///< Frequency shift [-5000, 5000] Hz

    // Bitwise (D19)
    uint16_t xorPattern = 0xAAAA;   ///< XOR mask [0x0000, 0xFFFF]

    // Experimental (D20-D25)
    float chaosAmount = 0.5f;       ///< Attractor influence [0, 1]
    float attractorSpeed = 1.0f;    ///< Attractor evolution rate [0.01, 100]
    int chaosAttractor = 0;         ///< Chaos attractor model [0-3]
    float chaosCoupling = 0.5f;     ///< Chaos input coupling [0, 1]
    float chaosXDrive = 0.5f;       ///< Chaos X drive [0, 1]
    float chaosYDrive = 0.5f;       ///< Chaos Y drive [0, 1]
    int vowelSelect = 0;            ///< Formant vowel select [0-4]
    float formantShift = 0.0f;      ///< Formant shift [-24, 24] semitones
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
    int stochasticNoiseColor = 0;   ///< Noise distribution color (0-4)

    // Allpass Resonant (D26)
    int allpassTopo = 0;            ///< Allpass topology select
    float resonantFreq = 440.0f;    ///< Resonant frequency [20, 2000] Hz
    float allpassFeedback = 0.7f;   ///< Allpass feedback [0, 0.99]
    float decayTimeS = 1.0f;        ///< Decay time [0.01, 10] s
    float allpassDrive = 0.5f;      ///< Allpass drive amount [0, 1]
    int allpassSatType = 0;         ///< Allpass saturation curve select (0-3)
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
/// - Latency reporting and alignment for the Spectral type
///
/// Real-time safe: no allocations after prepare().
///
/// @par Signal Flow
/// Input -> [Drive Scale] -> [processRaw] -> [DC Block (if needed)] ->
/// [Tone Filter] -> [Mix Blend] -> Output
///
/// @par Drive Gate
/// When drive == 0.0, the entire distortion path is bypassed and the dry
/// image is returned unchanged (passthrough). For 0 < drive < kDriveBlendFull
/// the wet path runs at kDriveBlendFull and its mix share is scaled by
/// drive / kDriveBlendFull, so the output is continuous across drive 0 (D-014).
///
/// @par Latency
/// Spectral (D23) delays its output by its FFT size - 1; its dry image is delayed
/// by the same amount, so mix < 1 gives one image (D-081). Query via
/// getProcessingLatency(). Every other type returns 0 latency.
class DistortionAdapter {
public:
    /// @brief Test probe: DistortionAdapter instances alive (each holds every type's
    /// processor, about 1 MB prepared), so a test can count what an owner keeps.
    static inline std::atomic<int> debugLiveInstances{0};

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare all processors for the given sample rate (allocates).
    ///
    /// Every member's buffers are sized for sampleRate * maxOversampleFactor, so
    /// setProcessingRate() can later move the adapter to any rate up to that
    /// without allocating. Processing starts at sampleRate. SpectralDistortion
    /// allocates in its prepare(), so it is prepared once, at sampleRate, and is
    /// not re-rated (Spectral recommends 1x, distortion_types.h).
    /// @param sampleRate Processing sample rate (the host rate for an oversampling owner)
    /// @param maxBlockSize Maximum block size for block-based processors
    /// @param maxOversampleFactor Highest factor setProcessingRate() may be given
    void prepare(double sampleRate, int maxBlockSize = 512, int maxOversampleFactor = 1) noexcept;

    /// @brief Re-derive every rate-dependent coefficient for the rate the adapter is
    /// clocked at (D-009). Allocation-free; resets the processors' filter state, so call
    /// it at the silent point of a factor switch. Clamped to the rate prepare() sized for.
    /// @param rate Rate at which process() will be called, in Hz
    void setProcessingRate(double rate) noexcept;

    /// @brief Reset all internal state.
    void reset() noexcept;

    // =========================================================================
    // Type Selection
    // =========================================================================

    /// @brief Set the active distortion type.
    ///
    /// A change of type clears the new type's processor at the next process() (after the
    /// caller's setParams) and crossfades the output from the last sample over 5 ms
    /// (D-032, D-080, D-141). Setting the current type again changes nothing.
    /// @param type The distortion type to use
    void setType(DistortionType type) noexcept;

    /// @brief Get the current distortion type.
    [[nodiscard]] DistortionType getType() const noexcept { return currentType_; }

    // =========================================================================
    // Parameter Control
    // =========================================================================

    /// @brief Set common parameters. A Drive change re-routes the types that read it in
    /// their parameter routing (Temporal, Stochastic), whatever order setParams() came in.
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

    /// @brief Query the fixed processing latency of the active type (D-081, T2.10).
    ///
    /// Spectral: FFT size - 1 (SpectralDistortion returns a frame's first output on the
    /// call that completes the frame; measured 511 / 2047 / 4095 at 512 / 2048 / 4096).
    /// Every other type: 0. Granular grains read the newest input (measured 0 with grain
    /// position and position variance at 0); its position and jitter are effect delays,
    /// like FeedbackDist's delay time, not a latency.
    ///
    /// @return Latency in samples at the rate process() is clocked at
    [[nodiscard]] int getProcessingLatency() const noexcept;

    /// @brief The latency a prepared adapter of @p type with FFT size @p fftSize reports
    /// (getProcessingLatency), without an adapter: Spectral's active FFT size (the next power
    /// of two from SpectralDistortion's 256, at most kSpectralMaxFftSize) - 1, else 0.
    [[nodiscard]] static int latencyFor(DistortionType type, int fftSize) noexcept {
        if (type != DistortionType::Spectral) return 0;
        constexpr std::size_t kSpectralMinFftSize = Krate::DSP::SpectralDistortion::kMinFFTSize;
        std::size_t pow2 = kSpectralMinFftSize;
        const auto requested = static_cast<std::size_t>(fftSize);  // as updateBlockLatency passes it
        while (pow2 < requested && pow2 < kSpectralMaxFftSize) pow2 <<= 1;
        return std::clamp(static_cast<int>(pow2) - 1, 0, kMaxLatencySamples);
    }

    /// @brief Pad the output to a total latency of @p samples (clocked samples, clamped
    /// to [0, kMaxLatencySamples]). An owner that sums adapters of different types
    /// (MorphEngine) aligns every one to the slowest. Below the own latency it has no effect.
    void setLatencyAlignment(int samples) noexcept;

    /// @brief Latency of process() output: max(own latency, alignment), in clocked samples.
    [[nodiscard]] int getAlignedLatency() const noexcept { return std::max(blockLatency_, alignLatency_); }

    /// @brief Rate process() is clocked at (the host rate x the owner's factor).
    [[nodiscard]] double getProcessingRate() const noexcept { return sampleRate_; }

    /// @brief Take over @p other's Ring Saturation carrier state (phase, tracked frequency),
    /// rescaled to this adapter's rate: an oversampling crossfade's incoming lane continues the
    /// running lane's carrier instead of restarting it (follow-up 84, user ruling 2026-11-10).
    /// @p inputLagSeconds: how much later this adapter's input is than @p other's.
    void copyCarrierStateFrom(const DistortionAdapter& other, double inputLagSeconds) noexcept {
        ringSaturation_.setCarrierState(other.ringSaturation_.getCarrierState(), other.ringSaturation_.getSampleRate(),
                                        inputLagSeconds);
    }

    /// @brief True for the types whose shaper runs through first-order antiderivative
    /// anti-aliasing (ADAA1) when the adapter is clocked above the host rate: HalfRectify
    /// (the step at the threshold) and TriangleFold (the fold corners). Ruling 2026-11-09.
    [[nodiscard]] static bool usesAdaa(DistortionType type) noexcept {
        return type == DistortionType::HalfRectify || type == DistortionType::TriangleFold;
    }

    /// ADAA1 delays the shaper by half a clocked sample (its linear segments read the mean of
    /// the last two inputs); owners align the band to it (BandProcessor latency).
    static constexpr double kAdaaDelayClockedSamples = 0.5;

    /// Largest Spectral FFT size the plugin offers (processor_params.cpp Spectral
    /// slot 1: 512..4096). SpectralDistortion is allocated for it once in prepare().
    static constexpr std::size_t kSpectralMaxFftSize = 4096;

    /// Largest latency an adapter can have or be aligned to: Spectral at the largest FFT size.
    static constexpr int kMaxLatencySamples = static_cast<int>(kSpectralMaxFftSize) - 1;

    /// Drive below which the wet path is blended in by drive / kDriveBlendFull
    /// (D-014); at and above it the drive gate is fully open.
    static constexpr float kDriveBlendFull = 0.01f;

private:
    // =========================================================================
    // Internal Processing
    // =========================================================================

    /// @brief Prepare every member except SpectralDistortion for @p rate (sets sampleRate_).
    void prepareRateDependent(double rate) noexcept;

    /// @brief Raw processing for the current type (no mix/tone/DC).
    [[nodiscard]] float processRaw(float input) noexcept;

    /// @brief Drive, type, DC block, tone and mix of @p input; @p dry is the dry image the
    /// mix and the drive gate use (the input itself, or delayed by a latent type's latency).
    [[nodiscard]] float processWithDry(float input, float dry) noexcept;

    /// @brief Re-derive blockLatency_ for the active type (Spectral: its active FFT size).
    void updateBlockLatency() noexcept;

    /// @brief Clear the latency delay lines when the adapter becomes latent (D-081).
    void refreshLatencyMode() noexcept;

    /// @brief Apply tone filter to wet signal.
    [[nodiscard]] float applyTone(float wet) noexcept;

    /// @brief Update DC blocker state based on current type.
    void updateDCBlockerState() noexcept;

    /// @brief True for the types whose wet signal runs through the tone filter.
    [[nodiscard]] static bool usesToneFilter(DistortionType type) noexcept;

    /// @brief Clear the state of the processor(s) @p type runs (allocation-free).
    void resetProcessorFor(DistortionType type) noexcept;

    /// @brief Route type-specific parameters to the active processor.
    void routeParamsToProcessor() noexcept;

    // =========================================================================
    // State
    // =========================================================================

    double sampleRate_ = 44100.0;          ///< Rate process() is clocked at
    double maxProcessingRate_ = 44100.0;   ///< Rate prepare() sized the buffers for
    double hostRate_ = 44100.0;            ///< Rate prepare() was called with (the host rate)

    /// @brief ADAA1 runs only when oversampled: at the host rate its cos(pi f / fs) on the
    /// linear segments would dull the type (-2.4 dB at 10 kHz, 44.1 kHz).
    [[nodiscard]] bool adaaActive() const noexcept { return sampleRate_ > hostRate_; }

    /// HalfRectify ADAA1: the previous shaper input
    double halfRectAdaaPrev_ = 0.0;
    int maxBlockSize_ = 512;               ///< Block size prepare() was given
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

    /// One TubeStage per cascade stage (satStage 0-3 runs 1-4 of them in series, D-077)
    std::array<Krate::DSP::TubeStage, 4> tube_;
    Krate::DSP::TapeSaturator tape_;
    Krate::DSP::FuzzProcessor fuzz_;
    Krate::DSP::WavefolderProcessor wavefolder_;
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

    /// Single-sample buffer for block processors that only have block processing
    float singleSampleBuffer_[1] = {0.0f};

    /// Raw bit crusher primitive for direct quantization (Bitcrush type)
    Krate::DSP::BitCrusher rawBitCrusher_;

    /// Bitcrush jitter amount [0, 1] — randomizes bit depth per sample
    float bitcrushJitter_ = 0.0f;

    /// Bitcrush dither mode: 0=Truncate (no dither), 1=Dither (use dither amount)
    int bitcrushDitherMode_ = 0;

    /// PRNG state for bitcrush jitter (xorshift32)
    uint32_t jitterRng_ = 0x5A3E7B91u;

    // =========================================================================
    // Tape-Specific Processing State
    // =========================================================================

    /// One-pole lowpass for tape HF rolloff (D04)
    Krate::DSP::OnePoleLP tapeHfFilter_;

    /// One-pole lowpass for asymmetric fuzz body control (D06)
    Krate::DSP::OnePoleLP bodyFilter_;

    /// One-pole lowpass for wavefold smooth parameter (D07-D09)
    Krate::DSP::OnePoleLP wavefoldSmoothFilter_;

    /// Secondary wavefolder for shape crossfade (triangle model for sine/triangle blend)
    Krate::DSP::Wavefolder wavefoldTriangle_;

    /// Phase accumulator for tape flutter LFO (D04)
    float flutterPhase_ = 0.0f;

    // =========================================================================
    // Block-Based Processing State
    // =========================================================================

    /// Fixed latency of the active type in clocked samples (Spectral; 0 otherwise)
    int blockLatency_ = 0;

    /// Total latency the owner aligns the output to (setLatencyAlignment)
    int alignLatency_ = 0;

    /// True while blockLatency_ or alignLatency_ is non-zero: process() runs the delays
    bool latent_ = false;

    /// Input history: the dry image of a latent type, read at blockLatency_ (D-081)
    Krate::DSP::DelayLine dryDelay_;

    /// Output history: the pad up to alignLatency_, read at alignLatency_ - blockLatency_
    Krate::DSP::DelayLine padDelay_;

    // =========================================================================
    // Type-Change Reset and Fade (D-032, D-080, D-141)
    // =========================================================================

    /// Length of the output crossfade after a type change
    static constexpr double kTypeFadeSeconds = 0.005;

    /// The new type's processor is cleared at the next process()
    bool resetPending_ = false;

    /// process() has produced a sample since prepare()/reset()
    bool hasOutput_ = false;

    /// Last process() output: the origin of the next type-change fade
    float lastOutput_ = 0.0f;

    /// Output the running fade starts from
    float typeFadeOrigin_ = 0.0f;

    /// Fade length in clocked samples, and the samples of it still to run
    int typeFadeLength_ = 1;
    int typeFadeRemaining_ = 0;

    /// Counts this instance in debugLiveInstances
    struct LiveCount {
        LiveCount() noexcept { debugLiveInstances.fetch_add(1, std::memory_order_relaxed); }
        LiveCount(const LiveCount& /*other*/) noexcept { debugLiveInstances.fetch_add(1, std::memory_order_relaxed); }
        LiveCount(LiveCount&& /*other*/) noexcept { debugLiveInstances.fetch_add(1, std::memory_order_relaxed); }
        LiveCount& operator=(const LiveCount& /*other*/) noexcept = default;
        LiveCount& operator=(LiveCount&& /*other*/) noexcept = default;
        ~LiveCount() { debugLiveInstances.fetch_sub(1, std::memory_order_relaxed); }
    };
    LiveCount liveCount_;
};

} // namespace Disrumpo
