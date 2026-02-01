#pragma once

// ==============================================================================
// SpectrumDisplay Custom View
// ==============================================================================
// FR-013: Custom VSTGUI view for displaying frequency band regions
// Renders colored frequency band regions with draggable crossover dividers,
// real-time FFT spectrum curves, peak hold lines, and dB scale markers.
//
// Coordinate mapping uses logarithmic scale from 20Hz to 20kHz:
// - x = width * log2(freq/20) / log2(1000)
// - freq = 20 * 2^(x/width * log2(1000))
// ==============================================================================

#include "spectrum_analyzer.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"

#include <krate/dsp/primitives/spectrum_fifo.h>

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
    ~SpectrumDisplay() override;

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

    /// @brief Set per-band sweep intensity values for overlay rendering (FR-050)
    /// @param intensities Array of intensity values [0, 2] per band (up to 8)
    /// @param numBands Number of bands with valid intensity data
    void setSweepBandIntensities(const std::array<float, 4>& intensities, int numBands);

    /// @brief Enable high contrast mode with specified colors (Spec 012 FR-025a)
    /// Increases border widths to 2px, uses solid fills instead of gradients,
    /// ensures >= 4.5:1 contrast ratio.
    /// @param borderColor Color for borders and dividers
    /// @param bgColor Background color
    /// @param accentColor Accent color for highlights
    void setHighContrastMode(bool enabled,
                             const VSTGUI::CColor& borderColor = VSTGUI::CColor(255, 255, 255),
                             const VSTGUI::CColor& bgColor = VSTGUI::CColor(0, 0, 0),
                             const VSTGUI::CColor& accentColor = VSTGUI::CColor(0x3A, 0x96, 0xDD));

    /// @brief Enable or disable sweep intensity overlay (FR-050)
    /// @param enabled true to show overlay
    void setSweepEnabled(bool enabled);

    // ==========================================================================
    // Spectrum Analyzer API
    // ==========================================================================

    /// @brief Set FIFO pointers for real-time spectrum analysis
    /// @param inputFIFO Pointer to input (pre-distortion) FIFO, or nullptr
    /// @param outputFIFO Pointer to output (post-distortion) FIFO, or nullptr
    void setSpectrumFIFOs(Krate::DSP::SpectrumFIFO<8192>* inputFIFO,
                          Krate::DSP::SpectrumFIFO<8192>* outputFIFO);

    /// @brief Start spectrum analysis with the given sample rate
    /// @param sampleRate Audio sample rate in Hz
    void startAnalysis(double sampleRate);

    /// @brief Stop spectrum analysis and release timer
    void stopAnalysis();

    /// @brief Toggle input spectrum visibility
    /// @param show true to show input spectrum
    void setShowInput(bool show) { showInput_ = show; invalid(); }

    /// @brief Toggle overlaid mode (input + output simultaneously)
    /// @param overlaid true for overlaid display
    void setOverlaidMode(bool overlaid) { overlaidMode_ = overlaid; invalid(); }

    /// @brief Check if spectrum analysis is active
    [[nodiscard]] bool isAnalysisActive() const { return analysisActive_; }

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

    CLASS_METHODS_NOCOPY(SpectrumDisplay, CView)

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
    static constexpr int kMaxBands = 4;
    static constexpr float kDividerHitTolerance = 10.0f;  // Pixels
    static constexpr float kMinOctaveSpacing = 0.5f;  // Minimum spacing between dividers

    // ==========================================================================
    // State
    // ==========================================================================

    int numBands_ = 4;  // Default to 4 bands
    std::array<float, kMaxBands - 1> crossoverFreqs_{};  // Up to 3 crossover frequencies
    SpectrumDisplayListener* listener_ = nullptr;

    // Drag state (for Phase 8)
    int draggingDivider_ = -1;

    // Band colors (from ui-mockups.md)
    static const std::array<VSTGUI::CColor, kMaxBands> kBandColors;

    // Sweep intensity overlay (FR-050)
    bool sweepEnabled_ = false;
    std::array<float, kMaxBands> sweepIntensities_{};

    /// @brief Draw sweep intensity overlay on band regions
    void drawSweepIntensityOverlay(VSTGUI::CDrawContext* context);

    // High contrast mode (Spec 012 FR-025a)
    bool highContrastEnabled_ = false;
    VSTGUI::CColor hcBorderColor_{255, 255, 255};
    VSTGUI::CColor hcBgColor_{0, 0, 0};
    VSTGUI::CColor hcAccentColor_{0x3A, 0x96, 0xDD};

    // ==========================================================================
    // Spectrum Analyzer State
    // ==========================================================================

    /// @brief Analyzers for input (pre-distortion) and output (post-distortion)
    SpectrumAnalyzer inputAnalyzer_;
    SpectrumAnalyzer outputAnalyzer_;

    /// @brief FIFO pointers (owned by Processor, nulled on disconnect)
    Krate::DSP::SpectrumFIFO<8192>* inputFIFO_ = nullptr;
    Krate::DSP::SpectrumFIFO<8192>* outputFIFO_ = nullptr;

    /// @brief Timer for ~30fps spectrum updates
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> analysisTimer_;

    /// @brief Display flags
    bool showInput_ = false;
    bool overlaidMode_ = false;
    bool analysisActive_ = false;

    // ==========================================================================
    // Spectrum Drawing Helpers
    // ==========================================================================

    /// @brief Draw filled spectrum curve for one analyzer, clipped per-band
    /// @param context Draw context
    /// @param analyzer The analyzer to render
    /// @param alphaScale Alpha multiplier (e.g., 0.2 for input, 0.5 for output)
    void drawSpectrumCurve(VSTGUI::CDrawContext* context,
                           const SpectrumAnalyzer& analyzer,
                           float alphaScale);

    /// @brief Draw peak hold line for one analyzer
    /// @param context Draw context
    /// @param analyzer The analyzer to render peaks from
    /// @param alpha Line alpha (0-255)
    void drawPeakHoldLine(VSTGUI::CDrawContext* context,
                          const SpectrumAnalyzer& analyzer,
                          uint8_t alpha);

    /// @brief Draw dB scale gridlines and labels
    void drawDbScale(VSTGUI::CDrawContext* context);

    /// @brief Convert dB value to Y coordinate within the view
    /// @param db dB value (minDb to maxDb)
    /// @return Y coordinate from view top
    float dbToY(float db) const;

    /// @brief Spectrum display dB range
    static constexpr float kMinDb = -96.0f;
    static constexpr float kMaxDb = 0.0f;
};

} // namespace Disrumpo
