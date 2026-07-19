// ==============================================================================
// Mod-Source View Pointer Teardown Tests
// ==============================================================================
// The nine mod-source views (LFO1/LFO2 waveform, chaos, rungler, sample & hold,
// random, and the three sidechain indicator labels) are owned by the editor
// frame, but the controller caches raw pointers to them. Three separate paths
// destroy those views:
//
//   1. willClose()                      -- editor teardown          (T1.1 / F001)
//   2. MainTab switch away from MOD     -- onTabChanged()           (T2.2 / F003)
//   3. ModSourceViewMode switch         -- UIViewSwitchContainer    (T2.1 / F002)
//
// If any path fails to null the cached pointers they dangle. Every deref site is
// behind an `if (ptr)` guard, which a dangling non-null pointer passes -- so the
// next setComponentState() / syncAllViews() reads freed memory. "All pointers are
// null after teardown" is therefore the observable invariant these tests assert.
//
// The mod-source views only exist while the MOD tab is showing, and only one
// ModSource_* template is instantiated at a time, so each test must first drive
// the view switches to bring the pointers into existence -- otherwise it would
// assert "null after teardown" on pointers that were never captured, and pass
// vacuously.

#include <editor_lifecycle_harness.h>

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"

#include "controller/controller.h"
#include "plugin_ids.h"

#include <string>
#include <vector>

namespace {

std::string uidescPath() {
    return std::string(RUINAE_RESOURCES_DIR) + "/editor.uidesc";
}

#if SMTG_OS_WINDOWS
constexpr Steinberg::FIDString kPlatformType = Steinberg::kPlatformTypeHWND;
#elif SMTG_OS_MACOS
constexpr Steinberg::FIDString kPlatformType = Steinberg::kPlatformTypeNSView;
#else
constexpr Steinberg::FIDString kPlatformType = Steinberg::kPlatformTypeX11EmbedWindowID;
#endif

void collectSwitchContainers(VSTGUI::CViewContainer* parent,
                             std::vector<VSTGUI::UIViewSwitchContainer*>& out) {
    if (!parent) return;
    parent->forEachChild([&out](VSTGUI::CView* child) {
        if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(child)) {
            out.push_back(sw);
        }
        if (auto* container = child->asViewContainer()) {
            collectSwitchContainers(container, out);
        }
    });
}

// The tab-content switch spans the whole editor body; the mod-source switch is
// the 540-wide one nested inside the MOD tab template. Match on width so this
// does not depend on child ordering in the .uidesc.
VSTGUI::UIViewSwitchContainer* findSwitchByWidth(VSTGUI::CFrame* frame, VSTGUI::CCoord width) {
    std::vector<VSTGUI::UIViewSwitchContainer*> found;
    collectSwitchContainers(frame, found);
    for (auto* sw : found) {
        if (sw->getViewSize().getWidth() == width) return sw;
    }
    return nullptr;
}

constexpr VSTGUI::CCoord kMainTabSwitchWidth = 1400.0;
constexpr VSTGUI::CCoord kModSourceSwitchWidth = 540.0;
constexpr int kMainTabModIndex = 1;   // template-names="Tab_Sound,Tab_Mod,Tab_Fx,Tab_Seq"
constexpr int kModSourceTemplateCount = 10;

// Bring the mod-source views into existence and return the mod-source switch.
//
// Two levels of lazy instantiation are in play. The headless harness never gets a
// real platform window, so CFrame::open() fails and no container is ever
// "attached" -- which is what would normally drive a UIViewSwitchContainer to
// build its initial template. Both switches must therefore be stepped by hand:
// first the tab-content switch to Tab_Mod, then the (now-existing) nested
// mod-source switch to its first template. Each step calls createView(), which is
// what fires verifyView() and lets the controller cache its pointers.
VSTGUI::UIViewSwitchContainer* showModTab(VSTGUI::CFrame* frame) {
    auto* mainTabSwitch = findSwitchByWidth(frame, kMainTabSwitchWidth);
    REQUIRE(mainTabSwitch != nullptr);
    mainTabSwitch->setCurrentViewIndex(kMainTabModIndex);

    auto* modSourceSwitch = findSwitchByWidth(frame, kModSourceSwitchWidth);
    REQUIRE(modSourceSwitch != nullptr);
    modSourceSwitch->setCurrentViewIndex(0); // ModSource_LFO1
    return modSourceSwitch;
}

} // namespace

TEST_CASE("willClose nulls every cached mod-source view pointer",
          "[ruinae][controller][ui][lifecycle]")
{
    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    Krate::TestSupport::ensureVstguiInitialized();

    auto* editor = new VSTGUI::VST3Editor(&controller, "editor", uidescPath().c_str());
    Steinberg::IPlugView* view = editor;
    REQUIRE(view->attached(nullptr, kPlatformType) == Steinberg::kResultTrue);
    REQUIRE(editor->getFrame() != nullptr);

    showModTab(editor->getFrame());

    // Guard against a vacuous test: if the open path never captured any of these
    // pointers there is nothing for teardown to clear and a green result is a lie.
    REQUIRE(controller.modSourceViewPointerCount() > 0);

    view->removed(); // -> close() -> willClose()

    CHECK(controller.modSourceViewPointerCount() == 0);

    view->release();
    controller.terminate();
}
