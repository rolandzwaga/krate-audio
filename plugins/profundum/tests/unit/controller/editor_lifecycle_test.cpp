// ==============================================================================
// Profundum - editor lifecycle tests (Phase 10 T019, SC-013.1 - .4)
// ==============================================================================
// The [lifecycle] tag is REQUIRED: valgrind-nightly.yml selects cases by it.
//
// SECTION "PresetConfigIsLive" (FR-050, FR-051, FR-052): the preset config drives
// a LIVE PresetManager scan. Both directory overrides point at SEPARATE temp
// directories: scanPresets() scans user AND factory, so one shared directory
// double-counts, and an unset user override would read the machine's real user
// preset folder. Model: plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp.
//
// SECTION "HarnessCycles" (FR-055; Phase 12 SC-016: 10 cycles): headless open/close
// cycles through the shared harness (tests/test_helpers/editor_lifecycle_harness.h:102),
// then the controller's preset manager is non-null.
//
// SECTION "EditorBindsSurfaceV12" (Phase 12 SC-002 / SC-016; replaces the Phase 10
// EditorBindsSurface): the BUILT view tree binds exactly the 89 visible IDs (every
// registered ID but the hidden performance IDs 4 - 7); tags at or above 9000 are the
// UI-only session tags (plan 4.7) and are not parameter bindings. The getTag() >= 0
// filter is load-bearing: CTextLabel IS-A CControl and keeps tag -1 when untagged.
// The editor is built with the ABSOLUTE uidesc path, exactly as the harness does:
// createView's "editor.uidesc" is a bundle-resource name that the test executable
// (not a plugin bundle) cannot resolve. CreateViewNames covers createView itself.
//
// SECTION "CustomViewCounts" (SC-016, FR-070, FR-080): exactly two HarmonicBarViews
// (Main, Advanced core) and one SpreadCurveView in the built tree; every controller
// view pointer is null after willClose (FR-091).
// SECTION "ClosedFrameCachedThenShown" (SC-016, FR-091): a frame delivered while the
// editor is closed touches no view pointer and is cached; on reopen both bar views
// draw it.
// SECTION "TimerOwningViewsAttachRemove" (SC-016): attached / removed 10 times on each
// built HarmonicBarView (the only timer-owning view); the timer exists only while attached.
//
// SECTION "CreateViewNames" (FR-055): only ViewType::kEditor yields a view.
//
// SECTION "MorphIndicatorSlider" (Phase 14 T018; FR-035, SC-016; plan 4.9): the Main-page
// MorphPosition slider is the one MorphIndicatorSlider of the built tree and the panel
// registers it; open / close cycles leave the shared-view count unchanged, and every view
// pointer (morphIndicator_ included) is null after willClose.
//
// NEVER name a kPlatformType* constant here - always
// Krate::TestSupport::nativePlatformType() (lint-platform-type-literals.js).
// ==============================================================================

#include "test_helpers/editor_lifecycle_harness.h"
#include <shared_editor_views_probe.h>
#include "ui_tree_helpers.h"  // HeadlessEditor, collectViewsOfType (Phase 12)

#include "controller/controller.h"
#include "plugin_ids.h"
#include "preset/preset_manager.h"
#include "preset/profundum_preset_config.h"
#include "processor/spectrum_frame.h"
#include "ui/harmonic_bar_view.h"
#include "ui/morph_indicator_slider.h"  // Phase 14 T018
#include "ui/preset_browser_view.h"  // plugins/shared/src/ui (Phase 13 T013 walker stop)
#include "ui/spread_curve_view.h"
#include "update/profundum_update_config.h"

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/cslider.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

/// Per-run counter for unique temp directory names. Seeded from the clock so two
/// concurrent runs of the binary do not share a directory.
unsigned long long nextTempCounter() {
    static unsigned long long counter = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return ++counter;
}

/// Recursive walk collecting every CControl bound to a parameter (tag >= 0).
void collectBoundControls(VSTGUI::CViewContainer* container,
                          std::vector<VSTGUI::CControl*>& out) {
    if (container == nullptr) {
        return;
    }
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* view = container->getView(i);
        if (view == nullptr) {
            continue;
        }
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
            if (control->getTag() >= 0) {
                out.push_back(control);
            }
        }
        // Phase 13 T013: never descend into the PresetBrowserView overlay (its own tagged buttons, 1-5 / 10-31).
        if (dynamic_cast<Krate::Plugins::PresetBrowserView*>(view) != nullptr) {
            continue;
        }
        if (auto* child = dynamic_cast<VSTGUI::CViewContainer*>(view)) {
            collectBoundControls(child, out);
        }
    }
}

}  // namespace

TEST_CASE("Profundum_EditorLifecycle", "[profundum][controller][ui][lifecycle]") {
    SECTION("PresetConfigIsLive") {  // SC-013.1, FR-050, FR-051, FR-052
        const auto cfg = ::Profundum::makeProfundumPresetConfig();
        REQUIRE(cfg.pluginName == "Profundum");
        REQUIRE(cfg.pluginCategoryDesc == "Synth");
        REQUIRE(cfg.subcategoryNames == std::vector<std::string>{"Sub", "Resonant", "Harmonic", "Driven",
                                                           "Plucked", "Animated"});  // Phase 13 FR-001 (Q1)
        const bool processorUidMatches = (cfg.processorUID == ::Profundum::kProcessorUID);
        REQUIRE(processorUidMatches);

        namespace fs = std::filesystem;
        const auto n = std::to_string(nextTempCounter());
        const fs::path tempRoot = fs::temp_directory_path();
        const fs::path userDir = tempRoot / ("profundum_sc013_user_" + n);
        const fs::path factoryDir = tempRoot / ("profundum_sc013_factory_" + n);

        std::error_code ec;
        fs::remove_all(userDir, ec);
        fs::remove_all(factoryDir, ec);
        REQUIRE(fs::create_directories(userDir));
        REQUIRE(fs::create_directories(factoryDir / "Sub"));
        {
            // Zero-byte probe: the scan keys on the .vstpreset extension and the
            // parent directory name only.
            std::ofstream probe(factoryDir / "Sub" / "Probe.vstpreset", std::ios::binary);
            REQUIRE(probe.good());
        }

        std::size_t presetCount = 0;
        std::string firstSubcategory;
        {
            Krate::Plugins::PresetManager pm(cfg, nullptr, nullptr, userDir, factoryDir);
            const auto presets = pm.scanPresets();
            presetCount = presets.size();
            if (!presets.empty()) {
                firstSubcategory = presets[0].subcategory;
            }
        }

        // Remove the temp tree BEFORE asserting so a failure does not leak it.
        fs::remove_all(userDir, ec);
        fs::remove_all(factoryDir, ec);

        REQUIRE(presetCount == 1);
        REQUIRE(firstSubcategory == "Sub");

        // FR-051: the filesystem half agrees with the config list.
        const fs::path seedDir = fs::path(PROFUNDUM_RESOURCES_DIR "/presets/Sub");
        REQUIRE(fs::is_directory(seedDir));
        REQUIRE(seedDir.filename().string() == cfg.subcategoryNames[0]);

        // FR-052: the update config compiles and names this plugin; no checker exists.
        const auto u = ::Profundum::makeProfundumUpdateConfig();
        REQUIRE(u.endpointUrl == "https://rolandzwaga.github.io/krate-audio/versions.json");
        REQUIRE(u.pluginName == "Profundum");
    }

    SECTION("HarnessCycles") {  // SC-013.2, FR-055
        auto controller = Steinberg::owned(new ::Profundum::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

        Krate::TestSupport::exerciseEditorLifecycle(
            *controller, "editor", std::string(PROFUNDUM_RESOURCES_DIR) + "/editor.uidesc",
            /*cycles=*/10);

        REQUIRE(controller->presetManagerForTest() != nullptr);
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("EditorBindsSurfaceV12") {  // Phase 12 SC-002 counts, SC-016
        auto controller = Steinberg::owned(new ::Profundum::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

        // The 89 visible IDs: every registered ID except the hidden performance IDs 4 - 7.
        std::set<std::int32_t> expected;
        const Steinberg::int32 paramCount = controller->getParameterCount();
        for (Steinberg::int32 i = 0; i < paramCount; ++i) {
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(controller->getParameterInfo(i, info) == Steinberg::kResultOk);
            const auto id = static_cast<std::int32_t>(info.id);
            if (id < static_cast<std::int32_t>(::Profundum::kPitchBendId) ||
                id > static_cast<std::int32_t>(::Profundum::kChannelPressureId)) {
                expected.insert(id);
            }
        }

        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidescPath = std::string(PROFUNDUM_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
                Steinberg::kResultTrue);
        REQUIRE(editor->getFrame() != nullptr);

        std::vector<VSTGUI::CControl*> controls;
        collectBoundControls(editor->getFrame(), controls);

        std::set<std::int32_t> tags;  // parameter bindings only: session tags (>= 9000) are UI-only
        for (auto* control : controls) {
            if (control->getTag() < 9000) {
                tags.insert(control->getTag());
            }
        }

        view->removed();
        view->release();
        REQUIRE(controller->terminate() == Steinberg::kResultOk);

        REQUIRE(paramCount == 93);
        REQUIRE(expected.size() == 89u);
        REQUIRE(tags.size() == 89u);
        REQUIRE(tags == expected);
        for (const std::int32_t hidden : {4, 5, 6, 7}) {
            CAPTURE(hidden);
            REQUIRE_FALSE(tags.contains(hidden));
        }
    }

    SECTION("CustomViewCounts") {  // SC-016, FR-070, FR-080, FR-091
        auto controller = Steinberg::owned(new ::Profundum::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        {
            ProfundumPluginTest::HeadlessEditor ed(controller.get());
            REQUIRE(ed.frame() != nullptr);
            const auto bars = ProfundumPluginTest::collectViewsOfType<::Profundum::UI::HarmonicBarView>(ed.frame());
            const auto spreads = ProfundumPluginTest::collectViewsOfType<::Profundum::UI::SpreadCurveView>(ed.frame());
            REQUIRE(bars.size() == 2u);
            CHECK(spreads.size() == 1u);
            const int mainBars = static_cast<int>(bars[0]->variant() == ::Profundum::UI::HarmonicBarView::Variant::Main) +
                                 static_cast<int>(bars[1]->variant() == ::Profundum::UI::HarmonicBarView::Variant::Main);
            CHECK(mainBars == 1);  // one Main instance, one Advanced core instance
            CHECK_FALSE(controller->viewPointersNullForTest());  // non-vacuity: held while open
        }
        CHECK(controller->viewPointersNullForTest());
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("ClosedFrameCachedThenShown") {  // SC-016, FR-091
        auto controller = Steinberg::owned(new ::Profundum::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        {
            ProfundumPluginTest::HeadlessEditor ed(controller.get());
            REQUIRE(ed.frame() != nullptr);
        }
        REQUIRE(controller->viewPointersNullForTest());

        ::Profundum::ProfundumSpectrumFrame frame = ::Profundum::Controller::silenceFrame();
        frame.sequence = 5151u;
        frame.flags = 0;
        frame.sampleRate = 48000.0f;
        frame.f0Hz = 41.2f;
        frame.anchorPower = 0.25f;
        frame.numPartials = 4;
        frame.partialPower[0] = 0.5f;
        frame.partialPower[1] = 0.125f;
        frame.partialPower[2] = 0.05f;
        frame.partialPower[3] = 0.02f;
        Steinberg::Vst::DataExchangeBlock block{};
        block.data = &frame;
        block.size = static_cast<Steinberg::uint32>(sizeof(frame));
        block.blockID = 0;
        const std::uint64_t receivedBefore = controller->framesReceivedForTest();
        controller->onDataExchangeBlocksReceived(::Profundum::kProfundumFrameUserContextId, 1u, &block, 0);
        CHECK(controller->framesReceivedForTest() == receivedBefore + 1u);
        CHECK(controller->cachedFrameForTest().sequence == 5151u);  // cached
        CHECK(controller->viewPointersNullForTest());               // no view touched

        {
            ProfundumPluginTest::HeadlessEditor ed(controller.get());
            REQUIRE(ed.frame() != nullptr);
            const auto bars = ProfundumPluginTest::collectViewsOfType<::Profundum::UI::HarmonicBarView>(ed.frame());
            REQUIRE(bars.size() == 2u);
            for (auto* bar : bars) {
                CHECK(bar->barHeightsForTest()[0] == 0.0f);  // nothing drawn before the first tick
                bar->onTickForTest(0.033f);
                CHECK(bar->barHeightsForTest()[0] > 0.0f);  // the cached frame
            }
            CHECK(bars[0]->barHeightsForTest() == bars[1]->barHeightsForTest());
        }
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("TimerOwningViewsAttachRemove") {  // SC-016, FR-091
        auto controller = Steinberg::owned(new ::Profundum::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        {
            ProfundumPluginTest::HeadlessEditor ed(controller.get());
            REQUIRE(ed.frame() != nullptr);
            // (view, parent container) of every HarmonicBarView; the walk supplies the parent, because an
            // unattached view does not know it.
            std::vector<std::pair<::Profundum::UI::HarmonicBarView*, VSTGUI::CViewContainer*>> owners;
            std::vector<VSTGUI::CViewContainer*> pending{ed.frame()};
            while (!pending.empty()) {
                VSTGUI::CViewContainer* container = pending.back();
                pending.pop_back();
                for (std::uint32_t i = 0; i < container->getNbViews(); ++i) {
                    VSTGUI::CView* v = container->getView(i);
                    if (auto* bar = dynamic_cast<::Profundum::UI::HarmonicBarView*>(v)) {
                        owners.emplace_back(bar, container);
                    }
                    if (auto* child = v != nullptr ? v->asViewContainer() : nullptr) {
                        pending.push_back(child);
                    }
                }
            }
            REQUIRE(owners.size() == 2u);
            for (auto& [bar, parent] : owners) {
                const bool wasAttached = bar->isAttached();
                if (wasAttached) {
                    REQUIRE(bar->removed(parent));
                }
                REQUIRE_FALSE(bar->hasTimerForTest());
                for (int cycle = 0; cycle < 10; ++cycle) {
                    CAPTURE(cycle);
                    REQUIRE(bar->attached(parent));
                    CHECK(bar->hasTimerForTest());
                    REQUIRE(bar->removed(parent));
                    CHECK_FALSE(bar->hasTimerForTest());
                }
                if (wasAttached) {
                    REQUIRE(bar->attached(parent));  // back to the state the editor built
                }
            }
        }
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("CreateViewNames") {  // SC-013.4, FR-055
        auto controller = Steinberg::owned(new ::Profundum::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        Krate::TestSupport::ensureVstguiInitialized();

        REQUIRE(controller->createView("nonsense") == nullptr);

        Steinberg::IPlugView* editorView =
            controller->createView(Steinberg::Vst::ViewType::kEditor);
        REQUIRE(editorView != nullptr);
        const bool isVst3Editor = dynamic_cast<VSTGUI::VST3Editor*>(editorView) != nullptr;
        editorView->release();
        REQUIRE(isVst3Editor);

        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("MorphIndicatorSlider") {  // Phase 14 T018: FR-035, SC-016, FR-091
        auto controller = Steinberg::owned(new ::Profundum::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        Krate::TestSupport::ensureVstguiInitialized();

        // viewPointersNullForTest covers morphIndicator_: a registered slider alone makes it false, willClose clears it.
        {
            auto standalone = VSTGUI::makeOwned<::Profundum::MorphIndicatorSlider>(
                VSTGUI::CRect(0, 0, 476, 24), nullptr, static_cast<int32_t>(::Profundum::kMorphPositionId));
            REQUIRE(controller->viewPointersNullForTest());
            controller->registerMorphIndicator(standalone.get());
            CHECK_FALSE(controller->viewPointersNullForTest());
            controller->willClose(nullptr);
            CHECK(controller->morphIndicatorForTest() == nullptr);
            CHECK(controller->viewPointersNullForTest());
        }

        const std::string uidescPath = std::string(PROFUNDUM_RESOURCES_DIR) + "/editor.uidesc";
        const auto baseline = Krate::TestSupport::openEditorAndCountSharedViews(*controller, "editor", uidescPath);
        for (int cycle = 0; cycle < 3; ++cycle) {
            CAPTURE(cycle);
            {
                ProfundumPluginTest::HeadlessEditor ed(controller.get());
                REQUIRE(ed.frame() != nullptr);

                // The Main-page MorphPosition slider is the one MorphIndicatorSlider, and the panel registered it.
                const auto indicators =
                    ProfundumPluginTest::collectViewsOfType<::Profundum::MorphIndicatorSlider>(ed.frame());
                REQUIRE(indicators.size() == 1u);
                CHECK(indicators[0]->getTag() == static_cast<int32_t>(::Profundum::kMorphPositionId));
                CHECK(controller->morphIndicatorForTest() == indicators[0]);
                int morphSliders = 0;
                for (auto* slider : ProfundumPluginTest::collectViewsOfType<VSTGUI::CSlider>(ed.frame())) {
                    if (slider->getTag() == static_cast<int32_t>(::Profundum::kMorphPositionId)) {
                        ++morphSliders;
                        CHECK(dynamic_cast<::Profundum::MorphIndicatorSlider*>(slider) != nullptr);
                    }
                }
                CHECK(morphSliders == 1);
                CHECK_FALSE(controller->viewPointersNullForTest());
            }
            CHECK(controller->morphIndicatorForTest() == nullptr);
            CHECK(controller->viewPointersNullForTest());

            const auto count = Krate::TestSupport::openEditorAndCountSharedViews(*controller, "editor", uidescPath);
            CHECK(count.banners == baseline.banners);
            CHECK(count.checkButtons == baseline.checkButtons);
            CHECK(count.versionLabels == baseline.versionLabels);
            CHECK(count.legacyTagged == baseline.legacyTagged);
            CHECK(controller->viewPointersNullForTest());
        }
        CHECK(baseline.banners == 1);
        CHECK(baseline.checkButtons == 1);
        CHECK(baseline.versionLabels == 1);

        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }
}

// Every Krate editor carries the shared views createSharedEditorView builds: one
// update banner, one Check for Updates button and one VersionLabel (VERSION_STR),
// and no hand-filled tag-9999 version label. tools/lint-plugin-editor-baseline.js
// requires this case in every plugin.
TEST_CASE("Profundum editor carries the shared update and version views",
          "[profundum][controller][ui][version]")
{
    ::Profundum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto count = Krate::TestSupport::openEditorAndCountSharedViews(
        controller, "editor", std::string(PROFUNDUM_RESOURCES_DIR) + "/editor.uidesc");
    CHECK(count.banners == 1);
    CHECK(count.checkButtons == 1);
    CHECK(count.versionLabels == 1);
    CHECK(count.legacyTagged == 0);

    controller.terminate();
}
