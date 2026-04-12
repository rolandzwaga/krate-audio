#pragma once

// ==============================================================================
// MembrumPresetConfig -- Kit and pad preset configurations for Membrum Phase 4
// ==============================================================================
// FR-050: Kit presets use PresetManager with subcategories
// FR-060: Pad presets use PresetManager with drum-type subcategories
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "plugin_ids.h"

namespace Membrum {

/// Kit preset configuration: saves/loads all 32 pad configurations.
/// Subcategories: Electronic, Acoustic, Experimental, Cinematic.
inline Krate::Plugins::PresetManagerConfig kitPresetConfig()
{
    return Krate::Plugins::PresetManagerConfig{
        .processorUID       = kProcessorUID,
        .pluginName         = "Membrum",
        .pluginCategoryDesc = "Kit Presets",
        .subcategoryNames   = {"Electronic", "Acoustic", "Experimental", "Cinematic"},
    };
}

/// Pad preset configuration: saves/loads a single pad's sound.
/// Subcategories: Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, 808, FX.
inline Krate::Plugins::PresetManagerConfig padPresetConfig()
{
    return Krate::Plugins::PresetManagerConfig{
        .processorUID       = kProcessorUID,
        .pluginName         = "Membrum",
        .pluginCategoryDesc = "Pad Presets",
        .subcategoryNames   = {"Kick", "Snare", "Tom", "Hat", "Cymbal", "Perc",
                               "Tonal", "808", "FX"},
    };
}

} // namespace Membrum
