// ==============================================================================
// Layer 3: System Tests - ProfundumCore, main TU
//                                    (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/profundum-phase1-harmonic-core/spec.md
//            specs/profundum-phase1-harmonic-core/plan.md  (S4, S8.1-S8.3)
//            specs/profundum-phase1-harmonic-core/tasks.md (T009 onward)
//
// SCOPE OF THIS TU (plan S8.1: behaviour, contract, short renders, L==R; tags
//   [systems][profundum]): SC-003 (render arm), SC-010(a), SC-010(d), SC-012,
//   SC-014 (a)-(c), SC-015, SC-016 (PartialCountExtremes), SC-017
//   (Deterministic), SC-018, SC-020 (a)-(c), SC-021 (b)-(e), SC-023, FR-041/E-6,
//   FR-043, FR-061/E-2, E-7, E-11, plus the plan-internal DetuneShadowNoDrift
//   and FirstNoteSeedsPhases cases.
// ==============================================================================

#include "profundum_core_test_helpers.h"
#include "render_fingerprint.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/scoped_denormal_mode.h>   // KRATE_HAS_SSE_DENORMAL_CONTROL, <xmmintrin.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;

// =============================================================================
// SC-023: kMaxPartials is untouched by Phase 1
// =============================================================================

TEST_CASE("ProfundumCore_KMaxPartialsUntouched", "[systems][profundum]") {
    static_assert(Krate::DSP::kMaxPartials == 96);
    SUCCEED("kMaxPartials == 96 (static_assert)");
}

// =============================================================================
// SC-016 count arm / FR-041: prepare-time partial count, clamped to [1, 96]
// =============================================================================

TEST_CASE("ProfundumCore_PartialCountExtremes", "[systems][profundum]") {
    auto core = std::make_unique<ProfundumCore>();
    REQUIRE_FALSE(core->isPrepared());

    core->prepare(48000.0, 1);
    REQUIRE(core->isPrepared());
    REQUIRE(core->numPartials() == 1);

    core->prepare(48000.0, 96);
    REQUIRE(core->numPartials() == 96);

    core->prepare(48000.0, 128);   // clamp: kMaxPartials is 96
    REQUIRE(core->numPartials() == 96);

    core->prepare(48000.0, 0);
    REQUIRE(core->numPartials() == 1);

    core->prepare(48000.0);
    REQUIRE(core->numPartials() == ProfundumCore::kDefaultPartials);
    REQUIRE(core->numPartials() == 64);
}

// =============================================================================
// FR-061 / E-2: silence and no state change before prepare; null / empty blocks
// =============================================================================

TEST_CASE("ProfundumCore_BeforePrepareSilent", "[systems][profundum]") {
    constexpr std::size_t kN = 256;
    auto core = std::make_unique<ProfundumCore>();
    REQUIRE_FALSE(core->isPrepared());

    std::vector<float> L(kN, 1.0f);
    std::vector<float> R(kN, 1.0f);
    core->processBlock(L.data(), R.data(), kN);
    for (std::size_t i = 0; i < kN; ++i) {
        INFO("sample " << i);
        REQUIRE(L[i] == 0.0f);
        REQUIRE(R[i] == 0.0f);
    }

    // E-2: null buffers and zero-length blocks leave the buffers untouched.
    std::fill(L.begin(), L.end(), 1.0f);
    std::fill(R.begin(), R.end(), 1.0f);
    core->processBlock(nullptr, R.data(), 64);
    core->processBlock(L.data(), nullptr, 64);
    core->processBlock(L.data(), R.data(), 0);
    for (std::size_t i = 0; i < kN; ++i) {
        INFO("sample " << i);
        REQUIRE(L[i] == 1.0f);
        REQUIRE(R[i] == 1.0f);
    }

    // Every setter and noteOn before prepare is a no-op (the memcmp-vs-fresh-core arm follows
    // at the end of this case).
    SpectralShapeRecipe::Controls c = SpectralShapeRecipe::kGrowl;
    core->setControls(c);
    core->setFrequency(220.0f);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::FreeRunning);
    std::array<float, Krate::DSP::kMaxPartials> pan{};
    pan.fill(0.5f);
    core->setPartialPanOffsets(pan);
    core->noteOn(110.0f);

    REQUIRE_FALSE(core->isPrepared());
    REQUIRE(core->numPartials() == 0);
    REQUIRE(core->shapeGains().empty());
    REQUIRE(core->deliveredGains().empty());
    REQUIRE(core->currentFrequency() == ProfundumCore::kDefaultF0Hz);
    REQUIRE(core->baseFrequency() == ProfundumCore::kDefaultF0Hz);
    REQUIRE(core->maskFrequency() == 0.0f);
    REQUIRE(core->stateFinite());

    std::fill(L.begin(), L.end(), 1.0f);
    std::fill(R.begin(), R.end(), 1.0f);
    core->processBlock(L.data(), R.data(), kN);
    for (std::size_t i = 0; i < kN; ++i) {
        INFO("sample " << i);
        REQUIRE(L[i] == 0.0f);
        REQUIRE(R[i] == 0.0f);
    }

    // Deferred arm (T010): the pre-prepare calls left no trace. prepare + the same post-prepare
    // calls render memcmp-equal to a fresh core. The post-prepare calls deliberately set neither
    // controls, frequency, policy nor pan, so a leaked kGrowl / 220 Hz / FreeRunning / pan 0.5
    // from before prepare would show in the render.
    auto fresh = std::make_unique<ProfundumCore>();
    core->prepare(48000.0);
    fresh->prepare(48000.0);
    core->noteOn(110.0f);
    fresh->noteOn(110.0f);
    constexpr std::size_t kRenderN = 9600;   // 0.2 s at 48 kHz, in 64-blocks
    std::vector<float> aL(kRenderN, 1.0f), aR(kRenderN, 1.0f), bL(kRenderN, -1.0f), bR(kRenderN, -1.0f);
    for (std::size_t pos = 0; pos < kRenderN; pos += 64) {
        core->processBlock(aL.data() + pos, aR.data() + pos, 64);
        fresh->processBlock(bL.data() + pos, bR.data() + pos, 64);
    }
    REQUIRE(PT::samplesBitEqual(aL.data(), bL.data(), kRenderN));
    REQUIRE(PT::samplesBitEqual(aR.data(), bR.data(), kRenderN));
    PT::requireLREqual(aL.data(), aR.data(), kRenderN);
    // Non-vacuity: the note sounds.
    REQUIRE(std::any_of(aL.begin(), aL.end(), [](float x) { return x != 0.0f; }));
}

// =============================================================================
// SC-015 static part: every audio-thread method and observer is noexcept
// =============================================================================

TEST_CASE("ProfundumCore_NoexceptContract", "[systems][profundum]") {
    using Core = ProfundumCore;
    using Pan = std::array<float, Krate::DSP::kMaxPartials>;
    static_assert(noexcept(std::declval<Core&>().prepare(48000.0)));
    static_assert(noexcept(std::declval<Core&>().prepare(48000.0, 64)));
    static_assert(noexcept(std::declval<Core&>().reset()));
    static_assert(noexcept(std::declval<Core&>().setControls(std::declval<const SpectralShapeRecipe::Controls&>())));
    static_assert(noexcept(std::declval<Core&>().setFrequency(55.0f)));
    static_assert(noexcept(std::declval<Core&>().setRetriggerPhase(Core::RetriggerPhase::Reset)));
    static_assert(noexcept(std::declval<Core&>().noteOn(55.0f)));
    static_assert(noexcept(std::declval<Core&>().setPartialPanOffsets(std::declval<const Pan&>())));
    static_assert(noexcept(std::declval<Core&>().processBlock(std::declval<float*>(), std::declval<float*>(),
                                                              std::size_t{64})));
    static_assert(noexcept(std::declval<Core&>().processBlock(std::declval<float*>(), std::declval<float*>(),
                                                              std::size_t{64}, std::declval<const float*>())));
    static_assert(noexcept(std::declval<const Core&>().shapeGains()));
    static_assert(noexcept(std::declval<const Core&>().deliveredGains()));
    static_assert(noexcept(std::declval<const Core&>().maxShapeStepPerInterval()));
    static_assert(noexcept(std::declval<const Core&>().currentFrequency()));
    static_assert(noexcept(std::declval<const Core&>().maskFrequency()));
    static_assert(noexcept(std::declval<const Core&>().baseFrequency()));
    static_assert(noexcept(std::declval<const Core&>().numPartials()));
    static_assert(noexcept(std::declval<const Core&>().isPrepared()));
    static_assert(noexcept(std::declval<const Core&>().stateFinite()));
    static_assert(std::is_nothrow_default_constructible_v<Core>);
    static_assert(std::is_nothrow_move_constructible_v<Core>);
    static_assert(std::is_nothrow_move_assignable_v<Core>);
    static_assert(!std::is_copy_constructible_v<Core>);
    static_assert(!std::is_copy_assignable_v<Core>);
    SUCCEED("noexcept contract holds (static_assert)");
}

// =============================================================================
// Plan S4.3: prepare defaults
// =============================================================================

TEST_CASE("ProfundumCore_PrepareDefaults", "[systems][profundum]") {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(48000.0);

    const auto shape = core->shapeGains();
    REQUIRE(shape.size() == 64);
    const std::vector<float> expected = PT::shapeOf(SpectralShapeRecipe::kDefaultControls, 64);
    REQUIRE(PT::samplesBitEqual(shape.data(), expected.data(), expected.size()));
    REQUIRE(core->deliveredGains().size() == 64);

    REQUIRE(core->currentFrequency() == 55.0f);
    REQUIRE(core->baseFrequency() == 55.0f);

    // Before any noteOn the core is silent: exact zeros on both channels.
    constexpr std::size_t kN = 1024;
    std::vector<float> L(kN, 1.0f);
    std::vector<float> R(kN, 1.0f);
    core->processBlock(L.data(), R.data(), kN);
    for (std::size_t i = 0; i < kN; ++i) {
        INFO("sample " << i);
        REQUIRE(L[i] == 0.0f);
        REQUIRE(R[i] == 0.0f);
    }
    PT::requireLREqual(L.data(), R.data(), kN);

    REQUIRE(core->stateFinite());
}

// =============================================================================
// Self-check of the measurement helpers (plan S8.2)
// =============================================================================

TEST_CASE("ProfundumCore_TestHelpersSelfCheck", "[systems][profundum]") {
    constexpr double kPi = 3.14159265358979323846;

    SECTION("BH7 window") {
        const std::vector<double> w = PT::bh7Window(1024);
        REQUIRE(w.size() == 1024);
        REQUIRE(w[512] == Catch::Approx(1.0));
        const auto& a = PT::kBh7Coefficients;
        const double w0 = a[0] - a[1] + a[2] - a[3] + a[4] - a[5] + a[6];
        REQUIRE(w[0] == Catch::Approx(w0).margin(1e-12));
    }

    SECTION("aliasedPower: clean sine floor and teeth") {
        constexpr std::size_t kLen = 8192;
        constexpr double kFs = 48000.0;
        std::vector<float> x(kLen);
        for (std::size_t k = 0; k < kLen; ++k)
            x[k] = static_cast<float>(std::sin(2.0 * kPi * 1000.0 * static_cast<double>(k) / kFs));
        const PT::AliasResult clean = PT::aliasedPower(x.data(), kLen, kFs, 1000.0);
        INFO("clean aliasedDbfs = " << clean.aliasedDbfs);
        REQUIRE(clean.aliasedDbfs <= -150.0);
        REQUIRE(clean.totalDb == Catch::Approx(0.0).margin(0.01));

        for (std::size_t k = 0; k < kLen; ++k)
            x[k] = static_cast<float>(std::sin(2.0 * kPi * 1000.0 * static_cast<double>(k) / kFs)
                                      + 0.001 * std::sin(2.0 * kPi * 1333.3 * static_cast<double>(k) / kFs));
        const PT::AliasResult dirty = PT::aliasedPower(x.data(), kLen, kFs, 1000.0);
        INFO("dirty aliasedDbfs = " << dirty.aliasedDbfs);
        REQUIRE(dirty.aliasedDbfs > -80.0);
    }

    SECTION("goertzelPhase") {
        constexpr std::size_t kLen = 4800;
        constexpr double kFs = 48000.0;
        std::vector<float> x(kLen);
        for (std::size_t k = 0; k < kLen; ++k)
            x[k] = static_cast<float>(std::sin(2.0 * kPi * 100.0 * static_cast<double>(k) / kFs + 0.3));
        const double phase = PT::goertzelPhase(x.data(), kLen, kFs, 100.0);
        REQUIRE(phase == Catch::Approx(0.3 - kPi / 2.0).margin(1e-6));
    }

    SECTION("aliasFftLength") {
        REQUIRE(PT::aliasFftLength(96000.0, 8.1758) == (std::size_t{1} << 20));
        REQUIRE(PT::aliasFftLength(48000.0, 1000.0) == 8192);
    }

    SECTION("renderIdeal: static kSineAnchor at C2 matches the core <= -80 dBFS RMS, onset included") {
        // T017 RULING (D-3, item 1 restated): the ideal's one-pole starts from zero at noteOn, so the
        // onset ramp cancels and the residual is read over the whole render.
        constexpr double kFs = 48000.0;
        constexpr float kC2Hz = 65.41f;
        const auto sine = SpectralShapeRecipe::kSineAnchor;
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = PT::renderCore(*core, sine, kC2Hz, 1.0);
        const std::vector<double> ideal = PT::renderIdeal(
            kFs, r.L.size(), [&](std::size_t) { return sine; }, [&](std::size_t) { return static_cast<double>(kC2Hz); });
        const std::vector<float> res = PT::residual(r.L, ideal);
        const double resDb = PT::rmsDbfs(res.data(), res.size());
        INFO("core - ideal RMS " << resDb << " dBFS (bar -80); core RMS " << PT::rmsDbfs(r.L.data(), r.L.size())
                                 << " dBFS");
        REQUIRE(resDb <= -80.0);
    }
}

// =============================================================================
// T010 shared fixtures
// =============================================================================

namespace {

using Controls = SpectralShapeRecipe::Controls;

constexpr float kC1 = 32.70f;
constexpr float kC2 = 65.41f;
constexpr float kC3 = 130.81f;
constexpr double kP0 = static_cast<double>(SpectralShapeRecipe::kPowerTarget);

/// Σaₙ² in double.
double powerOf(std::span<const float> a) {
    double p = 0.0;
    for (const float x : a)
        p += static_cast<double>(x) * static_cast<double>(x);
    return p;
}

/// SC-010(d) / SC-021(e): |Σaₙ² − P0| / P0 ≤ 1e-4.
void requireUnitPower(std::span<const float> a) {
    const double rel = std::abs(powerOf(a) - kP0) / kP0;
    INFO("|sum a^2 - P0| / P0 = " << rel);
    REQUIRE(rel <= 1e-4);
}

bool bitEqual(std::span<const float> a, const std::vector<float>& b) {
    return a.size() == b.size() && PT::samplesBitEqual(a.data(), b.data(), b.size());
}

/// The 33-point Depth sweep over [0, 1] (kDepthTriangle = 0.5 is point 16).
std::vector<float> depthSweep33() {
    std::vector<float> xs(33);
    for (std::size_t i = 0; i < xs.size(); ++i)
        xs[i] = static_cast<float>(i) / 32.0f;
    return xs;
}

/// One of the four SC-010 control axes (D, B, E, S) and its full range.
struct ControlAxis {
    const char* name;
    float lo;
    float hi;
    float Controls::*field;
};

constexpr std::array<ControlAxis, 4> kAxes{{
    {"depth", 0.0f, 1.0f, &Controls::depth},
    {"body", 0.0f, 1.0f, &Controls::body},
    {"edge", 0.0f, 1.0f, &Controls::edge},
    {"shift", -1.0f, 1.0f, &Controls::shift},
}};

/// Renders `numSamples` from `core` in `block`-sized blocks, appended to L/R.
void renderInto(ProfundumCore& core, std::vector<float>& L, std::vector<float>& R, std::size_t numSamples,
                std::size_t block) {
    const std::size_t start = L.size();
    L.resize(start + numSamples, 0.0f);
    R.resize(start + numSamples, 0.0f);
    for (std::size_t pos = 0; pos < numSamples; pos += block) {
        const std::size_t n = std::min(block, numSamples - pos);
        core.processBlock(L.data() + start + pos, R.data() + start + pos, n);
    }
}

}  // namespace

// =============================================================================
// FR-045 (plan-internal): the first note seeds every phase at 0, so sample 0 is exactly 0.0
// =============================================================================

TEST_CASE("ProfundumCore_FirstNoteSeedsPhases", "[systems][profundum]") {
    for (const auto policy : {ProfundumCore::RetriggerPhase::Reset, ProfundumCore::RetriggerPhase::FreeRunning}) {
        INFO("policy " << (policy == ProfundumCore::RetriggerPhase::Reset ? "Reset" : "FreeRunning"));
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(48000.0);
        core->setRetriggerPhase(policy);
        core->setControls(PT::midGrid());

        // Put the grid mid-interval before the note (37 silent samples).
        std::array<float, 37> preL{};
        std::array<float, 37> preR{};
        core->processBlock(preL.data(), preR.data(), preL.size());
        REQUIRE(std::all_of(preL.begin(), preL.end(), [](float x) { return x == 0.0f; }));

        core->noteOn(65.41f);
        std::array<float, 64> L{};
        std::array<float, 64> R{};
        core->processBlock(L.data(), R.data(), L.size());
        REQUIRE(L[0] == 0.0f);
        REQUIRE(R[0] == 0.0f);
        PT::requireLREqual(L.data(), R.data(), L.size());
        // Non-vacuity: the note starts sounding inside the block.
        REQUIRE(std::any_of(L.begin(), L.end(), [](float x) { return x != 0.0f; }));
    }
}

// =============================================================================
// SC-012 Reset arm, FR-046, plan S1 C-9: a Reset noteOn erases the prior state
// =============================================================================

TEST_CASE("ProfundumCore_RetriggerPhasePolicy", "[systems][profundum]") {
    using RP = ProfundumCore::RetriggerPhase;

    // Core A: 0.3 s of kGrowl at C3 in 64-blocks.
    auto a = std::make_unique<ProfundumCore>();
    a->prepare(48000.0);
    const PT::Render preA = PT::renderCore(*a, SpectralShapeRecipe::kGrowl, kC3, 0.3, RP::Reset, false, 64);
    PT::requireLREqual(preA.L.data(), preA.R.data(), preA.L.size());

    // Core B: 34 104 samples (0.7105 s) of kSawAnchor at C1 in 37-blocks (a different grid phase:
    // 34 104 is not a multiple of kControlInterval, while Core A's 14 400 is).
    auto b = std::make_unique<ProfundumCore>();
    b->prepare(48000.0);
    const PT::Render preB = PT::renderCore(*b, SpectralShapeRecipe::kSawAnchor, kC1, 0.7105, RP::Reset, false, 37);
    REQUIRE(preB.L.size() == 34104);
    REQUIRE(preB.L.size() % ProfundumCore::kControlInterval != 0);
    PT::requireLREqual(preB.L.data(), preB.R.data(), preB.L.size());

    // Both: mid grid, Reset noteOn at C2, 0.5 s in 64-blocks.  // plan S1 C-9
    const PT::Render postA = PT::renderCore(*a, PT::midGrid(), kC2, 0.5, RP::Reset, false, 64);
    const PT::Render postB = PT::renderCore(*b, PT::midGrid(), kC2, 0.5, RP::Reset, false, 64);
    REQUIRE(postA.L.size() == postB.L.size());
    REQUIRE(PT::samplesBitEqual(postA.L.data(), postB.L.data(), postA.L.size()));
    REQUIRE(PT::samplesBitEqual(postA.R.data(), postB.R.data(), postA.R.size()));
    REQUIRE(postA.L[0] == 0.0f);
    REQUIRE(postA.R[0] == 0.0f);
    PT::requireLREqual(postA.L.data(), postA.R.data(), postA.L.size());
    REQUIRE(std::any_of(postA.L.begin(), postA.L.end(), [](float x) { return x != 0.0f; }));

    // Same comparison with the second core cleared by reset() instead of a long prior render.
    auto c = std::make_unique<ProfundumCore>();
    c->prepare(48000.0);
    const PT::Render preC = PT::renderCore(*c, SpectralShapeRecipe::kSawAnchor, kC1, 0.71, RP::Reset, false, 37);
    PT::requireLREqual(preC.L.data(), preC.R.data(), preC.L.size());
    (*c).reset();
    const PT::Render postC = PT::renderCore(*c, PT::midGrid(), kC2, 0.5, RP::Reset, false, 64);
    REQUIRE(postC.L.size() == postA.L.size());
    REQUIRE(PT::samplesBitEqual(postA.L.data(), postC.L.data(), postA.L.size()));
    REQUIRE(PT::samplesBitEqual(postA.R.data(), postC.R.data(), postA.R.size()));

    // ---- FreeRunning arms (T011) ----

    // Same f0 and controls: a mid-render FreeRunning noteOn is bit-identical (in-process) to an
    // uninterrupted render. 37-blocks put the noteOn mid-interval, so the grid must survive it.
    {
        const auto n = static_cast<std::size_t>(std::llround(0.3 * 48000.0));
        auto x = std::make_unique<ProfundumCore>();
        auto y = std::make_unique<ProfundumCore>();
        x->prepare(48000.0);
        y->prepare(48000.0);
        const PT::Render x0 = PT::renderCore(*x, PT::midGrid(), kC2, 0.3, RP::Reset, false, 37);
        const PT::Render y0 = PT::renderCore(*y, PT::midGrid(), kC2, 0.3, RP::Reset, false, 37);
        REQUIRE(PT::samplesBitEqual(x0.L.data(), y0.L.data(), x0.L.size()));

        std::vector<float> xL, xR, yL, yR;
        renderInto(*x, xL, xR, n, 37);
        y->setRetriggerPhase(RP::FreeRunning);
        y->setControls(PT::midGrid());
        y->noteOn(kC2);
        renderInto(*y, yL, yR, n, 37);
        REQUIRE(PT::samplesBitEqual(xL.data(), yL.data(), n));
        REQUIRE(PT::samplesBitEqual(xR.data(), yR.data(), n));
        PT::requireLREqual(yL.data(), yR.data(), n);
        REQUIRE(std::any_of(yL.begin(), yL.end(), [](float v) { return v != 0.0f; }));
    }

    // +0.5 semitone (no bank crossfade): no step at the boundary sample k0 beyond the steady
    // state's own largest adjacent delta, and no reset to phase 0 (y[k0] != 0).
    const std::array<Controls, 2> states{SpectralShapeRecipe::kSawAnchor, PT::midGrid()};
    for (std::size_t si = 0; si < states.size(); ++si) {
        INFO("state #" << si << (si == 0 ? " (kSawAnchor)" : " (mid grid)"));
        constexpr double kFs = 48000.0;
        constexpr std::size_t kBlock = 64;
        const auto lookback = static_cast<std::size_t>(std::llround(0.1 * kFs));   // 100 ms

        // Pre-render to choose a block-aligned k0 where |y| >= 0.25 × the state's peak.
        auto p = std::make_unique<ProfundumCore>();
        p->prepare(kFs);
        const PT::Render ref = PT::renderCore(*p, states[si], kC2, 0.6, RP::Reset, false, kBlock);
        float peak = 0.0f;
        for (std::size_t k = static_cast<std::size_t>(0.1 * kFs); k < ref.L.size(); ++k)
            peak = std::max(peak, std::abs(ref.L[k]));
        REQUIRE(peak > 0.0f);
        const std::size_t kStart = (static_cast<std::size_t>(0.4 * kFs) + kBlock - 1) / kBlock * kBlock;
        std::size_t k0 = 0;
        for (std::size_t k = kStart; k + 1 < ref.L.size(); k += kBlock) {
            if (std::abs(ref.L[k]) >= 0.25f * peak) {
                k0 = k;
                break;
            }
        }
        REQUIRE(k0 > lookback);

        // Same Reset render up to k0 (bit-identical prefix), then a FreeRunning noteOn +0.5 st.
        auto q = std::make_unique<ProfundumCore>();
        q->prepare(kFs);
        const PT::Render pre = PT::renderCore(*q, states[si], kC2, static_cast<double>(k0) / kFs, RP::Reset,
                                              false, kBlock);
        REQUIRE(pre.L.size() == k0);
        REQUIRE(PT::samplesBitEqual(pre.L.data(), ref.L.data(), k0));

        const float fUp = static_cast<float>(static_cast<double>(kC2) * std::exp2(0.5 / 12.0));
        q->setRetriggerPhase(RP::FreeRunning);
        q->noteOn(fUp);
        std::vector<float> L, R;
        renderInto(*q, L, R, lookback, kBlock);
        PT::requireLREqual(L.data(), R.data(), L.size());
        REQUIRE(q->baseFrequency() == fUp);   // non-vacuity: the note moved

        float maxAdjacent = 0.0f;
        for (std::size_t k = k0 - lookback + 1; k < k0; ++k)
            maxAdjacent = std::max(maxAdjacent, std::abs(pre.L[k] - pre.L[k - 1]));
        const float boundaryStep = std::abs(L[0] - pre.L[k0 - 1]);
        INFO("k0 " << k0 << " y[k0] " << L[0] << " boundary step " << boundaryStep << " max adjacent "
                   << maxAdjacent);
        REQUIRE(boundaryStep <= maxAdjacent);
        REQUIRE(L[0] != 0.0f);
    }
}

// =============================================================================
// SC-021(d)/(e), FR-051(d): the first delivered vector after noteOn is already masked
// =============================================================================

TEST_CASE("ProfundumCore_NoteOnMaskImmediate", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(kFs));
    REQUIRE(capHz == 19200.0);

    for (const auto policy : {ProfundumCore::RetriggerPhase::Reset, ProfundumCore::RetriggerPhase::FreeRunning}) {
        for (const float f0 : {1000.0f, 3000.0f, 9000.0f}) {
            INFO("policy " << (policy == ProfundumCore::RetriggerPhase::Reset ? "Reset" : "FreeRunning")
                           << " f0 " << f0);
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(kFs);
            // A sounding prior note at A1, where nothing is capped.
            const PT::Render prior = PT::renderCore(*core, PT::midGrid(), 55.0f, 0.1, policy, false, 64);
            PT::requireLREqual(prior.L.data(), prior.R.data(), prior.L.size());

            core->noteOn(f0);
            std::array<float, 64> L{};
            std::array<float, 64> R{};
            core->processBlock(L.data(), R.data(), L.size());
            PT::requireLREqual(L.data(), R.data(), L.size());

            const auto d = core->deliveredGains();
            REQUIRE(d.size() == 64);
            REQUIRE(core->maskFrequency() == f0);
            int zeroed = 0;
            for (int n = 2; n <= 64; ++n) {
                if (static_cast<double>(n) * static_cast<double>(f0) >= capHz) {
                    INFO("harmonic " << n);
                    REQUIRE(d[static_cast<std::size_t>(n - 1)] == 0.0f);
                    ++zeroed;
                }
            }
            REQUIRE(zeroed > 0);   // non-vacuity: every f0 here caps some partials
            requireUnitPower(d);
        }
    }
}

// =============================================================================
// SC-010(d) control arm, FR-050: per-interval shape step ceiling, P0 norm, convergence
// =============================================================================

TEST_CASE("ProfundumCore_ShapeGainStepCeiling", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = ProfundumCore::kControlInterval;   // grid-aligned: one control update per block
    // Δmax = 0.25·√P0 at 48 kHz / 32: a quarter-circle (orthogonal states) is ⌈(π/2 − 2·asin(0.125))
    // / (2·asin(0.999·0.125))⌉ = 6 slew steps, plus the snap.
    constexpr std::size_t kConvergeIntervals = 7;

    for (const ControlAxis& axis : kAxes) {
        INFO("axis " << axis.name);
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const float dMax = core->maxShapeStepPerInterval();

        std::vector<std::vector<float>> shapes;
        std::vector<std::vector<float>> delivered;
        std::vector<float> L(kBlock);
        std::vector<float> R(kBlock);
        const auto step = [&](const Controls* c) {
            if (c != nullptr)
                core->setControls(*c);
            core->processBlock(L.data(), R.data(), kBlock);
            PT::requireLREqual(L.data(), R.data(), kBlock);
            const auto s = core->shapeGains();
            const auto d = core->deliveredGains();
            shapes.emplace_back(s.begin(), s.end());
            delivered.emplace_back(d.begin(), d.end());
        };
        const auto withValue = [&](float v) {
            Controls c = PT::midGrid();
            c.*(axis.field) = v;
            return c;
        };
        // A change set before block j has converged by record j + kConvergeIntervals − 1.
        const auto requireConvergedAfter = [&](std::size_t j, const Controls& target) {
            INFO("change before block " << j);
            REQUIRE(shapes.size() > j + kConvergeIntervals - 1);
            REQUIRE(bitEqual(shapes[j + kConvergeIntervals - 1], PT::shapeOf(target, 64)));
        };

        const std::size_t holdBlocks = static_cast<std::size_t>(std::llround(0.5 * kFs)) / kBlock;   // 750

        // Fixture 1: lo -> hi -> lo with 0.5 s holds, from a Reset note at lo.
        const Controls cLo = withValue(axis.lo);
        const Controls cHi = withValue(axis.hi);
        core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
        core->setControls(cLo);
        core->noteOn(kC2);
        for (std::size_t k = 0; k < holdBlocks; ++k)
            step(nullptr);
        const std::size_t up = shapes.size();
        step(&cHi);
        for (std::size_t k = 1; k < holdBlocks; ++k)
            step(nullptr);
        const std::size_t down = shapes.size();
        step(&cLo);
        for (std::size_t k = 1; k < holdBlocks; ++k)
            step(nullptr);
        requireConvergedAfter(up, cHi);
        requireConvergedAfter(down, cLo);
        if (axis.field == &Controls::depth) {
            // Non-vacuity: a full-range Depth step is farther than one Δmax, so the slew engages
            // (the first update after the step is not yet the target).
            REQUIRE_FALSE(bitEqual(shapes[up], PT::shapeOf(cHi, 64)));
        }

        // Reset exemption: the first recorded shape after a Reset noteOn to a far target is the
        // target, bitwise (no slew).
        core->setControls(cHi);
        core->noteOn(kC2);
        step(nullptr);
        REQUIRE(bitEqual(shapes.back(), PT::shapeOf(cHi, 64)));
        core->setControls(cLo);
        core->noteOn(kC2);
        step(nullptr);
        REQUIRE(bitEqual(shapes.back(), PT::shapeOf(cLo, 64)));
        const std::size_t afterExemption = shapes.size();   // the exempt records are this − 2 and − 1

        // Fixture 2: a 10 ms full-range linear sweep lo -> hi, controls set before every block.
        const auto sweepSamples = static_cast<std::size_t>(std::llround(0.010 * kFs));   // 480
        const std::size_t sweepBlocks = (sweepSamples + kBlock - 1) / kBlock;           // 15
        std::size_t lastChange = 0;
        for (std::size_t k = 0; k < sweepBlocks; ++k) {
            const float t = std::min(1.0f, static_cast<float>((k + 1) * kBlock) / static_cast<float>(sweepSamples));
            const Controls c = withValue(axis.lo + (axis.hi - axis.lo) * t);
            lastChange = shapes.size();
            step(&c);
        }
        for (std::size_t k = 0; k < 2 * kConvergeIntervals; ++k)
            step(nullptr);
        requireConvergedAfter(lastChange, cHi);

        // Fixture 3: a 2 s, 2 Hz full-range triangle, controls set before every block.
        const std::size_t triBlocks = static_cast<std::size_t>(std::llround(2.0 * kFs)) / kBlock;
        for (std::size_t k = 0; k < triBlocks; ++k) {
            const double t = static_cast<double>(k * kBlock) / kFs;
            const double ph = std::fmod(t * 2.0, 1.0);
            const double tri = ph < 0.5 ? 2.0 * ph : 2.0 - 2.0 * ph;
            const Controls c = withValue(axis.lo + (axis.hi - axis.lo) * static_cast<float>(tri));
            step(&c);
        }

        // Every vector is on the P0 sphere; every consecutive step (outside the two Reset
        // noteOns, which are exempt) is within Δmax per element.
        for (std::size_t k = 0; k < shapes.size(); ++k) {
            INFO("record " << k);
            requireUnitPower(shapes[k]);
            requireUnitPower(delivered[k]);
        }
        float worst = 0.0f;
        for (std::size_t k = 1; k < shapes.size(); ++k) {
            if (k == afterExemption - 2 || k == afterExemption - 1)
                continue;
            for (std::size_t n = 0; n < shapes[k].size(); ++n)
                worst = std::max(worst, std::abs(shapes[k][n] - shapes[k - 1][n]));
        }
        INFO("max |delta shape| = " << worst << ", ceiling " << dMax);
        REQUIRE(worst <= dMax);
        REQUIRE(worst > 0.0f);
    }
}

// =============================================================================
// SC-018, FR-049: steady-state output −12 dBFS RMS per channel ± 0.1 dB
// =============================================================================

TEST_CASE("ProfundumCore_OutputLevelConstant", "[systems][profundum]") {
    const std::array<Controls, 12> coords{
        SpectralShapeRecipe::kSineAnchor, SpectralShapeRecipe::kTriangleAnchor, SpectralShapeRecipe::kSawAnchor,
        SpectralShapeRecipe::kHeavy,      SpectralShapeRecipe::kHollow,         SpectralShapeRecipe::kGrowl,
        SpectralShapeRecipe::kBodyRound,  SpectralShapeRecipe::kBodyHollow,     SpectralShapeRecipe::kBodyWoody,
        SpectralShapeRecipe::kBodyNasal,  SpectralShapeRecipe::kBodyThick,      PT::midGrid()};
    const double target = static_cast<double>(SpectralShapeRecipe::kCoreOutputRmsDb);

    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(fs);
        const auto skip = static_cast<std::size_t>(std::llround(0.150 * fs));
        for (const float f0 : {kC1, kC2, kC3}) {
            for (std::size_t i = 0; i < coords.size(); ++i) {
                INFO("fs " << fs << " f0 " << f0 << " coordinate #" << i);
                const PT::Render r = PT::renderCore(*core, coords[i], f0, 1.2);
                PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
                const double db = PT::rmsDbOverPeriods(r.L.data() + skip, r.L.size() - skip, fs,
                                                       static_cast<double>(f0), 1.0);
                INFO("RMS " << db << " dBFS");
                REQUIRE(db == Catch::Approx(target).margin(0.1));
            }
        }
    }
}

// =============================================================================
// SC-003 render arm, FR-005: Depth is not a volume knob (±0.5 dB of the sweep median)
// =============================================================================

namespace {

/// One fresh Reset render per Depth step, RMS within ±0.5 dB of the sweep median at that grid
/// point and pitch. The render is 1.2 s, not the 1.15 s tasks.md names: after the 150 ms skip,
/// 1.15 s leaves exactly 1.000 s, which floors to fewer than 1 s of whole periods at C1/C2/C3
/// (e.g. 65 periods of 65.41 Hz = 0.994 s), so rmsDbOverPeriods' ">= 1 s" precondition could not
/// hold. The bar (±0.5 dB, >= 1 s of whole periods) is unchanged.
void requireDepthLoudnessFlat(ProfundumCore& core, const Controls& gridPoint, float f0) {
    constexpr double kFs = 48000.0;
    constexpr double kRenderSeconds = 1.2;
    const auto skip = static_cast<std::size_t>(std::llround(0.150 * kFs));
    std::vector<float> depths = depthSweep33();
    depths.push_back(SpectralShapeRecipe::kDepthTriangle);
    std::sort(depths.begin(), depths.end());
    depths.erase(std::unique(depths.begin(), depths.end()), depths.end());
    REQUIRE(depths.size() == 33);

    std::vector<double> db;
    db.reserve(depths.size());
    for (const float d : depths) {
        Controls c = gridPoint;
        c.depth = d;
        const PT::Render r = PT::renderCore(core, c, f0, kRenderSeconds);
        PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
        db.push_back(PT::rmsDbOverPeriods(r.L.data() + skip, r.L.size() - skip, kFs, static_cast<double>(f0), 1.0));
    }
    std::vector<double> sorted = db;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2];
    for (std::size_t i = 0; i < db.size(); ++i) {
        INFO("f0 " << f0 << " body " << gridPoint.body << " edge " << gridPoint.edge << " shift "
                   << gridPoint.shift << " depth " << depths[i] << ": " << db[i] << " dB, median " << median);
        REQUIRE(std::abs(db[i] - median) <= 0.5);
    }
}

}  // namespace

TEST_CASE("ProfundumCore_DepthLoudnessFlat", "[systems][profundum][long]") {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(48000.0);
    for (const float f0 : {kC1, kC2, kC3})
        for (int iB = 0; iB < 3; ++iB)
            for (int iE = 0; iE < 3; ++iE)
                for (int iS = 0; iS < 3; ++iS)
                    requireDepthLoudnessFlat(*core, PT::gridPoint(1, iB, iE, iS), f0);
}

TEST_CASE("ProfundumCore_DepthLoudnessFlatSmoke", "[systems][profundum]") {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(48000.0);
    requireDepthLoudnessFlat(*core, PT::midGrid(), kC2);
    for (const int iB : {0, 2})
        for (const int iE : {0, 2})
            for (const int iS : {0, 2})
                requireDepthLoudnessFlat(*core, PT::gridPoint(1, iB, iE, iS), kC2);
}

// =============================================================================
// SC-017, FR-063: same call sequence -> bit-identical render (in-process)
// =============================================================================

TEST_CASE("ProfundumCore_Deterministic", "[systems][profundum]") {
    using RP = ProfundumCore::RetriggerPhase;
    constexpr std::size_t kBlock = 37;
    const auto samples = [](double s) { return static_cast<std::size_t>(std::llround(s * 48000.0)); };

    const auto script = [&](ProfundumCore& core, std::vector<float>& L, std::vector<float>& R) {
        core.prepare(48000.0);
        core.setRetriggerPhase(RP::Reset);
        core.setControls(SpectralShapeRecipe::kGrowl);
        core.noteOn(kC2);
        renderInto(core, L, R, samples(0.4), kBlock);
        core.setControls(SpectralShapeRecipe::kHollow);
        renderInto(core, L, R, samples(0.3), kBlock);
        core.setRetriggerPhase(RP::FreeRunning);
        core.setControls(SpectralShapeRecipe::kHeavy);
        core.noteOn(98.0f);
        renderInto(core, L, R, samples(0.4), kBlock);
        core.setFrequency(87.31f);
        renderInto(core, L, R, samples(0.3), kBlock);
        core.setRetriggerPhase(RP::Reset);
        core.setControls(SpectralShapeRecipe::kSawAnchor);
        core.noteOn(41.2f);
        renderInto(core, L, R, samples(0.6), kBlock);
    };

    auto a = std::make_unique<ProfundumCore>();
    auto b = std::make_unique<ProfundumCore>();
    std::vector<float> aL, aR, bL, bR;
    script(*a, aL, aR);
    script(*b, bL, bR);
    REQUIRE(aL.size() == samples(2.0));
    REQUIRE(aL.size() == bL.size());
    REQUIRE(PT::samplesBitEqual(aL.data(), bL.data(), aL.size()));
    REQUIRE(PT::samplesBitEqual(aR.data(), bR.data(), aR.size()));
    PT::requireLREqual(aL.data(), aR.data(), aL.size());
    REQUIRE(std::any_of(aL.begin(), aL.end(), [](float x) { return x != 0.0f; }));
}

// =============================================================================
// E-11: zero-target lanes under the test main's FTZ/DAZ stay finite and exactly zero
// =============================================================================

TEST_CASE("ProfundumCore_ZeroTargetLanesUnderFtz", "[systems][profundum]") {
#if KRATE_HAS_SSE_DENORMAL_CONTROL
    REQUIRE(_MM_GET_FLUSH_ZERO_MODE() == _MM_FLUSH_ZERO_ON);
#endif
    constexpr double kFs = 48000.0;
    const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(kFs));

    for (const float f0 : {kC2, 1046.5f}) {   // C2, and C6 (capped lanes)
        INFO("f0 " << f0);
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = PT::renderCore(*core, SpectralShapeRecipe::kTriangleAnchor, f0, 5.0);
        PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
        bool allFinite = true;
        for (std::size_t i = 0; i < r.L.size(); ++i)
            allFinite = allFinite && Krate::DSP::detail::isFinite(r.L[i]) && Krate::DSP::detail::isFinite(r.R[i]);
        REQUIRE(allFinite);
        REQUIRE(core->stateFinite());

        const auto d = core->deliveredGains();
        int capped = 0;
        for (std::size_t n = 2; n <= d.size(); ++n) {
            INFO("harmonic " << n);
            if (n % 2 == 0)
                REQUIRE(d[n - 1] == 0.0f);   // triangle: every even partial
            if (static_cast<double>(n) * static_cast<double>(f0) >= capHz) {
                REQUIRE(d[n - 1] == 0.0f);   // capped lanes
                ++capped;
            }
        }
        if (f0 > 1000.0f)
            REQUIRE(capped > 0);   // non-vacuity for the C6 arm
        requireUnitPower(d);
    }
}

// =============================================================================
// FR-042 skip rule: in steady state nothing the control path owns changes
// =============================================================================

TEST_CASE("ProfundumCore_ControlSkipRule", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    // 64 Hz = exactly 750 samples per period, so the 1 s windows at 5 s and 8 s hold the same
    // whole number of periods at the same phase and their fingerprints are comparable.
    constexpr float kF0 = 64.0f;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    const PT::Render r = PT::renderCore(*core, PT::midGrid(), kF0, 10.0, ProfundumCore::RetriggerPhase::Reset,
                                        /*record=*/true, 64);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    REQUIRE(r.shapes.size() >= 101);

    // The Reset noteOn converges at once; from the first record on, nothing changes over every
    // one of the >= 100 following intervals.
    for (std::size_t k = 1; k < r.shapes.size(); ++k) {
        INFO("record " << k);
        REQUIRE(PT::samplesBitEqual(r.shapes[k].data(), r.shapes[k - 1].data(), r.shapes[k].size()));
        REQUIRE(PT::samplesBitEqual(r.delivered[k].data(), r.delivered[k - 1].data(), r.delivered[k].size()));
        REQUIRE(std::bit_cast<std::uint32_t>(r.baseF0[k]) == std::bit_cast<std::uint32_t>(r.baseF0[k - 1]));
    }

    namespace TU = Krate::DSP::TestUtils;
    const auto second = static_cast<std::size_t>(kFs);
    const auto fpA = TU::fingerprintRender(std::span<const float>(r.L.data() + 5 * second, second));
    const auto fpB = TU::fingerprintRender(std::span<const float>(r.L.data() + 8 * second, second));
    const auto cmp = TU::compareFingerprints(fpB, fpA);
    INFO("worst metric relative error " << cmp.worstMetricRelativeError << " (" << cmp.detail << ")");
    REQUIRE(cmp.worstMetricRelativeError <= TU::kMetricTolerance);
    REQUIRE(fpA.rms > 0.0);
}

// =============================================================================
// T011 shared fixtures: f0 trajectories (plan S4.6, S8.3)
// =============================================================================

namespace {

using RP = ProfundumCore::RetriggerPhase;

constexpr std::size_t kU = ProfundumCore::kPitchUpdateInterval;   // 16

std::size_t samplesAt(double seconds, double fs) { return static_cast<std::size_t>(std::llround(seconds * fs)); }

float midiToHz(int midi) { return static_cast<float>(440.0 * std::exp2((static_cast<double>(midi) - 69.0) / 12.0)); }

/// Appends a log-linear glide from t.back() to `to` over rampN samples (t.back() excluded, `to`
/// included), then holdN samples of `to`. t must not be empty.
void appendGlide(std::vector<float>& t, float to, std::size_t rampN, std::size_t holdN) {
    REQUIRE_FALSE(t.empty());
    const std::vector<float> r = PT::logLinearRamp(t.back(), to, rampN + 1);
    t.insert(t.end(), r.begin() + 1, r.end());
    t.insert(t.end(), holdN, to);
}

/// Hold `from` until startSec, a 10 ms log-linear glide to `to`, then hold `to` to totalSec.
std::vector<float> dropTrajectory(double fs, float from, float to, double startSec, double totalSec) {
    std::vector<float> t(samplesAt(startSec, fs), from);
    appendGlide(t, to, samplesAt(0.010, fs), 0);
    t.resize(samplesAt(totalSec, fs), to);
    return t;
}

/// 5 Hz ±2-semitone vibrato around `centre`, sample i.
float vibratoAt(std::size_t i, double fs, float centre) {
    const double semis = 2.0 * std::sin(PT::kTwoPi * 5.0 * static_cast<double>(i) / fs);
    return static_cast<float>(static_cast<double>(centre) * std::exp2(semis / 12.0));
}

std::vector<float> vibratoTrajectory(double fs, float centre, double seconds) {
    std::vector<float> t(samplesAt(seconds, fs));
    for (std::size_t i = 0; i < t.size(); ++i)
        t[i] = vibratoAt(i, fs, centre);
    return t;
}

/// SC-009 bend legs: a 50 ms hold at loMidi, then 1-semitone legs up to hiMidi and back down,
/// each leg a 10 ms log-linear glide plus a 50 ms hold.
std::vector<float> bendLegs(double fs, int loMidi, int hiMidi) {
    const std::size_t ramp = samplesAt(0.010, fs);
    const std::size_t hold = samplesAt(0.050, fs);
    std::vector<float> t(hold, midiToHz(loMidi));
    for (int m = loMidi + 1; m <= hiMidi; ++m)
        appendGlide(t, midiToHz(m), ramp, hold);
    for (int m = hiMidi - 1; m >= loMidi; --m)
        appendGlide(t, midiToHz(m), ramp, hold);
    return t;
}

/// ±12-semitone bends in 50 ms around C2: C2 -> C3 -> C2 -> C1 -> C2, 100 ms holds between.
std::vector<float> bendAroundC2(double fs) {
    const std::size_t ramp = samplesAt(0.050, fs);
    const std::size_t hold = samplesAt(0.100, fs);
    std::vector<float> t(hold, kC2);
    appendGlide(t, kC3, ramp, hold);
    appendGlide(t, kC2, ramp, hold);
    appendGlide(t, kC1, ramp, hold);
    appendGlide(t, kC2, ramp, 2 * hold);
    return t;
}

/// One control interval: the block for per-interval records (one control update per record).
constexpr std::size_t kInterval = ProfundumCore::kControlInterval;

/// Reset noteOn at traj.front(), then the whole trajectory through f0PerSample.
PT::Render renderTrajectory(ProfundumCore& core, const Controls& c, const std::vector<float>& traj, double fs,
                            std::size_t block, bool record) {
    const PT::Render r = PT::renderCore(core, c, traj.front(), static_cast<double>(traj.size()) / fs, RP::Reset,
                                        record, block, traj.data());
    REQUIRE(r.L.size() == traj.size());
    return r;
}

/// renderInto with an f0 trajectory slice per block (traj holds numSamples values).
void renderTrajInto(ProfundumCore& core, std::vector<float>& L, std::vector<float>& R, const std::vector<float>& traj,
                    std::size_t block) {
    const std::size_t start = L.size();
    const std::size_t numSamples = traj.size();
    L.resize(start + numSamples, 0.0f);
    R.resize(start + numSamples, 0.0f);
    for (std::size_t pos = 0; pos < numSamples; pos += block) {
        const std::size_t n = std::min(block, numSamples - pos);
        core.processBlock(L.data() + start + pos, R.data() + start + pos, n, traj.data() + pos);
    }
}

/// Largest |12·log2(base_k / base_{k−1})| over consecutive records.
double maxBaseStepSemitones(const PT::Render& r) {
    double worst = 0.0;
    for (std::size_t k = 1; k < r.baseF0.size(); ++k)
        worst = std::max(worst, std::abs(12.0 * std::log2(static_cast<double>(r.baseF0[k])
                                                          / static_cast<double>(r.baseF0[k - 1]))));
    return worst;
}

/// SC-021(c): every n >= 2 at or above the cap of that interval's maskFrequency() is exactly 0.
/// The product is formed in float, as the recipe's evaluateMask forms it (FR-032). Returns the
/// number of lanes the rule covered (for non-vacuity).
std::size_t requireCapExact(const PT::Render& r, double fs) {
    const float capHz = SpectralShapeRecipe::capFrequency(fs);
    std::size_t covered = 0;
    std::size_t violations = 0;
    std::size_t firstK = 0;
    std::size_t firstN = 0;
    for (std::size_t k = 0; k < r.delivered.size(); ++k) {
        for (std::size_t n = 2; n <= r.delivered[k].size(); ++n) {
            if (static_cast<float>(n) * r.maskF0[k] >= capHz) {
                ++covered;
                if (r.delivered[k][n - 1] != 0.0f) {
                    if (violations == 0) {
                        firstK = k;
                        firstN = n;
                    }
                    ++violations;
                }
            }
        }
    }
    INFO("cap " << capHz << " Hz: " << violations << " non-zero capped lanes (first: record " << firstK
                << ", harmonic " << firstN << ")");
    REQUIRE(violations == 0);
    return covered;
}

/// SC-021(b)/(e): per record, |Δdelivered_n| <= L_f·|log2(maskF0_k / maskF0_{k−1})| + 1e-6 and
/// Σa² = P0 ± 1e-4. Returns the largest |Δdelivered_n| seen (for non-vacuity).
double requireMaskStepBound(const PT::Render& r) {
    const double lf = static_cast<double>(SpectralShapeRecipe::kLipschitzOctaveCeiling) * std::sqrt(kP0);
    double worstDelta = 0.0;
    double worstExcess = -1.0;
    std::size_t worstK = 0;
    for (std::size_t k = 0; k < r.delivered.size(); ++k) {
        INFO("record " << k);
        requireUnitPower(r.delivered[k]);
        if (k == 0)
            continue;
        const double oct = std::abs(std::log2(static_cast<double>(r.maskF0[k]) / static_cast<double>(r.maskF0[k - 1])));
        const double bound = lf * oct + 1e-6;
        for (std::size_t n = 0; n < r.delivered[k].size(); ++n) {
            const double d = std::abs(static_cast<double>(r.delivered[k][n]) - static_cast<double>(r.delivered[k - 1][n]));
            worstDelta = std::max(worstDelta, d);
            if (d - bound > worstExcess) {
                worstExcess = d - bound;
                worstK = k;
            }
        }
    }
    INFO("worst |delta delivered| - bound = " << worstExcess << " at record " << worstK);
    REQUIRE(worstExcess <= 0.0);
    return worstDelta;
}

/// Every sample finite and within the bank's output clamp.
void requireFiniteBounded(const std::vector<float>& x) {
    const float clamp = Krate::DSP::HarmonicOscillatorBank::kOutputClamp;
    bool ok = true;
    for (const float v : x)
        ok = ok && Krate::DSP::detail::isFinite(v) && std::abs(v) <= clamp;
    REQUIRE(ok);
}

Krate::DSP::TestUtils::ClickDetector makeClickDetector() {
    Krate::DSP::TestUtils::ClickDetector det(Krate::DSP::TestUtils::ClickDetectorConfig{
        .sampleRate = 48000.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5});
    det.prepare();
    return det;
}

/// Phase of h1 in cycles at sample p (BH7 Goertzel over 8 periods starting at p, + π/2 so a
/// sine's own phase is returned). Eight periods put the negative-frequency image 16 bins and h2
/// 8 bins away, outside the BH7 main lobe (±7 bins); a one-period BH7 window has its image 2 bins
/// away, inside the main lobe, which biases the phase by up to ~0.38 rad.
double h1PhaseCycles(const std::vector<float>& x, std::size_t p, double fs, double hz) {
    const auto len = static_cast<std::size_t>(std::llround(8.0 * fs / hz));
    REQUIRE(p + len <= x.size());
    return (PT::goertzelPhase(x.data() + p, len, fs, hz) + 0.5 * 3.14159265358979323846) / PT::kTwoPi;
}

double wrapHalfCycle(double c) { return c - std::round(c); }

/// SC-010(a) amended (D-1/D-2 and D-3 rulings): the click detector runs on the residual
/// core − renderIdeal; the residual's RMS re the core's RMS is logged (diagnostic, not gated).
/// Returns the detections.
std::vector<Krate::DSP::TestUtils::ClickDetection> residualClicks(Krate::DSP::TestUtils::ClickDetector& det,
                                                                  const std::vector<float>& core,
                                                                  const std::vector<double>& ideal,
                                                                  const char* fixture) {
    const std::vector<float> res = PT::residual(core, ideal);
    const auto clicks = det.detect(res.data(), res.size());
    const double reDb = PT::rmsDbfs(res.data(), res.size()) - PT::rmsDbfs(core.data(), core.size());
    INFO(fixture << ": residual RMS " << reDb << " dB re core RMS; " << clicks.size() << " clicks, first at sample "
                 << (clicks.empty() ? 0 : clicks.front().sampleIndex));
    WARN("SC-010(a) " << fixture << ": residual RMS " << reDb << " dB re core RMS, " << clicks.size() << " clicks");
    return clicks;
}

}  // namespace

// =============================================================================
// SC-020(a), FR-043: nullptr after setFrequency(f) == a trajectory filled with f
// =============================================================================

TEST_CASE("ProfundumCore_PitchTrajectoryNullptr", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    auto a = std::make_unique<ProfundumCore>();
    auto b = std::make_unique<ProfundumCore>();
    a->prepare(kFs);
    b->prepare(kFs);
    const PT::Render preA = PT::renderCore(*a, PT::midGrid(), kC2, 0.2, RP::Reset, false, 37);
    const PT::Render preB = PT::renderCore(*b, PT::midGrid(), kC2, 0.2, RP::Reset, false, 37);
    REQUIRE(PT::samplesBitEqual(preA.L.data(), preB.L.data(), preA.L.size()));

    const std::size_t n = samplesAt(1.0, kFs);
    const std::vector<float> traj(n, 98.0f);
    std::vector<float> aL, aR, bL, bR;
    a->setFrequency(98.0f);
    renderInto(*a, aL, aR, n, 37);   // f0PerSample == nullptr
    renderTrajInto(*b, bL, bR, traj, 37);

    REQUIRE(PT::samplesBitEqual(aL.data(), bL.data(), n));
    REQUIRE(PT::samplesBitEqual(aR.data(), bR.data(), n));
    PT::requireLREqual(aL.data(), aR.data(), n);
    // Non-vacuity: both cores moved to 98 Hz and sound.
    REQUIRE(a->currentFrequency() == 98.0f);
    REQUIRE(b->currentFrequency() == 98.0f);
    REQUIRE(a->baseFrequency() == 98.0f);
    REQUIRE(std::any_of(aL.begin(), aL.end(), [](float v) { return v != 0.0f; }));
}

// =============================================================================
// SC-020(b), FR-043: the same trajectory renders bit-identically at host blocks 512, 64, 37
// =============================================================================

TEST_CASE("ProfundumCore_PitchTrajectoryBlockSizeIndependent", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    const std::array<std::vector<float>, 2> trajectories{
        dropTrajectory(kFs, kC3, kC1, 0.2, 1.0),    // 24-semitone 10 ms drop at 0.2 s, then held
        vibratoTrajectory(kFs, kC2, 1.0)};          // 5 Hz ±2 semitones around C2

    for (std::size_t ti = 0; ti < trajectories.size(); ++ti) {
        INFO("trajectory #" << ti << (ti == 0 ? " (drop)" : " (vibrato)"));
        const std::vector<float>& traj = trajectories[ti];
        REQUIRE(traj.size() == samplesAt(1.0, kFs));

        auto ref = std::make_unique<ProfundumCore>();
        ref->prepare(kFs);
        const PT::Render r512 = renderTrajectory(*ref, PT::midGrid(), traj, kFs, 512, false);
        PT::requireLREqual(r512.L.data(), r512.R.data(), r512.L.size());
        REQUIRE(std::any_of(r512.L.begin(), r512.L.end(), [](float v) { return v != 0.0f; }));

        for (const std::size_t block : {std::size_t{64}, std::size_t{37}}) {
            INFO("block " << block);
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(kFs);
            const PT::Render r = renderTrajectory(*core, PT::midGrid(), traj, kFs, block, false);
            REQUIRE(PT::samplesBitEqual(r.L.data(), r512.L.data(), r.L.size()));
            REQUIRE(PT::samplesBitEqual(r.R.data(), r512.R.data(), r.R.size()));
        }
    }
}

// =============================================================================
// SC-020(c), FR-043: h1 phase tracks ∫f0PerSample dt within the U sample-and-hold bound
// =============================================================================

TEST_CASE("ProfundumCore_PitchTrajectorySampleAccurate", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 512;
    const std::size_t dropBlock = 47 * kBlock;    // 24 064 samples (~0.5 s) of C3 first
    const std::size_t dropStart = dropBlock + 100;
    const std::size_t dropEnd = dropBlock + 400;  // inclusive: the drop lies inside one 512-block
    const std::size_t total = dropBlock + 40 * kBlock;

    std::vector<float> traj(total, kC1);
    std::fill(traj.begin(), traj.begin() + static_cast<std::ptrdiff_t>(dropStart), kC3);
    const std::vector<float> ramp = PT::logLinearRamp(kC3, kC1, dropEnd - dropStart + 1);
    std::copy(ramp.begin(), ramp.end(), traj.begin() + static_cast<std::ptrdiff_t>(dropStart));
    REQUIRE(traj[dropStart] == kC3);
    REQUIRE(traj[dropEnd] == kC1);

    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    const PT::Render r = renderTrajectory(*core, SpectralShapeRecipe::kSineAnchor, traj, kFs, kBlock, false);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());

    // Reference phase in cycles: cum[k] = Σ_{j<k} f0[j]/fs (output sample k is rendered before
    // the oscillator advances), accumulated in double.
    std::vector<double> cum(total + 1, 0.0);
    for (std::size_t k = 0; k < total; ++k)
        cum[k + 1] = cum[k] + static_cast<double>(traj[k]) / kFs;

    const double fHi = static_cast<double>(kC3);
    const double fLo = static_cast<double>(kC1);
    // Constant offset fitted before the drop (8 periods of C3 ending 16 samples before it).
    const auto lenHi = static_cast<std::size_t>(std::llround(8.0 * kFs / fHi));
    const std::size_t pFit = dropStart - lenHi - 16;
    const double offset = h1PhaseCycles(r.L, pFit, kFs, fHi) - cum[pFit];

    const double bound = 0.5 * (static_cast<double>(kU) / kFs) * std::abs(fLo - fHi) + 0.02;   // 0.0364
    REQUIRE(bound == Catch::Approx(0.0364).margin(1e-4));
    for (const std::size_t p : {dropEnd, dropEnd + samplesAt(0.100, kFs)}) {
        const double err = wrapHalfCycle(h1PhaseCycles(r.L, p, kFs, fLo) - cum[p] - offset);
        INFO("check point " << p << ": phase error " << err << " cycle, bound " << bound);
        REQUIRE(std::abs(err) <= bound);
    }
}

// =============================================================================
// Plan-internal (S4.6): the detune shadow is a bit-exact mirror, so 10^6 multiplier updates
// leave no frequency drift
// =============================================================================

namespace {

/// `updates` pitch updates (U = 16) of a 5 Hz ±2-semitone vibrato at C2 in 512-blocks, then 2 s
/// static at C2: the h1 frequency error is < 0.01 cent by the SC-001 method (phase advance between
/// two 1 s BH7 frames whose starts are 1 s apart).
void requireNoShadowDrift(std::size_t updates) {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 512;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    core->setRetriggerPhase(RP::Reset);
    core->setControls(PT::midGrid());
    core->noteOn(vibratoAt(0, kFs, kC2));

    const std::size_t vibratoSamples = updates * kU;
    std::array<float, kBlock> L{};
    std::array<float, kBlock> R{};
    std::array<float, kBlock> traj{};
    for (std::size_t pos = 0; pos < vibratoSamples; pos += kBlock) {
        const std::size_t n = std::min(kBlock, vibratoSamples - pos);
        for (std::size_t i = 0; i < n; ++i)
            traj[i] = vibratoAt(pos + i, kFs, kC2);
        core->processBlock(L.data(), R.data(), n, traj.data());
    }
    REQUIRE(core->stateFinite());

    core->setFrequency(kC2);
    std::vector<float> sL, sR;
    renderInto(*core, sL, sR, samplesAt(2.2, kFs), kBlock);
    PT::requireLREqual(sL.data(), sR.data(), sL.size());
    REQUIRE(core->currentFrequency() == kC2);

    const double f = static_cast<double>(kC2);
    const std::size_t frame = samplesAt(1.0, kFs);
    const std::size_t a = samplesAt(0.1, kFs);
    const std::size_t b = a + frame;
    REQUIRE(b + frame <= sL.size());
    const double phA = PT::goertzelPhase(sL.data() + a, frame, kFs, f);
    const double phB = PT::goertzelPhase(sL.data() + b, frame, kFs, f);
    // Over exactly 1 s the nominal advance is f cycles; the residual is the frequency error in Hz.
    const double deltaHz = wrapHalfCycle((phB - phA) / PT::kTwoPi - (f - std::floor(f)));
    const double cents = 1200.0 * std::log2((f + deltaHz) / f);
    INFO("after " << updates << " pitch updates: h1 error " << cents << " cent");
    REQUIRE(std::abs(cents) < 0.01);
}

}  // namespace

TEST_CASE("ProfundumCore_DetuneShadowNoDrift", "[systems][profundum][long]") {
    requireNoShadowDrift(1000000);   // 333.3 s at 48 kHz
}

TEST_CASE("ProfundumCore_DetuneShadowNoDriftSmoke", "[systems][profundum]") {
    requireNoShadowDrift(100000);    // 33.3 s at 48 kHz
}

// =============================================================================
// FR-043 crossfade clause: the bank's base pitch never steps > 0.9 semitone per interval
// =============================================================================

TEST_CASE("ProfundumCore_NoCrossfadeOnGlide", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    const double ceiling = static_cast<double>(ProfundumCore::kMaxBaseStepSemitones) + 1e-9;

    SECTION("24-semitone 10 ms drop") {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = renderTrajectory(*core, PT::midGrid(), dropTrajectory(kFs, kC3, kC1, 0.2, 1.0), kFs,
                                              kInterval, true);
        PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
        const double worst = maxBaseStepSemitones(r);
        INFO("max base step " << worst << " semitones");
        REQUIRE(worst <= ceiling);
        REQUIRE(worst > 0.85);                // non-vacuity: the chase clamp engaged
        REQUIRE(r.baseF0.back() == kC1);      // and the base caught up with f after the drop
    }

    SECTION("SC-009 bend legs MIDI 24 -> 36") {
        std::vector<float> t(samplesAt(0.050, kFs), midiToHz(24));
        for (int m = 25; m <= 36; ++m)
            appendGlide(t, midiToHz(m), samplesAt(0.010, kFs), samplesAt(0.050, kFs));
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = renderTrajectory(*core, PT::midGrid(), t, kFs, kInterval, true);
        PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
        const double worst = maxBaseStepSemitones(r);
        INFO("max base step " << worst << " semitones");
        REQUIRE(worst <= ceiling);
        REQUIRE(worst > 0.0);
        for (std::size_t k = 0; k < r.baseF0.size(); ++k) {
            INFO("record " << k);
            REQUIRE(std::bit_cast<std::uint32_t>(r.baseF0[k]) == std::bit_cast<std::uint32_t>(r.maskF0[k]));
        }
    }

    SECTION("2 s, 5 Hz ±2-semitone vibrato") {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r =
            renderTrajectory(*core, PT::midGrid(), vibratoTrajectory(kFs, kC2, 2.0), kFs, kInterval, true);
        PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
        const double worst = maxBaseStepSemitones(r);
        INFO("max base step " << worst << " semitones");
        REQUIRE(worst <= ceiling);
        REQUIRE(worst > 0.0);
        for (std::size_t k = 0; k < r.baseF0.size(); ++k) {
            INFO("record " << k);
            REQUIRE(std::bit_cast<std::uint32_t>(r.baseF0[k]) == std::bit_cast<std::uint32_t>(r.maskF0[k]));
        }
    }

    SECTION("positive control: a FreeRunning noteOn 2 semitones away is a 2-semitone base step") {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = PT::renderCore(*core, PT::midGrid(), kC2, 0.2, RP::Reset, true, 64);
        const float before = r.baseF0.back();
        REQUIRE(before == kC2);
        core->setRetriggerPhase(RP::FreeRunning);
        core->noteOn(static_cast<float>(static_cast<double>(kC2) * std::exp2(2.0 / 12.0)));
        std::array<float, 64> L{};
        std::array<float, 64> R{};
        core->processBlock(L.data(), R.data(), L.size());
        const double step = 12.0 * std::log2(static_cast<double>(core->baseFrequency()) / static_cast<double>(before));
        REQUIRE(step == Catch::Approx(2.0).margin(1e-4));
    }
}

// =============================================================================
// SC-021(b)/(e), FR-051: the mask follows pitch unslewed, bounded by L_f × octaves moved
// SC-021(c): the cap tracks pitch exactly
// =============================================================================

TEST_CASE("ProfundumCore_MaskNeverSlewedBend", "[systems][profundum]") {
    constexpr double kFs = 48000.0;

    SECTION("SC-009 legs MIDI 24 -> 60 -> 24") {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = renderTrajectory(*core, PT::midGrid(), bendLegs(kFs, 24, 60), kFs, kInterval, true);
        PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
        REQUIRE(requireMaskStepBound(r) > 0.0);   // non-vacuity: the cap taper moved the vector
    }

    SECTION("±12-semitone 50 ms bends at C2") {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = renderTrajectory(*core, PT::midGrid(), bendAroundC2(kFs), kFs, kInterval, true);
        PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
        (void)requireMaskStepBound(r);
    }

    SECTION("24-semitone 10 ms drop from C2 through the guard onset") {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = renderTrajectory(*core, PT::midGrid(), dropTrajectory(kFs, kC2, 16.35f, 0.2, 1.0),
                                              kFs, kInterval, true);
        PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
        REQUIRE(r.maskF0.back() < SpectralShapeRecipe::kLowNoteGuardOnsetHz);   // the guard engaged
        REQUIRE(requireMaskStepBound(r) > 0.0);
    }
}

TEST_CASE("ProfundumCore_MaskNeverSlewedBendLong", "[systems][profundum][long]") {
    constexpr double kFs = 48000.0;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    const PT::Render r = renderTrajectory(*core, PT::midGrid(), bendLegs(kFs, 24, 108), kFs, kInterval, true);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    REQUIRE(requireMaskStepBound(r) > 0.0);
    REQUIRE(requireCapExact(r, kFs) > 0);
}

TEST_CASE("ProfundumCore_CapTracksPitchExactly", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    const std::array<std::vector<float>, 4> fixtures{
        bendLegs(kFs, 24, 60),
        bendAroundC2(kFs),
        dropTrajectory(kFs, kC2, 16.35f, 0.2, 1.0),
        // Non-vacuity: the three fixtures above never put a partial (n <= 64) at the 48 kHz cap
        // (64 × 261.6 Hz = 16.7 kHz < 19.2 kHz), so the SC-009 legs' top octave runs per-push too.
        bendLegs(kFs, 96, 108)};
    std::size_t covered = 0;
    for (std::size_t i = 0; i < fixtures.size(); ++i) {
        INFO("fixture #" << i);
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = renderTrajectory(*core, PT::midGrid(), fixtures[i], kFs, kInterval, true);
        covered += requireCapExact(r, kFs);
    }
    REQUIRE(covered > 0);
}

// =============================================================================
// E-7: a FreeRunning jump of ±12 / ±24 semitones stays finite and bounded, masked at once
// =============================================================================

TEST_CASE("ProfundumCore_LargeFreeRunningJumpBounded", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    const float capHz = SpectralShapeRecipe::capFrequency(kFs);
    const std::array<Controls, 2> states{SpectralShapeRecipe::kSawAnchor, PT::midGrid()};

    for (std::size_t si = 0; si < states.size(); ++si) {
        for (const int semis : {12, -12, 24, -24}) {
            INFO("state #" << si << " jump " << semis << " semitones");
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(kFs);
            const PT::Render pre = PT::renderCore(*core, states[si], kC2, 0.3, RP::Reset, false, 64);
            requireFiniteBounded(pre.L);

            const float f = static_cast<float>(static_cast<double>(kC2) * std::exp2(static_cast<double>(semis) / 12.0));
            core->setRetriggerPhase(RP::FreeRunning);
            core->noteOn(f);
            std::vector<float> L, R;
            renderInto(*core, L, R, 64, 64);

            // The first delivered vector is already masked at the new f0 (FR-051(d)).
            REQUIRE(core->maskFrequency() == f);
            REQUIRE(core->baseFrequency() == f);
            const auto d = core->deliveredGains();
            for (std::size_t n = 2; n <= d.size(); ++n) {
                if (static_cast<float>(n) * f >= capHz) {
                    INFO("harmonic " << n);
                    REQUIRE(d[n - 1] == 0.0f);
                }
            }
            requireUnitPower(d);

            renderInto(*core, L, R, samplesAt(0.3, kFs), 64);
            requireFiniteBounded(L);
            requireFiniteBounded(R);
            PT::requireLREqual(L.data(), R.data(), L.size());
            REQUIRE(core->stateFinite());
            REQUIRE(std::any_of(L.begin(), L.end(), [](float v) { return v != 0.0f; }));
        }
    }
}

// =============================================================================
// SC-010(a): no clicks under control steps/sweeps and pitch bends; the detector has teeth.
// Amended 2026-10-10 (D-1/D-2 and D-3 rulings): the detector runs on the residual
// core − PT::renderIdeal (per-sample targets -> continuous FR-050 slew -> 2 ms one-pole from zero,
// Reset phases, per-sample f, same mask), so the residual is the control-rate and pitch-update
// quantisation only. The bars are unchanged.
// D-4 ruling (2026-10-10): kControlInterval = 32 (plan S9.4), and the control-arm ideal is centred
// on the core's zero-order hold by its mean delay: the core moves a whole slew step at each interval
// start, so it leads the per-sample ideal by about half an interval. Steps lead the ideal by 16
// samples (the ruled value: the probe's green column; 15 read 1 click on shift), sweeps by
// (I + 1)/2 = 16.5 samples.
// =============================================================================

TEST_CASE("ProfundumCore_NoZipperControlSweeps", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = ProfundumCore::kControlInterval;   // one control update per block
    constexpr std::size_t kStepLead = kBlock / 2;                      // 16 samples (D-4 ruling)
    constexpr double kSweepLead = (static_cast<double>(kBlock) + 1.0) / 2.0;   // 16.5 samples (D-4 ruling)
    auto det = makeClickDetector();
    const auto f0C2 = [](std::size_t) { return static_cast<double>(kC2); };

    for (const ControlAxis& axis : kAxes) {
        INFO("axis " << axis.name);
        const auto withValue = [&](float v) {
            Controls c = PT::midGrid();
            c.*(axis.field) = v;
            return c;
        };
        const Controls cLo = withValue(axis.lo);
        const Controls cHi = withValue(axis.hi);
        const std::size_t hold = samplesAt(0.5, kFs);

        {   // lo -> hi -> lo step with 0.5 s holds
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(kFs);
            core->setRetriggerPhase(RP::Reset);
            core->setControls(cLo);
            core->noteOn(kC2);
            std::vector<float> L, R;
            renderInto(*core, L, R, hold, kBlock);
            core->setControls(cHi);
            renderInto(*core, L, R, hold, kBlock);
            core->setControls(cLo);
            renderInto(*core, L, R, hold, kBlock);
            PT::requireLREqual(L.data(), R.data(), L.size());
            // hold is a multiple of kControlInterval, so the core latches each step at its own sample;
            // the ideal's steps lead those latches by kStepLead (D-4 ruling).
            const std::vector<double> ideal = PT::renderIdeal(
                kFs, L.size(),
                [&](std::size_t i) {
                    const std::size_t j = i + kStepLead;
                    return (j >= hold && j < 2 * hold) ? cHi : cLo;
                },
                f0C2);
            const auto clicks = residualClicks(det, L, ideal, (std::string(axis.name) + " step").c_str());
            REQUIRE(clicks.empty());
        }

        {   // 10 ms full-range sweep lo -> hi, controls set before every block
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(kFs);
            core->setRetriggerPhase(RP::Reset);
            core->setControls(cLo);
            core->noteOn(kC2);
            std::vector<float> L, R;
            renderInto(*core, L, R, hold, kBlock);
            const std::size_t sweepSamples = samplesAt(0.010, kFs);
            for (std::size_t pos = 0; pos < sweepSamples; pos += kBlock) {
                const float t = std::min(1.0f, static_cast<float>(pos + kBlock) / static_cast<float>(sweepSamples));
                core->setControls(withValue(axis.lo + (axis.hi - axis.lo) * t));
                renderInto(*core, L, R, std::min(kBlock, sweepSamples - pos), kBlock);
            }
            renderInto(*core, L, R, hold, kBlock);
            PT::requireLREqual(L.data(), R.data(), L.size());
            // The core holds t = (pos + kBlock) / sweepSamples over [hold + pos, hold + pos + kBlock).
            // The ideal is the continuous linear ramp centred on that zero-order hold (D-4 ruling):
            // its mean over each held block equals the held value, i.e. the ramp
            // t = (i + (kBlock + 1)/2 − hold) / sweepSamples.
            const auto sweepAt = [&](std::size_t i) {
                const double t = std::clamp((static_cast<double>(i) + kSweepLead - static_cast<double>(hold))
                                                / static_cast<double>(sweepSamples),
                                            0.0, 1.0);
                return withValue(axis.lo + (axis.hi - axis.lo) * static_cast<float>(t));
            };
            const std::vector<double> ideal = PT::renderIdeal(kFs, L.size(), sweepAt, f0C2);
            const auto clicks = residualClicks(det, L, ideal, (std::string(axis.name) + " sweep").c_str());
            REQUIRE(clicks.empty());
        }
    }
}

TEST_CASE("ProfundumCore_NoZipperPitchBend", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    auto det = makeClickDetector();
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    const std::vector<float> traj = bendAroundC2(kFs);
    const Controls mid = PT::midGrid();
    const PT::Render r = renderTrajectory(*core, mid, traj, kFs, 64, false);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    // Per-sample f from the same trajectory (never the core's U-held f), delayed by the U-hold's
    // group delay (U − 1)/2 so the residual is the pitch-update quantisation only.
    const std::vector<double> ideal = PT::renderIdeal(
        kFs, r.L.size(), [&](std::size_t) { return mid; },
        [&](std::size_t i) { return PT::pitchHoldAlignedF0(traj, i); });
    const auto clicks = residualClicks(det, r.L, ideal, "+-12 semitone bend");
    REQUIRE(clicks.empty());
}

TEST_CASE("ProfundumCore_ClickDetectorPositiveControl", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kSplice = 24000;
    auto det = makeClickDetector();
    const auto f0C2 = [](std::size_t) { return static_cast<double>(kC2); };

    for (const ControlAxis& axis : kAxes) {
        INFO("axis " << axis.name);
        Controls cLo = PT::midGrid();
        cLo.*(axis.field) = axis.lo;
        Controls cHi = PT::midGrid();
        cHi.*(axis.field) = axis.hi;

        // Both steady renders start from a Reset noteOn, so their phases are identical; the
        // splice is an instantaneous spectrum (gain) change at one sample.
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render a = PT::renderCore(*core, cLo, kC2, 1.0);
        const PT::Render b = PT::renderCore(*core, cHi, kC2, 1.0);
        REQUIRE(a.L.size() == samplesAt(1.0, kFs));
        std::vector<float> spliced(a.L.begin(), a.L.begin() + static_cast<std::ptrdiff_t>(kSplice));
        spliced.insert(spliced.end(), b.L.begin() + static_cast<std::ptrdiff_t>(kSplice), b.L.end());
        REQUIRE(spliced.size() == a.L.size());

        // Minus the (slewed, one-pole smoothed) ideal of the un-spliced lo -> hi step at kSplice:
        // the residual keeps the splice's jump (D-3 ruling).
        const std::vector<double> ideal = PT::renderIdeal(
            kFs, spliced.size(), [&](std::size_t i) { return i >= kSplice ? cHi : cLo; }, f0C2);
        const std::vector<float> res = PT::residual(spliced, ideal);
        const auto clicks = det.detect(res.data(), res.size());
        INFO(clicks.size() << " clicks on spliced - ideal, first at sample "
                           << (clicks.empty() ? 0 : clicks.front().sampleIndex));
        REQUIRE(clicks.size() >= 1);
    }
}

// =============================================================================
// FR-041 / E-6: sample-rate clamp, Δmax, and the remaining arms at 22.05 and 192 kHz
// =============================================================================

TEST_CASE("ProfundumCore_SampleRateExtremes", "[systems][profundum]") {
    const double sqrtP0 = std::sqrt(static_cast<double>(SpectralShapeRecipe::kPowerTarget));

    for (const double fs : {22050.0, 48000.0, 192000.0}) {
        INFO("fs = " << fs);
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(fs);
        const double expected = 375.0 * sqrtP0 * static_cast<double>(ProfundumCore::kControlInterval) / fs;
        REQUIRE(static_cast<double>(core->maxShapeStepPerInterval()) == Catch::Approx(expected).epsilon(1e-6));
    }

    {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(48000.0);
        // 375·√P0·kControlInterval/fs at 48 kHz / 32 (D-4 ruling; 0.25119 at the former 64).
        REQUIRE(static_cast<double>(core->maxShapeStepPerInterval()) == Catch::Approx(0.125594).margin(1e-5));
    }

    // Below/above the supported range clamps to the edge, bit for bit.
    const auto requireSamePrepared = [](double outside, double edge) {
        INFO("prepare(" << outside << ") vs prepare(" << edge << ")");
        auto a = std::make_unique<ProfundumCore>();
        auto b = std::make_unique<ProfundumCore>();
        a->prepare(outside);
        b->prepare(edge);
        REQUIRE(std::bit_cast<std::uint32_t>(a->maxShapeStepPerInterval())
                == std::bit_cast<std::uint32_t>(b->maxShapeStepPerInterval()));
        REQUIRE(a->numPartials() == b->numPartials());
        const auto sa = a->shapeGains();
        const auto sb = b->shapeGains();
        REQUIRE(sa.size() == sb.size());
        REQUIRE(PT::samplesBitEqual(sa.data(), sb.data(), sa.size()));
        REQUIRE(std::bit_cast<std::uint32_t>(a->currentFrequency())
                == std::bit_cast<std::uint32_t>(b->currentFrequency()));
        REQUIRE(std::bit_cast<std::uint32_t>(a->baseFrequency())
                == std::bit_cast<std::uint32_t>(b->baseFrequency()));

        constexpr std::size_t kN = 1024;
        std::vector<float> aL(kN, 1.0f), aR(kN, 1.0f), bL(kN, 1.0f), bR(kN, 1.0f);
        a->processBlock(aL.data(), aR.data(), kN);
        b->processBlock(bL.data(), bR.data(), kN);
        REQUIRE(PT::samplesBitEqual(aL.data(), bL.data(), kN));
        REQUIRE(PT::samplesBitEqual(aR.data(), bR.data(), kN));
    };
    requireSamePrepared(16000.0, 22050.0);
    requireSamePrepared(400000.0, 192000.0);

    // Remaining arms (T011) at the two extremes.
    for (const double fs : {22050.0, 192000.0}) {
        INFO("extreme fs = " << fs);

        {   // Finite output and state; SC-018 level at mid grid / C2.
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(fs);
            const PT::Render r = PT::renderCore(*core, PT::midGrid(), kC2, 1.2);
            requireFiniteBounded(r.L);
            requireFiniteBounded(r.R);
            PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
            REQUIRE(core->stateFinite());
            const std::size_t skip = samplesAt(0.150, fs);
            const double db = PT::rmsDbOverPeriods(r.L.data() + skip, r.L.size() - skip, fs,
                                                   static_cast<double>(kC2), 1.0);
            INFO("RMS " << db << " dBFS");
            REQUIRE(db == Catch::Approx(static_cast<double>(SpectralShapeRecipe::kCoreOutputRmsDb)).margin(0.1));
        }

        {   // SC-021(c) over a ±12-semitone 50 ms bend, and SC-021(e) on every delivered vector.
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(fs);
            const PT::Render r = renderTrajectory(*core, PT::midGrid(), bendAroundC2(fs), fs, kInterval, true);
            requireFiniteBounded(r.L);
            PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
            (void)requireCapExact(r, fs);
            for (std::size_t k = 0; k < r.delivered.size(); ++k) {
                INFO("record " << k);
                requireUnitPower(r.delivered[k]);
            }
            REQUIRE(core->stateFinite());
        }

        {   // SC-010(d) step ceiling: Depth 0 -> 1 -> 0 with 0.1 s holds, grid-aligned kControlInterval blocks.
            constexpr std::size_t kBlock = ProfundumCore::kControlInterval;
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(fs);
            const float dMax = core->maxShapeStepPerInterval();
            Controls lo = PT::midGrid();
            lo.depth = 0.0f;
            Controls hi = PT::midGrid();
            hi.depth = 1.0f;
            std::vector<std::vector<float>> shapes;
            std::array<float, kBlock> L{};
            std::array<float, kBlock> R{};
            const std::size_t holdBlocks = samplesAt(0.1, fs) / kBlock;
            const auto hold = [&] {
                for (std::size_t k = 0; k < holdBlocks; ++k) {
                    core->processBlock(L.data(), R.data(), kBlock);
                    const auto s = core->shapeGains();
                    shapes.emplace_back(s.begin(), s.end());
                    requireUnitPower(s);
                    requireUnitPower(core->deliveredGains());
                }
            };
            core->setRetriggerPhase(RP::Reset);
            core->setControls(lo);
            core->noteOn(kC2);
            hold();
            core->setControls(hi);
            hold();
            core->setControls(lo);
            hold();
            float worst = 0.0f;
            for (std::size_t k = 1; k < shapes.size(); ++k)
                for (std::size_t n = 0; n < shapes[k].size(); ++n)
                    worst = std::max(worst, std::abs(shapes[k][n] - shapes[k - 1][n]));
            INFO("max |delta shape| " << worst << ", ceiling " << dMax);
            REQUIRE(worst <= dMax);
            REQUIRE(worst > 0.0f);
            REQUIRE(bitEqual(core->shapeGains(), PT::shapeOf(lo, 64)));   // converged back
        }
    }
}

// =============================================================================
// SC-014 (a)-(c), FR-048, FR-072: the partial-pan hook and its zero/return state machine
// =============================================================================

namespace {

using PanOffsets = std::array<float, Krate::DSP::kMaxPartials>;

/// The 12 named, colour and mid states (the SC-003 render-arm coordinates).
std::array<Controls, 12> namedColourMidStates() {
    return {SpectralShapeRecipe::kSineAnchor, SpectralShapeRecipe::kTriangleAnchor, SpectralShapeRecipe::kSawAnchor,
            SpectralShapeRecipe::kHeavy,      SpectralShapeRecipe::kHollow,         SpectralShapeRecipe::kGrowl,
            SpectralShapeRecipe::kBodyRound,  SpectralShapeRecipe::kBodyHollow,     SpectralShapeRecipe::kBodyWoody,
            SpectralShapeRecipe::kBodyNasal,  SpectralShapeRecipe::kBodyThick,      PT::midGrid()};
}

/// SC-014(b) offsets: o[i] = (i % 2 ? magnitude : -magnitude).
PanOffsets alternatingPan(float magnitude) {
    PanOffsets o{};
    for (std::size_t i = 0; i < o.size(); ++i)
        o[i] = (i % 2 != 0) ? magnitude : -magnitude;
    return o;
}

/// An all-zero vector with -0.0f in every third element.
PanOffsets signedZeroPan() {
    PanOffsets z{};
    for (std::size_t i = 0; i < z.size(); i += 3)
        z[i] = -0.0f;
    return z;
}

bool allFinite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return Krate::DSP::detail::isFinite(v); });
}

}  // namespace

TEST_CASE("ProfundumCore_ZeroPanBitIdentical", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 64;

    SECTION("named, colour and mid states at C2") {
        const auto states = namedColourMidStates();
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        for (std::size_t i = 0; i < states.size(); ++i) {
            INFO("state #" << i);
            const PT::Render r = PT::renderCore(*core, states[i], kC2, 0.3, RP::Reset, false, kBlock);
            PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
        }
    }

    SECTION("+-12-semitone bend") {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = renderTrajectory(*core, PT::midGrid(), bendAroundC2(kFs), kFs, kBlock, false);
        PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    }

    SECTION("2 Hz control sweep") {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        core->setRetriggerPhase(RP::Reset);
        core->setControls(PT::midGrid());
        core->noteOn(kC2);
        std::vector<float> L, R;
        const std::size_t total = samplesAt(1.0, kFs);
        for (std::size_t pos = 0; pos < total; pos += kBlock) {
            const double t = static_cast<double>(pos) / kFs;
            const auto u = static_cast<float>(0.5 + 0.5 * std::sin(PT::kTwoPi * 2.0 * t));
            Controls c = PT::midGrid();
            c.depth = u;
            c.body = 1.0f - u;
            c.edge = u;
            c.shift = 2.0f * u - 1.0f;
            core->setControls(c);
            renderInto(*core, L, R, std::min(kBlock, total - pos), kBlock);
        }
        PT::requireLREqual(L.data(), R.data(), L.size());
    }

    SECTION("an explicit all-zero vector (with -0.0f) is never forwarded") {
        // Core A receives zero vectors before the note and twice while sounding; twin B never
        // calls the setter. Any applyPanOffsets call would rewrite the centre tables through
        // cos/sin(pi/4) and break bit-identity with B.
        auto a = std::make_unique<ProfundumCore>();
        auto b = std::make_unique<ProfundumCore>();
        a->prepare(kFs);
        b->prepare(kFs);
        const PanOffsets zeros{};
        const PanOffsets signedZeros = signedZeroPan();
        a->setPartialPanOffsets(signedZeros);
        for (ProfundumCore* core : {a.get(), b.get()}) {
            core->setRetriggerPhase(RP::Reset);
            core->setControls(PT::midGrid());
            core->noteOn(kC2);
        }
        std::vector<float> aL, aR, bL, bR;
        renderInto(*a, aL, aR, samplesAt(0.1, kFs), kBlock);
        renderInto(*b, bL, bR, samplesAt(0.1, kFs), kBlock);
        a->setPartialPanOffsets(zeros);
        renderInto(*a, aL, aR, samplesAt(0.1, kFs), kBlock);
        renderInto(*b, bL, bR, samplesAt(0.1, kFs), kBlock);
        a->setPartialPanOffsets(signedZeros);
        renderInto(*a, aL, aR, samplesAt(0.2, kFs), kBlock);
        renderInto(*b, bL, bR, samplesAt(0.2, kFs), kBlock);

        REQUIRE(aL.size() == bL.size());
        PT::requireLREqual(aL.data(), aR.data(), aL.size());
        REQUIRE(PT::samplesBitEqual(aL.data(), bL.data(), aL.size()));
        REQUIRE(PT::samplesBitEqual(aR.data(), bR.data(), aR.size()));
    }
}

TEST_CASE("ProfundumCore_PanHookForwards", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 64;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    core->setPartialPanOffsets(alternatingPan(0.5f));
    const PT::Render r = PT::renderCore(*core, PT::midGrid(), kC2, 0.5, RP::Reset, false, kBlock);

    std::size_t differing = 0;
    const std::size_t from = ProfundumCore::kControlInterval;   // after the first control interval
    for (std::size_t i = from; i < r.L.size(); ++i)
        if (std::bit_cast<std::uint32_t>(r.L[i]) != std::bit_cast<std::uint32_t>(r.R[i]))
            ++differing;
    const double fraction = static_cast<double>(differing) / static_cast<double>(r.L.size() - from);
    INFO("L != R on " << fraction * 100.0 << " % of samples");
    REQUIRE(fraction >= 0.99);
    REQUIRE(allFinite(r.L));
    REQUIRE(allFinite(r.R));
    REQUIRE(core->stateFinite());
}

TEST_CASE("ProfundumCore_PanReturnToCentreBitIdentical", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 64;   // grid-aligned: one control update at every block start
    const PanOffsets zeros{};
    const PanOffsets o = alternatingPan(0.5f);

    auto a = std::make_unique<ProfundumCore>();
    auto b = std::make_unique<ProfundumCore>();
    for (ProfundumCore* core : {a.get(), b.get()}) {
        core->prepare(kFs);
        core->setPartialPanOffsets(zeros);
        core->setRetriggerPhase(RP::Reset);
        core->setControls(PT::midGrid());
        core->noteOn(kC2);
    }
    std::vector<float> aL, aR, bL, bR;
    const std::size_t pre = 75 * kBlock;   // 0.1 s, a whole number of control intervals
    renderInto(*a, aL, aR, pre, kBlock);
    renderInto(*b, bL, bR, pre, kBlock);

    // The excursion (A only): zeros -> o, 256 samples, -> zeros.
    constexpr std::size_t kExcursion = 256;
    a->setPartialPanOffsets(o);
    renderInto(*a, aL, aR, kExcursion, kBlock);
    renderInto(*b, bL, bR, kExcursion, kBlock);
    // Non-vacuity: the excursion was audible on A.
    REQUIRE(!PT::samplesBitEqual(aL.data() + pre, aR.data() + pre, kExcursion));
    REQUIRE(!PT::samplesBitEqual(aL.data() + pre, bL.data() + pre, kExcursion));
    a->setPartialPanOffsets(zeros);

    // From the first control interval after the return (the next block start), for 1 s.
    const std::size_t from = aL.size();
    const std::size_t span = samplesAt(1.0, kFs);
    renderInto(*a, aL, aR, span, kBlock);
    renderInto(*b, bL, bR, span, kBlock);

    REQUIRE(PT::samplesBitEqual(aL.data() + from, aR.data() + from, span));
    REQUIRE(PT::samplesBitEqual(aL.data() + from, bL.data() + from, span));
    REQUIRE(PT::samplesBitEqual(aR.data() + from, bR.data() + from, span));
    REQUIRE(a->stateFinite());
}

TEST_CASE("ProfundumCore_PanLargeOffsetBounded", "[systems][profundum]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 64;

    // The bank clamps panPosition + offset to [-1, 1] (HarmonicOscillatorBank::applyPanOffsets),
    // so +-1e30f must render exactly like +-1.0f.
    PanOffsets huge{};
    PanOffsets unit{};
    for (std::size_t i = 0; i < huge.size(); ++i) {
        huge[i] = (i % 2 != 0) ? 1e30f : -1e30f;
        unit[i] = (i % 2 != 0) ? 1.0f : -1.0f;
    }

    auto a = std::make_unique<ProfundumCore>();
    auto b = std::make_unique<ProfundumCore>();
    a->prepare(kFs);
    b->prepare(kFs);
    a->setPartialPanOffsets(huge);
    b->setPartialPanOffsets(unit);
    const PT::Render ra = PT::renderCore(*a, PT::midGrid(), kC2, 0.5, RP::Reset, false, kBlock);
    const PT::Render rb = PT::renderCore(*b, PT::midGrid(), kC2, 0.5, RP::Reset, false, kBlock);

    REQUIRE(allFinite(ra.L));
    REQUIRE(allFinite(ra.R));
    REQUIRE(a->stateFinite());
    REQUIRE(PT::samplesBitEqual(ra.L.data(), rb.L.data(), ra.L.size()));
    REQUIRE(PT::samplesBitEqual(ra.R.data(), rb.R.data(), ra.R.size()));
    // Non-vacuity: the offsets were forwarded (hard-panned partials, L != R).
    REQUIRE(!PT::samplesBitEqual(ra.L.data(), ra.R.data(), ra.L.size()));
}

// =============================================================================
// SC-015, FR-060: no allocation on the audio thread
// =============================================================================
// Counting relies on allocation_operator_overrides.h being linked into dsp_systems_tests from
// exactly one other TU; the liveness probe runs first so a mis-wired binary cannot pass
// vacuously. Nothing but the core runs inside a tracked window (no Catch2 macros).
TEST_CASE("ProfundumCore_NoAllocationOnAudioThread", "[systems][profundum]") {
    auto& detector = TestHelpers::AllocationDetector::instance();

    detector.startTracking();
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    int* probe = new int[16];
    probe[0] = 42;
    volatile int* probeSink = probe;   // defeat new/delete elision (N3664)
    const int probeObserved = probeSink[0];
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete[] probe;
    const std::size_t livenessCount = detector.stopTracking();
    REQUIRE(probeObserved == 42);
    REQUIRE(livenessCount >= std::size_t{1});

    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 64;
    constexpr std::size_t kBlocks = 40;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);   // control thread, outside the window

    std::array<float, kBlock> L{};
    std::array<float, kBlock> R{};
    std::array<float, kBlock> f0Traj{};
    for (std::size_t i = 0; i < kBlock; ++i)
        f0Traj[i] = kC2 * static_cast<float>(1.0 + 0.002 * static_cast<double>(i));
    const PanOffsets zeros{};
    const PanOffsets o = alternatingPan(0.5f);
    Controls lo = PT::midGrid();
    lo.depth = 0.0f;
    Controls hi = PT::midGrid();
    hi.depth = 1.0f;
    double sumSquares = 0.0;

    const auto run = [&](std::size_t blocks, const float* traj) {
        for (std::size_t k = 0; k < blocks; ++k) {
            core->processBlock(L.data(), R.data(), kBlock, traj);
            for (std::size_t i = 0; i < kBlock; ++i)
                sumSquares += static_cast<double>(L[i]) * static_cast<double>(L[i]);
        }
    };

    std::array<std::size_t, 4> counts{};

    // Setters and a Reset noteOn.
    detector.startTracking();
    core->setControls(lo);
    core->setFrequency(kC2);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core->noteOn(kC2);
    run(kBlocks, nullptr);
    counts[0] = detector.stopTracking();

    // A slew (lo -> hi), with and without f0PerSample.
    detector.startTracking();
    core->setControls(hi);
    run(kBlocks / 2, nullptr);
    run(kBlocks / 2, f0Traj.data());
    counts[1] = detector.stopTracking();

    // Pan: zero, non-zero, then zero (the return through restoreCenterPan).
    detector.startTracking();
    core->setPartialPanOffsets(zeros);
    run(4, nullptr);
    core->setPartialPanOffsets(o);
    run(4, f0Traj.data());
    core->setPartialPanOffsets(zeros);
    run(kBlocks, nullptr);
    counts[2] = detector.stopTracking();

    // A FreeRunning noteOn while sounding, then a Reset one.
    detector.startTracking();
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::FreeRunning);
    core->noteOn(kC3);
    run(kBlocks, nullptr);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core->noteOn(kC1);
    run(kBlocks, f0Traj.data());
    counts[3] = detector.stopTracking();

    INFO("allocations: setters+noteOn " << counts[0] << ", slew " << counts[1] << ", pan " << counts[2]
                                         << ", FreeRunning/Reset noteOn " << counts[3]);
    REQUIRE(counts[0] == 0);
    REQUIRE(counts[1] == 0);
    REQUIRE(counts[2] == 0);
    REQUIRE(counts[3] == 0);
    REQUIRE(sumSquares > 0.0);   // non-vacuity: the core rendered sound
    REQUIRE(core->stateFinite());
}
