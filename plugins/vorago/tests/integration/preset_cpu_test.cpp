// ==============================================================================
// Vorago Phase 14 - per-preset CPU budget; FR-041, SC-017 (plan 6.17, T049)
// ==============================================================================
// Vorago_PresetCpu is HIDDEN ([.perf]) and wall-clock based: run it ALONE,
// nothing else executing (node tools/run-cpu-tests.js vorago_tests - the
// runner's FILTER excludes [vorago-sweep], ruling R-6).
//
//   For every factory preset and the default surface (no setState):
//     1. PresetHost at 48 kHz / 512; setState; block 0 carries NoteOn for all
//        four kCpuNotes and kPolyphonyId -> 0.6 (list index 3, 4 voices) so the
//        stored polyphony is overridden (C-5 bounds every preset at <= 4, so four
//        voices sounding kCpuNotes is the preset's worst-case cost).
//     2. Untimed pre-roll to the patch's own A + 5 s (makeTimeline: sus0).
//     3. 16 trials x 100 blocks, std::chrono::steady_clock, interleaving one
//        preset trial and one default trial. The default host is re-created and
//        re-pre-rolled whenever its next trial would leave its own Sus window
//        [A + 5 s, A + 65 s].
//   Gate  - worst min-trial(preset) / min-trial(default) <= 1.15 (FR-041).
//   Printed, not gated - each preset's ns/block against kReferenceNs
//   (3 200 000 ns, Phase 10 budget header) and the stored-polyphony figure
//   (measured separately only when the stored polyphony differs from 4;
//   otherwise it IS the forced figure).
//
// No fast-math exemption for this TU (tests/CMakeLists.txt): the timed code is
// what the Phase 10 budget measured.
// ==============================================================================

#include "plugin_ids.h"
#include "preset_test_support.h"
#include VORAGO_PERF_BUDGET_HEADER

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr double kCpuSampleRate = 48000.0;
constexpr std::size_t kCpuBlock = 512;
constexpr std::array<std::uint8_t, 4> kCpuNotes{36, 40, 43, 47};  // == processor_cpu_test.cpp
constexpr float kCpuVelocity = 100.0f / 127.0f;                    // quantiseVelocity -> 100
constexpr double kForcedPolyphonyNormalized = 0.6;                 // list index 3 -> 4 voices
constexpr std::size_t kForcedPolyphony = 4;
constexpr int kForcedPolyphonyStored = 4;  // the same value as the decoded (int) stored polyphony sees it
constexpr std::size_t kTrials = 16;
constexpr std::size_t kBlocksPerTrial = 100;
constexpr double kPresetCostCeiling = 1.15;  // FR-041 / SC-017
constexpr double kBlockSeconds = static_cast<double>(kCpuBlock) / kCpuSampleRate;

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedNs(Clock::time_point t0, Clock::time_point t1) noexcept {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

struct Patch {
    std::string name;
    std::vector<std::uint8_t> comp;  // empty -> the default surface (no setState)
    double sus0 = 0.0;               // A + 5 s
    double sus1 = 0.0;               // A + 65 s
    int storedPolyphony = 0;
};

[[nodiscard]] bool describePatch(Patch& p, std::string& why) {
    std::vector<std::uint8_t> scratch;
    VoragoTest::DecodedPresetState st;
    if (!VoragoTest::decodePresetState(
            VoragoTest::detail::stateBytesOrDefault(std::span<const std::uint8_t>(p.comp), scratch),
            st)) {
        why = p.name + ": decodePresetState failed";
        return false;
    }
    const VoragoTest::SweepTimeline tl = VoragoTest::makeTimeline(st, false);
    p.sus0 = tl.sus0;
    p.sus1 = tl.sus1;
    p.storedPolyphony = st.global.polyphony.load(std::memory_order_relaxed);
    return true;
}

/// One patch under a fresh processor: state loaded, the four notes (and the
/// forced polyphony) on block 0, pre-rolled untimed to sus0. Tracks its block
/// position so the caller can keep every timed block inside the Sus window.
class PatchHost {
public:
    [[nodiscard]] bool start(const Patch& p, bool forcePolyphony, std::string& why) {
        host_ = std::make_unique<VoragoTest::PresetHost>();
        if (host_->prepare(kCpuSampleRate, static_cast<Steinberg::int32>(kCpuBlock)) !=
            Steinberg::kResultOk) {
            why = p.name + ": prepare failed";
            return false;
        }
        if (!p.comp.empty() &&
            host_->loadState(std::span<const std::uint8_t>(p.comp)) != Steinberg::kResultOk) {
            why = p.name + ": setState failed";
            return false;
        }
        Krate::Test::EventList notes;
        for (const std::uint8_t note : kCpuNotes) {
            notes.addNoteOn(static_cast<Steinberg::int16>(note), kCpuVelocity, 0);
        }
        Krate::Test::ParameterChanges forced;
        if (forcePolyphony) {
            forced.addChange(::Vorago::kPolyphonyId, kForcedPolyphonyNormalized);
        }
        if (host_->process(kCpuBlock, &notes, forcePolyphony ? &forced : nullptr) !=
            Steinberg::kResultOk) {
            why = p.name + ": block 0 failed";
            return false;
        }
        blocks_ = 1;
        const auto preroll = static_cast<std::size_t>(std::ceil(p.sus0 / kBlockSeconds));
        while (blocks_ < preroll) {
            if (host_->process(kCpuBlock, nullptr, nullptr) != Steinberg::kResultOk) {
                why = p.name + ": pre-roll block failed";
                return false;
            }
            ++blocks_;
        }
        susEndBlocks_ = static_cast<std::size_t>(std::floor(p.sus1 / kBlockSeconds));
        return true;
    }

    [[nodiscard]] bool fits(std::size_t n) const noexcept { return blocks_ + n <= susEndBlocks_; }

    /// Times n blocks; the result flag records any non-kResultOk process() call.
    [[nodiscard]] double timeBlocks(std::size_t n, bool& ok) {
        const Clock::time_point t0 = Clock::now();
        for (std::size_t b = 0; b < n; ++b) {
            ok = (host_->process(kCpuBlock, nullptr, nullptr) == Steinberg::kResultOk) && ok;
        }
        const Clock::time_point t1 = Clock::now();
        blocks_ += n;
        return elapsedNs(t0, t1);
    }

    [[nodiscard]] std::size_t enginePolyphony() noexcept {
        const Krate::DSP::VoragoEngine* const e = host_->engineForTweak();
        return e != nullptr ? e->getPolyphony() : 0u;
    }

private:
    std::unique_ptr<VoragoTest::PresetHost> host_;
    std::size_t blocks_ = 0;
    std::size_t susEndBlocks_ = 0;
};

struct Row {
    std::string name;
    double presetNs = 0.0;   // best trial, ns per block, forced polyphony 4
    double defaultNs = 0.0;  // best trial of the interleaved default host
    double ratio = 0.0;
    int storedPolyphony = 0;
    double storedNs = 0.0;   // == presetNs when the stored polyphony is 4
};

}  // namespace

TEST_CASE("Vorago_PresetCpu", "[vorago][.perf][performance]") {
    namespace PD = ::Vorago::PresetDefs;
    std::string why;

    // ---------------------------------------------------------------- patches
    Patch defaultPatch;
    defaultPatch.name = "<default surface>";
    REQUIRE(describePatch(defaultPatch, why));
    INFO(why);
    REQUIRE(defaultPatch.sus0 == 160.0);  // FR-041: the default surface's A is 155 s

    std::vector<Patch> presets;
    for (const PD::VoragoPresetDef& def : PD::allPresets()) {
        Patch p;
        p.name = std::string(def.name);
        const bool built = VoragoTest::buildPresetComponentState(def, p.comp, why);
        INFO("preset " << p.name << ": " << why);
        REQUIRE(built);
        REQUIRE(describePatch(p, why));
        presets.push_back(std::move(p));
    }
    REQUIRE_FALSE(presets.empty());
    // 16 x 100 blocks = 17.07 s of audio, inside every 60 s Sus window.
    REQUIRE(static_cast<double>(kTrials * kBlocksPerTrial) * kBlockSeconds <= 60.0);

    std::printf("\n=== Vorago_PresetCpu (FR-041, SC-017; %zu presets + default, 48 kHz / 512, "
                "polyphony forced to %zu, %zu trials x %zu blocks, interleaved) ===\n",
                presets.size(), kForcedPolyphony, kTrials, kBlocksPerTrial);

    // ---------------------------------------------------------------- default host
    PatchHost defaultHost;
    REQUIRE(defaultHost.start(defaultPatch, true, why));
    INFO(why);
    REQUIRE(defaultHost.enginePolyphony() == kForcedPolyphony);

    // ---------------------------------------------------------------- per preset
    std::vector<Row> rows;
    rows.reserve(presets.size());
    for (const Patch& p : presets) {
        PatchHost presetHost;
        const bool started = presetHost.start(p, true, why);
        INFO(why);
        REQUIRE(started);
        REQUIRE(presetHost.enginePolyphony() == kForcedPolyphony);  // the override took

        bool ok = true;
        double bestP = std::numeric_limits<double>::max();
        double bestD = std::numeric_limits<double>::max();
        for (std::size_t trial = 0; trial < kTrials; ++trial) {
            REQUIRE(presetHost.fits(kBlocksPerTrial));
            if (!defaultHost.fits(kBlocksPerTrial)) {  // next trial would leave the default's Sus
                REQUIRE(defaultHost.start(defaultPatch, true, why));
            }
            bestP = std::min(bestP, presetHost.timeBlocks(kBlocksPerTrial, ok));
            bestD = std::min(bestD, defaultHost.timeBlocks(kBlocksPerTrial, ok));
        }
        REQUIRE(ok);

        Row r;
        r.name = p.name;
        r.presetNs = bestP / static_cast<double>(kBlocksPerTrial);
        r.defaultNs = bestD / static_cast<double>(kBlocksPerTrial);
        r.ratio = r.presetNs / r.defaultNs;
        r.storedPolyphony = p.storedPolyphony;
        r.storedNs = r.presetNs;
        if (p.storedPolyphony != kForcedPolyphonyStored) {
            // Stored-polyphony figure (recorded, not gated): the same patch at its own polyphony.
            PatchHost storedHost;
            REQUIRE(storedHost.start(p, false, why));
            bool okS = true;
            double bestS = std::numeric_limits<double>::max();
            for (std::size_t trial = 0; trial < kTrials; ++trial) {
                REQUIRE(storedHost.fits(kBlocksPerTrial));
                bestS = std::min(bestS, storedHost.timeBlocks(kBlocksPerTrial, okS));
            }
            REQUIRE(okS);
            r.storedNs = bestS / static_cast<double>(kBlocksPerTrial);
        }
        std::printf("  %-22s preset %10.0f ns  default %10.0f ns  ratio %.4f %s  "
                    "vs kReferenceNs %.4f  stored poly %d -> %10.0f ns\n",
                    r.name.c_str(), r.presetNs, r.defaultNs, r.ratio,
                    (r.ratio <= kPresetCostCeiling) ? "ok " : "BREACH",
                    r.presetNs / Krate::DSP::TestUtils::Vorago::kReferenceNs, r.storedPolyphony,
                    r.storedNs);
        rows.push_back(std::move(r));
    }

    // ---------------------------------------------------------------- verdict
    const auto worst = std::max_element(
        rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ratio < b.ratio; });
    REQUIRE(worst != rows.end());
    const double defaultRef = worst->defaultNs / Krate::DSP::TestUtils::Vorago::kReferenceNs;
    std::printf("worst preset: %s  ratio %.4f (gate <= %.2f)  default surface %.0f ns/block "
                "= %.4f x kReferenceNs (%.0f ns, recorded only)\n",
                worst->name.c_str(), worst->ratio, kPresetCostCeiling, worst->defaultNs,
                defaultRef, Krate::DSP::TestUtils::Vorago::kReferenceNs);
    WARN("SC-017 worst preset " << worst->name << " ratio " << worst->ratio
                                << " (gate <= " << kPresetCostCeiling << ")");
    for (const Row& r : rows) {
        INFO("FR-041 preset " << r.name << ": " << r.presetNs << " ns/block vs default "
                              << r.defaultNs << " ns/block, ratio " << r.ratio);
        CHECK(r.ratio <= kPresetCostCeiling);
    }
    REQUIRE(worst->ratio <= kPresetCostCeiling);  // FR-041 / SC-017, never relaxed
}
