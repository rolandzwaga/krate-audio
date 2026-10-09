#pragma once

// ==============================================================================
// VoragoPanelSubController - page switching, session controls, Gravity capture
// ==============================================================================
// Spec:  specs/vorago-phase13-ui/spec.md  (C-5, FR-043, FR-055, FR-056, SC-017, SC-018)
// Plan:  specs/vorago-phase13-ui/plan.md  (section 5.3)
// Tasks: specs/vorago-phase13-ui/tasks.md (T013)
//
// IT IS NOT A CView. It is a VSTGUI::DelegationController created by
// Controller::createSubController("VoragoPanel") for the template root, so every
// view in the document passes through verifyView() below. Modelled on
// plugins/seraphis/src/ui/edit_sub_controller.h.
//
// It reads the RAW UIAttributes, so it also sees views made by createCustomView
// (which the view factory never decorates):
//   - uidesc-label="page-N" (N 0..6) on a CViewContainer -> one of the 7 pages;
//     visible iff N == owner->activePage().
//   - session-tag="pages" on the CSegmentButton -> the page strip.
//   - custom-view-name="PresetBrowserButton" -> the header preset button.
//   - control tag kMacroGravityId on an ArcKnob, and uidesc-label=
//     "macro-gravity-label" on a CTextLabel -> handed to the controller for the
//     Gravity anchor-mode display (FR-080).
//
// SESSION TAGS (>= kSessionTagBase) are never ParamIDs and never reach the host:
// valueChanged / controlBeginEdit / controlEndEdit swallow them, so a page
// switch or a preset-button click produces zero begin/perform/end edits (SC-018).
// The active page is controller session state only (never persisted, C-5).
//
// Page switching only toggles visibility: no resize, no view removal (FR-056).
//
// THREADING. UI thread only.
// ==============================================================================

#include "vstgui/lib/vstguifwd.h"
#include "vstgui/uidescription/delegationcontroller.h"

#include <array>
#include <cstdint>

namespace Vorago {
class Controller;
}  // namespace Vorago

namespace Vorago::UI {

/// EVERY session tag is >= this; never a ParamID (max registered ID is 1502).
inline constexpr std::int32_t kSessionTagBase = 9000;
inline constexpr std::int32_t kPresetButtonTag = 9000;  ///< header preset button
inline constexpr std::int32_t kPageStripTag = 9100;     ///< the seven-page CSegmentButton
inline constexpr int kVoragoPageCount = 7;

class VoragoPanelSubController : public VSTGUI::DelegationController {
public:
    /// `parent` may be null in a headless test that constructs this directly;
    /// every delegation is null-guarded (DelegationController dereferences it
    /// unconditionally).
    VoragoPanelSubController(Vorago::Controller* owner, VSTGUI::IController* parent);

    void valueChanged(VSTGUI::CControl* control) override;
    void controlBeginEdit(VSTGUI::CControl* control) override;
    void controlEndEdit(VSTGUI::CControl* control) override;

    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                              const VSTGUI::IUIDescription* description) override;

private:
    [[nodiscard]] static bool isSessionTag(std::int32_t tag) noexcept {
        return tag >= kSessionTagBase;
    }

    void applyPage(int page) noexcept;

    Vorago::Controller* owner_ = nullptr;
    std::array<VSTGUI::CViewContainer*, kVoragoPageCount> pages_{};
    VSTGUI::CSegmentButton* pageStrip_ = nullptr;
};

}  // namespace Vorago::UI
