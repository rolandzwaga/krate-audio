// ==============================================================================
// Profundum - factory preset descriptions scan back (Phase 13 T037; FR-065, SC-033 (b))
// ==============================================================================
// Every file of the committed factory tree (PROFUNDUM_PRESETS_DIR) scans back through the shared PresetManager
// with exactly its def's description (tools/profundum_preset_defs.h). The scan reads the Info chunk's Comment
// attribute (PresetManager::readMetadata, FR-065).
// ==============================================================================

#include "preset/preset_manager.h"
#include "preset/profundum_preset_config.h"
#include "profundum_preset_defs.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#ifndef PROFUNDUM_PRESETS_DIR
#error "PROFUNDUM_PRESETS_DIR must be defined (plugins/profundum/tests/CMakeLists.txt)"
#endif

TEST_CASE("Profundum_PresetDescriptions", "[profundum][preset][description]") {
    namespace fs = std::filesystem;
    const fs::path factory(PROFUNDUM_PRESETS_DIR);
    const fs::path emptyUser = fs::temp_directory_path() / "profundum_desc_no_user_presets";
    std::error_code ec;
    fs::create_directories(emptyUser, ec);

    Krate::Plugins::PresetManager manager(Profundum::makeProfundumPresetConfig(), nullptr, nullptr, emptyUser, factory);
    const auto scanned = manager.scanPresets();
    const auto& defs = Profundum::PresetDefs::allPresets();
    REQUIRE(scanned.size() == defs.size());

    std::size_t matched = 0;
    for (const auto& def : defs) {
        const std::string name(def.name);
        INFO("preset " << name);
        const Krate::Plugins::PresetInfo* found = nullptr;
        for (const auto& p : scanned) {
            if (p.name == name) {
                found = &p;
            }
        }
        REQUIRE(found != nullptr);
        CHECK_FALSE(def.description.empty());
        CHECK(found->description == std::string(def.description));
        CHECK(found->category == std::string(def.category));
        if (found->description == std::string(def.description)) {
            ++matched;
        }
    }
    CHECK(matched == defs.size());
    fs::remove_all(emptyUser, ec);
}
