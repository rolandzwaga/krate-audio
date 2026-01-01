// =============================================================================
// PresetDataSource Tests
// =============================================================================
// Spec 042: Preset Browser
// Tests for PresetDataSource filtering functionality
// =============================================================================

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "ui/preset_data_source.h"
#include "preset/preset_info.h"

// Test helper to create preset info
Iterum::PresetInfo makePreset(
    const std::string& name,
    const std::string& category,
    Iterum::DelayMode mode,
    bool isFactory = false
) {
    Iterum::PresetInfo info;
    info.name = name;
    info.category = category;
    info.mode = mode;
    info.path = "/presets/" + name + ".vstpreset";
    info.isFactory = isFactory;
    return info;
}

// =============================================================================
// Basic Data Management Tests
// =============================================================================

TEST_CASE("PresetDataSource basic data management", "[preset][datasource]") {
    Iterum::PresetDataSource dataSource;

    SECTION("initially has no presets") {
        REQUIRE(dataSource.getPresetAtRow(0) == nullptr);
    }

    SECTION("setPresets stores presets") {
        std::vector<Iterum::PresetInfo> presets = {
            makePreset("Preset A", "Ambient", Iterum::DelayMode::Shimmer),
            makePreset("Preset B", "Rhythmic", Iterum::DelayMode::Digital)
        };

        dataSource.setPresets(presets);

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Preset A");
        REQUIRE(dataSource.getPresetAtRow(1) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(1)->name == "Preset B");
    }

    SECTION("getPresetAtRow returns nullptr for invalid indices") {
        std::vector<Iterum::PresetInfo> presets = {
            makePreset("Only One", "Category", Iterum::DelayMode::Digital)
        };
        dataSource.setPresets(presets);

        REQUIRE(dataSource.getPresetAtRow(-1) == nullptr);
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);
        REQUIRE(dataSource.getPresetAtRow(100) == nullptr);
    }
}

// =============================================================================
// Mode Filter Tests
// =============================================================================

TEST_CASE("PresetDataSource mode filtering", "[preset][datasource][filter]") {
    Iterum::PresetDataSource dataSource;

    std::vector<Iterum::PresetInfo> presets = {
        makePreset("Digital 1", "Clean", Iterum::DelayMode::Digital),
        makePreset("Digital 2", "Rhythmic", Iterum::DelayMode::Digital),
        makePreset("Tape 1", "Vintage", Iterum::DelayMode::Tape),
        makePreset("Shimmer 1", "Ambient", Iterum::DelayMode::Shimmer),
        makePreset("Granular 1", "Experimental", Iterum::DelayMode::Granular)
    };
    dataSource.setPresets(presets);

    SECTION("mode filter -1 shows all presets") {
        dataSource.setModeFilter(-1);
        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(4) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(5) == nullptr);
    }

    SECTION("mode filter shows only matching presets") {
        dataSource.setModeFilter(static_cast<int>(Iterum::DelayMode::Digital));

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Digital 1");
        REQUIRE(dataSource.getPresetAtRow(1) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(1)->name == "Digital 2");
        REQUIRE(dataSource.getPresetAtRow(2) == nullptr);
    }

    SECTION("mode filter for Tape shows only Tape presets") {
        dataSource.setModeFilter(static_cast<int>(Iterum::DelayMode::Tape));

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Tape 1");
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);
    }

    SECTION("mode filter for non-existent mode shows empty list") {
        dataSource.setModeFilter(static_cast<int>(Iterum::DelayMode::Freeze));

        REQUIRE(dataSource.getPresetAtRow(0) == nullptr);
    }
}

// =============================================================================
// Search Filter Tests
// =============================================================================

TEST_CASE("PresetDataSource search filtering", "[preset][datasource][filter]") {
    Iterum::PresetDataSource dataSource;

    std::vector<Iterum::PresetInfo> presets = {
        makePreset("Ambient Pad", "Ambient", Iterum::DelayMode::Shimmer),
        makePreset("Clean Digital", "Clean", Iterum::DelayMode::Digital),
        makePreset("Tape Echo", "Vintage", Iterum::DelayMode::Tape),
        makePreset("AMBIENT WASH", "Ambient", Iterum::DelayMode::Shimmer)
    };
    dataSource.setPresets(presets);

    SECTION("empty search shows all presets") {
        dataSource.setSearchFilter("");
        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(3) != nullptr);
    }

    SECTION("search is case-insensitive") {
        dataSource.setSearchFilter("ambient");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Ambient Pad");
        REQUIRE(dataSource.getPresetAtRow(1) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(1)->name == "AMBIENT WASH");
        REQUIRE(dataSource.getPresetAtRow(2) == nullptr);
    }

    SECTION("search matches partial names") {
        dataSource.setSearchFilter("pad");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Ambient Pad");
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);
    }

    SECTION("search for non-existent term shows empty list") {
        dataSource.setSearchFilter("nonexistent");

        REQUIRE(dataSource.getPresetAtRow(0) == nullptr);
    }
}

// =============================================================================
// Combined Filter Tests
// =============================================================================

TEST_CASE("PresetDataSource combined filtering", "[preset][datasource][filter]") {
    Iterum::PresetDataSource dataSource;

    std::vector<Iterum::PresetInfo> presets = {
        makePreset("Ambient Shimmer", "Ambient", Iterum::DelayMode::Shimmer),
        makePreset("Ambient Digital", "Ambient", Iterum::DelayMode::Digital),
        makePreset("Clean Shimmer", "Clean", Iterum::DelayMode::Shimmer),
        makePreset("Clean Digital", "Clean", Iterum::DelayMode::Digital)
    };
    dataSource.setPresets(presets);

    SECTION("mode and search filters combine") {
        dataSource.setModeFilter(static_cast<int>(Iterum::DelayMode::Shimmer));
        dataSource.setSearchFilter("ambient");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Ambient Shimmer");
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);
    }

    SECTION("clearing search restores mode-filtered results") {
        dataSource.setModeFilter(static_cast<int>(Iterum::DelayMode::Digital));
        dataSource.setSearchFilter("ambient");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Ambient Digital");
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);

        // Clear search
        dataSource.setSearchFilter("");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(1) != nullptr);
    }

    SECTION("clearing mode filter restores search-filtered results") {
        dataSource.setModeFilter(static_cast<int>(Iterum::DelayMode::Shimmer));
        dataSource.setSearchFilter("clean");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Clean Shimmer");
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);

        // Clear mode filter
        dataSource.setModeFilter(-1);

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(1) != nullptr);
    }
}

// =============================================================================
// Callback Tests
// =============================================================================

TEST_CASE("PresetDataSource callbacks", "[preset][datasource][callback]") {
    Iterum::PresetDataSource dataSource;

    SECTION("selection callback can be set") {
        int lastSelected = -1;
        dataSource.setSelectionCallback([&lastSelected](int row) {
            lastSelected = row;
        });

        // Callback is stored but we can't trigger it without VSTGUI
        // Just verify it doesn't crash
        REQUIRE(lastSelected == -1);
    }

    SECTION("double-click callback can be set") {
        int lastDoubleClicked = -1;
        dataSource.setDoubleClickCallback([&lastDoubleClicked](int row) {
            lastDoubleClicked = row;
        });

        // Callback is stored but we can't trigger it without VSTGUI
        // Just verify it doesn't crash
        REQUIRE(lastDoubleClicked == -1);
    }
}

// =============================================================================
// Factory Preset Tests
// =============================================================================

TEST_CASE("PresetDataSource factory preset handling", "[preset][datasource]") {
    Iterum::PresetDataSource dataSource;

    std::vector<Iterum::PresetInfo> presets = {
        makePreset("User Preset", "User", Iterum::DelayMode::Digital, false),
        makePreset("Factory Preset", "Factory", Iterum::DelayMode::Digital, true)
    };
    dataSource.setPresets(presets);

    SECTION("factory flag is preserved") {
        const auto* userPreset = dataSource.getPresetAtRow(0);
        const auto* factoryPreset = dataSource.getPresetAtRow(1);

        REQUIRE(userPreset != nullptr);
        REQUIRE(factoryPreset != nullptr);
        REQUIRE_FALSE(userPreset->isFactory);
        REQUIRE(factoryPreset->isFactory);
    }
}
