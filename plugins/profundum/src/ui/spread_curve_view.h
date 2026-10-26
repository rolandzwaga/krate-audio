#pragma once

// ==============================================================================
// Profundum Phase 12 - Profundum::UI::SpreadCurveView + SpreadCurveModel (FR-080)
// ==============================================================================
// Spec:  specs/profundum-phase12-ui/spec.md  (FR-080; SC-014)
// Plan:  specs/profundum-phase12-ui/plan.md  (sections 4.6.8, 4.9)
// Design: specs/profundum-phase12-ui/design/advanced-page.md section 6 (D2)
// Tasks: specs/profundum-phase12-ui/tasks.md (T016; Deviation 4)
//
// The SPREAD mini-spectrum: the static stereo width w(f) = c(f) * m of the
// Edit-Target endpoint on a log-frequency axis from 20 Hz to 10 kHz, 0 (MONO)
// to 1 (WIDE), with the law's two knee marks. The controller fills the model on
// the UI thread (refreshSpreadModel, plan 4.6.8) and calls invalid(); the view
// only draws it and has no timer.
//
// SpreadCurveModel is declared here, with the view that draws it, so the view
// carries no controller dependency (tasks.md Deviation 4).
//
// DISPLAY ONLY: a CView, not a CControl; it never issues performEdit.
// ==============================================================================

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/vstguifwd.h"

#include <array>

namespace Profundum::UI {

/// Plan 4.6.8, exactly. hz[i] = 20 * (10000 / 20)^(i / 63); w[i] = width at hz[i].
struct SpreadCurveModel {
    std::array<float, 64> hz{}, w{};
    float crossoverHz = 120.0f, lowerKneeHz = 0.0f, upperKneeHz = 0.0f;
    bool wide = false; bool solid = false;
};

class SpreadCurveView : public VSTGUI::CView {
public:
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 10000.0f;

    /// @p model is the controller's model; it must outlive the view.
    SpreadCurveView(const VSTGUI::CRect& size, const SpreadCurveModel* model);

    SpreadCurveView(const SpreadCurveView&) = delete;
    SpreadCurveView& operator=(const SpreadCurveView&) = delete;
    SpreadCurveView(SpreadCurveView&&) = delete;
    SpreadCurveView& operator=(SpreadCurveView&&) = delete;

    void draw(VSTGUI::CDrawContext* context) override;

    // Test surface (plan 4.9).
    [[nodiscard]] const std::array<float, 64>& sampledValuesForTest() const noexcept;
    [[nodiscard]] std::array<float, 2> kneeMarksForTest() const noexcept;
    [[nodiscard]] bool wideLabelVisibleForTest() const noexcept;

    /// Horizontal position in [0, 1] of @p hz on the 20 Hz - 10 kHz log axis (clamped).
    [[nodiscard]] static float axisPositionOf(float hz) noexcept;

private:
    void resolvePalette();

    const SpreadCurveModel* model_;

    // D7 tokens: panel, track, text, text-dim, accent. Fixed fallbacks, replaced
    // by the named uidesc colours the first time the view draws in an editor.
    VSTGUI::CColor panel_;
    VSTGUI::CColor track_;
    VSTGUI::CColor text_;
    VSTGUI::CColor textDim_;
    VSTGUI::CColor accent_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    bool paletteResolved_ = false;
};

}  // namespace Profundum::UI
