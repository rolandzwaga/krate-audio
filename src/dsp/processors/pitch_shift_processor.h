// ==============================================================================
// Layer 2: DSP Processor
// PitchShiftProcessor - Pitch shifting with multiple quality modes
// ==============================================================================
// Feature: 016-pitch-shifter
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XIV: ODR-safe implementation (no duplicate definitions)
// ==============================================================================
//
// Three quality modes:
// - Simple: Delay-line modulation (zero latency, audible artifacts)
// - Granular: OLA grains (~46ms latency, good quality)
// - PhaseVocoder: STFT-based (~116ms latency, excellent quality)
//
// Dependencies (Layer 1):
// - DelayLine: For Simple mode delay buffer
// - STFT: For PhaseVocoder mode
// - WindowFunctions: For grain windowing
// - OnePoleSmoother: For parameter smoothing
//
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <array>
#include <memory>
#include <vector>
#include <algorithm>

// Layer 0 dependencies
#include "dsp/core/db_utils.h"

// Layer 1 dependencies
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/smoother.h"
#include "dsp/primitives/stft.h"
#include "dsp/primitives/spectral_buffer.h"
#include "dsp/core/window_functions.h"

namespace Iterum::DSP {

// ==============================================================================
// Enumerations
// ==============================================================================

/// Quality mode selection for pitch shifting algorithm
enum class PitchMode : std::uint8_t {
    Simple = 0,      ///< Delay-line modulation, zero latency, audible artifacts
    Granular = 1,    ///< OLA grains, ~46ms latency, good quality
    PhaseVocoder = 2 ///< STFT-based, ~116ms latency, excellent quality
};

// ==============================================================================
// Utility Functions
// ==============================================================================

/// @brief Convert semitones to pitch ratio
///
/// Formula: ratio = 2^(semitones/12)
///
/// @param semitones Pitch shift in semitones
/// @return Pitch ratio (e.g., 12 semitones -> 2.0)
[[nodiscard]] inline float pitchRatioFromSemitones(float semitones) noexcept {
    // Using std::pow for runtime accuracy
    // 2^(semitones/12) = exp(semitones * ln(2) / 12)
    constexpr float kLn2Over12 = 0.05776226504666210911810267678818f; // ln(2)/12
    return std::exp(semitones * kLn2Over12);
}

/// @brief Convert pitch ratio to semitones
///
/// Formula: semitones = 12 * log2(ratio)
///
/// @param ratio Pitch ratio (must be > 0)
/// @return Pitch shift in semitones (e.g., 2.0 -> 12 semitones)
[[nodiscard]] inline float semitonesFromPitchRatio(float ratio) noexcept {
    if (ratio <= 0.0f) return 0.0f;
    // 12 * log2(ratio) = 12 * ln(ratio) / ln(2)
    constexpr float k12OverLn2 = 17.31234049066756088832381614082f; // 12/ln(2)
    return std::log(ratio) * k12OverLn2;
}

// ==============================================================================
// PitchShiftProcessor Class
// ==============================================================================

/// @brief Layer 2 pitch shift processor with multiple quality modes
///
/// Shifts audio pitch by semitones without changing playback duration.
/// Supports three quality modes with different latency/quality trade-offs:
/// - Simple: Zero latency using delay-line modulation (audible artifacts)
/// - Granular: Low latency (~46ms) using overlap-add grains
/// - PhaseVocoder: High quality using STFT with phase locking (~116ms latency)
///
/// Formant preservation is available in Granular and PhaseVocoder modes
/// to prevent the "chipmunk" effect when shifting vocals.
///
/// Thread Safety:
/// - Parameter setters are thread-safe (atomic writes)
/// - process() must be called from a single thread
/// - Mode/formant changes are safe between process() calls
///
/// Real-Time Safety:
/// - No memory allocation in process()
/// - No blocking operations
/// - Pre-allocate all buffers in prepare()
///
/// Usage:
/// @code
/// PitchShiftProcessor shifter;
/// shifter.prepare(44100.0, 512);
/// shifter.setMode(PitchMode::Granular);
/// shifter.setSemitones(7.0f);  // Perfect fifth up
///
/// // In audio callback:
/// shifter.process(input, output, numSamples);
/// @endcode
class PitchShiftProcessor {
public:
    //=========================================================================
    // Construction
    //=========================================================================

    /// @brief Construct pitch shift processor with default settings
    ///
    /// Default state:
    /// - Mode: Granular
    /// - Semitones: 0
    /// - Cents: 0
    /// - Formant preservation: disabled
    ///
    /// Must call prepare() before process().
    PitchShiftProcessor() noexcept;

    /// @brief Destructor
    ~PitchShiftProcessor() noexcept;

    // Non-copyable (internal state is complex)
    PitchShiftProcessor(const PitchShiftProcessor&) = delete;
    PitchShiftProcessor& operator=(const PitchShiftProcessor&) = delete;

    // Movable
    PitchShiftProcessor(PitchShiftProcessor&&) noexcept;
    PitchShiftProcessor& operator=(PitchShiftProcessor&&) noexcept;

    //=========================================================================
    // Lifecycle
    //=========================================================================

    /// @brief Prepare processor for given sample rate and block size
    ///
    /// Allocates all internal buffers. Must be called before process().
    /// Can be called multiple times to change sample rate.
    /// Implicitly calls reset().
    ///
    /// @param sampleRate Sample rate in Hz [44100, 192000]
    /// @param maxBlockSize Maximum samples per process() call [1, 8192]
    ///
    /// @pre sampleRate >= 44100.0 && sampleRate <= 192000.0
    /// @pre maxBlockSize >= 1 && maxBlockSize <= 8192
    /// @post isPrepared() == true
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state to initial conditions
    ///
    /// Clears delay buffers, grain states, phase accumulators.
    /// Does not deallocate memory or change parameters.
    /// Safe to call from audio thread.
    ///
    /// @pre isPrepared() == true (otherwise no-op)
    void reset() noexcept;

    /// @brief Check if processor is ready for processing
    /// @return true if prepare() has been called successfully
    [[nodiscard]] bool isPrepared() const noexcept;

    //=========================================================================
    // Processing
    //=========================================================================

    /// @brief Process audio through pitch shifter
    ///
    /// Applies pitch shift to input samples and writes to output.
    /// Supports in-place processing (input == output).
    ///
    /// @param input Pointer to input samples
    /// @param output Pointer to output samples (can equal input)
    /// @param numSamples Number of samples to process [1, maxBlockSize]
    ///
    /// @pre isPrepared() == true
    /// @pre input != nullptr
    /// @pre output != nullptr
    /// @pre numSamples <= maxBlockSize passed to prepare()
    ///
    /// @note Real-time safe: no allocations, no blocking
    void process(const float* input, float* output, std::size_t numSamples) noexcept;

    //=========================================================================
    // Parameters - Mode
    //=========================================================================

    /// @brief Set quality mode
    ///
    /// Changing mode during playback causes a brief crossfade.
    /// Latency reporting changes immediately.
    ///
    /// @param mode Quality mode (Simple, Granular, or PhaseVocoder)
    void setMode(PitchMode mode) noexcept;

    /// @brief Get current quality mode
    /// @return Current PitchMode
    [[nodiscard]] PitchMode getMode() const noexcept;

    //=========================================================================
    // Parameters - Pitch
    //=========================================================================

    /// @brief Set pitch shift in semitones
    ///
    /// Positive values shift pitch up, negative values shift down.
    /// Combined with cents for total shift.
    /// Changes are smoothed to prevent clicks.
    ///
    /// @param semitones Pitch shift in semitones [-24, +24]
    ///
    /// @note Values outside range are clamped
    void setSemitones(float semitones) noexcept;

    /// @brief Get pitch shift in semitones
    /// @return Current semitone setting [-24, +24]
    [[nodiscard]] float getSemitones() const noexcept;

    /// @brief Set fine pitch adjustment in cents
    ///
    /// 100 cents = 1 semitone.
    /// Added to semitones for total pitch shift.
    /// Changes are smoothed to prevent clicks.
    ///
    /// @param cents Fine pitch adjustment [-100, +100]
    ///
    /// @note Values outside range are clamped
    void setCents(float cents) noexcept;

    /// @brief Get fine pitch adjustment in cents
    /// @return Current cents setting [-100, +100]
    [[nodiscard]] float getCents() const noexcept;

    /// @brief Get current pitch ratio
    ///
    /// Computed as: 2^((semitones + cents/100) / 12)
    ///
    /// @return Current pitch ratio (e.g., 2.0 for octave up, 0.5 for octave down)
    [[nodiscard]] float getPitchRatio() const noexcept;

    //=========================================================================
    // Parameters - Formant Preservation
    //=========================================================================

    /// @brief Enable or disable formant preservation
    ///
    /// When enabled, attempts to preserve vocal formant frequencies
    /// during pitch shifting to avoid "chipmunk" effect.
    ///
    /// Only effective in Granular and PhaseVocoder modes.
    /// Simple mode ignores this setting.
    ///
    /// @param enable true to enable, false to disable
    void setFormantPreserve(bool enable) noexcept;

    /// @brief Get formant preservation state
    /// @return true if formant preservation is enabled
    [[nodiscard]] bool getFormantPreserve() const noexcept;

    //=========================================================================
    // Latency
    //=========================================================================

    /// @brief Get processing latency in samples
    ///
    /// Returns the algorithmic latency for the current mode:
    /// - Simple: 0 samples
    /// - Granular: ~grain_size samples (~2048 at 44.1kHz)
    /// - PhaseVocoder: FFT_SIZE + HOP_SIZE samples (~5120 at 44.1kHz)
    ///
    /// @return Latency in samples for current mode
    ///
    /// @pre isPrepared() == true
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

private:
    //=========================================================================
    // Internal Implementation
    //=========================================================================

    // Forward declaration of implementation
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

// ==============================================================================
// SimplePitchShifter - Internal class for delay-line modulation
// ==============================================================================

/// @brief Zero-latency pitch shifter using dual delay-line crossfade
///
/// Algorithm based on MathWorks delay-based pitch shifter and DSPRELATED theory:
/// The pitch shift comes from TIME-VARYING DELAY (Doppler effect).
///
/// Key physics: ω_out = ω_in × (1 - dDelay/dt)
/// For pitch ratio R: dDelay/dt = 1 - R
/// - R > 1 (pitch up): delay DECREASES at rate (R-1) samples per sample
/// - R < 1 (pitch down): delay INCREASES at rate (1-R) samples per sample
///
/// Implementation:
/// - Two delays ramping in opposite directions
/// - When one delay reaches its limit, reset it and crossfade to the other
/// - Continuous half-sine crossfade preserves energy
///
/// Sources:
/// - https://www.mathworks.com/help/audio/ug/delay-based-pitch-shifter.html
/// - https://www.dsprelated.com/freebooks/pasp/Time_Varying_Delay_Effects.html
/// - https://www.katjaas.nl/pitchshiftlowlatency/pitchshiftlowlatency.html
class SimplePitchShifter {
public:
    static constexpr float kWindowTimeMs = 50.0f;  // 50ms crossfade window
    static constexpr float kPi = 3.14159265358979323846f;

    SimplePitchShifter() = default;

    void prepare(double sampleRate, std::size_t /*maxBlockSize*/) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Delay range in samples (~2205 at 44.1kHz for 50ms window)
        maxDelay_ = sampleRate_ * kWindowTimeMs * 0.001f;
        minDelay_ = 1.0f;  // Minimum safe delay

        // Buffer must be large enough to hold max delay + safety margin
        bufferSize_ = static_cast<std::size_t>(maxDelay_) * 2 + 64;
        buffer_.resize(bufferSize_, 0.0f);

        reset();
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writePos_ = 0;

        // delay1 starts at max, is the "active" delay
        // delay2 will be set when we need to crossfade
        delay1_ = maxDelay_;
        delay2_ = maxDelay_;  // Will be reset when needed
        crossfadePhase_ = 0.0f;  // 0 = use delay1 only
        needsCrossfade_ = false;
    }

    void process(const float* input, float* output, std::size_t numSamples,
                 float pitchRatio) noexcept {
        // At unity pitch, just pass through
        if (std::abs(pitchRatio - 1.0f) < 0.0001f) {
            if (input != output) {
                std::copy(input, input + numSamples, output);
            }
            return;
        }

        // Delay-based pitch shifter using Doppler effect:
        //
        // Key physics: ω_out = ω_in × (1 - dDelay/dt)
        // For pitch ratio R: dDelay/dt = 1 - R
        //
        // R = 2.0: delay decreases by 1 sample/sample (pitch UP)
        // R = 0.5: delay increases by 0.5 samples/sample (pitch DOWN)
        //
        // Algorithm:
        // 1. Delay1 is the "active" delay, ramping in the appropriate direction
        // 2. When delay1 approaches its limit, reset delay2 to the START and crossfade
        // 3. After crossfade completes, delay2 becomes active (swap roles)
        // 4. Repeat

        const float delayChange = 1.0f - pitchRatio;  // Negative for pitch up
        const float bufferSizeF = static_cast<float>(bufferSize_);

        // Crossfade over ~25% of the delay range for smooth transitions
        const float crossfadeLength = maxDelay_ * 0.25f;
        const float crossfadeRate = 1.0f / crossfadeLength;

        // Threshold for triggering crossfade (when delay gets close to limit)
        const float triggerThreshold = crossfadeLength;

        for (std::size_t i = 0; i < numSamples; ++i) {
            // Write input to buffer
            buffer_[writePos_] = input[i];

            // Read from both delay taps
            float readPos1 = static_cast<float>(writePos_) - delay1_;
            float readPos2 = static_cast<float>(writePos_) - delay2_;

            // Wrap to valid buffer range
            if (readPos1 < 0.0f) readPos1 += bufferSizeF;
            if (readPos2 < 0.0f) readPos2 += bufferSizeF;

            float sample1 = readInterpolated(readPos1);
            float sample2 = readInterpolated(readPos2);

            // Half-sine crossfade for constant power
            float gain1 = std::cos(crossfadePhase_ * kPi * 0.5f);
            float gain2 = std::sin(crossfadePhase_ * kPi * 0.5f);

            output[i] = sample1 * gain1 + sample2 * gain2;

            // Update the active delay (always delay1 conceptually, but we swap)
            delay1_ += delayChange;
            delay2_ += delayChange;

            // Check if we need to start a crossfade
            if (!needsCrossfade_) {
                // For pitch UP (delayChange < 0): delay decreases toward minDelay_
                // For pitch DOWN (delayChange > 0): delay increases toward maxDelay_
                bool approachingLimit = (delayChange < 0.0f && delay1_ <= minDelay_ + triggerThreshold) ||
                                        (delayChange > 0.0f && delay1_ >= maxDelay_ - triggerThreshold);

                if (approachingLimit) {
                    // Reset delay2 to the START of the cycle
                    delay2_ = (pitchRatio > 1.0f) ? maxDelay_ : minDelay_;
                    needsCrossfade_ = true;
                }
            }

            // Manage crossfade
            if (needsCrossfade_) {
                crossfadePhase_ += crossfadeRate;

                if (crossfadePhase_ >= 1.0f) {
                    // Crossfade complete - swap delays
                    crossfadePhase_ = 0.0f;
                    needsCrossfade_ = false;

                    // Swap delay1 and delay2 (delay2 becomes the new active)
                    std::swap(delay1_, delay2_);
                }
            }

            // Clamp delays to valid range (safety, shouldn't normally hit this)
            delay1_ = std::clamp(delay1_, minDelay_, maxDelay_);
            delay2_ = std::clamp(delay2_, minDelay_, maxDelay_);

            // Advance write position
            writePos_ = (writePos_ + 1) % bufferSize_;
        }
    }

private:
    [[nodiscard]] float readInterpolated(float pos) const noexcept {
        const std::size_t idx0 = static_cast<std::size_t>(pos) % bufferSize_;
        const std::size_t idx1 = (idx0 + 1) % bufferSize_;
        const float frac = pos - std::floor(pos);
        return buffer_[idx0] * (1.0f - frac) + buffer_[idx1] * frac;
    }

    std::vector<float> buffer_;
    std::size_t bufferSize_ = 0;
    std::size_t writePos_ = 0;
    float delay1_ = 0.0f;
    float delay2_ = 0.0f;
    float crossfadePhase_ = 0.0f;
    float maxDelay_ = 0.0f;
    float minDelay_ = 1.0f;
    float sampleRate_ = 44100.0f;
    bool needsCrossfade_ = false;
};

// ==============================================================================
// GranularPitchShifter - Dual-delay with Hann crossfade
// ==============================================================================

/// @brief Higher quality pitch shifter using Hann window crossfades
///
/// Quality improvements over SimplePitchShifter:
/// 1. Hann window crossfade (vs half-sine) - smoother transitions
/// 2. Longer window time (46ms vs 50ms) - more time for crossfade
/// 3. Longer crossfade region (33% vs 25%) - more overlap during transitions
///
/// Uses the same dual-delay architecture as SimplePitchShifter but with
/// Hann windows for crossfading. The Hann window provides smoother
/// amplitude transitions than half-sine, reducing audible artifacts.
///
/// Key physics: ω_out = ω_in × (1 - dDelay/dt)
/// For pitch ratio R: dDelay/dt = 1 - R
///
/// Sources:
/// - https://www.mathworks.com/help/audio/ug/delay-based-pitch-shifter.html
/// - https://www.katjaas.nl/pitchshiftlowlatency/pitchshiftlowlatency.html
///
/// Latency: ~grainSize samples (~46ms at 44.1kHz) - reports latency unlike Simple mode
class GranularPitchShifter {
public:
    static constexpr float kWindowTimeMs = 46.0f;  // Longer than Simple (50ms) for spec
    static constexpr float kPi = 3.14159265358979323846f;

    GranularPitchShifter() = default;

    void prepare(double sampleRate, std::size_t /*maxBlockSize*/) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Delay range in samples (~2029 at 44.1kHz for 46ms)
        maxDelay_ = sampleRate_ * kWindowTimeMs * 0.001f;
        minDelay_ = 1.0f;

        // Store grain size for latency reporting
        grainSize_ = static_cast<std::size_t>(maxDelay_);

        // Buffer must hold max delay + safety margin
        bufferSize_ = static_cast<std::size_t>(maxDelay_) * 2 + 64;
        buffer_.resize(bufferSize_, 0.0f);

        // Pre-compute Hann window for crossfade (using first half: 0 to 1)
        // The first half of a Hann window rises smoothly from 0 to 1
        // We allocate full window but only use first half for fade-in
        crossfadeWindowSize_ = static_cast<std::size_t>(maxDelay_ * 0.5f);
        const std::size_t fullWindowSize = crossfadeWindowSize_ * 2;
        crossfadeWindow_.resize(fullWindowSize);
        Window::generateHann(crossfadeWindow_.data(), fullWindowSize);
        // Only first half (indices 0 to crossfadeWindowSize_-1) will be used

        reset();
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writePos_ = 0;

        delay1_ = maxDelay_;
        delay2_ = maxDelay_;
        crossfadePhase_ = 0.0f;
        needsCrossfade_ = false;
    }

    void process(const float* input, float* output, std::size_t numSamples,
                 float pitchRatio) noexcept {
        // At unity pitch, pass through
        if (std::abs(pitchRatio - 1.0f) < 0.0001f) {
            if (input != output) {
                std::copy(input, input + numSamples, output);
            }
            return;
        }

        pitchRatio = std::clamp(pitchRatio, 0.25f, 4.0f);

        const float delayChange = 1.0f - pitchRatio;
        const float bufferSizeF = static_cast<float>(bufferSize_);

        // Longer crossfade (33% of delay range) for smoother transitions
        const float crossfadeLength = maxDelay_ * 0.33f;
        const float crossfadeRate = 1.0f / crossfadeLength;
        const float triggerThreshold = crossfadeLength;

        for (std::size_t i = 0; i < numSamples; ++i) {
            buffer_[writePos_] = input[i];

            // Read from both delay taps
            float readPos1 = static_cast<float>(writePos_) - delay1_;
            float readPos2 = static_cast<float>(writePos_) - delay2_;

            if (readPos1 < 0.0f) readPos1 += bufferSizeF;
            if (readPos2 < 0.0f) readPos2 += bufferSizeF;

            float sample1 = readInterpolated(readPos1);
            float sample2 = readInterpolated(readPos2);

            // Hann window crossfade (smoother than half-sine)
            // Map crossfadePhase [0,1] to Hann window index
            std::size_t fadeIdx = static_cast<std::size_t>(crossfadePhase_ *
                                  static_cast<float>(crossfadeWindowSize_));
            if (fadeIdx >= crossfadeWindowSize_) fadeIdx = crossfadeWindowSize_ - 1;

            // Hann window goes 0 -> 1 over first half
            float gain2 = crossfadeWindow_[fadeIdx];
            float gain1 = 1.0f - gain2;

            output[i] = sample1 * gain1 + sample2 * gain2;

            // Update both delays
            delay1_ += delayChange;
            delay2_ += delayChange;

            // Check if we need to start a crossfade
            if (!needsCrossfade_) {
                bool approachingLimit = (delayChange < 0.0f && delay1_ <= minDelay_ + triggerThreshold) ||
                                        (delayChange > 0.0f && delay1_ >= maxDelay_ - triggerThreshold);

                if (approachingLimit) {
                    delay2_ = (pitchRatio > 1.0f) ? maxDelay_ : minDelay_;
                    needsCrossfade_ = true;
                }
            }

            // Manage crossfade
            if (needsCrossfade_) {
                crossfadePhase_ += crossfadeRate;

                if (crossfadePhase_ >= 1.0f) {
                    crossfadePhase_ = 0.0f;
                    needsCrossfade_ = false;
                    std::swap(delay1_, delay2_);
                }
            }

            delay1_ = std::clamp(delay1_, minDelay_, maxDelay_);
            delay2_ = std::clamp(delay2_, minDelay_, maxDelay_);

            writePos_ = (writePos_ + 1) % bufferSize_;
        }
    }

    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return grainSize_;
    }

private:
    [[nodiscard]] float readInterpolated(float pos) const noexcept {
        const std::size_t idx0 = static_cast<std::size_t>(pos) % bufferSize_;
        const std::size_t idx1 = (idx0 + 1) % bufferSize_;
        const float frac = pos - std::floor(pos);
        return buffer_[idx0] * (1.0f - frac) + buffer_[idx1] * frac;
    }

    std::vector<float> buffer_;
    std::vector<float> crossfadeWindow_;
    std::size_t grainSize_ = 0;
    std::size_t crossfadeWindowSize_ = 0;
    std::size_t bufferSize_ = 0;
    std::size_t writePos_ = 0;
    float delay1_ = 0.0f;
    float delay2_ = 0.0f;
    float crossfadePhase_ = 0.0f;
    float maxDelay_ = 0.0f;
    float minDelay_ = 1.0f;
    float sampleRate_ = 44100.0f;
    bool needsCrossfade_ = false;
};

// ==============================================================================
// PhaseVocoderPitchShifter - STFT-based pitch shifting
// ==============================================================================

/// @brief High-quality pitch shifter using phase vocoder algorithm
///
/// Uses Short-Time Fourier Transform (STFT) with phase manipulation to
/// achieve high-quality pitch shifting. The algorithm works by:
/// 1. Analyzing audio into overlapping spectral frames
/// 2. Computing instantaneous frequencies from phase differences
/// 3. Scaling the spectrum by the pitch ratio with phase coherence
/// 4. Resynthesizing using overlap-add
///
/// Quality is significantly higher than delay-line methods, especially
/// for large pitch shifts, at the cost of ~116ms latency.
///
/// Sources:
/// - Dolson, "The Phase Vocoder: A Tutorial"
/// - Laroche & Dolson, "Improved Phase Vocoder Time-Scale Modification"
///
/// Latency: FFT_SIZE + HOP_SIZE samples (~116ms at 44.1kHz with 4096 FFT)
class PhaseVocoderPitchShifter {
public:
    static constexpr std::size_t kFFTSize = 4096;      // ~93ms at 44.1kHz
    static constexpr std::size_t kHopSize = 1024;      // 25% overlap (4x)
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTwoPi = 2.0f * kPi;

    PhaseVocoderPitchShifter() = default;

    void prepare(double sampleRate, std::size_t /*maxBlockSize*/) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Prepare STFT analysis
        stft_.prepare(kFFTSize, kHopSize, WindowType::Hann);

        // Prepare overlap-add synthesis
        ola_.prepare(kFFTSize, kHopSize, WindowType::Hann);

        // Prepare spectral buffers
        analysisSpectrum_.prepare(kFFTSize);
        synthesisSpectrum_.prepare(kFFTSize);

        // Allocate phase tracking arrays
        const std::size_t numBins = kFFTSize / 2 + 1;
        prevPhase_.resize(numBins, 0.0f);
        synthPhase_.resize(numBins, 0.0f);
        magnitude_.resize(numBins, 0.0f);
        frequency_.resize(numBins, 0.0f);

        // Calculate expected phase advance per bin per hop
        // For bin k: expected_advance = 2π * k * hop_size / fft_size
        expectedPhaseInc_.resize(numBins);
        for (std::size_t k = 0; k < numBins; ++k) {
            expectedPhaseInc_[k] = kTwoPi * static_cast<float>(k) *
                                   static_cast<float>(kHopSize) /
                                   static_cast<float>(kFFTSize);
        }

        // Output buffer for resampled output
        outputBuffer_.resize(kFFTSize * 4, 0.0f);
        outputReadPos_ = 0;
        outputWritePos_ = 0;
        outputSamplesReady_ = 0;

        // Input buffer for accumulating samples
        inputBuffer_.resize(kFFTSize * 4, 0.0f);
        inputWritePos_ = 0;
        inputSamplesReady_ = 0;

        reset();
    }

    void reset() noexcept {
        stft_.reset();
        ola_.reset();
        analysisSpectrum_.reset();
        synthesisSpectrum_.reset();

        std::fill(prevPhase_.begin(), prevPhase_.end(), 0.0f);
        std::fill(synthPhase_.begin(), synthPhase_.end(), 0.0f);
        std::fill(inputBuffer_.begin(), inputBuffer_.end(), 0.0f);
        std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);

        inputWritePos_ = 0;
        inputSamplesReady_ = 0;
        outputReadPos_ = 0;
        outputWritePos_ = 0;
        outputSamplesReady_ = 0;
    }

    void process(const float* input, float* output, std::size_t numSamples,
                 float pitchRatio) noexcept {
        // At unity pitch, pass through (with latency compensation)
        if (std::abs(pitchRatio - 1.0f) < 0.0001f) {
            processUnityPitch(input, output, numSamples);
            return;
        }

        pitchRatio = std::clamp(pitchRatio, 0.25f, 4.0f);

        // Push input samples to STFT
        stft_.pushSamples(input, numSamples);

        // Process as many frames as possible
        while (stft_.canAnalyze()) {
            // Analyze frame
            stft_.analyze(analysisSpectrum_);

            // Phase vocoder pitch shift
            processFrame(pitchRatio);

            // Synthesize frame
            ola_.synthesize(synthesisSpectrum_);
        }

        // Pull available output samples
        std::size_t samplesToOutput = std::min(numSamples, ola_.samplesAvailable());
        if (samplesToOutput > 0) {
            ola_.pullSamples(output, samplesToOutput);
        }

        // Zero any remaining output (during startup latency)
        for (std::size_t i = samplesToOutput; i < numSamples; ++i) {
            output[i] = 0.0f;
        }
    }

    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        // Total latency: FFT size + hop size
        return kFFTSize + kHopSize;
    }

private:
    /// @brief Process unity pitch ratio with proper latency
    void processUnityPitch(const float* input, float* output, std::size_t numSamples) noexcept {
        // For unity, we still need to maintain consistent latency
        stft_.pushSamples(input, numSamples);

        while (stft_.canAnalyze()) {
            stft_.analyze(analysisSpectrum_);
            // Pass through spectrum unchanged
            ola_.synthesize(analysisSpectrum_);
        }

        std::size_t samplesToOutput = std::min(numSamples, ola_.samplesAvailable());
        if (samplesToOutput > 0) {
            ola_.pullSamples(output, samplesToOutput);
        }

        for (std::size_t i = samplesToOutput; i < numSamples; ++i) {
            output[i] = 0.0f;
        }
    }

    /// @brief Phase vocoder frame processing
    void processFrame(float pitchRatio) noexcept {
        const std::size_t numBins = kFFTSize / 2 + 1;

        // Step 1: Extract magnitude and compute instantaneous frequency
        for (std::size_t k = 0; k < numBins; ++k) {
            // Get magnitude and phase
            magnitude_[k] = analysisSpectrum_.getMagnitude(k);
            float phase = analysisSpectrum_.getPhase(k);

            // Compute phase difference from previous frame
            float phaseDiff = phase - prevPhase_[k];
            prevPhase_[k] = phase;

            // Subtract expected phase increment to get deviation
            float deviation = phaseDiff - expectedPhaseInc_[k];

            // Wrap deviation to [-π, π]
            deviation = wrapPhase(deviation);

            // Compute true frequency as deviation from bin center
            // true_freq = bin_freq + deviation / (2π * hopSize / sampleRate)
            // But we store as phase per hop for synthesis
            frequency_[k] = expectedPhaseInc_[k] + deviation;
        }

        // Step 2: Pitch shift by scaling frequencies and resampling spectrum
        synthesisSpectrum_.reset();

        for (std::size_t k = 0; k < numBins; ++k) {
            // Map source bin to destination bin
            float srcBin = static_cast<float>(k) / pitchRatio;

            // Skip if source bin is out of range
            if (srcBin >= static_cast<float>(numBins - 1)) continue;

            // Linear interpolation for magnitude
            std::size_t srcBin0 = static_cast<std::size_t>(srcBin);
            std::size_t srcBin1 = srcBin0 + 1;
            if (srcBin1 >= numBins) srcBin1 = numBins - 1;

            float frac = srcBin - static_cast<float>(srcBin0);
            float mag = magnitude_[srcBin0] * (1.0f - frac) + magnitude_[srcBin1] * frac;

            // Scale frequency by pitch ratio
            float freq = frequency_[srcBin0] * pitchRatio;

            // Accumulate synthesis phase
            synthPhase_[k] += freq;
            synthPhase_[k] = wrapPhase(synthPhase_[k]);

            // Set synthesis bin (Cartesian form)
            float real = mag * std::cos(synthPhase_[k]);
            float imag = mag * std::sin(synthPhase_[k]);
            synthesisSpectrum_.setCartesian(k, real, imag);
        }
    }

    /// @brief Wrap phase to [-π, π]
    [[nodiscard]] static float wrapPhase(float phase) noexcept {
        while (phase > kPi) phase -= kTwoPi;
        while (phase < -kPi) phase += kTwoPi;
        return phase;
    }

    // STFT analysis and synthesis
    STFT stft_;
    OverlapAdd ola_;

    // Spectral buffers
    SpectralBuffer analysisSpectrum_;
    SpectralBuffer synthesisSpectrum_;

    // Phase vocoder state
    std::vector<float> prevPhase_;      // Previous frame phases
    std::vector<float> synthPhase_;     // Accumulated synthesis phases
    std::vector<float> magnitude_;      // Temporary magnitude storage
    std::vector<float> frequency_;      // Instantaneous frequencies
    std::vector<float> expectedPhaseInc_; // Expected phase increment per bin

    // I/O buffers for sample-level processing
    std::vector<float> inputBuffer_;
    std::vector<float> outputBuffer_;
    std::size_t inputWritePos_ = 0;
    std::size_t inputSamplesReady_ = 0;
    std::size_t outputReadPos_ = 0;
    std::size_t outputWritePos_ = 0;
    std::size_t outputSamplesReady_ = 0;

    float sampleRate_ = 44100.0f;
};

// ==============================================================================
// PitchShiftProcessor Implementation
// ==============================================================================

struct PitchShiftProcessor::Impl {
    // Parameters
    PitchMode mode = PitchMode::Simple;  // Default to Simple for US1
    float semitones = 0.0f;
    float cents = 0.0f;
    bool formantPreserve = false;
    double sampleRate = 44100.0;
    std::size_t maxBlockSize = 512;
    bool prepared = false;

    // Internal processors
    SimplePitchShifter simpleShifter;
    GranularPitchShifter granularShifter;
    PhaseVocoderPitchShifter phaseVocoderShifter;

    // Parameter smoothers
    OnePoleSmoother semitoneSmoother;
    OnePoleSmoother centsSmoother;
};

inline PitchShiftProcessor::PitchShiftProcessor() noexcept
    : pImpl_(std::make_unique<Impl>()) {}

inline PitchShiftProcessor::~PitchShiftProcessor() noexcept = default;

inline PitchShiftProcessor::PitchShiftProcessor(PitchShiftProcessor&&) noexcept = default;
inline PitchShiftProcessor& PitchShiftProcessor::operator=(PitchShiftProcessor&&) noexcept = default;

inline void PitchShiftProcessor::prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
    pImpl_->sampleRate = sampleRate;
    pImpl_->maxBlockSize = maxBlockSize;

    // Prepare all internal shifters
    pImpl_->simpleShifter.prepare(sampleRate, maxBlockSize);
    pImpl_->granularShifter.prepare(sampleRate, maxBlockSize);
    pImpl_->phaseVocoderShifter.prepare(sampleRate, maxBlockSize);

    // Configure parameter smoothers (10ms smoothing time)
    constexpr float kSmoothTimeMs = 10.0f;
    pImpl_->semitoneSmoother.configure(kSmoothTimeMs, static_cast<float>(sampleRate));
    pImpl_->centsSmoother.configure(kSmoothTimeMs, static_cast<float>(sampleRate));

    pImpl_->prepared = true;
    reset();
}

inline void PitchShiftProcessor::reset() noexcept {
    if (!pImpl_->prepared) return;

    pImpl_->simpleShifter.reset();
    pImpl_->granularShifter.reset();
    pImpl_->phaseVocoderShifter.reset();
    pImpl_->semitoneSmoother.reset();
    pImpl_->semitoneSmoother.setTarget(pImpl_->semitones);
    pImpl_->centsSmoother.reset();
    pImpl_->centsSmoother.setTarget(pImpl_->cents);
}

inline bool PitchShiftProcessor::isPrepared() const noexcept {
    return pImpl_->prepared;
}

inline void PitchShiftProcessor::process(const float* input, float* output,
                                         std::size_t numSamples) noexcept {
    if (!pImpl_->prepared || input == nullptr || output == nullptr || numSamples == 0) {
        return;
    }

    // Update smoother targets
    pImpl_->semitoneSmoother.setTarget(pImpl_->semitones);
    pImpl_->centsSmoother.setTarget(pImpl_->cents);

    // Calculate pitch ratio
    // Note: Per-sample smoothing would be ideal but adds complexity.
    // For now, use direct parameters and snap smoothers to maintain consistency.
    // TODO: Implement proper per-sample smoothing for parameter automation (US6)
    float totalSemitones = pImpl_->semitones + pImpl_->cents / 100.0f;
    pImpl_->semitoneSmoother.snapToTarget();
    pImpl_->centsSmoother.snapToTarget();

    float pitchRatio = pitchRatioFromSemitones(totalSemitones);

    // Route to appropriate processor based on mode
    switch (pImpl_->mode) {
        case PitchMode::Simple:
            pImpl_->simpleShifter.process(input, output, numSamples, pitchRatio);
            break;

        case PitchMode::Granular:
            pImpl_->granularShifter.process(input, output, numSamples, pitchRatio);
            break;

        case PitchMode::PhaseVocoder:
            pImpl_->phaseVocoderShifter.process(input, output, numSamples, pitchRatio);
            break;
    }
}

inline void PitchShiftProcessor::setMode(PitchMode mode) noexcept {
    pImpl_->mode = mode;
}

inline PitchMode PitchShiftProcessor::getMode() const noexcept {
    return pImpl_->mode;
}

inline void PitchShiftProcessor::setSemitones(float semitones) noexcept {
    // Clamp to valid range
    pImpl_->semitones = std::clamp(semitones, -24.0f, 24.0f);
}

inline float PitchShiftProcessor::getSemitones() const noexcept {
    return pImpl_->semitones;
}

inline void PitchShiftProcessor::setCents(float cents) noexcept {
    // Clamp to valid range
    pImpl_->cents = std::clamp(cents, -100.0f, 100.0f);
}

inline float PitchShiftProcessor::getCents() const noexcept {
    return pImpl_->cents;
}

inline float PitchShiftProcessor::getPitchRatio() const noexcept {
    float totalSemitones = pImpl_->semitones + pImpl_->cents / 100.0f;
    return pitchRatioFromSemitones(totalSemitones);
}

inline void PitchShiftProcessor::setFormantPreserve(bool enable) noexcept {
    pImpl_->formantPreserve = enable;
}

inline bool PitchShiftProcessor::getFormantPreserve() const noexcept {
    return pImpl_->formantPreserve;
}

inline std::size_t PitchShiftProcessor::getLatencySamples() const noexcept {
    if (!pImpl_->prepared) return 0;

    switch (pImpl_->mode) {
        case PitchMode::Simple:
            return 0;  // Zero latency
        case PitchMode::Granular:
            // Use actual grain size from the shifter
            return pImpl_->granularShifter.getLatencySamples();
        case PitchMode::PhaseVocoder:
            // Use actual latency from the phase vocoder
            return pImpl_->phaseVocoderShifter.getLatencySamples();
    }
    return 0;
}

} // namespace Iterum::DSP
