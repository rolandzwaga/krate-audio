#pragma once

// ==============================================================================
// KitMetersView -- Phase 6 (T046) kit-column peak meter
// ==============================================================================
// Simple stereo peak meter bar. Driven by Controller::updateMeterViews from
// the 30 Hz UI timer tick with the peak L/R of every MetersBlock received
// since the previous tick (T2.31).
//
// This view renders two horizontal bars (L on top, R on bottom), each filling
// proportional to the held peak in [0, 1]: a higher peak shows at once, and
// the shown peak falls at 20 dB/s (one 30 Hz tick per setPeaks). Clipping above
// ~0.9 is rendered in a warning colour so overages are obvious at a glance.
// Asset loading is intentionally avoided (VSTGUI primitives only).
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/lib/crect.h"

namespace Membrum::UI {

class KitMetersView : public VSTGUI::CView
{
public:
    explicit KitMetersView(const VSTGUI::CRect& size) noexcept;
    ~KitMetersView() override = default;

    KitMetersView(const KitMetersView&)            = delete;
    KitMetersView& operator=(const KitMetersView&) = delete;

    /// Called from the UI thread (30 Hz timer), once per tick. Values are
    /// clamped to [0, 1] and held with a 20 dB/s fall.
    void setPeaks(float peakL, float peakR) noexcept;

    [[nodiscard]] float peakLForTest() const noexcept { return peakL_; }
    [[nodiscard]] float peakRForTest() const noexcept { return peakR_; }

    void draw(VSTGUI::CDrawContext* ctx) override;

private:
    float peakL_ = 0.0f;
    float peakR_ = 0.0f;
};

} // namespace Membrum::UI
