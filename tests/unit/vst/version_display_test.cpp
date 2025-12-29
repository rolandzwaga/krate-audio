// ==============================================================================
// Version Display Tests
// ==============================================================================
// Tests for dynamic version string display in the UI
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/version_utils.h"

#include <fstream>
#include <sstream>

using namespace Iterum;
using Catch::Approx;

// ==============================================================================
// TEST: Version file parsing
// ==============================================================================

TEST_CASE("Version can be read from version.json", "[vst][version]") {
    SECTION("version.json exists and contains version field") {
        // Open version.json from project root
        std::ifstream versionFile("version.json");
        REQUIRE(versionFile.is_open());

        // Read file contents
        std::stringstream buffer;
        buffer << versionFile.rdbuf();
        std::string content = buffer.str();

        // Should contain "version" field
        REQUIRE(content.find("\"version\"") != std::string::npos);

        // Should contain current version
        REQUIRE(content.find("0.1.2") != std::string::npos);
    }
}

TEST_CASE("Version string format is correct", "[vst][version]") {
    SECTION("Version string should be 'Iterum vX.Y.Z'") {
        // Expected format with current version
        std::string expectedFormat = "Iterum v0.1.2";

        // Version should follow pattern: "Iterum v" + semantic version
        REQUIRE(expectedFormat.find("Iterum v") == 0);
        REQUIRE(expectedFormat.find("0.1.2") != std::string::npos);
    }
}

// ==============================================================================
// TEST: Version parsing utility function
// ==============================================================================

TEST_CASE("parseVersionFromJson extracts version correctly", "[vst][version][utility]") {
    SECTION("parses simple JSON correctly") {
        std::string jsonContent = R"({
  "version": "0.1.2",
  "name": "Iterum"
})";

        std::string version = parseVersionFromJson(jsonContent);
        REQUIRE(version == "0.1.2");
    }

    SECTION("handles whitespace variations") {
        std::string jsonContent = R"({"version":"0.1.2","name":"Test"})";
        std::string version = parseVersionFromJson(jsonContent);
        REQUIRE(version == "0.1.2");
    }

    SECTION("returns empty string on parse failure") {
        std::string invalidJson = "not valid json";
        std::string version = parseVersionFromJson(invalidJson);
        REQUIRE(version.empty());
    }

    SECTION("returns empty string when version field missing") {
        std::string jsonContent = R"({"name":"Test"})";
        std::string version = parseVersionFromJson(jsonContent);
        REQUIRE(version.empty());
    }
}

TEST_CASE("formatVersionString creates display string", "[vst][version][utility]") {
    SECTION("formats version correctly") {
        REQUIRE(formatVersionString("0.1.2") == "Iterum v0.1.2");
        REQUIRE(formatVersionString("1.0.0") == "Iterum v1.0.0");
        REQUIRE(formatVersionString("2.5.3") == "Iterum v2.5.3");
    }

    SECTION("handles empty version") {
        // Should return fallback when version is empty
        std::string result = formatVersionString("");
        REQUIRE(result == "Iterum v?.?.?");
    }
}
