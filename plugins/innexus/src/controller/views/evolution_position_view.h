#pragma once

// ==============================================================================
// EvolutionPositionView - Evolution Position Display Custom View
// ==============================================================================
// FR-036: Horizontal track with playhead and ghost marker
// ==============================================================================

#include "controller/display_data.h"
#include "vstgui/lib/cview.h"

namespace Innexus {

class EvolutionPositionView : public VSTGUI::CView
{
public:
    explicit EvolutionPositionView(const VSTGUI::CRect& size);

    /// Called from timer callback with latest display data
    void updateData(const DisplayData& data, bool evolutionActive);

    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS_NOCOPY(EvolutionPositionView, CView)

    // Test accessors
    float getPosition() const { return position_; }
    float getManualPosition() const { return manualPosition_; }
    bool getShowGhost() const { return showGhost_; }

private:
    float position_ = 0.0f;
    float manualPosition_ = 0.0f;
    bool showGhost_ = false;
};

} // namespace Innexus
