// ==============================================================================
// API Contract: SweepProcessor
// ==============================================================================
// Core DSP class for calculating per-band intensity multipliers based on
// sweep parameters. Supports Gaussian (Smooth) and linear (Sharp) falloff modes.
//
// Layer: Plugin DSP (composes Layer 1 primitives)
//
// Reference: specs/007-sweep-system/spec.md
// ==============================================================================

#pragma once

#include "sweep_types.h"
#include "custom_curve.h"

#include <krate/dsp/primitives/smoother.h>

#include <array>
#include <cstdint>

namespace Disrumpo {

// Forward declarations
struct SweepPositionData;
enum class MorphLinkMode : uint8_t;

/// @brief Maximum number of frequency bands supported.
constexpr int kMaxBands = 8;

/// @brief Minimum sweep frequency in Hz.
constexpr float kMinSweepFreqHz = 20.0f;

/// @brief Maximum sweep frequency in Hz.
constexpr float kMaxSweepFreqHz = 20000.0f;

/// @brief Default sweep frequency in Hz.
constexpr float kDefaultSweepFreqHz = 1000.0f;

/// @brief Minimum sweep width in octaves.
constexpr float kMinSweepWidth = 0.5f;

/// @brief Maximum sweep width in octaves.
constexpr float kMaxSweepWidth = 4.0f;

/// @brief Default sweep width in octaves.
constexpr float kDefaultSweepWidth = 1.5f;

/// @brief Maximum intensity (200%).
constexpr float kMaxIntensity = 2.0f;

/// @brief Default intensity (50%).
constexpr float kDefaultIntensity = 0.5f;

/// @brief Default smoothing time in milliseconds.
constexpr float kDefaultSmoothingTimeMs = 20.0f;

/// @brief Core sweep processor for per-band intensity calculation.
///
/// Calculates intensity multipliers for each frequency band based on the
/// sweep center frequency, width, and falloff mode. Supports both Gaussian
/// (Smooth) and linear (Sharp) intensity distributions.
///
/// Thread Safety:
/// - prepare()/reset(): Call from non-audio thread only
/// - Parameter setters: Thread-safe via atomic or smoothed transition
/// - process()/calculateBandIntensity(): Audio thread only
///
/// @note Real-time safe: no allocations after prepare()
/// @note Per spec FR-001 through FR-022
class SweepProcessor {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    SweepProcessor() noexcept;

    /// @brief Prepare processor for given sample rate.
    ///
    /// Allocates internal state and configures smoothers.
    /// Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size (for future block processing)
    void prepare(double sampleRate, int maxBlockSize = 512) noexcept;

    /// @brief Reset all internal state.
    ///
    /// Clears smoothers and resets to initial values.
    /// Call when starting new playback or after discontinuity.
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-002 to FR-007)
    // =========================================================================

    /// @brief Enable or disable sweep processing.
    /// @param enabled true to enable sweep, false to bypass
    void setEnabled(bool enabled) noexcept;

    /// @brief Set sweep center frequency.
    ///
    /// Changes are smoothed per FR-007a to prevent zipper noise.
    ///
    /// @param hz Center frequency in Hz [20, 20000]
    void setCenterFrequency(float hz) noexcept;

    /// @brief Set sweep width.
    /// @param octaves Width in octaves [0.5, 4.0]
    void setWidth(float octaves) noexcept;

    /// @brief Set sweep intensity.
    ///
    /// Per FR-010: Uses multiplicative scaling (50% = half peak, 200% = double).
    ///
    /// @param value Intensity [0.0, 2.0] where 1.0 = 100%
    void setIntensity(float value) noexcept;

    /// @brief Set falloff mode.
    /// @param mode Sharp (linear) or Smooth (Gaussian)
    void setFalloffMode(SweepFalloff mode) noexcept;

    /// @brief Set sweep-morph linking mode.
    /// @param mode Morph link curve type
    void setMorphLinkMode(MorphLinkMode mode) noexcept;

    /// @brief Set custom curve for Custom morph link mode.
    /// @param curve Pointer to custom curve (ownership not transferred)
    void setCustomCurve(const CustomCurve* curve) noexcept;

    /// @brief Set frequency smoothing time.
    ///
    /// Per FR-007a: Range 10-50ms recommended.
    ///
    /// @param ms Smoothing time in milliseconds
    void setSmoothingTime(float ms) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Check if sweep is enabled.
    [[nodiscard]] bool isEnabled() const noexcept;

    /// @brief Get target center frequency (before smoothing).
    [[nodiscard]] float getTargetFrequency() const noexcept;

    /// @brief Get current smoothed center frequency.
    [[nodiscard]] float getSmoothedFrequency() const noexcept;

    /// @brief Get sweep width in octaves.
    [[nodiscard]] float getWidth() const noexcept;

    /// @brief Get intensity value.
    [[nodiscard]] float getIntensity() const noexcept;

    /// @brief Get falloff mode.
    [[nodiscard]] SweepFalloff getFalloffMode() const noexcept;

    /// @brief Get morph link mode.
    [[nodiscard]] MorphLinkMode getMorphLinkMode() const noexcept;

    // =========================================================================
    // Processing (FR-007, FR-008, FR-009)
    // =========================================================================

    /// @brief Process one sample worth of smoothing.
    ///
    /// Advances the frequency smoother. Call once per sample or once per
    /// block with the number of samples.
    void process() noexcept;

    /// @brief Process a block of samples.
    /// @param numSamples Number of samples in the block
    void processBlock(int numSamples) noexcept;

    /// @brief Calculate intensity multiplier for a given band center frequency.
    ///
    /// Uses Gaussian distribution for Smooth mode (FR-008):
    ///   intensity = intensityParam * exp(-0.5 * (distanceOctaves / sigma)^2)
    ///
    /// Uses linear falloff for Sharp mode (FR-006a):
    ///   intensity = intensityParam * max(0, 1 - abs(distanceOctaves) / (width/2))
    ///
    /// @param bandCenterHz Band center frequency in Hz
    /// @return Intensity multiplier [0.0, 2.0]
    [[nodiscard]] float calculateBandIntensity(float bandCenterHz) const noexcept;

    /// @brief Calculate intensities for all bands at once.
    ///
    /// More efficient than calling calculateBandIntensity() repeatedly.
    ///
    /// @param bandCenters Array of band center frequencies in Hz
    /// @param numBands Number of bands to process
    /// @param outIntensities Output array for intensity values
    void calculateAllBandIntensities(const float* bandCenters, int numBands,
                                      float* outIntensities) const noexcept;

    // =========================================================================
    // Morph Linking (FR-014 to FR-022)
    // =========================================================================

    /// @brief Get linked morph position based on current sweep frequency.
    ///
    /// Converts normalized sweep frequency through the selected morph link
    /// curve to produce a morph position.
    ///
    /// @return Morph position [0.0, 1.0]
    [[nodiscard]] float getMorphPosition() const noexcept;

    // =========================================================================
    // Audio-UI Synchronization (FR-046)
    // =========================================================================

    /// @brief Get position data for UI synchronization.
    ///
    /// Packages current sweep state for communication to UI thread.
    ///
    /// @param samplePosition Current sample position for timing sync
    /// @return Sweep position data structure
    [[nodiscard]] SweepPositionData getPositionData(uint64_t samplePosition) const noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Calculate normalized sweep frequency position.
    /// @return Normalized position [0, 1] where 0 = 20Hz, 1 = 20kHz
    [[nodiscard]] float normalizedSweepPosition() const noexcept;

    /// @brief Apply morph link curve to normalized frequency.
    /// @param normalizedFreq Normalized frequency [0, 1]
    /// @return Morph position [0, 1]
    [[nodiscard]] float applyMorphLinkCurve(float normalizedFreq) const noexcept;

    // =========================================================================
    // State
    // =========================================================================

    double sampleRate_ = 44100.0;
    bool enabled_ = false;
    bool prepared_ = false;

    // Sweep parameters
    float targetFreqHz_ = kDefaultSweepFreqHz;
    float widthOctaves_ = kDefaultSweepWidth;
    float intensity_ = kDefaultIntensity;
    SweepFalloff falloffMode_ = SweepFalloff::Smooth;
    MorphLinkMode morphLinkMode_ = MorphLinkMode::None;

    // Smoothing
    Krate::DSP::OnePoleSmoother frequencySmoother_;
    float smoothingTimeMs_ = kDefaultSmoothingTimeMs;

    // Custom curve (borrowed pointer, not owned)
    const CustomCurve* customCurve_ = nullptr;
};

} // namespace Disrumpo
