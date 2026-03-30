#pragma once

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"
#include <string>
#include <vector>

namespace Gradus {

inline Krate::Plugins::PresetManagerConfig makeGradusPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID =*/ kProcessorUID,
        /*.pluginName =*/ "Gradus",
        /*.pluginCategoryDesc =*/ "Instrument",
        /*.subcategoryNames =*/ {
            "Arp Classic", "Arp Acid", "Arp Euclidean", "Arp Polymetric",
            "Arp Generative", "Arp Performance", "Arp Chords", "Arp Advanced"
        }
    };
}

inline std::vector<std::string> getGradusTabLabels() {
    return {
        "All",
        "Arp Classic", "Arp Acid", "Arp Euclidean", "Arp Polymetric",
        "Arp Generative", "Arp Performance", "Arp Chords", "Arp Advanced"
    };
}

} // namespace Gradus
