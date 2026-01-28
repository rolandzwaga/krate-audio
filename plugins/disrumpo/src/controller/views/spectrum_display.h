#pragma once

// ==============================================================================
// SpectrumDisplay Custom View
// ==============================================================================
// FR-013: Custom VSTGUI view for displaying frequency band regions
// Phase 3: Static colored regions only (no FFT until Week 13)
//
// Coordinate mapping uses logarithmic scale from 20Hz to 20kHz:
// - x = width * log2(freq/20) / log2(1000)
// - freq = 20 * 2^(x/width * log2(1000))
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/lib/ccolor.h"

#include <array>
#include <vector>

namespace Disrumpo {

/// @brief Listener interface for SpectrumDisplay events
class SpectrumDisplayListener {
public:
    virtual ~SpectrumDisplayListener() = default;

    /// Called when a crossover divider is dragged to a new position
    /// @param dividerIndex Index of the divider (0 to numBands-2)
    /// @param frequencyHz New frequency position in Hz
    virtual void onCrossoverChanged(int dividerIndex, float frequencyHz) = 0;

    /// Called when a band region is clicked
    /// @param bandIndex Index of the clicked band (0 to numBands-1)
    virtual void onBandSelected(int bandIndex) = 0;
};

/// @brief Custom VSTGUI view for displaying frequency band regions
/// @details Renders colored frequency band regions with draggable crossover dividers.
///          Uses logarithmic frequency scale (20Hz - 20kHz).
class SpectrumDisplay : public VSTGUI::CView {
public:
    explicit SpectrumDisplay(const VSTGUI::CRect& size);
    ~SpectrumDisplay() override = default;

    // ==========================================================================
    // Configuration API
    // ==========================================================================

    /// @brief Set the number of active frequency bands
    /// @param numBands Number of bands (1-8)
    void setNumBands(int numBands);

    /// @brief Get the number of active frequency bands
    int getNumBands() const { return numBands_; }

    /// @brief Set a crossover frequency
    /// @param index Crossover index (0 to numBands-2)
    /// @param freqHz Frequency in Hz
    void setCrossoverFrequency(int index, float freqHz);

    /// @brief Get a crossover frequency
    /// @param index Crossover index (0 to numBands-2)
    /// @return Frequency in Hz
    float getCrossoverFrequency(int index) const;

    /// @brief Set the listener for events
    void setListener(SpectrumDisplayListener* listener) { listener_ = listener; }

    // ==========================================================================
    // Coordinate Conversion
    // ==========================================================================

    /// @brief Convert frequency (Hz) to X coordinate (pixels from left edge)
    /// @param freq Frequency in Hz [20, 20000]
    /// @return X coordinate [0, width]
    float freqToX(float freq) const;

    /// @brief Convert X coordinate (pixels from left edge) to frequency (Hz)
    /// @param x X coordinate [0, width]
    /// @return Frequency in Hz [20, 20000]
    float xToFreq(float x) const;

    // ==========================================================================
    // CView Overrides
    // ==========================================================================

    void draw(VSTGUI::CDrawContext* context) override;

    // Mouse interaction (Phase 8 - Crossover dragging)
    // For now these are no-ops, full implementation in User Story 6
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;

    CLASS_METHODS(SpectrumDisplay, CView)

private:
    // ==========================================================================
    // Internal Helpers
    // ==========================================================================

    /// @brief Draw the frequency band regions
    void drawBandRegions(VSTGUI::CDrawContext* context);

    /// @brief Draw the crossover dividers
    void drawCrossoverDividers(VSTGUI::CDrawContext* context);

    /// @brief Draw the frequency scale labels
    void drawFrequencyScale(VSTGUI::CDrawContext* context);

    /// @brief Hit test for crossover dividers
    /// @param x X coordinate to test
    /// @return Divider index if hit, -1 otherwise
    int hitTestDivider(float x) const;

    /// @brief Get the band index at a given frequency
    /// @param freq Frequency in Hz
    /// @return Band index (0 to numBands-1)
    int getBandAtFrequency(float freq) const;

    // ==========================================================================
    // Constants
    // ==========================================================================

    static constexpr float kMinFreqHz = 20.0f;
    static constexpr float kMaxFreqHz = 20000.0f;
    static constexpr float kLogRatio = 9.9657842846620869f;  // log2(20000/20) = log2(1000)
    static constexpr int kMaxBands = 8;
    static constexpr float kDividerHitTolerance = 10.0f;  // Pixels
    static constexpr float kMinOctaveSpacing = 0.5f;  // Minimum spacing between dividers

    // ==========================================================================
    // State
    // ==========================================================================

    int numBands_ = 4;  // Default to 4 bands
    std::array<float, kMaxBands - 1> crossoverFreqs_;  // Up to 7 crossover frequencies
    SpectrumDisplayListener* listener_ = nullptr;

    // Drag state (for Phase 8)
    int draggingDivider_ = -1;

    // Band colors (from ui-mockups.md)
    static const std::array<VSTGUI::CColor, kMaxBands> kBandColors;
};

} // namespace Disrumpo
