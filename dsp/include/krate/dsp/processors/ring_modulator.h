// ==============================================================================
// Ring Modulator - Layer 2 DSP Processor
// ==============================================================================
// Four-quadrant ring modulator that multiplies input by an internally generated
// carrier signal, producing sum and difference frequency sidebands.
//
// Carrier waveforms: Sine (Gordon-Smith phasor), Triangle/Sawtooth/Square
// (PolyBLEP), Noise (NoiseOscillator with white noise).
//
// Feature: 085-ring-mod-distortion
// Reference: specs/085-ring-mod-distortion/spec.md
// Layer: 2 (depends on Layer 0: math_constants, db_utils; Layer 1: polyblep,
//           noise oscillator, smoother)
// ==============================================================================

#pragma once

// Layer 0
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/pattern_freeze_types.h>

// Layer 1
#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// Carrier waveform selection for the ring modulator.
enum class RingModCarrierWaveform : uint8_t {
    Sine = 0,       ///< Gordon-Smith magic circle (2 muls + 2 adds)
    Triangle = 1,   ///< Band-limited via PolyBlepOscillator
    Sawtooth = 2,   ///< Band-limited via PolyBlepOscillator
    Square = 3,     ///< Band-limited via PolyBlepOscillator
    Noise = 4       ///< White noise via NoiseOscillator
};

/// Frequency mode for the ring modulator carrier.
enum class RingModFreqMode : uint8_t {
    Free = 0,       ///< Carrier frequency set directly in Hz
    NoteTrack = 1   ///< Carrier frequency = noteFrequency * ratio
};

// =============================================================================
// RingModulator Class
// =============================================================================

/// @brief Four-quadrant ring modulator (Layer 2 DSP Processor).
///
/// Multiplies input signal by an internally generated carrier:
/// output[n] = input[n] * carrier[n] * amplitude
///
/// @par Carrier Waveforms
/// - Sine: Gordon-Smith magic circle phasor (2 muls + 2 adds per sample)
/// - Triangle/Sawtooth/Square: PolyBLEP oscillator (band-limited)
/// - Noise: White noise via NoiseOscillator (frequency-independent)
///
/// @par Frequency Modes
/// - Free: Carrier frequency set directly in Hz
/// - NoteTrack: Carrier frequency = noteFrequency * ratio
///
/// @par Thread Safety
/// Single-threaded model. All methods called from audio thread.
///
/// @par Real-Time Safety
/// processBlock() is fully real-time safe: no allocation, no exceptions,
/// no blocking, no I/O (FR-010).
class RingModulator {
public:
    // =========================================================================
    // Constants (FR-006, FR-023, FR-024)
    // =========================================================================

    static constexpr float kMinFreqHz = 0.1f;          ///< Min carrier freq
    static constexpr float kMaxFreqHz = 20000.0f;      ///< Max carrier freq
    static constexpr float kMinRatio = 0.25f;           ///< Min carrier ratio
    static constexpr float kMaxRatio = 16.0f;           ///< Max carrier ratio
    static constexpr float kMaxSpreadOffsetHz = 50.0f;  ///< Max stereo spread
    static constexpr float kSmoothingTimeMs = 5.0f;     ///< Freq smoother time
    static constexpr int kRenormInterval = 1024;           ///< Sine renorm interval

    // =========================================================================
    // Lifecycle (FR-008)
    // =========================================================================

    RingModulator() noexcept = default;
    ~RingModulator() = default;

    // Copyable and movable (all members are value types)
    RingModulator(const RingModulator&) noexcept = default;
    RingModulator& operator=(const RingModulator&) noexcept = default;
    RingModulator(RingModulator&&) noexcept = default;
    RingModulator& operator=(RingModulator&&) noexcept = default;

    /// @brief Initialize all sub-components for given sample rate (FR-008).
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size (unused, for API consistency)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all oscillator and smoother state (FR-008).
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-002 through FR-006)
    // =========================================================================

    /// @brief Set carrier waveform (FR-002).
    void setCarrierWaveform(RingModCarrierWaveform waveform) noexcept;

    /// @brief Set frequency mode (FR-003).
    void setFreqMode(RingModFreqMode mode) noexcept;

    /// @brief Set carrier frequency in Hz for Free mode (FR-003).
    /// @param hz Clamped to [0.1, 20000]
    void setFrequency(float hz) noexcept;

    /// @brief Set note frequency from voice (FR-016).
    /// @param hz Voice note frequency in Hz
    void setNoteFrequency(float hz) noexcept;

    /// @brief Set carrier-to-note frequency ratio (FR-004).
    /// @param ratio Clamped to [0.25, 16.0]
    void setRatio(float ratio) noexcept;

    /// @brief Set carrier amplitude / drive (FR-005).
    /// @param amplitude Clamped to [0.0, 1.0]
    void setAmplitude(float amplitude) noexcept;

    /// @brief Set stereo spread amount (FR-006).
    /// @param spread Clamped to [0.0, 1.0]
    void setStereoSpread(float spread) noexcept;

    // =========================================================================
    // Processing (FR-007, FR-010)
    // =========================================================================

    /// @brief Process mono block in-place (FR-007).
    /// output[n] = input[n] * carrier[n] * amplitude
    /// @param buffer Input/output buffer
    /// @param numSamples Number of samples
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Process stereo block in-place (FR-007).
    /// Left uses center_freq - spread_offset, right uses center_freq + spread_offset.
    /// @param left Left channel buffer
    /// @param right Right channel buffer
    /// @param numSamples Number of samples
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if prepare() has been called (FR-008).
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Compute effective carrier frequency based on mode (FR-003).
    [[nodiscard]] float computeEffectiveFrequency() const noexcept;

    /// @brief Advance Gordon-Smith sine phasor by one sample (FR-002, R-001).
    /// @param s Sine state variable (updated in place)
    /// @param c Cosine state variable (updated in place)
    /// @param epsilon Phase increment coefficient
    /// @param counter Renormalization counter (updated in place)
    static void tickSineCarrier(float& s, float& c, float epsilon,
                                int& counter) noexcept;

    /// @brief Generate one carrier sample based on current waveform.
    /// @param smoother Frequency smoother for this channel
    /// @param sinState Sine state for Gordon-Smith (this channel)
    /// @param cosState Cosine state for Gordon-Smith (this channel)
    /// @param counter Renorm counter for Gordon-Smith (this channel)
    /// @param osc PolyBLEP oscillator for this channel
    /// @param noise Noise oscillator for this channel
    /// @return Carrier sample in [-1, +1]
    [[nodiscard]] float generateCarrierSample(
        OnePoleSmoother& smoother,
        float& sinState, float& cosState, int& counter,
        PolyBlepOscillator& osc, NoiseOscillator& noise) noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Parameters
    RingModCarrierWaveform carrierWaveform_ = RingModCarrierWaveform::Sine;
    RingModFreqMode freqMode_ = RingModFreqMode::NoteTrack;
    float freqHz_ = 440.0f;
    float noteFrequency_ = 440.0f;
    float ratio_ = 2.0f;
    float amplitude_ = 1.0f;
    float stereoSpread_ = 0.0f;

    // Gordon-Smith sine state (left/mono channel)
    float sinState_ = 0.0f;
    float cosState_ = 1.0f;
    int renormCounter_ = 0;

    // Gordon-Smith sine state (right channel, stereo)
    float sinStateR_ = 0.0f;
    float cosStateR_ = 1.0f;
    int renormCounterR_ = 0;

    // PolyBLEP oscillator (Triangle/Sawtooth/Square)
    PolyBlepOscillator polyBlepOsc_;
    PolyBlepOscillator polyBlepOscR_;

    // Noise oscillator
    NoiseOscillator noiseOsc_;
    NoiseOscillator noiseOscR_;

    // Frequency smoothers
    OnePoleSmoother freqSmoother_;
    OnePoleSmoother freqSmootherR_;
};

// =============================================================================
// Inline Implementations
// =============================================================================

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

inline void RingModulator::prepare(double sampleRate,
                                   [[maybe_unused]] size_t maxBlockSize) noexcept {
    sampleRate_ = sampleRate;
    prepared_ = true;

    const auto sampleRateF = static_cast<float>(sampleRate);

    // Configure frequency smoothers
    freqSmoother_.configure(kSmoothingTimeMs, sampleRateF);
    freqSmootherR_.configure(kSmoothingTimeMs, sampleRateF);

    // Snap smoothers to effective frequency (no transient on first block)
    const float effectiveFreq = computeEffectiveFrequency();
    freqSmoother_.snapTo(effectiveFreq);
    freqSmootherR_.snapTo(effectiveFreq);

    // Initialize Gordon-Smith sine state (phase = 0)
    sinState_ = 0.0f;
    cosState_ = 1.0f;
    renormCounter_ = 0;
    sinStateR_ = 0.0f;
    cosStateR_ = 1.0f;
    renormCounterR_ = 0;

    // Prepare PolyBLEP oscillators
    // Note: prepare() resets waveform to Sine, so we re-apply the user's
    // chosen waveform afterward via setCarrierWaveform().
    polyBlepOsc_.prepare(sampleRate);
    polyBlepOsc_.reset();
    polyBlepOscR_.prepare(sampleRate);
    polyBlepOscR_.reset();

    // Re-apply carrier waveform (PolyBLEP prepare resets waveform to Sine)
    setCarrierWaveform(carrierWaveform_);

    // Prepare noise oscillators
    noiseOsc_.prepare(sampleRate);
    noiseOsc_.setColor(NoiseColor::White);
    noiseOsc_.reset();
    noiseOscR_.prepare(sampleRate);
    noiseOscR_.setColor(NoiseColor::White);
    noiseOscR_.setSeed(54321); // Different seed for R channel decorrelation
    noiseOscR_.reset();
}

inline void RingModulator::reset() noexcept {
    // Reset Gordon-Smith sine state
    sinState_ = 0.0f;
    cosState_ = 1.0f;
    renormCounter_ = 0;
    sinStateR_ = 0.0f;
    cosStateR_ = 1.0f;
    renormCounterR_ = 0;

    // Reset sub-components
    polyBlepOsc_.reset();
    polyBlepOscR_.reset();
    noiseOsc_.reset();
    noiseOscR_.reset();

    // Reset smoothers
    freqSmoother_.reset();
    freqSmootherR_.reset();
}

// -----------------------------------------------------------------------------
// Parameter Setters
// -----------------------------------------------------------------------------

inline void RingModulator::setCarrierWaveform(
    RingModCarrierWaveform waveform) noexcept {
    carrierWaveform_ = waveform;

    // Map to PolyBLEP waveform for non-sine tonal carriers
    switch (waveform) {
        case RingModCarrierWaveform::Triangle:
            polyBlepOsc_.setWaveform(OscWaveform::Triangle);
            polyBlepOscR_.setWaveform(OscWaveform::Triangle);
            break;
        case RingModCarrierWaveform::Sawtooth:
            polyBlepOsc_.setWaveform(OscWaveform::Sawtooth);
            polyBlepOscR_.setWaveform(OscWaveform::Sawtooth);
            break;
        case RingModCarrierWaveform::Square:
            polyBlepOsc_.setWaveform(OscWaveform::Square);
            polyBlepOscR_.setWaveform(OscWaveform::Square);
            break;
        default:
            break; // Sine and Noise don't use PolyBLEP
    }
}

inline void RingModulator::setFreqMode(RingModFreqMode mode) noexcept {
    freqMode_ = mode;
    // Recompute effective frequency so the smoother can start tracking
    if (prepared_) {
        const float effectiveFreq = computeEffectiveFrequency();
        freqSmoother_.setTarget(effectiveFreq);
        const float spreadOffset = stereoSpread_ * kMaxSpreadOffsetHz;
        freqSmootherR_.setTarget(effectiveFreq + spreadOffset);
    }
}

inline void RingModulator::setFrequency(float hz) noexcept {
    freqHz_ = std::clamp(hz, kMinFreqHz, kMaxFreqHz);
    if (prepared_ && freqMode_ == RingModFreqMode::Free) {
        freqSmoother_.setTarget(freqHz_);
        const float spreadOffset = stereoSpread_ * kMaxSpreadOffsetHz;
        freqSmootherR_.setTarget(freqHz_ + spreadOffset);
    }
}

inline void RingModulator::setNoteFrequency(float hz) noexcept {
    noteFrequency_ = hz;
    if (prepared_ && freqMode_ == RingModFreqMode::NoteTrack) {
        const float effectiveFreq = computeEffectiveFrequency();
        freqSmoother_.setTarget(effectiveFreq);
        const float spreadOffset = stereoSpread_ * kMaxSpreadOffsetHz;
        freqSmootherR_.setTarget(effectiveFreq + spreadOffset);
    }
}

inline void RingModulator::setRatio(float ratio) noexcept {
    ratio_ = std::clamp(ratio, kMinRatio, kMaxRatio);
    if (prepared_ && freqMode_ == RingModFreqMode::NoteTrack) {
        const float effectiveFreq = computeEffectiveFrequency();
        freqSmoother_.setTarget(effectiveFreq);
        const float spreadOffset = stereoSpread_ * kMaxSpreadOffsetHz;
        freqSmootherR_.setTarget(effectiveFreq + spreadOffset);
    }
}

inline void RingModulator::setAmplitude(float amplitude) noexcept {
    amplitude_ = std::clamp(amplitude, 0.0f, 1.0f);
}

inline void RingModulator::setStereoSpread(float spread) noexcept {
    stereoSpread_ = std::clamp(spread, 0.0f, 1.0f);
    if (prepared_) {
        const float effectiveFreq = computeEffectiveFrequency();
        const float spreadOffset = stereoSpread_ * kMaxSpreadOffsetHz;
        freqSmoother_.setTarget(effectiveFreq - spreadOffset);
        freqSmootherR_.setTarget(effectiveFreq + spreadOffset);
    }
}

// -----------------------------------------------------------------------------
// Query
// -----------------------------------------------------------------------------

inline bool RingModulator::isPrepared() const noexcept {
    return prepared_;
}

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

inline float RingModulator::computeEffectiveFrequency() const noexcept {
    if (carrierWaveform_ == RingModCarrierWaveform::Noise) {
        return 0.0f; // Noise has no meaningful frequency
    }

    float freq = freqHz_;
    if (freqMode_ == RingModFreqMode::NoteTrack) {
        freq = noteFrequency_ * ratio_;
    }
    return std::clamp(freq, kMinFreqHz, kMaxFreqHz);
}

inline void RingModulator::tickSineCarrier(float& s, float& c, float epsilon,
                                           int& counter) noexcept {
    // Gordon-Smith magic circle:
    //   s_new = s + epsilon * c
    //   c_new = c - epsilon * s_new  (uses updated s!)
    s += epsilon * c;
    c -= epsilon * s;

    // Periodic renormalization to prevent floating-point drift
    if (++counter >= kRenormInterval) {
        counter = 0;
        const float r = std::sqrt(s * s + c * c);
        if (r > 0.0f) {
            s /= r;
            c /= r;
        }
    }
}

inline float RingModulator::generateCarrierSample(
    OnePoleSmoother& smoother,
    float& sinState, float& cosState, int& counter,
    PolyBlepOscillator& osc, NoiseOscillator& noise) noexcept {

    const float smoothedFreq = smoother.process();

    switch (carrierWaveform_) {
        case RingModCarrierWaveform::Sine: {
            // Compute epsilon from smoothed frequency
            const float epsilon = 2.0f * std::sin(
                kPi * smoothedFreq / static_cast<float>(sampleRate_));
            tickSineCarrier(sinState, cosState, epsilon, counter);
            // Clamp to [-1, +1] to bound output amplitude (FR-002).
            // Gordon-Smith sine values can slightly exceed unity between
            // renormalization intervals due to the approximation nature
            // of the magic circle rotation.
            return std::clamp(sinState, -1.0f, 1.0f);
        }
        case RingModCarrierWaveform::Triangle:
        case RingModCarrierWaveform::Sawtooth:
        case RingModCarrierWaveform::Square: {
            osc.setFrequency(smoothedFreq);
            return osc.process();
        }
        case RingModCarrierWaveform::Noise: {
            // Noise is frequency-independent (FR-009)
            // Still tick the smoother above to keep it running, but ignore freq
            return noise.process();
        }
    }

    return 0.0f; // Unreachable
}

// -----------------------------------------------------------------------------
// Processing
// -----------------------------------------------------------------------------

inline void RingModulator::processBlock(float* buffer,
                                        size_t numSamples) noexcept {
    if (!prepared_ || buffer == nullptr) return;

    // Update smoother target for center frequency
    const float effectiveFreq = computeEffectiveFrequency();
    freqSmoother_.setTarget(effectiveFreq);

    for (size_t i = 0; i < numSamples; ++i) {
        const float carrier = generateCarrierSample(
            freqSmoother_, sinState_, cosState_, renormCounter_,
            polyBlepOsc_, noiseOsc_);

        buffer[i] *= carrier * amplitude_;
    }
}

inline void RingModulator::processBlock(float* left, float* right,
                                        size_t numSamples) noexcept {
    if (!prepared_ || left == nullptr || right == nullptr) return;

    // Update smoother targets with spread offset
    const float effectiveFreq = computeEffectiveFrequency();
    const float spreadOffset = stereoSpread_ * kMaxSpreadOffsetHz;
    freqSmoother_.setTarget(effectiveFreq - spreadOffset);
    freqSmootherR_.setTarget(effectiveFreq + spreadOffset);

    for (size_t i = 0; i < numSamples; ++i) {
        // Left channel carrier
        const float carrierL = generateCarrierSample(
            freqSmoother_, sinState_, cosState_, renormCounter_,
            polyBlepOsc_, noiseOsc_);

        // Right channel carrier
        const float carrierR = generateCarrierSample(
            freqSmootherR_, sinStateR_, cosStateR_, renormCounterR_,
            polyBlepOscR_, noiseOscR_);

        left[i] *= carrierL * amplitude_;
        right[i] *= carrierR * amplitude_;
    }
}

} // namespace Krate::DSP
