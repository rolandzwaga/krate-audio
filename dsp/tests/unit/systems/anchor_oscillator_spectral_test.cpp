// ==============================================================================
// Layer 3: System Tests - Profundum Anchor, spectral TU
//                                    (Profundum Phase 3, specs/profundum-phase3-anchor)
// ==============================================================================
// Reference: specs/profundum-phase3-anchor/plan.md (S8.1)
//            specs/profundum-phase3-anchor/tasks.md (T006 wires this TU,
//                                                    T011, T012, T014, T019 fill it)
//
// SCOPE OF THIS TU (plan S8.1): SC-001, SC-002, SC-008 (a)(b)(c), SC-010, SC-011,
//   SC-012, SC-025. Tags [systems][profundum][anchor].
//   T019: SC-012 (AnchorOscillator_AudibilityGate) at the end of the TU.
//
// T011 fixtures: level 1, LOCK, a1 = kMaxCoreH1Amplitude unless stated (tasks.md T011).
// ==============================================================================

#include "anchor_oscillator_test_helpers.h"

#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/anchor_oscillator.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

using Krate::DSP::AnchorOscillator;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;

namespace {

using AW = AnchorOscillator::Waveform;
using AM = AnchorOscillator::Mode;
using ARP = AnchorOscillator::RetriggerPhase;

constexpr std::array<AW, 5> kWaves{AW::Sine, AW::Triangle, AW::RoundedSine, AW::AsymmetricSine, AW::OctaveDownSine};
constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;   // 262144
constexpr std::size_t kSettle = 262144;                                       // descriptor rule (tasks.md rule 11)
constexpr int kThdHarmonics = 31;
constexpr double kHarmonicFloorDb = -90.0;

const char* waveName(AW w) {
    switch (w) {
        case AW::Sine: return "sine";
        case AW::Triangle: return "triangle";
        case AW::RoundedSine: return "rounded";
        case AW::AsymmetricSine: return "asymmetric";
        case AW::OctaveDownSine: return "octave-down";
    }
    return "?";
}

/// f_a: the waveform's own fundamental (octave-down runs at f0/2, FR-015).
double waveFa(AW w, double f0) { return w == AW::OctaveDownSine ? 0.5 * f0 : f0; }

/// A fresh Anchor at fs: waveform w, LOCK, level 1, a1, Reset noteOn f0; n samples in 512 blocks.
std::vector<float> renderWave(AW w, float f0, double fs, std::size_t n,
                              float a1 = AnchorOscillator::kMaxCoreH1Amplitude) {
    AnchorOscillator a;
    a.prepare(fs);
    AnchorOscillator::Controls c{};
    c.waveform = w;
    c.mode = AM::Lock;
    c.level = 1.0f;
    a.setControls(c);
    a.setCoreH1Amplitude(a1);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(f0);
    PT::AnchorRender r = PT::renderAnchor(a, n, 512);
    REQUIRE(a.stateFinite());
    return std::move(r.L);
}

/// T014: a fresh Anchor at fs: waveform w, FOLLOW, level 1, a1 = kMaxCoreH1Amplitude, held ratio r,
/// Reset noteOn f0; n samples in 512 blocks with the optional whole-render trajectories.
std::vector<float> renderFollowT014(AW w, float f0, double fs, std::size_t n, float r, const float* f0Traj = nullptr,
                                    const float* rTraj = nullptr) {
    AnchorOscillator a;
    a.prepare(fs);
    AnchorOscillator::Controls c{};
    c.waveform = w;
    c.mode = AM::Follow;
    c.level = 1.0f;
    a.setControls(c);
    a.setFollowRatio(r);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(f0);
    PT::AnchorRender out = PT::renderAnchor(a, n, 512, f0Traj, rTraj);
    REQUIRE(a.stateFinite());
    return std::move(out.L);
}

double centsT014(double f, double ref) {
    return 1200.0 * std::log2(f / ref);
}

/// 10·log10(wn/w1) for n = 1..N (index n − 1).
std::vector<double> relDb(const std::vector<double>& pw) {
    REQUIRE(!pw.empty());
    REQUIRE(pw[0] > 0.0);
    std::vector<double> d(pw.size(), 0.0);
    for (std::size_t i = 0; i < pw.size(); ++i)
        d[i] = PT::safeDb10(pw[i] / pw[0]);
    return d;
}

/// THD = Σ_{n≥2} wn / w1 in dB (tasks.md rule 11).
double thdDb(const std::vector<double>& pw) {
    double s = 0.0;
    for (std::size_t i = 1; i < pw.size(); ++i)
        s += pw[i];
    return PT::safeDb10(s / pw[0]);
}

/// The recipe's per-harmonic cap branch restated in double from its constants
/// (spectral_shape_recipe.h:72-75, cap branch :173-182), without the low-note guard; h1 exempt.
double recipeTaper(int n, double f, double fs) {
    if (n == 1)
        return 1.0;
    const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(fs));
    const double taperCents = static_cast<double>(SpectralShapeRecipe::kCapTaperCents);
    const double tailCents = static_cast<double>(SpectralShapeRecipe::kCapTailCents);
    const double dbPerCent = static_cast<double>(SpectralShapeRecipe::kCapTaperDbPerCent);
    const double taperStartHz = capHz * std::exp2(-taperCents / 1200.0);
    const double tailFloor = std::pow(10.0, -dbPerCent * (taperCents - tailCents) / 20.0);
    const double nf = static_cast<double>(n) * f;
    if (nf >= capHz)
        return 0.0;
    if (nf <= taperStartHz)
        return 1.0;
    const double y = 1200.0 * std::log2(capHz / nf);
    if (y >= taperCents)
        return 1.0;
    return y > tailCents ? std::pow(10.0, -dbPerCent * (taperCents - y) / 20.0) : tailFloor * std::max(0.0, y) / tailCents;
}

/// Asserts SC-001(b)'s triangle shape on a harmonic-power vector at f_a; returns h3 in dB re h1.
double checkTriangleShape(const std::vector<double>& pw, double fa, double fs, const char* tag) {
    const std::vector<double> d = relDb(pw);
    for (int n = 3; n <= 15; n += 2) {
        const double expected = -40.0 * std::log10(static_cast<double>(n));
        INFO(tag << " triangle n " << n << ": " << d[static_cast<std::size_t>(n - 1)] << " dB, expected " << expected);
        CHECK(std::abs(d[static_cast<std::size_t>(n - 1)] - expected) <= 0.5);
    }
    for (int n = 2; n <= kThdHarmonics; n += 2) {
        if (static_cast<double>(n) * fa >= 0.5 * fs)
            continue;
        INFO(tag << " triangle even n " << n << ": " << d[static_cast<std::size_t>(n - 1)] << " dB");
        CHECK(d[static_cast<std::size_t>(n - 1)] <= kHarmonicFloorDb);
    }
    const double thd = thdDb(pw);
    INFO(tag << " triangle THD " << thd << " dB (model -18.335)");
    CHECK(std::abs(thd - (-18.3)) <= 0.3);
    return d[2];
}

/// Every n ≥ 2 other than `except` (0 = none) is at or below the −90 dB floor re w1.
void checkOthersBelowFloor(const std::vector<double>& pw, int except, double fa, double fs, const char* tag) {
    const std::vector<double> d = relDb(pw);
    for (int n = 2; n <= kThdHarmonics; ++n) {
        if (n == except || static_cast<double>(n) * fa >= 0.5 * fs)
            continue;
        INFO(tag << " n " << n << ": " << d[static_cast<std::size_t>(n - 1)] << " dB re w1");
        CHECK(d[static_cast<std::size_t>(n - 1)] <= kHarmonicFloorDb);
    }
}

}  // namespace

// =============================================================================
// SC-001 (a)-(e): THD per waveform (FR-010, FR-017, D-2, plan S1 C-1)
// =============================================================================

TEST_CASE("AnchorOscillator_WaveformThd", "[systems][profundum][anchor]") {
    constexpr double kA3Db = -12.041199826559248;   // 20·log10(0.25), FR-017
    for (const double fs : kRates) {
        for (const int midi : {28, 36, 48}) {   // E1, C2, C3
            const float f0 = PT::anchorMidiHz(midi);
            double triH3 = 0.0;
            double roundedH3 = 0.0;
            for (const AW w : kWaves) {
                const double fa = waveFa(w, static_cast<double>(f0));
                const std::vector<float> x = renderWave(w, f0, fs, kSettle + kFrame);
                const std::vector<double> pw = PT::renderedHarmonicPowers(x.data() + kSettle, fs, fa, kThdHarmonics);
                REQUIRE(pw[0] > 0.0);
                const std::vector<double> d = relDb(pw);
                const double thd = thdDb(pw);
                std::printf("SC-001 %-11s midi %d fs %.0f: THD %.3f dB, h2 %.3f h3 %.3f h5 %.3f dB re w1\n", waveName(w),
                            midi, fs, thd, d[1], d[2], d[4]);
                INFO(waveName(w) << " midi " << midi << " fs " << fs);
                switch (w) {
                    case AW::Sine:   // (a)
                        CHECK(thd <= -90.0);
                        break;
                    case AW::Triangle:   // (b)
                        triH3 = checkTriangleShape(pw, fa, fs, "SC-001(b)");
                        break;
                    case AW::RoundedSine:   // (c)
                        roundedH3 = d[2];
                        CHECK(std::abs(d[2] - kA3Db) <= 0.25);
                        checkOthersBelowFloor(pw, 3, fa, fs, "SC-001(c) rounded");
                        break;
                    case AW::AsymmetricSine:   // (d)
                        CHECK(std::abs(d[1] - kA3Db) <= 0.25);
                        checkOthersBelowFloor(pw, 2, fa, fs, "SC-001(d) asymmetric");
                        break;
                    case AW::OctaveDownSine:   // (e): THD re f0/2, and w at f0 (n = 2 of f0/2)
                        CHECK(thd <= -90.0);
                        CHECK(d[1] <= -90.0);
                        break;
                }
            }
            std::printf("SC-001(c) midi %d fs %.0f: rounded h3 - triangle h3 = %.3f dB (bar 7.0)\n", midi, fs,
                        roundedH3 - triH3);
            CHECK(roundedH3 - triH3 >= 7.0);
        }
    }
}

// =============================================================================
// SC-001(f): the THD tool's floor and a non-bin-centred triangle
// =============================================================================

TEST_CASE("AnchorOscillator_ThdToolFloor", "[systems][profundum][anchor]") {
    const auto a1 = AnchorOscillator::kMaxCoreH1Amplitude;
    for (const double fs : kRates) {
        for (const int midi : {28, 36, 48}) {
            const double f0 = static_cast<double>(PT::anchorMidiHz(midi));
            std::vector<float> x(kFrame, 0.0f);
            for (std::size_t i = 0; i < kFrame; ++i) {
                double ph = f0 * static_cast<double>(i) / fs;
                ph -= std::floor(ph);
                const auto ang = static_cast<float>(PT::kTwoPi * (ph >= 0.5 ? ph - 1.0 : ph));
                x[i] = a1 * std::sin(ang);
            }
            const std::vector<double> pw = PT::renderedHarmonicPowers(x.data(), fs, f0, kThdHarmonics);
            const double thd = thdDb(pw);
            std::printf("SC-001(f) float sine midi %d fs %.0f: THD %.3f dB (bar -100)\n", midi, fs, thd);
            INFO("midi " << midi << " fs " << fs);
            CHECK(thd <= -100.0);
        }
    }

    // A deliberately non-bin-centred note reproduces (b).
    constexpr double kFs = 48000.0;
    const auto f0 = static_cast<float>(static_cast<double>(PT::anchorMidiHz(36)) * 1.0003);
    const std::vector<float> x = renderWave(AW::Triangle, f0, kFs, kSettle + kFrame);
    const std::vector<double> pw =
        PT::renderedHarmonicPowers(x.data() + kSettle, kFs, static_cast<double>(f0), kThdHarmonics);
    const double h3 = checkTriangleShape(pw, static_cast<double>(f0), kFs, "SC-001(f) C2 x 1.0003");
    std::printf("SC-001(f) triangle C2 x 1.0003: h3 %.3f dB, THD %.3f dB\n", h3, thdDb(pw));
}

// =============================================================================
// FR-012, E-2: the triangle's cap taper keyed on f_T (plan S4.5, C-1)
// =============================================================================

TEST_CASE("AnchorOscillator_TriangleCapTaper", "[systems][profundum][anchor]") {
    SECTION("C5, 44.1 kHz: slow path, every odd n follows T(n f)/n^2") {
        constexpr double kFs = 44100.0;
        const float f0 = PT::anchorMidiHz(72);   // C5
        const double f = static_cast<double>(f0);
        const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(kFs));
        const auto nMax = static_cast<int>(std::floor((0.5 * kFs - 1.0) / f));
        const std::vector<float> x = renderWave(AW::Triangle, f0, kFs, kSettle + kFrame);
        const std::vector<double> pw = PT::renderedHarmonicPowers(x.data() + kSettle, kFs, f, nMax);
        const std::vector<double> d = relDb(pw);
        int checkedTapered = 0;
        for (int n = 2; n <= nMax; ++n) {
            const double nf = static_cast<double>(n) * f;
            const double got = d[static_cast<std::size_t>(n - 1)];
            if (nf >= capHz) {
                INFO("n " << n << " at " << nf << " Hz (>= cap " << capHz << "): " << got << " dB");
                CHECK(got <= kHarmonicFloorDb);
                continue;
            }
            if (n % 2 == 0 || n > 31)
                continue;
            const double t = recipeTaper(n, f, kFs);
            if (t <= 0.0)
                continue;
            const double expected = 20.0 * std::log10(t / (static_cast<double>(n) * static_cast<double>(n)));
            std::printf("FR-012 C5 44.1k: n %2d at %8.1f Hz T %.6f expected %8.3f dB got %8.3f dB\n", n, nf, t,
                        expected, got);
            if (expected <= kHarmonicFloorDb)
                continue;
            INFO("n " << n << " expected " << expected << " got " << got);
            CHECK(std::abs(got - expected) <= 0.5);
            if (t < 1.0)
                ++checkedTapered;
        }
        REQUIRE(checkedTapered >= 1);   // the fixture really is in the taper band
    }

    SECTION("E1, 44.1 kHz: fast path, the 16 terms untapered") {
        constexpr double kFs = 44100.0;
        const float f0 = PT::anchorMidiHz(28);
        const std::vector<float> x = renderWave(AW::Triangle, f0, kFs, kSettle + kFrame);
        const std::vector<double> pw =
            PT::renderedHarmonicPowers(x.data() + kSettle, kFs, static_cast<double>(f0), kThdHarmonics);
        const std::vector<double> d = relDb(pw);
        for (int n = 3; n <= 15; n += 2) {
            const double expected = -40.0 * std::log10(static_cast<double>(n));
            INFO("E1 n " << n << ": " << d[static_cast<std::size_t>(n - 1)] << " expected " << expected);
            CHECK(std::abs(d[static_cast<std::size_t>(n - 1)] - expected) <= 0.5);
        }
    }
}

// =============================================================================
// SC-002 waveform arms: RMS calibration relative to core h1 (FR-011, FR-023, FR-024)
// =============================================================================

TEST_CASE("AnchorOscillator_RmsCalibration", "[systems][profundum][anchor]") {
    const float a1Max = AnchorOscillator::kMaxCoreH1Amplitude;
    const float a1Quarter = 0.25f * a1Max;
    for (const double fs : kRates) {
        for (const int midi : {28, 48}) {   // E1, C3
            const float f0 = PT::anchorMidiHz(midi);
            const auto n = static_cast<std::size_t>(std::llround(2.2 * fs));
            const auto from = static_cast<std::size_t>(std::llround(1.0 * fs));
            std::array<double, kWaves.size()> rmsMax{};
            std::array<double, kWaves.size()> rmsQuarter{};
            for (std::size_t wi = 0; wi < kWaves.size(); ++wi) {
                const AW w = kWaves[wi];
                const double fa = waveFa(w, static_cast<double>(f0));
                for (int ai = 0; ai < 2; ++ai) {
                    const float a1 = ai == 0 ? a1Max : a1Quarter;
                    const std::vector<float> x = renderWave(w, f0, fs, n, a1);
                    const double rms = PT::rmsDbOverPeriods(x.data() + from, n - from, fs, fa, 1.0);
                    const double expected = 20.0 * std::log10(static_cast<double>(AnchorOscillator::kAnchorToH1)
                                                               * static_cast<double>(a1) / std::sqrt(2.0));
                    std::printf("SC-002 %-11s midi %d fs %.0f a1 %.6f: RMS %.4f dB expected %.4f dB\n", waveName(w),
                                midi, fs, static_cast<double>(a1), rms, expected);
                    INFO(waveName(w) << " midi " << midi << " fs " << fs << " a1 " << a1);
                    CHECK(std::abs(rms - expected) <= 0.1);
                    (ai == 0 ? rmsMax : rmsQuarter)[wi] = rms;
                }
                const double ratio = rmsMax[wi] - rmsQuarter[wi];
                INFO(waveName(w) << " a1 ratio " << ratio << " dB");
                CHECK(std::abs(ratio - 20.0 * std::log10(4.0)) <= 0.05);
            }
            for (const auto* set : {&rmsMax, &rmsQuarter}) {
                const auto [lo, hi] = std::minmax_element(set->begin(), set->end());
                std::printf("SC-002 midi %d fs %.0f: waveform spread %.4f dB (bar 0.1)\n", midi, fs, *hi - *lo);
                CHECK(*hi - *lo <= 0.1);
            }
        }
    }

    // ---- T012 zero arm (FR-021, plan S4.9): a1 -> 0 and level 1 -> 0 reach an exact 0.0f output ----
    // The smoother snaps once |current − target| < 1e-4 (smoother.h:55, :197-211) with a time constant
    // of kLevelSmoothMs/5 ms, so a step of size Δ is exact zero after ln(Δ/1e-4) time constants:
    // ≈ 8.18 ms for a1 = kMaxCoreH1Amplitude, ≈ 9.21 ms for level 1.
    const double tauMs = static_cast<double>(AnchorOscillator::kLevelSmoothMs) / 5.0;
    for (const double fs : kRates) {
        for (const int midi : {28, 48}) {   // E1, C3
            const float f0 = PT::anchorMidiHz(midi);
            for (const AW w : kWaves) {
                for (const bool a1Arm : {true, false}) {
                    AnchorOscillator a;
                    a.prepare(fs);
                    AnchorOscillator::Controls c{};
                    c.waveform = w;
                    c.mode = AM::Lock;
                    c.level = 1.0f;
                    a.setControls(c);
                    a.setCoreH1Amplitude(a1Max);
                    a.setRetriggerPhase(ARP::Reset);
                    a.noteOn(f0);
                    (void)PT::renderAnchor(a, static_cast<std::size_t>(std::llround(0.5 * fs)), 512);
                    if (a1Arm) {
                        a.setCoreH1Amplitude(0.0f);
                    } else {
                        c.level = 0.0f;
                        a.setControls(c);   // latched at the next block's offset 0, i.e. the next sample
                    }
                    const auto tail = static_cast<std::size_t>(std::llround(0.05 * fs));
                    const PT::AnchorRender r = PT::renderAnchor(a, tail, 512);
                    REQUIRE(a.stateFinite());
                    std::size_t firstZero = 0;   // first sample from which every sample is exactly 0.0f
                    for (std::size_t i = 0; i < tail; ++i) {
                        if (r.L[i] != 0.0f)
                            firstZero = i + 1;
                    }
                    const auto from12ms = static_cast<std::size_t>(std::llround(0.012 * fs));
                    bool zeroFrom12ms = true;
                    for (std::size_t i = from12ms; i < tail; ++i)
                        zeroFrom12ms = zeroFrom12ms && r.L[i] == 0.0f;
                    const double start = a1Arm ? static_cast<double>(a1Max) : 1.0;
                    const double derivedMs = std::log(start / 1e-4) * tauMs;
                    const double boundSamples = derivedMs * fs / 1000.0 + 1.0;
                    std::printf("SC-002 zero arm %-11s midi %d fs %.0f %s: first exact-zero sample %zu (%.3f ms), "
                                "bound %.2f samples (derived %.3f ms + 1 sample)\n",
                                waveName(w), midi, fs, a1Arm ? "a1 -> 0" : "level 1 -> 0", firstZero,
                                static_cast<double>(firstZero) * 1000.0 / fs, boundSamples, derivedMs);
                    INFO(waveName(w) << " midi " << midi << " fs " << fs << (a1Arm ? " a1 arm" : " level arm"));
                    CHECK(firstZero > 0);   // it was sounding when the change arrived
                    CHECK(static_cast<double>(firstZero) <= boundSamples);
                    CHECK(zeroFrom12ms);
                }
            }
        }
    }

    // Finite out-of-range a1 clamps: setCoreH1Amplitude(1.0f) renders bitwise equal to kMaxCoreH1Amplitude.
    for (const AW w : kWaves) {
        const float f0 = PT::anchorMidiHz(48);
        const std::vector<float> over = renderWave(w, f0, 48000.0, 4800, 1.0f);
        const std::vector<float> atMax = renderWave(w, f0, 48000.0, 4800, a1Max);
        INFO(waveName(w));
        CHECK(std::memcmp(over.data(), atMax.data(), over.size() * sizeof(float)) == 0);
    }
}

// =============================================================================
// SC-010 LOCK arms: aliasing at E0-E2 with the PUNCH +12 dB peak added analytically (FR-061)
// =============================================================================

TEST_CASE("AnchorOscillator_NoAliasing", "[systems][profundum][anchor]") {
    constexpr double kPunchPeakDb = 12.0;   // plan "SC-010 note"
    for (const double fs : kRates) {
        for (const int midi : {16, 28, 40}) {   // E0, E1, E2
            const float f0 = PT::anchorMidiHz(midi);
            for (const AW w : kWaves) {
                const double fa = waveFa(w, static_cast<double>(f0));
                const std::size_t fftLen = PT::aliasFftLength(fs, fa);
                const std::vector<float> x = renderWave(w, f0, fs, 2 * fftLen);
                const PT::AliasResult ap = PT::aliasedPower(x.data() + fftLen, fftLen, fs, fa);
                const double v = ap.aliasedDbfs + kPunchPeakDb;
                std::printf("SC-010 %-11s midi %d fs %.0f: aliased %.2f dBFS (+12 -> %.2f, bar -80)\n", waveName(w),
                            midi, fs, ap.aliasedDbfs, v);
                INFO(waveName(w) << " midi " << midi << " fs " << fs);
                CHECK(v <= -80.0);
            }
        }
    }

    // Recorded, not gated: every waveform per octave E2 -> C5 at 48 kHz.
    constexpr double kFs = 48000.0;
    for (const int midi : {40, 52, 64, 72}) {
        const float f0 = PT::anchorMidiHz(midi);
        for (const AW w : kWaves) {
            const double fa = waveFa(w, static_cast<double>(f0));
            const std::size_t fftLen = PT::aliasFftLength(kFs, fa);
            const std::vector<float> x = renderWave(w, f0, kFs, 2 * fftLen);
            const PT::AliasResult ap = PT::aliasedPower(x.data() + fftLen, fftLen, kFs, fa);
            std::printf("SC-010 recorded %-11s midi %d 48k: aliased %.2f dBFS (+12 -> %.2f)\n", waveName(w), midi,
                        ap.aliasedDbfs, ap.aliasedDbfs + kPunchPeakDb);
        }
    }

    // ---- T014 FOLLOW arm (plan S4.4: the taper is keyed on f_T = r_eff·f, capped): r = 4 at 44.1 kHz ----
    // aliasedPower excludes the harmonic bins, so the second check (every wn at or above the cap and
    // below fs/2 at <= -90 dB re w1) is what sees a taper still keyed on f0 below Nyquist.
    {
        constexpr double kFsF = 44100.0;
        constexpr float kR = 4.0f;
        const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(kFsF));
        const std::size_t n = kSettle + kFrame;
        const auto checkFrame = [&](const std::vector<float>& x, double fa, const std::string& tag) {
            REQUIRE(x.size() == n);
            const std::size_t fftLen = PT::aliasFftLength(kFsF, fa);
            REQUIRE(fftLen <= kFrame);
            const PT::AliasResult ap = PT::aliasedPower(x.data() + kSettle, fftLen, kFsF, fa);
            const double v = ap.aliasedDbfs + kPunchPeakDb;
            const auto nMax = static_cast<int>(std::floor((0.5 * kFsF - 1.0) / fa));
            REQUIRE(nMax >= 2);
            const std::vector<double> d = relDb(PT::renderedHarmonicPowers(x.data() + kSettle, kFsF, fa, nMax));
            double worstAboveCap = -400.0;
            int aboveCap = 0;
            for (int h = 2; h <= nMax; ++h) {
                const double hf = static_cast<double>(h) * fa;
                if (hf < capHz || hf >= 0.5 * kFsF)
                    continue;
                ++aboveCap;
                const double got = d[static_cast<std::size_t>(h - 1)];
                worstAboveCap = std::max(worstAboveCap, got);
                INFO(tag << " n " << h << " at " << hf << " Hz (cap " << capHz << "): " << got << " dB re w1");
                CHECK(got <= kHarmonicFloorDb);
            }
            std::printf("SC-010 FOLLOW %s: f_a %.2f Hz, aliased %.2f dBFS (+12 -> %.2f, bar -80); %d harmonics in "
                        "[cap, fs/2), worst %.2f dB re w1 (bar -90)\n",
                        tag.c_str(), fa, ap.aliasedDbfs, v, aboveCap, worstAboveCap);
            INFO(tag);
            CHECK(v <= -80.0);
        };

        for (const AW w : {AW::Triangle, AW::RoundedSine, AW::AsymmetricSine}) {
            for (const int midi : {40, 48, 60}) {   // E2, C3, C4
                const float f0 = PT::anchorMidiHz(midi);
                const double fa = static_cast<double>(kR) * static_cast<double>(f0);
                checkFrame(renderFollowT014(w, f0, kFsF, n, kR), fa,
                           std::string(waveName(w)) + " r 4 midi " + std::to_string(midi));
            }

            // An r step 1 -> 4 at constant f0 (C3), once via setFollowRatio and once via
            // followRatioPerSample; the frame (from kSettle) is long after the step's glide.
            const float f0 = PT::anchorMidiHz(48);
            const double fa = static_cast<double>(kR) * static_cast<double>(f0);
            constexpr std::size_t kStepAt = 4800;
            {
                AnchorOscillator a;
                a.prepare(kFsF);
                AnchorOscillator::Controls c{};
                c.waveform = w;
                c.mode = AM::Follow;
                c.level = 1.0f;
                a.setControls(c);
                a.setRetriggerPhase(ARP::Reset);
                a.noteOn(f0);
                PT::AnchorRender r = PT::renderAnchor(a, kStepAt, 512);
                a.setFollowRatio(kR);
                const PT::AnchorRender r2 = PT::renderAnchor(a, n - kStepAt, 512);
                r.L.insert(r.L.end(), r2.L.begin(), r2.L.end());
                REQUIRE(a.stateFinite());
                checkFrame(r.L, fa, std::string(waveName(w)) + " r step 1 -> 4 (setFollowRatio) C3");
            }
            std::vector<float> rTraj(n, 1.0f);
            std::fill(rTraj.begin() + static_cast<std::ptrdiff_t>(kStepAt), rTraj.end(), kR);
            checkFrame(renderFollowT014(w, f0, kFsF, n, 1.0f, nullptr, rTraj.data()), fa,
                       std::string(waveName(w)) + " r step 1 -> 4 (followRatioPerSample) C3");
        }
    }
}

TEST_CASE("AnchorOscillator_AliasGatePositiveControl", "[systems][profundum][anchor]") {
    // A naive piecewise-linear triangle 4·|φ − 0.5| − 1 in sine phase (0 at φ = 0, +1 at 0.25) at
    // the Anchor's level-1 gain for a unit-RMS triangle (peak √3), C4 / 44.1 kHz. Model −74.6 /
    // −62.6 dBFS (plan S9.7, which scales by a1 alone; the √3/√2 peak here reads 1.76 dB higher).
    constexpr double kFs = 44100.0;
    const double f0 = static_cast<double>(PT::anchorMidiHz(60));
    const double peak = static_cast<double>(AnchorOscillator::kAnchorToH1)
                        * static_cast<double>(AnchorOscillator::kMaxCoreH1Amplitude) / std::sqrt(2.0) * std::sqrt(3.0);
    const std::size_t fftLen = PT::aliasFftLength(kFs, f0);
    std::vector<float> x(fftLen, 0.0f);
    for (std::size_t i = 0; i < fftLen; ++i) {
        double ph = f0 * static_cast<double>(i + fftLen) / kFs + 0.75;
        ph -= std::floor(ph);
        x[i] = static_cast<float>(peak * (4.0 * std::abs(ph - 0.5) - 1.0));
    }
    const PT::AliasResult ap = PT::aliasedPower(x.data(), fftLen, kFs, f0);
    std::printf("SC-010 positive control: naive triangle C4 44.1k aliased %.2f dBFS (+12 -> %.2f), bar -80\n",
                ap.aliasedDbfs, ap.aliasedDbfs + 12.0);
    REQUIRE(ap.aliasedDbfs > -80.0);
    REQUIRE(ap.aliasedDbfs + 12.0 > -80.0);
}

// =============================================================================
// SC-011: no DC (FR-013)
// =============================================================================

TEST_CASE("AnchorOscillator_NoDc", "[systems][profundum][anchor]") {
    constexpr double kFs = 48000.0;
    for (const int midi : {16, 28, 48}) {   // E0, E1, C3
        const float f0 = PT::anchorMidiHz(midi);
        for (const AW w : kWaves) {
            const double fa = waveFa(w, static_cast<double>(f0));
            const auto n = static_cast<std::size_t>(std::llround(2.3 * kFs));
            const auto from = static_cast<std::size_t>(std::llround(1.0 * kFs));
            const std::vector<float> x = renderWave(w, f0, kFs, n);
            const double period = kFs / fa;
            const double periods = std::floor(static_cast<double>(n - from) / period);
            const auto len = static_cast<std::size_t>(std::llround(periods * period));
            REQUIRE(len >= static_cast<std::size_t>(kFs));
            REQUIRE(from + len <= n);
            double sum = 0.0;
            for (std::size_t i = from; i < from + len; ++i)
                sum += static_cast<double>(x[i]);
            const double mean = sum / static_cast<double>(len);
            const double dcDb = 20.0 * std::log10(std::max(std::abs(mean), 1e-30));
            std::printf("SC-011 %-11s midi %d: DC %.2f dBFS over %.0f periods (bar -80)\n", waveName(w), midi, dcDb,
                        periods);
            INFO(waveName(w) << " midi " << midi);
            CHECK(dcDb <= -80.0);
        }
    }
}

// =============================================================================
// SC-008(a): FOLLOW input-contract accuracy (T014; plan S4.10; FR-045, FR-049)
// =============================================================================

TEST_CASE("AnchorOscillator_FollowRatioAccuracy", "[systems][profundum][anchor]") {
    constexpr double kFs = 48000.0;

    SECTION("static r: the Goertzel phase slope over two 1 s windows 1 s apart reads r*f0 within 1 cent") {
        const auto L = static_cast<std::size_t>(kFs);   // 1 s windows
        const std::size_t startA = L / 2;
        const std::size_t startB = startA + L;           // 1 s apart
        const double dt = static_cast<double>(startB - startA) / kFs;
        const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(kFs));
        double worst = 0.0;
        for (const float r : {0.5f, 0.75f, 1.5f, 2.0f}) {
            for (const int midi : {28, 36, 48}) {   // E1, C2, C3
                const float f0 = PT::anchorMidiHz(midi);
                const double fa = static_cast<double>(r) * static_cast<double>(f0);
                REQUIRE(fa < capHz);
                const std::vector<float> x = renderFollowT014(AW::Sine, f0, kFs, startB + L, r);
                // f = f_a + Δθ/(2π·Δt), Δθ the phase advance beyond f_a's, wrapped to (−π, π].
                const double thA = PT::goertzelPhase(x.data() + startA, L, kFs, fa);
                const double thB = PT::goertzelPhase(x.data() + startB, L, kFs, fa);
                const double dTheta = std::remainder(thB - thA - PT::kTwoPi * fa * dt, PT::kTwoPi);
                const double f = fa + dTheta / (PT::kTwoPi * dt);
                const double c = centsT014(f, fa);
                // Coarse cross-check that resolves the phase slope's 1/Δt ambiguity: positive-going
                // zero crossings over both windows (2 s), within a semitone of f_a.
                std::size_t crossings = 0;
                for (std::size_t i = startA + 1; i < startB + L; ++i) {
                    if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                        ++crossings;
                }
                const double fZc = static_cast<double>(crossings) / (2.0 * dt);
                std::printf("SC-008(a) r %.2f midi %d: f %.6f Hz, r*f0 %.6f Hz, %+.5f cents (bar 1); zero-crossing "
                            "estimate %.2f Hz\n",
                            static_cast<double>(r), midi, f, fa, c, fZc);
                INFO("r " << r << " midi " << midi);
                CHECK(std::abs(c) <= 1.0);
                CHECK(std::abs(centsT014(fZc, fa)) <= 100.0);
                worst = std::max(worst, std::abs(c));
            }
        }
        std::printf("SC-008(a) static r: worst %.5f cents (bar 1)\n", worst);
    }

    SECTION("+-12-semitone 200 ms f0 glide at r = 1.5: within 5 cents of 1.5*f0(grid t) from 50 ms after the end") {
        constexpr float kR = 1.5f;
        constexpr std::size_t kFrom = 4800;
        const auto len = static_cast<std::size_t>(std::llround(0.2 * kFs));
        const auto settle = static_cast<std::size_t>(std::llround(0.05 * kFs));
        const std::size_t checkFrom = kFrom + len + settle;
        const std::size_t total = checkFrom + 4800;
        double worstAll = 0.0;
        for (const int midi : {28, 36, 48}) {
            const float f0 = PT::anchorMidiHz(midi);
            for (const double semis : {12.0, -12.0}) {
                std::vector<float> traj(total, f0);   // linear in semitones over 200 ms, then held
                for (std::size_t i = kFrom; i < total; ++i) {
                    const double x = std::min(1.0, static_cast<double>(i - kFrom) / static_cast<double>(len));
                    traj[i] = static_cast<float>(static_cast<double>(f0) * std::exp2(semis * x / 12.0));
                }
                AnchorOscillator a;
                a.prepare(kFs);
                AnchorOscillator::Controls c{};
                c.mode = AM::Follow;
                c.level = 1.0f;
                a.setControls(c);
                a.setFollowRatio(kR);
                a.setRetriggerPhase(ARP::Reset);
                a.noteOn(f0);
                const PT::AnchorRender r = PT::renderAnchor(a, total, 1, traj.data(), nullptr, true);
                REQUIRE(a.stateFinite());
                double worst = 0.0;
                for (std::size_t n = checkFrom; n < total; ++n) {
                    const std::size_t grid = n - n % AnchorOscillator::kPitchUpdateInterval;
                    const double expected = static_cast<double>(kR) * static_cast<double>(traj[grid]);
                    worst = std::max(worst, std::abs(centsT014(PT::anchorInstFreqHz(r.phase, n, kFs), expected)));
                }
                std::printf("SC-008(a) glide midi %d %+.0f st, r 1.5: worst %.5f cents from 50 ms after the end "
                            "(bar 5)\n",
                            midi, semis, worst);
                INFO("midi " << midi << " glide " << semis);
                CHECK(worst <= 5.0);
                worstAll = std::max(worstAll, worst);
            }
        }
        std::printf("SC-008(a) glide: worst %.5f cents (bar 5)\n", worstAll);
    }

    SECTION("ratio clamp: 8 renders as 4 and 0.1 as 0.25 (C2), bitwise") {
        const float f0 = PT::anchorMidiHz(36);
        constexpr std::size_t kN = 9600;
        const std::vector<float> at4 = renderFollowT014(AW::Triangle, f0, kFs, kN, 4.0f);
        const std::vector<float> at8 = renderFollowT014(AW::Triangle, f0, kFs, kN, 8.0f);
        const std::vector<float> atQuarter = renderFollowT014(AW::Triangle, f0, kFs, kN, 0.25f);
        const std::vector<float> atTenth = renderFollowT014(AW::Triangle, f0, kFs, kN, 0.1f);
        const std::vector<float> atOne = renderFollowT014(AW::Triangle, f0, kFs, kN, 1.0f);
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        CHECK(std::memcmp(at8.data(), at4.data(), kN * sizeof(float)) == 0);
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        CHECK(std::memcmp(atTenth.data(), atQuarter.data(), kN * sizeof(float)) == 0);
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        CHECK_FALSE(std::memcmp(at4.data(), atOne.data(), kN * sizeof(float)) == 0);        // r is audible
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        CHECK_FALSE(std::memcmp(atQuarter.data(), atOne.data(), kN * sizeof(float)) == 0);
    }
}

// =============================================================================
// T019 SC-012: the audibility gate (FR-060; plan S1 C-9, C-14)
// =============================================================================

namespace {

constexpr double kFsT019 = 48000.0;
constexpr int kSepHarmonicsT019 = 15;   // SC-012(a): n <= 15

/// N = max(1, round(ms·fs/1000)) (plan S4.7).
std::size_t msSamplesT019(double ms, double fs) {
    return static_cast<std::size_t>(std::max<long long>(1, std::llround(ms * fs / 1000.0)));
}

/// A fresh Anchor at fs with the given Controls, a1 = kMaxCoreH1Amplitude, Reset noteOn f0; n samples in
/// 512 blocks.
std::vector<float> renderControlsT019(const AnchorOscillator::Controls& c, float f0, double fs, std::size_t n,
                                      float a1 = AnchorOscillator::kMaxCoreH1Amplitude) {
    AnchorOscillator a;
    a.prepare(fs);
    a.setControls(c);
    a.setCoreH1Amplitude(a1);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(f0);
    PT::AnchorRender r = PT::renderAnchor(a, n, 512);
    REQUIRE(a.stateFinite());
    return std::move(r.L);
}

/// Σ x² over [from, to) in double.
double energyT019(const std::vector<float>& x, std::size_t from, std::size_t to) {
    return PT::anchorWindowEnergy(std::span<const float>(x), from, to);
}

/// The fundamental near fa (Hz), the Goertzel phase slope between two 1 s windows 1 s apart (the
/// SC-008(a) method of AnchorOscillator_FollowRatioAccuracy). The phase slope is ambiguous by 1/Δt =
/// 1 Hz, so a positive-going zero-crossing count over both windows must land within a semitone of fa.
double fundamentalHzT019(const std::vector<float>& x, double fs, double fa) {
    const auto L = static_cast<std::size_t>(fs);
    const std::size_t startA = L / 2;
    const std::size_t startB = startA + L;
    REQUIRE(x.size() >= startB + L);
    const double dt = static_cast<double>(startB - startA) / fs;
    const double thA = PT::goertzelPhase(x.data() + startA, L, fs, fa);
    const double thB = PT::goertzelPhase(x.data() + startB, L, fs, fa);
    const double dTheta = std::remainder(thB - thA - PT::kTwoPi * fa * dt, PT::kTwoPi);
    const double f = fa + dTheta / (PT::kTwoPi * dt);
    std::size_t crossings = 0;
    for (std::size_t i = startA + 1; i < startB + L; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            ++crossings;
    }
    const double fZc = static_cast<double>(crossings) / (2.0 * dt);
    INFO("zero-crossing estimate " << fZc << " Hz, f_a " << fa << " Hz");
    REQUIRE(std::abs(centsT014(fZc, fa)) <= 100.0);
    return f;
}

/// The D-4 / D-5 closed-form mode gain at sample n of a snap arm (plan S4.7): LOCK / FOLLOW 1; PUNCH
/// Cosine(1 -> B, nRise), Cosine(B -> 1, nTime), Hold(1); GHOST Cosine(0 -> 1, nGhost), Hold(1).
double modeGainT019(AM m, std::size_t n, double b, std::size_t nRise, std::size_t nTime, std::size_t nGhost) {
    switch (m) {
        case AM::Lock:
        case AM::Follow:
            return 1.0;
        case AM::Punch:
            if (n < nRise)
                return PT::anchorRaisedCosine(1.0, b - 1.0, n, nRise);
            if (n < nRise + nTime)
                return PT::anchorRaisedCosine(b, 1.0 - b, n - nRise, nTime);
            return 1.0;
        case AM::Ghost:
            return n < nGhost ? PT::anchorRaisedCosine(0.0, 1.0, n, nGhost) : 1.0;
    }
    return 1.0;
}

/// SC-012(g) analytic ratio in dB, in double: Σ g_num²·y² / Σ g_den²·y² over [0, len) with y = sin(2π f0 n/fs)
/// (a Reset onset starts at phase 0) and the D-4 defaults.
double analyticModeRatioDbT019(AM num, AM den, double f0, double fs, std::size_t len) {
    const AnchorOscillator::Controls d{};
    const double b = std::pow(10.0, static_cast<double>(d.punchBoostDb) / 20.0);
    const std::size_t nRise = msSamplesT019(static_cast<double>(AnchorOscillator::kPunchRiseMs), fs);
    const std::size_t nTime = msSamplesT019(static_cast<double>(d.punchTimeMs), fs);
    const std::size_t nGhost = msSamplesT019(static_cast<double>(d.ghostTimeMs), fs);
    double eNum = 0.0;
    double eDen = 0.0;
    for (std::size_t n = 0; n < len; ++n) {
        const double y = std::sin(PT::kTwoPi * f0 * static_cast<double>(n) / fs);
        const double gn = modeGainT019(num, n, b, nRise, nTime, nGhost);
        const double gd = modeGainT019(den, n, b, nRise, nTime, nGhost);
        eNum += gn * gn * y * y;
        eDen += gd * gd * y * y;
    }
    return PT::safeDb10(eNum / eDen);
}

/// Log-spaced values lo·(hi/lo)^(i/(count−1)), i = 0..count−1, as float.
std::vector<float> logSpacedT019(double lo, double hi, std::size_t count) {
    std::vector<float> v(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i)
        v[i] = static_cast<float>(lo * std::pow(hi / lo, static_cast<double>(i) / static_cast<double>(count - 1)));
    return v;
}

/// Strictly increasing (sign +1) or strictly decreasing (sign −1) over the whole series.
bool strictlyMonotonicT019(const std::vector<double>& e, double sign) {
    for (std::size_t i = 1; i < e.size(); ++i) {
        if (!(sign * (e[i] - e[i - 1]) > 0.0))
            return false;
    }
    return true;
}

}  // namespace

TEST_CASE("AnchorOscillator_AudibilityGate", "[systems][profundum][anchor]") {
    constexpr double kFs = kFsT019;

    for (const int midi : {36, 28}) {   // C2, E1
        const float f0 = PT::anchorMidiHz(midi);
        const double hz = static_cast<double>(f0);
        const char* note = midi == 36 ? "C2" : "E1";
        INFO("note " << note);

        // ---- (a) waveform separation (plan S1 C-14) ----
        {
            AnchorOscillator::Controls c{};
            c.mode = AM::Lock;
            c.level = 1.0f;
            std::array<std::vector<double>, kWaves.size()> pw{};
            std::array<std::vector<float>, kWaves.size()> renders{};
            for (std::size_t i = 0; i < kWaves.size(); ++i) {
                c.waveform = kWaves[i];
                renders[i] = renderControlsT019(c, f0, kFs, kSettle + kFrame);
                pw[i] = PT::renderedHarmonicPowers(renders[i].data() + kSettle, kFs, waveFa(kWaves[i], hz),
                                                   kSepHarmonicsT019);
                REQUIRE(pw[i][0] > 0.0);
            }
            const auto idx = [](AW w) { return static_cast<std::size_t>(w); };
            int separatedPairs = 0;
            for (std::size_t i = 0; i < kWaves.size(); ++i) {
                for (std::size_t j = i + 1; j < kWaves.size(); ++j) {
                    const bool octavePair = (kWaves[i] == AW::Sine && kWaves[j] == AW::OctaveDownSine)
                                            || (kWaves[i] == AW::OctaveDownSine && kWaves[j] == AW::Sine);
                    if (octavePair)
                        continue;   // the octave clause below
                    const bool sep = PT::anchorWaveformsSeparated(pw[i], pw[j]);
                    std::printf("SC-012(a) %s %s vs %s: %s\n", note, waveName(kWaves[i]), waveName(kWaves[j]),
                                sep ? "separated" : "NOT separated");
                    INFO(waveName(kWaves[i]) << " vs " << waveName(kWaves[j]));
                    CHECK(sep);
                    if (sep)
                        ++separatedPairs;
                }
            }
            CHECK(separatedPairs == 9);

            // Octave clause: octave-down's w1 at f0/2 within 1 cent, its power at f0 <= -90 dB re it; the
            // sine's fundamental at f0 within 1 cent.
            const std::vector<float>& oct = renders[idx(AW::OctaveDownSine)];
            const double fOct = fundamentalHzT019(oct, kFs, 0.5 * hz);
            const double fSine = fundamentalHzT019(renders[idx(AW::Sine)], kFs, hz);
            const std::vector<double>& pOct = pw[idx(AW::OctaveDownSine)];
            const double atF0Db = PT::safeDb10(pOct[1] / pOct[0]);   // n = 2 of f0/2 is f0
            std::printf("SC-012(a) %s sine vs octave-down: octave-down fundamental %.6f Hz (%+.5f cents re f0/2, bar 1), "
                        "sine %.6f Hz (%+.5f cents re f0), power at f0 %.2f dB re its w1 (bar -90)\n",
                        note, fOct, centsT014(fOct, 0.5 * hz), fSine, centsT014(fSine, hz), atF0Db);
            CHECK(std::abs(centsT014(fOct, 0.5 * hz)) <= 1.0);
            CHECK(std::abs(centsT014(fSine, hz)) <= 1.0);
            CHECK(atF0Db <= -90.0);

            // Printed analytics: rounded vs triangle at h3 (7.04 dB), asymmetric vs each other at h2 (>= 12 dB).
            const auto floored = [](const std::vector<double>& p, std::size_t i) {
                return std::max(PT::safeDb10(p[i] / p[0]), -90.0);
            };
            std::printf("SC-012(a) %s rounded vs triangle at h3: %.3f dB (analytic 7.04)\n", note,
                        floored(pw[idx(AW::RoundedSine)], 2) - floored(pw[idx(AW::Triangle)], 2));
            for (const AW w : kWaves) {
                if (w == AW::AsymmetricSine)
                    continue;
                std::printf("SC-012(a) %s asymmetric vs %s at h2: %.3f dB (analytic >= 12)\n", note, waveName(w),
                            floored(pw[idx(AW::AsymmetricSine)], 1) - floored(pw[idx(w)], 1));
            }

            // Positive control: a second render of the same waveform (a1 / 4) reads NOT separated.
            for (const AW w : {AW::Sine, AW::Triangle}) {
                c.waveform = w;
                const std::vector<float> again =
                    renderControlsT019(c, f0, kFs, kSettle + kFrame, 0.25f * AnchorOscillator::kMaxCoreH1Amplitude);
                const std::vector<double> p2 =
                    PT::renderedHarmonicPowers(again.data() + kSettle, kFs, hz, kSepHarmonicsT019);
                const bool sep = PT::anchorWaveformsSeparated(pw[idx(w)], p2);
                std::printf("SC-012(a) %s positive control %s vs %s (second render): %s\n", note, waveName(w),
                            waveName(w), sep ? "separated" : "not separated");
                INFO("positive control " << waveName(w));
                CHECK_FALSE(sep);
            }
        }

        // ---- (b) level: 33 points linear 0.01 .. 1, E_sub strictly increasing, span >= 40 dB - 1e-4 dB (C-9) ----
        {
            constexpr std::size_t kPoints = 33;
            std::vector<double> eSub(kPoints, 0.0);
            for (std::size_t i = 0; i < kPoints; ++i) {
                AnchorOscillator::Controls c{};
                c.level = static_cast<float>(0.01 + 0.99 * static_cast<double>(i) / static_cast<double>(kPoints - 1));
                const std::vector<float> x = renderControlsT019(c, f0, kFs, kSettle + kFrame);
                eSub[i] = PT::renderedHarmonicPowers(x.data() + kSettle, kFs, hz, 1)[0];
            }
            const double span = PT::safeDb10(eSub.back() / eSub.front());
            const bool mono = strictlyMonotonicT019(eSub, 1.0);
            std::printf("SC-012(b) %s level sweep: E_sub %s, span %.7f dB (bar 40 dB - 1e-4 dB, plan S1 C-9)\n", note,
                        mono ? "strictly increasing" : "NOT strictly increasing", span);
            CHECK(mono);
            CHECK(span >= 40.0 - 1e-4);
        }

        // ---- (c) punch boost 0 .. 12 dB, 13 points: energy over [0, N_rise + N_time) strictly increasing, span >= 6 dB ----
        {
            const AnchorOscillator::Controls d{};
            const std::size_t nRise = msSamplesT019(static_cast<double>(AnchorOscillator::kPunchRiseMs), kFs);
            const std::size_t nTime = msSamplesT019(static_cast<double>(d.punchTimeMs), kFs);
            const std::size_t win = nRise + nTime;
            std::vector<double> e(13, 0.0);
            for (std::size_t i = 0; i < e.size(); ++i) {
                AnchorOscillator::Controls c{};
                c.mode = AM::Punch;
                c.level = 1.0f;
                c.punchBoostDb = static_cast<float>(i);
                e[i] = energyT019(renderControlsT019(c, f0, kFs, win), 0, win);
            }
            const double span = PT::safeDb10(e.back() / e.front());
            const bool mono = strictlyMonotonicT019(e, 1.0);
            std::printf("SC-012(c) %s punch boost: window %zu samples, energy %s, span %.3f dB (bar 6)\n", note, win,
                        mono ? "strictly increasing" : "NOT strictly increasing", span);
            CHECK(mono);
            CHECK(span >= 6.0);
        }

        // ---- (d) punch time 5 .. 250 ms, 9 log-spaced: energy over [N_rise, N_rise + N(250 ms)) strictly
        // increasing, span >= 3 dB ----
        {
            const std::size_t nRise = msSamplesT019(static_cast<double>(AnchorOscillator::kPunchRiseMs), kFs);
            const std::size_t nMax = msSamplesT019(static_cast<double>(AnchorOscillator::kMaxPunchTimeMs), kFs);
            const std::vector<float> times = logSpacedT019(static_cast<double>(AnchorOscillator::kMinPunchTimeMs),
                                                           static_cast<double>(AnchorOscillator::kMaxPunchTimeMs), 9);
            std::vector<double> e(times.size(), 0.0);
            for (std::size_t i = 0; i < times.size(); ++i) {
                AnchorOscillator::Controls c{};
                c.mode = AM::Punch;
                c.level = 1.0f;
                c.punchTimeMs = times[i];
                e[i] = energyT019(renderControlsT019(c, f0, kFs, nRise + nMax), nRise, nRise + nMax);
            }
            const double span = PT::safeDb10(e.back() / e.front());
            const bool mono = strictlyMonotonicT019(e, 1.0);
            std::printf("SC-012(d) %s punch time: window [%zu, %zu), energy %s, span %.3f dB (bar 3)\n", note, nRise,
                        nRise + nMax, mono ? "strictly increasing" : "NOT strictly increasing", span);
            CHECK(mono);
            CHECK(span >= 3.0);
        }

        // ---- (e) ghost time 1 .. 100 ms, 9 log-spaced: first-10 ms energy strictly decreasing, span >= 10 dB ----
        {
            const std::size_t n10 = msSamplesT019(10.0, kFs);
            const std::vector<float> times = logSpacedT019(static_cast<double>(AnchorOscillator::kMinGhostTimeMs),
                                                           static_cast<double>(AnchorOscillator::kMaxGhostTimeMs), 9);
            std::vector<double> e(times.size(), 0.0);
            for (std::size_t i = 0; i < times.size(); ++i) {
                AnchorOscillator::Controls c{};
                c.mode = AM::Ghost;
                c.level = 1.0f;
                c.ghostTimeMs = times[i];
                e[i] = energyT019(renderControlsT019(c, f0, kFs, n10), 0, n10);
            }
            const double span = PT::safeDb10(e.front() / e.back());
            const bool mono = strictlyMonotonicT019(e, -1.0);
            std::printf("SC-012(e) %s ghost time: first %zu samples, energy %s, span %.3f dB (bar 10)\n", note, n10,
                        mono ? "strictly decreasing" : "NOT strictly decreasing", span);
            CHECK(mono);
            CHECK(span >= 10.0);
        }

        // ---- (f) FOLLOW r in {0.5, 2}: the fundamental at r·f0 within 1 cent ----
        {
            const auto n = static_cast<std::size_t>(std::llround(2.5 * kFs));
            for (const float r : {0.5f, 2.0f}) {
                const double fa = static_cast<double>(r) * hz;
                const std::vector<float> x = renderFollowT014(AW::Sine, f0, kFs, n, r);
                const double f = fundamentalHzT019(x, kFs, fa);
                const double c = centsT014(f, fa);
                std::printf("SC-012(f) %s FOLLOW r %.1f: fundamental %.6f Hz, r*f0 %.6f Hz, %+.5f cents (bar 1)\n", note,
                            static_cast<double>(r), f, fa, c);
                INFO("r " << r);
                CHECK(std::abs(c) <= 1.0);
            }
        }
    }

    // ---- (g) mode pairs at the D-4 defaults, same calls, each over its own envelope's window; the analytic
    // values recomputed in double and printed beside the spec's (E1 / C2 / C3) ----
    {
        struct SpecRow {
            int midi;
            double ghostLock, punchLock, punchGhost;
        };
        constexpr std::array<SpecRow, 3> kSpec{SpecRow{28, -6.32, 3.90, 5.17}, SpecRow{36, -4.11, 3.87, 5.02},
                                               SpecRow{48, -4.13, 3.76, 4.87}};
        const AnchorOscillator::Controls d{};
        const std::size_t nGhostWin = msSamplesT019(static_cast<double>(d.ghostTimeMs), kFs);
        const std::size_t nPunchWin = msSamplesT019(static_cast<double>(AnchorOscillator::kPunchRiseMs), kFs)
                                      + msSamplesT019(static_cast<double>(d.punchTimeMs), kFs);
        for (const SpecRow& row : kSpec) {
            const float f0 = PT::anchorMidiHz(row.midi);
            const double hz = static_cast<double>(f0);
            const auto render = [&](AM m) {
                AnchorOscillator::Controls c{};
                c.mode = m;
                return renderControlsT019(c, f0, kFs, nPunchWin);
            };
            REQUIRE(nGhostWin <= nPunchWin);
            const std::vector<float> lock = render(AM::Lock);
            const std::vector<float> ghost = render(AM::Ghost);
            const std::vector<float> punch = render(AM::Punch);
            const double ghostLock = PT::safeDb10(energyT019(ghost, 0, nGhostWin) / energyT019(lock, 0, nGhostWin));
            const double punchLock = PT::safeDb10(energyT019(punch, 0, nPunchWin) / energyT019(lock, 0, nPunchWin));
            const double punchGhost = PT::safeDb10(energyT019(punch, 0, nPunchWin) / energyT019(ghost, 0, nPunchWin));
            const double aGhostLock = analyticModeRatioDbT019(AM::Ghost, AM::Lock, hz, kFs, nGhostWin);
            const double aPunchLock = analyticModeRatioDbT019(AM::Punch, AM::Lock, hz, kFs, nPunchWin);
            const double aPunchGhost = analyticModeRatioDbT019(AM::Punch, AM::Ghost, hz, kFs, nPunchWin);
            std::printf("SC-012(g) midi %d: GHOST/LOCK %.3f dB (analytic %.3f, spec %.2f; bar <= -3), PUNCH/LOCK %.3f dB "
                        "(analytic %.3f, spec %.2f; bar >= 3), PUNCH/GHOST %.3f dB (analytic %.3f, spec %.2f; bar >= 3)\n",
                        row.midi, ghostLock, aGhostLock, row.ghostLock, punchLock, aPunchLock, row.punchLock,
                        punchGhost, aPunchGhost, row.punchGhost);
            INFO("midi " << row.midi);
            CHECK(ghostLock <= -3.0);
            CHECK(punchLock >= 3.0);
            CHECK(punchGhost >= 3.0);
        }
    }
}
