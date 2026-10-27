// Profundum Phase 14 morph-modulation preset tests (specs/profundum-phase14-morph-modulation/tasks.md)
//
// T003 (FR-062, FR-063; plan 5.8, 4.13; tasks Deviation 2): the pre-phase harvest, sharded four ways.
//   - Profundum_MorphModPrePhaseHarvest_<k>of4 ([.harvest], run by the main loop through
//     evidence/prephase/run-harvest.sh at the pre-phase tree, before any engine or generator edit): every factory
//     definition i with i % 4 == k is built (presetCompOf) and rendered with renderPresetSweep(comp, {note}, 48 kHz,
//     false) at E1 and C3. Per render and channel, fingerprintRender is written to
//     evidence/prephase/fingerprints-<k>.csv (metrics as %a hex floats, the 32 checkpoints as their u32 bit
//     patterns); the decoded pre-existing state regions go to evidence/prephase/decoded-<k>.csv as hex bytes; shard 0
//     also writes every definition's claims to evidence/prephase-claims.csv. evidence/prephase/merge.js joins the
//     shards into evidence/prephase-fingerprints.csv and evidence/prephase-decoded.csv. These CSVs are evidence read
//     by a test (the close comparison), never C++ literals.
//
// T019 (FR-045; SC-023 (e), SC-008; plan 4.12, 5.8; tasks Deviations 2, 4):
//   - Profundum_MorphModHarnessCarriage: the processor engine's pushed config and wheel equal the effective
//     script's (probe context and a preset-context M-WHEEL arm); the named refusals; the C-10 decode; the Sync Rate
//     inverse lookup.
//   - Profundum_MorphModOverrideSmoke: renderPresetSweep's MorphModOverride (source / amount, Compare, wheel guard).
//   - Profundum_MorphModPrePhaseFingerprints_<k>of4 ([.harvest], close lane): every harvested preset re-rendered
//     (converted ones forced to Source Off) against evidence/prephase-fingerprints.csv with the shared
//     tolerances, and its pre-existing state regions bit-equal to evidence/prephase-decoded.csv.
//
// T024 (FR-060 (a), FR-061; SC-007, SC-023 (a)(b); tasks Deviation 6):
//   - Profundum_MorphModPresetTransparency_<k>of64 ([long], nightly, R10): every (definition i, note n) unit,
//     n 0 = E1, 1 = C3, with (2i + n) % 64 == k, through renderPresetSweep: Source Off vs every source at Amount 0
//     (wheel 0 and wheel 1), and Source Off vs the source(s) at Amount 1 under Compare A and under Compare B,
//     std::memcmp-identical on L and R (same binary, same code). Every Comp is 610 bytes, its first int32 is the
//     literal 3, and its pre-existing regions are
//     bit-equal to evidence/prephase-decoded.csv. Each shard prints its wall clock.
//   - Profundum_MorphModHarnessFidelity ([long]): every primary of M-ENV / M-CYCLE / M-VEL / M-WHEEL, E1 and C3,
//     the C-15 lo / hi arms through the processor: the C-5 fidelity arm passes, and over the placed window the
//     forceLinearCore twin's grid-record resolved.position equals the processor render's within 1e-6.

#include <catch2/catch_test_macros.hpp>

#include "plugin_ids.h"
#include "preset_render_support.h"
#include "preset_test_support.h"
#include "profundum_test_fixture.h"

#include <profundum_preset_defs.h>
#include <render_fingerprint.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

/// The phase's evidence directory (the scCoverageDir pattern, integration/preset_showcase_test.cpp:79-82).
[[nodiscard]] std::filesystem::path mmEvidenceDir() {
    return {std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase14-morph-modulation/evidence"};
}

/// One named region of the pre-phase state stream.
struct MmStateRegion {
    std::string_view field;
    std::size_t offset;
    std::size_t length;
};

/// The pre-existing state regions, named by the plugin_ids.h layout constants (plugin_ids.h:25-63) and the fixture's
/// derived global / macro offsets (profundum_test_fixture.h:1004-1008). Sorted by offset; they tile
/// [0, kStateNamesOffset + 2 * kEndpointNameBlockBytes).
[[nodiscard]] std::vector<MmStateRegion> mmStateRegions() {
    namespace P = ::Profundum;
    return {
        {.field="version", .offset=0, .length=kV2GlobalsAt},
        {.field="globals", .offset=kV2GlobalsAt, .length=kV2MacrosAt - kV2GlobalsAt},
        {.field="macros", .offset=kV2MacrosAt, .length=P::kStateV1Bytes - kV2MacrosAt},
        {.field="note", .offset=P::kStateV1Bytes, .length=P::kStateNoteBlockBytes},
        {.field="morphControl", .offset=P::kStateV1Bytes + P::kStateNoteBlockBytes, .length=P::kStateMorphControlBytes},
        {.field="seed", .offset=P::kStateV1Bytes + P::kStateNoteBlockBytes + P::kStateMorphControlBytes, .length=P::kStateSeedBytes},
        {.field="endpointA", .offset=P::kStateEndpointAOffset, .length=Krate::DSP::kMorphStateBytes},
        {.field="endpointB", .offset=P::kStateEndpointBOffset, .length=Krate::DSP::kMorphStateBytes},
        {.field="runtime", .offset=P::kStateRuntimeOffset, .length=Krate::DSP::BassMorphEngine::kMorphRuntimeBytes},
        {.field="nameA", .offset=P::kStateNamesOffset, .length=P::kEndpointNameBlockBytes},
        {.field="nameB", .offset=P::kStateNamesOffset + P::kEndpointNameBlockBytes, .length=P::kEndpointNameBlockBytes},
    };
}

/// %a hex float (exact).
[[nodiscard]] std::string mmHexFloat(double v) {
    std::array<char, 64> buf{};
    std::snprintf(buf.data(), buf.size(), "%a", v);
    return {buf.data()};
}

/// 0x%08X of a float's bit pattern.
[[nodiscard]] std::string mmBits(float v) {
    std::array<char, 16> buf{};
    std::snprintf(buf.data(), buf.size(), "0x%08X", static_cast<unsigned int>(std::bit_cast<std::uint32_t>(v)));
    return {buf.data()};
}

/// Lower-case hex of bytes [offset, offset + length).
[[nodiscard]] std::string mmHexBytes(const std::vector<std::uint8_t>& comp, std::size_t offset, std::size_t length) {
    static constexpr std::string_view kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(2u * length);
    for (std::size_t i = offset; i < offset + length; ++i) {
        out += kDigits[static_cast<std::size_t>(comp[i] >> 4u)];
        out += kDigits[static_cast<std::size_t>(comp[i] & 0x0Fu)];
    }
    return out;
}

void mmWriteFile(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
    CHECK(out.good());
}

/// Plan 4.13 / FR-063: one row per definition: name, primary label, secondary labels sorted and joined by ';'.
[[nodiscard]] std::string mmClaimsCsv() {
    namespace Pd = ::Profundum::PresetDefs;
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    std::ostringstream os;
    os << "name,primaryLabel,secondaryLabels\n";
    for (const Pd::ProfundumPresetDef& def : defs) {
        std::vector<std::string> secondaries;
        secondaries.reserve(def.secondaries.size());
        for (const Pd::Capability c : def.secondaries) {
            secondaries.emplace_back(Pd::capabilityTable()[static_cast<std::size_t>(c)].label);
        }
        std::sort(secondaries.begin(), secondaries.end());
        std::string joined;
        for (std::size_t i = 0; i < secondaries.size(); ++i) {
            if (i > 0) {
                joined += ';';
            }
            joined += secondaries[i];
        }
        const std::string primary(Pd::capabilityTable()[static_cast<std::size_t>(def.primary)].label);
        os << auRd::auCsvField(std::string(def.name)) << ',' << auRd::auCsvField(primary) << ','
           << auRd::auCsvField(joined) << '\n';
    }
    return os.str();
}

/// One Profundum_MorphModPrePhaseHarvest shard: every definition index i with i % 4 == k (plan 5.8, FR-062).
void mmRunHarvestShard(std::size_t k) {
    namespace P = ::Profundum;
    namespace Pd = ::Profundum::PresetDefs;
    namespace Tu = Krate::DSP::TestUtils;
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());

    // The regions tile the pre-existing stream exactly (sorted, contiguous, ending at the v3 name blocks' end).
    const std::vector<MmStateRegion> regions = mmStateRegions();
    constexpr std::size_t kPreExistingEnd = P::kStateNamesOffset + 2 * P::kEndpointNameBlockBytes;
    REQUIRE(std::is_sorted(regions.begin(), regions.end(),
                           [](const MmStateRegion& a, const MmStateRegion& b) { return a.offset < b.offset; }));
    REQUIRE(regions.front().offset == 0u);
    for (std::size_t r = 1; r < regions.size(); ++r) {
        INFO("region " << regions[r].field);
        REQUIRE(regions[r].offset == regions[r - 1].offset + regions[r - 1].length);
    }
    REQUIRE(regions.back().offset + regions.back().length == kPreExistingEnd);

    std::ostringstream fp;
    fp << "preset,note,channel,rms,peak,meanAbs,totalVariation";
    for (std::size_t c = 0; c < Tu::kRenderCheckpoints; ++c) {
        fp << ",cp" << c;
    }
    fp << '\n';
    std::ostringstream dec;
    dec << "preset,field,offset,length,hex\n";

    constexpr std::array<std::uint8_t, 2> kNotes{auRd::kAuE1, auRd::kAuC3};
    std::size_t presets = 0;
    std::size_t fpRows = 0;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (i % 4u != k) {
            continue;
        }
        const Pd::ProfundumPresetDef& def = defs[i];
        const std::string name(def.name);
        INFO("preset " << name);
        const std::vector<std::uint8_t> comp = presetCompOf(def);
        REQUIRE(comp.size() == P::kStateV3Bytes);
        REQUIRE(kPreExistingEnd <= comp.size());
        ++presets;

        for (const std::uint8_t note : kNotes) {
            const std::array<std::uint8_t, 1> notes{note};
            const PresetSweepRender r = renderPresetSweep(comp, notes, 48000.0, false);
            const Tu::RenderFingerprint fl = Tu::fingerprintRender(r.L);
            const Tu::RenderFingerprint fr = Tu::fingerprintRender(r.R);
            const std::array<std::pair<const char*, const Tu::RenderFingerprint*>, 2> channels{{{"L", &fl}, {"R", &fr}}};
            for (const auto& [channel, f] : channels) {
                fp << auRd::auCsvField(name) << ',' << static_cast<int>(note) << ',' << channel << ','
                   << mmHexFloat(f->rms) << ',' << mmHexFloat(f->peak) << ',' << mmHexFloat(f->meanAbs) << ','
                   << mmHexFloat(f->totalVariation);
                for (const float cp : f->checkpoints) {
                    fp << ',' << mmBits(cp);
                }
                fp << '\n';
                ++fpRows;
            }
        }

        for (const MmStateRegion& region : regions) {
            dec << auRd::auCsvField(name) << ',' << region.field << ',' << region.offset << ',' << region.length << ','
                << mmHexBytes(comp, region.offset, region.length) << '\n';
        }
    }
    REQUIRE(presets > 0u);

    const std::filesystem::path dir = mmEvidenceDir();
    mmWriteFile(dir / "prephase" / ("fingerprints-" + std::to_string(k) + ".csv"), fp.str());
    mmWriteFile(dir / "prephase" / ("decoded-" + std::to_string(k) + ".csv"), dec.str());
    if (k == 0u) {
        mmWriteFile(dir / "prephase-claims.csv", mmClaimsCsv());
    }
    std::cout << "# Profundum_MorphModPrePhaseHarvest_" << k << "of4: presets=" << presets << " fingerprintRows=" << fpRows
              << " decodedRows=" << presets * regions.size() << "\n"
              << std::flush;
}

/// Seconds since @p t0 (steady clock).
[[nodiscard]] double mmSecondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

TEST_CASE("Profundum_MorphModPrePhaseHarvest_0of4", "[profundum][morphmod][preset][.harvest]") {
    const auto t0 = std::chrono::steady_clock::now();
    mmRunHarvestShard(0);
    std::cout << "WALL Profundum_MorphModPrePhaseHarvest_0of4 " << mmSecondsSince(t0) << " s\n" << std::flush;
}

TEST_CASE("Profundum_MorphModPrePhaseHarvest_1of4", "[profundum][morphmod][preset][.harvest]") {
    const auto t0 = std::chrono::steady_clock::now();
    mmRunHarvestShard(1);
    std::cout << "WALL Profundum_MorphModPrePhaseHarvest_1of4 " << mmSecondsSince(t0) << " s\n" << std::flush;
}

TEST_CASE("Profundum_MorphModPrePhaseHarvest_2of4", "[profundum][morphmod][preset][.harvest]") {
    const auto t0 = std::chrono::steady_clock::now();
    mmRunHarvestShard(2);
    std::cout << "WALL Profundum_MorphModPrePhaseHarvest_2of4 " << mmSecondsSince(t0) << " s\n" << std::flush;
}

TEST_CASE("Profundum_MorphModPrePhaseHarvest_3of4", "[profundum][morphmod][preset][.harvest]") {
    const auto t0 = std::chrono::steady_clock::now();
    mmRunHarvestShard(3);
    std::cout << "WALL Profundum_MorphModPrePhaseHarvest_3of4 " << mmSecondsSince(t0) << " s\n" << std::flush;
}

// =============================================================================
// T019 (FR-045; SC-023 (e), SC-008; plan 4.12, 5.5, 5.8; tasks Deviations 2, 4): the render harness carries the
// modulation, the renderPresetSweep override, and the pre-phase close comparison.
// =============================================================================

namespace {

using MmMod = Krate::DSP::ProfundumMorphModulator;

/// The carriage renders: 64 host blocks (the config is pushed at the first process()).
constexpr std::uint64_t kMmCarriageSamples = std::uint64_t{64} * 64u;

[[nodiscard]] std::uint32_t mmBitsOf(float x) noexcept {
    return std::bit_cast<std::uint32_t>(x);
}

/// SC-023 (e): enums and the note pair equal, every float bit-equal.
void mmCheckConfigEqual(const MmMod::Config& got, const MmMod::Config& want) {
    CHECK(got.source == want.source);
    CHECK(mmBitsOf(got.amount) == mmBitsOf(want.amount));
    CHECK(mmBitsOf(got.envAttackMs) == mmBitsOf(want.envAttackMs));
    CHECK(mmBitsOf(got.envDecayMs) == mmBitsOf(want.envDecayMs));
    CHECK(mmBitsOf(got.envSustain) == mmBitsOf(want.envSustain));
    CHECK(got.cycleShape == want.cycleShape);
    CHECK(mmBitsOf(got.cycleRateHz) == mmBitsOf(want.cycleRateHz));
    CHECK(got.cycleSync == want.cycleSync);
    CHECK(got.cycleNote == want.cycleNote);
    CHECK(got.cycleModifier == want.cycleModifier);
}

/// A probe-context script on A_s (auOne), MONO, one held note.
[[nodiscard]] auPt::VoiceEngineScript mmMakeScript(std::uint64_t total) {
    return auRd::auScript(auRd::auOne(auRd::auStateAs()), auRd::kAuE1, total);
}

[[nodiscard]] std::vector<std::uint8_t> mmCompOf(const StateBytes& st) {
    std::vector<std::uint8_t> comp(st.size());
    for (std::size_t i = 0; i < st.size(); ++i) {
        comp[i] = std::to_integer<std::uint8_t>(st[i]);
    }
    return comp;
}

/// One CSV line split into fields; a quoted field may hold commas and "" escapes (auCsvField's form).
[[nodiscard]] std::vector<std::string> mmCsvSplit(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    out.push_back(field);
    return out;
}

/// The data lines of @p file (header dropped, CR stripped, empty lines skipped); REQUIREs the file.
[[nodiscard]] std::vector<std::vector<std::string>> mmReadLines(const std::filesystem::path& file) {
    INFO("evidence file " << file.string());
    REQUIRE(std::filesystem::exists(file));
    std::ifstream in(file, std::ios::binary);
    REQUIRE(in.good());
    std::vector<std::vector<std::string>> rows;
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (header) {
            header = false;
            continue;
        }
        if (line.empty()) {
            continue;
        }
        rows.push_back(mmCsvSplit(line));
    }
    return rows;
}

/// One Profundum_MorphModPrePhaseFingerprints shard (plan 5.8, SC-008, FR-061; Deviation 2).
void mmRunCloseComparisonShard(std::size_t k) {
    namespace P = ::Profundum;
    namespace Pd = ::Profundum::PresetDefs;
    namespace Tu = Krate::DSP::TestUtils;
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());

    // The harvested references, parsed at run time (hex floats via strtod, checkpoints via stoul + bit_cast).
    using FpKey = std::tuple<std::string, int, std::string>;
    std::map<FpKey, Tu::RenderFingerprint> refs;
    std::set<std::string> harvested;
    for (const std::vector<std::string>& f : mmReadLines(mmEvidenceDir() / "prephase-fingerprints.csv")) {
        REQUIRE(f.size() == 7u + Tu::kRenderCheckpoints);
        Tu::RenderFingerprint fp;
        fp.rms = std::strtod(f[3].c_str(), nullptr);
        fp.peak = std::strtod(f[4].c_str(), nullptr);
        fp.meanAbs = std::strtod(f[5].c_str(), nullptr);
        fp.totalVariation = std::strtod(f[6].c_str(), nullptr);
        for (std::size_t c = 0; c < Tu::kRenderCheckpoints; ++c) {
            const auto bits = static_cast<std::uint32_t>(std::stoul(f[7 + c], nullptr, 16));
            fp.checkpoints[c] = std::bit_cast<float>(bits);
        }
        harvested.insert(f[0]);
        refs.emplace(FpKey{f[0], std::stoi(f[1]), f[2]}, fp);
    }
    REQUIRE(!refs.empty());
    struct Region {
        std::size_t offset;
        std::size_t length;
        std::string hex;
    };
    std::map<std::string, std::vector<Region>> regions;
    for (const std::vector<std::string>& f : mmReadLines(mmEvidenceDir() / "prephase-decoded.csv")) {
        REQUIRE(f.size() == 5u);
        regions[f[0]].push_back(
            Region{.offset=static_cast<std::size_t>(std::stoul(f[2])), .length=static_cast<std::size_t>(std::stoul(f[3])), .hex=f[4]});
    }

    // A harvested preset missing from allPresets() fails.
    std::set<std::string> current;
    for (const Pd::ProfundumPresetDef& def : defs) {
        current.insert(std::string(def.name));
    }
    for (const std::string& name : harvested) {
        INFO("harvested preset " << name);
        CHECK(current.count(name) == 1u);
    }

    constexpr std::array<std::uint8_t, 2> kNotes{auRd::kAuE1, auRd::kAuC3};
    std::size_t compared = 0;
    std::size_t converted = 0;
    std::size_t failures = 0;
    std::size_t regionMismatches = 0;
    double worstMetric = 0.0;
    double worstSample = 0.0;
    std::string worstWhere = "none";
    std::vector<std::string> skipped;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (i % 4u != k) {
            continue;
        }
        const std::string name(defs[i].name);
        if (!harvested.contains(name)) {
            skipped.push_back(name);   // a new preset: no pre-phase reference
            continue;
        }
        INFO("preset " << name);
        const std::vector<std::uint8_t> comp = presetCompOf(defs[i]);
        REQUIRE(comp.size() == P::kStateV3Bytes);
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        const bool isConverted = d.morphMod.source != 0;
        converted += isConverted ? 1u : 0u;
        ++compared;

        for (const std::uint8_t note : kNotes) {
            const std::array<std::uint8_t, 1> notes{note};
            const PresetSweepRender r = isConverted
                                            ? renderPresetSweep(comp, notes, 48000.0, false, std::nullopt,
                                                                MorphModOverride{.source = 0})
                                            : renderPresetSweep(comp, notes, 48000.0, false);
            const Tu::RenderFingerprint fl = Tu::fingerprintRender(r.L);
            const Tu::RenderFingerprint fr = Tu::fingerprintRender(r.R);
            const std::array<std::pair<const char*, const Tu::RenderFingerprint*>, 2> channels{{{"L", &fl}, {"R", &fr}}};
            for (const auto& [channel, actual] : channels) {
                const auto it = refs.find(FpKey{name, static_cast<int>(note), channel});
                INFO("note " << static_cast<int>(note) << " channel " << channel);
                REQUIRE(it != refs.end());
                const Tu::FingerprintComparison c = Tu::compareFingerprints(*actual, it->second);
                if (!c.withinTolerance()) {
                    ++failures;
                    std::cout << "# FINGERPRINT FAIL " << name << " note " << static_cast<int>(note) << ' ' << channel
                              << ": metric " << c.worstMetricRelativeError << " sample " << c.worstSampleError << ' '
                              << c.detail << '\n';
                }
                if (c.worstMetricRelativeError > worstMetric || static_cast<double>(c.worstSampleError) > worstSample) {
                    worstWhere = name + " " + std::to_string(static_cast<int>(note)) + " " + channel;
                }
                worstMetric = std::max(worstMetric, c.worstMetricRelativeError);
                worstSample = std::max(worstSample, static_cast<double>(c.worstSampleError));
            }
        }

        // FR-061: every pre-existing region bit-equal to the harvest.
        const auto reg = regions.find(name);
        REQUIRE(reg != regions.end());
        REQUIRE(!reg->second.empty());
        for (const Region& g : reg->second) {
            REQUIRE(g.offset + g.length <= comp.size());
            if (mmHexBytes(comp, g.offset, g.length) != g.hex) {
                ++regionMismatches;
                std::cout << "# REGION MISMATCH " << name << " at " << g.offset << " (" << g.length << " bytes)\n";
            }
        }
    }

    std::cout << "# Profundum_MorphModPrePhaseFingerprints_" << k << "of4: compared=" << compared
              << " converted=" << converted << " failures=" << failures << " regionMismatches=" << regionMismatches
              << " worstMetric=" << worstMetric << " worstSample=" << worstSample << " (" << worstWhere << ")"
              << " skippedNew=" << skipped.size();
    for (const std::string& s : skipped) {
        std::cout << " [" << s << "]";
    }
    std::cout << "\n" << std::flush;
    CHECK(compared > 0u);
    CHECK(failures == 0u);
    CHECK(regionMismatches == 0u);
}

}  // namespace

TEST_CASE("Profundum_MorphModHarnessCarriage", "[profundum][morphmod][preset]") {
    namespace P = ::Profundum;
    namespace Pd = ::Profundum::PresetDefs;
    using K = auPt::VoiceEngineEventKind;
    enableFTZDAZ();

    SECTION("probe context: the script's Envelope config reaches the processor engine") {
        auPt::VoiceEngineScript sc = mmMakeScript(kMmCarriageSamples);
        sc.morphMod.source = MmMod::Source::Envelope;
        sc.morphMod.envAttackMs = 150.0f;
        sc.morphMod.envDecayMs = 600.0f;
        sc.morphMod.envSustain = 0.6f;
        sc.morphMod.amount = 0.8f;
        auto fx = std::make_unique<ProcessorFixture>();
        PresetArmContext ctx;
        ctx.processor = fx.get();
        const auPt::VoiceEngineScript e = effectiveScriptFor(sc, ctx);
        CHECK(e.morphMod.source == MmMod::Source::Envelope);   // probe context: the script's own config stands
        const auPt::VoiceEngineTaps t = renderScriptThroughProcessor(sc, false, -1, ctx);
        REQUIRE(ctx.stop.empty());
        CHECK(t.outL.size() == static_cast<std::size_t>(kMmCarriageSamples));
        REQUIRE(fx->engine() != nullptr);
        mmCheckConfigEqual(fx->engine()->morphModulation(), e.morphMod);
        CHECK(mmBitsOf(fx->engine()->morphModWheel()) == mmBitsOf(e.morphModWheel));
    }

    SECTION("preset context: the M-WHEEL hi arm (source forced to Mod Wheel, Amount 1, wheel 1)") {
        const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
        REQUIRE(!defs.empty());
        const std::vector<std::uint8_t> comp = presetCompOf(defs.front());
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        REQUIRE(d.morphMod.present);
        const auPt::VoiceEngineScript sc =
            auRd::auScript(preset_showcase_detail::settingOf(d), auRd::kAuE1, kMmCarriageSamples);
        auto fx = std::make_unique<ProcessorFixture>();
        PresetArmContext ctx;
        ctx.preset = &d;
        ctx.comp = comp;
        ctx.allowedEndpoints = {{d.a, d.b}};
        ctx.processor = fx.get();
        DecodedPresetState::MorphMod candidate = d.morphMod;
        candidate.amount = 1.0f;
        ctx.morphModCandidate = candidate;
        ctx.forceMorphModSource = P::kMorphModSourceModWheel;
        ctx.morphModWheel = 1.0f;
        const auPt::VoiceEngineScript e = effectiveScriptFor(sc, ctx);
        CHECK(e.morphMod.source == MmMod::Source::ModWheel);
        CHECK(mmBitsOf(e.morphMod.amount) == mmBitsOf(1.0f));
        CHECK(mmBitsOf(e.morphModWheel) == mmBitsOf(1.0f));
        // The sample-0 block carries the wheel (parameter 6 at its normalised value).
        const auto params = processorSampleZeroParams(e);
        CHECK(std::count_if(params.begin(), params.end(), [](const auto& p) {
                  return p.first == P::kModWheelId && p.second == 1.0;
              }) == 1);
        const auPt::VoiceEngineTaps t = renderScriptThroughProcessor(sc, false, -1, ctx);
        REQUIRE(ctx.stop.empty());
        CHECK(t.outL.size() == static_cast<std::size_t>(kMmCarriageSamples));
        REQUIRE(fx->engine() != nullptr);
        mmCheckConfigEqual(fx->engine()->morphModulation(), e.morphMod);
        CHECK(mmBitsOf(fx->engine()->morphModWheel()) == mmBitsOf(e.morphModWheel));

        // Non-vacuity: without the three overlays the stored config and wheel stand, so the equality above is not
        // a default.
        PresetArmContext plain;
        plain.preset = &d;
        const auPt::VoiceEngineScript stored = effectiveScriptFor(sc, plain);
        CHECK(stored.morphMod.source == static_cast<MmMod::Source>(d.morphMod.source));
        CHECK(mmBitsOf(stored.morphModWheel) == mmBitsOf(0.0f));
        const auto storedParams = processorSampleZeroParams(stored);
        CHECK(std::none_of(storedParams.begin(), storedParams.end(),
                           [](const auto& p) { return p.first == P::kModWheelId; }));
    }

    SECTION("named refusals: Morph Mod bypass, MorphModulation and MorphModBypass events render nothing") {
        const auto refused = [](const auPt::VoiceEngineScript& s) {
            PresetArmContext ctx;
            const auPt::VoiceEngineTaps t = renderScriptThroughProcessor(s, false, -1, ctx);
            INFO("refusal: " << ctx.stop);
            CHECK(!ctx.stop.empty());
            CHECK(ctx.stop.find("Morph Mod") != std::string::npos);
            CHECK(t.outL.empty());
        };
        auPt::VoiceEngineScript bypass = mmMakeScript(kMmCarriageSamples);
        bypass.morphModBypass = true;
        refused(bypass);

        auPt::VoiceEngineScript config = mmMakeScript(kMmCarriageSamples);
        config.morphModStates.push_back(MmMod::Config{});
        auPt::VoiceEngineEvent cev{};
        cev.sample = 640;
        cev.kind = K::MorphModulation;
        cev.index = 0;
        config.events.push_back(cev);
        refused(config);

        auPt::VoiceEngineScript bypassEvent = mmMakeScript(kMmCarriageSamples);
        auPt::VoiceEngineEvent bev{};
        bev.sample = 640;
        bev.kind = K::MorphModBypass;
        bev.index = 1;
        bypassEvent.events.push_back(bev);
        refused(bypassEvent);

        // Control: a mid-render wheel move has a carrier (parameter 6) and is not refused.
        auPt::VoiceEngineScript wheel = mmMakeScript(kMmCarriageSamples);
        auPt::VoiceEngineEvent wev{};
        wev.sample = 640;
        wev.kind = K::MorphModWheel;
        wev.value = 0.5f;
        wheel.events.push_back(wev);
        Steinberg::Vst::ParamID id = 0;
        double norm = 0.0;
        CHECK(processorCarrierParam(wev, id, norm));
        CHECK(id == P::kModWheelId);
        CHECK(norm == 0.5);
        PresetArmContext probe;
        CHECK(processorRenderRefusal(wheel, probe).empty());
    }

    SECTION("decodePresetState reads the C-10 block of a 610-byte Comp") {
        auto fx = std::make_unique<ProcessorFixture>();
        StateBytes st = writeV2Stream(*fx->proc);
        REQUIRE(st.size() == P::kStateV3Bytes);
        constexpr std::size_t at = P::kStateMorphModOffset;
        patchI32(st, at, 2);
        patchF32(st, at + 4u, -0.6f);
        patchF32(st, at + 8u, 50.0f);
        patchF32(st, at + 12u, 900.0f);
        patchF32(st, at + 16u, 0.4f);
        patchI32(st, at + 20u, 3);
        patchF32(st, at + 24u, 3.0f);
        patchI32(st, at + 28u, 1);
        patchI32(st, at + 32u, 7);
        const DecodedPresetState q = decodePresetState(mmCompOf(st));
        CHECK(q.ok);
        CHECK(q.morphMod.present);
        CHECK(q.morphMod.source == 2);
        CHECK(mmBitsOf(q.morphMod.amount) == mmBitsOf(-0.6f));
        CHECK(mmBitsOf(q.morphMod.attackMs) == mmBitsOf(50.0f));
        CHECK(mmBitsOf(q.morphMod.decayMs) == mmBitsOf(900.0f));
        CHECK(mmBitsOf(q.morphMod.sustain) == mmBitsOf(0.4f));
        CHECK(q.morphMod.cycleShape == 3);
        CHECK(mmBitsOf(q.morphMod.cycleRateHz) == mmBitsOf(3.0f));
        CHECK(q.morphMod.cycleSync == 1);
        CHECK(q.morphMod.cycleSyncRate == 7);

        // withMorphMod writes the same block from the config (Sync Rate as the inverse table index).
        const MmMod::Config c = morphModConfigOfDecoded(q.morphMod);
        CHECK(c.cycleNote == Krate::DSP::NoteValue::Sixteenth);
        CHECK(c.cycleModifier == Krate::DSP::NoteModifier::None);
        const StateBytes rewritten = withMorphMod(writeV2Stream(*fx->proc), c);
        const DecodedPresetState r = decodePresetState(mmCompOf(rewritten));
        CHECK(r.morphMod.source == q.morphMod.source);
        CHECK(mmBitsOf(r.morphMod.amount) == mmBitsOf(q.morphMod.amount));
        CHECK(mmBitsOf(r.morphMod.attackMs) == mmBitsOf(q.morphMod.attackMs));
        CHECK(mmBitsOf(r.morphMod.decayMs) == mmBitsOf(q.morphMod.decayMs));
        CHECK(mmBitsOf(r.morphMod.sustain) == mmBitsOf(q.morphMod.sustain));
        CHECK(r.morphMod.cycleShape == q.morphMod.cycleShape);
        CHECK(mmBitsOf(r.morphMod.cycleRateHz) == mmBitsOf(q.morphMod.cycleRateHz));
        CHECK(r.morphMod.cycleSync == q.morphMod.cycleSync);
        CHECK(r.morphMod.cycleSyncRate == q.morphMod.cycleSyncRate);

        // Non-vacuity: a stream cut before the block decodes no block.
        StateBytes cut = writeV2Stream(*fx->proc);
        cut.resize(P::kStateMorphModOffset);
        const DecodedPresetState none = decodePresetState(mmCompOf(cut));
        CHECK_FALSE(none.morphMod.present);
        CHECK_FALSE(none.ok);
    }

    SECTION("mmSyncIndexOf is the inverse of kNoteValueDropdownMapping") {
        using NV = Krate::DSP::NoteValue;
        using NM = Krate::DSP::NoteModifier;
        CHECK(mmSyncIndexOf(NV::Quarter, NM::None) == 13);
        CHECK(mmSyncIndexOf(NV::Sixteenth, NM::None) == 7);
        for (int i = 0; i < Krate::DSP::kNoteValueDropdownCount; ++i) {
            const Krate::DSP::NoteValueMapping& m = Krate::DSP::kNoteValueDropdownMapping[static_cast<std::size_t>(i)];
            CHECK(mmSyncIndexOf(m.note, m.modifier) == i);
        }
        CHECK(mmSyncIndexOf(NV::Quarter, static_cast<NM>(3)) == -1);   // a pair not in the table
        CHECK(mmSyncIndexOf(static_cast<NV>(10), NM::None) == -1);
    }
}

TEST_CASE("Profundum_MorphModOverrideSmoke", "[profundum][morphmod][preset]") {
    namespace Pd = ::Profundum::PresetDefs;
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    // One factory preset stored at Source Off (the override's transparency arm needs a stored Off).
    std::vector<std::uint8_t> comp;
    for (const Pd::ProfundumPresetDef& def : defs) {
        std::vector<std::uint8_t> c = presetCompOf(def);
        if (decodePresetState(c).morphMod.source == 0) {
            comp = std::move(c);
            break;
        }
    }
    REQUIRE(!comp.empty());
    const std::array<std::uint8_t, 1> notes{auRd::kAuE1};
    const auto sameBits = [](const PresetSweepRender& x, const PresetSweepRender& y) {
        return x.L.size() == y.L.size() && x.R.size() == y.R.size() &&
               std::memcmp(x.L.data(), y.L.data(), x.L.size() * sizeof(float)) == 0 &&
               std::memcmp(x.R.data(), y.R.data(), x.R.size() * sizeof(float)) == 0;
    };

    const PresetSweepRender stored = renderPresetSweep(comp, notes, 48000.0, false);
    const PresetSweepRender cycleZero =
        renderPresetSweep(comp, notes, 48000.0, false, std::nullopt, {.source = 2, .amount = 0.0f});
    CHECK(sameBits(stored, cycleZero));   // Cycle at Amount 0 is transparent
    const PresetSweepRender cycleOne =
        renderPresetSweep(comp, notes, 48000.0, false, std::nullopt, {.source = 2, .amount = 1.0f});
    CHECK_FALSE(sameBits(stored, cycleOne));   // non-vacuity: the patched source / amount reach the render

    // Compare A after setState: renderPresetSweep REQUIREs compareModeForTest() == A after prepare.
    const PresetSweepRender compareA =
        renderPresetSweep(comp, notes, 48000.0, false, std::nullopt, {.compare = ::Profundum::CompareMode::A});
    CHECK(compareA.L.size() == stored.L.size());

    // The two wheel writers are refused together (the predicate renderPresetSweep REQUIREs false).
    CHECK(morphModOverrideWheelConflict(true, MorphModOverride{.wheel = 1.0}));
    CHECK_FALSE(morphModOverrideWheelConflict(false, MorphModOverride{.wheel = 1.0}));
    CHECK_FALSE(morphModOverrideWheelConflict(true, MorphModOverride{}));
}

TEST_CASE("Profundum_MorphModPrePhaseFingerprints_0of4", "[profundum][morphmod][preset][.harvest]") {
    const auto t0 = std::chrono::steady_clock::now();
    mmRunCloseComparisonShard(0);
    std::cout << "WALL Profundum_MorphModPrePhaseFingerprints_0of4 " << mmSecondsSince(t0) << " s\n" << std::flush;
}

TEST_CASE("Profundum_MorphModPrePhaseFingerprints_1of4", "[profundum][morphmod][preset][.harvest]") {
    const auto t0 = std::chrono::steady_clock::now();
    mmRunCloseComparisonShard(1);
    std::cout << "WALL Profundum_MorphModPrePhaseFingerprints_1of4 " << mmSecondsSince(t0) << " s\n" << std::flush;
}

TEST_CASE("Profundum_MorphModPrePhaseFingerprints_2of4", "[profundum][morphmod][preset][.harvest]") {
    const auto t0 = std::chrono::steady_clock::now();
    mmRunCloseComparisonShard(2);
    std::cout << "WALL Profundum_MorphModPrePhaseFingerprints_2of4 " << mmSecondsSince(t0) << " s\n" << std::flush;
}

TEST_CASE("Profundum_MorphModPrePhaseFingerprints_3of4", "[profundum][morphmod][preset][.harvest]") {
    const auto t0 = std::chrono::steady_clock::now();
    mmRunCloseComparisonShard(3);
    std::cout << "WALL Profundum_MorphModPrePhaseFingerprints_3of4 " << mmSecondsSince(t0) << " s\n" << std::flush;
}

// =============================================================================
// T024 (FR-060 (a), FR-061; SC-007, SC-023 (a)(b); tasks Deviation 6): the per-push transparency shards and the
// [long] harness-fidelity case.
// =============================================================================

namespace {

/// SC-007: the transparency shard count over (definition, note) units (G17: 32 per-definition shards read 26.9-93.4 s
/// under the 30-worker gate and 29.7 s alone for two presets, target 9 s; re-sharded to 64, never fewer renders).
constexpr std::size_t kMmTransparencyShards = 64;

/// SC-023 (b): the twin's grid-record resolved.position against the processor render's.
constexpr double kMmTwinPositionTolerance = 1.0e-6;

/// The C-10 sources other than Off: Envelope 1, Cycle 2, Velocity 3, Mod Wheel 4 (morph_mod_params.h).
constexpr std::array<std::int32_t, 4> kMmSources{1, 2, 3, ::Profundum::kMorphModSourceModWheel};

/// The first sample at which two renders differ bitwise on L or R; nullopt when L and R are memcmp-identical.
[[nodiscard]] std::optional<std::size_t> mmFirstBitDifference(const PresetSweepRender& x, const PresetSweepRender& y) {
    if (x.L.size() != y.L.size() || x.R.size() != y.R.size()) {
        return std::size_t{0};
    }
    if (std::memcmp(x.L.data(), y.L.data(), x.L.size() * sizeof(float)) == 0 &&
        std::memcmp(x.R.data(), y.R.data(), x.R.size() * sizeof(float)) == 0) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < x.L.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(x.L[i]) != std::bit_cast<std::uint32_t>(y.L[i]) ||
            std::bit_cast<std::uint32_t>(x.R[i]) != std::bit_cast<std::uint32_t>(y.R[i])) {
            return i;
        }
    }
    return std::size_t{0};
}

/// A short label of an override (the failure report).
[[nodiscard]] std::string mmOverrideLabel(const MorphModOverride& mm) {
    std::string s = "{";
    if (mm.source.has_value()) {
        s += " source " + std::to_string(*mm.source);
    }
    if (mm.amount.has_value()) {
        s += " amount " + std::to_string(*mm.amount);
    }
    if (mm.wheel.has_value()) {
        s += " wheel " + std::to_string(*mm.wheel);
    }
    if (mm.compare.has_value()) {
        s += *mm.compare == ::Profundum::CompareMode::A ? " compare A" : " compare B";
    }
    return s + " }";
}

/// The little-endian first int32 of a Comp, read from the bytes (never through the version symbol).
[[nodiscard]] std::int32_t mmFirstWord(const std::vector<std::uint8_t>& comp) {
    REQUIRE(comp.size() >= 4u);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(comp[0]) | (static_cast<std::uint32_t>(comp[1]) << 8u) |
                                     (static_cast<std::uint32_t>(comp[2]) << 16u) |
                                     (static_cast<std::uint32_t>(comp[3]) << 24u));
}

/// One Profundum_MorphModPresetTransparency shard (SC-007; FR-060 (a), FR-061).
void mmRunTransparencyShard(std::size_t k) {
    namespace P = ::Profundum;
    namespace Pd = ::Profundum::PresetDefs;
    static_assert(P::kStateV3Bytes == 610u, "SC-007: every factory Comp is 610 bytes");
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());

    // FR-061 reference: the pre-phase decoded regions (evidence/prephase-decoded.csv, T005), parsed at run time.
    struct Region {
        std::size_t offset;
        std::size_t length;
        std::string hex;
    };
    std::map<std::string, std::vector<Region>> regions;
    for (const std::vector<std::string>& f : mmReadLines(mmEvidenceDir() / "prephase-decoded.csv")) {
        REQUIRE(f.size() == 5u);
        regions[f[0]].push_back(
            Region{.offset=static_cast<std::size_t>(std::stoul(f[2])), .length=static_cast<std::size_t>(std::stoul(f[3])), .hex=f[4]});
    }
    REQUIRE(!regions.empty());

    constexpr std::array<std::uint8_t, 2> kNotes{auRd::kAuE1, auRd::kAuC3};
    constexpr std::array<P::CompareMode, 2> kCompares{P::CompareMode::A, P::CompareMode::B};
    std::size_t presets = 0;
    std::size_t converted = 0;
    std::size_t pairs = 0;
    std::size_t mismatches = 0;
    std::size_t regionMismatches = 0;
    std::vector<std::string> unharvested;
    // The shard unit is (definition i, note n): unit i * kNotes.size() + n goes to shard unit % kMmTransparencyShards.
    const std::size_t noteCount = kNotes.size();
    const auto inShard = [k, noteCount](std::size_t i, std::size_t n) {
        return (i * noteCount + n) % kMmTransparencyShards == k;
    };
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (!inShard(i, 0) && !inShard(i, 1)) {
            continue;
        }
        const std::string name(defs[i].name);
        INFO("preset " << name);
        const std::vector<std::uint8_t> comp = presetCompOf(defs[i]);
        CHECK(comp.size() == 610u);
        REQUIRE(comp.size() == P::kStateV3Bytes);
        CHECK(mmFirstWord(comp) == 3);
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        REQUIRE(d.morphMod.present);
        const std::int32_t stored = d.morphMod.source;
        REQUIRE((stored >= 0 && stored <= 4));
        const bool isConverted = stored != 0;
        converted += isConverted ? 1u : 0u;
        ++presets;

        // FR-061: every pre-existing region bit-equal to the harvest (a preset added this phase has none).
        if (const auto reg = regions.find(name); reg == regions.end()) {
            unharvested.push_back(name);
        } else {
            REQUIRE(!reg->second.empty());
            for (const Region& g : reg->second) {
                REQUIRE(g.offset + g.length <= comp.size());
                if (mmHexBytes(comp, g.offset, g.length) != g.hex) {
                    ++regionMismatches;
                    std::cout << "# REGION MISMATCH " << name << " at " << g.offset << " (" << g.length << " bytes)\n";
                }
            }
        }

        for (std::size_t n = 0; n < kNotes.size(); ++n) {
            if (!inShard(i, n)) {
                continue;
            }
            const std::uint8_t note = kNotes[n];
            const std::array<std::uint8_t, 1> notes{note};
            INFO("note " << static_cast<int>(note));
            const auto render = [&comp, &notes](const MorphModOverride& mm) {
                return renderPresetSweep(comp, notes, 48000.0, false, std::nullopt, mm);
            };
            const auto checkPair = [&](const MorphModOverride& refMm, const PresetSweepRender& ref,
                                       const MorphModOverride& mm) {
                const PresetSweepRender r = render(mm);
                const std::optional<std::size_t> at = mmFirstBitDifference(ref, r);
                ++pairs;
                if (at.has_value()) {
                    ++mismatches;
                    std::cout << "# TRANSPARENCY FAIL " << name << " note " << static_cast<int>(note) << ": "
                              << mmOverrideLabel(refMm) << " vs " << mmOverrideLabel(mm) << " first differing sample "
                              << *at << '\n';
                }
                INFO(mmOverrideLabel(refMm) << " vs " << mmOverrideLabel(mm));
                CHECK_FALSE(at.has_value());
            };

            // Amount 0, wheel 0 and wheel 1, against Source Off (a stored-Off preset at wheel 0: as stored, mm = {}).
            for (const bool wheelUp : {false, true}) {
                const double w = wheelUp ? 1.0 : 0.0;
                MorphModOverride ref;
                if (isConverted || wheelUp) {
                    ref.source = 0;
                }
                if (wheelUp) {
                    ref.wheel = w;
                }
                const PresetSweepRender refRender = render(ref);
                for (const std::int32_t x : kMmSources) {
                    MorphModOverride mm;
                    mm.source = x;
                    mm.amount = 0.0f;
                    mm.wheel = w;
                    checkPair(ref, refRender, mm);
                }
            }

            // Amount 1 under the Compare bypass: Source Off vs every source (stored Off) or the stored source only
            // (converted), under Compare A and under Compare B.
            for (const P::CompareMode c : kCompares) {
                MorphModOverride ref;
                ref.source = 0;
                ref.compare = c;
                const PresetSweepRender refRender = render(ref);
                for (const std::int32_t x : kMmSources) {
                    if (isConverted && x != stored) {
                        continue;
                    }
                    MorphModOverride mm;
                    mm.source = x;
                    mm.amount = 1.0f;
                    mm.compare = c;
                    checkPair(ref, refRender, mm);
                }
            }
        }
    }

    std::cout << "# Profundum_MorphModPresetTransparency_" << k << "of" << kMmTransparencyShards
              << ": presets=" << presets << " converted=" << converted << " pairs=" << pairs
              << " mismatches=" << mismatches << " regionMismatches=" << regionMismatches
              << " unharvested=" << unharvested.size();
    for (const std::string& s : unharvested) {
        std::cout << " [" << s << "]";
    }
    std::cout << "\n" << std::flush;
    CHECK(presets > 0u);
    CHECK(mismatches == 0u);
    CHECK(regionMismatches == 0u);
}

/// SC-023 (b): over [t0, t0 + w), every processor grid record has a twin record at the same sample whose
/// resolved.position agrees within kMmTwinPositionTolerance. Returns the number of compared grid points; the worst
/// difference goes to @p worst.
[[nodiscard]] std::size_t mmCheckTwinPositions(const std::vector<auPt::VoiceEngineGridRecord>& proc,
                                               const std::vector<auPt::VoiceEngineGridRecord>& twin, std::size_t t0,
                                               std::size_t w, double& worst) {
    const auto bySample = [](const auPt::VoiceEngineGridRecord& r, std::uint64_t s) { return r.sample < s; };
    std::size_t compared = 0;
    const auto it0 = std::lower_bound(proc.begin(), proc.end(), static_cast<std::uint64_t>(t0), bySample);
    const auto it1 = std::lower_bound(proc.begin(), proc.end(), static_cast<std::uint64_t>(t0 + w), bySample);
    for (auto it = it0; it != it1; ++it) {
        const auto tw = std::lower_bound(twin.begin(), twin.end(), it->sample, bySample);
        INFO("grid sample " << it->sample);
        REQUIRE(tw != twin.end());
        REQUIRE(tw->sample == it->sample);
        const double diff =
            std::abs(static_cast<double>(it->resolved.position) - static_cast<double>(tw->resolved.position));
        worst = std::max(worst, diff);
        CHECK(diff <= kMmTwinPositionTolerance);
        ++compared;
    }
    return compared;
}

/// One morphModSourceCell arm (step 3) rendered through the processor with its twin.
struct MmFidelityArm {
    auPt::VoiceEngineTaps proc;
    auPt::VoiceEngineTaps twin;
};

}  // namespace

#define MM_TRANSPARENCY_SHARD(K)                                                                             \
    TEST_CASE("Profundum_MorphModPresetTransparency_" #K "of64", "[profundum][morphmod][preset][long]") {    \
        const auto t0 = std::chrono::steady_clock::now();                                                    \
        mmRunTransparencyShard(K);                                                                           \
        std::cout << "WALL Profundum_MorphModPresetTransparency_" #K "of64 " << mmSecondsSince(t0) << " s\n" \
                  << std::flush;                                                                             \
    }

MM_TRANSPARENCY_SHARD(0)
MM_TRANSPARENCY_SHARD(1)
MM_TRANSPARENCY_SHARD(2)
MM_TRANSPARENCY_SHARD(3)
MM_TRANSPARENCY_SHARD(4)
MM_TRANSPARENCY_SHARD(5)
MM_TRANSPARENCY_SHARD(6)
MM_TRANSPARENCY_SHARD(7)
MM_TRANSPARENCY_SHARD(8)
MM_TRANSPARENCY_SHARD(9)
MM_TRANSPARENCY_SHARD(10)
MM_TRANSPARENCY_SHARD(11)
MM_TRANSPARENCY_SHARD(12)
MM_TRANSPARENCY_SHARD(13)
MM_TRANSPARENCY_SHARD(14)
MM_TRANSPARENCY_SHARD(15)
MM_TRANSPARENCY_SHARD(16)
MM_TRANSPARENCY_SHARD(17)
MM_TRANSPARENCY_SHARD(18)
MM_TRANSPARENCY_SHARD(19)
MM_TRANSPARENCY_SHARD(20)
MM_TRANSPARENCY_SHARD(21)
MM_TRANSPARENCY_SHARD(22)
MM_TRANSPARENCY_SHARD(23)
MM_TRANSPARENCY_SHARD(24)
MM_TRANSPARENCY_SHARD(25)
MM_TRANSPARENCY_SHARD(26)
MM_TRANSPARENCY_SHARD(27)
MM_TRANSPARENCY_SHARD(28)
MM_TRANSPARENCY_SHARD(29)
MM_TRANSPARENCY_SHARD(30)
MM_TRANSPARENCY_SHARD(31)
MM_TRANSPARENCY_SHARD(32)
MM_TRANSPARENCY_SHARD(33)
MM_TRANSPARENCY_SHARD(34)
MM_TRANSPARENCY_SHARD(35)
MM_TRANSPARENCY_SHARD(36)
MM_TRANSPARENCY_SHARD(37)
MM_TRANSPARENCY_SHARD(38)
MM_TRANSPARENCY_SHARD(39)
MM_TRANSPARENCY_SHARD(40)
MM_TRANSPARENCY_SHARD(41)
MM_TRANSPARENCY_SHARD(42)
MM_TRANSPARENCY_SHARD(43)
MM_TRANSPARENCY_SHARD(44)
MM_TRANSPARENCY_SHARD(45)
MM_TRANSPARENCY_SHARD(46)
MM_TRANSPARENCY_SHARD(47)
MM_TRANSPARENCY_SHARD(48)
MM_TRANSPARENCY_SHARD(49)
MM_TRANSPARENCY_SHARD(50)
MM_TRANSPARENCY_SHARD(51)
MM_TRANSPARENCY_SHARD(52)
MM_TRANSPARENCY_SHARD(53)
MM_TRANSPARENCY_SHARD(54)
MM_TRANSPARENCY_SHARD(55)
MM_TRANSPARENCY_SHARD(56)
MM_TRANSPARENCY_SHARD(57)
MM_TRANSPARENCY_SHARD(58)
MM_TRANSPARENCY_SHARD(59)
MM_TRANSPARENCY_SHARD(60)
MM_TRANSPARENCY_SHARD(61)
MM_TRANSPARENCY_SHARD(62)
MM_TRANSPARENCY_SHARD(63)

#undef MM_TRANSPARENCY_SHARD

// SC-023 (a)(b): every primary of the four MorphMod columns, E1 and C3: the C-15 lo / hi arms of its stored source
// (morphModSourceCell's levers, preset_render_support.h) rendered through the processor with the twin.
TEST_CASE("Profundum_MorphModHarnessFidelity", "[profundum][morphmod][preset][long]") {
    namespace Pd = ::Profundum::PresetDefs;
    namespace sd = preset_showcase_detail;
    namespace md = morph_mod_reader_detail;
    using C = Pd::Capability;
    enableFTZDAZ();
    const auto wall0 = std::chrono::steady_clock::now();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    constexpr std::array<std::uint8_t, 2> kNotes{auRd::kAuE1, auRd::kAuC3};
    const std::size_t w = Krate::DSP::TestUtils::kLowFrequencyFftSize;

    std::array<std::size_t, 4> primariesPerColumn{};
    for (const Pd::ProfundumPresetDef& def : defs) {
        std::int32_t source = 0;
        switch (def.primary) {
            case C::MEnv: source = 1; break;
            case C::MCycle: source = 2; break;
            case C::MVel: source = 3; break;
            case C::MWheel: source = 4; break;
            default: break;
        }
        if (source == 0) {
            continue;
        }
        ++primariesPerColumn[static_cast<std::size_t>(source - 1)];
        const std::string name(def.name);
        INFO("preset " << name << ", stored source " << source);
        const std::vector<std::uint8_t> comp = presetCompOf(def);
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        REQUIRE(d.morphMod.present);
        REQUIRE(d.morphMod.source == source);
        const auRd::AuSetting setting = sd::settingOf(d);

        for (const std::uint8_t note : kNotes) {
            INFO("note " << static_cast<int>(note));
            PresetArmContext ctx;
            ctx.preset = &d;
            ctx.comp = comp;
            ctx.allowedEndpoints = {{setting.a, setting.b}};
            const auPt::VoiceEngineScript sc = auRd::auScript(setting, note, kMorphModArmTotal);
            REQUIRE(!sc.events.empty());
            REQUIRE(sc.events[0].kind == auRd::AuKind::NoteOn);

            // morphModSourceCell step 3 (C-15 table keyed by source). Each arm: the processor render with twin
            // (which appends one ctx.fidelity entry) and the forceLinearCore twin of the same effective script.
            const auto arm = [&](bool hi) {
                auPt::VoiceEngineScript s = sc;
                ctx.forceMorphModSource.reset();
                ctx.morphModWheel.reset();
                switch (source) {
                    case 1:
                    case 2:
                        if (!hi) {
                            ctx.forceMorphModSource = 0;
                        }
                        break;
                    case 3: s.events[0].velocity = hi ? std::uint8_t{127} : std::uint8_t{1}; break;
                    default: ctx.morphModWheel = hi ? 1.0f : 0.0f; break;
                }
                MmFidelityArm a;
                a.proc = renderScriptThroughProcessor(s, true, -1, ctx);
                INFO("refusal: " << ctx.stop);
                REQUIRE(ctx.stop.empty());
                a.twin = auPt::voiceEngineRender(auPt::voiceEngineTwinScript(effectiveScriptFor(s, ctx)));
                return a;
            };
            const MmFidelityArm lo = arm(false);
            const MmFidelityArm hi = arm(true);

            // SC-023 (a): the C-5 fidelity arm of both processor renders.
            REQUIRE(ctx.fidelity.size() == 2u);
            for (const PresetArmContext::Fidelity& f : ctx.fidelity) {
                std::cout << "# Profundum_MorphModHarnessFidelity " << name << " note " << static_cast<int>(note)
                          << ": fidelity " << (f.pass ? "pass" : "FAIL") << " worstSample " << f.worstSample
                          << " worstMetric " << f.worstMetric << " (" << f.tap << ")\n";
                INFO("fidelity tap " << f.tap << " worstSample " << f.worstSample << " worstMetric " << f.worstMetric);
                CHECK(f.pass);
            }

            // SC-023 (b): over the placed C-15 window, the twin's grid-record resolved.position equals the
            // processor render's within 1e-6 at every grid point, in both arms.
            const md::MmArmWindow win = md::mmPlaceWindow(lo.proc.grid, hi.proc.grid, auRd::kAuFs);
            INFO("resolved-position range lo [" << win.loMin << ", " << win.loMax << "] hi [" << win.hiMin << ", "
                                                << win.hiMax << "]");
            REQUIRE(win.found);
            double worst = 0.0;
            const std::size_t nLo = mmCheckTwinPositions(lo.proc.grid, lo.twin.grid, win.t0, w, worst);
            const std::size_t nHi = mmCheckTwinPositions(hi.proc.grid, hi.twin.grid, win.t0, w, worst);
            CHECK(nLo > 0u);
            CHECK(nHi > 0u);
            std::cout << "# Profundum_MorphModHarnessFidelity " << name << " note " << static_cast<int>(note)
                      << ": t0 " << static_cast<double>(win.t0) / auRd::kAuFs << " s, p_lo " << win.pLo << ", p_hi "
                      << win.pHi << ", grid points " << nLo << " + " << nHi << ", worst |twin - processor| " << worst
                      << "\n"
                      << std::flush;
        }
    }
    // Each new column has at least one primary to measure (FR-040; Profundum_CoverageMatrixComplete holds the rule).
    for (std::size_t c = 0; c < primariesPerColumn.size(); ++c) {
        INFO("MorphMod column " << c);
        CHECK(primariesPerColumn[c] > 0u);
    }
    std::cout << "WALL Profundum_MorphModHarnessFidelity " << mmSecondsSince(wall0) << " s\n" << std::flush;
}
