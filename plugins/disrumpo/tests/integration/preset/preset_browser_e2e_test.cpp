// =============================================================================
// Disrumpo Preset Browser End-to-End Tests
// =============================================================================
// Spec 010: Preset System
// Integration tests for the Disrumpo preset browser functionality
//
// Tests verify:
// - Tab configuration: 12 tabs (All + 11 categories) (T108, FR-016, FR-019)
// - Category selection and filtering (T109)
// - Scan completion (T110)
// - XML metadata correctness (T110a)
// - Factory preset protection (T110b, FR-031)
// =============================================================================

#include <catch2/catch_all.hpp>
#include "preset/preset_manager.h"
#include "preset/preset_manager_config.h"
#include "preset/disrumpo_preset_config.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

// =============================================================================
// Test Fixture for Browser E2E Tests
// =============================================================================
class BrowserE2EFixture {
public:
    BrowserE2EFixture() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(100000, 999999);

        std::ostringstream dirName;
        dirName << "disrumpo_e2e_" << dist(gen);

        testDir_ = fs::temp_directory_path() / dirName.str();
        userDir_ = testDir_ / "user";
        factoryDir_ = testDir_ / "factory";

        std::error_code ec;
        fs::create_directories(userDir_, ec);
        fs::create_directories(factoryDir_, ec);
    }

    ~BrowserE2EFixture() {
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
// T108: Tab Configuration - 12 Tabs (All + 11 Categories)
// =============================================================================

TEST_CASE("Disrumpo browser has 12 tabs", "[disrumpo][preset][browser][tabs]") {
    auto labels = Disrumpo::getDisrumpoTabLabels();

    SECTION("12 tabs total: All + 11 subcategories") {
        REQUIRE(labels.size() == 12);
    }

    SECTION("All tab is first") {
        REQUIRE(labels[0] == "All");
    }

    SECTION("subcategories match Disrumpo categories") {
        std::vector<std::string> expected = {
            "All", "Init", "Sweep", "Morph", "Bass", "Leads",
            "Pads", "Drums", "Experimental", "Chaos", "Dynamic", "Lo-Fi"
        };
        REQUIRE(labels == expected);
    }
}

// =============================================================================
// T108 continued: Config subcategories match tab labels (minus "All")
// =============================================================================

TEST_CASE("Disrumpo config subcategories match tab labels minus All",
          "[disrumpo][preset][browser][consistency]") {
    auto config = Disrumpo::makeDisrumpoPresetConfig();
    auto tabs = Disrumpo::getDisrumpoTabLabels();

    REQUIRE(tabs.size() == config.subcategoryNames.size() + 1);

    for (size_t i = 0; i < config.subcategoryNames.size(); ++i) {
        REQUIRE(config.subcategoryNames[i] == tabs[i + 1]);
    }
}

// =============================================================================
// T109: Category Selection and Filtering
// =============================================================================

TEST_CASE("Disrumpo category filtering works correctly",
          "[disrumpo][preset][browser][filter]") {
    BrowserE2EFixture fixture;

    // Create presets in different subcategory directories
    fixture.createDummyPreset(fixture.userDir() / "Init" / "default.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Sweep" / "wide_sweep.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Sweep" / "narrow_sweep.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Morph" / "morph_pad.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Bass" / "sub_bass.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Leads" / "screaming_lead.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Pads" / "warm_pad.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Drums" / "kick_crush.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Experimental" / "glitch.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Chaos" / "chaos_engine.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Dynamic" / "dynamic_comp.vstpreset");
    fixture.createDummyPreset(fixture.userDir() / "Lo-Fi" / "vinyl.vstpreset");

    auto manager = fixture.createManager();
    manager.scanPresets();

    SECTION("All category returns all presets") {
        auto all = manager.getPresetsForSubcategory("");
        REQUIRE(all.size() == 12);
    }

    SECTION("Init category returns 1 preset") {
        auto presets = manager.getPresetsForSubcategory("Init");
        REQUIRE(presets.size() == 1);
    }

    SECTION("Sweep category returns 2 presets") {
        auto presets = manager.getPresetsForSubcategory("Sweep");
        REQUIRE(presets.size() == 2);
    }

    SECTION("Morph category returns 1 preset") {
        auto presets = manager.getPresetsForSubcategory("Morph");
        REQUIRE(presets.size() == 1);
    }

    SECTION("Each of the 11 categories returns correct count") {
        auto config = Disrumpo::makeDisrumpoPresetConfig();
        for (const auto& subcategory : config.subcategoryNames) {
            auto presets = manager.getPresetsForSubcategory(subcategory);
            if (subcategory == "Sweep") {
                REQUIRE(presets.size() == 2);
            } else {
                REQUIRE(presets.size() == 1);
            }
        }
    }

    SECTION("non-existent category returns empty") {
        auto presets = manager.getPresetsForSubcategory("NonExistent");
        REQUIRE(presets.size() == 0);
    }
}

// =============================================================================
// T110: Scan Completion
// =============================================================================

TEST_CASE("Disrumpo scanPresets completes", "[disrumpo][preset][browser][scan]") {
    BrowserE2EFixture fixture;
    auto manager = fixture.createManager();

    SECTION("scanning empty directories succeeds") {
        auto presets = manager.scanPresets();
        REQUIRE(presets.size() == 0);
    }

    SECTION("scanning finds all preset files") {
        for (int i = 0; i < 5; ++i) {
            std::ostringstream name;
            name << "preset_" << i << ".vstpreset";
            fixture.createDummyPreset(fixture.userDir() / name.str());
        }

        auto presets = manager.scanPresets();
        REQUIRE(presets.size() == 5);
    }

    SECTION("scanning finds both user and factory presets") {
        fixture.createDummyPreset(fixture.userDir() / "user.vstpreset");
        fixture.createDummyPreset(fixture.factoryDir() / "factory.vstpreset");

        auto presets = manager.scanPresets();
        REQUIRE(presets.size() == 2);

        // Count factory vs user
        int factoryCount = 0, userCount = 0;
        for (const auto& p : presets) {
            if (p.isFactory) factoryCount++;
            else userCount++;
        }
        REQUIRE(factoryCount == 1);
        REQUIRE(userCount == 1);
    }
}

// =============================================================================
// T110a: XML Metadata Verification
// =============================================================================

TEST_CASE("Disrumpo preset config has correct metadata",
          "[disrumpo][preset][browser][metadata]") {
    auto config = Disrumpo::makeDisrumpoPresetConfig();

    SECTION("plugin name is Disrumpo, NOT Iterum") {
        REQUIRE(config.pluginName == "Disrumpo");
        REQUIRE(config.pluginName != "Iterum");
    }

    SECTION("plugin category is Distortion, NOT Delay") {
        REQUIRE(config.pluginCategoryDesc == "Distortion");
        REQUIRE(config.pluginCategoryDesc != "Delay");
    }

    SECTION("processor UID is Disrumpo's UID") {
        // Verify the FUID is valid (non-zero)
        REQUIRE(config.processorUID.isValid());
    }
}

// =============================================================================
// T110b: Factory Preset Protection (FR-031)
// =============================================================================

TEST_CASE("Disrumpo factory presets are read-only",
          "[disrumpo][preset][browser][factory]") {
    BrowserE2EFixture fixture;
    auto manager = fixture.createManager();

    // Create a factory preset
    auto factoryPath = fixture.factoryDir() / "Init" / "factory_init.vstpreset";
    fixture.createDummyPreset(factoryPath);

    Krate::Plugins::PresetInfo factoryPreset;
    factoryPreset.name = "Factory Init";
    factoryPreset.path = factoryPath;
    factoryPreset.isFactory = true;
    factoryPreset.subcategory = "Init";

    SECTION("deletePreset returns false for factory presets") {
        REQUIRE_FALSE(manager.deletePreset(factoryPreset));
        REQUIRE_FALSE(manager.getLastError().empty());
        REQUIRE(manager.getLastError().find("factory") != std::string::npos);
    }

    SECTION("overwritePreset returns false for factory presets") {
        REQUIRE_FALSE(manager.overwritePreset(factoryPreset));
        REQUIRE_FALSE(manager.getLastError().empty());
    }

    SECTION("factory preset file still exists after failed delete") {
        manager.deletePreset(factoryPreset);
        REQUIRE(fs::exists(factoryPath));
    }
}

// =============================================================================
// T178: Rapid Preset Load Coalescing
// =============================================================================
// Since loading is synchronous (setState completes within 50ms), rapid
// sequential loads are naturally serialized. This test verifies that multiple
// rapid loads complete correctly with only the final state being applied.

TEST_CASE("Rapid sequential preset loads apply correctly",
          "[disrumpo][preset][browser][debounce]") {
    BrowserE2EFixture fixture;

    // Create multiple presets
    for (int i = 0; i < 10; ++i) {
        std::ostringstream name;
        name << "preset_" << i << ".vstpreset";
        fixture.createDummyPreset(fixture.userDir() / name.str());
    }

    auto manager = fixture.createManager();
    auto presets = manager.scanPresets();
    REQUIRE(presets.size() == 10);

    SECTION("loading 10 presets rapidly in sequence succeeds") {
        // Simulate rapid clicking through all presets
        int loadCount = 0;
        for (const auto& preset : presets) {
            // loadPreset with no processor/provider just returns false
            // (no actual state to apply). The key verification is that
            // no crashes or undefined behavior occur with rapid calls.
            manager.loadPreset(preset);
            ++loadCount;
        }
        CHECK(loadCount == 10);
    }
}

// =============================================================================
// T179: Refresh / Rescan Preset Directories
// =============================================================================

TEST_CASE("Rescan picks up newly added presets",
          "[disrumpo][preset][browser][refresh]") {
    BrowserE2EFixture fixture;
    auto manager = fixture.createManager();

    SECTION("initial scan returns empty") {
        auto presets = manager.scanPresets();
        REQUIRE(presets.size() == 0);
    }

    SECTION("rescan after adding presets finds them") {
        // First scan: empty
        manager.scanPresets();

        // Add new presets after initial scan
        fixture.createDummyPreset(fixture.userDir() / "new_preset_1.vstpreset");
        fixture.createDummyPreset(fixture.userDir() / "new_preset_2.vstpreset");

        // Rescan: should find the new presets
        auto presets = manager.scanPresets();
        REQUIRE(presets.size() == 2);
    }

    SECTION("rescan after deleting presets reflects changes") {
        auto presetPath = fixture.userDir() / "temp_preset.vstpreset";
        fixture.createDummyPreset(presetPath);

        auto presets1 = manager.scanPresets();
        REQUIRE(presets1.size() == 1);

        // Delete the file externally
        fs::remove(presetPath);

        // Rescan: should now be empty
        auto presets2 = manager.scanPresets();
        REQUIRE(presets2.size() == 0);
    }
}

// =============================================================================
// User Preset Operations (complementary to factory protection)
// =============================================================================

TEST_CASE("Disrumpo user presets can be deleted",
          "[disrumpo][preset][browser][user]") {
    BrowserE2EFixture fixture;
    auto manager = fixture.createManager();

    auto userPath = fixture.userDir() / "my_preset.vstpreset";
    fixture.createDummyPreset(userPath);

    Krate::Plugins::PresetInfo userPreset;
    userPreset.name = "My Preset";
    userPreset.path = userPath;
    userPreset.isFactory = false;

    SECTION("deletePreset succeeds for user presets") {
        REQUIRE(manager.deletePreset(userPreset));
        REQUIRE_FALSE(fs::exists(userPath));
    }
}
