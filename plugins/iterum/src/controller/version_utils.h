#pragma once

// ==============================================================================
// Version Utility Functions
// ==============================================================================
// Simple wrappers around compile-time version constants from version.h
// ==============================================================================

#include "version.h"
#include <string>

namespace Iterum {

/// Get the UI version string (compile-time constant)
/// @return Version string (e.g. "Iterum v0.1.2")
inline std::string getUIVersionString() {
    return UI_VERSION_STR;
}

/// Get the version number only (compile-time constant)
/// @return Version string (e.g. "0.1.2")
inline std::string getVersionString() {
    return VERSION_STR;
}

/// Get the plugin name (compile-time constant)
/// @return Plugin name (e.g. "Iterum")
inline std::string getPluginName() {
    return stringPluginName;
}

} // namespace Iterum
