// ==============================================================================
// Version Display Tests
// ==============================================================================
// Tests for compile-time version constants used in the UI
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/version_utils.h"
#include "version.h"

#include <string>

using namespace Iterum;
using Catch::Approx;

// ==============================================================================
// TEST: Compile-time version constants
// ==============================================================================

TEST_CASE("UI_VERSION_STR is defined and formatted correctly", "[vst][version]") {
    SECTION("UI_VERSION_STR contains plugin name and version") {
        std::string uiVersion = UI_VERSION_STR;

        // Should contain "Iterum v"
        REQUIRE(uiVersion.find("Iterum v") == 0);

        // Should contain version number (e.g., "0.1.2")
        REQUIRE(uiVersion.find(VERSION_STR) != std::string::npos);
    }

    SECTION("UI_VERSION_STR matches expected format") {
        std::string uiVersion = UI_VERSION_STR;
        std::string expected = std::string(stringPluginName) + " v" + VERSION_STR;

        REQUIRE(uiVersion == expected);
    }
}

TEST_CASE("VERSION_STR is defined correctly", "[vst][version]") {
    SECTION("VERSION_STR follows semantic versioning") {
        std::string version = VERSION_STR;

        // Should not be empty
        REQUIRE_FALSE(version.empty());

        // Should contain at least two dots (X.Y.Z format)
        size_t firstDot = version.find('.');
        REQUIRE(firstDot != std::string::npos);

        size_t secondDot = version.find('.', firstDot + 1);
        REQUIRE(secondDot != std::string::npos);
    }

    SECTION("VERSION_STR matches current version") {
        // This test documents the current version
        // Update this when version changes in version.json
        REQUIRE(std::string(VERSION_STR) == "0.1.2");
    }
}

TEST_CASE("Version component macros are consistent", "[vst][version]") {
    SECTION("VERSION_STR matches individual components") {
        std::string version = VERSION_STR;
        std::string expected = std::string(MAJOR_VERSION_STR) + "." +
                              SUB_VERSION_STR + "." +
                              RELEASE_NUMBER_STR;

        REQUIRE(version == expected);
    }

    SECTION("Integer and string versions match") {
        REQUIRE(std::string(MAJOR_VERSION_STR) == std::to_string(MAJOR_VERSION_INT));
        REQUIRE(std::string(SUB_VERSION_STR) == std::to_string(SUB_VERSION_INT));
        REQUIRE(std::string(RELEASE_NUMBER_STR) == std::to_string(RELEASE_NUMBER_INT));
    }
}

// ==============================================================================
// TEST: Version utility functions
// ==============================================================================

TEST_CASE("getUIVersionString returns correct value", "[vst][version][utility]") {
    SECTION("returns UI_VERSION_STR constant") {
        REQUIRE(getUIVersionString() == UI_VERSION_STR);
    }

    SECTION("returns formatted version string") {
        std::string uiVersion = getUIVersionString();
        REQUIRE(uiVersion.find("Iterum v") == 0);
        REQUIRE(uiVersion.find("0.1.2") != std::string::npos);
    }
}

TEST_CASE("getVersionString returns correct value", "[vst][version][utility]") {
    SECTION("returns VERSION_STR constant") {
        REQUIRE(getVersionString() == VERSION_STR);
    }

    SECTION("returns version number only") {
        std::string version = getVersionString();
        REQUIRE(version == "0.1.2");
    }
}

TEST_CASE("getPluginName returns correct value", "[vst][version][utility]") {
    SECTION("returns stringPluginName constant") {
        REQUIRE(getPluginName() == stringPluginName);
    }

    SECTION("returns plugin name") {
        std::string name = getPluginName();
        REQUIRE(name == "Iterum");
    }
}

// ==============================================================================
// TEST: Plugin metadata constants
// ==============================================================================

TEST_CASE("Plugin metadata is defined", "[vst][version][metadata]") {
    SECTION("All string constants are non-empty") {
        REQUIRE_FALSE(std::string(stringPluginName).empty());
        REQUIRE_FALSE(std::string(stringOriginalFilename).empty());
        REQUIRE_FALSE(std::string(stringFileDescription).empty());
        REQUIRE_FALSE(std::string(stringCompanyName).empty());
        REQUIRE_FALSE(std::string(stringVendorURL).empty());
        REQUIRE_FALSE(std::string(stringLegalCopyright).empty());
    }

    SECTION("Plugin name matches expected value") {
        REQUIRE(std::string(stringPluginName) == "Iterum");
    }

    SECTION("Company name is correct") {
        REQUIRE(std::string(stringCompanyName) == "Krate Audio");
    }
}
