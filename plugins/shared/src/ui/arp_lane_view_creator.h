#pragma once

// ==============================================================================
// ArpLaneViewCreator<LaneT> - Shared ViewCreator Template for Arp Lanes
// ==============================================================================
// Every per-step arp lane (chord, inversion, condition, modifier, ...) registered
// a near-identical VSTGUI ViewCreator. The only differences across lanes are:
//   - the uidesc view name and editor display name, and
//   - the *step base param* attribute name + its setter, which legitimately
//     diverge per lane:
//       enum popup lanes : "step-base-param-id"           -> setStepBaseId()
//       condition lane   : "step-condition-base-param-id" -> setStepBaseId()
//       modifier lane    : "step-flag-base-param-id"      -> setStepBaseId()
// This template preserves those exact attribute names while collapsing the
// boilerplate to one instantiation per lane.
//
// LaneT requirements:
//   - derives from VSTGUI::CControl and IArpLane,
//     constructible as LaneT(const CRect&, IControlListener*, int32)
//   - static constexpr const char* kViewName       // uidesc view name
//   - static constexpr const char* kDisplayName    // editor display name
//   - static constexpr const char* kStepBaseAttr   // step-base attribute name
//   - void setStepBaseId(uint32_t)                 // uniform step-base setter
//                                                  // (thin forwarder per lane)
//   - setAccentColor(CColor) / getAccentColor() const
//   - setLaneName(const std::string&)
//   - setLengthParamId(uint32_t) / setPlayheadParamId(uint32_t)
//
// Common attribute set: accent-color, lane-name, <kStepBaseAttr>,
// length-param-id, playhead-param-id.
//
// Location: plugins/shared/src/ui/arp_lane_view_creator.h
// ==============================================================================

#include "arp_lane.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <cstdint>
#include <string>

namespace Krate::Plugins {

template <class LaneT>
struct ArpLaneViewCreator : VSTGUI::ViewCreatorAdapter {
    ArpLaneViewCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return LaneT::kViewName; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override { return LaneT::kDisplayName; }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new LaneT(VSTGUI::CRect(0, 0, 500, 44), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* lane = dynamic_cast<LaneT*>(view);
        if (!lane) return false;

        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("accent-color"), color, description))
            lane->setAccentColor(color);

        const auto* nameStr = attributes.getAttributeValue("lane-name");
        if (nameStr) lane->setLaneName(*nameStr);

        const auto* baseIdStr = attributes.getAttributeValue(LaneT::kStepBaseAttr);
        if (baseIdStr)
            lane->setStepBaseId(static_cast<uint32_t>(std::stoul(*baseIdStr)));

        const auto* lengthIdStr = attributes.getAttributeValue("length-param-id");
        if (lengthIdStr)
            lane->setLengthParamId(static_cast<uint32_t>(std::stoul(*lengthIdStr)));

        const auto* playheadIdStr = attributes.getAttributeValue("playhead-param-id");
        if (playheadIdStr)
            lane->setPlayheadParamId(static_cast<uint32_t>(std::stoul(*playheadIdStr)));

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("accent-color");
        attributeNames.emplace_back("lane-name");
        attributeNames.emplace_back(LaneT::kStepBaseAttr);
        attributeNames.emplace_back("length-param-id");
        attributeNames.emplace_back("playhead-param-id");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "accent-color") return kColorType;
        if (attributeName == "lane-name") return kStringType;
        if (attributeName == LaneT::kStepBaseAttr) return kStringType;
        if (attributeName == "length-param-id") return kStringType;
        if (attributeName == "playhead-param-id") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* lane = dynamic_cast<LaneT*>(view);
        if (!lane) return false;

        if (attributeName == "accent-color") {
            VSTGUI::UIViewCreator::colorToString(
                lane->getAccentColor(), stringValue, desc);
            return true;
        }
        return false;
    }
};

} // namespace Krate::Plugins
