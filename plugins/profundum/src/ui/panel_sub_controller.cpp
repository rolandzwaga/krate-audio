// ==============================================================================
// ProfundumPanelSubController implementation (Profundum Phase 12, T026; plan 4.7)
// ==============================================================================

#include "ui/panel_sub_controller.h"

#include "controller/controller.h"
#include "parameters/morph_params.h"
#include "parameters/sound_param_row.h"  // indexFromNormalized
#include "plugin_ids.h"
#include "ui/macro_ui_map.h"
#include "ui/offset_ring_knob.h"
#include "ui/morph_indicator_slider.h"

// plugins/shared/src/ui (header-only; their inline creators also register the views in this TU)
#include "ui/adsr_display.h"
#include "ui/arc_knob.h"
#include "ui/bipolar_slider.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/cparamdisplay.h"
#include "vstgui/lib/controls/csegmentbutton.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"
#include "vstgui/uidescription/iuidescription.h"
#include "vstgui/uidescription/uiattributes.h"

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <system_error>

namespace Profundum::UI {

namespace {

using Steinberg::Vst::ParamID;
using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;

constexpr const char* kLabelAttribute = "uidesc-label";
constexpr const char* kSessionTagAttribute = "session-tag";
constexpr const char* kTitleAttribute = "title";
constexpr const char* kCustomViewNameAttribute = "custom-view-name";
constexpr const char* kPresetButtonViewName = "PresetBrowserButton";  // Phase 13 FR-059 (Vorago :89-95)
constexpr const char* kHeldLabelName = "held-at-a";
constexpr const char* kHeldLabelText = "Held at A";

constexpr std::array<const char*, 2> kPageLabels{"page-main", "page-advanced"};
constexpr std::array<const char*, 3> kTabLabels{"tab-sections", "tab-modulation", "tab-settings"};

constexpr float kDimmedAlpha = 0.45f;  // FR-044 (plan 4.7)
constexpr float kFullAlpha = 1.0f;
constexpr int kHoldIndex = 0;          // Anchor Morph Mode list "Hold, Blend, Yield" (morph_params.h:180)
constexpr int kEditTargetBIndex = 1;   // Edit Target list "A, B" (morph_params.h:181)

/// The fallback of the D7 `highlight` token (#E0B060) when the uidesc has no such colour.
constexpr VSTGUI::CColor kFallbackHighlight{0xE0, 0xB0, 0x60, 0xFF};

[[nodiscard]] bool isMacroTag(std::int32_t tag) noexcept {
    return tag >= static_cast<std::int32_t>(kMacroWeightId) && tag <= static_cast<std::int32_t>(kMacroCleanViolentId);
}

[[nodiscard]] bool isAnchorTag(std::int32_t tag) noexcept {
    return tag >= static_cast<std::int32_t>(kAnchorWaveformId) && tag <= static_cast<std::int32_t>(kAnchorFoldFeedId);
}

/// The selected index of a segment button or option menu (any other control: its rounded value).
[[nodiscard]] int selectedIndexOf(const VSTGUI::CControl* control) {
    if (const auto* segment = dynamic_cast<const VSTGUI::CSegmentButton*>(control)) {
        return static_cast<int>(segment->getSelectedSegment());
    }
    if (const auto* menu = dynamic_cast<const VSTGUI::COptionMenu*>(control)) {
        return static_cast<int>(menu->getCurrentIndex());
    }
    return static_cast<int>(std::lround(control->getValue()));
}

/// Shows @p index on a segment button or option menu WITHOUT valueChanged / begin / end edits (no feedback loop).
void showIndex(VSTGUI::CControl* control, int index) {
    if (auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(control)) {
        const std::size_t count = segment->getSegments().size();
        if (count > 1u) {
            segment->setValueNormalized(static_cast<float>(index) / static_cast<float>(count - 1u));
        }
    } else {
        control->setValue(static_cast<float>(index));  // COptionMenu: the value is the entry index
    }
    control->invalid();
}

}  // namespace

ProfundumPanelSubController::ProfundumPanelSubController(Profundum::Controller* owner, VSTGUI::IController* parent)
    : VSTGUI::DelegationController(parent), owner_(owner), highlightColor_(kFallbackHighlight) {
    for (int m = 0; m < kNumMacros; ++m) {
        masks_[static_cast<std::size_t>(m)] = highlightMask(static_cast<Krate::DSP::ProfundumMacro>(m));
    }
}

ProfundumPanelSubController::~ProfundumPanelSubController() {
    // Controls still alive keep no listener pointing here; deleted ones already dropped it (viewWillDelete).
    for (VSTGUI::CControl* control : macroControls_) {
        control->unregisterViewEventListener(this);
        control->unregisterViewListener(this);
    }
    macroControls_.clear();
    if (owner_ != nullptr) {
        owner_->unregisterPanel(this);
    }
}

// ==============================================================================
// verifyView - collect every view the panel drives
// ==============================================================================

std::int32_t ProfundumPanelSubController::sessionTagOf(const VSTGUI::CControl* control,
                                                       const VSTGUI::UIAttributes& attributes) noexcept {
    std::int32_t tag = -1;
    const std::string* customName = attributes.getAttributeValue(kCustomViewNameAttribute);
    if (customName != nullptr && *customName == kPresetButtonViewName) {
        tag = kPresetBrowserTag;  // the header preset button (created with tag -1, controller.cpp createCustomView)
    } else if (const std::string* value = attributes.getAttributeValue(kSessionTagAttribute)) {
        std::int32_t parsed = -1;
        const char* first = value->data();
        const char* last = first + value->size();
        if (std::from_chars(first, last, parsed).ec == std::errc{}) {
            tag = parsed;
        }
    } else if (control->getTag() >= kSessionTagBase) {
        tag = control->getTag();
    }
    return std::find(kSessionTags.begin(), kSessionTags.end(), tag) != kSessionTags.end() ? tag : -1;
}

void ProfundumPanelSubController::collectContainer(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes) {
    const std::string* label = attributes.getAttributeValue(kLabelAttribute);
    if (label != nullptr) {
        if (auto* container = dynamic_cast<VSTGUI::CViewContainer*>(view)) {
            for (std::size_t i = 0; i < kPageLabels.size(); ++i) {
                if (*label == kPageLabels[i]) {
                    pages_[i] = container;
                    container->setVisible(static_cast<int>(i) == owner_->activePage());
                }
            }
            for (std::size_t i = 0; i < kTabLabels.size(); ++i) {
                if (*label == kTabLabels[i]) {
                    tabs_[i] = container;
                    container->setVisible(static_cast<int>(i) == owner_->activeTab());
                }
            }
        }
    }
    // The "Held at A" label (FR-044): by its uidesc-label or its title; hidden until the dimming shows it.
    const std::string* title = attributes.getAttributeValue(kTitleAttribute);
    if (dynamic_cast<VSTGUI::CTextLabel*>(view) != nullptr &&
        ((label != nullptr && *label == kHeldLabelName) || (title != nullptr && *title == kHeldLabelText))) {
        heldLabel_ = view;
        view->setVisible(false);
    }
}

void ProfundumPanelSubController::collectSessionControl(VSTGUI::CControl* control, std::int32_t tag) {
    // Listener first: CControl::setTag notifies the listener, and the VST3Editor would bind a tag it hears about.
    control->setListener(this);
    control->setTag(tag);
    switch (tag) {
        case kPageStripTag:
            pageStrips_.push_back(control);
            showIndex(control, owner_->activePage());
            break;
        case kTabStripTag:
            tabStrips_.push_back(control);
            showIndex(control, owner_->activeTab());
            break;
        case kCompareMainTag:
        case kCompareAdvancedTag:
            compareSegments_.push_back(control);
            showIndex(control, segmentOfCompareMode(owner_->compareMode()));
            break;
        case kZoomTag:
            zoomControls_.push_back(control);
            showIndex(control, owner_->zoomStep());
            break;
        case kNameATag:
        case kNameBTag:
            nameEdits_[tag == kNameATag ? 0u : 1u] = dynamic_cast<VSTGUI::CTextEdit*>(control);
            break;
        case kCaptionATag:
        case kCaptionBTag:
            captionLabels_[tag == kCaptionATag ? 0u : 1u] = dynamic_cast<VSTGUI::CTextLabel*>(control);
            break;
        default:  // the copy buttons need nothing beyond the tag and the listener
            break;
    }
}

void ProfundumPanelSubController::collectBoundControl(VSTGUI::CControl* control) {
    // The ADSRDisplay stands for 420 - 426 whatever its tag (plan 4.6.10): wire it, highlight it as one entry.
    if (auto* display = dynamic_cast<Krate::Plugins::ADSRDisplay*>(control)) {
        owner_->wireAdsrDisplay(display);
        highlightEntries_.push_back({.control = control,
                                     .kind = HighlightKind::Adsr,
                                     .id = kAmpAttackId,
                                     .original = display->getControlPointColor()});
        return;
    }
    const std::int32_t tag = control->getTag();
    if (tag < 0 || tag >= static_cast<std::int32_t>(kNumRoutedIds)) {
        return;
    }
    const auto id = static_cast<ParamID>(tag);

    if (isMacroTag(tag)) {
        control->registerViewEventListener(this);
        control->registerViewListener(this);
        macroControls_.push_back(control);
    }
    if (isAnchorTag(tag)) {
        anchorControls_.push_back(control);
    }
    if (id == kNotePortamentoId) {
        if (auto* readout = dynamic_cast<VSTGUI::CParamDisplay*>(control)) {
            portaDisplay_ = readout;
        }
    }
    if (auto* ring = dynamic_cast<OffsetRingKnob*>(control)) {
        owner_->registerRingKnob(id, ring);  // ignores an ID outside kMacroTargetParamIds
    }
    if (id == kMorphPositionId) {
        if (auto* slider = dynamic_cast<MorphIndicatorSlider*>(control)) {
            owner_->registerMorphIndicator(slider);  // Phase 14 FR-035 (plan 4.9)
        }
    }
    bool inHighlightSet = false;
    for (const Mask& mask : masks_) {
        inHighlightSet = inHighlightSet || mask[id];
    }
    if (inHighlightSet) {
        if (auto* knob = dynamic_cast<Krate::Plugins::ArcKnob*>(control)) {
            highlightEntries_.push_back(
                {.control = control, .kind = HighlightKind::Arc, .id = id, .original = knob->getGuideColor()});
        } else if (auto* slider = dynamic_cast<Krate::Plugins::BipolarSlider*>(control)) {
            highlightEntries_.push_back(
                {.control = control, .kind = HighlightKind::Bipolar, .id = id, .original = slider->getTrackColor()});
        }
    }
}

VSTGUI::CView* ProfundumPanelSubController::verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                                                       const VSTGUI::IUIDescription* description) {
    if (view != nullptr && owner_ != nullptr) {
        if (!registered_) {
            registered_ = true;
            owner_->registerPanel(this);
            VSTGUI::CColor token;
            if (description != nullptr && description->getColor("highlight", token)) {
                highlightColor_ = token;  // the D7 colour token; the fallback otherwise
            }
        }
        collectContainer(view, attributes);
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
            const std::int32_t sessionTag = sessionTagOf(control, attributes);
            if (sessionTag >= 0) {
                collectSessionControl(control, sessionTag);
            } else {
                collectBoundControl(control);
            }
        }
    }
    // DelegationController::verifyView dereferences its parent unconditionally.
    return (controller != nullptr) ? VSTGUI::DelegationController::verifyView(view, attributes, description) : view;
}

// ==============================================================================
// valueChanged - session controls handled here, everything else forwarded
// ==============================================================================

void ProfundumPanelSubController::valueChanged(VSTGUI::CControl* control) {
    if (control == nullptr) {
        return;
    }
    const std::int32_t tag = control->getTag();
    if (!isSessionTag(tag)) {
        if (controller != nullptr) {
            VSTGUI::DelegationController::valueChanged(control);
        }
        return;
    }
    if (owner_ == nullptr) {
        return;
    }
    switch (tag) {
        case kPageStripTag:
            owner_->setActivePage(selectedIndexOf(control));
            showSessionState();
            break;
        case kTabStripTag:
            owner_->setActiveTab(selectedIndexOf(control));
            showSessionState();
            break;
        case kCompareMainTag:
        case kCompareAdvancedTag:
            // FR-041 / FR-049: the click is a request; the segments show only the controller's reported mode.
            owner_->pressCompare(compareModeOfSegment(selectedIndexOf(control)));
            showCompareMode(owner_->compareMode());
            break;
        case kCopyAToBMainTag:
        case kCopyAToBAdvancedTag:
        case kCopyBToAMainTag:
        case kCopyBToAAdvancedTag:
            if (control->getValueNormalized() >= 0.5f) {  // the press, not the release of a kick button
                const bool fromA = tag == kCopyAToBMainTag || tag == kCopyAToBAdvancedTag;
                owner_->copyEndpoint(fromA ? Endpoint::A : Endpoint::B);
            }
            break;
        case kNameATag:
        case kNameBTag:
            if (auto* edit = dynamic_cast<VSTGUI::CTextEdit*>(control)) {
                const Endpoint e = tag == kNameATag ? Endpoint::A : Endpoint::B;
                owner_->setEndpointName(e, edit->getText().getString());  // refreshes the captions and names
            }
            break;
        case kZoomTag:
            owner_->setZoomStep(selectedIndexOf(control));
            showSessionState();  // both zoom controls (header and SETTINGS) show the one session step
            break;
        case kPresetBrowserTag:  // Phase 13 FR-030 / FR-059 (Vorago :145-148): one valueChanged per press
            owner_->openPresetBrowser();
            break;
        default:  // the captions are display only; never forwarded (not a ParamID)
            break;
    }
}

// Session tags never reach EditController::beginEdit / endEdit (no host gesture for a UI-only control).
void ProfundumPanelSubController::controlBeginEdit(VSTGUI::CControl* control) {
    if (control != nullptr && isSessionTag(control->getTag())) {
        return;
    }
    if (controller != nullptr) {
        VSTGUI::DelegationController::controlBeginEdit(control);
    }
}

void ProfundumPanelSubController::controlEndEdit(VSTGUI::CControl* control) {
    if (control != nullptr && isSessionTag(control->getTag())) {
        return;
    }
    if (controller != nullptr) {
        VSTGUI::DelegationController::controlEndEdit(control);
    }
}

// ==============================================================================
// Macro highlight (FR-060; plan 4.7)
// ==============================================================================

void ProfundumPanelSubController::viewOnEvent(VSTGUI::CView* view, VSTGUI::Event& event) {
    const auto* control = dynamic_cast<const VSTGUI::CControl*>(view);
    if (control == nullptr || !isMacroTag(control->getTag())) {
        return;
    }
    const auto m = static_cast<std::size_t>(control->getTag() - static_cast<std::int32_t>(kMacroWeightId));
    if (event.type == VSTGUI::EventType::MouseEnter) {
        hovered_[m] = true;
        applyHighlights();
    } else if (event.type == VSTGUI::EventType::MouseExit) {
        hovered_[m] = false;  // an open gesture keeps the highlight on (isMacroActive)
        applyHighlights();
    }
    // Never consumed: the knob handles its own events.
}

void ProfundumPanelSubController::viewWillDelete(VSTGUI::CView* view) {
    const auto it = std::find(macroControls_.begin(), macroControls_.end(), view);
    if (it != macroControls_.end()) {
        (*it)->unregisterViewEventListener(this);
        (*it)->unregisterViewListener(this);
        macroControls_.erase(it);
    }
}

void ProfundumPanelSubController::onMacroEdit(ParamID tag, bool open) {
    if (!isMacroTag(static_cast<std::int32_t>(tag))) {
        return;
    }
    editOpen_[static_cast<std::size_t>(tag) - static_cast<std::size_t>(kMacroWeightId)] = open;
    applyHighlights();
}

bool ProfundumPanelSubController::isHighlighted(ParamID id) const noexcept {
    if (id >= static_cast<ParamID>(kNumRoutedIds)) {
        return false;
    }
    for (int m = 0; m < kNumMacros; ++m) {
        if (isMacroActive(m) && masks_[static_cast<std::size_t>(m)][id]) {
            return true;
        }
    }
    return false;
}

void ProfundumPanelSubController::applyHighlights() {
    for (const HighlightEntry& e : highlightEntries_) {
        switch (e.kind) {
            case HighlightKind::Arc: {
                const bool on = isHighlighted(e.id);
                if (auto* knob = dynamic_cast<Krate::Plugins::ArcKnob*>(e.control)) {
                    knob->setGuideColor(on ? highlightColor_ : e.original);
                }
                break;
            }
            case HighlightKind::Bipolar: {
                const bool on = isHighlighted(e.id);
                if (auto* slider = dynamic_cast<Krate::Plugins::BipolarSlider*>(e.control)) {
                    slider->setTrackColor(on ? highlightColor_ : e.original);
                }
                break;
            }
            case HighlightKind::Adsr: {
                // The display stands for the three amp times a macro can drive (FR-060: Tight <-> Loose).
                const bool on = isHighlighted(kAmpAttackId) || isHighlighted(kAmpDecayId) ||
                                isHighlighted(kAmpReleaseId);
                if (auto* adsr = dynamic_cast<Krate::Plugins::ADSRDisplay*>(e.control)) {
                    adsr->setControlPointColor(on ? highlightColor_ : e.original);
                }
                break;
            }
        }
        e.control->invalid();
    }
}

std::vector<ParamID> ProfundumPanelSubController::highlightedIdsForTest() const {
    std::vector<ParamID> ids;
    for (ParamID id = 0; id < static_cast<ParamID>(kNumRoutedIds); ++id) {
        if (isHighlighted(id)) {
            ids.push_back(id);
        }
    }
    return ids;
}

// ==============================================================================
// Refreshes the controller drives
// ==============================================================================

void ProfundumPanelSubController::refreshAnchorDimming() {
    if (owner_ == nullptr) {
        return;
    }
    const bool dim =
        indexFromNormalized(owner_->getParamNormalized(kMorphAnchorModeId), kNumAnchorMorphModes - 1) == kHoldIndex &&
        indexFromNormalized(owner_->getParamNormalized(kMorphEditTargetId), kNumEditTargets - 1) == kEditTargetBIndex;
    for (VSTGUI::CControl* control : anchorControls_) {
        control->setAlphaValue(dim ? kDimmedAlpha : kFullAlpha);  // setMouseEnabled is never touched (FR-044)
    }
    if (heldLabel_ != nullptr) {
        heldLabel_->setVisible(dim);
        heldLabel_->invalid();
    }
}

bool ProfundumPanelSubController::isAnchorDimmedForTest(ParamID id) const {
    for (const VSTGUI::CControl* control : anchorControls_) {
        if (control->getTag() == static_cast<std::int32_t>(id)) {
            return control->getAlphaValue() < kFullAlpha;
        }
    }
    return false;
}

void ProfundumPanelSubController::refreshPortamentoReadout() {
    if (portaDisplay_ == nullptr) {
        return;
    }
    // The readout's string comes from getParamStringByValue at draw time, which reads the current 414
    // (controller.cpp getParamStringByValue, FR-007), so a redraw is the whole refresh.
    portaDisplay_->invalid();
    ++portaReadoutInvalidations_;
}

void ProfundumPanelSubController::showCompareMode(CompareMode m) {
    for (VSTGUI::CControl* segment : compareSegments_) {
        showIndex(segment, segmentOfCompareMode(m));
    }
}

void ProfundumPanelSubController::onCompareModeChanged(CompareMode m) { showCompareMode(m); }

void ProfundumPanelSubController::refreshCaptions() {
    if (owner_ == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < 2u; ++i) {
        const Endpoint e = i == 0u ? Endpoint::A : Endpoint::B;
        if (captionLabels_[i] != nullptr) {
            const std::string caption = owner_->endpointCaption(e);
            captionLabels_[i]->setText(caption.c_str());
        }
        VSTGUI::CTextEdit* edit = nameEdits_[i];
        if (edit != nullptr && (edit->getPlatformTextEdit() == nullptr)) {  // never under the user's open text field
            const std::string name(owner_->endpointName(e).view());
            edit->setText(name.c_str());
        }
    }
}

void ProfundumPanelSubController::applyVisibility() {
    VSTGUI::CView* area = nullptr;
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i] != nullptr) {
            pages_[i]->setVisible(static_cast<int>(i) == owner_->activePage());
            area = pages_[i]->getParentView();
        }
    }
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i] != nullptr) {
            tabs_[i]->setVisible(static_cast<int>(i) == owner_->activeTab());
        }
    }
    if (area != nullptr) {
        area->invalid();  // visibility only: no resize, no view removal
    }
}

void ProfundumPanelSubController::showSessionState() {
    if (owner_ == nullptr) {
        return;
    }
    applyVisibility();
    for (VSTGUI::CControl* control : pageStrips_) {
        showIndex(control, owner_->activePage());
    }
    for (VSTGUI::CControl* control : tabStrips_) {
        showIndex(control, owner_->activeTab());
    }
    for (VSTGUI::CControl* control : zoomControls_) {
        showIndex(control, owner_->zoomStep());
    }
}

}  // namespace Profundum::UI
