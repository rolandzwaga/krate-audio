// =============================================================================
// Disrumpo Preset Save Tests
// =============================================================================
// Spec 010: Preset System - User Story 4
// Integration tests for saving user presets through the shared save dialog.
//
// Tests verify:
// - Save dialog workflow (T115)
// - .vstpreset file creation in correct directory (T116, FR-020, FR-021, FR-022)
// - Preset appears in browser after save (T117, FR-016)
// - Overwrite behavior when preset with same name exists (T118, FR-023)
// - Save failure error handling (T119, FR-023a)
// - Name validation (T126, T127)
// =============================================================================

#include <catch2/catch_all.hpp>
#include "preset/preset_manager.h"
#include "preset/preset_manager_config.h"
#include "preset/disrumpo_preset_config.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"
#include "plugin_ids.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

// =============================================================================
// Test Fixture
// =============================================================================
class PresetSaveFixture {
public:
    PresetSaveFixture() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(100000, 999999);

        std::ostringstream dirName;
        dirName << "disrumpo_save_" << dist(gen);

        testDir_ = fs::temp_directory_path() / dirName.str();
        userDir_ = testDir_ / "user";
        factoryDir_ = testDir_ / "factory";

        std::error_code ec;
        fs::create_directories(userDir_, ec);
        fs::create_directories(factoryDir_, ec);
    }

    ~PresetSaveFixture() {
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

    /// Create a minimal v6 state stream for testing save with state provider
    Steinberg::MemoryStream* createMinimalStateStream() {
        auto* stream = new Steinberg::MemoryStream();
        Steinberg::IBStreamer streamer(stream, kLittleEndian);

        // Version
        streamer.writeInt32(Disrumpo::kPresetVersion);

        // Globals (3 floats)
        streamer.writeFloat(0.5f);  // inputGain
        streamer.writeFloat(0.5f);  // outputGain
        streamer.writeFloat(1.0f);  // globalMix

        // Band count
        streamer.writeInt32(1);

        // 8 band states (gain, pan, solo, bypass, mute)
        for (int b = 0; b < 8; ++b) {
            streamer.writeFloat(0.0f);  // gain
            streamer.writeFloat(0.0f);  // pan
            streamer.writeInt8(0);      // solo
            streamer.writeInt8(0);      // bypass
            streamer.writeInt8(0);      // mute
        }

        // 7 crossover frequencies
        float defaultFreqs[] = {250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
        for (int c = 0; c < 7; ++c) {
            streamer.writeFloat(defaultFreqs[c]);
        }

        // Sweep (6+6+4+breakpoints)
        // Core
        streamer.writeInt8(0);     // enable
        streamer.writeFloat(0.5f); // freq
        streamer.writeFloat(0.5f); // width
        streamer.writeFloat(0.25f); // intensity
        streamer.writeInt8(1);     // falloff
        streamer.writeInt8(0);     // morphLink

        // LFO
        streamer.writeInt8(0);     // enable
        streamer.writeFloat(0.5f); // rate
        streamer.writeInt8(0);     // waveform
        streamer.writeFloat(0.0f); // depth
        streamer.writeInt8(0);     // sync
        streamer.writeInt8(0);     // noteIndex

        // Envelope
        streamer.writeInt8(0);     // enable
        streamer.writeFloat(0.1f); // attack
        streamer.writeFloat(0.2f); // release
        streamer.writeFloat(0.5f); // sensitivity

        // Custom curve (2 default points)
        streamer.writeInt32(2);
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
        streamer.writeFloat(1.0f); streamer.writeFloat(1.0f);

        // Modulation system (v5) - write defaults for all sources/routings
        // LFO 1 (7 values)
        streamer.writeFloat(0.5f); streamer.writeInt8(0); streamer.writeFloat(0.0f);
        streamer.writeInt8(0); streamer.writeInt8(0); streamer.writeInt8(0); streamer.writeInt8(1);

        // LFO 2 (7 values)
        streamer.writeFloat(0.5f); streamer.writeInt8(0); streamer.writeFloat(0.0f);
        streamer.writeInt8(0); streamer.writeInt8(0); streamer.writeInt8(0); streamer.writeInt8(1);

        // Envelope Follower (4 values)
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
        streamer.writeFloat(0.5f); streamer.writeInt8(0);

        // Random (3 values)
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f); streamer.writeInt8(0);

        // Chaos (3 values)
        streamer.writeInt8(0); streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);

        // Sample & Hold (3 values)
        streamer.writeInt8(0); streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);

        // Pitch Follower (4 values)
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
        streamer.writeFloat(0.5f); streamer.writeFloat(0.0f);

        // Transient (3 values)
        streamer.writeFloat(0.5f); streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);

        // Macros (4 x 4 values)
        for (int m = 0; m < 4; ++m) {
            streamer.writeFloat(0.0f); // value
            streamer.writeFloat(0.0f); // min
            streamer.writeFloat(1.0f); // max
            streamer.writeInt8(0);     // curve
        }

        // Routings (32 x 4 values)
        for (int r = 0; r < 32; ++r) {
            streamer.writeInt8(0);     // source
            streamer.writeInt32(0);    // dest
            streamer.writeFloat(0.0f); // amount
            streamer.writeInt8(0);     // curve
        }

        // Morph nodes (v6) - 8 bands
        for (int b = 0; b < 8; ++b) {
            streamer.writeFloat(0.5f); // morphX
            streamer.writeFloat(0.5f); // morphY
            streamer.writeInt8(0);     // morphMode
            streamer.writeInt8(2);     // activeNodes
            streamer.writeFloat(0.0f); // smoothing

            // 4 nodes per band
            for (int n = 0; n < 4; ++n) {
                streamer.writeInt8(0);      // type (SoftClip)
                streamer.writeFloat(1.0f);  // drive
                streamer.writeFloat(1.0f);  // mix
                streamer.writeFloat(4000.0f); // tone
                streamer.writeFloat(0.0f);  // bias
                streamer.writeFloat(1.0f);  // folds
                streamer.writeFloat(16.0f); // bitDepth
            }
        }

        return stream;
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
// T115-T116: Save Dialog Workflow - File Creation
// =============================================================================

TEST_CASE("Disrumpo preset save creates file", "[disrumpo][preset][save]") {
    PresetSaveFixture fixture;
    auto manager = fixture.createManager();

    // Set up state provider that returns a valid state stream
    manager.setStateProvider([&fixture]() -> Steinberg::IBStream* {
        return fixture.createMinimalStateStream();
    });

    SECTION("savePreset creates .vstpreset file in user directory") {
        bool result = manager.savePreset("My Bass Preset", "Bass");
        REQUIRE(result);

        // Verify file was created in the Bass subdirectory
        auto expectedPath = fixture.userDir() / "Bass" / "My Bass Preset.vstpreset";
        REQUIRE(fs::exists(expectedPath));
    }

    SECTION("savePreset creates subcategory directory if needed") {
        bool result = manager.savePreset("New Preset", "Experimental");
        REQUIRE(result);

        auto expectedDir = fixture.userDir() / "Experimental";
        REQUIRE(fs::exists(expectedDir));
        REQUIRE(fs::is_directory(expectedDir));
    }

    SECTION("savePreset works for each Disrumpo subcategory") {
        auto config = Disrumpo::makeDisrumpoPresetConfig();
        for (const auto& subcategory : config.subcategoryNames) {
            std::string name = "Test_" + subcategory;
            bool result = manager.savePreset(name, subcategory);
            REQUIRE(result);
        }
    }
}

// =============================================================================
// T117: Preset Appears in Browser After Save
// =============================================================================

TEST_CASE("Disrumpo saved preset appears in browser", "[disrumpo][preset][save]") {
    PresetSaveFixture fixture;
    auto manager = fixture.createManager();

    manager.setStateProvider([&fixture]() -> Steinberg::IBStream* {
        return fixture.createMinimalStateStream();
    });

    SECTION("saved preset found by scanPresets") {
        manager.savePreset("My Sweep", "Sweep");
        auto presets = manager.scanPresets();

        // Should find the saved preset
        bool found = false;
        for (const auto& p : presets) {
            if (p.name == "My Sweep") {
                found = true;
                REQUIRE_FALSE(p.isFactory);
            }
        }
        REQUIRE(found);
    }

    SECTION("saved preset found by getPresetsForSubcategory") {
        manager.savePreset("My Bass", "Bass");
        manager.scanPresets();

        auto bassPresets = manager.getPresetsForSubcategory("Bass");
        REQUIRE(bassPresets.size() >= 1);

        bool found = false;
        for (const auto& p : bassPresets) {
            if (p.name == "My Bass") {
                found = true;
            }
        }
        REQUIRE(found);
    }
}

// =============================================================================
// T118: Overwrite Confirmation
// =============================================================================

TEST_CASE("Disrumpo preset overwrite behavior", "[disrumpo][preset][save][overwrite]") {
    PresetSaveFixture fixture;
    auto manager = fixture.createManager();

    manager.setStateProvider([&fixture]() -> Steinberg::IBStream* {
        return fixture.createMinimalStateStream();
    });

    SECTION("saving with same name overwrites existing file") {
        // Save first time
        REQUIRE(manager.savePreset("Duplicate", "Bass"));
        auto path = fixture.userDir() / "Bass" / "Duplicate.vstpreset";
        REQUIRE(fs::exists(path));

        // Save again with same name
        REQUIRE(manager.savePreset("Duplicate", "Bass"));
        REQUIRE(fs::exists(path));

        // File should still exist (overwritten) with non-zero size
        REQUIRE(fs::file_size(path) > 0);
    }
}

// =============================================================================
// T119: Save Failure Error Handling
// =============================================================================

TEST_CASE("Disrumpo preset save error handling", "[disrumpo][preset][save][error]") {
    PresetSaveFixture fixture;
    auto manager = fixture.createManager();

    SECTION("savePreset fails without state provider") {
        REQUIRE_FALSE(manager.savePreset("Test", "Bass"));
    }

    SECTION("savePreset fails with state provider returning nullptr") {
        manager.setStateProvider([]() -> Steinberg::IBStream* {
            return nullptr;
        });
        REQUIRE_FALSE(manager.savePreset("Test", "Bass"));
    }

    SECTION("savePreset fails with invalid name") {
        manager.setStateProvider([&fixture]() -> Steinberg::IBStream* {
            return fixture.createMinimalStateStream();
        });

        REQUIRE_FALSE(manager.savePreset("", "Bass"));
        REQUIRE_FALSE(manager.savePreset("Bad/Name", "Bass"));
        REQUIRE_FALSE(manager.savePreset("Bad\\Name", "Bass"));
    }
}

// =============================================================================
// T126-T127: Name Validation
// =============================================================================

TEST_CASE("Disrumpo preset name validation for save", "[disrumpo][preset][save][validation]") {
    SECTION("empty name is rejected") {
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName(""));
    }

    SECTION("names with special filesystem characters are rejected") {
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Test/Name"));
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Test\\Name"));
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Test:Name"));
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Test*Name"));
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Test?Name"));
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Test\"Name"));
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Test<Name"));
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Test>Name"));
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Test|Name"));
    }

    SECTION("valid names with spaces, hyphens, underscores are accepted") {
        REQUIRE(Krate::Plugins::PresetManager::isValidPresetName("Heavy Bass Preset"));
        REQUIRE(Krate::Plugins::PresetManager::isValidPresetName("Lo-Fi_Crush_01"));
        REQUIRE(Krate::Plugins::PresetManager::isValidPresetName("Sweep (Wide)"));
    }

    SECTION("names at 255 character limit are accepted") {
        std::string maxName(255, 'a');
        REQUIRE(Krate::Plugins::PresetManager::isValidPresetName(maxName));
    }

    SECTION("names exceeding 255 characters are rejected") {
        std::string longName(256, 'a');
        REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName(longName));
    }
}
