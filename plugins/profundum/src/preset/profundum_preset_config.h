#pragma once

// ==============================================================================
// Profundum Preset Configuration (FR-050, FR-051)   Plan section 2.7
// ==============================================================================
// `Sub` is the Phase 10 seed category (Q4: permanent). The list is ADDITIVE-ONLY:
// Phase 13 may only add names. A rename orphans every preset saved against it,
// because PresetManager matches the parent directory name against
// `subcategoryNames` by exact `==`.
//
// Phase 13 (Q1): six names, permanent once shipped - Sub, Resonant, Harmonic,
// Driven, Plucked, Animated (specs/profundum-phase13-presets-release/spec.md FR-001).
// Phase 14b adds the seventh, Kicks (specs/Profundum-roadmap.md Phase 14b).
//
// Every name lives in TWO places that must agree: this list and the filesystem
// subdirectory `resources/presets/<Name>/`.
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"

#include <string>
#include <vector>

namespace Profundum {

// Field order is load-bearing (plugins/shared/src/preset/preset_manager_config.h:18-23).
inline Krate::Plugins::PresetManagerConfig makeProfundumPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID      =*/kProcessorUID,
        /*.pluginName        =*/"Profundum",
        /*.pluginCategoryDesc=*/"Synth",
        /*.subcategoryNames  =*/{"Sub", "Resonant", "Harmonic", "Driven", "Plucked", "Animated", "Kicks"}};
}

/// The preset browser tab labels: "All" first, then the config subcategory list in
/// order (copied from makeVoragoPresetTabLabels(),
/// plugins/vorago/src/preset/vorago_preset_config.h:40-48).
[[nodiscard]] inline std::vector<std::string> makeProfundumPresetTabLabels() {
    const Krate::Plugins::PresetManagerConfig config = makeProfundumPresetConfig();
    std::vector<std::string> tabLabels;
    tabLabels.reserve(config.subcategoryNames.size() + 1u);
    tabLabels.emplace_back("All");
    tabLabels.insert(tabLabels.end(), config.subcategoryNames.begin(),
                     config.subcategoryNames.end());
    return tabLabels;
}

}  // namespace Profundum
