// ==============================================================================
// Layer 4: Effect Tests - the composed Vorago chain
//                                    (specs/vorago-phase10-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10-voice-engine/spec.md
//            specs/vorago-phase10-voice-engine/plan.md
//            specs/vorago-phase10-voice-engine/tasks.md  (T006 creates and wires
//                                                         this TU; T022 fills it)
//
// SCOPE OF THIS TU: the AR-1 seam, exercised end to end -
//     engine.processStereoBlock(l, r, n);        // voices -> sum -> sub -> smear
//     cavern.processStereoBlock(l, r, l, r, n);  // Layer 4, owned by the CALLER
//     engine.processOutputStage(l, r, n);        // TapeSaturator -> TruePeakLimiter
//   VoragoEngine is Layer 3 and may not name a Layer 4 type, so this is the
//   ONLY Phase 10 TU that names one, and it lives in dsp_effects_tests rather
//   than dsp_systems_tests so it compiles under this target's
//   KRATE_DSP_AETHER_TEST_HOOKS consistently with every other TU in this
//   executable (FR-084a / OQ3).
//
// THE THREE CASES (tasks.md T022):
//   VoragoComposed_OutputIsBounded    FR-073, untagged. The only place FR-073 is
//                                     asserted on the REAL chain.
//   VoragoComposed_CavernDefaultsMatch SC-009 clause 1's Cavern half. The only
//                                     thing standing between VoragoCavernTargets'
//                                     duplicated literals and silent drift: a
//                                     literal-vs-literal comparison cannot see a
//                                     drifted duplicate, so each field is
//                                     compared against the REAL CavernVerb
//                                     constant, which only this TU can name.
//   VoragoComposed_DepthMacroAxis     SC-008's `Depth` row as amended by A-1,
//                                     [long].
//
// IN-PLACE (ALIASED) CAVERN CALL. `cavern.processStereoBlock(l, r, l, r, n)` is
//   the DOCUMENTED composed-chain call (vorago_engine.h:907-909) and it is
//   alias-safe by construction: CavernVerb::renderSlice copies every input
//   sample into dryScratch_/erScratch_ in its first loop and writes the output
//   only in its second (cavern_verb.h:1163-1228). This is NOT true of
//   AtmosphereEngine, which forbids aliasing - hence the engine's own separate
//   atmos scratch (vorago_engine.h:877).
//
// NEVER include <allocation_operator_overrides.h> here:
//   dsp/tests/unit/effects/aether_reverb_test.cpp already owns the global
//   operator new/delete replacement for this image, and a second include is a
//   duplicate-symbol link error. Use <allocation_detector.h> only. (No case in
//   this TU needs allocation detection: SC-014 is VoragoEngine_NoAllocation-
//   AfterPrepare in dsp_systems_tests.)
//
// CONSTRUCTING NON-FINITE VALUES: never std::numeric_limits<float>::quiet_NaN()
//   or infinity(), and never std::isnan / std::isinf / std::isfinite. Build the
//   values from bit patterns through a VOLATILE sink. This TU never needs to
//   BUILD one, but it does need to DETECT one, which isFiniteBits below does
//   off the exponent field for the same -ffast-math reason.
//
// HEAP-ONLY ENGINE. std::array<VoragoVoice, kMaxVoices> is hundreds of
//   kilobytes and MSVC's default main-thread stack is 1 MiB
//   (vorago_engine.h:146-151), so every VoragoEngine here is heap-allocated.
//   CavernVerb is a stack local, matching Phase 9's own precedent
//   (cavern_verb_test.cpp:506-507, :173-176 - the instance is passed by
//   REFERENCE and never returned by value).
//
// WHAT THIS TU DELIBERATELY DOES NOT DO: the OPTIONAL SC-001a arithmetic
//   cross-check (T022's last paragraph - measure the composed chain's cost here
//   and compare it against `engine + the Phase 9 compliance.md CavernVerb
//   figure`, specs/vorago-phase9-cavern-space/compliance.md:123 "(a) default :
//   124497" ns/block). It is NOT written, and the reason is a project rule
//   rather than an oversight: CPU-budget measurements must run ALONE and
//   P-core-pinned (CLAUDE.md, "Timing-sensitive tests run SEPARATELY"), while
//   T022's own verify command selects this TU by the name pattern
//   "VoragoComposed_*", which Catch2 matches against HIDDEN ([.perf]) cases too.
//   A perf arm here would therefore be dragged into a non-isolated run on every
//   verification of this task and measure the machine, not the code. The
//   cross-check belongs in vorago_perf_test.cpp's isolated lane if it is wanted.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>  // pulls vorago_engine.h (its Layer 3 peer)

// The shared Phase 10 fixtures (T016): applyFastAttack, kFastAttackEnvelopeConfig,
// spearmanRho. Reachable here because tests/test_helpers is an INTERFACE target
// linked into dsp_effects_tests (dsp/tests/CMakeLists.txt:570-575).
#include <vorago_fixtures.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::CavernVerb;
using Krate::DSP::VoragoCavernTargets;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoEngineConfig;
using Krate::DSP::VoragoMacro;
using Krate::DSP::VoragoMacroMatrix;
using Krate::DSP::VoragoMacroRow;
using Krate::DSP::VoragoMacroTargetOwner;
using Krate::DSP::VoragoMacroValues;

using Krate::DSP::TestUtils::Vorago::applyFastAttack;
using Krate::DSP::TestUtils::Vorago::spearmanRho;

constexpr double kSampleRate48 = 48000.0;

/// The prepared maximum block for both halves of the seam. 512 is the block the
/// whole phase measures against (tasks.md T021's ns-per-512-sample-block basis).
constexpr std::size_t kMaxBlockSamples = 512;

/// The notes the bounded case holds, one per admitted slot (kMaxVoices is 6 -
/// the Q-A ruling of 2026-09-19, vorago_engine.h:168).
constexpr std::array<std::uint8_t, VoragoEngine::kMaxVoices> kNotes = {
    {36u, 40u, 43u, 47u, 50u, 53u}};

/// @brief True when @p v is neither infinite nor NaN, read off the exponent
///        field. Immune to -ffast-math, which std::isfinite is not
///        (vorago_engine_test.cpp:129-133 uses the identical predicate).
[[nodiscard]] bool isFiniteBits(float v) noexcept {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// -----------------------------------------------------------------------------
// The seven REAL Layer 4 constants, in VoragoMacroTarget's Cavern-block order -
// which IS VoragoCavernTargets' field order (vorago_macro_matrix.h:176-190).
// NAMING THEM IS THE WHOLE POINT OF THIS TU: vorago_macro_matrix.h may not, so
// its seven duplicated literals are unguarded everywhere else.
// -----------------------------------------------------------------------------
constexpr std::array<float, VoragoMacroMatrix::kNumCavernTargets> kRealCavernDefaults = {
    {CavernVerb::kDefaultSize, CavernVerb::kDefaultDarkness, CavernVerb::kDefaultDecaySeconds,
     CavernVerb::kDefaultFog, CavernVerb::kDefaultDamperDepth, CavernVerb::kDefaultMix,
     CavernVerb::kDefaultWidth}};

constexpr std::array<const char*, VoragoMacroMatrix::kNumCavernTargets> kCavernFieldNames = {
    {"size", "darkness", "decaySeconds", "fog", "damperDepth", "mix", "width"}};

/// @brief One field of a VoragoCavernTargets, addressed by its FIELD INDEX.
///
/// Written out rather than reached through a pointer-to-member table, so the
/// POD's declaration order is restated INDEPENDENTLY here: if a field is ever
/// inserted or reordered without the enum moving with it, this mapping and
/// VoragoMacroMatrix::cavernFieldIndex() disagree and the case fails.
[[nodiscard]] float cavernFieldValue(const VoragoCavernTargets& t, int fieldIndex) noexcept {
    switch (fieldIndex) {
        case 0:
            return t.size;
        case 1:
            return t.darkness;
        case 2:
            return t.decaySeconds;
        case 3:
            return t.fog;
        case 4:
            return t.damperDepth;
        case 5:
            return t.mix;
        case 6:
            return t.width;
        default:
            return 0.0f;
    }
}

// -----------------------------------------------------------------------------
// The composed chain
// -----------------------------------------------------------------------------

/// @brief Push the seven Cavern-owned rows into the real reverb (AR-1's caller
///        side).
///
/// The POD carries the RAW SUM (vorago_macro_matrix.h:969-972); range clamping
/// belongs to these setters, which is exactly why the matrix does not do it.
///
/// @param forceDry SC-008's `Depth` reference arm (A-1): the mix is pinned to 0
///                 and every other target is left EXACTLY as the macro produced
///                 it, so the two arms differ in one control only.
void pushCavernTargets(CavernVerb& cavern, const VoragoCavernTargets& t, bool forceDry) noexcept {
    cavern.setSize(t.size);
    cavern.setDarkness(t.darkness);
    cavern.setDecaySeconds(t.decaySeconds);
    cavern.setFog(t.fog);
    cavern.setDamperDepth(t.damperDepth);
    cavern.setMix(forceDry ? 0.0f : t.mix);
    cavern.setWidth(t.width);
}

/// @brief What a composed render is measured by. Streaming, so a 60 s render
///        needs no 23 MB buffer.
struct ComposedStats {
    double sumSquares = 0.0;         ///< over the measurement window only
    std::size_t windowSamples = 0;   ///< samples SUMMED (both channels count once)
    float maxAbs = 0.0f;             ///< over the WHOLE render, FR-073's statistic
    std::size_t nonFiniteCount = 0;  ///< bit-pattern detected, over the whole render

    /// Mean-square over the window, per channel, as an amplitude.
    [[nodiscard]] double rms() const noexcept {
        if (windowSamples == 0u) {
            return 0.0;
        }
        return std::sqrt(sumSquares / static_cast<double>(windowSamples));
    }
};

/// @brief Render the full AR-1 chain and measure it.
///
/// The three calls are the composed chain verbatim, in the order
/// vorago_engine.h:907-909 states, with the cavern call ALIASED in place as the
/// documented usage. `targets` is pushed BEFORE EVERY BLOCK: with @p blockSamples
/// set to CavernVerb::kControlChunkSamples that is T022's "pushed into the
/// CavernVerb each control chunk" cadence, and with a larger block it is the
/// host cadence a Phase 11 processor will actually use. Both are legal because
/// every CavernVerb setter is idempotent at the smoother (cavern_verb.h:604-611).
///
/// @param windowStart First absolute sample index that contributes to sumSquares.
[[nodiscard]] ComposedStats renderComposed(VoragoEngine& engine, CavernVerb& cavern,
                                           const VoragoCavernTargets& targets, bool forceDry,
                                           std::size_t totalSamples, std::size_t blockSamples,
                                           std::size_t windowStart) {
    ComposedStats stats{};
    if (blockSamples == 0u) {
        return stats;
    }
    std::vector<float> l(blockSamples, 0.0f);
    std::vector<float> r(blockSamples, 0.0f);

    std::size_t done = 0;
    while (done < totalSamples) {
        const std::size_t n = std::min(blockSamples, totalSamples - done);

        pushCavernTargets(cavern, targets, forceDry);

        engine.processStereoBlock(l.data(), r.data(), n);
        cavern.processStereoBlock(l.data(), r.data(), l.data(), r.data(), n);
        engine.processOutputStage(l.data(), r.data(), n);

        for (std::size_t s = 0; s < n; ++s) {
            const float a = l[s];
            const float b = r[s];
            if (!isFiniteBits(a) || !isFiniteBits(b)) {
                ++stats.nonFiniteCount;
                continue;
            }
            const float m = std::max(std::fabs(a), std::fabs(b));
            if (m > stats.maxAbs) {
                stats.maxAbs = m;
            }
            if ((done + s) >= windowStart) {
                const double da = static_cast<double>(a);
                const double db = static_cast<double>(b);
                stats.sumSquares += 0.5 * ((da * da) + (db * db));
                ++stats.windowSamples;
            }
        }
        done += n;
    }
    return stats;
}

/// @brief A prepared engine at @p seed holding @p polyphony notes.
///
/// setSeed() is called BEFORE prepare() deliberately: VoragoVoice::setSeed only
/// stores while the voice is UNPREPARED (vorago_engine.h:251-253 step 3 quotes
/// the rule), so a post-prepare reseed would silently leave every slot on its
/// old stream. setPolyphony() is post-prepare because it allocates nothing
/// (vorago_engine.h:440-443).
///
/// @param notes The notes to hold, one per admitted slot; kNotes unless the
///              caller measures a specific register (the Depth tail holds C4).
[[nodiscard]] std::unique_ptr<VoragoEngine> makeSeededEngine(
    std::uint32_t seed, std::size_t polyphony, std::span<const std::uint8_t> notes = kNotes) {
    auto engine = std::make_unique<VoragoEngine>();
    engine->setSeed(seed);

    VoragoEngineConfig cfg{};
    cfg.maxBlockSamples = kMaxBlockSamples;
    engine->prepare(kSampleRate48, cfg);

    engine->setPolyphony(polyphony);
    // FR-014a: the shipped envelope is a 20 s attack, so an unhelped render
    // measures the attack rather than the macro (vorago_fixtures.h:642).
    applyFastAttack(*engine);

    const std::size_t held = std::min(polyphony, notes.size());
    for (std::size_t i = 0; i < held; ++i) {
        engine->noteOn(notes[i], 100u);
    }
    return engine;
}

/// @brief P-1's cavern configuration (cavern_verb_test.cpp:129-138), at this
///        TU's block size.
void prepareCavern(CavernVerb& cavern, std::uint32_t seed) {
    CavernVerb::PrepareConfig cfg{};
    cfg.numChannels = 8u;
    cfg.maxBlockSamples = kMaxBlockSamples;
    cfg.maxEarlySeconds = CavernVerb::kDefaultMaxEarlySeconds;
    cfg.maxDelaySeconds = 0.50f;
    cfg.spectralDiffusionEnabled = true;
    cfg.diffusionFftSize = 1024u;
    cfg.seed = seed;
    cavern.prepare(kSampleRate48, cfg);
}

// -----------------------------------------------------------------------------
// FR-073's configuration grid
// -----------------------------------------------------------------------------

constexpr std::array<const char*, VoragoMacroMatrix::kNumMacros> kMacroNames = {
    {"Darkness", "Age", "Density", "Movement", "Gravity", "Entropy", "Pressure", "Weight", "Fog",
     "Life", "Depth", "Mass"}};

struct MacroConfig {
    std::string name;
    VoragoMacroValues values;
};

/// @brief Every macro extreme: each of the twelve at 0 and at 1 with the other
///        eleven at their FR-061 neutral, plus the two all-at-once corners.
///
/// The values travel through VoragoMacroMatrix::setMacro / getMacros rather
/// than through a hand-written field switch, so this grid cannot drift away
/// from the shipped macro -> field dispatch.
[[nodiscard]] std::vector<MacroConfig> buildExtremeConfigs() {
    // Indexed rather than iterated as floats, so nothing here ever compares a
    // float for equality to pick its own label.
    constexpr std::array<float, 2> kExtremes = {{0.0f, 1.0f}};
    constexpr std::array<const char*, 2> kExtremeNames = {{"0", "1"}};

    std::vector<MacroConfig> out;
    out.reserve((VoragoMacroMatrix::kNumMacros * kExtremes.size()) + kExtremes.size());

    for (std::size_t i = 0; i < VoragoMacroMatrix::kNumMacros; ++i) {
        const auto macro = static_cast<VoragoMacro>(i);
        for (std::size_t e = 0; e < kExtremes.size(); ++e) {
            VoragoMacroMatrix matrix;  // default-constructed = every FR-061 neutral
            matrix.setMacro(macro, kExtremes[e]);
            out.push_back(MacroConfig{std::string(kMacroNames[i]) + " = " + kExtremeNames[e],
                                      matrix.getMacros()});
        }
    }

    for (std::size_t e = 0; e < kExtremes.size(); ++e) {
        VoragoMacroMatrix matrix;
        for (std::size_t i = 0; i < VoragoMacroMatrix::kNumMacros; ++i) {
            matrix.setMacro(static_cast<VoragoMacro>(i), kExtremes[e]);
        }
        out.push_back(
            MacroConfig{std::string("ALL = ") + kExtremeNames[e], matrix.getMacros()});
    }
    return out;
}

}  // namespace

// ==============================================================================
// FR-073 - the bound, on the REAL chain
// ==============================================================================
// "The output of processOutputStage is bounded: |out| <= 1.0 after the limiter,
// for any parameter combination, any macro combination and any polyphony."
// (spec.md:1106-1107.)
//
// Every OTHER case that touches the bound measures the ENGINE's output, i.e. a
// chain with no Layer 4 stage between the smear and the saturator. Only here
// does the limiter see what it will actually see in the product: a 45 s-decay
// cavern return summed on top of six voices. That is what the seam exists for.
//
// Each of the 26 configurations is rendered WITHOUT resetting the engine, so the
// grid also walks 25 macro TRANSITIONS per polyphony - the bound has to survive
// those too.
// ==============================================================================
TEST_CASE("VoragoComposed_OutputIsBounded", "[effects][vorago]") {
    // 0.256 s per configuration: long enough for kFastAttackEnvelopeConfig's
    // 300 ms walk to have opened the voices and for the limiter to be engaged,
    // short enough that 8 polyphonies x 26 configurations stays a per-push case.
    constexpr std::size_t kRenderSamples = 12288;
    const std::vector<MacroConfig> configs = buildExtremeConfigs();
    REQUIRE(configs.size() == ((VoragoMacroMatrix::kNumMacros * 2u) + 2u));

    float worstOverall = 0.0f;

    for (std::size_t polyphony = 1; polyphony <= VoragoEngine::kMaxVoices; ++polyphony) {
        auto engine = makeSeededEngine(1u, polyphony);
        REQUIRE(engine->isPrepared());
        REQUIRE(engine->getPolyphony() == polyphony);

        CavernVerb cavern;
        prepareCavern(cavern, 1u);
        REQUIRE(cavern.isPrepared());

        for (const MacroConfig& config : configs) {
            VoragoMacroMatrix matrix;
            matrix.setMacros(config.values);
            matrix.apply(*engine);
            const VoragoCavernTargets targets = matrix.computeCavernTargets();

            const ComposedStats stats = renderComposed(*engine, cavern, targets, /*forceDry=*/false,
                                                       kRenderSamples, kMaxBlockSamples,
                                                       /*windowStart=*/0u);

            INFO("polyphony = " << polyphony << ", macros: " << config.name
                                << ", max|out| = " << stats.maxAbs);
            REQUIRE(stats.nonFiniteCount == 0u);
            REQUIRE(stats.maxAbs <= 1.0f);

            worstOverall = std::max(worstOverall, stats.maxAbs);
        }

        // Nothing in the grid may have driven the engine's own containment path;
        // if it did, the bound above was met by a REPLACEMENT rather than by the
        // limiter, which is a different (and much weaker) statement.
        REQUIRE(engine->getNonFiniteRecoveryCount() == 0u);
    }

    WARN("FR-073: worst |out| over "
         << (VoragoEngine::kMaxVoices * configs.size()) << " composed renders (" << configs.size()
         << " macro extremes x polyphony 1.." << VoragoEngine::kMaxVoices << ") = " << worstOverall
         << " (bound 1.0)");
    REQUIRE(worstOverall <= 1.0f);
    // A grid that produced digital silence everywhere would satisfy the bound
    // vacuously. The chain has to have been audible for the case to mean anything.
    REQUIRE(worstOverall > 0.01f);
}

// ==============================================================================
// SC-009 clause 1, the Cavern half - the anti-drift differential
// ==============================================================================
// VoragoCavernTargets duplicates seven Layer 4 literals because a Layer 3 header
// may not name a Layer 4 type (vorago_macro_matrix.h:169-190). A duplicate that
// drifts is INVISIBLE to a literal-vs-literal comparison - the POD would simply
// agree with itself - so this case is the only thing standing between the POD
// and silent drift. It renders nothing; it is a compile-and-compare case.
// ==============================================================================
TEST_CASE("VoragoComposed_CavernDefaultsMatch", "[effects][vorago]") {
    SECTION("(a) each VoragoCavernTargets field default IS the real CavernVerb constant") {
        const VoragoCavernTargets defaults{};

        // Exact equality is deliberate: both sides are meant to be the SAME
        // literal, copied, and a drifted copy is precisely what this rules out.
        REQUIRE(defaults.size == CavernVerb::kDefaultSize);
        REQUIRE(defaults.darkness == CavernVerb::kDefaultDarkness);
        REQUIRE(defaults.decaySeconds == CavernVerb::kDefaultDecaySeconds);
        REQUIRE(defaults.fog == CavernVerb::kDefaultFog);
        REQUIRE(defaults.damperDepth == CavernVerb::kDefaultDamperDepth);
        REQUIRE(defaults.mix == CavernVerb::kDefaultMix);
        REQUIRE(defaults.width == CavernVerb::kDefaultWidth);

        // And the same seven read through the FIELD INDEX, which is what
        // cavernFieldIndex() hands the rest of the system. A field inserted or
        // reordered without the enum moving with it fails here.
        for (std::size_t f = 0; f < VoragoMacroMatrix::kNumCavernTargets; ++f) {
            INFO("VoragoCavernTargets field " << f << " (" << kCavernFieldNames[f] << ")");
            REQUIRE(cavernFieldValue(defaults, static_cast<int>(f)) == kRealCavernDefaults[f]);
        }
    }

    SECTION("(b) every Cavern-owned kRows base IS the real CavernVerb constant") {
        std::size_t cavernRows = 0;
        for (const VoragoMacroRow& row : VoragoMacroMatrix::kRows) {
            if (row.owner != VoragoMacroTargetOwner::Cavern) {
                continue;
            }
            ++cavernRows;
            const int field = VoragoMacroMatrix::cavernFieldIndex(row.target);
            INFO("Cavern row, field index " << field);
            REQUIRE(field >= 0);
            REQUIRE(std::cmp_less(field, VoragoMacroMatrix::kNumCavernTargets));
            INFO("Cavern row on field " << kCavernFieldNames[static_cast<std::size_t>(field)]);
            REQUIRE(row.base == kRealCavernDefaults[static_cast<std::size_t>(field)]);
        }
        // Every one of the seven Cavern targets carries at least one row
        // (everyTargetIsClaimed, vorago_macro_matrix.h:732), so a zero here
        // would mean the loop found nothing and asserted nothing.
        REQUIRE(cavernRows >= VoragoMacroMatrix::kNumCavernTargets);
    }

    SECTION("(c) computeCavernTargets() at the neutrals returns the real constants") {
        // FR-066's identity, stated against Layer 4 rather than against the POD:
        // applyModCurve(c, 0) == 0 for all three permitted curves and g == 0 at
        // Gravity = 0.5, so a default-constructed matrix must return each row's
        // base - which clause (b) has just pinned to the shipped constant.
        const VoragoMacroMatrix matrix;
        const VoragoCavernTargets targets = matrix.computeCavernTargets();

        for (std::size_t f = 0; f < VoragoMacroMatrix::kNumCavernTargets; ++f) {
            INFO("computeCavernTargets() field " << f << " ("
                                                 << kCavernFieldNames[f] << ") = "
                                                 << cavernFieldValue(targets, static_cast<int>(f))
                                                 << ", CavernVerb constant = "
                                                 << kRealCavernDefaults[f]);
            REQUIRE(cavernFieldValue(targets, static_cast<int>(f)) == kRealCavernDefaults[f]);
        }
    }
}

// ==============================================================================
// SC-008's `Depth` row - the POST-NOTE-OFF TAIL (2026-09-19 ruling) - [long]
// ==============================================================================
// HISTORY. The A-1 metric (dB ratio of the composed RMS to the same render with
// CavernVerb::setMix(0)) read -7.74 -> -7.93 dB across Depth on this product:
// the wet output is early-reflection-dominated (kDefaultEarlyLevel 0.8) and the
// drone's energy sits at 16-65 Hz, below the FDN loop's ~40 Hz DC-blocker
// corner where decaySeconds has no authority, so more decay and size moved the
// share by -0.2 dB. The ruling replaces it with the tail: hold the note for the
// settle period, note-off, and measure the RT30 of the composed chain - the
// seconds the output takes to fall 30 dB below its level at note-off. The
// fast-attack fixture's release is 100 ms, so the tail measured is the
// cavern's (plus whatever the engine holds), which is the point.
//
// MEASUREMENT. 100 ms mean-square envelope blocks; the reference level is the
// mean over the last 1 s before note-off; RT30 is the centre of the first block
// after note-off at or below reference - 30 dB. The render is CAPPED at
// kTailCapSeconds after note-off and a tail that never crosses reports the cap.
//
// THRESHOLD. Derived from the measured extremes (half the mean endpoint,
// rounded) - see kRt30EndpointBoundSeconds and its note. Never a guess.
// ==============================================================================

namespace {

/// The seconds after note-off the tail render is allowed to run before it is
/// capped and reported at the cap (ruling: "if the tail runs longer than a
/// sensible render, say 120 s, cap the render and report").
constexpr double kTailCapSeconds = 120.0;
/// The settle period the note is held for before note-off: SC-008's window
/// start, so the cavern has the same build-up every other row measures after.
constexpr double kTailHoldSeconds = 10.0;
constexpr double kTailBlockSeconds = 0.1;
constexpr double kTailDropDb = 30.0;

/// @brief The post-note-off RT30 of the composed chain, in seconds.
///
/// @return The RT30, or kTailCapSeconds when the output never falls 30 dB
///         below its note-off level inside the cap.
struct TailResult {
    double rt30Seconds = 0.0;
    double referenceDb = -300.0;
    double floorDb = -300.0;      ///< the quietest 100 ms block after note-off, rel. reference
    std::size_t nonFiniteCount = 0;
};

[[nodiscard]] TailResult renderTail(VoragoEngine& engine, CavernVerb& cavern,
                                    const VoragoCavernTargets& targets, std::uint8_t note) {
    TailResult out{};
    const auto blockSamples = static_cast<std::size_t>(kTailBlockSeconds * kSampleRate48);
    const auto holdSamples = static_cast<std::size_t>(kTailHoldSeconds * kSampleRate48);
    const auto totalSamples = holdSamples + static_cast<std::size_t>(kTailCapSeconds * kSampleRate48);
    const std::size_t chunk = CavernVerb::kControlChunkSamples;

    std::vector<double> envelope;  // mean-square per 100 ms block
    envelope.reserve(totalSamples / blockSamples + 1u);
    std::vector<float> l(chunk, 0.0f);
    std::vector<float> r(chunk, 0.0f);
    double acc = 0.0;
    std::size_t accCount = 0;
    bool released = false;

    for (std::size_t done = 0; done < totalSamples; done += chunk) {
        if (!released && done >= holdSamples) {
            engine.noteOff(note);
            released = true;
        }
        const std::size_t n = std::min(chunk, totalSamples - done);
        pushCavernTargets(cavern, targets, /*forceDry=*/false);
        engine.processStereoBlock(l.data(), r.data(), n);
        cavern.processStereoBlock(l.data(), r.data(), l.data(), r.data(), n);
        engine.processOutputStage(l.data(), r.data(), n);
        for (std::size_t s = 0; s < n; ++s) {
            const float a = l[s];
            const float b = r[s];
            if (!isFiniteBits(a) || !isFiniteBits(b)) {
                ++out.nonFiniteCount;
                continue;
            }
            acc += 0.5 * ((static_cast<double>(a) * a) + (static_cast<double>(b) * b));
            if (++accCount == blockSamples) {
                envelope.push_back(acc / static_cast<double>(blockSamples));
                acc = 0.0;
                accCount = 0;
            }
        }
    }

    const std::size_t offBlock = holdSamples / blockSamples;
    const std::size_t refBlocks = static_cast<std::size_t>(1.0 / kTailBlockSeconds);
    if (envelope.size() <= offBlock || offBlock < refBlocks) {
        return out;
    }
    double ref = 0.0;
    for (std::size_t b = offBlock - refBlocks; b < offBlock; ++b) {
        ref += envelope[b];
    }
    ref /= static_cast<double>(refBlocks);
    out.referenceDb = 10.0 * std::log10(std::max(ref, 1.0e-30));

    out.rt30Seconds = kTailCapSeconds;
    double quietest = 0.0;
    bool crossed = false;
    for (std::size_t b = offBlock; b < envelope.size(); ++b) {
        const double relDb = (10.0 * std::log10(std::max(envelope[b], 1.0e-30))) - out.referenceDb;
        quietest = (b == offBlock) ? relDb : std::min(quietest, relDb);
        if (!crossed && relDb <= -kTailDropDb) {
            out.rt30Seconds = (static_cast<double>(b - offBlock) + 0.5) * kTailBlockSeconds;
            crossed = true;
        }
    }
    out.floorDb = quietest;
    return out;
}

}  // namespace

TEST_CASE("VoragoComposed_DepthMacroAxis", "[effects][vorago][long]") {
    // SC-008's fixture: five sweep points x three engine seeds. Each render
    // holds the note for kTailHoldSeconds and then runs up to kTailCapSeconds
    // after note-off.
    constexpr std::array<float, 5> kSweepPoints = {{0.0f, 0.25f, 0.5f, 0.75f, 1.0f}};
    constexpr std::array<std::uint32_t, 3> kSeeds = {{1u, 7u, 1337u}};
    constexpr double kSpearmanBound = 0.9;
    // DERIVED (2026-09-19 ruling): half the mean measured endpoint, rounded
    // clean. With the sub tracking default at 1.0 (the subs now follow the bus
    // down after note-off instead of holding a -20 dB floor) the RT30 at C4
    // measured 8.7 -> 15.9 s, 8.4 -> 14.5 s and 8.5 -> 14.3 s across Depth 0 -> 1
    // for seeds 1 / 7 / 1337 (mean endpoint +6.4 s) -> 3 s.
    constexpr double kRt30EndpointBoundSeconds = 3.0;
    constexpr std::size_t kPolyphony = 1;
    // C4, NOT C1 (2026-09-19 ruling). The FDN loop's DC blocker sits near
    // 40 Hz, so at C1 (65 Hz, subs at 33 / 16 Hz) the late field's decay is
    // capped at a few seconds whatever decaySeconds asks and the arms did not
    // separate. Measured across Depth 0 -> 1: C3 (131 Hz) +1.1 / +1.0 / +0.8 s,
    // C4 (262 Hz) +7.2 / +6.1 / +5.8 s - so C4 is where CavernDecaySeconds
    // carries the tail, and that is the note the row measures.
    constexpr std::uint8_t kNote = 60u;

    std::array<double, 5> axis{};
    for (std::size_t p = 0; p < kSweepPoints.size(); ++p) {
        axis[p] = static_cast<double>(kSweepPoints[p]);
    }

    std::array<std::array<double, 5>, 3> rt30{};
    std::array<std::array<double, 5>, 3> floorDb{};
    std::array<double, 3> rhoPerSeed{};
    std::array<double, 3> endpointPerSeed{};
    std::size_t nonFinite = 0;
    VoragoCavernTargets depthLow{};
    VoragoCavernTargets depthHigh{};

    for (std::size_t s = 0; s < kSeeds.size(); ++s) {
        for (std::size_t p = 0; p < kSweepPoints.size(); ++p) {
            VoragoMacroMatrix matrix;  // every other macro stays at its FR-061 neutral
            matrix.setMacro(VoragoMacro::Depth, kSweepPoints[p]);
            const VoragoCavernTargets targets = matrix.computeCavernTargets();
            if (p == 0u) {
                depthLow = targets;
            }
            if (p == kSweepPoints.size() - 1u) {
                depthHigh = targets;
            }

            // The engine HOLDS kNote and renderTail RELEASES kNote: the same
            // note on both sides, or the note-off would release nothing and
            // the "tail" would be the drone.
            const std::array<std::uint8_t, 1> heldNote = {{kNote}};
            auto engine = makeSeededEngine(kSeeds[s], kPolyphony, heldNote);
            matrix.apply(*engine);
            CavernVerb cavern;
            prepareCavern(cavern, kSeeds[s]);
            const TailResult tail = renderTail(*engine, cavern, targets, kNote);

            nonFinite += tail.nonFiniteCount;
            rt30[s][p] = tail.rt30Seconds;
            floorDb[s][p] = tail.floorDb;
        }
        rhoPerSeed[s] = spearmanRho(axis, rt30[s]);
        endpointPerSeed[s] = rt30[s][4] - rt30[s][0];
    }

    // -------------------------------------------------------------------------
    // Everything is PRINTED before anything is asserted.
    // -------------------------------------------------------------------------
    for (std::size_t s = 0; s < kSeeds.size(); ++s) {
        WARN("SC-008 Depth (tail), seed "
             << kSeeds[s] << ": composed RT30 s at Depth {0, 0.25, 0.5, 0.75, 1} = " << rt30[s][0]
             << ", " << rt30[s][1] << ", " << rt30[s][2] << ", " << rt30[s][3] << ", " << rt30[s][4]
             << " (cap " << kTailCapSeconds << " s; quietest block rel. note-off level dB = "
             << floorDb[s][0] << ", " << floorDb[s][1] << ", " << floorDb[s][2] << ", "
             << floorDb[s][3] << ", " << floorDb[s][4] << ") | Spearman rho = " << rhoPerSeed[s]
             << " | endpoint = " << endpointPerSeed[s] << " s");
    }

    double rhoMean = 0.0;
    double endpointMean = 0.0;
    for (std::size_t s = 0; s < kSeeds.size(); ++s) {
        rhoMean += rhoPerSeed[s];
        endpointMean += endpointPerSeed[s];
    }
    rhoMean /= static_cast<double>(kSeeds.size());
    endpointMean /= static_cast<double>(kSeeds.size());

    WARN("SC-008 Depth (tail): mean Spearman rho = " << rhoMean << " (bound +" << kSpearmanBound
                                                     << "), mean endpoint = " << endpointMean
                                                     << " s (bound " << kRt30EndpointBoundSeconds
                                                     << " s)");

    CHECK(nonFinite == 0u);
    // Direction is POSITIVE and stated as such, averaged over the three seeds.
    CHECK(rhoMean >= kSpearmanBound);
    CHECK(endpointMean >= kRt30EndpointBoundSeconds);

    // The Depth readback clauses (FR-068's `Decay` lengthening half): the
    // decay target reads LONGER and the size target LARGER at 1.
    INFO("Depth decaySeconds " << depthLow.decaySeconds << " -> " << depthHigh.decaySeconds
                               << ", size " << depthLow.size << " -> " << depthHigh.size);
    CHECK(depthHigh.decaySeconds > depthLow.decaySeconds);
    CHECK(depthHigh.size > depthLow.size);
}
