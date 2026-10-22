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
// SECTION "HarnessCycles" (FR-055): headless open/close cycles through the shared
// harness (tests/test_helpers/editor_lifecycle_harness.h:102), then the
// controller's preset manager is non-null.
//
// SECTION "EditorBindsSurface" (FR-054): the BUILT view tree carries exactly the
// eight bound controls {0, 1, 2, 3, 100, 101, 102, 103}; Voice Mode (1) and
// Polyphony (2) are COptionMenus (both are StringListParameters). The
// getTag() >= 0 filter is load-bearing: CTextLabel IS-A CControl and keeps tag -1
// when untagged, so an unfiltered walk would also count the labels.
// The editor is built with the ABSOLUTE uidesc path, exactly as the harness does:
// createView's "editor.uidesc" is a bundle-resource name that the test executable
// (not a plugin bundle) cannot resolve. CreateViewNames covers createView itself.
//
// SECTION "CreateViewNames" (FR-055): only ViewType::kEditor yields a view.
//
// NEVER name a kPlatformType* constant here - always
// Krate::TestSupport::nativePlatformType() (lint-platform-type-literals.js).
// ==============================================================================

#include "test_helpers/editor_lifecycle_harness.h"

#include "controller/controller.h"
#include "plugin_ids.h"
#include "preset/preset_manager.h"
#include "preset/profundum_preset_config.h"
#include "update/profundum_update_config.h"

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
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
        REQUIRE(cfg.subcategoryNames == std::vector<std::string>{"Sub"});
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
            /*cycles=*/3);

        REQUIRE(controller->presetManagerForTest() != nullptr);
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("EditorBindsSurface") {  // SC-013.3, FR-054
        auto controller = Steinberg::owned(new ::Profundum::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidescPath = std::string(PROFUNDUM_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
                Steinberg::kResultTrue);
        REQUIRE(editor->getFrame() != nullptr);

        std::vector<VSTGUI::CControl*> controls;
        collectBoundControls(editor->getFrame(), controls);

        std::set<std::int32_t> tags;
        bool voiceModeIsMenu = false;
        bool polyphonyIsMenu = false;
        for (auto* control : controls) {
            const std::int32_t tag = control->getTag();
            tags.insert(tag);
            const bool isMenu = dynamic_cast<VSTGUI::COptionMenu*>(control) != nullptr;
            if (tag == 1) {
                voiceModeIsMenu = isMenu;
            }
            if (tag == 2) {
                polyphonyIsMenu = isMenu;
            }
        }
        const std::size_t controlCount = controls.size();

        view->removed();
        view->release();
        REQUIRE(controller->terminate() == Steinberg::kResultOk);

        const std::set<std::int32_t> expected{0, 1, 2, 3, 100, 101, 102, 103};
        REQUIRE(controlCount == 8u);
        REQUIRE(tags == expected);
        REQUIRE(voiceModeIsMenu);
        REQUIRE(polyphonyIsMenu);
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
}
