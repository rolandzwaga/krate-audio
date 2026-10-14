// ==============================================================================
// Layer 2: Processor Tests (-fno-fast-math) - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// Reference: specs/profundum-phase4-drop-note-behaviour/plan.md (S11.1)
//            specs/profundum-phase4-drop-note-behaviour/tasks.md (T006 wires this TU, T017 fills it)
//
// Holds: SC-017 (FR-003, FR-009, E-14; tasks.md deviation 7). Listed in the -fno-fast-math block.
//   DropEnvelope_NonFinite          setters keep the previous value on NaN / +-Inf, subnormal ->
//                                   +0 then clamp, prepare(non-finite) is a no-op.
//   DropPhaseLock_NonFinite         non-finite / non-positive trigger arguments are ignored;
//                                   process(non-finite) returns 0.0f, the mirror stays finite.
//   RetriggerDeclick_NonFinite      prepare(NaN / +-Inf) is a no-op.
//   MonoHandler_GlideModeNonFinite  setPortamentoTime(NaN / +-Inf) keeps the glide time in every
//                                   glide law; out-of-enum setGlideMode is ignored.
//   DropNoteBehaviour_CornerRenders 10 s renders at every parameter corner: every d, d_c and gain
//                                   sample finite and not subnormal.
//
// Every special value is built from its bit pattern (std::bit_cast), never from std::numeric_limits
// arithmetic, so the patterns are exactly the ones the task names.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/drop_curve.h>
#include <krate/dsp/primitives/retrigger_declick.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/drop_phase_lock.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/processors/mono_handler.h>

#include "drop_c1_checker.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using Krate::DSP::DropCurve;
using Krate::DSP::DropEnvelope;
using Krate::DSP::DropPhaseLock;
using Krate::DSP::GlideMode;
using Krate::DSP::MonoHandler;
using Krate::DSP::RetriggerDeclick;
using Krate::DSP::DropTest::dropCurveBitEqual;

namespace {

using Kind = DropPhaseLock::TriggerKind;

constexpr float kNaN = std::bit_cast<float>(0x7FC00000u);
constexpr float kPosInf = std::bit_cast<float>(0x7F800000u);
constexpr float kNegInf = std::bit_cast<float>(0xFF800000u);
constexpr float kMinSub = std::bit_cast<float>(0x00000001u);     // smallest positive subnormal
constexpr float kMaxSub = std::bit_cast<float>(0x007FFFFFu);     // largest positive subnormal
constexpr float kMaxNegSub = std::bit_cast<float>(0x807FFFFFu);  // largest-magnitude negative subnormal

constexpr std::array<float, 3> kNonFinite{kNaN, kPosInf, kNegInf};
constexpr std::array<float, 3> kSubnormals{kMinSub, kMaxSub, kMaxNegSub};

constexpr double kNaND = std::bit_cast<double>(0x7FF8000000000000ULL);
constexpr double kPosInfD = std::bit_cast<double>(0x7FF0000000000000ULL);
constexpr double kNegInfD = std::bit_cast<double>(0xFFF0000000000000ULL);
constexpr std::array<double, 3> kNonFiniteRates{kNaND, kPosInfD, kNegInfD};

constexpr double kFs = 48000.0;
constexpr float kMinF0 = 8.0f;  // == ProfundumCore::kMinF0Hz (as in drop_phase_lock_test.cpp)
constexpr std::size_t kU = 16;  // == ProfundumCore pitch-update interval (C-14)

std::uint32_t bitsOf(float x) { return std::bit_cast<std::uint32_t>(x); }
bool bitEqual(float a, float b) { return bitsOf(a) == bitsOf(b); }
bool bitZero(float x) { return bitsOf(x) == 0u; }
/// Exponent bits 0 and mantissa non-zero: a subnormal (±0 is not).
bool isSubnormal(float x) {
    const std::uint32_t u = bitsOf(x);
    return (u & 0x7F800000u) == 0u && (u & 0x007FFFFFu) != 0u;
}
bool isFiniteF(float x) { return Krate::DSP::detail::isFinite(x); }
bool isFiniteD(double x) { return Krate::DSP::detail::isFinite(x); }

double midiHz(int note) { return 440.0 * std::exp2((static_cast<double>(note) - 69.0) / 12.0); }
constexpr int kE0 = 16;
constexpr int kE1 = 28;

DropEnvelope makeEnv(float amount, float timeMs, float shape, float rebound) {
    DropEnvelope env;
    env.prepare(kFs);
    env.setAmount(amount);
    env.setTime(timeMs);
    env.setShape(shape);
    env.setRebound(rebound);
    return env;
}

/// 0 = Amount, 1 = Time, 2 = Shape, 3 = Rebound.
void applySetter(DropEnvelope& env, int setter, float v) {
    switch (setter) {
        case 0: env.setAmount(v); break;
        case 1: env.setTime(v); break;
        case 2: env.setShape(v); break;
        default: env.setRebound(v); break;
    }
}

std::size_t curveLength(const DropCurve& c) { return static_cast<std::size_t>(std::ceil(c.endSample)); }

}  // namespace

// ------------------------------------------------------------------------------
// SC-017 / FR-003: DropEnvelope setters and prepare
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_NonFinite", "[processors][profundum][drop-nonfinite]") {
    SECTION("NaN / +-Inf keeps the previous value (next trigger's plannedCurve equals the reference)") {
        for (const float bad : kNonFinite) {
            for (int setter = 0; setter < 5; ++setter) {  // 4 = all four setters at once
                for (const bool snap : {true, false}) {
                    CAPTURE(bitsOf(bad), setter, snap);
                    DropEnvelope ref = makeEnv(24.0f, 200.0f, 0.5f, 0.5f);
                    DropEnvelope env = makeEnv(24.0f, 200.0f, 0.5f, 0.5f);
                    ref.trigger(true);
                    env.trigger(true);
                    for (int n = 0; n < 500; ++n)
                        REQUIRE(bitEqual(env.process(), ref.process()));
                    if (setter < 4) {
                        applySetter(env, setter, bad);
                    } else {
                        for (int s = 0; s < 4; ++s)
                            applySetter(env, s, bad);
                    }
                    ref.trigger(snap);
                    env.trigger(snap);
                    CHECK(dropCurveBitEqual(env.plannedCurve(), ref.plannedCurve()));
                    const std::size_t len = curveLength(ref.plannedCurve()) + 32;
                    std::size_t mismatches = 0;
                    for (std::size_t n = 0; n < len; ++n) {
                        const float a = env.process();
                        const float b = ref.process();
                        if (!bitEqual(a, b) || !isFiniteF(a))
                            ++mismatches;
                    }
                    CHECK(mismatches == 0);
                    CHECK_FALSE(env.isActive());
                }
            }
        }
    }

    SECTION("subnormal Amount -> 0: d is exactly +0.0f on every sample after a trigger (E-1)") {
        for (const float sub : kSubnormals) {
            for (const float s : {0.0f, 0.5f, 1.0f}) {
                for (const float R : {0.0f, 1.0f}) {
                    CAPTURE(bitsOf(sub), s, R);
                    DropEnvelope env = makeEnv(24.0f, 200.0f, s, R);
                    env.setAmount(sub);
                    bool allZero = true;
                    const auto run = [&](std::size_t count) {
                        for (std::size_t i = 0; i < count; ++i)
                            allZero = allZero && bitZero(env.process());
                    };
                    env.trigger(true);
                    CHECK(std::bit_cast<std::uint64_t>(env.plannedCurve().amount) == 0u);
                    run(5);
                    env.trigger(false);  // bridged mid-"drop"
                    run(curveLength(env.plannedCurve()) + 32);
                    env.trigger(false);  // bridged from idle
                    run(curveLength(env.plannedCurve()) + 32);
                    CHECK(allZero);
                    CHECK_FALSE(env.isActive());
                }
            }
        }
    }

    SECTION("subnormal Shape / Rebound -> exactly +0, subnormal Time -> 1 ms") {
        for (const float sub : kSubnormals) {
            for (const bool snap : {true, false}) {
                CAPTURE(bitsOf(sub), snap);
                {
                    DropEnvelope ref = makeEnv(24.0f, 200.0f, 0.75f, 0.5f);
                    DropEnvelope env = makeEnv(24.0f, 200.0f, 0.75f, 0.5f);
                    ref.setShape(0.0f);
                    env.setShape(sub);
                    ref.trigger(snap);
                    env.trigger(snap);
                    CHECK(dropCurveBitEqual(env.plannedCurve(), ref.plannedCurve()));
                    CHECK(std::bit_cast<std::uint64_t>(env.plannedCurve().blendW) == 0u);
                }
                {
                    DropEnvelope ref = makeEnv(24.0f, 200.0f, 0.5f, 0.75f);
                    DropEnvelope env = makeEnv(24.0f, 200.0f, 0.5f, 0.75f);
                    ref.setRebound(0.0f);
                    env.setRebound(sub);
                    ref.trigger(snap);
                    env.trigger(snap);
                    CHECK(dropCurveBitEqual(env.plannedCurve(), ref.plannedCurve()));
                    CHECK(std::bit_cast<std::uint64_t>(env.plannedCurve().reboundDepth) == 0u);
                }
                {
                    DropEnvelope ref = makeEnv(24.0f, 200.0f, 0.5f, 0.5f);
                    DropEnvelope env = makeEnv(24.0f, 200.0f, 0.5f, 0.5f);
                    ref.setTime(DropEnvelope::kMinTimeMs);
                    env.setTime(sub);
                    ref.trigger(snap);
                    env.trigger(snap);
                    CHECK(dropCurveBitEqual(env.plannedCurve(), ref.plannedCurve()));
                }
            }
        }
    }

    SECTION("out-of-range input clamps") {
        struct Case {
            int setter;
            float in;
            float clamped;
        };
        const std::array<Case, 8> cases{{{0, -5.0f, 0.0f},
                                         {0, 1.0e9f, DropEnvelope::kMaxAmountSemitones},
                                         {1, 0.0f, DropEnvelope::kMinTimeMs},
                                         {1, 1.0e9f, DropEnvelope::kMaxTimeMs},
                                         {2, -1.0f, 0.0f},
                                         {2, 2.0f, 1.0f},
                                         {3, -1.0f, 0.0f},
                                         {3, 2.0f, 1.0f}}};
        for (const Case& c : cases) {
            CAPTURE(c.setter, c.in);
            DropEnvelope ref = makeEnv(24.0f, 200.0f, 0.5f, 0.5f);
            DropEnvelope env = makeEnv(24.0f, 200.0f, 0.5f, 0.5f);
            applySetter(ref, c.setter, c.clamped);
            applySetter(env, c.setter, c.in);
            ref.trigger(true);
            env.trigger(true);
            CHECK(dropCurveBitEqual(env.plannedCurve(), ref.plannedCurve()));
        }
    }

    SECTION("prepare(non-finite) is a no-op (deviation 7)") {
        // Unprepared stays unprepared: setters, trigger and process stay inert.
        {
            DropEnvelope env;
            for (const double r : kNonFiniteRates)
                env.prepare(r);
            env.setAmount(24.0f);
            env.trigger(true);
            bool allZero = true;
            for (int n = 0; n < 1000; ++n)
                allZero = allZero && bitZero(env.process());
            CHECK(allZero);
            CHECK_FALSE(env.isActive());
        }
        // Prepared: a running drop continues bit-identically (no reset, rate unchanged).
        {
            DropEnvelope ref = makeEnv(24.0f, 200.0f, 1.0f, 1.0f);
            DropEnvelope env = makeEnv(24.0f, 200.0f, 1.0f, 1.0f);
            ref.trigger(true);
            env.trigger(true);
            for (int n = 0; n < 300; ++n)
                REQUIRE(bitEqual(env.process(), ref.process()));
            for (const double r : kNonFiniteRates)
                env.prepare(r);
            CHECK(env.isActive());
            const std::size_t len = curveLength(ref.plannedCurve()) + 32;
            std::size_t mismatches = 0;
            for (std::size_t n = 0; n < len; ++n) {
                if (!bitEqual(env.process(), ref.process()))
                    ++mismatches;
            }
            CHECK(mismatches == 0);
            // The next trigger plans at the unchanged rate.
            ref.trigger(true);
            env.trigger(true);
            CHECK(dropCurveBitEqual(env.plannedCurve(), ref.plannedCurve()));
        }
    }
}

// ------------------------------------------------------------------------------
// Deviation 7: DropPhaseLock trigger arguments, process(non-finite), prepare(non-finite)
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_NonFinite", "[processors][profundum][drop-nonfinite]") {
    const float fRes = static_cast<float>(midiHz(kE1));
    const float cap = static_cast<float>(0.4 * kFs);
    DropEnvelope env = makeEnv(24.0f, 60.0f, 0.0f, 1.0f);
    env.trigger(true);
    const DropCurve curve = env.plannedCurve();
    std::vector<float> d(curveLength(curve) + 2048);
    for (auto& v : d)
        v = env.process();

    SECTION("non-finite or non-positive fRes / minF0 / cap triggers are ignored") {
        DropPhaseLock ref;
        ref.prepare(kFs);
        ref.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
        DropPhaseLock lock;
        lock.prepare(kFs);
        lock.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
        const std::size_t split = 200;
        for (std::size_t n = 0; n < split; ++n)
            REQUIRE(bitEqual(lock.process(d[n]), ref.process(d[n])));
        const float negZero = std::bit_cast<float>(0x80000000u);
        for (const float bad : {kNaN, kPosInf, kNegInf, 0.0f, negZero, -1.0f, kMaxNegSub}) {
            for (const Kind kind : {Kind::SnapReset, Kind::SnapFreeRunning, Kind::Bridged}) {
                lock.trigger(curve, bad, 0, kMinF0, cap, kind);
                lock.trigger(curve, fRes, 0, bad, cap, kind);
                lock.trigger(curve, fRes, 0, kMinF0, bad, kind);
            }
        }
        std::size_t mismatches = 0;
        for (std::size_t n = split; n < d.size(); ++n) {
            if (!bitEqual(lock.process(d[n]), ref.process(d[n])))
                ++mismatches;
        }
        CHECK(mismatches == 0);
        CHECK(std::bit_cast<std::uint64_t>(lock.correctionGamma()) == std::bit_cast<std::uint64_t>(ref.correctionGamma()));
        CHECK(std::bit_cast<std::uint64_t>(lock.accumulatedExcessCycles()) ==
              std::bit_cast<std::uint64_t>(ref.accumulatedExcessCycles()));
    }

    SECTION("process(non-finite) returns +0.0f; the plan advances and the accumulator stays finite") {
        DropPhaseLock ref;
        ref.prepare(kFs);
        ref.trigger(curve, fRes, 3, kMinF0, cap, Kind::SnapReset);
        DropPhaseLock lock;
        lock.prepare(kFs);
        lock.trigger(curve, fRes, 3, kMinF0, cap, Kind::SnapReset);
        std::size_t badOut = 0;
        std::size_t mismatches = 0;
        std::size_t nonFiniteAcc = 0;
        for (std::size_t n = 0; n < d.size(); ++n) {
            const bool inject = (n % 7) == 0 || (n >= 1000 && n < 1100);  // incl. the trigger sample n = 0
            const float x = inject ? kNonFinite[n % kNonFinite.size()] : d[n];
            const float y = lock.process(x);
            const float yRef = ref.process(d[n]);
            if (inject) {
                if (!bitZero(y))
                    ++badOut;
            } else if (!bitEqual(y, yRef)) {
                ++mismatches;  // the correction is input-independent: same d_c on every finite sample
            }
            if (!isFiniteD(lock.accumulatedExcessCycles()))
                ++nonFiniteAcc;
        }
        CHECK(badOut == 0);
        CHECK(mismatches == 0);
        CHECK(nonFiniteAcc == 0);
    }

    SECTION("processBlock: non-finite inputs -> +0.0f, finite inputs equal process()") {
        DropPhaseLock ref;
        ref.prepare(kFs);
        ref.trigger(curve, fRes, 0, kMinF0, cap, Kind::Bridged);
        DropPhaseLock lock;
        lock.prepare(kFs);
        lock.trigger(curve, fRes, 0, kMinF0, cap, Kind::Bridged);
        std::vector<float> in(d);
        for (std::size_t n = 0; n < in.size(); n += 5)
            in[n] = kNonFinite[(n / 5) % kNonFinite.size()];
        std::vector<float> out(in.size());
        lock.processBlock(in.data(), out.data(), in.size());
        std::size_t mismatches = 0;
        for (std::size_t n = 0; n < in.size(); ++n) {
            const float yRef = ref.process(in[n]);
            if (!bitEqual(out[n], yRef))
                ++mismatches;
            if (n % 5 == 0 && !bitZero(out[n]))
                ++mismatches;
        }
        CHECK(mismatches == 0);
        CHECK(isFiniteD(lock.accumulatedExcessCycles()));
    }

    SECTION("no plan: process(non-finite) returns +0.0f") {
        DropPhaseLock lock;
        lock.prepare(kFs);
        for (const float bad : kNonFinite)
            CHECK(bitZero(lock.process(bad)));
        CHECK(lock.accumulatedExcessCycles() == 0.0);
    }

    SECTION("subnormal trigger arguments: outputs and accumulator stay finite") {
        DropPhaseLock lock;
        lock.prepare(kFs);
        lock.trigger(curve, kMinSub, 0, kMinF0, cap, Kind::SnapReset);
        std::size_t bad = 0;
        for (const float v : d) {
            const float y = lock.process(v);
            if (!isFiniteF(y) || isSubnormal(y))
                ++bad;
        }
        lock.trigger(curve, fRes, 0, kMaxSub, cap, Kind::SnapReset);
        for (const float v : d) {
            const float y = lock.process(v);
            if (!isFiniteF(y) || isSubnormal(y))
                ++bad;
        }
        CHECK(bad == 0);
        CHECK(isFiniteD(lock.accumulatedExcessCycles()));
        CHECK(isFiniteD(lock.correctionGamma()));
    }

    SECTION("prepare(non-finite) is a no-op (deviation 7)") {
        DropPhaseLock ref;
        ref.prepare(kFs);
        ref.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
        DropPhaseLock lock;
        lock.prepare(kFs);
        lock.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
        for (std::size_t n = 0; n < 100; ++n)
            REQUIRE(bitEqual(lock.process(d[n]), ref.process(d[n])));
        for (const double r : kNonFiniteRates)
            lock.prepare(r);
        std::size_t mismatches = 0;
        for (std::size_t n = 100; n < d.size(); ++n) {
            if (!bitEqual(lock.process(d[n]), ref.process(d[n])))
                ++mismatches;
        }
        CHECK(mismatches == 0);
        CHECK(std::bit_cast<std::uint64_t>(lock.accumulatedExcessCycles()) ==
              std::bit_cast<std::uint64_t>(ref.accumulatedExcessCycles()));
    }
}

// ------------------------------------------------------------------------------
// Deviation 7: RetriggerDeclick prepare(non-finite)
// ------------------------------------------------------------------------------
TEST_CASE("RetriggerDeclick_NonFinite", "[processors][profundum][drop-nonfinite]") {
    SECTION("unprepared stays unprepared") {
        RetriggerDeclick dec;
        for (const double r : kNonFiniteRates)
            dec.prepare(r);
        dec.arm();
        CHECK_FALSE(dec.isActive());
        bool allOne = true;
        for (int n = 0; n < 1000; ++n)
            allOne = allOne && bitEqual(dec.process(), 1.0f);
        CHECK(allOne);
    }

    SECTION("prepared: the dip length and a running dip are unchanged") {
        constexpr std::size_t kLd = 72;  // ceil(1.5 * 48000 / 1000)
        RetriggerDeclick ref;
        ref.prepare(kFs);
        RetriggerDeclick dec;
        dec.prepare(kFs);
        for (const double r : kNonFiniteRates)
            dec.prepare(r);
        ref.arm();
        dec.arm();
        CHECK(dec.samplesUntilSwitch() == kLd - 1);
        for (int n = 0; n < 30; ++n)
            REQUIRE(bitEqual(dec.process(), ref.process()));
        for (const double r : kNonFiniteRates)
            dec.prepare(r);  // mid-dip: no reset
        CHECK(dec.isActive());
        CHECK(dec.samplesUntilSwitch() == ref.samplesUntilSwitch());
        std::size_t mismatches = 0;
        for (int n = 0; n < 200; ++n) {
            if (!bitEqual(dec.process(), ref.process()))
                ++mismatches;
        }
        CHECK(mismatches == 0);
        CHECK_FALSE(dec.isActive());
    }
}

// ------------------------------------------------------------------------------
// SC-017 / E-14: MonoHandler glide API
// ------------------------------------------------------------------------------
TEST_CASE("MonoHandler_GlideModeNonFinite", "[processors][profundum][drop-nonfinite]") {
    constexpr std::size_t kRender = 2 * 48000;  // 24 st at 500 ms (per octave in ConstantRate) <= 1 s
    const auto configured = [](GlideMode mode) {
        MonoHandler h;
        h.prepare(kFs);
        h.setGlideMode(mode);
        h.setPortamentoTime(500.0f);
        return h;
    };
    /// Samples until the frequency first equals its final value (the glide's duration).
    const auto glideDuration = [](const std::vector<float>& f) {
        const float last = f.back();
        std::size_t k = f.size();
        while (k > 0 && bitEqual(f[k - 1], last))
            --k;
        return k;
    };

    SECTION("setPortamentoTime(NaN / +-Inf) keeps the previous time: trace and glide duration unchanged") {
        for (const GlideMode mode : {GlideMode::ConstantTime, GlideMode::ConstantRate, GlideMode::Quantized}) {
            for (const float bad : kNonFinite) {
                for (const bool midGlide : {false, true}) {
                    CAPTURE(static_cast<int>(mode), bitsOf(bad), midGlide);
                    MonoHandler ref = configured(mode);
                    MonoHandler h = configured(mode);
                    if (!midGlide)
                        h.setPortamentoTime(bad);
                    (void)ref.noteOn(36, 100);
                    (void)h.noteOn(36, 100);
                    for (int n = 0; n < 64; ++n)
                        REQUIRE(bitEqual(h.processPortamento(), ref.processPortamento()));
                    (void)ref.noteOn(60, 100);
                    (void)h.noteOn(60, 100);
                    std::vector<float> fRef(kRender);
                    std::vector<float> fH(kRender);
                    for (std::size_t n = 0; n < kRender; ++n) {
                        if (midGlide && n == 2000)
                            h.setPortamentoTime(bad);
                        fRef[n] = ref.processPortamento();
                        fH[n] = h.processPortamento();
                    }
                    std::size_t mismatches = 0;
                    std::size_t nonFinite = 0;
                    for (std::size_t n = 0; n < kRender; ++n) {
                        if (!bitEqual(fH[n], fRef[n]))
                            ++mismatches;
                        if (!isFiniteF(fH[n]))
                            ++nonFinite;
                    }
                    CHECK(mismatches == 0);
                    CHECK(nonFinite == 0);
                    const std::size_t durRef = glideDuration(fRef);
                    CHECK(durRef > 2000);  // a real glide, longer than the mid-glide injection point
                    CHECK(glideDuration(fH) == durRef);
                }
            }
        }
    }

    SECTION("out-of-enum setGlideMode is ignored") {
        for (const GlideMode mode : {GlideMode::ConstantTime, GlideMode::ConstantRate, GlideMode::Quantized}) {
            for (const std::uint8_t raw : {std::uint8_t{3}, std::uint8_t{4}, std::uint8_t{255}}) {
                CAPTURE(static_cast<int>(mode), static_cast<int>(raw));
                MonoHandler ref = configured(mode);
                MonoHandler h = configured(mode);
                (void)ref.noteOn(36, 100);
                (void)h.noteOn(36, 100);
                (void)ref.noteOn(60, 100);
                (void)h.noteOn(60, 100);
                std::size_t mismatches = 0;
                for (std::size_t n = 0; n < kRender; ++n) {
                    if (n == 1000)
                        h.setGlideMode(static_cast<GlideMode>(raw));
                    if (!bitEqual(h.processPortamento(), ref.processPortamento()))
                        ++mismatches;
                }
                CHECK(h.glideMode() == mode);
                CHECK(mismatches == 0);
            }
        }
    }
}

// ------------------------------------------------------------------------------
// SC-017 / FR-009: 10 s corner renders, every output finite and never subnormal
// ------------------------------------------------------------------------------
TEST_CASE("DropNoteBehaviour_CornerRenders", "[processors][profundum][drop-nonfinite]") {
    constexpr std::size_t kLen = 10 * 48000;   // 10 s at 48 kHz
    constexpr std::size_t kRetrigger = 1776;   // 37 ms
    constexpr std::size_t kArm = 2400;         // 50 ms
    const float cap = static_cast<float>(0.4 * kFs);

    for (const float A : {0.0f, 48.0f}) {
        for (const float T : {1.0f, 4000.0f}) {
            for (const float s : {0.0f, 1.0f}) {
                for (const float R : {0.0f, 1.0f}) {
                    for (const double fResD : {midiHz(kE0), 2000.0}) {
                        const auto fRes = static_cast<float>(fResD);
                        CAPTURE(A, T, s, R, fRes);
                        DropEnvelope env = makeEnv(A, T, s, R);
                        DropPhaseLock lock;
                        lock.prepare(kFs);
                        RetriggerDeclick dec;
                        dec.prepare(kFs);

                        std::size_t badD = 0, badDc = 0, badGain = 0;
                        std::size_t firstBad = kLen;
                        bool snap = true;
                        for (std::size_t n = 0; n < kLen; ++n) {
                            if (n % kRetrigger == 0) {
                                env.trigger(snap);
                                const std::size_t gridPhase = (kU - n % kU) % kU;
                                lock.trigger(env.plannedCurve(), fRes, gridPhase, kMinF0, cap,
                                             snap ? Kind::SnapReset : Kind::Bridged);
                                snap = !snap;
                            }
                            if (n % kArm == 0)
                                dec.arm();
                            const float d = env.process();
                            const float dc = lock.process(d);
                            const float g = dec.process();
                            const bool dBad = !isFiniteF(d) || isSubnormal(d);
                            const bool dcBad = !isFiniteF(dc) || isSubnormal(dc);
                            const bool gBad = !isFiniteF(g) || isSubnormal(g);
                            badD += dBad ? 1u : 0u;
                            badDc += dcBad ? 1u : 0u;
                            badGain += gBad ? 1u : 0u;
                            if ((dBad || dcBad || gBad) && firstBad == kLen)
                                firstBad = n;
                        }
                        CAPTURE(firstBad);
                        CHECK(badD == 0);
                        CHECK(badDc == 0);
                        CHECK(badGain == 0);
                        CHECK(isFiniteD(lock.accumulatedExcessCycles()));
                        CHECK(isFiniteD(lock.correctionGamma()));
                    }
                }
            }
        }
    }
}
