// ==============================================================================
// Factory Preset Validation Test
// ==============================================================================
// Verifies that all 120 factory presets load correctly through the Processor's
// setState() and can round-trip through getState()/setState() without data loss.
//
// FR-015: Factory presets round-trip faithfully
// SC-004: All 120 factory presets pass validation
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <cstring>
#include <chrono>

using Catch::Approx;
using namespace Disrumpo;
namespace fs = std::filesystem;

namespace {

// Read a .vstpreset file and extract the component state data
std::vector<uint8_t> readPresetComponentState(const fs::path& presetPath) {
    std::ifstream f(presetPath, std::ios::binary);
    if (!f) return {};

    // Read header
    char magic[4];
    f.read(magic, 4);
    if (std::string(magic, 4) != "VST3") return {};

    uint32_t version = 0;
    f.read(reinterpret_cast<char*>(&version), 4);

    char classId[32];
    f.read(classId, 32);

    int64_t listOffset = 0;
    f.read(reinterpret_cast<char*>(&listOffset), 8);

    // Read chunk list to find component state
    f.seekg(listOffset);
    char listMagic[4];
    f.read(listMagic, 4);
    if (std::string(listMagic, 4) != "List") return {};

    uint32_t entryCount = 0;
    f.read(reinterpret_cast<char*>(&entryCount), 4);

    for (uint32_t i = 0; i < entryCount; ++i) {
        char chunkId[4];
        f.read(chunkId, 4);
        int64_t offset = 0;
        int64_t size = 0;
        f.read(reinterpret_cast<char*>(&offset), 8);
        f.read(reinterpret_cast<char*>(&size), 8);

        if (std::string(chunkId, 4) == "Comp") {
            // Found component state
            std::vector<uint8_t> data(static_cast<size_t>(size));
            f.seekg(offset);
            f.read(reinterpret_cast<char*>(data.data()), size);
            return data;
        }
    }
    return {};
}

// Discover all .vstpreset files in a directory tree
std::vector<fs::path> findPresetFiles(const fs::path& rootDir) {
    std::vector<fs::path> presets;
    if (!fs::exists(rootDir)) return presets;

    for (const auto& entry : fs::recursive_directory_iterator(rootDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".vstpreset") {
            presets.push_back(entry.path());
        }
    }
    // Sort for deterministic ordering
    std::sort(presets.begin(), presets.end());
    return presets;
}

// Create and initialize a Processor for testing
std::unique_ptr<Processor> createTestProcessor() {
    auto proc = std::make_unique<Processor>();
    proc->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.processMode = Steinberg::Vst::kRealtime;
    proc->setupProcessing(setup);

    return proc;
}

// Get the preset resources directory
fs::path getPresetsDir() {
    // Try multiple paths relative to working directory
    std::vector<fs::path> candidates = {
        "plugins/disrumpo/resources/presets",
        "../plugins/disrumpo/resources/presets",
        "../../plugins/disrumpo/resources/presets",
        "../../../plugins/disrumpo/resources/presets",
    };

    // Also try from the test executable location
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) return candidate;
    }

    // Fallback: absolute path
    fs::path absolute = "F:/projects/iterum/plugins/disrumpo/resources/presets";
    if (fs::exists(absolute)) return absolute;

    return "plugins/disrumpo/resources/presets"; // Return default even if missing
}

} // anonymous namespace

TEST_CASE("Factory preset files exist", "[preset][factory][validation]") {
    auto presetsDir = getPresetsDir();
    auto presetFiles = findPresetFiles(presetsDir);

    REQUIRE_FALSE(presetFiles.empty());
    INFO("Found " << presetFiles.size() << " preset files in " << presetsDir.string());
    CHECK(presetFiles.size() == 120);
}

TEST_CASE("Factory presets load without error", "[preset][factory][validation]") {
    auto presetsDir = getPresetsDir();
    auto presetFiles = findPresetFiles(presetsDir);
    REQUIRE_FALSE(presetFiles.empty());

    for (const auto& presetPath : presetFiles) {
        DYNAMIC_SECTION("Load: " << presetPath.filename().string()) {
            // Read component state from .vstpreset file
            auto stateData = readPresetComponentState(presetPath);
            REQUIRE_FALSE(stateData.empty());

            // Create initialized processor and apply state
            auto processor = createTestProcessor();
            auto* stream = new Steinberg::MemoryStream(
                stateData.data(), static_cast<Steinberg::TSize>(stateData.size()));

            auto result = processor->setState(stream);
            stream->release();

            CHECK(result == Steinberg::kResultOk);
        }
    }
}

TEST_CASE("Factory presets round-trip through getState/setState", "[preset][factory][validation]") {
    auto presetsDir = getPresetsDir();
    auto presetFiles = findPresetFiles(presetsDir);
    REQUIRE_FALSE(presetFiles.empty());

    for (const auto& presetPath : presetFiles) {
        DYNAMIC_SECTION("Round-trip: " << presetPath.filename().string()) {
            // Read component state from .vstpreset file
            auto stateData = readPresetComponentState(presetPath);
            REQUIRE_FALSE(stateData.empty());

            // Load preset into processor, then save
            auto processor1 = createTestProcessor();
            {
                auto* stream = new Steinberg::MemoryStream(
                    stateData.data(), static_cast<Steinberg::TSize>(stateData.size()));
                auto result = processor1->setState(stream);
                stream->release();
                REQUIRE(result == Steinberg::kResultOk);
            }

            // First getState: processor's serialization after loading
            auto* outStream1 = new Steinberg::MemoryStream();
            {
                auto result = processor1->getState(outStream1);
                REQUIRE(result == Steinberg::kResultOk);
            }

            // Load the processor's output into a fresh processor
            auto processor2 = createTestProcessor();
            {
                outStream1->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                auto result = processor2->setState(outStream1);
                REQUIRE(result == Steinberg::kResultOk);
            }

            // Second getState
            auto* outStream2 = new Steinberg::MemoryStream();
            {
                auto result = processor2->getState(outStream2);
                REQUIRE(result == Steinberg::kResultOk);
            }

            // Verify sizes match
            auto size1 = outStream1->getSize();
            auto size2 = outStream2->getSize();
            CHECK(size1 == size2);

            // Compare with tolerance for floating-point round-trip through
            // normalize/denormalize (log/exp) transforms. The processor uses
            // log/exp for LFO rate normalization which can cause 1-2 ULP drift
            // per cycle in serialized float values. We verify the total byte
            // difference is minimal (at most a few bytes per affected float).
            if (size1 == size2 && size1 > 0) {
                const uint8_t* data1 = reinterpret_cast<const uint8_t*>(outStream1->getData());
                const uint8_t* data2 = reinterpret_cast<const uint8_t*>(outStream2->getData());

                // Count differing bytes. With ~1574 bytes total, allow up to
                // 16 bytes difference (4 floats with ULP drift in LSB).
                int differingBytes = 0;
                for (Steinberg::TSize i = 0; i < size1; ++i) {
                    if (data1[i] != data2[i]) {
                        ++differingBytes;
                    }
                }

                // Require at least 99% identical bytes
                double matchPct = 100.0 * (1.0 - static_cast<double>(differingBytes) /
                                  static_cast<double>(size1));
                INFO("Differing bytes: " << differingBytes << " / " << size1
                     << " (" << matchPct << "% match)");
                CHECK(differingBytes <= 16);
            }

            outStream1->release();
            outStream2->release();
        }
    }
}

TEST_CASE("Factory preset category distribution matches spec", "[preset][factory][validation]") {
    auto presetsDir = getPresetsDir();
    REQUIRE(fs::exists(presetsDir));

    // Expected categories and counts (FR-027, T173)
    std::map<std::string, int> expectedCounts = {
        {"Init", 5}, {"Sweep", 15}, {"Morph", 15},
        {"Bass", 10}, {"Leads", 10}, {"Pads", 10},
        {"Drums", 10}, {"Experimental", 15}, {"Chaos", 10},
        {"Dynamic", 10}, {"Lo-Fi", 10}
    };

    for (const auto& [category, expectedCount] : expectedCounts) {
        auto categoryDir = presetsDir / category;
        auto presets = findPresetFiles(categoryDir);
        INFO("Category: " << category);
        CHECK(static_cast<int>(presets.size()) == expectedCount);
    }
}

TEST_CASE("Factory preset names are unique within categories", "[preset][factory][validation]") {
    auto presetsDir = getPresetsDir();
    REQUIRE(fs::exists(presetsDir));

    for (const auto& entry : fs::directory_iterator(presetsDir)) {
        if (!entry.is_directory()) continue;

        std::set<std::string> names;
        auto categoryName = entry.path().filename().string();

        for (const auto& preset : fs::recursive_directory_iterator(entry.path())) {
            if (preset.is_regular_file() && preset.path().extension() == ".vstpreset") {
                auto name = preset.path().stem().string();
                INFO("Category: " << categoryName << ", Preset: " << name);
                CHECK(names.find(name) == names.end());
                names.insert(name);
            }
        }
    }
}

TEST_CASE("Factory preset load completes within 50ms", "[preset][factory][performance]") {
    // SC-002: Verify preset load completes within 50ms using high-resolution
    // timer wrapping full setState sequence at max config (8 bands, 4 nodes,
    // 32 mod routes). We test the most complex preset to verify worst-case.
    auto presetsDir = getPresetsDir();
    auto presetFiles = findPresetFiles(presetsDir);
    REQUIRE_FALSE(presetFiles.empty());

    // Find the largest preset file (most complex = worst case)
    fs::path largestPreset;
    uintmax_t largestSize = 0;
    for (const auto& p : presetFiles) {
        auto sz = fs::file_size(p);
        if (sz > largestSize) {
            largestSize = sz;
            largestPreset = p;
        }
    }
    REQUIRE_FALSE(largestPreset.empty());

    auto stateData = readPresetComponentState(largestPreset);
    REQUIRE_FALSE(stateData.empty());

    // Warm up processor creation
    auto warmup = createTestProcessor();
    (void)warmup;

    // Measure load time (average over multiple runs for stability)
    constexpr int kRuns = 10;
    double totalMs = 0.0;
    double worstMs = 0.0;

    for (int run = 0; run < kRuns; ++run) {
        auto processor = createTestProcessor();
        auto* stream = new Steinberg::MemoryStream(
            stateData.data(), static_cast<Steinberg::TSize>(stateData.size()));

        auto start = std::chrono::high_resolution_clock::now();
        auto result = processor->setState(stream);
        auto end = std::chrono::high_resolution_clock::now();

        stream->release();
        REQUIRE(result == Steinberg::kResultOk);

        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        totalMs += ms;
        if (ms > worstMs) worstMs = ms;
    }

    double avgMs = totalMs / kRuns;
    INFO("Preset: " << largestPreset.filename().string()
         << ", Size: " << largestSize << " bytes"
         << ", Avg: " << avgMs << " ms"
         << ", Worst: " << worstMs << " ms");
    CHECK(worstMs < 50.0);
}
