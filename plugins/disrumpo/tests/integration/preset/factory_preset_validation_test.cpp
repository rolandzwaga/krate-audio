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

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "../../test_support/disrumpo_rig.h"

#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/pink_noise_filter.h>

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"

#include <filesystem>
#include <functional>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <numbers>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

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

// The factory presets of this checkout, whatever the working directory (D-131)
fs::path getPresetsDir() { return fs::path(DISRUMPO_RESOURCES_DIR) / "presets"; }

// Deterministic pink noise (Kellet filter over Xorshift32), mean removed, then
// scaled so its peak is `peak` (linear). The raw 2 s pink sequence has a sample
// mean of 1.4 % of its peak, which an amplifying preset would carry into the
// output DC reading; the DC bar is meant for DC the plugin produces.
std::vector<float> pinkNoise(size_t n, float peak) {
    Krate::DSP::Xorshift32 rng(0x7301u);
    Krate::DSP::PinkNoiseFilter pink;
    std::vector<float> x(n);
    double sum = 0.0;
    for (auto& v : x) {
        v = pink.process(rng.nextFloat());
        sum += v;
    }
    const auto mean = static_cast<float>(sum / static_cast<double>(n));
    float p = 0.0f;
    for (auto& v : x) {
        v -= mean;
        p = std::max(p, std::fabs(v));
    }
    for (auto& v : x) v *= peak / p;
    return x;
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
        {"Init", 4}, {"Sweep", 15}, {"Morph", 15},
        {"Bass", 10}, {"Leads", 10}, {"Pads", 10},
        {"Drums", 10}, {"Experimental", 16}, {"Chaos", 10},
        {"Dynamic", 10}, {"Lo-Fi", 10}
    };

    for (const auto& [category, expectedCount] : expectedCounts) {
        auto categoryDir = presetsDir / category;
        auto presets = findPresetFiles(categoryDir);
        INFO("Category: " << category);
        CHECK(std::cmp_equal(presets.size(), expectedCount));
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
                CHECK(!names.contains(name));
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
        worstMs = std::max(worstMs, ms);
    }

    double avgMs = totalMs / kRuns;
    INFO("Preset: " << largestPreset.filename().string()
         << ", Size: " << largestSize << " bytes"
         << ", Avg: " << avgMs << " ms"
         << ", Worst: " << worstMs << " ms");
    CHECK(worstMs < 50.0);
}

// D-144 (T3.01): every factory preset, loaded into a fresh 44.1 kHz instance,
// renders 2 s of pink noise whose peak is -12 dBFS (the same signal on L and R)
// to finite audio with peak <= 0 dBFS, |DC| < 0.01 per channel and RMS above
// -60 dBFS. One line per preset is printed for the level table.
TEST_CASE("Every factory preset renders finite, bounded, non-silent audio", "[disrumpo][preset][long][D-144]") {
    using namespace Disrumpo::TestSupport;
    constexpr double kSampleRate = 44100.0;
    const auto presetFiles = findPresetFiles(getPresetsDir());
    REQUIRE_FALSE(presetFiles.empty());
    const auto noise = pinkNoise(static_cast<size_t>(2.0 * kSampleRate), 0.25118864f);  // -12 dBFS peak

    int failing = 0;
    for (const auto& presetPath : presetFiles) {
        const std::string name =
            presetPath.parent_path().filename().string() + "/" + presetPath.stem().string();
        INFO("Preset: " << name);
        const auto comp = readPresetComponentState(presetPath);
        REQUIRE_FALSE(comp.empty());

        Rig rig(kSampleRate, 512);
        REQUIRE(setStateBytes(*rig.p, comp));
        std::vector<float> outL;
        std::vector<float> outR;
        rig.render(noise, noise, outL, outR, 512);

        size_t nonFinite = 0;
        for (size_t i = 0; i < outL.size(); ++i) {
            if (!fin(outL[i])) ++nonFinite;
            if (!fin(outR[i])) ++nonFinite;
        }
        const double peak = std::max(peakOf(outL.data(), outL.size()), peakOf(outR.data(), outR.size()));
        const double rmsL = rmsOf(outL.data(), outL.size());
        const double rmsR = rmsOf(outR.data(), outR.size());
        const double rms = std::sqrt(0.5 * (rmsL * rmsL + rmsR * rmsR));
        const double dcL = meanOf(outL.data(), outL.size());
        const double dcR = meanOf(outR.data(), outR.size());
        const double dc = std::max(std::fabs(dcL), std::fabs(dcR));

        const bool ok = nonFinite == 0 && peak <= 1.0 && dc < 0.01 && rms > 0.001;
        if (!ok) ++failing;
        std::printf("D-144 %s peak_dBFS %.2f rms_dBFS %.2f dc %.5f nonfinite %zu %s\n", name.c_str(), toDb(peak),
                    toDb(rms), dc, nonFinite, ok ? "ok" : "FAIL");
        CHECK(nonFinite == 0);
        CHECK(peak <= 1.0);
        CHECK(dc < 0.01);
        CHECK(rms > 0.001);
    }
    std::printf("D-144 %zu presets, %d failing\n", presetFiles.size(), failing);
}

// =============================================================================
// Audit T6.08 (probe "presets 0/8..7/8", D-046, D-160, D-161): every factory preset
// must sound the same whichever way a host loads it. 440 Hz at -12 dBFS, 0.5 s,
// 44.1 kHz, 512-sample blocks. Reference c: the file loaded by setState BEFORE
// setupProcessing / setActive (the host's restore order). Against c:
// - the file loaded by setState into an active processor, applied by a process()
//   call, then setupProcessing again: within 1e-5 (audit baseline: 120 of 121 differed,
//   the state did not survive setupProcessing);
// - getState of c, loaded into a fresh processor: within 1e-5 (baseline: 35 presets up to
//   1.25), and its render is not silent and keeps c's level (D-161: RMS > -60 dBFS,
//   |RMS b - RMS c| < 0.1 dB);
// - every parameter the controller sets from the file (Controller::setComponentState,
//   recorded) replayed through processParameterChanges, then setupProcessing: within 1e-4
//   (D-160: the setState path skipped the slot mapping the parameter path does).
// Each load into an active processor is followed by setupProcessing, as a host's
// sample-rate or block-size change would: straight after a load while active the
// instance glides from its previous patch by design (T1.24, "A deferred load renders
// like a load made while inactive"), which the plan's literal comparison of an active
// load against c measures instead.
// Asserted: the round trip, D-161 and the active load followed by setupProcessing (follow-up
// 86, user ruling 2026-11-10: 17 of 120 presets differed because the random modulation sources
// did not restart on prepare / reset; the active load rendered at once glides from the previous
// patch by design, T1.24, and is not compared). Printed, bar held back (follow-up 87): the
// parameter replay within 1e-4. Since the exact round trip of the plain-stored values (case
// "Replaying a factory preset's parameters stores exactly what setState stores") both paths
// hold bit-identical parameters; 9 presets with chaotic or self-oscillating types still render
// apart (up to 0.065, Sweep/Slow_Scan), from the order the values arrive in, not their values.
// The level bars (peak, DC, RMS) are the [D-144] case above.
// =============================================================================

namespace {

/// Disrumpo controller that records every parameter setComponentState sets.
class PresetParamRecorder : public Controller {
public:
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                     Steinberg::Vst::ParamValue value) override {
        calls.emplace_back(tag, static_cast<double>(value));
        return Controller::setParamNormalized(tag, value);
    }
    Disrumpo::TestSupport::PList calls;
};

double maxAbsDiffOf(const std::vector<float>& a, const std::vector<float>& b) {
    double m = 0.0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        m = std::max(m, std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
    }
    return m;
}

}  // namespace

TEST_CASE("Every factory preset renders the same after host-order load, round trip and parameter replay",
          "[disrumpo][audit][preset][long][D-046][D-160][D-161]") {
    using namespace Disrumpo::TestSupport;
    constexpr double kSr = 44100.0;
    const size_t len = static_cast<size_t>(kSr * 0.5);
    const size_t from = static_cast<size_t>(kSr * 0.1);
    std::vector<float> in(len);
    for (size_t i = 0; i < len; ++i) {
        in[i] = static_cast<float>(0.25118864315095801 *
                                   std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / kSr));
    }
    const auto files = findPresetFiles(getPresetsDir());
    REQUIRE(files.size() == 120);

    struct Renders {
        std::vector<float> l;
        std::vector<float> r;
    };
    const auto diff = [](const Renders& x, const Renders& y) {
        return std::max(maxAbsDiffOf(x.l, y.l), maxAbsDiffOf(x.r, y.r));
    };
    const auto rmsDbOf = [&](const Renders& x) {
        return toDb(std::max(rmsOf(x.l.data() + from, len - from), rmsOf(x.r.data() + from, len - from)));
    };
    // Load into an active processor through `load`, apply it with a 0-sample process()
    // call, then setupProcessing + setActive again, and render.
    const auto renderAfterActiveLoad = [&](const std::function<void(Rig&)>& load) {
        Renders out;
        Rig rig(kSr);
        load(rig);
        rig.apply({});
        rig.deactivate();
        rig.setup();
        rig.render(in, in, out.l, out.r);
        return out;
    };
    const auto renderHostOrder = [&](const std::vector<uint8_t>& state, std::vector<uint8_t>* saved) {
        Renders out;
        Rig rig(kSr, 512, /*doSetup*/ false);
        REQUIRE(setStateBytes(*rig.p, state));
        rig.setup();
        rig.render(in, in, out.l, out.r);
        if (saved) *saved = getStateBytes(*rig.p);
        return out;
    };

    int failing = 0;
    for (const auto& path : files) {
        const std::string name = path.parent_path().filename().string() + "/" + path.stem().string();
        INFO("Preset: " << name);
        const auto bytes = readPresetComponentState(path);
        REQUIRE_FALSE(bytes.empty());

        std::vector<uint8_t> saved;
        const Renders c = renderHostOrder(bytes, &saved);
        const Renders active = renderAfterActiveLoad([&](Rig& rig) { REQUIRE(setStateBytes(*rig.p, bytes)); });
        const Renders roundTrip = renderHostOrder(saved, nullptr);
        PList recorded;
        {
            PresetParamRecorder ctl;
            REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);
            ctl.calls.clear();
            std::vector<uint8_t> stream = bytes;
            auto* s = new Steinberg::MemoryStream(stream.data(), static_cast<Steinberg::TSize>(stream.size()));
            REQUIRE(ctl.setComponentState(s) == Steinberg::kResultOk);
            s->release();
            recorded = ctl.calls;
            ctl.terminate();
        }
        const Renders replay = renderAfterActiveLoad([&](Rig& rig) { rig.apply(recorded); });

        const double activeDiff = diff(c, active);
        const double roundTripDiff = diff(c, roundTrip);
        const double replayDiff = diff(c, replay);
        // The replay against setState through the same load sequence (both into an active
        // processor, then setupProcessing): what is left without follow-up 86's RandomSource
        const double replayVsSetState = diff(active, replay);
        const double rmsC = rmsDbOf(c);
        const double rmsB = rmsDbOf(roundTrip);
        // Asserted: the round trip and its level (D-161). The active-load and replay readings are
        // printed; their bars are held back for a ruling (follow-ups 86 and 87).
        const bool ok = roundTripDiff <= 1e-5 && rmsB > -60.0 && std::fabs(rmsB - rmsC) < 0.1 && activeDiff <= 1e-5;
        if (!ok) ++failing;
        std::printf("T6.08 %s active-load %.3g round-trip %.3g rms c %.2f b %.2f dBFS replay %.3g, vs setState %.3g "
                    "(%zu params) %s\n",
                    name.c_str(), activeDiff, roundTripDiff, rmsC, rmsB, replayDiff, replayVsSetState, recorded.size(),
                    ok ? "ok" : "FAIL");
        CHECK(roundTripDiff <= 1e-5);
        CHECK(rmsB > -60.0);
        CHECK(std::fabs(rmsB - rmsC) < 0.1);
        // Follow-up 86 (user ruling 2026-11-10): an active load followed by setupProcessing renders
        // like the host-order load (the random modulation sources restart on prepare and reset)
        CHECK(activeDiff <= 1e-5);
    }
    std::printf("T6.08 %zu presets, %d failing\n", files.size(), failing);
}

TEST_CASE("Loading a factory preset sets Bias, Folds and Bit Depth to what the processor runs",
          "[disrumpo][preset][D-160]") {
    // Follow-ups 34 and 87, user ruling 2026-11-09: the type's shape slot wins over the node
    // parameter, and the node parameter mirrors it. The controller's state load (the host's
    // setComponentState and the plugin's own preset browser, which sends every value to the
    // processor) sets kNodeBias / kNodeFolds / kNodeBitDepth to the value the processor derives
    // from the slots on setState, not to the stored node value a slot overrides. Every node of
    // every band of every factory preset, within 1e-6 (normalized).
    using namespace Disrumpo::TestSupport;
    const auto files = findPresetFiles(getPresetsDir());
    REQUIRE(files.size() == 120);
    int wrongPresets = 0;
    int wrongValues = 0;
    int checked = 0;
    for (const auto& path : files) {
        const std::string name = path.parent_path().filename().string() + "/" + path.stem().string();
        const auto bytes = readPresetComponentState(path);
        REQUIRE_FALSE(bytes.empty());
        Rig rig(44100.0, 512, false);
        REQUIRE(setStateBytes(*rig.p, bytes));
        rig.setup();
        std::map<Steinberg::Vst::ParamID, double> loaded;
        {
            PresetParamRecorder ctl;
            REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);
            ctl.calls.clear();
            std::vector<uint8_t> stream = bytes;
            auto* s = new Steinberg::MemoryStream(stream.data(), static_cast<Steinberg::TSize>(stream.size()));
            REQUIRE(ctl.setComponentState(s) == Steinberg::kResultOk);
            s->release();
            for (const auto& [id, v] : ctl.calls) loaded[id] = v;
            ctl.terminate();
        }
        int wrongHere = 0;
        for (uint8_t band = 0; band < kMaxBands; ++band) {
            const MorphEngine* engine = rig.p->getBandProcessor(band).getMorphEngine();
            REQUIRE(engine != nullptr);
            for (uint8_t node = 0; node < kMaxMorphNodes; ++node) {
                const auto& p = engine->getNodeParams(node);
                const std::pair<NodeParamType, double> want[] = {
                    {NodeParamType::kNodeBias, (static_cast<double>(p.bias) + 1.0) / 2.0},
                    {NodeParamType::kNodeFolds, (static_cast<double>(p.folds) - 1.0) / 11.0},
                    {NodeParamType::kNodeBitDepth, (static_cast<double>(p.bitDepth) - 4.0) / 20.0}};
                for (const auto& [type, value] : want) {
                    const auto it = loaded.find(makeNodeParamId(band, node, type));
                    REQUIRE(it != loaded.end());
                    ++checked;
                    if (std::fabs(it->second - value) > 1e-6) {
                        if (wrongHere == 0 && wrongPresets < 10) {
                            std::printf("D-160 %s band %d node %d param 0x%02x: controller %.6f, processor %.6f\n",
                                        name.c_str(), band, node, static_cast<unsigned>(type), it->second, value);
                        }
                        ++wrongHere;
                    }
                }
            }
        }
        wrongValues += wrongHere;
        if (wrongHere > 0) ++wrongPresets;
    }
    std::printf("D-160 dual controls: %d of %zu presets with a node parameter the processor overrides (%d of %d values)\n",
                wrongPresets, files.size(), wrongValues, checked);
    CHECK(wrongPresets == 0);
}

TEST_CASE("Replaying a factory preset's parameters stores exactly what setState stores", "[disrumpo][preset][D-160]") {
    // D-160 (follow-up 87, user ruling 2026-11-10): the state stores some values plain (band gain
    // and pan, crossover Hz, node drive / tone / bias, morph smoothing, routing amounts). A load
    // through the parameters (the controller's state load, the plugin's preset browser, a host
    // replaying automation) takes them plain -> normalized -> plain; computed in double both ways
    // they come back bit-exact. Every factory preset: the processor's getState after setState
    // and after the controller's parameters replayed, each decoded by the controller, must hold
    // the same value for every parameter (exact equality).
    using namespace Disrumpo::TestSupport;
    constexpr double kSr = 44100.0;
    const auto files = findPresetFiles(getPresetsDir());
    REQUIRE(files.size() == 120);
    const auto decode = [](const std::vector<uint8_t>& bytes) {
        PresetParamRecorder ctl;
        REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);
        ctl.calls.clear();
        std::vector<uint8_t> stream = bytes;
        auto* s = new Steinberg::MemoryStream(stream.data(), static_cast<Steinberg::TSize>(stream.size()));
        REQUIRE(ctl.setComponentState(s) == Steinberg::kResultOk);
        s->release();
        PList calls = ctl.calls;
        ctl.terminate();
        return calls;
    };
    Rig loaded(kSr);
    int presetsDiffering = 0;
    int valuesDiffering = 0;
    for (const auto& path : files) {
        const std::string name = path.parent_path().filename().string() + "/" + path.stem().string();
        const auto bytes = readPresetComponentState(path);
        REQUIRE_FALSE(bytes.empty());
        REQUIRE(setStateBytes(*loaded.p, bytes));
        loaded.apply({});
        const auto viaState = decode(getStateBytes(*loaded.p));
        Rig replayed(kSr);
        replayed.apply(decode(bytes));
        const auto viaReplay = decode(getStateBytes(*replayed.p));
        REQUIRE(viaState.size() == viaReplay.size());
        int here = 0;
        for (size_t i = 0; i < viaState.size(); ++i) {
            REQUIRE(viaState[i].first == viaReplay[i].first);
            if (viaState[i].second != viaReplay[i].second) {
                if (here == 0 && presetsDiffering < 8) {
                    std::printf("D-160 %s param 0x%04x: setState %.10g, replay %.10g\n", name.c_str(),
                                static_cast<unsigned>(viaState[i].first), viaState[i].second, viaReplay[i].second);
                }
                ++here;
            }
        }
        valuesDiffering += here;
        if (here > 0) ++presetsDiffering;
    }
    std::printf("D-160 exact parameters: %d of %zu presets differ (%d values)\n", presetsDiffering, files.size(),
                valuesDiffering);
    CHECK(presetsDiffering == 0);
}

// =============================================================================
// Drive re-stage (user ruling 2026-11-10): a distortion preset must sound distorted at a
// normal input level. Every non-Init factory preset, loaded the DAW way (setState before
// setupProcessing) into a fresh 48 kHz instance, is given 2 s of a 220 Hz sine at
// -18 dBFS (peak) and then 2 s of decaying 110 Hz saw plucks (peak -12 dBFS, tau 0.15 s, one
// every 0.5 s). Asserted, both at least -26 dB (5 %):
// - sine THD+N: everything but the 220 Hz fundamental over the fundamental, 0.75-1.75 s
//   (220 whole cycles), mono sum, read past the plugin's latency;
// - pluck non-linearity: the pluck section against the same render of the whole signal 40 dB
//   quieter, scaled back up (a linear patch, filters and modulation included, reads -inf).
// Before the re-stage (f097edcd): 56 of 116 presets below a bar (54 sine, 4 pluck) and the
// Init 1 Band sine at -131 dB.
// =============================================================================

namespace {

constexpr double kBiteSr = 48000.0;
constexpr double kBiteBarDb = -26.0;           // 5 % THD+N
constexpr double kBiteSineAmp = 0.12589254117941673;  // -18 dBFS
constexpr double kBiteSineSeconds = 2.0;
constexpr double kBitePluckSeconds = 2.0;

struct BiteReading {
    double sineThdnDb = 0.0;
    double pluckNonLinDb = 0.0;
    double rmsDb = 0.0;
    double peakDb = 0.0;
};

std::vector<float> biteSignal(double gain) {
    const auto nSine = static_cast<size_t>(kBiteSineSeconds * kBiteSr);
    const auto n = nSine + static_cast<size_t>(kBitePluckSeconds * kBiteSr);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kBiteSr;
        double v = 0.0;
        if (i < nSine) {
            v = kBiteSineAmp * std::sin(2.0 * std::numbers::pi * 220.0 * t);
        } else {
            const double tp = std::fmod(t - kBiteSineSeconds, 0.5);
            const double ph = std::fmod(110.0 * tp, 1.0);
            v = 2.0 * kBiteSineAmp * std::exp(-tp / 0.15) * (2.0 * ph - 1.0);
        }
        x[i] = static_cast<float>(gain * v);
    }
    return x;
}

/// Mono sum of a fresh host-order render of @p comp, with @p latency set to the plugin's latency.
std::vector<double> biteRender(const std::vector<uint8_t>& comp, const std::vector<float>& in, int& latency) {
    using namespace Disrumpo::TestSupport;
    Rig rig(kBiteSr, 512, /*doSetup*/ false);
    REQUIRE(setStateBytes(*rig.p, comp));
    rig.setup();
    std::vector<float> padded = in;
    padded.resize(in.size() + 8192, 0.0f);  // room for a Spectral latency of up to 4095
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(padded, padded, outL, outR);
    latency = static_cast<int>(rig.p->getLatencySamples());
    std::vector<double> mono(outL.size());
    for (size_t i = 0; i < mono.size(); ++i) {
        mono[i] = 0.5 * (static_cast<double>(outL[i]) + static_cast<double>(outR[i]));
    }
    return mono;
}

BiteReading measureBite(const std::vector<uint8_t>& comp) {
    BiteReading r;
    int lat = 0;
    int latQuiet = 0;
    const auto y = biteRender(comp, biteSignal(1.0), lat);
    const auto yq = biteRender(comp, biteSignal(0.01), latQuiet);
    const auto off = static_cast<size_t>(lat);

    // Sine THD+N over 0.75-1.75 s: 48000 samples, 220 whole cycles of 220 Hz
    const auto a = static_cast<size_t>(0.75 * kBiteSr);
    const auto n = static_cast<size_t>(kBiteSr);
    double c = 0.0;
    double s = 0.0;
    double sum = 0.0;
    double sq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double v = y[off + a + i];
        const double ph = 2.0 * std::numbers::pi * 220.0 * static_cast<double>(a + i) / kBiteSr;
        c += v * std::cos(ph);
        s += v * std::sin(ph);
        sum += v;
        sq += v * v;
    }
    const double mean = sum / static_cast<double>(n);
    const double total = sq / static_cast<double>(n) - mean * mean;
    const double f1 = 2.0 / static_cast<double>(n) * std::sqrt(c * c + s * s);
    const double fundPow = 0.5 * f1 * f1;
    r.sineThdnDb = 10.0 * std::log10(std::max(total - fundPow, 1e-24) / std::max(fundPow, 1e-24));

    // Pluck section against the quiet render scaled back up by 40 dB
    const auto p0 = static_cast<size_t>(kBiteSineSeconds * kBiteSr);
    const auto pn = static_cast<size_t>(kBitePluckSeconds * kBiteSr);
    const auto offQ = static_cast<size_t>(latQuiet);
    double num = 0.0;
    double den = 0.0;
    for (size_t i = 0; i < pn; ++i) {
        const double v = y[off + p0 + i];
        const double d = v - 100.0 * yq[offQ + p0 + i];
        num += d * d;
        den += v * v;
    }
    r.pluckNonLinDb = 10.0 * std::log10(std::max(num, 1e-24) / std::max(den, 1e-24));

    double all = 0.0;
    double pk = 0.0;
    const size_t len = p0 + pn;
    for (size_t i = 0; i < len; ++i) {
        all += y[off + i] * y[off + i];
        pk = std::max(pk, std::fabs(y[off + i]));
    }
    r.rmsDb = 10.0 * std::log10(std::max(all / static_cast<double>(len), 1e-24));
    r.peakDb = 20.0 * std::log10(std::max(pk, 1e-12));
    return r;
}

/// Measures every factory preset of @p category (all of them, or only @p onlyStem) against the bars.
void checkBite(const std::string& category, const std::string& onlyStem = {}) {
    const auto files = findPresetFiles(getPresetsDir() / category);
    REQUIRE_FALSE(files.empty());
    int measured = 0;
    int failing = 0;
    for (const auto& path : files) {
        if (!onlyStem.empty() && path.stem().string() != onlyStem) continue;
        const std::string name = category + "/" + path.stem().string();
        INFO("Preset: " << name);
        const auto comp = readPresetComponentState(path);
        REQUIRE_FALSE(comp.empty());
        const BiteReading r = measureBite(comp);
        const bool ok = r.sineThdnDb >= kBiteBarDb && r.pluckNonLinDb >= kBiteBarDb;
        ++measured;
        if (!ok) ++failing;
        std::printf("BITE %s sine THD+N %.1f dB, pluck non-linearity %.1f dB, RMS %.1f dBFS, peak %.1f dBFS %s\n",
                    name.c_str(), r.sineThdnDb, r.pluckNonLinDb, r.rmsDb, r.peakDb, ok ? "ok" : "FAIL");
        CHECK(r.sineThdnDb >= kBiteBarDb);
        CHECK(r.pluckNonLinDb >= kBiteBarDb);
    }
    std::printf("BITE %s: %d presets, %d below -26 dB\n", category.c_str(), measured, failing);
    REQUIRE(measured > 0);
}

}  // namespace

TEST_CASE("Init 1 Band (the default patch) distorts audibly at -18 dBFS", "[disrumpo][preset][bite]") {
    checkBite("Init", "Init_1_Band");
}
TEST_CASE("Every Bass preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") { checkBite("Bass"); }
TEST_CASE("Every Chaos preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") { checkBite("Chaos"); }
TEST_CASE("Every Drums preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") { checkBite("Drums"); }
TEST_CASE("Every Dynamic preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") { checkBite("Dynamic"); }
TEST_CASE("Every Experimental preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") {
    checkBite("Experimental");
}
TEST_CASE("Every Leads preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") { checkBite("Leads"); }
TEST_CASE("Every Lo-Fi preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") { checkBite("Lo-Fi"); }
TEST_CASE("Every Morph preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") { checkBite("Morph"); }
TEST_CASE("Every Pads preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") { checkBite("Pads"); }
TEST_CASE("Every Sweep preset distorts audibly at -18 dBFS", "[disrumpo][preset][bite][long]") { checkBite("Sweep"); }

// AllpassResonant at 8x (user ruling 2026-11-11, round 3) must not self-oscillate where it did not at
// 4x: AllpassSaturator's damping cutoff follows its clocked rate, and at 8x five presets' loops
// sustained themselves on a -58 dBFS input (Allpass Shimmer: -12.6 dBFS out, -48.1 at 4x; BITE pluck
// non-linearity -10.5 -> 41.0 dB). The quiet BITE render (-40 dB) at Oversample Max 8x stays within
// 1.5 dB of the same render at 4x. Experimental/All_Types_Morph, whose BITE pluck reading also moved
// (-4.5 -> 11.5 dB), was a different defect: its band 3 morph flips between 4x and 8x under its
// Random modulation, and each lane switch passed the Allpass Resonant node's DC through a cleared
// 10 Hz DC blocker (-50.9 -> -42.3 dBFS here; probe [allpass-lane-probe]). Since the incoming lane
// starts warm (round 4, DistortionAdapter::warmStartFrom) it reads -50.88 at 8x against -50.89 at 4x
// and is checked here too.
TEST_CASE("AllpassResonant presets at 8x track the quiet input as at 4x", "[disrumpo][preset][allpass][D-034]") {
    using namespace Disrumpo::TestSupport;
    for (const char* rel : {"Pads/Allpass_Shimmer", "Chaos/Rossler_Morph", "Dynamic/Pitch_Tracker", "Sweep/Slow_Scan",
                            "Experimental/All_Types_Morph"}) {
        const auto comp = readPresetComponentState(getPresetsDir() / (std::string(rel) + ".vstpreset"));
        REQUIRE_FALSE(comp.empty());
        const auto quietDb = [&](double osNorm) {
            Rig rig(kBiteSr, 512, /*doSetup*/ false);
            REQUIRE(setStateBytes(*rig.p, comp));
            rig.setup();
            rig.apply({{Disrumpo::kOversampleMaxId, osNorm}});
            rig.reactivate();
            const auto in = biteSignal(0.01);
            std::vector<float> outL;
            std::vector<float> outR;
            rig.render(in, in, outL, outR);
            double s = 0.0;
            const auto a = static_cast<size_t>(0.5 * kBiteSr);
            for (size_t i = a; i < outL.size(); ++i) s += static_cast<double>(outL[i]) * outL[i];
            return 10.0 * std::log10(s / static_cast<double>(outL.size() - a) + 1e-30);
        };
        const double at4 = quietDb(2.0 / 3.0);
        const double at8 = quietDb(1.0);
        std::printf("Allpass 8x %s: quiet render %.2f dBFS at 4x, %.2f dBFS at 8x\n", rel, at4, at8);
        INFO(rel << ": " << at4 << " dBFS at 4x, " << at8 << " at 8x");
        CHECK(std::fabs(at8 - at4) <= 1.5);
    }
}

// Probe (hidden, round 3 ruling 1): Experimental/All_Types_Morph's quiet render per soloed band at
// Oversample Max 4x and 8x, and band 3 with each of its nodes alone (Node Mix: morph X/Y on a node).
TEST_CASE("Probe: All_Types_Morph quiet render per band at 4x and 8x", "[.][all-types-probe]") {
    using namespace Disrumpo::TestSupport;
    const auto comp = readPresetComponentState(getPresetsDir() / "Experimental" / "All_Types_Morph.vstpreset");
    REQUIRE_FALSE(comp.empty());
    const auto quietDb = [&](double osNorm, int solo, const PList& extra) {
        Rig rig(kBiteSr, 512, /*doSetup*/ false);
        REQUIRE(setStateBytes(*rig.p, comp));
        rig.setup();
        PList l{{Disrumpo::kOversampleMaxId, osNorm}};
        if (solo >= 0) l.emplace_back(makeBandParamId(static_cast<uint8_t>(solo), BandParamType::kBandSolo), 1.0);
        for (const auto& e : extra) l.push_back(e);
        rig.apply(l);
        rig.reactivate();
        const auto in = biteSignal(0.01);
        std::vector<float> outL;
        std::vector<float> outR;
        rig.render(in, in, outL, outR);
        double s = 0.0;
        const auto a = static_cast<size_t>(0.5 * kBiteSr);
        for (size_t i = a; i < outL.size(); ++i) s += static_cast<double>(outL[i]) * outL[i];
        return 10.0 * std::log10(s / static_cast<double>(outL.size() - a) + 1e-30);
    };
    for (int solo = -1; solo < 4; ++solo) {
        std::printf("ATM solo band %d: quiet %.2f dBFS at 4x, %.2f at 8x\n", solo, quietDb(2.0 / 3.0, solo, {}),
                    quietDb(1.0, solo, {}));
    }
    for (const double osNorm : {2.0 / 3.0, 1.0}) {
        // Band 3 soloed, no modulation, the cursor stepped between (1, 0) and (0.9, 0) every 0.3 s
        Rig rig(kBiteSr, 512, /*doSetup*/ false);
        REQUIRE(setStateBytes(*rig.p, comp));
        rig.setup();
        rig.apply({{Disrumpo::kOversampleMaxId, osNorm},
                   {makeBandParamId(3, BandParamType::kBandSolo), 1.0},
                   {makeRoutingParamId(3, 2), 0.5},
                   {makeBandParamId(3, BandParamType::kBandMorphX), 1.0},
                   {makeBandParamId(3, BandParamType::kBandMorphY), 0.0}});
        rig.reactivate();
        const auto in = biteSignal(0.01);
        std::vector<float> outL;
        std::vector<float> outR;
        const auto step = static_cast<size_t>(0.3 * kBiteSr);
        rig.render(in, in, outL, outR, 512, [&](size_t, size_t start, PChanges& pc) {
            if (start > 0 && (start / step) != ((start - 512) / step)) {
                pc.addChange(makeBandParamId(3, BandParamType::kBandMorphX), (start / step) % 2 == 1 ? 0.9 : 1.0);
            }
        });
        double pk = 0.0;
        double s = 0.0;
        for (size_t i = static_cast<size_t>(0.5 * kBiteSr); i < outL.size(); ++i) {
            pk = std::max(pk, std::fabs(static_cast<double>(outL[i])));
            s += static_cast<double>(outL[i]) * outL[i];
        }
        std::printf("ATM band 3 cursor stepped 1.0 <-> 0.9 at OS Max %s: RMS %.1f dBFS, peak %.1f dBFS\n",
                    osNorm < 1.0 ? "4x" : "8x", 10.0 * std::log10(s / static_cast<double>(outL.size()) + 1e-30),
                    20.0 * std::log10(pk + 1e-15));
    }
    for (uint8_t node = 0; node < 4; ++node) {
        const PList off{{makeNodeParamId(3, node, NodeParamType::kNodeDrive), 0.0}};
        std::printf("ATM band 3 node %u drive 0: quiet %.2f dBFS at 4x, %.2f at 8x\n", static_cast<unsigned>(node),
                    quietDb(2.0 / 3.0, 3, off), quietDb(1.0, 3, off));
    }
    {
        // Band 3 soloed at 8x with its modulation: 50 ms RMS and peak over time
        Rig rig(kBiteSr, 512, /*doSetup*/ false);
        REQUIRE(setStateBytes(*rig.p, comp));
        rig.setup();
        rig.apply({{Disrumpo::kOversampleMaxId, 1.0}, {makeBandParamId(3, BandParamType::kBandSolo), 1.0}});
        rig.reactivate();
        const auto in = biteSignal(0.01);
        std::vector<float> outL;
        std::vector<float> outR;
        int lastFactor = -1;
        rig.render(in, in, outL, outR, 512, [&](size_t, size_t start, PChanges&) {
            const auto& bp = rig.p->getBandProcessor(3);
            const int f = bp.getActiveOversampleFactor();
            if (f != lastFactor) {
                std::printf("ATM band 3 8x factor %d from block at %.3f s (requested %d)\n", f,
                            static_cast<double>(start) / kBiteSr, bp.getOversampleFactor());
                lastFactor = f;
            }
        });
        const auto w = static_cast<size_t>(0.05 * kBiteSr);
        for (size_t s0 = 0; s0 + w <= outL.size(); s0 += w) {
            double s = 0.0;
            double pk = 0.0;
            for (size_t i = s0; i < s0 + w; ++i) {
                s += static_cast<double>(outL[i]) * outL[i];
                pk = std::max(pk, std::fabs(static_cast<double>(outL[i])));
            }
            std::printf("ATM band 3 8x t %.2f s: RMS %.1f dBFS, peak %.1f dBFS\n", static_cast<double>(s0) / kBiteSr,
                        10.0 * std::log10(s / static_cast<double>(w) + 1e-30), 20.0 * std::log10(pk + 1e-15));
        }
    }
    // Band 3 with the morph cursor on each node (X/Y corners of the 4-node square) and the modulation
    // routed to its X removed (routing amount 0 is not reachable here: the cursor is pinned instead)
    const std::array<std::pair<double, double>, 6> corners{
        {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}, {0.9, 0.0}, {0.0, 0.9}}};
    for (const auto& corner : corners) {
        const PList at{{makeBandParamId(3, BandParamType::kBandMorphX), corner.first},
                       {makeBandParamId(3, BandParamType::kBandMorphY), corner.second},
                       {makeRoutingParamId(3, 2), 0.5}};  // routing 3 (Random -> band 3 X) amount 0
        std::printf("ATM band 3 cursor at (%.1f, %.1f), no modulation: quiet %.2f dBFS at 4x, %.2f at 8x\n",
                    corner.first, corner.second, quietDb(2.0 / 3.0, 3, at), quietDb(1.0, 3, at));
    }
}

// Probe (hidden, round 3 ruling 1): Pads/Allpass_Shimmer's pluck non-linearity moved -10.5 -> 41.0
// dB when AllpassResonant went to 8x. RMS of the loud and the quiet (-40 dB) BITE renders per
// section at Oversample Max 4x and 8x, and the loud render minus 100 x the quiet one.
TEST_CASE("Probe: Allpass_Shimmer quiet vs loud at 4x and 8x", "[.][allpass-shimmer-probe]") {
    using namespace Disrumpo::TestSupport;
    const auto comp = readPresetComponentState(getPresetsDir() / "Pads" / "Allpass_Shimmer.vstpreset");
    REQUIRE_FALSE(comp.empty());
    for (const double osNorm : {2.0 / 3.0, 1.0}) {
        const auto render = [&](double gain) {
            Rig rig(kBiteSr, 512, /*doSetup*/ false);
            REQUIRE(setStateBytes(*rig.p, comp));
            rig.setup();
            rig.apply({{Disrumpo::kOversampleMaxId, osNorm}});
            rig.reactivate();
            const auto in = biteSignal(gain);
            std::vector<float> outL;
            std::vector<float> outR;
            rig.render(in, in, outL, outR);
            return std::make_pair(outL, static_cast<int>(rig.p->getLatencySamples()));
        };
        const auto [loud, latL] = render(1.0);
        const auto [quiet, latQ] = render(0.01);
        const auto section = [&](const std::vector<float>& y, double t0, double t1) {
            double s = 0.0;
            const auto a = static_cast<size_t>(t0 * kBiteSr);
            const auto b = static_cast<size_t>(t1 * kBiteSr);
            for (size_t i = a; i < b; ++i) s += static_cast<double>(y[i]) * y[i];
            return 10.0 * std::log10(s / static_cast<double>(b - a) + 1e-30);
        };
        std::printf("APS OS Max %s (latency %d / %d)\n", osNorm < 1.0 ? "4x" : "8x", latL, latQ);
        for (const auto& [t0, t1] : {std::pair{0.5, 2.0}, std::pair{2.0, 2.5}, std::pair{2.5, 3.0}, std::pair{3.5, 4.0}}) {
            double num = 0.0;
            double den = 0.0;
            for (auto i = static_cast<size_t>(t0 * kBiteSr); i < static_cast<size_t>(t1 * kBiteSr); ++i) {
                const double d = static_cast<double>(loud[i]) - 100.0 * quiet[i];
                num += d * d;
                den += static_cast<double>(loud[i]) * loud[i];
            }
            std::printf("APS   %.1f-%.1f s: loud %.1f dBFS, quiet %.1f dBFS (+40 dB: %.1f), loud - 100 quiet %.1f dB re loud\n",
                        t0, t1, section(loud, t0, t1), section(quiet, t0, t1), section(quiet, t0, t1) + 40.0,
                        10.0 * std::log10(num / (den + 1e-30) + 1e-30));
        }
    }
}

// =============================================================================
// Probe (hidden, drive re-stage 2026-11-10): every factory preset on the 6 s audition drum riff
// (DISRUMPO_RIFF_WAV, default f:/tmp/disrumpo-brutal/dry_riff.wav: 48 kHz, -18 dBFS RMS), loaded
// the DAW way into a fresh 48 kHz instance. RIFF: peak (both channels) and RMS. RIFFCLIP: the
// same preset with its Output Gain lowered by up to 18 dB and scaled back up digitally (the gain
// is linear and sits right before the output safety clipper), i.e. the output without the
// clipper; its peak, and the energy the clipper changes, in dB re that unclipped output.
// =============================================================================

namespace {

std::vector<float> readRiffMono() {
    const char* env = std::getenv("DISRUMPO_RIFF_WAV");
    std::ifstream f(env != nullptr ? env : "f:/tmp/disrumpo-brutal/dry_riff.wav", std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t o = 12;
    int channels = 2;
    std::vector<float> x;
    while (o + 8 <= b.size()) {
        uint32_t sz = 0;
        std::memcpy(&sz, b.data() + o + 4, 4);
        if (std::memcmp(b.data() + o, "fmt ", 4) == 0) {
            uint16_t ch = 0;
            std::memcpy(&ch, b.data() + o + 10, 2);
            channels = ch;
        }
        if (std::memcmp(b.data() + o, "data", 4) == 0) {
            const size_t n = sz / 4 / static_cast<size_t>(channels);
            x.resize(n);
            for (size_t i = 0; i < n; ++i) {
                std::memcpy(&x[i], b.data() + o + 8 + i * 4 * static_cast<size_t>(channels), 4);
            }
            break;
        }
        o += 8 + sz;
    }
    return x;
}

void riffRender(std::vector<uint8_t> comp, const std::vector<float>& in, double gainDropDb, std::vector<float>& l,
                std::vector<float>& r) {
    using namespace Disrumpo::TestSupport;
    if (gainDropDb > 0.0) {
        float g = 0.5f;
        std::memcpy(&g, comp.data() + 8, 4);
        g = static_cast<float>(static_cast<double>(g) - gainDropDb / 48.0);
        std::memcpy(comp.data() + 8, &g, 4);
    }
    Rig rig(48000.0, 512, /*doSetup*/ false);
    REQUIRE(setStateBytes(*rig.p, comp));
    rig.setup();
    rig.render(in, in, l, r);
}

}  // namespace

TEST_CASE("Probe: factory presets on the audition drum riff", "[.][riff-probe]") {
    const auto in = readRiffMono();
    REQUIRE(in.size() > 48000u);
    const auto files = findPresetFiles(getPresetsDir());
    for (const auto& path : files) {
        const std::string name = path.parent_path().filename().string() + "/" + path.stem().string();
        const auto comp = readPresetComponentState(path);
        std::vector<float> l;
        std::vector<float> r;
        riffRender(comp, in, 0.0, l, r);
        double pk = 0.0;
        double sl = 0.0;
        double sr = 0.0;
        for (size_t i = 0; i < l.size(); ++i) {
            pk = std::max({pk, std::fabs(static_cast<double>(l[i])), std::fabs(static_cast<double>(r[i]))});
            sl += static_cast<double>(l[i]) * l[i];
            sr += static_cast<double>(r[i]) * r[i];
        }
        const double rms = std::sqrt(0.5 * (sl + sr) / static_cast<double>(l.size()));
        std::printf("RIFF %s peak_dBFS %.2f rms_dBFS %.2f\n", name.c_str(), 20.0 * std::log10(pk + 1e-12),
                    20.0 * std::log10(rms + 1e-12));
        float g = 0.5f;
        std::memcpy(&g, comp.data() + 8, 4);
        const double drop = std::min(18.0, static_cast<double>(g) * 48.0);
        std::vector<float> ul;
        std::vector<float> ur;
        riffRender(comp, in, drop, ul, ur);
        const double up = std::pow(10.0, drop / 20.0);
        double upk = 0.0;
        double num = 0.0;
        double den = 0.0;
        for (size_t i = 0; i < l.size(); ++i) {
            const double a = up * ul[i];
            const double b = up * ur[i];
            upk = std::max({upk, std::fabs(a), std::fabs(b)});
            num += (l[i] - a) * (l[i] - a) + (r[i] - b) * (r[i] - b);
            den += a * a + b * b;
        }
        std::printf("RIFFCLIP %s unclipped_peak_dBFS %.2f clipper_change_dB %.1f (gain drop %.1f dB)\n", name.c_str(),
                    20.0 * std::log10(upk + 1e-12), 10.0 * std::log10(num / (den + 1e-30) + 1e-30), drop);
    }
}
