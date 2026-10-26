// ==============================================================================
// Layer 3: System Tests - BassMorphEngine
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// Will hold: SC-001 - SC-003, SC-004(c)(d) engine, SC-005 engine, SC-006(a), SC-007(c)(d), SC-011, SC-013, SC-014, SC-019(b), SC-020 - SC-022, SC-024 engine.
// Filled by: T003, T004, T005 (each appends). T001 stub: registered in dsp/tests/CMakeLists.txt, no TEST_CASE yet.
//
// T003 (this block): FR-010 - FR-016, FR-020 - FR-022, FR-035, FR-050 - FR-052; SC-001, SC-002, SC-003(a)(c),
// SC-014, SC-021(a), SC-022; plan S4.1 - S4.4, C-3, C-5, C-7, C-14; tasks.md deviations 4, 11.
// T004 (appended): FR-030 - FR-037, FR-040, FR-053 - FR-055, FR-061, FR-082; SC-003(b), SC-004(c), SC-004(d) engine,
// SC-005 engine, SC-013, SC-019(b), SC-020, SC-021(b), SC-024 engine; plan S4.5 - S4.7, S6, C-2, C-4, C-6. From T004
// on the engine resolves interior positions by the S4.6 laws and the S4.5 per-group selection machine.
// T005 (appended): FR-062, FR-080, FR-081 restore side; SC-006(a), SC-007(c)(d), SC-011; plan S4.8, S4.9, C-2, C-7;
// tasks.md deviation 12 (BassMorph_Idempotent).
// Engine-only: no render, no FTZ / DAZ (tasks.md rule 9, plan R-7). The engine at this stage resolves every
// interior position to A (tasks.md deviation 4); every T003 assertion holds against that resolver.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>

#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/bass_morph_engine.h>

#include "bass_morph_state_helpers.h"

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
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Qualified calls into the helper namespace (tasks.md rule 5); no using-directive.
namespace BassMorphTest = Krate::DSP::BassMorphTest;

namespace {

using Krate::DSP::BassMorphEngine;
using Krate::DSP::BassStereoField;
using Krate::DSP::MassLaw;
using Krate::DSP::MorphState;
using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
namespace io = Krate::DSP::detail::morph_state_io;

using BmEndpoint = BassMorphEngine::Endpoint;
using BmMode = BassMorphEngine::AnchorMorphMode;
using BmResolved = BassMorphEngine::Resolved;

constexpr std::array<BmMode, 3> bmModes{BmMode::Lock, BmMode::Blend, BmMode::Punch};
constexpr double bmFlEps = static_cast<double>(std::numeric_limits<float>::epsilon());

/// B3-1 (ruled 2026-10-19): the absolute rounding term of SC-003(a) / SC-022, 0.5 ulp of the larger of the two
/// positions, because SlewLimiter::process adds the step in float (smoother.h:549).
double bmHalfUlp(float before, float after) {
    return 0.5 * static_cast<double>(BassMorphTest::ulpOf(std::fabs(before) > std::fabs(after) ? before : after));
}

const char* bmModeName(BmMode m) {
    switch (m) {
        case BmMode::Lock: return "Lock";
        case BmMode::Blend: return "Blend";
        case BmMode::Punch: return "Punch";
    }
    return "?";
}

bool bmSameBits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// FR-020: Delta p_max = 32 000 / (rampMs * fs), in double.
double bmDeltaMax(double rampMs, double fs) {
    return 32000.0 / (rampMs * fs);
}

/// SC-003(c): ceil(rampMs * fs / 32 000) intervals for a full traversal.
long bmTraversal(double rampMs, double fs) {
    return static_cast<long>(std::ceil(rampMs * fs / 32000.0));
}

/// tasks.md rule 17: the hard cap of "advance until landed".
long bmLandCap(double rampMs, double fs) {
    return 2 * bmTraversal(rampMs, fs) + 8;
}

/// tasks.md rule 17: landed = position() == targetPosition() and anchorPosition() == curve(mode, position()),
/// both bitwise.
bool bmIsLanded(const BassMorphEngine& e) {
    return bmSameBits(e.position(), e.targetPosition())
           && bmSameBits(e.anchorPosition(), BassMorphEngine::curve(e.anchorMorphMode(), e.position()));
}

/// tasks.md rule 17: advanceInterval() until landed, with a hard cap of 2 * ceil(rampMs * fs / 32 000) + 8
/// intervals. Hitting the cap is a test failure with the values printed. Returns the intervals advanced.
long bmLand(BassMorphEngine& e, double rampMs, double fs) {
    const long cap = bmLandCap(rampMs, fs);
    long n = 0;
    while (!bmIsLanded(e)) {
        if (n >= cap) {
            std::ostringstream os;
            os << std::setprecision(6) << "bmLand cap hit: cap " << cap << " rampMs " << rampMs << " fs " << fs
               << " mode " << bmModeName(e.anchorMorphMode()) << " p " << e.position() << " target "
               << e.targetPosition() << " p_a " << e.anchorPosition() << " c(p) "
               << BassMorphEngine::curve(e.anchorMorphMode(), e.position());
            FAIL(os.str());
        }
        static_cast<void>(e.advanceInterval());
        ++n;
    }
    return n;
}

/// Engine at fs with the given ramp and mode, endpoints set, landed at p0 by reset().
BassMorphEngine bmMakeEngine(double fs, float rampMs, BmMode mode, const MorphState& a, const MorphState& b,
                             float p0) {
    BassMorphEngine e;
    e.prepare(fs);
    e.setPositionRampMs(rampMs);
    e.setAnchorMorphMode(mode);
    REQUIRE(e.setEndpoint(BmEndpoint::A, a));
    REQUIRE(e.setEndpoint(BmEndpoint::B, b));
    e.setPosition(p0);
    e.reset();
    return e;
}

/// SC-001's expected state at p = 1: B, with the whole Anchor block taken from A in Lock (FR-052).
MorphState bmExpectedAtOne(const MorphState& a, const MorphState& b, BmMode mode) {
    MorphState e = b;
    if (mode == BmMode::Lock) {
        e.anchor = a.anchor;
        e.anchorFoldFeed = a.anchorFoldFeed;
    }
    return e;
}

/// SC-001 procedure (test 9) on one engine already prepared at fs with the ramp and mode set: endpoints (a, b);
/// setPosition(0) + reset() -> A; setPosition(1) + land -> E (bmExpectedAtOne), anchorMass A.mass in Lock and
/// B.mass otherwise; setPosition(0) + land -> A. Returns the number of mismatching checks (0 - 3).
int bmIdentityMismatches(BassMorphEngine& e, const MorphState& a, const MorphState& b, double rampMs, double fs) {
    int mismatches = 0;
    REQUIRE(e.setEndpoint(BmEndpoint::A, a));
    REQUIRE(e.setEndpoint(BmEndpoint::B, b));
    const BmMode mode = e.anchorMorphMode();

    e.setPosition(0.0f);
    e.reset();
    if (!BassMorphTest::bitEqual(e.resolved().state, a) || !bmSameBits(e.resolved().anchorMass, a.mass)) {
        ++mismatches;
    }

    e.setPosition(1.0f);
    static_cast<void>(bmLand(e, rampMs, fs));
    const MorphState expected = bmExpectedAtOne(a, b, mode);
    const float expectedMass = (mode == BmMode::Lock) ? a.mass : b.mass;
    if (!BassMorphTest::bitEqual(e.resolved().state, expected) || !bmSameBits(e.resolved().anchorMass, expectedMass)
        || !bmSameBits(e.position(), 1.0f)) {
        ++mismatches;
    }

    e.setPosition(0.0f);
    static_cast<void>(bmLand(e, rampMs, fs));
    if (!BassMorphTest::bitEqual(e.resolved().state, a) || !bmSameBits(e.resolved().anchorMass, a.mass)
        || !bmSameBits(e.position(), 0.0f)) {
        ++mismatches;
    }
    return mismatches;
}

/// The seven named recipes (FR-071), each embedded in MorphState{}.
std::array<SpectralShapeRecipe::Controls, 7> bmNamedRecipes() {
    return {SpectralShapeRecipe::kDefaultControls, SpectralShapeRecipe::kSineAnchor,
            SpectralShapeRecipe::kTriangleAnchor,  SpectralShapeRecipe::kSawAnchor,
            SpectralShapeRecipe::kHeavy,           SpectralShapeRecipe::kHollow,
            SpectralShapeRecipe::kGrowl};
}

/// Per-interval step bookkeeping for SC-003(a): |dp| and |dp_a| against Delta p_max * (1 + 4 FLT_EPSILON)
/// + 0.5 ulp(max(|p_old|, |p_new|)) (B3-1).
struct BmStepStats {
    long intervals = 0;
    long violations = 0;
    double worstRatio = 0.0;   // max |delta| / Delta p_max
    double worstP = 0.0;       // the p after the worst step
};

void bmRecordStep(BmStepStats& st, float before, float after, float pAfter, double dMax) {
    const double delta = std::fabs(static_cast<double>(after) - static_cast<double>(before));
    const double ratio = delta / dMax;
    if (ratio > st.worstRatio) {
        st.worstRatio = ratio;
        st.worstP = static_cast<double>(pAfter);
    }
    if (delta > dMax * (1.0 + 4.0 * bmFlEps) + bmHalfUlp(before, after)) {
        ++st.violations;
    }
}

/// One advanceInterval() with SC-003(a) bookkeeping of p and p_a.
void bmStepChecked(BassMorphEngine& e, BmStepStats& st, double dMax) {
    const float p0 = e.position();
    const float pa0 = e.anchorPosition();
    static_cast<void>(e.advanceInterval());
    bmRecordStep(st, p0, e.position(), e.position(), dMax);
    bmRecordStep(st, pa0, e.anchorPosition(), e.position(), dMax);
    ++st.intervals;
}

/// u = (w >> 8) * 2^-24 from one raw word (plan R-8; never std::uniform_*_distribution).
double bmUnit(std::mt19937& rng) {
    return BassMorphTest::unitFromWord(static_cast<std::uint32_t>(rng()));
}

// ---- T004 helpers (plan S4.5 - S4.6, S6) ----

using BmLaw = MorphState::Law;
using BmGroup = MorphState::SelectionGroup;

constexpr std::array<BmGroup, MorphState::kNumSelectionGroups> bmGroups{
    BmGroup::Character, BmGroup::ImageMode, BmGroup::FieldMotion, BmGroup::Motion, BmGroup::Anchor};

const char* bmGroupName(BmGroup g) {
    switch (g) {
        case BmGroup::None: return "None";
        case BmGroup::Character: return "Character";
        case BmGroup::ImageMode: return "ImageMode";
        case BmGroup::FieldMotion: return "FieldMotion";
        case BmGroup::Motion: return "Motion";
        case BmGroup::Anchor: return "Anchor";
    }
    return "?";
}

/// SC-019(b) / SC-003(b) / SC-024: the ulp multiplier per law (2 linear / equal-power, 8 logarithmic).
double bmUlpK(BmLaw law) {
    return law == BmLaw::Logarithmic ? 8.0 : 2.0;
}

/// The closed form of a continuous law in double at t (FR-031 - FR-033), with the FR-035 identity branches:
/// t == 0 or bitwise-equal endpoints -> x_A, t == 1 -> x_B.
double bmLawDouble(BmLaw law, float xa, float xb, float o, double t) {
    if (t == 0.0 || bmSameBits(xa, xb)) {
        return static_cast<double>(xa);
    }
    if (t == 1.0) {
        return static_cast<double>(xb);
    }
    const double da = static_cast<double>(xa);
    const double db = static_cast<double>(xb);
    const double dO = static_cast<double>(o);
    switch (law) {
        case BmLaw::Linear: return da + t * (db - da);
        case BmLaw::EqualPower: return std::sqrt((1.0 - t) * da * da + t * db * db);
        case BmLaw::Logarithmic: {
            const double la = std::log2(da + dO);
            const double lb = std::log2(db + dO);
            return std::exp2(la + t * (lb - la)) - dO;
        }
        case BmLaw::Stepped: break;
    }
    return da;
}

/// SC-003(b), plan S6: the per-interval bound of a continuous law for a position step d.
/// Linear |x_B - x_A| d; logarithmic (max + o) |((x_B + o) / (x_A + o))^d - 1|; equal-power sqrt(d) max(m_A, m_B).
double bmLawStepBound(BmLaw law, float xa, float xb, float o, double d) {
    const double da = static_cast<double>(xa);
    const double db = static_cast<double>(xb);
    const double dO = static_cast<double>(o);
    switch (law) {
        case BmLaw::Linear: return std::fabs(db - da) * d;
        case BmLaw::Logarithmic: return (std::max(da, db) + dO) * std::fabs(std::pow((db + dO) / (da + dO), d) - 1.0);
        case BmLaw::EqualPower: return std::sqrt(d) * std::max(std::fabs(da), std::fabs(db));
        case BmLaw::Stepped: break;
    }
    return 0.0;
}

/// The value of F32 leaf idx (0 for a non-F32 leaf).
float bmLeafValue(const MorphState& s, std::size_t idx) {
    float out = 0.0f;
    io::forEachMorphLeaf(
        [&]([[maybe_unused]] auto leaf, [[maybe_unused]] const auto& v) {
            using T = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>) {
                if (decltype(leaf)::value == idx) {
                    out = v;
                }
            }
        },
        s);
    return out;
}

/// SC-019(b): sets leaf idx of b to a value drawn by the row's rule, different from a's: F32 uniform on [lo, hi]
/// (log rows log-uniform on [lo + o, hi + o] - o), at least 1 % of the span away from a's value; Enum8 another
/// enumerator; Bool the other value.
void bmDrawDifferentLeaf(MorphState& b, const MorphState& a, std::size_t idx, std::mt19937& rng) {
    io::forEachMorphLeaf(
        [&](auto leaf, auto& y, const auto& x) {
            if (decltype(leaf)::value != idx) {
                return;
            }
            using T = std::remove_cvref_t<decltype(y)>;
            if constexpr (std::is_same_v<T, float>) {
                constexpr MorphState::FieldInfo row = io::kFieldTable[decltype(leaf)::value];
                const double lo = static_cast<double>(row.lo);
                const double hi = static_cast<double>(row.hi);
                const double o = static_cast<double>(row.logOffset);
                float f = x;
                for (int tries = 0; tries < 64; ++tries) {
                    const double u = bmUnit(rng);
                    double v = lo + u * (hi - lo);
                    if constexpr (row.law == BmLaw::Logarithmic) {
                        const double l0 = std::log2(lo + o);
                        const double l1 = std::log2(hi + o);
                        v = std::exp2(l0 + u * (l1 - l0)) - o;
                    }
                    f = std::clamp(static_cast<float>(v), row.lo, row.hi);
                    if constexpr (row.hiOpen) {
                        if (f >= row.hi) {
                            f = std::nextafter(row.hi, row.lo);
                        }
                    }
                    if (std::fabs(static_cast<double>(f) - static_cast<double>(x)) >= 0.01 * (hi - lo)) {
                        break;
                    }
                }
                REQUIRE(std::fabs(static_cast<double>(f) - static_cast<double>(x)) >= 0.01 * (hi - lo));
                y = f;
            } else if constexpr (std::is_same_v<T, bool>) {
                y = !x;
            } else {
                constexpr std::uint32_t count =
                    static_cast<std::uint32_t>(io::kFieldTable[decltype(leaf)::value].maxEnum) + 1u;
                const std::uint32_t step = 1u + static_cast<std::uint32_t>(rng()) % (count - 1u);
                y = static_cast<T>(static_cast<std::uint8_t>((static_cast<std::uint32_t>(x) + step) % count));
            }
        },
        b, a);
}

/// The discrete (Stepped) leaves of group g of x equal those of ref.
bool bmGroupLeavesEqual(const MorphState& x, const MorphState& ref, BmGroup g) {
    bool equal = true;
    io::forEachMorphLeaf(
        [&](auto leaf, [[maybe_unused]] const auto& u, [[maybe_unused]] const auto& v) {
            constexpr MorphState::FieldInfo row = io::kFieldTable[decltype(leaf)::value];
            if constexpr (row.law == BmLaw::Stepped) {
                if (row.group == g) {
                    equal = equal && u == v;
                }
            }
        },
        x, ref);
    return equal;
}

/// FR-034's governing amount of group g (plan S4.5). g is never None here.
float& bmGoverning(MorphState& s, BmGroup g) {
    switch (g) {
        case BmGroup::Character: return s.characterAmount;
        case BmGroup::ImageMode: return s.field.spread;
        case BmGroup::FieldMotion: return s.field.motionDepth;
        case BmGroup::Motion: return s.motion.amount;
        case BmGroup::Anchor: return s.anchor.level;
        case BmGroup::None: break;
    }
    FAIL("bmGoverning: group None has no governing amount");
    return s.characterAmount;
}

/// SC-019(b) governing-amount rows (plan S7.4): every discrete leaf of group g differs between a and b.
void bmConfigureGroupDiscretes(BmGroup g, MorphState& a, MorphState& b) {
    using Src = BassStereoField::MotionSource;
    using MSrc = MorphState::MotionConfig::Source;
    using MTgt = MorphState::MotionConfig::Target;
    using MPol = MorphState::MotionConfig::Polarity;
    switch (g) {
        case BmGroup::Character:
            a.characterAlgorithm = Krate::DSP::BassCharacterStage::Algorithm::Tube;
            b.characterAlgorithm = Krate::DSP::BassCharacterStage::Algorithm::Fold;
            break;
        case BmGroup::ImageMode:
            a.field.mode = BassStereoField::ImageMode::Solid;
            b.field.mode = BassStereoField::ImageMode::Wide;
            break;
        case BmGroup::FieldMotion:
            a.field.motionSource = Src::Drift;
            b.field.motionSource = Src::Cycle;
            a.field.cycleSync = false;
            b.field.cycleSync = true;
            a.field.cycleNote = Krate::DSP::NoteValue::Quarter;
            b.field.cycleNote = Krate::DSP::NoteValue::Whole;
            a.field.cycleModifier = Krate::DSP::NoteModifier::None;
            b.field.cycleModifier = Krate::DSP::NoteModifier::Dotted;
            break;
        case BmGroup::Motion:
            a.motion.source = MSrc::Env;
            b.motion.source = MSrc::Cycle;
            a.motion.target = MTgt::Mass;
            b.motion.target = MTgt::Body;
            a.motion.cycleShape = Krate::DSP::Waveform::Sine;
            b.motion.cycleShape = Krate::DSP::Waveform::Triangle;
            a.motion.cycleSync = false;
            b.motion.cycleSync = true;
            a.motion.cycleNote = Krate::DSP::NoteValue::Quarter;
            b.motion.cycleNote = Krate::DSP::NoteValue::Whole;
            a.motion.cycleModifier = Krate::DSP::NoteModifier::None;
            b.motion.cycleModifier = Krate::DSP::NoteModifier::Dotted;
            a.motion.cycleRetrigger = true;
            b.motion.cycleRetrigger = false;
            a.motion.polarity = MPol::Bipolar;
            b.motion.polarity = MPol::Unipolar;
            a.motion.depth = 1.0f;   // plan S7.4: motion.depth 1 on both sides (a depth accessor would fail)
            b.motion.depth = 1.0f;
            break;
        case BmGroup::Anchor:
            a.anchor.waveform = Krate::DSP::AnchorOscillator::Waveform::Sine;
            b.anchor.waveform = Krate::DSP::AnchorOscillator::Waveform::Triangle;
            a.anchor.mode = Krate::DSP::AnchorOscillator::Mode::Lock;
            b.anchor.mode = Krate::DSP::AnchorOscillator::Mode::Punch;
            break;
        case BmGroup::None: break;
    }
}

/// The rule-3 Character pair (both governing amounts non-zero): A Tube / 0.8, B Fold / 0.8, all else default.
std::pair<MorphState, MorphState> bmRule3CharacterPair() {
    MorphState a{};
    a.characterAlgorithm = Krate::DSP::BassCharacterStage::Algorithm::Tube;
    a.characterAmount = 0.8f;
    MorphState b = a;
    b.characterAlgorithm = Krate::DSP::BassCharacterStage::Algorithm::Fold;
    REQUIRE(Krate::DSP::isValidMorphState(a));
    REQUIRE(Krate::DSP::isValidMorphState(b));
    return {a, b};
}

/// tasks.md rule 11: evidence goes under specs/profundum-phase8-bass-morph/evidence/.
std::string bmEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase8-bass-morph/evidence/";
}

/// SC-003(b) bookkeeping: per continuous leaf and interval, |dx| against bound(|dq|) * (1 + 4 FLT_EPSILON)
/// + k ulp(max(|x_A|, |x_B|)) (plan S6), q = p_a for the Anchor block and p otherwise, |dq| the interval's own
/// step (SECTION (a) gates |dq| against Delta p_max).
struct BmLawStepStats {
    long intervals = 0;
    long checks = 0;
    long violations = 0;
    double worstRatio = 0.0;   // max |dx| / allowed
    const char* worstLeaf = "";
};

void bmRecordLawStep(BmLawStepStats& st, const BmResolved& before, const BmResolved& after, const MorphState& a,
                     const MorphState& b) {
    const double dp = std::fabs(static_cast<double>(after.position) - static_cast<double>(before.position));
    const double dpa =
        std::fabs(static_cast<double>(after.anchorPosition) - static_cast<double>(before.anchorPosition));
    io::forEachMorphLeaf(
        [&](auto leaf, [[maybe_unused]] const auto& x0, [[maybe_unused]] const auto& x1,
            [[maybe_unused]] const auto& xa, [[maybe_unused]] const auto& xb) {
            constexpr MorphState::FieldInfo row = io::kFieldTable[decltype(leaf)::value];
            if constexpr (row.law != BmLaw::Stepped) {
                const double d = row.anchorBlock ? dpa : dp;
                const double bound = bmLawStepBound(row.law, xa, xb, row.logOffset, d);
                const double ulp =
                    static_cast<double>(BassMorphTest::ulpOf(std::max(std::fabs(xa), std::fabs(xb))));
                const double allowed = bound * (1.0 + 4.0 * bmFlEps) + bmUlpK(row.law) * ulp;
                const double delta = std::fabs(static_cast<double>(x1) - static_cast<double>(x0));
                ++st.checks;
                if (delta / allowed > st.worstRatio) {
                    st.worstRatio = delta / allowed;
                    st.worstLeaf = row.name;
                }
                if (delta > allowed) {
                    ++st.violations;
                }
            }
        },
        before.state, after.state, a, b);
    ++st.intervals;
}

/// SC-003 runs (the T003 SECTION (a) procedure, unchanged): jumps 0 -> 1, 1 -> 0, 0.3 -> 0.7 from a landed start,
/// then 30 s of seeded random automation (seed 0x8003, a new target every 25 - 400 ms, 10 % exact 0 / 1, an
/// Anchor-mode change every 0.5 s). onStep(engine, before) is called after every advanceInterval().
template <class OnStep>
void bmSc003Runs(double fs, float ramp, BmMode mode, const MorphState& a, const MorphState& b, const OnStep& onStep) {
    constexpr std::array<std::pair<float, float>, 3> jumps{{{0.0f, 1.0f}, {1.0f, 0.0f}, {0.3f, 0.7f}}};
    for (const auto& [from, to] : jumps) {
        BassMorphEngine e = bmMakeEngine(fs, ramp, mode, a, b, from);
        e.setPosition(to);
        const long cap = bmLandCap(static_cast<double>(ramp), fs);
        long n = 0;
        while (!bmIsLanded(e) && n < cap) {
            const BmResolved before = e.resolved();
            static_cast<void>(e.advanceInterval());
            onStep(e, before);
            ++n;
        }
        CHECK(bmIsLanded(e));
    }

    std::mt19937 rng(0x8003u);
    BassMorphEngine e = bmMakeEngine(fs, ramp, mode, a, b, 0.0f);
    const auto total = static_cast<long>(std::ceil(30.0 * fs / 32.0));
    const auto modePeriod = static_cast<long>(std::lround(0.5 * fs / 32.0));
    long untilNextTarget = 0;
    auto modeIndex = static_cast<std::size_t>(mode);
    for (long i = 0; i < total; ++i) {
        if (untilNextTarget <= 0) {
            float target = 0.0f;
            if (bmUnit(rng) < 0.1) {
                target = (static_cast<std::uint32_t>(rng()) & 1u) != 0u ? 1.0f : 0.0f;
            } else {
                target = static_cast<float>(bmUnit(rng));
            }
            e.setPosition(target);
            const double ms = 25.0 + 375.0 * bmUnit(rng);
            untilNextTarget = std::max(1L, static_cast<long>(std::lround(ms * fs / 32000.0)));
        }
        if (i > 0 && i % modePeriod == 0) {
            modeIndex = (modeIndex + 1) % bmModes.size();
            e.setAnchorMorphMode(bmModes[modeIndex]);
        }
        const BmResolved before = e.resolved();
        static_cast<void>(e.advanceInterval());
        onStep(e, before);
        --untilNextTarget;
    }
}

/// FR-037 bookkeeping: along a sequence whose p and p_a never decrease, every continuous leaf moves toward x_B
/// (x_B >= x_A -> non-decreasing, else non-increasing), zero tolerance; anchorMass likewise against p_a.
struct BmMonotoneStats {
    long checks = 0;
    long reversals = 0;
    long positionReversals = 0;
    long interiorValues = 0;   // non-vacuity: resolved values strictly between the endpoints
};

void bmPrintReversal(const char* name, const BmResolved& prev, const BmResolved& cur, bool anchorBlock, float x0,
                     float x1, float xa, float xb) {
    std::cout << std::setprecision(9) << "BassMorph_MonotoneLaws reversal: leaf " << name << " q "
              << (anchorBlock ? prev.anchorPosition : prev.position) << " -> "
              << (anchorBlock ? cur.anchorPosition : cur.position) << " value " << x0 << " -> " << x1 << " (x_A "
              << xa << ", x_B " << xb << ")\n";
}

void bmCheckMonotone(BmMonotoneStats& st, const BmResolved& prev, const BmResolved& cur, const MorphState& a,
                     const MorphState& b) {
    if (cur.position < prev.position || cur.anchorPosition < prev.anchorPosition) {
        ++st.positionReversals;
    }
    io::forEachMorphLeaf(
        [&](auto leaf, [[maybe_unused]] const auto& x0, [[maybe_unused]] const auto& x1,
            [[maybe_unused]] const auto& xa, [[maybe_unused]] const auto& xb) {
            constexpr MorphState::FieldInfo row = io::kFieldTable[decltype(leaf)::value];
            if constexpr (row.law != BmLaw::Stepped) {
                ++st.checks;
                const bool ok = (xb >= xa) ? (x1 >= x0) : (x1 <= x0);
                if (!ok) {
                    ++st.reversals;
                    if (st.reversals <= 10) {
                        bmPrintReversal(row.name, prev, cur, row.anchorBlock, x0, x1, xa, xb);
                    }
                }
                if (x1 > std::min(xa, xb) && x1 < std::max(xa, xb)) {
                    ++st.interiorValues;
                }
            }
        },
        prev.state, cur.state, a, b);
    ++st.checks;
    const bool massOk =
        (b.mass >= a.mass) ? (cur.anchorMass >= prev.anchorMass) : (cur.anchorMass <= prev.anchorMass);
    if (!massOk) {
        ++st.reversals;
        if (st.reversals <= 10) {
            bmPrintReversal("anchorMass", prev, cur, true, prev.anchorMass, cur.anchorMass, a.mass, b.mass);
        }
    }
}

/// SC-013: one sample-stamped event (tasks.md T004 test 11).
enum class BmEventKind : std::uint8_t { Position, RampMs, Mode, EndpointA, EndpointB };

struct BmEvent {
    long stamp = 0;
    BmEventKind kind = BmEventKind::Position;
    float value = 0.0f;
    BmMode mode = BmMode::Blend;
    MorphState state{};
};

void bmApplyEvent(BassMorphEngine& e, const BmEvent& ev) {
    switch (ev.kind) {
        case BmEventKind::Position: e.setPosition(ev.value); break;
        case BmEventKind::RampMs: e.setPositionRampMs(ev.value); break;
        case BmEventKind::Mode: e.setAnchorMorphMode(ev.mode); break;
        case BmEventKind::EndpointA: REQUIRE(e.setEndpoint(BmEndpoint::A, ev.state)); break;
        case BmEventKind::EndpointB: REQUIRE(e.setEndpoint(BmEndpoint::B, ev.state)); break;
    }
}

/// SC-013 grid driver: host blocks of hostBlock samples; at every absolute grid sample (multiple of 32) inside a
/// block, every unapplied event with stamp <= that sample is applied, then advanceInterval() once and the Resolved
/// recorded. interior counts the intervals whose state equals neither endpoint (non-vacuity).
std::vector<BmResolved> bmRunPartition(const std::vector<BmEvent>& events, const MorphState& a0, const MorphState& b0,
                                       long total, long hostBlock, long& interior) {
    BassMorphEngine e;
    e.prepare(48000.0);
    REQUIRE(e.setEndpoint(BmEndpoint::A, a0));
    REQUIRE(e.setEndpoint(BmEndpoint::B, b0));
    e.setPosition(0.0f);
    e.reset();
    std::vector<BmResolved> out;
    out.reserve(static_cast<std::size_t>(total) / 32 + 1);
    interior = 0;
    std::size_t next = 0;
    for (long start = 0; start < total; start += hostBlock) {
        const long len = std::min(hostBlock, total - start);
        for (long g = ((start + 31) / 32) * 32; g < start + len; g += 32) {
            while (next < events.size() && events[next].stamp <= g) {
                bmApplyEvent(e, events[next]);
                ++next;
            }
            out.push_back(e.advanceInterval());
            if (!io::leafBitsEqual(out.back().state, e.endpoint(BmEndpoint::A))
                && !io::leafBitsEqual(out.back().state, e.endpoint(BmEndpoint::B))) {
                ++interior;
            }
        }
    }
    return out;
}

}  // namespace

// ==============================================================================
// T003 cases
// ==============================================================================

// FR-010, FR-014 (plan S4.1, S1 C-1; tasks.md deviation 11)
TEST_CASE("BassMorph_InterfaceStatics", "[systems][profundum][bass-morph][fr-010][fr-014]") {
    static_assert(BassMorphEngine::kControlInterval == 32);
    static_assert(BassMorphEngine::kControlInterval == ProfundumCore::kControlInterval);
    static_assert(BassMorphEngine::kControlInterval == MassLaw::kControlInterval);
    static_assert(BassMorphEngine::kControlInterval == BassStereoField::kControlInterval);
    static_assert(BassMorphEngine::kMinPositionRampMs == 5.0f);
    static_assert(BassMorphEngine::kMaxPositionRampMs == 2000.0f);
    static_assert(BassMorphEngine::kDefaultPositionRampMs == 40.0f);
    static_assert(BassMorphEngine::kThetaUp == 0.52f);
    static_assert(BassMorphEngine::kThetaDown == 0.48f);
    static_assert(BassMorphEngine::kPunchExponent == 3);
    static_assert(BassMorphEngine::kDefaultAnchorMorphMode == BmMode::Blend);
    static_assert(BassMorphEngine::kMorphRuntimeFormatVersion == 1);
    static_assert(BassMorphEngine::kMorphRuntimeBytes == 20);
    static_assert(&BassMorphEngine::kLawTable == &io::kFieldTable);   // plan S1 C-1: one table
    static_assert(std::is_same_v<BassMorphEngine::Law, MorphState::Law>);
    static_assert(std::is_same_v<BassMorphEngine::Axis, MorphState::Axis>);
    static_assert(std::is_same_v<BassMorphEngine::SelectionGroup, MorphState::SelectionGroup>);
    static_assert(std::is_copy_constructible_v<BassMorphEngine>);
    static_assert(std::is_nothrow_move_constructible_v<BassMorphEngine>);
    static_assert(sizeof(BmEndpoint) == 1);
    static_assert(sizeof(BmMode) == 1);

    // Fresh engine: default ramp, default mode, selections A (FR-011), group None reads A.
    BassMorphEngine e;
    CHECK(e.positionRampMs() == BassMorphEngine::kDefaultPositionRampMs);
    CHECK(e.anchorMorphMode() == BmMode::Blend);
    CHECK(e.selection(MorphState::SelectionGroup::None) == BmEndpoint::A);
    CHECK(e.selection(MorphState::SelectionGroup::Character) == BmEndpoint::A);
    CHECK(e.selection(MorphState::SelectionGroup::Anchor) == BmEndpoint::A);
}

// FR-012 (plan S1 C-14)
TEST_CASE("BassMorph_EndpointApi", "[systems][profundum][bass-morph][fr-012]") {
    BassMorphEngine fresh;
    CHECK(BassMorphTest::bitEqual(fresh.endpoint(BmEndpoint::A), MorphState{}));
    CHECK(BassMorphTest::bitEqual(fresh.endpoint(BmEndpoint::B), MorphState{}));

    std::mt19937 rng(0x8012u);
    const MorphState sa = BassMorphTest::randomValidState(rng);
    const MorphState sb = BassMorphTest::randomValidState(rng);

    BassMorphEngine e;
    CHECK(e.setEndpoint(BmEndpoint::A, sa));
    CHECK(e.setEndpoint(BmEndpoint::B, sb));
    CHECK(BassMorphTest::bitEqual(e.endpoint(BmEndpoint::A), sa));
    CHECK(BassMorphTest::bitEqual(e.endpoint(BmEndpoint::B), sb));

    // Undeclared endpoint: false, both unchanged; the const accessor reads A (C-14).
    const MorphState other = BassMorphTest::randomValidState(rng);
    CHECK_FALSE(e.setEndpoint(static_cast<BmEndpoint>(2), other));
    CHECK(BassMorphTest::bitEqual(e.endpoint(BmEndpoint::A), sa));
    CHECK(BassMorphTest::bitEqual(e.endpoint(BmEndpoint::B), sb));
    CHECK(BassMorphTest::bitEqual(e.endpoint(static_cast<BmEndpoint>(2)), e.endpoint(BmEndpoint::A)));

    // Invalid state: one field one ulp above its box -> false, unchanged.
    MorphState invalid = other;
    invalid.recipe.depth = std::nextafter(1.0f, 2.0f);
    REQUIRE_FALSE(Krate::DSP::isValidMorphState(invalid));
    CHECK_FALSE(e.setEndpoint(BmEndpoint::A, invalid));
    CHECK_FALSE(e.setEndpoint(BmEndpoint::B, invalid));
    CHECK(BassMorphTest::bitEqual(e.endpoint(BmEndpoint::A), sa));
    CHECK(BassMorphTest::bitEqual(e.endpoint(BmEndpoint::B), sb));

    // Mid-ramp (0 -> 1 at 48 kHz, 10 intervals in): re-setting a bitwise-identical B is a no-op.
    BassMorphEngine live = bmMakeEngine(48000.0, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, sa, sb, 0.0f);
    BassMorphEngine twin = live;
    live.setPosition(1.0f);
    twin.setPosition(1.0f);
    for (int i = 0; i < 10; ++i) {
        static_cast<void>(live.advanceInterval());
        static_cast<void>(twin.advanceInterval());
    }
    CHECK(live.setEndpoint(BmEndpoint::B, sb));
    CHECK(BassMorphTest::bitEqual(live.resolved(), twin.resolved()));
    int mismatches = 0;
    for (int i = 0; i < 16; ++i) {
        const BmResolved& r1 = live.advanceInterval();
        const BmResolved& r2 = twin.advanceInterval();
        if (!BassMorphTest::bitEqual(r1, r2)) {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}

// FR-014
TEST_CASE("BassMorph_AnchorModeApi", "[systems][profundum][bass-morph][fr-014]") {
    BassMorphEngine fresh;
    CHECK(fresh.anchorMorphMode() == BmMode::Blend);

    const auto [a, b] = BassMorphTest::anchorCurvePair();
    BassMorphEngine live = bmMakeEngine(48000.0, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, 0.0f);
    BassMorphEngine twin = live;
    live.setPosition(1.0f);
    twin.setPosition(1.0f);
    for (int i = 0; i < 10; ++i) {
        static_cast<void>(live.advanceInterval());
        static_cast<void>(twin.advanceInterval());
    }
    live.setAnchorMorphMode(static_cast<BmMode>(3));
    CHECK(live.anchorMorphMode() == BmMode::Blend);
    live.setAnchorMorphMode(static_cast<BmMode>(255));
    CHECK(live.anchorMorphMode() == BmMode::Blend);

    int mismatches = 0;
    for (int i = 0; i < 64; ++i) {
        static_cast<void>(live.advanceInterval());
        static_cast<void>(twin.advanceInterval());
        if (!bmSameBits(live.anchorPosition(), twin.anchorPosition())) {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}

// FR-022, FR-013 (plan S1 C-7: targets below kDenormalThreshold = 1e-15f are +0; curve() flushes)
TEST_CASE("BassMorph_SubThresholdLanding", "[systems][profundum][bass-morph][fr-022][fr-013]") {
    constexpr double fs = 48000.0;
    constexpr double ramp = BassMorphEngine::kDefaultPositionRampMs;
    const auto [a, b] = BassMorphTest::showcasePair();

    for (const BmMode mode : bmModes) {
        INFO("mode " << bmModeName(mode));
        BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, mode, a, b, 0.5f);

        e.setPosition(1e-20f);
        CHECK(std::bit_cast<std::uint32_t>(e.targetPosition()) == 0x00000000u);
        e.setPosition(std::nextafter(1e-15f, 0.0f));
        CHECK(std::bit_cast<std::uint32_t>(e.targetPosition()) == 0x00000000u);
        e.setPosition(1e-15f);
        CHECK(bmSameBits(e.targetPosition(), 1e-15f));

        static_cast<void>(bmLand(e, ramp, fs));
        CHECK(bmSameBits(e.position(), e.targetPosition()));
        CHECK(bmSameBits(e.anchorPosition(), BassMorphEngine::curve(mode, e.position())));
        const BmResolved landed = e.resolved();
        int drift = 0;
        for (int i = 0; i < 8; ++i) {
            static_cast<void>(e.advanceInterval());
            if (!bmSameBits(e.position(), 1e-15f) || !BassMorphTest::bitEqual(e.resolved(), landed)) {
                ++drift;
            }
        }
        CHECK(drift == 0);
    }

    // Punch at 5e-6: p^3 = 1.25e-16 < 1e-15 -> the curve value and the landed p_a are +0.
    {
        BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Punch, a, b, 0.5f);
        e.setPosition(5e-6f);
        static_cast<void>(bmLand(e, ramp, fs));
        CHECK(std::bit_cast<std::uint32_t>(e.anchorPosition()) == 0x00000000u);
        CHECK(bmSameBits(e.anchorPosition(), BassMorphEngine::curve(BmMode::Punch, e.position())));
        int drift = 0;
        for (int i = 0; i < 8; ++i) {
            static_cast<void>(e.advanceInterval());
            if (std::bit_cast<std::uint32_t>(e.anchorPosition()) != 0x00000000u || !bmSameBits(e.position(), 5e-6f)) {
                ++drift;
            }
        }
        CHECK(drift == 0);

        // reset() at 5e-6 publishes p_a = +0, and the next interval keeps resolved() bitwise.
        e.reset();
        CHECK(std::bit_cast<std::uint32_t>(e.anchorPosition()) == 0x00000000u);
        CHECK(std::bit_cast<std::uint32_t>(e.resolved().anchorPosition) == 0x00000000u);
        const BmResolved afterReset = e.resolved();
        static_cast<void>(e.advanceInterval());
        CHECK(BassMorphTest::bitEqual(e.resolved(), afterReset));
    }

    // FR-051 curve endpoints and the Punch midpoint (plan S4.4).
    for (const BmMode mode : {BmMode::Blend, BmMode::Punch}) {
        CHECK(bmSameBits(BassMorphEngine::curve(mode, 0.0f), 0.0f));
        CHECK(bmSameBits(BassMorphEngine::curve(mode, 1.0f), 1.0f));
    }
    CHECK(bmSameBits(BassMorphEngine::curve(BmMode::Lock, 0.0f), 0.0f));
    int lockNonZero = 0;
    for (int i = 0; i <= 1000; ++i) {
        const float p = static_cast<float>(i) / 1000.0f;
        if (!bmSameBits(BassMorphEngine::curve(BmMode::Lock, p), 0.0f)) {
            ++lockNonZero;
        }
    }
    CHECK(lockNonZero == 0);
    CHECK(bmSameBits(BassMorphEngine::curve(BmMode::Lock, 1.0f), 0.0f));
    CHECK(BassMorphEngine::curve(BmMode::Punch, 0.5f) == 0.125f);
}

// SC-003 (a) and (c); T004 appends SECTION (b). Delta p_max = 32 000 / (rampMs * fs), FR-020.
TEST_CASE("BassMorph_BoundedStep", "[systems][profundum][bass-morph][sc-003]") {
    constexpr std::array<double, 4> rates{44100.0, 48000.0, 96000.0, 192000.0};
    constexpr std::array<float, 3> ramps{BassMorphEngine::kMinPositionRampMs, BassMorphEngine::kDefaultPositionRampMs,
                                         BassMorphEngine::kMaxPositionRampMs};
    const auto [a, b] = BassMorphTest::showcasePair();

    SECTION("(a)") {
        long totalViolations = 0;
        for (const double fs : rates) {
            for (const float ramp : ramps) {
                const double dMax = bmDeltaMax(static_cast<double>(ramp), fs);
                for (const BmMode mode : bmModes) {
                    BmStepStats st;

                    // Jumps 0 -> 1, 1 -> 0, 0.3 -> 0.7 from a landed start.
                    constexpr std::array<std::pair<float, float>, 3> jumps{
                        {{0.0f, 1.0f}, {1.0f, 0.0f}, {0.3f, 0.7f}}};
                    for (const auto& [from, to] : jumps) {
                        BassMorphEngine e = bmMakeEngine(fs, ramp, mode, a, b, from);
                        e.setPosition(to);
                        const long cap = bmLandCap(static_cast<double>(ramp), fs);
                        long n = 0;
                        while (!bmIsLanded(e) && n < cap) {
                            bmStepChecked(e, st, dMax);
                            ++n;
                        }
                        CHECK(bmIsLanded(e));
                    }

                    // 30 s of seeded random automation (seed 0x8003): a new target every 25 - 400 ms, uniform on
                    // [0, 1] with 10 % exact 0 / 1; an Anchor-mode change every 0.5 s.
                    {
                        std::mt19937 rng(0x8003u);
                        BassMorphEngine e = bmMakeEngine(fs, ramp, mode, a, b, 0.0f);
                        const auto total = static_cast<long>(std::ceil(30.0 * fs / 32.0));
                        const auto modePeriod = static_cast<long>(std::lround(0.5 * fs / 32.0));
                        long untilNextTarget = 0;
                        auto modeIndex = static_cast<std::size_t>(mode);
                        for (long i = 0; i < total; ++i) {
                            if (untilNextTarget <= 0) {
                                float target = 0.0f;
                                if (bmUnit(rng) < 0.1) {
                                    target = (static_cast<std::uint32_t>(rng()) & 1u) != 0u ? 1.0f : 0.0f;
                                } else {
                                    target = static_cast<float>(bmUnit(rng));
                                }
                                e.setPosition(target);
                                const double ms = 25.0 + 375.0 * bmUnit(rng);
                                untilNextTarget = std::max(1L, static_cast<long>(std::lround(ms * fs / 32000.0)));
                            }
                            if (i > 0 && i % modePeriod == 0) {
                                modeIndex = (modeIndex + 1) % bmModes.size();
                                e.setAnchorMorphMode(bmModes[modeIndex]);
                            }
                            bmStepChecked(e, st, dMax);
                            --untilNextTarget;
                        }
                    }

                    std::cout << std::setprecision(6) << "BassMorph_BoundedStep (a) fs " << fs << " ramp " << ramp
                              << " ms mode " << bmModeName(mode) << ": intervals " << st.intervals
                              << " worst |delta|/dpmax " << st.worstRatio << " (worst - 1)/FLT_EPSILON "
                              << (st.worstRatio - 1.0) / bmFlEps << " at p " << st.worstP << " violations "
                              << st.violations << "\n";
                    totalViolations += st.violations;
                }
            }
        }
        CHECK(totalViolations == 0);
    }

    SECTION("(c)") {
        long outOfBand = 0;
        for (const double fs : rates) {
            for (const float ramp : ramps) {
                const long expected = bmTraversal(static_cast<double>(ramp), fs);
                for (const BmMode mode : bmModes) {
                    BassMorphEngine e = bmMakeEngine(fs, ramp, mode, a, b, 0.0f);
                    e.setPosition(1.0f);
                    const long cap = bmLandCap(static_cast<double>(ramp), fs);
                    long n = 0;
                    while (!bmSameBits(e.position(), 1.0f) && n < cap) {
                        static_cast<void>(e.advanceInterval());
                        ++n;
                    }
                    std::cout << "BassMorph_BoundedStep (c) fs " << fs << " ramp " << ramp << " ms mode "
                              << bmModeName(mode) << ": traversal " << n << " intervals, expected " << expected
                              << " +/- 1\n";
                    if (std::labs(n - expected) > 1) {
                        ++outOfBand;
                    }
                }
            }
        }
        CHECK(outOfBand == 0);
    }

    // T004: SC-003(b), plan S6. The same runs as (a) (bmSc003Runs reproduces T003's loops and seed); per continuous
    // leaf and interval |dx| <= bound(|dq|) * (1 + 4 FLT_EPSILON) + k ulp(max(|x_A|, |x_B|)), k = 2 linear /
    // equal-power, 8 log; the Anchor block at p_a. |dq| is the interval's own step, which (a) bounds by Delta p_max.
    SECTION("(b)") {
        // Named copies: the lambda below must not capture the structured bindings (portable to older Clang).
        const MorphState endA = a;
        const MorphState endB = b;
        long totalViolations = 0;
        long totalChecks = 0;
        for (const double fs : rates) {
            for (const float ramp : ramps) {
                for (const BmMode mode : bmModes) {
                    BmLawStepStats st;
                    bmSc003Runs(fs, ramp, mode, endA, endB, [&](const BassMorphEngine& e, const BmResolved& before) {
                        bmRecordLawStep(st, before, e.resolved(), endA, endB);
                    });
                    std::cout << std::setprecision(6) << "BassMorph_BoundedStep (b) fs " << fs << " ramp " << ramp
                              << " ms mode " << bmModeName(mode) << ": intervals " << st.intervals << " leaf checks "
                              << st.checks << " worst |dx|/allowed " << st.worstRatio << " (" << st.worstLeaf
                              << ") violations " << st.violations << "\n";
                    totalViolations += st.violations;
                    totalChecks += st.checks;
                }
            }
        }
        CHECK(totalChecks > 0);
        CHECK(totalViolations == 0);
    }
}

// SC-022 (FR-021)
TEST_CASE("BassMorph_RampTimeChange", "[systems][profundum][bass-morph][sc-022]") {
    constexpr double fs = 48000.0;
    const auto [a, b] = BassMorphTest::showcasePair();

    for (const float newRamp : {5.0f, 40.0f, 2000.0f}) {
        INFO("new ramp " << newRamp << " ms");
        BassMorphEngine live = bmMakeEngine(fs, 40.0f, BmMode::Blend, a, b, 0.0f);
        BassMorphEngine twin = live;
        live.setPosition(1.0f);
        twin.setPosition(1.0f);
        for (int i = 0; i < 10; ++i) {
            static_cast<void>(live.advanceInterval());
            static_cast<void>(twin.advanceInterval());
        }
        const float before = live.position();
        live.setPositionRampMs(newRamp);
        CHECK(live.positionRampMs() == newRamp);
        CHECK(bmSameBits(live.position(), before));
        CHECK(bmSameBits(live.position(), twin.position()));

        // From the next interval |dp| == the new Delta p_max (within the SC-003(a) tolerance) until p lands.
        const double dMax = bmDeltaMax(static_cast<double>(newRamp), fs);
        const long cap = bmLandCap(static_cast<double>(newRamp), fs);
        long n = 0;
        long offStep = 0;
        double worst = 0.0;
        while (!bmSameBits(live.position(), live.targetPosition()) && n < cap) {
            const float p0 = live.position();
            static_cast<void>(live.advanceInterval());
            ++n;
            if (bmSameBits(live.position(), live.targetPosition())) {
                break;   // the landing step is the remainder, not a full step
            }
            const double delta = std::fabs(static_cast<double>(live.position()) - static_cast<double>(p0));
            const double dev = std::fabs(delta / dMax - 1.0);
            worst = std::max(worst, dev);
            // B3-1: |delta - dMax| <= 4 eps dMax + 0.5 ulp(max(|p_old|, |p_new|)).
            if (std::fabs(delta - dMax) > 4.0 * bmFlEps * dMax + bmHalfUlp(p0, live.position())) {
                ++offStep;
            }
        }
        std::cout << std::setprecision(6) << "BassMorph_RampTimeChange: new ramp " << newRamp
                  << " ms, worst | |dp|/dpmax - 1 | / FLT_EPSILON " << worst / bmFlEps << ", off-step intervals "
                  << offStep << "\n";
        CHECK(bmSameBits(live.position(), 1.0f));
        CHECK(offStep == 0);
    }

    // Fresh traversals at the range ends and the default: SC-003(c) counts.
    for (const float ramp : {5.0f, 40.0f, 2000.0f}) {
        BassMorphEngine e = bmMakeEngine(fs, ramp, BmMode::Blend, a, b, 0.0f);
        e.setPosition(1.0f);
        const long expected = bmTraversal(static_cast<double>(ramp), fs);
        const long cap = bmLandCap(static_cast<double>(ramp), fs);
        long n = 0;
        while (!bmSameBits(e.position(), 1.0f) && n < cap) {
            static_cast<void>(e.advanceInterval());
            ++n;
        }
        std::cout << "BassMorph_RampTimeChange: fresh traversal at " << ramp << " ms: " << n << " intervals, expected "
                  << expected << " +/- 1\n";
        CHECK(std::labs(n - expected) <= 1);
    }

    // Clamps (the NaN row is T006's: this TU is fast-math).
    BassMorphEngine e;
    e.prepare(fs);
    e.setPositionRampMs(4.9f);
    CHECK(e.positionRampMs() == 5.0f);
    e.setPositionRampMs(2001.0f);
    CHECK(e.positionRampMs() == 2000.0f);

    // Before prepare the setter stores.
    BassMorphEngine unprepared;
    unprepared.setPositionRampMs(123.0f);
    CHECK(unprepared.positionRampMs() == 123.0f);
}

// SC-014 (FR-011, FR-020, FR-021)
TEST_CASE("BassMorph_SampleRates", "[systems][profundum][bass-morph][sc-014]") {
    constexpr std::array<double, 5> rates{22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    constexpr std::array<float, 3> ramps{BassMorphEngine::kMinPositionRampMs, BassMorphEngine::kDefaultPositionRampMs,
                                         BassMorphEngine::kMaxPositionRampMs};

    std::mt19937 rng(0x8014u);
    std::vector<std::pair<MorphState, MorphState>> pairs;
    pairs.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        pairs.push_back(BassMorphTest::randomValidPair(rng));
    }

    auto traversal = [](double prepareRate, float ramp, double capRate) {
        const auto [a, b] = BassMorphTest::showcasePair();
        BassMorphEngine e = bmMakeEngine(prepareRate, ramp, BmMode::Blend, a, b, 0.0f);
        e.setPosition(1.0f);
        const long cap = bmLandCap(static_cast<double>(ramp), capRate);
        long n = 0;
        while (!bmSameBits(e.position(), 1.0f) && n < cap) {
            static_cast<void>(e.advanceInterval());
            ++n;
        }
        return n;
    };

    long mismatches = 0;
    long outOfBand = 0;
    for (const double fs : rates) {
        for (const float ramp : ramps) {
            for (const BmMode mode : bmModes) {
                BassMorphEngine e;
                e.prepare(fs);
                e.setPositionRampMs(ramp);
                e.setAnchorMorphMode(mode);
                for (const auto& [a, b] : pairs) {
                    mismatches += bmIdentityMismatches(e, a, b, static_cast<double>(ramp), fs);
                }
            }
            const long n = traversal(fs, ramp, fs);
            const long expected = bmTraversal(static_cast<double>(ramp), fs);
            std::cout << "BassMorph_SampleRates: fs " << fs << " ramp " << ramp << " ms traversal " << n
                      << " intervals, expected " << expected << " +/- 1\n";
            if (std::labs(n - expected) > 1) {
                ++outOfBand;
            }
        }
    }
    CHECK(mismatches == 0);
    CHECK(outOfBand == 0);

    // Out-of-range rates clamp to [22 050, 192 000].
    for (const float ramp : ramps) {
        CHECK(traversal(8000.0, ramp, 22050.0) == traversal(22050.0, ramp, 22050.0));
        CHECK(traversal(384000.0, ramp, 192000.0) == traversal(192000.0, ramp, 192000.0));
    }
}

// SC-021 (a); T004 appends SECTION (b). (FR-011, plan S1 C-5)
TEST_CASE("BassMorph_Lifecycle", "[systems][profundum][bass-morph][sc-021]") {
    SECTION("(a)") {
        std::mt19937 rng(0x8021u);
        const auto [a, b] = BassMorphTest::randomValidPair(rng);

        BassMorphEngine unprepared;
        REQUIRE(unprepared.setEndpoint(BmEndpoint::A, a));
        REQUIRE(unprepared.setEndpoint(BmEndpoint::B, b));
        unprepared.setPosition(0.7f);
        const BmResolved r1 = unprepared.advanceInterval();

        BassMorphEngine prepared;
        prepared.prepare(48000.0);
        REQUIRE(prepared.setEndpoint(BmEndpoint::A, a));
        REQUIRE(prepared.setEndpoint(BmEndpoint::B, b));
        prepared.setPosition(0.7f);
        prepared.reset();
        const BmResolved r2 = prepared.resolved();

        CHECK(BassMorphTest::bitEqual(r1, r2));
        CHECK(unprepared.position() == 0.7f);
        CHECK(prepared.position() == 0.7f);
        CHECK(r1.position == 0.7f);
    }

    // T004: SC-021(b), FR-034 rule 3 inside the band (plan S4.3: reset() keeps the selection inside [0.48, 0.52]).
    SECTION("(b)") {
        constexpr double fs = 48000.0;
        constexpr double ramp = BassMorphEngine::kDefaultPositionRampMs;
        const auto [a, b] = bmRule3CharacterPair();
        const auto character = MorphState::SelectionGroup::Character;

        // Swept 0 -> 0.5 (landed), then reset(): A kept.
        BassMorphEngine fromBelow = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, 0.0f);
        fromBelow.setPosition(0.5f);
        static_cast<void>(bmLand(fromBelow, ramp, fs));
        fromBelow.reset();
        CHECK(fromBelow.selection(character) == BmEndpoint::A);
        CHECK(fromBelow.resolved().state.characterAlgorithm == a.characterAlgorithm);

        // Swept 1 -> 0.5 (landed), then reset(): B kept.
        BassMorphEngine fromAbove = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, 1.0f);
        fromAbove.setPosition(0.5f);
        static_cast<void>(bmLand(fromAbove, ramp, fs));
        fromAbove.reset();
        CHECK(fromAbove.selection(character) == BmEndpoint::B);
        CHECK(fromAbove.resolved().state.characterAlgorithm == b.characterAlgorithm);

        // reset() at 0.53 -> B and at 0.47 -> A, from either side.
        for (const float start : {0.0f, 1.0f}) {
            INFO("start " << start);
            BassMorphEngine up = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, start);
            up.setPosition(0.53f);
            up.reset();
            CHECK(up.selection(character) == BmEndpoint::B);
            CHECK(up.resolved().state.characterAlgorithm == b.characterAlgorithm);

            BassMorphEngine down = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, start);
            down.setPosition(0.47f);
            down.reset();
            CHECK(down.selection(character) == BmEndpoint::A);
            CHECK(down.resolved().state.characterAlgorithm == a.characterAlgorithm);
        }

        // A fresh engine reset() at 0.5 -> A (FR-011: selections start at A).
        BassMorphEngine fresh;
        fresh.prepare(fs);
        REQUIRE(fresh.setEndpoint(BmEndpoint::A, a));
        REQUIRE(fresh.setEndpoint(BmEndpoint::B, b));
        fresh.setPosition(0.5f);
        fresh.reset();
        CHECK(fresh.selection(character) == BmEndpoint::A);
        CHECK(fresh.resolved().state.characterAlgorithm == a.characterAlgorithm);
    }
}

// SC-001 (FR-035, FR-052, FR-022)
TEST_CASE("BassMorph_EndpointIdentity", "[systems][profundum][bass-morph][sc-001]") {
    constexpr double fs = 48000.0;
    constexpr double ramp = BassMorphEngine::kDefaultPositionRampMs;

    std::vector<std::pair<MorphState, MorphState>> pairs;
    pairs.reserve(10000 + 1 + 42);
    std::mt19937 rng(0x8001u);
    for (int i = 0; i < 10000; ++i) {
        pairs.push_back(BassMorphTest::randomValidPair(rng));
    }
    pairs.push_back(BassMorphTest::showcasePair());
    const auto recipes = bmNamedRecipes();
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        for (std::size_t j = 0; j < recipes.size(); ++j) {
            if (i == j) {
                continue;
            }
            MorphState a{};
            a.recipe = recipes[i];
            MorphState b{};
            b.recipe = recipes[j];
            pairs.emplace_back(a, b);
        }
    }
    REQUIRE(pairs.size() == 10043);

    long mismatches = 0;
    for (const BmMode mode : bmModes) {
        BassMorphEngine e;
        e.prepare(fs);
        e.setAnchorMorphMode(mode);
        long modeMismatches = 0;
        for (const auto& [a, b] : pairs) {
            modeMismatches += bmIdentityMismatches(e, a, b, ramp, fs);
        }
        std::cout << "BassMorph_EndpointIdentity: mode " << bmModeName(mode) << " pairs " << pairs.size()
                  << " mismatches " << modeMismatches << "\n";
        mismatches += modeMismatches;
    }
    REQUIRE(mismatches == 0);
}

// SC-002 (FR-035)
TEST_CASE("BassMorph_EqualEndpointsConstant", "[systems][profundum][bass-morph][sc-002]") {
    constexpr double fs = 48000.0;
    constexpr double ramp = BassMorphEngine::kDefaultPositionRampMs;
    std::mt19937 rng(0x8002u);

    long mismatches = 0;
    for (int k = 0; k < 1000; ++k) {
        const MorphState s = BassMorphTest::randomValidState(rng);
        for (const BmMode mode : bmModes) {
            BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, mode, s, s, 0.0f);

            for (int i = 0; i <= 100; ++i) {
                e.setPosition(static_cast<float>(i) / 100.0f);
                e.reset();
                if (!BassMorphTest::bitEqual(e.resolved().state, s)) {
                    ++mismatches;
                }
            }

            // Every interval of a 0 -> 1 -> 0 ramp.
            e.setPosition(0.0f);
            e.reset();
            for (const float target : {1.0f, 0.0f}) {
                e.setPosition(target);
                const long cap = bmLandCap(ramp, fs);
                long n = 0;
                while (!bmIsLanded(e) && n < cap) {
                    if (!BassMorphTest::bitEqual(e.advanceInterval().state, s)) {
                        ++mismatches;
                    }
                    ++n;
                }
                CHECK(bmIsLanded(e));
            }
        }
    }
    REQUIRE(mismatches == 0);
}

// ==============================================================================
// T004 cases: laws, selection machine, resolution cache (FR-030 - FR-037, FR-040, FR-053 - FR-055, FR-061, FR-082;
// plan S4.5 - S4.7, S6, C-2, C-4, C-6). BassMorph_BoundedStep (b) and BassMorph_Lifecycle (b) are SECTIONs above.
// ==============================================================================

// SC-019(b) (FR-030 - FR-033; plan S1 C-4: every law in double, rounded once)
TEST_CASE("BassMorph_LawTable", "[systems][profundum][bass-morph][sc-019]") {
    constexpr double fs = 48000.0;
    constexpr double ramp = BassMorphEngine::kDefaultPositionRampMs;
    std::mt19937 rng(0x8019u);
    const MorphState a{};

    long failures = 0;
    for (std::size_t idx = 0; idx < Krate::DSP::kMorphLeafFieldCount; ++idx) {
        const MorphState::FieldInfo& row = io::kFieldTable[idx];
        INFO("leaf " << idx << " " << row.name);
        MorphState b = a;
        bmDrawDifferentLeaf(b, a, idx, rng);
        REQUIRE(Krate::DSP::isValidMorphState(b));
        REQUIRE(BassMorphTest::differingLeaves(a, b) == std::vector<std::size_t>{idx});

        if (row.law != BmLaw::Stepped) {
            // Blend, reset() at p = 0.25 (p_a = 0.25 for the Anchor block).
            BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, 0.25f);
            const std::vector<std::size_t> diff = BassMorphTest::differingLeaves(e.resolved().state, a);
            const float xa = bmLeafValue(a, idx);
            const float xb = bmLeafValue(b, idx);
            const float got = bmLeafValue(e.resolved().state, idx);
            const double expected = bmLawDouble(row.law, xa, xb, row.logOffset, 0.25);
            const double tol =
                bmUlpK(row.law) * static_cast<double>(BassMorphTest::ulpOf(std::max(std::fabs(xa), std::fabs(xb))));
            const double err = std::fabs(static_cast<double>(got) - expected);
            const bool ok = diff == std::vector<std::size_t>{idx} && err <= tol;
            std::cout << std::setprecision(9) << "BassMorph_LawTable leaf " << idx << " " << row.name << ": x_A " << xa
                      << " x_B " << xb << " resolved(0.25) " << got << " closed form " << expected << " |err|/ulp "
                      << err / static_cast<double>(BassMorphTest::ulpOf(std::max(std::fabs(xa), std::fabs(xb))))
                      << " differing leaves " << diff.size() << (ok ? "" : "  FAIL") << "\n";
            if (!ok) {
                ++failures;
            }
        } else {
            // Rule 3 (every governing amount is the default on both sides): from p = 0, theta_up - 0.005 -> A,
            // theta_up -> B; no other leaf differs.
            BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, 0.0f);
            e.setPosition(BassMorphEngine::kThetaUp - 0.005f);
            static_cast<void>(bmLand(e, ramp, fs));
            const bool belowOk = BassMorphTest::differingLeaves(e.resolved().state, a).empty();
            e.setPosition(BassMorphEngine::kThetaUp);
            static_cast<void>(bmLand(e, ramp, fs));
            const bool atOk = BassMorphTest::differingLeaves(e.resolved().state, a) == std::vector<std::size_t>{idx}
                              && BassMorphTest::differingLeaves(e.resolved().state, b).empty();
            std::cout << "BassMorph_LawTable leaf " << idx << " " << row.name << " (Stepped, " << bmGroupName(row.group)
                      << "): theta_up - 0.005 -> " << (belowOk ? "A" : "not A") << ", theta_up -> "
                      << (atOk ? "B" : "not B") << "\n";
            if (!belowOk || !atOk) {
                ++failures;
            }
        }
    }
    CHECK(failures == 0);
}

// SC-019(b) governing-amount rows (FR-034 rules 1 - 3, FR-055; plan S1 C-2: one selection per group)
TEST_CASE("BassMorph_LawTable_GoverningAmount", "[systems][profundum][bass-morph][sc-019][fr-034]") {
    constexpr double fs = 48000.0;
    constexpr double ramp = BassMorphEngine::kDefaultPositionRampMs;
    constexpr float thetaIn = BassMorphEngine::kThetaUp - 0.005f;

    // Ascending then descending landed visits, with the rule-3 (hysteresis) selection expected at each.
    struct Visit {
        float p;
        BmEndpoint rule3;
    };
    constexpr std::array<Visit, 11> visits{{{.p = 0.0f, .rule3 = BmEndpoint::A},
                                            {.p = 0.01f, .rule3 = BmEndpoint::A},
                                            {.p = 0.25f, .rule3 = BmEndpoint::A},
                                            {.p = thetaIn, .rule3 = BmEndpoint::A},
                                            {.p = 0.99f, .rule3 = BmEndpoint::B},
                                            {.p = 1.0f, .rule3 = BmEndpoint::B},
                                            {.p = 0.99f, .rule3 = BmEndpoint::B},
                                            {.p = thetaIn, .rule3 = BmEndpoint::B},
                                            {.p = 0.25f, .rule3 = BmEndpoint::A},
                                            {.p = 0.01f, .rule3 = BmEndpoint::A},
                                            {.p = 0.0f, .rule3 = BmEndpoint::A}}};

    struct Case {
        const char* label;
        float govA;
        float govB;
        bool forced;        // rule 2 applies
        BmEndpoint side;    // the non-silent side when forced
    };

    long failures = 0;
    for (const BmGroup g : bmGroups) {
        std::vector<Case> cases;
        if (g == BmGroup::Motion) {
            // The non-zero side -0.5 and +0.5, and a zero side of -0.0f: `!= 0` and +/-0 (plan S7.4).
            for (const auto& [v, z, tag] : {std::tuple<float, float, const char*>{0.5f, 0.0f, "v +0.5, z +0"},
                                            std::tuple<float, float, const char*>{-0.5f, 0.0f, "v -0.5, z +0"},
                                            std::tuple<float, float, const char*>{0.5f, -0.0f, "v +0.5, z -0"}}) {
                static_cast<void>(tag);
                cases.push_back({.label = "zero on A", .govA = z, .govB = v, .forced = true, .side = BmEndpoint::B});
                cases.push_back({.label = "zero on B", .govA = v, .govB = z, .forced = true, .side = BmEndpoint::A});
                cases.push_back({.label = "both non-zero", .govA = v, .govB = -v,
                                 .forced = false, .side = BmEndpoint::A});
                cases.push_back({.label = "both zero", .govA = z, .govB = z, .forced = false, .side = BmEndpoint::A});
            }
        } else {
            cases.push_back({.label = "zero on A", .govA = 0.0f, .govB = 0.8f,
                             .forced = true, .side = BmEndpoint::B});
            cases.push_back({.label = "zero on B", .govA = 0.8f, .govB = 0.0f,
                             .forced = true, .side = BmEndpoint::A});
            cases.push_back({.label = "both non-zero", .govA = 0.8f, .govB = 0.8f,
                             .forced = false, .side = BmEndpoint::A});
            cases.push_back({.label = "both zero", .govA = 0.0f, .govB = 0.0f,
                             .forced = false, .side = BmEndpoint::A});
        }

        for (const Case& c : cases) {
            MorphState a{};
            MorphState b{};
            bmConfigureGroupDiscretes(g, a, b);
            bmGoverning(a, g) = c.govA;
            bmGoverning(b, g) = c.govB;
            REQUIRE(Krate::DSP::isValidMorphState(a));
            REQUIRE(Krate::DSP::isValidMorphState(b));

            BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, 0.0f);
            long caseFailures = 0;
            for (const Visit& v : visits) {
                e.setPosition(v.p);
                static_cast<void>(bmLand(e, ramp, fs));
                const float q = (g == BmGroup::Anchor) ? e.anchorPosition() : e.position();
                BmEndpoint expected = v.rule3;
                if (c.forced) {
                    if (q <= 0.0f) {
                        expected = BmEndpoint::A;
                    } else if (q >= 1.0f) {
                        expected = BmEndpoint::B;
                    } else {
                        expected = c.side;
                    }
                }
                const MorphState& state = e.resolved().state;
                bool ok = e.selection(g) == expected
                          && bmGroupLeavesEqual(state, expected == BmEndpoint::A ? a : b, g);
                // The other four groups: rule 3 (their governing amounts are the defaults on both sides) and their
                // discrete leaves (equal on both sides) at A's values.
                for (const BmGroup other : bmGroups) {
                    if (other == g) {
                        continue;
                    }
                    ok = ok && e.selection(other) == v.rule3 && bmGroupLeavesEqual(state, a, other);
                }
                if (!ok) {
                    ++caseFailures;
                    std::cout << "BassMorph_LawTable_GoverningAmount FAIL: group " << bmGroupName(g) << " case "
                              << c.label << " (govA " << c.govA << ", govB " << c.govB << ") p " << v.p
                              << " selection " << static_cast<int>(e.selection(g)) << " expected "
                              << static_cast<int>(expected) << "\n";
                }
            }
            std::cout << "BassMorph_LawTable_GoverningAmount: group " << bmGroupName(g) << " case " << c.label
                      << " (govA " << c.govA << ", govB " << c.govB << "): " << caseFailures << " failing visits\n";
            failures += caseFailures;
        }
    }
    CHECK(failures == 0);
}

// FR-037 (zero tolerance; plan S4.6: gated for every law, never toleranced)
TEST_CASE("BassMorph_MonotoneLaws", "[systems][profundum][bass-morph][fr-037]") {
    constexpr double fs = 48000.0;
    constexpr float slowRamp = BassMorphEngine::kMaxPositionRampMs;
    std::mt19937 rng(0x8037u);

    BmMonotoneStats st;
    for (int k = 0; k < 100; ++k) {
        const auto [pa, pb] = BassMorphTest::randomValidPair(rng);
        for (const BmMode mode : bmModes) {
            // p = j / 10 000, ascending, setPosition + reset() at each.
            BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, mode, pa, pb, 0.0f);
            BmResolved prev = e.resolved();
            for (int j = 1; j <= 10000; ++j) {
                e.setPosition(static_cast<float>(j) / 10000.0f);
                e.reset();
                bmCheckMonotone(st, prev, e.resolved(), pa, pb);
                prev = e.resolved();
            }

            // Every interval of a 0 -> 1 traversal at kMaxPositionRampMs (3 000 intervals at 48 kHz).
            BassMorphEngine t = bmMakeEngine(fs, slowRamp, mode, pa, pb, 0.0f);
            t.setPosition(1.0f);
            prev = t.resolved();
            const long cap = bmLandCap(static_cast<double>(slowRamp), fs);
            long n = 0;
            while (!bmIsLanded(t) && n < cap) {
                const BmResolved& cur = t.advanceInterval();
                bmCheckMonotone(st, prev, cur, pa, pb);
                prev = cur;
                ++n;
            }
            CHECK(bmIsLanded(t));
        }
    }
    std::cout << "BassMorph_MonotoneLaws: checks " << st.checks << " reversals " << st.reversals
              << " position reversals " << st.positionReversals << " interior values " << st.interiorValues << "\n";
    CHECK(st.positionReversals == 0);
    CHECK(st.interiorValues > 0);   // non-vacuity: the laws move between the endpoints
    CHECK(st.reversals == 0);
}

// SC-020 (FR-036)
TEST_CASE("BassMorph_ValidityClosure", "[systems][profundum][bass-morph][sc-020]") {
    std::mt19937 rng(0x8020u);
    BassMorphEngine e;
    e.prepare(48000.0);

    long invalid = 0;
    long outOfBox = 0;
    long interior = 0;
    for (int k = 0; k < 100000; ++k) {
        const auto [a, b] = BassMorphTest::randomValidPair(rng);
        float p = 0.0f;
        const double u = bmUnit(rng);
        if (u < 0.05) {
            p = (static_cast<std::uint32_t>(rng()) & 1u) != 0u ? 1.0f : 0.0f;
        } else if (u < 0.10) {
            const float theta = (static_cast<std::uint32_t>(rng()) & 1u) != 0u ? BassMorphEngine::kThetaUp
                                                                                : BassMorphEngine::kThetaDown;
            p = theta + static_cast<float>((2.0 * bmUnit(rng) - 1.0) * 1e-6);
        } else {
            p = static_cast<float>(bmUnit(rng));
        }
        const BmMode mode = bmModes[static_cast<std::uint32_t>(rng()) % 3u];

        REQUIRE(e.setEndpoint(BmEndpoint::A, a));
        REQUIRE(e.setEndpoint(BmEndpoint::B, b));
        e.setAnchorMorphMode(mode);
        e.setPosition(p);
        e.reset();
        const MorphState& r = e.resolved().state;
        if (!Krate::DSP::isValidMorphState(r)) {
            ++invalid;
        }
        io::forEachMorphLeaf(
            [&](auto leaf, [[maybe_unused]] const auto& x, [[maybe_unused]] const auto& xa,
                [[maybe_unused]] const auto& xb) {
                constexpr MorphState::FieldInfo row = io::kFieldTable[decltype(leaf)::value];
                if constexpr (row.law != BmLaw::Stepped) {
                    if (x < std::min(xa, xb) || x > std::max(xa, xb)) {
                        ++outOfBox;
                    }
                    if (x > std::min(xa, xb) && x < std::max(xa, xb)) {
                        ++interior;
                    }
                }
            },
            r, a, b);
    }
    std::cout << "BassMorph_ValidityClosure: 100000 pairs, invalid " << invalid << " out-of-box leaves " << outOfBox
              << " interior leaves " << interior << "\n";
    CHECK(interior > 0);   // non-vacuity
    CHECK(invalid == 0);
    CHECK(outOfBox == 0);
}

// SC-004(c) (FR-034 rule 3, D-1)
TEST_CASE("BassMorph_Hysteresis", "[systems][profundum][bass-morph][sc-004]") {
    constexpr double fs = 48000.0;
    constexpr double ramp = BassMorphEngine::kDefaultPositionRampMs;
    constexpr double twoPi = 6.283185307179586;
    const auto pair = bmRule3CharacterPair();   // named, not a structured binding: the lambda captures it
    const MorphState& a = pair.first;
    const MorphState& b = pair.second;
    const auto character = MorphState::SelectionGroup::Character;

    auto run = [&](float start, float land, float theta, BmEndpoint startSel, BmEndpoint endSel, const char* tag) {
        INFO(tag);
        BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, start);
        e.setPosition(land);
        static_cast<void>(bmLand(e, ramp, fs));
        REQUIRE(e.selection(character) == startSel);

        std::vector<float> ps;
        ps.reserve(3000);
        int switches = 0;
        BmEndpoint prev = e.selection(character);
        for (int i = 0; i < 3000; ++i) {
            const double t = static_cast<double>(i) * 32.0 / fs;
            e.setPosition(theta + static_cast<float>(0.01 * std::sin(twoPi * 5.0 * t)));
            static_cast<void>(e.advanceInterval());
            ps.push_back(e.position());
            const BmEndpoint sel = e.selection(character);
            if (sel != prev) {
                ++switches;
            }
            prev = sel;
        }
        // Positive control: a single threshold at theta on the recorded p sequence.
        int singleSwitches = 0;
        bool singleB = startSel == BmEndpoint::B;
        for (const float p : ps) {
            const bool nowB = p >= theta;
            if (nowB != singleB) {
                ++singleSwitches;
            }
            singleB = nowB;
        }
        std::cout << "BassMorph_Hysteresis " << tag << ": hysteresis switches " << switches
                  << ", single-threshold switches " << singleSwitches << "\n";
        CHECK(switches <= 1);
        CHECK(e.selection(character) == endSel);   // the jitter crosses theta, so the selection moved once
        CHECK(singleSwitches >= 10);
    };

    // From 0, landed at 0.51 (A), jitter around theta_up.
    run(0.0f, 0.51f, BassMorphEngine::kThetaUp, BmEndpoint::A, BmEndpoint::B, "theta_up");
    // Mirror: from 1, landed at 0.49 (B kept inside the band), jitter around theta_down.
    run(1.0f, 0.49f, BassMorphEngine::kThetaDown, BmEndpoint::B, BmEndpoint::A, "theta_down");
}

// SC-004(d) engine part (FR-034 rule 2, FR-055)
TEST_CASE("BassMorph_GoverningAmountRule", "[systems][profundum][bass-morph][sc-004]") {
    constexpr double fs = 48000.0;
    constexpr double ramp = BassMorphEngine::kDefaultPositionRampMs;
    using Alg = Krate::DSP::BassCharacterStage::Algorithm;
    using Img = BassStereoField::ImageMode;
    using AW = Krate::DSP::AnchorOscillator::Waveform;
    using AM = Krate::DSP::AnchorOscillator::Mode;

    enum class Kind : std::uint8_t { ZeroOnA, ZeroOnB, BothNonZero };
    struct Row {
        std::string name;
        MorphState a;
        MorphState b;
        bool anchorGroup;
        int (*read)(const MorphState&);
        Kind kind;
    };

    auto readAlg = [](const MorphState& s) { return static_cast<int>(s.characterAlgorithm); };
    auto readImg = [](const MorphState& s) { return static_cast<int>(s.field.mode); };
    auto readWave = [](const MorphState& s) { return static_cast<int>(s.anchor.waveform); };
    auto readMode = [](const MorphState& s) { return static_cast<int>(s.anchor.mode); };

    // Each kind: the silent side's discrete value s0 with amount 0, the other's s1 with amount v1.
    auto characterState = [](Alg alg, float amount) {
        MorphState s{};
        s.characterAlgorithm = alg;
        s.characterAmount = amount;
        return s;
    };
    auto imageState = [](Img mode, float spread) {
        MorphState s{};
        s.field.mode = mode;
        s.field.spread = spread;
        return s;
    };
    auto waveState = [](AW w, float level) {
        MorphState s{};
        s.anchor.waveform = w;
        s.anchor.level = level;
        return s;
    };
    auto modeState = [](AM m, float level) {
        MorphState s{};
        s.anchor.mode = m;
        s.anchor.level = level;
        return s;
    };

    std::vector<Row> rows;
    rows.push_back({.name = "Character zero on A",
                    .a = characterState(Alg::Tube, 0.0f),
                    .b = characterState(Alg::Fold, 0.8f),
                    .anchorGroup = false,
                    .read = +readAlg,
                    .kind = Kind::ZeroOnA});
    rows.push_back({.name = "Character zero on B",
                    .a = characterState(Alg::Fold, 0.8f),
                    .b = characterState(Alg::Tube, 0.0f),
                    .anchorGroup = false,
                    .read = +readAlg,
                    .kind = Kind::ZeroOnB});
    rows.push_back({.name = "Character both non-zero",
                    .a = characterState(Alg::Tube, 0.8f),
                    .b = characterState(Alg::Fold, 0.7f),
                    .anchorGroup = false,
                    .read = +readAlg,
                    .kind = Kind::BothNonZero});
    rows.push_back({.name = "ImageMode zero on A",
                    .a = imageState(Img::Solid, 0.0f),
                    .b = imageState(Img::Wide, 1.0f),
                    .anchorGroup = false,
                    .read = +readImg,
                    .kind = Kind::ZeroOnA});
    rows.push_back({.name = "ImageMode zero on B",
                    .a = imageState(Img::Wide, 1.0f),
                    .b = imageState(Img::Solid, 0.0f),
                    .anchorGroup = false,
                    .read = +readImg,
                    .kind = Kind::ZeroOnB});
    rows.push_back({.name = "ImageMode both non-zero",
                    .a = imageState(Img::Solid, 0.8f),
                    .b = imageState(Img::Wide, 0.7f),
                    .anchorGroup = false,
                    .read = +readImg,
                    .kind = Kind::BothNonZero});
    rows.push_back({.name = "Anchor waveform zero on A",
                    .a = waveState(AW::Sine, 0.0f),
                    .b = waveState(AW::Triangle, 0.7f),
                    .anchorGroup = true,
                    .read = +readWave,
                    .kind = Kind::ZeroOnA});
    rows.push_back({.name = "Anchor waveform zero on B",
                    .a = waveState(AW::Triangle, 0.7f),
                    .b = waveState(AW::Sine, 0.0f),
                    .anchorGroup = true,
                    .read = +readWave,
                    .kind = Kind::ZeroOnB});
    rows.push_back({.name = "Anchor waveform both non-zero",
                    .a = waveState(AW::Sine, 0.8f),
                    .b = waveState(AW::Triangle, 0.7f),
                    .anchorGroup = true,
                    .read = +readWave,
                    .kind = Kind::BothNonZero});
    rows.push_back({.name = "Anchor mode zero on A",
                    .a = modeState(AM::Lock, 0.0f),
                    .b = modeState(AM::Punch, 0.7f),
                    .anchorGroup = true,
                    .read = +readMode,
                    .kind = Kind::ZeroOnA});
    rows.push_back({.name = "Anchor mode zero on B",
                    .a = modeState(AM::Punch, 0.7f),
                    .b = modeState(AM::Lock, 0.0f),
                    .anchorGroup = true,
                    .read = +readMode,
                    .kind = Kind::ZeroOnB});
    rows.push_back({.name = "Anchor mode both non-zero",
                    .a = modeState(AM::Lock, 0.8f),
                    .b = modeState(AM::Punch, 0.7f),
                    .anchorGroup = true,
                    .read = +readMode,
                    .kind = Kind::BothNonZero});

    long failures = 0;
    for (const Row& row : rows) {
        INFO(row.name);
        REQUIRE(Krate::DSP::isValidMorphState(row.a));
        REQUIRE(Krate::DSP::isValidMorphState(row.b));
        const int valueA = row.read(row.a);
        const int valueB = row.read(row.b);
        REQUIRE(valueA != valueB);

        // 0 -> 1 -> 0 by two landed jumps, Blend (p_a == p); one record per interval, record 0 = reset() at 0.
        BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, row.a, row.b, 0.0f);
        std::vector<float> qs;
        std::vector<int> values;
        auto record = [&] {
            qs.push_back(row.anchorGroup ? e.anchorPosition() : e.position());
            values.push_back(row.read(e.resolved().state));
        };
        record();
        std::size_t risingEnd = 0;
        for (const float target : {1.0f, 0.0f}) {
            e.setPosition(target);
            const long cap = bmLandCap(ramp, fs);
            long n = 0;
            while (!bmIsLanded(e) && n < cap) {
                static_cast<void>(e.advanceInterval());
                record();
                ++n;
            }
            CHECK(bmIsLanded(e));
            if (target == 1.0f) {
                risingEnd = qs.size();   // records [1, risingEnd) rise, [risingEnd, size) fall
            }
        }

        long wrongValues = 0;
        for (std::size_t i = 0; i < qs.size(); ++i) {
            const float q = qs[i];
            int expected = values[i];
            // identity at 0 / 1; inside (0, 1) rule 2 selects the non-silent side.
            if (q <= 0.0f || (q < 1.0f && row.kind == Kind::ZeroOnB)) {
                expected = valueA;
            } else if (q >= 1.0f || row.kind == Kind::ZeroOnA) {
                expected = valueB;
            }
            if (values[i] != expected) {
                ++wrongValues;
            }
        }

        // Change indices per direction, against the expected interval.
        auto changesIn = [&](std::size_t begin, std::size_t end, std::vector<std::size_t>& at) {
            for (std::size_t i = begin; i < end; ++i) {
                if (values[i] != values[i - 1]) {
                    at.push_back(i);
                }
            }
        };
        auto firstWhere = [&](std::size_t begin, std::size_t end, auto pred) {
            for (std::size_t i = begin; i < end; ++i) {
                if (pred(qs[i])) {
                    return i;
                }
            }
            return end;
        };
        std::vector<std::size_t> risingChanges;
        std::vector<std::size_t> fallingChanges;
        changesIn(1, risingEnd, risingChanges);
        changesIn(risingEnd, qs.size(), fallingChanges);

        std::size_t expectRise = 0;
        std::size_t expectFall = 0;
        switch (row.kind) {
            case Kind::ZeroOnA:
                expectRise = firstWhere(1, risingEnd, [](float q) { return q > 0.0f; });
                expectFall = firstWhere(risingEnd, qs.size(), [](float q) { return q <= 0.0f; });
                break;
            case Kind::ZeroOnB:
                expectRise = firstWhere(1, risingEnd, [](float q) { return q >= 1.0f; });
                expectFall = firstWhere(risingEnd, qs.size(), [](float q) { return q < 1.0f; });
                break;
            case Kind::BothNonZero:
                expectRise = firstWhere(1, risingEnd, [](float q) { return q >= BassMorphEngine::kThetaUp; });
                expectFall = firstWhere(risingEnd, qs.size(), [](float q) { return q <= BassMorphEngine::kThetaDown; });
                break;
        }
        const bool riseOk = risingChanges.size() == 1 && risingChanges[0] == expectRise;
        const bool fallOk = fallingChanges.size() == 1 && fallingChanges[0] == expectFall;
        std::cout << std::setprecision(6) << "BassMorph_GoverningAmountRule " << row.name << ": rising changes "
                  << risingChanges.size() << " at "
                  << (risingChanges.empty() ? -1L : static_cast<long>(risingChanges[0])) << " (expected "
                  << expectRise << ", q " << (expectRise < qs.size() ? qs[expectRise] : -1.0f) << "), falling changes "
                  << fallingChanges.size() << " at "
                  << (fallingChanges.empty() ? -1L : static_cast<long>(fallingChanges[0])) << " (expected "
                  << expectFall << ", q " << (expectFall < qs.size() ? qs[expectFall] : -1.0f) << "), wrong values "
                  << wrongValues << "\n";
        if (row.kind != Kind::BothNonZero) {
            CHECK(wrongValues == 0);
        }
        CHECK(riseOk);
        CHECK(fallOk);
        if (!riseOk || !fallOk || (row.kind != Kind::BothNonZero && wrongValues != 0)) {
            ++failures;
        }
    }
    CHECK(failures == 0);
}

// SC-005 engine part (FR-050 - FR-054; plan S1 C-12: render-bound clauses are in BassMorph_AnchorCurvesRendered)
TEST_CASE("BassMorph_AnchorCurves", "[systems][profundum][bass-morph][sc-005]") {
    constexpr double fs = 48000.0;

    // (a) - (c): anchorCurvePair, reset() at p = k / 20.
    {
        const auto [a, b] = BassMorphTest::anchorCurvePair();
        std::array<std::array<float, 21>, 3> level{};
        for (std::size_t m = 0; m < bmModes.size(); ++m) {
            BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, bmModes[m], a, b, 0.0f);
            for (int k = 0; k <= 20; ++k) {
                e.setPosition(static_cast<float>(k) / 20.0f);
                e.reset();
                level[m][static_cast<std::size_t>(k)] = e.resolved().state.anchor.level;
            }
        }
        long lockOff = 0;
        long blendOff = 0;
        long punchOff = 0;
        long punchBelowBlend = 0;
        for (int k = 0; k <= 20; ++k) {
            const auto ki = static_cast<std::size_t>(k);
            const double p = static_cast<double>(static_cast<float>(k) / 20.0f);
            const float lock = level[0][ki];
            const float blend = level[1][ki];
            const float punch = level[2][ki];
            std::cout << std::setprecision(6) << "BassMorph_AnchorCurves p " << p << ": Lock " << lock << " Blend "
                      << blend << " (1 - 0.3p " << 1.0 - 0.3 * p << ") Punch " << punch << " (1 - 0.3p^3 "
                      << 1.0 - 0.3 * p * p * p << ")\n";
            if (!bmSameBits(lock, 1.0f)) {
                ++lockOff;
            }
            if (std::fabs(static_cast<double>(blend) - (1.0 - 0.3 * p)) > 1e-6) {
                ++blendOff;
            }
            if (std::fabs(static_cast<double>(punch) - (1.0 - 0.3 * p * p * p)) > 1e-6) {
                ++punchOff;
            }
            if (punch < blend) {
                ++punchBelowBlend;
            }
        }
        CHECK(lockOff == 0);
        CHECK(blendOff == 0);
        CHECK(punchOff == 0);
        CHECK(punchBelowBlend == 0);
    }

    // (e) massOnlyPair: the AnchorLevel row of MassLaw evaluated at anchorMass and the resolved anchor.level.
    {
        const auto [a, b] = BassMorphTest::massOnlyPair();
        const MassLaw::Row* anchorRow = nullptr;
        for (const auto& row : MassLaw::kRows) {
            if (row.target == MassLaw::Target::AnchorLevel) {
                anchorRow = &row;
            }
        }
        REQUIRE(anchorRow != nullptr);

        const std::string dir = bmEvidenceDir();
        std::filesystem::create_directories(dir);
        std::ofstream csv(dir + "sc005e-mass-only.csv");
        REQUIRE(csv.good());
        csv << "p,mode,anchorMass,effectiveLevel\n";

        long lockDrift = 0;
        long blendFalls = 0;
        float blendAt0 = -1.0f;
        float blendAt1 = -1.0f;
        for (const BmMode mode : bmModes) {
            BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, mode, a, b, 0.0f);
            const float lockRef = MassLaw::evaluateRow(*anchorRow, e.resolved().anchorMass,
                                                       e.resolved().state.anchor.level);
            float prev = -1.0f;
            for (int k = 0; k <= 20; ++k) {
                const float p = static_cast<float>(k) / 20.0f;
                e.setPosition(p);
                e.reset();
                const float mass = e.resolved().anchorMass;
                const float eff = MassLaw::evaluateRow(*anchorRow, mass, e.resolved().state.anchor.level);
                csv << std::setprecision(6) << p << "," << bmModeName(mode) << "," << mass << "," << eff << "\n";
                if (mode == BmMode::Lock && !bmSameBits(eff, lockRef)) {
                    ++lockDrift;
                }
                if (mode == BmMode::Blend) {
                    if (k > 0 && eff < prev) {
                        ++blendFalls;
                    }
                    if (k == 0) {
                        blendAt0 = eff;
                    }
                    if (k == 20) {
                        blendAt1 = eff;
                    }
                }
                prev = eff;
            }
        }
        std::cout << std::setprecision(6) << "BassMorph_AnchorCurves (e): Lock drift " << lockDrift
                  << ", Blend falls " << blendFalls << ", Blend effective level at p = 0 " << blendAt0
                  << ", at p = 1 " << blendAt1 << "\n";
        CHECK(lockDrift == 0);
        CHECK(blendFalls == 0);
        CHECK(std::fabs(static_cast<double>(blendAt0) - 0.0) <= 1e-6);
        CHECK(std::fabs(static_cast<double>(blendAt1) - 1.0) <= 1e-6);
    }
}

// SC-024 engine part (FR-061: an endpoint edit lands in one interval, by the law's weight; never ramped)
TEST_CASE("BassMorph_EndpointEditMidMorph", "[systems][profundum][bass-morph][sc-024]") {
    constexpr double fs = 48000.0;
    constexpr double ramp = BassMorphEngine::kDefaultPositionRampMs;
    std::mt19937 rng(0x8024u);

    auto [a, b] = BassMorphTest::randomValidPair(rng);
    for (const BmGroup g : bmGroups) {   // every governing amount non-zero (0 -> 0.5)
        if (bmGoverning(a, g) == 0.0f) {
            bmGoverning(a, g) = 0.5f;
        }
        if (bmGoverning(b, g) == 0.0f) {
            bmGoverning(b, g) = 0.5f;
        }
    }
    REQUIRE(Krate::DSP::isValidMorphState(a));
    REQUIRE(Krate::DSP::isValidMorphState(b));

    // B': every continuous leaf different from B's, every discrete leaf equal to B's, governing amounts non-zero.
    MorphState bPrime = BassMorphTest::randomValidState(rng);
    io::forEachMorphLeaf(
        [&](auto leaf, auto& y, const auto& x) {
            using T = std::remove_cvref_t<decltype(y)>;
            if constexpr (std::is_same_v<T, float>) {
                constexpr MorphState::FieldInfo row = io::kFieldTable[decltype(leaf)::value];
                if (bmSameBits(y, x)) {
                    const float quarter = row.lo + 0.25f * (row.hi - row.lo);
                    y = bmSameBits(quarter, x) ? row.lo + 0.75f * (row.hi - row.lo) : quarter;
                }
            } else {
                y = x;
            }
        },
        bPrime, std::as_const(b));
    for (const BmGroup g : bmGroups) {
        if (bmGoverning(bPrime, g) == 0.0f) {
            bmGoverning(bPrime, g) = bmSameBits(bmGoverning(b, g), 0.25f) ? 0.75f : 0.25f;
        }
    }
    REQUIRE(Krate::DSP::isValidMorphState(bPrime));

    BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, a, b, 0.0f);
    e.setPosition(0.3f);
    static_cast<void>(bmLand(e, ramp, fs));
    const BmResolved before = e.resolved();
    const double t = static_cast<double>(e.position());
    REQUIRE(bmSameBits(e.anchorPosition(), e.position()));   // Blend: p_a == p

    // Continuous: the change in the next interval equals law(0.3; A, B') - law(0.3; A, B) in double.
    REQUIRE(e.setEndpoint(BmEndpoint::B, bPrime));
    const BmResolved after = e.advanceInterval();
    long continuousOff = 0;
    long continuousChecked = 0;
    io::forEachMorphLeaf(
        [&](auto leaf, [[maybe_unused]] const auto& x0, [[maybe_unused]] const auto& x1,
            [[maybe_unused]] const auto& xa, [[maybe_unused]] const auto& xb, [[maybe_unused]] const auto& xbp) {
            constexpr MorphState::FieldInfo row = io::kFieldTable[decltype(leaf)::value];
            if constexpr (row.law != BmLaw::Stepped) {
                const double expected =
                    bmLawDouble(row.law, xa, xbp, row.logOffset, t) - bmLawDouble(row.law, xa, xb, row.logOffset, t);
                const double got = static_cast<double>(x1) - static_cast<double>(x0);
                const double tol =
                    bmUlpK(row.law)
                        * static_cast<double>(BassMorphTest::ulpOf(std::max(std::fabs(xa), std::fabs(xb))))
                    + bmUlpK(row.law)
                          * static_cast<double>(BassMorphTest::ulpOf(std::max(std::fabs(xa), std::fabs(xbp))));
                ++continuousChecked;
                if (std::fabs(got - expected) > tol) {
                    ++continuousOff;
                    std::cout << std::setprecision(9) << "BassMorph_EndpointEditMidMorph leaf " << row.name
                              << ": change " << got << " expected " << expected << " tol " << tol << "\n";
                }
            }
        },
        before.state, after.state, a, b, bPrime);
    std::cout << "BassMorph_EndpointEditMidMorph: continuous leaves checked " << continuousChecked << ", off "
              << continuousOff << "\n";
    CHECK(continuousChecked == 46);
    CHECK(continuousOff == 0);
    // Complete in that interval: the following interval is bitwise unchanged.
    CHECK(BassMorphTest::bitEqual(e.advanceInterval(), after));

    // Discrete: from the landed p = 0.3 (selection A), A's algorithm edited -> resolved after exactly one interval.
    REQUIRE(e.selection(MorphState::SelectionGroup::Character) == BmEndpoint::A);
    MorphState aPrime = a;
    aPrime.characterAlgorithm = static_cast<Krate::DSP::BassCharacterStage::Algorithm>(
        static_cast<std::uint8_t>((static_cast<unsigned>(a.characterAlgorithm) + 1u) % 6u));
    REQUIRE(Krate::DSP::isValidMorphState(aPrime));
    REQUIRE(e.setEndpoint(BmEndpoint::A, aPrime));
    CHECK(e.resolved().state.characterAlgorithm == a.characterAlgorithm);   // not before the interval
    static_cast<void>(e.advanceInterval());
    CHECK(e.resolved().state.characterAlgorithm == aPrime.characterAlgorithm);
}

// SC-013 (FR-082: resolution only on the 32-sample grid; host-block partition invariance)
TEST_CASE("BassMorph_Partition", "[systems][profundum][bass-morph][sc-013]") {
    constexpr double fs = 48000.0;
    constexpr long total = 20L * 48000L;
    std::mt19937 rng(0x8013u);
    const auto [a0, b0] = BassMorphTest::randomValidPair(rng);

    std::vector<BmEvent> events;
    // Position every 20 - 200 ms: 10 % at theta +/- 1e-3, 5 % exact 0 / 1, else uniform.
    for (long at = 0;;) {
        at += std::max(1L, static_cast<long>(std::lround((20.0 + 180.0 * bmUnit(rng)) * fs / 1000.0)));
        if (at >= total) {
            break;
        }
        BmEvent ev;
        ev.stamp = at;
        ev.kind = BmEventKind::Position;
        const double u = bmUnit(rng);
        if (u < 0.10) {
            const float theta = (static_cast<std::uint32_t>(rng()) & 1u) != 0u ? BassMorphEngine::kThetaUp
                                                                                : BassMorphEngine::kThetaDown;
            ev.value = theta + ((static_cast<std::uint32_t>(rng()) & 1u) != 0u ? 1e-3f : -1e-3f);
        } else if (u < 0.15) {
            ev.value = (static_cast<std::uint32_t>(rng()) & 1u) != 0u ? 1.0f : 0.0f;
        } else {
            ev.value = static_cast<float>(bmUnit(rng));
        }
        events.push_back(ev);
    }
    // RampMs every 1 s on [5, 2000].
    for (long at = 48000; at < total; at += 48000) {
        BmEvent ev;
        ev.stamp = at;
        ev.kind = BmEventKind::RampMs;
        ev.value = static_cast<float>(5.0 + 1995.0 * bmUnit(rng));
        events.push_back(ev);
    }
    // Mode every 0.7 s.
    for (long at = 33600; at < total; at += 33600) {
        BmEvent ev;
        ev.stamp = at;
        ev.kind = BmEventKind::Mode;
        ev.mode = bmModes[static_cast<std::uint32_t>(rng()) % 3u];
        events.push_back(ev);
    }
    // EndpointA / EndpointB every 1.3 s from randomValidState.
    for (long at = 62400; at < total; at += 62400) {
        BmEvent evA;
        evA.stamp = at;
        evA.kind = BmEventKind::EndpointA;
        evA.state = BassMorphTest::randomValidState(rng);
        events.push_back(evA);
        BmEvent evB;
        evB.stamp = at;
        evB.kind = BmEventKind::EndpointB;
        evB.state = BassMorphTest::randomValidState(rng);
        events.push_back(evB);
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const BmEvent& x, const BmEvent& y) { return x.stamp < y.stamp; });

    const auto expectedLength = static_cast<std::size_t>((total + 31) / 32);
    long interior = 0;
    const std::vector<BmResolved> reference = bmRunPartition(events, a0, b0, total, 1, interior);
    REQUIRE(reference.size() == expectedLength);
    std::cout << "BassMorph_Partition: " << events.size() << " events, " << reference.size()
              << " intervals, interior intervals " << interior << "\n";
    CHECK(interior > 0);   // non-vacuity: the sequence is not just the endpoints

    long mismatches = 0;
    for (const long block : {1L, 7L, 32L, 64L, 511L, 4096L}) {
        for (int engine = 0; engine < 2; ++engine) {
            if (block == 1 && engine == 0) {
                continue;   // the reference itself
            }
            long unused = 0;
            const std::vector<BmResolved> seq = bmRunPartition(events, a0, b0, total, block, unused);
            CHECK(seq.size() == expectedLength);
            long blockMismatches = 0;
            const std::size_t n = std::min(seq.size(), reference.size());
            for (std::size_t i = 0; i < n; ++i) {
                if (!BassMorphTest::bitEqual(seq[i], reference[i])) {
                    ++blockMismatches;
                }
            }
            std::cout << "BassMorph_Partition: block " << block << " engine " << engine << " mismatches "
                      << blockMismatches << "\n";
            mismatches += blockMismatches;
        }
    }
    CHECK(mismatches == 0);
}

// ==============================================================================
// T005 cases (FR-062, FR-080, FR-081 restore side; SC-006(a), SC-007(c)(d), SC-011; plan S4.8, S4.9, C-2, C-7)
// ==============================================================================

namespace {

using BmRuntimeBlob = std::array<std::byte, BassMorphEngine::kMorphRuntimeBytes>;
using BmStateBlob = std::array<std::byte, Krate::DSP::kMorphStateBytes>;

// Plan S4.8 layout (version 1), mirrored so a test can corrupt exactly one field.
constexpr std::size_t bmRtVersion = 0;
constexpr std::size_t bmRtTarget = 1;
constexpr std::size_t bmRtPosition = 5;
constexpr std::size_t bmRtAnchorPosition = 9;
constexpr std::size_t bmRtMode = 13;
constexpr std::size_t bmRtRamp = 14;
constexpr std::size_t bmRtMainMask = 18;
constexpr std::size_t bmRtAnchorSel = 19;

/// The engine's runtime blob (REQUIREs the serialiser wrote all 20 bytes).
BmRuntimeBlob bmRuntimeBytes(const BassMorphEngine& e) {
    BmRuntimeBlob blob{};
    REQUIRE(e.serializeRuntime(blob.data(), blob.size()) == BassMorphEngine::kMorphRuntimeBytes);
    return blob;
}

void bmPutBits(BmRuntimeBlob& blob, std::size_t offset, std::uint32_t bits) {
    std::memcpy(blob.data() + offset, &bits, sizeof(bits));
}

void bmPutByte(BmRuntimeBlob& blob, std::size_t offset, std::uint8_t v) {
    blob[offset] = static_cast<std::byte>(v);
}

std::uint8_t bmGetByte(const BmRuntimeBlob& blob, std::size_t offset) {
    return std::to_integer<std::uint8_t>(blob[offset]);
}

/// FR-005a without Catch assertions, for hot loops: the state leaf by leaf (floats by bit pattern) and the three
/// scalars by bit pattern.
bool bmResolvedBitsEqual(const BmResolved& x, const BmResolved& y) {
    return io::leafBitsEqual(x.state, y.state) && bmSameBits(x.position, y.position)
           && bmSameBits(x.anchorPosition, y.anchorPosition) && bmSameBits(x.anchorMass, y.anchorMass);
}

/// Seeded position automation at 48 kHz: a new target every [minMs, maxMs] (10 % exact 0 / 1, else uniform),
/// applied identically to every engine passed. No allocation (usable inside an AllocationScope).
struct BmAutomation {
    std::mt19937 rng;
    double minMs = 20.0;
    double maxMs = 400.0;
    long untilNext = 0;

    BmAutomation(std::uint32_t seed, double lo, double hi) : rng(seed), minMs(lo), maxMs(hi) {}

    void step(BassMorphEngine& x, BassMorphEngine* y) {
        if (untilNext <= 0) {
            float target = 0.0f;
            if (bmUnit(rng) < 0.1) {
                target = (static_cast<std::uint32_t>(rng()) & 1u) != 0u ? 1.0f : 0.0f;
            } else {
                target = static_cast<float>(bmUnit(rng));
            }
            x.setPosition(target);
            if (y != nullptr) {
                y->setPosition(target);
            }
            const double ms = minMs + (maxMs - minMs) * bmUnit(rng);
            untilNext = std::max(1L, static_cast<long>(std::lround(ms * 48000.0 / 32000.0)));
        }
        --untilNext;
    }
};

/// SC-007(c) bookkeeping.
struct BmRoundTripStats {
    long stops = 0;
    long restoreFailures = 0;
    long stopMismatches = 0;      // restored state / bytes / ramp / mode / selections differ at the stop
    long intervalMismatches = 0;  // a Resolved differs in the 4 096 intervals after the stop
    long interiorStops = 0;       // non-vacuity: 0 < p < 1 at the stop
    long inFlightStops = 0;       // non-vacuity: p != target or p_a != c(p) at the stop
};

/// SC-007(c), one stop: serialise both endpoints and the runtime blob of the live engine; a fresh engine
/// prepare(48000) + setEndpoint x 2 (from the deserialised bytes) + restoreRuntime must reproduce resolved()
/// bitwise, then track the live engine bitwise for 4 096 intervals under the same automation.
void bmRoundTripStop(BassMorphEngine& live, BmAutomation& autom, BmRoundTripStats& st, const char* label) {
    BmStateBlob aBytes{};
    BmStateBlob bBytes{};
    REQUIRE(Krate::DSP::serializeMorphState(live.endpoint(BmEndpoint::A), aBytes.data(), aBytes.size())
            == Krate::DSP::kMorphStateBytes);
    REQUIRE(Krate::DSP::serializeMorphState(live.endpoint(BmEndpoint::B), bBytes.data(), bBytes.size())
            == Krate::DSP::kMorphStateBytes);
    const BmRuntimeBlob blob = bmRuntimeBytes(live);

    ++st.stops;
    const float p = live.position();
    if (p > 0.0f && p < 1.0f) {
        ++st.interiorStops;
    }
    if (!bmSameBits(p, live.targetPosition())
        || !bmSameBits(live.anchorPosition(), BassMorphEngine::curve(live.anchorMorphMode(), p))) {
        ++st.inFlightStops;
    }

    BassMorphEngine restored;
    restored.prepare(48000.0);
    MorphState a{};
    MorphState b{};
    REQUIRE(Krate::DSP::deserializeMorphState(aBytes.data(), aBytes.size(), a));
    REQUIRE(Krate::DSP::deserializeMorphState(bBytes.data(), bBytes.size(), b));
    REQUIRE(restored.setEndpoint(BmEndpoint::A, a));
    REQUIRE(restored.setEndpoint(BmEndpoint::B, b));
    if (!restored.restoreRuntime(blob.data(), blob.size())) {
        ++st.restoreFailures;
        std::cout << std::setprecision(9) << "BassMorph_RuntimeRoundTrip: restore rejected at stop '" << label
                  << "' p " << p << " target " << live.targetPosition() << " p_a " << live.anchorPosition()
                  << " mode " << bmModeName(live.anchorMorphMode()) << "\n";
        return;
    }

    bool stopOk = bmResolvedBitsEqual(restored.resolved(), live.resolved())
                  && bmSameBits(restored.positionRampMs(), 333.0f)
                  && restored.anchorMorphMode() == live.anchorMorphMode()
                  && bmSameBits(restored.targetPosition(), live.targetPosition())
                  && bmRuntimeBytes(restored) == blob;
    for (const BmGroup g : bmGroups) {
        stopOk = stopOk && restored.selection(g) == live.selection(g);
    }
    if (!stopOk) {
        ++st.stopMismatches;
        std::cout << std::setprecision(9) << "BassMorph_RuntimeRoundTrip: stop mismatch at '" << label << "' p " << p
                  << " restored p " << restored.position() << " p_a " << live.anchorPosition() << " / "
                  << restored.anchorPosition() << " ramp " << restored.positionRampMs() << "\n";
    }

    long mismatches = 0;
    for (int i = 0; i < 4096; ++i) {
        autom.step(live, &restored);
        const BmResolved& rl = live.advanceInterval();
        const BmResolved& rr = restored.advanceInterval();
        if (!bmResolvedBitsEqual(rl, rr)) {
            ++mismatches;
        }
    }
    if (mismatches != 0) {
        std::cout << "BassMorph_RuntimeRoundTrip: " << mismatches << " interval mismatches after stop '" << label
                  << "'\n";
    }
    st.intervalMismatches += mismatches;
}

/// The engine's state for the "unchanged" checks of SC-007(d).
struct BmEngineSnapshot {
    BmResolved resolved{};
    BmRuntimeBlob runtime{};
};

BmEngineSnapshot bmSnapshot(const BassMorphEngine& e) {
    return {e.resolved(), bmRuntimeBytes(e)};
}

/// SC-007(d): a copy of pristine must reject (src, size) and stay bitwise as pristine (resolved() and the
/// serializeRuntime bytes).
void bmExpectRejected(const BassMorphEngine& pristine, const std::byte* src, std::size_t size, const char* what) {
    INFO(what);
    BassMorphEngine t = pristine;
    const BmEngineSnapshot before = bmSnapshot(pristine);
    CHECK_FALSE(t.restoreRuntime(src, size));
    const BmEngineSnapshot after = bmSnapshot(t);
    CHECK(BassMorphTest::bitEqual(after.resolved, before.resolved));
    CHECK(after.runtime == before.runtime);
}

/// Positive control for a rejection row: a copy of pristine accepts the unmodified blob.
void bmExpectAccepted(const BassMorphEngine& pristine, const BmRuntimeBlob& blob, const char* what) {
    INFO(what);
    BassMorphEngine t = pristine;
    CHECK(t.restoreRuntime(blob.data(), blob.size()));
    CHECK(bmRuntimeBytes(t) == blob);
}

/// A runtime blob from an engine with endpoints (a, b), Blend, ramp 333 ms, reset at 0 and landed at p.
BmRuntimeBlob bmBlobLandedAt(const MorphState& a, const MorphState& b, float p) {
    BassMorphEngine e = bmMakeEngine(48000.0, 333.0f, BmMode::Blend, a, b, 0.0f);
    e.setPosition(p);
    static_cast<void>(bmLand(e, 333.0, 48000.0));
    return bmRuntimeBytes(e);
}

}  // namespace

// SC-007(c), FR-062 (plan S4.8, C-2, C-3)
TEST_CASE("BassMorph_RuntimeRoundTrip", "[systems][profundum][bass-morph][sc-007]") {
    constexpr double fs = 48000.0;
    constexpr float ramp = 333.0f;
    std::mt19937 rng(0x8070u);
    BmRoundTripStats st;
    const auto [r3a, r3b] = bmRule3CharacterPair();
    const auto [sa, sb] = BassMorphTest::showcasePair();

    for (const BmMode mode : bmModes) {
        // 200 random stops under seeded automation.
        {
            const auto [a, b] = BassMorphTest::randomValidPair(rng);
            BassMorphEngine live = bmMakeEngine(fs, ramp, mode, a, b, 0.0f);
            BmAutomation autom(static_cast<std::uint32_t>(rng()), 20.0, 400.0);
            for (int stop = 0; stop < 200; ++stop) {
                const long gap = 1 + static_cast<long>(static_cast<std::uint32_t>(rng()) % 600u);
                for (long i = 0; i < gap; ++i) {
                    autom.step(live, nullptr);
                    static_cast<void>(live.advanceInterval());
                }
                bmRoundTripStop(live, autom, st, "random");
            }
        }
        // A band point reached from below (selections A) and from above (B), rule-3 pair.
        {
            BassMorphEngine live = bmMakeEngine(fs, ramp, mode, r3a, r3b, 0.0f);
            live.setPosition(0.5f);
            static_cast<void>(bmLand(live, static_cast<double>(ramp), fs));
            CHECK(live.selection(BmGroup::Character) == BmEndpoint::A);
            BmAutomation autom(static_cast<std::uint32_t>(rng()), 20.0, 400.0);
            bmRoundTripStop(live, autom, st, "band from below");
        }
        {
            BassMorphEngine live = bmMakeEngine(fs, ramp, mode, r3a, r3b, 1.0f);
            live.setPosition(0.5f);
            static_cast<void>(bmLand(live, static_cast<double>(ramp), fs));
            CHECK(live.selection(BmGroup::Character) == BmEndpoint::B);
            BmAutomation autom(static_cast<std::uint32_t>(rng()), 20.0, 400.0);
            bmRoundTripStop(live, autom, st, "band from above");
        }
        // Mid-ramp: target 1 from 0, stop after 7 intervals.
        {
            const auto [a, b] = BassMorphTest::randomValidPair(rng);
            BassMorphEngine live = bmMakeEngine(fs, ramp, mode, a, b, 0.0f);
            live.setPosition(1.0f);
            for (int i = 0; i < 7; ++i) {
                static_cast<void>(live.advanceInterval());
            }
            CHECK_FALSE(bmSameBits(live.position(), live.targetPosition()));
            BmAutomation autom(static_cast<std::uint32_t>(rng()), 20.0, 400.0);
            bmRoundTripStop(live, autom, st, "mid-ramp");
        }
        // The showcase pair landed at 0.3: rule 2 forces Character to B.
        {
            BassMorphEngine live = bmMakeEngine(fs, ramp, mode, sa, sb, 0.0f);
            live.setPosition(0.3f);
            static_cast<void>(bmLand(live, static_cast<double>(ramp), fs));
            CHECK(live.selection(BmGroup::Character) == BmEndpoint::B);
            BmAutomation autom(static_cast<std::uint32_t>(rng()), 20.0, 400.0);
            bmRoundTripStop(live, autom, st, "showcase at 0.3");
        }
    }
    // An in-flight mode change: Blend -> Punch at a landed 0.6, stop 3 intervals later.
    {
        const auto [a, b] = BassMorphTest::randomValidPair(rng);
        BassMorphEngine live = bmMakeEngine(fs, ramp, BmMode::Blend, a, b, 0.0f);
        live.setPosition(0.6f);
        static_cast<void>(bmLand(live, static_cast<double>(ramp), fs));
        live.setAnchorMorphMode(BmMode::Punch);
        for (int i = 0; i < 3; ++i) {
            static_cast<void>(live.advanceInterval());
        }
        CHECK_FALSE(bmSameBits(live.anchorPosition(), BassMorphEngine::curve(BmMode::Punch, live.position())));
        BmAutomation autom(static_cast<std::uint32_t>(rng()), 20.0, 400.0);
        bmRoundTripStop(live, autom, st, "in-flight mode change");
    }

    std::cout << "BassMorph_RuntimeRoundTrip: stops " << st.stops << " (interior " << st.interiorStops
              << ", in flight " << st.inFlightStops << "), restore failures " << st.restoreFailures
              << ", stop mismatches " << st.stopMismatches << ", interval mismatches " << st.intervalMismatches
              << "\n";
    CHECK(st.stops == 3 * (200 + 4) + 1);
    CHECK(st.interiorStops > 0);
    CHECK(st.inFlightStops > 0);
    CHECK(st.restoreFailures == 0);
    CHECK(st.stopMismatches == 0);
    CHECK(st.intervalMismatches == 0);
}

// SC-007(d) finite rows, FR-062 (plan S4.8, C-2, C-7). NaN / Inf / denormal rows are T006's (nonfinite TU).
TEST_CASE("BassMorph_RuntimeRejects", "[systems][profundum][bass-morph][sc-007]") {
    constexpr double fs = 48000.0;
    const auto [r3a, r3b] = bmRule3CharacterPair();
    const auto [sa, sb] = BassMorphTest::showcasePair();

    // The valid source blob: rule-3 pair, Blend, ramp 333 ms, landed at 0.25 (every selection A).
    const BmRuntimeBlob valid = bmBlobLandedAt(r3a, r3b, 0.25f);

    // The receiving engine: the same endpoints, a different runtime state (Punch, ramp 100 ms, mid-ramp 0.7 -> 0.1).
    BassMorphEngine pristine = bmMakeEngine(fs, 100.0f, BmMode::Punch, r3a, r3b, 0.7f);
    pristine.setPosition(0.1f);
    for (int i = 0; i < 5; ++i) {
        static_cast<void>(pristine.advanceInterval());
    }
    REQUIRE_FALSE(bmRuntimeBytes(pristine) == valid);
    bmExpectAccepted(pristine, valid, "positive control: the unmodified blob");

    SECTION("framing") {
        bmExpectRejected(pristine, nullptr, BassMorphEngine::kMorphRuntimeBytes, "src == nullptr");
        for (std::size_t size = 0; size < BassMorphEngine::kMorphRuntimeBytes; ++size) {
            INFO("size " << size);
            bmExpectRejected(pristine, valid.data(), size, "short size");
        }
        for (const std::uint8_t version : {std::uint8_t{0}, std::uint8_t{2}}) {
            BmRuntimeBlob blob = valid;
            bmPutByte(blob, bmRtVersion, version);
            INFO("version " << static_cast<int>(version));
            bmExpectRejected(pristine, blob.data(), blob.size(), "version");
        }
    }

    SECTION("positions out of range and -0 (C-7)") {
        const std::array<std::uint32_t, 3> bad{std::bit_cast<std::uint32_t>(1.5f),
                                               std::bit_cast<std::uint32_t>(-0.25f), 0x80000000u /* -0 */};
        for (const std::size_t offset : {bmRtTarget, bmRtPosition, bmRtAnchorPosition}) {
            for (const std::uint32_t bits : bad) {
                BmRuntimeBlob blob = valid;
                bmPutBits(blob, offset, bits);
                INFO("offset " << offset << " bits 0x" << std::hex << bits << std::dec);
                bmExpectRejected(pristine, blob.data(), blob.size(), "position field");
            }
        }
    }

    SECTION("ramp, mode, selection bytes") {
        for (const float rampMs : {4.9f, 2001.0f}) {
            BmRuntimeBlob blob = valid;
            bmPutBits(blob, bmRtRamp, std::bit_cast<std::uint32_t>(rampMs));
            INFO("ramp " << rampMs);
            bmExpectRejected(pristine, blob.data(), blob.size(), "ramp ms");
        }
        for (const std::uint8_t m : {std::uint8_t{3}, std::uint8_t{255}}) {
            BmRuntimeBlob blob = valid;
            bmPutByte(blob, bmRtMode, m);
            INFO("mode byte " << static_cast<int>(m));
            bmExpectRejected(pristine, blob.data(), blob.size(), "mode byte");
        }
        for (const std::uint8_t mask : {std::uint8_t{0x10}, std::uint8_t{0xFF}}) {
            BmRuntimeBlob blob = valid;
            bmPutByte(blob, bmRtMainMask, mask);
            INFO("main mask " << static_cast<int>(mask));
            bmExpectRejected(pristine, blob.data(), blob.size(), "main mask");
        }
        {
            BmRuntimeBlob blob = valid;
            bmPutByte(blob, bmRtAnchorSel, 2);
            bmExpectRejected(pristine, blob.data(), blob.size(), "Anchor byte 2");
        }
    }

    SECTION("selection contradicting S4.5 for the current endpoints") {
        // Rule 3, B at p = 0.40 (<= theta_down requires A).
        {
            const BmRuntimeBlob base = bmBlobLandedAt(r3a, r3b, 0.40f);
            REQUIRE((bmGetByte(base, bmRtMainMask) & 0x01u) == 0u);
            bmExpectAccepted(pristine, base, "control: p = 0.40, Character A");
            BmRuntimeBlob blob = base;
            bmPutByte(blob, bmRtMainMask, static_cast<std::uint8_t>(bmGetByte(base, bmRtMainMask) | 0x01u));
            bmExpectRejected(pristine, blob.data(), blob.size(), "rule 3: Character B at p = 0.40");
        }
        // Rule 3, A at p = 0.60 (>= theta_up requires B).
        {
            const BmRuntimeBlob base = bmBlobLandedAt(r3a, r3b, 0.60f);
            REQUIRE((bmGetByte(base, bmRtMainMask) & 0x01u) == 0x01u);
            bmExpectAccepted(pristine, base, "control: p = 0.60, Character B");
            BmRuntimeBlob blob = base;
            bmPutByte(blob, bmRtMainMask, static_cast<std::uint8_t>(bmGetByte(base, bmRtMainMask) & 0x0Eu));
            bmExpectRejected(pristine, blob.data(), blob.size(), "rule 3: Character A at p = 0.60");
        }
        // Rule 1: p = 0 with a B bit; p = 1 with an A bit (main and Anchor).
        {
            const BmRuntimeBlob base = bmBlobLandedAt(r3a, r3b, 0.0f);
            REQUIRE(bmGetByte(base, bmRtMainMask) == 0u);
            REQUIRE(bmGetByte(base, bmRtAnchorSel) == 0u);
            bmExpectAccepted(pristine, base, "control: p = 0, all A");
            BmRuntimeBlob blob = base;
            bmPutByte(blob, bmRtMainMask, 0x01u);
            bmExpectRejected(pristine, blob.data(), blob.size(), "rule 1: p = 0 with Character B");
            BmRuntimeBlob blobAnchor = base;
            bmPutByte(blobAnchor, bmRtAnchorSel, 1u);
            bmExpectRejected(pristine, blobAnchor.data(), blobAnchor.size(), "rule 1: p_a = 0 with Anchor B");
        }
        {
            const BmRuntimeBlob base = bmBlobLandedAt(r3a, r3b, 1.0f);
            REQUIRE(bmGetByte(base, bmRtMainMask) == 0x0Fu);
            REQUIRE(bmGetByte(base, bmRtAnchorSel) == 1u);
            bmExpectAccepted(pristine, base, "control: p = 1, all B");
            BmRuntimeBlob blob = base;
            bmPutByte(blob, bmRtMainMask, 0x0Eu);
            bmExpectRejected(pristine, blob.data(), blob.size(), "rule 1: p = 1 with Character A");
            BmRuntimeBlob blobAnchor = base;
            bmPutByte(blobAnchor, bmRtAnchorSel, 0u);
            bmExpectRejected(pristine, blobAnchor.data(), blobAnchor.size(), "rule 1: p_a = 1 with Anchor A");
        }
        // Rule 2: the showcase pair at 0.3 forces Character B; a blob with Character A is rejected.
        {
            const BassMorphEngine showcasePristine = bmMakeEngine(fs, 100.0f, BmMode::Punch, sa, sb, 0.7f);
            const BmRuntimeBlob base = bmBlobLandedAt(sa, sb, 0.3f);
            REQUIRE((bmGetByte(base, bmRtMainMask) & 0x01u) == 0x01u);
            bmExpectAccepted(showcasePristine, base, "control: showcase at 0.3, Character B");
            BmRuntimeBlob blob = base;
            bmPutByte(blob, bmRtMainMask, static_cast<std::uint8_t>(bmGetByte(base, bmRtMainMask) & 0x0Eu));
            bmExpectRejected(showcasePristine, blob.data(), blob.size(), "rule 2: showcase at 0.3 with Character A");
            // The check is against the CURRENT endpoints: the valid showcase blob (Character B at 0.3) contradicts
            // rule 3 under the rule-3 pair (0.3 <= theta_down requires A).
            bmExpectRejected(pristine, base.data(), base.size(), "showcase blob into rule-3 endpoints");
        }
    }

    SECTION("serializeRuntime null / short destination") {
        CHECK(pristine.serializeRuntime(nullptr, BassMorphEngine::kMorphRuntimeBytes) == 0);
        BmRuntimeBlob sentinel{};
        sentinel.fill(std::byte{0xAB});
        CHECK(pristine.serializeRuntime(sentinel.data(), BassMorphEngine::kMorphRuntimeBytes - 1) == 0);
        CHECK(std::all_of(sentinel.begin(), sentinel.end(), [](std::byte x) { return x == std::byte{0xAB}; }));
    }
}

// SC-006(a), tasks.md deviation 12
TEST_CASE("BassMorph_Idempotent", "[systems][profundum][bass-morph][sc-006]") {
    constexpr double fs = 48000.0;
    constexpr double rampMs = static_cast<double>(BassMorphEngine::kDefaultPositionRampMs);
    std::mt19937 rng(0x8006u);
    long mismatches = 0;
    long cases = 0;
    long movedIntervals = 0;   // non-vacuity: intervals whose state differs from A during (ii)
    for (int pair = 0; pair < 200; ++pair) {
        const auto [a, b] = BassMorphTest::randomValidPair(rng);
        for (const BmMode mode : bmModes) {
            BassMorphEngine e = bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, mode, a, b, 0.0f);
            const BmResolved initial = e.resolved();
            const BmRuntimeBlob initialBytes = bmRuntimeBytes(e);

            // (i) 0 -> 1 (land) -> 0 (land).
            e.setPosition(1.0f);
            static_cast<void>(bmLand(e, rampMs, fs));
            e.setPosition(0.0f);
            static_cast<void>(bmLand(e, rampMs, fs));
            ++cases;
            if (!BassMorphTest::bitEqual(e.resolved(), initial) || bmRuntimeBytes(e) != initialBytes) {
                ++mismatches;
                std::cout << "BassMorph_Idempotent: (i) mismatch, pair " << pair << " mode " << bmModeName(mode)
                          << "\n";
            }

            // (ii) 2 s of random automation (a new target every 30 - 300 ms, no mode change), then 0 and land.
            BmAutomation autom(static_cast<std::uint32_t>(rng()), 30.0, 300.0);
            const auto total = static_cast<long>(std::lround(2.0 * fs / 32.0));
            for (long i = 0; i < total; ++i) {
                autom.step(e, nullptr);
                static_cast<void>(e.advanceInterval());
                if (!io::leafBitsEqual(e.resolved().state, a)) {
                    ++movedIntervals;
                }
            }
            e.setPosition(0.0f);
            static_cast<void>(bmLand(e, rampMs, fs));
            ++cases;
            if (!BassMorphTest::bitEqual(e.resolved(), initial) || bmRuntimeBytes(e) != initialBytes) {
                ++mismatches;
                std::cout << "BassMorph_Idempotent: (ii) mismatch, pair " << pair << " mode " << bmModeName(mode)
                          << "\n";
            }
            for (const BmGroup g : bmGroups) {
                CHECK(e.selection(g) == BmEndpoint::A);
            }
        }
    }
    std::cout << "BassMorph_Idempotent: " << cases << " returns, " << mismatches << " mismatches, " << movedIntervals
              << " intervals away from A\n";
    CHECK(movedIntervals > 0);
    REQUIRE(mismatches == 0);
}

// SC-011, FR-080, FR-081 (plan S4.9)
TEST_CASE("BassMorph_RealTimeSafety", "[systems][profundum][bass-morph][sc-011]") {
    using Krate::DSP::kMorphStateBytes;
    using Engine = BassMorphEngine;
    static_assert(std::is_nothrow_default_constructible_v<Engine>);
    static_assert(std::is_nothrow_copy_constructible_v<Engine>);
    static_assert(std::is_nothrow_move_constructible_v<Engine>);
    static_assert(std::is_nothrow_copy_assignable_v<Engine>);
    static_assert(std::is_nothrow_move_assignable_v<Engine>);
    static_assert(noexcept(std::declval<Engine&>().prepare(48000.0)));
    static_assert(noexcept(std::declval<Engine&>().reset()));
    static_assert(noexcept(std::declval<Engine&>().setEndpoint(BmEndpoint::A, std::declval<const MorphState&>())));
    static_assert(noexcept(std::declval<const Engine&>().endpoint(BmEndpoint::A)));
    static_assert(noexcept(std::declval<Engine&>().setPosition(0.5f)));
    static_assert(noexcept(std::declval<const Engine&>().position()));
    static_assert(noexcept(std::declval<const Engine&>().targetPosition()));
    static_assert(noexcept(std::declval<const Engine&>().anchorPosition()));
    static_assert(noexcept(std::declval<Engine&>().setPositionRampMs(40.0f)));
    static_assert(noexcept(std::declval<const Engine&>().positionRampMs()));
    static_assert(noexcept(std::declval<Engine&>().setAnchorMorphMode(BmMode::Blend)));
    static_assert(noexcept(std::declval<const Engine&>().anchorMorphMode()));
    static_assert(noexcept(std::declval<const Engine&>().selection(MorphState::SelectionGroup::Character)));
    static_assert(noexcept(Engine::curve(BmMode::Punch, 0.5f)));
    static_assert(noexcept(std::declval<Engine&>().advanceInterval()));
    static_assert(noexcept(std::declval<const Engine&>().resolved()));
    static_assert(noexcept(std::declval<const Engine&>().serializeRuntime(nullptr, 0)));
    static_assert(noexcept(std::declval<Engine&>().restoreRuntime(nullptr, 0)));
    static_assert(noexcept(Krate::DSP::isValidMorphState(std::declval<const MorphState&>())));
    static_assert(noexcept(Krate::DSP::serializeMorphState(std::declval<const MorphState&>(), nullptr, 0)));
    static_assert(noexcept(Krate::DSP::deserializeMorphState(nullptr, 0, std::declval<MorphState&>())));

    // Everything pre-built outside the scope.
    constexpr double fs = 48000.0;
    std::mt19937 rng(0x8011u);
    std::array<MorphState, 16> pool{};
    for (MorphState& s : pool) {
        s = BassMorphTest::randomValidState(rng);
    }
    BassMorphEngine e =
        bmMakeEngine(fs, BassMorphEngine::kDefaultPositionRampMs, BmMode::Blend, pool[0], pool[1], 0.0f);
    BmAutomation autom(static_cast<std::uint32_t>(rng()), 20.0, 400.0);
    std::array<std::uint32_t, 64> draws{};   // pre-drawn choices for ramp / mode / endpoint / pool index
    for (std::uint32_t& d : draws) {
        d = static_cast<std::uint32_t>(rng());
    }
    BmRuntimeBlob runtime{};
    BmStateBlob stateBytes{};
    MorphState decoded{};
    constexpr long total = 15000;   // 10 s at 48 kHz

    float sink = 0.0f;
    long restores = 0;
    long restoreOk = 0;
    long stateRoundTrips = 0;
    long stateRoundTripOk = 0;
    long endpointCalls = 0;
    long endpointOk = 0;
    std::size_t allocations = 0;
    {
        const TestHelpers::AllocationScope scope;
        for (long i = 1; i <= total; ++i) {
            const std::uint32_t d = draws[static_cast<std::size_t>(i) % draws.size()];
            if (i % 500 == 0) {
                // The blob is consistent with the current endpoints (no setEndpoint since the last interval).
                ++restores;
                const std::size_t n = e.serializeRuntime(runtime.data(), runtime.size());
                if (n == BassMorphEngine::kMorphRuntimeBytes && e.restoreRuntime(runtime.data(), n)) {
                    ++restoreOk;
                }
                ++stateRoundTrips;
                const std::size_t m = Krate::DSP::serializeMorphState(e.endpoint(BmEndpoint::B), stateBytes.data(),
                                                                      stateBytes.size());
                if (m == kMorphStateBytes && Krate::DSP::deserializeMorphState(stateBytes.data(), m, decoded)) {
                    ++stateRoundTripOk;
                }
            }
            if (i % 100 == 0) {
                ++endpointCalls;
                const BmEndpoint which = (d & 1u) != 0u ? BmEndpoint::B : BmEndpoint::A;
                if (e.setEndpoint(which, pool[(d >> 1) % pool.size()])) {
                    ++endpointOk;
                }
            }
            if (i % 250 == 0) {
                e.setPositionRampMs(5.0f + static_cast<float>((d >> 8) % 1996u));
                e.setAnchorMorphMode(bmModes[(d >> 4) % bmModes.size()]);
            }
            if (i % 1000 == 0) {
                e.reset();
            }
            autom.step(e, nullptr);
            const BmResolved& r = e.advanceInterval();
            sink += r.position + r.anchorPosition + r.state.characterAmount;
        }
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    std::cout << "BassMorph_RealTimeSafety: " << total << " intervals, restores " << restoreOk << "/" << restores
              << ", state round trips " << stateRoundTripOk << "/" << stateRoundTrips << ", setEndpoint "
              << endpointOk << "/" << endpointCalls << ", allocations " << allocations << "\n";
    CHECK(allocations == 0);
    CHECK(restores == 30);
    CHECK(restoreOk == restores);
    CHECK(stateRoundTripOk == stateRoundTrips);
    CHECK(endpointOk == endpointCalls);
    CHECK(Krate::DSP::detail::isFinite(sink));
}
