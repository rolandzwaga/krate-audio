// ==============================================================================
// Layer 1: DSP Primitive - Stochastic Shaper
// ==============================================================================
// Waveshaper with stochastic modulation for analog-style variation.
// Adds controlled randomness to waveshaping transfer functions, simulating
// analog component tolerance variation.
//
// Feature: 106-stochastic-shaper
// Layer: 1 (Primitives)
// Dependencies:
//   - Layer 1: primitives/waveshaper.h (Waveshaper, WaveshapeType)
//   - Layer 1: primitives/smoother.h (OnePoleSmoother)
//   - Layer 0: core/random.h (Xorshift32)
//   - Layer 0: core/db_utils.h (isNaN, isInf)
//   - stdlib: <cstdint>, <cstddef>, <algorithm>, <cmath>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr where possible)
// - Principle IX: Layer 1 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
// - Principle XI: Performance Budget (< 0.1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/106-stochastic-shaper/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/pink_noise_filter.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/waveshaper.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Noise Color for Stochastic Modulation
// =============================================================================

/// @brief Noise distribution color for shaping stochastic modulation streams.
///
/// Controls the spectral character of the random values feeding the jitter
/// and drive modulation smoothers.
enum class StochasticNoiseColor : int {
    White = 0,   ///< Flat spectrum — uniform variation (default)
    Pink = 1,    ///< -3dB/oct — organic 1/f analog drift
    Brown = 2,   ///< -6dB/oct — slow brownian wander
    Blue = 3,    ///< +3dB/oct — brighter, responsive variation
    Violet = 4   ///< +6dB/oct — fast, twitchy, aggressive
};

/// @brief Per-stream noise color filter state.
///
/// Filters white noise from the RNG into the selected noise color.
/// Each modulation stream (jitter, drive) gets its own instance for
/// independent spectral characteristics.
struct NoiseColorFilter {
    PinkNoiseFilter pinkFilter;
    float brownState = 0.0f;
    float prevSample = 0.0f;

    [[nodiscard]] float process(float white, StochasticNoiseColor color) noexcept {
        switch (color) {
            case StochasticNoiseColor::White:
                return white;
            case StochasticNoiseColor::Pink:
                return pinkFilter.process(white);
            case StochasticNoiseColor::Brown: {
                constexpr float kLeak = 0.99f;
                brownState = kLeak * brownState + (1.0f - kLeak) * white;
                float out = brownState * 5.0f;
                return std::clamp(out, -1.0f, 1.0f);
            }
            case StochasticNoiseColor::Blue: {
                float pink = pinkFilter.process(white);
                float blue = (pink - prevSample) * 0.7f;
                prevSample = pink;
                return std::clamp(blue, -1.0f, 1.0f);
            }
            case StochasticNoiseColor::Violet: {
                float violet = (white - prevSample) * 0.5f;
                prevSample = white;
                return std::clamp(violet, -1.0f, 1.0f);
            }
            default:
                return white;
        }
    }

    void reset() noexcept {
        pinkFilter.reset();
        brownState = 0.0f;
        prevSample = 0.0f;
    }
};

// =============================================================================
// StochasticShaper Class
// =============================================================================

/// @brief Waveshaper with stochastic modulation for analog-style variation.
///
/// Adds controlled randomness to waveshaping by:
/// 1. Applying smoothed random jitter to the input signal before shaping
/// 2. Modulating the waveshaper drive with smoothed random values
///
/// Both modulations use independent smoothed random streams from a single RNG,
/// providing deterministic reproducibility with the same seed.
///
/// @par Features
/// - All 9 WaveshapeType base types (Tanh, Atan, Cubic, etc.)
/// - Jitter amount control (0-1) for signal offset variation
/// - Jitter rate control (0.01-Nyquist Hz) for variation speed
/// - Coefficient noise (0-1) for drive modulation
/// - Deterministic with seed for reproducibility
/// - Diagnostic getters for testing/validation
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (depends only on Layer 0/1)
/// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
/// - Principle XI: Performance Budget (< 0.1% CPU per instance)
///
/// @par Design Rationale
/// - No internal oversampling: Handled by processor layer when needed
/// - No internal DC blocking: Compose with DCBlocker for asymmetric types
/// - Stateful processing: prepare() required before processing
///
/// @par Usage Example
/// @code
/// StochasticShaper shaper;
/// shaper.prepare(44100.0);
/// shaper.setBaseType(WaveshapeType::Tanh);
/// shaper.setDrive(2.0f);
/// shaper.setJitterAmount(0.3f);    // Subtle random offset
/// shaper.setJitterRate(10.0f);     // Moderate variation rate
/// shaper.setCoefficientNoise(0.2f); // Subtle drive variation
///
/// // Sample-by-sample
/// float output = shaper.process(input);
///
/// // Block processing
/// shaper.processBlock(buffer, numSamples);
/// @endcode
///
/// @see specs/106-stochastic-shaper/spec.md
/// @see Waveshaper for base waveshaping types
/// @see DCBlocker for DC offset removal after asymmetric waveshaping
class StochasticShaper {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kDefaultJitterRate = 10.0f;    ///< Default jitter rate Hz (FR-014)
    static constexpr float kMinJitterRate = 0.01f;        ///< Minimum jitter rate Hz (FR-012)
    static constexpr float kMaxJitterOffset = 0.5f;       ///< Max offset at amount=1.0 (FR-011)
    static constexpr float kDriveModulationRange = 0.5f;  ///< +/- 50% at coeffNoise=1.0 (FR-017)
    static constexpr float kDefaultDrive = 1.0f;          ///< Default drive (FR-008b)
    static constexpr uint32_t kDefaultSeed = 1;           ///< Default RNG seed
    static constexpr float kDriftRate = 0.2f;             ///< Drift smoother rate Hz (slow wander)
    static constexpr float kDriftMaxOffset = 0.3f;        ///< Max drift offset at amount=1.0
    static constexpr float kMinSmoothTimeMs = 0.05f;      ///< Minimum output smooth time
    static constexpr float kMaxOutputSmoothMs = 20.0f;    ///< Maximum output smooth time ms

    /// Correlation mode: controls coupling between jitter and drive modulation
    enum class CorrelationMode : int {
        None = 0,    ///< Independent random streams (default)
        Sample = 1,  ///< Both use same random value per sample
        Param = 2,   ///< Drive mod depth scales with current jitter offset
        Both = 3     ///< Sample + Param correlation
    };

    // =========================================================================
    // Construction (FR-003)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes StochasticShaper with:
    /// - BaseType: Tanh (FR-007)
    /// - Drive: 1.0 (FR-008b)
    /// - JitterAmount: 0.0 (no jitter)
    /// - JitterRate: 10.0 Hz (FR-014)
    /// - CoefficientNoise: 0.0 (no drive modulation)
    /// - Seed: 1
    ///
    /// @note prepare() must be called before processing.
    StochasticShaper() noexcept = default;

    // Non-copyable (contains smoother state)
    StochasticShaper(const StochasticShaper&) = delete;
    StochasticShaper& operator=(const StochasticShaper&) = delete;

    // Movable
    StochasticShaper(StochasticShaper&&) noexcept = default;
    StochasticShaper& operator=(StochasticShaper&&) noexcept = default;

    ~StochasticShaper() = default;

    // =========================================================================
    // Initialization (FR-001, FR-002)
    // =========================================================================

    /// @brief Prepare for processing at given sample rate. (FR-001)
    ///
    /// Initializes jitter smoother and configures sample-rate-dependent
    /// parameters. Must be called before any processing.
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @pre sampleRate >= 1000.0 (clamped internally if lower)
    /// @post Smoothers configured with current jitter rate
    /// @post RNG initialized with current seed
    /// @note NOT real-time safe (may allocate smoother state)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::max(sampleRate, 1000.0);

        // Initialize RNG with current seed (FR-021: 0 replaced with default)
        rng_.seed(seed_ != 0 ? seed_ : kDefaultSeed);

        // Configure smoothers with current jitter rate
        reconfigureSmoothers();
        recalculateUpdateRate();

        // Configure drift smoother (very slow ~0.2 Hz)
        const float driftSmoothMs = std::clamp(800.0f / kDriftRate, kMinSmoothingTimeMs, kMaxSmoothingTimeMs);
        driftSmoother_.configure(driftSmoothMs, static_cast<float>(sampleRate_));
        driftSmoother_.snapTo(rng_.nextFloat());
        samplesPerDriftUpdate_ = static_cast<uint32_t>(sampleRate_ / kDriftRate);

        // Configure output smoother
        reconfigureOutputSmoother();
        outputSmoother_.snapTo(0.0f);

        // Initialize smoother state with random values
        jitterSmoother_.snapTo(rng_.nextFloat());
        driveSmoother_.snapTo(rng_.nextFloat());

        // Reset counters
        jitterCounter_ = 0;
        driftCounter_ = 0;

        prepared_ = true;
    }

    /// @brief Reset state while preserving configuration. (FR-002)
    ///
    /// Reinitializes RNG state and smoother state.
    /// Configuration (type, drive, amounts, rate, seed) is preserved.
    ///
    /// @post RNG restored to saved seed state
    /// @post Smoother states cleared
    /// @post Configuration preserved
    /// @note Real-time safe
    void reset() noexcept {
        // Reinitialize RNG with current seed (FR-021: 0 replaced with default)
        rng_.seed(seed_ != 0 ? seed_ : kDefaultSeed);

        // Reset smoothers and color filters
        jitterSmoother_.reset();
        driveSmoother_.reset();
        driftSmoother_.reset();
        outputSmoother_.reset();
        jitterColorFilter_.reset();
        driveColorFilter_.reset();

        // Initialize smoother state with fresh random values
        jitterSmoother_.snapTo(rng_.nextFloat());
        driveSmoother_.snapTo(rng_.nextFloat());
        driftSmoother_.snapTo(rng_.nextFloat());
        outputSmoother_.snapTo(0.0f);

        // Reset counters and diagnostic state
        jitterCounter_ = 0;
        driftCounter_ = 0;
        currentJitter_ = 0.0f;
        currentDriveMod_ = baseDrive_;
    }

    // =========================================================================
    // Base Waveshaper Configuration (FR-005 to FR-008b)
    // =========================================================================

    /// @brief Set the underlying waveshape curve type. (FR-005)
    ///
    /// @param type Waveshape type (Tanh, Atan, Cubic, etc.)
    ///
    /// @note All 9 WaveshapeType values are supported (FR-006)
    /// @note Default is WaveshapeType::Tanh (FR-007)
    void setBaseType(WaveshapeType type) noexcept {
        waveshaper_.setType(type);
    }

    /// @brief Set the base drive amount. (FR-008a)
    ///
    /// Drive controls saturation intensity before stochastic modulation.
    ///
    /// @param drive Drive amount (negative values treated as positive)
    ///
    /// @note Default is 1.0 (FR-008b)
    /// @note Effective drive = baseDrive * (1 + coeffNoise * random * 0.5)
    void setDrive(float drive) noexcept {
        baseDrive_ = std::abs(drive);
        waveshaper_.setDrive(baseDrive_);
    }

    /// @brief Get the current base waveshape type.
    [[nodiscard]] WaveshapeType getBaseType() const noexcept {
        return waveshaper_.getType();
    }

    /// @brief Get the current base drive amount.
    [[nodiscard]] float getDrive() const noexcept {
        return baseDrive_;
    }

    // =========================================================================
    // Jitter Parameters (FR-009 to FR-014)
    // =========================================================================

    /// @brief Set the jitter amount. (FR-009)
    ///
    /// Controls the intensity of random offset applied to input before shaping.
    ///
    /// @param amount Jitter amount, clamped to [0.0, 1.0]
    ///               - 0.0 = no random offset (FR-010)
    ///               - 1.0 = max offset of +/- 0.5 (FR-011)
    void setJitterAmount(float amount) noexcept {
        jitterAmount_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// @brief Set the jitter rate. (FR-012)
    ///
    /// Controls the smoothing filter applied to raw random values.
    /// Lower rate = smoother, slower variation.
    ///
    /// @param hz Rate in Hz, clamped to [0.01, sampleRate/2]
    ///
    /// @note Default is 10.0 Hz (FR-014)
    /// @note Affects both jitter and coefficient noise smoothing (FR-013)
    void setJitterRate(float hz) noexcept {
        // Clamp to valid range (FR-012)
        const float maxRate = static_cast<float>(sampleRate_ * 0.5);
        jitterRate_ = std::clamp(hz, kMinJitterRate, maxRate);

        // Reconfigure smoothers and update rate if prepared
        if (prepared_) {
            reconfigureSmoothers();
            recalculateUpdateRate();
        }
    }

    /// @brief Get the current jitter amount.
    [[nodiscard]] float getJitterAmount() const noexcept {
        return jitterAmount_;
    }

    /// @brief Get the current jitter rate.
    [[nodiscard]] float getJitterRate() const noexcept {
        return jitterRate_;
    }

    // =========================================================================
    // Coefficient Noise Parameters (FR-015 to FR-018)
    // =========================================================================

    /// @brief Set the coefficient noise amount. (FR-015)
    ///
    /// Controls the intensity of random modulation applied to drive.
    ///
    /// @param amount Coefficient noise, clamped to [0.0, 1.0]
    ///               - 0.0 = no drive modulation (FR-016)
    ///               - 1.0 = +/- 50% drive modulation (FR-017)
    ///
    /// @note Uses independent smoother from jitter (FR-018)
    void setCoefficientNoise(float amount) noexcept {
        coefficientNoise_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// @brief Get the current coefficient noise amount.
    [[nodiscard]] float getCoefficientNoise() const noexcept {
        return coefficientNoise_;
    }

    // =========================================================================
    // Drift Parameter
    // =========================================================================

    /// @brief Set the drift amount.
    ///
    /// Controls a slow random walk applied to the jitter baseline,
    /// simulating analog component drift over time.
    ///
    /// @param amount Drift amount, clamped to [0.0, 1.0]
    ///               - 0.0 = no drift
    ///               - 1.0 = max drift offset of +/- 0.3
    void setDrift(float amount) noexcept {
        drift_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// @brief Get the current drift amount.
    [[nodiscard]] float getDrift() const noexcept {
        return drift_;
    }

    // =========================================================================
    // Correlation Mode
    // =========================================================================

    /// @brief Set the correlation mode between jitter and drive modulation.
    ///
    /// @param mode CorrelationMode enum value
    ///   - None:   Independent random streams (default)
    ///   - Sample: Both smoothers receive the same random input per sample
    ///   - Param:  Drive modulation depth scales with absolute jitter offset
    ///   - Both:   Sample + Param correlation combined
    void setCorrelationMode(CorrelationMode mode) noexcept {
        correlationMode_ = mode;
    }

    /// @brief Set the correlation mode from integer (for parameter mapping).
    void setCorrelationMode(int mode) noexcept {
        correlationMode_ = static_cast<CorrelationMode>(std::clamp(mode, 0, 3));
    }

    /// @brief Get the current correlation mode.
    [[nodiscard]] CorrelationMode getCorrelationMode() const noexcept {
        return correlationMode_;
    }

    // =========================================================================
    // Output Smoothing
    // =========================================================================

    /// @brief Set the output smoothing amount.
    ///
    /// Applies a one-pole lowpass to the output to soften stochastic artifacts.
    ///
    /// @param amount Smoothing amount, clamped to [0.0, 1.0]
    ///               - 0.0 = no output smoothing (bypass)
    ///               - 1.0 = maximum smoothing (~20ms time constant)
    void setOutputSmooth(float amount) noexcept {
        outputSmoothAmount_ = std::clamp(amount, 0.0f, 1.0f);
        if (prepared_) {
            reconfigureOutputSmoother();
        }
    }

    /// @brief Get the current output smoothing amount.
    [[nodiscard]] float getOutputSmooth() const noexcept {
        return outputSmoothAmount_;
    }

    // =========================================================================
    // Noise Color
    // =========================================================================

    /// @brief Set the noise distribution color for modulation streams.
    ///
    /// Controls the spectral character of random values feeding the jitter
    /// and drive modulation smoothers. Different colors produce fundamentally
    /// different modulation characters:
    /// - White: uniform variation at all rates (default, classic behavior)
    /// - Pink: 1/f noise, most organic and analog-like
    /// - Brown: very slow drift, brownian wander
    /// - Blue: brighter, more responsive variation
    /// - Violet: fast, twitchy, aggressive digital character
    ///
    /// @param color StochasticNoiseColor value
    void setNoiseColor(StochasticNoiseColor color) noexcept {
        noiseColor_ = color;
    }

    /// @brief Set noise color from integer (for parameter mapping).
    void setNoiseColor(int color) noexcept {
        noiseColor_ = static_cast<StochasticNoiseColor>(std::clamp(color, 0, 4));
    }

    /// @brief Get the current noise color.
    [[nodiscard]] StochasticNoiseColor getNoiseColor() const noexcept {
        return noiseColor_;
    }

    // =========================================================================
    // Reproducibility (FR-019 to FR-021)
    // =========================================================================

    /// @brief Set the RNG seed for deterministic sequence. (FR-019)
    ///
    /// Same seed with same parameters produces identical output. (FR-020)
    ///
    /// @param seed Seed value (0 is replaced with default per FR-021)
    void setSeed(uint32_t seed) noexcept {
        seed_ = seed;
        // Note: RNG is re-seeded on prepare() or reset()
    }

    /// @brief Get the current seed.
    [[nodiscard]] uint32_t getSeed() const noexcept {
        return seed_;
    }

    // =========================================================================
    // Processing (FR-003, FR-004, FR-022 to FR-031)
    // =========================================================================

    /// @brief Process a single sample. (FR-003)
    ///
    /// Applies stochastic waveshaping:
    /// - jitterOffset = jitterAmount * smoothedRandom * 0.5 (FR-022)
    /// - effectiveDrive = baseDrive * (1 + coeffNoise * smoothedRandom2 * 0.5) (FR-023)
    /// - output = waveshaper.process(input + jitterOffset, effectiveDrive)
    ///
    /// @param x Input sample
    /// @return Stochastically waveshaped output sample
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-026)
    /// @note NaN input treated as 0.0 (FR-029)
    /// @note Infinity input clamped to [-1, 1] (FR-030)
    /// @note When jitterAmount=0 AND coeffNoise=0, equals standard Waveshaper (FR-024)
    [[nodiscard]] float process(float x) noexcept {
        // If not prepared, return input unchanged (safe fallback)
        if (!prepared_) {
            return x;
        }

        // Sanitize input (FR-029, FR-030)
        x = sanitizeInput(x);

        // Sample-and-hold: generate new random targets at jitter rate,
        // not every sample. The smoothers interpolate between targets.
        if (++jitterCounter_ >= samplesPerUpdate_) {
            jitterCounter_ = 0;

            const float randJitter = jitterColorFilter_.process(rng_.nextFloat(), noiseColor_);
            jitterSmoother_.setTarget(randJitter);

            const bool sampleCorr = (correlationMode_ == CorrelationMode::Sample ||
                                     correlationMode_ == CorrelationMode::Both);
            const float randDrive = sampleCorr ? randJitter
                : driveColorFilter_.process(rng_.nextFloat(), noiseColor_);
            driveSmoother_.setTarget(randDrive);
        }

        const float smoothedJitter = jitterSmoother_.process();
        const float smoothedDriveMod = driveSmoother_.process();

        // Drift: update target at a much slower rate (~0.2 Hz)
        if (++driftCounter_ >= samplesPerDriftUpdate_) {
            driftCounter_ = 0;
            driftSmoother_.setTarget(rng_.nextFloat());
        }
        const float driftOffset = drift_ * (driftSmoother_.process()) * kDriftMaxOffset;

        // FR-022: jitterOffset = jitterAmount * smoothedRandom * 0.5 + drift
        const float jitterOffset = jitterAmount_ * smoothedJitter * kMaxJitterOffset + driftOffset;

        // FR-023: effectiveDrive = baseDrive * (1.0 + coeffNoise * smoothedRandom * 0.5)
        // With Param correlation: drive mod depth scales with absolute jitter offset
        // Baseline of 0.25 ensures Coef is never fully killed by Param mode
        const bool paramCorr = (correlationMode_ == CorrelationMode::Param ||
                                correlationMode_ == CorrelationMode::Both);
        const float corrScale = paramCorr
            ? 0.25f + std::abs(jitterOffset) * 3.0f  // [0.25, ~2.0] range
            : 1.0f;
        const float effectiveDrive = baseDrive_ * (1.0f + coefficientNoise_ * smoothedDriveMod
                                                   * kDriveModulationRange * corrScale);

        // Store for diagnostics (FR-035, FR-036)
        currentJitter_ = jitterOffset;
        currentDriveMod_ = effectiveDrive;

        // Apply waveshaping with modulated parameters
        waveshaper_.setDrive(effectiveDrive);
        float out = waveshaper_.process(x + jitterOffset);

        // Output smoothing (when amount > 0)
        if (outputSmoothAmount_ > 0.0f) {
            outputSmoother_.setTarget(out);
            out = outputSmoother_.process();
        }

        return out;
    }

    /// @brief Process a block of samples in-place. (FR-004)
    ///
    /// Equivalent to calling process() for each sample sequentially.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-026, FR-027)
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Diagnostics (FR-035 to FR-037)
    // =========================================================================

    /// @brief Get the current smoothed jitter offset value. (FR-035)
    ///
    /// Returns the jitter offset from the most recent process() call.
    /// Range: [-0.5, 0.5] when jitterAmount=1.0.
    ///
    /// @return Current jitter offset
    ///
    /// @note Safe to call from any thread (FR-037)
    /// @note For inspection only - do not call during audio processing (FR-037)
    [[nodiscard]] float getCurrentJitter() const noexcept {
        return currentJitter_;
    }

    /// @brief Get the current effective drive value. (FR-036)
    ///
    /// Returns the effective drive after coefficient noise modulation
    /// from the most recent process() call.
    ///
    /// @return Current effective drive
    ///
    /// @note Safe to call from any thread (FR-037)
    /// @note For inspection only - do not call during audio processing (FR-037)
    [[nodiscard]] float getCurrentDriveModulation() const noexcept {
        return currentDriveMod_;
    }

    /// @brief Check if processor has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Sanitize input for NaN/Inf (FR-029, FR-030)
    [[nodiscard]] float sanitizeInput(float x) const noexcept {
        // FR-029: NaN input treated as 0.0
        if (detail::isNaN(x)) {
            return 0.0f;
        }
        // FR-030: Infinity input clamped to [-1, 1]
        if (detail::isInf(x)) {
            return x > 0.0f ? 1.0f : -1.0f;
        }
        return x;
    }

    /// @brief Calculate smoothing time from jitter rate (Research R1)
    ///
    /// Converts jitter rate (Hz) to OnePoleSmoother time constant.
    /// Formula: smoothTimeMs = 800 / jitterRate, clamped to [0.1, 1000]
    [[nodiscard]] float calculateSmoothingTime(float rateHz) const noexcept {
        // From research.md R1: smoothTimeMs = 800 / jitterRate
        // Clamped to smoother valid range
        return std::clamp(800.0f / rateHz, kMinSmoothingTimeMs, kMaxSmoothingTimeMs);
    }

    /// @brief Reconfigure smoothers with current rate
    void reconfigureSmoothers() noexcept {
        const float smoothTimeMs = calculateSmoothingTime(jitterRate_);
        const float sampleRateF = static_cast<float>(sampleRate_);

        jitterSmoother_.configure(smoothTimeMs, sampleRateF);
        driveSmoother_.configure(smoothTimeMs, sampleRateF);
    }

    /// @brief Recalculate sample-and-hold update interval from jitter rate
    void recalculateUpdateRate() noexcept {
        // Generate new random target at jitter rate
        // Minimum 1 sample (at Nyquist rate)
        samplesPerUpdate_ = std::max(1u,
            static_cast<uint32_t>(sampleRate_ / static_cast<double>(jitterRate_)));
    }

    /// @brief Reconfigure output smoother with current amount
    void reconfigureOutputSmoother() noexcept {
        // Map amount [0,1] to smooth time [0.05, 20] ms (exponential)
        const float timeMs = kMinSmoothTimeMs +
            outputSmoothAmount_ * outputSmoothAmount_ * (kMaxOutputSmoothMs - kMinSmoothTimeMs);
        outputSmoother_.configure(timeMs, static_cast<float>(sampleRate_));
    }

    // =========================================================================
    // Composed Primitives (FR-032 to FR-034)
    // =========================================================================

    Waveshaper waveshaper_;           ///< Delegated waveshaping (FR-032)
    Xorshift32 rng_{kDefaultSeed};    ///< Random number generator (FR-033)
    OnePoleSmoother jitterSmoother_;  ///< Smooths jitter offset (FR-034)
    OnePoleSmoother driveSmoother_;   ///< Smooths drive modulation (FR-018)
    OnePoleSmoother driftSmoother_;   ///< Slow drift random walk
    OnePoleSmoother outputSmoother_;  ///< Output smoothing filter
    NoiseColorFilter jitterColorFilter_;  ///< Noise color shaping for jitter stream
    NoiseColorFilter driveColorFilter_;   ///< Noise color shaping for drive stream

    // =========================================================================
    // Configuration
    // =========================================================================

    float jitterAmount_ = 0.0f;                 ///< [0.0, 1.0]
    float jitterRate_ = kDefaultJitterRate;     ///< [0.01, sampleRate/2] Hz
    float coefficientNoise_ = 0.0f;             ///< [0.0, 1.0]
    float drift_ = 0.0f;                        ///< [0.0, 1.0] drift amount
    float outputSmoothAmount_ = 0.0f;           ///< [0.0, 1.0] output smoothing
    CorrelationMode correlationMode_ = CorrelationMode::None;
    StochasticNoiseColor noiseColor_ = StochasticNoiseColor::White;
    float baseDrive_ = kDefaultDrive;           ///< Base drive before modulation
    uint32_t seed_ = kDefaultSeed;              ///< RNG seed
    double sampleRate_ = 44100.0;               ///< Sample rate
    bool prepared_ = false;                     ///< Initialization flag

    // Sample-and-hold counters for rate-controlled random updates
    uint32_t samplesPerUpdate_ = 4410;          ///< Samples between new random targets
    uint32_t samplesPerDriftUpdate_ = 220500;   ///< Samples between drift updates
    uint32_t jitterCounter_ = 0;                ///< Current jitter update counter
    uint32_t driftCounter_ = 0;                 ///< Current drift update counter

    // =========================================================================
    // Diagnostic State (FR-035, FR-036)
    // =========================================================================

    float currentJitter_ = 0.0f;     ///< Last computed jitter offset
    float currentDriveMod_ = kDefaultDrive;   ///< Last computed effective drive
};

} // namespace DSP
} // namespace Krate
