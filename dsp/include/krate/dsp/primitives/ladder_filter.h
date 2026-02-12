// ==============================================================================
// Layer 1: DSP Primitives
// ladder_filter.h - Moog Ladder Filter
// ==============================================================================
// API Contract for specs/075-ladder-filter
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, enum class)
// - Principle IX: Layer 1 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (flush denormals, handle edge cases)
// - Principle XII: Test-First Development
// - Principle XIV: ODR Prevention (unique class name verified)
//
// References:
// - Huovilainen, A. (2004). "Non-Linear Digital Implementation of the Moog Ladder Filter"
// - Stilson, T. & Smith, J. (1996). "Analyzing the Moog VCF"
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// LadderModel Enum
// =============================================================================

/// @brief Processing model selection for LadderFilter
///
/// @see LadderFilter::setModel()
enum class LadderModel : uint8_t {
    Linear,     ///< CPU-efficient 4-pole cascade without saturation (Stilson/Smith)
    Nonlinear   ///< Tanh saturation per stage for analog character (Huovilainen)
};

// =============================================================================
// LadderFilter Class
// =============================================================================

/// @brief Moog-style 4-pole resonant lowpass ladder filter
///
/// Implements the classic Moog ladder filter topology with:
/// - Two processing models: Linear (efficient) and Nonlinear (analog character)
/// - Variable slope: 1-4 poles (6-24 dB/octave)
/// - Resonance: 0-4 with self-oscillation at ~3.9
/// - Drive: 0-24 dB input gain
/// - Runtime-configurable oversampling (1x/2x/4x) for nonlinear model
/// - Internal parameter smoothing (~5ms) to prevent zipper noise
/// - Optional resonance compensation
///
/// @par Layer
/// Layer 1 - DSP Primitive
///
/// @par Thread Safety
/// NOT thread-safe. Must be used from a single thread (audio thread).
/// All processing methods are noexcept and real-time safe after prepare().
///
/// @par Dependencies
/// - Oversampler2xMono, Oversampler4xMono (primitives/oversampler.h)
/// - OnePoleSmoother (primitives/smoother.h)
/// - FastMath::fastTanh (core/fast_math.h)
/// - dbToGain, flushDenormal (core/db_utils.h)
///
/// @par References
/// - Huovilainen, A. (2004). "Non-Linear Digital Implementation of the Moog Ladder Filter"
/// - Stilson, T. & Smith, J. (1996). "Analyzing the Moog VCF"
///
/// @example
/// @code
/// LadderFilter filter;
/// filter.prepare(44100.0, 512);
/// filter.setModel(LadderModel::Nonlinear);
/// filter.setCutoff(1000.0f);
/// filter.setResonance(2.0f);
///
/// float output = filter.process(input);
/// @endcode
class LadderFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum cutoff frequency (Hz)
    static constexpr float kMinCutoff = 20.0f;

    /// Maximum cutoff as ratio of sample rate (Nyquist safety margin)
    static constexpr float kMaxCutoffRatio = 0.45f;

    /// Minimum resonance value
    static constexpr float kMinResonance = 0.0f;

    /// Maximum resonance value
    /// Nonlinear model handles high k safely via tanh saturation.
    /// Self-oscillation onset depends on model and frequency.
    static constexpr float kMaxResonance = 8.0f;

    /// Minimum drive in dB (unity gain)
    static constexpr float kMinDriveDb = 0.0f;

    /// Maximum drive in dB
    static constexpr float kMaxDriveDb = 24.0f;

    /// Minimum slope (1 pole = 6 dB/oct)
    static constexpr int kMinSlope = 1;

    /// Maximum slope (4 poles = 24 dB/oct)
    static constexpr int kMaxSlope = 4;

    /// Maximum resonance for linear model (below self-oscillation threshold)
    /// Linear model has no amplitude limiting, so k=4.0 causes unbounded growth
    static constexpr float kMaxLinearResonance = 3.85f;

    /// Default parameter smoothing time in milliseconds
    static constexpr float kDefaultSmoothingTimeMs = 5.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Creates an unprepared filter. Call prepare() before processing.
    LadderFilter() noexcept = default;

    /// @brief Prepare filter for processing
    ///
    /// Must be called before any processing. Allocates internal buffers
    /// and configures oversamplers and smoothers.
    ///
    /// @param sampleRate Sample rate in Hz (22050 - 192000)
    /// @param maxBlockSize Maximum block size for processBlock()
    ///
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, int maxBlockSize) noexcept {
        // Clamp sample rate to valid range
        sampleRate_ = std::clamp(sampleRate, 22050.0, 192000.0);
        updateOversampledRate();

        // Configure smoothers with 5ms time constant
        cutoffSmoother_.configure(kDefaultSmoothingTimeMs, static_cast<float>(sampleRate_));
        resonanceSmoother_.configure(kDefaultSmoothingTimeMs, static_cast<float>(sampleRate_));
        cutoffSmoother_.snapTo(targetCutoff_);
        resonanceSmoother_.snapTo(targetResonance_);

        // Prepare oversamplers
        oversampler2x_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize),
                              OversamplingQuality::High, OversamplingMode::ZeroLatency);
        oversampler4x_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize),
                              OversamplingQuality::High, OversamplingMode::ZeroLatency);

        reset();
        prepared_ = true;
    }

    /// @brief Reset filter state
    ///
    /// Clears all filter state variables while preserving configuration.
    /// Use when starting a new audio stream or after silence.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        state_.fill(0.0f);
        tanhState_.fill(0.0f);
        cutoffSmoother_.reset();
        resonanceSmoother_.reset();
        cutoffSmoother_.snapTo(targetCutoff_);
        resonanceSmoother_.snapTo(targetResonance_);
        oversampler2x_.reset();
        oversampler4x_.reset();
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set processing model
    ///
    /// @param model LadderModel::Linear for CPU-efficient processing,
    ///              LadderModel::Nonlinear for analog-like saturation
    ///
    /// @note Safe to call during processing (click-free transition)
    void setModel(LadderModel model) noexcept {
        model_ = model;
    }

    /// @brief Set number of iterations per sample for nonlinear model
    ///
    /// Multiple iterations reduce the effective feedback delay, improving
    /// self-oscillation frequency accuracy and lowering the threshold.
    /// Based on Huovilainen's approach: N iterations with coefficients at
    /// N*sampleRate effectively process at N times the base rate.
    ///
    /// @param n Number of iterations (1-4). Default is 1.
    ///
    /// @note Only affects nonlinear model via process(). No effect on
    ///       Linear model or oversampled processBlock() path.
    void setIterations(int n) noexcept {
        iterations_ = std::clamp(n, 1, 4);
    }

    /// @brief Set oversampling factor for nonlinear model
    ///
    /// Higher factors reduce aliasing from nonlinear processing at the
    /// cost of increased CPU usage. Has no effect on Linear model.
    ///
    /// @param factor 1 (no oversampling), 2, or 4. Value 3 rounds to 4.
    ///
    /// @note Affects latency. Call getLatency() after changing.
    void setOversamplingFactor(int factor) noexcept {
        factor = std::clamp(factor, 1, 4);
        if (factor == 3) factor = 4;  // Round up
        oversamplingFactor_ = factor;
        updateOversampledRate();
    }

    /// @brief Enable or disable resonance gain compensation
    ///
    /// When enabled, applies gain reduction as resonance increases to
    /// maintain consistent output level (within ~3dB).
    ///
    /// Formula: compensation = 1.0 / (1.0 + resonance * 0.25)
    ///
    /// @param enabled true to enable compensation
    void setResonanceCompensation(bool enabled) noexcept {
        resonanceCompensation_ = enabled;
    }

    /// @brief Set filter slope (number of poles)
    ///
    /// @param poles 1 (6dB/oct), 2 (12dB/oct), 3 (18dB/oct), or 4 (24dB/oct)
    ///
    /// @note Clamped to [1, 4]
    void setSlope(int poles) noexcept {
        slope_ = std::clamp(poles, kMinSlope, kMaxSlope);
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set cutoff frequency
    ///
    /// Sets target cutoff with internal smoothing (~5ms transition time).
    ///
    /// @param hz Cutoff frequency in Hz (clamped to [20, sampleRate * 0.45])
    void setCutoff(float hz) noexcept {
        float maxCutoff = static_cast<float>(sampleRate_) * kMaxCutoffRatio;
        targetCutoff_ = std::clamp(hz, kMinCutoff, maxCutoff);
        cutoffSmoother_.setTarget(targetCutoff_);
    }

    /// @brief Set resonance amount
    ///
    /// Sets target resonance with internal smoothing.
    /// Self-oscillation occurs at approximately 3.9.
    ///
    /// @param amount Resonance value (clamped to [0, 4])
    void setResonance(float amount) noexcept {
        targetResonance_ = std::clamp(amount, kMinResonance, kMaxResonance);
        resonanceSmoother_.setTarget(targetResonance_);
    }

    /// @brief Set input drive gain
    ///
    /// Applies gain before filtering. In Nonlinear mode, this increases
    /// saturation and harmonic content.
    ///
    /// @param db Drive in decibels (clamped to [0, 24])
    void setDrive(float db) noexcept {
        driveDb_ = std::clamp(db, kMinDriveDb, kMaxDriveDb);
        driveGain_ = dbToGain(driveDb_);
    }

    // =========================================================================
    // Getters
    // =========================================================================

    /// @brief Get current processing model
    [[nodiscard]] LadderModel getModel() const noexcept { return model_; }

    /// @brief Get target cutoff frequency
    [[nodiscard]] float getCutoff() const noexcept { return targetCutoff_; }

    /// @brief Get target resonance
    [[nodiscard]] float getResonance() const noexcept { return targetResonance_; }

    /// @brief Get drive in dB
    [[nodiscard]] float getDrive() const noexcept { return driveDb_; }

    /// @brief Get current slope (number of poles)
    [[nodiscard]] int getSlope() const noexcept { return slope_; }

    /// @brief Get current oversampling factor
    [[nodiscard]] int getOversamplingFactor() const noexcept { return oversamplingFactor_; }

    /// @brief Check if resonance compensation is enabled
    [[nodiscard]] bool isResonanceCompensationEnabled() const noexcept { return resonanceCompensation_; }

    /// @brief Check if filter is prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief Get processing latency in samples
    ///
    /// Returns latency introduced by oversampling filters.
    /// Zero for Linear model or 1x oversampling.
    ///
    /// @return Latency in samples at base sample rate
    [[nodiscard]] int getLatency() const noexcept {
        if (model_ == LadderModel::Linear || oversamplingFactor_ == 1) {
            return 0;
        }
        if (oversamplingFactor_ == 2) {
            return static_cast<int>(oversampler2x_.getLatency());
        }
        return static_cast<int>(oversampler4x_.getLatency());
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    ///
    /// @param input Input sample
    /// @return Filtered output sample
    ///
    /// @pre prepare() must have been called
    /// @post If input is NaN or Inf, returns 0.0f and resets state
    ///
    /// @note Real-time safe, noexcept
    /// @note For nonlinear model, this processes at base sample rate.
    ///       Use processBlock() for oversampled nonlinear processing.
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) return input;  // Bypass if not prepared

        // Handle NaN/Inf
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Smooth parameters
        float smoothedCutoff = cutoffSmoother_.process();
        float smoothedResonance = resonanceSmoother_.process();

        // Apply drive
        input *= driveGain_;

        // Process based on model
        float output;
        if (model_ == LadderModel::Linear) {
            float g = calculateG(smoothedCutoff, static_cast<float>(sampleRate_));
            output = processLinear(input, g, smoothedResonance);
        } else {
            // Huovilainen N-iteration approach: process filter N times per sample
            // using coefficients at N*sampleRate. This reduces the effective
            // feedback delay by 1/N, improving self-oscillation accuracy.
            float effectiveRate = static_cast<float>(sampleRate_) * static_cast<float>(iterations_);
            float g = calculateG(smoothedCutoff, effectiveRate);
            for (int i = 0; i < iterations_ - 1; ++i) {
                (void)processNonlinear(input, g, smoothedResonance);
            }
            output = processNonlinear(input, g, smoothedResonance);
        }

        // Apply resonance compensation if enabled
        if (resonanceCompensation_) {
            output = applyCompensation(output, smoothedResonance);
        }

        return output;
    }

    /// @brief Process a block of samples in-place
    ///
    /// For Nonlinear model with oversampling > 1, this method handles
    /// upsampling, processing at oversampled rate, and decimation internally.
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    ///
    /// @pre prepare() must have been called with maxBlockSize >= numSamples
    ///
    /// @note Real-time safe, noexcept
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (!prepared_ || buffer == nullptr || numSamples == 0) return;

        // Direct processing for linear model or 1x oversampling
        if (model_ == LadderModel::Linear || oversamplingFactor_ == 1) {
            for (size_t i = 0; i < numSamples; ++i) {
                buffer[i] = process(buffer[i]);
            }
            return;
        }

        // Oversampled processing for nonlinear model
        // Use the oversampler to upsample, process at higher rate, and decimate
        if (oversamplingFactor_ == 2) {
            oversampler2x_.process(buffer, numSamples, [this](float* upsampled, size_t n) {
                processOversampledBlock(upsampled, n);
            });
        } else { // 4x
            oversampler4x_.process(buffer, numSamples, [this](float* upsampled, size_t n) {
                processOversampledBlock(upsampled, n);
            });
        }
    }

private:
    // =========================================================================
    // Internal Constants
    // =========================================================================

    /// Thermal voltage scaling for Huovilainen model (affects saturation character)
    static constexpr float kThermal = 1.22f;

    // =========================================================================
    // State Variables
    // =========================================================================

    /// One-pole stage outputs (4 stages)
    std::array<float, 4> state_{};

    /// Cached tanh values for Huovilainen model
    std::array<float, 4> tanhState_{};

    // =========================================================================
    // Smoothers
    // =========================================================================

    /// Cutoff frequency smoother
    OnePoleSmoother cutoffSmoother_;

    /// Resonance smoother
    OnePoleSmoother resonanceSmoother_;

    // =========================================================================
    // Oversamplers
    // =========================================================================

    /// 2x oversampler for nonlinear model
    Oversampler2xMono oversampler2x_;

    /// 4x oversampler for nonlinear model
    Oversampler4xMono oversampler4x_;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Base sample rate
    double sampleRate_ = 44100.0;

    /// Effective sample rate (sampleRate_ * oversamplingFactor_)
    double oversampledRate_ = 44100.0;

    /// Current processing model
    LadderModel model_ = LadderModel::Linear;

    /// Oversampling factor (1, 2, or 4)
    int oversamplingFactor_ = 2;

    /// Number of iterations per sample for nonlinear model (1-4)
    int iterations_ = 1;

    /// Number of poles (1-4)
    int slope_ = 4;

    /// Resonance compensation enabled
    bool resonanceCompensation_ = false;

    /// Filter is prepared for processing
    bool prepared_ = false;

    // =========================================================================
    // Cached Parameters
    // =========================================================================

    /// Target cutoff frequency (Hz)
    float targetCutoff_ = 1000.0f;

    /// Target resonance (0-4)
    float targetResonance_ = 0.0f;

    /// Drive in dB
    float driveDb_ = 0.0f;

    /// Cached linear gain from drive
    float driveGain_ = 1.0f;

    // =========================================================================
    // Private Methods
    // =========================================================================

    /// Update oversampledRate_ based on current settings
    void updateOversampledRate() noexcept {
        oversampledRate_ = sampleRate_ * static_cast<double>(oversamplingFactor_);
    }

    /// Calculate frequency coefficient g
    ///
    /// Uses the standard bilinear transform coefficient for the one-pole
    /// stages in the ladder filter. Includes clamping to prevent instability.
    [[nodiscard]] float calculateG(float cutoff, float rate) noexcept {
        // Clamp to prevent instability near Nyquist
        // Use 0.45 as max ratio to stay well away from the pi/2 singularity
        float fc = std::min(cutoff, rate * 0.45f);

        // Standard bilinear transform coefficient
        float g = std::tan(kPi * fc / rate);

        // Additional safety: clamp g to prevent numerical instability
        // At fc/rate = 0.45, g = tan(0.45*pi) = ~5.67
        return std::min(g, 10.0f);
    }

    /// Linear model processing (Stilson/Smith)
    ///
    /// Implements the 4-pole cascade with trapezoidal integration.
    /// Each stage is a one-pole lowpass filter:
    ///   y[n] = a * (x[n] - y[n-1]) + y[n-1]
    /// where a = 2*g / (1 + g) for trapezoidal integration
    [[nodiscard]] float processLinear(float input, float g, float k) noexcept {
        // Calculate normalized coefficient for trapezoidal integration
        float a = 2.0f * g / (1.0f + g);

        // Cap resonance below self-oscillation threshold for linear model.
        // At k=4.0 the loop gain equals 1 and self-oscillation occurs.
        // Without tanh saturation (nonlinear model), amplitude grows unbounded.
        float safeK = std::min(k, kMaxLinearResonance);

        // Feedback from 4th stage
        float fb = state_[3] * safeK;
        float u = input - fb;

        // Cascade through 4 stages using trapezoidal one-pole
        for (int i = 0; i < 4; ++i) {
            float stageInput = (i == 0) ? u : state_[i - 1];
            // One-pole lowpass: y = y_prev + a * (x - y_prev)
            state_[i] = detail::flushDenormal(state_[i] + a * (stageInput - state_[i]));
        }

        return selectOutput();
    }

    /// Nonlinear model processing (Huovilainen)
    ///
    /// Implements a bilinear-transform variant of the Huovilainen algorithm:
    /// - Per-stage tanh saturation for analog-like nonlinearity
    /// - Thermal voltage scaling (kThermal = 1.22)
    /// - Accumulation in voltage domain (state_[i]), not tanh-compressed domain
    /// - Thermal compensation in coefficient (divide by kThermal) ensures
    ///   small-signal behavior matches linear model
    /// - LINEAR feedback path (tanh only inside stages, not on feedback)
    ///
    /// Self-oscillation occurs at k ≈ 4.0 (same as linear model for small
    /// signals). The tanh inside each stage naturally limits oscillation
    /// amplitude. Uses half-sample delay averaging for improved frequency
    /// accuracy at higher cutoff settings.
    ///
    /// Reference: Huovilainen, A. (2004). "Non-Linear Digital Implementation
    /// of the Moog Ladder Filter", Proc. DAFx-04
    [[nodiscard]] float processNonlinear(float input, float g, float k) noexcept {
        // Calculate normalized coefficient for trapezoidal integration.
        // Divide by kThermal to compensate for tanh(x*T) gain: small-signal
        // behavior matches linear model (tanh(x*T) ≈ x*T, coefficient/T cancels).
        float a = 2.0f * g / ((1.0f + g) * kThermal);

        // Linear feedback from 4th stage output.
        // The tanh nonlinearity belongs INSIDE the stages only (not on feedback).
        // With bilinear transform discretization, the tan() pre-warping handles
        // frequency mapping correctly without needing half-sample delay averaging.
        float fb = k * state_[3];
        float u = input - fb;

        // Cascade through 4 stages with per-stage saturation.
        // KEY: Accumulate in voltage domain (state_[i]), apply tanh only for
        // the difference computation. This gives correct DC gain of 1.0 per stage.
        for (int i = 0; i < 4; ++i) {
            float stageInput = (i == 0) ? u : state_[i - 1];

            // Apply thermal scaling to compress input
            float tanhInput = FastMath::fastTanh(stageInput * kThermal);

            // Trapezoidal one-pole with saturation:
            // state += (a/T) * (tanh(input * T) - tanh(state * T))
            // For small signals: state += a * (input - state) [T cancels out]
            float newState = state_[i] + a * (tanhInput - tanhState_[i]);
            state_[i] = detail::flushDenormal(newState);

            // Cache the tanh of updated state for next stage and next sample
            tanhState_[i] = FastMath::fastTanh(state_[i] * kThermal);
        }

        return selectOutput();
    }

    /// Select output based on slope setting
    [[nodiscard]] float selectOutput() const noexcept {
        switch (slope_) {
            case 1: return state_[0];
            case 2: return state_[1];
            case 3: return state_[2];
            case 4:
            default: return state_[3];
        }
    }

    /// Apply resonance gain compensation
    [[nodiscard]] float applyCompensation(float output, float k) noexcept {
        float compensation = 1.0f / (1.0f + k * 0.25f);
        return output * compensation;
    }

    /// Process a block of samples at the oversampled rate (called by oversampler)
    ///
    /// This is the core nonlinear processing that runs at 2x or 4x the base rate.
    /// Parameter smoothing runs at oversampled rate for smooth modulation.
    void processOversampledBlock(float* buffer, size_t numSamples) noexcept {
        float rate = static_cast<float>(oversampledRate_);

        for (size_t i = 0; i < numSamples; ++i) {
            // Smooth parameters at oversampled rate
            float smoothedCutoff = cutoffSmoother_.process();
            float smoothedResonance = resonanceSmoother_.process();

            // Calculate coefficient at oversampled rate
            float g = calculateG(smoothedCutoff, rate);

            // Handle NaN/Inf
            float input = buffer[i];
            if (detail::isNaN(input) || detail::isInf(input)) {
                reset();
                buffer[i] = 0.0f;
                continue;
            }

            // Apply drive
            input *= driveGain_;

            // Process nonlinear (always nonlinear in oversampled path)
            float output = processNonlinear(input, g, smoothedResonance);

            // Apply compensation if enabled
            if (resonanceCompensation_) {
                output = applyCompensation(output, smoothedResonance);
            }

            buffer[i] = output;
        }
    }
};

} // namespace DSP
} // namespace Krate
