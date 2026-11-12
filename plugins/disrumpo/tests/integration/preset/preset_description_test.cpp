// ==============================================================================
// Disrumpo - factory preset descriptions scan back (as Profundum_PresetDescriptions)
// ==============================================================================
// Every file of the committed factory tree (DISRUMPO_RESOURCES_DIR/presets) scans back through the shared
// PresetManager, the way the preset browser reads it (PresetManager::readMetadata, the Info chunk's Comment
// attribute; PresetBrowserView shows it on the line under the list), with a one-line description whose length
// is in the range the Profundum and Vorago libraries use (73 - 339 characters there).
// The descriptions are written by tools/disrumpo_preset_generator.cpp.
// ==============================================================================

#include "preset/disrumpo_preset_config.h"
#include "preset/preset_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>

#ifndef DISRUMPO_RESOURCES_DIR
#error "DISRUMPO_RESOURCES_DIR must be defined (plugins/disrumpo/tests/CMakeLists.txt)"
#endif

TEST_CASE("Every Disrumpo factory preset has a one-line description in the browser",
          "[disrumpo][preset][description]") {
    namespace fs = std::filesystem;
    constexpr std::size_t kFactoryPresetCount = 120;
    constexpr std::size_t kMinChars = 60;
    constexpr std::size_t kMaxChars = 240;

    const fs::path factory = fs::path(DISRUMPO_RESOURCES_DIR) / "presets";
    const fs::path emptyUser = fs::temp_directory_path() / "disrumpo_desc_no_user_presets";
    std::error_code ec;
    fs::create_directories(emptyUser, ec);

    Krate::Plugins::PresetManager manager(Disrumpo::makeDisrumpoPresetConfig(), nullptr, nullptr, emptyUser,
                                          factory);
    const auto scanned = manager.scanPresets();
    REQUIRE(scanned.size() == kFactoryPresetCount);

    std::size_t described = 0;
    std::set<std::string> distinct;
    for (const auto& p : scanned) {
        INFO("preset " << p.category << "/" << p.name << ": \"" << p.description << "\" ("
                       << p.description.size() << " chars)");
        CHECK(p.isFactory);
        CHECK(p.description.size() >= kMinChars);
        CHECK(p.description.size() <= kMaxChars);
        CHECK(p.description.find('\n') == std::string::npos);
        if (p.description.size() >= kMinChars && p.description.size() <= kMaxChars) {
            ++described;
        }
        distinct.insert(p.description);
    }
    CHECK(described == kFactoryPresetCount);
    // One description per preset, never a shared boilerplate line.
    CHECK(distinct.size() == kFactoryPresetCount);
    fs::remove_all(emptyUser, ec);
}
