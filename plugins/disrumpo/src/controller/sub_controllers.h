#pragma once

// ==============================================================================
// Sub-Controllers for Band Parameter Remapping
// ==============================================================================
// VSTGUI sub-controllers that remap generic control-tag names to band-specific
// parameter IDs using Disrumpo's bit-encoded parameter scheme.
//
// Pattern: Template definitions use generic tags like "Band.DisplayedType".
//          Per-band wrapper templates set sub-controller="BandShapeTab0" etc.
//          createSubController() extracts the band index from the name suffix.
//          getTagForName() remaps generic names to actual parameter IDs.
//
// See: .claude/skills/vst-guide/UI-COMPONENTS.md (Sub-Controllers section)
// ==============================================================================

#include "plugin_ids.h"
#include "controller/views/mod_slider.h"
#include "vstgui/uidescription/delegationcontroller.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/iuidescription.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ccontrol.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace Disrumpo {

// ==============================================================================
// parseMenuItems: Parse comma-separated menu items from uidesc attribute
// ==============================================================================
// Used by BandSubController::verifyView() to populate COptionMenu controls
// in TypeParams templates. Items defined via custom "menu-items" attribute.
// ==============================================================================

inline std::vector<std::string> parseMenuItems(const std::string& itemsStr) {
    std::vector<std::string> items;
    if (itemsStr.empty())
        return items;
    std::istringstream stream(itemsStr);
    std::string item;
    while (std::getline(stream, item, ',')) {
        items.push_back(item);
    }
    return items;
}

// ==============================================================================
// BandSubController: Base class for band-specific parameter remapping
// ==============================================================================
// Stores band index and provides getTagForName() override that remaps
// generic "Band.*" control-tag names to band-specific parameter IDs.
// ==============================================================================

class BandSubController : public VSTGUI::DelegationController {
public:
    BandSubController(int bandIndex, VSTGUI::IController* parentController)
        : DelegationController(parentController)
        , bandIndex_(bandIndex) {
        assert(bandIndex >= 0 && bandIndex < 4);
    }

    int32_t getTagForName(VSTGUI::UTF8StringPtr name, int32_t registeredTag) const override {
        auto band = static_cast<uint8_t>(bandIndex_);

        // ====================================================================
        // Band-level parameter remapping
        // ====================================================================
        if (std::strcmp(name, "Band.DisplayedType") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandDisplayedType));

        if (std::strcmp(name, "Band.Gain") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandGain));

        if (std::strcmp(name, "Band.Pan") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandPan));

        if (std::strcmp(name, "Band.Solo") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandSolo));

        if (std::strcmp(name, "Band.Bypass") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandBypass));

        if (std::strcmp(name, "Band.Mute") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandMute));

        if (std::strcmp(name, "Band.Expanded") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandExpanded));

        if (std::strcmp(name, "Band.ActiveNodes") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandActiveNodes));

        if (std::strcmp(name, "Band.MorphSmoothing") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandMorphSmoothing));

        if (std::strcmp(name, "Band.MorphX") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandMorphX));

        if (std::strcmp(name, "Band.MorphY") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandMorphY));

        if (std::strcmp(name, "Band.MorphMode") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandMorphMode));

        if (std::strcmp(name, "Band.MorphXLink") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandMorphXLink));

        if (std::strcmp(name, "Band.MorphYLink") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandMorphYLink));

        if (std::strcmp(name, "Band.SelectedNode") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandSelectedNode));

        if (std::strcmp(name, "Band.TabView") == 0)
            return static_cast<int32_t>(makeBandParamId(band, BandParamType::kBandTabView));

        // ====================================================================
        // UI-only visibility tag remapping
        // ====================================================================
        if (std::strcmp(name, "Band.ExpandedContainer") == 0)
            return 9100 + bandIndex_;

        // ====================================================================
        // Node-level parameter remapping (Node 0 = selected node's display)
        // ====================================================================
        if (std::strcmp(name, "Band.NodeDrive") == 0)
            return static_cast<int32_t>(makeNodeParamId(band, 0, NodeParamType::kNodeDrive));

        if (std::strcmp(name, "Band.NodeMix") == 0)
            return static_cast<int32_t>(makeNodeParamId(band, 0, NodeParamType::kNodeMix));

        if (std::strcmp(name, "Band.NodeTone") == 0)
            return static_cast<int32_t>(makeNodeParamId(band, 0, NodeParamType::kNodeTone));

        if (std::strcmp(name, "Band.NodeBias") == 0)
            return static_cast<int32_t>(makeNodeParamId(band, 0, NodeParamType::kNodeBias));

        // ====================================================================
        // Shape slot parameter remapping (generic per-type controls)
        // ====================================================================
        {
            char tagName[32];
            for (int s = 0; s < 10; ++s) {
                std::snprintf(tagName, sizeof(tagName), "Band.NodeShape%d", s);
                if (std::strcmp(name, tagName) == 0) {
                    auto shapeType = static_cast<NodeParamType>(
                        static_cast<uint8_t>(NodeParamType::kNodeShape0) + s);
                    return static_cast<int32_t>(makeNodeParamId(band, 0, shapeType));
                }
            }
        }

        // Delegate to parent controller for unrecognized names
        return DelegationController::getTagForName(name, registeredTag);
    }

    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                               const VSTGUI::IUIDescription* description) override {
        // Set modulation destination ID on ModSlider instances
        if (auto* modSlider = dynamic_cast<ModSlider*>(view)) {
            const auto* tagName = attributes.getAttributeValue("control-tag");
            if (tagName) {
                if (*tagName == "Band.NodeDrive")
                    modSlider->setModDestId(
                        ModDest::bandParam(bandIndex_, ModDest::kBandDrive));
                else if (*tagName == "Band.NodeMix")
                    modSlider->setModDestId(
                        ModDest::bandParam(bandIndex_, ModDest::kBandMix));
            }
        }

        // Populate COptionMenu controls from custom "menu-items" attribute.
        // Shape slot parameters are RangeParameter(stepCount=0), so VSTGUI's
        // auto-populate in ParameterChangeListener::updateControlValue() is
        // skipped (it's gated by stepCount > 0). Our entries survive.
        if (auto* menu = dynamic_cast<VSTGUI::COptionMenu*>(view)) {
            const auto* menuItemsAttr = attributes.getAttributeValue("menu-items");
            if (menuItemsAttr && !menuItemsAttr->empty()) {
                auto items = parseMenuItems(*menuItemsAttr);
                for (const auto& item : items) {
                    menu->addEntry(item.c_str());
                }
            }
        }

        return DelegationController::verifyView(view, attributes, description);
    }

    /// @brief Inject band index into custom view attributes.
    /// Custom views (DynamicNodeSelector, MorphPad, etc.) read a "band" attribute
    /// to determine which band's parameters to wire up. This override creates a
    /// modified copy of the UIAttributes with the correct band index injected.
    VSTGUI::CView* createView(const VSTGUI::UIAttributes& attributes,
                               const VSTGUI::IUIDescription* description) override {
        const auto* customViewName = attributes.getAttributeValue("custom-view-name");
        if (customViewName) {
            auto* modified = new VSTGUI::UIAttributes(attributes.empty() ? 0 : 16);
            for (auto it = attributes.begin(); it != attributes.end(); ++it) {
                modified->setAttribute(it->first, it->second);
            }
            modified->setAttribute("band", std::to_string(bandIndex_));
            auto* view = controller->createView(*modified, description);
            modified->forget();
            return view;
        }
        return DelegationController::createView(attributes, description);
    }

    // Dispatch nested sub-controllers (e.g., BitwiseOp for TypeParams_Bitwise)
    VSTGUI::IController* createSubController(VSTGUI::UTF8StringPtr name,
                                              const VSTGUI::IUIDescription* description) override;

protected:
    int bandIndex_;
};

// ==============================================================================
// BandExpandedStripController: Sub-controller for expanded band strip
// ==============================================================================
// Extends BandSubController with two additional overrides:
// - createView(): Injects the correct band index into custom view attributes
//   (MorphPad, DynamicNodeSelector, NodeEditorBorder read "band" from XML)
// - verifyView(): Updates the "Band 1" title label text and color per band
// ==============================================================================

class BandExpandedStripController : public BandSubController {
public:
    using BandSubController::BandSubController;

    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                               const VSTGUI::IUIDescription* description) override {
        // Update "Band 1" placeholder title with correct band number and color
        if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(view)) {
            if (label->getText() == "Band 1") {
                std::string title = "Band " + std::to_string(bandIndex_ + 1);
                label->setText(VSTGUI::UTF8String(title.c_str()));
                VSTGUI::CColor bandColor;
                std::string colorName = "band-" + std::to_string(bandIndex_ + 1);
                if (description->getColor(colorName.c_str(), bandColor)) {
                    label->setFontColor(bandColor);
                }
            }
        }

        return BandSubController::verifyView(view, attributes, description);
    }
};

// ==============================================================================
// BitwiseOpController: Conditional visibility for Bitwise Mangler controls
// ==============================================================================
// Sub-controller for TypeParams_Bitwise template. Watches the Op dropdown
// (Band.NodeShape0) and shows/hides Pattern and Bits containers based on
// which operation is selected:
//   - XorPattern (0): show Pattern
//   - BitRotate (2):  show Bits
//   - BitShuffle (3): show Pattern (labeled "Seed")
//   - Others:         hide both
// ==============================================================================

class BitwiseOpController : public VSTGUI::DelegationController {
public:
    BitwiseOpController(VSTGUI::IController* parentController)
        : DelegationController(parentController) {}

    ~BitwiseOpController() override {
        if (opControl_)
            opControl_->unregisterControlListener(this);
    }

    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                               const VSTGUI::IUIDescription* description) override {
        // Detect the Op COptionMenu by its control-tag
        const auto* tagName = attributes.getAttributeValue("control-tag");
        if (tagName && *tagName == "Band.NodeShape0") {
            if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
                opControl_ = control;
                opControl_->registerControlListener(this);
            }
        }

        // Detect Pattern and Bits wrapper containers by custom attribute
        const auto* visGroup = attributes.getAttributeValue("bitwise-group");
        if (visGroup) {
            if (auto* container = view->asViewContainer()) {
                if (*visGroup == "pattern")
                    patternContainer_ = container;
                else if (*visGroup == "bits")
                    bitsContainer_ = container;
            }
        }

        auto* result = DelegationController::verifyView(view, attributes, description);

        // Set initial visibility once all pieces are found
        if (opControl_ && patternContainer_ && bitsContainer_ && !initialVisibilitySet_) {
            initialVisibilitySet_ = true;
            updateVisibility();
        }

        return result;
    }

    void valueChanged(VSTGUI::CControl* control) override {
        if (control == opControl_) {
            updateVisibility();
        }
        DelegationController::valueChanged(control);
    }

private:
    void updateVisibility() {
        if (!opControl_) return;

        // Op dropdown: 6 items → normalized 0/5, 1/5, 2/5, 3/5, 4/5, 5/5
        float norm = opControl_->getValueNormalized();
        int op = static_cast<int>(norm * 5.0f + 0.5f);

        // Pattern visible for XorPattern(0) and BitShuffle(3)
        bool showPattern = (op == 0 || op == 3);
        // Bits visible for BitRotate(2)
        bool showBits = (op == 2);

        if (patternContainer_)
            patternContainer_->setVisible(showPattern);
        if (bitsContainer_)
            bitsContainer_->setVisible(showBits);
    }

    VSTGUI::CControl* opControl_ = nullptr;
    VSTGUI::CViewContainer* patternContainer_ = nullptr;
    VSTGUI::CViewContainer* bitsContainer_ = nullptr;
    bool initialVisibilitySet_ = false;
};

// ==============================================================================
// TemporalModeController: Conditional visibility for Hysteresis-only controls
// ==============================================================================
// Sub-controller for TypeParams_Temporal template. Watches the Mode dropdown
// (Band.NodeShape0) and shows/hides hysteresis-specific controls (Depth, Hold)
// based on which mode is selected:
//   - Hysteresis (3): show Depth and Hold
//   - Others (0-2):   hide them
// ==============================================================================

class TemporalModeController : public VSTGUI::DelegationController {
public:
    TemporalModeController(VSTGUI::IController* parentController)
        : DelegationController(parentController) {}

    ~TemporalModeController() override {
        if (modeControl_)
            modeControl_->unregisterControlListener(this);
    }

    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                               const VSTGUI::IUIDescription* description) override {
        // Detect the Mode COptionMenu by its control-tag
        const auto* tagName = attributes.getAttributeValue("control-tag");
        if (tagName && *tagName == "Band.NodeShape0") {
            if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
                modeControl_ = control;
                modeControl_->registerControlListener(this);
            }
        }

        // Detect hysteresis wrapper container by custom attribute
        const auto* visGroup = attributes.getAttributeValue("temporal-group");
        if (visGroup && *visGroup == "hysteresis") {
            if (auto* container = view->asViewContainer()) {
                hystContainer_ = container;
            }
        }

        auto* result = DelegationController::verifyView(view, attributes, description);

        // Set initial visibility once all pieces are found
        if (modeControl_ && hystContainer_ && !initialVisibilitySet_) {
            initialVisibilitySet_ = true;
            updateVisibility();
        }

        return result;
    }

    void valueChanged(VSTGUI::CControl* control) override {
        if (control == modeControl_) {
            updateVisibility();
        }
        DelegationController::valueChanged(control);
    }

private:
    void updateVisibility() {
        if (!modeControl_) return;

        // Mode dropdown: 4 items → normalized 0/3, 1/3, 2/3, 3/3
        float norm = modeControl_->getValueNormalized();
        int mode = static_cast<int>(norm * 3.0f + 0.5f);

        // Hysteresis is mode 3
        bool showHyst = (mode == 3);

        if (hystContainer_)
            hystContainer_->setVisible(showHyst);
    }

    VSTGUI::CControl* modeControl_ = nullptr;
    VSTGUI::CViewContainer* hystContainer_ = nullptr;
    bool initialVisibilitySet_ = false;
};

// ==============================================================================
// RingSatFreqModeController: Conditional visibility for Hz/Ratio knob
// ==============================================================================
// Sub-controller for TypeParams_RingSat template. Watches the Freq mode dropdown
// (Band.NodeShape5) and shows/hides the Hz/Ratio knob container based on mode:
//   - Fixed (0) or Harmonic (1): show Hz/Ratio knob
//   - Track (2) or Random (3):   hide it
// ==============================================================================

class RingSatFreqModeController : public VSTGUI::DelegationController {
public:
    RingSatFreqModeController(VSTGUI::IController* parentController)
        : DelegationController(parentController) {}

    ~RingSatFreqModeController() override {
        if (freqModeControl_)
            freqModeControl_->unregisterControlListener(this);
    }

    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                               const VSTGUI::IUIDescription* description) override {
        // Detect the Freq mode COptionMenu by its control-tag
        const auto* tagName = attributes.getAttributeValue("control-tag");
        if (tagName && *tagName == "Band.NodeShape5") {
            if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
                freqModeControl_ = control;
                freqModeControl_->registerControlListener(this);
            }
        }

        // Detect Hz/Ratio wrapper container by custom attribute
        const auto* visGroup = attributes.getAttributeValue("rs-freq-group");
        if (visGroup && *visGroup == "hz-ratio") {
            if (auto* container = view->asViewContainer()) {
                hzRatioContainer_ = container;
            }
        }

        auto* result = DelegationController::verifyView(view, attributes, description);

        // Set initial visibility once all pieces are found
        if (freqModeControl_ && hzRatioContainer_ && !initialVisibilitySet_) {
            initialVisibilitySet_ = true;
            updateVisibility();
        }

        return result;
    }

    void valueChanged(VSTGUI::CControl* control) override {
        if (control == freqModeControl_) {
            updateVisibility();
        }
        DelegationController::valueChanged(control);
    }

private:
    void updateVisibility() {
        if (!freqModeControl_) return;

        // Freq mode dropdown: 4 items → normalized 0/3, 1/3, 2/3, 3/3
        float norm = freqModeControl_->getValueNormalized();
        int mode = static_cast<int>(norm * 3.0f + 0.5f);

        // Show Hz/Ratio knob for Fixed (0) and Harmonic (1); hide for Track (2) and Random (3)
        bool showKnob = (mode <= 1);

        if (hzRatioContainer_)
            hzRatioContainer_->setVisible(showKnob);
    }

    VSTGUI::CControl* freqModeControl_ = nullptr;
    VSTGUI::CViewContainer* hzRatioContainer_ = nullptr;
    bool initialVisibilitySet_ = false;
};

// ---------------------------------------------------------------------------
// FractalModeController — shows/hides FB (Feedback mode) and Depth (Multiband mode)
// ---------------------------------------------------------------------------
class FractalModeController : public VSTGUI::DelegationController {
public:
    explicit FractalModeController(VSTGUI::IController* parentController)
        : DelegationController(parentController) {}

    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                               [[maybe_unused]] const VSTGUI::IUIDescription* description) override {
        // Detect the Mode dropdown (Band.NodeShape0)
        const auto* tagName = attributes.getAttributeValue("control-tag");
        if (tagName && *tagName == "Band.NodeShape0") {
            if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
                modeControl_ = control;
                modeControl_->registerControlListener(this);
            }
        }

        // Detect visibility containers by custom attribute
        const auto* group = attributes.getAttributeValue("fractal-group");
        if (group) {
            if (auto* container = view->asViewContainer()) {
                if (*group == "feedback")
                    feedbackContainer_ = container;
                else if (*group == "depth")
                    depthContainer_ = container;
            }
        }

        auto* result = DelegationController::verifyView(view, attributes, description);

        // Set initial visibility once all pieces are found
        if (modeControl_ && feedbackContainer_ && depthContainer_ && !initialVisibilitySet_) {
            initialVisibilitySet_ = true;
            updateVisibility();
        }
        return result;
    }

    void valueChanged(VSTGUI::CControl* control) override {
        if (control == modeControl_) {
            updateVisibility();
        }
        DelegationController::valueChanged(control);
    }

private:
    void updateVisibility() {
        if (!modeControl_) return;

        // Mode dropdown: 5 items → normalized 0/4, 1/4, 2/4, 3/4, 4/4
        // Residual=0, Multiband=1, Harmonic=2, Cascade=3, Feedback=4
        float norm = modeControl_->getValueNormalized();
        int mode = static_cast<int>(norm * 4.0f + 0.5f);

        if (feedbackContainer_)
            feedbackContainer_->setVisible(mode == 4);  // Feedback only
        if (depthContainer_)
            depthContainer_->setVisible(mode == 1);     // Multiband only
    }

    VSTGUI::CControl* modeControl_ = nullptr;
    VSTGUI::CViewContainer* feedbackContainer_ = nullptr;
    VSTGUI::CViewContainer* depthContainer_ = nullptr;
    bool initialVisibilitySet_ = false;
};

// Inline definition of BandSubController::createSubController
inline VSTGUI::IController* BandSubController::createSubController(
    VSTGUI::UTF8StringPtr name, const VSTGUI::IUIDescription* description) {
    if (std::strcmp(name, "BitwiseOp") == 0) {
        // Pass 'this' as parent so tag remapping is preserved
        return new BitwiseOpController(this);
    }
    if (std::strcmp(name, "TemporalMode") == 0) {
        return new TemporalModeController(this);
    }
    if (std::strcmp(name, "RingSatFreqMode") == 0) {
        return new RingSatFreqModeController(this);
    }
    if (std::strcmp(name, "FractalMode") == 0) {
        return new FractalModeController(this);
    }
    return DelegationController::createSubController(name, description);
}

} // namespace Disrumpo
