#pragma once

// ==============================================================================
// PresetInfo - Preset Metadata Structure (Shared)
// ==============================================================================
// Contains metadata for a single preset file.
// Generalized from Iterum's version: DelayMode replaced with string subcategory.
// ==============================================================================

#include <string>
#include <filesystem>

namespace Krate::Plugins {

struct PresetInfo {
    std::string name;                    // Display name (from filename or metadata)
    std::string category;                // Category label (e.g., "Ambient", "Rhythmic")
    std::string subcategory;             // Directory-derived subcategory (e.g., "Granular", "Bass")
    std::filesystem::path path;          // Full path to .vstpreset file
    bool isFactory = false;              // True if factory preset (read-only)
    std::string description;             // Optional description text
    std::string author;                  // Optional author name

    /// Check if preset info is valid (has name and path)
    [[nodiscard]] bool isValid() const {
        return !name.empty() && !path.empty();
    }

    /// Compare presets by name
    bool operator<(const PresetInfo& other) const {
        return name < other.name;
    }
};

} // namespace Krate::Plugins
