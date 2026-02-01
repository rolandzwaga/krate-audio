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
#include "vstgui/lib/controls/ctextlabel.h"

#include <cassert>
#include <cstring>
#include <string>

namespace Disrumpo {

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
        return DelegationController::verifyView(view, attributes, description);
    }

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

    VSTGUI::CView* createView(const VSTGUI::UIAttributes& attributes,
                               const VSTGUI::IUIDescription* description) override {
        const auto* customViewName = attributes.getAttributeValue("custom-view-name");
        if (customViewName) {
            // Custom views read "band" attribute to determine which band's parameters
            // to wire up. Create new attributes with the correct band index injected.
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

} // namespace Disrumpo
