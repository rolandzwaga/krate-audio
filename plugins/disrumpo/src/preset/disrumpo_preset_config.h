#pragma once

// ==============================================================================
// Disrumpo Preset Configuration
// ==============================================================================
// Plugin-specific configuration for the shared preset system.
// Provides Disrumpo's processor UID, plugin name, category, and subcategories.
//
// Reference: specs/010-preset-system/contracts/disrumpo-preset-api.md
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"

#include <string>
#include <vector>

namespace Disrumpo {

/// @brief Create Disrumpo-specific preset manager configuration.
///
/// Provides:
/// - Processor UID for .vstpreset file identification
/// - Plugin name "Disrumpo" for directory paths
/// - Category description "Distortion" for metadata
/// - 11 subcategories matching Disrumpo's preset organization (FR-027)
inline Krate::Plugins::PresetManagerConfig makeDisrumpoPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID =*/ kProcessorUID,
        /*.pluginName =*/ "Disrumpo",
        /*.pluginCategoryDesc =*/ "Distortion",
        /*.subcategoryNames =*/ {
            "Init", "Sweep", "Morph", "Bass", "Leads",
            "Pads", "Drums", "Experimental", "Chaos", "Dynamic", "Lo-Fi"
        }
    };
}

/// @brief Get tab labels for the Disrumpo preset browser.
///
/// Includes "All" as the first tab followed by the 11 subcategories.
/// Total: 12 tabs (FR-016, FR-019).
inline std::vector<std::string> getDisrumpoTabLabels() {
    return {
        "All", "Init", "Sweep", "Morph", "Bass", "Leads",
        "Pads", "Drums", "Experimental", "Chaos", "Dynamic", "Lo-Fi"
    };
}

} // namespace Disrumpo
