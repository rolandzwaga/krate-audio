// ==============================================================================
// Disrumpo Editor Lifecycle Test
// ==============================================================================
// Exercises the full editor open -> close lifecycle headlessly via the shared
// harness, guarding against use-after-free / dangling-pointer crashes in the
// VST3EditorDelegate teardown path (verifyView/didOpen/willClose). Run under
// AddressSanitizer or valgrind to catch UAF regressions as hard failures.

#include <editor_lifecycle_harness.h>
#include <shared_editor_views_probe.h>

#include "../../test_support/disrumpo_rig.h"

#include "controller/controller.h"
#include "controller/spectrum_block.h"
#include "controller/views/dynamic_node_selector.h"
#include "controller/views/mod_slider.h"
#include "controller/views/spectrum_display.h"
#include "display/shared_display_bridge.h"
#include "plugin_ids.h"
#include "state_codec.h"

#include "base/source/updatehandler.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "public.sdk/source/common/memorystream.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/iviewlistener.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"

#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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

/// Every ModSlider below `container`, in document order.
void collectModSliders(VSTGUI::CViewContainer* container, std::vector<const Disrumpo::ModSlider*>& out) {
    container->forEachChild([&out](VSTGUI::CView* child) {
        if (const auto* ms = dynamic_cast<const Disrumpo::ModSlider*>(child)) out.push_back(ms);
        if (auto* sub = child->asViewContainer()) collectModSliders(sub, out);
    });
}

/// The instance ID a processor state stream ends with (int32 "KRAT" marker, int64 ID).
uint64_t trailerInstanceId(const std::vector<uint8_t>& stream) {
    REQUIRE(stream.size() >= 12);
    int32_t marker = 0;
    std::memcpy(&marker, stream.data() + stream.size() - 12, sizeof(marker));
    REQUIRE(marker == 0x4B524154);
    uint64_t id = 0;
    std::memcpy(&id, stream.data() + stream.size() - 8, sizeof(id));
    return id;
}

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

// D-045 / D-057 / D-070: the mod-slider overlay and the Tier-3 spectrum fallback
// must work in every editor session, not only the first. Mod offsets arrive as
// values inside a SpectrumBlock; the SharedDisplayBridge lookup is redone on open.
TEST_CASE("Mod overlay and Tier-3 spectrum survive close/reopen",
          "[disrumpo][controller][lifecycle][D-045][D-057][D-070]")
{
    // A live processor registered in the bridge, its shared FIFOs and rate written
    Disrumpo::TestSupport::Rig rig(48000.0, 512);
    const std::vector<float> in(4096, 0.25f);
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR);
    auto state = Disrumpo::TestSupport::getStateBytes(*rig.p);

    Disrumpo::Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    {
        auto* s = new Steinberg::MemoryStream(state.data(), static_cast<Steinberg::TSize>(state.size()));
        REQUIRE(c.setComponentState(s) == Steinberg::kResultOk);
        s->release();
    }

    // One block from the processor, once: offset of ModDest d = 0.01 * (d + 1)
    auto block = std::make_unique<Disrumpo::SpectrumBlock>();
    block->numSamples = 64;
    block->sampleRate = 48000.0f;
    for (uint32_t d = 0; d < Disrumpo::kSpectrumBlockModOffsets; ++d)
        block->modOffsets[d] = 0.01f * static_cast<float>(d + 1);
    Steinberg::Vst::DataExchangeBlock deb{
        .data = block.get(), .size = static_cast<Steinberg::uint32>(sizeof(Disrumpo::SpectrumBlock)), .blockID = 0};
    c.onDataExchangeBlocksReceived(0, 1, &deb, static_cast<Steinberg::TBool>(false));

    {
        HeadlessEditor first(c);
        REQUIRE(first.ready());
    }  // willClose
    {
        HeadlessEditor ed(c);  // didOpen again
        REQUIRE(ed.ready());
        // The unattached frame builds no switch page by itself: step band 1 to Shape
        // and back so its Main page (five ModSliders) exists
        auto* tabSwitch = findSwitch(ed.frame(), kTabSwitchHeight);
        REQUIRE(tabSwitch != nullptr);
        tabSwitch->setCurrentViewIndex(1);
        tabSwitch->setCurrentViewIndex(0);
        for (int t = 0; t < 12; ++t) c.tickEditorTimerForTest();

        std::vector<const Disrumpo::ModSlider*> sliders;
        collectModSliders(ed.frame(), sliders);
        size_t withOffset = 0;
        for (const auto* ms : sliders) {
            const float want = 0.01f * static_cast<float>(ms->getModDestId() + 1);
            if (ms->getModulationOffset() == Catch::Approx(want).margin(1e-6)) ++withOffset;
        }
        const auto* display = c.spectrumDisplayForTest();
        const bool analysing = display != nullptr && display->isAnalysisActive();
        std::printf("T2.19 reopen: %zu of %zu ModSliders show their offset (want all); "
                    "spectrum display %s, Tier-3 analysis active: %s (want yes)\n",
                    withOffset, sliders.size(), display != nullptr ? "present" : "absent",
                    analysing ? "yes" : "no");
        REQUIRE_FALSE(sliders.empty());
        CHECK(withOffset == sliders.size());
        CHECK(analysing);
    }
    c.terminate();
}

// D-056: a processor restoring a stream whose instance ID another live processor
// holds keeps its own ID, and terminating it never removes the other's entry.
TEST_CASE("Two processors restoring the same stream keep distinct bridge registrations",
          "[disrumpo][lifecycle][D-056]")
{
    auto& bridge = Krate::Plugins::SharedDisplayBridge::instance();

    auto a = std::make_unique<Disrumpo::Processor>();
    REQUIRE(a->initialize(nullptr) == Steinberg::kResultOk);
    const auto stream = Disrumpo::TestSupport::getStateBytes(*a);
    const uint64_t idA = trailerInstanceId(stream);
    void* const displayA = bridge.lookupInstance(idA);
    REQUIRE(displayA != nullptr);

    // Restoring its own stream changes nothing
    REQUIRE(Disrumpo::TestSupport::setStateBytes(*a, stream));
    CHECK(trailerInstanceId(Disrumpo::TestSupport::getStateBytes(*a)) == idA);
    CHECK(bridge.lookupInstance(idA) == displayA);

    // A second live processor restores the same stream (duplicated track)
    auto b = std::make_unique<Disrumpo::Processor>();
    REQUIRE(b->initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(Disrumpo::TestSupport::setStateBytes(*b, stream));
    const uint64_t idB = trailerInstanceId(Disrumpo::TestSupport::getStateBytes(*b));
    void* const displayB = bridge.lookupInstance(idB);
    const bool aKeptAfterRestore = bridge.lookupInstance(idA) == displayA;
    b->terminate();
    b.reset();
    const bool aKeptAfterTerminate = bridge.lookupInstance(idA) == displayA;
    std::printf("T2.19 D-056: id B %s id A; A's entry %s after B's restore, %s after B's terminate (want: differs, "
                "kept, kept)\n",
                idB == idA ? "EQUALS" : "differs from", aKeptAfterRestore ? "kept" : "LOST",
                aKeptAfterTerminate ? "kept" : "LOST");
    CHECK(idB != idA);
    CHECK(displayB != nullptr);
    CHECK(displayB != displayA);
    CHECK(aKeptAfterRestore);
    CHECK(aKeptAfterTerminate);

    // A stream whose ID no live processor holds is adopted (project reload)
    REQUIRE(a->terminate() == Steinberg::kResultOk);
    a.reset();
    CHECK(bridge.lookupInstance(idA) == nullptr);
    auto reloaded = std::make_unique<Disrumpo::Processor>();
    REQUIRE(reloaded->initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(Disrumpo::TestSupport::setStateBytes(*reloaded, stream));
    CHECK(trailerInstanceId(Disrumpo::TestSupport::getStateBytes(*reloaded)) == idA);
    CHECK(bridge.lookupInstance(idA) != nullptr);
    reloaded->terminate();
    CHECK(bridge.lookupInstance(idA) == nullptr);
}

// D-071: the spectrum dividers follow the crossover parameters while the editor
// is open, and a Band Count change does not replace them with a view-local spread.
TEST_CASE("setParamNormalized on a crossover moves the spectrum divider",
          "[disrumpo][controller][ui][D-071]")
{
    Disrumpo::Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    const auto bandCountId = Disrumpo::makeGlobalParamId(Disrumpo::GlobalParamType::kGlobalBandCount);
    c.setParamNormalized(bandCountId, 1.0);  // 4 bands
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        const auto* display = c.spectrumDisplayForTest();
        REQUIRE(display != nullptr);

        const float before = display->getCrossoverFrequency(0);
        c.setParamNormalized(Disrumpo::makeCrossoverParamId(0), Disrumpo::crossoverHzToNorm(1000.0f));
        const float after = display->getCrossoverFrequency(0);
        std::printf("D-071 crossover 0 param -> 1000 Hz: divider %.1f Hz before, %.1f Hz after\n",
                    static_cast<double>(before), static_cast<double>(after));
        CHECK(after == Catch::Approx(1000.0f).margin(1.0f));

        const float wantHz[3] = {300.0f, 1500.0f, 6000.0f};
        for (uint8_t i = 0; i < 3; ++i)
            c.setParamNormalized(Disrumpo::makeCrossoverParamId(i), Disrumpo::crossoverHzToNorm(wantHz[i]));
        c.setParamNormalized(bandCountId, 1.0 / 3.0);  // 2 bands
        const float atTwo = display->getCrossoverFrequency(0);
        c.setParamNormalized(bandCountId, 1.0);        // 4 bands again
        float shown[3] = {};
        for (int i = 0; i < 3; ++i) shown[i] = display->getCrossoverFrequency(i);
        std::printf("D-071 params 300/1500/6000 Hz: divider 0 at 2 bands %.1f Hz; at 4 bands %.1f / %.1f / %.1f Hz\n",
                    static_cast<double>(atTwo), static_cast<double>(shown[0]), static_cast<double>(shown[1]),
                    static_cast<double>(shown[2]));
        CHECK(atTwo == Catch::Approx(300.0f).margin(1.0f));
        for (int i = 0; i < 3; ++i) {
            INFO("divider " << i);
            CHECK(shown[i] == Catch::Approx(wantHz[i]).margin(1.0f));
        }
    }
    c.terminate();
}
