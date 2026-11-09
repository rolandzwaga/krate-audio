#pragma once

// =============================================================================
// Disrumpo headless editor (test support, header-only)
// =============================================================================
// The real editor (resources/editor.uidesc) opened on a controller without a
// host window: attached() runs createCustomView / verifyView / didOpen exactly
// as a host open does. VSTGUI is initialised here, so a case using it passes
// when run alone. Plus the view-tree lookups several editor tests share.
// =============================================================================

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"

#include <cstdint>
#include <string>

namespace Disrumpo::TestSupport {

/// Editor opened headlessly, closed (removed -> willClose) by close() or on destruction.
class HeadlessEditor {
public:
    explicit HeadlessEditor(Controller& c) {
        Krate::TestSupport::ensureVstguiInitialized();
        editor_ = new VSTGUI::VST3Editor(&c, "editor",
                                         (std::string(DISRUMPO_RESOURCES_DIR) + "/editor.uidesc").c_str());
        Steinberg::IPlugView* view = editor_;
        opened_ = view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue &&
                  editor_->getFrame() != nullptr;
    }
    ~HeadlessEditor() { close(); }
    HeadlessEditor(const HeadlessEditor&) = delete;
    HeadlessEditor& operator=(const HeadlessEditor&) = delete;
    HeadlessEditor(HeadlessEditor&&) = delete;
    HeadlessEditor& operator=(HeadlessEditor&&) = delete;

    void close() {
        if (editor_ == nullptr) return;
        Steinberg::IPlugView* view = editor_;
        view->removed();  // -> willClose
        view->release();
        editor_ = nullptr;
    }

    [[nodiscard]] bool ready() const { return opened_; }
    [[nodiscard]] VSTGUI::CFrame* frame() const { return editor_ != nullptr ? editor_->getFrame() : nullptr; }

private:
    VSTGUI::VST3Editor* editor_ = nullptr;
    bool opened_ = false;
};

/// First UIViewSwitchContainer of the given height below `container`, in document order.
inline VSTGUI::UIViewSwitchContainer* findSwitch(VSTGUI::CViewContainer* container, VSTGUI::CCoord height) {
    VSTGUI::UIViewSwitchContainer* found = nullptr;
    container->forEachChild([&found, height](VSTGUI::CView* child) {
        if (found) return;
        if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(child)) {
            if (sw->getViewSize().getHeight() == height) {
                found = sw;
                return;
            }
        }
        if (auto* sub = child->asViewContainer()) found = findSwitch(sub, height);
    });
    return found;
}

/// Container holding the control tagged `tag` (the lookup the visibility and expand controllers use).
inline VSTGUI::CViewContainer* findContainerHoldingTag(VSTGUI::CViewContainer* container, int32_t tag) {
    VSTGUI::CViewContainer* found = nullptr;
    container->forEachChild([&found, container, tag](VSTGUI::CView* child) {
        if (found) return;
        if (const auto* ctrl = dynamic_cast<const VSTGUI::CControl*>(child); ctrl != nullptr && ctrl->getTag() == tag) {
            found = container;
            return;
        }
        if (auto* sub = child->asViewContainer()) found = findContainerHoldingTag(sub, tag);
    });
    return found;
}

}  // namespace Disrumpo::TestSupport
