// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// MassDynamics alone: SC-012, SC-013, SC-016, SC-018 dynamics arm; FR-013, FR-014, FR-015, FR-041,
// FR-047, FR-049
// ==============================================================================
// tasks.md T010 (plan S6, C-8, C-11; deviation 4). Tags [systems][profundum][mass-dynamics].
// Fixtures: ../processors/transient_shaper_test_helpers.h (kSawAnchor / bursts, notes) and, through
// it, ../processors/harmonic_fold_test_helpers.h (recipeAdditive for the reference voice).
//
// This TU is compiled in the shipping FP mode (not -fno-fast-math): non-finite arguments are built
// from bit patterns through a volatile sink, never from std::numeric_limits, and are never compared.
// No bit-exact float goldens: every bit-exact comparison is between two renders of this binary.
// TU-local names carry an `md` / `kMd` prefix (tasks.md rule 5).
// ==============================================================================

#include "mass_dynamics_probe.h"

#include "../processors/transient_shaper_test_helpers.h"

// allocation_detector.h ONLY: the global operator new/delete replacements are included from exactly
// one TU of dsp_systems_tests (selectable_oscillator_test.cpp).
#include <allocation_detector.h>

#include <krate/dsp/processors/period_peak_detector.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/primitives/dc_blocker.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

using namespace Krate::DSP;
namespace mdt = Krate::DSP::TransientShaperTest;
using MdProbe = Krate::DSP::detail::MassDynamicsProbe;

namespace {

constexpr double kMdFs = 48000.0;
constexpr std::size_t kMdCI = MassDynamics::kControlInterval;
constexpr std::size_t kMdMaxBlock = 512;
constexpr std::size_t kMdStepAt = 8704;   // lcm(17, 32, 64, 512), plan C-11

/// A float from its bit pattern through a volatile sink (fast-math safe).
float mdFloatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t read = sink;
    return std::bit_cast<float>(read);
}
float mdNaN() noexcept { return mdFloatFromBits(0x7FC00000u); }
float mdPosInf() noexcept { return mdFloatFromBits(0x7F800000u); }
float mdNegInf() noexcept { return mdFloatFromBits(0xFF800000u); }

bool mdBitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

std::vector<float> mdSaw(double f0, double fs, double seconds) {
    return mdt::steadyTone(mdt::Tone::Saw, f0, fs, seconds);
}

/// A control event at an absolute sample position.
struct MdEvent {
    std::size_t at;
    std::function<void(MassDynamics&)> apply;
};

/// One host-partitioned render, in place. A block is split at an event (sample-accurate host
/// automation), so an event lands at its absolute sample whatever the partition.
void mdRender(MassDynamics& md, float* L, float* R, std::size_t n, std::size_t block,
              const std::vector<MdEvent>& events = {}) {
    REQUIRE(block >= 1);
    std::size_t pos = 0;
    std::size_t e = 0;
    while (pos < n) {
        while (e < events.size() && events[e].at <= pos) {
            events[e].apply(md);
            ++e;
        }
        std::size_t len = std::min(block, n - pos);
        if (e < events.size() && events[e].at < pos + len)
            len = events[e].at - pos;
        md.processBlock(L + pos, R + pos, len);
        pos += len;
    }
}

/// The SC-018 / FR-049 starting settings: A = +12, S = -6, duration 40, the given amount, key E1.
void mdConfigure(MassDynamics& md, float amount) {
    md.setTransientAttackDb(12.0f);
    md.setTransientSustainDb(-6.0f);
    md.setTransientDurationMs(40.0f);
    md.setCompressionAmount(amount);
    md.setDetectorKeyHz(static_cast<float>(mdt::kE1Hz));
}

struct MdNote {
    const char* name;
    double hz;
};

}  // namespace

// =============================================================================
// 1. SC-012, FR-041 (FR-013): mono sum. One gain per stage on both channels.
// =============================================================================
TEST_CASE("MassDynamics_MonoSum", "[systems][profundum][mass-dynamics]") {
    const std::vector<float> x = mdt::burst(mdt::Tone::Saw, mdt::kE1Hz, kMdFs);

    SECTION("(a) L == R in gives L == R out, bit-identical, over the SC-008 grid x amounts") {
        constexpr std::array<float, 5> kGrid{-12.0f, -6.0f, 0.0f, 6.0f, 12.0f};
        constexpr std::array<float, 3> kAmounts{0.0f, 0.5f, 1.0f};
        std::size_t renders = 0;
        std::size_t unequal = 0;
        for (const float a : kGrid) {
            for (const float s : kGrid) {
                for (const float amount : kAmounts) {
                    std::vector<float> L = x;
                    std::vector<float> R = x;
                    MassDynamics md;
                    md.setTransientAttackDb(a);
                    md.setTransientSustainDb(s);
                    md.setCompressionAmount(amount);
                    md.setDetectorKeyHz(static_cast<float>(mdt::kE1Hz));
                    md.prepare(kMdFs, kMdMaxBlock);
                    mdRender(md, L.data(), R.data(), L.size(), kMdMaxBlock);
                    ++renders;
                    if (!mdBitEqual(L, R)) {
                        ++unequal;
                        std::printf("MassDynamics_MonoSum (a) A %.6g S %.6g amount %.6g: L != R\n",
                                    static_cast<double>(a), static_cast<double>(s), static_cast<double>(amount));
                    }
                }
            }
        }
        std::printf("MassDynamics_MonoSum (a): %zu renders, %zu with L != R\n", renders, unequal);
        REQUIRE(renders == kGrid.size() * kGrid.size() * kAmounts.size());
        REQUIRE(unequal == 0);
    }

    SECTION("(b) a 1 : 0.5 pair gets one gain; side / mid ratio unchanged") {
        std::vector<float> inR(x.size(), 0.0f);
        for (std::size_t n = 0; n < x.size(); ++n)
            inR[n] = 0.5f * x[n];
        std::vector<float> L = x;
        std::vector<float> R = inR;
        MassDynamics md;
        md.setTransientAttackDb(12.0f);
        md.setTransientSustainDb(-6.0f);
        md.setCompressionAmount(1.0f);
        md.setDetectorKeyHz(static_cast<float>(mdt::kE1Hz));
        md.prepare(kMdFs, kMdMaxBlock);
        float minGr = 0.0f;
        float maxShaperDb = -1000.0f;
        for (std::size_t pos = 0; pos < L.size(); pos += kMdCI) {
            const std::size_t len = std::min(kMdCI, L.size() - pos);
            md.processBlock(&L[pos], &R[pos], len);
            minGr = std::min(minGr, md.compressor().currentGainReductionDb());
            maxShaperDb = std::max(maxShaperDb, md.shaper().currentGainDb());
        }
        REQUIRE(minGr < -1.0f);         // the compressor engaged
        REQUIRE(maxShaperDb > 1.0f);    // the shaper boosted the onset

        std::size_t gainMismatch = 0;
        const double tol = 4.0 * static_cast<double>(FLT_EPSILON);
        for (std::size_t n = 0; n < L.size(); ++n) {
            const double lhs = static_cast<double>(L[n]) * static_cast<double>(inR[n]);
            const double rhs = static_cast<double>(R[n]) * static_cast<double>(x[n]);
            if (std::abs(lhs - rhs) > tol * std::abs(lhs))
                ++gainMismatch;
        }
        const double inRatio = mdt::sideMidRatioDb(x.data(), inR.data(), x.size());
        const double outRatio = mdt::sideMidRatioDb(L.data(), R.data(), L.size());
        std::printf("MassDynamics_MonoSum (b): min GR %.6g dB, max shaper gain %.6g dB, per-sample gain mismatches %zu, "
                    "side/mid in %.6g dB, out %.6g dB (|delta| %.6g)\n",
                    static_cast<double>(minGr), static_cast<double>(maxShaperDb), gainMismatch, inRatio, outRatio,
                    std::abs(outRatio - inRatio));
        REQUIRE(gainMismatch == 0);
        REQUIRE(std::abs(outRatio - inRatio) <= 0.01);
    }
}

// =============================================================================
// 2. SC-013: transparent at neutral (shaper 0 / 0, amount 0)
// =============================================================================
TEST_CASE("MassDynamics_TransparentAtNeutral", "[systems][profundum][mass-dynamics]") {
    const std::array<MdNote, 2> notes{{{"E0", mdt::kE0Hz}, {"C3", mdt::kC3Hz}}};
    for (const auto& note : notes) {
        for (int voice = 0; voice < 2; ++voice) {
            const char* voiceName = (voice == 0) ? "kSawAnchor" : "reference";
            const std::vector<float> x =
                (voice == 0) ? mdSaw(note.hz, kMdFs, 2.0)
                             : HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kDefaultControls, note.hz, kMdFs, 2.0);
            std::vector<float> L = x;
            std::vector<float> R = x;
            MassDynamics md;
            md.setTransientAttackDb(0.0f);
            md.setTransientSustainDb(0.0f);
            md.setCompressionAmount(0.0f);
            md.setDetectorKeyHz(static_cast<float>(note.hz));
            md.prepare(kMdFs, kMdMaxBlock);
            mdRender(md, L.data(), R.data(), L.size(), kMdMaxBlock);
            const std::size_t from = mdt::secondsToSamples(kMdFs, 0.1);
            REQUIRE(from < x.size());
            const std::size_t bytes = (x.size() - from) * sizeof(float);
            const bool lEqual = std::memcmp(L.data() + from, x.data() + from, bytes) == 0;
            const bool rEqual = std::memcmp(R.data() + from, x.data() + from, bytes) == 0;
            std::printf("MassDynamics_TransparentAtNeutral %s %s: L %s, R %s, shaper gain %.6g dB, compressor gain %.6g dB\n",
                        note.name, voiceName, lEqual ? "bit-equal" : "DIFFERS", rEqual ? "bit-equal" : "DIFFERS",
                        static_cast<double>(md.shaper().currentGainDb()),
                        static_cast<double>(md.compressor().currentGainDb()));
            REQUIRE(lEqual);
            REQUIRE(rEqual);
        }
    }
}

// =============================================================================
// 3. FR-015: the composition is TransientShaper -> BassCompressor, fixed
// =============================================================================
TEST_CASE("MassDynamics_ShaperThenCompressor", "[systems][profundum][mass-dynamics]") {
    const std::vector<float> x = mdSaw(mdt::kE1Hz, kMdFs, 3.0);
    const auto e1 = static_cast<float>(mdt::kE1Hz);

    std::vector<float> mL = x;
    std::vector<float> mR = x;
    MassDynamics md;
    mdConfigure(md, 1.0f);
    md.prepare(kMdFs, kMdMaxBlock);
    mdRender(md, mL.data(), mR.data(), mL.size(), kMdMaxBlock);

    // The same settings on a separately prepared pair, shaper first.
    const auto makePair = [e1](TransientShaper& ts, BassCompressor& bc) {
        ts.setAttackGainDb(12.0f);
        ts.setSustainGainDb(-6.0f);
        ts.setTransientDurationMs(40.0f);
        ts.prepare(kMdFs, kMdMaxBlock);
        bc.setAmount(1.0f);
        bc.setDetectorKeyHz(e1);
        bc.prepare(kMdFs, kMdMaxBlock);
    };
    std::vector<float> pL = x;
    std::vector<float> pR = x;
    {
        TransientShaper ts;
        BassCompressor bc;
        makePair(ts, bc);
        for (std::size_t pos = 0; pos < pL.size(); pos += kMdMaxBlock) {
            const std::size_t len = std::min(kMdMaxBlock, pL.size() - pos);
            ts.processBlock(&pL[pos], &pR[pos], len);
            bc.processBlock(&pL[pos], &pR[pos], len);
        }
    }

    // Negative control: the reversed order (compressor -> shaper) is a different sound, so the
    // equality above is not trivially met.
    std::vector<float> rL = x;
    std::vector<float> rR = x;
    {
        TransientShaper ts;
        BassCompressor bc;
        makePair(ts, bc);
        for (std::size_t pos = 0; pos < rL.size(); pos += kMdMaxBlock) {
            const std::size_t len = std::min(kMdMaxBlock, rL.size() - pos);
            bc.processBlock(&rL[pos], &rR[pos], len);
            ts.processBlock(&rL[pos], &rR[pos], len);
        }
    }

    const bool equalPair = mdBitEqual(mL, pL) && mdBitEqual(mR, pR);
    const bool equalReversed = mdBitEqual(mL, rL) && mdBitEqual(mR, rR);
    std::printf("MassDynamics_ShaperThenCompressor: vs shaper->compressor pair %s; vs reversed order %s\n",
                equalPair ? "bit-equal" : "DIFFERS", equalReversed ? "bit-equal" : "differs");
    REQUIRE(equalPair);
    CHECK_FALSE(equalReversed);
}

// =============================================================================
// 4. FR-049, plan C-11: setters latch on the absolute 32-sample grid
// =============================================================================
TEST_CASE("MassDynamics_LatchesOnGrid", "[systems][profundum][mass-dynamics]") {
    static_assert(kMdStepAt % kMdCI == 0, "8704 is a grid boundary");
    const std::vector<float> x = mdSaw(mdt::kE1Hz, kMdFs, 1.0);
    REQUIRE(x.size() > kMdStepAt + 4 * kMdCI);

    const auto renderWithCallAt = [&x](std::size_t at) {
        std::vector<float> L = x;
        std::vector<float> R = x;
        MassDynamics md;
        mdConfigure(md, 1.0f);
        md.prepare(kMdFs, kMdMaxBlock);
        const std::vector<MdEvent> events{{.at = at, .apply = [](MassDynamics& m) {
                                               m.setTransientDurationMs(250.0f);
                                               m.setCompressionAmount(0.5f);
                                           }}};
        mdRender(md, L.data(), R.data(), L.size(), kMdMaxBlock, events);
        std::vector<float> out = std::move(L);
        out.insert(out.end(), R.begin(), R.end());
        return out;
    };
    const std::vector<float> at8690 = renderWithCallAt(kMdStepAt - 14);
    const std::vector<float> at8704 = renderWithCallAt(kMdStepAt);
    const std::vector<float> at8705 = renderWithCallAt(kMdStepAt + 1);
    const bool sameInterval = mdBitEqual(at8690, at8704);
    const bool nextInterval = mdBitEqual(at8704, at8705);
    std::printf("MassDynamics_LatchesOnGrid: call at 8690 vs 8704 %s; call at 8705 vs 8704 %s\n",
                sameInterval ? "bit-equal" : "DIFFERS", nextInterval ? "bit-equal (WRONG)" : "differs");
    REQUIRE(sameInterval);
    REQUIRE_FALSE(nextInterval);
}

// =============================================================================
// 5. SC-018 dynamics arm: partition invariance and reset determinism
// =============================================================================
TEST_CASE("MassDynamics_PartitionInvariantDynamicsOnly", "[systems][profundum][mass-dynamics]") {
    const std::vector<float> x = mdSaw(mdt::kE1Hz, kMdFs, 3.0);
    const std::vector<MdEvent> events{{.at = kMdStepAt, .apply = [](MassDynamics& m) {
                                           m.setTransientDurationMs(100.0f);
                                           m.setCompressionAmount(1.0f);
                                           m.setDetectorKeyHz(static_cast<float>(mdt::kE2Hz));
                                           MdProbe::setFixedReleaseMs(MdProbe::compressor(m), 275.0f);
                                       }}};

    const std::array<std::size_t, 5> blocks{1, 17, 32, 64, 512};
    std::vector<std::vector<float>> outsL;
    std::vector<std::vector<float>> outsR;
    for (const std::size_t block : blocks) {
        std::vector<float> L = x;
        std::vector<float> R = x;
        MassDynamics md;
        mdConfigure(md, 0.5f);
        md.prepare(kMdFs, kMdMaxBlock);
        mdRender(md, L.data(), R.data(), L.size(), block, events);
        outsL.push_back(std::move(L));
        outsR.push_back(std::move(R));
    }
    for (std::size_t b = 1; b < blocks.size(); ++b) {
        const bool equal = mdBitEqual(outsL[b], outsL[0]) && mdBitEqual(outsR[b], outsR[0]);
        std::printf("MassDynamics_PartitionInvariantDynamicsOnly block %zu vs 1: %s\n", blocks[b],
                    equal ? "bit-equal" : "DIFFERS");
        REQUIRE(equal);
    }

    // Two renders from reset() on one instance. BassCompressor::reset() snaps its smoothers to the
    // LATCHED targets (plan S5.2 step 6) and the previous render's event latched amount 1 / key E2 /
    // fixed release, so the starting controls are re-issued and latched (one silent interval)
    // before each reset() (T007 run-log D-5).
    MassDynamics md;
    mdConfigure(md, 0.5f);
    md.prepare(kMdFs, kMdMaxBlock);
    std::array<std::vector<float>, 2> rl;
    std::array<std::vector<float>, 2> rr;
    for (std::size_t r = 0; r < 2; ++r) {
        mdConfigure(md, 0.5f);
        MdProbe::setFixedReleaseMs(MdProbe::compressor(md), 0.0f);
        std::array<float, kMdCI> silentL{};
        std::array<float, kMdCI> silentR{};
        md.processBlock(silentL.data(), silentR.data(), kMdCI);
        md.reset();
        rl[r] = x;
        rr[r] = x;
        mdRender(md, rl[r].data(), rr[r].data(), x.size(), 64, events);
    }
    const bool resetEqual = mdBitEqual(rl[0], rl[1]) && mdBitEqual(rr[0], rr[1]);
    const bool resetEqualsFresh = mdBitEqual(rl[0], outsL[0]) && mdBitEqual(rr[0], outsR[0]);
    std::printf("MassDynamics_PartitionInvariantDynamicsOnly: two renders from reset() %s; vs a fresh render %s\n",
                resetEqual ? "bit-equal" : "DIFFERS", resetEqualsFresh ? "bit-equal" : "DIFFERS");
    REQUIRE(resetEqual);
    CHECK(resetEqualsFresh);
}

// =============================================================================
// 6. SC-016 (FR-040): no allocation in any audio-thread method of the Phase 6 classes
// =============================================================================
TEST_CASE("MassDynamics_NoAllocation", "[systems][profundum][mass-dynamics]") {
    auto& detector = TestHelpers::AllocationDetector::instance();

    // Positive control: the detector sees a real allocation in this binary.
    detector.startTracking();
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    int* probe = new int[16];
    probe[0] = 42;
    volatile int* probeSink = probe;   // defeat new/delete elision
    const int probeObserved = probeSink[0];
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete[] probe;
    const std::size_t livenessCount = detector.stopTracking();
    REQUIRE(probeObserved == 42);
    REQUIRE(livenessCount >= std::size_t{1});

    // Every buffer and every instance's prepare outside the window (control thread).
    const std::vector<float> x = mdSaw(mdt::kE1Hz, kMdFs, 1.0);
    std::vector<float> sL = x;
    std::vector<float> sR = x;
    std::vector<float> cL = x;
    std::vector<float> cR = x;
    std::vector<float> dL = x;
    std::vector<float> dR = x;
    const float nan = mdNaN();
    const float pinf = mdPosInf();
    const float ninf = mdNegInf();
    const auto e1 = static_cast<float>(mdt::kE1Hz);
    const auto c3 = static_cast<float>(mdt::kC3Hz);
    TransientShaper ts;
    ts.prepare(kMdFs, kMdMaxBlock);
    BassCompressor bc;
    bc.prepare(kMdFs, kMdMaxBlock);
    MassDynamics md;
    md.prepare(kMdFs, kMdMaxBlock);
    MassLaw law;
    law.prepare(kMdFs);
    const MassLaw::Targets base{};
    MassLaw::Targets nonFiniteBase{};
    nonFiniteBase.depth = nan;
    nonFiniteBase.foldAmount = pinf;
    constexpr std::size_t kQ = 4096;

    detector.startTracking();
    // ---- TransientShaper ----
    for (const float v : {12.0f, nan, pinf, ninf, -20.0f}) {
        ts.setAttackGainDb(v);
        ts.setSustainGainDb(v);
        ts.setTransientDurationMs(v);
    }
    ts.setAttackGainDb(12.0f);
    ts.setSustainGainDb(-6.0f);
    ts.setTransientDurationMs(100.0f);
    ts.processBlock(sL.data(), sR.data(), kQ);
    ts.reset();
    ts.processBlock(sL.data() + kQ, sL.data() + kQ, kQ);   // mono path

    // ---- BassCompressor ----
    for (const float v : {0.75f, nan, pinf, ninf, -1.0f}) {
        bc.setAmount(v);
        bc.setDetectorKeyHz(v);
    }
    bc.setAmount(1.0f);
    bc.setDetectorKeyHz(e1);
    bc.processBlock(cL.data(), cR.data(), kQ);
    bc.reset();
    bc.processBlock(cL.data() + kQ, cL.data() + kQ, kQ);   // mono path

    // ---- MassDynamics, including a forced release change through the probe ----
    for (const float v : {0.5f, nan, pinf, ninf, -3.0f}) {
        md.setTransientAttackDb(v);
        md.setTransientSustainDb(v);
        md.setTransientDurationMs(v);
        md.setCompressionAmount(v);
        md.setDetectorKeyHz(v);
    }
    md.setTransientAttackDb(12.0f);
    md.setTransientSustainDb(-6.0f);
    md.setTransientDurationMs(40.0f);
    md.setCompressionAmount(1.0f);
    md.setDetectorKeyHz(e1);
    md.processBlock(dL.data(), dR.data(), kQ);
    MdProbe::setFixedReleaseMs(MdProbe::compressor(md), 275.0f);
    md.setDetectorKeyHz(c3);
    md.processBlock(dL.data() + kQ, dR.data() + kQ, kQ);
    MdProbe::setFixedReleaseMs(MdProbe::compressor(md), 0.0f);
    md.processBlock(dL.data() + 2 * kQ, dR.data() + 2 * kQ, kQ);
    md.reset();
    md.processBlock(dL.data() + 3 * kQ, dL.data() + 3 * kQ, kQ);   // mono path

    // ---- MassLaw: setters, evaluation and a Mass 0 -> 100 step ----
    law.setMass(nan);
    law.setMass(pinf);
    law.setMass(ninf);
    law.setMass(0.0f);
    law.setBase(base);
    law.setBase(nonFiniteBase);
    law.reset();
    law.setMass(100.0f);
    float lastMass = 0.0f;
    for (std::size_t k = 0; k < 128; ++k)
        lastMass = law.advanceInterval();
    const MassLaw::Targets t0 = MassLaw::evaluate(0.0f, base);
    const MassLaw::Targets t100 = MassLaw::evaluate(100.0f, base);
    const MassLaw::Targets tCur = law.currentTargets();
    const std::size_t count = detector.stopTracking();

    std::printf("MassDynamics_NoAllocation: %zu allocation(s) (positive control %zu); Mass after the step %.6g, "
                "depth at 0 / 100 %.6g / %.6g, current depth %.6g\n",
                count, livenessCount, static_cast<double>(lastMass), static_cast<double>(t0.depth),
                static_cast<double>(t100.depth), static_cast<double>(tCur.depth));
    REQUIRE(count == 0);
    REQUIRE(lastMass == 100.0f);
    REQUIRE(ts.stateFinite());
    REQUIRE(bc.stateFinite());
    REQUIRE(md.stateFinite());
}

// =============================================================================
// 7. FR-047 lifecycle (plan S13.3 FR-047 row (1)-(4)), inside the allocation window
// =============================================================================
TEST_CASE("MassDynamics_Lifecycle", "[systems][profundum][mass-dynamics]") {
    auto& detector = TestHelpers::AllocationDetector::instance();
    constexpr double kFs96 = 96000.0;
    const auto setup = [](MassDynamics& m) {
        m.setTransientAttackDb(12.0f);
        m.setCompressionAmount(1.0f);
        m.setDetectorKeyHz(static_cast<float>(mdt::kE1Hz));
    };

    // Every buffer is allocated before the tracked windows.
    const std::vector<float> x48 = mdSaw(mdt::kE1Hz, kMdFs, 3.0);   // 2 s drive + 1 s render
    const std::vector<float> x96 = mdSaw(mdt::kE1Hz, kFs96, 2.0);
    const std::size_t n2s = mdt::secondsToSamples(kMdFs, 2.0);
    const std::size_t n1s = mdt::secondsToSamples(kMdFs, 1.0);
    std::vector<float> sentinel(64, 0.123f);
    const std::vector<float> sentinelRef = sentinel;
    std::vector<float> sentinelR(64, -0.456f);
    const std::vector<float> sentinelRRef = sentinelR;
    std::vector<float> aL(x48.begin(), x48.begin() + static_cast<std::ptrdiff_t>(n1s));
    std::vector<float> aR = aL;
    std::vector<float> bL = aL;
    std::vector<float> bR = aL;
    std::vector<float> driveL(x48.begin(), x48.begin() + static_cast<std::ptrdiff_t>(n2s));
    std::vector<float> driveR = driveL;
    std::vector<float> r3L(x48.begin() + static_cast<std::ptrdiff_t>(n2s), x48.end());
    std::vector<float> r3R = r3L;
    std::vector<float> f3L = r3L;
    std::vector<float> f3R = r3L;
    std::vector<float> d96L = x96;
    std::vector<float> d96R = x96;
    std::vector<float> r4L = r3L;
    std::vector<float> r4R = r3L;
    std::vector<float> f4L = r3L;
    std::vector<float> f4R = r3L;

    // ---- (1) before prepare: a no-op; taps at 0; setters store a pending value ----
    MassDynamics a;
    detector.startTracking();
    setup(a);
    a.processBlock(sentinel.data(), sentinelR.data(), sentinel.size());
    const bool untouched1 = mdBitEqual(sentinel, sentinelRef) && mdBitEqual(sentinelR, sentinelRRef);
    const bool tapsDefault = a.shaper().currentGainDb() == 0.0f && a.compressor().currentGainDb() == 0.0f;
    const std::size_t count1 = detector.stopTracking();

    // First prepares (control thread, outside the window).
    a.prepare(kMdFs, kMdMaxBlock);
    const bool pendingApplied = a.compressor().amount() == 1.0f && a.shaper().attackGainDb() == 12.0f;
    MassDynamics b;
    setup(b);
    b.prepare(kMdFs, kMdMaxBlock);
    MassDynamics fresh3;
    setup(fresh3);
    fresh3.prepare(kMdFs, kMdMaxBlock);
    MassDynamics fresh4;
    setup(fresh4);
    fresh4.prepare(kMdFs, kMdMaxBlock);

    detector.startTracking();
    // ---- (2) null channel / 0 samples are no-ops; no phase advances ----
    a.processBlock(nullptr, sentinelR.data(), sentinelR.size());
    a.processBlock(sentinel.data(), nullptr, sentinel.size());
    a.processBlock(sentinel.data(), sentinelR.data(), 0);
    const bool untouched2 = mdBitEqual(sentinel, sentinelRef) && mdBitEqual(sentinelR, sentinelRRef);
    a.processBlock(aL.data(), aR.data(), aL.size());
    b.processBlock(bL.data(), bR.data(), bL.size());
    const bool noPhaseAdvanced = mdBitEqual(aL, bL) && mdBitEqual(aR, bR);

    // ---- (3) reset() after 2 s at A = +12, amount 1 with f > 0.5, then a render == a fresh one's ----
    a.processBlock(driveL.data(), driveR.data(), driveL.size());
    const float f3 = a.compressor().programFraction();
    a.reset();
    a.processBlock(r3L.data(), r3R.data(), r3L.size());
    fresh3.processBlock(f3L.data(), f3R.data(), f3L.size());
    const bool resetEqualsFresh = mdBitEqual(r3L, f3L) && mdBitEqual(r3R, f3R);

    // ---- (4) prepare(96k), 2 s at f > 0.5, prepare(48k) ----
    a.prepare(kFs96, kMdMaxBlock);
    a.processBlock(d96L.data(), d96R.data(), d96L.size());
    const float f4 = a.compressor().programFraction();
    a.prepare(kMdFs, kMdMaxBlock);
    const float f4After = a.compressor().programFraction();
    const float releaseAfter = a.compressor().currentReleaseMs();
    a.processBlock(r4L.data(), r4R.data(), r4L.size());
    fresh4.processBlock(f4L.data(), f4R.data(), f4L.size());
    const bool reprepareEqualsFresh = mdBitEqual(r4L, f4L) && mdBitEqual(r4R, f4R);
    const std::size_t count2 = detector.stopTracking();

    std::printf("MassDynamics_Lifecycle: f before reset %.6g, f at 96 kHz %.6g, f after prepare(48k) %.6g, "
                "release %.6g ms; allocations %zu + %zu\n",
                static_cast<double>(f3), static_cast<double>(f4), static_cast<double>(f4After),
                static_cast<double>(releaseAfter), count1, count2);
    REQUIRE(untouched1);
    REQUIRE(tapsDefault);
    REQUIRE(pendingApplied);
    REQUIRE(untouched2);
    REQUIRE(noPhaseAdvanced);
    REQUIRE(f3 > 0.5f);
    REQUIRE(resetEqualsFresh);
    REQUIRE(f4 > 0.5f);
    REQUIRE(f4After == 0.0f);
    REQUIRE(releaseAfter == BassCompressor::kMinReleaseMs);
    REQUIRE(reprepareEqualsFresh);
    REQUIRE(count1 == 0);
    REQUIRE(count2 == 0);
}

// =============================================================================
// 8. FR-014: zero latency
// =============================================================================
TEST_CASE("MassDynamics_LatencyZero", "[systems][profundum][mass-dynamics]") {
    STATIC_REQUIRE(TransientShaper::latencySamples() == 0);
    STATIC_REQUIRE(BassCompressor::latencySamples() == 0);
    STATIC_REQUIRE(MassDynamics::latencySamples() == 0);
}
