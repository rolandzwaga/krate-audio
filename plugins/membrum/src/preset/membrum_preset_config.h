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
/// Phase 6 (T052): pluginName uses "Membrum/Kits" so that the two preset
/// scopes (kits and pads) resolve to separate directories per spec 141:
/// `{ProgramData}/Krate Audio/Membrum/Kits/` and `.../Pads/`.
inline Krate::Plugins::PresetManagerConfig kitPresetConfig()
{
    return Krate::Plugins::PresetManagerConfig{
        .processorUID       = kProcessorUID,
        .pluginName         = "Membrum/Kits",
        .pluginCategoryDesc = "Kit Presets",
        .subcategoryNames   = {"Acoustic", "Electronic", "Percussive", "Unnatural"},
    };
}

/// Pad preset configuration: saves/loads a single pad's sound.
/// Subcategories: Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, FX.
/// Phase 6 (T052): pluginName uses "Membrum/Pads" so per-pad presets live in
/// `{ProgramData}/Krate Audio/Membrum/Pads/` (research.md section 11).
inline Krate::Plugins::PresetManagerConfig padPresetConfig()
{
    return Krate::Plugins::PresetManagerConfig{
        .processorUID       = kProcessorUID,
        .pluginName         = "Membrum/Pads",
        .pluginCategoryDesc = "Pad Presets",
        .subcategoryNames   = {"Kick", "Snare", "Tom", "Hat", "Cymbal", "Perc",
                               "Tonal", "FX"},
    };
}

} // namespace Membrum
