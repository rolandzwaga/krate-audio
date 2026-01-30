#include "preset_paths.h"
#include <cstdlib>
#include <algorithm>
#include <cctype>

namespace Krate::Plugins::Platform {

std::filesystem::path getUserPresetDirectory(const std::string& pluginName) {
    namespace fs = std::filesystem;

#ifdef _WIN32
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return fs::path(userProfile) / "Documents" / "Krate Audio" / pluginName;
    }
    return {};
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / "Documents" / "Krate Audio" / pluginName;
    }
    return fs::path();
#else
    // Linux
    const char* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / "Documents" / "Krate Audio" / pluginName;
    }
    return fs::path();
#endif
}

std::filesystem::path getFactoryPresetDirectory(const std::string& pluginName) {
    namespace fs = std::filesystem;

#ifdef _WIN32
    const char* programData = std::getenv("PROGRAMDATA");
    if (programData) {
        return fs::path(programData) / "Krate Audio" / pluginName;
    }
    return {};
#elif defined(__APPLE__)
    return fs::path("/Library/Application Support/Krate Audio/" + pluginName);
#else
    // Linux - lowercase plugin name for Linux conventions
    std::string lowerName = pluginName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return fs::path("/usr/share/krate-audio/" + lowerName);
#endif
}

bool ensureDirectoryExists(const std::filesystem::path& path) {
    namespace fs = std::filesystem;

    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    if (fs::exists(path, ec)) {
        return fs::is_directory(path, ec);
    }

    return fs::create_directories(path, ec);
}

} // namespace Krate::Plugins::Platform
