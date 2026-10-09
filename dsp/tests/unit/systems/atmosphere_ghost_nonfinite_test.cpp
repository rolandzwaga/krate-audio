// ==============================================================================
// Layer 3: System Tests - AtmosphereEngine ghost extension, the ONE non-finite TU
//          (specs/vorago-phase10a-ghost-extension)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10a-ghost-extension/spec.md
//            specs/vorago-phase10a-ghost-extension/plan.md   (S3.3 non-finiteness)
//            specs/vorago-phase10a-ghost-extension/tasks.md  (T003 creates this
//            stub; T006 fills it)
//
// SCOPE OF THIS TU (tasks.md T003's table): SC-008 (b) and nothing else.
//
// THIS IS THE ONLY Phase 10a TU THAT INJECTS NaN/+-Inf BIT PATTERNS, and
// therefore the ONLY one listed in the -fno-fast-math opt-in block of
// dsp/tests/CMakeLists.txt. That split is load-bearing in both directions:
//   - values are built from bit patterns laundered through a volatile sink,
//     never from std::numeric_limits<float>::quiet_NaN()/infinity(), which fold
//     to finite garbage on the -ffast-math legs;
//   - the other four Phase 10a TUs stay OUT of that block so the header's
//     ITERUM_NOINLINE isFinite guards are proved in the /fp:fast + -ffast-math
//     mode AtmosphereEngine actually ships in (the
//     dsp/tests/CMakeLists.txt:885-895 precedent for Seraphis Phase 5), and so
//     atmosphere_ghost_perf_test.cpp's figures are not moved by the flag.
// std::isnan / std::isinf / std::isfinite appear NOWHERE in this phase.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "atmosphere_ghost_fixtures.h"

#include <krate/dsp/systems/atmosphere_engine.h>

#include <cstdint>
#include <cstring>

namespace {

// =============================================================================
// Non-finite construction, by bit pattern only
// =============================================================================

/// The three bit patterns tasks.md T006 names, kept as constants rather than
/// literals at the call sites so the pattern -> meaning mapping is stated once.
/// Identical to the Seraphis Phase 5 TU's set
/// (atmosphere_engine_nonfinite_test.cpp:145-147), deliberately: one phase must
/// not invent a second spelling of "quiet NaN".
constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// @brief Build a non-finite float from its bit pattern, through a volatile sink.
///
/// NEVER std::numeric_limits<float>::quiet_NaN() / infinity(): under
/// -ffast-math / -ffinite-math-only those fold to finite garbage, and although
/// THIS TU carries -fno-fast-math -fno-finite-math-only (FR-042, the single
/// Phase 10a entry in dsp/tests/CMakeLists.txt's opt-in block), the value has to
/// be constructed the one way that reads correctly under either flag set. The
/// volatile READ is the sink that defeats constant folding; the memcpy is the
/// only well-defined float <- bits reinterpretation.
///
/// This is the same helper shape as
/// dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp:160-166, in its
/// own anonymous namespace (internal linkage, so no ODR question arises between
/// the two TUs of dsp_systems_tests).
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;        // defeats constant folding
    const std::uint32_t materialized = b;   // the volatile READ is the sink
    float value = 0.0f;
    std::memcpy(&value, &materialized, sizeof(value));
    return value;
}

/// Prepare geometry for this TU. SC-008 (b) is a pure control-surface contract -
/// nothing is rendered here - so the cheapest legal geometry is used rather than
/// the Vorago ghost operating point: captureSeconds at the kMinCaptureSeconds
/// floor (atmosphere_engine.h:317) and both spectral stages off, so prepare()
/// allocates a 65 536-sample ring and no FFT state.
[[nodiscard]] Krate::DSP::AtmosphereEngine::PrepareConfig minimalPrepareConfig() noexcept {
    return Krate::DSP::AtmosphereEngine::PrepareConfig{.captureSeconds = 1.0f,
                                                       .blurEnabled = false,
                                                       .freezeEnabled = false,
                                                       .blurFftSize = 1024,
                                                       .freezeFftSize = 2048,
                                                       .maxBlockSamples = 512};
}

}  // namespace

// =============================================================================
// SC-008 (b) - the setter contract for setGrainReverseProbability (FR-003)
// =============================================================================
//
// THE ASSERTION WITH TEETH IS THE SUBSTITUTED VALUE, NOT MERELY "FINITE".
// A non-finite argument is SUBSTITUTED WITH THE CONTROL'S DEFAULT, 0.0f - it is
// NOT a no-op that retains the previous value. That is the shipped
// `isFinite(x) ? x : <default>` shape this component uses for every other
// control (setGrainSeconds atmosphere_engine.h:815-818, setDensity :828-831,
// setDecorrelation :902-905, setLevel :982-986), and FR-003 states explicitly
// that it differs BY DESIGN from Phase 10's FR-069 MacroMatrix rule, which
// retains the previous value on a non-finite write. Writing 1.0f first and
// requiring 0.0f back is what separates the two behaviours; a bare
// "the result is finite" check would pass under either.
//
// NON-VACUOUSNESS: every injected value is first put through
// VoragoGhostFix::isNonFiniteBits (atmosphere_ghost_fixtures.h:352-354), an
// INTEGER test on the exponent field, so a launder that had been folded away
// would fail the case here instead of silently turning the injection into a
// write of some finite number.
TEST_CASE("AtmosphereGhost_NonFiniteSetter", "[atmosphere][ghost][nonfinite]") {
    Krate::DSP::AtmosphereEngine engine;

    // --- FR-002: the default is 0.0f, i.e. every grain is forward, i.e. the
    //     pre-change behaviour - before prepare() and after it. prepare() ends
    //     with reset() (:400-402) and reset() does not touch a control value,
    //     so both readings are the same requirement stated at both lifecycle
    //     points.
    REQUIRE(engine.getGrainReverseProbability() == 0.0f);

    engine.prepare(48000.0, minimalPrepareConfig());
    REQUIRE(engine.getGrainReverseProbability() == 0.0f);

    SECTION("non-finite arguments substitute the control default, not the previous value") {
        const float quietNaN = makeNonFinite(kQuietNaNBits);
        const float posInf = makeNonFinite(kPosInfBits);
        const float negInf = makeNonFinite(kNegInfBits);

        // The injections are real: if the volatile launder were folded, these
        // would be finite and the substitutions below would prove nothing.
        REQUIRE(VoragoGhostFix::isNonFiniteBits(quietNaN));
        REQUIRE(VoragoGhostFix::isNonFiniteBits(posInf));
        REQUIRE(VoragoGhostFix::isNonFiniteBits(negInf));

        // quiet NaN
        engine.setGrainReverseProbability(1.0f);
        REQUIRE(engine.getGrainReverseProbability() == 1.0f);  // the value that must NOT survive
        engine.setGrainReverseProbability(quietNaN);
        REQUIRE(engine.getGrainReverseProbability() == 0.0f);

        // +Inf
        engine.setGrainReverseProbability(1.0f);
        REQUIRE(engine.getGrainReverseProbability() == 1.0f);
        engine.setGrainReverseProbability(posInf);
        REQUIRE(engine.getGrainReverseProbability() == 0.0f);

        // -Inf
        engine.setGrainReverseProbability(1.0f);
        REQUIRE(engine.getGrainReverseProbability() == 1.0f);
        engine.setGrainReverseProbability(negInf);
        REQUIRE(engine.getGrainReverseProbability() == 0.0f);
    }

    SECTION("out-of-range arguments clamp to [0, 1] and the getter reports the clamp") {
        engine.setGrainReverseProbability(-1.0f);
        REQUIRE(engine.getGrainReverseProbability() == 0.0f);

        engine.setGrainReverseProbability(2.0f);
        REQUIRE(engine.getGrainReverseProbability() == 1.0f);

        // The two endpoints themselves are IN range and must pass through
        // untouched - otherwise "clamps to [0, 1]" would also be satisfied by a
        // setter that clamped to [0, 0] or ignored its argument entirely.
        engine.setGrainReverseProbability(0.0f);
        REQUIRE(engine.getGrainReverseProbability() == 0.0f);

        engine.setGrainReverseProbability(1.0f);
        REQUIRE(engine.getGrainReverseProbability() == 1.0f);
    }
}
