#pragma once

// ==============================================================================
// PresetManagerConfig - Plugin-Specific Preset Configuration
// ==============================================================================
// Configuration structure provided by each plugin to customize shared preset
// operations. Enables the shared library to work with any Krate Audio plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include <string>
#include <vector>

namespace Krate::Plugins {

// IMPORTANT: Field order matters for C++20 designated initializers.
// All designated initializer usage must match this declaration order.
// If fields are reordered, update ALL initializer sites.
struct PresetManagerConfig {
    Steinberg::FUID processorUID;                  // .vstpreset file header identification
    std::string pluginName;                        // Directory path segment ("Iterum", "Disrumpo")
    std::string pluginCategoryDesc;                // Metadata field ("Delay", "Distortion")
    std::vector<std::string> subcategoryNames;     // Subfolder names for scanning/saving
};

} // namespace Krate::Plugins
