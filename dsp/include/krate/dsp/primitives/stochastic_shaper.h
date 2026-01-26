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
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/waveshaper.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

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

        // Initialize smoother state with random values
        jitterSmoother_.snapTo(rng_.nextFloat());
        driveSmoother_.snapTo(rng_.nextFloat());

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

        // Reset smoothers
        jitterSmoother_.reset();
        driveSmoother_.reset();

        // Initialize smoother state with fresh random values
        jitterSmoother_.snapTo(rng_.nextFloat());
        driveSmoother_.snapTo(rng_.nextFloat());

        // Reset diagnostic state
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

        // Reconfigure smoothers if prepared
        if (prepared_) {
            reconfigureSmoothers();
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

        // Generate smoothed random values (FR-025, FR-034)
        // Update jitter smoother target with new random value
        jitterSmoother_.setTarget(rng_.nextFloat());
        const float smoothedJitter = jitterSmoother_.process();

        // Update drive smoother target with new random value (FR-018: independent smoother)
        driveSmoother_.setTarget(rng_.nextFloat());
        const float smoothedDriveMod = driveSmoother_.process();

        // FR-022: jitterOffset = jitterAmount * smoothedRandom * 0.5
        const float jitterOffset = jitterAmount_ * smoothedJitter * kMaxJitterOffset;

        // FR-023: effectiveDrive = baseDrive * (1.0 + coeffNoise * smoothedRandom * 0.5)
        const float effectiveDrive = baseDrive_ * (1.0f + coefficientNoise_ * smoothedDriveMod * kDriveModulationRange);

        // Store for diagnostics (FR-035, FR-036)
        currentJitter_ = jitterOffset;
        currentDriveMod_ = effectiveDrive;

        // Apply waveshaping with modulated parameters (FR-025a workaround)
        waveshaper_.setDrive(effectiveDrive);
        return waveshaper_.process(x + jitterOffset);
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

    // =========================================================================
    // Composed Primitives (FR-032 to FR-034)
    // =========================================================================

    Waveshaper waveshaper_;           ///< Delegated waveshaping (FR-032)
    Xorshift32 rng_{kDefaultSeed};    ///< Random number generator (FR-033)
    OnePoleSmoother jitterSmoother_;  ///< Smooths jitter offset (FR-034)
    OnePoleSmoother driveSmoother_;   ///< Smooths drive modulation (FR-018)

    // =========================================================================
    // Configuration
    // =========================================================================

    float jitterAmount_ = 0.0f;                 ///< [0.0, 1.0]
    float jitterRate_ = kDefaultJitterRate;     ///< [0.01, sampleRate/2] Hz
    float coefficientNoise_ = 0.0f;             ///< [0.0, 1.0]
    float baseDrive_ = kDefaultDrive;           ///< Base drive before modulation
    uint32_t seed_ = kDefaultSeed;              ///< RNG seed
    double sampleRate_ = 44100.0;               ///< Sample rate
    bool prepared_ = false;                     ///< Initialization flag

    // =========================================================================
    // Diagnostic State (FR-035, FR-036)
    // =========================================================================

    float currentJitter_ = 0.0f;     ///< Last computed jitter offset
    float currentDriveMod_ = kDefaultDrive;   ///< Last computed effective drive
};

} // namespace DSP
} // namespace Krate
