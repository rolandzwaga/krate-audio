// ==============================================================================
// Layer 3: System Tests - BassStereoField, interface / law / WIDE / stage unit TU
//                          (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// Reference: specs/profundum-phase7-stereo-field/spec.md (FR-011, FR-034, FR-053, SC-004(b)(c), SC-005 gates, SC-009(a')(c), SC-011, SC-012, SC-013(c), SC-017, SC-018/019/020/021 law)
//            specs/profundum-phase7-stereo-field/tasks.md (T002 wires this TU, T003 - T006 fill it, each appending)
//
// T003: constants, controls, validation, lifecycle, control rate and MOTION (FR-001 - FR-004, FR-020,
//       FR-028, FR-030 source side, FR-031, FR-033 - FR-036; SC-008 law parts, SC-020 law; plan S3, S5,
//       S4.1, S7.1, C-4, C-7, C-9, C-11).
// T006: the post-sum safety stage gates, determinism and RT safety (FR-001, FR-020 - FR-028,
//       FR-050 - FR-053, FR-062, FR-071, FR-074; SC-004(b)(c), SC-005, SC-009(c), SC-011, SC-012,
//       SC-020 stage; plan S7, S8, C-1, C-2, C-3, C-4, C-18, R-11).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/stereo_field.h>

#include "bass_stereo_field_probe.h"
#include "mass_dynamics_test_helpers.h"   // T006: MassDynamicsTest::gridDriver / anchorControlsFor (read-only)

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using Krate::DSP::BassStereoField;
using BsfControls = Krate::DSP::BassStereoField::Controls;
using BsfMode = Krate::DSP::BassStereoField::ImageMode;
using BsfSource = Krate::DSP::BassStereoField::MotionSource;
using BsfProbe = Krate::DSP::detail::BassStereoFieldProbe;
using Krate::DSP::NoteModifier;
using Krate::DSP::NoteValue;

constexpr double kBsfFs = 48000.0;
constexpr std::size_t kBsfBlock = 512;

/// A float from its bit pattern through a volatile read, so fast-math cannot fold the NaN / Inf away
/// (memory note: NaN/Inf under -ffast-math in tests).
float bsfFromBits(std::uint32_t bits) {
    volatile std::uint32_t v = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(v));
}

float bsfNaN() { return bsfFromBits(0x7FC00000u); }
float bsfPosInf() { return bsfFromBits(0x7F800000u); }
float bsfNegInf() { return bsfFromBits(0xFF800000u); }

std::uint32_t bsfBits(float x) { return std::bit_cast<std::uint32_t>(x); }

std::string bsfFmt(double x) {
    std::ostringstream os;
    os << std::setprecision(6) << x;
    return os.str();
}

/// Prepared at 48 kHz with `c`, then reset (so the law crossover is snapped, C-4).
void bsfPrepare(BassStereoField& f, const BsfControls& c, double fs = kBsfFs) {
    f.setControls(c);
    f.prepare(fs, kBsfBlock);
    f.reset();
}

/// The six float fields, for the per-field clamp / keep arms.
using BsfFloatField = float BsfControls::*;
constexpr std::array<BsfFloatField, 6> kBsfFloatFields = {
    &BsfControls::spread,      &BsfControls::gravity,     &BsfControls::crossoverHz,
    &BsfControls::curve,       &BsfControls::motionDepth, &BsfControls::motionRate};
constexpr std::array<const char*, 6> kBsfFloatFieldNames = {"spread", "gravity", "crossoverHz",
                                                            "curve",  "motionDepth", "motionRate"};

/// Bit-equal comparison of every field of two Controls (floats by bit pattern).
bool bsfControlsBitEqual(const BsfControls& a, const BsfControls& b) {
    for (const auto field : kBsfFloatFields) {
        if (bsfBits(a.*field) != bsfBits(b.*field)) {
            return false;
        }
    }
    return a.mode == b.mode && a.motionSource == b.motionSource && a.cycleSync == b.cycleSync &&
           a.cycleNote == b.cycleNote && a.cycleModifier == b.cycleModifier;
}

/// A valid, non-default start for the keep arms.
BsfControls bsfValidStart() {
    BsfControls c{};
    c.mode = BsfMode::Wide;
    c.spread = 0.3f;
    c.gravity = 0.7f;
    c.crossoverHz = 180.0f;
    c.curve = 0.2f;
    c.motionDepth = 0.4f;
    c.motionRate = 0.6f;
    c.motionSource = BsfSource::Cycle;
    c.cycleSync = true;
    c.cycleNote = NoteValue::Half;
    c.cycleModifier = NoteModifier::Dotted;
    return c;
}

std::string bsfEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase7-stereo-field/evidence/";
}

constexpr std::array<const char*, 10> kBsfNoteNames = {"QuadrupleWhole", "TripleWhole", "DoubleWhole",
                                                       "Whole",          "Half",        "Quarter",
                                                       "Eighth",         "Sixteenth",   "ThirtySecond",
                                                       "SixtyFourth"};
constexpr std::array<const char*, 3> kBsfModifierNames = {"None", "Dotted", "Triplet"};

}  // namespace

// ==============================================================================
// FR-001: constants (plan S3; C-8 the field keeps its own interval and partial count)
// ==============================================================================

TEST_CASE("BassStereoField_ConstantsMatchPlan", "[systems][profundum][stereo-field][fr-001]") {
    // plan S1 C-8: the field does not include profundum_core.h; its copies must equal the core's.
    static_assert(BassStereoField::kControlInterval == Krate::DSP::ProfundumCore::kControlInterval);
    static_assert(BassStereoField::kControlInterval == 32);
    static_assert(BassStereoField::kControlInterval == Krate::DSP::MassDynamics::kControlInterval);
    static_assert(BassStereoField::kDefaultPartials == Krate::DSP::ProfundumCore::kDefaultPartials);
    static_assert(BassStereoField::kDefaultPartials == 64);
    static_assert(BassStereoField::kMinCrossoverHz == 60.0f);
    static_assert(BassStereoField::kMaxCrossoverHz == 250.0f);
    static_assert(BassStereoField::kDefaultCrossoverHz == 120.0f);
    static_assert(BassStereoField::kStageCrossoverMultiple == 2.0f);
    static_assert(BassStereoField::kUpperKneeOctaves == 2.0f);       // plan S1 C-5
    static_assert(BassStereoField::kLowerKneeMaxOctaves == 0.25f);   // plan S1 C-5
    static_assert(BassStereoField::kMaxCurveExponent == 4.0f);       // plan S1 C-5
    static_assert(BassStereoField::kMaxPanStepPerInterval == 0.003f);  // plan S1 C-6
    static_assert(BassStereoField::kMotionExcursion == 0.5f);
    static_assert(BassStereoField::kCycleMinHz == 0.05f);   // plan S1 C-9
    static_assert(BassStereoField::kCycleMaxHz == 1.0f);
    static_assert(BassStereoField::kDriftTauMaxSec == 30.0f);
    static_assert(BassStereoField::kDriftTauMinSec == 0.5f);
    static_assert(BassStereoField::kDefaultTempoBpm == 120.0f);
    static_assert(BassStereoField::kWideCeilingRectifyFracture == 0.10f);
    static_assert(BassStereoField::kSideGainSmoothMs == 10.0f);
    static_assert(BassStereoField::kModeCrossfadeMs == 10.0f);
    static_assert(BassStereoField::kCrossoverSmoothMs == 20.0f);
    static_assert(BassStereoField::kStageCrossoverSmoothMs == Krate::DSP::kMinSmoothingTimeMs);
    static_assert(BassStereoField::kDefaultDriftSeed == Krate::DSP::BrownianDrift::kDefaultDriftSeed);
    static_assert(BassStereoField::latencySamples() == 0);
    static_assert(BassStereoField::kSideFloorGain > 0.0f && BassStereoField::kSideFloorGain < 1.0f);  // plan S1 C-11
    static_assert(BassStereoField::kWideOffsetScale > 0.0f && BassStereoField::kWideOffsetScale <= 1.0f);
    static_assert(BassStereoField::kCycleSyncFold);  // plan S1 C-9 (A), ratified 2026-10-18

    CHECK(BassStereoField::latencySamples() == 0u);
}

// ==============================================================================
// FR-002: Controls defaults
// ==============================================================================

TEST_CASE("BassStereoField_ControlsDefaults", "[systems][profundum][stereo-field][fr-002]") {
    static_assert(std::is_aggregate_v<BsfControls>);
    const BsfControls c{};
    CHECK(c.mode == BsfMode::Spread);
    CHECK(c.spread == 0.5f);
    CHECK(c.gravity == 0.5f);
    CHECK(c.crossoverHz == 120.0f);
    CHECK(c.curve == 0.5f);
    CHECK(c.motionDepth == 0.0f);
    CHECK(c.motionRate == 0.3f);
    CHECK(c.motionSource == BsfSource::Drift);
    CHECK(c.cycleSync == false);
    CHECK(c.cycleNote == NoteValue::Whole);
    CHECK(c.cycleModifier == NoteModifier::None);
}

// ==============================================================================
// FR-001 / FR-002: setControls clamps, keeps on non-finite / undeclared, latches until advanceControl
// ==============================================================================

TEST_CASE("BassStereoField_SetControlsClampsAndKeeps", "[systems][profundum][stereo-field][fr-001][fr-002]") {
    BassStereoField f;
    bsfPrepare(f, BsfControls{});

    struct ClampCase {
        BsfFloatField field;
        float in;
        float expected;
    };
    const std::array<ClampCase, 11> clampCases = {{
        {&BsfControls::spread, 1.5f, 1.0f},
        {&BsfControls::spread, -0.2f, 0.0f},
        {&BsfControls::gravity, -1.0f, 0.0f},
        {&BsfControls::gravity, 2.0f, 1.0f},
        {&BsfControls::crossoverHz, 30.0f, 60.0f},
        {&BsfControls::crossoverHz, 400.0f, 250.0f},
        {&BsfControls::curve, 2.0f, 1.0f},
        {&BsfControls::curve, -1.0f, 0.0f},
        {&BsfControls::motionDepth, 3.0f, 1.0f},
        {&BsfControls::motionRate, -1.0f, 0.0f},
        {&BsfControls::motionRate, 5.0f, 1.0f},
    }};

    SECTION("out-of-range floats clamp") {
        for (const auto& cc : clampCases) {
            const BsfControls start = bsfValidStart();
            f.setControls(start);
            BsfControls c = start;
            c.*(cc.field) = cc.in;
            f.setControls(c);
            INFO("in " << bsfFmt(cc.in) << " read back " << bsfFmt(f.controls().*(cc.field)));
            CHECK(bsfBits(f.controls().*(cc.field)) == bsfBits(cc.expected));
            // No other field moved.
            BsfControls expected = start;
            expected.*(cc.field) = cc.expected;
            CHECK(bsfControlsBitEqual(f.controls(), expected));
        }
    }

    SECTION("a non-finite float keeps that field and changes no other") {
        const std::array<float, 3> bad = {bsfNaN(), bsfPosInf(), bsfNegInf()};
        for (std::size_t i = 0; i < kBsfFloatFields.size(); ++i) {
            for (const float b : bad) {
                const BsfControls start = bsfValidStart();
                f.setControls(start);
                BsfControls c = start;
                c.*(kBsfFloatFields[i]) = b;
                f.setControls(c);
                INFO("field " << kBsfFloatFieldNames[i] << " bad bits 0x" << std::hex << bsfBits(b));
                CHECK(bsfControlsBitEqual(f.controls(), start));
            }
        }
    }

    SECTION("an undeclared enum value keeps the previous enum") {
        const BsfControls start = bsfValidStart();
        f.setControls(start);
        BsfControls c = start;
        c.mode = static_cast<BsfMode>(7);
        f.setControls(c);
        CHECK(f.controls().mode == start.mode);
        CHECK(bsfControlsBitEqual(f.controls(), start));

        c = start;
        c.motionSource = static_cast<BsfSource>(9);
        f.setControls(c);
        CHECK(f.controls().motionSource == start.motionSource);
        CHECK(bsfControlsBitEqual(f.controls(), start));
    }

    SECTION("latched until advanceControl (plan S1 C-7, FR-074)") {
        BassStereoField g;
        bsfPrepare(g, BsfControls{});
        const float fcBefore = g.smoothedCrossoverHz();
        const float stageBefore = g.stageCrossoverHz();
        g.setControls({.crossoverHz = 250.0f});
        CHECK(bsfBits(g.smoothedCrossoverHz()) == bsfBits(fcBefore));
        CHECK(bsfBits(g.stageCrossoverHz()) == bsfBits(stageBefore));
        g.advanceControl();
        INFO("fc " << bsfFmt(fcBefore) << " -> " << bsfFmt(g.smoothedCrossoverHz()) << ", stage "
                   << bsfFmt(stageBefore) << " -> " << bsfFmt(g.stageCrossoverHz()));
        CHECK(bsfBits(g.smoothedCrossoverHz()) != bsfBits(fcBefore));
        CHECK(bsfBits(g.stageCrossoverHz()) != bsfBits(stageBefore));
    }
}

// ==============================================================================
// FR-003 / SC-021: isValidControls accepts exactly the clamped box
// ==============================================================================

TEST_CASE("BassStereoField_IsValidControls", "[systems][profundum][stereo-field][fr-003][sc-021]") {
    CHECK(BassStereoField::isValidControls(BsfControls{}));

    struct Bounds {
        BsfFloatField field;
        float lo;
        float hi;
    };
    const std::array<Bounds, 6> bounds = {{
        {&BsfControls::spread, 0.0f, 1.0f},
        {&BsfControls::gravity, 0.0f, 1.0f},
        {&BsfControls::crossoverHz, 60.0f, 250.0f},
        {&BsfControls::curve, 0.0f, 1.0f},
        {&BsfControls::motionDepth, 0.0f, 1.0f},
        {&BsfControls::motionRate, 0.0f, 1.0f},
    }};

    for (std::size_t i = 0; i < bounds.size(); ++i) {
        const auto& b = bounds[i];
        INFO("field " << kBsfFloatFieldNames[i]);
        BsfControls c{};
        c.*(b.field) = b.lo;
        CHECK(BassStereoField::isValidControls(c));
        c.*(b.field) = b.hi;
        CHECK(BassStereoField::isValidControls(c));
        c.*(b.field) = std::nextafter(b.lo, -1000.0f);
        CHECK_FALSE(BassStereoField::isValidControls(c));
        c.*(b.field) = std::nextafter(b.hi, 1000.0f);
        CHECK_FALSE(BassStereoField::isValidControls(c));
        c.*(b.field) = bsfNaN();
        CHECK_FALSE(BassStereoField::isValidControls(c));
        c.*(b.field) = bsfPosInf();
        CHECK_FALSE(BassStereoField::isValidControls(c));
        c.*(b.field) = bsfNegInf();
        CHECK_FALSE(BassStereoField::isValidControls(c));
    }

    {
        BsfControls c{};
        c.mode = static_cast<BsfMode>(7);
        CHECK_FALSE(BassStereoField::isValidControls(c));
        c = BsfControls{};
        c.motionSource = static_cast<BsfSource>(7);
        CHECK_FALSE(BassStereoField::isValidControls(c));
        c = BsfControls{};
        c.cycleNote = static_cast<NoteValue>(17);
        CHECK_FALSE(BassStereoField::isValidControls(c));
        c = BsfControls{};
        c.cycleModifier = static_cast<NoteModifier>(7);
        CHECK_FALSE(BassStereoField::isValidControls(c));
    }

    // SC-021: the box is convex, so a field-wise linear interpolation of two valid sets is valid.
    std::mt19937 rng{7};
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> xover(60.0f, 250.0f);
    std::uniform_int_distribution<int> modeDist(0, 2);
    std::uniform_int_distribution<int> srcDist(0, 1);
    std::uniform_int_distribution<int> noteDist(0, 9);
    std::uniform_int_distribution<int> modDist(0, 2);
    const auto randomPair = [&](BsfControls& a, BsfControls& b) {
        a.mode = static_cast<BsfMode>(modeDist(rng));
        a.motionSource = static_cast<BsfSource>(srcDist(rng));
        a.cycleSync = srcDist(rng) == 1;
        a.cycleNote = static_cast<NoteValue>(noteDist(rng));
        a.cycleModifier = static_cast<NoteModifier>(modDist(rng));
        b.mode = a.mode;
        b.motionSource = a.motionSource;
        b.cycleSync = a.cycleSync;
        b.cycleNote = a.cycleNote;
        b.cycleModifier = a.cycleModifier;
        for (BsfControls* p : {&a, &b}) {
            p->spread = unit(rng);
            p->gravity = unit(rng);
            p->crossoverHz = xover(rng);
            p->curve = unit(rng);
            p->motionDepth = unit(rng);
            p->motionRate = unit(rng);
        }
    };
    int invalid = 0;
    for (int pair = 0; pair < 20; ++pair) {
        BsfControls a{};
        BsfControls b{};
        randomPair(a, b);
        REQUIRE(BassStereoField::isValidControls(a));
        REQUIRE(BassStereoField::isValidControls(b));
        for (const float t : {0.25f, 0.5f, 0.75f}) {
            BsfControls m = a;
            for (const auto field : kBsfFloatFields) {
                m.*field = std::lerp(a.*field, b.*field, t);
            }
            if (!BassStereoField::isValidControls(m)) {
                ++invalid;
            }
        }
    }
    CHECK(invalid == 0);
}

// ==============================================================================
// FR-031 / SC-008: the MOTION rate maps (plan S1 C-9, S5)
// ==============================================================================

TEST_CASE("BassStereoField_MotionRateMaps", "[systems][profundum][stereo-field][fr-031][sc-008]") {
    // plan S1 C-9: Cycle f = 0.05 * 20^rate, Drift tau = 30 * (0.5 / 30)^rate.
    BassStereoField f;
    bsfPrepare(f, BsfControls{});

    struct Row {
        float rate;
        float cycleHz;
        float tauSec;
    };
    const std::array<Row, 3> rows = {{{0.0f, 0.05f, 30.0f}, {0.5f, 0.223607f, 3.87298f}, {1.0f, 1.0f, 0.5f}}};
    for (const auto& r : rows) {
        BsfControls c{};
        c.motionRate = r.rate;
        f.setControls(c);
        f.advanceControl();
        INFO("rate " << bsfFmt(r.rate) << " cycle " << bsfFmt(f.cycleFrequencyHz()) << " Hz, tau "
                     << bsfFmt(f.driftTauSeconds()) << " s");
        CHECK(std::abs(f.cycleFrequencyHz() - r.cycleHz) <= 1e-5f);
        CHECK(std::abs(f.driftTauSeconds() - r.tauSec) <= 1e-4f);
    }

    for (const float rate : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        BsfControls c{};
        c.motionRate = rate;
        f.setControls(c);
        f.advanceControl();
        INFO("rate " << bsfFmt(rate) << " tau " << bsfFmt(f.driftTauSeconds()));
        CHECK(f.driftTauSeconds() >= 0.5f - 1e-6f);
        CHECK(f.driftTauSeconds() <= 30.0f + 1e-4f);
    }
}

// ==============================================================================
// FR-036 / SC-008: tempo sync with the octave fold (plan S1 C-9 option A)
// ==============================================================================

TEST_CASE("BassStereoField_TempoSyncFold", "[systems][profundum][stereo-field][fr-036][sc-008]") {
    // plan S1 C-9 (A): bpm_eff = bpm / 2^j, the smallest j >= 0 with (bpm_eff / 60) / beats <= 1 Hz.
    BsfControls base{};
    base.motionSource = BsfSource::Cycle;
    base.cycleSync = true;
    BassStereoField f;
    bsfPrepare(f, base);

    const auto readHz = [&](NoteValue n, NoteModifier m) {
        BsfControls c = base;
        c.cycleNote = n;
        c.cycleModifier = m;
        f.setControls(c);
        f.advanceControl();
        return f.cycleFrequencyHz();
    };
    const auto checkHz = [&](NoteValue n, NoteModifier m, float expected) {
        const float hz = readHz(n, m);
        INFO("note " << kBsfNoteNames[static_cast<std::size_t>(n)] << " "
                     << kBsfModifierNames[static_cast<std::size_t>(m)] << " -> " << bsfFmt(hz) << " Hz (expected "
                     << bsfFmt(expected) << ")");
        CHECK(std::abs(hz - expected) <= 1e-5f);
    };

    f.setTempoBpm(120.0f);
    checkHz(NoteValue::Whole, NoteModifier::None, 0.5f);
    checkHz(NoteValue::Quarter, NoteModifier::None, 1.0f);
    checkHz(NoteValue::SixtyFourth, NoteModifier::Triplet, 0.75f);
    f.setTempoBpm(200.0f);
    checkHz(NoteValue::SixtyFourth, NoteModifier::Triplet, 0.625f);

    // Every note value x modifier at the tempo extremes obeys the 1 Hz bar.
    float worst = 0.0f;
    for (const float bpm : {20.0f, 120.0f, 200.0f, 300.0f}) {
        f.setTempoBpm(bpm);
        for (int n = 0; n < 10; ++n) {
            for (int m = 0; m < 3; ++m) {
                const float hz = readHz(static_cast<NoteValue>(n), static_cast<NoteModifier>(m));
                worst = std::max(worst, hz);
                INFO("bpm " << bsfFmt(bpm) << " note " << kBsfNoteNames[static_cast<std::size_t>(n)] << " "
                            << kBsfModifierNames[static_cast<std::size_t>(m)] << " -> " << bsfFmt(hz));
                CHECK(hz <= 1.0f + 1e-6f);
            }
        }
    }
    INFO("fastest synced rate " << bsfFmt(worst) << " Hz");
    CHECK(worst <= 1.0f + 1e-6f);

    // Invalid tempos keep the previous one.
    f.setTempoBpm(120.0f);
    checkHz(NoteValue::Whole, NoteModifier::None, 0.5f);
    for (const float bad : {bsfNaN(), 0.0f, -1.0f, bsfPosInf()}) {
        f.setTempoBpm(bad);
        checkHz(NoteValue::Whole, NoteModifier::None, 0.5f);
    }
    // Clamps to [20, 300] BPM.
    f.setTempoBpm(10.0f);
    checkHz(NoteValue::Whole, NoteModifier::None, 0.0833333f);
    f.setTempoBpm(400.0f);
    checkHz(NoteValue::Whole, NoteModifier::None, 0.625f);  // unfolded 1.25 -> folded 0.625

    // Evidence: evidence/sc008-sync-fold.csv (every note value x modifier at 120 and 200 BPM).
    struct Row {
        float bpm;
        int note;
        int modifier;
        float unfolded;
        float folded;
    };
    std::vector<Row> rows;
    for (const float bpm : {120.0f, 200.0f}) {
        f.setTempoBpm(bpm);
        for (int n = 0; n < 10; ++n) {
            for (int m = 0; m < 3; ++m) {
                const float beats =
                    Krate::DSP::getBeatsForNote(static_cast<NoteValue>(n), static_cast<NoteModifier>(m));
                const float unfolded = (bpm / 60.0f) / beats;
                const float folded = readHz(static_cast<NoteValue>(n), static_cast<NoteModifier>(m));
                rows.push_back(Row{.bpm = bpm, .note = n, .modifier = m, .unfolded = unfolded, .folded = folded});
            }
        }
    }
    const std::string dir = bsfEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc008-sync-fold.csv");
    REQUIRE(csv.is_open());
    csv << "bpm,note,modifier,unfolded_hz,folded_hz,aliases_with\n";
    csv << std::setprecision(6);
    for (const auto& r : rows) {
        std::string aliases;
        for (const auto& o : rows) {
            if (&o == &r || o.bpm != r.bpm) {
                continue;
            }
            if (std::abs(o.folded - r.folded) <= 1e-5f * r.folded) {
                if (!aliases.empty()) {
                    aliases += ';';
                }
                aliases += std::string(kBsfNoteNames[static_cast<std::size_t>(o.note)]) + "+" +
                           kBsfModifierNames[static_cast<std::size_t>(o.modifier)];
            }
        }
        csv << r.bpm << ',' << kBsfNoteNames[static_cast<std::size_t>(r.note)] << ','
            << kBsfModifierNames[static_cast<std::size_t>(r.modifier)] << ',' << r.unfolded << ',' << r.folded << ','
            << aliases << '\n';
    }
    CHECK(rows.size() == 60u);
}

// ==============================================================================
// FR-033 / SC-012: the Drift seed is deterministic, settable and restored by reset()
// ==============================================================================

TEST_CASE("BassStereoField_DriftSeedDeterminism", "[systems][profundum][stereo-field][fr-033][sc-012]") {
    BsfControls c{};
    c.motionSource = BsfSource::Drift;
    c.motionDepth = 1.0f;
    c.motionRate = 1.0f;
    BassStereoField a;
    BassStereoField b;
    bsfPrepare(a, c);
    bsfPrepare(b, c);

    constexpr int kIntervals = 2000;
    const auto run = [&](BassStereoField& f) {
        std::vector<std::uint32_t> seq(kIntervals);
        for (int i = 0; i < kIntervals; ++i) {
            f.advanceControl();
            seq[static_cast<std::size_t>(i)] = bsfBits(f.motionValue());
        }
        return seq;
    };

    const std::vector<std::uint32_t> a0 = run(a);
    a.reset();
    const std::vector<std::uint32_t> b0 = run(b);
    b.reset();
    CHECK(a0 == b0);

    b.setDriftSeed(0x1234u);
    b.reset();
    const std::vector<std::uint32_t> b1 = run(b);
    int differing = 0;
    for (int i = 0; i < kIntervals; ++i) {
        if (b1[static_cast<std::size_t>(i)] != a0[static_cast<std::size_t>(i)]) {
            ++differing;
        }
    }
    INFO("intervals differing between seeds 0xB17E and 0x1234: " << differing);
    CHECK(differing > 0);

    a.reset();
    b.reset();
    CHECK(run(a) == a0);
    CHECK(run(b) == b1);
}

// ==============================================================================
// FR-034: the MOTION output conforms to ModulationSource; both sources advance every interval (S5)
// ==============================================================================

TEST_CASE("BassStereoField_MotionOutputConformance", "[systems][profundum][stereo-field][fr-034]") {
    constexpr int kIntervals = 10000;
    for (const BsfSource source : {BsfSource::Drift, BsfSource::Cycle}) {
        BsfControls c{};
        c.motionSource = source;
        c.motionRate = 1.0f;
        c.motionDepth = 1.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        const Krate::DSP::ModulationSource& src = f.motionSource();
        const auto range = src.getSourceRange();
        CHECK(bsfBits(range.first) == bsfBits(-1.0f));
        CHECK(bsfBits(range.second) == bsfBits(1.0f));

        int mismatches = 0;
        int outOfRange = 0;
        float maxAbs = 0.0f;
        float lo = 1.0f;
        float hi = -1.0f;
        for (int i = 0; i < kIntervals; ++i) {
            f.advanceControl();
            const float m = f.motionValue();
            if (bsfBits(src.getCurrentValue()) != bsfBits(m)) {
                ++mismatches;
            }
            if (m < -1.0f || m > 1.0f) {
                ++outOfRange;
            }
            maxAbs = std::max(maxAbs, std::abs(m));
            lo = std::min(lo, m);
            hi = std::max(hi, m);
        }
        INFO((source == BsfSource::Drift ? "Drift" : "Cycle") << ": max |m| " << bsfFmt(maxAbs) << ", min "
                                                               << bsfFmt(lo) << ", max " << bsfFmt(hi));
        CHECK(mismatches == 0);
        CHECK(outOfRange == 0);
        if (source == BsfSource::Cycle) {
            CHECK(maxAbs >= 0.99f);
        } else {
            CHECK(hi - lo >= 0.1f);
        }
    }

    // Both sources advance every interval: switching Drift -> Cycle at interval 5000 lands on the
    // bit-same Cycle value as a field that ran Cycle from interval 0.
    BsfControls drift{};
    drift.motionSource = BsfSource::Drift;
    drift.motionRate = 1.0f;
    drift.motionDepth = 1.0f;
    BsfControls cycle = drift;
    cycle.motionSource = BsfSource::Cycle;
    BassStereoField switching;
    BassStereoField reference;
    bsfPrepare(switching, drift);
    bsfPrepare(reference, cycle);
    int mismatches = 0;
    for (int i = 0; i < kIntervals; ++i) {
        if (i == 5000) {
            switching.setControls(cycle);
        }
        switching.advanceControl();
        reference.advanceControl();
        if (i >= 5000 && bsfBits(switching.motionValue()) != bsfBits(reference.motionValue())) {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}

// ==============================================================================
// SC-020 / FR-021: the side-gain law g_side = floor * (1 - G) (plan S1 C-11)
// ==============================================================================

TEST_CASE("BassStereoField_SideGainLaw", "[systems][profundum][stereo-field][sc-020][fr-021]") {
    // plan S1 C-11
    static_assert(BassStereoField::sideGainForGravity(1.0f) == 0.0f);
    CHECK(BassStereoField::sideGainForGravity(0.0f) == BassStereoField::kSideFloorGain);
    CHECK(bsfBits(BassStereoField::sideGainForGravity(1.0f)) == 0u);
    float prev = BassStereoField::sideGainForGravity(0.0f);
    for (int i = 1; i <= 10; ++i) {
        const float g = BassStereoField::sideGainForGravity(static_cast<float>(i) / 10.0f);
        INFO("gravity " << i << "/10 -> g_side " << bsfFmt(g));
        CHECK(g <= prev);
        prev = g;
    }

    BassStereoField f;
    bsfPrepare(f, BsfControls{});  // Gravity 0.5
    CHECK(bsfBits(f.currentSideGain()) == bsfBits(BassStereoField::sideGainForGravity(0.5f)));

    // tasks.md deviation 5: the instance members start at the constants, and reset() leaves them alone.
    CHECK(bsfBits(BsfProbe::sideFloorGain(f)) == bsfBits(BassStereoField::kSideFloorGain));
    CHECK(bsfBits(BsfProbe::wideOffsetScale(f)) == bsfBits(BassStereoField::kWideOffsetScale));
    BsfProbe::setSideFloorGain(f, 0.25f);
    BsfProbe::setWideOffsetScale(f, 0.75f);
    f.reset();
    CHECK(BsfProbe::sideFloorGain(f) == 0.25f);
    CHECK(BsfProbe::wideOffsetScale(f) == 0.75f);
}

// ==============================================================================
// FR-024: the law crossover is smoothed at 20 ms on the control grid; the stage gets 2 * f_c,s
// ==============================================================================

TEST_CASE("BassStereoField_CrossoverLawSmoothing", "[systems][profundum][stereo-field][fr-024]") {
    BassStereoField f;
    bsfPrepare(f, BsfControls{});  // 120 Hz
    REQUIRE(f.smoothedCrossoverHz() == 120.0f);

    f.setControls({.crossoverHz = 250.0f});
    Krate::DSP::OnePoleSmoother ref;
    ref.configure(20.0f, 1500.0f);  // kCrossoverSmoothMs at fs / 32
    ref.snapTo(120.0f);
    ref.setTarget(250.0f);

    float worstErr = 0.0f;
    int stageMismatches = 0;
    for (int i = 0; i < 200; ++i) {
        f.advanceControl();
        const float r = ref.process();
        worstErr = std::max(worstErr, std::abs(f.smoothedCrossoverHz() - r));
        if (f.stageCrossoverHz() != BassStereoField::kStageCrossoverMultiple * f.smoothedCrossoverHz()) {
            ++stageMismatches;
        }
    }
    INFO("worst |f_c,s - reference| " << bsfFmt(worstErr) << " Hz over 200 intervals; last f_c,s "
                                      << bsfFmt(f.smoothedCrossoverHz()));
    CHECK(worstErr <= 1e-3f);
    CHECK(stageMismatches == 0);

    // plan S1 C-4: reset() snaps both crossovers.
    f.reset();
    CHECK(f.smoothedCrossoverHz() == 250.0f);
    CHECK(f.stageCrossoverHz() == 500.0f);
}

// ==============================================================================
// FR-001 / SC-011: lifecycle before prepare
// ==============================================================================

TEST_CASE("BassStereoField_LifecycleBeforePrepare", "[systems][profundum][stereo-field][fr-001][sc-011]") {
    BassStereoField f;
    f.reset();
    f.setControls(BsfControls{});
    f.advanceControl();
    f.advanceControl();
    f.advanceControl();
    CHECK(f.smoothedCrossoverHz() == 120.0f);
    CHECK(Krate::DSP::detail::isFinite(f.motionValue()));
}

// ==============================================================================
// T004: the generation-domain width law, slew and handoff count (FR-010 - FR-019, FR-030 vector side;
//       FR-011 / C-15; SC-009(a'), SC-013(c), SC-018 law, SC-019 law, SC-021; plan S4.2, S4.3, C-5,
//       C-6, C-8, C-15, C-17)
// ==============================================================================

namespace {

using BsfVec = std::array<float, Krate::DSP::kMaxPartials>;
using BsfVoice = Krate::DSP::BassStereoField::VoiceState;
constexpr std::size_t kBsfMaxPartials = Krate::DSP::kMaxPartials;
constexpr float kBsfDelta = BassStereoField::kMaxPanStepPerInterval;

/// Equal-tempered MIDI note frequency (A4 = 440 Hz), computed in double.
float bsfMidiHz(int midi) {
    return static_cast<float>(440.0 * std::pow(2.0, static_cast<double>(midi - 69) / 12.0));
}
constexpr int kBsfE0 = 16;
constexpr int kBsfE1 = 28;
constexpr int kBsfC2 = 36;
constexpr int kBsfE2 = 40;
constexpr int kBsfC3 = 48;
constexpr int kBsfC4 = 60;

/// The plan S1 C-5 law in double: c = 0.5 - 0.5 cos(pi x^gamma), lower knee 0.25 (1 - G) oct below f_c,
/// upper knee 2 oct above, gamma = 4^curve.
double bsfRefCurve(double f, double fc, double gravity, double curve) {
    if (!(f > 0.0)) {
        return 0.0;
    }
    const double uL = std::log2(fc) - 0.25 * (1.0 - gravity);
    const double uH = std::log2(fc) + 2.0;
    const double x = (std::log2(f) - uL) / (uH - uL);
    if (x <= 0.0) {
        return 0.0;
    }
    if (x >= 1.0) {
        return 1.0;
    }
    const double gamma = std::pow(4.0, curve);
    return 0.5 - 0.5 * std::cos(3.14159265358979323846 * std::pow(x, gamma));
}

/// Law-level width of one partial: W(p) = max(-120, 20 log10 |tan(pi p / 4)|) dB (the constant-power
/// identity of the bank's pan law, harmonic_oscillator_bank.h:647-659).
double bsfWidthDb(float p) {
    const double t = std::abs(std::tan(3.14159265358979323846 * static_cast<double>(p) / 4.0));
    if (!(t > 0.0)) {
        return -120.0;
    }
    return std::max(-120.0, 20.0 * std::log10(t));
}

/// Re-latch `c` and snap (reset), then one advanceControl (the first interval).
void bsfConfigure(BassStereoField& f, const BsfControls& c) {
    f.setControls(c);
    f.reset();
    f.advanceControl();
}

/// An unprimed query on a fresh VoiceState at the field's current state.
BsfVec bsfUnprimed(const BassStereoField& f, float f0, int n = BassStereoField::kDefaultPartials) {
    BsfVoice vs;
    BsfVec out{};
    f.computePanVector(vs, f0, out, n);
    return out;
}

bool bsfVecBitEqual(const BsfVec& a, const BsfVec& b) {
    for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
        if (bsfBits(a[k]) != bsfBits(b[k])) {
            return false;
        }
    }
    return true;
}

bool bsfVecAllBitZero(const BsfVec& v) {
    for (const float x : v) {
        if (bsfBits(x) != 0u) {
            return false;
        }
    }
    return true;
}

/// Largest per-entry step |a_k - b_k|.
float bsfMaxStep(const BsfVec& a, const BsfVec& b) {
    float m = 0.0f;
    for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
        m = std::max(m, std::abs(a[k] - b[k]));
    }
    return m;
}

/// The direction law (FR-014): a non-zero entry has the sign bit set iff k is even.
bool bsfSignOk(const BsfVec& v) {
    for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
        const std::uint32_t b = bsfBits(v[k]);
        if (b == 0u) {
            continue;
        }
        const bool negative = (b & 0x80000000u) != 0u;
        if (negative != (k % 2 == 0)) {
            return false;
        }
    }
    return true;
}

/// Field-wise linear interpolation of the float controls (enums from `a`).
BsfControls bsfLerpControls(const BsfControls& a, const BsfControls& b, float t) {
    BsfControls m = a;
    for (const auto field : kBsfFloatFields) {
        m.*field = std::lerp(a.*field, b.*field, t);
    }
    return m;
}

struct BsfPair {
    const char* name;
    BsfControls a;
    BsfControls b;
};

}  // namespace

// ------------------------------------------------------------------------------
// FR-012 / SC-021: widthCurve values (plan S1 C-5)
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_WidthCurveValues", "[systems][profundum][stereo-field][fr-012][sc-021]") {
    BassStereoField f;
    const auto at = [&](float xover, float gravity, float curve) {
        BsfControls c{};
        c.crossoverHz = xover;
        c.gravity = gravity;
        c.curve = curve;
        bsfPrepare(f, c);
        f.advanceControl();
    };

    at(120.0f, 0.0f, 0.0f);
    INFO("G 0 curve 0: c(120) " << bsfFmt(f.widthCurve(120.0f)) << " c(240) " << bsfFmt(f.widthCurve(240.0f)));
    CHECK(std::abs(f.widthCurve(120.0f) - 0.0301537f) <= 1e-5f);
    CHECK(std::abs(f.widthCurve(240.0f) - 0.5868241f) <= 1e-5f);
    CHECK(f.widthCurve(480.0f) == 1.0f);
    CHECK(f.widthCurve(1000.0f) == 1.0f);
    CHECK(f.widthCurve(90.0f) == 0.0f);
    CHECK(bsfBits(f.widthCurve(90.0f)) == 0u);
    // A non-finite or non-positive frequency gives 0.
    CHECK(bsfBits(f.widthCurve(bsfNaN())) == 0u);
    CHECK(bsfBits(f.widthCurve(bsfPosInf())) == 0u);
    CHECK(bsfBits(f.widthCurve(0.0f)) == 0u);
    CHECK(bsfBits(f.widthCurve(-5.0f)) == 0u);

    at(120.0f, 1.0f, 0.0f);
    CHECK(f.widthCurve(120.0f) == 0.0f);
    CHECK(std::abs(f.widthCurve(240.0f) - 0.5f) <= 1e-5f);

    at(120.0f, 0.0f, 0.5f);
    CHECK(std::abs(f.widthCurve(240.0f) - 0.2171966f) <= 1e-5f);
    at(120.0f, 0.5f, 0.5f);
    CHECK(std::abs(f.widthCurve(240.0f) - 0.1816232f) <= 1e-5f);
    at(120.0f, 0.0f, 1.0f);
    CHECK(std::abs(f.widthCurve(240.0f) - 0.0222237f) <= 1e-5f);

    at(60.0f, 0.0f, 0.0f);
    CHECK(std::abs(f.widthCurve(60.0f) - 0.0301537f) <= 1e-5f);
    at(250.0f, 0.0f, 0.0f);
    CHECK(std::abs(f.widthCurve(250.0f) - 0.0301537f) <= 1e-5f);
}

// ------------------------------------------------------------------------------
// FR-010 / FR-013 / FR-014 / FR-017 / FR-018: the pan vector's shape
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_PanVectorShape",
          "[systems][profundum][stereo-field][fr-010][fr-013][fr-014][fr-017][fr-018]") {
    BassStereoField f;
    bsfPrepare(f, BsfControls{});  // Spread 0.5, Gravity 0.5, curve 0.5, crossover 120
    f.advanceControl();
    const float e1 = bsfMidiHz(kBsfE1);
    const BsfVec v = bsfUnprimed(f, e1);

    // Expectations in double from the C-5 formula; the transcribed table must match them (a slip shows).
    const auto expected = [&](std::size_t k) {
        const double s = (k % 2 == 0) ? -1.0 : 1.0;
        return s * 0.5 * bsfRefCurve(static_cast<double>(k + 1) * static_cast<double>(e1), 120.0, 0.5, 0.5);
    };
    struct TableRow {
        std::size_t k;
        double value;
    };
    const std::array<TableRow, 9> table = {{{2, -0.0000479},
                                            {3, +0.006948},
                                            {4, -0.039453},
                                            {5, +0.104321},
                                            {6, -0.193907},
                                            {7, +0.292827},
                                            {8, -0.384166},
                                            {9, +0.453827},
                                            {10, -0.492837}}};
    for (const auto& row : table) {
        INFO("k " << row.k << " table " << bsfFmt(row.value) << " formula " << bsfFmt(expected(row.k)) << " out "
                  << bsfFmt(v[row.k]));
        CHECK(std::abs(row.value - expected(row.k)) <= 1e-4);
        CHECK(std::abs(static_cast<double>(v[row.k]) - row.value) <= 2e-5);
    }
    CHECK(bsfBits(v[0]) == 0u);
    CHECK(bsfBits(v[1]) == 0u);
    for (std::size_t k = 1; k < 64; ++k) {
        INFO("k " << k << " out " << bsfFmt(v[k]) << " formula " << bsfFmt(expected(k)));
        CHECK(std::abs(static_cast<double>(v[k]) - expected(k)) <= 2e-5);
        if (k >= 11) {
            CHECK(std::abs(static_cast<double>(v[k]) - ((k % 2 == 0) ? -0.5 : 0.5)) <= 2e-5);
        }
    }
    for (std::size_t k = 64; k < kBsfMaxPartials; ++k) {
        CHECK(bsfBits(v[k]) == 0u);
    }
    for (const float x : v) {
        CHECK(std::abs(x) <= 1.0f);
    }
    CHECK(bsfSignOk(v));

    // numPartials 32 -> entries k >= 32 are bit 0; 0 -> all bit 0; 200 -> clamped to 96.
    {
        const BsfVec v32 = bsfUnprimed(f, e1, 32);
        for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
            if (k < 32) {
                CHECK(bsfBits(v32[k]) == bsfBits(v[k]));
            } else {
                CHECK(bsfBits(v32[k]) == 0u);
            }
        }
        CHECK(bsfVecAllBitZero(bsfUnprimed(f, e1, 0)));
        CHECK(bsfVecAllBitZero(bsfUnprimed(f, e1, -3)));
        const BsfVec v200 = bsfUnprimed(f, e1, 200);
        for (std::size_t k = 1; k < kBsfMaxPartials; ++k) {
            INFO("N 200, k " << k);
            CHECK(std::abs(static_cast<double>(v200[k]) - expected(k)) <= 2e-5);
        }
        CHECK(bsfBits(v200[0]) == 0u);
    }

    // f0 guard: NaN, 0 or -5 on a fresh state emits the vector of kDefaultF0Hz (55 Hz) on a fresh state.
    {
        const BsfVec ref55 = bsfUnprimed(f, BassStereoField::kDefaultF0Hz);
        for (const float bad : {bsfNaN(), 0.0f, -5.0f, bsfPosInf()}) {
            INFO("bad f0 bits 0x" << std::hex << bsfBits(bad));
            CHECK(bsfVecBitEqual(bsfUnprimed(f, bad), ref55));
        }
        // After a valid E1 query, a NaN query on that state equals a second E1 query on a copy.
        BsfVoice vs;
        BsfVec out{};
        f.computePanVector(vs, e1, out);
        BsfVoice copy = vs;
        BsfVec outNaN{};
        BsfVec outE1{};
        f.computePanVector(vs, bsfNaN(), outNaN);
        f.computePanVector(copy, e1, outE1);
        CHECK(bsfVecBitEqual(outNaN, outE1));
        CHECK(bsfBits(vs.lastF0Hz) == bsfBits(e1));
    }
}

// ------------------------------------------------------------------------------
// FR-012 / SC-003: monotone in f, Gravity, curve and Spread
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_PanVectorMonotone", "[systems][profundum][stereo-field][fr-012][sc-003]") {
    BassStereoField f;
    bsfPrepare(f, BsfControls{});
    const std::array<float, 3> levels = {0.0f, 0.5f, 1.0f};
    const std::array<float, 3> xovers = {60.0f, 120.0f, 250.0f};

    std::vector<float> freqs;
    for (int i = 0;; ++i) {
        const float hz = static_cast<float>(10.0 * std::pow(2.0, static_cast<double>(i) / 48.0));
        if (hz > 20000.0f) {
            break;
        }
        freqs.push_back(hz);
    }

    // curves[(g, x, u, i)]
    std::vector<float> curves(3 * 3 * 3 * freqs.size());
    const auto idx = [&](std::size_t g, std::size_t x, std::size_t u, std::size_t i) {
        return ((g * 3 + x) * 3 + u) * freqs.size() + i;
    };
    int nonMonotoneF = 0;
    int notOneAboveKnee = 0;
    for (std::size_t g = 0; g < 3; ++g) {
        for (std::size_t x = 0; x < 3; ++x) {
            for (std::size_t u = 0; u < 3; ++u) {
                BsfControls c{};
                c.gravity = levels[g];
                c.crossoverHz = xovers[x];
                c.curve = levels[u];
                bsfConfigure(f, c);
                for (std::size_t i = 0; i < freqs.size(); ++i) {
                    const float w = f.widthCurve(freqs[i]);
                    curves[idx(g, x, u, i)] = w;
                    if (i > 0 && w < curves[idx(g, x, u, i - 1)] - 1e-7f) {
                        ++nonMonotoneF;
                    }
                    if (freqs[i] >= 4.0f * xovers[x] && w != 1.0f) {
                        ++notOneAboveKnee;
                    }
                }
            }
        }
    }
    CHECK(nonMonotoneF == 0);
    CHECK(notOneAboveKnee == 0);

    int gravityRaises = 0;
    int curveRaises = 0;
    for (std::size_t x = 0; x < 3; ++x) {
        for (std::size_t i = 0; i < freqs.size(); ++i) {
            for (std::size_t a = 0; a < 3; ++a) {
                for (std::size_t b = 1; b < 3; ++b) {
                    // Gravity b-1 -> b at curve a; curve b-1 -> b at Gravity a.
                    if (curves[idx(b, x, a, i)] > curves[idx(b - 1, x, a, i)] + 1e-7f) {
                        ++gravityRaises;
                    }
                    if (curves[idx(a, x, b, i)] > curves[idx(a, x, b - 1, i)] + 1e-7f) {
                        ++curveRaises;
                    }
                }
            }
        }
    }
    CHECK(gravityRaises == 0);
    CHECK(curveRaises == 0);

    // |p_k| non-decreasing in Spread (unprimed queries), at E1 and C2.
    int spreadDecreases = 0;
    for (const int note : {kBsfE1, kBsfC2}) {
        BsfVec prev{};
        for (int i = 0; i <= 10; ++i) {
            BsfControls c{};
            c.spread = static_cast<float>(i) / 10.0f;
            bsfConfigure(f, c);
            const BsfVec v = bsfUnprimed(f, bsfMidiHz(note));
            if (i > 0) {
                for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
                    if (std::abs(v[k]) < std::abs(prev[k])) {
                        ++spreadDecreases;
                    }
                }
            }
            prev = v;
        }
    }
    CHECK(spreadDecreases == 0);
}

// ------------------------------------------------------------------------------
// FR-011 / FR-015 / FR-051: SOLID's zero vector, reached by slew (plan S1 C-15, ratified 2026-10-18)
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_SolidZeroVector", "[systems][profundum][stereo-field][fr-011][fr-015][fr-051]") {
    const float e1 = bsfMidiHz(kBsfE1);

    SECTION("(a) a start in SOLID is bit-zero from the first query") {
        BsfControls c{};
        c.mode = BsfMode::Solid;
        c.spread = 1.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        BsfVoice vs;
        BsfVec out{};
        int nonZero = 0;
        for (int i = 0; i < 400; ++i) {
            f.advanceControl();
            f.computePanVector(vs, e1, out);
            if (!bsfVecAllBitZero(out)) {
                ++nonZero;
            }
        }
        CHECK(nonZero == 0);
        CHECK(vs.handoffCount == 0u);
    }

    const auto runSwitch = [&](const BsfControls& after, const char* label) {
        BsfControls before{};
        before.spread = 1.0f;
        BassStereoField f;
        bsfPrepare(f, before);
        BsfVoice vs;
        BsfVec out{};
        for (int i = 0; i < 10; ++i) {
            f.advanceControl();
            f.computePanVector(vs, e1, out);
        }
        // |p| = 1 at k >= 11 (E1, Spread 1): the full-range case.
        REQUIRE(std::abs(out[11]) == 1.0f);
        f.setControls(after);
        int landing = -1;
        bool nonZeroAt333 = false;
        int nonZeroFrom334 = 0;
        for (int q = 1; q <= 400; ++q) {
            f.advanceControl();
            f.computePanVector(vs, e1, out);
            const bool zero = bsfVecAllBitZero(out);
            if (zero && landing < 0) {
                landing = q;
            }
            if (q == 333) {
                nonZeroAt333 = !zero;
            }
            if (q >= 334 && !zero) {
                ++nonZeroFrom334;
            }
        }
        std::cout << "[T004 C-15] " << label << ": landing query " << landing << " (bar: 334)\n";
        INFO(label << " landing query " << landing);
        CHECK(nonZeroAt333);
        CHECK(nonZeroFrom334 == 0);
    };

    SECTION("(b) SPREAD Spread 1 -> SOLID") {
        BsfControls after{};
        after.spread = 1.0f;
        after.mode = BsfMode::Solid;
        runSwitch(after, "Spread 1 -> SOLID");
    }
    SECTION("(c) SPREAD Spread 1 -> Spread 0") {
        BsfControls after{};
        after.spread = 0.0f;
        runSwitch(after, "Spread 1 -> Spread 0");
    }
}

// ------------------------------------------------------------------------------
// FR-015 / FR-035: the slew bound (plan S1 C-6)
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_SlewBound", "[systems][profundum][stereo-field][fr-015][fr-035]") {
    SECTION("(a) Spread 0 -> 1 at E1") {
        const float e1 = bsfMidiHz(kBsfE1);
        BsfControls c{};
        c.spread = 0.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        BsfVoice vs;
        BsfVec out{};
        for (int i = 0; i < 10; ++i) {
            f.advanceControl();
            f.computePanVector(vs, e1, out);
        }
        c.spread = 1.0f;
        f.setControls(c);
        BsfVec prev = out;
        float worst = 0.0f;
        int signFlips = 0;
        int notLandedFrom334 = 0;
        for (int q = 1; q <= 400; ++q) {
            f.advanceControl();
            f.computePanVector(vs, e1, out);
            worst = std::max(worst, bsfMaxStep(out, prev));
            if (!bsfSignOk(out)) {
                ++signFlips;
            }
            if (q >= 334 && !bsfVecBitEqual(out, bsfUnprimed(f, e1))) {
                ++notLandedFrom334;
            }
            prev = out;
        }
        INFO("worst step " << bsfFmt(worst));
        CHECK(worst <= kBsfDelta + 1e-7f);
        CHECK(signFlips == 0);
        CHECK(notLandedFrom334 == 0);
    }

    SECTION("(b) a 48-semitone Drop at Spread 1") {
        BsfControls c{};
        c.spread = 1.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        BsfVoice vs;
        BsfVec out{};
        const double c3 = static_cast<double>(bsfMidiHz(kBsfC3));
        f.advanceControl();
        f.computePanVector(vs, static_cast<float>(c3), out);
        BsfVec prev = out;
        float worst = 0.0f;
        int signFlips = 0;
        for (int i = 1; i <= 200; ++i) {
            const double semis = -48.0 * static_cast<double>(i) / 200.0;
            const float f0 = static_cast<float>(c3 * std::pow(2.0, semis / 12.0));
            f.advanceControl();
            f.computePanVector(vs, f0, out);
            worst = std::max(worst, bsfMaxStep(out, prev));
            if (!bsfSignOk(out)) {
                ++signFlips;
            }
            prev = out;
        }
        INFO("Drop worst step " << bsfFmt(worst));
        CHECK(worst <= kBsfDelta + 1e-7f);
        CHECK(signFlips == 0);
    }

    SECTION("(c) Cycle rate 1, depth 1, Spread 1") {
        BsfControls c{};
        c.spread = 1.0f;
        c.motionSource = BsfSource::Cycle;
        c.motionRate = 1.0f;
        c.motionDepth = 1.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        BsfVoice vs;
        BsfVec out{};
        const float e1 = bsfMidiHz(kBsfE1);
        f.advanceControl();
        f.computePanVector(vs, e1, out);
        BsfVec prev = out;
        float worst = 0.0f;
        int signFlips = 0;
        for (int i = 0; i < 3000; ++i) {
            f.advanceControl();
            f.computePanVector(vs, e1, out);
            worst = std::max(worst, bsfMaxStep(out, prev));
            if (!bsfSignOk(out)) {
                ++signFlips;
            }
            prev = out;
        }
        INFO("Cycle worst step " << bsfFmt(worst));
        CHECK(worst <= kBsfDelta + 1e-7f);
        CHECK(signFlips == 0);
    }
}

// ------------------------------------------------------------------------------
// FR-016 / SC-013(c): the handoff count
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_HandoffCount", "[systems][profundum][stereo-field][fr-016][sc-013]") {
    const float e1 = bsfMidiHz(kBsfE1);
    // Runs `intervals` intervals; returns the count after the first query.
    const auto run = [&](const BsfControls& c, int intervals, BsfVoice& vs) {
        BassStereoField f;
        bsfPrepare(f, c);
        BsfVec out{};
        std::uint64_t afterFirst = 0;
        for (int i = 0; i < intervals; ++i) {
            f.advanceControl();
            f.computePanVector(vs, e1, out);
            if (i == 0) {
                afterFirst = vs.handoffCount;
            }
        }
        return afterFirst;
    };

    {
        BsfVoice vs;
        const std::uint64_t first = run(BsfControls{}, 10000, vs);  // depth 0
        INFO("Controls{} handoffs: " << first << " after the first query, " << vs.handoffCount << " after 10000");
        CHECK(first == 1u);
        CHECK(vs.handoffCount == 1u);
        vs.reset();
        CHECK(vs.handoffCount == 1u);   // VoiceState::reset() leaves the counter alone
    }
    {
        BsfControls c{};
        c.spread = 0.0f;
        BsfVoice vs;
        run(c, 10000, vs);
        CHECK(vs.handoffCount == 0u);
    }
    {
        BsfControls c{};
        c.spread = 1.0f;
        c.motionSource = BsfSource::Cycle;
        c.motionRate = 1.0f;
        c.motionDepth = 1.0f;
        BsfVoice vs;
        run(c, 10000, vs);
        std::cout << "[T004 SC-013(c)] Cycle rate 1 depth 1 Spread 1: " << vs.handoffCount
                  << " handoffs in 10000 intervals\n";
        CHECK(vs.handoffCount >= 9900u);
    }
}

// ------------------------------------------------------------------------------
// SC-009(a'): the Reset retrigger order (plan S10: advanceControl, vs.reset(), then the query)
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_ResetRetriggerOrder", "[systems][profundum][stereo-field][sc-009]") {
    const float e1 = bsfMidiHz(kBsfE1);
    BsfControls held{};
    held.spread = 1.0f;
    BsfControls retrig = held;
    retrig.spread = 0.2f;
    retrig.crossoverHz = 250.0f;

    const auto settle = [&](BassStereoField& f, BsfVoice& vs, BsfVec& out) {
        bsfPrepare(f, held);
        for (int i = 0; i < 500; ++i) {
            f.advanceControl();
            f.computePanVector(vs, e1, out);
        }
    };

    {
        BassStereoField f;
        BsfVoice vs;
        BsfVec out{};
        settle(f, vs, out);
        f.setControls(retrig);
        f.advanceControl();
        vs.reset();
        f.computePanVector(vs, e1, out);
        CHECK(bsfVecBitEqual(out, bsfUnprimed(f, e1)));
        BsfVec prev = out;
        float worst = 0.0f;
        for (int i = 0; i < 400; ++i) {
            f.advanceControl();
            f.computePanVector(vs, e1, out);
            worst = std::max(worst, bsfMaxStep(out, prev));
            prev = out;
        }
        INFO("after the retrigger, worst step " << bsfFmt(worst));
        CHECK(worst <= kBsfDelta + 1e-7f);
    }

    {   // Positive control: the old order (query first, then vs.reset()) jumps on the next interval.
        BassStereoField f;
        BsfVoice vs;
        BsfVec out{};
        settle(f, vs, out);
        f.setControls(retrig);
        f.advanceControl();
        f.computePanVector(vs, e1, out);
        vs.reset();
        const BsfVec noteOn = out;
        f.advanceControl();
        f.computePanVector(vs, e1, out);
        const float step = bsfMaxStep(out, noteOn);
        std::cout << "[T004 SC-009(a')] positive control (query, then reset): next-interval step " << step
                  << " (Delta " << kBsfDelta << ")\n";
        CHECK(step > kBsfDelta);
    }
}

// ------------------------------------------------------------------------------
// FR-030 / FR-014: MOTION only pulls width inward and never swaps sides
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_MotionInwardLaw", "[systems][profundum][stereo-field][fr-030][fr-014]") {
    const float e1 = bsfMidiHz(kBsfE1);
    BsfControls c{};
    c.spread = 1.0f;
    c.motionSource = BsfSource::Cycle;
    c.motionRate = 1.0f;
    c.motionDepth = 1.0f;
    BsfControls still = c;
    still.motionDepth = 0.0f;

    BassStereoField ref;
    bsfPrepare(ref, still);
    ref.advanceControl();
    const BsfVec target0 = bsfUnprimed(ref, e1);   // P_k (depth 0)

    BassStereoField f;
    bsfPrepare(f, c);
    BsfVoice vs;
    BsfVec out{};
    f.advanceControl();
    f.computePanVector(vs, e1, out);
    int outward = 0;
    int signFlips = 0;
    int h1NonZero = 0;
    int centredMoved = 0;
    float minRatio = 1.0f;
    for (int i = 0; i < 1500; ++i) {
        f.advanceControl();
        f.computePanVector(vs, e1, out);
        for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
            const float pk = std::abs(target0[k]);
            if (std::abs(out[k]) > pk + 1e-6f) {
                ++outward;
            }
            if (bsfBits(target0[k]) == 0u && bsfBits(out[k]) != 0u) {
                ++centredMoved;
            }
            if (pk > 0.5f) {
                minRatio = std::min(minRatio, std::abs(out[k]) / pk);
            }
        }
        if (!bsfSignOk(out)) {
            ++signFlips;
        }
        if (bsfBits(out[0]) != 0u) {
            ++h1NonZero;
        }
    }
    INFO("smallest |p| / P over the wide partials " << bsfFmt(minRatio) << " (non-vacuity: MOTION moved it)");
    CHECK(outward == 0);
    CHECK(signFlips == 0);
    CHECK(h1NonZero == 0);
    CHECK(centredMoved == 0);
    CHECK(minRatio < 0.9f);
}

// ------------------------------------------------------------------------------
// SC-018 / FR-013 / FR-061: h1 is centred at every setting (law level)
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_H1CentredLaw", "[systems][profundum][stereo-field][sc-018][fr-013][fr-061]") {
    BassStereoField f;
    bsfPrepare(f, BsfControls{});
    const std::array<int, 6> notes = {kBsfE0, kBsfE1, kBsfC2, kBsfE2, kBsfC3, kBsfC4};
    long checked = 0;
    long nonZero = 0;
    for (const BsfMode mode : {BsfMode::Solid, BsfMode::Spread, BsfMode::Wide}) {
        for (const float spread : {0.0f, 0.5f, 1.0f}) {
            for (const float gravity : {0.0f, 1.0f}) {
                for (const float xover : {60.0f, 250.0f}) {
                    for (const float curve : {0.0f, 1.0f}) {
                        for (int motion = 0; motion < 3; ++motion) {
                            BsfControls c{};
                            c.mode = mode;
                            c.spread = spread;
                            c.gravity = gravity;
                            c.crossoverHz = xover;
                            c.curve = curve;
                            c.motionDepth = motion == 0 ? 0.0f : 1.0f;
                            c.motionRate = 1.0f;
                            c.motionSource = motion == 2 ? BsfSource::Cycle : BsfSource::Drift;
                            for (const int note : notes) {
                                f.setControls(c);
                                f.reset();
                                BsfVoice vs;
                                BsfVec out{};
                                for (int i = 0; i < 50; ++i) {
                                    f.advanceControl();
                                    f.computePanVector(vs, bsfMidiHz(note), out);
                                    ++checked;
                                    if (bsfBits(out[0]) != 0u) {
                                        ++nonZero;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    INFO("queries checked " << checked);
    CHECK(checked == 3L * 3 * 2 * 2 * 2 * 3 * 6 * 50);
    CHECK(nonZero == 0);
}

// ------------------------------------------------------------------------------
// SC-019 / FR-014: the direction law over 60 s (law level)
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_PanOnlyDirectionLaw", "[systems][profundum][stereo-field][sc-019][fr-014]") {
    constexpr int kIntervals = 90000;   // 60 s at 48 kHz / 32
    BassStereoField f;
    bsfPrepare(f, BsfControls{});
    for (const BsfMode mode : {BsfMode::Solid, BsfMode::Spread, BsfMode::Wide}) {
        for (const float spread : {0.5f, 1.0f}) {
            for (int motion = 0; motion < 3; ++motion) {
                for (const int note : {kBsfE1, kBsfC2}) {
                    BsfControls c{};
                    c.mode = mode;
                    c.spread = spread;
                    c.motionDepth = motion == 0 ? 0.0f : 1.0f;
                    c.motionRate = 1.0f;
                    c.motionSource = motion == 2 ? BsfSource::Cycle : BsfSource::Drift;
                    f.setControls(c);
                    f.reset();
                    BsfVoice vs;
                    BsfVec out{};
                    const float f0 = bsfMidiHz(note);
                    long pairs = 0;
                    long wrong = 0;
                    for (int i = 0; i < kIntervals; ++i) {
                        f.advanceControl();
                        f.computePanVector(vs, f0, out);
                        for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
                            const std::uint32_t b = bsfBits(out[k]);
                            if (b == 0u) {
                                continue;
                            }
                            ++pairs;
                            if (((b & 0x80000000u) != 0u) != (k % 2 == 0)) {
                                ++wrong;
                            }
                        }
                    }
                    INFO("mode " << static_cast<int>(mode) << " spread " << bsfFmt(spread) << " motion " << motion
                                 << " note " << note << ": checked pairs " << pairs << ", wrong " << wrong);
                    CHECK(wrong == 0);
                    if (mode == BsfMode::Solid) {
                        CHECK(pairs == 0);
                    } else {
                        CHECK(pairs > 0);
                    }
                }
            }
        }
    }

    // The set {k : out_k != 0, sign} is identical across a reset(), two VoiceStates and two Drift seeds
    // at depth 0 (deterministic, no seed, no state: FR-014).
    const auto signature = [](const BsfVec& v) {
        std::vector<int> s;
        for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
            const std::uint32_t b = bsfBits(v[k]);
            if (b != 0u) {
                const int kk = static_cast<int>(k) + 1;
                s.push_back((b & 0x80000000u) != 0u ? -kk : kk);
            }
        }
        return s;
    };
    for (const BsfMode mode : {BsfMode::Spread, BsfMode::Wide}) {
        for (const float spread : {0.5f, 1.0f}) {
            for (const int note : {kBsfE1, kBsfC2}) {
                BsfControls c{};
                c.mode = mode;
                c.spread = spread;
                const float f0 = bsfMidiHz(note);
                BassStereoField a;
                bsfPrepare(a, c);
                a.advanceControl();
                BsfVoice v1;
                BsfVoice v2;
                BsfVec o1{};
                BsfVec o2{};
                a.computePanVector(v1, f0, o1);
                a.computePanVector(v2, f0, o2);
                const std::vector<int> s0 = signature(o1);
                CHECK_FALSE(s0.empty());
                CHECK(signature(o2) == s0);
                a.reset();
                a.advanceControl();
                CHECK(signature(bsfUnprimed(a, f0)) == s0);
                BassStereoField b;
                b.setDriftSeed(0x1234u);
                bsfPrepare(b, c);
                b.advanceControl();
                CHECK(signature(bsfUnprimed(b, f0)) == s0);
            }
        }
    }
}

// ------------------------------------------------------------------------------
// SC-021: continuity in curve, and the interpolation bar on single-control and same-direction pairs
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_CurveContinuous", "[systems][profundum][stereo-field][sc-021]") {
    BassStereoField f;
    bsfPrepare(f, BsfControls{});
    const std::array<int, 2> notes = {kBsfE1, kBsfC2};

    // curve i/10 x crossover x Gravity: valid, finite, |p| <= 1; the largest adjacent W step is printed.
    for (const float xover : {60.0f, 120.0f, 250.0f}) {
        for (const float gravity : {0.0f, 1.0f}) {
            double largestStep = 0.0;
            std::array<std::array<double, kBsfMaxPartials>, 2> prevW{};
            int nonFinite = 0;
            int overOne = 0;
            for (int i = 0; i <= 10; ++i) {
                BsfControls c{};
                c.crossoverHz = xover;
                c.gravity = gravity;
                c.curve = static_cast<float>(i) / 10.0f;
                CHECK(BassStereoField::isValidControls(c));
                bsfConfigure(f, c);
                for (std::size_t n = 0; n < notes.size(); ++n) {
                    const BsfVec v = bsfUnprimed(f, bsfMidiHz(notes[n]));
                    for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
                        if (!Krate::DSP::detail::isFinite(v[k])) {
                            ++nonFinite;
                        }
                        if (!(std::abs(v[k]) <= 1.0f)) {
                            ++overOne;
                        }
                        const double w = bsfWidthDb(v[k]);
                        if (i > 0) {
                            largestStep = std::max(largestStep, std::abs(w - prevW[n][k]));
                        }
                        prevW[n][k] = w;
                    }
                }
            }
            CHECK(nonFinite == 0);
            CHECK(overOne == 0);
            std::cout << "[T004 SC-021] crossover " << xover << " Gravity " << gravity
                      << ": largest adjacent curve-step W change " << bsfFmt(largestStep) << " dB\n";
        }
    }

    // Pairs gated by the 0.5 dB endpoint bar (plan S1 C-17 ruling: single-control and same-direction).
    std::vector<BsfPair> pairs;
    {
        BsfControls a{};
        BsfControls b{};
        a.spread = 0.0f;
        b.spread = 1.0f;
        pairs.push_back({"Spread 0 -> 1", a, b});
        a = BsfControls{};
        b = BsfControls{};
        a.gravity = 0.0f;
        b.gravity = 1.0f;
        pairs.push_back({"Gravity 0 -> 1", a, b});
        a = BsfControls{};
        b = BsfControls{};
        a.crossoverHz = 60.0f;
        b.crossoverHz = 250.0f;
        pairs.push_back({"crossover 60 -> 250", a, b});
        a = BsfControls{};
        b = BsfControls{};
        a.curve = 0.0f;
        b.curve = 1.0f;
        pairs.push_back({"curve 0 -> 1", a, b});
        a = BsfControls{};
        b = BsfControls{};
        a.spread = 0.0f;
        a.gravity = 1.0f;
        a.crossoverHz = 250.0f;
        a.curve = 1.0f;
        b.spread = 1.0f;
        b.gravity = 0.0f;
        b.crossoverHz = 60.0f;
        b.curve = 0.0f;
        pairs.push_back({"same-direction (S0 G1 X250 C1) -> (S1 G0 X60 C0)", a, b});
    }

    for (const auto& pr : pairs) {
        for (const int note : notes) {
            const float f0 = bsfMidiHz(note);
            bsfConfigure(f, pr.a);
            const BsfVec v0 = bsfUnprimed(f, f0);
            bsfConfigure(f, pr.b);
            const BsfVec v1 = bsfUnprimed(f, f0);
            double worstOutside = 0.0;
            int outside = 0;
            for (const float t : {0.25f, 0.5f, 0.75f}) {
                const BsfControls m = bsfLerpControls(pr.a, pr.b, t);
                CHECK(BassStereoField::isValidControls(m));
                bsfConfigure(f, m);
                const BsfVec vt = bsfUnprimed(f, f0);
                for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
                    const double w0 = bsfWidthDb(v0[k]);
                    const double w1 = bsfWidthDb(v1[k]);
                    const double wt = bsfWidthDb(vt[k]);
                    const double lo = std::min(w0, w1) - 0.5;
                    const double hi = std::max(w0, w1) + 0.5;
                    if (wt < lo || wt > hi) {
                        ++outside;
                        worstOutside = std::max(worstOutside, std::max(lo - wt, wt - hi));
                    }
                }
            }
            INFO(pr.name << " note " << note << ": " << outside << " (t, k) outside the 0.5 dB bar, worst by "
                         << bsfFmt(worstOutside) << " dB");
            CHECK(outside == 0);
        }
    }
}

// ------------------------------------------------------------------------------
// SC-021 (plan S1 C-17): mixed pairs, recorded (ruled 2026-10-18)
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_CurveInterpolationMixedPairs", "[systems][profundum][stereo-field][sc-021]") {
    constexpr bool kGateMixedInterpolationPairs = false;  // plan S1 C-17, ruled 2026-10-18:
    // mixed pairs recorded, not bounded (true = spec bar as written)

    std::vector<BsfPair> pairs;
    {
        BsfControls a{};
        BsfControls b{};
        a.spread = 0.0f;
        a.crossoverHz = 60.0f;
        b.spread = 1.0f;
        b.crossoverHz = 250.0f;
        pairs.push_back({"(Spread 0, X60) -> (Spread 1, X250) [plan counter-example]", a, b});
        a = BsfControls{};
        b = BsfControls{};
        a.spread = 1.0f;
        a.gravity = 1.0f;
        b.spread = 0.2f;
        b.gravity = 0.0f;
        pairs.push_back({"(Spread 1, G1) -> (Spread 0.2, G0)", a, b});
        a = BsfControls{};
        b = BsfControls{};
        a.curve = 0.0f;
        a.crossoverHz = 250.0f;
        b.curve = 1.0f;
        b.crossoverHz = 60.0f;
        pairs.push_back({"(curve 0, X250) -> (curve 1, X60)", a, b});
    }

    BassStereoField f;
    bsfPrepare(f, BsfControls{});
    constexpr int kSteps = 20;   // t per 0.05
    for (const auto& pr : pairs) {
        for (const int note : {kBsfE1, kBsfC2}) {
            const float f0 = bsfMidiHz(note);
            std::array<BsfVec, kSteps + 1> path{};
            int nonFinite = 0;
            int overOne = 0;
            for (int s = 0; s <= kSteps; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(kSteps);
                const BsfControls m = bsfLerpControls(pr.a, pr.b, t);
                CHECK(BassStereoField::isValidControls(m));
                bsfConfigure(f, m);
                path[static_cast<std::size_t>(s)] = bsfUnprimed(f, f0);
                for (const float x : path[static_cast<std::size_t>(s)]) {
                    if (!Krate::DSP::detail::isFinite(x)) {
                        ++nonFinite;
                    }
                    if (!(std::abs(x) <= 1.0f)) {
                        ++overOne;
                    }
                }
            }
            CHECK(nonFinite == 0);
            CHECK(overOne == 0);
            double largestStep = 0.0;
            double worstExcursion = 0.0;
            std::size_t worstK = 0;
            int outsideAtQuarters = 0;
            for (std::size_t k = 0; k < kBsfMaxPartials; ++k) {
                const double w0 = bsfWidthDb(path.front()[k]);
                const double w1 = bsfWidthDb(path.back()[k]);
                const double lo = std::min(w0, w1);
                const double hi = std::max(w0, w1);
                for (int s = 0; s <= kSteps; ++s) {
                    const double w = bsfWidthDb(path[static_cast<std::size_t>(s)][k]);
                    if (s > 0) {
                        largestStep = std::max(
                            largestStep, std::abs(w - bsfWidthDb(path[static_cast<std::size_t>(s - 1)][k])));
                    }
                    const double excursion = std::max({0.0, lo - w, w - hi});
                    if (excursion > worstExcursion) {
                        worstExcursion = excursion;
                        worstK = k;
                    }
                    if ((s == 5 || s == 10 || s == 15) && excursion > 0.5) {
                        ++outsideAtQuarters;
                    }
                }
            }
            std::ostringstream os;
            os << "[T004 C-17] " << pr.name << " note " << note << ": largest W step per 0.05 t "
               << bsfFmt(largestStep) << " dB; worst excursion beyond the endpoints " << bsfFmt(worstExcursion)
               << " dB at k " << worstK << "; " << outsideAtQuarters
               << " (t in {.25,.5,.75}, k) beyond 0.5 dB; W path at k " << worstK << " (dB):";
            for (int s = 0; s <= kSteps; ++s) {
                os << ' ' << bsfFmt(bsfWidthDb(path[static_cast<std::size_t>(s)][worstK]));
            }
            std::cout << os.str() << '\n';
            if constexpr (kGateMixedInterpolationPairs) {
                INFO(os.str());
                REQUIRE(outsideAtQuarters == 0);
            }
        }
    }
}

// ==============================================================================
// T005: WIDE offsets and ceilings (FR-040 - FR-044; SC-017; plan S6, C-10)
// ==============================================================================

namespace {

using BsfAlgorithm = Krate::DSP::BassCharacterStage::Algorithm;

constexpr std::array<BsfAlgorithm, 6> kBsfAlgorithms{BsfAlgorithm::Tube,  BsfAlgorithm::Fold,
                                                      BsfAlgorithm::Rectify, BsfAlgorithm::Bite,
                                                      BsfAlgorithm::Crush, BsfAlgorithm::Fracture};

bool bsfIsRectifyOrFracture(BsfAlgorithm a) {
    return a == BsfAlgorithm::Rectify || a == BsfAlgorithm::Fracture;
}

}  // namespace

// ------------------------------------------------------------------------------
// SC-017 / FR-041 / FR-042: per-algorithm ceilings, the C-10 signs, Spread scaling, latching
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_WideOffsetCeilings", "[systems][profundum][stereo-field][sc-017][fr-041][fr-042]") {
    // The ceilings (FR-041, Phase 5 handoff)
    for (const auto a : kBsfAlgorithms) {
        INFO("algorithm " << static_cast<int>(a));
        if (bsfIsRectifyOrFracture(a)) {
            CHECK(BassStereoField::wideCeiling(a) == 0.10f);
        } else {
            CHECK(BassStereoField::wideCeiling(a) == Krate::DSP::BassCharacterStage::kMaxChannelOffset);
            CHECK(BassStereoField::wideCeiling(a) == 0.15f);
        }
    }

    // Every algorithm x Spread i/10 in WIDE; SOLID and SPREAD give bit-0 at every Spread.
    for (const BsfMode mode : {BsfMode::Solid, BsfMode::Spread, BsfMode::Wide}) {
        for (int i = 0; i <= 10; ++i) {
            const float spread = static_cast<float>(i) / 10.0f;
            BsfControls c{};
            c.mode = mode;
            c.spread = spread;
            BassStereoField f;
            bsfPrepare(f, c);
            f.advanceControl();
            for (const auto a : kBsfAlgorithms) {
                INFO("mode " << static_cast<int>(mode) << " spread " << spread << " algorithm " << static_cast<int>(a));
                const auto o = f.wideOffsets(a);
                if (mode != BsfMode::Wide || i == 0) {
                    CHECK(bsfBits(o.amount) == 0u);
                    CHECK(bsfBits(o.tone) == 0u);
                    continue;
                }
                const float ceiling = BassStereoField::wideCeiling(a);
                CHECK(std::abs(o.amount) <= ceiling);
                CHECK(std::abs(o.tone) <= ceiling);
                CHECK(std::abs(o.amount - ceiling * spread * BassStereoField::kWideOffsetScale) <= 1e-6f);
                CHECK(o.amount > 0.0f);                       // C-10: amount +K
                CHECK(bsfBits(o.tone) == bsfBits(-o.amount));  // C-10: tone -K
            }
        }
    }

    // Every ordered algorithm pair (36): after the switch the offsets are the new algorithm's.
    {
        BsfControls c{};
        c.mode = BsfMode::Wide;
        c.spread = 1.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        f.advanceControl();
        int pairs = 0;
        for (const auto from : kBsfAlgorithms) {
            for (const auto to : kBsfAlgorithms) {
                INFO("switch " << static_cast<int>(from) << " -> " << static_cast<int>(to));
                const auto before = f.wideOffsets(from);
                const auto after = f.wideOffsets(to);
                const float ceiling = BassStereoField::wideCeiling(to);
                CHECK(bsfBits(before.amount) == bsfBits(BassStereoField::wideCeiling(from) * BassStereoField::kWideOffsetScale));
                CHECK(bsfBits(after.amount) == bsfBits(ceiling * BassStereoField::kWideOffsetScale));
                CHECK(bsfBits(after.tone) == bsfBits(-after.amount));
                CHECK(std::abs(after.amount) <= ceiling);
                CHECK(std::abs(after.tone) <= ceiling);
                if (bsfIsRectifyOrFracture(to)) {
                    CHECK(std::abs(after.amount) <= 0.10f);
                    CHECK(std::abs(after.tone) <= 0.10f);
                }
                ++pairs;
            }
        }
        CHECK(pairs == 36);
    }

    // Latched (FR-074): a setControls without advanceControl keeps the previous mode's offsets.
    {
        BsfControls c{};
        c.mode = BsfMode::Spread;
        c.spread = 1.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        f.advanceControl();
        c.mode = BsfMode::Wide;
        f.setControls(c);
        for (const auto a : kBsfAlgorithms) {
            const auto o = f.wideOffsets(a);
            CHECK(bsfBits(o.amount) == 0u);   // still SPREAD
            CHECK(bsfBits(o.tone) == 0u);
        }
        // G7-2 (FR-040, FR-052): entering WIDE engages the offsets over the FR-015 slew length (334 intervals).
        f.advanceControl();
        for (const auto a : kBsfAlgorithms) {
            const auto o = f.wideOffsets(a);   // one interval in: one engagement step
            const float full = BassStereoField::wideCeiling(a) * BassStereoField::kWideOffsetScale;
            CHECK(o.amount > 0.0f);
            CHECK(o.amount <= full * BassStereoField::kWideEngageStep + 1e-6f);
            CHECK(bsfBits(o.tone) == bsfBits(-o.amount));
        }
        for (int i = 1; i < 334; ++i) {
            f.advanceControl();
        }
        for (const auto a : kBsfAlgorithms) {
            const auto o = f.wideOffsets(a);   // landed exactly at the 334th interval
            CHECK(bsfBits(o.amount) == bsfBits(BassStereoField::wideCeiling(a) * BassStereoField::kWideOffsetScale));
        }
        c.mode = BsfMode::Solid;
        f.setControls(c);
        for (const auto a : kBsfAlgorithms) {
            const auto o = f.wideOffsets(a);   // still WIDE until the next interval
            CHECK(bsfBits(o.amount) == bsfBits(BassStereoField::wideCeiling(a) * BassStereoField::kWideOffsetScale));
        }
        // FR-043 (G7-2): leaving WIDE releases the offsets over the same slew length, landing on exact zero.
        f.advanceControl();
        for (const auto a : kBsfAlgorithms) {
            const auto o = f.wideOffsets(a);
            const float full = BassStereoField::wideCeiling(a) * BassStereoField::kWideOffsetScale;
            CHECK(o.amount > 0.0f);
            CHECK(std::abs(o.amount - full * (1.0f - BassStereoField::kWideEngageStep)) <= 1e-6f);
        }
        for (int i = 1; i < 334; ++i) {
            f.advanceControl();
        }
        for (const auto a : kBsfAlgorithms) {
            const auto o = f.wideOffsets(a);   // FR-043: {0, 0}, bit-zero (no -0)
            CHECK(bsfBits(o.amount) == 0u);
            CHECK(bsfBits(o.tone) == 0u);
        }
    }
}

// ------------------------------------------------------------------------------
// FR-044 / SC-017: WIDE emits the SPREAD vector bit for bit
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_WideVectorEqualsSpread", "[systems][profundum][stereo-field][fr-044][sc-017]") {
    const float e1 = bsfMidiHz(kBsfE1);
    BsfControls c{};
    c.spread = 0.0f;
    c.motionSource = BsfSource::Drift;
    c.motionDepth = 1.0f;
    c.motionRate = 1.0f;

    BsfControls cs = c;
    cs.mode = BsfMode::Spread;
    BsfControls cw = c;
    cw.mode = BsfMode::Wide;

    BassStereoField fs;
    BassStereoField fw;
    bsfPrepare(fs, cs);
    bsfPrepare(fw, cw);
    BsfVoice vs;
    BsfVoice vw;
    BsfVec outS{};
    BsfVec outW{};

    constexpr int kIntervals = 3000;
    constexpr int kRamp = 1500;
    int mismatches = 0;
    int firstMismatch = -1;
    int nonZeroIntervals = 0;
    for (int i = 0; i < kIntervals; ++i) {
        const float spread = std::min(1.0f, static_cast<float>(i) / static_cast<float>(kRamp));
        cs.spread = spread;
        cw.spread = spread;
        fs.setControls(cs);
        fw.setControls(cw);
        fs.advanceControl();
        fw.advanceControl();
        fs.computePanVector(vs, e1, outS);
        fw.computePanVector(vw, e1, outW);
        bool same = true;
        for (std::size_t k = 0; k < outS.size(); ++k)
            same = same && std::bit_cast<std::uint32_t>(outS[k]) == std::bit_cast<std::uint32_t>(outW[k]);
        if (!same) {
            ++mismatches;
            if (firstMismatch < 0) {
                firstMismatch = i;
            }
        }
        if (!bsfVecAllBitZero(outW)) {
            ++nonZeroIntervals;
        }
    }
    INFO("mismatching intervals " << mismatches << ", first at " << firstMismatch);
    CHECK(mismatches == 0);
    CHECK(nonZeroIntervals > 0);   // the comparison is not vacuous
    CHECK(vs.handoffCount == vw.handoffCount);
}

// ==============================================================================
// T006: the post-sum safety stage, determinism and RT safety (FR-001, FR-020 - FR-028,
//       FR-050 - FR-053, FR-062, FR-071, FR-074; SC-004(b)(c), SC-005 gates, SC-009(c), SC-011,
//       SC-012, SC-020 stage; plan S7, S8, C-1, C-2, C-3, C-4, C-18, R-11)
// ==============================================================================

namespace {

// plan S1 C-1, ruled 2026-10-18: numeric bound gated (true = spec bar as written)
constexpr bool kGateMidBitExact = false;
// plan S1 C-2, ruled 2026-10-18: decay recorded (true = spec bar as written)
constexpr bool kGateLrBitExactAnyState = false;

constexpr std::size_t kBsfNone = std::numeric_limits<std::size_t>::max();

struct BsfStereo {
    std::vector<float> l;
    std::vector<float> r;
};

/// The noise fixture: seeded mt19937{0x5EED}, independent uniform [-0.5, 0.5] on L and R.
BsfStereo bsfNoise(std::size_t n) {
    std::mt19937 rng{0x5EEDu};
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    BsfStereo s{.l = std::vector<float>(n), .r = std::vector<float>(n)};
    for (std::size_t i = 0; i < n; ++i) {
        s.l[i] = d(rng);
        s.r[i] = d(rng);
    }
    return s;
}

/// The L == R fixture: sines at 41.2, 123.6 and 400 Hz, 0.2 each (computed in double, cast once).
std::vector<float> bsfLrSines(std::size_t n, double fs) {
    constexpr double kTwoPi = 6.283185307179586476925;
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        x[i] = static_cast<float>(0.2 * (std::sin(kTwoPi * 41.2 * t) + std::sin(kTwoPi * 123.6 * t) +
                                         std::sin(kTwoPi * 400.0 * t)));
    }
    return x;
}

/// The anchor fixture: an AnchorOscillator prepared at fs, Reset retrigger, Sine / LOCK at
/// kDefaultLevel, a1 = 0.25, noteOn(f0), rendered in 32-sample blocks into L and R. Asserts L == R
/// and returns L.
std::vector<float> bsfAnchor(double f0, std::size_t n, double fs) {
    auto a = std::make_unique<Krate::DSP::AnchorOscillator>();
    a->prepare(fs);
    a->setRetriggerPhase(Krate::DSP::AnchorOscillator::RetriggerPhase::Reset);
    a->setControls(Krate::DSP::MassDynamicsTest::anchorControlsFor(Krate::DSP::AnchorOscillator::kDefaultLevel));
    a->setCoreH1Amplitude(0.25f);
    a->noteOn(static_cast<float>(f0));
    std::vector<float> l(n, 0.0f);
    std::vector<float> r(n, 0.0f);
    for (std::size_t s = 0; s < n; s += BassStereoField::kControlInterval) {
        const std::size_t len = std::min(BassStereoField::kControlInterval, n - s);
        a->processBlock(l.data() + s, r.data() + s, len);
    }
    Krate::DSP::ProfundumTest::requireLREqual(l.data(), r.data(), n);
    double energy = 0.0;
    for (const float v : l) {
        energy += static_cast<double>(v) * static_cast<double>(v);
    }
    REQUIRE(energy > 0.0);   // the fixture sounds
    return l;
}

/// Grid-driven in-place render: at every multiple of 32 of the absolute counter onGrid(start) runs
/// (it calls advanceControl itself), then the run is processed by one processBlock call.
template <class OnGrid>
void bsfRenderBlocks(BassStereoField& f, float* L, float* R, std::size_t n, std::size_t hostBlock,
                     const OnGrid& onGrid) {
    Krate::DSP::MassDynamicsTest::gridDriver(n, hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        if (atGrid) {
            onGrid(start);
        }
        f.processBlock(L + start, R + start, len);
    });
}

/// Per-sample probe: onGrid(i) at every multiple of 32 (calls advanceControl itself), before(i) may
/// write sample i's input, processBlock(L + i, R + i, 1), then after(i) reads the observers.
template <class OnGrid, class Before, class After>
void bsfRenderPerSample(BassStereoField& f, float* L, float* R, std::size_t n, const OnGrid& onGrid,
                        const Before& before, const After& after) {
    for (std::size_t i = 0; i < n; ++i) {
        if (i % BassStereoField::kControlInterval == 0) {
            onGrid(i);
        }
        before(i);
        f.processBlock(L + i, R + i, 1);
        after(i);
    }
}

/// The settings grid: mode x Gravity {0, 0.5, 1} x crossover {60, 120, 250} x Spread {0, 1} x
/// (depth 0 | depth 1 Drift | depth 1 Cycle).
std::vector<BsfControls> bsfSettingsGrid() {
    std::vector<BsfControls> grid;
    for (const BsfMode mode : {BsfMode::Solid, BsfMode::Spread, BsfMode::Wide}) {
        for (const float gravity : {0.0f, 0.5f, 1.0f}) {
            for (const float xover : {60.0f, 120.0f, 250.0f}) {
                for (const float spread : {0.0f, 1.0f}) {
                    for (int motion = 0; motion < 3; ++motion) {
                        BsfControls c{};
                        c.mode = mode;
                        c.gravity = gravity;
                        c.crossoverHz = xover;
                        c.spread = spread;
                        c.motionDepth = (motion == 0) ? 0.0f : 1.0f;
                        c.motionSource = (motion == 2) ? BsfSource::Cycle : BsfSource::Drift;
                        grid.push_back(c);
                    }
                }
            }
        }
    }
    return grid;
}

std::string bsfDescribe(const BsfControls& c) {
    std::ostringstream os;
    os << std::setprecision(6) << "mode " << static_cast<int>(c.mode) << " gravity " << c.gravity << " crossover "
       << c.crossoverHz << " spread " << c.spread << " depth " << c.motionDepth << " source "
       << static_cast<int>(c.motionSource);
    return os.str();
}

bool bsfSameFloats(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), sizeof(float) * a.size()) == 0;
}

/// Index from which every output pair of [from, n) is bit-equal (L == R) and, when `in` is non-null,
/// bit-equal to in[i]; n when the tail never settles.
std::size_t bsfSettledFrom(const std::vector<float>& L, const std::vector<float>& R, const float* in,
                           std::size_t from) {
    std::size_t settled = from;
    for (std::size_t i = from; i < L.size(); ++i) {
        const bool lr = bsfBits(L[i]) == bsfBits(R[i]);
        const bool id = (in == nullptr) || bsfBits(L[i]) == bsfBits(in[i]);
        if (!lr || !id) {
            settled = i + 1;
        }
    }
    return settled;
}

constexpr std::array<BsfMode, 3> kBsfModes{BsfMode::Solid, BsfMode::Spread, BsfMode::Wide};
constexpr std::array<float, 3> kBsfGravities{0.0f, 0.5f, 1.0f};
constexpr std::array<float, 3> kBsfCrossovers{60.0f, 120.0f, 250.0f};

void bsfAdvanceOnly(BassStereoField& f) { f.advanceControl(); }

}  // namespace

// ------------------------------------------------------------------------------
// SC-005 (gates) / FR-001 / FR-022: zero latency and the C-1 numeric mid bound
// ------------------------------------------------------------------------------

// SC-005; plan S1 C-1 (the numeric bound is always gated)
TEST_CASE("BassStereoField_SafetyStageGates", "[systems][profundum][stereo-field][sc-005][fr-001][fr-022]") {
    static_assert(BassStereoField::latencySamples() == 0);
    const auto n = static_cast<std::size_t>(kBsfFs);   // 1 s
    const BsfStereo in = bsfNoise(n);
    REQUIRE(!bsfSameFloats(in.l, in.r));
    const double kBound = std::ldexp(1.0, -23);

    double worstRatio = 0.0;
    std::size_t totalViolations = 0;
    for (const BsfMode mode : kBsfModes) {
        for (const float gravity : kBsfGravities) {
            for (const float xover : kBsfCrossovers) {
                BsfControls c{};
                c.mode = mode;
                c.gravity = gravity;
                c.crossoverHz = xover;
                BassStereoField f;
                bsfPrepare(f, c);
                const std::size_t latency = BassStereoField::latencySamples();   // runtime read, every mode
                CHECK(latency == 0);
                std::vector<float> L = in.l;
                std::vector<float> R = in.r;
                bsfRenderBlocks(f, L.data(), R.data(), n, kBsfBlock, [&](std::size_t) { bsfAdvanceOnly(f); });

                std::size_t violations = 0;
                std::size_t sideActive = 0;
                double cellRatio = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    const double mIn = (static_cast<double>(in.l[i]) + static_cast<double>(in.r[i])) * 0.5;
                    const double mOut = (static_cast<double>(L[i]) + static_cast<double>(R[i])) * 0.5;
                    const double sOut = (static_cast<double>(L[i]) - static_cast<double>(R[i])) * 0.5;
                    const double err = std::abs(mOut - mIn);
                    const double bound = kBound * (std::abs(mIn) + std::abs(sOut));
                    if (err > bound) {
                        ++violations;
                    }
                    if (bound > 0.0) {
                        cellRatio = std::max(cellRatio, err / bound);
                    }
                    if (sOut != 0.0) {
                        ++sideActive;
                    }
                }
                INFO(bsfDescribe(c) << ": violations " << violations << ", largest error / bound " << bsfFmt(cellRatio));
                CHECK(violations == 0);
                if (mode != BsfMode::Solid) {
                    CHECK(sideActive > 0);   // the stage carried side content: the bound is not vacuous
                }
                worstRatio = std::max(worstRatio, cellRatio);
                totalViolations += violations;
            }
        }
    }
    std::cout << "[T006 SC-005 C-1] largest |M_out - M_in| / (2^-23 (|M_in| + |S'_out|)) over 27 cells: "
              << bsfFmt(worstRatio) << " (violations " << totalViolations << ")\n";
}

// ------------------------------------------------------------------------------
// SC-005 / FR-025: the spec's bit-exact mid bar (C-1 switch); SOLID settled is bit-exact always
// ------------------------------------------------------------------------------

// SC-005; plan S1 C-1 (spec bar behind kGateMidBitExact)
TEST_CASE("BassStereoField_SafetyStageMidBitExact", "[systems][profundum][stereo-field][sc-005][fr-025]") {
    const auto n = static_cast<std::size_t>(kBsfFs);
    const BsfStereo in = bsfNoise(n);
    std::size_t totalDiffer = 0;
    double largestDelta = 0.0;
    for (const BsfMode mode : kBsfModes) {
        for (const float gravity : kBsfGravities) {
            for (const float xover : kBsfCrossovers) {
                BsfControls c{};
                c.mode = mode;
                c.gravity = gravity;
                c.crossoverHz = xover;
                BassStereoField f;
                bsfPrepare(f, c);
                std::vector<float> L = in.l;
                std::vector<float> R = in.r;
                bool zeroRead = bsfBits(f.modeFade()) == 0u;
                std::size_t differ = 0;
                std::size_t solidBad = 0;
                double cellDelta = 0.0;
                bsfRenderPerSample(
                    f, L.data(), R.data(), n, [&](std::size_t) { bsfAdvanceOnly(f); }, [](std::size_t) {},
                    [&](std::size_t i) {
                        const float mOut = (L[i] + R[i]) * 0.5f;
                        const float mIn = (in.l[i] + in.r[i]) * 0.5f;
                        const bool d = bsfBits(mOut) != bsfBits(mIn);
                        if (d) {
                            ++differ;
                            cellDelta = std::max(cellDelta,
                                                 std::abs(static_cast<double>(mOut) - static_cast<double>(mIn)));
                            if (zeroRead) {
                                ++solidBad;
                            }
                        }
                        if (bsfBits(f.modeFade()) == 0u) {
                            zeroRead = true;
                        }
                    });
                INFO(bsfDescribe(c) << ": samples whose fl((L+R)/2) differs in bits " << differ << ", largest |delta| "
                                    << bsfFmt(cellDelta));
                if (mode == BsfMode::Solid) {
                    REQUIRE(zeroRead);
                    REQUIRE(solidBad == 0);   // bit-exact once modeFade() has read 0, either ruling
                }
                if constexpr (kGateMidBitExact) {
                    REQUIRE(differ == 0);
                }
                totalDiffer += differ;
                largestDelta = std::max(largestDelta, cellDelta);
            }
        }
    }
    std::cout << "[T006 SC-005 C-1 record] kGateMidBitExact " << (kGateMidBitExact ? "true" : "false")
              << ": mid bit differences " << totalDiffer << " over 27 x " << n << " samples, largest |delta| "
              << bsfFmt(largestDelta) << '\n';
}

// ------------------------------------------------------------------------------
// SC-004(b) / FR-025: L == R in -> L == R out (and the input) from reset, over the settings grid
// ------------------------------------------------------------------------------

// SC-004(b); plan S1 C-2 (the from-reset arm is always gated)
TEST_CASE("BassStereoField_StageLrInLrOut", "[systems][profundum][stereo-field][sc-004][fr-025]") {
    const auto n = static_cast<std::size_t>(kBsfFs);
    const std::vector<float> xL = bsfLrSines(n, kBsfFs);
    const std::vector<float> xR = bsfLrSines(n, kBsfFs);
    Krate::DSP::ProfundumTest::requireLREqual(xL.data(), xR.data(), n);
    const auto grid = bsfSettingsGrid();
    REQUIRE(grid.size() == 162);
    for (const auto& c : grid) {
        BassStereoField f;
        bsfPrepare(f, c);
        std::vector<float> L = xL;
        std::vector<float> R = xR;
        bsfRenderBlocks(f, L.data(), R.data(), n, kBsfBlock, [&](std::size_t) { bsfAdvanceOnly(f); });
        INFO(bsfDescribe(c));
        Krate::DSP::ProfundumTest::requireLREqual(L.data(), R.data(), n);
        REQUIRE(bsfSameFloats(L, xL));
    }
}

// ------------------------------------------------------------------------------
// SC-004(b) / FR-025: the dirty-state arm (C-2 switch, decay recorded) and the after-SOLID arm (gated)
// ------------------------------------------------------------------------------

// SC-004(b); plan S1 C-2
TEST_CASE("BassStereoField_StageLrInLrOutDirtyState", "[systems][profundum][stereo-field][sc-004][fr-025]") {
    const auto nNoise = static_cast<std::size_t>(0.5 * kBsfFs);   // 24000, a grid point
    const auto nLr = static_cast<std::size_t>(kBsfFs);
    const std::size_t n = nNoise + nLr;
    REQUIRE(nNoise % BassStereoField::kControlInterval == 0);
    const BsfStereo noise = bsfNoise(nNoise);
    const std::vector<float> x = bsfLrSines(nLr, kBsfFs);
    std::vector<float> inL(n);
    std::vector<float> inR(n);
    for (std::size_t i = 0; i < n; ++i) {
        inL[i] = i < nNoise ? noise.l[i] : x[i - nNoise];
        inR[i] = i < nNoise ? noise.r[i] : x[i - nNoise];
    }
    Krate::DSP::ProfundumTest::requireLREqual(inL.data() + nNoise, inR.data() + nNoise, nLr);

    const std::string dir = bsfEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "side-decay-record.csv");
    REQUIRE(csv.is_open());
    csv << "crossover_hz,samples,ms,decayed_within_1s\n" << std::setprecision(6);

    for (const float xover : kBsfCrossovers) {
        BsfControls c{};
        c.mode = BsfMode::Spread;
        c.gravity = 0.0f;
        c.crossoverHz = xover;

        // The record (C-2 ruled false): the first index after the switch from which L == R bit-for-bit.
        {
            BassStereoField f;
            bsfPrepare(f, c);
            std::vector<float> L = inL;
            std::vector<float> R = inR;
            bsfRenderBlocks(f, L.data(), R.data(), n, kBsfBlock, [&](std::size_t) { bsfAdvanceOnly(f); });
            const std::size_t settled = bsfSettledFrom(L, R, nullptr, nNoise);
            const bool decayed = settled < n;
            const std::size_t samples = settled - nNoise;
            const double ms = 1000.0 * static_cast<double>(samples) / kBsfFs;
            csv << xover << ',' << samples << ',' << ms << ',' << (decayed ? "yes" : "no") << '\n';
            std::cout << "[T006 SC-004(b) C-2 record] crossover " << xover << " Hz: L == R bit-for-bit from "
                      << samples << " samples (" << bsfFmt(ms) << " ms) after the first L == R input sample"
                      << (decayed ? "" : " (not within the 1 s render)") << '\n';
            if constexpr (kGateLrBitExactAnyState) {
                REQUIRE(settled == nNoise);
            }
        }

        // Always gated (the recommended reading): a switch to SOLID at the first L == R sample is L == R
        // bit-identical from the first sample with modeFade() == 0, + 1.
        {
            BassStereoField f;
            bsfPrepare(f, c);
            std::vector<float> L = inL;
            std::vector<float> R = inR;
            std::size_t zeroAt = kBsfNone;
            bsfRenderPerSample(
                f, L.data(), R.data(), n,
                [&](std::size_t i) {
                    if (i == nNoise) {
                        BsfControls s = c;
                        s.mode = BsfMode::Solid;
                        f.setControls(s);
                    }
                    f.advanceControl();
                },
                [](std::size_t) {},
                [&](std::size_t i) {
                    if (i >= nNoise && zeroAt == kBsfNone && bsfBits(f.modeFade()) == 0u) {
                        zeroAt = i;
                    }
                });
            INFO("crossover " << xover << " Hz, modeFade() first 0 at sample " << zeroAt);
            REQUIRE(zeroAt != kBsfNone);
            REQUIRE(bsfSettledFrom(L, R, nullptr, zeroAt + 1) == zeroAt + 1);
        }
    }
}

// ------------------------------------------------------------------------------
// SC-004(c) / FR-062: the Anchor passes bit-identically from reset and after a SOLID crossfade
// ------------------------------------------------------------------------------

// SC-004(c); plan S1 C-2 (from-reset and after-SOLID arms gated)
TEST_CASE("BassStereoField_AnchorBitIdentical", "[systems][profundum][stereo-field][sc-004][fr-062]") {
    const auto n = static_cast<std::size_t>(kBsfFs);
    const auto grid = bsfSettingsGrid();
    for (const int midi : {16, 28, 40, 48}) {   // E0, E1, E2, C3
        const double f0 = Krate::DSP::ShapeCharacterTest::noteHz(midi);
        const std::vector<float> anchor = bsfAnchor(f0, n, kBsfFs);
        for (const auto& c : grid) {
            BassStereoField f;
            bsfPrepare(f, c);
            std::vector<float> L = anchor;
            std::vector<float> R = anchor;
            bsfRenderBlocks(f, L.data(), R.data(), n, kBsfBlock, [&](std::size_t) { bsfAdvanceOnly(f); });
            INFO("MIDI " << midi << ", " << bsfDescribe(c));
            Krate::DSP::ProfundumTest::requireLREqual(L.data(), R.data(), n);
            REQUIRE(bsfSameFloats(L, anchor));
        }

        // After a SOLID crossfade: 0.5 s noise in SPREAD, switch to SOLID, the anchor from the crossfade's
        // last sample + 1.
        const auto sw = static_cast<std::size_t>(0.5 * kBsfFs);
        const std::size_t total = sw + 1024 + n;
        const BsfStereo noise = bsfNoise(total);
        BsfControls c{};
        c.mode = BsfMode::Spread;
        c.spread = 1.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        std::vector<float> L(total, 0.0f);
        std::vector<float> R(total, 0.0f);
        std::vector<float> in(total, 0.0f);
        std::size_t anchorStart = kBsfNone;
        std::size_t bad = 0;
        bsfRenderPerSample(
            f, L.data(), R.data(), total,
            [&](std::size_t i) {
                if (i == sw) {
                    BsfControls s = c;
                    s.mode = BsfMode::Solid;
                    f.setControls(s);
                }
                f.advanceControl();
            },
            [&](std::size_t i) {
                if (anchorStart != kBsfNone && i >= anchorStart) {
                    const std::size_t j = i - anchorStart;
                    const float v = j < n ? anchor[j] : 0.0f;
                    L[i] = v;
                    R[i] = v;
                    in[i] = v;
                } else {
                    L[i] = noise.l[i];
                    R[i] = noise.r[i];
                }
            },
            [&](std::size_t i) {
                if (anchorStart != kBsfNone && i >= anchorStart) {
                    if (bsfBits(L[i]) != bsfBits(R[i]) || bsfBits(L[i]) != bsfBits(in[i])) {
                        ++bad;
                    }
                }
                if (i >= sw && anchorStart == kBsfNone && bsfBits(f.modeFade()) == 0u) {
                    anchorStart = i + 1;
                }
            });
        INFO("MIDI " << midi << " after SOLID: anchor from sample " << anchorStart);
        REQUIRE(anchorStart != kBsfNone);
        REQUIRE(total - anchorStart >= n);
        CHECK(bad == 0);
    }
}

// ------------------------------------------------------------------------------
// SC-004(c) / FR-062: the dirty-state Anchor arm (C-2 switch, decay recorded)
// ------------------------------------------------------------------------------

// SC-004(c); plan S1 C-2
TEST_CASE("BassStereoField_AnchorBitIdenticalDirtyState", "[systems][profundum][stereo-field][sc-004][fr-062]") {
    const auto nNoise = static_cast<std::size_t>(0.5 * kBsfFs);
    const auto nA = static_cast<std::size_t>(kBsfFs);
    const std::size_t n = nNoise + nA;
    const BsfStereo noise = bsfNoise(nNoise);
    for (const int midi : {16, 28, 40, 48}) {
        const double f0 = Krate::DSP::ShapeCharacterTest::noteHz(midi);
        const std::vector<float> anchor = bsfAnchor(f0, nA, kBsfFs);
        std::vector<float> inL(n);
        std::vector<float> inR(n);
        for (std::size_t i = 0; i < n; ++i) {
            inL[i] = i < nNoise ? noise.l[i] : anchor[i - nNoise];
            inR[i] = i < nNoise ? noise.r[i] : anchor[i - nNoise];
        }
        BsfControls c{};
        c.mode = BsfMode::Spread;
        BassStereoField f;
        bsfPrepare(f, c);
        std::vector<float> L = inL;
        std::vector<float> R = inR;
        bsfRenderBlocks(f, L.data(), R.data(), n, kBsfBlock, [&](std::size_t) { bsfAdvanceOnly(f); });
        const std::size_t settled = bsfSettledFrom(L, R, inL.data(), nNoise);
        const std::size_t samples = settled - nNoise;
        std::cout << "[T006 SC-004(c) C-2 record] MIDI " << midi
                  << " (SPREAD, 120 Hz, Gravity 0.5): output == anchor and L == R from " << samples << " samples ("
                  << bsfFmt(1000.0 * static_cast<double>(samples) / kBsfFs) << " ms) after the noise"
                  << (settled < n ? "" : " (not within the 1 s render)") << '\n';
        if constexpr (kGateLrBitExactAnyState) {
            REQUIRE(settled == nNoise);
        }
    }
}

// ------------------------------------------------------------------------------
// FR-051 / FR-052 / SC-004: SOLID forces a zero side, the 10 ms crossfade, idle and wake (R-11)
// ------------------------------------------------------------------------------

// SC-004(a2); plan R-11
TEST_CASE("BassStereoField_SolidForcesZeroSide", "[systems][profundum][stereo-field][fr-051][fr-052][sc-004]") {
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        constexpr std::size_t kG = BassStereoField::kControlInterval;
        const std::size_t sw = (static_cast<std::size_t>(std::llround(0.5 * fs)) / kG) * kG;
        const std::size_t back = sw + (static_cast<std::size_t>(std::llround(0.1 * fs)) / kG) * kG;
        const std::size_t n = back + static_cast<std::size_t>(std::llround(0.05 * fs));
        const long long fade = std::llround(0.010 * fs);
        const BsfStereo noise = bsfNoise(n);
        BsfControls c{};
        c.mode = BsfMode::Spread;
        c.spread = 1.0f;
        BassStereoField f;
        bsfPrepare(f, c, fs);
        std::vector<float> L = noise.l;
        std::vector<float> R = noise.r;
        std::size_t zeroAt = kBsfNone;
        std::size_t oneAt = kBsfNone;
        bool pairEqualAtLast = false;
        std::size_t pairBad = 0;
        std::size_t idleBad = 0;
        std::size_t wakeBad = 0;
        bsfRenderPerSample(
            f, L.data(), R.data(), n,
            [&](std::size_t i) {
                if (i == sw) {
                    BsfControls s = c;
                    s.mode = BsfMode::Solid;
                    f.setControls(s);
                }
                if (i == back) {
                    f.setControls(c);
                }
                f.advanceControl();
            },
            [](std::size_t) {},
            [&](std::size_t i) {
                if (i >= sw && i < back && zeroAt == kBsfNone && bsfBits(f.modeFade()) == 0u) {
                    zeroAt = i;
                    pairEqualAtLast = bsfBits(L[i]) == bsfBits(R[i]);
                }
                if (zeroAt != kBsfNone && i < back) {
                    if (!f.sideIdle()) {
                        ++idleBad;
                    }
                    if (i > zeroAt) {
                        const float m = (noise.l[i] + noise.r[i]) * 0.5f;
                        if (bsfBits(L[i]) != bsfBits(R[i]) || bsfBits(L[i]) != bsfBits(m)) {
                            ++pairBad;
                        }
                    }
                }
                if (i >= back) {
                    if (f.sideIdle()) {
                        ++wakeBad;
                    }
                    if (oneAt == kBsfNone && bsfBits(f.modeFade()) == bsfBits(1.0f)) {
                        oneAt = i;
                    }
                }
            });
        INFO("fs " << fs << ": switch at " << sw << ", modeFade() 0 at " << zeroAt << ", back at " << back
                   << ", modeFade() 1 at " << oneAt);
        REQUIRE(zeroAt != kBsfNone);
        const auto lenIn = static_cast<long long>(zeroAt - sw + 1);
        CHECK(std::llabs(lenIn - fade) <= 1);
        CHECK(pairBad == 0);
        CHECK(idleBad == 0);
        REQUIRE(oneAt != kBsfNone);
        const auto lenOut = static_cast<long long>(oneAt - back + 1);
        CHECK(std::llabs(lenOut - fade) <= 1);
        CHECK(wakeBad == 0);
        std::cout << "[T006 FR-052 R-11] fs " << fs << ": crossfade into SOLID " << lenIn << " samples, out of SOLID "
                  << lenOut << " samples (bar " << fade << " +- 1); pair already equal at the crossfade's last sample: "
                  << (pairEqualAtLast ? "yes" : "no") << '\n';
    }
}

// ------------------------------------------------------------------------------
// SC-020 / SC-006 / FR-021: stage-level Gravity attenuation (positive controls)
// ------------------------------------------------------------------------------

// SC-020 (stage), SC-006 positive control; plan S1 C-11, S7.3, model S12.1
TEST_CASE("BassStereoField_GravityAttenuation", "[systems][profundum][stereo-field][sc-020][sc-006][fr-021]") {
    constexpr double kTwoPi = 6.283185307179586476925;
    const auto n = static_cast<std::size_t>(2.0 * kBsfFs);
    const auto measureFrom = static_cast<std::size_t>(kBsfFs);   // the last 1 s
    for (const float xover : kBsfCrossovers) {
        const double fc = static_cast<double>(xover);
        for (const double tone : {0.95 * fc, std::sqrt(20.0 * fc)}) {
            std::vector<float> x(n);
            for (std::size_t i = 0; i < n; ++i) {
                x[i] = static_cast<float>(0.25 * std::sin(kTwoPi * tone * static_cast<double>(i) / kBsfFs));
            }
            const double r = tone / (static_cast<double>(BassStereoField::kStageCrossoverMultiple) * fc);
            const double r4 = r * r * r * r;
            double prevDb = 0.0;
            for (int gi = 0; gi <= 10; ++gi) {
                const float gravity = static_cast<float>(gi) / 10.0f;
                BsfControls c{};
                c.mode = BsfMode::Spread;
                c.gravity = gravity;
                c.crossoverHz = xover;
                BassStereoField f;
                bsfPrepare(f, c);
                std::vector<float> L = x;
                std::vector<float> R(n);
                for (std::size_t i = 0; i < n; ++i) {
                    R[i] = -x[i];
                }
                bsfRenderBlocks(f, L.data(), R.data(), n, kBsfBlock, [&](std::size_t) { bsfAdvanceOnly(f); });
                double eIn = 0.0;
                double eOut = 0.0;
                for (std::size_t i = measureFrom; i < n; ++i) {
                    const double sOut = (static_cast<double>(L[i]) - static_cast<double>(R[i])) * 0.5;
                    eIn += static_cast<double>(x[i]) * static_cast<double>(x[i]);
                    eOut += sOut * sOut;
                }
                const double gainDb = 10.0 * std::log10(std::max(eOut, 1e-300) / eIn);
                const double g = static_cast<double>(BassStereoField::sideGainForGravity(gravity));
                const double analyticDb = 20.0 * std::log10(std::max(g / (1.0 + r4) + r4 / (1.0 + r4), 1e-300));
                INFO("crossover " << xover << " Hz, tone " << bsfFmt(tone) << " Hz, Gravity " << gravity
                                  << ": side out / in " << bsfFmt(gainDb) << " dB (analytic LR4 " << bsfFmt(analyticDb)
                                  << " dB)");
                if (gi == 0) {
                    CHECK(gainDb < 0.0);
                } else {
                    CHECK(gainDb <= prevDb + 0.01);   // attenuation non-decreasing in Gravity
                }
                if (gi == 0 || gi == 10) {
                    std::cout << "[T006 SC-020 stage] crossover " << xover << " Hz, tone " << bsfFmt(tone)
                              << " Hz, Gravity " << gravity << ": " << bsfFmt(gainDb) << " dB (analytic LR4 "
                              << bsfFmt(analyticDb) << " dB" << (gi == 10 ? "; bar <= -24)\n" : "; bar < 0)\n");
                }
                if (gi == 10) {
                    CHECK(gainDb <= -24.0);
                }
                prevDb = gainDb;
            }
        }
    }

    // After a Gravity 0 -> 1 step, currentSideGain() is bit 0 within 10 x kSideGainSmoothMs (4800 samples
    // at 48 kHz).
    {
        BsfControls c{};
        c.mode = BsfMode::Spread;
        c.gravity = 0.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        constexpr std::size_t kStep = 3200;   // a grid point
        const std::size_t total = kStep + 9600;
        const BsfStereo noise = bsfNoise(total);
        std::vector<float> L = noise.l;
        std::vector<float> R = noise.r;
        std::size_t zeroAt = kBsfNone;
        bsfRenderPerSample(
            f, L.data(), R.data(), total,
            [&](std::size_t i) {
                if (i == kStep) {
                    BsfControls s = c;
                    s.gravity = 1.0f;
                    f.setControls(s);
                }
                f.advanceControl();
            },
            [](std::size_t) {},
            [&](std::size_t i) {
                if (i >= kStep && zeroAt == kBsfNone && bsfBits(f.currentSideGain()) == 0u) {
                    zeroAt = i;
                }
            });
        REQUIRE(zeroAt != kBsfNone);
        const std::size_t samples = zeroAt - kStep + 1;
        INFO("currentSideGain() bit 0 after " << samples << " samples");
        CHECK(samples <= 4800);
        std::cout << "[T006 FR-027] Gravity 0 -> 1: currentSideGain() bit 0 after " << samples
                  << " samples (bar <= 4800)\n";
    }
}

// ------------------------------------------------------------------------------
// FR-053: continuous controls never trigger the mode crossfade
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_ContinuousControlsNoCrossfade", "[systems][profundum][stereo-field][fr-053]") {
    const auto n = static_cast<std::size_t>(2.0 * kBsfFs);
    const BsfStereo noise = bsfNoise(n);
    constexpr std::size_t kEventSpacing = 9600;   // 0.2 s, a grid multiple
    for (const BsfMode mode : {BsfMode::Spread, BsfMode::Wide}) {
        BsfControls base{};
        base.mode = mode;
        BassStereoField f;
        bsfPrepare(f, base);
        std::vector<float> L = noise.l;
        std::vector<float> R = noise.r;
        BsfControls cur = base;
        std::size_t bad = 0;
        int events = 0;
        bsfRenderPerSample(
            f, L.data(), R.data(), n,
            [&](std::size_t i) {
                const std::size_t e = i / kEventSpacing;
                if (i > 0 && i % kEventSpacing == 0 && e <= 8) {
                    switch (e) {
                        case 1: cur.spread = 1.0f; break;
                        case 2: cur.gravity = 1.0f; break;
                        case 3: cur.crossoverHz = 250.0f; break;
                        case 4: cur.curve = 0.0f; break;
                        case 5: cur.motionDepth = 1.0f; break;
                        case 6: cur.motionRate = 0.9f; break;
                        case 7: cur.motionSource = BsfSource::Cycle; break;
                        default: cur = base; break;   // 8: every control at once
                    }
                    ++events;
                    f.setControls(cur);
                }
                f.advanceControl();
            },
            [](std::size_t) {},
            [&](std::size_t) {
                if (bsfBits(f.modeFade()) != bsfBits(1.0f)) {
                    ++bad;
                }
            });
        INFO("mode " << static_cast<int>(mode) << ": samples with modeFade() != 1 " << bad);
        CHECK(events == 8);
        CHECK(bad == 0);
    }
}

// ------------------------------------------------------------------------------
// SC-009(c) / FR-024: law / stage crossover agreement and the corner bound by computation (C-18)
// ------------------------------------------------------------------------------

namespace {

struct BsfCornerReading {
    double worstSmoothed = 0.0;       // max |smoothed - handed| at an interval end
    double worstEfficient = 0.0;      // max |lastCoeff - handed|, Efficient rule
    double worstHighAccuracy = 0.0;   // the same under the HighAccuracy rule (printed, not used)
    std::size_t smoothedBad = 0;      // intervals with |smoothed - handed| > 0.1 Hz
    std::size_t cornerBad = 0;        // intervals with |lastCoeff - handed| > residual + 0.1 + 1e-4
};

/// A real OnePoleSmoother configured (kStageCrossoverSmoothMs, fs), snapped to the first handed value,
/// one process() per sample, setTarget at each interval start; the Efficient rule of
/// crossover_filter.h:306-311 and the HighAccuracy rule (:298-303) applied per sample.
BsfCornerReading bsfSimulateCorner(const std::vector<float>& handed, double fs) {
    BsfCornerReading out;
    Krate::DSP::OnePoleSmoother sm;
    Krate::DSP::OnePoleSmoother smHa;
    sm.configure(BassStereoField::kStageCrossoverSmoothMs, static_cast<float>(fs));
    smHa.configure(BassStereoField::kStageCrossoverSmoothMs, static_cast<float>(fs));
    sm.snapTo(handed.front());
    smHa.snapTo(handed.front());
    float lastEff = handed.front();
    float lastHa = handed.front();
    for (const float h : handed) {
        sm.setTarget(h);
        smHa.setTarget(h);
        float v = h;
        for (std::size_t s = 0; s < BassStereoField::kControlInterval; ++s) {
            v = sm.process();
            if (std::abs(v - lastEff) >= Krate::DSP::CrossoverLR4::kHysteresisThreshold) {
                lastEff = v;
            }
            const float vHa = smHa.process();
            if (!smHa.isComplete()) {
                lastHa = vHa;
            }
        }
        const double residual = std::abs(static_cast<double>(v) - static_cast<double>(h));
        const double corner = std::abs(static_cast<double>(lastEff) - static_cast<double>(h));
        out.worstSmoothed = std::max(out.worstSmoothed, residual);
        out.worstEfficient = std::max(out.worstEfficient, corner);
        out.worstHighAccuracy =
            std::max(out.worstHighAccuracy, std::abs(static_cast<double>(lastHa) - static_cast<double>(h)));
        if (residual > 0.1) {
            ++out.smoothedBad;
        }
        if (corner > residual + 0.1 + 1e-4) {
            ++out.cornerBad;
        }
    }
    return out;
}

}  // namespace

// SC-009(c); plan S1 C-18
TEST_CASE("BassStereoField_CrossoverAgreement", "[systems][profundum][stereo-field][sc-009][fr-024]") {
    // (a) the relation and the per-query crossover, after a 60 -> 250 -> 60 Hz step pair
    {
        BsfControls c{};
        c.crossoverHz = 60.0f;
        BassStereoField f;
        bsfPrepare(f, c);
        BsfVoice vs;
        BsfVec out{};
        const auto e1 = static_cast<float>(Krate::DSP::ShapeCharacterTest::noteHz(28));
        constexpr std::size_t kIntervals = 1000;
        constexpr std::size_t kG = BassStereoField::kControlInterval;
        const BsfStereo noise = bsfNoise(kIntervals * kG);
        std::vector<float> L = noise.l;
        std::vector<float> R = noise.r;
        std::size_t stageBad = 0;
        std::size_t voiceBad = 0;
        std::size_t checked = 0;
        float peak = 0.0f;
        for (std::size_t i = 0; i < kIntervals; ++i) {
            if (i == 100) {
                c.crossoverHz = 250.0f;
                f.setControls(c);
            }
            if (i == 500) {
                c.crossoverHz = 60.0f;
                f.setControls(c);
            }
            f.advanceControl();
            f.computePanVector(vs, e1, out);
            f.processBlock(L.data() + i * kG, R.data() + i * kG, kG);
            peak = std::max(peak, f.smoothedCrossoverHz());
            if (i >= 101) {   // from one interval after the first step
                ++checked;
                const float rel = std::abs(f.stageCrossoverHz() -
                                           BassStereoField::kStageCrossoverMultiple * f.smoothedCrossoverHz());
                if (!(rel <= 0.1f)) {
                    ++stageBad;
                }
                if (bsfBits(vs.lastCrossoverHz) != bsfBits(f.smoothedCrossoverHz())) {
                    ++voiceBad;
                }
            }
        }
        INFO("intervals checked " << checked << ", stage mismatches " << stageBad << ", query mismatches "
                                  << voiceBad << ", peak f_c,s " << bsfFmt(peak));
        CHECK(stageBad == 0);
        CHECK(voiceBad == 0);
        CHECK(peak > 249.0f);                     // the step pair moved the crossover ...
        CHECK(f.smoothedCrossoverHz() < 61.0f);   // ... and returned
    }

    // (b) the coefficient corner by computation, at every rate
    for (const double fs : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        const auto intervalsPerSecond =
            static_cast<std::size_t>(fs / static_cast<double>(BassStereoField::kControlInterval));
        // Full-range step: 60 -> 250 at interval 10, back to 60 half way.
        std::vector<float> stepHanded;
        {
            BsfControls c{};
            c.crossoverHz = 60.0f;
            BassStereoField f;
            bsfPrepare(f, c, fs);
            const std::size_t count = intervalsPerSecond;
            for (std::size_t i = 0; i < count; ++i) {
                if (i == 10) {
                    c.crossoverHz = 250.0f;
                    f.setControls(c);
                }
                if (i == count / 2) {
                    c.crossoverHz = 60.0f;
                    f.setControls(c);
                }
                f.advanceControl();
                stepHanded.push_back(f.stageCrossoverHz());
            }
        }
        // Slow sweep: the law crossover moved by 2.5e-5 Hz per interval for 10 s (the stage target by 5e-5).
        std::vector<float> sweepHanded;
        {
            BsfControls c{};
            c.crossoverHz = 120.0f;
            BassStereoField f;
            bsfPrepare(f, c, fs);
            const std::size_t count = 10 * intervalsPerSecond;
            for (std::size_t i = 0; i < count; ++i) {
                c.crossoverHz = static_cast<float>(120.0 + 2.5e-5 * static_cast<double>(i));
                f.setControls(c);
                f.advanceControl();
                sweepHanded.push_back(f.stageCrossoverHz());
            }
        }
        INFO("fs " << fs);
        REQUIRE(sweepHanded.back() > sweepHanded.front());   // the sweep moved the handed value
        const BsfCornerReading step = bsfSimulateCorner(stepHanded, fs);
        const BsfCornerReading sweep = bsfSimulateCorner(sweepHanded, fs);
        CHECK(step.smoothedBad == 0);
        CHECK(step.cornerBad == 0);
        CHECK(sweep.smoothedBad == 0);
        CHECK(sweep.cornerBad == 0);
        std::cout << "[T006 SC-009(c) C-18] fs " << fs << ": step worst |smoothed - handed| "
                  << bsfFmt(step.worstSmoothed) << " Hz, Efficient corner " << bsfFmt(step.worstEfficient)
                  << " Hz; slow sweep worst |smoothed - handed| " << bsfFmt(sweep.worstSmoothed)
                  << " Hz, Efficient corner " << bsfFmt(sweep.worstEfficient)
                  << " Hz, HighAccuracy corner (rejected rule) " << bsfFmt(sweep.worstHighAccuracy) << " Hz\n";
    }
}

// ------------------------------------------------------------------------------
// SC-012 / FR-074: determinism across host block sizes, resets and seeds
// ------------------------------------------------------------------------------

namespace {

struct BsfDetRun {
    std::vector<float> l;
    std::vector<float> r;
    std::vector<float> vecs;
};

/// The determinism schedule at absolute sample t (48 kHz): SPREAD -> WIDE -> SOLID -> SPREAD at 1, 2, 3 s,
/// a Spread ramp 0 -> 1 over 0 - 1 s, MOTION depth 1 from `src`; `moveCrossover` moves 60 -> 250 at 2 s.
BsfControls bsfDetControls(std::size_t t, BsfSource src, bool moveCrossover) {
    const auto sec = static_cast<std::size_t>(kBsfFs);
    BsfControls c{};
    c.mode = BsfMode::Spread;   // [0, 1 s) and from 3 s on
    if (t >= sec && t < 2 * sec) {
        c.mode = BsfMode::Wide;
    } else if (t >= 2 * sec && t < 3 * sec) {
        c.mode = BsfMode::Solid;
    }
    c.spread = std::min(1.0f, static_cast<float>(t) / static_cast<float>(sec));
    c.motionDepth = 1.0f;
    c.motionSource = src;
    c.crossoverHz = 120.0f;
    if (moveCrossover) {
        c.crossoverHz = (t < 2 * sec) ? 60.0f : 250.0f;
    }
    return c;
}

/// Renders `in` through `f` at `hostBlock`, the schedule latched at every grid point, a VoiceState
/// queried every interval at E1 (the emitted vectors are concatenated).
BsfDetRun bsfDeterminismRender(BassStereoField& f, const BsfStereo& in, BsfSource src, std::size_t hostBlock,
                               bool moveCrossover) {
    BsfDetRun out{.l = in.l, .r = in.r, .vecs = {}};
    const std::size_t n = in.l.size();
    out.vecs.reserve((n / BassStereoField::kControlInterval + 1) * kBsfMaxPartials);
    BsfVoice vs;
    BsfVec v{};
    const auto e1 = static_cast<float>(Krate::DSP::ShapeCharacterTest::noteHz(28));
    Krate::DSP::MassDynamicsTest::gridDriver(n, hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        if (atGrid) {
            f.setControls(bsfDetControls(start, src, moveCrossover));
            f.advanceControl();
            f.computePanVector(vs, e1, v);
            out.vecs.insert(out.vecs.end(), v.begin(), v.end());
        }
        f.processBlock(out.l.data() + start, out.r.data() + start, len);
    });
    return out;
}

bool bsfSameRun(const BsfDetRun& a, const BsfDetRun& b) {
    return bsfSameFloats(a.l, b.l) && bsfSameFloats(a.r, b.r) && bsfSameFloats(a.vecs, b.vecs);
}

}  // namespace

// SC-012; plan S1 C-4 (reset snaps the crossover), C-7 (the absolute grid)
TEST_CASE("BassStereoField_Deterministic", "[systems][profundum][stereo-field][sc-012][fr-074]") {
    const BsfStereo in = bsfNoise(static_cast<std::size_t>(4.0 * kBsfFs));
    for (const BsfSource src : {BsfSource::Drift, BsfSource::Cycle}) {
        INFO("source " << static_cast<int>(src));
        const BsfControls start = bsfDetControls(0, src, false);
        BassStereoField ref;
        bsfPrepare(ref, start);
        const BsfDetRun r512 = bsfDeterminismRender(ref, in, src, 512, false);
        REQUIRE(!bsfSameFloats(r512.l, in.l));   // the stage changed the signal (not vacuous)

        for (const std::size_t hb : {std::size_t{1}, std::size_t{17}, std::size_t{32}, std::size_t{64}}) {
            BassStereoField f;
            bsfPrepare(f, start);
            const BsfDetRun r = bsfDeterminismRender(f, in, src, hb, false);
            INFO("host block " << hb);
            CHECK(bsfSameRun(r, r512));
        }

        // Two runs from reset(), the first after a render that moved the crossover 60 -> 250 (C-4).
        BassStereoField f;
        bsfPrepare(f, bsfDetControls(0, src, true));
        const BsfDetRun moved = bsfDeterminismRender(f, in, src, 512, true);
        CHECK(!bsfSameRun(moved, r512));   // the first render is a different render
        f.setControls(start);
        f.reset();
        const BsfDetRun a = bsfDeterminismRender(f, in, src, 512, false);
        f.setControls(start);
        f.reset();
        const BsfDetRun b = bsfDeterminismRender(f, in, src, 512, false);
        CHECK(bsfSameRun(a, b));
        CHECK(bsfSameRun(a, r512));
    }

    // Seeds 0xB17E and 0x1234 (Drift) give different outputs. MOTION feeds the per-voice law, not the
    // stage, so the difference shows in the emitted vectors.
    {
        const BsfControls start = bsfDetControls(0, BsfSource::Drift, false);
        BassStereoField f1;
        BassStereoField f2;
        f1.setDriftSeed(0xB17Eu);
        f2.setDriftSeed(0x1234u);
        bsfPrepare(f1, start);
        bsfPrepare(f2, start);
        const BsfDetRun r1 = bsfDeterminismRender(f1, in, BsfSource::Drift, 512, false);
        const BsfDetRun r2 = bsfDeterminismRender(f2, in, BsfSource::Drift, 512, false);
        CHECK(!bsfSameFloats(r1.vecs, r2.vecs));
    }
}

// ------------------------------------------------------------------------------
// SC-011 / FR-071: no allocation on the audio-thread methods, and the fresh-field arm
// ------------------------------------------------------------------------------

// SC-011; plan S1 C-4 (reset() calls CrossoverLR4::prepare inside the scope)
TEST_CASE("BassStereoField_NoAllocation", "[systems][profundum][stereo-field][sc-011][fr-071]") {
    const BsfStereo noise = bsfNoise(4096);
    const BsfControls c{};
    BassStereoField f;
    bsfPrepare(f, c);
    {
        std::vector<float> wL = noise.l;
        std::vector<float> wR = noise.r;
        bsfRenderBlocks(f, wL.data(), wR.data(), wL.size(), kBsfBlock, [&](std::size_t) { bsfAdvanceOnly(f); });
    }
    std::vector<float> L = noise.l;
    std::vector<float> R = noise.r;
    BsfVoice vs;
    BsfVec v{};
    float sink = 0.0f;
    std::size_t allocations = 0;
    {
        const TestHelpers::AllocationScope scope;
        f.setControls(c);
        f.advanceControl();
        f.computePanVector(vs, 41.2f, v);
        const auto o = f.wideOffsets(Krate::DSP::BassCharacterStage::Algorithm::Tube);
        sink += o.amount + o.tone + v[1];
        f.processBlock(L.data(), R.data(), 1);
        f.processBlock(L.data(), R.data(), 32);
        f.processBlock(L.data(), R.data(), 4096);
        f.reset();
        BsfControls m = c;
        for (const BsfMode mode : {BsfMode::Solid, BsfMode::Wide, BsfMode::Spread}) {
            m.mode = mode;
            f.setControls(m);
            f.advanceControl();
            f.processBlock(L.data(), R.data(), 32);
        }
        m.motionSource = BsfSource::Cycle;
        m.motionDepth = 1.0f;
        f.setControls(m);
        f.advanceControl();
        f.processBlock(L.data(), R.data(), 32);
        f.setTempoBpm(140.0f);
        f.setDriftSeed(0x1234u);
        f.advanceControl();
        f.processBlock(L.data(), R.data(), 32);
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(allocations == 0);
    CHECK(Krate::DSP::detail::isFinite(sink));

    // Fresh-field arm: a never-prepared field, inside a scope.
    std::vector<float> fl(64);
    std::vector<float> fr(64);
    std::copy_n(noise.l.begin(), 64, fl.begin());
    std::copy_n(noise.r.begin(), 64, fr.begin());
    const std::vector<float> inL = fl;
    const std::vector<float> inR = fr;
    BassStereoField fresh;
    BsfVoice freshVs;
    BsfVec freshV{};
    std::size_t freshAllocations = 0;
    {
        const TestHelpers::AllocationScope scope;
        fresh.reset();
        fresh.advanceControl();
        fresh.computePanVector(freshVs, 41.2f, freshV);
        fresh.processBlock(fl.data(), fr.data(), 64);
        freshAllocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(freshAllocations == 0);
    std::size_t freshBad = 0;
    for (std::size_t i = 0; i < 64; ++i) {
        const float m = (inL[i] + inR[i]) * 0.5f;   // the unprepared crossover returns {0, 0}: M +- 0
        if (!Krate::DSP::detail::isFinite(fl[i]) || !Krate::DSP::detail::isFinite(fr[i]) ||
            bsfBits(fl[i]) != bsfBits(m) || bsfBits(fr[i]) != bsfBits(m)) {
            ++freshBad;
        }
    }
    CHECK(freshBad == 0);

    // A following prepare + reset render equals one from a field that never saw the early calls.
    bsfPrepare(fresh, c);
    BassStereoField clean;
    bsfPrepare(clean, c);
    const BsfStereo longNoise = bsfNoise(static_cast<std::size_t>(kBsfFs));
    std::vector<float> aL = longNoise.l;
    std::vector<float> aR = longNoise.r;
    std::vector<float> bL = longNoise.l;
    std::vector<float> bR = longNoise.r;
    bsfRenderBlocks(fresh, aL.data(), aR.data(), aL.size(), kBsfBlock, [&](std::size_t) { bsfAdvanceOnly(fresh); });
    bsfRenderBlocks(clean, bL.data(), bR.data(), bL.size(), kBsfBlock, [&](std::size_t) { bsfAdvanceOnly(clean); });
    CHECK(bsfSameFloats(aL, bL));
    CHECK(bsfSameFloats(aR, bR));
}

// ------------------------------------------------------------------------------
// FR-001 / FR-072: processBlock edge cases (null, zero length, right == left)
// ------------------------------------------------------------------------------

TEST_CASE("BassStereoField_ProcessEdgeCases", "[systems][profundum][stereo-field][fr-001][fr-072]") {
    const BsfStereo noise = bsfNoise(64);
    BassStereoField f;
    bsfPrepare(f, BsfControls{});
    f.advanceControl();

    std::vector<float> L = noise.l;
    std::vector<float> R = noise.r;
    f.processBlock(nullptr, R.data(), 64);
    CHECK(bsfSameFloats(R, noise.r));
    f.processBlock(L.data(), nullptr, 64);
    CHECK(bsfSameFloats(L, noise.l));
    f.processBlock(L.data(), R.data(), 0);
    CHECK(bsfSameFloats(L, noise.l));
    CHECK(bsfSameFloats(R, noise.r));

    // right == left, finite input: returned unchanged.
    std::vector<float> mono = noise.l;
    f.processBlock(mono.data(), mono.data(), 64);
    CHECK(bsfSameFloats(mono, noise.l));

    // right == left with non-finite samples: 0.0f (bit 0) there, the rest unchanged.
    std::vector<float> bad = noise.l;
    bad[10] = bsfNaN();
    bad[20] = bsfPosInf();
    bad[30] = bsfNegInf();
    f.processBlock(bad.data(), bad.data(), 64);
    std::size_t wrong = 0;
    for (std::size_t i = 0; i < 64; ++i) {
        const bool injected = (i == 10 || i == 20 || i == 30);
        const std::uint32_t expected = injected ? 0u : bsfBits(noise.l[i]);
        if (bsfBits(bad[i]) != expected) {
            ++wrong;
        }
    }
    CHECK(wrong == 0);
}
