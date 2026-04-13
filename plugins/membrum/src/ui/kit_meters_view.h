#pragma once

// ==============================================================================
// KitMetersView -- Phase 6 (T046) kit-column peak meter
// ==============================================================================
// Simple stereo peak meter bar. Driven by Controller::updateMeterViews from
// the 30 Hz UI timer tick; reads peak L/R from the most recent MetersBlock
// received over IDataExchange.
//
// This view renders two horizontal bars (L on top, R on bottom), each filling
// proportional to the last published peak value in [0, 1]. Clipping above
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

    /// Called from the UI thread (30 Hz timer). Values are clamped to [0, 1].
    void setPeaks(float peakL, float peakR) noexcept;

    [[nodiscard]] float peakLForTest() const noexcept { return peakL_; }
    [[nodiscard]] float peakRForTest() const noexcept { return peakR_; }

    void draw(VSTGUI::CDrawContext* ctx) override;

private:
    float peakL_ = 0.0f;
    float peakR_ = 0.0f;
};

} // namespace Membrum::UI
