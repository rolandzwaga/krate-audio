// ==============================================================================
// Layer 2: Processor Tests - HarmonicFold
//                                    (Profundum Phase 2, specs/profundum-phase2-edge-fold)
// ==============================================================================
// Reference: specs/profundum-phase2-edge-fold/plan.md (S4, S8.1, S8.2)
//            specs/profundum-phase2-edge-fold/tasks.md (T006 wires this TU,
//                                                       T008-T010 fill it)
//
// SCOPE OF THIS TU (plan S8.1/S8.2; tags [profundum][edge-fold]): Fold API,
//   regimes, continuity, detector, stereo, RT - SC-005 (Fold arm), SC-006,
//   SC-007, SC-008, SC-010, SC-011, SC-012 (Fold arms), SC-013 (a)(c),
//   SC-015, SC-016 (RangeAndRates), SC-017, SC-019, SC-024, SC-025, SC-026.
//   Layer rule: no systems/ include, directly or through a helper.
//
// T008: API / constants, detector timing constants (SC-024(a)) and the
//   static-map anchors (HarmonicFold_StaticMapTables, needs processBlock).
// T009: processBlock contract - transparency (SC-010), zero in (SC-011),
//   L == R (SC-013(a)), zero latency (SC-026), block-size independence
//   (SC-017), no allocation (SC-015), range and rates (SC-016), output
//   bounded (SC-019).
// T010: descriptor gates - audibility and monotonicity (SC-006), harmonic
//   compression (SC-007), continuity (SC-008), DC (SC-011 Fold arm), h1
//   preservation (SC-012(d) Fold arm), per-channel detector (SC-013(c),
//   plan S1 C-4), in-cycle gain modulation (SC-024(b) + C-3), level
//   behaviour per regime (SC-025). [long] siblings carry the 401-point grids.
// ==============================================================================

#include "harmonic_fold_test_helpers.h"

// allocation_detector.h ONLY. NEVER allocation_operator_overrides.h: the global operator
// new/delete replacements are included from exactly one TU of dsp_processors_tests
// (brownian_drift_test.cpp); a second include is a duplicate-symbol link error.
#include <allocation_detector.h>

#include <krate/dsp/processors/harmonic_fold.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;
namespace hft = Krate::DSP::HarmonicFoldTest;

namespace {

/// MIDI note -> Hz in double (A4 = 440).
double midiHz(int note) { return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0); }

/// Bitwise L == R (SC-013(a)); local to this TU (the systems helper is out of layer).
void requireLREqual(const std::vector<float>& L, const std::vector<float>& R) {
    REQUIRE(L.size() == R.size());
    REQUIRE(std::memcmp(L.data(), R.data(), L.size() * sizeof(float)) == 0);
}

/// Bitwise equality of two renders (SC-010, SC-017).
bool bitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

/// t_settle = max(500 ms, 8·tau_DC), tau_DC = 1/(2π·kDcCornerHz) (spec "Steady state"), in samples.
std::size_t settleSamples(double fs) {
    const double tauDc = 1.0 / (hft::kTwoPi * static_cast<double>(HarmonicFold::kDcCornerHz));
    return static_cast<std::size_t>(std::ceil(std::max(0.5, 8.0 * tauDc) * fs));
}

/// The Phase 1 SC-009 state: kSawAnchor with edge = 1, shift = 1.
SpectralShapeRecipe::Controls sc009State() {
    SpectralShapeRecipe::Controls c = SpectralShapeRecipe::kSawAnchor;
    c.edge = 1.0f;
    c.shift = 1.0f;
    return c;
}

/// A prepared Fold at `amount` (set before prepare, so the smoother starts snapped).
HarmonicFold makeFold(double fs, float amount, double f0) {
    HarmonicFold fold;
    fold.setAmount(amount);
    fold.prepare(fs, 512);
    fold.setFundamentalHz(static_cast<float>(f0));
    return fold;
}

bool allFiniteAndBounded(const std::vector<float>& y, float bound) {
    return std::all_of(y.begin(), y.end(), [bound](float v) {
        return detail::isFinite(v) && std::abs(v) <= bound;
    });
}

}  // namespace

// =============================================================================
// API, constants, setters (FR-020/021/031/034/042/043; SC-019 ceiling; SC-026)
// =============================================================================

TEST_CASE("HarmonicFold_ApiAndConstants", "[processors][profundum][edge-fold]") {
    static_assert(HarmonicFold::kSoftEnd == 30.0f);
    static_assert(HarmonicFold::kCompressionEnd == 65.0f);
    static_assert(HarmonicFold::kNormalisationRampEnd == 40.0f);
    static_assert(HarmonicFold::kFoldIndexMax == 3.0f);  // plan S1 C-11
    static_assert(HarmonicFold::kFoldIndexEaseExponent == 1.5f);  // tasks.md D-1 (edge-fold-model.js FOLD_LAW=1.5)
    static_assert(HarmonicFold::kSplitHz == 500.0f);
    static_assert(HarmonicFold::kSplitEngageAmount == 10.0f);
    static_assert(HarmonicFold::kDcCornerHz == 2.0f);
    static_assert(HarmonicFold::kOutputCeiling == 6.0f);
    static_assert(HarmonicFold::kOutputCeiling < 8.0f);  // SC-019
    static_assert(HarmonicFold::kTablePoints == 401);
    static_assert(HarmonicFold::kDetectorAttackMs == 0.0f);  // plan S1 C-4 (instant attack)
    static_assert(HarmonicFold::kDetectorReleasePeriods == 4.0f);
    static_assert(HarmonicFold::latencySamples() == 0);  // SC-026

    static_assert(noexcept(std::declval<HarmonicFold&>().setAmount(0.0f)));
    static_assert(noexcept(std::declval<HarmonicFold&>().setFundamentalHz(0.0f)));
    static_assert(noexcept(std::declval<HarmonicFold&>().processBlock(nullptr, nullptr, std::size_t{0})));
    static_assert(noexcept(std::declval<HarmonicFold&>().prepare(48000.0, std::size_t{512})));
    static_assert(noexcept(std::declval<HarmonicFold&>().reset()));
    static_assert(std::is_copy_constructible_v<HarmonicFold>);
    static_assert(std::is_nothrow_move_constructible_v<HarmonicFold>);

    HarmonicFold fold;
    REQUIRE(fold.amount() == 0.0f);
    REQUIRE(fold.fundamentalHz() == HarmonicFold::kDefaultFundamentalHz);

    fold.setAmount(-5.0f);
    REQUIRE(fold.amount() == 0.0f);
    fold.setAmount(250.0f);
    REQUIRE(fold.amount() == 100.0f);

    // S3.3 factor table (FR-045): 4x up to 100 kHz, 2x above.
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        HarmonicFold f;
        f.prepare(fs, 512);
        INFO("fs = " << fs);
        REQUIRE(f.oversamplingFactor() == 4);
    }
    HarmonicFold f192;
    f192.prepare(192000.0, 512);
    REQUIRE(f192.oversamplingFactor() == 2);
}

// =============================================================================
// SC-024(a): period-aware release (FR-037)
// =============================================================================

TEST_CASE("HarmonicFold_DetectorTimingConstants", "[processors][profundum][edge-fold]") {
    HarmonicFold fold;
    fold.prepare(48000.0, 512);

    // Default (E0): release == kDetectorReleasePeriods / E0.
    REQUIRE(static_cast<double>(fold.releaseSeconds()) == Approx(4.0 / 20.6017223).epsilon(1e-6));

    // SC-024(a) as written: the release in effect at f0 = 20.6 Hz is >= kDetectorReleasePeriods / 20.6 s.
    fold.setFundamentalHz(20.6f);
    REQUIRE(fold.releaseSeconds() >= HarmonicFold::kDetectorReleasePeriods / 20.6f);

    fold.setFundamentalHz(41.2034f);
    REQUIRE(static_cast<double>(fold.releaseSeconds()) == Approx(4.0 / 41.2034).epsilon(1e-6));

    fold.setFundamentalHz(1.0f);
    REQUIRE(fold.fundamentalHz() == 8.0f);
    fold.setFundamentalHz(30000.0f);
    REQUIRE(fold.fundamentalHz() == 20000.0f);
}

// =============================================================================
// Static map anchors (plan S4.6). Not the SC gates: they catch a table
// transcription error early. They need processBlock (T009).
// =============================================================================

TEST_CASE("HarmonicFold_StaticMapTables", "[processors][profundum][edge-fold]") {
    constexpr double fs = 48000.0;
    const double f0 = midiHz(36);  // C2
    // t_settle = max(500 ms, 8·tau_DC), tau_DC = 1/(2π·kDcCornerHz) (spec "Steady state").
    const double tauDc = 1.0 / (hft::kTwoPi * static_cast<double>(HarmonicFold::kDcCornerHz));
    const auto settle = static_cast<std::size_t>(std::ceil(std::max(0.5, 8.0 * tauDc) * fs));
    const double seconds = static_cast<double>(settle + TestUtils::kLowFrequencyFftSize) / fs + 0.05;
    const std::vector<float> in = hft::calibrationSine(fs, f0, seconds);
    const std::size_t n = in.size() - settle;

    const auto describeAt = [&](float amount) {
        HarmonicFold fold;
        fold.setAmount(amount);
        fold.prepare(fs, 512);
        fold.setFundamentalHz(static_cast<float>(f0));
        const std::vector<float> out = hft::renderFold(fold, in, 512);
        return hft::describeFold(in.data() + settle, out.data() + settle, n, fs, f0);
    };

    const hft::FoldDescriptors d30 = describeAt(30.0f);
    const hft::FoldDescriptors d65 = describeAt(65.0f);
    const hft::FoldDescriptors d100 = describeAt(100.0f);
    std::printf("HarmonicFold_StaticMapTables C2/48k: THD(30) %.2f dB (plan -33.6), THD(65) %.2f dB "
                "(plan -19.6), H(100) %d (plan 17)\n",
                d30.thdDb, d65.thdDb, d100.h);

    REQUIRE(d30.thdDb == Approx(-33.6).margin(1.0));
    REQUIRE(d65.thdDb == Approx(-19.6).margin(1.0));
    REQUIRE(d100.h >= 15);
}

// =============================================================================
// SC-010 (Fold arm): transparent at amount 0 (FR-026 idle is a bit-exact copy)
// =============================================================================

TEST_CASE("HarmonicFold_TransparentAtZero", "[processors][profundum][edge-fold]") {
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        for (const int note : {12, 24, 36, 48, 60, 72}) {  // C0 ... C5
            const double f0 = midiHz(note);
            INFO("fs = " << fs << ", MIDI " << note);
            const std::vector<float> in = hft::calibrationSine(fs, f0, 2.0);
            HarmonicFold fold = makeFold(fs, 0.0f, f0);
            const std::vector<float> out = hft::renderFold(fold, in, 512);

            double residual = 0.0;
            double power = 0.0;
            for (std::size_t k = 0; k < in.size(); ++k) {
                const double d = static_cast<double>(out[k]) - static_cast<double>(in[k]);
                residual += d * d;
                power += static_cast<double>(in[k]) * static_cast<double>(in[k]);
            }
            REQUIRE(power > 0.0);
            REQUIRE(hft::safeDb10(residual / power) <= -90.0);
            REQUIRE(bitEqual(out, in));
        }
    }
}

// =============================================================================
// SC-011 zero clause: zero in -> zero out at every regime
// =============================================================================

TEST_CASE("HarmonicFold_ZeroInZeroOut", "[processors][profundum][edge-fold]") {
    for (const float amount : {0.0f, 30.0f, 65.0f, 100.0f}) {
        INFO("amount = " << amount);
        HarmonicFold fold;
        fold.prepare(48000.0, 512);
        fold.setAmount(amount);
        fold.reset();  // snaps the smoother to the pending amount
        std::vector<float> L(4096, 0.0f);
        std::vector<float> R(4096, 0.0f);
        fold.processBlock(L.data(), R.data(), L.size());
        REQUIRE(std::all_of(L.begin(), L.end(), [](float v) { return v == 0.0f; }));
        REQUIRE(std::all_of(R.begin(), R.end(), [](float v) { return v == 0.0f; }));
        REQUIRE(fold.stateFinite());
    }
}

// =============================================================================
// SC-013(a): identical L/R input -> bitwise L == R (and right == left aliasing)
// =============================================================================

TEST_CASE("HarmonicFold_LREqualForIdenticalInput", "[processors][profundum][edge-fold]") {
    constexpr double fs = 48000.0;
    const double f0 = midiHz(28);  // E1
    const std::vector<float> in = hft::calibrationSine(fs, f0, 1.0);

    // amount < 0 encodes "0 -> 100 ramp" (setAmount(100) after prepare at 0).
    for (const float amount : {0.0f, 15.0f, 47.5f, 82.5f, 100.0f, -1.0f}) {
        INFO("amount = " << amount << " (-1 = 0 -> 100 ramp)");
        const auto make = [&] {
            HarmonicFold f = makeFold(fs, amount < 0.0f ? 0.0f : amount, f0);
            if (amount < 0.0f)
                f.setAmount(100.0f);
            return f;
        };

        HarmonicFold stereo = make();
        const auto [L, R] = hft::renderFold(stereo, in, in, 512);
        requireLREqual(L, R);
        REQUIRE(stereo.stateFinite());

        // right == left: one channel, channel 0's state; same samples as the stereo run's L.
        HarmonicFold aliased = make();
        std::vector<float> buf = in;
        for (std::size_t pos = 0; pos < buf.size(); pos += 512) {
            const std::size_t n = std::min<std::size_t>(512, buf.size() - pos);
            aliased.processBlock(buf.data() + pos, buf.data() + pos, n);
        }
        REQUIRE(bitEqual(buf, L));
    }
}

// Identical-input stereo that later diverges (and later matches again): each output channel is
// bit-identical to a one-channel render of its own input stream, at every amount, so any sharing
// of work between channels while their inputs agree never leaks into the image (SC-018 load).
TEST_CASE("HarmonicFold_StereoDivergesAfterIdenticalInput", "[processors][profundum][edge-fold]") {
    constexpr double fs = 48000.0;
    const double f0 = midiHz(28);  // E1
    const std::vector<float> in = hft::calibrationSine(fs, f0, 1.0);
    const std::vector<float> other = hft::calibrationSine(fs, 1.5 * f0, 1.0);

    // R = L for the first third, a different signal for the second, L again for the last.
    std::vector<float> right = in;
    const std::size_t third = in.size() / 3;
    for (std::size_t i = third; i < 2 * third; ++i)
        right[i] = other[i];

    for (const float amount : {0.0f, 15.0f, 47.5f, 82.5f, 100.0f, -1.0f}) {
        INFO("amount = " << amount << " (-1 = 0 -> 100 ramp)");
        const auto make = [&] {
            HarmonicFold f = makeFold(fs, amount < 0.0f ? 0.0f : amount, f0);
            if (amount < 0.0f)
                f.setAmount(100.0f);
            return f;
        };

        HarmonicFold stereo = make();
        const auto [L, R] = hft::renderFold(stereo, in, right, 37);
        REQUIRE(stereo.stateFinite());

        const auto renderAliased = [&](std::vector<float> buf) {
            HarmonicFold f = make();
            for (std::size_t pos = 0; pos < buf.size(); pos += 37) {
                const std::size_t n = std::min<std::size_t>(37, buf.size() - pos);
                f.processBlock(buf.data() + pos, buf.data() + pos, n);
            }
            return buf;
        };
        REQUIRE(bitEqual(renderAliased(in), L));
        REQUIRE(bitEqual(renderAliased(right), R));
    }
}

// =============================================================================
// SC-026: zero base-rate latency (cross-correlation lag 0 at amount 2)
// =============================================================================

TEST_CASE("HarmonicFold_ZeroLatency", "[processors][profundum][edge-fold]") {
    static_assert(HarmonicFold::latencySamples() == 0);
    constexpr double fs = 48000.0;
    constexpr int kMaxLag = 8;
    const std::size_t settle = settleSamples(fs);

    for (const int note : {28, 36}) {  // E1, C2
        const double f0 = midiHz(note);
        INFO("MIDI " << note);
        const std::vector<float> in = hft::calibrationSine(fs, f0, static_cast<double>(settle) / fs + 1.5);
        HarmonicFold fold = makeFold(fs, 2.0f, f0);
        const std::vector<float> out = hft::renderFold(fold, in, 512);

        // Steady window of an integer number of periods (>= 1 s), with kMaxLag guard samples
        // on both sides so every lag stays inside the render.
        const std::size_t start = settle + static_cast<std::size_t>(kMaxLag);
        const std::size_t len = hft::integerPeriodLength(in.size() - start - static_cast<std::size_t>(kMaxLag),
                                                         fs, f0, 1.0);
        int bestLag = kMaxLag + 1;
        double best = -1.0e300;
        for (int lag = -kMaxLag; lag <= kMaxLag; ++lag) {
            double acc = 0.0;
            for (std::size_t k = start; k < start + len; ++k) {
                const auto j = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(k) + lag);
                acc += static_cast<double>(out[j]) * static_cast<double>(in[k]);
            }
            if (acc > best) {
                best = acc;
                bestLag = lag;
            }
        }
        REQUIRE(bestLag == 0);
    }
}

// =============================================================================
// SC-017: block-size independence (a pending 0 -> 100 ramp spans block boundaries)
// =============================================================================

TEST_CASE("HarmonicFold_BlockSizeIndependent", "[processors][profundum][edge-fold]") {
    constexpr double fs = 48000.0;
    const double f0 = midiHz(28);  // E1
    const std::vector<float> in = hft::calibrationSine(fs, f0, 0.5);

    const auto renderWith = [&](std::size_t block) {
        HarmonicFold fold = makeFold(fs, 0.0f, f0);
        fold.setAmount(100.0f);
        return hft::renderFold(fold, in, in, block);
    };

    const auto ref = renderWith(512);
    for (const std::size_t block : {std::size_t{64}, std::size_t{37}, std::size_t{1}}) {
        INFO("block = " << block);
        const auto other = renderWith(block);
        REQUIRE(bitEqual(other.first, ref.first));
        REQUIRE(bitEqual(other.second, ref.second));
    }
    const auto twin = renderWith(512);
    REQUIRE(bitEqual(twin.first, ref.first));
    REQUIRE(bitEqual(twin.second, ref.second));
}

// =============================================================================
// SC-015: no allocation on the audio thread (any numSamples, > maxBlockSize included)
// =============================================================================
// Nothing but the Fold runs inside a tracked window (no Catch2 macros). The liveness
// probe runs first so a mis-wired binary cannot pass vacuously.
TEST_CASE("HarmonicFold_NoAllocation", "[processors][profundum][edge-fold]") {
    auto& detector = TestHelpers::AllocationDetector::instance();

    detector.startTracking();
    std::size_t probeSize = 0;
    {
        std::vector<int> probe(16);
        volatile int* probeSink = probe.data();  // defeat new/delete elision
        probeSink[0] = 42;
        probeSize = probe.size();
    }
    const std::size_t livenessCount = detector.stopTracking();
    REQUIRE(probeSize == 16);
    REQUIRE(livenessCount > std::size_t{0});

    constexpr double fs = 48000.0;
    HarmonicFold fold;
    fold.prepare(fs, 512);  // control thread, outside the window

    constexpr std::size_t kMaxBlock = 8192;
    const std::vector<float> sine = hft::calibrationSine(fs, midiHz(28), static_cast<double>(kMaxBlock) / fs);
    REQUIRE(sine.size() >= kMaxBlock);
    std::vector<float> L(sine.begin(), sine.begin() + static_cast<std::ptrdiff_t>(kMaxBlock));
    std::vector<float> R = L;
    constexpr std::array<std::size_t, 6> kBlocks{1, 37, 64, 512, 4096, 8192};
    constexpr std::array<float, 6> kAmounts{0.0f, 15.0f, 47.5f, 82.5f, 100.0f, 30.0f};

    detector.startTracking();
    for (std::size_t i = 0; i < kBlocks.size(); ++i) {
        fold.setAmount(kAmounts[i]);
        fold.setFundamentalHz(41.2034f + static_cast<float>(i));
        fold.processBlock(L.data(), R.data(), kBlocks[i]);
        fold.processBlock(L.data(), L.data(), kBlocks[i]);  // right == left path
    }
    const std::size_t count = detector.stopTracking();
    REQUIRE(count == std::size_t{0});
    REQUIRE(fold.stateFinite());
}

// =============================================================================
// SC-016: range and rates (22.05 and 192 kHz), argument no-ops, FR-042
// =============================================================================

TEST_CASE("HarmonicFold_RangeAndRates", "[processors][profundum][edge-fold]") {
    const double f0 = midiHz(28);  // E1
    for (const double fs : {22050.0, 192000.0}) {
        INFO("fs = " << fs);
        const std::vector<float> in = hft::recipeAdditive(sc009State(), f0, fs, 2.0);
        HarmonicFold fold = makeFold(fs, 100.0f, f0);
        const auto [L, R] = hft::renderFold(fold, in, in, 512);
        REQUIRE(allFiniteAndBounded(L, HarmonicFold::kOutputCeiling));
        REQUIRE(allFiniteAndBounded(R, HarmonicFold::kOutputCeiling));
        REQUIRE(fold.stateFinite());

        // No-op calls: buffers, stateFinite() and every later output unchanged.
        HarmonicFold twin = fold;
        std::vector<float> l(64, 0.25f);
        std::vector<float> r(64, -0.25f);
        const std::vector<float> l0 = l;
        const std::vector<float> r0 = r;
        const bool finiteBefore = fold.stateFinite();
        fold.processBlock(l.data(), r.data(), 0);
        fold.processBlock(nullptr, r.data(), 64);
        fold.processBlock(l.data(), nullptr, 64);
        REQUIRE(bitEqual(l, l0));
        REQUIRE(bitEqual(r, r0));
        REQUIRE(fold.stateFinite() == finiteBefore);

        std::vector<float> a = in;
        std::vector<float> b = in;
        std::vector<float> a2 = in;
        std::vector<float> b2 = in;
        fold.processBlock(a.data(), b.data(), 512);
        twin.processBlock(a2.data(), b2.data(), 512);
        REQUIRE(bitEqual(a, a2));
        REQUIRE(bitEqual(b, b2));
    }

    // Before prepare (FR-042): the buffers are left untouched.
    HarmonicFold unprepared;
    unprepared.setAmount(100.0f);
    std::vector<float> l(64, 0.25f);
    std::vector<float> r(64, -0.25f);
    unprepared.processBlock(l.data(), r.data(), l.size());
    REQUIRE(std::all_of(l.begin(), l.end(), [](float v) { return v == 0.25f; }));
    REQUIRE(std::all_of(r.begin(), r.end(), [](float v) { return v == -0.25f; }));
}

// =============================================================================
// SC-019 (Fold arm): output bounded, without relying on the clamp
// =============================================================================

TEST_CASE("HarmonicFold_OutputBounded", "[processors][profundum][edge-fold]") {
    constexpr double fs = 48000.0;
    constexpr double seconds = 1.0;

    struct Fixture {
        const char* name;
        int note;
        std::vector<float> in;
    };
    std::vector<Fixture> fixtures;
    for (const int note : {16, 28, 40, 36, 48, 60, 72})  // E0, E1, E2, C2, C3, C4, C5
        fixtures.push_back({"calibration sine", note, hft::calibrationSine(fs, midiHz(note), seconds)});
    const std::array<std::pair<const char*, SpectralShapeRecipe::Controls>, 4> recipes{{
        {"kSawAnchor", SpectralShapeRecipe::kSawAnchor},
        {"mid grid", ProfundumTest::midGrid()},
        {"kGrowl", SpectralShapeRecipe::kGrowl},
        {"SC-009 state", sc009State()},
    }};
    for (const auto& [name, controls] : recipes) {
        for (const int note : {12, 24, 36, 48, 60})
            fixtures.push_back({name, note, hft::recipeAdditive(controls, midiHz(note), fs, seconds)});
    }

    float worst = 0.0f;
    const char* worstName = "";
    int worstNote = 0;
    float worstAmount = 0.0f;
    for (const float amount : {30.0f, 65.0f, 82.5f, 100.0f}) {
        for (const Fixture& fx : fixtures) {
            INFO(fx.name << " at MIDI " << fx.note << ", amount " << amount);
            HarmonicFold fold = makeFold(fs, amount, midiHz(fx.note));
            fold.clearPreCeilingPeak();
            const auto [L, R] = hft::renderFold(fold, fx.in, fx.in, 512);
            const float peak = fold.preCeilingPeak();
            REQUIRE(peak < HarmonicFold::kOutputCeiling);
            REQUIRE(allFiniteAndBounded(L, HarmonicFold::kOutputCeiling));
            REQUIRE(allFiniteAndBounded(R, HarmonicFold::kOutputCeiling));
            if (peak > worst) {
                worst = peak;
                worstName = fx.name;
                worstNote = fx.note;
                worstAmount = amount;
            }
        }
    }
    std::printf("HarmonicFold_OutputBounded: worst preCeilingPeak %.4f (%s, MIDI %d, amount %.1f; plan <= 2.40)\n",
                static_cast<double>(worst), worstName, worstNote, static_cast<double>(worstAmount));
}

// =============================================================================
// T010 fixtures: shared by the SC-006/007/008/011/012(d)/013(c)/024(b)/025 gates
// =============================================================================
// All renders 48 kHz unless stated. makeFold() calls setFundamentalHz(f0) with the
// played f0 (tasks.md T010). Every window starts at t_settle (spec "Steady state").
// Mono renders use the right == left path, which SC-013(a)
// (HarmonicFold_LREqualForIdenticalInput) proves bit-identical to the L of a stereo
// render with L == R, at half the cost.

namespace {

constexpr int kNoteE0 = 16;
constexpr int kNoteE1 = 28;
constexpr int kNoteE2 = 40;
constexpr int kNoteC2 = 36;
constexpr double kFs = 48000.0;
constexpr double kHalfTurnRad = hft::kTwoPi * 0.5;

/// Amount on the 0.25 grid: q quarters -> q·0.25 (exact in float).
float quarterAmount(int q) { return static_cast<float>(q) * 0.25f; }

/// Peak of a sine at `rmsDbfs` dBFS RMS.
double levelPeak(double rmsDbfs) { return std::sqrt(2.0) * std::pow(10.0, rmsDbfs / 20.0); }

enum class Source : std::uint8_t { Sine, MidGrid, Saw };

const char* sourceName(Source s) {
    switch (s) {
    case Source::Sine:
        return "calibration sine";
    case Source::MidGrid:
        return "recipe mid grid";
    case Source::Saw:
        return "recipe kSawAnchor";
    }
    return "?";
}

std::vector<float> makeSource(Source s, double fs, double f0, double seconds) {
    switch (s) {
    case Source::Sine:
        return hft::calibrationSine(fs, f0, seconds);
    case Source::MidGrid:
        return hft::recipeAdditive(ProfundumTest::midGrid(), f0, fs, seconds);
    case Source::Saw:
        return hft::recipeAdditive(SpectralShapeRecipe::kSawAnchor, f0, fs, seconds);
    }
    return {};
}

/// Render length for the spectral descriptors: t_settle + one kLowFrequencyFftSize frame (+50 ms).
double describeSeconds(double fs) {
    return static_cast<double>(settleSamples(fs) + TestUtils::kLowFrequencyFftSize) / fs + 0.05;
}

/// Render length for the time-domain descriptors (L, CF, T1, DC, C0 residual): t_settle + 1.1 s,
/// so the largest integer number of periods after t_settle is >= 1 s down to E0.
double shortSeconds(double fs) { return static_cast<double>(settleSamples(fs)) / fs + 1.1; }

/// Mono render through the right == left path in `block`-sized host blocks.
std::vector<float> renderMono(HarmonicFold& fold, const std::vector<float>& in, std::size_t block = 512) {
    std::vector<float> y = in;
    for (std::size_t pos = 0; pos < y.size(); pos += block) {
        const std::size_t n = std::min(block, y.size() - pos);
        fold.processBlock(y.data() + pos, y.data() + pos, n);
    }
    return y;
}

struct LevelCrest {
    double levelDb;
    double crestDb;
};

/// L and CF over [0, len) exactly as describeFold computes them (spec "Descriptor definitions").
LevelCrest levelAndCrest(const float* in, const float* out, std::size_t len) {
    double sumIn = 0.0;
    double sumOut = 0.0;
    double peakOut = 0.0;
    for (std::size_t k = 0; k < len; ++k) {
        const double xi = static_cast<double>(in[k]);
        const double yo = static_cast<double>(out[k]);
        sumIn += xi * xi;
        sumOut += yo * yo;
        peakOut = std::max(peakOut, std::abs(yo));
    }
    REQUIRE(sumIn > 0.0);
    REQUIRE(sumOut > 0.0);
    const double msOut = sumOut / static_cast<double>(len);
    return LevelCrest{.levelDb = hft::safeDb10(sumOut / sumIn), .crestDb = hft::safeDb10(peakOut * peakOut / msOut)};
}

/// describeFold without T1 (the gates that use these do not need it, and the two BH7 Goertzels
/// over >= 262144 samples dominate describeFold's cost). Same arithmetic for every field.
struct Steady {
    double thdDb;
    int h;
    double levelDb;
    double crestDb;
    double centroidOct;
};

Steady describeSteady(const float* in, const float* out, std::size_t n, double fs, double f0) {
    REQUIRE(n >= TestUtils::kLowFrequencyFftSize);
    const std::size_t len = hft::integerPeriodLength(n, fs, f0, 1.0);
    const LevelCrest lc = levelAndCrest(in, out, len);

    const int nHarm = static_cast<int>(std::ceil(fs * 0.5 / f0));
    const std::vector<double> w = hft::renderedHarmonicPowers(out, fs, f0, nHarm);
    REQUIRE(!w.empty());
    const double w1 = w[0];
    REQUIRE(w1 > 0.0);
    double rest = 0.0;
    int count = 0;
    for (std::size_t i = 1; i < w.size(); ++i) {
        rest += w[i];
        if (static_cast<double>(i + 1) * f0 < fs * 0.5 && w[i] >= w1 * 1.0e-6)
            ++count;
    }
    return Steady{.thdDb = hft::safeDb10(rest / w1),
                  .h = count,
                  .levelDb = lc.levelDb,
                  .crestDb = lc.crestDb,
                  .centroidOct = ProfundumTest::describePowers(w).centroidOct};
}

/// Descriptor-length input per (source, note, rate), generated once per process.
const std::vector<float>& describeInput(Source s, int note, double fs) {
    static std::map<std::tuple<int, int, long>, std::vector<float>> cache;
    const auto key = std::make_tuple(static_cast<int>(s), note, std::lround(fs));
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.emplace(key, makeSource(s, fs, midiHz(note), describeSeconds(fs))).first;
    return it->second;
}

/// Steady descriptors of the Fold at amount q·0.25 on `s` at `note`, `fs`. Memoised: SC-006,
/// SC-007(a)(c) and SC-008(a) read overlapping points (a render is deterministic, SC-017).
Steady describeAt(Source s, int note, double fs, int q) {
    static std::map<std::tuple<int, int, long, int>, Steady> cache;
    const auto key = std::make_tuple(static_cast<int>(s), note, std::lround(fs), q);
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;
    const double f0 = midiHz(note);
    const std::vector<float>& in = describeInput(s, note, fs);
    HarmonicFold fold = makeFold(fs, quarterAmount(q), f0);
    const std::vector<float> out = renderMono(fold, in);
    const std::size_t settle = settleSamples(fs);
    const Steady d = describeSteady(in.data() + settle, out.data() + settle, in.size() - settle, fs, f0);
    cache.emplace(key, d);
    return d;
}

/// The SC-007(b) / SC-025 input levels (dBFS RMS).
constexpr std::array<double, 4> kLevelsDbfs{-24.0, -18.0, -12.0, -6.0};

/// THD of the Fold at amount q·0.25 for the calibration sine at each of kLevelsDbfs.
std::array<double, 4> thdOverLevels(int q, int note) {
    const double f0 = midiHz(note);
    const std::size_t settle = settleSamples(kFs);
    std::array<double, 4> thd{};
    for (std::size_t i = 0; i < kLevelsDbfs.size(); ++i) {
        const std::vector<float> in = hft::calibrationSine(kFs, f0, describeSeconds(kFs), levelPeak(kLevelsDbfs[i]));
        HarmonicFold fold = makeFold(kFs, quarterAmount(q), f0);
        const std::vector<float> out = renderMono(fold, in);
        thd[i] = describeSteady(in.data() + settle, out.data() + settle, in.size() - settle, kFs, f0).thdDb;
    }
    return thd;
}

double spreadOf(const std::array<double, 4>& v) {
    const auto [mn, mx] = std::minmax_element(v.begin(), v.end());
    return *mx - *mn;
}

/// Static-map descriptors of a memoryless odd map y = tanh(g·x) on x = peak·sin(φ), in double
/// (SC-007(b)(c) reference). The map is static, so its descriptors on the calibration input are
/// those of one period: the harmonics come from a 16384-point DFT over φ (exact to round-off for
/// every n < 8192), THD sums the n >= 2 with n·f0 < fs/2 exactly as the Fold descriptor does,
/// L and CF are the period RMS ratio and peak / RMS.
struct StaticDescriptors {
    double thdDb;
    double levelDb;
    double crestDb;
};

StaticDescriptors tanhDescriptors(double g, double peak, double fs, double f0) {
    constexpr std::size_t kPhasePoints = 16384;
    std::vector<std::complex<double>> spec(kPhasePoints);
    double sumX2 = 0.0;
    double sumY2 = 0.0;
    double peakY = 0.0;
    for (std::size_t k = 0; k < kPhasePoints; ++k) {
        const double x = peak * std::sin(hft::kTwoPi * static_cast<double>(k) / static_cast<double>(kPhasePoints));
        const double y = std::tanh(g * x);
        spec[k] = std::complex<double>(y, 0.0);
        sumX2 += x * x;
        sumY2 += y * y;
        peakY = std::max(peakY, std::abs(y));
    }
    hft::fftDouble(spec);
    const double w1 = std::norm(spec[1]);
    REQUIRE(w1 > 0.0);
    double rest = 0.0;
    for (std::size_t n = 2; n < kPhasePoints / 2 && static_cast<double>(n) * f0 < fs * 0.5; ++n)
        rest += std::norm(spec[n]);
    const double msY = sumY2 / static_cast<double>(kPhasePoints);
    return StaticDescriptors{.thdDb = hft::safeDb10(rest / w1),
                             .levelDb = hft::safeDb10(sumY2 / sumX2),
                             .crestDb = hft::safeDb10(peakY * peakY / msY)};
}

/// g in [0.1, 100] (log bisection) whose tanh THD at `peak` equals `targetDb`.
double bisectTanhDrive(double targetDb, double peak, double fs, double f0) {
    double lo = std::log(0.1);
    double hi = std::log(100.0);
    REQUIRE(tanhDescriptors(std::exp(lo), peak, fs, f0).thdDb < targetDb);
    REQUIRE(tanhDescriptors(std::exp(hi), peak, fs, f0).thdDb > targetDb);
    for (int it = 0; it < 80; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (tanhDescriptors(std::exp(mid), peak, fs, f0).thdDb < targetDb)
            lo = mid;
        else
            hi = mid;
    }
    return std::exp(0.5 * (lo + hi));
}

/// Test-side transcription of the plan S4.3 static map in double (the SC-013(c) shared-detector
/// teeth only; the Fold's own tables are private). Same formulas as HarmonicFold::computeTables.
struct StaticMapModel {
    double P = 0.0;
    double g = 0.0;
    double theta = 0.0;
    double V = 0.0;
    double mk = 1.0;

    [[nodiscard]] double sat(double q) const {
        if (g < 1.0e-6)
            return q;
        const double v = g * q / P;
        return (P / g) * (v / std::sqrt(v * v + 1.0));
    }
    [[nodiscard]] double z(double q) const {
        const double s = sat(q);
        if (theta < static_cast<double>(WavefoldMath::kSineFoldGainEpsilon))
            return s;
        return (V / theta) * std::sin(theta * s / V);
    }
    /// Phi_fold(u) = mk · z(G·u) / G.
    [[nodiscard]] double phi(double u, double G) const { return mk * z(G * u) / G; }
};

StaticMapModel staticMapModel(double a) {
    StaticMapModel m;
    m.P = static_cast<double>(HarmonicFold::kCalibrationPeak);
    const double gS = static_cast<double>(HarmonicFold::kSoftDriveAtSoftEnd);
    const double gC = static_cast<double>(HarmonicFold::kCompressionDriveAtEnd);
    const double softEnd = static_cast<double>(HarmonicFold::kSoftEnd);
    const double compEnd = static_cast<double>(HarmonicFold::kCompressionEnd);
    m.g = (a <= softEnd) ? gS * a / softEnd : gS * std::pow(gC / gS, (a - softEnd) / (compEnd - softEnd));
    // tasks.md D-1: ease-out fold index, theta = thetaMax·(1 − (1 − t)^p).
    m.theta = (a <= compEnd) ? 0.0
                             : static_cast<double>(HarmonicFold::kFoldIndexMax)
                                   * (1.0 - std::pow(1.0 - (a - compEnd) / (100.0 - compEnd),
                                                     static_cast<double>(HarmonicFold::kFoldIndexEaseExponent)));
    m.V = m.sat(m.P);
    double peak = 0.0;
    for (std::size_t j = 1; j <= 4096; ++j)
        peak = std::max(peak, std::abs(m.z(m.P * static_cast<double>(j) / 4096.0)));
    m.mk = (peak > 0.0) ? m.P / peak : 1.0;
    return m;
}

/// BH7-windowed single-bin DFT with a precomputed window: goertzelPhasor's arithmetic, without
/// rebuilding the window per call (SC-012(d) evaluates hundreds of renders per window length).
std::complex<double> phasorWithWindow(const float* x, const std::vector<double>& w, double fs, double hz) {
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < w.size(); ++k) {
        const double v = static_cast<double>(x[k]) * w[k];
        const double ph = hft::kTwoPi * hz * static_cast<double>(k) / fs;
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return {re, im};
}

/// 10·log10 of the mean square over [start, start + len).
double meanSquareDb(const std::vector<float>& y, std::size_t start, std::size_t len) {
    double s = 0.0;
    for (std::size_t k = start; k < start + len; ++k)
        s += static_cast<double>(y[k]) * static_cast<double>(y[k]);
    return hft::safeDb10(s / static_cast<double>(len));
}

/// Frame RMS (dB) of frames of `frameLen` hopped by `hop` over [start, end).
std::vector<double> frameRmsDb(const std::vector<float>& y, std::size_t start, std::size_t end, std::size_t frameLen,
                               std::size_t hop) {
    std::vector<double> out;
    for (std::size_t pos = start; pos + frameLen <= end; pos += hop)
        out.push_back(meanSquareDb(y, pos, frameLen));
    return out;
}

double maxAdjacentChange(const std::vector<double>& v) {
    double m = 0.0;
    for (std::size_t i = 1; i < v.size(); ++i)
        m = std::max(m, std::abs(v[i] - v[i - 1]));
    return m;
}

/// (max − min) / mean over [first, last).
double rippleOf(const double* first, const double* last) {
    REQUIRE(last > first);
    const auto [mn, mx] = std::minmax_element(first, last);
    double sum = 0.0;
    for (const double* p = first; p != last; ++p)
        sum += *p;
    const double mean = sum / static_cast<double>(last - first);
    REQUIRE(mean > 0.0);
    return (*mx - *mn) / mean;
}

std::vector<int> quarterGrid(int step) {
    std::vector<int> q;
    for (int i = 0; i <= 400; i += step)
        q.push_back(i);
    return q;
}

/// The SC-006 smoke grid: 41 points step 2.5 plus 0.25.
std::vector<int> smokeMonotoneGrid() {
    std::vector<int> q = quarterGrid(10);
    q.insert(q.begin() + 1, 1);
    return q;
}

}  // namespace

// =============================================================================
// SC-006: THD strictly increasing and H non-decreasing over the amount sweep
// =============================================================================

namespace {

void requireThdAndCountMonotonic(const std::vector<double>& rates, const std::vector<int>& quarters,
                                 const char* label) {
    for (const double fs : rates) {
        for (const int note : {kNoteE0, kNoteE1, kNoteE2, kNoteC2}) {
            double minStep = 1.0e300;
            int minStepQ = -1;
            Steady prev = describeAt(Source::Sine, note, fs, quarters.front());
            for (std::size_t i = 1; i < quarters.size(); ++i) {
                const Steady d = describeAt(Source::Sine, note, fs, quarters[i]);
                INFO(label << ": fs " << fs << ", MIDI " << note << ", amount " << quarterAmount(quarters[i - 1])
                           << " -> " << quarterAmount(quarters[i]) << ": THD " << prev.thdDb << " -> " << d.thdDb
                           << " dB, H " << prev.h << " -> " << d.h);
                CHECK(d.thdDb > prev.thdDb);
                CHECK(d.h >= prev.h);
                if (d.thdDb - prev.thdDb < minStep) {
                    minStep = d.thdDb - prev.thdDb;
                    minStepQ = quarters[i - 1];
                }
                prev = d;
            }
            std::printf("%s fs %.0f MIDI %d: min THD step %+.3f dB at amount %.2f (plan D-1 +0.069 at 100);"
                        "THD(100) %.2f dB, H(100) %d\n",
                        label, fs, note, minStep, static_cast<double>(quarterAmount(minStepQ)), prev.thdDb, prev.h);
        }
    }
}

}  // namespace

TEST_CASE("HarmonicFold_ThdAndCountMonotonicSmoke", "[processors][profundum][edge-fold]") {
    requireThdAndCountMonotonic({kFs}, smokeMonotoneGrid(), "HarmonicFold_ThdAndCountMonotonicSmoke");
}

TEST_CASE("HarmonicFold_ThdAndCountMonotonic", "[processors][profundum][edge-fold][long]") {
    requireThdAndCountMonotonic({44100.0, kFs, 96000.0}, quarterGrid(1), "HarmonicFold_ThdAndCountMonotonic");
}

// =============================================================================
// SC-006 audibility: each regime moves THD >= 6 dB; H(100) >= 4·H(30); the
// mid-grid centroid at C2 rises >= 0.5 octave (plan S1 C-11: kFoldIndexMax 3.0)
// =============================================================================

TEST_CASE("HarmonicFold_RegimesAudible", "[processors][profundum][edge-fold]") {
    for (const int note : {kNoteE0, kNoteE1, kNoteE2, kNoteC2}) {
        const Steady d0 = describeAt(Source::Sine, note, kFs, 0);
        const Steady d30 = describeAt(Source::Sine, note, kFs, 120);
        const Steady d65 = describeAt(Source::Sine, note, kFs, 260);
        const Steady d100 = describeAt(Source::Sine, note, kFs, 400);
        std::printf("HarmonicFold_RegimesAudible MIDI %d: THD rise %.1f / %.1f / %.1f dB (plan 42.3 / 14.0 / 24.1), "
                    "H(30) %d, H(100) %d (plan 1, 17)\n",
                    note, d30.thdDb - d0.thdDb, d65.thdDb - d30.thdDb, d100.thdDb - d65.thdDb, d30.h, d100.h);
        INFO("MIDI " << note);
        CHECK(d30.thdDb - d0.thdDb >= 6.0);
        CHECK(d65.thdDb - d30.thdDb >= 6.0);
        CHECK(d100.thdDb - d65.thdDb >= 6.0);
        CHECK(d100.h >= 4 * d30.h);
    }

    // plan S1 C-11: the centroid clause is what fixed kFoldIndexMax at 3.0.
    const Steady m0 = describeAt(Source::MidGrid, kNoteC2, kFs, 0);
    const Steady m100 = describeAt(Source::MidGrid, kNoteC2, kFs, 400);
    std::printf("HarmonicFold_RegimesAudible mid grid C2: C(0) %.3f oct, C(100) %.3f oct, rise %.3f oct "
                "(plan 1.270 -> 1.870)\n",
                m0.centroidOct, m100.centroidOct, m100.centroidOct - m0.centroidOct);
    CHECK(m100.centroidOct - m0.centroidOct >= 0.5);
}

// =============================================================================
// SC-007(a): density rises across the compression regime before level does
// =============================================================================

TEST_CASE("HarmonicFold_CompressionRegimeDensityBeforeLevel", "[processors][profundum][edge-fold]") {
    const std::size_t settle = settleSamples(kFs);
    for (const int note : {kNoteE1, kNoteC2}) {
        const double f0 = midiHz(note);
        INFO("MIDI " << note);
        const Steady d30 = describeAt(Source::Sine, note, kFs, 120);
        const Steady d65 = describeAt(Source::Sine, note, kFs, 260);
        CHECK(d65.thdDb - d30.thdDb >= 10.0);
        CHECK(static_cast<double>(d65.h) >= 1.5 * static_cast<double>(d30.h));

        // L and CF on the 0.25 grid over [30, 65], each within ±1 dB of its value at 30.
        const std::vector<float> in = hft::calibrationSine(kFs, f0, shortSeconds(kFs));
        const std::size_t len = hft::integerPeriodLength(in.size() - settle, kFs, f0, 1.0);
        LevelCrest ref{};
        double worstDl = 0.0;
        double worstDcf = 0.0;
        for (int q = 120; q <= 260; ++q) {
            HarmonicFold fold = makeFold(kFs, quarterAmount(q), f0);
            const std::vector<float> out = renderMono(fold, in);
            const LevelCrest lc = levelAndCrest(in.data() + settle, out.data() + settle, len);
            if (q == 120)
                ref = lc;
            INFO("amount " << quarterAmount(q) << ": L " << lc.levelDb << " dB, CF " << lc.crestDb << " dB");
            CHECK(std::abs(lc.levelDb - ref.levelDb) <= 1.0);
            CHECK(std::abs(lc.crestDb - ref.crestDb) <= 1.0);
            worstDl = std::max(worstDl, std::abs(lc.levelDb - ref.levelDb));
            worstDcf = std::max(worstDcf, std::abs(lc.crestDb - ref.crestDb));
        }
        std::printf("HarmonicFold_CompressionRegimeDensityBeforeLevel MIDI %d: THD %+.2f dB (plan +14.0), H %d -> %d, "
                    "worst |dL| %.3f dB, |dCF| %.3f dB (plan 0.68 / 0.68)\n",
                    note, d65.thdDb - d30.thdDb, d30.h, d65.h, worstDl, worstDcf);
    }
}

// =============================================================================
// SC-007(b): THD level-independent at the compression midpoint; a THD-matched
// static tanh in the same test must move >= 12 dB (teeth)
// =============================================================================

TEST_CASE("HarmonicFold_CompressionLevelIndependent", "[processors][profundum][edge-fold]") {
    constexpr int kMidCompression = 190;  // amount 47.5
    for (const int note : {kNoteE1, kNoteC2}) {
        const double f0 = midiHz(note);
        INFO("MIDI " << note);
        const std::array<double, 4> thd = thdOverLevels(kMidCompression, note);
        const double spread = spreadOf(thd);

        // Teeth: std::tanh(g·x) with g matched to the Fold's THD at -12 dBFS (index 2).
        const double g = bisectTanhDrive(thd[2], levelPeak(-12.0), kFs, f0);
        std::array<double, 4> tanhThd{};
        for (std::size_t i = 0; i < kLevelsDbfs.size(); ++i)
            tanhThd[i] = tanhDescriptors(g, levelPeak(kLevelsDbfs[i]), kFs, f0).thdDb;
        const double tanhSpread = spreadOf(tanhThd);

        std::printf("HarmonicFold_CompressionLevelIndependent MIDI %d: Fold THD %.2f / %.2f / %.2f / %.2f dB "
                    "(spread %.2f, bar 3); tanh g %.4f THD %.2f / %.2f / %.2f / %.2f dB (spread %.2f, bar 12)\n",
                    note, thd[0], thd[1], thd[2], thd[3], spread, g, tanhThd[0], tanhThd[1], tanhThd[2], tanhThd[3],
                    tanhSpread);
        REQUIRE(std::abs(tanhThd[2] - thd[2]) <= 0.1);
        CHECK(spread <= 3.0);
        CHECK(tanhSpread >= 12.0);
    }
}

// =============================================================================
// SC-007(c): the compression-regime trajectory is >= 3 dB (median) from a plain
// tanh sweep at matched THD; prints the RMS-matched median as SC-022(d)
// =============================================================================
// The Fold's (THD, L, CF) is rendered on a 1.0 amount grid over [30, 65] and
// interpolated linearly in THD at the 33 THD-equispaced points.

TEST_CASE("HarmonicFold_TrajectoryDistinctFromTanh", "[processors][profundum][edge-fold]") {
    const double f0 = midiHz(kNoteE1);
    const double peak = static_cast<double>(HarmonicFold::kCalibrationPeak);

    std::vector<Steady> fold;
    for (int q = 120; q <= 260; q += 4)
        fold.push_back(describeAt(Source::Sine, kNoteE1, kFs, q));
    for (std::size_t i = 1; i < fold.size(); ++i) {
        INFO("Fold THD must increase on [30, 65]: point " << i);
        REQUIRE(fold[i].thdDb > fold[i - 1].thdDb);
    }

    // Reference: std::tanh(g·x), g log-spaced 0.1 ... 100, 401 points.
    std::vector<StaticDescriptors> ref;
    for (int i = 0; i <= 400; ++i) {
        const double g = 0.1 * std::pow(1000.0, static_cast<double>(i) / 400.0);
        ref.push_back(tanhDescriptors(g, peak, kFs, f0));
    }

    const double thdLo = fold.front().thdDb;
    const double thdHi = fold.back().thdDb;
    // No extrapolation (spec SC-007(c) method): every Fold THD inside the reference range.
    REQUIRE(thdLo >= ref.front().thdDb);
    REQUIRE(thdHi <= ref.back().thdDb);
    // The reference is parameterised by THD: monotone over the span used.
    for (std::size_t i = 1; i < ref.size() && ref[i - 1].thdDb < thdHi; ++i) {
        INFO("tanh reference THD must increase: point " << i);
        REQUIRE(ref[i].thdDb > ref[i - 1].thdDb);
    }

    const auto interpFold = [&fold](double t) {
        std::size_t i = 1;
        while (i + 1 < fold.size() && fold[i].thdDb < t)
            ++i;
        const double u = (t - fold[i - 1].thdDb) / (fold[i].thdDb - fold[i - 1].thdDb);
        return std::pair<double, double>{fold[i - 1].levelDb + u * (fold[i].levelDb - fold[i - 1].levelDb),
                                         fold[i - 1].crestDb + u * (fold[i].crestDb - fold[i - 1].crestDb)};
    };
    const auto interpRef = [&ref](double t) {
        std::size_t i = 1;
        while (i + 1 < ref.size() && ref[i].thdDb < t)
            ++i;
        const double u = (t - ref[i - 1].thdDb) / (ref[i].thdDb - ref[i - 1].thdDb);
        return std::pair<double, double>{ref[i - 1].levelDb + u * (ref[i].levelDb - ref[i - 1].levelDb),
                                         ref[i - 1].crestDb + u * (ref[i].crestDb - ref[i - 1].crestDb)};
    };

    constexpr int kPoints = 33;
    std::vector<double> dist;
    std::vector<double> distRmsMatched;
    for (int j = 0; j < kPoints; ++j) {
        const double t = thdLo + (thdHi - thdLo) * static_cast<double>(j) / static_cast<double>(kPoints - 1);
        const auto [fL, fCF] = interpFold(t);
        const auto [rL, rCF] = interpRef(t);
        dist.push_back(std::sqrt((fL - rL) * (fL - rL) + (fCF - rCF) * (fCF - rCF)));
        // RMS-matched tanh·k(g): L = 0 by construction, CF unchanged by the gain.
        distRmsMatched.push_back(std::sqrt(fL * fL + (fCF - rCF) * (fCF - rCF)));
    }
    const auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    const double med = median(dist);
    const double medRms = median(distRmsMatched);
    std::printf("HarmonicFold_TrajectoryDistinctFromTanh E1: Fold THD %.2f ... %.2f dB; median distance %.3f dB "
                "(bar 3, plan 5.49); SC-022(d) RMS-matched tanh median %.3f dB (recorded)\n",
                thdLo, thdHi, med, medRms);
    // SC-022: a missing figure fails, its value does not (bit-pattern check: this TU is fast-math).
    REQUIRE(detail::isFinite(medRms));
    CHECK(med >= 3.0);
}

// =============================================================================
// SC-008(a): static continuity on the 0.25 grid
// =============================================================================

namespace {

struct ContinuityInput {
    Source source;
    int note;
};
constexpr std::array<ContinuityInput, 3> kContinuityInputs{
    {{Source::Sine, kNoteE1}, {Source::Sine, kNoteC2}, {Source::MidGrid, kNoteC2}}};

struct ContinuityPair {
    int q;
    double dL;
    double dCF;
    double dC;
    double dThd;
    bool thdApplies;
};

void requireRegimeContinuity(const std::vector<int>& quarters, const char* label) {
    for (const ContinuityInput& ci : kContinuityInputs) {
        std::vector<ContinuityPair> pairs;
        for (std::size_t i = 0; i + 1 < quarters.size(); ++i) {
            if (quarters[i + 1] != quarters[i] + 1)
                continue;
            const Steady a = describeAt(ci.source, ci.note, kFs, quarters[i]);
            const Steady b = describeAt(ci.source, ci.note, kFs, quarters[i + 1]);
            pairs.push_back(ContinuityPair{.q = quarters[i],
                                           .dL = std::abs(b.levelDb - a.levelDb),
                                           .dCF = std::abs(b.crestDb - a.crestDb),
                                           .dC = std::abs(b.centroidOct - a.centroidOct),
                                           .dThd = std::abs(b.thdDb - a.thdDb),
                                           .thdApplies = a.thdDb >= -60.0 && b.thdDb >= -60.0});
        }
        REQUIRE(!pairs.empty());

        std::vector<double> thdSteps;
        for (const ContinuityPair& p : pairs) {
            if (p.thdApplies)
                thdSteps.push_back(p.dThd);
        }
        double thdBound = 1.0;
        if (!thdSteps.empty()) {
            std::sort(thdSteps.begin(), thdSteps.end());
            thdBound = std::max(3.0 * thdSteps[thdSteps.size() / 2], 1.0);
        }

        double worstL = 0.0;
        double worstCF = 0.0;
        double worstC = 0.0;
        double worstThd = 0.0;
        for (const ContinuityPair& p : pairs) {
            INFO(label << ": " << sourceName(ci.source) << " MIDI " << ci.note << ", amount " << quarterAmount(p.q)
                       << " -> " << quarterAmount(p.q + 1) << ": |dL| " << p.dL << ", |dCF| " << p.dCF << ", |dC| "
                       << p.dC << ", |dTHD| " << p.dThd << " (bound " << thdBound << ")");
            CHECK(p.dL <= 0.25);
            CHECK(p.dCF <= 0.25);
            CHECK(p.dC <= 0.05);
            if (p.thdApplies) {
                CHECK(p.dThd <= thdBound);
                worstThd = std::max(worstThd, p.dThd);
            }
            worstL = std::max(worstL, p.dL);
            worstCF = std::max(worstCF, p.dCF);
            worstC = std::max(worstC, p.dC);
        }
        std::printf("%s %s MIDI %d (%zu pairs): worst |dL| %.3f dB, |dCF| %.3f dB, |dC| %.4f oct, |dTHD| %.3f dB "
                    "(bound %.3f) (plan D-1 sine 0.109 dB, 0.032 oct; mid 0.072 dB, 0.016 oct)\n",
                    label, sourceName(ci.source), ci.note, pairs.size(), worstL, worstCF, worstC, worstThd, thdBound);
    }
}

}  // namespace

TEST_CASE("HarmonicFold_RegimeContinuityStaticSmoke", "[processors][profundum][edge-fold]") {
    // The 0.25 pairs straddling 0, 30, 65 and 100, plus (a, a + 0.25) on the 41-point grid.
    std::set<int> q{0, 1, 119, 120, 121, 259, 260, 261, 399, 400};
    for (int k = 0; k < 40; ++k) {
        q.insert(10 * k);
        q.insert(10 * k + 1);
    }
    requireRegimeContinuity(std::vector<int>(q.begin(), q.end()), "HarmonicFold_RegimeContinuityStaticSmoke");
}

TEST_CASE("HarmonicFold_RegimeContinuityStatic", "[processors][profundum][edge-fold][long]") {
    requireRegimeContinuity(quarterGrid(1), "HarmonicFold_RegimeContinuityStatic");
}

// =============================================================================
// SC-008(b): C0 in amount, ε = 0.05
// =============================================================================

namespace {

void requireContinuousInAmount(const std::vector<int>& quarters, const char* label) {
    constexpr float kEps = 0.05f;
    const std::size_t settle = settleSamples(kFs);
    for (const ContinuityInput& ci : kContinuityInputs) {
        const double f0 = midiHz(ci.note);
        const std::vector<float> in = makeSource(ci.source, kFs, f0, shortSeconds(kFs));
        double worst = 0.0;
        float worstA = 0.0f;
        for (const int q : quarters) {
            const float a = quarterAmount(q);
            // y(a) against y(a + ε); at a = 100 (a + ε clamps to 100) the neighbour is 100 − ε.
            const float b = (a + kEps <= 100.0f) ? a + kEps : 100.0f - kEps;
            HarmonicFold fa = makeFold(kFs, a, f0);
            HarmonicFold fb = makeFold(kFs, b, f0);
            const std::vector<float> ya = renderMono(fa, in);
            const std::vector<float> yb = renderMono(fb, in);
            double sumD = 0.0;
            double sumY = 0.0;
            for (std::size_t k = settle; k < ya.size(); ++k) {
                const double d = static_cast<double>(yb[k]) - static_cast<double>(ya[k]);
                sumD += d * d;
                sumY += static_cast<double>(ya[k]) * static_cast<double>(ya[k]);
            }
            REQUIRE(sumY > 0.0);
            const double ratio = std::sqrt(sumD / sumY);
            INFO(label << ": " << sourceName(ci.source) << " MIDI " << ci.note << ", amount " << a << " vs " << b
                       << ": RMS ratio " << ratio);
            CHECK(ratio <= 0.02);
            if (ratio > worst) {
                worst = ratio;
                worstA = a;
            }
        }
        std::printf("%s %s MIDI %d: worst RMS(y(a+e) - y(a)) / RMS(y(a)) %.5f at amount %.2f (bar 0.02)\n", label,
                    sourceName(ci.source), ci.note, worst, static_cast<double>(worstA));
    }
}

}  // namespace

TEST_CASE("HarmonicFold_ContinuousInAmount", "[processors][profundum][edge-fold]") {
    requireContinuousInAmount(quarterGrid(10), "HarmonicFold_ContinuousInAmount");
}

TEST_CASE("HarmonicFold_ContinuousInAmountLong", "[processors][profundum][edge-fold][long]") {
    requireContinuousInAmount(quarterGrid(1), "HarmonicFold_ContinuousInAmountLong");
}

// =============================================================================
// SC-008(c): no jump under automation (sample delta and frame level)
// =============================================================================
// Scenarios: a 2 s 0 -> 100 -> 0 linear sweep set per 32-sample block (then 0.5 s
// at 0), and steps 0 <-> 5, 28 <-> 32 (±2 around 30) and 63 <-> 67 (±2 around
// 65) with 0.5 s holds, each after a t_settle pre-roll at the start amount. Each
// scenario's two monotone segments traverse the same amount range and share the
// same endpoints, so both are evaluated with the same bounds in one pass.
// "The amount range one hop traverses" is the scheduled amount over the hop that
// the later frame adds, shifted back by one smoother time constant
// (kAmountSmoothMs): for the sweep that is exactly the smoothed amount's range.

TEST_CASE("HarmonicFold_AutomationNoJump", "[processors][profundum][edge-fold]") {
    const std::size_t settle = settleSamples(kFs);
    const auto tauSamples =
        static_cast<std::size_t>(std::lround(static_cast<double>(HarmonicFold::kAmountSmoothMs) * 1.0e-3 * kFs));
    constexpr std::size_t kAutomationBlock = 32;
    const auto halfSecond = static_cast<std::size_t>(0.5 * kFs);
    const auto oneSecond = static_cast<std::size_t>(kFs);

    struct Scenario {
        const char* name;
        float lo;
        float hi;
        bool sweep;
    };
    constexpr std::array<Scenario, 4> kScenarios{{{"sweep 0 -> 100 -> 0", 0.0f, 100.0f, true},
                                                  {"steps 0 <-> 5", 0.0f, 5.0f, false},
                                                  {"steps 28 <-> 32", 28.0f, 32.0f, false},
                                                  {"steps 63 <-> 67", 63.0f, 67.0f, false}}};

    const auto qLo = [](float a) { return static_cast<std::size_t>(std::floor(static_cast<double>(a) * 4.0)); };
    const auto qHi = [](float a) {
        return std::min<std::size_t>(400, static_cast<std::size_t>(std::ceil(static_cast<double>(a) * 4.0)));
    };

    for (const ContinuityInput& ci : kContinuityInputs) {
        const double f0 = midiHz(ci.note);
        const auto frameLen = static_cast<std::size_t>(std::lround(4.0 * kFs / f0));
        const auto hop = static_cast<std::size_t>(std::lround(kFs / f0));

        // Steady renders on every 0.25-grid amount: L (the SC-008(a) quantity), the maximum
        // steady |y[k] − y[k−1]|, and the frame jitter under the scenario framing (for J).
        const std::vector<float> steadyIn = makeSource(ci.source, kFs, f0, shortSeconds(kFs));
        const std::size_t len = hft::integerPeriodLength(steadyIn.size() - settle, kFs, f0, 1.0);
        std::vector<double> gridL(401, 0.0);
        std::vector<double> gridMaxDelta(401, 0.0);
        std::vector<double> gridJitter(401, 0.0);
        for (int q = 0; q <= 400; ++q) {
            HarmonicFold fold = makeFold(kFs, quarterAmount(q), f0);
            const std::vector<float> y = renderMono(fold, steadyIn);
            const auto qi = static_cast<std::size_t>(q);
            gridL[qi] = levelAndCrest(steadyIn.data() + settle, y.data() + settle, len).levelDb;
            double md = 0.0;
            for (std::size_t k = settle + 1; k < settle + len; ++k)
                md = std::max(md, std::abs(static_cast<double>(y[k]) - static_cast<double>(y[k - 1])));
            gridMaxDelta[qi] = md;
            gridJitter[qi] = maxAdjacentChange(frameRmsDb(y, settle, y.size(), frameLen, hop));
        }

        for (const Scenario& sc : kScenarios) {
            const std::size_t start = settle;
            const std::size_t body = sc.sweep ? 2 * oneSecond + halfSecond : 2 * halfSecond;
            const std::size_t total = start + body;
            const std::vector<float> in = makeSource(ci.source, kFs, f0, static_cast<double>(total) / kFs);
            REQUIRE(in.size() >= total);

            // The scheduled amount, constant over each 32-sample block.
            std::vector<float> schedule(in.size(), sc.lo);
            for (std::size_t pos = start; pos < schedule.size(); pos += kAutomationBlock) {
                const std::size_t t = pos - start;
                float a = sc.lo;
                if (sc.sweep) {
                    if (t < oneSecond)
                        a = static_cast<float>(100.0 * static_cast<double>(t) / kFs);
                    else if (t < 2 * oneSecond)
                        a = static_cast<float>(100.0 - 100.0 * static_cast<double>(t - oneSecond) / kFs);
                    else
                        a = 0.0f;
                } else {
                    a = (t < halfSecond) ? sc.hi : sc.lo;
                }
                const std::size_t end = std::min(pos + kAutomationBlock, schedule.size());
                std::fill(schedule.begin() + static_cast<std::ptrdiff_t>(pos),
                          schedule.begin() + static_cast<std::ptrdiff_t>(end), a);
            }

            HarmonicFold fold = makeFold(kFs, sc.lo, f0);
            std::vector<float> y = in;
            for (std::size_t pos = 0; pos < y.size(); pos += kAutomationBlock) {
                const std::size_t n = std::min(kAutomationBlock, y.size() - pos);
                fold.setAmount(schedule[pos]);
                fold.processBlock(y.data() + pos, y.data() + pos, n);
            }

            // Evaluation starts one frame before the first change (a steady pre-change frame).
            REQUIRE(start >= frameLen);
            const std::size_t evalStart = start - frameLen;

            // Sample delta: <= 1.1 × the steady maximum over every grid amount traversed.
            double steadyMax = 0.0;
            for (std::size_t q = qLo(sc.lo); q <= qHi(sc.hi); ++q)
                steadyMax = std::max(steadyMax, gridMaxDelta[q]);
            double worstDelta = 0.0;
            for (std::size_t k = evalStart + 1; k < y.size(); ++k)
                worstDelta = std::max(worstDelta, std::abs(static_cast<double>(y[k]) - static_cast<double>(y[k - 1])));
            {
                INFO(sourceName(ci.source) << " MIDI " << ci.note << ", " << sc.name << ": worst |dy| " << worstDelta
                                           << " vs 1.1 x steady max " << steadyMax);
                CHECK(worstDelta <= 1.1 * steadyMax);
            }

            // Frame level: change <= J + max(1 dB, 1.5 × the |dL| the grid accumulates over one hop).
            const double J = std::max(gridJitter[qLo(sc.lo)], gridJitter[qHi(sc.hi)]);
            const std::vector<double> frames = frameRmsDb(y, evalStart, y.size(), frameLen, hop);
            double worstExcess = -1.0e300;
            for (std::size_t i = 1; i < frames.size(); ++i) {
                // The hop frame i adds: [end of frame i−1, end of frame i), shifted back by tau.
                const std::size_t hopEnd = evalStart + i * hop + frameLen;
                const std::size_t hopBegin = hopEnd - hop;
                const std::size_t b0 = hopBegin - std::min(hopBegin, tauSamples);
                const std::size_t b1 = std::min(hopEnd - std::min(hopEnd, tauSamples), schedule.size() - 1);
                float aMin = schedule[b0];
                float aMax = schedule[b0];
                for (std::size_t k = b0; k <= b1; ++k) {
                    aMin = std::min(aMin, schedule[k]);
                    aMax = std::max(aMax, schedule[k]);
                }
                double accL = 0.0;
                if (aMax > aMin) {
                    for (std::size_t q = qLo(aMin); q < qHi(aMax); ++q)
                        accL += std::abs(gridL[q + 1] - gridL[q]);
                }
                const double allowance = J + std::max(1.0, 1.5 * accL);
                const double change = std::abs(frames[i] - frames[i - 1]);
                INFO(sourceName(ci.source) << " MIDI " << ci.note << ", " << sc.name << ", frame " << i
                                           << ": change " << change << " dB, allowance " << allowance << " (J " << J
                                           << ", amount " << aMin << " ... " << aMax << ")");
                CHECK(change <= allowance);
                worstExcess = std::max(worstExcess, change - allowance);
            }
            std::printf("HarmonicFold_AutomationNoJump %s MIDI %d, %s: worst |dy| %.5f (bound %.5f); "
                        "worst frame change - allowance %+.3f dB (J %.3f)\n",
                        sourceName(ci.source), ci.note, sc.name, worstDelta, 1.1 * steadyMax, worstExcess, J);
        }
    }
}

// =============================================================================
// SC-011 (Fold arm): DC <= -80 dBFS with symmetric and even-harmonic input
// =============================================================================
// The real max-excitation input is the chain arm (T015).

TEST_CASE("HarmonicFold_DcFreeWithEvenHarmonicInput", "[processors][profundum][edge-fold]") {
    const std::size_t settle = settleSamples(kFs);
    double worstDb = -1.0e300;
    for (const int note : {kNoteE0, kNoteE1, kNoteE2}) {
        const double f0 = midiHz(note);
        const std::vector<float> sine = hft::calibrationSine(kFs, f0, shortSeconds(kFs));

        // (ii) asymmetric input in double: h1 + h2 at -20 dB + h4 at -30 dB re h1, sine phase,
        // rescaled to -12 dBFS RMS.
        const double a2 = std::pow(10.0, -20.0 / 20.0);
        const double a4 = std::pow(10.0, -30.0 / 20.0);
        const double unitRms = std::sqrt((1.0 + a2 * a2 + a4 * a4) * 0.5);
        const double scale =
            std::pow(10.0, static_cast<double>(SpectralShapeRecipe::kCoreOutputRmsDb) / 20.0) / unitRms;
        std::vector<float> asym(sine.size(), 0.0f);
        for (std::size_t n = 0; n < asym.size(); ++n) {
            const double ph = hft::kTwoPi * f0 * static_cast<double>(n) / kFs;
            asym[n] = static_cast<float>(scale * (std::sin(ph) + a2 * std::sin(2.0 * ph) + a4 * std::sin(4.0 * ph)));
        }

        const std::size_t len = hft::integerPeriodLength(sine.size() - settle, kFs, f0, 1.0);
        for (const float amount : {30.0f, 65.0f, 100.0f}) {
            for (int which = 0; which < 2; ++which) {
                const std::vector<float>& in = (which == 0) ? sine : asym;
                HarmonicFold fold = makeFold(kFs, amount, f0);
                const std::vector<float> y = renderMono(fold, in);
                double sum = 0.0;
                for (std::size_t k = settle; k < settle + len; ++k)
                    sum += static_cast<double>(y[k]);
                const double mean = sum / static_cast<double>(len);
                const double dcDb = hft::safeDb10(mean * mean);  // 20·log10|mean| re 1.0
                INFO("MIDI " << note << ", amount " << amount << ", " << (which == 0 ? "sine" : "asymmetric")
                             << ": DC " << dcDb << " dBFS");
                CHECK(dcDb <= -80.0);
                worstDb = std::max(worstDb, dcDb);
            }
        }
    }
    std::printf("HarmonicFold_DcFreeWithEvenHarmonicInput: worst DC %.1f dBFS (bar -80)\n", worstDb);
}

// =============================================================================
// SC-012(d) (Fold arm): h1 phase within ±5°, no polarity flip, |T1| floors
// =============================================================================

namespace {

void requireFundamentalPreserved(const std::vector<int>& quarters, const char* label) {
    double worstAngle = 0.0;
    double minMagBelow65 = 1.0e300;
    double minMagFold = 1.0e300;
    for (const double fs : {44100.0, kFs, 96000.0}) {
        const std::size_t settle = settleSamples(fs);
        for (const int note : {kNoteE0, kNoteE1, kNoteE2}) {
            const double f0 = midiHz(note);
            for (const Source src : {Source::Sine, Source::Saw}) {
                const std::vector<float> in = makeSource(src, fs, f0, shortSeconds(fs));
                const std::size_t len = hft::integerPeriodLength(in.size() - settle, fs, f0, 1.0);
                const std::vector<double> w = hft::bh7Window(len);
                const std::complex<double> hIn = phasorWithWindow(in.data() + settle, w, fs, f0);
                REQUIRE(std::norm(hIn) > 0.0);
                for (const int q : quarters) {
                    const float a = quarterAmount(q);
                    HarmonicFold fold = makeFold(fs, a, f0);
                    const std::vector<float> y = renderMono(fold, in);
                    const std::complex<double> t1 = phasorWithWindow(y.data() + settle, w, fs, f0) / hIn;
                    const double angle = std::arg(t1) * 180.0 / kHalfTurnRad;
                    const double magDb = 20.0 * std::log10(std::max(std::abs(t1), 1.0e-300));
                    INFO(label << ": fs " << fs << ", MIDI " << note << ", " << sourceName(src) << ", amount " << a
                               << ": angle " << angle << " deg, |T1| " << magDb << " dB");
                    CHECK(std::abs(angle) <= 5.0);
                    CHECK(t1.real() > 0.0);
                    if (a < HarmonicFold::kCompressionEnd) {
                        CHECK(magDb >= -6.0);
                        minMagBelow65 = std::min(minMagBelow65, magDb);
                    } else {
                        CHECK(magDb >= -12.0);
                        minMagFold = std::min(minMagFold, magDb);
                    }
                    worstAngle = std::max(worstAngle, std::abs(angle));
                }
            }
        }
    }
    std::printf("%s: worst |angle T1| %.3f deg (bar 5), min |T1| %.2f dB below 65 (bar -6), %.2f dB on [65, 100] "
                "(bar -12, plan -9.46)\n",
                label, worstAngle, minMagBelow65, minMagFold);
}

}  // namespace

TEST_CASE("HarmonicFold_FundamentalPreserved", "[processors][profundum][edge-fold]") {
    requireFundamentalPreserved(quarterGrid(10), "HarmonicFold_FundamentalPreserved");
}

TEST_CASE("HarmonicFold_FundamentalPreservedLong", "[processors][profundum][edge-fold][long]") {
    requireFundamentalPreserved(quarterGrid(1), "HarmonicFold_FundamentalPreservedLong");
}

// =============================================================================
// SC-013(c) detector clause (plan S1 C-4, ratified per-channel 2026-10-10): the
// input's L/R ratio survives steady state and a -24 -> -6 dBFS step
// =============================================================================

TEST_CASE("HarmonicFold_StereoDetectorPerChannel", "[processors][profundum][edge-fold]") {
    const double f0 = midiHz(kNoteE1);
    const auto hold = static_cast<std::size_t>(kFs);  // 1 s holds
    const std::size_t settle = settleSamples(kFs);
    const double rGain = std::pow(10.0, -6.02 / 20.0);
    std::vector<float> inL(2 * hold, 0.0f);
    std::vector<float> inR(2 * hold, 0.0f);
    for (std::size_t n = 0; n < inL.size(); ++n) {
        const double pk = levelPeak(n < hold ? -24.0 : -6.0);
        const double s = std::sin(hft::kTwoPi * f0 * static_cast<double>(n) / kFs);
        inL[n] = static_cast<float>(pk * s);
        inR[n] = static_cast<float>(pk * rGain * s);
    }
    const auto frameLen = static_cast<std::size_t>(std::lround(4.0 * kFs / f0));
    const auto hop = static_cast<std::size_t>(std::lround(kFs / f0));
    const auto ratioDb = [](const std::vector<float>& a, const std::vector<float>& b, std::size_t start,
                            std::size_t len) {
        double sa = 0.0;
        double sb = 0.0;
        for (std::size_t k = start; k < start + len; ++k) {
            sa += static_cast<double>(a[k]) * static_cast<double>(a[k]);
            sb += static_cast<double>(b[k]) * static_cast<double>(b[k]);
        }
        REQUIRE(sb > 0.0);
        return hft::safeDb10(sa / sb);
    };

    // Steady windows: [t_settle, step) and [step + t_settle, end), integer periods.
    const std::size_t preStart = settle;
    const std::size_t preLen = hft::integerPeriodLength(hold - settle, kFs, f0, 0.0);
    const std::size_t postStart = hold + settle;
    const std::size_t postLen = hft::integerPeriodLength(2 * hold - postStart, kFs, f0, 0.0);

    for (const float amount : {47.5f, 82.5f, 100.0f}) {
        HarmonicFold fold = makeFold(kFs, amount, f0);
        const auto [L, R] = hft::renderFold(fold, inL, inR, 512);
        INFO("amount " << amount);

        const double preDev = std::abs(ratioDb(L, R, preStart, preLen) - ratioDb(inL, inR, preStart, preLen));
        const double postDev = std::abs(ratioDb(L, R, postStart, postLen) - ratioDb(inL, inR, postStart, postLen));
        CHECK(preDev <= 0.1);
        CHECK(postDev <= 0.1);

        // Through the step: 4-period frames hopped by one period.
        double worstFrame = 0.0;
        for (std::size_t pos = settle; pos + frameLen <= L.size(); pos += hop) {
            const double dev = std::abs(ratioDb(L, R, pos, frameLen) - ratioDb(inL, inR, pos, frameLen));
            INFO("frame at " << pos << ": L/R deviation " << dev << " dB");
            CHECK(dev <= 0.1);
            worstFrame = std::max(worstFrame, dev);
        }
        std::printf("HarmonicFold_StereoDetectorPerChannel amount %.1f: steady deviation %.4f / %.4f dB, "
                    "worst frame deviation %.4f dB (bar 0.1, plan 0.000)\n",
                    static_cast<double>(amount), preDev, postDev, worstFrame);
    }

    // Teeth: the rejected shared max(|L|, |R|) detector, built in the test on the S4.3 static map
    // (steady state: G = P / the shared windowed peak), must move the ratio by > 0.1 dB at 82.5.
    const StaticMapModel m = staticMapModel(82.5);
    double sharedPeak = 0.0;
    for (std::size_t k = preStart; k < preStart + preLen; ++k) {
        sharedPeak = std::max(sharedPeak, std::abs(static_cast<double>(inL[k])));
        sharedPeak = std::max(sharedPeak, std::abs(static_cast<double>(inR[k])));
    }
    REQUIRE(sharedPeak > 0.0);
    const double G = m.P / sharedPeak;
    double sl = 0.0;
    double sr = 0.0;
    for (std::size_t k = preStart; k < preStart + preLen; ++k) {
        const double yLeft = m.phi(static_cast<double>(inL[k]), G);
        const double yRight = m.phi(static_cast<double>(inR[k]), G);
        sl += yLeft * yLeft;
        sr += yRight * yRight;
    }
    REQUIRE(sr > 0.0);
    const double sharedRatio = hft::safeDb10(sl / sr);
    const double inputRatio = ratioDb(inL, inR, preStart, preLen);
    std::printf("HarmonicFold_StereoDetectorPerChannel teeth: shared-detector L/R ratio at 82.5 %.3f dB "
                "vs input %.3f dB (plan D-1 0.53 vs 6.02)\n",
                sharedRatio, inputRatio);
    REQUIRE(std::abs(sharedRatio - inputRatio) > 0.1);
}

// =============================================================================
// SC-024(b) + plan S1 C-3: no in-cycle gain modulation
// =============================================================================
// (i) the periodicity residual, kept without its teeth claim (C-3); (ii) C-3's
// (b'): detectorLevel() ripple over one steady period, with a double x/env(x)
// reference (instant attack, 1 ms release) that must show ripple >= 0.1.

TEST_CASE("HarmonicFold_NoInCycleGainModulation", "[processors][profundum][edge-fold]") {
    const auto hold = static_cast<std::size_t>(2.0 * kFs);
    const auto oneSecond = static_cast<std::size_t>(kFs);
    const std::size_t settle = settleSamples(kFs);
    const double alphaRef = 1.0 - std::exp(-1.0 / (1.0e-3 * kFs));

    for (const std::size_t period : {std::size_t{2400}, std::size_t{1200}, std::size_t{600}}) {
        const double f0 = kFs / static_cast<double>(period);  // 20 / 40 / 80 Hz
        std::vector<float> in(2 * hold, 0.0f);
        for (std::size_t n = 0; n < in.size(); ++n) {
            const double pk = levelPeak(n < hold ? -24.0 : -6.0);
            in[n] = static_cast<float>(pk * std::sin(hft::kTwoPi * f0 * static_cast<double>(n) / kFs));
        }

        // Teeth reference env on |x| in double.
        std::vector<double> refEnv(in.size(), 0.0);
        double e = 0.0;
        for (std::size_t n = 0; n < in.size(); ++n) {
            const double d = std::abs(static_cast<double>(in[n]));
            e = (d > e) ? d : e + alphaRef * (d - e);
            refEnv[n] = e;
        }

        for (const float amount : {47.5f, 82.5f}) {
            HarmonicFold fold = makeFold(kFs, amount, f0);
            std::vector<float> y = in;
            std::vector<double> env(in.size(), 0.0);
            for (std::size_t k = 0; k < y.size(); ++k) {  // 1-sample blocks (legal by SC-017)
                fold.processBlock(y.data() + k, y.data() + k, 1);
                env[k] = static_cast<double>(fold.detectorLevel());
            }

            for (const std::size_t start : {settle, hold + settle}) {
                REQUIRE(start + oneSecond + period <= y.size());
                double sd = 0.0;
                double sy = 0.0;
                for (std::size_t k = start; k < start + oneSecond; ++k) {
                    const double d = static_cast<double>(y[k + period]) - static_cast<double>(y[k]);
                    sd += d * d;
                    sy += static_cast<double>(y[k]) * static_cast<double>(y[k]);
                }
                REQUIRE(sy > 0.0);
                const double residualDb = hft::safeDb10(sd / sy);

                // Ripple over the last steady period of the hold.
                const std::size_t ripEnd = (start < hold) ? hold : y.size();
                const double ripple = rippleOf(env.data() + (ripEnd - period), env.data() + ripEnd);
                const double refRipple = rippleOf(refEnv.data() + (ripEnd - period), refEnv.data() + ripEnd);

                INFO("f0 " << f0 << " Hz, amount " << amount << (start < hold ? ", before" : ", after")
                           << " the step: residual " << residualDb << " dB, ripple " << ripple << ", reference ripple "
                           << refRipple);
                CHECK(residualDb <= -50.0);
                CHECK(ripple <= 1.0e-4);
                CHECK(refRipple >= 0.1);
                std::printf("HarmonicFold_NoInCycleGainModulation f0 %.0f Hz amount %.1f %s: residual %.1f dB "
                            "(bar -50), ripple %.3g (bar 1e-4), x/env reference ripple %.3f (bar >= 0.1)\n",
                            f0, static_cast<double>(amount), start < hold ? "pre " : "post", residualDb, ripple,
                            refRipple);
            }
        }
    }
}

// =============================================================================
// D-4 (tasks.md): steady detector on a non-integer sample period
// =============================================================================
// The SC-024(b) fixture above uses integer periods only. At E1 / 48 kHz the period is
// 1164.95 samples, so the plain sample maximum of a sharp-peaked waveform moves with the sample
// phase; the windowed maximum stepped as the window slid and modulated G at a non-harmonic rate
// (SC-005 read -76 dBFS at amount 100). The C-3 ripple bar (1e-4) must hold here too.

TEST_CASE("HarmonicFold_DetectorSteadyOnNonIntegerPeriod", "[processors][profundum][edge-fold]") {
    const double f0 = midiHz(kNoteE1);
    // 64 partials (the core's count) and a wide-band saw to 0.45·fs (sharp Gibbs peaks, as the
    // excitation's output at max drive presents to the chain).
    for (const bool wide : {false, true})
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        const int partials = wide ? static_cast<int>(0.45 * fs / f0) : 64;
        const std::size_t settle = settleSamples(fs);
        const auto window = static_cast<std::size_t>(2.0 * fs);
        std::vector<float> y(settle + window, 0.0f);
        for (std::size_t n = 0; n < y.size(); ++n) {  // band-limited saw, peak ~0.31-0.44
            double s = 0.0;
            for (int h = 1; h <= partials; ++h)
                s += std::sin(hft::kTwoPi * h * f0 * static_cast<double>(n) / fs) / h;
            y[n] = static_cast<float>(0.2 * s);
        }
        HarmonicFold fold = makeFold(fs, 100.0f, f0);
        std::vector<double> env(y.size(), 0.0);
        for (std::size_t k = 0; k < y.size(); k += 64) {
            const std::size_t n = std::min<std::size_t>(64, y.size() - k);
            fold.processBlock(y.data() + k, y.data() + k, n);
            env[k] = static_cast<double>(fold.detectorLevel());
            for (std::size_t j = k + 1; j < k + n; ++j)
                env[j] = env[k];
        }
        const double ripple = rippleOf(env.data() + settle, env.data() + y.size());
        std::printf("HarmonicFold_DetectorSteadyOnNonIntegerPeriod %d partials, fs %.0f, f0 %.4f Hz: ripple %.3g "
                    "(bar 1e-4)\n",
                    partials, fs, f0, ripple);
        INFO(partials << " partials, fs " << fs << ": ripple " << ripple);
        CHECK(ripple <= 1.0e-4);
    }
}

// D-4's release hysteresis must not stop the release: after a 12 dB drop the detector settles
// within kDetectorReleaseHysteresis above the new peak (it read the old peak before the drop).
TEST_CASE("HarmonicFold_DetectorReleasesAfterLevelDrop", "[processors][profundum][edge-fold]") {
    const double f0 = midiHz(kNoteE1);
    const std::size_t half = static_cast<std::size_t>(2.0 * kFs);
    std::vector<float> y(2 * half, 0.0f);
    const double hi = levelPeak(-12.0);
    const double lo = levelPeak(-24.0);
    for (std::size_t n = 0; n < y.size(); ++n)
        y[n] = static_cast<float>((n < half ? hi : lo) * std::sin(hft::kTwoPi * f0 * static_cast<double>(n) / kFs));
    HarmonicFold fold = makeFold(kFs, 82.5f, f0);
    fold.processBlock(y.data(), y.data(), half);
    const double before = static_cast<double>(fold.detectorLevel());
    fold.processBlock(y.data() + half, y.data() + half, half);
    const double after = static_cast<double>(fold.detectorLevel());
    const double h = static_cast<double>(HarmonicFold::kDetectorReleaseHysteresis);
    std::printf("HarmonicFold_DetectorReleasesAfterLevelDrop: env %.6f before (peak %.6f), %.6f after (peak %.6f, "
                "bar <= peak/(1-h) = %.6f)\n",
                before, hi, after, lo, lo / (1.0 - h));
    CHECK(before == Approx(hi).epsilon(1.0e-3));
    CHECK(after >= lo * (1.0 - 1.0e-4));
    CHECK(after <= lo / (1.0 - h) * (1.0 + 1.0e-4));
}

// =============================================================================
// SC-025: level behaviour per regime
// =============================================================================

TEST_CASE("HarmonicFold_SoftRegimeLevelDependent", "[processors][profundum][edge-fold]") {
    constexpr int kSoftMid = 60;  // amount 15 = kSoftEnd / 2
    static_assert(HarmonicFold::kSoftEnd / 2.0f == 15.0f);
    for (const int note : {kNoteE1, kNoteC2}) {
        const std::array<double, 4> thd = thdOverLevels(kSoftMid, note);
        std::printf("HarmonicFold_SoftRegimeLevelDependent MIDI %d: THD %.2f / %.2f / %.2f / %.2f dB, spread %.2f "
                    "(bar >= 12)\n",
                    note, thd[0], thd[1], thd[2], thd[3], spreadOf(thd));
        INFO("MIDI " << note);
        CHECK(spreadOf(thd) >= 12.0);
    }
}

TEST_CASE("HarmonicFold_FoldRegimeLevelNormalised", "[processors][profundum][edge-fold]") {
    constexpr int kFoldMid = 330;  // amount 82.5 = (kCompressionEnd + 100) / 2
    static_assert((HarmonicFold::kCompressionEnd + 100.0f) / 2.0f == 82.5f);
    for (const int note : {kNoteE1, kNoteC2}) {
        const std::array<double, 4> thd = thdOverLevels(kFoldMid, note);
        std::printf("HarmonicFold_FoldRegimeLevelNormalised MIDI %d: THD %.2f / %.2f / %.2f / %.2f dB, spread %.2f "
                    "(bar <= 3)\n",
                    note, thd[0], thd[1], thd[2], thd[3], spreadOf(thd));
        INFO("MIDI " << note);
        CHECK(spreadOf(thd) <= 3.0);
    }
}
