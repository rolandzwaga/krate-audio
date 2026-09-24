// ==============================================================================
// Vorago - editor lifecycle tests (SC-012)
// ==============================================================================
// The [lifecycle] tag is REQUIRED: valgrind-nightly.yml selects cases by it.
//
// SECTION "PresetConfigIsLive" (SC-012.3, FR-050, FR-052): the preset config
// drives a LIVE PresetManager scan. Both directory overrides point at SEPARATE
// temp directories (P-4): scanPresets() scans user AND factory
// (preset_manager.cpp:41-49), so one shared directory double-counts, and an
// unset user override would read the machine's real user preset folder.
//
// SECTION "HarnessCycles" (SC-012.1, FR-055): three headless open/close cycles
// through the shared harness (tests/test_helpers/editor_lifecycle_harness.h:102),
// then the controller's own preset manager is non-null.
//
// SECTION "EditorBindsFourteenControls" (SC-012.2, FR-054): the BUILT view tree
// carries exactly the fourteen bound controls. The harness alone proves nothing
// about the file's contents - a template holding one CTextLabel passes it.
// The getTag() >= 0 filter is load-bearing: CTextLabel IS-A CControl and keeps
// tag -1 when untagged, so an unfiltered walk would also count the labels.
//
// NEVER name a kPlatformType* constant here - always
// Krate::TestSupport::nativePlatformType() (lint-platform-type-literals.js).
// ==============================================================================

#include "test_helpers/editor_lifecycle_harness.h"

#include "controller/controller.h"
#include "plugin_ids.h"
#include "preset/preset_manager.h"
#include "preset/vorago_preset_config.h"
#include "update/vorago_update_config.h"

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

TEST_CASE("Vorago_EditorLifecycle", "[vorago][controller][ui][lifecycle]") {
    SECTION("PresetConfigIsLive") {
        const auto cfg = ::Vorago::makeVoragoPresetConfig();
        REQUIRE(cfg.pluginName == "Vorago");
        REQUIRE(cfg.pluginCategoryDesc == "Synth");
        REQUIRE(cfg.subcategoryNames == std::vector<std::string>{"Drones"});
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

    SECTION("HarnessCycles") {  // SC-012.1
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

        Krate::TestSupport::exerciseEditorLifecycle(
            *controller, "editor", std::string(VORAGO_RESOURCES_DIR) + "/editor.uidesc");

        REQUIRE(controller->presetManagerForTest() != nullptr);
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("EditorBindsFourteenControls") {  // SC-012.2
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
        collectBoundControls(editor->getFrame(), controls);

        std::set<std::int32_t> tags;
        VSTGUI::CControl* polyphonyControl = nullptr;
        for (auto* control : controls) {
            tags.insert(control->getTag());
            if (control->getTag() == static_cast<std::int32_t>(::Vorago::kPolyphonyId)) {
                polyphonyControl = control;
            }
        }

        // Explicit casts, not brace-narrowing: ParamID is uint32.
        std::set<std::int32_t> expected{static_cast<std::int32_t>(::Vorago::kMasterGainId),
                                        static_cast<std::int32_t>(::Vorago::kPolyphonyId)};
        for (auto id = static_cast<std::int32_t>(::Vorago::kMacroDarknessId);
             id <= static_cast<std::int32_t>(::Vorago::kMacroMassId); ++id) {
            expected.insert(id);
        }
        REQUIRE(expected.size() == 14u);

        const std::size_t controlCount = controls.size();
        const bool tagsMatch = (tags == expected);
        const bool polyphonyIsMenu =
            dynamic_cast<VSTGUI::COptionMenu*>(polyphonyControl) != nullptr;

        view->removed();
        view->release();
        REQUIRE(controller->terminate() == Steinberg::kResultOk);

        REQUIRE(controlCount == 14u);
        REQUIRE(tagsMatch);
        REQUIRE(polyphonyIsMenu);
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
