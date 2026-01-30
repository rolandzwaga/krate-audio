#pragma once

// ==============================================================================
// Iterum Preset Configuration
// ==============================================================================
// Provides the PresetManagerConfig for Iterum and adapter functions between
// DelayMode enum and string subcategory used by the shared library.
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"
#include "../delay_mode.h"
#include <string>
#include <utility>

namespace Iterum {

inline Krate::Plugins::PresetManagerConfig makeIterumPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID =*/ kProcessorUID,
        /*.pluginName =*/ "Iterum",
        /*.pluginCategoryDesc =*/ "Delay",
        /*.subcategoryNames =*/ {
            "Granular", "Spectral", "Shimmer", "Tape", "BBD",
            "Digital", "PingPong", "Reverse", "MultiTap", "Freeze", "Ducking"
        }
    };
}

/// Convert DelayMode to subcategory string for save operations
inline std::string delayModeToSubcategory(DelayMode mode) {
    static const char* names[] = {
        "Granular", "Spectral", "Shimmer", "Tape", "BBD",
        "Digital", "PingPong", "Reverse", "MultiTap", "Freeze", "Ducking"
    };
    int idx = static_cast<int>(mode);
    if (idx >= 0 && idx < static_cast<int>(DelayMode::NumModes))
        return names[idx];
    return "Digital";
}

/// Convert subcategory string back to DelayMode for Iterum-specific logic
inline DelayMode subcategoryToDelayMode(const std::string& subcategory) {
    static const std::pair<std::string, DelayMode> mapping[] = {
        {"Granular", DelayMode::Granular}, {"Spectral", DelayMode::Spectral},
        {"Shimmer", DelayMode::Shimmer},   {"Tape", DelayMode::Tape},
        {"BBD", DelayMode::BBD},           {"Digital", DelayMode::Digital},
        {"PingPong", DelayMode::PingPong}, {"Reverse", DelayMode::Reverse},
        {"MultiTap", DelayMode::MultiTap}, {"Freeze", DelayMode::Freeze},
        {"Ducking", DelayMode::Ducking}
    };
    for (const auto& [name, mode] : mapping) {
        if (subcategory == name) return mode;
    }
    return DelayMode::Digital;
}

/// Get Iterum tab labels (All + 11 mode names)
inline std::vector<std::string> getIterumTabLabels() {
    return {
        "All",
        "Granular", "Spectral", "Shimmer", "Tape", "BBD",
        "Digital", "PingPong", "Reverse", "MultiTap", "Freeze", "Ducking"
    };
}

} // namespace Iterum
