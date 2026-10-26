// ==============================================================================
// Layer 3: System Tests - BassStereoField, MOTION TU
//                          (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// Reference: specs/profundum-phase7-stereo-field/spec.md (SC-008, SC-009(a); FR-015, FR-030 - FR-036)
//            specs/profundum-phase7-stereo-field/plan.md (S5; C-9 octave-folded sync, C-16 pan references)
//            specs/profundum-phase7-stereo-field/tasks.md T011 (deviation 6: every [long] case has a Smoke sibling)
//
// T011:
//   BassStereoField_MotionBounded       [long] SC-008, law level (advanceControl + computePanVector per
//                                       interval, 60 s = 90 000 intervals at 48 kHz): excursion, |p| <= P, Cycle
//                                       non-vacuity, Cycle rate (BH7 peak), Drift tau (configured + fitted 1/e),
//                                       fixed partials, no sign change, inward motion, synced Cycle (C-9 (A)) and
//                                       the bad-tempo keep. CSV evidence/sc008-motion.csv.
//   BassStereoField_MotionBoundedSmoke  10 s, mu 1, Spread 1, Cycle rate 1 and Drift rate 1.
//   BassStereoField_NoZipper            [long] SC-009(a), C-16: held vs interpolated pan references from the
//                                       product's vector sequence; validity, Spread sweep, MOTION, 48 st Drop.
//   BassStereoField_NoZipperSmoke       the validity arm and arm (1)'s 0 -> 1 step.
//
// Recorded decisions (evidence/run-log/T011.txt):
//   - The per-partial series of one cell share one motion value m(t). For Cycle the TU gates, on every moving
//     partial, that p_k(t) == s_k P_k (1 - kappa mu + kappa mu m(t)) within 1e-6 (no slew engaged), so the
//     spectral peak and the 1/e time are read on two representatives (the largest and the smallest moving P_k).
//   - The Cycle inward-motion mean is taken over the largest whole number of Cycle periods inside the render
//     (a part period biases the mean of a 0.22 Hz sine by up to 2.4 % of kappa mu P_k); the plain 60 s mean is
//     printed beside it.
//   - The BH7 spectrum removes the window-weighted mean first (p_k(t) sits on its DC p-bar).
//   - The Drop references take ProfundumTest::pitchHoldAlignedF0 of the per-sample track, so the held reference
//     is phase-aligned with the core for the click count; the residual (held - interpolated) does not depend on it.
// ==============================================================================

#include "bass_stereo_field_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/profundum_core.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::BassStereoField;
using Krate::DSP::kMaxPartials;
using Krate::DSP::NoteModifier;
using Krate::DSP::NoteValue;
using BmoControls = Krate::DSP::BassStereoField::Controls;
using BmoMode = Krate::DSP::BassStereoField::ImageMode;
using BmoSource = Krate::DSP::BassStereoField::MotionSource;
using BmoVec = std::array<float, Krate::DSP::kMaxPartials>;
namespace Sft = Krate::DSP::StereoFieldTest;
namespace Pft = Krate::DSP::ProfundumTest;
namespace Sct = Krate::DSP::ShapeCharacterTest;

constexpr double kBmoFs = 48000.0;
constexpr double kBmoControlRate = kBmoFs / static_cast<double>(BassStereoField::kControlInterval);   // 1500 Hz
constexpr int kBmoPartials = BassStereoField::kDefaultPartials;
constexpr auto kBmoPartialsU = static_cast<std::size_t>(BassStereoField::kDefaultPartials);
static_assert(BassStereoField::kDefaultPartials == Krate::DSP::ProfundumCore::kDefaultPartials,
              "the law and the core share the 64-partial default (C-8)");
constexpr std::size_t kBmoPeakFft = std::size_t{1} << 17;    ///< SC-008: control-rate series zero-padded to 2^17
constexpr std::size_t kBmoAcfFft = std::size_t{1} << 18;     ///< >= 2 x 90 000: linear (not circular) autocorrelation
constexpr double kBmoKappa = static_cast<double>(BassStereoField::kMotionExcursion);   // 0.5
constexpr double kBmoEps = 1e-6;              ///< SC-008 bound slack
constexpr double kBmoMovingFloor = 0.01;      ///< SC-008 non-vacuity applies to P_k > 0.01
constexpr double kBmoTauFitMaxSec = 6.0;      ///< SC-008: the fit is gated where tau <= 6 s
constexpr double kBmoMeanRelTol = 1e-3;       ///< SC-008 inward motion, Cycle: within 1e-3 of P_k
constexpr double kBmoZipperBarDb = -60.0;     ///< SC-009(a)
/// ProfundumTest::sidebandPower reads x[0, 2^17) and totalRef[0, 2^17) (profundum_core_test_helpers.h:326).
constexpr std::size_t kBmoSidebandFrame = std::size_t{1} << 17;

/// A float from its bit pattern through a volatile read, so fast-math cannot fold the NaN away
/// (memory note: NaN/Inf under -ffast-math in tests).
float bmoNaN() {
    volatile std::uint32_t v = 0x7FC00000u;
    return std::bit_cast<float>(static_cast<std::uint32_t>(v));
}

std::uint32_t bmoBits(float x) { return std::bit_cast<std::uint32_t>(x); }

std::string bmoEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase7-stereo-field/evidence/";
}

const char* bmoSourceName(BmoSource s) { return s == BmoSource::Drift ? "drift" : "cycle"; }

// -----------------------------------------------------------------------------
// SC-008: law-level MOTION renders
// -----------------------------------------------------------------------------

struct BmoMotionSpec {
    BmoSource source = BmoSource::Cycle;
    float mu = 1.0f;
    float spread = 1.0f;
    float rate = 1.0f;
    bool sync = false;          ///< SixtyFourth + Triplet (the fastest note value), tempo `bpm`
    float bpm = 120.0f;
    double seconds = 60.0;
    bool badTempoMidway = false;   ///< setTempoBpm(NaN) and setTempoBpm(-1) before the middle interval
};

BmoControls bmoMotionControls(const BmoMotionSpec& s, float mu) {
    BmoControls c{};
    c.mode = BmoMode::Spread;
    c.spread = s.spread;
    c.motionDepth = mu;
    c.motionRate = s.rate;
    c.motionSource = s.source;
    c.cycleSync = s.sync;
    if (s.sync) {
        c.cycleNote = NoteValue::SixtyFourth;
        c.cycleModifier = NoteModifier::Triplet;
    }
    return c;
}

std::unique_ptr<BassStereoField> bmoPreparedField(const BmoControls& c, const BmoMotionSpec& s) {
    auto f = std::make_unique<BassStereoField>();
    if (s.sync)
        f->setTempoBpm(s.bpm);
    f->setControls(c);
    f->prepare(kBmoFs, 512);
    return f;
}

struct BmoMotionRun {
    BmoVec staticVec{};                    ///< the depth-0 vector: P_k = |entry|, s_k = its sign
    std::vector<std::vector<float>> p;     ///< p[k][q], k < 64, one value per control interval
    std::vector<float> m;                  ///< motionValue() per interval
    std::size_t intervals = 0;
    std::size_t tailViolations = 0;        ///< entries k >= 64 that were not +0
    float cycleHz = 0.0f;
    float tauSec = 0.0f;
    float cycleHzBeforeBad = 0.0f;
    float cycleHzAfterBad = 0.0f;
};

/// plan S1 C-7: advanceControl() first, then the interval's query. E1, 64 partials.
BmoMotionRun bmoRunMotion(const BmoMotionSpec& s) {
    const auto f0 = static_cast<float>(Sct::kE1Hz);
    BmoMotionRun r;
    {
        auto f0Field = bmoPreparedField(bmoMotionControls(s, 0.0f), s);
        f0Field->advanceControl();
        BassStereoField::VoiceState vs{};
        f0Field->computePanVector(vs, f0, r.staticVec, kBmoPartials);   // unprimed: lands on the target
    }
    r.intervals = static_cast<std::size_t>(std::llround(s.seconds * kBmoControlRate));
    REQUIRE(r.intervals > 1);
    r.p.assign(kBmoPartialsU, std::vector<float>(r.intervals, 0.0f));
    r.m.assign(r.intervals, 0.0f);

    auto f = bmoPreparedField(bmoMotionControls(s, s.mu), s);
    BassStereoField::VoiceState vs{};
    BmoVec out{};
    const std::size_t badAt = s.badTempoMidway ? r.intervals / 2 : r.intervals;
    for (std::size_t q = 0; q < r.intervals; ++q) {
        if (q == badAt) {
            r.cycleHzBeforeBad = f->cycleFrequencyHz();
            f->setTempoBpm(bmoNaN());
            f->setTempoBpm(-1.0f);
        }
        f->advanceControl();
        if (q == badAt)
            r.cycleHzAfterBad = f->cycleFrequencyHz();
        f->computePanVector(vs, f0, out, kBmoPartials);
        for (std::size_t k = 0; k < kBmoPartialsU; ++k)
            r.p[k][q] = out[k];
        for (std::size_t k = kBmoPartialsU; k < kMaxPartials; ++k) {
            if (bmoBits(out[k]) != 0u)
                ++r.tailViolations;
        }
        r.m[q] = f->motionValue();
    }
    r.cycleHz = f->cycleFrequencyHz();
    r.tauSec = f->driftTauSeconds();
    return r;
}

/// The BH7 spectral peak (bins 1 .. N/2) of x at `rate`, after removing the window-weighted mean, zero-padded
/// to kBmoPeakFft.
double bmoPeakHz(const std::vector<float>& x, double rate) {
    const std::size_t L = x.size();
    REQUIRE(L >= 2);
    REQUIRE(L <= kBmoPeakFft);
    const std::vector<double> w = Pft::bh7Window(L);
    double sw = 0.0;
    double sxw = 0.0;
    for (std::size_t k = 0; k < L; ++k) {
        sw += w[k];
        sxw += w[k] * static_cast<double>(x[k]);
    }
    REQUIRE(sw > 0.0);
    const double mean = sxw / sw;
    std::vector<std::complex<double>> a(kBmoPeakFft, std::complex<double>(0.0, 0.0));
    for (std::size_t k = 0; k < L; ++k)
        a[k] = std::complex<double>((static_cast<double>(x[k]) - mean) * w[k], 0.0);
    Pft::fftDouble(a);
    std::size_t best = 1;
    double bestPow = -1.0;
    for (std::size_t b = 1; b <= kBmoPeakFft / 2; ++b) {
        const double p = std::norm(a[b]);
        if (p > bestPow) {
            bestPow = p;
            best = b;
        }
    }
    return static_cast<double>(best) * rate / static_cast<double>(kBmoPeakFft);
}

/// The 1/e time (s) of the mean-removed autocorrelation of x at `rate` (linear interpolation between lags);
/// -1 when r(lag) stays above 1/e up to half the series.
double bmoOneOverETimeSec(const std::vector<float>& x, double rate) {
    const std::size_t L = x.size();
    REQUIRE(L >= 4);
    REQUIRE(2 * L <= kBmoAcfFft);
    double mean = 0.0;
    for (const float v : x)
        mean += static_cast<double>(v);
    mean /= static_cast<double>(L);
    std::vector<std::complex<double>> a(kBmoAcfFft, std::complex<double>(0.0, 0.0));
    for (std::size_t k = 0; k < L; ++k)
        a[k] = std::complex<double>(static_cast<double>(x[k]) - mean, 0.0);
    Pft::fftDouble(a);
    for (auto& v : a)
        v = std::complex<double>(std::norm(v), 0.0);
    Pft::fftDouble(a);   // the power spectrum is real and even: its forward FFT is N x the autocorrelation
    const double r0 = a[0].real();
    REQUIRE(r0 > 0.0);
    const double target = std::exp(-1.0);
    for (std::size_t lag = 1; lag < L / 2; ++lag) {
        const double r = a[lag].real() / r0;
        if (r <= target) {
            const double prev = a[lag - 1].real() / r0;
            const double frac = (prev - r) > 0.0 ? (prev - target) / (prev - r) : 0.0;
            return (static_cast<double>(lag - 1) + frac) / rate;
        }
    }
    return -1.0;
}

std::string bmoLabel(const BmoMotionSpec& s) {
    std::ostringstream os;
    // Values only: the label fills the source,mu,spread,rate,sync,bpm columns of the CSV.
    os << std::setprecision(6) << bmoSourceName(s.source) << ',' << s.mu << ',' << s.spread << ',' << s.rate << ','
       << (s.sync ? 1 : 0) << ',' << (s.sync ? s.bpm : 0.0f);
    return os.str();
}

std::string bmoCsvHeader() {
    return "kind,source,mu,spread,rate,sync,bpm,k,P,max_exc,exc_bound,max_abs,mean_abs_all,mean_abs_whole_periods,"
           "expected_mean,zero_hits,cycle_hz,peak_hz,bin_hz,tau_cfg_s,tau_fit_s";
}

/// Every SC-008 reading of one run. `spectral` adds the Cycle peak and the Drift 1/e fit on the
/// representatives (the [long] case only); `csv` (may be null) receives one row per partial and per
/// representative.
void bmoCheckMotion(const BmoMotionSpec& s, const BmoMotionRun& r, bool spectral, std::ostream* csv) {
    const std::string label = bmoLabel(s);
    const auto mu = static_cast<double>(s.mu);
    const bool cycle = s.source == BmoSource::Cycle;
    INFO("cell " << label);

    // The largest whole number of Cycle periods inside the render (Cycle inward-motion mean).
    std::size_t meanSpan = r.intervals;
    if (cycle && r.cycleHz > 0.0f) {
        const double periodIntervals = kBmoControlRate / static_cast<double>(r.cycleHz);
        const double periods = std::floor(static_cast<double>(r.intervals) / periodIntervals);
        if (periods >= 1.0)
            meanSpan = std::min(r.intervals, static_cast<std::size_t>(std::llround(periods * periodIntervals)));
    }

    std::size_t nFixed = 0;
    std::size_t nMoving = 0;
    std::size_t fixedViol = 0;
    std::size_t signViol = 0;
    std::size_t zeroHits = 0;
    std::size_t excViol = 0;
    std::size_t absViol = 0;
    std::size_t vacuityViol = 0;
    std::size_t meanViol = 0;
    std::size_t affineViol = 0;
    double worstExcOverBound = 0.0;
    double worstAffine = 0.0;
    double worstMeanErrRel = 0.0;
    std::size_t kMaxP = kBmoPartialsU;
    std::size_t kMinP = kBmoPartialsU;

    for (std::size_t k = 0; k < kBmoPartialsU; ++k) {
        const float st = r.staticVec[k];
        const std::vector<float>& pk = r.p[k];
        if (bmoBits(st) == 0u) {   // c_k = 0 (h1 included): bit-equal throughout
            ++nFixed;
            for (const float v : pk) {
                if (bmoBits(v) != bmoBits(st))
                    ++fixedViol;
            }
            continue;
        }
        const double P = std::abs(static_cast<double>(st));
        const double sgn = std::signbit(st) ? -1.0 : 1.0;
        const double pBar = sgn * P * (1.0 - kBmoKappa * mu);
        double maxExc = 0.0;
        double maxAbs = 0.0;
        double sumAll = 0.0;
        double sumSpan = 0.0;
        double maxAffine = 0.0;
        std::size_t zeros = 0;
        for (std::size_t q = 0; q < r.intervals; ++q) {
            const float vf = pk[q];
            const auto v = static_cast<double>(vf);
            maxExc = std::max(maxExc, std::abs(v - pBar));
            maxAbs = std::max(maxAbs, std::abs(v));
            if (vf == 0.0f)
                ++zeros;   // centre, allowed (m = -1 at mu = 1); never the other side
            else if (std::signbit(vf) != std::signbit(st))
                ++signViol;
            sumAll += std::abs(v);
            if (q < meanSpan)
                sumSpan += std::abs(v);
            const double model = sgn * P * (1.0 - kBmoKappa * mu + kBmoKappa * mu * static_cast<double>(r.m[q]));
            maxAffine = std::max(maxAffine, std::abs(v - model));
        }
        zeroHits += zeros;
        const double meanAll = sumAll / static_cast<double>(r.intervals);
        const double meanWhole = sumSpan / static_cast<double>(meanSpan);
        const double expectedMean = P * (1.0 - kBmoKappa * mu);
        const double bound = kBmoKappa * mu * P + kBmoEps;
        if (maxExc > bound)
            ++excViol;
        worstExcOverBound = std::max(worstExcOverBound, maxExc - bound);
        if (maxAbs > P + kBmoEps)
            ++absViol;
        if (cycle && mu > 0.0 && P > kBmoMovingFloor && maxExc < 0.99 * kBmoKappa * mu * P)
            ++vacuityViol;
        if (cycle) {
            const double err = std::abs(meanWhole - expectedMean);
            worstMeanErrRel = std::max(worstMeanErrRel, err / P);
            if (err > kBmoMeanRelTol * P)
                ++meanViol;
            if (maxAffine > kBmoEps)
                ++affineViol;
        }
        worstAffine = std::max(worstAffine, maxAffine);
        if (P > kBmoMovingFloor) {
            ++nMoving;
            if (kMaxP == kBmoPartialsU || P > std::abs(static_cast<double>(r.staticVec[kMaxP])))
                kMaxP = k;
            if (kMinP == kBmoPartialsU || P < std::abs(static_cast<double>(r.staticVec[kMinP])))
                kMinP = k;
        }
        if (csv != nullptr) {
            *csv << "k," << label << ',' << k << ',' << P << ',' << maxExc << ',' << bound << ',' << maxAbs << ','
                 << meanAll << ',' << meanWhole << ',' << expectedMean << ',' << zeros << ',' << r.cycleHz
                 << ",na,na," << r.tauSec << ",na\n";
        }
    }

    std::cout << "BSF_MOTION," << label << ",fixed," << nFixed << ",moving," << nMoving << ",worst_exc_minus_bound,"
              << worstExcOverBound << ",worst_affine," << worstAffine << ",worst_mean_err_rel," << worstMeanErrRel
              << ",zero_hits," << zeroHits << ",cycle_hz," << r.cycleHz << ",tau_s," << r.tauSec << "\n";
    if (kMaxP < kBmoPartialsU) {
        const double P = std::abs(static_cast<double>(r.staticVec[kMaxP]));
        double sumAll = 0.0;
        for (const float v : r.p[kMaxP])
            sumAll += std::abs(static_cast<double>(v));
        std::cout << "BSF_MOTION," << label << ",inward,k," << kMaxP << ",P," << P << ",mean_abs,"
                  << sumAll / static_cast<double>(r.intervals) << ",P(1-kappa mu)," << P * (1.0 - kBmoKappa * mu)
                  << "\n";
    }

    // Fixed partials: h1 always, plus at least one c_k = 0 partial at E1 (k = 1, 82.4 Hz, under the lower knee).
    CHECK(bmoBits(r.staticVec[0]) == 0u);
    CHECK(nFixed >= 2u);
    CHECK(fixedViol == 0u);
    CHECK(r.tailViolations == 0u);
    // Excursion and |p| <= P.
    CHECK(excViol == 0u);
    CHECK(absViol == 0u);
    // No side swap.
    CHECK(signViol == 0u);
    REQUIRE(nMoving > 0u);
    if (cycle) {
        CHECK(vacuityViol == 0u);
        CHECK(meanViol == 0u);
        CHECK(affineViol == 0u);   // licenses the two representatives below
    }

    if (!spectral || !(mu > 0.0))
        return;
    const double binHz = kBmoControlRate / static_cast<double>(kBmoPeakFft);
    for (const std::size_t k : {kMaxP, kMinP}) {
        const double P = std::abs(static_cast<double>(r.staticVec[k]));
        if (cycle) {
            const double peak = bmoPeakHz(r.p[k], kBmoControlRate);
            std::cout << "BSF_MOTION," << label << ",cycle_peak,k," << k << ",P," << P << ",peak_hz," << peak
                      << ",cycle_hz," << r.cycleHz << ",bin_hz," << binHz << "\n";
            if (csv != nullptr) {
                *csv << "rep," << label << ',' << k << ',' << P << ",na,na,na,na,na,na,na," << r.cycleHz << ','
                     << peak << ',' << binHz << ',' << r.tauSec << ",na\n";
            }
            INFO("k " << k << " peak " << peak << " Hz, configured " << r.cycleHz << " Hz, bin " << binHz);
            CHECK(peak <= static_cast<double>(BassStereoField::kCycleMaxHz));
            CHECK(std::abs(peak - static_cast<double>(r.cycleHz)) <= binHz);
        } else {
            const double tauFit = bmoOneOverETimeSec(r.p[k], kBmoControlRate);
            const double tau = static_cast<double>(r.tauSec);
            const bool gated = tau <= kBmoTauFitMaxSec;
            std::cout << "BSF_MOTION," << label << ",drift_tau,k," << k << ",P," << P << ",tau_cfg_s," << tau
                      << ",tau_fit_s," << tauFit << ",ratio," << (tauFit > 0.0 ? tauFit / tau : 0.0) << ",gated,"
                      << (gated ? 1 : 0) << "\n";
            if (csv != nullptr) {
                *csv << "rep," << label << ',' << k << ',' << P << ",na,na,na,na,na,na,na," << r.cycleHz
                     << ",na,na," << tau << ',' << tauFit << '\n';
            }
            if (gated) {
                INFO("k " << k << " tau fit " << tauFit << " s, configured " << tau << " s");
                CHECK(tauFit > 0.0);
                CHECK(tauFit >= 0.5 * tau);
                CHECK(tauFit <= 2.0 * tau);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// SC-009(a): zipper references (plan S1 C-16)
// -----------------------------------------------------------------------------

/// Clean core kSawAnchor, 64 partials, 48 kHz, no shape / character / Anchor; bypassField with
/// bypassHandsVector: the stage is not run, the law's vector is handed (T007 decision). SPREAD at Spread 1.
Sft::FieldChainConfig bmoZipperConfig(double f0, double seconds) {
    Sft::FieldChainConfig cfg;
    cfg.chain.base = Krate::DSP::SpectralShapeRecipe::kSawAnchor;
    cfg.chain.f0 = f0;
    cfg.chain.fs = kBmoFs;
    cfg.chain.seconds = seconds;
    cfg.chain.attachAnchor = false;
    cfg.shapeOn = false;
    cfg.bypassField = true;
    cfg.bypassHandsVector = true;
    cfg.field = BmoControls{};
    cfg.field.mode = BmoMode::Spread;
    cfg.field.spread = 1.0f;
    return cfg;
}

/// The whole-period length (samples) <= available whose period count (from the top half of the counts)
/// is closest to an integer number of samples, so the rectangular single-bin DFT's image term is negligible.
std::size_t bmoWholePeriodLength(double fs, double f0, std::size_t available) {
    const double period = fs / f0;
    const auto maxP = static_cast<std::size_t>(std::floor(static_cast<double>(available) / period));
    REQUIRE(maxP >= 1);
    const std::size_t minP = std::max<std::size_t>(1, maxP / 2);
    std::size_t bestP = maxP;
    double bestErr = 2.0;
    for (std::size_t p = maxP; p >= minP; --p) {
        const double len = static_cast<double>(p) * period;
        const double err = std::abs(len - std::round(len));
        if (err < bestErr) {
            bestErr = err;
            bestP = p;
        }
    }
    return static_cast<std::size_t>(std::llround(static_cast<double>(bestP) * period));
}

/// (a_n, phi_n), n = 1..64, from a settled Spread 0 render of the same static note (plan S1 C-16).
std::vector<Sft::FittedHarmonic> bmoFit(double f0) {
    Sft::FieldChainConfig cfg = bmoZipperConfig(f0, 2.0);
    cfg.field.spread = 0.0f;
    const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);
    const std::size_t total = t.coreL.size();
    const auto settle = static_cast<std::size_t>(std::llround(Krate::DSP::MassDynamicsTest::kSettleSeconds * kBmoFs));
    REQUIRE(total > settle);
    const std::size_t len = bmoWholePeriodLength(kBmoFs, f0, total - settle);
    std::vector<Sft::FittedHarmonic> h = Sft::fitHarmonics(t.coreL.data(), settle, len, kBmoFs, f0, kBmoPartials);
    REQUIRE(h.size() == kBmoPartialsU);
    REQUIRE(h[0].amplitude > 1e-4);   // non-vacuity: the note sounds
    std::cout << "BSF_ZIPPER,fit,f0," << f0 << ",start," << settle << ",length," << len << ",a1," << h[0].amplitude
              << ",a2," << h[1].amplitude << ",a64," << h[kBmoPartialsU - 1].amplitude << "\n";
    return h;
}

/// sum (x - y)^2 / sum x^2 over [begin, end), dB.
template <class Y>
double bmoDiffReTotalDb(const std::vector<float>& x, const std::vector<Y>& y, std::size_t begin, std::size_t end) {
    REQUIRE(x.size() == y.size());
    REQUIRE(begin < end);
    REQUIRE(end <= x.size());
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        const auto xv = static_cast<double>(x[i]);
        const double d = xv - static_cast<double>(y[i]);
        num += d * d;
        den += xv * xv;
    }
    REQUIRE(den > 0.0);
    return Pft::safeDb10(num / den);
}

std::vector<float> bmoDiff(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    std::vector<float> r(a.size(), 0.0f);
    for (std::size_t i = 0; i < a.size(); ++i)
        r[i] = static_cast<float>(a[i] - b[i]);
    return r;
}

std::vector<float> bmoToFloat(const std::vector<double>& a) {
    std::vector<float> r(a.size(), 0.0f);
    for (std::size_t i = 0; i < a.size(); ++i)
        r[i] = static_cast<float>(a[i]);
    return r;
}

/// The largest per-interval step and the largest |p| of the recorded vector sequence (non-vacuity).
std::pair<float, float> bmoVectorMotion(const Sft::FieldChainTaps& t) {
    float maxStep = 0.0f;
    float maxAbs = 0.0f;
    for (std::size_t q = 0; q < t.vectorPerInterval.size(); ++q) {
        for (std::size_t k = 0; k < kMaxPartials; ++k) {
            maxAbs = std::max(maxAbs, std::abs(t.vectorPerInterval[q][k]));
            if (q > 0)
                maxStep = std::max(maxStep, std::abs(t.vectorPerInterval[q][k] - t.vectorPerInterval[q - 1][k]));
        }
    }
    return {maxStep, maxAbs};
}

/// Validity (SC-009(a)): a static E1 note at Spread 1, product - held <= -60 dB re total on each channel, from
/// the settle on.
void bmoValidityArm(const std::vector<Sft::FittedHarmonic>& fit, double seconds) {
    const double f0 = Sct::kE1Hz;
    const Sft::FieldChainTaps t = Sft::renderFieldChain(bmoZipperConfig(f0, seconds));
    const std::size_t total = t.coreL.size();
    const auto settle = static_cast<std::size_t>(std::llround(Krate::DSP::MassDynamicsTest::kSettleSeconds * kBmoFs));
    REQUIRE(total > settle);
    const auto f0At = [f0](std::size_t) { return f0; };
    const Sft::PanReference held =
        Sft::renderPanReference(kBmoFs, total, f0At, fit, t.vectorPerInterval, Sft::PanReferenceMode::Held);
    const double dL = bmoDiffReTotalDb(t.coreL, held.L, settle, total);
    const double dR = bmoDiffReTotalDb(t.coreR, held.R, settle, total);
    const double lrDb = bmoDiffReTotalDb(t.coreL, t.coreR, settle, total);   // non-vacuity: really stereo
    std::cout << "BSF_ZIPPER,validity,E1,spread1,product_minus_held_db_L," << dL << ",R," << dR
              << ",L_minus_R_re_L_db," << lrDb << "\n";
    REQUIRE(lrDb > -40.0);
    CHECK(dL <= kBmoZipperBarDb);
    CHECK(dR <= kBmoZipperBarDb);
}

/// Arms (1) and (2): static f0, sidebandPower of held - interpolated over the 2^17 frame from frameStart,
/// re the product's total, <= -60 dB on L and R.
void bmoStaticZipperArm(const char* name, const Sft::FieldChainConfig& cfg, const std::vector<Sft::FittedHarmonic>& fit,
                        std::size_t frameStart) {
    const double f0 = cfg.chain.f0;
    const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);
    const std::size_t total = t.coreL.size();
    REQUIRE(frameStart + kBmoSidebandFrame <= total);
    const auto [maxStep, maxAbs] = bmoVectorMotion(t);
    const auto f0At = [f0](std::size_t) { return f0; };
    const Sft::PanReference held =
        Sft::renderPanReference(kBmoFs, total, f0At, fit, t.vectorPerInterval, Sft::PanReferenceMode::Held);
    const Sft::PanReference interp =
        Sft::renderPanReference(kBmoFs, total, f0At, fit, t.vectorPerInterval, Sft::PanReferenceMode::Interpolated);
    const std::vector<float> resL = bmoDiff(held.L, interp.L);
    const std::vector<float> resR = bmoDiff(held.R, interp.R);
    const Pft::SidebandResult sbL =
        Pft::sidebandPower(resL.data() + frameStart, kBmoFs, f0, BassStereoField::kControlInterval, 0, 0.0, 0.0,
                           t.coreL.data() + frameStart);
    const Pft::SidebandResult sbR =
        Pft::sidebandPower(resR.data() + frameStart, kBmoFs, f0, BassStereoField::kControlInterval, 0, 0.0, 0.0,
                           t.coreR.data() + frameStart);
    double resPow = 0.0;
    for (std::size_t i = frameStart; i < frameStart + kBmoSidebandFrame; ++i)
        resPow += static_cast<double>(resL[i]) * static_cast<double>(resL[i]);
    std::cout << "BSF_ZIPPER," << name << ",frame_start," << frameStart << ",total," << total << ",max_step,"
              << maxStep << ",max_abs_p," << maxAbs << ",sideband_db_L," << sbL.powerDbReTotal << ",remaining_L,"
              << sbL.remainingFraction << ",sideband_db_R," << sbR.powerDbReTotal << ",remaining_R,"
              << sbR.remainingFraction << "\n";
    INFO("arm " << name);
    REQUIRE(maxStep > 0.0f);   // non-vacuity: the vector moved
    REQUIRE(resPow > 0.0);     // ... and the staircase differs from the interpolated ideal
    CHECK(sbL.powerDbReTotal <= kBmoZipperBarDb);
    CHECK(sbR.powerDbReTotal <= kBmoZipperBarDb);
}

/// Arm (1): Spread 0 -> 1 at `upSec` and (when downSec > 0) 1 -> 0 at `downSec`, static E1.
Sft::FieldChainConfig bmoSpreadSweepConfig(double seconds, double upSec, double downSec) {
    Sft::FieldChainConfig cfg = bmoZipperConfig(Sct::kE1Hz, seconds);
    const auto up = static_cast<std::size_t>(std::llround(upSec * kBmoFs));
    const std::size_t down =
        downSec > 0.0 ? static_cast<std::size_t>(std::llround(downSec * kBmoFs)) : std::numeric_limits<std::size_t>::max();
    cfg.fieldAt = [up, down](std::size_t n) {
        BmoControls c{};
        c.mode = BmoMode::Spread;
        c.spread = (n >= up && n < down) ? 1.0f : 0.0f;
        return c;
    };
    return cfg;
}

}  // namespace

// ==============================================================================
// SC-008: MOTION bounded (law level; plan S5, C-9 (A))
// ==============================================================================

TEST_CASE("BassStereoField_MotionBounded", "[systems][profundum][stereo-field][motion][sc-008][long]") {
    std::cout << std::setprecision(6);

    // Drift tau: the configured tau inside [0.5, 30] s at every rate setting.
    {
        auto f = std::make_unique<BassStereoField>();
        f->prepare(kBmoFs, 512);
        for (const float rate : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            BmoControls c{};
            c.motionRate = rate;
            f->setControls(c);
            f->advanceControl();
            const float tau = f->driftTauSeconds();
            const double expected = 30.0 * std::pow(0.5 / 30.0, static_cast<double>(rate));
            std::cout << "BSF_MOTION,drift_tau_cfg,rate," << rate << ",tau_s," << tau << ",formula_s," << expected
                      << "\n";
            INFO("rate " << rate << " tau " << tau);
            CHECK(tau >= BassStereoField::kDriftTauMinSec - 1e-6f);
            CHECK(tau <= BassStereoField::kDriftTauMaxSec + 1e-4f);
        }
    }

    const std::string dir = bmoEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc008-motion.csv");
    REQUIRE(csv.is_open());
    csv << std::setprecision(6) << bmoCsvHeader() << "\n";

    // The grid: source x Spread x mu (0 printed for the inward-motion record) x rate, E1, 60 s.
    for (const BmoSource source : {BmoSource::Drift, BmoSource::Cycle}) {
        for (const float spread : {0.5f, 1.0f}) {
            for (const float mu : {0.0f, 0.5f, 1.0f}) {
                for (const float rate : {0.0f, 0.5f, 1.0f}) {
                    BmoMotionSpec s;
                    s.source = source;
                    s.spread = spread;
                    s.mu = mu;
                    s.rate = rate;
                    const BmoMotionRun r = bmoRunMotion(s);
                    REQUIRE(r.intervals == 90000u);
                    bmoCheckMotion(s, r, true, &csv);
                }
            }
        }
    }

    // Synced Cycle (FR-036, C-9 (A)): SixtyFourth + Triplet at 120 and 200 BPM -> 0.75 / 0.625 Hz after the fold;
    // a NaN and a -1 tempo mid-render keep the previous one.
    for (const auto& [bpm, expectedHz] : {std::pair{120.0f, 0.75f}, std::pair{200.0f, 0.625f}}) {
        BmoMotionSpec s;
        s.source = BmoSource::Cycle;
        s.spread = 1.0f;
        s.mu = 1.0f;
        s.rate = 0.3f;
        s.sync = true;
        s.bpm = bpm;
        s.badTempoMidway = true;
        const BmoMotionRun r = bmoRunMotion(s);
        std::cout << "BSF_MOTION,sync,bpm," << bpm << ",cycle_hz," << r.cycleHz << ",expected," << expectedHz
                  << ",before_bad," << r.cycleHzBeforeBad << ",after_bad," << r.cycleHzAfterBad << "\n";
        INFO("bpm " << bpm);
        CHECK(std::abs(r.cycleHz - expectedHz) <= 1e-5f);
        CHECK(r.cycleHz <= BassStereoField::kCycleMaxHz);
        CHECK(bmoBits(r.cycleHzAfterBad) == bmoBits(r.cycleHzBeforeBad));
        CHECK(bmoBits(r.cycleHz) == bmoBits(r.cycleHzBeforeBad));
        bmoCheckMotion(s, r, true, &csv);
    }
}

TEST_CASE("BassStereoField_MotionBoundedSmoke", "[systems][profundum][stereo-field][motion][sc-008]") {
    std::cout << std::setprecision(6);
    for (const BmoSource source : {BmoSource::Cycle, BmoSource::Drift}) {
        BmoMotionSpec s;
        s.source = source;
        s.spread = 1.0f;
        s.mu = 1.0f;
        s.rate = 1.0f;
        s.seconds = 10.0;
        const BmoMotionRun r = bmoRunMotion(s);
        REQUIRE(r.intervals == 15000u);
        bmoCheckMotion(s, r, false, nullptr);
    }
}

// ==============================================================================
// SC-009(a): no zipper (plan S1 C-16 references)
// ==============================================================================

TEST_CASE("BassStereoField_NoZipper", "[systems][profundum][stereo-field][motion][sc-009][long]") {
    std::cout << std::setprecision(6);
    const std::vector<Sft::FittedHarmonic> fitE1 = bmoFit(Sct::kE1Hz);

    // Validity: the held reference reproduces the bank.
    bmoValidityArm(fitE1, 3.0);

    // (1) Spread 0 -> 1 at 1 s and 1 -> 0 at 3 s (each a full-speed slewed sweep), static E1. The 2^17 frame from
    // 0.75 s holds both sweeps.
    bmoStaticZipperArm("spread_sweep", bmoSpreadSweepConfig(3.5, 1.0, 3.0), fitE1,
                       static_cast<std::size_t>(std::llround(0.75 * kBmoFs)));

    // (2) MOTION Cycle rate 1, depth 1, Spread 1, static E1; frame from 1 s.
    {
        Sft::FieldChainConfig cfg = bmoZipperConfig(Sct::kE1Hz, 3.8);
        cfg.field.motionDepth = 1.0f;
        cfg.field.motionRate = 1.0f;
        cfg.field.motionSource = BmoSource::Cycle;
        bmoStaticZipperArm("motion_cycle", cfg, fitE1, static_cast<std::size_t>(std::llround(1.0 * kBmoFs)));
    }

    // (3) 48-semitone Drop C3 -> C3 * 2^-4 over 1 s (0.5 s .. 1.5 s), Spread 1.
    {
        const double c3 = Sct::kC3Hz;
        const std::vector<Sft::FittedHarmonic> fitC3 = bmoFit(c3);
        Sft::FieldChainConfig cfg = bmoZipperConfig(c3, 2.5);
        const auto g0 = static_cast<std::size_t>(std::llround(0.5 * kBmoFs));
        const auto g1 = static_cast<std::size_t>(std::llround(1.5 * kBmoFs));
        cfg.f0At = [c3, g0, g1](std::size_t i) {
            if (i < g0)
                return c3;
            if (i >= g1)
                return c3 * std::exp2(-4.0);
            const double t = static_cast<double>(i - g0) / static_cast<double>(g1 - g0);
            return c3 * std::exp2(-4.0 * t);
        };
        const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);
        const std::size_t total = t.coreL.size();
        REQUIRE(total > g1);
        std::vector<float> traj(total, 0.0f);
        for (std::size_t i = 0; i < total; ++i)
            traj[i] = static_cast<float>(cfg.f0At(i));   // the track renderFieldChain fed the core
        const auto refF0 = [&traj](std::size_t i) { return Pft::pitchHoldAlignedF0(traj, i); };
        const Sft::PanReference held =
            Sft::renderPanReference(kBmoFs, total, refF0, fitC3, t.vectorPerInterval, Sft::PanReferenceMode::Held);
        const Sft::PanReference interp = Sft::renderPanReference(kBmoFs, total, refF0, fitC3, t.vectorPerInterval,
                                                                 Sft::PanReferenceMode::Interpolated);
        const auto [maxStep, maxAbs] = bmoVectorMotion(t);

        // Broadband residual (held - interpolated) re the product's total, over [glide start, end).
        const auto broadband = [&](const std::vector<double>& h, const std::vector<double>& ip,
                                   const std::vector<float>& product) {
            double num = 0.0;
            double den = 0.0;
            for (std::size_t i = g0; i < total; ++i) {
                const double d = h[i] - ip[i];
                num += d * d;
                den += static_cast<double>(product[i]) * static_cast<double>(product[i]);
            }
            REQUIRE(den > 0.0);
            return Pft::safeDb10(num / den);
        };
        const double bbL = broadband(held.L, interp.L, t.coreL);
        const double bbR = broadband(held.R, interp.R, t.coreR);

        // Differential clicks of the product against the held reference, from 0.25 s (past the note-on).
        const auto off = static_cast<std::size_t>(std::llround(0.25 * kBmoFs));
        const std::vector<float> heldL = bmoToFloat(held.L);
        const std::vector<float> heldR = bmoToFloat(held.R);
        const Sct::ClickCount clL =
            Sct::differentialClicks(t.coreL.data() + off, heldL.data() + off, total - off, kBmoFs, c3);
        const Sct::ClickCount clR =
            Sct::differentialClicks(t.coreR.data() + off, heldR.data() + off, total - off, kBmoFs, c3);

        std::cout << "BSF_ZIPPER,drop48,max_step," << maxStep << ",max_abs_p," << maxAbs << ",broadband_db_L," << bbL
                  << ",broadband_db_R," << bbR << ",clicks_L," << clL.counted << ",raw_L," << clL.transitionDetections
                  << ",ref_L," << clL.referenceDetections << ",clicks_R," << clR.counted << ",raw_R,"
                  << clR.transitionDetections << ",ref_R," << clR.referenceDetections << "\n";
        REQUIRE(maxStep > 0.0f);   // non-vacuity: partials crossed the ramp, the vector moved
        CHECK(bbL <= kBmoZipperBarDb);
        CHECK(bbR <= kBmoZipperBarDb);
        CHECK(clL.counted == 0u);
        CHECK(clR.counted == 0u);
    }
}

TEST_CASE("BassStereoField_NoZipperSmoke", "[systems][profundum][stereo-field][motion][sc-009]") {
    std::cout << std::setprecision(6);
    const std::vector<Sft::FittedHarmonic> fitE1 = bmoFit(Sct::kE1Hz);
    bmoValidityArm(fitE1, 2.0);
    // Arm (1)'s 0 -> 1 step only; the render holds sidebandPower's 2^17 frame (2.73 s) from 0.75 s.
    bmoStaticZipperArm("spread_step_smoke", bmoSpreadSweepConfig(3.5, 1.0, 0.0), fitE1,
                       static_cast<std::size_t>(std::llround(0.75 * kBmoFs)));
}
