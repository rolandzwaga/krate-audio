// ProfundumPanelSubController — written by T026
#pragma once

// ==============================================================================
// Profundum Phase 12 - Profundum::UI::ProfundumPanelSubController
// ==============================================================================
// Spec:  specs/profundum-phase12-ui/spec.md  (FR-013, FR-014, FR-030, FR-035, FR-041, FR-044, FR-060; SC-009,
//        SC-017, SC-018, SC-032)
// Plan:  specs/profundum-phase12-ui/plan.md  (section 4.7; 4.6.3, 4.6.9, 4.6.10)
// Design: specs/profundum-phase12-ui/design/interaction-automation.md section 1.1 (the D6 session-tag list)
// Tasks: specs/profundum-phase12-ui/tasks.md (T026)
//
// IT IS NOT A CView. It is the VSTGUI::DelegationController that
// Controller::createSubController("ProfundumPanel") makes for the template root,
// so every view of the editor passes through verifyView() below. Modelled on
// plugins/vorago/src/ui/panel_sub_controller.h.
//
// verifyView reads the RAW attributes (uidesc-label, session-tag, title) and the
// control tag, and collects:
//   - the page containers page-main / page-advanced and the tab containers
//     tab-sections / tab-modulation / tab-settings (visibility only, FR-013, FR-030);
//   - every UI-only control: its session-tag attribute (a number, the D6 list) or
//     its own tag >= kSessionTagBase becomes its tag, and this object becomes its
//     listener BEFORE the tag is set, so the VST3Editor never binds it to a
//     parameter. Two controls may share a tag (9300: header and SETTINGS zoom,
//     user ruling 2026-10-23; 9200 / 9203 share the compare mapping);
//   - the controls bound to 300 - 306 (Lock dimming, FR-044) and the "Held at A" label;
//   - the macro controls 100 - 103 (hover listeners, FR-060);
//   - every control bound to an ID of a macro highlight set (ArcKnob guide colour,
//     BipolarSlider track colour; the ADSRDisplay control-point colour for 420 / 421 / 423);
//   - the OffsetRingKnobs of the macro targets (owner->registerRingKnob, P-5) and the
//     ADSRDisplay (owner->wireAdsrDisplay, plan 4.6.10);
//   - the 412 CParamDisplay readout (FR-035), the endpoint name edits and captions.
//
// SESSION TAGS (>= kSessionTagBase) are never ParamIDs: valueChanged handles them
// and controlBeginEdit / controlEndEdit swallow them, so a page, tab, zoom, compare,
// copy or name control produces no begin / perform / end edit. A compare click
// never shows its own state: every compare segment is reset to the controller's
// compareMode() at once (FR-041).
//
// The controller calls the refresh hooks only while it holds this panel (from the
// first verifyView until willClose, or this object's destruction).
//
// THREADING. UI thread only. Nothing here runs on the audio thread.
// ==============================================================================

#include "parameters/morph_params.h"  // CompareMode
#include "plugin_ids.h"               // kNumRoutedIds

#include "pluginterfaces/vst/vsttypes.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/iviewlistener.h"
#include "vstgui/lib/vstguifwd.h"
#include "vstgui/uidescription/delegationcontroller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Profundum {
class Controller;
}  // namespace Profundum

namespace Profundum::UI {

/// EVERY session tag is >= this; never a ParamID (the highest registered ID is 1002).
inline constexpr std::int32_t kSessionTagBase = 9000;
inline constexpr std::int32_t kPageStripTag = 9100;      ///< MAIN / ADVANCED
inline constexpr std::int32_t kTabStripTag = 9110;       ///< SECTIONS / MODULATION / SETTINGS
inline constexpr std::int32_t kCompareMainTag = 9200;    ///< A / A.B / B on Main
inline constexpr std::int32_t kCopyAToBMainTag = 9201;
inline constexpr std::int32_t kCopyBToAMainTag = 9202;
inline constexpr std::int32_t kCompareAdvancedTag = 9203;  ///< A / A.B / B in section-morph-behaviour
inline constexpr std::int32_t kCopyAToBAdvancedTag = 9204;
inline constexpr std::int32_t kCopyBToAAdvancedTag = 9205;
inline constexpr std::int32_t kNameATag = 9210;          ///< CTextEdit
inline constexpr std::int32_t kNameBTag = 9211;
inline constexpr std::int32_t kCaptionATag = 9220;       ///< CTextLabel, display only
inline constexpr std::int32_t kCaptionBTag = 9221;
inline constexpr std::int32_t kZoomTag = 9300;           ///< header and SETTINGS (one session step)

/// The 13 UI-only tags of plan 4.7 / D6, in table order.
inline constexpr std::array<std::int32_t, 13> kSessionTags{
    kPageStripTag,   kTabStripTag,         kCompareMainTag,      kCopyAToBMainTag, kCopyBToAMainTag,
    kCompareAdvancedTag, kCopyAToBAdvancedTag, kCopyBToAAdvancedTag, kNameATag,        kNameBTag,
    kCaptionATag,    kCaptionBTag,         kZoomTag};

/// Plan 4.7: compare segment index 0 -> A, 1 -> A.B, 2 -> B.
[[nodiscard]] constexpr CompareMode compareModeOfSegment(int index) noexcept {
    return index <= 0 ? CompareMode::A : (index == 1 ? CompareMode::AB : CompareMode::B);
}
[[nodiscard]] constexpr int segmentOfCompareMode(CompareMode m) noexcept {
    return m == CompareMode::A ? 0 : (m == CompareMode::AB ? 1 : 2);
}

class ProfundumPanelSubController : public VSTGUI::DelegationController,
                                    public VSTGUI::IViewEventListener,
                                    public VSTGUI::ViewListenerAdapter {
public:
    /// @p parent may be null in a headless test that constructs this directly; every
    /// delegation is null-guarded (DelegationController dereferences it unconditionally).
    ProfundumPanelSubController(Profundum::Controller* owner, VSTGUI::IController* parent);
    ~ProfundumPanelSubController() override;

    ProfundumPanelSubController(const ProfundumPanelSubController&) = delete;
    ProfundumPanelSubController& operator=(const ProfundumPanelSubController&) = delete;
    ProfundumPanelSubController(ProfundumPanelSubController&&) = delete;
    ProfundumPanelSubController& operator=(ProfundumPanelSubController&&) = delete;

    // ---- IController ----
    void valueChanged(VSTGUI::CControl* control) override;
    void controlBeginEdit(VSTGUI::CControl* control) override;
    void controlEndEdit(VSTGUI::CControl* control) override;
    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                              const VSTGUI::IUIDescription* description) override;

    // ---- IViewEventListener (macro hover, FR-060) ----
    void viewOnEvent(VSTGUI::CView* view, VSTGUI::Event& event) override;
    // ---- IViewListener: a macro control that is deleted drops both listeners first ----
    void viewWillDelete(VSTGUI::CView* view) override;

    // ---- Controller hooks ----
    /// Plan 4.6.4: a gesture on macro @p tag (100 - 103) opened or closed; the highlight holds while it is open.
    void onMacroEdit(Steinberg::Vst::ParamID tag, bool open);
    /// FR-044: alpha 0.45 on every 300 - 306 control and the "Held at A" label shown while Anchor Morph Mode is Hold
    /// and Edit Target is B; alpha 1.0 and the label hidden otherwise. Mouse handling is never touched.
    void refreshAnchorDimming();
    /// FR-035: the 412 readout re-queries its string (the suffix follows 414).
    void refreshPortamentoReadout();
    /// FR-046: every compare segment shows @p m.
    void onCompareModeChanged(CompareMode m);
    /// FR-040 / FR-047: both captions and both name fields from the controller.
    void refreshCaptions();
    /// The page and tab containers and every page, tab and zoom control from the controller's session state.
    void showSessionState();

    // ---- Test reads (SC-009, SC-018, FR-044) ----
    /// The IDs (< kNumRoutedIds) of every highlighted macro's set, ascending.
    [[nodiscard]] std::vector<Steinberg::Vst::ParamID> highlightedIdsForTest() const;
    /// True when a collected 300 - 306 control bound to @p id is drawn dimmed.
    [[nodiscard]] bool isAnchorDimmedForTest(Steinberg::Vst::ParamID id) const;
    [[nodiscard]] std::uint32_t portaReadoutInvalidationsForTest() const noexcept {
        return portaReadoutInvalidations_;
    }

private:
    enum class HighlightKind : std::uint8_t { Arc, Bipolar, Adsr };
    struct HighlightEntry {
        VSTGUI::CControl* control = nullptr;
        HighlightKind kind = HighlightKind::Arc;
        Steinberg::Vst::ParamID id = 0;  // the bound ID (Adsr: 420)
        VSTGUI::CColor original{};
    };
    static constexpr int kNumMacros = 4;
    using Mask = std::array<bool, kNumRoutedIds>;

    [[nodiscard]] static bool isSessionTag(std::int32_t tag) noexcept { return tag >= kSessionTagBase; }
    /// Session tag of @p control from its session-tag attribute or its own tag; -1 when it has none.
    [[nodiscard]] static std::int32_t sessionTagOf(const VSTGUI::CControl* control,
                                                   const VSTGUI::UIAttributes& attributes) noexcept;
    void collectSessionControl(VSTGUI::CControl* control, std::int32_t tag);
    void collectBoundControl(VSTGUI::CControl* control);
    void collectContainer(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes);

    [[nodiscard]] bool isMacroActive(int m) const noexcept {
        const auto i = static_cast<std::size_t>(m);
        return hovered_[i] || editOpen_[i];
    }
    [[nodiscard]] bool isHighlighted(Steinberg::Vst::ParamID id) const noexcept;
    void applyHighlights();
    void applyVisibility();
    void showCompareMode(CompareMode m);

    Profundum::Controller* owner_ = nullptr;
    bool registered_ = false;  // owner->registerPanel(this) done

    std::array<VSTGUI::CViewContainer*, 2> pages_{};  // page-main, page-advanced
    std::array<VSTGUI::CViewContainer*, 3> tabs_{};   // tab-sections, tab-modulation, tab-settings
    std::vector<VSTGUI::CControl*> pageStrips_;
    std::vector<VSTGUI::CControl*> tabStrips_;
    std::vector<VSTGUI::CControl*> compareSegments_;
    std::vector<VSTGUI::CControl*> zoomControls_;
    std::array<VSTGUI::CTextEdit*, 2> nameEdits_{};
    std::array<VSTGUI::CTextLabel*, 2> captionLabels_{};

    std::vector<VSTGUI::CControl*> anchorControls_;  // bound to 300 - 306
    VSTGUI::CView* heldLabel_ = nullptr;             // "Held at A"
    VSTGUI::CParamDisplay* portaDisplay_ = nullptr;  // the 412 readout
    std::uint32_t portaReadoutInvalidations_ = 0;

    std::vector<VSTGUI::CControl*> macroControls_;   // 100 - 103, hover listeners registered
    std::vector<HighlightEntry> highlightEntries_;
    std::array<Mask, kNumMacros> masks_{};           // highlightMask(m), filled at construction
    std::array<bool, kNumMacros> hovered_{};
    std::array<bool, kNumMacros> editOpen_{};
    VSTGUI::CColor highlightColor_{};
};

}  // namespace Profundum::UI
