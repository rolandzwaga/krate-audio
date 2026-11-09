// ==============================================================================
// Disrumpo Editor Lifecycle Test
// ==============================================================================
// Exercises the full editor open -> close lifecycle headlessly via the shared
// harness, guarding against use-after-free / dangling-pointer crashes in the
// VST3EditorDelegate teardown path (verifyView/didOpen/willClose). Run under
// AddressSanitizer or valgrind to catch UAF regressions as hard failures.

#include <editor_lifecycle_harness.h>
#include <registered_defaults_probe.h>
#include <shared_editor_views_probe.h>

#include "../../test_support/disrumpo_rig.h"
#include "../../test_support/headless_editor.h"

#include "controller/controller.h"
#include "controller/spectrum_block.h"
#include "controller/views/dynamic_node_selector.h"
#include "controller/views/level_meter_view.h"
#include "controller/views/mod_slider.h"
#include "controller/views/spectrum_display.h"
#include "display/shared_display_bridge.h"
#include "dsp/sweep_envelope.h"
#include "plugin_ids.h"
#include "state_codec.h"
#include "ui/note_value_ui.h"

#include "base/source/updatehandler.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "public.sdk/source/common/memorystream.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/iviewlistener.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"

#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
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

using Disrumpo::TestSupport::findContainerHoldingTag;
using Disrumpo::TestSupport::findSwitch;
using Disrumpo::TestSupport::HeadlessEditor;

constexpr VSTGUI::CCoord kTabSwitchHeight = 243.0;  // Band<N>MainTab / Band<N>ShapeTab switch

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

/// Every ModSlider below `container`, in document order.
void collectModSliders(VSTGUI::CViewContainer* container, std::vector<const Disrumpo::ModSlider*>& out) {
    container->forEachChild([&out](VSTGUI::CView* child) {
        if (const auto* ms = dynamic_cast<const Disrumpo::ModSlider*>(child)) out.push_back(ms);
        if (auto* sub = child->asViewContainer()) collectModSliders(sub, out);
    });
}

/// Every LevelMeterView below `container`, by source.
void collectLevelMeters(VSTGUI::CViewContainer* container, std::vector<Disrumpo::LevelMeterView*>& out) {
    container->forEachChild([&out](VSTGUI::CView* child) {
        if (auto* m = dynamic_cast<Disrumpo::LevelMeterView*>(child)) out.push_back(m);
        if (auto* sub = child->asViewContainer()) collectLevelMeters(sub, out);
    });
}

Disrumpo::LevelMeterView* meterFor(const std::vector<Disrumpo::LevelMeterView*>& meters,
                                   Disrumpo::LevelMeterView::Source source) {
    for (auto* m : meters)
        if (m->source() == source) return m;
    return nullptr;
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

// Level meters: the editor holds one meter per source; a SpectrumBlock's window reaches
// them through the editor timer, and a latched clip clears on a click.
TEST_CASE("Level meters show the processor's window through the editor timer",
          "[disrumpo][controller][lifecycle][meters]")
{
    using Source = Disrumpo::LevelMeterView::Source;
    Disrumpo::Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        std::vector<Disrumpo::LevelMeterView*> meters;
        collectLevelMeters(ed.frame(), meters);
        std::printf("Meters in the editor: %zu (want 6: IN, OUT, 4 bands)\n", meters.size());
        REQUIRE(meters.size() == 6);
        for (const auto source : {Source::Input, Source::Output, Source::Band0, Source::Band1, Source::Band2,
                                  Source::Band3})
            REQUIRE(meterFor(meters, source) != nullptr);

        auto block = std::make_unique<Disrumpo::SpectrumBlock>();
        block->numSamples = 64;
        block->sampleRate = 48000.0f;
        auto& w = block->meters;
        w.inPeak[0] = 0.5f;
        w.inPeak[1] = 0.25f;
        w.inMeanSq[0] = 0.125f;
        w.outPeak[1] = 1.25f;
        w.bandPeak[0] = 0.7f;
        w.bandPeak[3] = 0.1f;
        w.clipFlags = Disrumpo::kMeterClipOutR;
        w.numSamples = 14400;  // the whole 300 ms RMS window at 48 kHz
        Steinberg::Vst::DataExchangeBlock deb{
            .data = block.get(), .size = static_cast<Steinberg::uint32>(sizeof(Disrumpo::SpectrumBlock)), .blockID = 0};
        c.onDataExchangeBlocksReceived(0, 1, &deb, static_cast<Steinberg::TBool>(false));
        c.tickEditorTimerForTest();

        const auto* in = meterFor(meters, Source::Input);
        auto* out = meterFor(meters, Source::Output);
        const auto* band0 = meterFor(meters, Source::Band0);
        const auto* band3 = meterFor(meters, Source::Band3);
        std::printf("Meters after one tick: IN L peak %.2f dB RMS %.2f dB, IN R peak %.2f dB; OUT R peak %.2f dB, "
                    "OUT clip %d, IN clip %d; band 1 %.2f dB, band 4 %.2f dB, band 2 %.1f dB\n",
                    static_cast<double>(in->channel(0).peakDb()), static_cast<double>(in->channel(0).rmsDb()),
                    static_cast<double>(in->channel(1).peakDb()), static_cast<double>(out->channel(1).peakDb()),
                    out->clipLatched() ? 1 : 0, in->clipLatched() ? 1 : 0,
                    static_cast<double>(band0->channel(0).peakDb()), static_cast<double>(band3->channel(0).peakDb()),
                    static_cast<double>(meterFor(meters, Source::Band1)->channel(0).peakDb()));
        CHECK(in->channel(0).peakDb() == Catch::Approx(-6.0206).margin(0.01));
        CHECK(in->channel(0).rmsDb() == Catch::Approx(-9.0309).margin(0.01));
        CHECK(in->channel(1).peakDb() == Catch::Approx(-12.0412).margin(0.01));
        CHECK(out->channel(1).peakDb() == Catch::Approx(1.9382).margin(0.01));
        CHECK(out->clipLatched());
        CHECK_FALSE(in->clipLatched());
        CHECK(band0->channel(0).peakDb() == Catch::Approx(-3.098).margin(0.01));
        CHECK(band3->channel(0).peakDb() == Catch::Approx(-20.0).margin(0.01));
        CHECK(meterFor(meters, Source::Band1)->channel(0).peakDb() == Disrumpo::MeterBallistics::kFloorDb);

        // The clip stays latched through clean windows until the meter is clicked
        block->meters = Disrumpo::MeterWindow{};
        block->meters.numSamples = 1440;
        for (int t = 0; t < 30; ++t) {
            c.onDataExchangeBlocksReceived(0, 1, &deb, static_cast<Steinberg::TBool>(false));
            c.tickEditorTimerForTest();
        }
        CHECK(out->clipLatched());
        VSTGUI::MouseDownEvent click(out->getViewSize().getCenter(), VSTGUI::MouseButton::Left);
        out->onMouseDownEvent(click);
        CHECK(static_cast<bool>(click.consumed));
        CHECK_FALSE(out->clipLatched());
    }
    c.terminate();
}

TEST_CASE("Level meters read the Tier-3 SharedMeters without DataExchange",
          "[disrumpo][controller][lifecycle][meters][tier3]")
{
    using Source = Disrumpo::LevelMeterView::Source;
    Disrumpo::TestSupport::Rig rig(48000.0, 512);
    auto state = Disrumpo::TestSupport::getStateBytes(*rig.p);
    Disrumpo::Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    {
        auto* s = new Steinberg::MemoryStream(state.data(), static_cast<Steinberg::TSize>(state.size()));
        REQUIRE(c.setComponentState(s) == Steinberg::kResultOk);
        s->release();
    }
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        for (int t = 0; t < 12; ++t) c.tickEditorTimerForTest();  // no DataExchange: the Tier-3 fallback starts
        const std::vector<float> in(4096, 0.25f);                 // -12.04 dBFS at the input
        std::vector<float> outL;
        std::vector<float> outR;
        rig.render(in, in, outL, outR);
        c.tickEditorTimerForTest();
        std::vector<Disrumpo::LevelMeterView*> meters;
        collectLevelMeters(ed.frame(), meters);
        const auto* input = meterFor(meters, Source::Input);
        REQUIRE(input != nullptr);
        std::printf("Meters Tier 3: IN L peak %.2f dB, IN R peak %.2f dB (want -12.04)\n",
                    static_cast<double>(input->channel(0).peakDb()), static_cast<double>(input->channel(1).peakDb()));
        CHECK(input->channel(0).peakDb() == Catch::Approx(-12.0412).margin(0.01));
        CHECK(input->channel(1).peakDb() == Catch::Approx(-12.0412).margin(0.01));
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

// D-170: band 3's expanded panel (container 9102) is shown only while band 3 is
// expanded AND Band Count makes band 3 visible, whichever of the two changes.
TEST_CASE("Expanded band panel follows Band Count", "[disrumpo][controller][ui][D-170]")
{
    Disrumpo::Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    const auto bandCountId = Disrumpo::makeGlobalParamId(Disrumpo::GlobalParamType::kGlobalBandCount);
    const auto expanded3Id = Disrumpo::makeBandParamId(2, Disrumpo::BandParamType::kBandExpanded);
    constexpr int32_t kBand3ExpandedTag = 9102;

    // Band Count 4, Band3Expanded 1, Band Count 2, Band Count 4
    c.setParamNormalized(bandCountId, 1.0);
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        auto* strip = findContainerHoldingTag(ed.frame(), kBand3ExpandedTag);
        REQUIRE(strip != nullptr);
        c.setParamNormalized(expanded3Id, 1.0);
        const bool atFour = strip->isVisible();
        c.setParamNormalized(bandCountId, 1.0 / 3.0);
        const bool atTwo = strip->isVisible();
        c.setParamNormalized(bandCountId, 1.0);
        const bool backAtFour = strip->isVisible();
        std::printf("D-170 expanded then Band Count 4 -> 2 -> 4: container 9102 visible %d -> %d -> %d (want 1 -> 0 -> 1),"
                    " height %.0f\n",
                    atFour ? 1 : 0, atTwo ? 1 : 0, backAtFour ? 1 : 0, strip->getViewSize().getHeight());
        CHECK(atFour);
        CHECK_FALSE(atTwo);
        CHECK(backAtFour);

        // Expanded set again while band 3 is hidden, then Band Count raised
        c.setParamNormalized(bandCountId, 1.0 / 3.0);
        c.setParamNormalized(expanded3Id, 0.0);
        c.setParamNormalized(expanded3Id, 1.0);
        const bool expandedWhileHidden = strip->isVisible();
        c.setParamNormalized(bandCountId, 1.0);
        const bool shownWithBand = strip->isVisible();
        std::printf("D-170 expanded while hidden, Band Count 2 -> 4: container 9102 visible %d -> %d (want 0 -> 1),"
                    " height %.0f (want 290)\n",
                    expandedWhileHidden ? 1 : 0, shownWithBand ? 1 : 0, strip->getViewSize().getHeight());
        CHECK_FALSE(expandedWhileHidden);
        CHECK(shownWithBand);
        CHECK(strip->getViewSize().getHeight() == Catch::Approx(290.0));
    }
    c.terminate();
}

// D-072 / D-122: a parameter's registered default is where a host's reset puts it;
// it must be the value a fresh controller starts on (Band Count started on "4"
// but reset to "1", Oversample Max started on "4x" but reset to "1x").
TEST_CASE("Disrumpo parameters start on their registered default", "[disrumpo][controller][D-072][D-122]")
{
    Disrumpo::Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    const auto mismatches = Krate::TestSupport::parametersStartingOffTheirDefault(c);
    for (const auto& m : mismatches) std::printf("  off its default: %s\n", m.c_str());
    CHECK(mismatches.empty());
    c.terminate();
}

namespace {

std::string asciiOf(const Steinberg::Vst::TChar* s) {
    std::string out;
    for (int i = 0; i < 128 && s[i] != 0; ++i) out += s[i] < 128 ? static_cast<char>(s[i]) : '?';
    return out;
}

struct PlainMapping {
    PlainMapping(const char* mappingName, Steinberg::Vst::ParamID paramId, std::function<double(double)> plain)
        : name(mappingName), id(paramId), processorPlain(std::move(plain)) {}
    const char* name;
    Steinberg::Vst::ParamID id;
    std::function<double(double)> processorPlain;  ///< the processor's denormalization, in the registered unit
};

}  // namespace

// D-101 / D-169: the host shows ParameterInfo.units next to Parameter::toString(norm).
// The registered parameter's plain value must be the processor's own decode
// (state_codec.h ranges, lfoRateNormToHz) in that unit, or automation lanes and
// readouts show a number the DSP does not run (Env Attack 10 ms read "0.091 ms").
TEST_CASE("Registered plain value matches processor denormalization", "[disrumpo][controller][D-101][D-169]")
{
    using namespace Disrumpo;
    const auto sweep = [](SweepParamType t) { return makeSweepParamId(t); };
    const auto mod = [](ModParamType t) { return makeModParamId(t); };
    const auto linear = [](const LinearRange& r, double scale = 1.0) {
        return [r, scale](double n) { return scale * static_cast<double>(r.toPlain(static_cast<float>(n))); };
    };
    const auto percent = [](double n) { return 100.0 * n; };
    const auto lfoHz = [](double n) { return static_cast<double>(lfoRateNormToHz(static_cast<float>(n))); };

    const std::vector<PlainMapping> mappings = {
        {"Sweep Width (oct)", sweep(SweepParamType::kSweepWidth), linear(kSweepWidthOctRange)},
        {"Sweep Intensity (%)", sweep(SweepParamType::kSweepIntensity), linear(kSweepIntensityRange, 100.0)},
        {"Sweep LFO Rate (Hz)", sweep(SweepParamType::kSweepLFORate), lfoHz},
        {"Sweep Env Attack (ms)", sweep(SweepParamType::kSweepEnvAttack),
         linear(LinearRange{.min = kMinSweepEnvAttackMs, .span = kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs})},
        {"Sweep Env Release (ms)", sweep(SweepParamType::kSweepEnvRelease),
         linear(LinearRange{.min = kMinSweepEnvReleaseMs, .span = kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs})},
        {"LFO 1 Rate (Hz)", mod(ModParamType::kLFO1Rate), lfoHz},
        {"LFO 2 Rate (Hz)", mod(ModParamType::kLFO2Rate), lfoHz},
        {"LFO 1 Phase (deg)", mod(ModParamType::kLFO1Phase), linear(kLfoPhaseDegRange)},
        {"LFO 2 Phase (deg)", mod(ModParamType::kLFO2Phase), linear(kLfoPhaseDegRange)},
        {"Env Attack (ms)", mod(ModParamType::kEnvFollowerAttack), linear(kEnvFollowerAttackMsRange)},
        {"Env Release (ms)", mod(ModParamType::kEnvFollowerRelease), linear(kEnvFollowerReleaseMsRange)},
        {"Env Sensitivity (%)", mod(ModParamType::kEnvFollowerSensitivity), percent},
        {"Random Rate (Hz)", mod(ModParamType::kRandomRate), linear(kRandomRateHzRange)},
        {"Random Smoothness (%)", mod(ModParamType::kRandomSmoothness), percent},
        {"Chaos Speed (x)", mod(ModParamType::kChaosSpeed), linear(kChaosSpeedRange)},
        {"S&H Rate (Hz)", mod(ModParamType::kSampleHoldRate), linear(kSampleHoldRateHzRange)},
        {"S&H Slew (ms)", mod(ModParamType::kSampleHoldSlew), linear(kSampleHoldSlewMsRange)},
        {"Pitch Min (Hz)", mod(ModParamType::kPitchFollowerMinHz), linear(kPitchFollowerMinHzRange)},
        {"Pitch Max (Hz)", mod(ModParamType::kPitchFollowerMaxHz), linear(kPitchFollowerMaxHzRange)},
        {"Pitch Tracking (ms)", mod(ModParamType::kPitchFollowerTrackingSpeed), linear(kPitchFollowerTrackingMsRange)},
        {"Transient Attack (ms)", mod(ModParamType::kTransientAttack), linear(kTransientAttackMsRange)},
        {"Transient Decay (ms)", mod(ModParamType::kTransientDecay), linear(kTransientDecayMsRange)},
        {"Rungler Rate (Hz)", mod(ModParamType::kRunglerRate), linear(kRunglerRateHzRange)},
        {"Route 1 Amount (%)", makeRoutingParamId(0, 2), [](double n) { return 100.0 * (2.0 * n - 1.0); }},
    };

    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    int wrong = 0;
    for (const auto& m : mappings) {
        Steinberg::Vst::Parameter* param = c.getParameterObject(m.id);
        REQUIRE(param != nullptr);
        for (const double n : {0.0, 0.5, 1.0}) {
            const double want = m.processorPlain(n);
            const double plain = param->toPlain(n);
            Steinberg::Vst::String128 text{};
            REQUIRE(c.getParamStringByValue(m.id, n, text) == Steinberg::kResultOk);
            const std::string shown = asciiOf(text);
            const double tol = 1e-4 + 1e-5 * std::fabs(want);
            const bool plainOk = std::fabs(plain - want) <= tol;
            const bool textOk = !shown.empty() && std::fabs(std::strtod(shown.c_str(), nullptr) - want) <= tol;
            if (plainOk && textOk) continue;
            ++wrong;
            std::printf("  %s at norm %.1f: registered plain %.4f, host text \"%s\", processor %.4f\n", m.name, n,
                        plain, shown.c_str(), want);
        }
    }
    std::printf("Registered vs processor plain values: %d of %zu mismatched\n", wrong, mappings.size() * 3);
    CHECK(wrong == 0);
    c.terminate();
}

// D-092 (fix plan T4.04): the three note-value lists are the shared dropdown
// (plugins/shared/src/ui/note_value_ui.h), which getNoteValueFromDropdown decodes,
// and start on its default entry.
TEST_CASE("Note value lists are the shared dropdown with its default", "[disrumpo][controller][D-092]")
{
    using namespace Disrumpo;
    namespace Shared = Krate::Plugins::Parameters;
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    const double wantDefault = static_cast<double>(Shared::kNoteValueDefaultIndex) /
                               static_cast<double>(Shared::kNoteValueDropdownCount - 1);
    int wrong = 0;
    for (const auto id : {makeSweepParamId(SweepParamType::kSweepLFONoteValue),
                          makeModParamId(ModParamType::kLFO1NoteValue), makeModParamId(ModParamType::kLFO2NoteValue)}) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(c.getParameterInfoByTag(id, info) == Steinberg::kResultTrue);
        const std::string title = asciiOf(info.title);
        if (info.stepCount != Shared::kNoteValueDropdownCount - 1) {
            ++wrong;
            std::printf("  %s: %d entries, shared dropdown %d\n", title.c_str(), info.stepCount + 1,
                        Shared::kNoteValueDropdownCount);
        }
        for (int i = 0; i < Shared::kNoteValueDropdownCount && i <= info.stepCount; ++i) {
            Steinberg::Vst::String128 text{};
            REQUIRE(c.getParamStringByValue(id, static_cast<double>(i) / info.stepCount, text) == Steinberg::kResultOk);
            const std::string shown = asciiOf(text);
            const std::string shared = asciiOf(Shared::kNoteValueDropdownStrings[i]);
            if (shown != shared) {
                ++wrong;
                std::printf("  %s entry %d: \"%s\", shared \"%s\"\n", title.c_str(), i, shown.c_str(), shared.c_str());
            }
        }
        if (std::fabs(info.defaultNormalizedValue - wantDefault) > 1e-9) {
            ++wrong;
            std::printf("  %s: default %.4f (entry %ld), shared default %.4f (entry %d)\n", title.c_str(),
                        info.defaultNormalizedValue, std::lround(info.defaultNormalizedValue * info.stepCount),
                        wantDefault, Shared::kNoteValueDefaultIndex);
        }
    }
    std::printf("Note value lists vs the shared dropdown: %d mismatches\n", wrong);
    CHECK(wrong == 0);
    c.terminate();
}
