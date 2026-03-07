#pragma once

// ==============================================================================
// Innexus Preset Configuration
// ==============================================================================
// Provides the PresetManagerConfig for Innexus and tab labels for the preset
// browser. Categories are organized by input signal type.
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"
#include <string>
#include <vector>

namespace Innexus {

inline Krate::Plugins::PresetManagerConfig makeInnexusPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID =*/ kProcessorUID,
        /*.pluginName =*/ "Innexus",
        /*.pluginCategoryDesc =*/ "Instrument",
        /*.subcategoryNames =*/ {
            "Voice", "Strings", "Keys", "Brass and Winds",
            "Drums and Perc", "Pads and Drones", "Found Sound"
        }
    };
}

inline std::vector<std::string> getInnexusTabLabels() {
    return {
        "All",
        "Voice", "Strings", "Keys", "Brass and Winds",
        "Drums and Perc", "Pads and Drones", "Found Sound"
    };
}

} // namespace Innexus
