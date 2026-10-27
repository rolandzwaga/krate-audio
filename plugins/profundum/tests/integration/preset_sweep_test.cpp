// Profundum Phase 13 preset sweep tests (specs/profundum-phase13-presets-release/tasks.md)
//
// Placeholder created by T007 (Deviation 1). Cases written by T019, T022, T024:
// sweep, rates, max Mass, mono sum, determinism, distinctness, loudness (FR-038 - FR-044, FR-061, FR-063, FR-064;
// SC-016 - SC-021, SC-031), Profundum_PresetAuditionRenders (FR-050; SC-023).
//
// T019 (FR-038 - FR-044, FR-061, FR-063, FR-064; plan 5.5, 5.6, 5.8, 5.9, 5.12):
//   Per push, mechanism ([profundum][sweep]) on a test-local preset T (both endpoints auStateAs(), sustain 1):
//   - Profundum_SweepTimeline: makeTimeline at 48 / 44.1 / 96 kHz; getTailSamples() after setState and
//     setupProcessing equals the Phase 11 formula ceil((R_eff + 1250) fs / 1000) + 64 for T's release.
//   - Profundum_PresetMonoSumMechanics: T's 48 kHz E1 sweep render reads valid on W1 below f_c with rho >= 0.99 and
//     |Q| <= 1.0 dB, 0 anchor mismatches, the R' = -L copy fails the rho bar; a SOLID-stored T reads L == R bitwise
//     from s0 = 544.
//   - Profundum_PresetDeterminismMechanics: a test-local DRIFT -> Mass preset reproduces under its seed within
//     kSampleTolerance and differs under seed ^ 0x5A5A; T (seed-independent) agrees under both seeds.
//   Per push ([profundum][distinct]):
//   - Profundum_PresetDistinctnessParams (Deviation 6): every library pair differs in >= 3 registered parameters
//     (120 endpoint slots + 24 non-E slots); non-vacuity on two test-local defs differing in exactly 2, then 3.
//   [long] library cases (iterate allPresets(); T023 / T027 run them):
//   - Profundum_PresetSweep, Profundum_PresetSweepRates, Profundum_PresetTruePeakMaxMass, Profundum_PresetMonoSum,
//     Profundum_PresetDeterminism, Profundum_PresetLoudnessMatch, Profundum_PresetDistinctness.
//   Hidden pilot ([.pilot]): Profundum_PresetDistinctnessPilot writes evidence/distinctness.csv (plan 5.8 steps 1-5).
//
// Recorded choices (T019 notes):
//   - The [long] cases read through the presetReadings cache (preset_render_support.h), so one render per
//     (preset, note, fs, variant) serves every case in the same process.
//   - The true-peak bar is the decoded stored ceiling + 0.1 dB (FR-039; FR-060 pins the ceiling at -1 dBTP).
//   - The four hidden performance parameters (4-7) are never stored, so their slots never differ (they are counted
//     in the 24 non-E slots as constants).
//   - distinctness.csv is a two-column key,value file; the library case reads key "F" and requires both control
//     rows to have passed.

#include <catch2/catch_test_macros.hpp>

#include "preset_render_support.h"
#include "profundum_preset_host.h"
#include "profundum_test_fixture.h"

#include "parameters/global_params.h"
#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"

#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace Pd = ::Profundum::PresetDefs;
using SwMS = Krate::DSP::MorphState;

constexpr double kSwFs = 48000.0;
constexpr std::array<double, 3> kSwRates{44100.0, 48000.0, 96000.0};
constexpr std::array<std::uint8_t, 2> kSwNotes{auRd::kAuE1, auRd::kAuC3};
static_assert(kPresetChordNotes[0] == 28 && kPresetChordNotes[1] == 35 && kPresetChordNotes[2] == 40,
              "FR-063: the chord is E1, B1, E2 (MIDI 28, 35, 40)");

[[nodiscard]] std::vector<std::uint8_t> swBytesOf(const StateBytes& b) {
    std::vector<std::uint8_t> out(b.size());
    if (!b.empty()) {
        std::memcpy(out.data(), b.data(), b.size());
    }
    return out;
}

[[nodiscard]] StateBytes swStateOf(std::span<const std::uint8_t> comp) {
    StateBytes out(comp.size());
    if (!comp.empty()) {
        std::memcpy(out.data(), comp.data(), comp.size());
    }
    return out;
}

/// A test-local preset: endpoints @p a / @p b over a fresh processor's stream (every other field the registered
/// default, so the P-6 pins hold; Morph Position 0).
[[nodiscard]] std::vector<std::uint8_t> swPresetComp(const SwMS& a, const SwMS& b) {
    auto fresh = std::make_unique<ProcessorFixture>();
    return swBytesOf(withEndpoints(writeV2Stream(*fresh->proc), a, b));
}

/// T: auStateAs() (A_s with sustain 1, voiceEngineAs).
[[nodiscard]] SwMS swStateT() {
    SwMS t = auRd::auStateAs();
    REQUIRE(t.ampEnv.sustain == 1.0f);
    REQUIRE(Krate::DSP::isValidMorphState(t));
    return t;
}

/// T stored in SOLID.
[[nodiscard]] SwMS swStateSolid() {
    SwMS t = swStateT();
    t.field.mode = Krate::DSP::BassStereoField::ImageMode::Solid;
    REQUIRE(Krate::DSP::isValidMorphState(t));
    return t;
}

/// The test-local DRIFT -> Mass preset: the AT-40 setting (A_s + anchor level 0.5, Motion DRIFT -> Mass, amount 1,
/// depth 1, smoothness 0.5, Bipolar; profundum_audibility_readers.h:1888-1889, :1961-1967).
[[nodiscard]] SwMS swStateDrift() {
    using Src = SwMS::MotionConfig::Source;
    using Tgt = SwMS::MotionConfig::Target;
    using Pol = SwMS::MotionConfig::Polarity;
    SwMS s = auRd::auStateAs();
    s.anchor.level = auRd::kAuAt26AnchorLevel;
    s.motion.source = Src::Drift;
    s.motion.target = Tgt::Mass;
    s.motion.amount = 1.0f;
    s.motion.depth = 1.0f;
    s.motion.driftSmoothness = 0.5f;
    s.motion.polarity = Pol::Bipolar;
    REQUIRE(Krate::DSP::isValidMorphState(s));
    return s;
}

/// The FR-060 formula (the tail_test.cpp:56-65 exact-integer ceil, independent of the processor's helper).
[[nodiscard]] Steinberg::uint32 swFormulaTail(double rEffMs, double fs) {
    const double x = (rEffMs + 1250.0) * fs;
    double q = std::ceil(x / 1000.0);
    if ((q - 1.0) * 1000.0 >= x) {
        q -= 1.0;
    } else if (q * 1000.0 < x) {
        q += 1.0;
    }
    return static_cast<Steinberg::uint32>(q) +
           static_cast<Steinberg::uint32>(Krate::DSP::ProfundumEngine::kLimiterLookaheadSamples);
}

/// R_eff = max(A, B release) through the macro law, on a stack-local matrix (tail_test.cpp:68-78).
[[nodiscard]] float swEffectiveRelease(const DecodedPresetState& d) {
    const float r = std::max(d.a.ampEnv.releaseMs, d.b.ampEnv.releaseMs);
    Krate::DSP::ProfundumMacroMatrix m;
    m.setMacros(Krate::DSP::ProfundumMacroValues{
        .weight = d.macros[0], .darkBright = d.macros[1], .tightLoose = d.macros[2], .cleanViolent = d.macros[3]});
    const float c = m.contributions()[static_cast<std::size_t>(Krate::DSP::ProfundumMacroTarget::AmpReleaseOct)];
    if ((std::bit_cast<std::uint32_t>(c) & 0x7FFFFFFFu) == 0u) {
        return r;
    }
    return std::clamp(r * std::exp2(c), Krate::DSP::kMinEnvelopeTimeMs, Krate::DSP::kMaxEnvelopeTimeMs);
}

/// The worst per-sample |x - y| over both channels of two renders of the same timeline; +inf on a non-finite
/// sample or a length mismatch.
[[nodiscard]] double swWorstDiff(const PresetSweepRender& x, const PresetSweepRender& y) {
    if (x.timeline.total != y.timeline.total || x.L.size() < x.timeline.total || y.L.size() < y.timeline.total) {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    const std::array<std::pair<const std::vector<float>*, const std::vector<float>*>, 2> ch{
        {{&x.L, &y.L}, {&x.R, &y.R}}};
    for (const auto& [a, b] : ch) {
        for (std::size_t i = 0; i < x.timeline.total; ++i) {
            const float u = (*a)[i];
            const float v = (*b)[i];
            if (!presetFloatIsFinite(u) || !presetFloatIsFinite(v)) {
                return std::numeric_limits<double>::infinity();
            }
            worst = std::max(worst, std::abs(static_cast<double>(u) - static_cast<double>(v)));
        }
    }
    return worst;
}

/// Plan 5.9 on one Comp (48 kHz E1 sweep timeline).
struct SwDeterminism {
    double sameSeed = 0.0;    // two renders under the stored seed (seed-dependent presets only)
    double otherSeed = 0.0;   // stored seed against stored ^ 0x5A5A
    bool pass = false;
};

[[nodiscard]] SwDeterminism swDeterminism(std::span<const std::uint8_t> comp, bool seedDependent) {
    const DecodedPresetState d = decodePresetState(comp);
    REQUIRE(d.ok);
    const std::array<std::uint8_t, 1> e1{auRd::kAuE1};
    const double tol = static_cast<double>(Krate::DSP::TestUtils::kSampleTolerance);
    SwDeterminism out;
    const PresetSweepRender a = renderPresetSweep(comp, e1, kSwFs, false, d.seed);
    const PresetSweepRender c = renderPresetSweep(comp, e1, kSwFs, false, d.seed ^ kPresetSeedTwinXor);
    out.otherSeed = swWorstDiff(a, c);
    if (seedDependent) {
        const PresetSweepRender b = renderPresetSweep(comp, e1, kSwFs, false, d.seed);
        out.sameSeed = swWorstDiff(a, b);
        out.pass = out.sameSeed <= tol && out.otherSeed > tol;
    } else {
        out.pass = out.otherSeed <= tol;
    }
    return out;
}

// -----------------------------------------------------------------------------
// Parameter space (C-7, plan 5.8 "Library check, parameter part")
// -----------------------------------------------------------------------------

/// One registered-parameter slot, re-normalized from the decoded Comp through the laws.
struct SwParamSlot {
    std::string name;
    bool stepped = false;
    double norm = 0.0;   // continuous slots
    long long index = 0;   // stepped slots: the list index
};

/// 60 E-parameters per endpoint (120) + the 24 non-E registered parameters.
[[nodiscard]] std::vector<SwParamSlot> swParamSlots(const DecodedPresetState& d) {
    namespace P = ::Profundum;
    std::vector<SwParamSlot> out;
    out.reserve(2u * P::kSoundRows.size() + 24u);
    const auto cont = [&out](std::string name, double norm) {
        out.push_back(SwParamSlot{.name = std::move(name), .stepped = false, .norm = norm, .index = 0});
    };
    const auto step = [&out](std::string name, long long index) {
        out.push_back(SwParamSlot{.name = std::move(name), .stepped = true, .norm = 0.0, .index = index});
    };

    const std::array<std::pair<const char*, const SwMS*>, 2> endpoints{{{"A", &d.a}, {"B", &d.b}}};
    for (const auto& [label, state] : endpoints) {
        const P::LeafBits bits = P::leafBitsOf(*state);
        for (const P::SoundParamRow& row : P::kSoundRows) {
            const P::FieldInfo& f = P::lawOf(row.leaf);
            const std::string name = std::string(label) + "." + std::to_string(row.id) + "." + std::string(f.name);
            if (row.leaf2 != P::kNoLeaf) {
                step(name, P::syncRateIndexOf(bits[row.leaf], bits[row.leaf2]));
            } else if (f.kind != SwMS::Kind::F32) {
                step(name, static_cast<long long>(bits[row.leaf]));
            } else if (f.law == SwMS::Law::Stepped) {
                step(name, std::llround(static_cast<double>(std::bit_cast<float>(bits[row.leaf]))));
            } else {
                cont(name, P::normalizedFromLeafWord(row.leaf, bits[row.leaf]));
            }
        }
    }

    cont("0.outputGain", P::outputGainToNormalized(d.outputGain));
    step("1.voiceMode", d.voiceMode);
    step("2.polyphony", d.polyphony);
    cont("3.outputCeiling", P::ceilingToNormalized(d.outputCeilingDb));
    for (int id = 4; id <= 7; ++id) {
        cont(std::to_string(id) + ".performance (not stored)", 0.0);
    }
    for (std::size_t i = 0; i < d.macros.size(); ++i) {
        cont(std::to_string(100 + i) + ".macro", static_cast<double>(d.macros[i]));
    }
    cont("105.morphPosition", static_cast<double>(d.morph.position));
    step("410.monoPriority", d.note.priority);
    step("411.legato", d.note.legato);
    cont("412.portamento", P::normalizedFromPlain(P::kPortamentoLaw, static_cast<double>(d.note.portamentoMs)));
    step("413.portamentoMode", d.note.portamentoMode);
    step("414.glideMode", d.note.glideMode);
    step("415.noteMemory", d.note.memory);
    step("416.retrigger", d.note.retrigger);
    step("417.bendRange", std::llround(static_cast<double>(d.note.bendSt)));
    step("1000.anchorMorphMode", d.morph.anchorMode);
    step("1001.editTarget", d.morph.editTarget);
    cont("1002.morphRamp", P::normalizedFromPlain(P::kMorphRampLaw, static_cast<double>(d.morph.rampMs)));
    return out;
}

/// The count of slots differing by >= 0.1 normalized or by list index; @p listed names them.
[[nodiscard]] std::size_t swDifferingParams(const std::vector<SwParamSlot>& x, const std::vector<SwParamSlot>& y,
                                            std::string& listed) {
    REQUIRE(x.size() == y.size());
    listed.clear();
    std::size_t n = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        REQUIRE(x[i].name == y[i].name);
        const bool differs = x[i].stepped ? x[i].index != y[i].index : !(std::abs(x[i].norm - y[i].norm) < 0.1);
        if (differs) {
            ++n;
            if (!listed.empty()) {
                listed += ";";
            }
            listed += x[i].name;
        }
    }
    return n;
}

[[nodiscard]] std::vector<SwParamSlot> swSlotsOf(const Pd::ProfundumPresetDef& def) {
    const std::vector<std::uint8_t> comp = presetCompOf(def);
    const DecodedPresetState d = decodePresetState(comp);
    REQUIRE(d.ok);
    std::vector<SwParamSlot> slots = swParamSlots(d);
    REQUIRE(slots.size() == 2u * ::Profundum::kSoundRows.size() + 24u);
    return slots;
}

/// A test-local definition carrying only @p controls (macros), for the parameter-space non-vacuity arm.
[[nodiscard]] Pd::ProfundumPresetDef swControlsDef(std::string_view name, std::vector<Pd::ParamSetting> controls) {
    Pd::ProfundumPresetDef def{};
    def.name = name;
    def.category = "Sub";
    def.description = "T019 parameter-space mechanics";
    def.primary = Pd::Capability::AT10;
    def.controls = std::move(controls);
    def.nameA = "Low";
    def.nameB = "High";
    return def;
}

// -----------------------------------------------------------------------------
// distinctness.csv (plan 5.8 step 5)
// -----------------------------------------------------------------------------

[[nodiscard]] std::filesystem::path swDistinctnessCsv() {
    return {std::string(KRATE_DSP_TESTS_DIR) +
                                 "/../../specs/profundum-phase13-presets-release/evidence/distinctness.csv"};
}

/// key,value rows (the header row "key,value" is skipped).
[[nodiscard]] std::map<std::string, std::string> swReadKeyValues(const std::filesystem::path& p) {
    std::map<std::string, std::string> out;
    std::ifstream in(p, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t comma = line.find(',');
        if (comma == std::string::npos) {
            continue;
        }
        out[line.substr(0, comma)] = line.substr(comma + 1);
    }
    out.erase("key");
    return out;
}

/// The two-note descriptor of a Comp (E1, C3) at host block @p block and seed @p seed (kPresetStoredSeed: stored).
[[nodiscard]] std::array<PresetDescriptor, 2> swDescribe(std::span<const std::uint8_t> comp, std::size_t block,
                                                          std::uint32_t seed) {
    const DecodedPresetState d = decodePresetState(comp);
    REQUIRE(d.ok);
    return {describePreset(d, comp, kSwNotes[0], block, seed), describePreset(d, comp, kSwNotes[1], block, seed)};
}

[[nodiscard]] bool swDescriptorSound(const std::array<PresetDescriptor, 2>& p) {
    return p[0].windowValid && p[1].windowValid && p[0].fidelityPass && p[1].fidelityPass;
}

}  // namespace

// =============================================================================
// Per push: mechanism cases on the test-local preset T
// =============================================================================

TEST_CASE("Profundum_SweepTimeline", "[profundum][sweep]") {
    SECTION("makeTimeline: note-off at llround(3 fs), total = note-off + tail, block 512") {
        constexpr Steinberg::uint32 kTail = 61234;
        const SweepTimeline t48 = makeTimeline(48000.0, kTail);
        CHECK(t48.fs == 48000.0);
        CHECK(t48.block == 512u);
        CHECK(t48.noteOff == 144000u);
        CHECK(t48.total == 144000u + kTail);
        const SweepTimeline t44 = makeTimeline(44100.0, kTail);
        CHECK(t44.block == 512u);
        CHECK(t44.noteOff == 132300u);
        CHECK(t44.total == 132300u + kTail);
        const SweepTimeline t96 = makeTimeline(96000.0, kTail);
        CHECK(t96.block == 512u);
        CHECK(t96.noteOff == 288000u);
        CHECK(t96.total == 288000u + kTail);
    }

    SECTION("getTailSamples after setState and setupProcessing equals the Phase 11 formula for T") {
        const std::vector<std::uint8_t> comp = swPresetComp(swStateT(), swStateT());
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        const float rEff = swEffectiveRelease(d);
        for (const double fs : kSwRates) {
            const Steinberg::uint32 expected = swFormulaTail(static_cast<double>(rEff), fs);
            INFO("fs " << fs << ", R " << d.a.ampEnv.releaseMs << " ms, R_eff " << rEff << " ms, expected "
                       << expected);
            // setState, then setupProcessing (the sweep order).
            {
                ProcessorFixture fx;
                REQUIRE(loadStateBytes(*fx.proc, swStateOf(comp)) == Steinberg::kResultOk);
                REQUIRE(fx.prepare(fs, 512) == Steinberg::kResultOk);
                CHECK(fx.proc->getTailSamples() == expected);
            }
            // setupProcessing, then setState: the tail follows the loaded state.
            {
                ProcessorFixture fx;
                REQUIRE(fx.prepare(fs, 512) == Steinberg::kResultOk);
                REQUIRE(loadStateBytes(*fx.proc, swStateOf(comp)) == Steinberg::kResultOk);
                CHECK(fx.proc->getTailSamples() == expected);
            }
        }
    }
}

TEST_CASE("Profundum_PresetMonoSumMechanics", "[profundum][sweep]") {
    namespace sft = Krate::DSP::StereoFieldTest;
    enableFTZDAZ();
    const std::array<std::uint8_t, 1> e1{auRd::kAuE1};

    SECTION("T, 48 kHz E1: W1 valid below f_c, rho and Q bars, 0 anchor mismatches, R' = -L fails") {
        const std::vector<std::uint8_t> comp = swPresetComp(swStateT(), swStateT());
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        const PresetSweepRender r = renderPresetSweep(comp, e1, kSwFs, false);
        CHECK(r.tailSamples == swFormulaTail(static_cast<double>(swEffectiveRelease(d)), kSwFs));
        CHECK(r.timeline.noteOff == 144000u);
        const PresetRenderReadings rd = presetSweepReadings(r, d);
        INFO("f_c " << rd.crossoverHz << " Hz, window " << rd.monoSumWindow << " at " << rd.monoSumStart << ", rho "
                    << rd.monoSum.rho << ", Q " << rd.monoSum.q << " dB, energy " << rd.monoSum.meanEnergy
                    << ", anchor mismatches " << rd.anchorMismatches << " over " << rd.voiceChunks
                    << " chunks, control rho " << rd.controlRho);
        REQUIRE(r.haveGrid);
        CHECK(rd.allFinite);
        CHECK(rd.monoMeasured);
        CHECK(rd.subInBand);
        CHECK(rd.monoSumWindow == 1);
        CHECK(rd.monoSumStart == 72000u);   // 1.5 s at 48 kHz
        CHECK(rd.monoSum.valid);
        CHECK(rd.monoSum.rho >= sft::kSubBandRhoBar);
        CHECK(std::abs(rd.monoSum.q) <= sft::kSubBandQBarDb);
        CHECK(rd.voiceChunks > 0u);
        CHECK(r.anchorPeak > 0.0);   // the identity is not read on silence
        CHECK(rd.anchorMismatches == 0u);
        CHECK(rd.anchorBitIdentical);
        // Non-vacuity: R' = -L on a copy fails the rho bar.
        CHECK(rd.controlFails);
        CHECK(rd.controlRho < sft::kSubBandRhoBar);
    }

    SECTION("SOLID-stored T reads L == R bitwise from s0 = 544") {
        REQUIRE(presetSolidSettleSample(kSwFs) == 544u);   // 64 + llround(10 ms * 48 kHz)
        const std::vector<std::uint8_t> comp = swPresetComp(swStateSolid(), swStateSolid());
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        REQUIRE(presetSolidActive(d));
        const PresetSweepRender r = renderPresetSweep(comp, e1, kSwFs, false);
        const PresetRenderReadings rd = presetSweepReadings(r, d);
        INFO("L != R samples from " << rd.solidFrom << ": " << rd.solidDiffering);
        CHECK(rd.solidActive);
        CHECK(rd.solidFrom == 544u);
        CHECK(rd.solidDiffering == 0u);
        CHECK(rd.solidBitIdentical);
        CHECK(rd.heldRmsDb > -60.0);   // the identity is not read on silence
    }
}

TEST_CASE("Profundum_PresetDeterminismMechanics", "[profundum][sweep]") {
    enableFTZDAZ();
    const double tol = static_cast<double>(Krate::DSP::TestUtils::kSampleTolerance);

    SECTION("DRIFT -> Mass: same seed within tolerance, seed ^ 0x5A5A differs") {
        const std::vector<std::uint8_t> comp = swPresetComp(swStateDrift(), swStateDrift());
        const SwDeterminism m = swDeterminism(comp, true);
        INFO("same seed worst " << m.sameSeed << ", other seed worst " << m.otherSeed << " (tolerance " << tol << ")");
        CHECK(m.sameSeed <= tol);
        CHECK(m.otherSeed > tol);
        CHECK(m.pass);
    }

    SECTION("T (seed-independent): seed and seed ^ 0x5A5A within tolerance") {
        const std::vector<std::uint8_t> comp = swPresetComp(swStateT(), swStateT());
        const SwDeterminism m = swDeterminism(comp, false);
        INFO("other seed worst " << m.otherSeed << " (tolerance " << tol << ")");
        CHECK(m.otherSeed <= tol);
        CHECK(m.pass);
        // The flag is verified in both directions: declaring T seed-dependent contradicts the measurement.
        CHECK_FALSE(swDeterminism(comp, true).pass);
    }
}

TEST_CASE("Profundum_PresetDistinctnessParams", "[profundum][distinct]") {
    namespace P = ::Profundum;

    SECTION("non-vacuity: 2 differing parameters fail the predicate, 3 pass") {
        const Pd::ProfundumPresetDef base = swControlsDef("T019 Params Base", {});
        const Pd::ProfundumPresetDef two =
            swControlsDef("T019 Params Two", {{.id=P::kMacroWeightId, .normalized=0.9}, {.id=P::kMacroDarkBrightId, .normalized=0.9}});
        const Pd::ProfundumPresetDef three = swControlsDef(
            "T019 Params Three", {{.id=P::kMacroWeightId, .normalized=0.9}, {.id=P::kMacroDarkBrightId, .normalized=0.9}, {.id=P::kMacroTightLooseId, .normalized=0.9}});
        const std::vector<SwParamSlot> sBase = swSlotsOf(base);
        const std::vector<SwParamSlot> sTwo = swSlotsOf(two);
        const std::vector<SwParamSlot> sThree = swSlotsOf(three);
        std::string listed;
        const std::size_t nTwo = swDifferingParams(sBase, sTwo, listed);
        INFO("two: " << listed);
        CHECK(nTwo == 2u);
        CHECK_FALSE(nTwo >= 3u);
        const std::size_t nThree = swDifferingParams(sBase, sThree, listed);
        INFO("three: " << listed);
        CHECK(nThree == 3u);
        CHECK(nThree >= 3u);
        CHECK(swDifferingParams(sBase, sBase, listed) == 0u);
    }

    SECTION("library: every pair differs in >= 3 registered parameters") {
        const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
        std::vector<std::vector<SwParamSlot>> slots;
        slots.reserve(defs.size());
        for (const Pd::ProfundumPresetDef& def : defs) {
            slots.push_back(swSlotsOf(def));
        }
        std::size_t fewest = std::numeric_limits<std::size_t>::max();
        std::string fewestPair = "none";
        for (std::size_t i = 0; i < defs.size(); ++i) {
            for (std::size_t j = i + 1; j < defs.size(); ++j) {
                std::string listed;
                const std::size_t n = swDifferingParams(slots[i], slots[j], listed);
                INFO(defs[i].name << " vs " << defs[j].name << ": " << n << " (" << listed << ")");
                CHECK(n >= 3u);
                if (n < fewest) {
                    fewest = n;
                    fewestPair = std::string(defs[i].name) + " vs " + std::string(defs[j].name);
                }
            }
        }
        std::cout << "# Profundum_PresetDistinctnessParams: presets=" << defs.size() << " fewest="
                  << (defs.size() < 2u ? 0u : fewest) << " pair=" << fewestPair << '\n'
                  << std::flush;
    }
}

// =============================================================================
// [long] library cases
// =============================================================================

TEST_CASE("Profundum_PresetSweep", "[profundum][sweep][long]") {
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    CHECK(kPresetChordNotes.size() == 3u);   // FR-063 / SC-032: exactly E1, B1, E2
    CHECK(kPresetChordNotes[0] == 28);
    CHECK(kPresetChordNotes[1] == 35);
    CHECK(kPresetChordNotes[2] == 40);
    std::cout << "# Profundum_PresetSweep: presets=" << defs.size() << "\npreset,render,non_finite,held_rms_db,"
              << "tail_rms_db,tail_samples\n";
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const DecodedPresetState d = decodePresetState(presetCompOf(defs[i]));
        REQUIRE(d.ok);
        std::vector<std::pair<std::string, const PresetRenderReadings*>> renders;
        renders.reserve(kSwNotes.size() + 1u);   // + the POLY chord
        for (const std::uint8_t note : kSwNotes) {
            renders.emplace_back(auRd::auNoteName(note), &presetReadings(i, note, kSwFs, kPresetVariantStored));
        }
        if (d.voiceMode == 1) {   // Poly: the FR-063 chord
            renders.emplace_back("chord E1+B1+E2", &presetReadings(i, auRd::kAuE1, kSwFs, kPresetVariantChord));
        }
        for (const auto& [label, r] : renders) {
            std::cout << auRd::auCsvField(std::string(defs[i].name)) << ',' << label << ',' << r->nonFinite << ','
                      << r->heldRmsDb << ',' << r->tailRmsDb << ',' << r->tailSamples << '\n';
            INFO(defs[i].name << " " << label << ": non-finite " << r->nonFinite << ", held " << r->heldRmsDb
                              << " dBFS, tail " << r->tailRmsDb << " dBFS");
            CHECK(r->allFinite);
            CHECK(r->heldRmsDb > -60.0);
            CHECK(r->tailRmsDb <= -100.0);
        }
    }
    std::cout << std::flush;
}

TEST_CASE("Profundum_PresetSweepRates", "[profundum][sweep][long]") {
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    double worstMargin = std::numeric_limits<double>::infinity();
    std::string worstAt = "none";
    std::cout << "# Profundum_PresetSweepRates: presets=" << defs.size()
              << "\npreset,fs,non_finite,held_rms_db,tail_rms_db,true_peak_dbtp,margin_db\n";
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const DecodedPresetState d = decodePresetState(presetCompOf(defs[i]));
        REQUIRE(d.ok);
        const double bar = static_cast<double>(d.outputCeilingDb) + 0.1;
        for (const double fs : kSwRates) {
            const PresetRenderReadings& r = presetReadings(i, auRd::kAuE1, fs, kPresetVariantStored);
            const double margin = bar - r.truePeakDb;
            std::cout << auRd::auCsvField(std::string(defs[i].name)) << ',' << fs << ',' << r.nonFinite << ','
                      << r.heldRmsDb << ',' << r.tailRmsDb << ',' << r.truePeakDb << ',' << margin << '\n';
            INFO(defs[i].name << " E1 at " << fs << " Hz: true peak " << r.truePeakDb << " dBTP (bar " << bar
                              << "), held " << r.heldRmsDb << ", tail " << r.tailRmsDb);
            CHECK(r.allFinite);
            CHECK(r.heldRmsDb > -60.0);
            CHECK(r.tailRmsDb <= -100.0);
            CHECK(r.truePeakDb <= bar);
            if (margin < worstMargin) {
                worstMargin = margin;
                worstAt = std::string(defs[i].name) + " at " + std::to_string(fs);
            }
        }
    }
    std::cout << "# worst true-peak margin " << worstMargin << " dB (" << worstAt << ")\n" << std::flush;
}

TEST_CASE("Profundum_PresetTruePeakMaxMass", "[profundum][sweep][long]") {
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    double worstMargin = std::numeric_limits<double>::infinity();
    std::cout << "# Profundum_PresetTruePeakMaxMass: presets=" << defs.size()
              << "\npreset,note,non_finite,true_peak_dbtp,margin_db\n";
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const DecodedPresetState d = decodePresetState(presetCompOf(defs[i]));
        REQUIRE(d.ok);
        const double bar = static_cast<double>(d.outputCeilingDb) + 0.1;
        for (const std::uint8_t note : kSwNotes) {
            const PresetRenderReadings& r = presetReadings(i, note, kSwFs, kPresetVariantMaxMass);
            const double margin = bar - r.truePeakDb;
            worstMargin = std::min(worstMargin, margin);
            std::cout << auRd::auCsvField(std::string(defs[i].name)) << ',' << auRd::auNoteName(note) << ','
                      << r.nonFinite << ',' << r.truePeakDb << ',' << margin << '\n';
            INFO(defs[i].name << " " << auRd::auNoteName(note) << " at max Mass: true peak " << r.truePeakDb
                              << " dBTP (bar " << bar << "), non-finite " << r.nonFinite);
            CHECK(r.allFinite);
            CHECK(r.truePeakDb <= bar);
        }
    }
    std::cout << "# worst true-peak margin " << worstMargin << " dB\n" << std::flush;
}

TEST_CASE("Profundum_PresetMonoSum", "[profundum][mono][sweep][long]") {
    namespace sft = Krate::DSP::StereoFieldTest;
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    std::size_t ungated = 0;
    std::cout << "# Profundum_PresetMonoSum: presets=" << defs.size()
              << "\npreset,fc_hz,window,start,valid,rho,q_db,anchor_mismatches,solid_active,solid_differing,"
              << "control_rho\n";
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const PresetRenderReadings& r = presetReadings(i, auRd::kAuE1, kSwFs, kPresetVariantStored);
        const bool gated = r.monoMeasured && r.monoSumWindow != 0 && r.subInBand;
        std::cout << auRd::auCsvField(std::string(defs[i].name)) << ',' << r.crossoverHz << ',' << r.monoSumWindow
                  << ',' << r.monoSumStart << ',' << (r.monoSum.valid ? 1 : 0) << ',' << r.monoSum.rho << ','
                  << r.monoSum.q << ',' << r.anchorMismatches << ',' << (r.solidActive ? 1 : 0) << ','
                  << r.solidDiffering << ',' << r.controlRho << '\n';
        INFO(defs[i].name << ": f_c " << r.crossoverHz << " Hz, window " << r.monoSumWindow << ", rho "
                          << r.monoSum.rho << ", Q " << r.monoSum.q << " dB, anchor mismatches "
                          << r.anchorMismatches << ", SOLID " << r.solidActive << " (" << r.solidDiffering
                          << " differing from " << r.solidFrom << "), control rho " << r.controlRho);
        CHECK(gated);   // invalid in both windows (or E1 not below f_c) fails the preset, never skipped
        if (!gated) {
            ++ungated;
        } else {
            CHECK(r.monoSum.rho >= sft::kSubBandRhoBar);
            CHECK(std::abs(r.monoSum.q) <= sft::kSubBandQBarDb);
        }
        CHECK(r.anchorBitIdentical);
        if (r.solidActive) {
            CHECK(r.solidBitIdentical);
        }
        CHECK(r.controlFails);
    }
    std::cout << "# ungated=" << ungated << '\n' << std::flush;
    CHECK(ungated == 0u);
}

TEST_CASE("Profundum_PresetDeterminism", "[profundum][determinism][sweep][long]") {
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    std::size_t contradicting = 0;
    std::cout << "# Profundum_PresetDeterminism: presets=" << defs.size()
              << "\npreset,seed_dependent,same_seed_worst,other_seed_worst,pass\n";
    for (const Pd::ProfundumPresetDef& def : defs) {
        const SwDeterminism m = swDeterminism(presetCompOf(def), def.seedDependent);
        std::cout << auRd::auCsvField(std::string(def.name)) << ',' << (def.seedDependent ? 1 : 0) << ','
                  << m.sameSeed << ',' << m.otherSeed << ',' << (m.pass ? 1 : 0) << '\n';
        INFO(def.name << ": the measurement contradicts seedDependent = " << (def.seedDependent ? "true" : "false")
                      << " (same seed worst " << m.sameSeed << ", seed ^ 0x5A5A worst " << m.otherSeed << ")");
        CHECK(m.pass);
        if (!m.pass) {
            ++contradicting;
        }
    }
    std::cout << "# contradicting=" << contradicting << '\n' << std::flush;
}

TEST_CASE("Profundum_PresetLoudnessMatch", "[profundum][sweep][long]") {
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    std::vector<double> level(defs.size());
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const PresetRenderReadings& r = presetReadings(i, auRd::kAuE1, kSwFs, kPresetVariantStored);
        INFO(defs[i].name << ": descriptor window at " << r.windowStart << " reads " << r.midTotalDb << " dBFS");
        CHECK(r.windowValid);
        level[i] = r.midTotalDb;
    }
    std::vector<double> sorted = level;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t n = sorted.size();
    const double median = n % 2u == 1u ? sorted[n / 2u] : 0.5 * (sorted[n / 2u - 1u] + sorted[n / 2u]);
    double worst = 0.0;
    std::cout << "# Profundum_PresetLoudnessMatch: presets=" << n << " median_db=" << median
              << "\npreset,level_db,deviation_db\n";
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const double dev = level[i] - median;
        worst = std::max(worst, std::abs(dev));
        std::cout << auRd::auCsvField(std::string(defs[i].name)) << ',' << level[i] << ',' << dev << '\n';
        INFO(defs[i].name << ": " << level[i] << " dBFS, deviation " << dev << " dB from the median " << median);
        CHECK(std::abs(dev) <= 4.0);
    }
    std::cout << "# worst deviation " << worst << " dB\n" << std::flush;
}

TEST_CASE("Profundum_PresetDistinctness", "[profundum][distinct][long]") {
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    const std::map<std::string, std::string> kv = swReadKeyValues(swDistinctnessCsv());
    INFO("evidence/distinctness.csv: " << swDistinctnessCsv().string());
    REQUIRE(kv.contains("F"));
    REQUIRE(kv.contains("near_variant_pass"));
    REQUIRE(kv.contains("control_pass"));
    REQUIRE(kv.at("near_variant_pass") == "1");   // F is void unless both pilot controls passed
    REQUIRE(kv.at("control_pass") == "1");
    const double f = std::stod(kv.at("F"));
    REQUIRE(f >= 1.0);
    // Library floor from the designed-library pass (2026-10-26): the closest pair rose from 1.91 (Bedrock vs Locked
    // Floor) to 2.42 (Loose Ends vs Locked Floor); no pair may fall back under 2.0, whatever the measured F.
    constexpr double kLibraryDistanceFloor = 2.0;
    const double bar = std::max(f, kLibraryDistanceFloor);

    std::vector<std::array<PresetDescriptor, 2>> desc;
    desc.reserve(defs.size());
    for (const Pd::ProfundumPresetDef& def : defs) {
        desc.push_back(swDescribe(presetCompOf(def), 512, kPresetStoredSeed));
        const auto& p = desc.back();
        INFO(def.name << ": E1 window " << p[0].midTotalDb << " dBFS (valid " << p[0].windowValid << "), C3 window "
                      << p[1].midTotalDb << " dBFS (valid " << p[1].windowValid << "), fidelity E1 "
                      << p[0].fidelityPass << " C3 " << p[1].fidelityPass);
        CHECK(p[0].windowValid);   // no descriptor window below -60 dBFS (SC-020)
        CHECK(p[1].windowValid);
        CHECK(p[0].fidelityPass);   // every dNl twin's fidelity arm
        CHECK(p[1].fidelityPass);
    }

    double closest = std::numeric_limits<double>::infinity();
    std::string closestPair = "none";
    std::size_t closestMasked = 0;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        for (std::size_t j = i + 1; j < defs.size(); ++j) {
            std::size_t masked = 0;
            const double dist = presetDistance(desc[i], desc[j], masked);
            INFO(defs[i].name << " vs " << defs[j].name << ": d " << dist << " (F " << f << ", bar " << bar
                              << "), masked " << masked);
            CHECK(dist >= bar);
            if (dist < closest) {
                closest = dist;
                closestPair = std::string(defs[i].name) + " vs " + std::string(defs[j].name);
                closestMasked = masked;
            }
        }
    }
    std::cout << "# Profundum_PresetDistinctness: presets=" << defs.size() << " F=" << f << " bar=" << bar
              << " closest=" << closestPair
              << " d=" << closest << " masked=" << closestMasked << '\n'
              << std::flush;
}

// =============================================================================
// Morph endpoints (2026-10-26, the designed-library pass): every preset's B is a second sound, not a nudge of A, and Bass
// Morph does not dip or jump in level on the way. B is rendered as a preset whose two endpoints are both B (the audition
// render's _B_E1 file), described with the same C-7 descriptor as A.
// =============================================================================

namespace {

// C-7 distance between A and B. Calibrated 2026-10-26 on the library before the pass: the designed B endpoints (the six
// pilots, Bedrock Sine, Morph Span, Halfway Morph) read 11.2 - 30.7; the 49 B endpoints that nudged one or two
// parameters read 0 - 8.0 (Biting Reed 7.35, Dark To Bright 8.03). The library F is 1.0.
constexpr double kMorphEndpointMinDistance = 10.0;
constexpr double kMorphEndpointMaxLevelDeltaDb = 3.0;  // |B - A| descriptor-window level, per note

void swRunMorphEndpointShard(std::size_t k) {
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    std::ostringstream os;
    os << std::setprecision(6);
    os << "preset,d_ab,masked,level_a_e1,level_b_e1,level_a_c3,level_b_c3\n";
    std::size_t presets = 0;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (i % 8u != k) {
            continue;
        }
        const Pd::ProfundumPresetDef& def = defs[i];
        const std::vector<std::uint8_t> comp = presetCompOf(def);
        const DecodedPresetState d = decodePresetState(comp);
        INFO("preset " << def.name);
        REQUIRE(d.ok);
        const std::vector<std::uint8_t> compB = swBytesOf(withEndpoints(swStateOf(comp), d.b, d.b));
        // R8 (Phase 14, user 2026-10-27): a preset that stores a Morph Mod source is measured at its two designed sounds
        // with the modulation bypassed. A, like B, is then rendered with both endpoints A, so the source cannot move it;
        // a Source-Off preset is rendered as stored, as before.
        const std::vector<std::uint8_t> compA =
            d.morphMod.source != 0 ? swBytesOf(withEndpoints(swStateOf(comp), d.a, d.a)) : comp;
        const std::array<PresetDescriptor, 2> descA = swDescribe(compA, 512, kPresetStoredSeed);
        const std::array<PresetDescriptor, 2> descB = swDescribe(compB, 512, kPresetStoredSeed);
        CHECK(swDescriptorSound(descA));
        CHECK(swDescriptorSound(descB));
        std::size_t masked = 0;
        const double dist = presetDistance(descA, descB, masked);
        ++presets;
        os << auRd::auCsvField(std::string(def.name)) << ',' << dist << ',' << masked << ',' << descA[0].midTotalDb << ','
           << descB[0].midTotalDb << ',' << descA[1].midTotalDb << ',' << descB[1].midTotalDb << '\n';
        INFO("A vs B: d " << dist << " (bar " << kMorphEndpointMinDistance << "), masked " << masked);
        CHECK(dist >= kMorphEndpointMinDistance);
        for (std::size_t n = 0; n < kSwNotes.size(); ++n) {
            INFO(auRd::auNoteName(kSwNotes[n]) << ": A " << descA[n].midTotalDb << " dBFS, B " << descB[n].midTotalDb
                                               << " dBFS");
            CHECK(std::abs(descB[n].midTotalDb - descA[n].midTotalDb) <= kMorphEndpointMaxLevelDeltaDb);
        }
    }
    std::cout << "# Profundum_PresetMorphEndpoints_" << k << "of8: presets=" << presets << '\n'
              << os.str() << std::flush;
}

}  // namespace

TEST_CASE("Profundum_PresetMorphEndpoints_0of8", "[profundum][distinct][long]") { swRunMorphEndpointShard(0); }
TEST_CASE("Profundum_PresetMorphEndpoints_1of8", "[profundum][distinct][long]") { swRunMorphEndpointShard(1); }
TEST_CASE("Profundum_PresetMorphEndpoints_2of8", "[profundum][distinct][long]") { swRunMorphEndpointShard(2); }
TEST_CASE("Profundum_PresetMorphEndpoints_3of8", "[profundum][distinct][long]") { swRunMorphEndpointShard(3); }
TEST_CASE("Profundum_PresetMorphEndpoints_4of8", "[profundum][distinct][long]") { swRunMorphEndpointShard(4); }
TEST_CASE("Profundum_PresetMorphEndpoints_5of8", "[profundum][distinct][long]") { swRunMorphEndpointShard(5); }
TEST_CASE("Profundum_PresetMorphEndpoints_6of8", "[profundum][distinct][long]") { swRunMorphEndpointShard(6); }
TEST_CASE("Profundum_PresetMorphEndpoints_7of8", "[profundum][distinct][long]") { swRunMorphEndpointShard(7); }

// =============================================================================
// Hidden pilot (plan 5.8 steps 1-5): writes evidence/distinctness.csv
// =============================================================================

TEST_CASE("Profundum_PresetDistinctnessPilot", "[profundum][distinct][.pilot]") {
    namespace P = ::Profundum;
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    const std::uint32_t twinSeed = Krate::DSP::ProfundumEngine::kDefaultSeed ^ kPresetSeedTwinXor;

    // 1. Twins: the seed twin over the seed-independent presets, the block twin (37 vs 512) over every preset.
    double tMax = 0.0;
    std::string tMaxTwin = "none";
    std::ostringstream twins;
    twins << std::setprecision(9);
    twins << "preset,twin,d,masked\n";
    for (const Pd::ProfundumPresetDef& def : defs) {
        const std::vector<std::uint8_t> comp = presetCompOf(def);
        const std::array<PresetDescriptor, 2> base = swDescribe(comp, 512, kPresetStoredSeed);
        INFO(def.name << ": base descriptor window valid E1 " << base[0].windowValid << " C3 " << base[1].windowValid);
        CHECK(swDescriptorSound(base));
        const auto twin = [&](const char* kind, const std::array<PresetDescriptor, 2>& t) {
            std::size_t masked = 0;
            const double d = presetDistance(base, t, masked);
            twins << auRd::auCsvField(std::string(def.name)) << ',' << kind << ',' << d << ',' << masked << '\n';
            if (d > tMax) {
                tMax = d;
                tMaxTwin = std::string(def.name) + ":" + kind;
            }
        };
        if (!def.seedDependent) {
            const DecodedPresetState d = decodePresetState(comp);
            REQUIRE(d.ok);
            INFO(def.name << ": the seed twin must differ from the stored seed " << d.seed);
            CHECK(d.seed != twinSeed);
            twin("seed", swDescribe(comp, 512, twinSeed));
        }
        twin("block37", swDescribe(comp, 37, kPresetStoredSeed));
    }

    // 2. F = max(2.5 t_max, 1.0).
    const double f = presetDistinctnessThreshold(tMax);

    // 3. Near-variant: "Bedrock Sine" with Mass (104) on endpoint A raised by 0.05 normalized reads d < F.
    const auto pilotIt = std::find_if(defs.begin(), defs.end(),
                                      [](const Pd::ProfundumPresetDef& def) { return def.name == "Bedrock Sine"; });
    REQUIRE(pilotIt != defs.end());
    REQUIRE_FALSE(pilotIt->seedDependent);
    const std::vector<std::uint8_t> pilotComp = presetCompOf(*pilotIt);
    const DecodedPresetState pilot = decodePresetState(pilotComp);
    REQUIRE(pilot.ok);
    const P::SoundParamRow* massRow = P::soundRowOf(P::kMassId);
    REQUIRE(massRow != nullptr);
    REQUIRE(P::lawOf(massRow->leaf).kind == SwMS::Kind::F32);
    P::LeafBits massBits = P::leafBitsOf(pilot.a);
    const double massFrom = P::normalizedFromLeafWord(massRow->leaf, massBits[massRow->leaf]);
    REQUIRE(massFrom + 0.05 <= 1.0);
    const double massTo = massFrom + 0.05;
    massBits[massRow->leaf] = P::leafWordFromNormalized(massRow->leaf, massTo);
    const SwMS nearA = P::morphStateFromLeafBits(massBits);
    REQUIRE(Krate::DSP::isValidMorphState(nearA));
    const std::vector<std::uint8_t> nearComp = swBytesOf(withEndpoints(swStateOf(pilotComp), nearA, pilot.b));
    std::size_t nearMasked = 0;
    const double nearD = presetDistance(swDescribe(pilotComp, 512, kPresetStoredSeed),
                                        swDescribe(nearComp, 512, kPresetStoredSeed), nearMasked);
    const bool nearPass = nearD < f;

    // 4. Positive control: showcasePair() endpoints, each as a single-endpoint preset, read d >= F.
    const std::pair<SwMS, SwMS> pair = Krate::DSP::BassMorphTest::showcasePair();
    std::size_t controlMasked = 0;
    const double controlD = presetDistance(swDescribe(swPresetComp(pair.first, pair.first), 512, kPresetStoredSeed),
                                           swDescribe(swPresetComp(pair.second, pair.second), 512, kPresetStoredSeed),
                                           controlMasked);
    const bool controlPass = controlD >= f;

    // 5. Record.
    std::ostringstream os;
    os << std::setprecision(9);
    os << "key,value\n";
    os << "F," << f << '\n';
    os << "t_max," << tMax << '\n';
    os << "t_max_twin," << auRd::auCsvField(tMaxTwin) << '\n';
    os << "near_variant_preset,Bedrock Sine\n";
    os << "near_variant_parameter,Mass (104) endpoint A " << massFrom << " -> " << massTo << " normalized\n";
    os << "near_variant_d," << nearD << '\n';
    os << "near_variant_masked," << nearMasked << '\n';
    os << "near_variant_pass," << (nearPass ? 1 : 0) << '\n';
    os << "control,showcasePair() first vs second (single-endpoint presets)\n";
    os << "control_d," << controlD << '\n';
    os << "control_masked," << controlMasked << '\n';
    os << "control_pass," << (controlPass ? 1 : 0) << '\n';
    const std::string csv = os.str();
    std::cout << "# Profundum_PresetDistinctnessPilot: presets=" << defs.size() << "\n# twins\n"
              << twins.str() << "# distinctness.csv\n"
              << csv << std::flush;
    const std::filesystem::path path = swDistinctnessCsv();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << csv;
    CHECK(out.good());

    INFO("F " << f << " (t_max " << tMax << " from " << tMaxTwin << "); near-variant d " << nearD << ", control d "
              << controlD);
    CHECK(nearPass);      // a failing control voids F: stop and surface, F is never lowered
    CHECK(controlPass);
}

// =============================================================================
// T022 (FR-050; SC-023; C-9 rule 5; plan 6 rule 5, 8 "Audition"): the hidden audition render set
// =============================================================================
//
// Profundum_PresetAuditionRenders ([.listen], hidden; run by evidence/listen/render.sh) writes, under
// specs/profundum-phase13-presets-release/evidence/listen/:
//   - <category>/<name>_E1.wav and _C3.wav: the plan 5.5 sweep render (renderPresetSweep, 48 kHz, block 512, note on at
//     0, off at 3 s, rendered through the reported tail), 32-bit float stereo as the Phase 12 case writes;
//   - <category>/<name>_B_E1.wav: endpoint B on its own (both endpoints = B), rendered like _E1; the sheet records the
//     held E1 level (mid RMS over [1.0 s, 3.0 s)) of A and of B;
//   - gesture WAVs (48 kHz, block 512, the stored state, tail = getTailSamples() after setupProcessing):
//       _morph   (every preset: Morph is part of each preset's sound): E1 held 3 s, Morph Position (105) 0 -> 1
//                linearly over 2 s from sample 0, one point per host block;
//       _macro   (AT-47 - AT-50 claimants): the claimed macro 0 -> 1 the same way (one file per claimed macro;
//                _macro_AT-nn when a preset claims more than one);
//       _legato  (AT-23, AT-43 - AT-46 claimants): E1 on at 0, E2 on at 1.0 s, E1 off at 1.5 s, E2 off at 3.0 s;
//   - manifest.txt: one line per WAV, "file,preset,note,gesture" (gesture "none" for the E1 / C3 renders, last
//     field); lines starting with '#' are comments;
//   - listening-sheet.md: one block per preset ending in an empty "Verdict (user):" line (an agent never fills it);
//     a sheet that already holds a filled verdict is copied to listening-sheet.prev<k>.md before it is replaced;
//   - fundamental-loss.csv and one "Fundamental-loss note (C-9 rule 5):" line per fundamentalLossFlagged preset:
//     delta_fund = E(stored) - E(Amount 0), E = auWindowESubDb(outL, outR, start, length, f0(E1)) over the plan 5.8
//     descriptor window of the stored E1 48 kHz render ([1.0 s, 3.0 s) if its mid total >= -60 dBFS, else
//     [t90, t90 + 2^16)); the reference arm is the same Comp with Character Amount 0 on both endpoints
//     (withEndpoints), read over the same window.
// The only assertions are finiteness, the file writes and the counts; the last stdout line is
// "# Profundum_PresetAuditionRenders: presets=N wavs=W gestures=G flagged=K", which render.sh cross-checks.

namespace {

[[nodiscard]] std::filesystem::path swListenDir() {
    return {std::string(KRATE_DSP_TESTS_DIR) +
                                 "/../../specs/profundum-phase13-presets-release/evidence/listen"};
}

// TU-local WAV writer (the Phase 12 main_page_render_set_test.cpp writer: IEEE float, 2 channels, 32-bit).
void swPutU16(std::vector<unsigned char>& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xFFu));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
}

void swPutU32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFFu));
    }
}

void swPutTag(std::vector<unsigned char>& b, const char* tag) {
    for (std::size_t i = 0; i < 4; ++i) {
        b.push_back(static_cast<unsigned char>(tag[i]));
    }
}

[[nodiscard]] bool swWriteWav(const std::filesystem::path& path, const std::vector<float>& L,
                              const std::vector<float>& R, double fs) {
    const std::size_t frames = std::min(L.size(), R.size());
    const std::uint64_t dataBytes = static_cast<std::uint64_t>(frames) * 8u;
    if (dataBytes > 0xFFFF0000ull) {
        return false;
    }
    const auto sr = static_cast<std::uint32_t>(std::llround(fs));
    std::vector<unsigned char> b;
    b.reserve(static_cast<std::size_t>(dataBytes) + 64u);
    swPutTag(b, "RIFF");
    swPutU32(b, static_cast<std::uint32_t>(4u + (8u + 18u) + (8u + 4u) + 8u + dataBytes));
    swPutTag(b, "WAVE");
    swPutTag(b, "fmt ");
    swPutU32(b, 18u);
    swPutU16(b, 3u);       // WAVE_FORMAT_IEEE_FLOAT
    swPutU16(b, 2u);       // channels
    swPutU32(b, sr);
    swPutU32(b, sr * 8u);  // byte rate
    swPutU16(b, 8u);       // block align
    swPutU16(b, 32u);      // bits per sample
    swPutU16(b, 0u);       // cbSize
    swPutTag(b, "fact");
    swPutU32(b, 4u);
    swPutU32(b, static_cast<std::uint32_t>(frames));
    swPutTag(b, "data");
    swPutU32(b, static_cast<std::uint32_t>(dataBytes));
    for (std::size_t i = 0; i < frames; ++i) {
        swPutU32(b, std::bit_cast<std::uint32_t>(L[i]));
        swPutU32(b, std::bit_cast<std::uint32_t>(R[i]));
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

[[nodiscard]] std::size_t swNonFinite(const std::vector<float>& L, const std::vector<float>& R) {
    std::size_t n = 0;
    for (const float v : L) {
        n += presetFloatIsFinite(v) ? 0u : 1u;
    }
    for (const float v : R) {
        n += presetFloatIsFinite(v) ? 0u : 1u;
    }
    return n;
}

[[nodiscard]] bool swClaims(const Pd::ProfundumPresetDef& def, Pd::Capability c) {
    return def.primary == c || std::find(def.secondaries.begin(), def.secondaries.end(), c) != def.secondaries.end();
}

[[nodiscard]] std::string_view swLabelOf(Pd::Capability c) {
    return Pd::capabilityTable()[static_cast<std::size_t>(c)].label;
}

/// "AT-nn" of a capability label (its first word).
[[nodiscard]] std::string swAtOf(Pd::Capability c) {
    const std::string_view label = swLabelOf(c);
    return std::string(label.substr(0, label.find(' ')));
}

[[nodiscard]] const char* swAlgorithmName(Krate::DSP::BassCharacterStage::Algorithm a) noexcept {
    using Alg = Krate::DSP::BassCharacterStage::Algorithm;
    switch (a) {
        case Alg::Tube: return "Tube";
        case Alg::Fold: return "Fold";
        case Alg::Rectify: return "Rectify";
        case Alg::Bite: return "Bite";
        case Alg::Crush: return "Crush";
        case Alg::Fracture: return "Fracture";
    }
    return "?";
}

/// The fundamentalLossFlagged predicate on one endpoint (preset_test_support.h), for naming the flagged endpoint.
[[nodiscard]] bool swLossyEndpoint(const SwMS& s) noexcept {
    using Alg = Krate::DSP::BassCharacterStage::Algorithm;
    const bool alg = s.characterAlgorithm == Alg::Rectify || s.characterAlgorithm == Alg::Crush ||
                     s.characterAlgorithm == Alg::Fracture;
    return alg && s.characterAmount >= 0.9f && s.anchor.level < 0.25f;
}

/// One audition render: the stored Comp, setupProcessing(48 kHz, 512), then the script to @p noteOff plus the
/// reported tail.
struct SwAuditionRender {
    std::vector<float> L, R;
    std::size_t nonFinite = 0;
};

using SwEv = ProcessorFixture::ScriptedEvent;

[[nodiscard]] SwAuditionRender swRenderGesture(std::span<const std::uint8_t> comp, const std::vector<SwEv>& script,
                                               std::size_t noteOff) {
    REQUIRE(comp.size() == ::Profundum::kStateV3Bytes);
    auto fx = std::make_unique<ProcessorFixture>();
    REQUIRE(loadStateBytes(*fx->proc, swStateOf(comp)) == Steinberg::kResultOk);
    constexpr std::size_t kBlock = 512;
    REQUIRE(fx->prepare(kSwFs, static_cast<Steinberg::int32>(kBlock)) == Steinberg::kResultOk);
    const std::size_t total = noteOff + static_cast<std::size_t>(fx->proc->getTailSamples());
    fx->renderScript(script, total, kBlock);
    REQUIRE(fx->proc->endpointRejectionsForTest() == 0u);
    fx->proc->setActive(0u);
    REQUIRE(fx->capturedL.size() == total);
    REQUIRE(fx->capturedR.size() == total);
    SwAuditionRender out;
    out.L = std::move(fx->capturedL);
    out.R = std::move(fx->capturedR);
    out.nonFinite = swNonFinite(out.L, out.R);
    return out;
}

[[nodiscard]] std::size_t swAt(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds * kSwFs));
}

[[nodiscard]] SwEv swNoteOn(std::size_t at, std::uint8_t note) {
    return SwEv{.sample = static_cast<Steinberg::int64>(at),
                .kind = SwEv::Kind::NoteOn,
                .pitch = static_cast<Steinberg::int16>(note),
                .velocity = static_cast<float>(auRd::kAuVelocity) / 127.0f};
}

[[nodiscard]] SwEv swNoteOff(std::size_t at, std::uint8_t note) {
    return SwEv{.sample = static_cast<Steinberg::int64>(at),
                .kind = SwEv::Kind::NoteOff,
                .pitch = static_cast<Steinberg::int16>(note)};
}

/// E1 held 3 s; parameter @p id (plain 0..1 == normalized: Morph Position 105 and the four macros) ramps 0 -> 1
/// linearly over 2 s from sample 0, one point per 512-sample host block.
[[nodiscard]] std::vector<SwEv> swRampScript(Steinberg::Vst::ParamID id) {
    const std::size_t rampLen = swAt(2.0);
    std::vector<SwEv> script;
    script.push_back(swNoteOn(0, auRd::kAuE1));
    for (std::size_t s = 0;; s += 512u) {
        const double v = std::min(1.0, static_cast<double>(s) / static_cast<double>(rampLen));
        SwEv p{};
        p.sample = static_cast<Steinberg::int64>(s);
        p.kind = SwEv::Kind::Param;
        p.id = id;
        p.value = v;
        script.push_back(p);
        if (v >= 1.0) {
            break;
        }
    }
    script.push_back(swNoteOff(swAt(3.0), auRd::kAuE1));
    return script;
}

/// The plan 5.8 descriptor window of a 48 kHz sweep render: [1.0 s, 3.0 s) if its mid total is >= -60 dBFS,
/// else [t90, t90 + 2^16) (the describePreset rule, preset_render_support.h).
struct SwWindow {
    std::size_t start = 0;
    std::size_t length = 0;
    double midTotalDb = -200.0;
};

[[nodiscard]] SwWindow swDescriptorWindow(const PresetSweepRender& r) {
    namespace dd = preset_descriptor_detail;
    REQUIRE(r.timeline.fs == auRd::kAuFs);
    const std::size_t heldFrom = swAt(1.0);
    SwWindow w;
    w.start = heldFrom;
    w.length = r.timeline.noteOff - heldFrom;
    w.midTotalDb = dd::midTotalDb(dd::bandsOver(r.L, r.R, w.start, w.length));
    if (w.midTotalDb < dd::kFloorDb) {
        w.start = dd::t90Sample(r.L, r.R, r.timeline.noteOff);
        w.length = dd::kFallbackLength;
        w.midTotalDb = dd::midTotalDb(dd::bandsOver(r.L, r.R, w.start, w.length));
    }
    return w;
}

/// The held level of a 48 kHz sweep render: mid ((L + R) / 2) RMS in dBFS over [1.0 s, 3.0 s), the held part of
/// the note (on at 0, off at 3 s). -200 for silence.
[[nodiscard]] double swHeldLevelDb(const std::vector<float>& L, const std::vector<float>& R) {
    const std::size_t from = swAt(1.0);
    const std::size_t to = std::min({swAt(3.0), L.size(), R.size()});
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        const double mid = 0.5 * (static_cast<double>(L[i]) + static_cast<double>(R[i]));
        sum += mid * mid;
    }
    const double meanSquare = to > from ? sum / static_cast<double>(to - from) : 0.0;
    return meanSquare > 0.0 ? 10.0 * std::log10(meanSquare) : -200.0;
}

/// Keeps a sheet the user has written verdicts into: copied to listening-sheet.prev<k>.md (first free k).
void swKeepFilledSheet(const std::filesystem::path& sheet) {
    std::ifstream in(sheet, std::ios::binary);
    if (!in) {
        return;
    }
    constexpr std::string_view kVerdict = "Verdict (user):";
    bool filled = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.starts_with(kVerdict) && line.find_first_not_of(" \t\r", kVerdict.size()) != std::string::npos) {
            filled = true;
            break;
        }
    }
    in.close();
    if (!filled) {
        return;
    }
    for (int k = 1;; ++k) {
        const std::filesystem::path keep = sheet.parent_path() / ("listening-sheet.prev" + std::to_string(k) + ".md");
        if (!std::filesystem::exists(keep)) {
            std::filesystem::copy_file(sheet, keep);
            std::cout << "# kept the filled listening sheet as " << keep.string() << '\n';
            return;
        }
    }
}

}  // namespace

TEST_CASE("Profundum_PresetAuditionRenders", "[profundum][.listen]") {
    namespace P = ::Profundum;
    using C = Pd::Capability;
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());

    const std::filesystem::path dir = swListenDir();
    std::filesystem::create_directories(dir);
    const std::filesystem::path sheetPath = dir / "listening-sheet.md";
    swKeepFilledSheet(sheetPath);

    std::ostringstream manifest;
    manifest << "# Profundum_PresetAuditionRenders (T022): 48 kHz, block 512, velocity "
             << static_cast<int>(auRd::kAuVelocity)
             << "/127, 32-bit float stereo; E1 / C3: note on at 0, off at 3 s, rendered through the reported tail; "
                "_morph / _macro: E1 held 3 s, the parameter 0 -> 1 over 2 s; _legato: E1 0-1.5 s, E2 1.0-3.0 s\n"
             << "# file,preset,note,gesture\n";
    std::ostringstream sheet;
    sheet << std::setprecision(4);
    sheet << "# Profundum factory library: listening sheet\n\n"
          << "Generated by `Profundum_PresetAuditionRenders` (T022, FR-050). One block per preset; the files are "
             "listed in `manifest.txt`. The verdict lines are the user's: an agent never fills them.\n";
    std::ostringstream loss;
    loss << std::setprecision(6);
    loss << "preset,endpoint,algorithm,amount,anchor_level,e_stored_db,e_amount0_db,delta_fund_db\n";

    std::size_t wavs = 0;
    std::size_t gestures = 0;
    std::size_t flagged = 0;
    const std::array<std::uint8_t, 1> e1{auRd::kAuE1};
    const double f0 = auRd::auHz(auRd::kAuE1);
    const std::array<std::pair<C, Steinberg::Vst::ParamID>, 4> macroClaims{{{C::AT47, P::kMacroWeightId},
                                                                            {C::AT48, P::kMacroDarkBrightId},
                                                                            {C::AT49, P::kMacroTightLooseId},
                                                                            {C::AT50, P::kMacroCleanViolentId}}};

    for (const Pd::ProfundumPresetDef& def : defs) {
        const std::vector<std::uint8_t> comp = presetCompOf(def);
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        const std::string name(def.name);
        const std::string category(def.category);
        std::filesystem::create_directories(dir / category);
        std::vector<std::string> files;

        const auto emit = [&](const std::string& suffix, const std::vector<float>& L, const std::vector<float>& R,
                              const std::string& note, const std::string& gesture) {
            std::string rel = category;
            rel.append("/").append(name).append(suffix).append(".wav");
            INFO("writing " << rel);
            REQUIRE(swWriteWav(dir / category / (name + suffix + ".wav"), L, R, kSwFs));
            manifest << auRd::auCsvField(rel) << ',' << auRd::auCsvField(name) << ',' << note << ',' << gesture
                     << '\n';
            files.push_back(rel);
            ++wavs;
            if (gesture != "none") {
                ++gestures;
            }
        };

        // E1 and C3: the plan 5.5 sweep render; the E1 render also carries the rule-5 reading.
        std::optional<PresetSweepRender> storedE1;
        for (const std::uint8_t note : kSwNotes) {
            const std::array<std::uint8_t, 1> one{note};
            PresetSweepRender r = renderPresetSweep(comp, one, kSwFs, false);
            const std::size_t bad = swNonFinite(r.L, r.R);
            INFO(name << " " << auRd::auNoteName(note) << ": non-finite " << bad);
            CHECK(bad == 0u);
            emit("_" + auRd::auNoteName(note), r.L, r.R, auRd::auNoteName(note), "none");
            if (note == auRd::kAuE1) {
                storedE1 = std::move(r);
            }
        }
        REQUIRE(storedE1.has_value());

        // Endpoint B on its own (both endpoints = B, so Morph Position 0 renders B): the A / B comparison every preset
        // owes the listener, since Morph is part of every preset's sound. Its E1 level beside A's goes on the sheet.
        const std::vector<std::uint8_t> compB = swBytesOf(withEndpoints(swStateOf(comp), d.b, d.b));
        const PresetSweepRender rB = renderPresetSweep(compB, e1, kSwFs, false);
        {
            INFO(name << " _B_E1: non-finite " << swNonFinite(rB.L, rB.R));
            CHECK(swNonFinite(rB.L, rB.R) == 0u);
            emit("_B_E1", rB.L, rB.R, "E1", "endpoint B static");
        }
        const double levelA = swHeldLevelDb(storedE1->L, storedE1->R);
        const double levelB = swHeldLevelDb(rB.L, rB.R);

        // Gestures. The morph sweep runs for every preset, not only the AT-41 / AT-42 claimants.
        {
            const SwAuditionRender g = swRenderGesture(comp, swRampScript(P::kMorphPositionId), swAt(3.0));
            INFO(name << " _morph: non-finite " << g.nonFinite);
            CHECK(g.nonFinite == 0u);
            emit("_morph", g.L, g.R, "E1", "morph 105 0->1 over 2 s");
        }
        std::size_t claimedMacros = 0;
        for (const auto& mc : macroClaims) {
            claimedMacros += swClaims(def, mc.first) ? 1u : 0u;
        }
        for (const auto& mc : macroClaims) {
            if (!swClaims(def, mc.first)) {
                continue;
            }
            const std::string atName = swAtOf(mc.first);
            const SwAuditionRender g = swRenderGesture(comp, swRampScript(mc.second), swAt(3.0));
            INFO(name << " _macro " << atName << ": non-finite " << g.nonFinite);
            CHECK(g.nonFinite == 0u);
            emit(claimedMacros > 1u ? "_macro_" + atName : std::string("_macro"), g.L, g.R, "E1",
                 "macro " + std::to_string(mc.second) + " 0->1 over 2 s (" + atName + ")");
        }
        if (swClaims(def, C::AT23) || swClaims(def, C::AT43) || swClaims(def, C::AT44) || swClaims(def, C::AT45) ||
            swClaims(def, C::AT46)) {
            const std::vector<SwEv> script{swNoteOn(0, auRd::kAuE1), swNoteOn(swAt(1.0), auRd::kAuE2),
                                           swNoteOff(swAt(1.5), auRd::kAuE1), swNoteOff(swAt(3.0), auRd::kAuE2)};
            const SwAuditionRender g = swRenderGesture(comp, script, swAt(3.0));
            INFO(name << " _legato: non-finite " << g.nonFinite);
            CHECK(g.nonFinite == 0u);
            emit("_legato", g.L, g.R, "E1->E2", "legato E1 0-1.5 s, E2 1.0-3.0 s");
        }

        // The sheet block.
        sheet << "\n## " << category << " / " << name << "\n\n";
        sheet << "- Comment: " << def.description << '\n';
        sheet << "- Primary: " << swLabelOf(def.primary);
        if (!def.secondaries.empty()) {
            sheet << "; secondaries:";
            for (std::size_t k = 0; k < def.secondaries.size(); ++k) {
                sheet << (k == 0 ? " " : ", ") << swLabelOf(def.secondaries[k]);
            }
        }
        sheet << '\n';
        sheet << "- Endpoints: A \"" << d.names[0] << "\", B \"" << d.names[1] << "\"; stored Morph Position "
              << d.morph.position << '\n';
        sheet << "- E1 held level (1-3 s, mid RMS): A " << levelA << " dB, B " << levelB << " dB (B - A "
              << levelB - levelA << " dB)\n";
        std::cout << "# " << name << ": E1 held level A " << levelA << " dB, B " << levelB << " dB\n";
        sheet << "- Files:";
        for (std::size_t k = 0; k < files.size(); ++k) {
            sheet << (k == 0 ? " `" : ", `") << files[k] << '`';
        }
        sheet << "\n\n";

        // C-9 rule 5 (plan 6): the measured change in summed fundamental.
        if (fundamentalLossFlagged(d)) {
            const SwWindow w = swDescriptorWindow(*storedE1);
            SwMS a0 = d.a;
            SwMS b0 = d.b;
            a0.characterAmount = 0.0f;
            b0.characterAmount = 0.0f;
            REQUIRE(Krate::DSP::isValidMorphState(a0));
            REQUIRE(Krate::DSP::isValidMorphState(b0));
            const std::vector<std::uint8_t> comp0 = swBytesOf(withEndpoints(swStateOf(comp), a0, b0));
            const PresetSweepRender r0 = renderPresetSweep(comp0, e1, kSwFs, false);
            const double eStored = auRd::auWindowESubDb(storedE1->L, storedE1->R, w.start, w.length, f0);
            const double eZero = auRd::auWindowESubDb(r0.L, r0.R, w.start, w.length, f0);
            const double delta = eStored - eZero;

            // The flagged endpoint(s): plan 5.6 active endpoints, as fundamentalLossFlagged reads them.
            const bool aFlag = !(d.morph.position >= 1.0f) && swLossyEndpoint(d.a);
            const bool bFlag = !(d.morph.position <= 0.0f) && swLossyEndpoint(d.b);
            REQUIRE((aFlag || bFlag));
            std::string ep;
            std::string alg;
            std::string amount;
            std::string anchor;
            const auto add = [&](const char* label, const SwMS& s) {
                const std::string sep = ep.empty() ? "" : "|";
                std::ostringstream am;
                std::ostringstream an;
                am << s.characterAmount;
                an << s.anchor.level;
                ep += sep + label;
                alg += sep + swAlgorithmName(s.characterAlgorithm);
                amount += sep + am.str();
                anchor += sep + an.str();
            };
            if (aFlag) {
                add("A", d.a);
            }
            if (bFlag) {
                add("B", d.b);
            }
            loss << auRd::auCsvField(name) << ',' << ep << ',' << alg << ',' << amount << ',' << anchor << ','
                 << eStored << ',' << eZero << ',' << delta << '\n';
            std::ostringstream note;
            note << std::setprecision(4);
            note << "Fundamental-loss note (C-9 rule 5): delta_fund = " << delta << " dB (E1 48 kHz, window from "
                 << static_cast<double>(w.start) / kSwFs << " s, " << static_cast<double>(w.length) / kSwFs
                 << " s long; E stored " << eStored << " dB, E at Character Amount 0 " << eZero << " dB; endpoint "
                 << ep << ", " << alg << ", Amount " << amount << ", Anchor Level " << anchor << ")";
            sheet << note.str() << "\n\n";
            std::cout << "# " << name << ": " << note.str() << '\n';
            ++flagged;
        }

        sheet << "Verdict (user):\n";
    }

    CHECK(wavs == 2u * defs.size() + gestures);

    const auto writeText = [](const std::filesystem::path& p, const std::string& text) {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out << text;
        out.flush();
        INFO("writing " << p.string());
        CHECK(out.good());
    };
    writeText(dir / "manifest.txt", manifest.str());
    writeText(sheetPath, sheet.str());
    writeText(dir / "fundamental-loss.csv", loss.str());

    std::cout << "# Profundum_PresetAuditionRenders: presets=" << defs.size() << " wavs=" << wavs
              << " gestures=" << gestures << " flagged=" << flagged << '\n'
              << std::flush;
}

// =============================================================================
// Morph sweep (2026-10-26, the designed-library pass): every preset's A -> B travel is part of its sound, so every
// preset is swept, not only the AT-41 / AT-42 claimants. The audition's _morph render (E1 held 3 s, Morph Position
// 0 -> 1 over 2 s): finite, under the stored output ceiling in true peak (the sweep cases' 0.1 dB allowance), and no
// 100 ms window of [0 s, 3 s) under -60 dBFS (Morph never passes through silence).
// Phase 14b ruling (user, 2026-10-27; specs/Profundum-roadmap.md Phase 14b): the bar is endpoint-referenced. A window
// counts where the same gesture with Morph frozen at A (both endpoints A) or at B (both B) reads above -60 dBFS: a
// dropout is silence Morph makes that neither endpoint has. A sustained preset counts every window; a one-hit kick is
// checked during its hit, while the ramp already moves. The frozen renders run only when the sweep has a quiet window.
// =============================================================================

TEST_CASE("Profundum_PresetMorphSweep", "[profundum][sweep][long]") {
    namespace P = ::Profundum;
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());
    const std::size_t window = swAt(0.1);
    constexpr double kSilenceDb = -60.0;
    const auto windowsDb = [window](const SwAuditionRender& r) {
        std::vector<double> out;
        for (std::size_t at = 0; at + window <= swAt(3.0); at += window) {
            double sum = 0.0;
            for (std::size_t n = at; n < at + window; ++n) {
                sum += 0.5 * (static_cast<double>(r.L[n]) * r.L[n] + static_cast<double>(r.R[n]) * r.R[n]);
            }
            out.push_back(10.0 * std::log10(std::max(sum / static_cast<double>(window), 1.0e-30)));
        }
        return out;
    };
    std::cout << "# Profundum_PresetMorphSweep: presets=" << defs.size()
              << "\npreset,non_finite,true_peak_dbtp,ceiling_dbtp,min_window_db,max_window_db,counted_windows\n";
    for (const Pd::ProfundumPresetDef& def : defs) {
        const std::vector<std::uint8_t> comp = presetCompOf(def);
        const DecodedPresetState d = decodePresetState(comp);
        INFO("preset " << def.name);
        REQUIRE(d.ok);
        const std::vector<SwEv> script = swRampScript(P::kMorphPositionId);
        const SwAuditionRender g = swRenderGesture(comp, script, swAt(3.0));
        const double truePeak = static_cast<double>(
            Krate::DSP::TestUtils::measureTruePeakDb(g.L.data(), g.R.data(), g.L.size(), kSwFs));
        const double bar = static_cast<double>(d.outputCeilingDb) + 0.1;
        const std::vector<double> sweep = windowsDb(g);
        std::vector<bool> counted(sweep.size(), true);
        if (std::any_of(sweep.begin(), sweep.end(), [](double db) { return db <= kSilenceDb; })) {
            const std::vector<double> atA =
                windowsDb(swRenderGesture(swBytesOf(withEndpoints(swStateOf(comp), d.a, d.a)), script, swAt(3.0)));
            const std::vector<double> atB =
                windowsDb(swRenderGesture(swBytesOf(withEndpoints(swStateOf(comp), d.b, d.b)), script, swAt(3.0)));
            for (std::size_t w = 0; w < sweep.size(); ++w) {
                counted[w] = atA[w] > kSilenceDb || atB[w] > kSilenceDb;
            }
        }
        double minDb = std::numeric_limits<double>::max();
        double maxDb = std::numeric_limits<double>::lowest();
        std::size_t countedWindows = 0;
        for (std::size_t w = 0; w < sweep.size(); ++w) {
            maxDb = std::max(maxDb, sweep[w]);
            if (counted[w]) {
                minDb = std::min(minDb, sweep[w]);
                ++countedWindows;
            }
        }
        std::cout << auRd::auCsvField(std::string(def.name)) << ',' << g.nonFinite << ',' << truePeak << ','
                  << d.outputCeilingDb << ',' << minDb << ',' << maxDb << ',' << countedWindows << '\n';
        INFO("non-finite " << g.nonFinite << ", true peak " << truePeak << " dBTP (bar " << bar
                           << "), counted 100 ms windows " << countedWindows << " of " << sweep.size() << ", "
                           << minDb << " .. " << maxDb << " dBFS");
        CHECK(g.nonFinite == 0u);
        CHECK(truePeak <= bar);
        CHECK(countedWindows > 0u);
        CHECK(minDb > kSilenceDb);
    }
    std::cout << std::flush;
}
