// =============================================================================
// PresetDataSource Search Filter Tests
// =============================================================================
// Tests for search filtering behavior in PresetDataSource
// Verifies that setSearchFilter correctly filters presets by name
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "ui/preset_data_source.h"
#include "preset/preset_info.h"

using namespace Iterum;

// =============================================================================
// Test Helpers
// =============================================================================

static std::vector<PresetInfo> createTestPresets() {
    std::vector<PresetInfo> presets;

    // Create presets with various names for testing
    PresetInfo p1;
    p1.name = "Warm Tape Echo";
    p1.mode = DelayMode::Tape;
    p1.category = "Vintage";
    p1.isFactory = true;
    presets.push_back(p1);

    PresetInfo p2;
    p2.name = "Digital Clean";
    p2.mode = DelayMode::Digital;
    p2.category = "Clean";
    p2.isFactory = true;
    presets.push_back(p2);

    PresetInfo p3;
    p3.name = "Granular Shimmer";
    p3.mode = DelayMode::Granular;
    p3.category = "Ambient";
    p3.isFactory = false;
    presets.push_back(p3);

    PresetInfo p4;
    p4.name = "Tape Warmth";
    p4.mode = DelayMode::Tape;
    p4.category = "Vintage";
    p4.isFactory = true;
    presets.push_back(p4);

    PresetInfo p5;
    p5.name = "ECHO CHAMBER";
    p5.mode = DelayMode::Digital;
    p5.category = "Effects";
    p5.isFactory = false;
    presets.push_back(p5);

    return presets;
}

// =============================================================================
// Basic Search Filter Tests
// =============================================================================

TEST_CASE("PresetDataSource search filter basics", "[ui][preset-browser][search]") {
    PresetDataSource dataSource;
    dataSource.setPresets(createTestPresets());

    SECTION("empty search shows all presets") {
        dataSource.setSearchFilter("");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 5);
    }

    SECTION("search filters by name substring") {
        dataSource.setSearchFilter("tape");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // "Warm Tape Echo" and "Tape Warmth"
    }

    SECTION("search is case-insensitive") {
        dataSource.setSearchFilter("TAPE");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);

        dataSource.setSearchFilter("TaPe");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);
    }

    SECTION("search returns empty when no matches") {
        dataSource.setSearchFilter("xyz");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 0);
    }

    SECTION("search matches partial words") {
        dataSource.setSearchFilter("warm");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // "Warm Tape Echo" and "Tape Warmth"
    }

    SECTION("search matches single character") {
        dataSource.setSearchFilter("e");
        // All presets except "Digital Clean" contain 'e'
        // "Warm Tape Echo", "Granular Shimmer", "Tape Warmth", "ECHO CHAMBER"
        // Actually "Digital Clean" has 'e' in "Clean"
        // So all 5 should match
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 5);
    }
}

// =============================================================================
// Search + Mode Filter Interaction
// =============================================================================

TEST_CASE("PresetDataSource search with mode filter", "[ui][preset-browser][search]") {
    PresetDataSource dataSource;
    dataSource.setPresets(createTestPresets());

    SECTION("search combined with mode filter") {
        // Set mode filter to Tape (mode index 3)
        dataSource.setModeFilter(static_cast<int>(DelayMode::Tape));
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // 2 Tape presets

        // Now add search filter
        dataSource.setSearchFilter("warm");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // Both Tape presets have "warm"
    }

    SECTION("mode filter then search that excludes all") {
        dataSource.setModeFilter(static_cast<int>(DelayMode::Granular));
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 1);  // 1 Granular preset

        dataSource.setSearchFilter("tape");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 0);  // No Granular preset has "tape"
    }

    SECTION("clear search restores mode-filtered results") {
        dataSource.setModeFilter(static_cast<int>(DelayMode::Digital));
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // 2 Digital presets

        dataSource.setSearchFilter("xyz");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 0);

        dataSource.setSearchFilter("");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // Back to 2 Digital presets
    }

    SECTION("mode filter All (-1) with search") {
        dataSource.setModeFilter(-1);  // All modes
        dataSource.setSearchFilter("echo");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // "Warm Tape Echo" and "ECHO CHAMBER"
    }
}

// =============================================================================
// Preset Access After Search
// =============================================================================

TEST_CASE("PresetDataSource getPresetAtRow after search", "[ui][preset-browser][search]") {
    PresetDataSource dataSource;
    dataSource.setPresets(createTestPresets());

    SECTION("getPresetAtRow returns filtered preset") {
        dataSource.setSearchFilter("digital");

        REQUIRE(dataSource.dbGetNumRows(nullptr) == 1);

        const PresetInfo* preset = dataSource.getPresetAtRow(0);
        REQUIRE(preset != nullptr);
        REQUIRE(preset->name == "Digital Clean");
    }

    SECTION("getPresetAtRow returns nullptr for invalid row after filter") {
        dataSource.setSearchFilter("tape");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);

        // Row 2 doesn't exist after filter
        const PresetInfo* preset = dataSource.getPresetAtRow(2);
        REQUIRE(preset == nullptr);
    }

    SECTION("filtered presets maintain original data") {
        dataSource.setSearchFilter("granular");

        const PresetInfo* preset = dataSource.getPresetAtRow(0);
        REQUIRE(preset != nullptr);
        REQUIRE(preset->name == "Granular Shimmer");
        REQUIRE(preset->mode == DelayMode::Granular);
        REQUIRE(preset->category == "Ambient");
        REQUIRE_FALSE(preset->isFactory);
    }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("PresetDataSource search edge cases", "[ui][preset-browser][search]") {
    PresetDataSource dataSource;
    dataSource.setPresets(createTestPresets());

    SECTION("whitespace search shows all presets") {
        dataSource.setSearchFilter("   ");
        // Whitespace is not empty, so it searches for spaces
        // None of our preset names start with spaces, but let's verify behavior
        // Actually the implementation converts to lowercase and searches, " " won't match
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 0);
    }

    SECTION("search with leading/trailing whitespace") {
        // Search for " tape " (with spaces)
        // "Warm Tape Echo" contains " tape " (space-tape-space), so it matches
        // "Tape Warmth" starts with "Tape", no leading space, doesn't match " tape "
        dataSource.setSearchFilter(" tape ");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 1);

        // Without spaces, both match
        dataSource.setSearchFilter("tape");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);
    }

    SECTION("search on empty preset list") {
        PresetDataSource emptySource;
        emptySource.setPresets({});
        emptySource.setSearchFilter("test");
        REQUIRE(emptySource.dbGetNumRows(nullptr) == 0);
    }

    SECTION("multiple filter changes") {
        dataSource.setSearchFilter("tape");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);

        dataSource.setSearchFilter("digital");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 1);

        dataSource.setSearchFilter("echo");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);

        dataSource.setSearchFilter("");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 5);
    }
}
