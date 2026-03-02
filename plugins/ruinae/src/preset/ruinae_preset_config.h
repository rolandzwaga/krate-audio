#pragma once

// ==============================================================================
// Ruinae Preset Configuration
// ==============================================================================
// Provides the PresetManagerConfig for Ruinae and adapter functions between
// preset categories and the shared library.
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"
#include <string>
#include <vector>

namespace Ruinae {

inline Krate::Plugins::PresetManagerConfig makeRuinaePresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID =*/ kProcessorUID,
        /*.pluginName =*/ "Ruinae",
        /*.pluginCategoryDesc =*/ "Synth",
        /*.subcategoryNames =*/ {
            "Pads", "Leads", "Bass", "Textures", "Rhythmic", "Experimental",
            "Arp Classic", "Arp Acid", "Arp Euclidean", "Arp Polymetric",
            "Arp Generative", "Arp Performance"
        }
    };
}

/// Get Ruinae synth tab labels (regular preset browser)
inline std::vector<std::string> getRuinaeTabLabels() {
    return {
        "All",
        "Pads", "Leads", "Bass", "Textures", "Rhythmic", "Experimental"
    };
}

/// Get Ruinae ARP tab labels (future ARP-specific browser)
inline std::vector<std::string> getRuinaeArpTabLabels() {
    return {
        "All",
        "Arp Classic", "Arp Acid", "Arp Euclidean", "Arp Polymetric",
        "Arp Generative", "Arp Performance"
    };
}

} // namespace Ruinae
