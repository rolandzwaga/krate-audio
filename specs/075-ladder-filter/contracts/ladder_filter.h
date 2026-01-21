// ==============================================================================
// API Contract: Moog Ladder Filter (LadderFilter)
// ==============================================================================
// This is the PUBLIC API CONTRACT for the LadderFilter primitive.
// Implementation must match this interface exactly.
//
// Spec: specs/075-ladder-filter/spec.md
// Plan: specs/075-ladder-filter/plan.md
// Target: dsp/include/krate/dsp/primitives/ladder_filter.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all processing methods noexcept)
// - Principle III: Modern C++ (enum class, [[nodiscard]], constexpr)
// - Principle IX: Layer 1 primitive
// - Principle XII: Test-First Development
// - Principle XIV: ODR Prevention (unique class name verified)
// ==============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Dependencies (Layer 0 and Layer 1 only)
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/primitives/smoother.h>

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

    /// Maximum resonance value (self-oscillation occurs around 3.9)
    static constexpr float kMaxResonance = 4.0f;

    /// Minimum drive in dB (unity gain)
    static constexpr float kMinDriveDb = 0.0f;

    /// Maximum drive in dB
    static constexpr float kMaxDriveDb = 24.0f;

    /// Minimum slope (1 pole = 6 dB/oct)
    static constexpr int kMinSlope = 1;

    /// Maximum slope (4 poles = 24 dB/oct)
    static constexpr int kMaxSlope = 4;

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
    void prepare(double sampleRate, int maxBlockSize) noexcept;

    /// @brief Reset filter state
    ///
    /// Clears all filter state variables while preserving configuration.
    /// Use when starting a new audio stream or after silence.
    ///
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set processing model
    ///
    /// @param model LadderModel::Linear for CPU-efficient processing,
    ///              LadderModel::Nonlinear for analog-like saturation
    ///
    /// @note Safe to call during processing (click-free transition)
    void setModel(LadderModel model) noexcept;

    /// @brief Set oversampling factor for nonlinear model
    ///
    /// Higher factors reduce aliasing from nonlinear processing at the
    /// cost of increased CPU usage. Has no effect on Linear model.
    ///
    /// @param factor 1 (no oversampling), 2, or 4. Value 3 rounds to 4.
    ///
    /// @note Affects latency. Call getLatency() after changing.
    void setOversamplingFactor(int factor) noexcept;

    /// @brief Enable or disable resonance gain compensation
    ///
    /// When enabled, applies gain reduction as resonance increases to
    /// maintain consistent output level (within ~3dB).
    ///
    /// Formula: compensation = 1.0 / (1.0 + resonance * 0.25)
    ///
    /// @param enabled true to enable compensation
    void setResonanceCompensation(bool enabled) noexcept;

    /// @brief Set filter slope (number of poles)
    ///
    /// @param poles 1 (6dB/oct), 2 (12dB/oct), 3 (18dB/oct), or 4 (24dB/oct)
    ///
    /// @note Clamped to [1, 4]
    void setSlope(int poles) noexcept;

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set cutoff frequency
    ///
    /// Sets target cutoff with internal smoothing (~5ms transition time).
    ///
    /// @param hz Cutoff frequency in Hz (clamped to [20, sampleRate * 0.45])
    void setCutoff(float hz) noexcept;

    /// @brief Set resonance amount
    ///
    /// Sets target resonance with internal smoothing.
    /// Self-oscillation occurs at approximately 3.9.
    ///
    /// @param amount Resonance value (clamped to [0, 4])
    void setResonance(float amount) noexcept;

    /// @brief Set input drive gain
    ///
    /// Applies gain before filtering. In Nonlinear mode, this increases
    /// saturation and harmonic content.
    ///
    /// @param db Drive in decibels (clamped to [0, 24])
    void setDrive(float db) noexcept;

    // =========================================================================
    // Getters
    // =========================================================================

    /// @brief Get current processing model
    [[nodiscard]] LadderModel getModel() const noexcept;

    /// @brief Get target cutoff frequency
    [[nodiscard]] float getCutoff() const noexcept;

    /// @brief Get target resonance
    [[nodiscard]] float getResonance() const noexcept;

    /// @brief Get drive in dB
    [[nodiscard]] float getDrive() const noexcept;

    /// @brief Get current slope (number of poles)
    [[nodiscard]] int getSlope() const noexcept;

    /// @brief Get current oversampling factor
    [[nodiscard]] int getOversamplingFactor() const noexcept;

    /// @brief Check if resonance compensation is enabled
    [[nodiscard]] bool isResonanceCompensationEnabled() const noexcept;

    /// @brief Check if filter is prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get processing latency in samples
    ///
    /// Returns latency introduced by oversampling filters.
    /// Zero for Linear model or 1x oversampling.
    ///
    /// @return Latency in samples at base sample rate
    [[nodiscard]] int getLatency() const noexcept;

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
    [[nodiscard]] float process(float input) noexcept;

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
    void processBlock(float* buffer, size_t numSamples) noexcept;

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
    void updateOversampledRate() noexcept;

    /// Calculate frequency coefficient g
    [[nodiscard]] float calculateG(float cutoff, double rate) noexcept;

    /// Linear model processing (Stilson/Smith)
    [[nodiscard]] float processLinear(float input, float g, float k) noexcept;

    /// Nonlinear model processing (Huovilainen)
    [[nodiscard]] float processNonlinear(float input, float g, float k) noexcept;

    /// Core nonlinear processing (called at oversampled rate)
    [[nodiscard]] float processNonlinearCore(float input) noexcept;

    /// Select output based on slope setting
    [[nodiscard]] float selectOutput() const noexcept;

    /// Apply resonance gain compensation
    [[nodiscard]] float applyCompensation(float output, float k) noexcept;
};

} // namespace DSP
} // namespace Krate
