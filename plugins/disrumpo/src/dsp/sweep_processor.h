// ==============================================================================
// SweepProcessor - Core Sweep DSP
// ==============================================================================
// Calculates per-band intensity multipliers based on sweep parameters.
// Supports Gaussian (Smooth) and linear (Sharp) falloff modes.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 3 (composes Layer 1/2 primitives)
// - Principle XII: Test-First Development
//
// Reference: specs/007-sweep-system/spec.md (FR-001 to FR-022)
// Reference: specs/007-sweep-system/data-model.md (SweepProcessor entity)
// ==============================================================================

#pragma once

#include "sweep_types.h"
#include "sweep_morph_link.h"
#include "custom_curve.h"
#include "band_state.h"  // For kMaxBands

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/sweep_position_buffer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Disrumpo {

// kMaxBands is defined in band_state.h

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
    SweepProcessor() noexcept = default;

    /// @brief Prepare processor for given sample rate.
    ///
    /// Allocates internal state and configures smoothers.
    /// Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size (for future block processing)
    void prepare(double sampleRate, int maxBlockSize = 512) noexcept {
        (void)maxBlockSize;  // Reserved for future use
        sampleRate_ = sampleRate;
        frequencySmoother_.configure(smoothingTimeMs_, static_cast<float>(sampleRate_));
        frequencySmoother_.snapTo(targetFreqHz_);
        prepared_ = true;
    }

    /// @brief Reset all internal state.
    ///
    /// Clears smoothers and resets to initial values.
    /// Call when starting new playback or after discontinuity.
    void reset() noexcept {
        frequencySmoother_.snapTo(targetFreqHz_);
    }

    // =========================================================================
    // Parameter Setters (FR-002 to FR-007)
    // =========================================================================

    /// @brief Enable or disable sweep processing.
    /// @param enabled true to enable sweep, false to bypass
    void setEnabled(bool enabled) noexcept {
        enabled_ = enabled;
    }

    /// @brief Set sweep center frequency.
    ///
    /// Changes are smoothed per FR-007a to prevent zipper noise.
    ///
    /// @param hz Center frequency in Hz [20, 20000]
    void setCenterFrequency(float hz) noexcept {
        targetFreqHz_ = std::clamp(hz, kMinSweepFreqHz, kMaxSweepFreqHz);
        frequencySmoother_.setTarget(targetFreqHz_);
    }

    /// @brief Set sweep width.
    /// @param octaves Width in octaves [0.5, 4.0]
    void setWidth(float octaves) noexcept {
        widthOctaves_ = std::clamp(octaves, kMinSweepWidth, kMaxSweepWidth);
    }

    /// @brief Set sweep intensity.
    ///
    /// Per FR-010: Uses multiplicative scaling (50% = half peak, 200% = double).
    ///
    /// @param value Intensity [0.0, 2.0] where 1.0 = 100%
    void setIntensity(float value) noexcept {
        intensity_ = std::clamp(value, 0.0f, kMaxIntensity);
    }

    /// @brief Set falloff mode.
    /// @param mode Sharp (linear) or Smooth (Gaussian)
    void setFalloffMode(SweepFalloff mode) noexcept {
        falloffMode_ = mode;
    }

    /// @brief Set sweep-morph linking mode.
    /// @param mode Morph link curve type
    void setMorphLinkMode(MorphLinkMode mode) noexcept {
        morphLinkMode_ = mode;
    }

    /// @brief Set custom curve for Custom morph link mode.
    /// @param curve Pointer to custom curve (ownership not transferred)
    void setCustomCurve(const CustomCurve* curve) noexcept {
        customCurve_ = curve;
    }

    /// @brief Set frequency smoothing time.
    ///
    /// Per FR-007a: Range 10-50ms recommended.
    ///
    /// @param ms Smoothing time in milliseconds
    void setSmoothingTime(float ms) noexcept {
        smoothingTimeMs_ = std::clamp(ms, 1.0f, 100.0f);
        if (prepared_) {
            frequencySmoother_.configure(smoothingTimeMs_, static_cast<float>(sampleRate_));
        }
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Check if sweep is enabled.
    [[nodiscard]] bool isEnabled() const noexcept {
        return enabled_;
    }

    /// @brief Get target center frequency (before smoothing).
    [[nodiscard]] float getTargetFrequency() const noexcept {
        return targetFreqHz_;
    }

    /// @brief Get current smoothed center frequency.
    [[nodiscard]] float getSmoothedFrequency() const noexcept {
        return frequencySmoother_.getCurrentValue();
    }

    /// @brief Get sweep width in octaves.
    [[nodiscard]] float getWidth() const noexcept {
        return widthOctaves_;
    }

    /// @brief Get intensity value.
    [[nodiscard]] float getIntensity() const noexcept {
        return intensity_;
    }

    /// @brief Get falloff mode.
    [[nodiscard]] SweepFalloff getFalloffMode() const noexcept {
        return falloffMode_;
    }

    /// @brief Get morph link mode.
    [[nodiscard]] MorphLinkMode getMorphLinkMode() const noexcept {
        return morphLinkMode_;
    }

    // =========================================================================
    // Processing (FR-007, FR-008, FR-009)
    // =========================================================================

    /// @brief Process one sample worth of smoothing.
    ///
    /// Advances the frequency smoother. Call once per sample or once per
    /// block with the number of samples.
    void process() noexcept {
        (void)frequencySmoother_.process();
    }

    /// @brief Process a block of samples.
    /// @param numSamples Number of samples in the block
    void processBlock(int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            (void)frequencySmoother_.process();
        }
    }

    /// @brief Calculate intensity multiplier for a given band center frequency.
    ///
    /// Uses Gaussian distribution for Smooth mode (FR-008):
    ///   intensity = intensityParam * exp(-0.5 * (distanceOctaves / sigma)^2)
    ///
    /// Uses linear falloff for Sharp mode (FR-006a):
    ///   intensity = intensityParam * max(0, 1 - abs(distanceOctaves) / (width/2))
    ///
    /// @param bandCenterHz Band center frequency in Hz
    /// @return Intensity multiplier [0.0, 2.0] (0.0 if disabled)
    [[nodiscard]] float calculateBandIntensity(float bandCenterHz) const noexcept {
        if (!enabled_) {
            return 0.0f;
        }

        float sweepCenterHz = frequencySmoother_.getCurrentValue();

        if (falloffMode_ == SweepFalloff::Smooth) {
            return calculateGaussianIntensity(bandCenterHz, sweepCenterHz, widthOctaves_, intensity_);
        } else {
            return calculateLinearFalloff(bandCenterHz, sweepCenterHz, widthOctaves_, intensity_);
        }
    }

    /// @brief Calculate intensities for all bands at once.
    ///
    /// More efficient than calling calculateBandIntensity() repeatedly.
    ///
    /// @param bandCenters Array of band center frequencies in Hz
    /// @param numBands Number of bands to process
    /// @param outIntensities Output array for intensity values
    void calculateAllBandIntensities(const float* bandCenters, int numBands,
                                      float* outIntensities) const noexcept {
        if (!enabled_) {
            for (int i = 0; i < numBands; ++i) {
                outIntensities[i] = 0.0f;
            }
            return;
        }

        float sweepCenterHz = frequencySmoother_.getCurrentValue();

        if (falloffMode_ == SweepFalloff::Smooth) {
            for (int i = 0; i < numBands; ++i) {
                outIntensities[i] = calculateGaussianIntensity(
                    bandCenters[i], sweepCenterHz, widthOctaves_, intensity_);
            }
        } else {
            for (int i = 0; i < numBands; ++i) {
                outIntensities[i] = calculateLinearFalloff(
                    bandCenters[i], sweepCenterHz, widthOctaves_, intensity_);
            }
        }
    }

    // =========================================================================
    // Morph Linking (FR-014 to FR-022)
    // =========================================================================

    /// @brief Get linked morph position based on current sweep frequency.
    ///
    /// Converts normalized sweep frequency through the selected morph link
    /// curve to produce a morph position.
    ///
    /// @return Morph position [0.0, 1.0]
    [[nodiscard]] float getMorphPosition() const noexcept {
        if (!enabled_) {
            return 0.5f;  // Return center when disabled
        }

        float normalizedFreq = normalizedSweepPosition();

        if (morphLinkMode_ == MorphLinkMode::Custom && customCurve_ != nullptr) {
            return customCurve_->evaluate(normalizedFreq);
        }

        return applyMorphLinkCurve(morphLinkMode_, normalizedFreq);
    }

    // =========================================================================
    // Audio-UI Synchronization (FR-046)
    // =========================================================================

    /// @brief Get position data for UI synchronization.
    ///
    /// Packages current sweep state for communication to UI thread.
    ///
    /// @param samplePosition Current sample position for timing sync
    /// @return Sweep position data structure
    [[nodiscard]] Krate::DSP::SweepPositionData getPositionData(uint64_t samplePosition) const noexcept {
        Krate::DSP::SweepPositionData data;
        data.centerFreqHz = frequencySmoother_.getCurrentValue();
        data.widthOctaves = widthOctaves_;
        data.intensity = intensity_;
        data.samplePosition = samplePosition;
        data.enabled = enabled_;
        data.falloff = static_cast<Krate::DSP::SweepFalloffType>(falloffMode_);
        return data;
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Calculate normalized sweep frequency position.
    /// @return Normalized position [0, 1] where 0 = 20Hz, 1 = 20kHz
    [[nodiscard]] float normalizedSweepPosition() const noexcept {
        return normalizeSweepFrequency(frequencySmoother_.getCurrentValue());
    }

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
