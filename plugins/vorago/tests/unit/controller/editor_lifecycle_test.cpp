// ==============================================================================
// Vorago - editor lifecycle tests (Phase 11 SC-012, Phase 13 SC-016 a)
// ==============================================================================
// The [lifecycle] tag is REQUIRED: valgrind-nightly.yml selects cases by it.
//
// SECTION "PresetConfigIsLive" (SC-012.3, FR-050, FR-052): the preset config
// drives a LIVE PresetManager scan. Both directory overrides point at SEPARATE
// temp directories (P-4): scanPresets() scans user AND factory
// (preset_manager.cpp:41-49), so one shared directory double-counts, and an
// unset user override would read the machine's real user preset folder.
// Phase 13 (plan D-4): the browser tab labels are "All" + the subcategories.
//
// SECTION "HarnessCycles" (SC-016 a, FR-055): ten headless open/close cycles
// through the shared harness (tests/test_helpers/editor_lifecycle_harness.h:102)
// - every cycle builds and drops the EcosystemView, the page sub-controller and
// the PresetBrowserView overlay - then the controller's preset manager is non-null.
//
// SECTION "EditorBindsSurface" (SC-016 a, FR-041, FR-042, FR-071): the BUILT
// view tree carries exactly the 106 bound controls (every registered ID except
// the hidden 4 and 5), exactly one EcosystemView, and the preset browser
// overlay; the controller's frame-owned pointers are dropped on close; unknown
// custom-view and sub-controller names return null.
// The getTag() >= 0 filter is load-bearing: CTextLabel IS-A CControl and keeps
// tag -1 when untagged, so an unfiltered walk would also count the labels.
// Session tags (>= Vorago::UI::kSessionTagBase: page strip, preset button) are
// never ParamIDs and are excluded. The walk never descends into the didOpen
// PresetBrowserView overlay: it holds its own tagged buttons (tags 1-31,
// preset_browser_view.h:34-50) that would collide with ParamIDs 1-5.
//
// NEVER name a kPlatformType* constant here - always
// Krate::TestSupport::nativePlatformType() (lint-platform-type-literals.js).
// ==============================================================================

#include "test_helpers/editor_lifecycle_harness.h"

#include "controller/controller.h"
#include "plugin_ids.h"
#include "preset/preset_manager.h"
#include "preset/vorago_preset_config.h"
#include "ui/ecosystem_view.h"
#include "ui/panel_sub_controller.h"
#include "unit/param_table_expected.h"
#include "update/vorago_update_config.h"

#include "ui/preset_browser_view.h"

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiattributes.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

/// Per-run counter for unique temp directory names. Seeded from the clock so two
/// concurrent runs of the binary do not share a directory.
unsigned long long nextTempCounter() {
    static unsigned long long counter = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return ++counter;
}

/// Recursive walk collecting every CControl bound to a parameter
/// (0 <= tag < kSessionTagBase) and counting EcosystemView instances. Never
/// descends into the PresetBrowserView overlay (its own tagged buttons).
void collectBoundControls(VSTGUI::CViewContainer* container,
                          std::vector<VSTGUI::CControl*>& out,
                          std::size_t& ecosystemViewCount) {
    if (container == nullptr) {
        return;
    }
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* view = container->getView(i);
        if (view == nullptr) {
            continue;
        }
        if (dynamic_cast<::Vorago::UI::EcosystemView*>(view) != nullptr) {
            ++ecosystemViewCount;
        }
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
            const std::int32_t tag = control->getTag();
            if (tag >= 0 && tag < ::Vorago::UI::kSessionTagBase) {
                out.push_back(control);
            }
        }
        if (dynamic_cast<Krate::Plugins::PresetBrowserView*>(view) != nullptr) {
            continue;
        }
        if (auto* child = dynamic_cast<VSTGUI::CViewContainer*>(view)) {
            collectBoundControls(child, out, ecosystemViewCount);
        }
    }
}

}  // namespace

TEST_CASE("Vorago_EditorLifecycle", "[vorago][controller][ui][lifecycle]") {
    SECTION("PresetConfigIsLive") {
        const auto cfg = ::Vorago::makeVoragoPresetConfig();
        REQUIRE(cfg.pluginName == "Vorago");
        REQUIRE(cfg.pluginCategoryDesc == "Synth");
        REQUIRE(cfg.subcategoryNames == std::vector<std::string>{"Drones"});
        REQUIRE(::Vorago::makeVoragoPresetTabLabels() ==
                std::vector<std::string>{"All", "Drones"});  // plan D-4 (T012)
        const bool processorUidMatches = (cfg.processorUID == ::Vorago::kProcessorUID);
        REQUIRE(processorUidMatches);

        namespace fs = std::filesystem;
        const auto n = std::to_string(nextTempCounter());
        const fs::path tempRoot = fs::temp_directory_path();
        const fs::path userDir = tempRoot / ("vorago_sc012_user_" + n);
        const fs::path factoryDir = tempRoot / ("vorago_sc012_factory_" + n);

        std::error_code ec;
        fs::remove_all(userDir, ec);
        fs::remove_all(factoryDir, ec);
        REQUIRE(fs::create_directories(userDir));
        REQUIRE(fs::create_directories(factoryDir / "Drones"));
        {
            // Zero-byte probe: the scan keys on the .vstpreset extension and the
            // parent directory name only (preset_manager.cpp:63, :95-101).
            std::ofstream probe(factoryDir / "Drones" / "Probe.vstpreset",
                                std::ios::binary);
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
        REQUIRE(firstSubcategory == "Drones");

        // FR-051: the filesystem half agrees with the config list.
        REQUIRE(fs::is_directory(VORAGO_RESOURCES_DIR "/presets/Drones"));

        const auto u = ::Vorago::makeVoragoUpdateConfig();
        REQUIRE(u.endpointUrl == "https://rolandzwaga.github.io/krate-audio/versions.json");
        REQUIRE(u.pluginName == "Vorago");
    }

    SECTION("HarnessCycles") {  // SC-016 a
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

        Krate::TestSupport::exerciseEditorLifecycle(
            *controller, "editor", std::string(VORAGO_RESOURCES_DIR) + "/editor.uidesc",
            /*cycles=*/10);

        REQUIRE(controller->presetManagerForTest() != nullptr);
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("EditorBindsSurface") {  // SC-016 a, FR-041, FR-042, FR-071
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

        // Resolved exactly as the harness does: absolute path to the source file.
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidescPath = std::string(VORAGO_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
                Steinberg::kResultTrue);
        REQUIRE(editor->getFrame() != nullptr);

        std::vector<VSTGUI::CControl*> controls;
        std::size_t ecosystemViewCount = 0;
        collectBoundControls(editor->getFrame(), controls, ecosystemViewCount);

        std::set<std::int32_t> tags;
        for (auto* control : controls) {
            tags.insert(control->getTag());
        }

        // Every registered ID except the hidden 4 (SustainPedal) and 5
        // (ChannelPressure). Explicit casts, not brace-narrowing: ParamID is uint32.
        std::set<std::int32_t> expected;
        for (const auto& row : VoragoTest::kExpectedParams) {
            if (row.id != 4u && row.id != 5u) {
                expected.insert(static_cast<std::int32_t>(row.id));
            }
        }
        REQUIRE(expected.size() == 106u);

        const std::size_t controlCount = controls.size();
        const bool tagsMatch = (tags == expected);
        const bool ecosystemViewWhileOpen = (controller->ecosystemViewForTest() != nullptr);
        const bool browserWhileOpen = (controller->presetBrowserViewForTest() != nullptr);

        // FR-071: the header button's open path reaches the overlay.
        controller->openPresetBrowser();
        const auto* browser = controller->presetBrowserViewForTest();
        const bool browserOpened = (browser != nullptr) && browser->isOpen();

        // Unknown names fall through to null (FR-042, FR-043).
        VSTGUI::UIAttributes noAttributes;
        VSTGUI::CView* unknownView =
            controller->createCustomView("Nope", noAttributes, nullptr, editor);
        VSTGUI::IController* unknownSub = controller->createSubController("Nope", nullptr, editor);
        const bool unknownViewNull = (unknownView == nullptr);
        const bool unknownSubNull = (unknownSub == nullptr);

        view->removed();
        const bool ecosystemViewAfterClose = (controller->ecosystemViewForTest() != nullptr);
        const bool browserAfterClose = (controller->presetBrowserViewForTest() != nullptr);
        view->release();
        REQUIRE(controller->terminate() == Steinberg::kResultOk);

        REQUIRE(controlCount == 106u);
        REQUIRE(tagsMatch);
        REQUIRE(ecosystemViewCount == 1u);
        REQUIRE(ecosystemViewWhileOpen);
        REQUIRE(browserWhileOpen);  // R-2: no save-dialog accessor to assert
        REQUIRE(browserOpened);
        REQUIRE(unknownViewNull);
        REQUIRE(unknownSubNull);
        REQUIRE_FALSE(ecosystemViewAfterClose);
        REQUIRE_FALSE(browserAfterClose);
    }

    SECTION("CreateViewNames") {  // FR-055
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        Krate::TestSupport::ensureVstguiInitialized();

        REQUIRE(controller->createView("nonsense") == nullptr);

        Steinberg::IPlugView* editorView = controller->createView(Steinberg::Vst::ViewType::kEditor);
        REQUIRE(editorView != nullptr);
        const bool isVst3Editor = dynamic_cast<VSTGUI::VST3Editor*>(editorView) != nullptr;
        editorView->release();
        REQUIRE(isVst3Editor);

        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }
}
