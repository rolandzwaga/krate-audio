// =============================================================================
// PresetInfo Tests
// =============================================================================
// Spec 042: Preset Browser
// Tests for PresetInfo struct metadata handling
// =============================================================================

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "preset/preset_info.h"
#include <filesystem>

namespace fs = std::filesystem;

using Krate::Plugins::PresetInfo;

TEST_CASE("PresetInfo default construction", "[preset][info]") {
    PresetInfo info;

    SECTION("has empty name by default") {
        REQUIRE(info.name.empty());
    }

    SECTION("has empty category by default") {
        REQUIRE(info.category.empty());
    }

    SECTION("has empty subcategory by default") {
        REQUIRE(info.subcategory.empty());
    }

    SECTION("has empty path by default") {
        REQUIRE(info.path.empty());
    }

    SECTION("is not factory preset by default") {
        REQUIRE_FALSE(info.isFactory);
    }

    SECTION("has empty description by default") {
        REQUIRE(info.description.empty());
    }

    SECTION("has empty author by default") {
        REQUIRE(info.author.empty());
    }
}

TEST_CASE("PresetInfo::isValid() checks name and path", "[preset][info]") {
    PresetInfo info;

    SECTION("empty info is not valid") {
        REQUIRE_FALSE(info.isValid());
    }

    SECTION("name only is not valid") {
        info.name = "Test Preset";
        REQUIRE_FALSE(info.isValid());
    }

    SECTION("path only is not valid") {
        info.path = "/path/to/preset.vstpreset";
        REQUIRE_FALSE(info.isValid());
    }

    SECTION("name and path together is valid") {
        info.name = "Test Preset";
        info.path = "/path/to/preset.vstpreset";
        REQUIRE(info.isValid());
    }
}

TEST_CASE("PresetInfo comparison operator", "[preset][info]") {
    PresetInfo a, b;

    SECTION("compares by name alphabetically") {
        a.name = "Alpha";
        b.name = "Beta";
        REQUIRE(a < b);
        REQUIRE_FALSE(b < a);
    }

    SECTION("equal names are not less than each other") {
        a.name = "Same";
        b.name = "Same";
        REQUIRE_FALSE(a < b);
        REQUIRE_FALSE(b < a);
    }

    SECTION("comparison is case-sensitive") {
        a.name = "alpha";
        b.name = "Beta";
        // lowercase 'a' > uppercase 'B' in ASCII
        REQUIRE_FALSE(a < b);
    }
}

TEST_CASE("PresetInfo can store all metadata fields", "[preset][info]") {
    PresetInfo info;
    info.name = "Ambient Pad";
    info.category = "Ambient";
    info.subcategory = "Shimmer";
    info.path = fs::path("/presets/Shimmer/Ambient Pad.vstpreset");
    info.isFactory = true;
    info.description = "A lush ambient shimmer pad";
    info.author = "Krate Audio";

    REQUIRE(info.name == "Ambient Pad");
    REQUIRE(info.category == "Ambient");
    REQUIRE(info.subcategory == "Shimmer");
    REQUIRE(info.path.string().find("Ambient Pad.vstpreset") != std::string::npos);
    REQUIRE(info.isFactory == true);
    REQUIRE(info.description == "A lush ambient shimmer pad");
    REQUIRE(info.author == "Krate Audio");
    REQUIRE(info.isValid());
}

TEST_CASE("PresetInfo supports all subcategories", "[preset][info]") {
    std::vector<std::string> allSubcategories = {
        "Granular", "Spectral", "Shimmer", "Tape", "BBD",
        "Digital", "PingPong", "Reverse", "MultiTap", "Freeze"
    };

    for (const auto& subcategory : allSubcategories) {
        PresetInfo info;
        info.name = "Test";
        info.path = "/test.vstpreset";
        info.subcategory = subcategory;

        REQUIRE(info.subcategory == subcategory);
        REQUIRE(info.isValid());
    }
}
