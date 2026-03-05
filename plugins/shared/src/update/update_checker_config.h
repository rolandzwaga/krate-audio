#pragma once

// ==============================================================================
// UpdateCheckerConfig - Plugin-Specific Update Check Configuration
// ==============================================================================
// Configuration structure provided by each plugin to customize update checking.
// Follows the PresetManagerConfig pattern.
// ==============================================================================

#include <string>

namespace Krate::Plugins {

struct UpdateCheckerConfig {
    std::string pluginName;      // e.g., "Iterum"
    std::string currentVersion;  // e.g., "0.14.2" from VERSION_STR
    std::string endpointUrl;     // URL to versions.json endpoint
};

} // namespace Krate::Plugins
