// ==============================================================================
// Layer 3: System Tests - Profundum Anchor, non-finite TU
//                                    (Profundum Phase 3, specs/profundum-phase3-anchor)
// ==============================================================================
// Reference: specs/profundum-phase3-anchor/plan.md (S8.1, S4.3)
//            specs/profundum-phase3-anchor/tasks.md (T006 wires this TU,
//                                                    T015 fills it)
//
// SCOPE OF THIS TU (plan S8.1): SC-015 and the SC-007 non-finite rows (FR-054,
//   FR-055; plan S1 C-11). Compiled with -fno-fast-math (listed in the
//   -fno-fast-math block of dsp/tests/CMakeLists.txt) so NaN/Inf bit patterns
//   keep IEEE semantics. Bad values are built by bit pattern (std::bit_cast) and
//   finiteness is checked with detail::isFinite only. Tags [systems][profundum][anchor].
//   No [long] case lives here (NaN/Inf guards are never [long], tasks.md rule 6).
//
// Every render of this TU goes through PT::renderAnchor (L == R bitwise) and
// requireSafe: every sample finite, |y| <= kOutputCeiling, L + L == L + R
// bitwise (mono sum = 2·L, SC-007), stateFinite() true.
//
// FTZ/DAZ: dsp_test_main.cpp calls enableFTZDAZ() before any case runs
// (FR-057 precondition). Denormal inputs are therefore never compared as floats
// here (DAZ reads them as zero); they are identified and read back by bits.
// ==============================================================================

#include "anchor_oscillator_test_helpers.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/anchor_oscillator.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

using Krate::DSP::AnchorOscillator;
namespace PT = Krate::DSP::ProfundumTest;

namespace {

using AW = AnchorOscillator::Waveform;
using AM = AnchorOscillator::Mode;
using ARP = AnchorOscillator::RetriggerPhase;
using Controls = AnchorOscillator::Controls;
using Hook = std::function<void(AnchorOscillator&)>;

constexpr double kFs = 48000.0;
constexpr std::size_t kQuarter = 4800;    // 0.1 s at 48 kHz: a multiple of 64 and of the 16-sample grid
constexpr std::size_t kSegments = 4;      // arm, mid, arm, mid (see runAnchor)
constexpr std::size_t kTotal = kQuarter * kSegments;
constexpr std::array<std::size_t, 5> kBlocks{1, 37, 64, 512, 4096};   // SC-007 host blocks
constexpr std::size_t kControlBlock = 64;                            // positive controls

/// Truncated-triangle crest factor (K = 16, plan S1 C-1; anchor-model.js `tri`).
constexpr double kTriangleCrest = 1.7101;

struct BadValue {
    const char* name;
    std::uint32_t bits;
};

constexpr std::array<BadValue, 3> kNonFinite{{
    {"NaN", 0x7FC00000u},
    {"+Inf", 0x7F800000u},
    {"-Inf", 0xFF800000u},
}};

constexpr std::array<BadValue, 2> kDenormals{{
    {"min denormal", 0x00000001u},
    {"1e-40", 0x000116C2u},   // 71362 · 2^-149
}};

std::uint32_t bitsOf(float x) noexcept { return std::bit_cast<std::uint32_t>(x); }

/// A non-finite pattern, checked against detail::isFinite first.
float nonFiniteValue(const BadValue& b) {
    const float v = std::bit_cast<float>(b.bits);
    INFO("value " << b.name);
    REQUIRE_FALSE(Krate::DSP::detail::isFinite(v));
    return v;
}

/// A denormal pattern: finite, exponent field zero, mantissa non-zero (checked by bits; DAZ is on).
float denormalValue(const BadValue& b) {
    const float v = std::bit_cast<float>(b.bits);
    INFO("value " << b.name);
    REQUIRE(Krate::DSP::detail::isFinite(v));
    REQUIRE((b.bits & 0x7F800000u) == 0u);
    REQUIRE((b.bits & 0x007FFFFFu) != 0u);
    return v;
}

/// One Controls float field: its SC-015 mode and its C-11 denormal read-back.
struct ControlField {
    const char* name;
    float Controls::*member;
    AM mode;                // LOCK for level, PUNCH for the punch fields, GHOST for ghost (tasks.md T015)
    float denormalReadBack; // C-11: flushed to 0, then clamped
    float control;          // a finite in-range value that must change the render (positive control)
};

constexpr std::array<ControlField, 4> kFields{{
    {"level", &Controls::level, AM::Lock, 0.0f, 0.25f},
    {"punchBoostDb", &Controls::punchBoostDb, AM::Punch, 0.0f, 12.0f},
    {"punchTimeMs", &Controls::punchTimeMs, AM::Punch, AnchorOscillator::kMinPunchTimeMs, 100.0f},
    {"ghostTimeMs", &Controls::ghostTimeMs, AM::Ghost, AnchorOscillator::kMinGhostTimeMs, 50.0f},
}};

struct Out {
    std::vector<float> L, R;
};

/// SC-015 / SC-007 checks on one rendered slice (L == R is already required by renderAnchor).
void requireSafe(const AnchorOscillator& a, const PT::AnchorRender& r) {
    bool finite = true;
    bool bounded = true;
    bool monoSum = true;
    for (std::size_t i = 0; i < r.L.size(); ++i) {
        const float l = r.L[i];
        const float rr = r.R[i];
        finite = finite && Krate::DSP::detail::isFinite(l) && Krate::DSP::detail::isFinite(rr);
        bounded = bounded && std::abs(l) <= AnchorOscillator::kOutputCeiling;
        monoSum = monoSum && bitsOf(l + l) == bitsOf(l + rr);
    }
    REQUIRE(finite);
    REQUIRE(bounded);
    REQUIRE(monoSum);
    REQUIRE(a.stateFinite());
}

struct Setup {
    Controls controls{};
    float ratio = 1.0f;
    const std::vector<float>* f0Traj = nullptr;   // whole-render trajectory (kTotal samples) or null
    const std::vector<float>* rTraj = nullptr;
};

/// Prepares at 48 kHz (Reset policy, a1 = kMaxCoreH1Amplitude, held f0 = f0), then renders four
/// 0.1 s segments in `block`-sized blocks. Before segments 0 and 2 `armHook` runs (it is expected
/// to issue the arming noteOn); before segments 1 and 3 `midHook` runs on the sounding voice.
Out runAnchor(const Setup& s, float f0, const Hook& armHook, const Hook& midHook, std::size_t block) {
    AnchorOscillator a;
    a.prepare(kFs);
    a.setRetriggerPhase(ARP::Reset);
    a.setControls(s.controls);
    a.setFollowRatio(s.ratio);
    a.setCoreH1Amplitude(AnchorOscillator::kMaxCoreH1Amplitude);
    a.setFrequency(f0);
    Out o;
    o.L.reserve(kTotal);
    o.R.reserve(kTotal);
    for (std::size_t seg = 0; seg < kSegments; ++seg) {
        if (seg % 2 == 0)
            armHook(a);
        else
            midHook(a);
        const std::size_t off = seg * kQuarter;
        const PT::AnchorRender r =
            PT::renderAnchor(a, kQuarter, block, s.f0Traj != nullptr ? s.f0Traj->data() + off : nullptr,
                             s.rTraj != nullptr ? s.rTraj->data() + off : nullptr);
        requireSafe(a, r);
        o.L.insert(o.L.end(), r.L.begin(), r.L.end());
        o.R.insert(o.R.end(), r.R.begin(), r.R.end());
    }
    return o;
}

bool bitEqual(const Out& a, const Out& b) {
    return a.L.size() == b.L.size() && a.R.size() == b.R.size()
           && std::memcmp(a.L.data(), b.L.data(), a.L.size() * sizeof(float)) == 0
           && std::memcmp(a.R.data(), b.R.data(), a.R.size() * sizeof(float)) == 0;
}

double energy(const Out& o) {
    double e = 0.0;
    for (const float v : o.L)
        e += static_cast<double>(v) * static_cast<double>(v);
    return e;
}

/// One setter that takes a float: the bad value is passed where `control` would be.
struct CallCase {
    const char* name;
    AM mode;
    float ratio;
    std::function<void(AnchorOscillator&, float)> call;
    float control;
    bool isNoteOn;   // noteOn's "previous value" is the held f0: the reference calls noteOn(held f0)
};

}  // namespace

// =============================================================================
// SC-015, SC-007 non-finite rows; FR-054, FR-055; plan S1 C-11
// =============================================================================

TEST_CASE("AnchorOscillator_NonFiniteInputs", "[systems][profundum][anchor]") {
    const float c2 = PT::anchorMidiHz(36);
    const float c3 = PT::anchorMidiHz(48);
    const float e0 = PT::anchorMidiHz(16);
    const Hook armDefault = [c2](AnchorOscillator& a) { a.noteOn(c2); };
    const Hook midNothing = [](AnchorOscillator&) {};

    SECTION("non-finite setter and Controls inputs keep the previous value (memcmp-equal renders)") {
        std::vector<CallCase> cases;
        for (const ControlField& f : kFields) {
            float Controls::*m = f.member;
            const AM mode = f.mode;
            cases.push_back(CallCase{
                f.name, mode, 1.0f,
                [m, mode](AnchorOscillator& a, float v) {
                    Controls c{.mode = mode};
                    c.*m = v;
                    a.setControls(c);
                },
                f.control, false});
        }
        cases.push_back(CallCase{"noteOn", AM::Lock, 1.0f, [](AnchorOscillator& a, float v) { a.noteOn(v); }, c3,
                                 true});
        cases.push_back(CallCase{"setFrequency", AM::Lock, 1.0f,
                                 [](AnchorOscillator& a, float v) { a.setFrequency(v); }, c3, false});
        cases.push_back(CallCase{"setFollowRatio", AM::Follow, 1.5f,
                                 [](AnchorOscillator& a, float v) { a.setFollowRatio(v); }, 2.0f, false});
        cases.push_back(CallCase{"setCoreH1Amplitude", AM::Lock, 1.0f,
                                 [](AnchorOscillator& a, float v) { a.setCoreH1Amplitude(v); }, 0.1f, false});

        for (const CallCase& cc : cases) {
            INFO("input " << cc.name);
            const Setup setup{.controls = Controls{.mode = cc.mode}, .ratio = cc.ratio};
            const auto call = cc.call;
            // Reference: the run without the bad call. For noteOn the previous value is the held f0
            // (C2), so the reference issues noteOn(C2) at the same two call sites.
            const Hook& refArm = armDefault;
            const Hook& refMid = cc.isNoteOn ? armDefault : midNothing;

            const auto hooksFor = [&](float v, Hook& arm, Hook& mid) {
                if (cc.isNoteOn) {
                    arm = [call, v](AnchorOscillator& a) { call(a, v); };
                    mid = arm;
                } else {
                    arm = [call, v, c2](AnchorOscillator& a) {
                        call(a, v);
                        a.noteOn(c2);
                    };
                    mid = [call, v](AnchorOscillator& a) { call(a, v); };
                }
            };

            // Positive control: a finite in-range value at the same call sites changes the render.
            {
                const Out ref = runAnchor(setup, c2, refArm, refMid, kControlBlock);
                REQUIRE(energy(ref) > 0.0);
                Hook arm;
                Hook mid;
                hooksFor(cc.control, arm, mid);
                const Out ctl = runAnchor(setup, c2, arm, mid, kControlBlock);
                REQUIRE_FALSE(bitEqual(ref, ctl));
            }

            for (const std::size_t blk : kBlocks) {
                INFO("block " << blk);
                const Out ref = runAnchor(setup, c2, refArm, refMid, blk);
                for (const BadValue& b : kNonFinite) {
                    INFO("value " << b.name);
                    Hook arm;
                    Hook mid;
                    hooksFor(nonFiniteValue(b), arm, mid);
                    const Out bad = runAnchor(setup, c2, arm, mid, blk);
                    REQUIRE(bitEqual(ref, bad));
                }
            }
        }
    }

    SECTION("non-finite f0PerSample / followRatioPerSample elements hold the previous element") {
        std::vector<float> f0Ramp(kTotal);
        std::vector<float> rRamp(kTotal);
        for (std::size_t i = 0; i < kTotal; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(kTotal);
            f0Ramp[i] = static_cast<float>(static_cast<double>(c2) * (1.0 + 0.05 * t));
            rRamp[i] = static_cast<float>(1.5 + 0.1 * t);
        }
        // Grid sample mid-block, off-grid, last and first sample of a 64-block, the first sample
        // after a mid hook (4800) and after the Reset re-arm (9600), and the same offsets in the
        // second arm (9600 + 3216 = 12816). Every position > 0, so its predecessor is finite.
        constexpr std::array<std::size_t, 10> kPositions{3216, 3221, 3263, 3264, 4800,
                                                         9600, 12816, 12821, 12863, 12864};
        constexpr std::size_t kGridPosition = 3216;   // 3216 % 16 == 0: read at a grid sample

        struct TrajCase {
            const char* name;
            AM mode;
            float ratio;
            bool isRatio;
            float control;
        };
        const std::array<TrajCase, 2> trajCases{{
            {"f0PerSample", AM::Lock, 1.0f, false, c3},
            {"followRatioPerSample", AM::Follow, 1.5f, true, 3.0f},
        }};

        for (const TrajCase& tc : trajCases) {
            INFO("input " << tc.name);
            const std::vector<float>& ramp = tc.isRatio ? rRamp : f0Ramp;
            const auto setupFor = [&](const std::vector<float>& traj) {
                return Setup{.controls = Controls{.mode = tc.mode},
                             .ratio = tc.ratio,
                             .f0Traj = tc.isRatio ? nullptr : &traj,
                             .rTraj = tc.isRatio ? &traj : nullptr};
            };

            {   // Positive control: a finite element at a grid sample changes the render.
                const Out ref = runAnchor(setupFor(ramp), c2, armDefault, midNothing, kControlBlock);
                REQUIRE(energy(ref) > 0.0);
                std::vector<float> ctlTraj = ramp;
                ctlTraj[kGridPosition] = tc.control;
                const Out ctl = runAnchor(setupFor(ctlTraj), c2, armDefault, midNothing, kControlBlock);
                REQUIRE_FALSE(bitEqual(ref, ctl));
            }

            for (const std::size_t pos : kPositions) {
                INFO("element " << pos);
                std::vector<float> refTraj = ramp;
                // FR-005 per-element rule: hold the previous element. FR-005 mirrors the core, and the
                // core's applyNoteOn sets prevSampleF0_ = f (profundum_core.h:525, "a non-finite first
                // trajectory element holds the note's f0"), so for f0 the first element after an arming
                // noteOn (segment 2 start, 9600) holds the note's f0, c2. The ratio has no noteOn value:
                // applyNoteOn keeps the held ratio, which is the previous block's last element.
                const bool afterArm = pos % (2 * kQuarter) == 0;
                refTraj[pos] = (afterArm && !tc.isRatio) ? c2 : refTraj[pos - 1];
                for (const std::size_t blk : kBlocks) {
                    INFO("block " << blk);
                    const Out ref = runAnchor(setupFor(refTraj), c2, armDefault, midNothing, blk);
                    for (const BadValue& b : kNonFinite) {
                        INFO("value " << b.name);
                        std::vector<float> badTraj = ramp;
                        badTraj[pos] = nonFiniteValue(b);
                        const Out bad = runAnchor(setupFor(badTraj), c2, armDefault, midNothing, blk);
                        REQUIRE(bitEqual(ref, bad));
                    }
                }
            }
        }
    }

    SECTION("an out-of-range enum keeps the previous value (bitwise-equal render)") {
        // Fixed underlying type std::uint8_t: 7 and 9 are representable, only out of the enumerator range.
        const Controls badWave{.waveform = static_cast<AW>(7)};  // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
        const Controls badMode{.mode = static_cast<AM>(9)};      // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
        struct EnumCase {
            const char* name;
            Controls bad;
            Controls control;
        };
        const std::array<EnumCase, 2> enumCases{{
            {"Waveform(7)", badWave, Controls{.waveform = AW::Triangle}},
            {"Mode(9)", badMode, Controls{.mode = AM::Ghost}},
        }};
        const Setup setup{.controls = Controls{}};

        for (const EnumCase& ec : enumCases) {
            INFO("input " << ec.name);
            {   // read-back: the pending enum is unchanged
                AnchorOscillator a;
                a.prepare(kFs);
                a.setControls(ec.bad);
                const Controls p = a.pendingControls();
                REQUIRE(p.waveform == AW::Sine);
                REQUIRE(p.mode == AM::Lock);
            }
            const auto hooksFor = [c2](const Controls& c, Hook& arm, Hook& mid) {
                arm = [c, c2](AnchorOscillator& a) {
                    a.setControls(c);
                    a.noteOn(c2);
                };
                mid = [c](AnchorOscillator& a) { a.setControls(c); };
            };
            {   // Positive control: the in-range enum changes the render.
                const Out ref = runAnchor(setup, c2, armDefault, midNothing, kControlBlock);
                REQUIRE(energy(ref) > 0.0);
                Hook arm;
                Hook mid;
                hooksFor(ec.control, arm, mid);
                const Out ctl = runAnchor(setup, c2, arm, mid, kControlBlock);
                REQUIRE_FALSE(bitEqual(ref, ctl));
            }
            for (const std::size_t blk : kBlocks) {
                INFO("block " << blk);
                const Out ref = runAnchor(setup, c2, armDefault, midNothing, blk);
                Hook arm;
                Hook mid;
                hooksFor(ec.bad, arm, mid);
                const Out bad = runAnchor(setup, c2, arm, mid, blk);
                REQUIRE(bitEqual(ref, bad));
            }
        }
    }

    SECTION("Controls read-back through pendingControls() (plan S1 C-11)") {
        const Controls defaults{};
        for (const ControlField& f : kFields) {
            INFO("field " << f.name);
            // Non-finite: the pending field keeps its previous value, bitwise.
            for (const BadValue& b : kNonFinite) {
                INFO("value " << b.name);
                AnchorOscillator a;
                a.prepare(kFs);
                Controls c{};
                c.*(f.member) = nonFiniteValue(b);
                a.setControls(c);
                REQUIRE(bitsOf(a.pendingControls().*(f.member)) == bitsOf(defaults.*(f.member)));
            }
            // Denormal: flushed to 0.0, then clamped. level / punchBoostDb read +0.0f exactly;
            // punchTimeMs / ghostTimeMs read their minimum (5.0f / 1.0f).
            for (const BadValue& b : kDenormals) {
                INFO("value " << b.name);
                AnchorOscillator a;
                a.prepare(kFs);
                Controls c{.mode = f.mode};
                c.*(f.member) = denormalValue(b);
                a.setControls(c);
                const Controls p = a.pendingControls();
                REQUIRE(bitsOf(p.*(f.member)) == bitsOf(f.denormalReadBack));
                for (const ControlField& other : kFields) {   // the other fields are untouched
                    if (other.member != f.member)
                        REQUIRE(bitsOf(p.*(other.member)) == bitsOf(defaults.*(other.member)));
                }
                // The flushed value renders finite and bounded in the field's own mode.
                a.setCoreH1Amplitude(AnchorOscillator::kMaxCoreH1Amplitude);
                a.noteOn(c2);
                const PT::AnchorRender r = PT::renderAnchor(a, kQuarter, kControlBlock);
                requireSafe(a, r);
            }
        }
        // The 1e-40 pattern is what its name says (in double: no DAZ involved).
        REQUIRE(std::abs(std::ldexp(static_cast<double>(kDenormals[1].bits), -149) - 1e-40) < 1e-44);
    }

    SECTION("FR-055 bound arm: triangle, level 1, PUNCH +12 dB, a1 = kMaxCoreH1Amplitude") {
        constexpr std::array<double, 5> kRates{22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
        struct Note {
            const char* name;
            float hz;
        };
        const std::array<Note, 2> notes{{{"C2", c2}, {"E0", e0}}};
        // (k·a1/√2)·B·crest: the waveform-peak-meets-boost-peak product (plan S1 C-1).
        const double analytic = static_cast<double>(AnchorOscillator::kAnchorToH1)
                                * static_cast<double>(AnchorOscillator::kMaxCoreH1Amplitude) / std::sqrt(2.0)
                                * std::pow(10.0, static_cast<double>(AnchorOscillator::kMaxPunchBoostDb) / 20.0)
                                * kTriangleCrest;
        const Controls worst{.waveform = AW::Triangle,
                             .mode = AM::Punch,
                             .level = 1.0f,
                             .punchBoostDb = AnchorOscillator::kMaxPunchBoostDb,
                             .punchTimeMs = AnchorOscillator::kMaxPunchTimeMs};
        constexpr std::size_t kReArms = 8;

        for (const double fs : kRates) {
            for (const Note& note : notes) {
                INFO("fs " << fs << " note " << note.name);
                AnchorOscillator a;
                a.prepare(fs);
                a.setRetriggerPhase(ARP::Reset);
                a.setControls(worst);
                a.setCoreH1Amplitude(AnchorOscillator::kMaxCoreH1Amplitude);
                a.noteOn(note.hz);
                // 0.3 s per arm: longer than the 1 ms rise + 250 ms fall, so every re-arm starts at g = 1.
                const auto rest = static_cast<std::size_t>(std::llround(0.3 * fs));
                const double periodSamples = fs / static_cast<double>(note.hz);
                float maxAbs = 0.0f;
                const auto renderAndTrack = [&](std::size_t n) {
                    const PT::AnchorRender r = PT::renderAnchor(a, n, kControlBlock);
                    requireSafe(a, r);
                    for (const float v : r.L)
                        maxAbs = std::max(maxAbs, std::abs(v));
                };
                renderAndTrack(rest);
                // FreeRunning re-arms at eighth-period offsets: the boost peak lands at eight
                // different waveform phases (Hermite bridge from (1, 0) to B, then the fall).
                a.setRetriggerPhase(ARP::FreeRunning);
                for (std::size_t k = 0; k < kReArms; ++k) {
                    a.noteOn(note.hz);
                    const auto shift =
                        static_cast<std::size_t>(std::llround(periodSamples * static_cast<double>(k) / 8.0));
                    renderAndTrack(rest + shift);
                }
                std::printf("SC-015 bound arm: fs %6.0f Hz %s  max|y| %.6f  analytic worst %.6f  ceiling %.2f\n",
                            fs, note.name, static_cast<double>(maxAbs), analytic,
                            static_cast<double>(AnchorOscillator::kOutputCeiling));
                REQUIRE(maxAbs > 0.0f);
                REQUIRE(maxAbs <= AnchorOscillator::kOutputCeiling);
            }
        }
    }
}
