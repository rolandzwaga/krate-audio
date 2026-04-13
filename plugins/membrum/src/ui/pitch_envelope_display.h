#pragma once

// ==============================================================================
// PitchEnvelopeDisplay -- Phase 6 (Spec 141, US8) -- primary voice control
// ==============================================================================
// FR covered: pitch envelope display promoted to the Acoustic-mode Selected-Pad
// panel (not behind a tab). Patterned on plugins/shared/src/ui/adsr_display.h
// but tailored to the simpler Start -> End, Time + Curve envelope used by
// Membrum's tone shaper.
//
// Parameters driven (all normalised [0, 1] at the VST boundary):
//   - kToneShaperPitchEnvStartId  -- start frequency (20..2000 Hz)
//   - kToneShaperPitchEnvEndId    -- end   frequency (20..2000 Hz)
//   - kToneShaperPitchEnvTimeId   -- sweep time      (0..500 ms)
//   - kToneShaperPitchEnvCurveId  -- curve shape     (StringListParameter: Exp/Lin)
//
// Interaction:
//   - Dragging the Start handle vertically edits kToneShaperPitchEnvStartId.
//   - Dragging the End   handle vertically edits kToneShaperPitchEnvEndId.
//   - Dragging the Time  handle horizontally edits kToneShaperPitchEnvTimeId.
//   - The Curve selector (adjacent COptionMenu) edits kToneShaperPitchEnvCurveId
//     as a two-entry string list (0 = Exp, 1 = Lin).
//
// Parameter edits are forwarded to the owning controller via an
// ``EditCallback`` the controller installs at view construction time. The view
// itself never touches VST3 SDK APIs directly; the controller's callback
// performs ``beginEdit`` / ``performEdit`` / ``endEdit``. This mirrors the
// approach used by ``CouplingMatrixView``.
//
// Hit-testing, dragging, and drawing all use VSTGUI primitives only (CPoint,
// CRect, CDrawContext, MouseDownEvent/MouseMoveEvent/MouseUpEvent). No native
// Win32/Cocoa mouse APIs are touched (T083).
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/events.h"

#include <cstdint>
#include <functional>
#include <utility>

namespace Membrum::UI {

class PitchEnvelopeDisplay : public VSTGUI::CView
{
public:
    // Which control point the user is dragging.
    enum class DragTarget : std::uint8_t
    {
        None  = 0,
        Start = 1,  // vertical  -> kToneShaperPitchEnvStartId
        End   = 2,  // vertical  -> kToneShaperPitchEnvEndId
        Time  = 3   // horizontal -> kToneShaperPitchEnvTimeId
    };

    // Operations forwarded to the controller. The callback must route these to
    // beginEdit / performEdit / endEdit on the paramId.
    enum class EditOp : std::uint8_t
    {
        Begin   = 0,
        Perform = 1,
        End     = 2
    };

    using EditCallback =
        std::function<void(std::uint32_t paramId, EditOp op, float normalized)>;

    // Parameter tags the view writes to. Defaults match Membrum's plugin_ids.h
    // but are overridable so tests can use synthetic IDs.
    struct ParamTags
    {
        std::uint32_t start; // kToneShaperPitchEnvStartId
        std::uint32_t end;   // kToneShaperPitchEnvEndId
        std::uint32_t time;  // kToneShaperPitchEnvTimeId
        std::uint32_t curve; // kToneShaperPitchEnvCurveId

        constexpr ParamTags() noexcept
            : start(216), end(217), time(218), curve(219) {}
        constexpr ParamTags(std::uint32_t s, std::uint32_t e,
                            std::uint32_t t, std::uint32_t c) noexcept
            : start(s), end(e), time(t), curve(c) {}
    };

    explicit PitchEnvelopeDisplay(const VSTGUI::CRect& size,
                                  ParamTags             tags = ParamTags{}) noexcept;

    ~PitchEnvelopeDisplay() override;

    PitchEnvelopeDisplay(const PitchEnvelopeDisplay&)            = delete;
    PitchEnvelopeDisplay& operator=(const PitchEnvelopeDisplay&) = delete;

    // --- configuration -----------------------------------------------------
    void setEditCallback(EditCallback cb) noexcept { editCallback_ = std::move(cb); }

    [[nodiscard]] const ParamTags& paramTags() const noexcept { return tags_; }

    // --- parameter sync (controller pushes current normalised values) ------
    void setStartNormalized(float n) noexcept;
    void setEndNormalized  (float n) noexcept;
    void setTimeNormalized (float n) noexcept;
    void setCurveNormalized(float n) noexcept;

    /// Generic setter addressed by param tag; ignores unknown tags. Useful for
    /// the controller's ``update()`` IDependent dispatch.
    void setNormalized(std::uint32_t paramId, float n) noexcept;

    [[nodiscard]] float startNormalized() const noexcept { return startN_; }
    [[nodiscard]] float endNormalized()   const noexcept { return endN_; }
    [[nodiscard]] float timeNormalized()  const noexcept { return timeN_; }
    [[nodiscard]] float curveNormalized() const noexcept { return curveN_; }

    // --- testable hit-test and drag helpers --------------------------------
    /// Hit-test a local-coordinate point against the three handles. Returns
    /// the handle closest to `p` within ``kHitRadius`` pixels, or ``None``.
    [[nodiscard]] DragTarget hitTest(VSTGUI::CPoint p) const noexcept;

    /// Returns the local-coordinate rect-centre of the given handle.
    [[nodiscard]] VSTGUI::CPoint handleCenter(DragTarget h) const noexcept;

    /// Direct handler used by tests and by the VSTGUI mouse events. Drives the
    /// EditCallback with Begin/Perform/End as appropriate.
    void handleMouseDown(VSTGUI::CPoint localPoint) noexcept;
    void handleMouseMove(VSTGUI::CPoint localPoint) noexcept;
    void handleMouseUp  (VSTGUI::CPoint localPoint) noexcept;

    [[nodiscard]] DragTarget activeDrag() const noexcept { return dragTarget_; }

    // --- VSTGUI overrides --------------------------------------------------
    void draw(VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent  (VSTGUI::MouseUpEvent&   event) override;
    bool removed(CView* parent) override;

    // --- geometry ----------------------------------------------------------
    // Hit radius for handle selection (px).
    static constexpr double kHitRadius = 10.0;
    // Handle visual radius (px).
    static constexpr double kHandleRadius = 5.0;
    // Inset from view edge where the envelope is drawn (px).
    static constexpr double kPadding = 12.0;

private:
    // Compute local-coordinate point from a normalised value for each handle.
    [[nodiscard]] double toLocalXFromTime (float n) const noexcept;
    [[nodiscard]] double toLocalYFromPitch(float n) const noexcept;

    [[nodiscard]] float  fromLocalXToTime (double x) const noexcept;
    [[nodiscard]] float  fromLocalYToPitch(double y) const noexcept;

    void dispatch(std::uint32_t paramId, EditOp op, float normalized) const noexcept;

    ParamTags    tags_ {};
    EditCallback editCallback_ {};

    float startN_ = 0.5f;
    float endN_   = 0.25f;
    float timeN_  = 0.0f;
    float curveN_ = 0.0f;

    DragTarget dragTarget_ = DragTarget::None;
};

} // namespace Membrum::UI
