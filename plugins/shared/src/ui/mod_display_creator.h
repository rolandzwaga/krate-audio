#pragma once

// ==============================================================================
// ModDisplayCreator - Template ViewCreator for mod display views
// ==============================================================================
// Eliminates boilerplate ViewCreator registration code. All mod displays follow
// the same pattern: single color attribute, CView base, dynamic_cast to ViewT.
//
// Usage:
//   inline ModDisplayCreator<MyDisplay> gMyDisplayCreator{
//       "MyDisplay", "My Display", "my-color", {0, 0, 510, 230}};
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"

#include <string>

namespace Krate::Plugins {

template <typename ViewT>
class ModDisplayCreator : public VSTGUI::ViewCreatorAdapter {
public:
    ModDisplayCreator(const char* viewName, const char* displayName,
                      const char* colorAttr, VSTGUI::CRect defaultSize)
        : viewName_(viewName), displayName_(displayName)
        , colorAttr_(colorAttr), defaultSize_(defaultSize) {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return viewName_; }
    VSTGUI::IdStringPtr getBaseViewName() const override { return VSTGUI::UIViewCreator::kCView; }
    VSTGUI::UTF8StringPtr getDisplayName() const override { return displayName_; }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ViewT(defaultSize_);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attrs,
               const VSTGUI::IUIDescription* /*description*/) const override {
        auto* display = dynamic_cast<ViewT*>(view);
        if (!display) return false;
        if (auto colorStr = attrs.getAttributeValue(colorAttr_))
            display->setColorFromString(*colorStr);
        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& names) const override {
        names.emplace_back(colorAttr_);
        return true;
    }

    AttrType getAttributeType(const std::string& name) const override {
        if (name == colorAttr_) return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& name,
                           std::string& value,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        auto* display = dynamic_cast<ViewT*>(view);
        if (!display) return false;
        if (name == colorAttr_) {
            value = display->getColorString();
            return true;
        }
        return false;
    }

private:
    const char* viewName_;
    const char* displayName_;
    const char* colorAttr_;
    VSTGUI::CRect defaultSize_;
};

} // namespace Krate::Plugins
