// ==============================================================================
// Layer 2: DSP Processor - PitchShiftProcessor
// ==============================================================================
// Pitch shifting with multiple quality modes (Simple, Granular, PhaseVocoder).
// Feature: 016-pitch-shifter
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle VIII: DSP algorithms must be independently testable
// - Principle IX: Layer 2 (depends on Layer 0-1)
// - Principle XIV: ODR-safe implementation (no duplicate definitions)
//
// Quality Modes:
// - Simple: Delay-line modulation (zero latency, audible artifacts)
// - Granular: OLA grains (~46ms latency, good quality)
// - PhaseVocoder: STFT-based (~116ms latency, excellent quality)
// ==============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Layer 0 dependencies
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/pitch_utils.h>

// Layer 1 dependencies
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/pitch_detector.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/spectral_utils.h>
#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/core/window_functions.h>

// Layer 1 dependencies (spectral transient detection)
#include <krate/dsp/primitives/spectral_transient_detector.h>

// Layer 2 dependencies (extracted)
#include <krate/dsp/processors/formant_preserver.h>

namespace Krate::DSP {

// ==============================================================================
// Enumerations
// ==============================================================================

/// Quality mode selection for pitch shifting algorithm
enum class PitchMode : std::uint8_t {
    Simple = 0,      ///< Delay-line modulation, zero latency, audible artifacts
    Granular = 1,    ///< OLA grains, ~46ms latency, good quality
    PhaseVocoder = 2, ///< STFT-based, ~116ms latency, excellent quality
    PitchSync = 3    ///< Pitch-synchronized grains, ~5-10ms latency, good for tonal signals
};

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

    /// @brief Sub-block size for parameter smoothing granularity.
    /// At 44.1 kHz this gives ~689 ratio updates/sec; at 96 kHz ~1500/sec.
    static constexpr std::size_t kSmoothingSubBlockSize = 64;

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
    // Parameters - Phase Reset
    //=========================================================================

    /// @brief Enable or disable transient-aware phase reset for PhaseVocoder mode.
    /// Only effective when mode is PitchMode::PhaseVocoder.
    /// @param enable true to enable, false to disable
    void setPhaseReset(bool enable) noexcept;

    /// @brief Get phase reset state
    /// @return true if phase reset is enabled
    [[nodiscard]] bool getPhaseReset() const noexcept;

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

    //=========================================================================
    // Shared-Analysis API (spec 065)
    //=========================================================================

    /// @brief Process one analysis frame using shared analysis, bypassing internal STFT.
    ///
    /// When mode is PhaseVocoder: delegates to internal PhaseVocoderPitchShifter's
    /// processWithSharedAnalysis(). The pitch ratio is passed directly without
    /// internal parameter smoothing (the caller is responsible for smoothing).
    ///
    /// When mode is NOT PhaseVocoder (Simple, Granular, PitchSync): no-op.
    /// No frame is pushed to the OLA buffer. pullSharedAnalysisOutput() will
    /// return 0 for this frame.
    ///
    /// @param analysis  Read-only reference to pre-computed analysis spectrum.
    /// @param pitchRatio  Pitch ratio for this frame (direct, not smoothed).
    ///
    /// @pre prepare() has been called.
    /// @pre Mode is set via setMode() before calling.
    void processWithSharedAnalysis(const SpectralBuffer& analysis,
                                   float pitchRatio) noexcept;

    /// @brief Synthesize one frame as a unity-pitch passthrough.
    ///
    /// Called by HarmonizerEngine for unity-pitch voices (FR-025).
    /// Passes the analysis spectrum directly to OLA without processFrame().
    ///
    /// @param analysis  Read-only reference to pre-computed analysis spectrum.
    void synthesizePassthrough(const SpectralBuffer& analysis) noexcept;

    /// @brief Pull output samples from the PhaseVocoder OLA buffer after
    ///        processWithSharedAnalysis() calls.
    ///
    /// @param output      Destination buffer.
    /// @param maxSamples  Maximum samples to pull.
    /// @return            Samples actually written (may be less if OLA has fewer).
    ///
    /// When mode is NOT PhaseVocoder: returns 0, output untouched.
    std::size_t pullSharedAnalysisOutput(float* output,
                                         std::size_t maxSamples) noexcept;

    /// @brief Query available output samples from the PhaseVocoder OLA buffer.
    ///
    /// @return Samples available, or 0 if mode is not PhaseVocoder.
    [[nodiscard]] std::size_t sharedAnalysisSamplesAvailable() const noexcept;

    /// @brief Get the PhaseVocoder's FFT size for shared STFT configuration.
    /// @return 4096 (compile-time constant).
    [[nodiscard]] static constexpr std::size_t getPhaseVocoderFFTSize() noexcept {
        return 4096;
    }

    /// @brief Get the PhaseVocoder's hop size for shared STFT configuration.
    /// @return 1024 (compile-time constant).
    [[nodiscard]] static constexpr std::size_t getPhaseVocoderHopSize() noexcept {
        return 1024;
    }

private:
    //=========================================================================
    // Internal Implementation
    //=========================================================================

    // Forward declaration of implementation
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

// ==============================================================================
// FormantPreserver - now in its own header (formant_preserver.h)
// ==============================================================================
// The FormantPreserver class has been extracted to:
//   <krate/dsp/processors/formant_preserver.h>
// It is included above and available in this translation unit.
// ==============================================================================

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
    static constexpr float kRatioSmoothTimeMs = 5.0f;  // 5ms smoothing for ratio changes
    // Note: kPi is now defined in math_constants.h (Layer 0)

    SimplePitchShifter() = default;

    void prepare(double sampleRate, std::size_t /*maxBlockSize*/) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Delay range in samples (~2205 at 44.1kHz for 50ms window)
        maxDelay_ = sampleRate_ * kWindowTimeMs * 0.001f;
        minDelay_ = 1.0f;  // Minimum safe delay

        // Buffer must be large enough to hold max delay + safety margin
        bufferSize_ = static_cast<std::size_t>(maxDelay_) * 2 + 64;
        buffer_.resize(bufferSize_, 0.0f);

        // Calculate smoothing coefficient for ratio changes
        // One-pole filter: coeff = 1 - exp(-1 / (tau * sampleRate))
        const float tau = kRatioSmoothTimeMs * 0.001f;  // Convert ms to seconds
        ratioSmoothCoeff_ = 1.0f - std::exp(-1.0f / (tau * sampleRate_));

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
        // Mark smoothedRatio_ as uninitialized - will snap to first ratio
        smoothedRatioInitialized_ = false;
    }

    void process(const float* input, float* output, std::size_t numSamples,
                 float pitchRatio) noexcept {
        // At unity pitch, just pass through
        // Check both target and current smoothed value (or uninitialized state)
        const bool targetIsUnity = std::abs(pitchRatio - 1.0f) < 0.0001f;
        const bool smoothedIsUnity = !smoothedRatioInitialized_ ||
                                     std::abs(smoothedRatio_ - 1.0f) < 0.0001f;
        if (targetIsUnity && smoothedIsUnity) {
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
        //
        // Per-sample smoothing of pitchRatio prevents clicks during parameter changes

        const float bufferSizeF = static_cast<float>(bufferSize_);

        // Crossfade over ~25% of the delay range for smooth transitions
        const float crossfadeLength = maxDelay_ * 0.25f;
        const float crossfadeRate = 1.0f / crossfadeLength;

        // Threshold for triggering crossfade (when delay gets close to limit)
        const float triggerThreshold = crossfadeLength;

        for (std::size_t i = 0; i < numSamples; ++i) {
            // Per-sample smoothing of pitch ratio to prevent clicks
            // On first use, snap to target to avoid startup transients
            if (!smoothedRatioInitialized_) {
                smoothedRatio_ = pitchRatio;
                smoothedRatioInitialized_ = true;
            } else {
                smoothedRatio_ += ratioSmoothCoeff_ * (pitchRatio - smoothedRatio_);
            }
            const float delayChange = 1.0f - smoothedRatio_;  // Negative for pitch up

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
                    delay2_ = (smoothedRatio_ > 1.0f) ? maxDelay_ : minDelay_;
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

    // Ratio smoothing for click-free parameter changes
    float smoothedRatio_ = 1.0f;
    float ratioSmoothCoeff_ = 0.0f;
    bool smoothedRatioInitialized_ = false;
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
    // Note: kPi is now defined in math_constants.h (Layer 0)

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
// PitchSyncGranularShifter - Pitch-synchronized low-latency pitch shifting
// ==============================================================================

/// @brief Low-latency pitch shifter with pitch-synchronized grain boundaries
///
/// Uses real-time pitch detection to synchronize grain boundaries to the
/// signal's fundamental period. This dramatically reduces latency compared
/// to fixed-grain approaches while maintaining quality for tonal signals.
///
/// Key improvements over GranularPitchShifter:
/// 1. Adaptive grain size based on detected pitch (vs fixed 46ms)
/// 2. Grain boundaries at pitch-synchronous points (cleaner splices)
/// 3. Typical latency ~5-10ms for pitched signals (vs 46ms fixed)
/// 4. Falls back to short fixed grains (~10ms) for unpitched content
///
/// Ideal for:
/// - Shimmer effects (feedback is already highly tonal)
/// - Vocal pitch correction
/// - Any application where input is primarily tonal
///
/// Algorithm:
/// 1. Continuously detect fundamental period using autocorrelation
/// 2. Set grain size to 2x detected period (or fallback for noise)
/// 3. Crossfade grains at pitch-synchronous boundaries
/// 4. Use Doppler-based delay modulation (same as other shifters)
///
/// Sources:
/// - https://www.katjaas.nl/pitchshiftlowlatency/pitchshiftlowlatency.html
/// - TD-PSOLA (Time-Domain Pitch-Synchronous Overlap-Add)
///
/// Latency: Variable, typically 2x detected period (~5-20ms)
class PitchSyncGranularShifter {
public:
    /// Minimum grain size in ms (used for unpitched content)
    static constexpr float kMinGrainMs = 10.0f;

    /// Maximum grain size in ms (safety limit)
    static constexpr float kMaxGrainMs = 30.0f;

    /// Multiplier for grain size relative to detected period
    static constexpr float kPeriodMultiplier = 2.0f;

    PitchSyncGranularShifter() = default;

    void prepare(double sampleRate, std::size_t /*maxBlockSize*/) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Calculate grain size limits in samples
        minGrainSamples_ = static_cast<std::size_t>(kMinGrainMs * 0.001f * sampleRate_);
        maxGrainSamples_ = static_cast<std::size_t>(kMaxGrainMs * 0.001f * sampleRate_);

        // Current grain size (will be updated by pitch detection)
        currentGrainSize_ = minGrainSamples_;

        // Buffer must hold max grain + safety margin
        bufferSize_ = maxGrainSamples_ * 4 + 64;
        buffer_.resize(bufferSize_, 0.0f);

        // Pre-compute Hann window for crossfade (sized for max grain)
        crossfadeWindowSize_ = maxGrainSamples_ / 2;
        const std::size_t fullWindowSize = crossfadeWindowSize_ * 2;
        crossfadeWindow_.resize(fullWindowSize);
        Window::generateHann(crossfadeWindow_.data(), fullWindowSize);

        // Prepare pitch detector (256 sample window = ~5.8ms)
        pitchDetector_.prepare(sampleRate, 256);

        reset();
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writePos_ = 0;

        // Initialize with min grain size
        currentGrainSize_ = minGrainSamples_;
        maxDelay_ = static_cast<float>(currentGrainSize_);
        minDelay_ = 1.0f;

        delay1_ = maxDelay_;
        delay2_ = maxDelay_;
        crossfadePhase_ = 0.0f;
        needsCrossfade_ = false;

        pitchDetector_.reset();
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

        for (std::size_t i = 0; i < numSamples; ++i) {
            // Write to buffer
            buffer_[writePos_] = input[i];

            // Update pitch detection
            pitchDetector_.push(input[i]);

            // Update grain size based on detected pitch
            updateGrainSize();

            // Crossfade parameters based on current grain size
            const float crossfadeLength = maxDelay_ * 0.4f;  // 40% crossfade
            const float crossfadeRate = 1.0f / crossfadeLength;
            const float triggerThreshold = crossfadeLength;

            // Read from both delay taps
            float readPos1 = static_cast<float>(writePos_) - delay1_;
            float readPos2 = static_cast<float>(writePos_) - delay2_;

            if (readPos1 < 0.0f) readPos1 += bufferSizeF;
            if (readPos2 < 0.0f) readPos2 += bufferSizeF;

            float sample1 = readInterpolated(readPos1);
            float sample2 = readInterpolated(readPos2);

            // Hann window crossfade
            std::size_t fadeIdx = static_cast<std::size_t>(crossfadePhase_ *
                                  static_cast<float>(crossfadeWindowSize_));
            if (fadeIdx >= crossfadeWindowSize_) fadeIdx = crossfadeWindowSize_ - 1;

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

    /// @brief Get current latency in samples (based on detected period)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return currentGrainSize_;
    }

    /// @brief Get detected pitch period in samples
    [[nodiscard]] float getDetectedPeriod() const noexcept {
        return pitchDetector_.getDetectedPeriod();
    }

    /// @brief Get pitch detection confidence [0, 1]
    [[nodiscard]] float getPitchConfidence() const noexcept {
        return pitchDetector_.getConfidence();
    }

private:
    /// @brief Update grain size based on pitch detection
    void updateGrainSize() noexcept {
        float period = pitchDetector_.getDetectedPeriod();

        // Use 2x period for grain size (gives one complete cycle + crossfade)
        float grainSizeF = period * kPeriodMultiplier;

        // Clamp to valid range
        std::size_t newGrainSize = static_cast<std::size_t>(grainSizeF);
        newGrainSize = std::clamp(newGrainSize, minGrainSamples_, maxGrainSamples_);

        // Only update if significantly different (avoid jitter)
        if (std::abs(static_cast<int>(newGrainSize) - static_cast<int>(currentGrainSize_)) > 10) {
            currentGrainSize_ = newGrainSize;
            maxDelay_ = static_cast<float>(currentGrainSize_);
        }
    }

    [[nodiscard]] float readInterpolated(float pos) const noexcept {
        const std::size_t idx0 = static_cast<std::size_t>(pos) % bufferSize_;
        const std::size_t idx1 = (idx0 + 1) % bufferSize_;
        const float frac = pos - std::floor(pos);
        return buffer_[idx0] * (1.0f - frac) + buffer_[idx1] * frac;
    }

    // Pitch detector
    PitchDetector pitchDetector_;

    // Buffers
    std::vector<float> buffer_;
    std::vector<float> crossfadeWindow_;

    // Grain size (adaptive)
    std::size_t currentGrainSize_ = 441;  // ~10ms at 44.1kHz
    std::size_t minGrainSamples_ = 441;
    std::size_t maxGrainSamples_ = 1323;  // ~30ms

    // Buffer management
    std::size_t crossfadeWindowSize_ = 0;
    std::size_t bufferSize_ = 0;
    std::size_t writePos_ = 0;

    // Delay tap state
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
    static constexpr std::size_t kHopSize = 1024;      // 75% overlap (4x)
    static constexpr std::size_t kMaxBins = 4097;      // 8192/2+1 (max supported FFT)
    static constexpr std::size_t kMaxPeaks = 512;      // Max detectable peaks per frame
    // Note: kPi and kTwoPi are now defined in math_constants.h (Layer 0)

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

        // Prepare formant preservation
        formantPreserver_.prepare(kFFTSize, sampleRate);
        originalEnvelope_.resize(numBins, 1.0f);
        shiftedEnvelope_.resize(numBins, 1.0f);
        shiftedMagnitude_.resize(numBins, 0.0f);

        // Prepare transient detector for phase reset
        transientDetector_.prepare(numBins);

        reset();
    }

    void reset() noexcept {
        stft_.reset();
        ola_.reset();
        analysisSpectrum_.reset();
        synthesisSpectrum_.reset();
        formantPreserver_.reset();

        std::fill(prevPhase_.begin(), prevPhase_.end(), 0.0f);
        std::fill(synthPhase_.begin(), synthPhase_.end(), 0.0f);
        std::fill(inputBuffer_.begin(), inputBuffer_.end(), 0.0f);
        std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);
        std::fill(originalEnvelope_.begin(), originalEnvelope_.end(), 1.0f);
        std::fill(shiftedEnvelope_.begin(), shiftedEnvelope_.end(), 1.0f);

        inputWritePos_ = 0;
        inputSamplesReady_ = 0;
        outputReadPos_ = 0;
        outputWritePos_ = 0;
        outputSamplesReady_ = 0;

        // Phase locking state
        isPeak_.fill(false);
        peakIndices_.fill(0);
        numPeaks_ = 0;
        regionPeak_.fill(0);
        wasLocked_ = false;

        // Transient detector state
        transientDetector_.reset();
    }

    /// @brief Enable or disable formant preservation
    void setFormantPreserve(bool enable) noexcept {
        formantPreserve_ = enable;
    }

    /// @brief Get formant preservation state
    [[nodiscard]] bool getFormantPreserve() const noexcept {
        return formantPreserve_;
    }

    /// @brief Enable or disable identity phase locking.
    /// When disabled, behavior is identical to the pre-modification basic phase vocoder.
    /// Phase locking is enabled by default.
    void setPhaseLocking(bool enabled) noexcept {
        phaseLockingEnabled_ = enabled;
    }

    /// @brief Get phase locking state
    [[nodiscard]] bool getPhaseLocking() const noexcept {
        return phaseLockingEnabled_;
    }

    /// @brief Enable or disable transient-aware phase reset.
    /// When enabled, synthesis phases are reset to analysis phases at transient frames.
    /// Independent of phase locking -- both can be enabled simultaneously.
    /// Phase reset is disabled by default.
    void setPhaseReset(bool enabled) noexcept {
        phaseResetEnabled_ = enabled;
    }

    /// @brief Get phase reset state
    [[nodiscard]] bool getPhaseReset() const noexcept {
        return phaseResetEnabled_;
    }

    /// Returns the number of peaks detected in the most recent frame.
    /// Intended for testing/diagnostics only.
    [[nodiscard]] std::size_t getNumPeaks() const noexcept {
        return numPeaks_;
    }

    /// Returns the region-peak assignment for a given analysis bin (test accessor).
    /// The returned value is the bin index of the peak that controls the given bin.
    [[nodiscard]] uint16_t getRegionPeak(std::size_t bin) const noexcept {
        return regionPeak_[bin];
    }

    /// Returns whether a given analysis bin is a detected peak (test accessor).
    [[nodiscard]] bool getIsPeak(std::size_t bin) const noexcept {
        return isPeak_[bin];
    }

    /// Returns the bin index of the i-th detected peak (test accessor).
    [[nodiscard]] uint16_t getPeakIndex(std::size_t i) const noexcept {
        return peakIndices_[i];
    }

    /// Returns a const reference to the synthesis spectrum buffer (test accessor).
    /// Allows tests to inspect output Cartesian values for phase verification.
    [[nodiscard]] const SpectralBuffer& getSynthesisSpectrum() const noexcept {
        return synthesisSpectrum_;
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

            // Phase vocoder pitch shift (FR-023: pass analysis/synthesis by reference)
            processFrame(analysisSpectrum_, synthesisSpectrum_, pitchRatio);

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

    //=========================================================================
    // Shared-Analysis API (spec 065)
    //=========================================================================

    /// @brief Process one analysis frame using an externally provided spectrum.
    ///
    /// Performs synthesis-only processing: phase rotation, optional phase locking,
    /// optional transient detection, optional formant preservation, synthesis iFFT,
    /// and overlap-add. Bypasses internal STFT analysis entirely.
    ///
    /// @param analysis  Read-only reference to the pre-computed analysis spectrum.
    ///                  Must have numBins() == kFFTSize / 2 + 1 (2049).
    ///                  The caller MUST NOT modify this spectrum during or after
    ///                  the call. The reference is only valid for the duration
    ///                  of this call (FR-024).
    /// @param pitchRatio  Pitch ratio for this frame (e.g., 1.0594 for +1 semitone).
    ///                    Clamped to [0.25, 4.0].
    ///
    /// @pre  prepare() has been called.
    /// @pre  analysis.numBins() == kFFTSize / 2 + 1 (= 2049 for kFFTSize = 4096).
    /// @post One synthesis frame has been added to the internal OLA buffer.
    ///       Use outputSamplesAvailable() and pullOutputSamples() to retrieve output.
    ///
    /// In degenerate conditions (unprepared, FFT size mismatch), the method is a
    /// no-op: no frame is pushed to the OLA buffer. pullOutputSamples() will
    /// return 0 for this frame.
    ///
    /// @note This method MUST NOT apply unity-pitch bypass internally. The caller
    ///       (HarmonizerEngine) is responsible for detecting unity pitch and
    ///       routing accordingly (FR-025).
    void processWithSharedAnalysis(const SpectralBuffer& analysis,
                                   float pitchRatio) noexcept {
        // Guard: not prepared
        if (!ola_.isPrepared()) return;

        // Guard: numBins mismatch (FR-008)
        constexpr std::size_t kExpectedBins = kFFTSize / 2 + 1;
        assert(analysis.numBins() == kExpectedBins &&
               "SpectralBuffer numBins mismatch: expected kFFTSize / 2 + 1");
        if (analysis.numBins() != kExpectedBins) return;

        // Clamp pitch ratio
        pitchRatio = std::clamp(pitchRatio, 0.25f, 4.0f);

        // FR-025: Do NOT apply unity-pitch bypass here.
        // The caller is responsible for unity-pitch routing.

        // Process one frame using the external analysis spectrum
        processFrame(analysis, synthesisSpectrum_, pitchRatio);

        // Synthesize via OLA
        ola_.synthesize(synthesisSpectrum_);
    }

    /// @brief Synthesize one frame as a unity-pitch passthrough.
    ///
    /// Passes the analysis spectrum directly to OLA synthesis without
    /// going through processFrame(). Matches the behavior of the internal
    /// processUnityPitch() method for output equivalence (SC-002).
    ///
    /// This is called by HarmonizerEngine when a voice's pitch ratio is
    /// near unity (FR-025: caller responsible for unity-pitch routing).
    ///
    /// @param analysis  Read-only reference to the pre-computed analysis spectrum.
    /// @pre  prepare() has been called.
    /// @pre  analysis.numBins() == kFFTSize / 2 + 1.
    void synthesizePassthrough(const SpectralBuffer& analysis) noexcept {
        if (!ola_.isPrepared()) return;
        constexpr std::size_t kExpectedBins = kFFTSize / 2 + 1;
        if (analysis.numBins() != kExpectedBins) return;

        ola_.synthesize(analysis);
    }

    /// @brief Pull processed samples from the internal OLA buffer.
    ///
    /// @param output      Destination buffer. Must have room for at least
    ///                    maxSamples floats.
    /// @param maxSamples  Maximum number of samples to pull.
    /// @return            Number of samples actually written to output.
    ///                    May be less than maxSamples if fewer are available.
    ///
    /// @pre  prepare() has been called.
    /// @post Up to maxSamples are copied from OLA buffer to output.
    ///       OLA buffer advances accordingly.
    std::size_t pullOutputSamples(float* output, std::size_t maxSamples) noexcept {
        if (!ola_.isPrepared() || output == nullptr) return 0;

        std::size_t available = ola_.samplesAvailable();
        std::size_t toPull = std::min(maxSamples, available);
        if (toPull == 0) return 0;

        ola_.pullSamples(output, toPull);
        return toPull;
    }

    /// @brief Query how many samples are available in the OLA buffer.
    ///
    /// @return Number of samples that can be pulled via pullOutputSamples().
    [[nodiscard]] std::size_t outputSamplesAvailable() const noexcept {
        if (!ola_.isPrepared()) return 0;
        return ola_.samplesAvailable();
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

    /// @brief Phase vocoder frame processing (FR-023: accepts analysis/synthesis by reference)
    ///
    /// @param analysis  Read-only reference to the analysis spectrum (magnitude + phase).
    /// @param synthesis  Output synthesis spectrum (written with pitch-shifted Cartesian data).
    /// @param pitchRatio  Pitch ratio for this frame (e.g., 1.0594 for +1 semitone).
    void processFrame(const SpectralBuffer& analysis, SpectralBuffer& synthesis,
                      float pitchRatio) noexcept {
        const std::size_t numBins = kFFTSize / 2 + 1;

        // Step 1: Extract magnitude and compute instantaneous frequency
        for (std::size_t k = 0; k < numBins; ++k) {
            // Get magnitude and phase from the analysis parameter (not internal member)
            magnitude_[k] = analysis.getMagnitude(k);
            float phase = analysis.getPhase(k);

            // Compute phase difference from previous frame
            float phaseDiff = phase - prevPhase_[k];
            prevPhase_[k] = phase;

            // Subtract expected phase increment to get deviation
            float deviation = phaseDiff - expectedPhaseInc_[k];

            // Wrap deviation to [-pi, pi]
            deviation = wrapPhase(deviation);

            // Compute true frequency as deviation from bin center
            // true_freq = bin_freq + deviation / (2pi * hopSize / sampleRate)
            // But we store as phase per hop for synthesis
            frequency_[k] = expectedPhaseInc_[k] + deviation;
        }

        // Step 1b: Extract original spectral envelope if formant preservation enabled
        if (formantPreserve_) {
            formantPreserver_.extractEnvelope(magnitude_.data(), originalEnvelope_.data());
        }

        // Step 1b-reset: Transient detection and phase reset (FR-012)
        // Note: prevPhase_[k] already holds the current frame's analysis phase
        // (updated above at line: prevPhase_[k] = phase), which is correct for
        // phase reset per FR-012.
        if (phaseResetEnabled_) {
            const bool isTransient = transientDetector_.detect(magnitude_.data(), numBins);
            if (isTransient) {
                for (std::size_t k = 0; k < numBins; ++k) {
                    synthPhase_[k] = prevPhase_[k];
                }
            }
        }

        // Step 1c: Phase locking setup (peak detection + region assignment)
        if (phaseLockingEnabled_) {
            // Stage A: Peak detection in analysis-domain magnitude spectrum
            numPeaks_ = 0;
            // Clear only the bins we use (numBins, not kMaxBins)
            for (std::size_t k = 0; k < numBins; ++k) {
                isPeak_[k] = false;
            }

            for (std::size_t k = 1; k < numBins - 1 && numPeaks_ < kMaxPeaks; ++k) {
                if (magnitude_[k] > magnitude_[k - 1] && magnitude_[k] > magnitude_[k + 1]) {
                    isPeak_[k] = true;
                    peakIndices_[numPeaks_] = static_cast<uint16_t>(k);
                    ++numPeaks_;
                }
            }

            // Stage B: Region-of-influence assignment
            if (numPeaks_ > 0) {
                if (numPeaks_ == 1) {
                    // Single peak: all bins assigned to it
                    for (std::size_t k = 0; k < numBins; ++k) {
                        regionPeak_[k] = peakIndices_[0];
                    }
                } else {
                    // Forward scan: assign bins to peaks based on midpoint boundaries
                    std::size_t peakIdx = 0;
                    for (std::size_t k = 0; k < numBins; ++k) {
                        // Move to next peak if we've passed the midpoint
                        if (peakIdx + 1 < numPeaks_) {
                            uint16_t midpoint = static_cast<uint16_t>(
                                (peakIndices_[peakIdx] + peakIndices_[peakIdx + 1]) / 2);
                            if (k > midpoint) {
                                ++peakIdx;
                            }
                        }
                        regionPeak_[k] = peakIndices_[peakIdx];
                    }
                }
            }
        }

        // Toggle-to-basic re-initialization check
        if (wasLocked_ && !phaseLockingEnabled_) {
            for (std::size_t k = 0; k < numBins; ++k) {
                synthPhase_[k] = prevPhase_[k];
            }
        }
        wasLocked_ = phaseLockingEnabled_;

        // Step 2: Pitch shift by scaling frequencies and resampling spectrum
        synthesis.reset();

        if (phaseLockingEnabled_ && numPeaks_ > 0) {
            // Two-pass synthesis: peaks first, then non-peaks

            // Pass 1: Process PEAK bins only (accumulate synthPhase_ for peaks)
            for (std::size_t k = 0; k < numBins; ++k) {
                float srcBin = static_cast<float>(k) / pitchRatio;
                if (srcBin >= static_cast<float>(numBins - 1)) continue;

                std::size_t srcBinRounded = static_cast<std::size_t>(srcBin + 0.5f);
                if (srcBinRounded >= numBins) srcBinRounded = numBins - 1;

                if (!isPeak_[srcBinRounded]) continue; // Skip non-peaks in Pass 1

                // Standard bin mapping and magnitude interpolation
                std::size_t srcBin0 = static_cast<std::size_t>(srcBin);
                std::size_t srcBin1 = srcBin0 + 1;
                if (srcBin1 >= numBins) srcBin1 = numBins - 1;

                float frac = srcBin - static_cast<float>(srcBin0);
                float mag = magnitude_[srcBin0] * (1.0f - frac) + magnitude_[srcBin1] * frac;
                shiftedMagnitude_[k] = mag;

                // Peak bin: standard horizontal phase propagation
                float freq = frequency_[srcBin0] * pitchRatio;
                synthPhase_[k] += freq;
                synthPhase_[k] = wrapPhase(synthPhase_[k]);

                float real = mag * std::cos(synthPhase_[k]);
                float imag = mag * std::sin(synthPhase_[k]);
                synthesis.setCartesian(k, real, imag);
            }

            // Pass 2: Process NON-PEAK bins (use peak phases from Pass 1)
            for (std::size_t k = 0; k < numBins; ++k) {
                float srcBin = static_cast<float>(k) / pitchRatio;
                if (srcBin >= static_cast<float>(numBins - 1)) continue;

                std::size_t srcBinRounded = static_cast<std::size_t>(srcBin + 0.5f);
                if (srcBinRounded >= numBins) srcBinRounded = numBins - 1;

                if (isPeak_[srcBinRounded]) continue; // Skip peaks in Pass 2

                // Standard bin mapping and magnitude interpolation
                std::size_t srcBin0 = static_cast<std::size_t>(srcBin);
                std::size_t srcBin1 = srcBin0 + 1;
                if (srcBin1 >= numBins) srcBin1 = numBins - 1;

                float frac = srcBin - static_cast<float>(srcBin0);
                float mag = magnitude_[srcBin0] * (1.0f - frac) + magnitude_[srcBin1] * frac;
                shiftedMagnitude_[k] = mag;

                // Non-peak bin: identity phase locking via rotation angle
                uint16_t analysisPeak = regionPeak_[srcBinRounded];

                // Find the synthesis bin corresponding to the analysis peak
                std::size_t synthPeakBin = static_cast<std::size_t>(
                    static_cast<float>(analysisPeak) * pitchRatio + 0.5f);
                if (synthPeakBin >= numBins) synthPeakBin = numBins - 1;

                // Rotation angle: peak's synthesis phase minus peak's analysis phase
                float analysisPhaseAtPeak = prevPhase_[analysisPeak];
                float rotationAngle = synthPhase_[synthPeakBin] - analysisPhaseAtPeak;

                // Apply rotation to this bin's analysis phase (interpolated)
                float analysisPhaseAtSrc = prevPhase_[srcBin0] * (1.0f - frac)
                                         + prevPhase_[srcBin1] * frac;
                float phaseForOutput = analysisPhaseAtSrc + rotationAngle;

                // Store in synthPhase_ for formant step compatibility
                synthPhase_[k] = phaseForOutput;

                float real = mag * std::cos(phaseForOutput);
                float imag = mag * std::sin(phaseForOutput);
                synthesis.setCartesian(k, real, imag);
            }
        } else {
            // Basic path: standard per-bin phase accumulation (pre-modification behavior)
            // Also used as fallback when phaseLockingEnabled_ && numPeaks_ == 0 (FR-011)
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

                // Store shifted magnitude for formant preservation
                shiftedMagnitude_[k] = mag;

                // Scale frequency by pitch ratio
                float freq = frequency_[srcBin0] * pitchRatio;

                // Accumulate synthesis phase
                synthPhase_[k] += freq;
                synthPhase_[k] = wrapPhase(synthPhase_[k]);

                // Set synthesis bin (Cartesian form)
                float real = mag * std::cos(synthPhase_[k]);
                float imag = mag * std::sin(synthPhase_[k]);
                synthesis.setCartesian(k, real, imag);
            }
        }

        // Step 3: Apply formant preservation if enabled
        if (formantPreserve_) {
            // Extract envelope of the shifted spectrum
            formantPreserver_.extractEnvelope(shiftedMagnitude_.data(), shiftedEnvelope_.data());

            // Apply formant preservation: adjust magnitudes to preserve original envelope
            for (std::size_t k = 0; k < numBins; ++k) {
                // Compute envelope ratio: originalEnv / shiftedEnv
                float shiftedEnv = std::max(shiftedEnvelope_[k], 1e-10f);
                float ratio = originalEnvelope_[k] / shiftedEnv;

                // Clamp ratio to avoid extreme amplification (especially at extreme shifts)
                ratio = std::min(ratio, 100.0f);
                ratio = std::max(ratio, 0.01f);

                // Apply ratio to shifted magnitude
                float adjustedMag = shiftedMagnitude_[k] * ratio;

                // Reconstruct Cartesian form with adjusted magnitude
                float real = adjustedMag * std::cos(synthPhase_[k]);
                float imag = adjustedMag * std::sin(synthPhase_[k]);
                synthesis.setCartesian(k, real, imag);
            }
        }
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

    // Formant preservation
    FormantPreserver formantPreserver_;
    std::vector<float> originalEnvelope_;  // Envelope of original spectrum
    std::vector<float> shiftedEnvelope_;   // Envelope of shifted spectrum
    std::vector<float> shiftedMagnitude_;  // Shifted magnitude for formant adjustment
    bool formantPreserve_ = false;

    // Phase locking state (pre-allocated, zero runtime allocation)
    std::array<bool, kMaxBins> isPeak_{};               // Peak flag per analysis bin
    std::array<uint16_t, kMaxPeaks> peakIndices_{};     // Analysis-domain peak bin indices
    std::size_t numPeaks_ = 0;                          // Number of detected peaks this frame
    std::array<uint16_t, kMaxBins> regionPeak_{};       // Region-peak assignment per analysis bin
    bool phaseLockingEnabled_ = true;                   // Phase locking toggle (default: enabled)
    bool wasLocked_ = false;                            // Previous frame's locking state (for toggle-to-basic re-init)

    // Transient detection for phase reset (FR-012, FR-013)
    SpectralTransientDetector transientDetector_;       // Spectral flux onset detector
    bool phaseResetEnabled_ = false;                    // Independent toggle (default: off)

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

// Compile-time verification that PitchShiftProcessor's FFT/hop accessors
// match PhaseVocoderPitchShifter's constants (spec 065, FR-011)
static_assert(PitchShiftProcessor::getPhaseVocoderFFTSize() == PhaseVocoderPitchShifter::kFFTSize,
              "PitchShiftProcessor FFT size must match PhaseVocoderPitchShifter::kFFTSize");
static_assert(PitchShiftProcessor::getPhaseVocoderHopSize() == PhaseVocoderPitchShifter::kHopSize,
              "PitchShiftProcessor hop size must match PhaseVocoderPitchShifter::kHopSize");

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
    PitchSyncGranularShifter pitchSyncShifter;

    // Parameter smoothers
    OnePoleSmoother semitoneSmoother;
    OnePoleSmoother centsSmoother;

    // Shared-analysis delegation methods (spec 065)
    void processWithSharedAnalysis(const SpectralBuffer& analysis,
                                   float pitchRatio) noexcept {
        if (!prepared) return;
        if (mode != PitchMode::PhaseVocoder) return;
        phaseVocoderShifter.processWithSharedAnalysis(analysis, pitchRatio);
    }

    void synthesizePassthrough(const SpectralBuffer& analysis) noexcept {
        if (!prepared) return;
        if (mode != PitchMode::PhaseVocoder) return;
        phaseVocoderShifter.synthesizePassthrough(analysis);
    }

    std::size_t pullSharedAnalysisOutput(float* output,
                                         std::size_t maxSamples) noexcept {
        if (!prepared || mode != PitchMode::PhaseVocoder) return 0;
        return phaseVocoderShifter.pullOutputSamples(output, maxSamples);
    }

    std::size_t sharedAnalysisSamplesAvailable() const noexcept {
        if (!prepared || mode != PitchMode::PhaseVocoder) return 0;
        return phaseVocoderShifter.outputSamplesAvailable();
    }
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
    pImpl_->pitchSyncShifter.prepare(sampleRate, maxBlockSize);

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
    pImpl_->pitchSyncShifter.reset();
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

    // Sub-block processing: advance smoothers and recompute pitch ratio every
    // kSmoothingSubBlockSize samples for smooth parameter automation.
    std::size_t samplesProcessed = 0;
    while (samplesProcessed < numSamples) {
        const std::size_t subBlockSize = std::min(kSmoothingSubBlockSize,
                                                   numSamples - samplesProcessed);

        // Advance smoothers by sub-block size (O(1) closed-form)
        pImpl_->semitoneSmoother.advanceSamples(subBlockSize);
        pImpl_->centsSmoother.advanceSamples(subBlockSize);

        // Compute pitch ratio from smoothed parameters
        const float smoothedSemitones = pImpl_->semitoneSmoother.getCurrentValue();
        const float smoothedCents = pImpl_->centsSmoother.getCurrentValue();
        const float totalSemitones = smoothedSemitones + smoothedCents / 100.0f;
        const float pitchRatio = semitonesToRatio(totalSemitones);

        const float* subInput = input + samplesProcessed;
        float* subOutput = output + samplesProcessed;

        // Route sub-block to appropriate processor
        switch (pImpl_->mode) {
            case PitchMode::Simple:
                pImpl_->simpleShifter.process(subInput, subOutput, subBlockSize, pitchRatio);
                break;

            case PitchMode::Granular:
                pImpl_->granularShifter.process(subInput, subOutput, subBlockSize, pitchRatio);
                break;

            case PitchMode::PhaseVocoder:
                pImpl_->phaseVocoderShifter.process(subInput, subOutput, subBlockSize, pitchRatio);
                break;

            case PitchMode::PitchSync:
                pImpl_->pitchSyncShifter.process(subInput, subOutput, subBlockSize, pitchRatio);
                break;
        }

        samplesProcessed += subBlockSize;
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
    return semitonesToRatio(totalSemitones);
}

inline void PitchShiftProcessor::setFormantPreserve(bool enable) noexcept {
    pImpl_->formantPreserve = enable;
    // Pass to internal PhaseVocoder shifter (Granular doesn't support formant preservation)
    pImpl_->phaseVocoderShifter.setFormantPreserve(enable);
}

inline bool PitchShiftProcessor::getFormantPreserve() const noexcept {
    return pImpl_->formantPreserve;
}

inline void PitchShiftProcessor::setPhaseReset(bool enable) noexcept {
    pImpl_->phaseVocoderShifter.setPhaseReset(enable);
}

inline bool PitchShiftProcessor::getPhaseReset() const noexcept {
    return pImpl_->phaseVocoderShifter.getPhaseReset();
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
        case PitchMode::PitchSync:
            // Use actual latency from the pitch-sync shifter (variable, ~5-20ms)
            return pImpl_->pitchSyncShifter.getLatencySamples();
    }
    return 0;
}

// Shared-analysis delegation wrappers (spec 065)

inline void PitchShiftProcessor::processWithSharedAnalysis(
    const SpectralBuffer& analysis, float pitchRatio) noexcept {
    pImpl_->processWithSharedAnalysis(analysis, pitchRatio);
}

inline std::size_t PitchShiftProcessor::pullSharedAnalysisOutput(
    float* output, std::size_t maxSamples) noexcept {
    return pImpl_->pullSharedAnalysisOutput(output, maxSamples);
}

inline std::size_t PitchShiftProcessor::sharedAnalysisSamplesAvailable() const noexcept {
    return pImpl_->sharedAnalysisSamplesAvailable();
}

inline void PitchShiftProcessor::synthesizePassthrough(
    const SpectralBuffer& analysis) noexcept {
    pImpl_->synthesizePassthrough(analysis);
}

} // namespace Krate::DSP
