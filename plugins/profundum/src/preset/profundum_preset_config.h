#pragma once

// ==============================================================================
// Profundum Preset Configuration (FR-050, FR-051)   Plan section 2.7
// ==============================================================================
// `Sub` is the Phase 10 seed category (Q4: permanent). The list is ADDITIVE-ONLY:
// Phase 13 may only add names. A rename orphans every preset saved against it,
// because PresetManager matches the parent directory name against
// `subcategoryNames` by exact `==`.
//
// Every name lives in TWO places that must agree: this list and the filesystem
// subdirectory `resources/presets/<Name>/`.
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"

namespace Profundum {

// Field order is load-bearing (plugins/shared/src/preset/preset_manager_config.h:18-23).
inline Krate::Plugins::PresetManagerConfig makeProfundumPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID      =*/kProcessorUID,
        /*.pluginName        =*/"Profundum",
        /*.pluginCategoryDesc=*/"Synth",
        /*.subcategoryNames  =*/{"Sub"}};  // Q4: permanent; Phase 13 may only add
}

}  // namespace Profundum
