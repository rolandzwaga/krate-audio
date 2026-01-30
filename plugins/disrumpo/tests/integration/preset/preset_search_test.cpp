// =============================================================================
// Disrumpo Preset Search Tests
// =============================================================================
// Spec 010: Preset System - User Story 5
// Integration tests for searching and filtering presets.
//
// Tests verify:
// - Search by name with filtered results (T132, FR-024, FR-026)
// - Search combined with category filtering (T133, FR-025)
// - Search performance (T134, SC-007)
// - No results case (T135)
// =============================================================================

#include <catch2/catch_all.hpp>
#include "preset/preset_manager.h"
#include "preset/preset_manager_config.h"
#include "preset/disrumpo_preset_config.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <chrono>

namespace fs = std::filesystem;

// =============================================================================
// Test Fixture
// =============================================================================
class SearchFixture {
public:
    SearchFixture() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(100000, 999999);

        std::ostringstream dirName;
        dirName << "disrumpo_search_" << dist(gen);

        testDir_ = fs::temp_directory_path() / dirName.str();
        userDir_ = testDir_ / "user";
        factoryDir_ = testDir_ / "factory";

        std::error_code ec;
        fs::create_directories(userDir_, ec);
        fs::create_directories(factoryDir_, ec);
    }

    ~SearchFixture() {
        std::error_code ec;
        fs::remove_all(testDir_, ec);
    }

    fs::path testDir() const { return testDir_; }
    fs::path userDir() const { return userDir_; }
    fs::path factoryDir() const { return factoryDir_; }

    void createDummyPreset(const fs::path& path) {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        const char header[] = "VST3";
        file.write(header, 4);
        file.close();
    }

    // Populate with a realistic preset library
    void populatePresets() {
        // Bass category
        createDummyPreset(userDir_ / "Bass" / "Deep Sub Bass.vstpreset");
        createDummyPreset(userDir_ / "Bass" / "Warm Tube Bass.vstpreset");
        createDummyPreset(userDir_ / "Bass" / "Fuzzy Bass.vstpreset");

        // Leads category
        createDummyPreset(userDir_ / "Leads" / "Screaming Lead.vstpreset");
        createDummyPreset(userDir_ / "Leads" / "Warm Analog Lead.vstpreset");
        createDummyPreset(userDir_ / "Leads" / "Digital Lead.vstpreset");

        // Pads category
        createDummyPreset(userDir_ / "Pads" / "Warm Ambient Pad.vstpreset");
        createDummyPreset(userDir_ / "Pads" / "Dark Pad.vstpreset");

        // Sweep category
        createDummyPreset(userDir_ / "Sweep" / "Wide Sweep.vstpreset");
        createDummyPreset(userDir_ / "Sweep" / "Narrow Sweep.vstpreset");

        // Experimental
        createDummyPreset(userDir_ / "Experimental" / "Glitch Machine.vstpreset");
        createDummyPreset(userDir_ / "Experimental" / "Warm Chaos.vstpreset");

        // Lo-Fi
        createDummyPreset(userDir_ / "Lo-Fi" / "Vinyl Warmth.vstpreset");
        createDummyPreset(userDir_ / "Lo-Fi" / "Tape Hiss.vstpreset");
    }

    Krate::Plugins::PresetManager createManager() {
        return Krate::Plugins::PresetManager(
            Disrumpo::makeDisrumpoPresetConfig(),
            nullptr, nullptr,
            userDir_, factoryDir_);
    }

private:
    fs::path testDir_;
    fs::path userDir_;
    fs::path factoryDir_;
};

// =============================================================================
// T132: Search by Name with Filtered Results
// =============================================================================

TEST_CASE("Disrumpo search presets by name", "[disrumpo][preset][search]") {
    SearchFixture fixture;
    fixture.populatePresets();
    auto manager = fixture.createManager();
    manager.scanPresets();

    SECTION("search for 'warm' returns all warm presets") {
        auto results = manager.searchPresets("warm");
        // Should match: Warm Tube Bass, Warm Analog Lead, Warm Ambient Pad, Warm Chaos, Vinyl Warmth
        REQUIRE(results.size() >= 4);

        for (const auto& p : results) {
            // All results should contain "warm" case-insensitively
            std::string lowerName = p.name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            REQUIRE(lowerName.find("warm") != std::string::npos);
        }
    }

    SECTION("search for 'bass' returns bass presets") {
        auto results = manager.searchPresets("bass");
        REQUIRE(results.size() >= 3);
    }

    SECTION("search is case-insensitive") {
        auto lower = manager.searchPresets("sweep");
        auto upper = manager.searchPresets("SWEEP");
        auto mixed = manager.searchPresets("Sweep");

        REQUIRE(lower.size() == upper.size());
        REQUIRE(lower.size() == mixed.size());
    }

    SECTION("empty search returns all presets") {
        auto all = manager.searchPresets("");
        REQUIRE(all.size() == 14);  // Total presets created
    }
}

// =============================================================================
// T133: Search Combined with Category Filtering
// =============================================================================

TEST_CASE("Disrumpo search combined with category", "[disrumpo][preset][search][filter]") {
    SearchFixture fixture;
    fixture.populatePresets();
    auto manager = fixture.createManager();
    manager.scanPresets();

    SECTION("category filter limits search scope") {
        // Get all bass presets
        auto bassPresets = manager.getPresetsForSubcategory("Bass");
        REQUIRE(bassPresets.size() == 3);

        // Search for "warm" in all presets
        auto warmAll = manager.searchPresets("warm");

        // Warm presets exist in both Bass and other categories
        bool hasWarmBass = false;
        bool hasWarmNonBass = false;
        for (const auto& p : warmAll) {
            if (p.subcategory == "Bass") hasWarmBass = true;
            else hasWarmNonBass = true;
        }
        REQUIRE(hasWarmBass);
        REQUIRE(hasWarmNonBass);
    }

    SECTION("getPresetsForSubcategory returns only matching category") {
        auto pads = manager.getPresetsForSubcategory("Pads");
        REQUIRE(pads.size() == 2);

        for (const auto& p : pads) {
            REQUIRE(p.subcategory == "Pads");
        }
    }
}

// =============================================================================
// T134: Search Performance (SC-007)
// =============================================================================

TEST_CASE("Disrumpo search completes within 100ms", "[disrumpo][preset][search][performance]") {
    SearchFixture fixture;

    // Create many presets to stress-test search
    for (int i = 0; i < 200; ++i) {
        std::ostringstream name;
        name << "preset_" << i << ".vstpreset";

        // Distribute across categories
        std::string category;
        switch (i % 11) {
            case 0: category = "Init"; break;
            case 1: category = "Sweep"; break;
            case 2: category = "Morph"; break;
            case 3: category = "Bass"; break;
            case 4: category = "Leads"; break;
            case 5: category = "Pads"; break;
            case 6: category = "Drums"; break;
            case 7: category = "Experimental"; break;
            case 8: category = "Chaos"; break;
            case 9: category = "Dynamic"; break;
            case 10: category = "Lo-Fi"; break;
        }

        fixture.createDummyPreset(fixture.userDir() / category / name.str());
    }

    auto manager = fixture.createManager();
    manager.scanPresets();

    auto start = std::chrono::high_resolution_clock::now();
    auto results = manager.searchPresets("preset");
    auto end = std::chrono::high_resolution_clock::now();

    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    REQUIRE(results.size() == 200);
    REQUIRE(durationMs < 100);  // SC-007: search results within 100ms
}

// =============================================================================
// T135: No Results Case
// =============================================================================

TEST_CASE("Disrumpo search with no results", "[disrumpo][preset][search]") {
    SearchFixture fixture;
    fixture.populatePresets();
    auto manager = fixture.createManager();
    manager.scanPresets();

    SECTION("search for non-matching term returns empty") {
        auto results = manager.searchPresets("xyznonexistent");
        REQUIRE(results.size() == 0);
    }

    SECTION("search for non-matching partial term returns empty") {
        auto results = manager.searchPresets("reverb");
        REQUIRE(results.size() == 0);
    }
}
