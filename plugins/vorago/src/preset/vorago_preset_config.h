#pragma once

// ==============================================================================
// Vorago Preset Configuration (FR-050, FR-051)   Plan section 2.7
// ==============================================================================
// The seven categories, in this order, were ratified in Clarifications session
// 2026-09-27 Q6 (specs/vorago-phase14-presets-release/spec.md FR-001): Drones,
// Abyss, Caverns, Organisms, Machines, Textures, Ghosts. `Drones` is the Phase 11
// seed category and stays verbatim. The list is ADDITIVE-ONLY: a rename orphans every
// preset saved against it, because PresetManager::parsePresetFile matches the
// parent directory name against `subcategoryNames` by exact `==` and leaves
// `subcategory` EMPTY on a miss (plugins/shared/src/preset/preset_manager.cpp:95-103).
//
// Every name lives in TWO places that must agree: this list and the filesystem
// subdirectory `resources/presets/<Name>/`.
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"

#include <string>
#include <vector>

namespace Vorago {

// Field order is load-bearing (plugins/shared/src/preset/preset_manager_config.h:19-24).
inline Krate::Plugins::PresetManagerConfig makeVoragoPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID      =*/kProcessorUID,
        /*.pluginName        =*/"Vorago",
        /*.pluginCategoryDesc=*/"Synth",
        /*.subcategoryNames  =*/{"Drones", "Abyss", "Caverns", "Organisms", "Machines",
                                 "Textures", "Ghosts"}};
}

/// The preset browser tab labels: "All" first, then the config subcategory list in
/// order. The controller builds the browser from this function, so a test can pin the
/// exact list the controller uses (copied from makeSeraphisPresetTabLabels(),
/// plugins/seraphis/src/preset/seraphis_preset_config.h:52-60).
[[nodiscard]] inline std::vector<std::string> makeVoragoPresetTabLabels() {
    const Krate::Plugins::PresetManagerConfig config = makeVoragoPresetConfig();
    std::vector<std::string> tabLabels;
    tabLabels.reserve(config.subcategoryNames.size() + 1u);
    tabLabels.emplace_back("All");
    tabLabels.insert(tabLabels.end(), config.subcategoryNames.begin(),
                     config.subcategoryNames.end());
    return tabLabels;
}

}  // namespace Vorago
