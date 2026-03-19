#pragma once

// ==============================================================================
// ConfidenceIndicatorView - Multi-Voice Pitch Detection Display
// ==============================================================================
// FR-013: Confidence bar with color coding
// FR-014: Green/yellow/red color zones
// FR-015: Detected note name display
// Polyphonic extension: shows up to 8 detected voices + mode badge
// ==============================================================================

#include "controller/display_data.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/ccolor.h"

#include <string>

namespace Innexus {

class ConfidenceIndicatorView : public VSTGUI::CView
{
public:
    struct VoiceCache {
        float f0 = 0.0f;
        float confidence = 0.0f;
        float amplitude = 0.0f;
    };

    explicit ConfidenceIndicatorView(const VSTGUI::CRect& size);

    /// Called from timer callback with latest display data
    void updateData(const DisplayData& data);

    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS_NOCOPY(ConfidenceIndicatorView, CView)

    // Test accessors
    float getConfidence() const { return confidence_; }
    float getF0() const { return f0_; }
    int getNumVoices() const { return numVoices_; }
    bool getIsPolyphonic() const { return isPolyphonic_; }
    uint8_t getAnalysisMode() const { return analysisMode_; }
    const VoiceCache& getVoice(int index) const { return voices_[index]; }

    /// Color for the confidence bar (public for testing)
    static VSTGUI::CColor getConfidenceColor(float confidence);

    /// Convert frequency to note name (public for testing)
    static std::string freqToNoteName(float freq);

    /// Mode label for badge display (public for testing)
    static std::string modeLabel(uint8_t analysisMode, bool isPolyphonic);

private:
    float confidence_ = 0.0f;
    float f0_ = 0.0f;

    VoiceCache voices_[8]{};
    int numVoices_ = 0;
    bool isPolyphonic_ = false;
    uint8_t analysisMode_ = 2;
};

} // namespace Innexus
