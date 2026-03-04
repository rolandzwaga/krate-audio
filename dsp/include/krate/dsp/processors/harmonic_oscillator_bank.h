// ==============================================================================
// Layer 2: DSP Processor - Harmonic Oscillator Bank
// ==============================================================================
// Synthesizes audio from HarmonicFrames using 48 Gordon-Smith Modified Coupled
// Form (MCF) oscillators in Structure-of-Arrays (SoA) layout.
//
// Features:
// - 48 MCF oscillators with per-partial frequency/amplitude control
// - SoA layout with 32-byte alignment for cache efficiency (FR-036)
// - Anti-aliasing via soft rolloff near Nyquist (FR-038)
// - Phase continuity on frequency changes (FR-039)
// - Crossfade on large pitch jumps > 1 semitone (FR-040)
// - Per-partial one-pole amplitude smoothing (FR-041)
// - Inharmonicity control from 0% (perfect harmonics) to 100% (FR-042)
//
// MCF Variant: sinNew = sin + eps*cos; cosNew = cos - eps*sinNew
// (uses updated sinNew, determinant = 1, amplitude-stable)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process: noexcept, no alloc, fixed arrays)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr)
// - Principle IV: SIMD & DSP Optimization (alignas(32), contiguous arrays)
// - Principle IX: Layer 2 (depends on Layer 0 core + harmonic_types.h)
// - Principle XII: Test-First Development
//
// Spec: specs/115-innexus-m1-core-instrument/spec.md (FR-035 to FR-042)
// ==============================================================================

#pragma once

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/pitch_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Suppress MSVC C4324: structure was padded due to alignment specifier
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace Krate::DSP {

/// @brief Harmonic oscillator bank for additive resynthesis from HarmonicFrames.
///
/// Uses 48 Gordon-Smith Modified Coupled Form (MCF) oscillators to resynthesize
/// audio from the analysis pipeline's HarmonicFrame output. Each oscillator
/// computes a sine wave via the MCF recurrence:
///   sinNew = sin + epsilon * cos
///   cosNew = cos - epsilon * sinNew
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: Layer 0 (math_constants.h, pitch_utils.h), harmonic_types.h
///
/// @par Memory Model
/// SoA (Structure-of-Arrays) layout with 32-byte alignment for SIMD-friendly
/// access. All arrays are fixed-size (kMaxPartials = 48).
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (computes coefficients)
/// - All other methods: Real-time safe (noexcept, no allocations)
class HarmonicOscillatorBank {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Crossfade duration in seconds for large pitch jumps (FR-040, default 3ms)
    static constexpr float kDefaultCrossfadeTimeSec = 0.003f;

    /// Amplitude smoothing time in seconds (FR-041, ~2ms)
    static constexpr float kAmpSmoothTimeSec = 0.002f;

    /// Anti-aliasing fade start as fraction of Nyquist (FR-038)
    static constexpr float kAntiAliasFadeStart = 0.8f;

    /// Output safety clamp
    static constexpr float kOutputClamp = 2.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    HarmonicOscillatorBank() noexcept = default;

    /// @brief Destructor.
    ~HarmonicOscillatorBank() noexcept = default;

    // Non-copyable, movable
    HarmonicOscillatorBank(const HarmonicOscillatorBank&) = delete;
    HarmonicOscillatorBank& operator=(const HarmonicOscillatorBank&) = delete;
    HarmonicOscillatorBank(HarmonicOscillatorBank&&) noexcept = default;
    HarmonicOscillatorBank& operator=(HarmonicOscillatorBank&&) noexcept = default;

    /// @brief Initialize for processing.
    ///
    /// Pre-computes smoothing coefficients and crossfade duration.
    /// Must be called before any processing.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @note NOT real-time safe
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        nyquist_ = static_cast<float>(sampleRate / 2.0);
        inverseSampleRate_ = 1.0f / static_cast<float>(sampleRate);

        // Amplitude smoothing coefficient: one-pole for ~2ms (FR-041)
        // coeff = 1 - exp(-1 / (tau * sampleRate))
        // tau = kAmpSmoothTimeSec
        ampSmoothCoeff_ = 1.0f - std::exp(-1.0f /
            (kAmpSmoothTimeSec * static_cast<float>(sampleRate)));

        // Crossfade length in samples (FR-040, default 3ms)
        crossfadeLengthSamples_ = static_cast<int>(
            kDefaultCrossfadeTimeSec * static_cast<float>(sampleRate));
        if (crossfadeLengthSamples_ < 1) crossfadeLengthSamples_ = 1;

        // Semitone ratio threshold for crossfade detection (> 1 semitone)
        crossfadeThresholdRatio_ = semitonesToRatio(1.0f);

        reset();
        prepared_ = true;
    }

    /// @brief Reset all oscillator states to silence.
    ///
    /// Clears all SoA arrays and sets active partials to 0.
    /// Does not change configuration or sample rate.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        sinState_.fill(0.0f);
        cosState_.fill(1.0f); // cos(0) = 1
        epsilon_.fill(0.0f);
        currentAmplitude_.fill(0.0f);
        targetAmplitude_.fill(0.0f);
        antiAliasGain_.fill(0.0f);
        relativeFrequency_.fill(0.0f);
        inharmonicDeviation_.fill(0.0f);
        harmonicIndex_.fill(0);
        activePartials_ = 0;
        targetPitch_ = 0.0f;
        frameLoaded_ = false;

        // Reset crossfade state
        crossfadeRemaining_ = 0;
        crossfadeOldLevel_ = 0.0f;
        renormCounter_ = 0;
    }

    // =========================================================================
    // Frame and Pitch Control
    // =========================================================================

    /// @brief Load a harmonic frame (updates target amplitudes and frequencies).
    ///
    /// Updates target amplitudes and frequency parameters from the analysis frame.
    /// Phase accumulators (sinState/cosState) are NOT reset for phase continuity
    /// (FR-039). Epsilon and target amplitude are updated per partial.
    ///
    /// @param frame The harmonic analysis frame
    /// @param targetPitch The target playback pitch in Hz (from MIDI note)
    /// @note Real-time safe
    void loadFrame(const HarmonicFrame& frame, float targetPitch) noexcept {
        if (!prepared_) return;

        // Detect large pitch jumps for crossfade (FR-040)
        if (frameLoaded_ && targetPitch_ > 0.0f && targetPitch > 0.0f) {
            float ratio = targetPitch / targetPitch_;
            if (ratio < 1.0f) ratio = 1.0f / ratio;
            if (ratio > crossfadeThresholdRatio_) {
                // Snapshot current output level for crossfade
                crossfadeOldLevel_ = lastOutputSample_;
                crossfadeRemaining_ = crossfadeLengthSamples_;
            }
        }

        targetPitch_ = targetPitch;
        int numPartials = std::min(frame.numPartials, static_cast<int>(kMaxPartials));
        activePartials_ = numPartials;

        for (int i = 0; i < numPartials; ++i) {
            const auto& partial = frame.partials[i];
            harmonicIndex_[i] = partial.harmonicIndex;
            relativeFrequency_[i] = partial.relativeFrequency;
            inharmonicDeviation_[i] = partial.inharmonicDeviation;
            targetAmplitude_[i] = partial.amplitude;

            // Initialize oscillator state if this is the first frame
            if (!frameLoaded_) {
                float freq = computePartialFrequency(i);
                float phase = partial.phase; // use analysis phase
                sinState_[i] = std::sin(phase);
                cosState_[i] = std::cos(phase);
                epsilon_[i] = 2.0f * std::sin(kPi * freq * inverseSampleRate_);
                currentAmplitude_[i] = 0.0f; // will ramp up via smoothing
            }
        }

        // Zero out unused partials
        for (size_t i = static_cast<size_t>(numPartials); i < kMaxPartials; ++i) {
            targetAmplitude_[i] = 0.0f;
            harmonicIndex_[i] = 0;
        }

        // Recalculate frequencies and anti-aliasing for all active partials
        recalculateFrequencies();
        recalculateAntiAliasing();

        frameLoaded_ = true;
    }

    /// @brief Set the target pitch (MIDI-driven).
    ///
    /// Recalculates epsilon for all active partials immediately.
    /// Anti-aliasing gain is recalculated on the new frequencies.
    /// Triggers crossfade if jump > 1 semitone (FR-040).
    ///
    /// @param frequencyHz Target fundamental frequency in Hz
    /// @note Real-time safe
    void setTargetPitch(float frequencyHz) noexcept {
        if (!prepared_ || frequencyHz <= 0.0f) return;

        // Detect large pitch jumps for crossfade (FR-040)
        if (frameLoaded_ && targetPitch_ > 0.0f) {
            float ratio = frequencyHz / targetPitch_;
            if (ratio < 1.0f) ratio = 1.0f / ratio;
            if (ratio > crossfadeThresholdRatio_) {
                crossfadeOldLevel_ = lastOutputSample_;
                crossfadeRemaining_ = crossfadeLengthSamples_;
            }
        }

        targetPitch_ = frequencyHz;
        recalculateFrequencies();
        recalculateAntiAliasing();
    }

    /// @brief Set inharmonicity amount (FR-042).
    ///
    /// @param amount 0.0 = perfect harmonic ratios, 1.0 = source's captured deviations
    /// @note Real-time safe
    void setInharmonicityAmount(float amount) noexcept {
        inharmonicityAmount_ = std::clamp(amount, 0.0f, 1.0f);
        if (prepared_ && frameLoaded_) {
            recalculateFrequencies();
            recalculateAntiAliasing();
        }
    }

    // =========================================================================
    // Processing (FR-035)
    // =========================================================================

    /// @brief Generate a single output sample.
    ///
    /// @return Mono output sample
    /// @note Real-time safe
    [[nodiscard]] float process() noexcept {
        if (!prepared_ || !frameLoaded_) {
            return 0.0f;
        }

        float sum = 0.0f;
        const int n = activePartials_;

        // Periodic renormalization counter -- every 16 samples, correct amplitude
        // drift in the MCF due to floating-point rounding at high epsilon values.
        // Frequency of renormalization chosen to be fast enough for near-Nyquist
        // partials (where the MCF has frequency-dependent amplitude scaling)
        // while remaining efficient.
        ++renormCounter_;
        const bool doRenorm = (renormCounter_ >= 16);
        if (doRenorm) renormCounter_ = 0;

        for (int i = 0; i < n; ++i) {
            // Amplitude smoothing (FR-041)
            float target = targetAmplitude_[i] * antiAliasGain_[i];
            currentAmplitude_[i] += ampSmoothCoeff_ * (target - currentAmplitude_[i]);

            // MCF oscillator (FR-035)
            float s = sinState_[i];
            float c = cosState_[i];
            float eps = epsilon_[i];

            // Output: amplitude * sine
            sum += s * currentAmplitude_[i];

            // Advance phasor: Gordon-Smith MCF (determinant = 1)
            float sNew = s + eps * c;
            float cNew = c - eps * sNew; // uses updated sNew

            // Periodic renormalization: correct amplitude drift
            if (doRenorm) {
                float mag2 = sNew * sNew + cNew * cNew;
                if (mag2 > 0.0f) {
                    float invMag = 1.0f / std::sqrt(mag2);
                    sNew *= invMag;
                    cNew *= invMag;
                }
            }

            sinState_[i] = sNew;
            cosState_[i] = cNew;
        }

        // Also smoothly fade out any partials beyond activePartials_ that
        // still have residual amplitude
        for (size_t i = static_cast<size_t>(n); i < kMaxPartials; ++i) {
            if (currentAmplitude_[i] > 1e-8f) {
                currentAmplitude_[i] += ampSmoothCoeff_ * (0.0f - currentAmplitude_[i]);

                float s = sinState_[i];
                float c = cosState_[i];
                float eps = epsilon_[i];
                sum += s * currentAmplitude_[i];
                float sNew = s + eps * c;
                float cNew = c - eps * sNew;

                if (doRenorm) {
                    float mag2 = sNew * sNew + cNew * cNew;
                    if (mag2 > 0.0f) {
                        float invMag = 1.0f / std::sqrt(mag2);
                        sNew *= invMag;
                        cNew *= invMag;
                    }
                }

                sinState_[i] = sNew;
                cosState_[i] = cNew;
            }
        }

        // Apply crossfade if active (FR-040)
        if (crossfadeRemaining_ > 0) {
            float fadeProgress = static_cast<float>(crossfadeRemaining_) /
                                 static_cast<float>(crossfadeLengthSamples_);
            // Linear crossfade: old * fadeProgress + new * (1 - fadeProgress)
            sum = crossfadeOldLevel_ * fadeProgress + sum * (1.0f - fadeProgress);
            --crossfadeRemaining_;
        }

        // Safety clamp
        sum = std::clamp(sum, -kOutputClamp, kOutputClamp);

        lastOutputSample_ = sum;
        return sum;
    }

    /// @brief Generate a block of output samples.
    ///
    /// @param output Destination buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    /// @note Real-time safe
    void processBlock(float* output, size_t numSamples) noexcept {
        if (output == nullptr || numSamples == 0) {
            return;
        }

        if (!prepared_) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = 0.0f;
            }
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if the bank has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief Get the current target pitch.
    [[nodiscard]] float getTargetPitch() const noexcept { return targetPitch_; }

    /// @brief Get the number of active partials.
    [[nodiscard]] int getActivePartials() const noexcept { return activePartials_; }

    /// @brief Get the current inharmonicity amount.
    [[nodiscard]] float getInharmonicityAmount() const noexcept { return inharmonicityAmount_; }

    /// @brief Check if a frame has been loaded.
    [[nodiscard]] bool isFrameLoaded() const noexcept { return frameLoaded_; }

    /// @brief Verify that SoA arrays are 32-byte aligned (for testing).
    [[nodiscard]] bool areArraysAligned() const noexcept {
        return (reinterpret_cast<std::uintptr_t>(sinState_.data()) % 32 == 0) &&
               (reinterpret_cast<std::uintptr_t>(cosState_.data()) % 32 == 0) &&
               (reinterpret_cast<std::uintptr_t>(epsilon_.data()) % 32 == 0) &&
               (reinterpret_cast<std::uintptr_t>(currentAmplitude_.data()) % 32 == 0);
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Compute per-partial frequency (FR-037).
    ///
    /// freq_n = (harmonicIndex + inharmonicDeviation * inharmonicityAmount) * targetPitch
    [[nodiscard]] float computePartialFrequency(int partialIndex) const noexcept {
        int n = harmonicIndex_[partialIndex];
        float deviation = inharmonicDeviation_[partialIndex];
        return (static_cast<float>(n) + deviation * inharmonicityAmount_) * targetPitch_;
    }

    /// @brief Recalculate epsilon for all active partials.
    ///
    /// Epsilon is clamped to a safe range to prevent MCF instability
    /// at frequencies very close to Nyquist.
    void recalculateFrequencies() noexcept {
        // Maximum safe epsilon to prevent MCF divergence near Nyquist
        // |epsilon| must be < 2.0; use 1.99 as safety margin
        constexpr float kMaxEpsilon = 1.99f;

        for (int i = 0; i < activePartials_; ++i) {
            float freq = computePartialFrequency(i);
            float eps = 2.0f * std::sin(kPi * freq * inverseSampleRate_);
            epsilon_[i] = std::clamp(eps, -kMaxEpsilon, kMaxEpsilon);
        }
    }

    /// @brief Recalculate anti-aliasing gains (FR-038).
    ///
    /// Full gain below 80% Nyquist, linear fade to zero at Nyquist.
    void recalculateAntiAliasing() noexcept {
        const float fadeStart = kAntiAliasFadeStart * nyquist_;
        const float fadeRange = nyquist_ - fadeStart; // 0.2 * nyquist

        for (int i = 0; i < activePartials_; ++i) {
            float freq = computePartialFrequency(i);
            if (freq <= fadeStart) {
                antiAliasGain_[i] = 1.0f;
            } else if (freq >= nyquist_) {
                antiAliasGain_[i] = 0.0f;
            } else {
                antiAliasGain_[i] = (nyquist_ - freq) / fadeRange;
            }
        }

        // Zero gain for unused partials
        for (size_t i = static_cast<size_t>(activePartials_); i < kMaxPartials; ++i) {
            antiAliasGain_[i] = 0.0f;
        }
    }

    // =========================================================================
    // Members -- SoA layout, 32-byte aligned (FR-036)
    // =========================================================================

    alignas(32) std::array<float, kMaxPartials> sinState_{};
    alignas(32) std::array<float, kMaxPartials> cosState_{};
    alignas(32) std::array<float, kMaxPartials> epsilon_{};
    alignas(32) std::array<float, kMaxPartials> currentAmplitude_{};
    alignas(32) std::array<float, kMaxPartials> targetAmplitude_{};
    alignas(32) std::array<float, kMaxPartials> antiAliasGain_{};
    alignas(32) std::array<float, kMaxPartials> relativeFrequency_{};
    alignas(32) std::array<float, kMaxPartials> inharmonicDeviation_{};

    /// Harmonic index per partial (not float -- integer)
    std::array<int, kMaxPartials> harmonicIndex_{};

    // --- Configuration ---
    float targetPitch_ = 0.0f;            ///< Target fundamental frequency (Hz)
    float inharmonicityAmount_ = 1.0f;    ///< 0 = pure harmonics, 1 = source deviation
    double sampleRate_ = 44100.0;          ///< Current sample rate
    float nyquist_ = 22050.0f;            ///< Nyquist frequency
    float inverseSampleRate_ = 1.0f / 44100.0f; ///< Precomputed 1/sampleRate

    // --- Smoothing ---
    float ampSmoothCoeff_ = 0.0f;         ///< One-pole amplitude smoothing coefficient

    // --- Crossfade (FR-040) ---
    int crossfadeLengthSamples_ = 0;      ///< Crossfade duration in samples
    int crossfadeRemaining_ = 0;          ///< Remaining crossfade samples
    float crossfadeOldLevel_ = 0.0f;      ///< Captured output level before jump
    float crossfadeThresholdRatio_ = 1.0f; ///< Ratio threshold (> 1 semitone)

    // --- State ---
    int activePartials_ = 0;              ///< Number of active partials
    float lastOutputSample_ = 0.0f;       ///< For crossfade capture
    int renormCounter_ = 0;               ///< Counter for periodic MCF renormalization
    bool prepared_ = false;               ///< Whether prepare() has been called
    bool frameLoaded_ = false;            ///< Whether loadFrame() has been called
};

} // namespace Krate::DSP

#ifdef _MSC_VER
#pragma warning(pop)
#endif
