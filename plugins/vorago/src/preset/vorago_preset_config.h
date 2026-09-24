#pragma once

// ==============================================================================
// Vorago Preset Configuration (FR-050, FR-051)   Plan section 2.7
// ==============================================================================
// `Drones` is the seed category. The list is ADDITIVE-ONLY: a rename orphans every
// preset saved against it, because PresetManager::parsePresetFile matches the
// parent directory name against `subcategoryNames` by exact `==` and leaves
// `subcategory` EMPTY on a miss (plugins/shared/src/preset/preset_manager.cpp:95-103).
//
// Every name lives in TWO places that must agree: this list and the filesystem
// subdirectory `resources/presets/<Name>/`.
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"

namespace Vorago {

// Field order is load-bearing (plugins/shared/src/preset/preset_manager_config.h:19-24).
inline Krate::Plugins::PresetManagerConfig makeVoragoPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID      =*/kProcessorUID,
        /*.pluginName        =*/"Vorago",
        /*.pluginCategoryDesc=*/"Synth",
        /*.subcategoryNames  =*/{"Drones"}};
}

}  // namespace Vorago
