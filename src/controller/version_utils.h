#pragma once

// ==============================================================================
// Version Utility Functions
// ==============================================================================
// Functions for reading and formatting version strings from version.json
// ==============================================================================

#include <string>
#include <fstream>
#include <sstream>

namespace Iterum {

/// Parse version string from JSON content
/// @param jsonContent String containing JSON with "version" field
/// @return Version string (e.g. "0.1.2") or empty string on failure
inline std::string parseVersionFromJson(const std::string& jsonContent) {
    // Simple JSON parsing - find "version": "X.Y.Z" pattern
    size_t versionPos = jsonContent.find("\"version\"");
    if (versionPos == std::string::npos) {
        return "";
    }

    // Find the colon after "version"
    size_t colonPos = jsonContent.find(':', versionPos + 9);
    if (colonPos == std::string::npos) {
        return "";
    }

    // Find the opening quote of the value (after the colon)
    size_t valueStart = jsonContent.find('\"', colonPos);
    if (valueStart == std::string::npos) {
        return "";
    }

    // Find the closing quote
    size_t valueEnd = jsonContent.find('\"', valueStart + 1);
    if (valueEnd == std::string::npos) {
        return "";
    }

    // Extract version string
    return jsonContent.substr(valueStart + 1, valueEnd - valueStart - 1);
}

/// Read version from version.json file
/// @param filePath Path to version.json file
/// @return Version string or empty string on failure
inline std::string readVersionFromFile(const std::string& filePath = "version.json") {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parseVersionFromJson(buffer.str());
}

/// Format version string for display
/// @param version Version number (e.g. "0.1.2")
/// @return Formatted string (e.g. "Iterum v0.1.2")
inline std::string formatVersionString(const std::string& version) {
    if (version.empty()) {
        return "Iterum v?.?.?";
    }
    return "Iterum v" + version;
}

} // namespace Iterum
