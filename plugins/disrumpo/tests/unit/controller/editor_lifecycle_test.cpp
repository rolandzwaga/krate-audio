// ==============================================================================
// Disrumpo Editor Lifecycle Test
// ==============================================================================
// Exercises the full editor open -> close lifecycle headlessly via the shared
// harness, guarding against use-after-free / dangling-pointer crashes in the
// VST3EditorDelegate teardown path (verifyView/didOpen/willClose). Run under
// AddressSanitizer or valgrind to catch UAF regressions as hard failures.

#include <editor_lifecycle_harness.h>
#include <shared_editor_views_probe.h>

#include "controller/controller.h"
#include "controller/views/dynamic_node_selector.h"

#include "base/source/updatehandler.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/iviewlistener.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

/// Records the address of every view it is registered on when that view is deleted.
class DeleteRecorder : public VSTGUI::ViewListenerAdapter {
public:
    void viewWillDelete(VSTGUI::CView* view) override {
        deleted.insert(view);
        view->unregisterViewListener(this);
    }
    std::set<const VSTGUI::CView*> deleted;
};

/// Disrumpo controller that watches every DynamicNodeSelector it builds for deletion.
class SelectorTrackingController : public Disrumpo::Controller {
public:
    VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
                                    const VSTGUI::IUIDescription* description,
                                    VSTGUI::VST3Editor* editor) override {
        VSTGUI::CView* view = Controller::createCustomView(name, attributes, description, editor);
        if (dynamic_cast<Disrumpo::DynamicNodeSelector*>(view) != nullptr) {
            const std::string* autoHide = attributes.getAttributeValue("auto-hide-single");
            (autoHide != nullptr && *autoHide == "true" ? compactCreated : expandedCreated).push_back(view);
            recorder.deleted.erase(view);  // a fresh view may reuse a freed address
            view->registerViewListener(&recorder);
        }
        return view;
    }

    /// Cached selector pointers whose view has already been deleted.
    [[nodiscard]] size_t danglingCachedSelectors() const {
        const auto cached = cachedNodeSelectorsForTest();
        return static_cast<size_t>(std::count_if(cached.begin(), cached.end(), [this](const VSTGUI::CView* v) {
            return recorder.deleted.contains(v);
        }));
    }

    [[nodiscard]] bool caches(const VSTGUI::CView* view) const {
        const auto cached = cachedNodeSelectorsForTest();
        return std::find(cached.begin(), cached.end(), view) != cached.end();
    }

    DeleteRecorder recorder;  // every watched view dies with the editor, before the controller
    std::vector<VSTGUI::CView*> compactCreated;
    std::vector<VSTGUI::CView*> expandedCreated;
};

constexpr VSTGUI::CCoord kTabSwitchHeight = 243.0;  // Band<N>MainTab / Band<N>ShapeTab switch

/// First UIViewSwitchContainer of the given height below `container`, in document order.
VSTGUI::UIViewSwitchContainer* findSwitch(VSTGUI::CViewContainer* container, VSTGUI::CCoord height) {
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

/// Container holding `view` below `container` (the headless frame is unattached, so getParentView() is null).
VSTGUI::CViewContainer* findParentOf(VSTGUI::CViewContainer* container, const VSTGUI::CView* view) {
    VSTGUI::CViewContainer* found = nullptr;
    container->forEachChild([&found, container, view](VSTGUI::CView* child) {
        if (found) return;
        if (child == view) {
            found = container;
            return;
        }
        if (auto* sub = child->asViewContainer()) found = findParentOf(sub, view);
    });
    return found;
}

/// Editor opened headlessly, closed (willClose) by close() or on destruction.
class HeadlessEditor {
public:
    explicit HeadlessEditor(Disrumpo::Controller& c) {
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

}  // namespace

TEST_CASE("Disrumpo editor open/close cycle is UAF- and leak-free",
          "[disrumpo][controller][ui][lifecycle]")
{
    Disrumpo::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // Every addDependent made while the editor is open (NodeSelectionController,
    // MorphPad, DynamicNodeSelector, ...) must be undone when it closes.
    const size_t dependentsBefore = Steinberg::UpdateHandler::instance()->countDependencies();

    Krate::TestSupport::exerciseEditorLifecycle(
        controller, "editor",
        std::string(DISRUMPO_RESOURCES_DIR) + "/editor.uidesc",
        /*cycles=*/3);

    const size_t dependentsAfter = Steinberg::UpdateHandler::instance()->countDependencies();
    std::printf("Disrumpo editor lifecycle: dependents before %zu, after 3 open/close cycles %zu\n",
                dependentsBefore, dependentsAfter);
    CHECK(dependentsAfter == dependentsBefore);

    controller.terminate();
}

// Every Krate editor carries the shared views createSharedEditorView builds: one
// update banner, one Check for Updates button and one VersionLabel (VERSION_STR),
// and no hand-filled tag-9999 version label. tools/lint-plugin-editor-baseline.js
// requires this case in every plugin.
TEST_CASE("Disrumpo editor carries the shared update and version views",
          "[disrumpo][controller][ui][version]")
{
    ::Disrumpo::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto count = Krate::TestSupport::openEditorAndCountSharedViews(
        controller, "editor", std::string(DISRUMPO_RESOURCES_DIR) + "/editor.uidesc");
    CHECK(count.banners == 1);
    CHECK(count.checkButtons == 1);
    CHECK(count.versionLabels == 1);
    CHECK(count.legacyTagged == 0);

    controller.terminate();
}

// D-061: the compact (auto-hide) node selector lives on band N's Shape page, a
// page of a UIViewSwitchContainer that frees it on every switch back to Main.
// The headless frame is never attached, so the switch is stepped by hand
// (setCurrentViewIndex), which runs the same removeAll/addView path as a click
// on the Main/Shape tab. The controller must not keep a pointer to the freed
// selector, or willClose deactivates freed memory.
TEST_CASE("Main/Shape tab switch then close does not touch freed selectors", "[disrumpo][lifecycle][D-061]")
{
    SelectorTrackingController c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        auto* tabSwitch = findSwitch(ed.frame(), kTabSwitchHeight);  // band 1 strip comes first
        REQUIRE(tabSwitch != nullptr);

        tabSwitch->setCurrentViewIndex(1);  // Shape page: builds the compact selector
        REQUIRE(c.compactCreated.size() == 1);
        const VSTGUI::CView* compact = c.compactCreated.front();
        REQUIRE_FALSE(c.recorder.deleted.contains(compact));

        tabSwitch->setCurrentViewIndex(0);  // back to Main: the Shape page and its selector are freed
        REQUIRE(c.recorder.deleted.contains(compact));

        const size_t dangling = c.danglingCachedSelectors();
        std::printf("T1.21 D-061 after Shape->Main: compact selector deleted, controller caches it %s; "
                    "%zu cached selector pointer(s) to deleted views (want 0)\n",
                    c.caches(compact) ? "YES" : "no", dangling);
        CHECK_FALSE(c.caches(compact));
        CHECK(dangling == 0);

        ed.close();  // willClose: must touch no freed selector
    }
    CHECK(c.cachedNodeSelectorsForTest().empty());
    c.terminate();
}

// D-061 (guard for the cached expanded-strip selectors): if one of them is
// deleted while the editor is open, the controller's slot is cleared with it.
TEST_CASE("Deleting an expanded-strip node selector clears the controller's slot", "[disrumpo][lifecycle][D-061]")
{
    SelectorTrackingController c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        REQUIRE_FALSE(c.expandedCreated.empty());
        VSTGUI::CView* selector = c.expandedCreated.front();
        REQUIRE(c.caches(selector));
        auto* parent = findParentOf(ed.frame(), selector);
        REQUIRE(parent != nullptr);

        REQUIRE(parent->removeView(selector, true));
        REQUIRE(c.recorder.deleted.contains(selector));

        const size_t dangling = c.danglingCachedSelectors();
        std::printf("T1.21 D-061 after deleting an expanded selector: %zu cached selector pointer(s) to deleted "
                    "views (want 0)\n",
                    dangling);
        CHECK(dangling == 0);
    }
    CHECK(c.cachedNodeSelectorsForTest().empty());
    c.terminate();
}
