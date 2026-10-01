// ==============================================================================
// Vorago Phase 14 - factory preset host, support harness, container and tree; FR-002..FR-009, FR-014, FR-021, FR-028..FR-032, FR-035; filled by T004, T006, T023, T024, T025-T027, T029
// ==============================================================================
// Skeleton registered by T003 (specs/vorago-phase14-presets-release/tasks.md) so
// the TU compiles into vorago_tests from the start.
//
// Vorago_PresetHost_DriveContract (T004, FR-021, plan 5.6): the Catch2-free
// VoragoTest::PresetHost prepares, renders event-in / stereo-out blocks that are
// finite and bounded, and round-trips component state byte-for-byte.
//
// T029 (FR-002..FR-009, FR-014, FR-028..FR-032, FR-035): the per-push harness
// over allPresets() and the committed resources/presets tree - directory set,
// container + Info, savePreset Info bytes, round-trip, browser scan, stream
// shape, tree == generator (kTreeFloatRelTol, plan 6.15) and parameter-space
// distinctness (plan 6.14). They hold on 0 presets and bite from T037 on.
// ==============================================================================

#include "vorago_preset_host.h"
#include "preset_test_support.h"
#include "vorago_test_fixture.h"  // kNumEcosystemRosterParams

#include "controller/controller.h"
#include "parameters/param_mapping.h"
#include "preset/preset_manager.h"
#include "preset/vorago_preset_config.h"
#include "plugin_ids.h"
#include "vorago_preset_defs.h"

#include "public.sdk/source/common/memorystream.h"

#include <vst_event_list.h>

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr float kPeakCeiling = 0.9661f;

// Bit-pattern based (fast-math immune) - never std::isnan / std::isfinite.
[[nodiscard]] bool allFiniteSamples(std::span<const float> x) noexcept {
    return std::ranges::all_of(x, [](float v) { return Krate::DSP::detail::isFinite(v); });
}

[[nodiscard]] bool allWithin(std::span<const float> x, float bound) noexcept {
    return std::ranges::all_of(x, [bound](float v) { return std::fabs(v) <= bound; });
}

[[nodiscard]] std::int32_t firstInt32LittleEndian(const std::vector<std::uint8_t>& b) {
    const std::uint32_t u = static_cast<std::uint32_t>(b[0]) |
                            (static_cast<std::uint32_t>(b[1]) << 8u) |
                            (static_cast<std::uint32_t>(b[2]) << 16u) |
                            (static_cast<std::uint32_t>(b[3]) << 24u);
    return static_cast<std::int32_t>(u);
}

}  // namespace

static_assert(!std::is_copy_constructible_v<VoragoTest::PresetHost>);
static_assert(!std::is_copy_assignable_v<VoragoTest::PresetHost>);

TEST_CASE("Vorago_PresetHost_DriveContract", "[vorago][preset]") {
    using Steinberg::kResultOk;

    VoragoTest::PresetHost h;
    REQUIRE(h.prepare(48000.0, 512) == kResultOk);

    Krate::Test::EventList ev;
    ev.addNoteOn(36, 100.0f / 127.0f, 0);
    REQUIRE(h.process(512, &ev, nullptr) == kResultOk);
    REQUIRE(h.outL().size() == 512);
    REQUIRE(h.outR().size() == 512);
    REQUIRE(allFiniteSamples(h.outL()));
    REQUIRE(allFiniteSamples(h.outR()));

    // 188 more blocks with no events: finite and bounded.
    bool finite = true;
    bool bounded = true;
    for (int block = 0; block < 188; ++block) {
        REQUIRE(h.process(512, nullptr, nullptr) == kResultOk);
        finite = finite && allFiniteSamples(h.outL()) && allFiniteSamples(h.outR());
        bounded = bounded && allWithin(h.outL(), kPeakCeiling) &&
                  allWithin(h.outR(), kPeakCeiling);
    }
    REQUIRE(finite);
    REQUIRE(bounded);

    // State round-trip through a second host.
    std::vector<std::uint8_t> a;
    REQUIRE(h.saveState(a));
    REQUIRE(a.size() >= 4u);
    REQUIRE(firstInt32LittleEndian(a) == ::Vorago::kCurrentStateVersion);

    VoragoTest::PresetHost h2;
    REQUIRE(h2.prepare(48000.0, 512) == kResultOk);
    REQUIRE(h2.loadState(std::span<const std::uint8_t>(a)) == kResultOk);
    std::vector<std::uint8_t> b;
    REQUIRE(h2.saveState(b));
    REQUIRE(a == b);
}

// ==============================================================================
// T006 - preset_test_support.h C1 rows (plan 5.8, P2-2)
// ==============================================================================

static_assert(2 * VoragoTest::kMaxTakes == VoragoTest::kNumSeedIndices);

TEST_CASE("Vorago_PresetSupport_TakeSets", "[vorago][preset]") {
    using VoragoTest::takeSeedIndex;
    using VoragoTest::seedNormalized;

    for (int j = 0; j < 8; ++j) {
        REQUIRE(takeSeedIndex(0, 0, j, 8) == j);
        REQUIRE(takeSeedIndex(0, 1, j, 8) == 8 + j);
    }
    REQUIRE(takeSeedIndex(15, 0, 1, 4) == 0);
    REQUIRE(takeSeedIndex(14, 1, 3, 2) == 3);

    for (int s = 0; s < VoragoTest::kNumSeedIndices; ++s) {
        std::set<int> a8;
        for (int j = 0; j < 8; ++j) {
            a8.insert(takeSeedIndex(s, 0, j, 8));
        }
        REQUIRE(a8.size() == 8u);
        for (const int k : {1, 2, 4, 8}) {
            std::set<int> a;
            std::set<int> b;
            for (int j = 0; j < k; ++j) {
                const int ia = takeSeedIndex(s, 0, j, k);
                const int ib = takeSeedIndex(s, 1, j, k);
                REQUIRE(ia >= 0);
                REQUIRE(ia < VoragoTest::kNumSeedIndices);
                REQUIRE(ib >= 0);
                REQUIRE(ib < VoragoTest::kNumSeedIndices);
                a.insert(ia);
                b.insert(ib);
            }
            REQUIRE(a.size() == static_cast<std::size_t>(k));
            REQUIRE(b.size() == static_cast<std::size_t>(k));
            for (const int x : a) {
                REQUIRE(!b.contains(x));     // disjoint
                REQUIRE(a8.count(x) == 1u);  // A_K subset of A_8
            }
        }
    }

    REQUIRE(seedNormalized(0) == 0.0);
    REQUIRE(seedNormalized(15) == 1.0);
    for (int i = 0; i < VoragoTest::kNumSeedIndices; ++i) {
        REQUIRE(::Vorago::indexFromNormalized(seedNormalized(i), 16) == i);
    }
}

TEST_CASE("Vorago_PresetSupport_RenderStreams", "[vorago][preset]") {
    using VoragoTest::RenderSpec;
    using VoragoTest::SweepCapture;
    using VoragoTest::renderPreset;

    const SweepCapture c = renderPreset(RenderSpec{.comp = {}, .end = 2.0, .capture = {{0.5, 1.5}}});
    REQUIRE(c.finite);
    REQUIRE(c.peak <= kPeakCeiling);
    REQUIRE(c.blockPowerL.size() == 188u);  // ceil(96000 / 512)
    REQUIRE(c.blockPowerR.size() == 188u);
    REQUIRE(c.capL.size() == 1u);
    REQUIRE(c.capR.size() == 1u);
    REQUIRE(c.capL[0].size() == 48000u);
    REQUIRE(c.capR[0].size() == 48000u);
    REQUIRE(allFiniteSamples(std::span<const float>(c.capL[0])));
    REQUIRE(allFiniteSamples(std::span<const float>(c.capR[0])));

    const SweepCapture seeded = renderPreset(RenderSpec{.comp = {}, .seedIndex = 3, .end = 2.0});
    REQUIRE(seeded.finite);
    REQUIRE(seeded.blockPowerL.size() == 188u);
    REQUIRE(seeded.capL.empty());

    const SweepCapture chord = renderPreset(
        RenderSpec{.comp = {}, .notes = {36, 40, 43, 47}, .forcePolyIndex = 3, .end = 2.0});
    REQUIRE(chord.finite);
    REQUIRE(chord.blockPowerR.size() == 188u);
}

TEST_CASE("Vorago_PresetSupport_PoolRunsAllJobs", "[vorago][preset]") {
    constexpr std::size_t kJobs = 37;
    for (const unsigned threads : {4u, 1u}) {
        std::vector<int> slots(kJobs, 0);
        std::vector<std::function<void()>> jobs;
        jobs.reserve(kJobs);
        for (std::size_t i = 0; i < kJobs; ++i) {
            jobs.emplace_back([&slots, i] { slots[i] += 1; });
        }
        VoragoTest::runJobs(jobs, threads);
        for (std::size_t i = 0; i < kJobs; ++i) {
            INFO("threads " << threads << " slot " << i);
            REQUIRE(slots[i] == 1);
        }
    }
}

TEST_CASE("Vorago_PresetSupport_EnvUnset", "[vorago][preset]") {
    const bool unset = (VoragoTest::sweepEnv("VORAGO_PHASE14_SURELY_UNSET") == std::nullopt);
    REQUIRE(unset);
}

// ==============================================================================
// T023 - the seven categories (FR-001, SC-001 config half; T029 adds the directory half)
// ==============================================================================

namespace {

/// The committed factory tree (FR-002).
[[nodiscard]] std::filesystem::path factoryPresetRoot() {
    return std::filesystem::path(VORAGO_RESOURCES_DIR) / "presets";
}

[[nodiscard]] std::string joinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) {
        out += "\n  ";
        out += l;
    }
    return lines.empty() ? std::string("(none)") : out;
}

}  // namespace

TEST_CASE("Vorago_FactoryPresets_CategoriesMatchConfig", "[vorago][preset]") {
    const std::vector<std::string> kSeven{"Drones",   "Abyss",    "Caverns", "Organisms",
                                          "Machines", "Textures", "Ghosts"};

    const auto cfg = ::Vorago::makeVoragoPresetConfig();
    REQUIRE(cfg.subcategoryNames == kSeven);  // order and bytes
    REQUIRE(cfg.pluginName == "Vorago");
    REQUIRE(cfg.pluginCategoryDesc == "Synth");
    const bool processorUidMatches = (cfg.processorUID == ::Vorago::kProcessorUID);
    REQUIRE(processorUidMatches);

    std::vector<std::string> expectedTabs{"All"};
    expectedTabs.insert(expectedTabs.end(), kSeven.begin(), kSeven.end());
    REQUIRE(::Vorago::makeVoragoPresetTabLabels() == expectedTabs);

    // ---- Directory half (T029, FR-002) ------------------------------------------
    // The directory set under resources/presets == the seven names, both ways;
    // every regular file is a .vstpreset directly inside a category directory, or
    // a .gitkeep in a category directory holding no preset.
    namespace fs = std::filesystem;
    const fs::path root = factoryPresetRoot();
    INFO("presets root: " << root.string());
    std::error_code ec;
    REQUIRE(fs::is_directory(root, ec));

    const std::set<std::string> sevenSet(kSeven.begin(), kSeven.end());
    std::set<std::string> dirs;
    std::vector<std::string> strays;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        std::error_code typeEc;
        if (entry.is_directory(typeEc)) {
            dirs.insert(entry.path().filename().string());
        } else {
            strays.push_back(entry.path().string() + " (not a category directory)");
        }
    }
    REQUIRE_FALSE(ec);
    REQUIRE(dirs == sevenSet);

    std::map<std::string, std::size_t> presetsIn;
    std::vector<std::string> gitkeepIn;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, ec)) {
        std::error_code typeEc;
        if (!entry.is_regular_file(typeEc)) {
            continue;
        }
        const fs::path rel = entry.path().lexically_relative(root);
        const bool directlyInCategory = std::distance(rel.begin(), rel.end()) == 2 &&
                                        sevenSet.contains(rel.begin()->string());
        if (!directlyInCategory) {
            strays.push_back(entry.path().string() + " (not directly inside a category)");
            continue;
        }
        const std::string category = rel.begin()->string();
        if (entry.path().extension() == ".vstpreset") {
            ++presetsIn[category];
        } else if (entry.path().filename() == ".gitkeep") {
            gitkeepIn.push_back(category);
        } else {
            strays.push_back(entry.path().string() + " (neither .vstpreset nor .gitkeep)");
        }
    }
    REQUIRE_FALSE(ec);
    for (const std::string& category : gitkeepIn) {
        if (presetsIn[category] > 0u) {
            strays.push_back(category + "/.gitkeep beside " + std::to_string(presetsIn[category]) +
                             " preset(s)");
        }
    }
    INFO("stray entries:" << joinLines(strays));
    REQUIRE(strays.empty());
}

// ==============================================================================
// T024 - tools/vorago_preset_defs.h: capability model and cell specs
// (FR-010, FR-022, plan 5.5, 6.7, 6.12). Expected values are literals typed
// from spec C-2.1 / plan 6.7, never read back from cellSpecs().
// ==============================================================================

namespace {

namespace PD = ::Vorago::PresetDefs;
using PD::Capability;

[[nodiscard]] constexpr std::size_t capIndex(Capability c) noexcept {
    return static_cast<std::size_t>(c);
}

[[nodiscard]] bool inCapRange(Capability c, Capability first, Capability last) noexcept {
    return capIndex(c) >= capIndex(first) && capIndex(c) <= capIndex(last);
}

struct ExpectedOverride {
    std::uint32_t id;
    double normalized;
};

void checkAblation(Capability c, std::initializer_list<ExpectedOverride> expected) {
    const PD::CellSpec& spec = PD::cellSpecs()[capIndex(c)];
    INFO("cell " << capIndex(c) << " (" << std::string(spec.label) << ")");
    REQUIRE(static_cast<std::size_t>(spec.ablationCount) == expected.size());
    std::size_t i = 0;
    for (const ExpectedOverride& e : expected) {
        CHECK(spec.ablation[i].id == e.id);
        CHECK(spec.ablation[i].normalized == e.normalized);
        ++i;
    }
}

[[nodiscard]] bool containsForbiddenXmlChar(std::string_view s) noexcept {
    return s.find_first_of("\"&<>") != std::string_view::npos;
}

}  // namespace

TEST_CASE("Vorago_PresetDefs_CellSpecsMatchSpec", "[vorago][preset]") {
    using PD::CapabilityGroup;
    using PD::Verification;

    REQUIRE(static_cast<int>(Capability::Count) == 79);
    const auto& specs = PD::cellSpecs();
    REQUIRE(specs.size() == 79u);

    // Every entry describes its own cell, with a non-empty label.
    int nS = 0;
    int nM = 0;
    int nE = 0;
    int nD = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        INFO("cell " << i);
        REQUIRE(capIndex(specs[i].cell) == i);
        REQUIRE(!specs[i].label.empty());
        REQUIRE(static_cast<std::size_t>(specs[i].ablationCount) <= std::size_t{4});
        switch (specs[i].group) {
            case CapabilityGroup::S: ++nS; break;
            case CapabilityGroup::M: ++nM; break;
            case CapabilityGroup::E: ++nE; break;
            case CapabilityGroup::D: ++nD; break;
        }
    }
    REQUIRE(nS == 10);
    REQUIRE(nM == 12);
    REQUIRE(nE == 7);
    REQUIRE(nD == 50);
    for (std::size_t i = 0; i < 10; ++i) {
        REQUIRE(specs[i].group == CapabilityGroup::S);
    }
    for (std::size_t i = 10; i < 22; ++i) {
        REQUIRE(specs[i].group == CapabilityGroup::M);
    }
    for (std::size_t i = 22; i < 29; ++i) {
        REQUIRE(specs[i].group == CapabilityGroup::E);
    }
    for (std::size_t i = 29; i < 79; ++i) {
        REQUIRE(specs[i].group == CapabilityGroup::D);
    }

    // Group S overrides (spec C-2.1 table).
    checkAblation(Capability::S1Noise, {{.id = 300, .normalized = 0.0}});
    checkAblation(Capability::S2Resonance, {{.id = 401, .normalized = 0.0}});
    checkAblation(Capability::S3Smear, {{.id = 700, .normalized = 0.0}, {.id = 701, .normalized = 0.0}});
    checkAblation(Capability::S4Ecology, {{.id = 500, .normalized = 0.0}});
    checkAblation(Capability::S5Sub, {{.id = 610, .normalized = 0.0}, {.id = 611, .normalized = 0.0}, {.id = 612, .normalized = 0.0}, {.id = 600, .normalized = 0.0}});
    checkAblation(Capability::S6Bloom, {{.id = 1300, .normalized = 0.0}});
    checkAblation(Capability::S7Ecosystem, {{.id = 900, .normalized = 0.0}});
    checkAblation(Capability::S8Cavern, {{.id = 1105, .normalized = 0.0}});
    checkAblation(Capability::S9Ghost, {{.id = 1400, .normalized = 0.0}});
    checkAblation(Capability::S10Body, {{.id = 1003, .normalized = 0.0}});

    // Group M: macro m (1-based) is ID 99 + m, reset to 0.0; Gravity (M5, 104) to 0.5.
    for (std::uint32_t m = 1; m <= 12; ++m) {
        const auto cell = static_cast<Capability>(capIndex(Capability::M1Darkness) + (m - 1));
        checkAblation(cell, {{.id = 99 + m, .normalized = (m == 5) ? 0.5 : 0.0}});
    }

    // Group E.
    for (const Capability c : {Capability::E1PartialBloom, Capability::E2ResonatorPeaks,
                               Capability::E3NoiseWake, Capability::E4FeedbackLoopWake,
                               Capability::E5GhostBursts}) {
        INFO("cell " << capIndex(c));
        REQUIRE(specs[capIndex(c)].verification == Verification::RouteIsolated);
    }
    checkAblation(Capability::E6SyncRateHi, {{.id = 901, .normalized = 0.0}});
    checkAblation(Capability::E7SelfAffinityHi, {{.id = 902, .normalized = 0.25}});
    REQUIRE(specs[capIndex(Capability::E6SyncRateHi)].verification == Verification::ExtReversion);
    REQUIRE(specs[capIndex(Capability::E7SelfAffinityHi)].verification == Verification::ExtReversion);

    // D13 / D14 reversion and depth ablations.
    checkAblation(Capability::D13SlowEvents, {{.id = 800, .normalized = 0.5}});
    checkAblation(Capability::D13FastEvents, {{.id = 800, .normalized = 0.5}});
    checkAblation(Capability::D14Breathing, {{.id = 1500, .normalized = 0.0}});
    checkAblation(Capability::D14Tidal, {{.id = 1502, .normalized = 0.0}});
    for (const Capability c : {Capability::D13SlowEvents, Capability::D13FastEvents,
                               Capability::D14Breathing, Capability::D14Tidal}) {
        INFO("cell " << capIndex(c));
        REQUIRE(specs[capIndex(c)].verification == Verification::StateWithReversion);
    }

    // Every S and M cell is ablation-verified.
    for (std::size_t i = 0; i < 22; ++i) {
        INFO("cell " << i);
        REQUIRE(specs[i].verification == Verification::Ablation);
    }

    // D verification kinds: D1-D7, D11, D12.1 StateWithS; D8/D9 AttackWindow;
    // D10.1 FreezeGesture; D10.2 / D12.2 StateOnly.
    const auto kindOf = [&specs](Capability c) { return specs[capIndex(c)].verification; };
    for (std::size_t i = capIndex(Capability::D1Glass); i <= capIndex(Capability::D7FifthBelow); ++i) {
        INFO("cell " << i);
        REQUIRE(specs[i].verification == Verification::StateWithS);
    }
    REQUIRE(kindOf(Capability::D11GhostReverse) == Verification::StateWithS);
    REQUIRE(kindOf(Capability::D12TriggersOn) == Verification::StateWithS);
    for (const Capability c : {Capability::D8Standard, Capability::D8Growth, Capability::D9FastAttack,
                               Capability::D9SlowAttack}) {
        INFO("cell " << capIndex(c));
        REQUIRE(kindOf(c) == Verification::AttackWindow);
    }
    REQUIRE(kindOf(Capability::D10FreezeHolds) == Verification::FreezeGesture);
    REQUIRE(kindOf(Capability::D10FreezeOff) == Verification::StateOnly);
    REQUIRE(kindOf(Capability::D12TriggersOff) == Verification::StateOnly);

    // sConjunct: D1.* / D2 -> S10, D3.* / D4.* -> S1, D5.* -> S2, D6.* -> S4,
    // D7.* -> S5, D11 / D12.1 -> S9, D13.* -> S7; every other cell (D14.* included) -> Count.
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const auto c = static_cast<Capability>(i);
        Capability expected = Capability::Count;
        if (inCapRange(c, Capability::D1Glass, Capability::D2BlendBoth)) {
            expected = Capability::S10Body;
        } else if (inCapRange(c, Capability::D3Direct, Capability::D4Type12)) {
            expected = Capability::S1Noise;
        } else if (inCapRange(c, Capability::D5Free, Capability::D5Hybrid)) {
            expected = Capability::S2Resonance;
        } else if (inCapRange(c, Capability::D6Lowpass, Capability::D6Highpass)) {
            expected = Capability::S4Ecology;
        } else if (inCapRange(c, Capability::D7Div2, Capability::D7FifthBelow)) {
            expected = Capability::S5Sub;
        } else if (c == Capability::D11GhostReverse || c == Capability::D12TriggersOn) {
            expected = Capability::S9Ghost;
        } else if (inCapRange(c, Capability::D13SlowEvents, Capability::D13FastEvents)) {
            expected = Capability::S7Ecosystem;
        }
        INFO("cell " << i);
        REQUIRE(specs[i].sConjunct == expected);
    }
}

TEST_CASE("Vorago_PresetDefs_ClaimsWellFormed", "[vorago][preset]") {
    const auto& presets = PD::allPresets();

    std::set<std::size_t> primaries;
    for (const PD::VoragoPresetDef& p : presets) {
        INFO("preset " << std::string(p.name));
        REQUIRE(p.primary != Capability::Count);
        REQUIRE(primaries.insert(capIndex(p.primary)).second);  // unique primaries
        REQUIRE(!PD::isRecordedDefaultState(p.primary));
        REQUIRE(p.primary != Capability::D10FreezeHolds);

        std::set<std::size_t> secs;
        for (const Capability s : p.secondaries) {
            REQUIRE(s != Capability::Count);
            REQUIRE(s != p.primary);
            REQUIRE(secs.insert(capIndex(s)).second);  // no duplicate secondary
            if (s == Capability::D10FreezeHolds) {
                REQUIRE(p.primary == Capability::S8Cavern);
            }
        }

        std::set<std::uint32_t> ids;
        for (const PD::ParamSetting& ps : p.params) {
            INFO("param " << ps.id);
            REQUIRE(Krate::DSP::detail::isFinite(ps.normalized));
            REQUIRE(ps.normalized >= 0.0);
            REQUIRE(ps.normalized <= 1.0);
            REQUIRE(ps.id != 4u);
            REQUIRE(ps.id != 5u);
            REQUIRE(ids.insert(ps.id).second);  // no duplicate ID within a def
        }

        REQUIRE(std::ranges::find(PD::kCategories, p.category) != PD::kCategories.end());
        REQUIRE(!containsForbiddenXmlChar(p.description));
    }
}

TEST_CASE("Vorago_PresetDefs_RequiredPrimaries", "[vorago][preset]") {
    const std::vector<Capability> required = PD::requiredPrimaryCells();
    REQUIRE(required.size() == 42u);  // T043 ruling 2026-09-29 (measured default-state set)

    // The MEASURED default-state set (T043): the four noise models are not
    // default-state (the noise bed is inaudible on the default surface), so
    // D3.1-D3.4 need primaries. E6.hi / E7.hi are secondaries (G2 ruling).
    std::set<std::size_t> expected;
    for (std::size_t i = capIndex(Capability::S1Noise); i <= capIndex(Capability::E5GhostBursts); ++i) {
        expected.insert(i);  // S1-S10, M1-M12, E1-E5
    }
    for (const Capability c : {Capability::D1Glass, Capability::D1Strings, Capability::D1MetalPlate,
                               Capability::D1Chamber, Capability::D1Ice, Capability::D1WoodenHull,
                               Capability::D1CathedralColumn, Capability::D1CavernWall,
                               Capability::D1GlassSphere, Capability::D3Direct,
                               Capability::D3FilteredWind, Capability::D3GranularDust,
                               Capability::D3MetallicHiss, Capability::D8Growth,
                               Capability::D9FastAttack}) {
        expected.insert(capIndex(c));
    }
    REQUIRE(expected.size() == 42u);

    std::set<std::size_t> actual;
    for (const Capability c : required) {
        actual.insert(capIndex(c));
    }
    REQUIRE(actual.size() == required.size());  // no duplicates
    REQUIRE(actual == expected);
}

TEST_CASE("Vorago_FactoryPresets_LibraryShape", "[vorago][preset]") {
    // R-7(ii), SC-029, SC-031. The one case allowed to stay red until the
    // library is authored (T047).
    const std::vector<Capability> required = PD::requiredPrimaryCells();
    REQUIRE(required.size() == 42u);  // any other size is the plan 6.12 FR-017 stop (T043: N = 42)

    const auto& presets = PD::allPresets();
    REQUIRE(presets.size() == required.size());

    for (const Capability c : required) {
        const auto n = std::ranges::count_if(
            presets, [c](const PD::VoragoPresetDef& p) { return p.primary == c; });
        INFO("required primary cell " << capIndex(c));
        REQUIRE(n == 1);
    }

    for (const std::string_view cat : PD::kCategories) {
        const auto n = std::ranges::count_if(
            presets, [cat](const PD::VoragoPresetDef& p) { return p.category == cat; });
        INFO("category " << std::string(cat));
        REQUIRE(n >= 3);
    }
}

TEST_CASE("Vorago_PresetDefs_InfoXmlBytes", "[vorago][preset]") {
    // The literal bytes preset_manager.cpp:265-277 writes for these values.
    const std::string expected =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<MetaInfo>\n"
        "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n"
        "  <Attr id=\"PlugInName\" value=\"Vorago\" type=\"string\"/>\n"
        "  <Attr id=\"PlugInCategory\" value=\"Synth\" type=\"string\"/>\n"
        "  <Attr id=\"Name\" value=\"Name\" type=\"string\"/>\n"
        "  <Attr id=\"MusicalCategory\" value=\"Abyss\" type=\"string\"/>\n"
        "  <Attr id=\"MusicalInstrument\" value=\"Abyss\" type=\"string\"/>\n"
        "  <Attr id=\"Comment\" value=\"Desc\" type=\"string\"/>\n"
        "</MetaInfo>\n";
    REQUIRE(PD::buildVoragoInfoXml("Name", "Abyss", "Desc") == expected);

    // An empty description omits the Comment line.
    const std::string expectedNoComment =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<MetaInfo>\n"
        "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n"
        "  <Attr id=\"PlugInName\" value=\"Vorago\" type=\"string\"/>\n"
        "  <Attr id=\"PlugInCategory\" value=\"Synth\" type=\"string\"/>\n"
        "  <Attr id=\"Name\" value=\"Name\" type=\"string\"/>\n"
        "  <Attr id=\"MusicalCategory\" value=\"Abyss\" type=\"string\"/>\n"
        "  <Attr id=\"MusicalInstrument\" value=\"Abyss\" type=\"string\"/>\n"
        "</MetaInfo>\n";
    REQUIRE(PD::buildVoragoInfoXml("Name", "Abyss", "") == expectedNoComment);
}

// ==============================================================================
// T025 - preset_test_support.h C2 rows: Container, Info, Typed decode, Timeline
// (plan 5.8, 6.1; FR-028, FR-031, C-6)
// ==============================================================================

namespace {

/// A fresh, prepared PresetHost's component state (the default surface).
[[nodiscard]] std::vector<std::uint8_t> defaultComponentState() {
    VoragoTest::PresetHost h;
    REQUIRE(h.prepare(48000.0, 512) == Steinberg::kResultOk);
    std::vector<std::uint8_t> comp;
    REQUIRE(h.saveState(comp));
    return comp;
}

[[nodiscard]] bool writeBytes(const std::filesystem::path& p, const std::vector<std::uint8_t>& b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(out);
}

void appendLE32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (std::uint32_t i = 0; i < 4u; ++i) {
        b.push_back(static_cast<std::uint8_t>((v >> (8u * i)) & 0xFFu));
    }
}

void appendLE64(std::vector<std::uint8_t>& b, std::uint64_t v) {
    for (std::uint64_t i = 0; i < 8u; ++i) {
        b.push_back(static_cast<std::uint8_t>((v >> (8u * i)) & 0xFFu));
    }
}

}  // namespace

TEST_CASE("Vorago_PresetSupport_DecodeDefaultSurface", "[vorago][preset]") {
    const std::vector<std::uint8_t> comp = defaultComponentState();

    VoragoTest::DecodedPresetState st;
    REQUIRE(VoragoTest::decodePresetState(std::span<const std::uint8_t>(comp), st));
    REQUIRE(st.version == 3);
    REQUIRE(st.bytesConsumed == ::Vorago::kStateV3Bytes);

    constexpr auto kR = std::memory_order_relaxed;
    const double stageSum = static_cast<double>(st.envelope.stage0TimeMs.load(kR)) +
                            static_cast<double>(st.envelope.stage1TimeMs.load(kR)) +
                            static_cast<double>(st.envelope.stage2TimeMs.load(kR)) +
                            static_cast<double>(st.envelope.stage3TimeMs.load(kR));
    REQUIRE(stageSum == Catch::Approx(155000.0).margin(1e-3));
    REQUIRE(static_cast<double>(st.envelope.releaseMs.load(kR)) ==
            Catch::Approx(45000.0).margin(1e-3));
    REQUIRE(st.envelope.mode.load(kR) ==
            static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Standard));
    REQUIRE(static_cast<double>(st.space.decaySeconds.load(kR)) ==
            Catch::Approx(20.0).margin(1e-6));
    REQUIRE(st.space.freeze.load(kR) == 0);
    REQUIRE(st.global.seedIndex.load(kR) == 0);
    REQUIRE(static_cast<double>(st.ecosystem.syncRate.load(kR)) ==
            Catch::Approx(0.0).margin(1e-9));
    REQUIRE(static_cast<double>(st.ecosystem.selfAffinity.load(kR)) ==
            Catch::Approx(-1.0).margin(1e-9));

    // One byte short: the last loader's read fails.
    const std::span<const std::uint8_t> truncated(comp.data(), comp.size() - 1u);
    VoragoTest::DecodedPresetState st2;
    REQUIRE_FALSE(VoragoTest::decodePresetState(truncated, st2));
}

TEST_CASE("Vorago_PresetSupport_TimelineDefault", "[vorago][preset]") {
    const std::vector<std::uint8_t> comp = defaultComponentState();
    VoragoTest::DecodedPresetState st;
    REQUIRE(VoragoTest::decodePresetState(std::span<const std::uint8_t>(comp), st));

    constexpr double kM = 1e-4;
    const VoragoTest::SweepTimeline tl = VoragoTest::makeTimeline(st, false);
    REQUIRE(tl.A == Catch::Approx(155.0).margin(kM));
    REQUIRE(tl.rel == Catch::Approx(45.0).margin(kM));
    REQUIRE(tl.rt60 == Catch::Approx(20.0).margin(kM));
    REQUIRE(tl.sus0 == Catch::Approx(160.0).margin(kM));
    REQUIRE(tl.sus1 == Catch::Approx(220.0).margin(kM));
    REQUIRE(tl.m[0][0] == Catch::Approx(160.0).margin(kM));
    REQUIRE(tl.m[0][1] == Catch::Approx(220.0).margin(kM));
    REQUIRE(tl.m[1][0] == Catch::Approx(220.0).margin(kM));
    REQUIRE(tl.m[1][1] == Catch::Approx(280.0).margin(kM));
    REQUIRE(tl.m[2][0] == Catch::Approx(280.0).margin(kM));
    REQUIRE(tl.m[2][1] == Catch::Approx(340.0).margin(kM));
    REQUIRE(tl.H == Catch::Approx(340.0).margin(kM));
    REQUIRE(tl.tail0 == Catch::Approx(410.0).margin(kM));  // H + Rel + RT60 + 5
    REQUIRE(tl.tail1 == Catch::Approx(420.0).margin(kM));  // H + Rel + RT60 + 15
    REQUIRE(tl.total == Catch::Approx(420.0).margin(kM));
    REQUIRE_FALSE(tl.freezeOnTail);

    const VoragoTest::SweepTimeline fz = VoragoTest::makeTimeline(st, true);
    REQUIRE(fz.tail0 == Catch::Approx(395.0).margin(kM));  // H + Rel + 10
    REQUIRE(fz.tail1 == Catch::Approx(455.0).margin(kM));  // H + Rel + 70
    REQUIRE(fz.total == Catch::Approx(455.0).margin(kM));
    REQUIRE(fz.freezeOnTail);

    // Growth mode: A is the growth duration.
    st.envelope.mode.store(static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Growth),
                           std::memory_order_relaxed);
    st.envelope.growthDurationSeconds.store(30.0f, std::memory_order_relaxed);
    const VoragoTest::SweepTimeline gr = VoragoTest::makeTimeline(st, false);
    REQUIRE(gr.A == Catch::Approx(30.0).margin(kM));
}

TEST_CASE("Vorago_PresetSupport_ParseVstPresetRejectsGarbage", "[vorago][preset]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "vorago_t025_parse_garbage";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    // A 10-byte file.
    const std::filesystem::path shortFile = dir / "short.vstpreset";
    REQUIRE(writeBytes(shortFile, std::vector<std::uint8_t>(10u, static_cast<std::uint8_t>(0x41u))));
    const VoragoTest::PresetFile a = VoragoTest::parseVstPreset(shortFile);
    REQUIRE_FALSE(a.ok);
    REQUIRE_FALSE(a.why.empty());

    // A correct 48-byte header whose list offset lies past EOF.
    std::vector<std::uint8_t> hdr{'V', 'S', 'T', '3'};
    appendLE32(hdr, 1u);
    for (int i = 0; i < 32; ++i) {
        hdr.push_back(static_cast<std::uint8_t>('0' + (i % 10)));
    }
    appendLE64(hdr, 4096u);
    REQUIRE(hdr.size() == 48u);
    const std::filesystem::path pastEof = dir / "past_eof.vstpreset";
    REQUIRE(writeBytes(pastEof, hdr));
    const VoragoTest::PresetFile b = VoragoTest::parseVstPreset(pastEof);
    REQUIRE_FALSE(b.ok);
    REQUIRE_FALSE(b.why.empty());

    std::filesystem::remove_all(dir, ec);
}

// ==============================================================================
// T026 - preset_test_support.h C2 rows: Descriptor overload, Outcomes, Vector,
// state predicates (plan 5.8, 6.4, 6.7, 6.10, 6.12; C-7.2, C-7.4, FR-011a, FR-075)
// ==============================================================================

namespace {

/// Component-for-component equality with `==` (the same binary computes both
/// sides; no stored digest, C-8).
void requireSameDescriptor(const VoragoTest::PresetDescriptor& a,
                           const VoragoTest::PresetDescriptor& b) {
    for (std::size_t k = 0; k < VoragoTest::kDescriptorBands; ++k) {
        INFO("band " << k);
        REQUIRE(a.band[k] == b.band[k]);
    }
    REQUIRE(a.motion == b.motion);
    REQUIRE(a.flux == b.flux);
    REQUIRE(a.corr == b.corr);
    REQUIRE(a.energySpread == b.energySpread);
    REQUIRE(a.crest == b.crest);
}

/// A render-scored entry with every conjunct true.
[[nodiscard]] VoragoTest::CellOutcome scoredOutcome(double d, double twoS) {
    VoragoTest::CellOutcome o;
    o.stateOk = true;
    o.conjunctOk = true;
    o.rendered = true;
    o.d = d;
    o.twoS = twoS;
    o.attribBase = -1.0;
    return o;
}

/// A state-only entry as the vector records it (plan 6.12).
[[nodiscard]] VoragoTest::CellOutcome stateOnlyOutcome(bool stateOk, bool conjunctOk) {
    VoragoTest::CellOutcome o;
    o.stateOk = stateOk;
    o.conjunctOk = conjunctOk;
    o.rendered = false;
    o.d = 0.0;
    o.twoS = 0.0;
    o.attribBase = -1.0;
    o.skip = "state-only kind";
    return o;
}

/// A definition whose only claim is `c` (its primary).
[[nodiscard]] PD::VoragoPresetDef onlyClaim(Capability c) {
    return PD::VoragoPresetDef{.name = "P",
                               .category = "Drones",
                               .description = "",
                               .primary = c,
                               .secondaries = {},
                               .params = {}};
}

}  // namespace

TEST_CASE("Vorago_PresetDescriptor_RefactorIsIdentity", "[vorago][preset]") {
    constexpr double kSr = 48000.0;

    // The specified 48 000-sample signal: describe == describeImpl(nullopt) ==
    // describeWithEnergyFloor(-1000), component for component.
    std::vector<float> L;
    std::vector<float> R;
    VoragoTest::makeDescriptorTestSignal(48000u, kSr, L, R);
    const VoragoTest::PresetDescriptor base = VoragoTest::describe(L, R, kSr);
    requireSameDescriptor(base, VoragoTest::detail::describeImpl(L, R, kSr, std::nullopt));
    requireSameDescriptor(base, VoragoTest::describeWithEnergyFloor(L, R, kSr, -1000.0));

    // One second is ONE block, whose spread is 0 whatever the floor; the floor
    // is therefore exercised on a three-second render of the same signal, where
    // the ramp gives a non-zero spread.
    std::vector<float> L3;
    std::vector<float> R3;
    VoragoTest::makeDescriptorTestSignal(static_cast<std::size_t>(3u * 48000u), kSr, L3, R3);
    const VoragoTest::PresetDescriptor three = VoragoTest::describe(L3, R3, kSr);
    REQUIRE(three.energySpread > 0.0);
    requireSameDescriptor(three, VoragoTest::detail::describeImpl(L3, R3, kSr, std::nullopt));
    requireSameDescriptor(three, VoragoTest::describeWithEnergyFloor(L3, R3, kSr, -1000.0));

    // A floor above every one-second value (the signal peaks below 0 dBFS).
    const VoragoTest::PresetDescriptor floored =
        VoragoTest::describeWithEnergyFloor(L3, R3, kSr, 10.0);
    REQUIRE(floored.energySpread == 0.0);
    // Only the spread reads the floor.
    for (std::size_t k = 0; k < VoragoTest::kDescriptorBands; ++k) {
        REQUIRE(floored.band[k] == three.band[k]);
    }
    REQUIRE(floored.motion == three.motion);
    REQUIRE(floored.flux == three.flux);
    REQUIRE(floored.corr == three.corr);
    REQUIRE(floored.crest == three.crest);
}

TEST_CASE("Vorago_PresetMatrix_NonSubsetRule", "[vorago][preset]") {
    using VoragoTest::ClaimRole;
    using VoragoTest::findWitness;
    using VoragoTest::VerificationVector;

    constexpr Capability kX = Capability::S3Smear;
    constexpr Capability kQPrimary = Capability::S1Noise;  // Q's own primary is not X

    SECTION("render-scored claim X") {
        const PD::VoragoPresetDef p = onlyClaim(kX);
        VerificationVector q;

        q.cells[capIndex(kX)] = scoredOutcome(2.5, 1.0);  // 1.5 < d < 4.0, 2s <= d
        REQUIRE_FALSE(findWitness(p, q, kQPrimary).has_value());  // SUBSET

        q.cells[capIndex(kX)] = scoredOutcome(1.0, 0.5);  // d below the secondary bar
        REQUIRE(findWitness(p, q, kQPrimary) == std::optional<Capability>(kX));

        q.cells[capIndex(kX)] = scoredOutcome(2.5, 3.0);  // 2 s(Q) > d: recorded, NOT gated (G2 ruling
        REQUIRE_FALSE(findWitness(p, q, kQPrimary).has_value());  // 2026-09-29) - still SUBSET

        // Attributability: d < attribBase + 1.5 fails.
        VoragoTest::CellOutcome attrib = scoredOutcome(2.5, 1.0);
        attrib.attribBase = 2.0;
        q.cells[capIndex(kX)] = attrib;
        REQUIRE(findWitness(p, q, kQPrimary) == std::optional<Capability>(kX));

        // X is Q's own primary: it must also hold at F = 4.0.
        q.cells[capIndex(kX)] = scoredOutcome(2.5, 1.0);
        REQUIRE(findWitness(p, q, kX) == std::optional<Capability>(kX));
        q.cells[capIndex(kX)] = scoredOutcome(4.5, 1.0);
        REQUIRE_FALSE(findWitness(p, q, kX).has_value());
    }

    SECTION("StateWithS D3.2 has no d term") {
        const PD::VoragoPresetDef p = onlyClaim(Capability::D3FilteredWind);
        VerificationVector q;
        q.cells[capIndex(Capability::S1Noise)] = scoredOutcome(2.5, 1.0);  // Q's S1 verifies

        q.cells[capIndex(Capability::D3FilteredWind)] = stateOnlyOutcome(true, true);
        REQUIRE_FALSE(findWitness(p, q, Capability::S2Resonance).has_value());  // SUBSET

        q.cells[capIndex(Capability::D3FilteredWind)] = stateOnlyOutcome(true, false);  // S1 false
        REQUIRE(findWitness(p, q, Capability::S2Resonance) ==
                std::optional<Capability>(Capability::D3FilteredWind));

        q.cells[capIndex(Capability::D3FilteredWind)] = stateOnlyOutcome(false, true);  // state false
        REQUIRE(findWitness(p, q, Capability::S2Resonance) ==
                std::optional<Capability>(Capability::D3FilteredWind));
    }

    SECTION("StateOnly D12.2") {
        const PD::VoragoPresetDef p = onlyClaim(Capability::D12TriggersOff);
        VerificationVector q;
        q.cells[capIndex(Capability::D12TriggersOff)] = stateOnlyOutcome(true, false);
        REQUIRE_FALSE(findWitness(p, q, Capability::S1Noise).has_value());
        q.cells[capIndex(Capability::D12TriggersOff)] = stateOnlyOutcome(false, false);
        REQUIRE(findWitness(p, q, Capability::S1Noise) ==
                std::optional<Capability>(Capability::D12TriggersOff));
    }

    SECTION("AttackWindow secondary D9.2 without a render") {
        const PD::VoragoPresetDef p = onlyClaim(Capability::D9SlowAttack);
        VerificationVector q;
        q.cells[capIndex(Capability::D9SlowAttack)] = stateOnlyOutcome(true, true);
        REQUIRE_FALSE(findWitness(p, q, Capability::S1Noise).has_value());
    }

    SECTION("FreezeGesture D10.1 in a non-S8 preset") {
        const PD::VoragoPresetDef p = onlyClaim(Capability::D10FreezeHolds);
        VerificationVector q;
        q.cells[capIndex(Capability::D10FreezeHolds)] = stateOnlyOutcome(false, true);
        REQUIRE(findWitness(p, q, Capability::S1Noise) ==
                std::optional<Capability>(Capability::D10FreezeHolds));
    }

    SECTION("secondaries are scanned in definition order after the primary") {
        PD::VoragoPresetDef p = onlyClaim(kX);
        p.secondaries = {Capability::D12TriggersOff, Capability::D10FreezeOff};
        VerificationVector q;
        q.cells[capIndex(kX)] = scoredOutcome(2.5, 1.0);
        q.cells[capIndex(Capability::D12TriggersOff)] = stateOnlyOutcome(false, false);
        q.cells[capIndex(Capability::D10FreezeOff)] = stateOnlyOutcome(false, false);
        REQUIRE(findWitness(p, q, kQPrimary) ==
                std::optional<Capability>(Capability::D12TriggersOff));
    }

    SECTION("verifiedAt at Primary on StateWithS") {
        VoragoTest::CellOutcome d1 = scoredOutcome(3.9, 1.0);  // state and S10 true
        REQUIRE_FALSE(VoragoTest::verifiedAt(d1, Capability::D1Glass, ClaimRole::Primary));
        d1.d = 4.1;
        d1.twoS = 4.1;  // twoS <= 4.1
        REQUIRE(VoragoTest::verifiedAt(d1, Capability::D1Glass, ClaimRole::Primary));
        REQUIRE(VoragoTest::verifiedAt(d1, PD::Verification::StateWithS, ClaimRole::Primary));

        // D3 primary rule (ruling 2026-09-30): a noise-model cell at Primary reads
        // its S1 conjunct's render terms (copied in by pass 3) against F.
        VoragoTest::CellOutcome d3 = scoredOutcome(100.0, 0.0);
        REQUIRE(VoragoTest::verifiedAt(d3, Capability::D3FilteredWind, ClaimRole::Primary));
        d3.d = 3.9;
        REQUIRE_FALSE(VoragoTest::verifiedAt(d3, Capability::D3FilteredWind, ClaimRole::Primary));
        d3.d = 0.0;
        REQUIRE_FALSE(VoragoTest::verifiedAt(d3, Capability::D3FilteredWind, ClaimRole::Primary));
        // ... while its secondary verdict never reads d.
        REQUIRE(VoragoTest::verifiedAt(d3, Capability::D3FilteredWind, ClaimRole::Secondary));
        // Every other non-D1 StateWithS cell stays false at Primary.
        VoragoTest::CellOutcome d5 = scoredOutcome(100.0, 0.0);
        REQUIRE_FALSE(VoragoTest::verifiedAt(d5, Capability::D5Free, ClaimRole::Primary));
    }
}

TEST_CASE("Vorago_PresetDefs_EExtSidePredicate", "[vorago][preset]") {
    const auto sideAfter = [](Steinberg::Vst::ParamID id, double normalized, Capability c) {
        VoragoTest::DecodedPresetState st;
        ::Vorago::handleEcosystemParamChange(st.ecosystem, id, normalized);
        return VoragoTest::extSidePredicate(c, st) && VoragoTest::statePredicate(c, st);
    };

    // Plain values the shipped handler maps them to.
    {
        VoragoTest::DecodedPresetState st;
        ::Vorago::handleEcosystemParamChange(st.ecosystem, ::Vorago::kEcosystemSyncRateId, 0.5);
        ::Vorago::handleEcosystemParamChange(st.ecosystem, ::Vorago::kEcosystemSelfAffinityId,
                                             0.625);
        REQUIRE(st.ecosystem.syncRate.load(std::memory_order_relaxed) == 0.25f);
        REQUIRE(st.ecosystem.selfAffinity.load(std::memory_order_relaxed) == 0.5f);
    }

    REQUIRE(sideAfter(::Vorago::kEcosystemSyncRateId, 0.5, Capability::E6SyncRateHi));
    REQUIRE_FALSE(sideAfter(::Vorago::kEcosystemSyncRateId, 0.49, Capability::E6SyncRateHi));
    REQUIRE(sideAfter(::Vorago::kEcosystemSelfAffinityId, 0.625, Capability::E7SelfAffinityHi));
    REQUIRE_FALSE(sideAfter(::Vorago::kEcosystemSelfAffinityId, 0.62, Capability::E7SelfAffinityHi));

    // Defaults (normalized 0.0 / 0.25) fail both.
    VoragoTest::DecodedPresetState dflt;
    REQUIRE_FALSE(VoragoTest::extSidePredicate(Capability::E6SyncRateHi, dflt));
    REQUIRE_FALSE(VoragoTest::extSidePredicate(Capability::E7SelfAffinityHi, dflt));
    REQUIRE_FALSE(VoragoTest::statePredicate(Capability::E6SyncRateHi, dflt));
    REQUIRE_FALSE(VoragoTest::statePredicate(Capability::E7SelfAffinityHi, dflt));
}

TEST_CASE("Vorago_PresetDefs_DStatePredicates", "[vorago][preset]") {
    using VoragoTest::statePredicate;
    constexpr auto kR = std::memory_order_relaxed;

    SECTION("D1 material and D2 blend") {
        VoragoTest::DecodedPresetState st;
        st.body.materialA.store(0, kR);  // Glass
        st.body.materialB.store(4, kR);  // Ice

        st.body.blend.store(0.65f, kR);
        REQUIRE(statePredicate(Capability::D1Glass, st));
        st.body.blend.store(0.66f, kR);
        REQUIRE_FALSE(statePredicate(Capability::D1Glass, st));

        st.body.blend.store(0.35f, kR);
        REQUIRE(statePredicate(Capability::D1Ice, st));
        st.body.blend.store(0.34f, kR);
        REQUIRE_FALSE(statePredicate(Capability::D1Ice, st));
        REQUIRE_FALSE(statePredicate(Capability::D1Strings, st));  // neither slot holds it

        st.body.blend.store(0.35f, kR);
        REQUIRE(statePredicate(Capability::D2BlendBoth, st));
        st.body.blend.store(0.65f, kR);
        REQUIRE(statePredicate(Capability::D2BlendBoth, st));
        st.body.blend.store(0.66f, kR);
        REQUIRE_FALSE(statePredicate(Capability::D2BlendBoth, st));
    }

    SECTION("D4 needs a Direct slot of that type") {
        constexpr int kDirect = static_cast<int>(Krate::DSP::NoiseOrganismModel::Direct);
        constexpr int kWind = static_cast<int>(Krate::DSP::NoiseOrganismModel::FilteredWind);
        for (int t = 1; t <= 12; ++t) {
            INFO("D4." << t);
            const auto cell =
                static_cast<Capability>(capIndex(Capability::D4Type1) + static_cast<std::size_t>(t - 1));
            VoragoTest::DecodedPresetState st;
            for (std::size_t s = 0; s < st.noise.model.size(); ++s) {
                st.noise.model[s].store(kWind, kR);  // no Direct slot anywhere
                st.noise.type[s].store(t - 1, kR);
            }
            REQUIRE_FALSE(statePredicate(cell, st));  // non-Direct slots with that type
            st.noise.model[1].store(kDirect, kR);
            REQUIRE(statePredicate(cell, st));
        }
    }

    SECTION("D7 strictly loudest, ties fail") {
        VoragoTest::DecodedPresetState st;
        st.sub.div2LevelDb.store(-18.0f, kR);
        st.sub.div4LevelDb.store(-24.0f, kR);
        st.sub.fifthBelowLevelDb.store(-30.0f, kR);
        REQUIRE(statePredicate(Capability::D7Div2, st));
        REQUIRE_FALSE(statePredicate(Capability::D7Div4, st));
        REQUIRE_FALSE(statePredicate(Capability::D7FifthBelow, st));

        st.sub.div4LevelDb.store(-18.0f, kR);
        REQUIRE_FALSE(statePredicate(Capability::D7Div2, st));
        REQUIRE_FALSE(statePredicate(Capability::D7Div4, st));
        REQUIRE_FALSE(statePredicate(Capability::D7FifthBelow, st));
    }

    SECTION("D9 attack span") {
        VoragoTest::DecodedPresetState st;
        st.envelope.mode.store(static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Growth), kR);
        st.envelope.growthDurationSeconds.store(10.0f, kR);
        REQUIRE(statePredicate(Capability::D9FastAttack, st));
        REQUIRE_FALSE(statePredicate(Capability::D9SlowAttack, st));
        st.envelope.growthDurationSeconds.store(10.01f, kR);
        REQUIRE_FALSE(statePredicate(Capability::D9FastAttack, st));
        st.envelope.growthDurationSeconds.store(90.0f, kR);
        REQUIRE(statePredicate(Capability::D9SlowAttack, st));
        REQUIRE_FALSE(statePredicate(Capability::D9FastAttack, st));
        REQUIRE(statePredicate(Capability::D8Growth, st));
        REQUIRE_FALSE(statePredicate(Capability::D8Standard, st));
    }

    SECTION("D13 event rate, D14 breathing") {
        VoragoTest::DecodedPresetState st;
        st.events.eventRateScale.store(0.3f, kR);
        REQUIRE(statePredicate(Capability::D13SlowEvents, st));
        REQUIRE_FALSE(statePredicate(Capability::D13FastEvents, st));
        st.events.eventRateScale.store(3.0f, kR);
        REQUIRE(statePredicate(Capability::D13FastEvents, st));
        REQUIRE_FALSE(statePredicate(Capability::D13SlowEvents, st));

        st.life.breathingDepth.store(0.7f, kR);
        REQUIRE(statePredicate(Capability::D14Breathing, st));
        st.life.breathingDepth.store(0.69f, kR);
        REQUIRE_FALSE(statePredicate(Capability::D14Breathing, st));
    }

    SECTION("M displacement") {
        for (int m = 0; m < 12; ++m) {
            const auto cell =
                static_cast<Capability>(capIndex(Capability::M1Darkness) + static_cast<std::size_t>(m));
            if (cell == Capability::M5Gravity) {
                continue;
            }
            INFO("macro index " << m);
            VoragoTest::DecodedPresetState st;
            REQUIRE_FALSE(VoragoTest::macroDisplaced(cell, st));  // registered default 0
            ::Vorago::macroField(st.macros, m).store(0.5f, kR);
            REQUIRE(VoragoTest::macroDisplaced(cell, st));
            REQUIRE(statePredicate(cell, st));
            ::Vorago::macroField(st.macros, m).store(0.49f, kR);
            REQUIRE_FALSE(VoragoTest::macroDisplaced(cell, st));
        }

        VoragoTest::DecodedPresetState st;
        REQUIRE_FALSE(VoragoTest::macroDisplaced(Capability::M5Gravity, st));  // default 0.5
        st.macros.gravity.store(0.85f, kR);
        REQUIRE(VoragoTest::macroDisplaced(Capability::M5Gravity, st));
        st.macros.gravity.store(0.15f, kR);
        REQUIRE(VoragoTest::macroDisplaced(Capability::M5Gravity, st));
        st.macros.gravity.store(0.84f, kR);
        REQUIRE_FALSE(VoragoTest::macroDisplaced(Capability::M5Gravity, st));
    }
}

// ------------------------------------------------------------------------------
// T027 (FR-021, C-4): buildPresetComponentState validates a definition's points,
// then drives them through ONE process(512) block on a fresh PresetHost and
// returns the resulting component state.
// ------------------------------------------------------------------------------
TEST_CASE("Vorago_PresetHost_BuildPresetComponentState", "[vorago][preset]") {
    using Vorago::PresetDefs::Capability;
    using Vorago::PresetDefs::ParamSetting;
    using Vorago::PresetDefs::VoragoPresetDef;
    constexpr auto kR = std::memory_order_relaxed;

    const auto makeDef = [](std::vector<ParamSetting> params) {
        return VoragoPresetDef{.name = "BuildTest",
                               .category = "Drones",
                               .description = "",
                               .primary = Capability::S1Noise,
                               .secondaries = {},
                               .params = std::move(params)};
    };

    SECTION("valid points reach the component state") {
        const VoragoPresetDef def =
            makeDef({{.id = ::Vorago::kMasterGainId, .normalized = 0.25}, {.id = ::Vorago::kEcosystemSyncRateId, .normalized = 1.0}});
        std::vector<std::uint8_t> comp;
        std::string why;
        REQUIRE(VoragoTest::buildPresetComponentState(def, comp, why));
        INFO(why);
        REQUIRE(comp.size() == 436u);

        VoragoTest::DecodedPresetState st;
        REQUIRE(VoragoTest::decodePresetState(std::span<const std::uint8_t>(comp), st));
        REQUIRE(static_cast<double>(st.ecosystem.syncRate.load(kR)) ==
                Catch::Approx(0.5).margin(1e-9));
        // global_params.h:65-67: normalized -> linear gain = value * 2 (clamped [0, 2]).
        REQUIRE(static_cast<double>(st.global.masterGain.load(kR)) ==
                Catch::Approx(0.5).margin(1e-9));
    }

    SECTION("invalid definitions are rejected with a reason") {
        const double nan = std::bit_cast<double>(std::uint64_t{0x7FF8000000000000ull});
        const std::vector<std::pair<std::string_view, std::vector<ParamSetting>>> bad{
            {"above one", {{.id = ::Vorago::kMasterGainId, .normalized = 1.0000001}}},
            {"below zero", {{.id = ::Vorago::kMasterGainId, .normalized = -0.1}}},
            {"NaN", {{.id = ::Vorago::kMasterGainId, .normalized = nan}}},
            {"sustain pedal (4)", {{.id = ::Vorago::kSustainPedalId, .normalized = 0.5}}},
            {"channel pressure (5)", {{.id = ::Vorago::kChannelPressureId, .normalized = 0.5}}},
            {"duplicate ID",
             {{.id = ::Vorago::kMasterGainId, .normalized = 0.25}, {.id = ::Vorago::kMasterGainId, .normalized = 0.30}}},
        };
        for (const auto& [label, params] : bad) {
            INFO(label);
            std::vector<std::uint8_t> comp;
            std::string why;
            REQUIRE_FALSE(VoragoTest::buildPresetComponentState(makeDef(params), comp, why));
            REQUIRE_FALSE(why.empty());
        }
    }

    SECTION("the empty definition equals a fresh host's state") {
        std::vector<std::uint8_t> comp;
        std::string why;
        REQUIRE(VoragoTest::buildPresetComponentState(makeDef({}), comp, why));
        INFO(why);
        REQUIRE(comp == defaultComponentState());
    }
}

// ==============================================================================
// T029 - per-push harness over allPresets() and the committed tree
// (FR-002..FR-009, FR-014, FR-028..FR-032, FR-035; SC-002..SC-006, SC-009).
// Every case holds on 0 presets and bites from T037 on.
// ==============================================================================

namespace {

namespace fs = std::filesystem;

/// SC-006 / plan 6.15: the committed-tree float tolerance, |c - r| / max(|c|, 1e-30).
/// Initially the one-ULP floor; re-pinned by T048 to
/// max(10 x worst over MSVC / GCC / AppleClang, 1.19e-7). Never widened silently.
constexpr double kTreeFloatRelTol = 1.19e-7;

/// C-7.1 / plan 6.14.
constexpr double kContinuousDistinctDelta = 0.10;
constexpr std::size_t kMinDifferingIds = 8;

/// Every `.vstpreset` under the committed tree, sorted (the on-disk set).
[[nodiscard]] std::vector<fs::path> factoryPresetFiles() {
    std::vector<fs::path> out;
    std::error_code ec;
    const fs::path root = factoryPresetRoot();
    if (!fs::is_directory(root, ec)) {
        return out;
    }
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, ec)) {
        std::error_code typeEc;
        if (entry.is_regular_file(typeEc) && entry.path().extension() == ".vstpreset") {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

[[nodiscard]] std::string categoryOf(const fs::path& file) {
    return file.parent_path().filename().string();
}

[[nodiscard]] std::string stemOf(const fs::path& file) {
    return file.stem().string();
}

[[nodiscard]] std::string presetKey(std::string_view category, std::string_view name) {
    return std::string(category) + "/" + std::string(name);
}

[[nodiscard]] fs::path defPath(const PD::VoragoPresetDef& d) {
    return factoryPresetRoot() / std::string(d.category) / (std::string(d.name) + ".vstpreset");
}

/// The definition a committed file belongs to (category + stem); null = none.
[[nodiscard]] const PD::VoragoPresetDef* findDef(std::string_view category, std::string_view name) {
    for (const PD::VoragoPresetDef& d : PD::allPresets()) {
        if (d.category == category && d.name == name) {
            return &d;
        }
    }
    return nullptr;
}

/// kProcessorUID's 32 hex characters (FUID::toString writes 32 + a terminator).
[[nodiscard]] std::string processorClassId() {
    std::array<Steinberg::char8, 33> buf{};
    ::Vorago::kProcessorUID.toString(buf.data());
    return {buf.data()};
}

// ---- The v3 stream layout: which 4-byte slot is an int32, which a float --------
// processor.cpp getState() order; each pack's shape is its save*Params write
// sequence (plugins/vorago/src/parameters/*_params.h) and the per-pack sums of
// plugin_ids.h:30-54.

enum class StreamFieldKind : std::uint8_t { Int32, Float };

struct StreamFieldRun {
    StreamFieldKind kind;
    std::size_t count;
    std::string_view label;
};

constexpr std::array<StreamFieldRun, 29> kStreamLayout{{
    {.kind = StreamFieldKind::Int32, .count = 1, .label = "version"},
    {.kind = StreamFieldKind::Float, .count = 1, .label = "global.masterGain"},
    {.kind = StreamFieldKind::Int32, .count = 1, .label = "global.polyphony"},
    {.kind = StreamFieldKind::Float, .count = 12, .label = "macros"},
    {.kind = StreamFieldKind::Int32, .count = 1, .label = "global.seedIndex"},
    {.kind = StreamFieldKind::Float, .count = 1, .label = "global.outputSaturation"},
    {.kind = StreamFieldKind::Float, .count = 7, .label = "cloud"},
    {.kind = StreamFieldKind::Float, .count = 3, .label = "noise.levelWakeWander"},
    {.kind = StreamFieldKind::Int32, .count = 8, .label = "noise.modelType"},
    {.kind = StreamFieldKind::Float, .count = 12, .label = "noise.comb"},
    {.kind = StreamFieldKind::Float, .count = 3, .label = "resonance.float"},
    {.kind = StreamFieldKind::Int32, .count = 1, .label = "resonance.int"},
    {.kind = StreamFieldKind::Float, .count = 2, .label = "ecology.float"},
    {.kind = StreamFieldKind::Int32, .count = 6, .label = "ecology.int"},
    {.kind = StreamFieldKind::Float, .count = 5, .label = "sub"},
    {.kind = StreamFieldKind::Float, .count = 3, .label = "smear"},
    {.kind = StreamFieldKind::Float, .count = 1, .label = "events.eventRateScale"},
    {.kind = StreamFieldKind::Float, .count = 1, .label = "ecosystem.depth"},
    {.kind = StreamFieldKind::Float, .count = 4, .label = "body.float"},
    {.kind = StreamFieldKind::Int32, .count = 2, .label = "body.int"},
    {.kind = StreamFieldKind::Float, .count = 15, .label = "space.float"},
    {.kind = StreamFieldKind::Int32, .count = 1, .label = "space.freeze"},
    {.kind = StreamFieldKind::Int32, .count = 1, .label = "envelope.mode"},
    {.kind = StreamFieldKind::Float, .count = 6, .label = "envelope.float"},
    {.kind = StreamFieldKind::Float, .count = 2, .label = "bloom"},
    {.kind = StreamFieldKind::Float, .count = 3, .label = "ghost.float"},
    {.kind = StreamFieldKind::Int32, .count = 1, .label = "ghost.triggers"},
    {.kind = StreamFieldKind::Float, .count = 3, .label = "life"},
    {.kind = StreamFieldKind::Float, .count = 2, .label = "ecosystem.v3"},
}};

[[nodiscard]] constexpr std::size_t streamLayoutBytes() {
    std::size_t n = 0;
    for (const StreamFieldRun& r : kStreamLayout) {
        n += 4u * r.count;
    }
    return n;
}
static_assert(streamLayoutBytes() == ::Vorago::kStateV3Bytes,
              "the slot table must cover exactly the v3 stream");

struct StreamField {
    StreamFieldKind kind;
    std::string label;
};

[[nodiscard]] std::vector<StreamField> streamFields() {
    std::vector<StreamField> out;
    for (const StreamFieldRun& r : kStreamLayout) {
        for (std::size_t k = 0; k < r.count; ++k) {
            std::string label(r.label);
            if (r.count > 1u) {
                label += "[" + std::to_string(k) + "]";
            }
            out.push_back(StreamField{.kind = r.kind, .label = std::move(label)});
        }
    }
    return out;
}

/// The slot of the k-th field of the run labelled `runLabel` (the run must exist
/// and hold more than k fields).
[[nodiscard]] std::size_t slotOf(std::string_view runLabel, std::size_t k) {
    std::size_t slot = 0;
    for (const StreamFieldRun& r : kStreamLayout) {
        if (r.label == runLabel) {
            REQUIRE(k < r.count);
            return slot + k;
        }
        slot += r.count;
    }
    INFO("no stream run labelled " << std::string(runLabel));
    FAIL();
    return 0;
}

/// Little-endian 4-byte slot (the caller has checked the stream length).
[[nodiscard]] std::uint32_t slotBits(const std::vector<std::uint8_t>& b, std::size_t slot) {
    const std::size_t at = 4u * slot;
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1u]) << 8u) |
           (static_cast<std::uint32_t>(b[at + 2u]) << 16u) |
           (static_cast<std::uint32_t>(b[at + 3u]) << 24u);
}

[[nodiscard]] float slotFloat(const std::vector<std::uint8_t>& b, std::size_t slot) {
    return std::bit_cast<float>(slotBits(b, slot));
}

[[nodiscard]] std::int32_t slotInt(const std::vector<std::uint8_t>& b, std::size_t slot) {
    return std::bit_cast<std::int32_t>(slotBits(b, slot));
}

/// |c - r| / max(|c|, 1e-30), 0 when bit-identical. Both sides finite (checked).
[[nodiscard]] double relativeDifference(float c, float r) {
    if (std::bit_cast<std::uint32_t>(c) == std::bit_cast<std::uint32_t>(r)) {
        return 0.0;
    }
    const auto cd = static_cast<double>(c);
    const auto rd = static_cast<double>(r);
    return std::fabs(cd - rd) / std::max(std::fabs(cd), 1e-30);
}

// ---- Committed tree vs in-process regeneration (C-9, plan 6.15) ---------------

struct TreeComparison {
    std::vector<std::string> failures;  ///< path / Info / version / length / int / non-finite
    std::vector<double> worst;          ///< per stream slot (float slots only)
    std::vector<std::string> worstAt;   ///< the preset holding that worst
    std::size_t compared = 0;
};

[[nodiscard]] TreeComparison compareTreeWithGenerator(const std::vector<StreamField>& fields) {
    TreeComparison t;
    t.worst.assign(fields.size(), 0.0);
    t.worstAt.assign(fields.size(), std::string{});

    // Same path set, both ways.
    std::set<std::string> onDisk;
    for (const fs::path& f : factoryPresetFiles()) {
        onDisk.insert(presetKey(categoryOf(f), stemOf(f)));
    }
    std::set<std::string> defined;
    for (const PD::VoragoPresetDef& d : PD::allPresets()) {
        defined.insert(presetKey(d.category, d.name));
    }
    for (const std::string& k : onDisk) {
        if (!defined.contains(k)) {
            t.failures.push_back(k + ": committed but not defined");
        }
    }
    for (const std::string& k : defined) {
        if (!onDisk.contains(k)) {
            t.failures.push_back(k + ": defined but not committed");
        }
    }

    for (const PD::VoragoPresetDef& d : PD::allPresets()) {
        const std::string key = presetKey(d.category, d.name);
        const VoragoTest::PresetFile pf = VoragoTest::parseVstPreset(defPath(d));
        if (!pf.ok) {
            t.failures.push_back(key + ": " + pf.why);
            continue;
        }
        if (pf.info != PD::buildVoragoInfoXml(d.name, d.category, d.description)) {
            t.failures.push_back(key + ": Info bytes differ from buildVoragoInfoXml");
        }
        std::vector<std::uint8_t> regen;
        std::string why;
        if (!VoragoTest::buildPresetComponentState(d, regen, why)) {
            std::string msg = key;
            msg += ": regeneration failed: ";
            msg += why;
            t.failures.push_back(std::move(msg));
            continue;
        }
        if (pf.comp.size() != ::Vorago::kStateV3Bytes || regen.size() != ::Vorago::kStateV3Bytes) {
            t.failures.push_back(key + ": length committed " + std::to_string(pf.comp.size()) +
                                 ", regenerated " + std::to_string(regen.size()) + ", expected 436");
            continue;
        }
        if (slotInt(pf.comp, 0) != 3 || slotInt(regen, 0) != 3) {
            t.failures.push_back(key + ": version committed " + std::to_string(slotInt(pf.comp, 0)) +
                                 ", regenerated " + std::to_string(slotInt(regen, 0)) +
                                 ", expected 3");
            continue;
        }
        ++t.compared;
        for (std::size_t s = 0; s < fields.size(); ++s) {
            if (fields[s].kind == StreamFieldKind::Int32) {
                if (slotInt(pf.comp, s) != slotInt(regen, s)) {
                    t.failures.push_back(key + ": int field " + fields[s].label + " committed " +
                                         std::to_string(slotInt(pf.comp, s)) + ", regenerated " +
                                         std::to_string(slotInt(regen, s)));
                }
                continue;
            }
            const float c = slotFloat(pf.comp, s);
            const float r = slotFloat(regen, s);
            if (!Krate::DSP::detail::isFinite(c) || !Krate::DSP::detail::isFinite(r)) {
                t.failures.push_back(key + ": float field " + fields[s].label + " is non-finite");
                continue;
            }
            const double rel = relativeDifference(c, r);
            if (rel > t.worst[s]) {
                t.worst[s] = rel;
                t.worstAt[s] = key;
            }
        }
    }
    return t;
}

[[nodiscard]] std::string treeWorstReport(const TreeComparison& t,
                                          const std::vector<StreamField>& fields) {
    std::ostringstream os;
    os.precision(9);
    os << "tree vs generator over " << t.compared
       << " preset(s); per float field worst |c - r| / max(|c|, 1e-30)"
       << " (tolerance " << kTreeFloatRelTol << "):";
    double overall = 0.0;
    std::size_t identical = 0;
    for (std::size_t s = 0; s < fields.size(); ++s) {
        if (fields[s].kind != StreamFieldKind::Float) {
            continue;
        }
        overall = std::max(overall, t.worst[s]);
        if (t.worst[s] == 0.0) {
            ++identical;
            continue;
        }
        os << "\n  " << fields[s].label << " " << t.worst[s] << " (" << t.worstAt[s] << ")";
    }
    os << "\n  " << identical << " float field(s) bit-identical on every preset";
    os << "\n  overall worst " << overall;
    return os.str();
}

// ---- Scratch user preset directory (RAII; error_code overloads only) ----------

class ScratchPresetDir {
public:
    explicit ScratchPresetDir(std::string_view prefix) {
        std::error_code ec;
        const fs::path base = fs::temp_directory_path(ec);
        if (ec) {
            return;
        }
        // create_directory returns false (no error) when the path exists, so a
        // directory left by a crashed run is walked past, never adopted.
        for (int attempt = 0; attempt < 4096; ++attempt) {
            const fs::path candidate = base / (std::string(prefix) + std::to_string(attempt));
            std::error_code createEc;
            if (fs::create_directory(candidate, createEc)) {
                path_ = candidate;
                return;
            }
        }
    }

    ~ScratchPresetDir() {
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }

    ScratchPresetDir(const ScratchPresetDir&) = delete;
    ScratchPresetDir& operator=(const ScratchPresetDir&) = delete;
    ScratchPresetDir(ScratchPresetDir&&) = delete;
    ScratchPresetDir& operator=(ScratchPresetDir&&) = delete;

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

// ---- Parameter space (plan 6.14) ------------------------------------------------

struct SurfaceParam {
    Steinberg::Vst::ParamID id;
    Steinberg::int32 stepCount;
};

[[nodiscard]] Steinberg::tresult applyComponentState(::Vorago::Controller& ctrl,
                                                     const std::vector<std::uint8_t>& comp) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    std::vector<std::uint8_t> buf(comp);  // IBStream::write takes a non-const pointer
    Steinberg::int32 written = 0;
    if (!buf.empty() &&
        (stream->write(buf.data(), static_cast<Steinberg::int32>(buf.size()), &written) !=
             Steinberg::kResultOk ||
         std::cmp_not_equal(written, buf.size()))) {
        return Steinberg::kResultFalse;
    }
    if (stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) != Steinberg::kResultOk) {
        return Steinberg::kResultFalse;
    }
    return ctrl.setComponentState(stream);
}

[[nodiscard]] std::vector<double> readSurface(::Vorago::Controller& ctrl,
                                              const std::vector<SurfaceParam>& params) {
    std::vector<double> v;
    v.reserve(params.size());
    for (const SurfaceParam& p : params) {
        v.push_back(ctrl.getParamNormalized(p.id));
    }
    return v;
}

/// List IDs (stepCount > 0) differ iff round(n * stepCount) differs; continuous
/// IDs iff |dn| >= 0.10.
[[nodiscard]] std::size_t countDifferingIds(const std::vector<SurfaceParam>& params,
                                            const std::vector<double>& a,
                                            const std::vector<double>& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < params.size(); ++i) {
        const Steinberg::int32 steps = params[i].stepCount;
        if (steps > 0) {
            const auto s = static_cast<double>(steps);
            if (std::llround(a[i] * s) != std::llround(b[i] * s)) {
                ++n;
            }
        } else if (std::fabs(a[i] - b[i]) >= kContinuousDistinctDelta) {
            ++n;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("Vorago_FactoryPresets_ContainerAndInfo", "[vorago][preset]") {
    // SC-002 (FR-003, FR-028, FR-030).
    const std::vector<fs::path> files = factoryPresetFiles();
    const auto& defs = PD::allPresets();
    INFO("presets root: " << factoryPresetRoot().string());

    // The on-disk set == allPresets() (count and names).
    REQUIRE(files.size() == defs.size());
    std::set<std::string> onDisk;
    for (const fs::path& f : files) {
        onDisk.insert(presetKey(categoryOf(f), stemOf(f)));
    }
    std::set<std::string> defined;
    for (const PD::VoragoPresetDef& d : defs) {
        defined.insert(presetKey(d.category, d.name));
    }
    REQUIRE(onDisk == defined);

    const std::string classId = processorClassId();
    REQUIRE(classId.size() == 32u);

    // preset_manager.cpp:266-275 attribute order.
    constexpr std::array<std::string_view, 7> kAttrOrder{
        "MediaType", "PlugInName", "PlugInCategory", "Name", "MusicalCategory", "MusicalInstrument",
        "Comment"};

    for (const fs::path& f : files) {
        INFO("file " << f.string());
        const VoragoTest::PresetFile pf = VoragoTest::parseVstPreset(f);
        INFO("parse: " << pf.why);
        REQUIRE(pf.ok);
        REQUIRE(pf.classId == classId);

        const std::string category = categoryOf(f);
        const std::string stem = stemOf(f);
        const PD::VoragoPresetDef* def = findDef(category, stem);
        REQUIRE(def != nullptr);
        REQUIRE(pf.info == PD::buildVoragoInfoXml(def->name, def->category, def->description));

        // Independently, the parsed attributes against literals.
        const std::map<std::string, std::string> attrs = VoragoTest::parseInfoAttributes(pf.info);
        REQUIRE(attrs.size() == (def->description.empty() ? 6u : 7u));
        REQUIRE(attrs.at("MediaType") == "VstPreset");
        REQUIRE(attrs.at("PlugInName") == "Vorago");
        REQUIRE(attrs.at("PlugInCategory") == "Synth");
        REQUIRE(attrs.at("Name") == stem);
        REQUIRE(attrs.at("MusicalCategory") == category);
        REQUIRE(attrs.at("MusicalInstrument") == category);
        if (def->description.empty()) {
            REQUIRE_FALSE(attrs.contains("Comment"));
        } else {
            REQUIRE(attrs.at("Comment") == std::string(def->description));
        }

        std::size_t previous = 0;
        bool first = true;
        for (const std::string_view id : kAttrOrder) {
            INFO("attribute " << std::string(id));
            const std::size_t at = pf.info.find("id=\"" + std::string(id) + "\"");
            if (at == std::string::npos) {
                REQUIRE(id == "Comment");  // only the Comment line may be absent
                continue;
            }
            REQUIRE((first || at > previous));
            previous = at;
            first = false;
        }
    }
}

TEST_CASE("Vorago_FactoryPresets_InfoMatchesSavePreset", "[vorago][preset]") {
    // FR-003: the Info bytes the shared PresetManager::savePreset writes
    // (preset_manager.cpp:227-277) == buildVoragoInfoXml for the same values.
    const ScratchPresetDir userDir("vorago_t029_save_");
    INFO("temporary user preset directory: " << userDir.path().string());
    REQUIRE(!userDir.path().empty());

    for (const std::string_view categoryView : PD::kCategories) {
        const std::string category(categoryView);
        INFO("category " << category);

        const PD::VoragoPresetDef* firstDef = nullptr;
        for (const PD::VoragoPresetDef& d : PD::allPresets()) {
            if (d.category == categoryView) {
                firstDef = &d;
                break;
            }
        }

        // The first definition of the category, or the default surface.
        VoragoTest::PresetHost host;
        REQUIRE(host.prepare(48000.0, 512) == Steinberg::kResultOk);
        if (firstDef != nullptr) {
            std::vector<std::uint8_t> comp;
            std::string why;
            INFO("definition " << std::string(firstDef->name));
            REQUIRE(VoragoTest::buildPresetComponentState(*firstDef, comp, why));
            REQUIRE(host.loadState(std::span<const std::uint8_t>(comp)) == Steinberg::kResultOk);
        }
        const std::string name =
            (firstDef != nullptr) ? std::string(firstDef->name) : std::string("Info Probe");
        const std::string description = (firstDef != nullptr)
                                            ? std::string(firstDef->description)
                                            : std::string("A probe of the Info bytes");

        Krate::Plugins::PresetManager manager(
            ::Vorago::makeVoragoPresetConfig(),
            static_cast<Steinberg::Vst::IComponent*>(&host.processor()), nullptr, userDir.path());

        REQUIRE(manager.savePreset(name, category, description));
        const VoragoTest::PresetFile saved =
            VoragoTest::parseVstPreset(userDir.path() / category / (name + ".vstpreset"));
        INFO("parse: " << saved.why);
        REQUIRE(saved.ok);
        REQUIRE(saved.info == PD::buildVoragoInfoXml(name, category, description));

        // The no-Comment branch (preset_manager.cpp:273-275).
        const std::string bareName = name + " Bare";
        REQUIRE(manager.savePreset(bareName, category, ""));
        const VoragoTest::PresetFile bare =
            VoragoTest::parseVstPreset(userDir.path() / category / (bareName + ".vstpreset"));
        INFO("parse: " << bare.why);
        REQUIRE(bare.ok);
        REQUIRE(bare.info == PD::buildVoragoInfoXml(bareName, category, ""));
    }
}

TEST_CASE("Vorago_FactoryPresets_RoundTrip", "[vorago][preset]") {
    // SC-003 (FR-029): setState(Comp) == kResultOk, then getState byte-identical.
    for (const fs::path& f : factoryPresetFiles()) {
        INFO("file " << f.string());
        const VoragoTest::PresetFile pf = VoragoTest::parseVstPreset(f);
        INFO("parse: " << pf.why);
        REQUIRE(pf.ok);

        VoragoTest::PresetHost host;
        REQUIRE(host.prepare(48000.0, 512) == Steinberg::kResultOk);
        REQUIRE(host.loadState(std::span<const std::uint8_t>(pf.comp)) == Steinberg::kResultOk);
        std::vector<std::uint8_t> out;
        REQUIRE(host.saveState(out));
        REQUIRE(out == pf.comp);
    }
}

TEST_CASE("Vorago_FactoryPresets_BrowserScan", "[vorago][preset]") {
    // SC-004 (FR-030): both overrides - an empty temp user dir, the committed tree
    // as the factory dir (preset_manager.h:55-61).
    const ScratchPresetDir userDir("vorago_t029_scan_");
    INFO("temporary user preset directory: " << userDir.path().string());
    REQUIRE(!userDir.path().empty());

    Krate::Plugins::PresetManager manager(::Vorago::makeVoragoPresetConfig(), nullptr, nullptr,
                                          userDir.path(), factoryPresetRoot());
    const Krate::Plugins::PresetManager::PresetList scanned = manager.scanPresets();

    const auto& defs = PD::allPresets();
    REQUIRE(scanned.size() == defs.size());
    for (const Krate::Plugins::PresetInfo& p : scanned) {
        INFO("scanned " << p.path.string());
        REQUIRE(p.isFactory);
        REQUIRE(!p.subcategory.empty());
    }
    for (const std::string_view c : PD::kCategories) {
        const auto expected = static_cast<std::size_t>(std::ranges::count_if(
            defs, [c](const PD::VoragoPresetDef& d) { return d.category == c; }));
        INFO("category " << std::string(c));
        REQUIRE(manager.getPresetsForSubcategory(std::string(c)).size() == expected);
    }
}

TEST_CASE("Vorago_FactoryPresets_StreamShape", "[vorago][preset]") {
    // SC-005 (FR-005..FR-009).
    constexpr auto kR = std::memory_order_relaxed;
    const std::vector<StreamField> fields = streamFields();

    std::set<std::string> names;
    for (const fs::path& f : factoryPresetFiles()) {
        INFO("file " << f.string());
        const VoragoTest::PresetFile pf = VoragoTest::parseVstPreset(f);
        INFO("parse: " << pf.why);
        REQUIRE(pf.ok);

        VoragoTest::DecodedPresetState st;
        REQUIRE(VoragoTest::decodePresetState(std::span<const std::uint8_t>(pf.comp), st));
        REQUIRE(st.version == 3);
        REQUIRE(pf.comp.size() == ::Vorago::kStateV3Bytes);

        // FR-005: valid, unique, ASCII, no path separator.
        const std::string name = stemOf(f);
        REQUIRE(Krate::Plugins::PresetManager::isValidPresetName(name));
        REQUIRE(names.insert(name).second);
        const bool ascii = std::ranges::all_of(name, [](char ch) {
            const auto u = static_cast<unsigned char>(ch);
            return u >= 0x20u && u < 0x7Fu;
        });
        REQUIRE(ascii);
        REQUIRE(name.find_first_of("/\\") == std::string::npos);

        // FR-007: polyphony index <= 3 (<= 4 voices; the stream holds the voice
        // count 1..6, global_params.h:43).
        REQUIRE(st.global.polyphony.load(kR) - 1 <= 3);

        // FR-008: A <= 180 s, Rel <= 60 s.
        REQUIRE(VoragoTest::attackSpanSeconds(st) <= 180.0);
        REQUIRE(static_cast<double>(st.envelope.releaseMs.load(kR)) / 1000.0 <= 60.0);

        // FR-009: every stored float finite by bit pattern.
        for (std::size_t s = 0; s < fields.size(); ++s) {
            if (fields[s].kind == StreamFieldKind::Float) {
                INFO("float field " << fields[s].label);
                REQUIRE(Krate::DSP::detail::isFinite(slotFloat(pf.comp, s)));
            }
        }
    }
}

TEST_CASE("Vorago_FactoryPresets_TreeToleranceProbe", "[.measure][vorago]") {
    // Plan 6.15: regenerate each definition through buildPresetComponentState and
    // print, per float field, the worst |c - r| / max(|c|, 1e-30).
    const std::vector<StreamField> fields = streamFields();
    const TreeComparison t = compareTreeWithGenerator(fields);
    WARN(treeWorstReport(t, fields));
    INFO("structural failures:" << joinLines(t.failures));
    REQUIRE(t.failures.empty());
}

TEST_CASE("Vorago_FactoryPresets_TreeMatchesGenerator", "[vorago][preset]") {
    // SC-006 (FR-021, FR-032, C-9): same path set; byte-identical Info; version 3;
    // length 436; every int32 field equal; every float field within kTreeFloatRelTol.
    const std::vector<StreamField> fields = streamFields();
    REQUIRE(fields.size() * 4u == ::Vorago::kStateV3Bytes);

    // The slot table itself, checked on the default surface against the typed
    // decode (a misplaced run moves these slots).
    {
        constexpr auto kR = std::memory_order_relaxed;
        const std::vector<std::uint8_t> dflt = defaultComponentState();
        REQUIRE(dflt.size() == ::Vorago::kStateV3Bytes);
        VoragoTest::DecodedPresetState st;
        REQUIRE(VoragoTest::decodePresetState(std::span<const std::uint8_t>(dflt), st));
        REQUIRE(slotInt(dflt, slotOf("version", 0)) == 3);
        REQUIRE(slotInt(dflt, slotOf("global.polyphony", 0)) == st.global.polyphony.load(kR));
        REQUIRE(slotInt(dflt, slotOf("global.seedIndex", 0)) == st.global.seedIndex.load(kR));
        REQUIRE(slotInt(dflt, slotOf("space.freeze", 0)) == st.space.freeze.load(kR));
        REQUIRE(slotInt(dflt, slotOf("envelope.mode", 0)) == st.envelope.mode.load(kR));
        REQUIRE(slotFloat(dflt, slotOf("global.masterGain", 0)) == st.global.masterGain.load(kR));
        REQUIRE(slotFloat(dflt, slotOf("envelope.float", 4)) == st.envelope.releaseMs.load(kR));
        REQUIRE(slotFloat(dflt, slotOf("envelope.float", 5)) ==
                st.envelope.growthDurationSeconds.load(kR));
        REQUIRE(slotFloat(dflt, slotOf("life", 0)) == st.life.breathingDepth.load(kR));
        REQUIRE(slotFloat(dflt, slotOf("ecosystem.v3", 0)) == st.ecosystem.syncRate.load(kR));
        REQUIRE(slotFloat(dflt, slotOf("ecosystem.v3", 1)) == st.ecosystem.selfAffinity.load(kR));
        for (std::size_t s = 0; s < fields.size(); ++s) {
            INFO("slot " << s << " " << fields[s].label);
            if (fields[s].kind == StreamFieldKind::Int32) {
                REQUIRE(slotInt(dflt, s) >= 0);
                REQUIRE(slotInt(dflt, s) <= 64);  // versions, indices, enums, voice counts
            } else {
                REQUIRE(Krate::DSP::detail::isFinite(slotFloat(dflt, s)));
            }
        }
    }

    const TreeComparison t = compareTreeWithGenerator(fields);
    WARN(treeWorstReport(t, fields));
    INFO("structural / int / non-finite failures:" << joinLines(t.failures));
    REQUIRE(t.failures.empty());
    for (std::size_t s = 0; s < fields.size(); ++s) {
        if (fields[s].kind == StreamFieldKind::Float) {
            INFO("float field " << fields[s].label << " worst at " << t.worstAt[s]);
            CHECK(t.worst[s] <= kTreeFloatRelTol);
        }
    }
}

TEST_CASE("Vorago_PresetMatrix_ParameterSpaceDistinct", "[vorago][preset]") {
    // SC-009 (FR-014, FR-035, plan 6.14).
    auto ctrl = Steinberg::owned(new ::Vorago::Controller());
    REQUIRE(ctrl->initialize(nullptr) == Steinberg::kResultOk);

    std::vector<SurfaceParam> params;
    const Steinberg::int32 count = ctrl->getParameterCount();
    REQUIRE(std::cmp_equal(count, 108 + VoragoTest::kNumEcosystemRosterParams));
    for (Steinberg::int32 k = 0; k < count; ++k) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ctrl->getParameterInfo(k, info) == Steinberg::kResultOk);
        if (info.id == ::Vorago::kSustainPedalId || info.id == ::Vorago::kChannelPressureId) {
            continue;
        }
        params.push_back(SurfaceParam{.id = info.id, .stepCount = info.stepCount});
    }
    REQUIRE(params.size() == 106u + VoragoTest::kNumEcosystemRosterParams);  // 108 IDs

    // The default surface is the freshly initialised controller.
    const std::vector<double> fresh = readSurface(*ctrl, params);

    std::vector<std::string> labels;
    std::vector<std::vector<double>> surfaces;
    for (const PD::VoragoPresetDef& d : PD::allPresets()) {
        INFO("preset " << std::string(d.name));
        std::vector<std::uint8_t> comp;
        std::string why;
        REQUIRE(VoragoTest::buildPresetComponentState(d, comp, why));
        REQUIRE(applyComponentState(*ctrl, comp) == Steinberg::kResultOk);
        labels.push_back(presetKey(d.category, d.name));
        surfaces.push_back(readSurface(*ctrl, params));
    }

    std::size_t minCount = std::numeric_limits<std::size_t>::max();
    std::string minPair = "(no presets)";
    std::vector<std::string> failures;
    const auto consider = [&](const std::string& a, const std::string& b, std::size_t n) {
        if (n < minCount) {
            minCount = n;
            minPair = a;
            minPair += " vs ";
            minPair += b;
        }
        if (n < kMinDifferingIds) {
            std::string msg = a;
            msg += " vs ";
            msg += b;
            msg += ": ";
            msg += std::to_string(n);
            msg += " differing IDs";
            failures.push_back(std::move(msg));
        }
    };
    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        consider(labels[i], "default surface", countDifferingIds(params, surfaces[i], fresh));
        for (std::size_t j = i + 1; j < surfaces.size(); ++j) {
            consider(labels[i], labels[j], countDifferingIds(params, surfaces[i], surfaces[j]));
        }
    }
    WARN("parameter-space minimum: "
         << (surfaces.empty() ? std::string("n/a") : std::to_string(minCount)) << " differing IDs ("
         << minPair << "); floor " << kMinDifferingIds);
    INFO("pairs below the floor:" << joinLines(failures));
    REQUIRE(failures.empty());

    REQUIRE(ctrl->terminate() == Steinberg::kResultOk);
}
