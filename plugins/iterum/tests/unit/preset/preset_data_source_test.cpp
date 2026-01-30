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
Krate::Plugins::PresetInfo makePreset(
    const std::string& name,
    const std::string& category,
    const std::string& subcategory,
    bool isFactory = false
) {
    Krate::Plugins::PresetInfo info;
    info.name = name;
    info.category = category;
    info.subcategory = subcategory;
    info.path = "/presets/" + name + ".vstpreset";
    info.isFactory = isFactory;
    return info;
}

// =============================================================================
// Basic Data Management Tests
// =============================================================================

TEST_CASE("PresetDataSource basic data management", "[preset][datasource]") {
    Krate::Plugins::PresetDataSource dataSource;

    SECTION("initially has no presets") {
        REQUIRE(dataSource.getPresetAtRow(0) == nullptr);
    }

    SECTION("setPresets stores presets") {
        std::vector<Krate::Plugins::PresetInfo> presets = {
            makePreset("Preset A", "Ambient", "Shimmer"),
            makePreset("Preset B", "Rhythmic", "Digital")
        };

        dataSource.setPresets(presets);

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Preset A");
        REQUIRE(dataSource.getPresetAtRow(1) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(1)->name == "Preset B");
    }

    SECTION("getPresetAtRow returns nullptr for invalid indices") {
        std::vector<Krate::Plugins::PresetInfo> presets = {
            makePreset("Only One", "Category", "Digital")
        };
        dataSource.setPresets(presets);

        REQUIRE(dataSource.getPresetAtRow(-1) == nullptr);
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);
        REQUIRE(dataSource.getPresetAtRow(100) == nullptr);
    }
}

// =============================================================================
// Subcategory Filter Tests
// =============================================================================

TEST_CASE("PresetDataSource subcategory filtering", "[preset][datasource][filter]") {
    Krate::Plugins::PresetDataSource dataSource;

    std::vector<Krate::Plugins::PresetInfo> presets = {
        makePreset("Digital 1", "Clean", "Digital"),
        makePreset("Digital 2", "Rhythmic", "Digital"),
        makePreset("Tape 1", "Vintage", "Tape"),
        makePreset("Shimmer 1", "Ambient", "Shimmer"),
        makePreset("Granular 1", "Experimental", "Granular")
    };
    dataSource.setPresets(presets);

    SECTION("empty subcategory filter shows all presets") {
        dataSource.setSubcategoryFilter("");
        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(4) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(5) == nullptr);
    }

    SECTION("subcategory filter shows only matching presets") {
        dataSource.setSubcategoryFilter("Digital");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Digital 1");
        REQUIRE(dataSource.getPresetAtRow(1) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(1)->name == "Digital 2");
        REQUIRE(dataSource.getPresetAtRow(2) == nullptr);
    }

    SECTION("subcategory filter for Tape shows only Tape presets") {
        dataSource.setSubcategoryFilter("Tape");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Tape 1");
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);
    }

    SECTION("subcategory filter for non-existent subcategory shows empty list") {
        dataSource.setSubcategoryFilter("Freeze");

        REQUIRE(dataSource.getPresetAtRow(0) == nullptr);
    }
}

// =============================================================================
// Search Filter Tests
// =============================================================================

TEST_CASE("PresetDataSource search filtering", "[preset][datasource][filter]") {
    Krate::Plugins::PresetDataSource dataSource;

    std::vector<Krate::Plugins::PresetInfo> presets = {
        makePreset("Ambient Pad", "Ambient", "Shimmer"),
        makePreset("Clean Digital", "Clean", "Digital"),
        makePreset("Tape Echo", "Vintage", "Tape"),
        makePreset("AMBIENT WASH", "Ambient", "Shimmer")
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
    Krate::Plugins::PresetDataSource dataSource;

    std::vector<Krate::Plugins::PresetInfo> presets = {
        makePreset("Ambient Shimmer", "Ambient", "Shimmer"),
        makePreset("Ambient Digital", "Ambient", "Digital"),
        makePreset("Clean Shimmer", "Clean", "Shimmer"),
        makePreset("Clean Digital", "Clean", "Digital")
    };
    dataSource.setPresets(presets);

    SECTION("subcategory and search filters combine") {
        dataSource.setSubcategoryFilter("Shimmer");
        dataSource.setSearchFilter("ambient");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Ambient Shimmer");
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);
    }

    SECTION("clearing search restores subcategory-filtered results") {
        dataSource.setSubcategoryFilter("Digital");
        dataSource.setSearchFilter("ambient");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Ambient Digital");
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);

        // Clear search
        dataSource.setSearchFilter("");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(1) != nullptr);
    }

    SECTION("clearing subcategory filter restores search-filtered results") {
        dataSource.setSubcategoryFilter("Shimmer");
        dataSource.setSearchFilter("clean");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(0)->name == "Clean Shimmer");
        REQUIRE(dataSource.getPresetAtRow(1) == nullptr);

        // Clear subcategory filter
        dataSource.setSubcategoryFilter("");

        REQUIRE(dataSource.getPresetAtRow(0) != nullptr);
        REQUIRE(dataSource.getPresetAtRow(1) != nullptr);
    }
}

// =============================================================================
// Callback Tests
// =============================================================================

TEST_CASE("PresetDataSource callbacks", "[preset][datasource][callback]") {
    Krate::Plugins::PresetDataSource dataSource;

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
    Krate::Plugins::PresetDataSource dataSource;

    std::vector<Krate::Plugins::PresetInfo> presets = {
        makePreset("User Preset", "User", "Digital", false),
        makePreset("Factory Preset", "Factory", "Digital", true)
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
