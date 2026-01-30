#pragma once

// ==============================================================================
// Platform-Specific Preset Paths (Shared)
// ==============================================================================
// Constitution Principle VI: Cross-Platform Compatibility
// - Uses std::filesystem for portable path handling
// - Platform detection via preprocessor macros
// - Parameterized by plugin name for shared library use
// ==============================================================================

#include <filesystem>
#include <string>

namespace Krate::Plugins::Platform {

/// Get user preset directory (writable)
/// Windows:  %USERPROFILE%\Documents\Krate Audio\{pluginName}
/// macOS:    ~/Documents/Krate Audio/{pluginName}
/// Linux:    ~/Documents/Krate Audio/{pluginName}
std::filesystem::path getUserPresetDirectory(const std::string& pluginName);

/// Get factory preset directory (read-only)
/// Windows:  %PROGRAMDATA%\Krate Audio\{pluginName}
/// macOS:    /Library/Application Support/Krate Audio/{pluginName}
/// Linux:    /usr/share/krate-audio/{pluginName}
std::filesystem::path getFactoryPresetDirectory(const std::string& pluginName);

/// Ensure directory exists, creating it if necessary
/// @return true if directory exists or was created
bool ensureDirectoryExists(const std::filesystem::path& path);

} // namespace Krate::Plugins::Platform
