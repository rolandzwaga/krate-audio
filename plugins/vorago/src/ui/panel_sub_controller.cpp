// ==============================================================================
// VoragoPanelSubController implementation (Vorago Phase 13, T013)
// ==============================================================================

#include "ui/panel_sub_controller.h"

#include "controller/controller.h"
#include "plugin_ids.h"

// Also registers gArcKnobCreator (arc_knob.h:716) in vorago_tests, which does
// not compile entry.cpp (plan R-9).
#include "ui/arc_knob.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/csegmentbutton.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/uidescription/uiattributes.h"

#include <cstddef>
#include <string>
#include <utility>

namespace Vorago::UI {

namespace {

constexpr const char* kLabelAttribute = "uidesc-label";
constexpr const char* kSessionTagAttribute = "session-tag";
constexpr const char* kCustomViewNameAttribute = "custom-view-name";

constexpr const char* kPageStripSessionName = "pages";
constexpr const char* kPresetButtonViewName = "PresetBrowserButton";
constexpr const char* kGravityLabelName = "macro-gravity-label";

/// "page-N" with N in [0, kVoragoPageCount) -> N; anything else -> -1.
[[nodiscard]] int pageIndexFromLabel(const std::string& label) noexcept {
    constexpr std::size_t kPrefixLength = 5;  // "page-"
    if (label.size() != kPrefixLength + 1 || label.compare(0, kPrefixLength, "page-") != 0) {
        return -1;
    }
    const char digit = label[kPrefixLength];
    if (digit < '0' || digit > '9') {
        return -1;
    }
    const int index = digit - '0';
    return (index < kVoragoPageCount) ? index : -1;
}

}  // namespace

VoragoPanelSubController::VoragoPanelSubController(Vorago::Controller* owner,
                                                   VSTGUI::IController* parent)
    : VSTGUI::DelegationController(parent), owner_(owner) {}

// ==============================================================================
// verifyView - capture pages, session controls and the Gravity views
// ==============================================================================
VSTGUI::CView* VoragoPanelSubController::verifyView(VSTGUI::CView* view,
                                                    const VSTGUI::UIAttributes& attributes,
                                                    const VSTGUI::IUIDescription* description) {
    if (view != nullptr && owner_ != nullptr) {
        const std::string* label = attributes.getAttributeValue(kLabelAttribute);

        // --- The seven page containers (FR-055) ---
        if (label != nullptr) {
            if (auto* container = dynamic_cast<VSTGUI::CViewContainer*>(view)) {
                const int page = pageIndexFromLabel(*label);
                if (page >= 0) {
                    pages_[static_cast<std::size_t>(page)] = container;
                    container->setVisible(page == owner_->activePage());
                }
            }
        }

        // --- The page strip (session-tag="pages") ---
        if (auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(view)) {
            const std::string* session = attributes.getAttributeValue(kSessionTagAttribute);
            if (session != nullptr && *session == kPageStripSessionName) {
                pageStrip_ = segment;
                segment->setTag(kPageStripTag);
                segment->setListener(this);
                // Fires valueChanged -> setActivePage(same page) + applyPage; the
                // begin/end edits it brackets are swallowed as session tags.
                segment->setSelectedSegment(static_cast<std::uint32_t>(owner_->activePage()));
            }
        }

        if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
            // --- The header preset button (OutlineBrowserButton, plan D-1) ---
            const std::string* customName = attributes.getAttributeValue(kCustomViewNameAttribute);
            if (customName != nullptr && *customName == kPresetButtonViewName) {
                control->setTag(kPresetButtonTag);
                control->setListener(this);
            }

            // --- The Gravity macro knob (FR-080) ---
            if (control->getTag() == static_cast<std::int32_t>(kMacroGravityId)) {
                if (auto* knob = dynamic_cast<Krate::Plugins::ArcKnob*>(control)) {
                    owner_->registerGravityViews(knob, nullptr);
                }
            }
        }

        // --- The Gravity macro label (FR-080) ---
        if (label != nullptr && *label == kGravityLabelName) {
            if (auto* textLabel = dynamic_cast<VSTGUI::CTextLabel*>(view)) {
                owner_->registerGravityViews(nullptr, textLabel);
            }
        }
    }

    // DelegationController::verifyView dereferences its parent unconditionally.
    return (controller != nullptr)
               ? VSTGUI::DelegationController::verifyView(view, attributes, description)
               : view;
}

// ==============================================================================
// valueChanged - session controls handled here, everything else forwarded
// ==============================================================================
void VoragoPanelSubController::valueChanged(VSTGUI::CControl* control) {
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

    if (tag == kPageStripTag && control == pageStrip_) {
        owner_->setActivePage(static_cast<int>(pageStrip_->getSelectedSegment()));
        applyPage(owner_->activePage());
        return;
    }
    if (tag == kPresetButtonTag) {
        owner_->openPresetBrowser();
        return;
    }
    // Any other session tag: never forwarded (it is not a ParamID).
}

// ==============================================================================
// Gesture boundaries - session tags never reach EditController::beginEdit
// ==============================================================================
void VoragoPanelSubController::controlBeginEdit(VSTGUI::CControl* control) {
    if (control != nullptr && isSessionTag(control->getTag())) {
        return;
    }
    if (controller != nullptr) {
        VSTGUI::DelegationController::controlBeginEdit(control);
    }
}

void VoragoPanelSubController::controlEndEdit(VSTGUI::CControl* control) {
    if (control != nullptr && isSessionTag(control->getTag())) {
        return;
    }
    if (controller != nullptr) {
        VSTGUI::DelegationController::controlEndEdit(control);
    }
}

// ==============================================================================
// applyPage - visibility only (FR-056: no resize, no remove)
// ==============================================================================
void VoragoPanelSubController::applyPage(int page) noexcept {
    VSTGUI::CView* pageArea = nullptr;
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        auto* container = pages_[i];
        if (container == nullptr) {
            continue;
        }
        container->setVisible(std::cmp_equal(i, page));
        if (pageArea == nullptr) {
            pageArea = container->getParentView();
        }
    }
    if (pageArea != nullptr) {
        pageArea->invalid();
    }
}

}  // namespace Vorago::UI
