#pragma once

// ==============================================================================
// SweepIndicator - Visual overlay for sweep position and width
// ==============================================================================
// FR-040 to FR-045: Renders Gaussian/triangular curve showing sweep focus area
// FR-046 to FR-049: Audio-visual synchronization via SweepPositionBuffer
//
// Constitution Compliance:
// - Principle V: VSTGUI cross-platform (no native code)
// - Principle VI: Thread safety (reads from lock-free buffer)
//
// Reference: specs/007-sweep-system/spec.md
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"

#include <krate/dsp/primitives/sweep_position_buffer.h>
#include "dsp/sweep_types.h"

#include <cmath>
#include <array>

namespace Disrumpo {

/// @brief Overlay view for displaying sweep position and intensity distribution
/// @details Renders a Gaussian or triangular curve showing the sweep focus area.
///          Reads position data from SweepPositionBuffer for audio-visual sync.
class SweepIndicator : public VSTGUI::CView {
public:
    explicit SweepIndicator(const VSTGUI::CRect& size);
    ~SweepIndicator() override = default;

    // ==========================================================================
    // Configuration API
    // ==========================================================================

    /// @brief Set the sweep position buffer for audio-visual sync (FR-046)
    /// @param buffer Pointer to the lock-free position buffer
    void setPositionBuffer(Krate::DSP::SweepPositionBuffer* buffer) noexcept {
        positionBuffer_ = buffer;
    }

    /// @brief Enable or disable the sweep indicator (FR-011, FR-012)
    /// @param enabled true to show indicator, false to hide
    void setEnabled(bool enabled) noexcept {
        enabled_ = enabled;
        setDirty();
    }

    /// @brief Check if sweep indicator is enabled
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    /// @brief Set the falloff mode (affects curve shape)
    /// @param mode Sharp (triangular) or Smooth (Gaussian)
    void setFalloffMode(SweepFalloff mode) noexcept {
        falloffMode_ = mode;
        setDirty();
    }

    /// @brief Set the indicator color
    /// @param color Fill color (will be rendered with alpha)
    void setIndicatorColor(const VSTGUI::CColor& color) noexcept {
        indicatorColor_ = color;
        setDirty();
    }

    /// @brief Manually set position (for testing or when buffer not available)
    /// @param centerFreqHz Sweep center frequency in Hz
    /// @param widthOctaves Sweep width in octaves
    /// @param intensity Sweep intensity (0-2, where 1 = 100%)
    void setPosition(float centerFreqHz, float widthOctaves, float intensity) noexcept {
        centerFreq_ = centerFreqHz;
        widthOctaves_ = widthOctaves;
        intensity_ = intensity;
        setDirty();
    }

    /// @brief Update position from buffer (call from UI timer/idle)
    /// @return true if position was updated
    bool updateFromBuffer() noexcept;

    // ==========================================================================
    // Coordinate Conversion
    // ==========================================================================

    /// @brief Convert frequency (Hz) to X coordinate
    /// @param freq Frequency in Hz [20, 20000]
    /// @return X coordinate [0, width]
    [[nodiscard]] float freqToX(float freq) const noexcept;

    /// @brief Convert X coordinate to frequency (Hz)
    /// @param x X coordinate [0, width]
    /// @return Frequency in Hz [20, 20000]
    [[nodiscard]] float xToFreq(float x) const noexcept;

    // ==========================================================================
    // CView Overrides
    // ==========================================================================

    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS(SweepIndicator, CView)

private:
    // ==========================================================================
    // Rendering Helpers
    // ==========================================================================

    /// @brief Draw the Gaussian intensity curve (FR-041)
    void drawGaussianCurve(VSTGUI::CDrawContext* context);

    /// @brief Draw the triangular intensity curve (FR-042)
    void drawTriangularCurve(VSTGUI::CDrawContext* context);

    /// @brief Draw the center frequency line (FR-043)
    void drawCenterLine(VSTGUI::CDrawContext* context);

    /// @brief Calculate Gaussian intensity at a given distance from center
    /// @param distanceOctaves Distance from center in octaves
    /// @return Intensity value [0, 1]
    [[nodiscard]] float calculateGaussianIntensity(float distanceOctaves) const noexcept;

    /// @brief Calculate linear falloff intensity at a given distance from center
    /// @param distanceOctaves Distance from center in octaves
    /// @return Intensity value [0, 1]
    [[nodiscard]] float calculateLinearIntensity(float distanceOctaves) const noexcept;

    // ==========================================================================
    // Constants
    // ==========================================================================

    static constexpr float kMinFreqHz = 20.0f;
    static constexpr float kMaxFreqHz = 20000.0f;
    static constexpr float kLogRatio = 9.9657842846620869f;  // log2(20000/20)
    static constexpr int kCurveResolution = 100;  // Points in curve path
    static constexpr float kAlpha = 0.4f;  // Base transparency for curve fill

    // ==========================================================================
    // State
    // ==========================================================================

    Krate::DSP::SweepPositionBuffer* positionBuffer_ = nullptr;
    bool enabled_ = false;
    SweepFalloff falloffMode_ = SweepFalloff::Smooth;
    VSTGUI::CColor indicatorColor_{0x4E, 0xCD, 0xC4, 0xFF};  // accent-secondary

    // Current sweep position
    float centerFreq_ = 1000.0f;
    float widthOctaves_ = 2.0f;
    float intensity_ = 1.0f;

    // Interpolation state for smooth 60fps display (FR-047)
    float lastCenterFreq_ = 1000.0f;
    float lastWidthOctaves_ = 2.0f;
    static constexpr float kInterpolationFactor = 0.3f;  // Smoothing factor
};

} // namespace Disrumpo
