// ==============================================================================
// Layer 3: System Tests - VoragoMacroMatrix
//                                    (specs/vorago-phase10-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10-voice-engine/spec.md
//            specs/vorago-phase10-voice-engine/plan.md   (S7.1 - S7.4)
//            specs/vorago-phase10-voice-engine/tasks.md  (T006 creates and wires
//                                                         this TU; T017 lands the
//                                                         two cases below; T025
//                                                         fills the rest)
//
// SCOPE OF THIS TU: SC-008 (the eleven macro sweeps), SC-009, SC-010,
//   SC-023 (matrix), SC-028 and FR-067's VoragoMacro_ApplyIsIdempotent. The
//   macro system is a constexpr DATA table (AR-6), so what this TU pins is each
//   row's DIRECTION, its curve and its end-to-end effect size - never an
//   `amount` literal, which is tuning.
//
// WHAT T017 LANDS HERE: the two degenerate/contract cases -
//   VoragoMacro_UnpreparedAndDegenerate (SC-023's matrix arm, A-8) and
//   VoragoMacro_SetterContract (SC-028, matrix half). Neither renders audio.
//
// ALLOCATION DETECTION: this TU includes neither <allocation_detector.h> nor
//   <allocation_operator_overrides.h>. The single owner of the global
//   operator new/delete replacements in dsp_systems_tests is
//   unit/systems/selectable_oscillator_test.cpp:388; a second include of
//   <allocation_operator_overrides.h> is a duplicate-symbol link error.
//
// PORTABILITY: no std::isnan / std::isinf / std::isfinite anywhere in this TU,
//   so it stays correct under -ffast-math. Every non-finite probe is built from
//   a BIT PATTERN through a volatile sink (makeNonFinite below), never from
//   std::numeric_limits, which -ffast-math folds to finite garbage.
//
// HEAP-ONLY ENGINE: std::array<VoragoVoice, kMaxVoices> is hundreds of kilobytes
//   and MSVC's default main-thread stack is 1 MiB (vorago_engine.h:146-151), so
//   every VoragoEngine in this TU is constructed through std::make_unique.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/systems/vorago_macro_matrix.h>

// The shared Phase 10 fixtures (T016): makeEngine(), applyFastAttack() and the
// analysis helpers T025's rendering cases measure with. Pulls in
// vorago_engine.h, which vorago_macro_matrix.h already includes.
#include <vorago_fixtures.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

using Krate::DSP::VoragoCavernTargets;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoMacro;
using Krate::DSP::VoragoMacroMatrix;
using Krate::DSP::VoragoMacroValues;

namespace {

/// Build a non-finite float from its bit pattern through a volatile sink.
/// std::numeric_limits<float>::quiet_NaN() / infinity() fold to finite garbage
/// under -ffast-math, so they are never used here.
/// 0x7FC00000 = quiet NaN, 0x7F800000 = +Inf, 0xFF800000 = -Inf.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;        // defeats constant folding
    const std::uint32_t materialized = b;   // the volatile READ is the sink
    float f = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// The twelve macros, enumerated once so every loop below covers all of them
/// rather than a hand-picked subset.
constexpr std::array<VoragoMacro, 12> kAllMacros = {{VoragoMacro::Darkness, VoragoMacro::Age,
                                                     VoragoMacro::Density, VoragoMacro::Movement,
                                                     VoragoMacro::Gravity, VoragoMacro::Entropy,
                                                     VoragoMacro::Pressure, VoragoMacro::Weight,
                                                     VoragoMacro::Fog, VoragoMacro::Life,
                                                     VoragoMacro::Depth, VoragoMacro::Mass}};

static_assert(kAllMacros.size() == static_cast<std::size_t>(VoragoMacro::Count),
              "kAllMacros must enumerate every VoragoMacro");

/// Read one field of VoragoMacroValues. Written out here rather than reusing
/// the matrix's own dispatch, so the bulk round-trip is checked against an
/// INDEPENDENT statement of the macro -> field mapping.
[[nodiscard]] float fieldOf(const VoragoMacroValues& v, VoragoMacro m) noexcept {
    switch (m) {
        case VoragoMacro::Darkness:
            return v.darkness;
        case VoragoMacro::Age:
            return v.age;
        case VoragoMacro::Density:
            return v.density;
        case VoragoMacro::Movement:
            return v.movement;
        case VoragoMacro::Gravity:
            return v.gravity;
        case VoragoMacro::Entropy:
            return v.entropy;
        case VoragoMacro::Pressure:
            return v.pressure;
        case VoragoMacro::Weight:
            return v.weight;
        case VoragoMacro::Fog:
            return v.fog;
        case VoragoMacro::Life:
            return v.life;
        case VoragoMacro::Depth:
            return v.depth;
        case VoragoMacro::Mass:
            return v.mass;
        case VoragoMacro::Count:
        default:
            return 0.0f;
    }
}

/// Write one field of VoragoMacroValues; the mirror of fieldOf above.
void setFieldOf(VoragoMacroValues& v, VoragoMacro m, float value) noexcept {
    switch (m) {
        case VoragoMacro::Darkness:
            v.darkness = value;
            break;
        case VoragoMacro::Age:
            v.age = value;
            break;
        case VoragoMacro::Density:
            v.density = value;
            break;
        case VoragoMacro::Movement:
            v.movement = value;
            break;
        case VoragoMacro::Gravity:
            v.gravity = value;
            break;
        case VoragoMacro::Entropy:
            v.entropy = value;
            break;
        case VoragoMacro::Pressure:
            v.pressure = value;
            break;
        case VoragoMacro::Weight:
            v.weight = value;
            break;
        case VoragoMacro::Fog:
            v.fog = value;
            break;
        case VoragoMacro::Life:
            v.life = value;
            break;
        case VoragoMacro::Depth:
            v.depth = value;
            break;
        case VoragoMacro::Mass:
            v.mass = value;
            break;
        case VoragoMacro::Count:
        default:
            break;
    }
}

/// FR-061's neutral, restated in the test rather than read from the matrix, so
/// the assertion is an independent statement of the contract.
[[nodiscard]] constexpr float expectedNeutral(VoragoMacro m) noexcept {
    return (m == VoragoMacro::Gravity) ? 0.5f : 0.0f;
}

/// The eight engine-owned observables apply() may ever write, captured as one
/// value so "writes nothing" is a single comparison.
struct EngineSnapshot {
    float subToneLevelOffsetDb = 0.0f;
    float subTrackingAmount = 0.0f;
    float smearAmount = 0.0f;
    float smearDecoherence = 0.0f;
    float smearTilt = 0.0f;
    float ghostPeakLevel = 0.0f;
    float atmosBlur = 0.0f;
    float outputSaturation = 0.0f;
    // A representative slice of the voice-owned half, read through the engine's
    // const getVoice() accessor (vorago_engine.h:984).
    float voiceRichness = 0.0f;
    float voiceSpectralTiltDb = 0.0f;
    float voiceBodyDamping = 0.0f;
    float voiceEcologyMix = 0.0f;
    float voiceTidalDepth = 0.0f;
};

[[nodiscard]] EngineSnapshot snapshot(const VoragoEngine& e) noexcept {
    EngineSnapshot s{};
    s.subToneLevelOffsetDb = e.getSubToneLevelOffsetDb();
    s.subTrackingAmount = e.getSubTrackingAmount();
    s.smearAmount = e.getSmearAmount();
    s.smearDecoherence = e.getSmearDecoherence();
    s.smearTilt = e.getSmearTilt();
    s.ghostPeakLevel = e.getGhostPeakLevel();
    s.atmosBlur = e.getAtmosBlur();
    s.outputSaturation = e.getOutputSaturation();
    const auto& v = e.getVoice(0);
    s.voiceRichness = v.getRichness();
    s.voiceSpectralTiltDb = v.getSpectralTiltDb();
    s.voiceBodyDamping = v.getBodyDamping();
    s.voiceEcologyMix = v.getEcologyMix();
    s.voiceTidalDepth = v.getTidalDepth();
    return s;
}

void requireSnapshotsEqual(const EngineSnapshot& a, const EngineSnapshot& b) {
    REQUIRE(a.subToneLevelOffsetDb == b.subToneLevelOffsetDb);
    REQUIRE(a.subTrackingAmount == b.subTrackingAmount);
    REQUIRE(a.smearAmount == b.smearAmount);
    REQUIRE(a.smearDecoherence == b.smearDecoherence);
    REQUIRE(a.smearTilt == b.smearTilt);
    REQUIRE(a.ghostPeakLevel == b.ghostPeakLevel);
    REQUIRE(a.atmosBlur == b.atmosBlur);
    REQUIRE(a.outputSaturation == b.outputSaturation);
    REQUIRE(a.voiceRichness == b.voiceRichness);
    REQUIRE(a.voiceSpectralTiltDb == b.voiceSpectralTiltDb);
    REQUIRE(a.voiceBodyDamping == b.voiceBodyDamping);
    REQUIRE(a.voiceEcologyMix == b.voiceEcologyMix);
    REQUIRE(a.voiceTidalDepth == b.voiceTidalDepth);
}

}  // namespace

// =============================================================================
// SC-023 (matrix arm, A-8) - the unprepared and degenerate surfaces
// =============================================================================

TEST_CASE("VoragoMacro_UnpreparedAndDegenerate", "[systems][vorago]") {
    SECTION("apply() against an UNPREPARED engine writes nothing and does not fault") {
        // Deliberately NOT prepared: prepare() is the only allocating path, and
        // apply() must be inert before it has run rather than half-writing a
        // pool that does not exist yet.
        auto engine = std::make_unique<VoragoEngine>();
        REQUIRE_FALSE(engine->isPrepared());

        const EngineSnapshot before = snapshot(*engine);

        // Every macro pushed to the far end of its range, so a write of ANY row
        // would move at least one observable.
        VoragoMacroMatrix matrix;
        for (const VoragoMacro m : kAllMacros) {
            matrix.setMacro(m, 1.0f);
        }
        matrix.apply(*engine);

        requireSnapshotsEqual(snapshot(*engine), before);

        // And the other extreme of the one bipolar macro.
        matrix.setMacro(VoragoMacro::Gravity, 0.0f);
        matrix.apply(*engine);
        requireSnapshotsEqual(snapshot(*engine), before);
    }

    SECTION("computeCavernTargets() on a DEFAULT-CONSTRUCTED matrix is the FR-063 default table") {
        const VoragoMacroMatrix matrix;
        const VoragoCavernTargets t = matrix.computeCavernTargets();

        // Field by field, and EXACT: at every neutral applyModCurve(c, 0) == 0
        // for all three permitted curves, so every contribution is exactly zero
        // and the result is each row's `base` bit-for-bit.
        REQUIRE(t.size == 0.50f);           // cavern_verb.h:253 kDefaultSize
        REQUIRE(t.darkness == 0.80f);       // :254 kDefaultDarkness
        REQUIRE(t.decaySeconds == 20.0f);   // :255 kDefaultDecaySeconds
        REQUIRE(t.fog == 0.30f);            // :259 kDefaultFog
        REQUIRE(t.damperDepth == 0.35f);    // :247 kDefaultDamperDepth
        REQUIRE(t.mix == 1.00f);            // :264 kDefaultMix
        REQUIRE(t.width == 1.00f);          // :263 kDefaultWidth

        // The POD's own member initialisers say the same thing; if the two ever
        // disagree the matrix has stopped seeding from kRows.
        const VoragoCavernTargets defaults{};
        REQUIRE(t.size == defaults.size);
        REQUIRE(t.darkness == defaults.darkness);
        REQUIRE(t.decaySeconds == defaults.decaySeconds);
        REQUIRE(t.fog == defaults.fog);
        REQUIRE(t.damperDepth == defaults.damperDepth);
        REQUIRE(t.mix == defaults.mix);
        REQUIRE(t.width == defaults.width);
    }

    SECTION("a default-constructed matrix is already at the FR-066 identity") {
        const VoragoMacroMatrix matrix;
        for (const VoragoMacro m : kAllMacros) {
            INFO("macro index " << static_cast<int>(m));
            REQUIRE(matrix.getMacro(m) == expectedNeutral(m));
        }
        const VoragoMacroValues v = matrix.getMacros();
        for (const VoragoMacro m : kAllMacros) {
            INFO("macro index " << static_cast<int>(m));
            REQUIRE(fieldOf(v, m) == expectedNeutral(m));
        }
    }

    SECTION("an OUT-OF-RANGE VoragoMacro is a no-op on setMacro and neutral on getMacro") {
        VoragoMacroMatrix matrix;
        matrix.setMacro(VoragoMacro::Darkness, 0.75f);
        const VoragoMacroValues before = matrix.getMacros();

        const auto outOfRange = static_cast<VoragoMacro>(200);
        matrix.setMacro(outOfRange, 1.0f);
        matrix.setMacro(VoragoMacro::Count, 1.0f);

        const VoragoMacroValues after = matrix.getMacros();
        for (const VoragoMacro m : kAllMacros) {
            INFO("macro index " << static_cast<int>(m));
            REQUIRE(fieldOf(after, m) == fieldOf(before, m));
        }

        // getMacro on a non-enumerator returns THAT macro's neutral, which for
        // anything outside the twelve is the unipolar 0.
        REQUIRE(matrix.getMacro(outOfRange) == 0.0f);
        REQUIRE(matrix.getMacro(VoragoMacro::Count) == 0.0f);
    }
}

// =============================================================================
// SC-028 (matrix half) - the setter contract
// =============================================================================

TEST_CASE("VoragoMacro_SetterContract", "[systems][vorago]") {
    const float nanValue = makeNonFinite(kQuietNaNBits);
    const float posInf = makeNonFinite(kPosInfBits);
    const float negInf = makeNonFinite(kNegInfBits);

    SECTION("setMacro / getMacro round-trip over both endpoints, the neutral and non-finites") {
        VoragoMacroMatrix matrix;
        for (const VoragoMacro m : kAllMacros) {
            const float neutral = expectedNeutral(m);

            struct Probe {
                float input;
                float expected;
            };
            // FR-069 / FR-071: a non-finite write is REJECTED and THE PREVIOUS
            // VALUE STANDS. The probes below are applied IN ORDER against one
            // matrix, so each non-finite row's expectation is literally the row
            // above it - and the table deliberately carries two different
            // standing values (1.0, then 0.25) so an implementation that reset
            // to a constant (the macro's neutral, say) cannot pass both.
            const std::array<Probe, 13> probes = {{
                {0.0f, 0.0f},            // the unipolar floor / the bipolar `air` end
                {1.0f, 1.0f},            // the ceiling / the bipolar `stone` end
                {0.5f, 0.5f},            // the bipolar neutral, mid-travel for the rest
                {neutral, neutral},      // this macro's own documented neutral
                {-0.25f, 0.0f},          // below range -> clamped, never rejected
                {1.25f, 1.0f},           // above range -> clamped
                {nanValue, 1.0f},        // non-finite -> rejected; 1.0 still stands
                {posInf, 1.0f},
                {negInf, 1.0f},
                {0.25f, 0.25f},          // move the standing value off 1.0 ...
                {nanValue, 0.25f},       // ... and the rejection still holds it
                {posInf, 0.25f},
                {negInf, 0.25f},
            }};

            for (const Probe& p : probes) {
                INFO("macro index " << static_cast<int>(m));
                matrix.setMacro(m, p.input);
                REQUIRE(matrix.getMacro(m) == p.expected);
                // The bulk getter must agree with the scalar one, always.
                REQUIRE(fieldOf(matrix.getMacros(), m) == p.expected);
            }

            // Leave it at the neutral so the next macro starts from identity.
            matrix.setMacro(m, neutral);
        }
    }

    SECTION("a non-finite write on ONE macro does not poison the others") {
        VoragoMacroMatrix matrix;
        for (const VoragoMacro m : kAllMacros) {
            matrix.setMacro(m, 0.25f);
        }
        matrix.setMacro(VoragoMacro::Density, nanValue);

        // FR-069 / FR-071: Density keeps the 0.25 it already held - the write is
        // rejected outright, not replaced by a value the caller never asked for.
        REQUIRE(matrix.getMacro(VoragoMacro::Density) == 0.25f);
        for (const VoragoMacro m : kAllMacros) {
            INFO("macro index " << static_cast<int>(m));
            REQUIRE(matrix.getMacro(m) == 0.25f);
        }
    }

    SECTION("setMacros / getMacros round-trip through the same sanitising clamp") {
        // A bulk write may not bypass setMacro's clamp OR its FR-069 rejection:
        // every field goes through setMacro, so the expectation below is computed
        // with the same rule. The matrix is SEEDED to 0.75 on every macro first,
        // so the three non-finite fields have a standing value that is neither
        // the struct default nor the macro's neutral - without the seed a
        // reject-and-keep and a reset-to-neutral are indistinguishable here.
        VoragoMacroValues in{};
        setFieldOf(in, VoragoMacro::Darkness, 1.0f);
        setFieldOf(in, VoragoMacro::Age, 0.0f);
        setFieldOf(in, VoragoMacro::Density, 4.0f);       // -> 1.0
        setFieldOf(in, VoragoMacro::Movement, -3.0f);     // -> 0.0
        setFieldOf(in, VoragoMacro::Gravity, nanValue);   // -> rejected, 0.75 stands
        setFieldOf(in, VoragoMacro::Entropy, posInf);     // -> rejected, 0.75 stands
        setFieldOf(in, VoragoMacro::Pressure, negInf);    // -> rejected, 0.75 stands
        setFieldOf(in, VoragoMacro::Weight, 0.5f);
        setFieldOf(in, VoragoMacro::Fog, 0.125f);
        setFieldOf(in, VoragoMacro::Life, 1.0f);
        setFieldOf(in, VoragoMacro::Depth, 0.875f);
        setFieldOf(in, VoragoMacro::Mass, 0.0f);

        VoragoMacroValues expected{};
        setFieldOf(expected, VoragoMacro::Darkness, 1.0f);
        setFieldOf(expected, VoragoMacro::Age, 0.0f);
        setFieldOf(expected, VoragoMacro::Density, 1.0f);
        setFieldOf(expected, VoragoMacro::Movement, 0.0f);
        setFieldOf(expected, VoragoMacro::Gravity, 0.75f);
        setFieldOf(expected, VoragoMacro::Entropy, 0.75f);
        setFieldOf(expected, VoragoMacro::Pressure, 0.75f);
        setFieldOf(expected, VoragoMacro::Weight, 0.5f);
        setFieldOf(expected, VoragoMacro::Fog, 0.125f);
        setFieldOf(expected, VoragoMacro::Life, 1.0f);
        setFieldOf(expected, VoragoMacro::Depth, 0.875f);
        setFieldOf(expected, VoragoMacro::Mass, 0.0f);

        VoragoMacroMatrix matrix;
        for (const VoragoMacro m : kAllMacros) {
            matrix.setMacro(m, 0.75f);
        }
        matrix.setMacros(in);

        const VoragoMacroValues out = matrix.getMacros();
        for (const VoragoMacro m : kAllMacros) {
            INFO("macro index " << static_cast<int>(m));
            REQUIRE(fieldOf(out, m) == fieldOf(expected, m));
            REQUIRE(matrix.getMacro(m) == fieldOf(expected, m));
        }

        // An in-range vector round-trips exactly, so the clamp is not a lossy
        // transform on legal input.
        VoragoMacroValues legal{};
        for (const VoragoMacro m : kAllMacros) {
            setFieldOf(legal, m, 0.375f);
        }
        matrix.setMacros(legal);
        const VoragoMacroValues back = matrix.getMacros();
        for (const VoragoMacro m : kAllMacros) {
            INFO("macro index " << static_cast<int>(m));
            REQUIRE(fieldOf(back, m) == 0.375f);
        }
    }

    SECTION("Count and out-of-range are SILENT no-ops on the bulk path too") {
        VoragoMacroMatrix matrix;
        VoragoMacroValues legal{};
        for (const VoragoMacro m : kAllMacros) {
            setFieldOf(legal, m, 0.625f);
        }
        matrix.setMacros(legal);

        matrix.setMacro(VoragoMacro::Count, 0.0f);
        matrix.setMacro(static_cast<VoragoMacro>(97), 1.0f);
        matrix.setMacro(static_cast<VoragoMacro>(255), nanValue);

        const VoragoMacroValues after = matrix.getMacros();
        for (const VoragoMacro m : kAllMacros) {
            INFO("macro index " << static_cast<int>(m));
            REQUIRE(fieldOf(after, m) == 0.625f);
        }
    }
}

// =============================================================================
// T025 - SC-008 sweeps, SC-009 neutrality, SC-010 continuity, FR-067 idempotence
// =============================================================================
// WHAT THE FOUR CASES BELOW NEED THAT THE TWO ABOVE DID NOT: a RENDERING engine.
// The shared Phase 10 fixtures own the construction path (makeEngine - the
// engine is hundreds of kilobytes and is NEVER a stack local), the FR-014a
// fast-attack envelope and every analysis statistic, so nothing here
// re-implements a metric that already exists.
//
// POLYPHONY 1 FOR THE SWEEPS, AND THAT IS A DECISION, NOT A SHORTCUT. Four of
// SC-008's eleven figures are read off ONE voice's components - Density's
// partial/source counts, Gravity's peak offsets, Mass's body-A modal bands and
// the folded-concept readbacks - and Mass in particular integrates the energy
// inside +/- 1 semitone of body A's first eight modes. A four-note chord would
// pour three other voices' partials straight into those bands AND into the
// broadband denominator, so the row would be measuring the chord, not the macro.
// SC-009 clause 3 and FR-067, which assert bit-identity on the mixed output
// rather than on one voice, DO use the shipped kDefaultPolyphony.
//
// THREE MEASUREMENT CONSTRUCTIONS ARE PER-ROW (the direction and the threshold
// of every row are SC-008's; the metrics are SC-008's except the three the
// 2026-09-19 ruling amended - see the next paragraph):
//   - NOTE. Eight rows render C1 (kMacroNote). Age renders C4, Weight and
//     Mass C3, because at C1 the band each metric integrates is EMPTY of the
//     thing the macro moves - see kAgeNote / kWeightNote.
//   - LIFE'S WINDOW. Every scheduler draws its FIRST onset at prepare() from
//     the UNSCALED 20-90 s range, so a 60 s render counts the pre-roll draw,
//     not the macro - see kLifeRenderSeconds / kLifeEdgeWindowStartSeconds.
//   - ENTROPY'S BAND. Spectral flatness is taken over the voice's harmonic
//     band [f0, 20 f0], not [0, Nyquist] - see harmonicBandFlatness().
//
// THREE ROWS WERE RE-METRICKED BY THE 2026-09-19 RULING, with thresholds
// derived from the measured extremes (half the mean endpoint, rounded - see
// kDarknessThresholdDb / kMassThresholdDb / kFogThreshold) and a readback
// clause each: Darkness (energy above 4 f0 relative to below, must fall), Mass
// (energy below 80 Hz at C3, must rise) and Fog (harmonic-band flatness, must
// rise). Gravity and Pressure stay exactly as SC-008 words them and are
// RECORDED AS FAILED by the same ruling; the case uses CHECK per row so every
// verdict is visible in one run, and renders the sweep exactly once.
// =============================================================================

namespace {

using Krate::DSP::VoragoEngineConfig;
using Krate::DSP::VoragoMacroTarget;
using Krate::DSP::VoragoMacroTargetOwner;
using Krate::DSP::VoragoVoice;

using Krate::DSP::TestUtils::Vorago::applyFastAttack;
using Krate::DSP::TestUtils::Vorago::bandEnergyDb;
using Krate::DSP::TestUtils::Vorago::crestFactorDb;
using Krate::DSP::TestUtils::Vorago::makeEngine;
using Krate::DSP::TestUtils::Vorago::maxDeltaInWindow;
using Krate::DSP::TestUtils::Vorago::perBandTotalVariation;
using Krate::DSP::TestUtils::Vorago::spearmanRho;

// -----------------------------------------------------------------------------
// Render constants, shared by all four cases
// -----------------------------------------------------------------------------

constexpr double kMacroSampleRate = 48000.0;
constexpr std::uint8_t kMacroNote = 36u;  ///< C1 - the drone register Vorago is written for
constexpr std::uint8_t kMacroVelocity = 100u;

/// SC-008's fixture: 60 s renders, measured over [10 s, 60 s] STATED IN SAMPLES.
constexpr double kSweepSeconds = 60.0;
constexpr double kSweepWindowStartSeconds = 10.0;
constexpr std::size_t kSweepTotalSamples =
    static_cast<std::size_t>(kSweepSeconds * kMacroSampleRate);  // 2 880 000
constexpr std::size_t kSweepWindowStartSample =
    static_cast<std::size_t>(kSweepWindowStartSeconds * kMacroSampleRate);  // 480 000
constexpr std::size_t kSweepBlockSamples = 512u;

/// Per-row NOTE choices. Every row is measured at kMacroNote (C1, 65.4 Hz)
/// except the two whose metric band holds NOTHING the macro moves at C1:
///   - Age's metric is HF energy above 4 kHz. The cloud carries 18 partials at
///     the shipped richness, so at C1 the top partial sits at 1.2 kHz and the
///     > 4 kHz band holds the numerical floor (-129 dB), which no tilt or
///     damping can move (measured: -121.4 -> -121.3 dB, rho +0.33). At C4
///     (261.6 Hz) partials 16-18 sit above 4 kHz and the row reads the tilt.
///   - Weight's metric is energy below 80 Hz. At C1 the note's OWN fundamental
///     sits inside that band and dominates it, 9 dB above the subharmonic tones
///     the macro raises (measured +2.5 dB against the >= 6 dB threshold). At C3
///     (130.8 Hz) the band holds the Div2 / Div4 subs (65 / 33 Hz) and nothing
///     else, so the figure IS the sub level the row moves.
constexpr std::uint8_t kAgeNote = 60u;     ///< C4
constexpr std::uint8_t kWeightNote = 48u;  ///< C3

/// Life's edge count needs a LONGER render than the spectral rows. Every
/// SlowEventScheduler draws its FIRST onset at prepare() from the UNSCALED
/// 20-90 s range (the FR-067 pre-roll, slow_event_scheduler.h:84-91) and no
/// later setter shortens it, so inside a 60 s render the count is a reading of
/// that one pre-roll draw, not of the macro (measured: 0 edges at every point
/// for all three seeds, because their pre-rolls exceed 60 s). The Life row
/// therefore renders 210 s and counts edges over [90 s, 210 s]: the window
/// opens at the pre-roll's ceiling, so every interval it contains was drawn
/// from the SCALED range the row writes. Its spectral window stays [10 s, 60 s].
constexpr double kLifeRenderSeconds = 210.0;
constexpr double kLifeEdgeWindowStartSeconds = 90.0;
constexpr std::size_t kLifeRenderSamples =
    static_cast<std::size_t>(kLifeRenderSeconds * kMacroSampleRate);  // 10 080 000
constexpr std::size_t kLifeEdgeWindowStartSample =
    static_cast<std::size_t>(kLifeEdgeWindowStartSeconds * kMacroSampleRate);  // 4 320 000

/// Entropy's flatness band, as multiples of the note frequency: [0.94 f0, 20 f0]
/// spans the cloud's 18 shipped partials with a half-semitone of drift margin
/// below the fundamental.
constexpr double kHarmonicBandLow = 0.94;
constexpr double kHarmonicBandHigh = 20.0;

/// The frequency of a MIDI note, A4 = 440 Hz.
[[nodiscard]] double midiToHz(std::uint8_t note) noexcept {
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

/// @brief Spectral flatness over the voice's HARMONIC BAND [0.94 f0, 20 f0]:
///        geometric over arithmetic mean of the Welch-averaged power spectrum,
///        exactly as the fixture's spectralFlatness() computes it, restricted to
///        the bins the instrument occupies.
///
/// WHY THE BAND. The full-band figure on this instrument is ~1e-11 and is
/// pinned by the ~3 400 bins above the cloud's top partial that hold only the
/// numerical floor (-129 dB): their geometric mean sets the numerator whatever
/// any macro does, and the measured full-band series was non-monotone (rho
/// +0.17, endpoint -6 %) while every Entropy row visibly spread the harmonic
/// band. Over [f0, 20 f0] the same statistic reads the mutation, inharmonicity,
/// decoherence and drift the macro writes. The spectrum machinery is the
/// fixture's own (welchPowerSpectrum / analysisFftSize), not a second FFT path.
[[nodiscard]] double harmonicBandFlatness(std::span<const float> x, double sr, double noteHz) {
    namespace fx = Krate::DSP::TestUtils::Vorago::detail;
    if (x.empty() || !(sr > 0.0) || !(noteHz > 0.0)) {
        return 0.0;
    }
    const std::size_t fftSize = fx::analysisFftSize(x.size());
    const std::vector<double> power = fx::welchPowerSpectrum(x, fftSize);
    const double binHz = sr / static_cast<double>(fftSize);
    const double loHz = kHarmonicBandLow * noteHz;
    const double hiHz = kHarmonicBandHigh * noteHz;

    double logSum = 0.0;
    double linearSum = 0.0;
    std::size_t count = 0u;
    for (std::size_t k = 1; k < power.size(); ++k) {  // skip DC
        const double freq = static_cast<double>(k) * binHz;
        if (freq < loHz || freq > hiHz) {
            continue;
        }
        const double p = std::max(power[k], fx::kPowerFloor);
        logSum += std::log(p);
        linearSum += p;
        ++count;
    }
    if (count == 0u) {
        return 0.0;
    }
    const double geometricMean = std::exp(logSum / static_cast<double>(count));
    const double arithmeticMean = linearSum / static_cast<double>(count);
    return (arithmeticMean > 0.0) ? (geometricMean / arithmeticMean) : 0.0;
}

/// Three engine seeds. Stated here so the whole sweep is reproducible; nothing
/// about the criterion depends on which three they are.
constexpr std::array<std::uint32_t, 3> kSweepSeeds = {{101u, 202u, 303u}};

/// SC-008's five sweep points, and the x-axis of every Spearman correlation.
constexpr std::array<double, 5> kSweepPoints = {{0.0, 0.25, 0.50, 0.75, 1.0}};

/// A noise source counts as AWAKE at or above half travel. The wake surface is a
/// continuous [0, 1] amount (noise_organism.h:844), so an "awake count" needs a
/// stated threshold; half is the one choice that is not tuning.
constexpr float kAwakeThreshold = 0.5f;

/// Darkness's band edge, as a multiple of the note frequency: the ratio of the
/// energy above 4 f0 to the energy below it (2026-09-19 ruling). The subs and
/// the fundamental sit below the edge, so the figure is sub-immune; the cloud's
/// partials 4..18 sit above it, so the figure IS the tilt.
constexpr double kDarknessBandEdge = 4.0;

/// Stand-in for "infinitely better than a zero baseline". A ratio or a
/// percentage against zero is undefined, and reporting 0 there would turn a
/// metric that went from nothing to something into a FAILURE.
constexpr double kUnboundedImprovement = 1.0e9;

/// Guards log10() of an exactly-zero energy sum.
constexpr double kEnergyFloor = 1.0e-30;

// -----------------------------------------------------------------------------
// One sweep render's measurement
// -----------------------------------------------------------------------------

/// Everything one 60 s sweep render yields: the eleven SC-008 figures, the
/// broadband level the non-silence clause needs, and the folded-concept
/// readbacks. ONE struct, so a render happens ONCE per (macro, point, seed) and
/// every clause reads the same render.
struct SweepSample {
    double darkRatioDb = 0.0;         ///< Darkness (energy above 4 f0 minus energy below, dB)
    double hfEnergyDb = -300.0;       ///< Age      (> 4 kHz)
    double sourceCount = 0.0;         ///< Density  (partials + awake noise sources)
    double bandVariation = 0.0;       ///< Movement
    double octaveOffset = 0.0;        ///< Gravity  (mean |log2(ratio) - nearest int|)
    double flatness = 0.0;            ///< Entropy AND Fog (harmonic band [f0, 20 f0])
    double crestDb = 0.0;             ///< Pressure
    double lowEnergyDb = -300.0;      ///< Weight AND Mass (< 80 Hz, both at C3)
    double lifeEdgesPerMinute = 0.0;  ///< Life
    double rmsDb = -300.0;            ///< the non-silence clause

    // Folded-concept readbacks (FR-068). The cavern fields come off the
    // matrix's own POD - nothing in this phase prepares a CavernVerb - and the
    // voice / engine fields are read back after the render.
    float cavernDecaySeconds = 0.0f;
    float cavernSize = 0.0f;
    float cavernFog = 0.0f;
    float cavernDarkness = 0.0f;
    float bodyDamping = 0.0f;
    float cloudMutation = 0.0f;
    float breathingIrregularity = 0.0f;
    float driftDepthCents = 0.0f;
    // Readback clauses of the three rows re-metricked by the 2026-09-19 ruling
    // (Darkness, Mass, Fog): the targets the metric is meant to see.
    float cloudTiltDb = 0.0f;
    float bodyResonance = 0.0f;
    float subTracking = 0.0f;
    float ghostPeak = 0.0f;
    float smearAmount = 0.0f;
};

/// Density's raw figure: the cloud's active partials plus the noise organism's
/// awake sources, summed - SC-008 row 3, read off the one sounding voice.
[[nodiscard]] double activeSourceCount(const VoragoVoice& voice) noexcept {
    double count = static_cast<double>(voice.cloud().getActivePartialCount());
    const std::size_t sources = voice.noise().getNumSources();
    for (std::size_t s = 0; s < sources; ++s) {
        if (voice.noise().getSourceWakeAmount(s) >= kAwakeThreshold) {
            count += 1.0;
        }
    }
    return count;
}

/// Gravity's raw figure: the mean distance, over the network's live peaks, from
/// log2(peak / note) to the nearest integer. AnchorMode::Hybrid at gravity +1
/// pulls every peak onto its keyed ratio and at -1 mirrors it away
/// (resonance_drift_network.h:631-637), so this number FALLS as the macro rises,
/// which is SC-008's documented direction for the row.
[[nodiscard]] double meanOctaveOffset(const VoragoVoice& voice) noexcept {
    const double note = static_cast<double>(voice.resonance().getNoteFrequency());
    if (!(note > 0.0)) {
        return 0.0;
    }
    const std::size_t peaks = voice.resonance().getNumPeaks();
    double sum = 0.0;
    std::size_t counted = 0u;
    for (std::size_t p = 0; p < peaks; ++p) {
        const double f = static_cast<double>(voice.resonance().getPeakCurrentFrequency(p));
        if (!(f > 0.0)) {
            continue;  // a peak the network has not resolved yet contributes nothing
        }
        const double l2 = std::log2(f / note);
        sum += std::fabs(l2 - std::round(l2));
        ++counted;
    }
    return (counted > 0u) ? (sum / static_cast<double>(counted)) : 0.0;
}

/// @brief ONE sweep render, with @p macros installed, seed @p seed, holding
///        MIDI @p note.
///
/// The spectral window is ALWAYS [10 s, 60 s] in samples. @p renderSamples and
/// @p edgeWindowStartSample exist for the Life row alone: it renders
/// kLifeRenderSamples and counts its edges from kLifeEdgeWindowStartSample;
/// every other row passes kSweepTotalSamples and 0 (edges over the whole 60 s).
///
/// ORDER: the macros are applied AFTER noteOn. noteOn runs silence() +
/// resetForSteal() on the slot (vorago_engine.h:1376-1380), so a matrix applied
/// before it would be writing into a slot that is about to be torn down; and
/// apply() is designed to be legal on a sounding voice (FR-067), so applying
/// afterwards is the order a host would use anyway.
[[nodiscard]] SweepSample renderSweepPoint(const VoragoMacroValues& macros, std::uint32_t seed,
                                           std::uint8_t note, std::size_t renderSamples,
                                           std::size_t edgeWindowStartSample) {
    SweepSample out;

    const VoragoEngineConfig cfg{};  // shipped defaults; the macro is the only variable
    auto engine = makeEngine(kMacroSampleRate, cfg);
    engine->setSeed(seed);
    engine->setPolyphony(1u);
    applyFastAttack(*engine);  // FR-014a: the shipped 20 s attack would eat the window
    engine->noteOn(note, kMacroVelocity);

    VoragoMacroMatrix matrix;
    matrix.setMacros(macros);
    matrix.apply(*engine);

    const VoragoCavernTargets cavern = matrix.computeCavernTargets();
    out.cavernDecaySeconds = cavern.decaySeconds;
    out.cavernSize = cavern.size;
    out.cavernFog = cavern.fog;

    std::vector<float> l(kSweepBlockSamples, 0.0f);
    std::vector<float> r(kSweepBlockSamples, 0.0f);
    std::vector<float> mono;
    mono.reserve(kSweepTotalSamples - kSweepWindowStartSample);

    std::array<bool, VoragoVoice::kNumEventSchedulers> wasActive{};
    double sourceAccum = 0.0;
    double offsetAccum = 0.0;
    std::size_t polls = 0u;
    std::size_t edges = 0u;

    for (std::size_t done = 0; done < renderSamples; done += kSweepBlockSamples) {
        const std::size_t n = std::min(kSweepBlockSamples, renderSamples - done);
        engine->processStereoBlock(l.data(), r.data(), n);
        engine->processOutputStage(l.data(), r.data(), n);

        const VoragoVoice& sounding = engine->getVoice(0);

        // Life: wake AND sleep edges over the five ROUTED destination families.
        // A transition into a family outside the roster is not an edge; a
        // transition out of an event that was counted always is. The state is
        // tracked from sample 0 so an event already running when the edge
        // window opens is not counted twice; only edges INSIDE the window count.
        for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
            const bool active = sounding.scheduler(k).isEventActive();
            const std::uint8_t family = sounding.scheduler(k).getActiveTarget();
            const bool inWindow = (done >= edgeWindowStartSample);
            if (active && !wasActive[k]) {
                if (family < static_cast<std::uint8_t>(VoragoVoice::kNumEventFamilies)) {
                    edges += inWindow ? 1u : 0u;
                    wasActive[k] = true;
                }
            } else if (!active && wasActive[k]) {
                edges += inWindow ? 1u : 0u;
                wasActive[k] = false;
            }
        }

        if ((done + n) > kSweepWindowStartSample && done < kSweepTotalSamples) {
            const std::size_t from =
                (done >= kSweepWindowStartSample) ? 0u : (kSweepWindowStartSample - done);
            const std::size_t to = std::min(n, kSweepTotalSamples - done);
            for (std::size_t i = from; i < to; ++i) {
                mono.push_back(0.5f * (l[i] + r[i]));
            }
            sourceAccum += activeSourceCount(sounding);
            offsetAccum += meanOctaveOffset(sounding);
            ++polls;
        }
    }

    const std::span<const float> window(mono);
    const double edgeWindowSeconds =
        static_cast<double>(renderSamples - edgeWindowStartSample) / kMacroSampleRate;

    const double noteHz = midiToHz(note);
    out.darkRatioDb =
        bandEnergyDb(window, kMacroSampleRate, kDarknessBandEdge * noteHz, kMacroSampleRate * 0.5)
        - bandEnergyDb(window, kMacroSampleRate, 20.0, kDarknessBandEdge * noteHz);
    out.hfEnergyDb = bandEnergyDb(window, kMacroSampleRate, 4000.0, kMacroSampleRate * 0.5);
    out.bandVariation = perBandTotalVariation(window, kMacroSampleRate);
    out.flatness = harmonicBandFlatness(window, kMacroSampleRate, noteHz);
    out.crestDb = crestFactorDb(window);
    out.lowEnergyDb = bandEnergyDb(window, kMacroSampleRate, 20.0, 80.0);
    out.sourceCount = (polls > 0u) ? (sourceAccum / static_cast<double>(polls)) : 0.0;
    out.octaveOffset = (polls > 0u) ? (offsetAccum / static_cast<double>(polls)) : 0.0;
    out.lifeEdgesPerMinute =
        (edgeWindowSeconds > 0.0) ? (static_cast<double>(edges) * 60.0 / edgeWindowSeconds) : 0.0;

    const VoragoVoice& voice = engine->getVoice(0);
    out.cavernDarkness = cavern.darkness;
    out.bodyDamping = voice.bodyA().getDamping();
    out.cloudMutation = voice.cloud().getMutation();
    out.breathingIrregularity = voice.getBreathingIrregularity();
    out.driftDepthCents = voice.getDriftDepthCents();
    out.cloudTiltDb = voice.getSpectralTiltDb();
    out.bodyResonance = voice.getBodyResonance();
    out.subTracking = engine->getSubTrackingAmount();
    out.ghostPeak = engine->getGhostPeakLevel();
    out.smearAmount = engine->getSmearAmount();

    if (mono.empty()) {
        out.rmsDb = -300.0;
    } else {
        double sumSquares = 0.0;
        for (const float s : mono) {
            sumSquares += static_cast<double>(s) * static_cast<double>(s);
        }
        const double meanSquare = sumSquares / static_cast<double>(mono.size());
        out.rmsDb = 10.0 * std::log10(std::max(meanSquare, kEnergyFloor));
    }
    return out;
}

// -----------------------------------------------------------------------------
// SC-008's eleven rows, as data
// -----------------------------------------------------------------------------

/// How a row's endpoint threshold is expressed. Five forms, because the spec
/// table uses five; no row is a bare inequality.
enum class EndpointKind : std::uint8_t {
    PercentLower,
    PercentHigher,
    DbLower,
    DbHigher,
    RatioHigher
};

struct SweepRow {
    VoragoMacro macro;
    const char* name;
    double SweepSample::*metric;
    EndpointKind kind;
    double threshold;
    std::uint8_t note;  ///< the MIDI note this row's renders hold (see kAgeNote / kWeightNote)
};

/// ELEVEN rows. `Depth` is deliberately absent: its metric is a composed-chain
/// level ratio against `CavernVerb::setMix(0)`, which is a Layer 4 measurement
/// and therefore lives in the composed TU (tasks.md T022). Its FOLDED clause is
/// asserted here, on the returned POD.
constexpr std::size_t kNumSweepRows = 11u;

/// THRESHOLDS DERIVED UNDER THE 2026-09-19 RULING (Darkness, Mass, Fog): each
/// is HALF the mean endpoint measured over the three SC-008 seeds with the row's
/// final amounts, rounded to a clean figure - never a guess:
///   Darkness  measured -13.3 / -11.4 / -13.4 dB (mean -12.7)  -> 6 dB
///   Mass      0.25 dB was derived from +0.51 / +0.36 / +0.49 dB (mean +0.46)
///             WITH the Mass -> SubTracking row (tracking 0.6 -> 0.0). The
///             2026-09-19 ruling that made the shipped tracking 1.0 dropped
///             that row, and with the subs fully tracked no remaining Mass row
///             raises the sub band: the final rows measure -0.35 dB (falling),
///             flipping BodyResonance too measures +0.14 dB with rho 0.9 by a
///             hair. NOTHING DERIVES from that, so the constant stands as the
///             last derived figure and the row is reported short.
///   Fog       measured +14.7 / +21.4 / +22.4 %  (mean +19.8)  -> 10 %
/// Every other threshold is SC-008's, untouched.
constexpr double kDarknessThresholdDb = 6.0;
constexpr double kMassThresholdDb = 0.25;
constexpr double kFogThreshold = 0.10;

const std::array<SweepRow, kNumSweepRows> kSweepRows = {{
    // 2026-09-19 ruling: the spectral centroid is pinned by the subharmonic
    // tones and the fundamental on this instrument (56 Hz at C1, moved 0.8 % by
    // a -12 dB/oct tilt), so the row measures the ENERGY RATIO of the band
    // above 4 f0 to the band below it - sub-immune, tilt-sensitive.
    {.macro = VoragoMacro::Darkness,
     .name = "Darkness  energy >4f0 rel. <4f0 (dB)",
     .metric = &SweepSample::darkRatioDb,
     .kind = EndpointKind::DbLower,
     .threshold = kDarknessThresholdDb,
     .note = kMacroNote},
    {.macro = VoragoMacro::Age,
     .name = "Age       HF energy > 4 kHz (dB) @C4",
     .metric = &SweepSample::hfEnergyDb,
     .kind = EndpointKind::DbLower,
     .threshold = 3.0,
     .note = kAgeNote},
    {.macro = VoragoMacro::Density,
     .name = "Density   partials + awake sources",
     .metric = &SweepSample::sourceCount,
     .kind = EndpointKind::PercentHigher,
     .threshold = 0.50,
     .note = kMacroNote},
    {.macro = VoragoMacro::Movement,
     .name = "Movement  per-band total variation",
     .metric = &SweepSample::bandVariation,
     .kind = EndpointKind::PercentHigher,
     .threshold = 0.20,
     .note = kMacroNote},
    {.macro = VoragoMacro::Gravity,
     .name = "Gravity   mean |log2(ratio) - int|",
     .metric = &SweepSample::octaveOffset,
     .kind = EndpointKind::PercentLower,
     .threshold = 0.30,
     .note = kMacroNote},
    {.macro = VoragoMacro::Entropy,
     .name = "Entropy   spectral flatness [f0, 20 f0]",
     .metric = &SweepSample::flatness,
     .kind = EndpointKind::PercentHigher,
     .threshold = 0.25,
     .note = kMacroNote},
    {.macro = VoragoMacro::Pressure,
     .name = "Pressure  crest factor (dB)",
     .metric = &SweepSample::crestDb,
     .kind = EndpointKind::DbLower,
     .threshold = 3.0,
     .note = kMacroNote},
    {.macro = VoragoMacro::Weight,
     .name = "Weight    energy < 80 Hz (dB) @C3",
     .metric = &SweepSample::lowEnergyDb,
     .kind = EndpointKind::DbHigher,
     .threshold = 6.0,
     .note = kWeightNote},
    // 2026-09-19 ruling: per-bin magnitude flux is REPLACED. The smear stage
    // itself ADDS flux (SpectralSmear at 0.9 with decoherence 0 measured 0.052
    // -> 0.066 on a steady drone; the shipped decoherence 0.2 sets the 0.17
    // baseline), so "lower flux" cannot express "blurrier" on this product.
    // Fog is measured as spectral flatness over the harmonic band [f0, 20 f0]
    // (harmonicBandFlatness): the smear and the ghost blur the line spectrum,
    // which RAISES it.
    {.macro = VoragoMacro::Fog,
     .name = "Fog       spectral flatness [f0, 20 f0]",
     .metric = &SweepSample::flatness,
     .kind = EndpointKind::PercentHigher,
     .threshold = kFogThreshold,
     .note = kMacroNote},
    {.macro = VoragoMacro::Life,
     .name = "Life      wake/sleep edges per min [90 s, 210 s]",
     .metric = &SweepSample::lifeEdgesPerMinute,
     .kind = EndpointKind::RatioHigher,
     .threshold = 2.0,
     .note = kMacroNote},
    // 2026-09-19 ruling: the modal-band share is capped near 0 dB on this
    // instrument (the subs are never inside body A's mode bands, so the share
    // could never rise the 4 dB asked; measured -2.8 -> -2.9 dB). Mass is
    // measured as energy below 80 Hz at C3 - Weight's construction - where
    // the band holds the subharmonic tones the row's sign flips now raise.
    {.macro = VoragoMacro::Mass,
     .name = "Mass      energy < 80 Hz (dB) @C3",
     .metric = &SweepSample::lowEnergyDb,
     .kind = EndpointKind::DbHigher,
     .threshold = kMassThresholdDb,
     .note = kWeightNote},
}};

/// The index of each row that carries a folded-concept clause, so the clauses
/// below cannot silently re-point at the wrong macro when the table is edited.
constexpr std::size_t kDarknessRowIndex = 0u;
constexpr std::size_t kAgeRowIndex = 1u;
constexpr std::size_t kEntropyRowIndex = 5u;
constexpr std::size_t kFogRowIndex = 8u;
constexpr std::size_t kMassRowIndex = 10u;

/// The documented SIGN of the Spearman correlation for a row's endpoint form.
[[nodiscard]] double expectedRhoSign(EndpointKind k) noexcept {
    return (k == EndpointKind::PercentLower || k == EndpointKind::DbLower) ? -1.0 : 1.0;
}

/// The row's endpoint figure, in the units its threshold is stated in.
[[nodiscard]] double endpointFigure(EndpointKind k, double v0, double v1) noexcept {
    switch (k) {
        case EndpointKind::PercentLower:
            return (v0 > 0.0) ? ((v0 - v1) / v0) : 0.0;
        case EndpointKind::PercentHigher:
            if (v0 > 0.0) {
                return (v1 - v0) / v0;
            }
            return (v1 > 0.0) ? kUnboundedImprovement : 0.0;
        case EndpointKind::DbLower:
            return v0 - v1;
        case EndpointKind::DbHigher:
            return v1 - v0;
        case EndpointKind::RatioHigher:
            if (v0 > 0.0) {
                return v1 / v0;
            }
            return (v1 > 0.0) ? kUnboundedImprovement : 0.0;
        default:
            return 0.0;
    }
}

/// A macro vector with every macro at its FR-061 neutral and @p macro at @p v.
[[nodiscard]] VoragoMacroValues neutralExcept(VoragoMacro macro, float v) noexcept {
    VoragoMacroValues values{};  // the default IS the FR-061 identity
    setFieldOf(values, macro, v);
    return values;
}

// -----------------------------------------------------------------------------
// SC-009's target readers - an INDEPENDENT statement of the row -> getter map
// -----------------------------------------------------------------------------

/// The Voice-owned half of apply()'s fan-out, read back. Written out here rather
/// than reused from the matrix, so `row.base == getter()` is checked against a
/// SECOND statement of the mapping.
[[nodiscard]] float readVoiceTarget(const VoragoVoice& v, VoragoMacroTarget t) noexcept {
    switch (t) {
        case VoragoMacroTarget::CloudRichness:
            return v.getRichness();
        case VoragoMacroTarget::CloudSpectralTiltDb:
            return v.getSpectralTiltDb();
        case VoragoMacroTarget::CloudMutation:
            return v.getMutation();
        case VoragoMacroTarget::CloudInharmonicity:
            return v.getInharmonicity();
        case VoragoMacroTarget::CloudDriftDepthCents:
            return v.getDriftDepthCents();
        case VoragoMacroTarget::NoiseLevelDb:
            return v.getNoiseLevelDb();
        case VoragoMacroTarget::NoiseWakeBase:
            return v.getNoiseWakeBase();
        case VoragoMacroTarget::NoiseWanderRate:
            return v.getNoiseWanderRate();
        case VoragoMacroTarget::ResonanceGravity:
            return v.getResonanceGravity();
        case VoragoMacroTarget::ResonanceMix:
            return v.getResonanceMix();
        case VoragoMacroTarget::ResonanceWanderRate:
            return v.getResonanceWanderRate();
        case VoragoMacroTarget::EcologyMix:
            return v.getEcologyMix();
        case VoragoMacroTarget::EcologyLoopGain:
            return v.getEcologyLoopGain();
        case VoragoMacroTarget::BodyBlend:
            return v.getBodyBlend();
        case VoragoMacroTarget::BodyDamping:
            return v.getBodyDamping();
        case VoragoMacroTarget::BodyResonance:
            return v.getBodyResonance();
        case VoragoMacroTarget::BodyMix:
            return v.getBodyMix();
        case VoragoMacroTarget::EcosystemDepth:
            return v.getEcosystemDepth();
        case VoragoMacroTarget::EventRateScale:
            return v.getEventRateScale();
        case VoragoMacroTarget::BloomDepth:
            return v.getBloomDepth();
        case VoragoMacroTarget::BloomSpawnRateHz:
            return v.getBloomSpawnRateHz();
        case VoragoMacroTarget::BreathingDepth:
            return v.getBreathingDepth();
        case VoragoMacroTarget::BreathingIrregularity:
            return v.getBreathingIrregularity();
        case VoragoMacroTarget::TidalDepth:
            return v.getTidalDepth();
        default:
            return 0.0f;
    }
}

/// The Engine-owned half, same construction.
[[nodiscard]] float readEngineTarget(const VoragoEngine& e, VoragoMacroTarget t) noexcept {
    switch (t) {
        case VoragoMacroTarget::SubToneLevelOffsetDb:
            return e.getSubToneLevelOffsetDb();
        case VoragoMacroTarget::SubTrackingAmount:
            return e.getSubTrackingAmount();
        case VoragoMacroTarget::SmearAmount:
            return e.getSmearAmount();
        case VoragoMacroTarget::SmearDecoherence:
            return e.getSmearDecoherence();
        case VoragoMacroTarget::SmearTilt:
            return e.getSmearTilt();
        case VoragoMacroTarget::GhostPeakLevel:
            return e.getGhostPeakLevel();
        case VoragoMacroTarget::AtmosBlur:
            return e.getAtmosBlur();
        case VoragoMacroTarget::OutputSaturation:
            return e.getOutputSaturation();
        default:
            return 0.0f;
    }
}

/// The Cavern-owned half, read off the POD.
[[nodiscard]] float readCavernTarget(const VoragoCavernTargets& c, VoragoMacroTarget t) noexcept {
    switch (t) {
        case VoragoMacroTarget::CavernSize:
            return c.size;
        case VoragoMacroTarget::CavernDarkness:
            return c.darkness;
        case VoragoMacroTarget::CavernDecaySeconds:
            return c.decaySeconds;
        case VoragoMacroTarget::CavernFog:
            return c.fog;
        case VoragoMacroTarget::CavernDamperDepth:
            return c.damperDepth;
        case VoragoMacroTarget::CavernMix:
            return c.mix;
        case VoragoMacroTarget::CavernWidth:
            return c.width;
        default:
            return 0.0f;
    }
}

/// The `base` every row on target @p t must agree on. The matrix's own
/// everyRowSharesOneBasePerTarget() makes that agreement a COMPILE error to
/// break, so the FIRST row on the target is THE base.
[[nodiscard]] float baseOfTarget(VoragoMacroTarget t) noexcept {
    for (const auto& row : VoragoMacroMatrix::kRows) {
        if (row.target == t) {
            return row.base;
        }
    }
    return 0.0f;
}

// -----------------------------------------------------------------------------
// The render loop shared by SC-009 clause 3, SC-010 and FR-067
// -----------------------------------------------------------------------------

/// A prepared, sounding engine: seeded, fast-attack, @p voices notes held.
[[nodiscard]] std::unique_ptr<VoragoEngine> makeSoundingEngine(std::uint32_t seed,
                                                               std::size_t voices) {
    const VoragoEngineConfig cfg{};
    auto engine = makeEngine(kMacroSampleRate, cfg);
    engine->setSeed(seed);
    engine->setPolyphony(voices);
    applyFastAttack(*engine);
    for (std::size_t v = 0; v < voices; ++v) {
        engine->noteOn(static_cast<std::uint8_t>(static_cast<std::size_t>(kMacroNote) + (3u * v)),
                       kMacroVelocity);
    }
    return engine;
}

/// Render @p samples frames, in blocks of @p blockSamples, running the output
/// stage on every block. The partition is an ARGUMENT because two renders
/// compared bit-for-bit must share it.
void renderWithOutputStage(VoragoEngine& engine, std::vector<float>& l, std::vector<float>& r,
                           std::size_t samples, std::size_t blockSamples) {
    l.assign(samples, 0.0f);
    r.assign(samples, 0.0f);
    for (std::size_t done = 0; done < samples; done += blockSamples) {
        const std::size_t n = std::min(blockSamples, samples - done);
        engine.processStereoBlock(l.data() + done, r.data() + done, n);
        engine.processOutputStage(l.data() + done, r.data() + done, n);
    }
}

}  // namespace

// =============================================================================
// SC-008 - the eleven macro sweeps
// =============================================================================

TEST_CASE("VoragoMacro_SweepAxes", "[systems][vorago][long]") {
    struct RowResult {
        double rho = 0.0;
        double endpoint = 0.0;
        std::array<double, 5> meanMetric{};
        SweepSample firstSeedAtZero{};
        SweepSample firstSeedAtOne{};
    };

    std::array<RowResult, kNumSweepRows> results{};

    for (std::size_t rowIndex = 0; rowIndex < kNumSweepRows; ++rowIndex) {
        const SweepRow& row = kSweepRows[rowIndex];
        RowResult& result = results[rowIndex];

        double rhoSum = 0.0;
        std::array<double, 5> metricSum{};

        // Life alone renders past the schedulers' pre-roll ceiling and counts
        // its edges from there (kLifeRenderSeconds); every other row renders
        // the 60 s fixture and counts over all of it.
        const bool lifeRow = (row.macro == VoragoMacro::Life);
        const std::size_t renderSamples = lifeRow ? kLifeRenderSamples : kSweepTotalSamples;
        const std::size_t edgeWindowStart = lifeRow ? kLifeEdgeWindowStartSample : 0u;

        for (std::size_t s = 0; s < kSweepSeeds.size(); ++s) {
            std::array<double, 5> series{};
            for (std::size_t p = 0; p < kSweepPoints.size(); ++p) {
                const SweepSample sample = renderSweepPoint(
                    neutralExcept(row.macro, static_cast<float>(kSweepPoints[p])), kSweepSeeds[s],
                    row.note, renderSamples, edgeWindowStart);
                series[p] = sample.*(row.metric);
                metricSum[p] += series[p];
                if (s == 0u && p == 0u) {
                    result.firstSeedAtZero = sample;
                }
                if (s == 0u && p == (kSweepPoints.size() - 1u)) {
                    result.firstSeedAtOne = sample;
                }
            }
            rhoSum += spearmanRho(kSweepPoints, series);
        }

        result.rho = rhoSum / static_cast<double>(kSweepSeeds.size());
        for (std::size_t p = 0; p < kSweepPoints.size(); ++p) {
            result.meanMetric[p] = metricSum[p] / static_cast<double>(kSweepSeeds.size());
        }
        result.endpoint =
            endpointFigure(row.kind, result.meanMetric.front(), result.meanMetric.back());
    }

    // The all-zeros render. Gravity at 0 is its AIR extreme, NOT its neutral -
    // which is the whole point of the clause.
    VoragoMacroValues allZero{};
    for (const VoragoMacro m : kAllMacros) {
        setFieldOf(allZero, m, 0.0f);
    }
    const SweepSample zeros =
        renderSweepPoint(allZero, kSweepSeeds[0], kMacroNote, kSweepTotalSamples, 0u);

    // -- print the WHOLE table before any REQUIRE fires ------------------------
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(4);
        os << "SC-008 macro sweeps (3 seeds x 5 points x 60 s; window [10 s, 60 s] = samples ["
           << kSweepWindowStartSample << ", " << kSweepTotalSamples << "); C1 unless the row says "
           << "@C3 / @C4; Life renders " << kLifeRenderSeconds << " s and counts edges over ["
           << kLifeEdgeWindowStartSeconds << " s, " << kLifeRenderSeconds << " s])\n";
        for (std::size_t i = 0; i < kNumSweepRows; ++i) {
            const RowResult& result = results[i];
            os << "  " << kSweepRows[i].name << "  rho=" << result.rho
               << "  endpoint=" << result.endpoint << "  [";
            for (std::size_t p = 0; p < result.meanMetric.size(); ++p) {
                os << ((p == 0u) ? "" : ", ") << result.meanMetric[p];
            }
            os << "]\n";
        }
        os << "  all-macros-at-zero broadband RMS = " << zeros.rmsDb << " dBFS\n";
        WARN(os.str());
    }

    // -------------------------------------------------------------------------
    // ONE PASS, NO SECTIONS (2026-09-19 ruling). Catch2 re-runs a case body
    // once per SECTION, which re-rendered the whole sweep three times (~55
    // min); every clause below reads the stored results. And CHECK, not
    // REQUIRE, per row, so a miss on one row leaves every other row's verdict
    // visible in the same run - the case still fails on any miss.
    // -------------------------------------------------------------------------

    // -- every row is monotone with the documented sign and clears its threshold
    for (std::size_t i = 0; i < kNumSweepRows; ++i) {
        const SweepRow& row = kSweepRows[i];
        const RowResult& result = results[i];
        INFO(row.name << "  rho=" << result.rho << "  endpoint=" << result.endpoint
                      << "  threshold=" << row.threshold);
        // GRAVITY and PRESSURE are RECORDED AS FAILED by the 2026-09-19 ruling.
        // Both stay measured exactly as SC-008 words them, thresholds untouched:
        //   Gravity   rho -0.93, endpoint 18.0 % against >= 30 %: the shipped
        //             keyed ratios (1.5, 2.98, 5.04, 6, 7.02, 9.98 ...) put the
        //             stone-end floor of mean |log2(ratio) - int| at 0.209 in
        //             theory (0.22 measured with wander) and the best air-end
        //             figure over notes 24-72 at 0.266, so no note reaches 30 %.
        //   Pressure  rho -1.00, endpoint 0.16 dB against >= 3 dB: the crest
        //             floor is the sub + fundamental beating; saturation at its
        //             1.0 ceiling alone moves it 0.4 dB, polyphony 4 the same.
        // The lever for either is a Phase 12 PRODUCT decision (ratio table,
        // a saturator drive row) - NEVER the threshold.
        CHECK((result.rho * expectedRhoSign(row.kind)) >= 0.9);
        CHECK(result.endpoint >= row.threshold);
    }

    // -- the four folded-concept clauses (FR-068) -----------------------------
    {
        // Age carries `Decay`'s SHORTENING half.
        const RowResult& age = results[kAgeRowIndex];
        INFO("Age decaySeconds " << age.firstSeedAtZero.cavernDecaySeconds << " -> "
                                 << age.firstSeedAtOne.cavernDecaySeconds << ", damping "
                                 << age.firstSeedAtZero.bodyDamping << " -> "
                                 << age.firstSeedAtOne.bodyDamping);
        CHECK(age.firstSeedAtOne.cavernDecaySeconds < age.firstSeedAtZero.cavernDecaySeconds);
        CHECK(age.firstSeedAtOne.bodyDamping > age.firstSeedAtZero.bodyDamping);
    }
    {
        // Entropy carries `Instability`: the cloud's mutation and the
        // life-modulator depths both read higher at 1.
        const RowResult& entropy = results[kEntropyRowIndex];
        INFO("Entropy mutation " << entropy.firstSeedAtZero.cloudMutation << " -> "
                                 << entropy.firstSeedAtOne.cloudMutation << ", irregularity "
                                 << entropy.firstSeedAtZero.breathingIrregularity << " -> "
                                 << entropy.firstSeedAtOne.breathingIrregularity << ", drift "
                                 << entropy.firstSeedAtZero.driftDepthCents << " -> "
                                 << entropy.firstSeedAtOne.driftDepthCents);
        CHECK(entropy.firstSeedAtOne.cloudMutation > entropy.firstSeedAtZero.cloudMutation);
        CHECK(entropy.firstSeedAtOne.breathingIrregularity
              > entropy.firstSeedAtZero.breathingIrregularity);
        CHECK(entropy.firstSeedAtOne.driftDepthCents > entropy.firstSeedAtZero.driftDepthCents);
    }
    {
        // Fog carries `Distance`: the distance-filtering target reads MORE
        // filtered at 1.
        const RowResult& fog = results[kFogRowIndex];
        INFO("Fog cavern fog " << fog.firstSeedAtZero.cavernFog << " -> "
                               << fog.firstSeedAtOne.cavernFog);
        CHECK(fog.firstSeedAtOne.cavernFog > fog.firstSeedAtZero.cavernFog);
    }
    {
        // Depth carries `Decay`'s LENGTHENING half. Asserted on the returned POD
        // ONLY - the render half of Depth's row is the composed TU's (T022).
        VoragoMacroMatrix low;
        VoragoMacroMatrix high;
        low.setMacro(VoragoMacro::Depth, 0.0f);
        high.setMacro(VoragoMacro::Depth, 1.0f);
        const VoragoCavernTargets depthLow = low.computeCavernTargets();
        const VoragoCavernTargets depthHigh = high.computeCavernTargets();
        INFO("Depth decaySeconds " << depthLow.decaySeconds << " -> " << depthHigh.decaySeconds
                                   << ", size " << depthLow.size << " -> " << depthHigh.size);
        CHECK(depthHigh.decaySeconds > depthLow.decaySeconds);
        CHECK(depthHigh.size > depthLow.size);
    }

    // -- the readback clauses of the three re-metricked rows (2026-09-19) -----
    {
        // Darkness: the tilt reads LOWER (steeper) and the cavern darkness
        // target HIGHER at 1, as the rows document.
        const RowResult& dark = results[kDarknessRowIndex];
        INFO("Darkness tilt " << dark.firstSeedAtZero.cloudTiltDb << " -> "
                              << dark.firstSeedAtOne.cloudTiltDb << " dB/oct, cavern darkness "
                              << dark.firstSeedAtZero.cavernDarkness << " -> "
                              << dark.firstSeedAtOne.cavernDarkness);
        CHECK(dark.firstSeedAtOne.cloudTiltDb < dark.firstSeedAtZero.cloudTiltDb);
        CHECK(dark.firstSeedAtOne.cavernDarkness > dark.firstSeedAtZero.cavernDarkness);
    }
    {
        // Mass: body resonance reads HIGHER; the sub-tracking amount reads
        // UNCHANGED at the shipped 1.0 (the Mass -> SubTracking row was dropped
        // by the 2026-09-19 ruling that made the default fully tracked, and its
        // claim row carries amount 0). There is no Mass -> BodyBlend row either
        // (plan S7.3 correction 2).
        const RowResult& mass = results[kMassRowIndex];
        INFO("Mass body resonance " << mass.firstSeedAtZero.bodyResonance << " -> "
                                    << mass.firstSeedAtOne.bodyResonance << ", sub tracking "
                                    << mass.firstSeedAtZero.subTracking << " -> "
                                    << mass.firstSeedAtOne.subTracking);
        CHECK(mass.firstSeedAtOne.bodyResonance > mass.firstSeedAtZero.bodyResonance);
        CHECK(mass.firstSeedAtOne.subTracking == mass.firstSeedAtZero.subTracking);
        CHECK(mass.firstSeedAtOne.subTracking == 1.0f);
    }
    {
        // Fog: the ghost burst peak and the smear amount both read HIGHER at 1.
        const RowResult& fog = results[kFogRowIndex];
        INFO("Fog ghost " << fog.firstSeedAtZero.ghostPeak << " -> " << fog.firstSeedAtOne.ghostPeak
                          << ", smear " << fog.firstSeedAtZero.smearAmount << " -> "
                          << fog.firstSeedAtOne.smearAmount);
        CHECK(fog.firstSeedAtOne.ghostPeak > fog.firstSeedAtZero.ghostPeak);
        CHECK(fog.firstSeedAtOne.smearAmount > fog.firstSeedAtZero.smearAmount);
    }

    // -- a macro set of all zeros is NOT a mute -------------------------------
    {
        INFO("broadband RMS over [10 s, 60 s] = " << zeros.rmsDb << " dBFS");
        CHECK(zeros.rmsDb > -60.0);
    }
}

// =============================================================================
// SC-009 - the neutral IS the identity
// =============================================================================

TEST_CASE("VoragoMacro_NeutralIsIdentity", "[systems][vorago]") {
    const VoragoEngineConfig cfg{};

    SECTION("1. every row's base equals the value read back immediately after prepare()") {
        auto engine = makeEngine(kMacroSampleRate, cfg);
        REQUIRE(engine->isPrepared());
        const VoragoVoice& voice = engine->getVoice(0);
        const VoragoCavernTargets defaults{};

        for (std::size_t i = 0; i < VoragoMacroMatrix::kRows.size(); ++i) {
            const auto& row = VoragoMacroMatrix::kRows[i];
            INFO("kRows[" << i << "]  macro " << static_cast<int>(row.macro) << "  target "
                          << static_cast<int>(row.target) << "  base " << row.base);
            switch (row.owner) {
                case VoragoMacroTargetOwner::Voice:
                    REQUIRE(readVoiceTarget(voice, row.target) == row.base);
                    break;
                case VoragoMacroTargetOwner::Engine:
                    REQUIRE(readEngineTarget(*engine, row.target) == row.base);
                    break;
                case VoragoMacroTargetOwner::Cavern:
                default:
                    REQUIRE(readCavernTarget(defaults, row.target) == row.base);
                    break;
            }
        }
    }

    SECTION("2. at the neutral apply() leaves every writable target at exactly its base") {
        auto engine = makeEngine(kMacroSampleRate, cfg);
        const VoragoMacroMatrix matrix;  // a default-constructed matrix IS the identity
        matrix.apply(*engine);

        const VoragoVoice& voice = engine->getVoice(0);
        const VoragoCavernTargets cavern = matrix.computeCavernTargets();

        for (std::size_t t = 0; t < VoragoMacroMatrix::kNumTargets; ++t) {
            const auto target = static_cast<VoragoMacroTarget>(t);
            const float base = baseOfTarget(target);
            INFO("target " << t << "  base " << base);
            switch (VoragoMacroMatrix::ownerOfTarget(target)) {
                case VoragoMacroTargetOwner::Voice:
                    REQUIRE(readVoiceTarget(voice, target) == base);
                    break;
                case VoragoMacroTargetOwner::Engine:
                    REQUIRE(readEngineTarget(*engine, target) == base);
                    break;
                case VoragoMacroTargetOwner::Cavern:
                default:
                    REQUIRE(readCavernTarget(cavern, target) == base);
                    break;
            }
        }

        // And the POD as a whole is EXACTLY the default table.
        const VoragoCavernTargets defaults{};
        REQUIRE(cavern.size == defaults.size);
        REQUIRE(cavern.darkness == defaults.darkness);
        REQUIRE(cavern.decaySeconds == defaults.decaySeconds);
        REQUIRE(cavern.fog == defaults.fog);
        REQUIRE(cavern.damperDepth == defaults.damperDepth);
        REQUIRE(cavern.mix == defaults.mix);
        REQUIRE(cavern.width == defaults.width);
    }

    SECTION("3. a 10 s render is BIT-IDENTICAL with the matrix never applied") {
        constexpr std::uint32_t kSeed = 4242u;
        constexpr std::size_t kSamples = static_cast<std::size_t>(10.0 * kMacroSampleRate);
        constexpr std::size_t kBlock = 512u;

        auto without = makeSoundingEngine(kSeed, VoragoEngine::kDefaultPolyphony);
        std::vector<float> withoutL;
        std::vector<float> withoutR;
        renderWithOutputStage(*without, withoutL, withoutR, kSamples, kBlock);

        auto applied = makeSoundingEngine(kSeed, VoragoEngine::kDefaultPolyphony);
        const VoragoMacroMatrix matrix;  // at the neutral
        matrix.apply(*applied);
        std::vector<float> appliedL;
        std::vector<float> appliedR;
        renderWithOutputStage(*applied, appliedL, appliedR, kSamples, kBlock);

        REQUIRE(appliedL.size() == withoutL.size());
        REQUIRE(appliedR.size() == withoutR.size());
        REQUIRE(std::memcmp(appliedL.data(), withoutL.data(), appliedL.size() * sizeof(float))
                == 0);
        REQUIRE(std::memcmp(appliedR.data(), withoutR.data(), appliedR.size() * sizeof(float))
                == 0);
    }
}

// =============================================================================
// SC-010 - automating a macro produces no zipper
// =============================================================================

TEST_CASE("VoragoMacro_NoZipper", "[systems][vorago][long]") {
    // 13 s: 5 s of settling, a 5 s ramp, then 3 s of hold - which is exactly
    // enough for a 2 s reference window on EACH side of the ramp with the 64 ms
    // guard band the criterion asks for.
    constexpr std::size_t kChunk = VoragoEngine::kControlChunkSamples;  // 64 - the macro grid
    constexpr std::size_t kTotal = static_cast<std::size_t>(13.0 * kMacroSampleRate);
    constexpr std::size_t kRampStart = static_cast<std::size_t>(5.0 * kMacroSampleRate);
    constexpr std::size_t kRampLength = static_cast<std::size_t>(5.0 * kMacroSampleRate);
    constexpr std::size_t kRampEnd = kRampStart + kRampLength;
    constexpr std::size_t kGuard = static_cast<std::size_t>(0.064 * kMacroSampleRate);   // 64 ms
    constexpr std::size_t kReference = static_cast<std::size_t>(2.0 * kMacroSampleRate);  // 2 s
    constexpr std::size_t kWindow = static_cast<std::size_t>(0.020 * kMacroSampleRate);   // 20 ms
    constexpr double kBound = 1.5;

    static_assert(kRampStart > (kGuard + kReference), "the pre-ramp reference window must fit");
    static_assert(kTotal >= (kRampEnd + kGuard + kReference),
                  "the post-ramp reference window must fit");

    std::array<double, kAllMacros.size()> rampStat{};
    std::array<double, kAllMacros.size()> baselineStat{};

    for (std::size_t m = 0; m < kAllMacros.size(); ++m) {
        auto engine = makeSoundingEngine(31u, 1u);
        VoragoMacroMatrix matrix;

        std::vector<float> l(kChunk, 0.0f);
        std::vector<float> r(kChunk, 0.0f);
        std::vector<float> mono(kTotal, 0.0f);

        for (std::size_t done = 0; done < kTotal; done += kChunk) {
            const std::size_t n = std::min(kChunk, kTotal - done);

            float value = 0.0f;
            if (done >= kRampEnd) {
                value = 1.0f;
            } else if (done > kRampStart) {
                value = static_cast<float>(static_cast<double>(done - kRampStart)
                                           / static_cast<double>(kRampLength));
            }
            matrix.setMacro(kAllMacros[m], value);
            matrix.apply(*engine);

            engine->processStereoBlock(l.data(), r.data(), n);
            engine->processOutputStage(l.data(), r.data(), n);
            for (std::size_t i = 0; i < n; ++i) {
                mono[done + i] = 0.5f * (l[i] + r[i]);
            }
        }

        const std::span<const float> all(mono);
        rampStat[m] = maxDeltaInWindow(all.subspan(kRampStart, kRampLength), kWindow);
        const double pre =
            maxDeltaInWindow(all.subspan(kRampStart - kGuard - kReference, kReference), kWindow);
        const double post = maxDeltaInWindow(all.subspan(kRampEnd + kGuard, kReference), kWindow);
        baselineStat[m] = std::max(pre, post);
    }

    {
        std::ostringstream os;
        os << std::scientific << std::setprecision(4);
        os << "SC-010 zipper statistic (20 ms window; reference 64 ms clear of the ramp)\n";
        for (std::size_t m = 0; m < kAllMacros.size(); ++m) {
            const double ratio = (baselineStat[m] > 0.0) ? (rampStat[m] / baselineStat[m]) : 0.0;
            os << "  macro " << static_cast<int>(kAllMacros[m]) << "  ramp=" << rampStat[m]
               << "  reference=" << baselineStat[m] << "  ratio=" << ratio << "\n";
        }
        WARN(os.str());
    }

    for (std::size_t m = 0; m < kAllMacros.size(); ++m) {
        INFO("macro index " << static_cast<int>(kAllMacros[m]) << "  ramp " << rampStat[m]
                            << "  reference " << baselineStat[m]);
        REQUIRE(baselineStat[m] > 0.0);
        REQUIRE(rampStat[m] <= (kBound * baselineStat[m]));
    }
}

// =============================================================================
// FR-067 - apply() is IDEMPOTENT
// =============================================================================
// WHY THIS CANNOT BE FOLDED INTO SC-009 OR SC-010. SC-009 clause 3 compares two
// renders at the NEUTRAL, where every forwarder writes the value its target
// already holds; SC-010 measures a zipper DURING a ramp, where a forwarder that
// re-arms a ramp on every unchanged write looks exactly like a correct one. Only
// a NON-NEUTRAL, UNCHANGED, re-applied-every-chunk render separates them: a
// setBodyBlend / setSmearAmount / setNoiseLevelDb forwarder that called snapTo
// instead of setTarget would pass the entire rest of this suite while stepping
// the instrument on EVERY block at any non-neutral macro setting.
// =============================================================================

TEST_CASE("VoragoMacro_ApplyIsIdempotent", "[systems][vorago]") {
    constexpr std::uint32_t kSeed = 909u;
    constexpr std::size_t kSamples = static_cast<std::size_t>(10.0 * kMacroSampleRate);
    constexpr std::size_t kChunk = VoragoEngine::kControlChunkSamples;  // 64

    // THE stated non-neutral vector: every macro at 0.75, Gravity at 0.85.
    VoragoMacroValues macros{};
    for (const VoragoMacro m : kAllMacros) {
        setFieldOf(macros, m, 0.75f);
    }
    setFieldOf(macros, VoragoMacro::Gravity, 0.85f);

    VoragoMacroMatrix matrix;
    matrix.setMacros(macros);

    // -- apply() ONCE, before the render --------------------------------------
    auto onceEngine = makeSoundingEngine(kSeed, VoragoEngine::kDefaultPolyphony);
    matrix.apply(*onceEngine);
    std::vector<float> onceL;
    std::vector<float> onceR;
    renderWithOutputStage(*onceEngine, onceL, onceR, kSamples, kChunk);

    // -- apply() at EVERY 64-sample control chunk -----------------------------
    // The partition is the same 64 samples in both renders, so the ONLY
    // difference between them is the number of apply() calls.
    auto everyEngine = makeSoundingEngine(kSeed, VoragoEngine::kDefaultPolyphony);
    std::vector<float> everyL(kSamples, 0.0f);
    std::vector<float> everyR(kSamples, 0.0f);
    for (std::size_t done = 0; done < kSamples; done += kChunk) {
        const std::size_t n = std::min(kChunk, kSamples - done);
        matrix.apply(*everyEngine);
        everyEngine->processStereoBlock(everyL.data() + done, everyR.data() + done, n);
        everyEngine->processOutputStage(everyL.data() + done, everyR.data() + done, n);
    }

    REQUIRE(onceL.size() == everyL.size());
    REQUIRE(onceR.size() == everyR.size());
    REQUIRE(std::memcmp(onceL.data(), everyL.data(), onceL.size() * sizeof(float)) == 0);
    REQUIRE(std::memcmp(onceR.data(), everyR.data(), onceR.size() * sizeof(float)) == 0);
}
