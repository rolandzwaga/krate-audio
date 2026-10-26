// Profundum Phase 5, task T001 (plan C-20, R-1, S10.7, S14 step 1): CPU feasibility microbench of the Shape +
// Character cost, written BEFORE any Phase 5 header lands.
//
// EPOCH 2 (2026-10-15, after the user ruling, spec Clarifications "T001 CPU bench ruling"; plan C-23, C-24):
//   --linked     mono link of plan S2: L == R input, zero offsets, ONE channel processed and copied to the other.
//                Default (no flag) = unlinked: decorrelated L/R, both channels processed. SC-015 gates the linked cell.
//   C-24         Fracture: s = sin(pi*phi) from a rotating phasor (complex rotation per F-sample, re-seeded with
//                std::sin/cos on every f0 change, re-normalised once per block); sigma(phi) and tanh(g*beta*s) from the
//                rational benchTanhR (Pade (7,6)); no std::sin / std::tanh in the per-sample loop. TanhADAA stays.
//   C-23         Only nonlinearities run at F*fs. Pre-tilts (Tube, Bite, BITE emphasis) run at the base rate on the
//                input before upsample; post-tilts (all six), ROUND's SVF low-pass and HOLLOW's notch + band run at the
//                base rate on the round-tripped outputs after downsample. HOLLOW filters the decimated linear path
//                (weighted by w_H) and never enters the oversampled loop. The lin / res split is kept.
//
// It includes the REAL reused KrateDSP headers (PolyphaseHalfbandOversampler, Biquad, DCBlocker, OnePoleLP, SVF,
// TanhADAA, TubeStage, WavefolderProcessor, BitCrusher, OnePoleSmoother, core/sigmoid.h) and prototypes the NEW Phase 5
// pieces locally, in an anonymous namespace with a `Bench` prefix, as plan S3-S6 specify:
//   BenchResidualPath     (S3: 1 up, 2 down, notches {0.50, 0.52, 0.55}*fs, Q {1.5, 2.0, 1.5} at 2fs, 2 Hz DC on
//                          the residual)
//   BenchAdaa2<Law, T>    (S4.1: closed-form ADAA2, eps = 1e-5 in argument units, F2 cached; double unless noted)
//   BenchDiodeLaw / BenchRampLaw / BenchAbsLaw (S4.1, S4.2)
//   BenchKeytrackedTilt   (S6.1: first-order tilt around pivotMultiple*f0; 10^x pair recomputed only on a changed tone)
//   BenchCharacter        (S5.3, S5.4: Tube with branch 2x + TubeStage at 2*F*fs, Fold, Rectify, Bite, Crush with two
//                          parity-keyed hold engines and the B-spline step ring, Fracture with TanhADAA)
//   BenchShapeMorph       (S6.3: ROUND / HOLLOW / BITE, linear weights, inactive regimes skipped, re-key on entry)
//
// Cells (48 kHz, F = 4, host block 64, input = 64-partial saw at E1, peak P = HarmonicFold::kCalibrationPeak;
// default UNLINKED: L and R processed independently, R offset by 0.37 period; --linked: R = L, channel 0 processed
// and copied; per-channel Amount 1 and Tone 0.5):
//   char_<algo>   Character alone at each algorithm (its own residual path included)
//   shape_<s>     ShapeMorph alone at SHAPE in {0, 0.25, 0.5, 0.75, 1} (its own residual path included)
//   ramp          ShapeMorph -> Character at the measured worst Shape x worst Character; Amount [0.8, 1], Tone
//                 [0.2, 0.8] and SHAPE (worst +- 0.1, clamped) targets follow triangles, so the 10 ms smoothed values
//                 change every base sample and every coefficient update of plan S8 runs per sample
//   drop          same chain, static controls, f0 falling 48 st -> 0 over 60 ms, re-triggered every 250 ms, f0
//                 sampled on the 16-sample grid (ProfundumCore kPitchUpdateInterval), so every keytracked member is
//                 re-keyed per grid step
// Report: ns per base sample (stereo) and % of one core = ns * 48000 / 1e7.
// Total = worst Character + worst Shape (each with its own path).
//
// Compile-time variants:
//   -DP5_CTRL_INTERVAL=16   coefficient updates quantised to a 16-sample grid anchored to the absolute sample count
//   -DP5_RECT_ADAA_FLOAT    float ADAA2 for the Rectify polynomial laws only (R-1 lever 4)
// Flags: --linked (mono-linked cells; default unlinked), --quick (0.2 s wall clock per cell, smoke only),
//        --seconds <s> (default 3.0).
//
// Build/run: bash specs/profundum-phase5-shape-character/bench/run-bench.sh (see that script).
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/bit_crusher.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/tanh_adaa.h>
#include <krate/dsp/processors/tube_stage.h>
#include <krate/dsp/processors/wavefolder_processor.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <xmmintrin.h>

#ifndef P5_CTRL_INTERVAL
#define P5_CTRL_INTERVAL 1
#endif

namespace {

using namespace Krate::DSP;

constexpr double kBenchFs = 48000.0;
constexpr std::size_t kBenchF = 4;
constexpr std::size_t kBenchBlock = 64;
constexpr float kBenchP = 0.3552343859f;           // HarmonicFold::kCalibrationPeak (harmonic_fold.h:83)
constexpr std::uint64_t kBenchCtrlInterval = P5_CTRL_INTERVAL;
constexpr double kBenchE1 = 41.20344461410875;     // 440 * 2^((28 - 69) / 12)
constexpr double kBenchPiD = 3.14159265358979323846;
#ifdef P5_RECT_ADAA_FLOAT
using BenchRectScalar = float;
constexpr const char* kBenchRectName = "float";
#else
using BenchRectScalar = double;
constexpr const char* kBenchRectName = "double";
#endif

// ---------------------------------------------------------------- S4.1 closed-form ADAA2 -------------------------
struct BenchDiodeLaw {   // mirrors Asymmetric::diode (sigmoid.h:358-366)
    template <class T> static T f(T x) noexcept {
        return x >= T(0) ? T(1) - std::exp(T(-1.5) * x) : x / (T(1) - T(0.5) * x);
    }
    template <class T> static T F1(T x) noexcept {
        if (x >= T(0)) return x + std::exp(T(-1.5) * x) / T(1.5) - T(1) / T(1.5);
        const T w = T(1) - T(0.5) * x;
        return T(-4) * std::log(w) - T(2) * x;
    }
    template <class T> static T F2(T x) noexcept {
        if (x >= T(0)) return T(0.5) * x * x - std::exp(T(-1.5) * x) / T(2.25) - x / T(1.5) + T(1) / T(2.25);
        const T w = T(1) - T(0.5) * x;
        return T(8) * (w * std::log(w) - w) - x * x + T(8);
    }
};
struct BenchRampLaw {    // max(x, 0)
    template <class T> static T f(T x) noexcept { return x > T(0) ? x : T(0); }
    template <class T> static T F1(T x) noexcept { return x > T(0) ? T(0.5) * x * x : T(0); }
    template <class T> static T F2(T x) noexcept { return x > T(0) ? x * x * x / T(6) : T(0); }
};
struct BenchAbsLaw {     // |x|
    template <class T> static T f(T x) noexcept { return x < T(0) ? -x : x; }
    template <class T> static T F1(T x) noexcept { return T(0.5) * x * (x < T(0) ? -x : x); }
    template <class T> static T F2(T x) noexcept { return x * x * (x < T(0) ? -x : x) / T(6); }
};

template <class Law, class T>
class BenchAdaa2 {
public:
    static constexpr T kEps = T(1.0e-5);
    void reset() noexcept { x1_ = x2_ = f2x1_ = d1_ = T(0); }
    T process(T x) noexcept {
        const T f2x = Law::template F2<T>(x);
        const T dx1 = x - x1_;
        const T d0 = std::abs(dx1) > kEps ? (f2x - f2x1_) / dx1 : Law::template F1<T>(T(0.5) * (x + x1_));
        T y;
        const T dx2 = x - x2_;
        if (std::abs(dx2) > kEps) {
            y = T(2) * (d0 - d1_) / dx2;
        } else {
            const T xb = T(0.5) * (x + x2_);
            const T delta = xb - x1_;
            y = std::abs(delta) > kEps
                    ? (T(2) / delta) * (Law::template F1<T>(xb) + (f2x1_ - Law::template F2<T>(xb)) / delta)
                    : Law::template f<T>(T(0.5) * (xb + x1_));
        }
        x2_ = x1_;
        x1_ = x;
        d1_ = d0;
        f2x1_ = f2x;
        return y;
    }
private:
    T x1_ = T(0), x2_ = T(0), f2x1_ = T(0), d1_ = T(0);
};

// ---------------------------------------------------------------- C-24 rational tanh ---------------------------
// tanh(x) ~= x * (135135 + 17325 x^2 + 378 x^4 + x^6) / (135135 + 62370 x^2 + 3150 x^4 + 28 x^6)
// (Pade (7,6), the Lambert continued fraction truncated after 6 levels), argument clamped to [-4, 4].
// Measured max |error| vs std::tanh over a 10001-point grid on [-4, 4]: 1.50e-5 (double evaluation, at |x| = 4);
// float evaluation as compiled here (clang -O2 -ffast-math): 1.499e-5 at x = -3.9992 (2026-10-15). Re-measured and
// printed once at start-up (`tanh_approx` line). Bar: 1e-4.
inline float benchTanhR(float x) noexcept {
    const float xc = std::clamp(x, -4.0f, 4.0f);
    const float x2 = xc * xc;
    return xc * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) /
           (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}
const float kBenchInvTanh4 = static_cast<float>(1.0 / std::tanh(4.0));   // namespace-scope init, never per sample

// h1 Fourier coefficient of g(sin phi) over 256 phase points (prepare-time only).
template <class Fn>
double benchH1(Fn&& g) {
    constexpr int kN = 256;
    double acc = 0.0;
    for (int j = 0; j < kN; ++j) {
        const double phi = 2.0 * kBenchPiD * (j + 0.5) / kN;
        acc += g(std::sin(phi)) * std::sin(phi);
    }
    return 2.0 * acc / kN;
}
double benchDiodeH1(double gain) {
    return benchH1([gain](double s) { return BenchDiodeLaw::f<double>(gain * s); });
}

using BenchTable = std::array<float, 65>;
float benchLerp(const BenchTable& t, float x) noexcept {
    const float pos = std::clamp(x, 0.0f, 1.0f) * 64.0f;
    const int i = std::min(static_cast<int>(pos), 63);
    const float fr = pos - static_cast<float>(i);
    const auto iu = static_cast<std::size_t>(i);
    return t[iu] + fr * (t[iu + 1] - t[iu]);
}

// ---------------------------------------------------------------- S6.1 KeytrackedTilt ---------------------------
class BenchKeytrackedTilt {
public:
    void prepare(double rate) noexcept { lp_.prepare(rate); }
    void configure(float pivotMultiple, float maxShelfDb) noexcept {
        k_ = pivotMultiple;
        maxDb_ = maxShelfDb;
        lastTone_ = -1.0f;
    }
    void setTone(float tone) noexcept {
        if (tone == lastTone_) return;   // the 10^x pair is recomputed only on a changed value (S6.1)
        lastTone_ = tone;
        const float g = (2.0f * tone - 1.0f) * maxDb_;
        const float gl = std::pow(10.0f, -g / 20.0f);
        const float gh = std::pow(10.0f, g / 20.0f);
        const float nu = std::sqrt(2.0f / (gl * gl + gh * gh));
        gL_ = gl * nu;
        gH_ = gh * nu;
    }
    void setFundamentalHz(float f0) noexcept { lp_.setCutoff(k_ * f0); }
    void reset() noexcept { lp_.reset(); }
    float process(float x) noexcept {
        const float lp = lp_.process(x);
        return gL_ * lp + gH_ * (x - lp);
    }
private:
    OnePoleLP lp_;
    float k_ = 2.0f, maxDb_ = 0.0f, gL_ = 1.0f, gH_ = 1.0f, lastTone_ = -1.0f;
};

// ---------------------------------------------------------------- S3 OversampledResidualPath -------------------
struct BenchResidualPath {
    static constexpr std::array<float, 3> kNotchRatios{0.50f, 0.52f, 0.55f};
    static constexpr std::array<float, 3> kNotchQs{1.5f, 2.0f, 1.5f};
    PolyphaseHalfbandOversampler up, downLin, downRes;
    std::array<Biquad, 3> notch{};
    DCBlocker dc;
    void prepare(double fs) noexcept {
        up.setFactor(kBenchF);
        downLin.setFactor(kBenchF);
        downRes.setFactor(kBenchF);
        for (std::size_t k = 0; k < 3; ++k)
            notch[k].configure(FilterType::Notch, kNotchRatios[k] * static_cast<float>(fs), kNotchQs[k], 0.0f,
                               static_cast<float>(2.0 * fs));
        dc.prepare(fs, 2.0f);
        reset();
    }
    void reset() noexcept {
        up.reset();
        downLin.reset();
        downRes.reset();
        for (auto& n : notch) n.reset();
        dc.reset();
    }
    void upsample(float x, float* u) noexcept { up.upsample(x, u); }
    // lb = decimated linear path; rb = decimated, notched, DC-blocked residual.
    void downsampleSplit(const float* lin, const float* res, float& lb, float& rb) noexcept {
        lb = downLin.downsample(lin);
        rb = dc.process(downRes.downsample(res, [this](float v) noexcept {
            for (auto& n : notch) v = n.process(v);
            return v;
        }));
    }
    float downsample(const float* lin, const float* res) noexcept {
        float lb = 0.0f, rb = 0.0f;
        downsampleSplit(lin, res, lb, rb);
        return lb + rb;
    }
};

// ---------------------------------------------------------------- S5.4 Crush hold engine -------------------------
// Causal integrated cubic B-spline over v in [0, 4) (S5.4, polyblep.h:105 pieces).
float benchStepS(float v) noexcept {
    if (v < 1.0f) return v * v * v * v / 24.0f;
    if (v < 2.0f) {
        const float w = 2.0f - v;
        return 0.5f - (3.0f * w * w * w * w - 8.0f * w * w * w + 16.0f * w) / 24.0f;
    }
    if (v < 3.0f) {
        const float w = v - 2.0f;
        return 0.5f + (3.0f * w * w * w * w - 8.0f * w * w * w + 16.0f * w) / 24.0f;
    }
    if (v < 4.0f) {
        const float w = 4.0f - v;
        return 1.0f - w * w * w * w / 24.0f;
    }
    return 1.0f;
}

struct BenchHoldEngine {
    struct Step { float d = 0.0f; float age = 0.0f; };
    std::array<Step, 3> ring{};
    std::size_t count = 0;
    float held = 0.0f;
    float m = 4.0f;
    BitCrusher bc;
    std::uint32_t retired = 0;
    void reset() noexcept {
        count = 0;
        held = 0.0f;
        bc.reset();
    }
    // thetaPrev in [0,1), theta unwrapped (may be >= 1); both in cycles of f0.
    float process(float thetaPrev, float theta, float dTheta, float uPrev, float u) noexcept {
        std::size_t w = 0;   // age the ring, drop finished steps
        for (std::size_t i = 0; i < count; ++i) {
            ring[i].age += 1.0f;
            if (ring[i].age < 4.0f) ring[w++] = ring[i];
        }
        count = w;
        const float a = std::floor(thetaPrev * m);
        const float b = std::floor(theta * m);
        if (b > a) {   // at most one crossing per F-sample under the occupancy bound
            float tau = ((a + 1.0f) / m - thetaPrev) / dTheta;
            tau = std::clamp(tau, 1.0e-6f, 1.0f);
            const float value = uPrev + tau * (u - uPrev);
            const float q = kBenchP / 0.9f * bc.process(0.9f * value / kBenchP);
            const float d = q - held;
            held = q;
            if (count == 3) {   // ring-full policy: retire the oldest (unreachable under the bound)
                ring[0] = ring[1];
                ring[1] = ring[2];
                count = 2;
                ++retired;
            }
            ring[count++] = Step{d, 1.0f - tau};   // age = n - t_k with t_k = n - 1 + tau
        }
        float out = held;
        for (std::size_t i = 0; i < count; ++i) out -= ring[i].d * (1.0f - benchStepS(ring[i].age));
        return out;
    }
};

// ---------------------------------------------------------------- S5 BassCharacterStage prototype ---------------
enum class BenchAlgo : int { Tube = 0, Fold, Rectify, Bite, Crush, Fracture };
constexpr std::array<const char*, 6> kBenchAlgoNames{"tube", "fold", "rectify", "bite", "crush", "fracture"};

struct BenchCharTables {
    BenchTable cTube{}, cBite{}, cFrac{}, cFold{};
    void fill() {
        for (std::size_t i = 0; i < 65; ++i) {
            const double a = static_cast<double>(i) / 64.0;
            const double gTube = std::pow(10.0, (-6.0 + 18.0 * a) / 20.0);
            cTube[i] = static_cast<float>(benchH1([gTube](double s) {
                return static_cast<double>(Asymmetric::tube(static_cast<float>(gTube * kBenchP * s))) / kBenchP;
            }));
            const double theta = 0.1 + 2.4 * a;
            const double mk = 1.0 / std::sin(std::min(theta, kBenchPiD / 2.0));
            cFold[i] = static_cast<float>(benchH1([theta, mk](double s) { return mk * std::sin(theta * s); }));
            cBite[i] = static_cast<float>(benchDiodeH1(std::pow(10.0, (-12.0 + 30.0 * a) / 20.0)));
            const double g = 0.5 + 1.5 * a;
            cFrac[i] = static_cast<float>(benchH1([g](double s) { return std::tanh(g * s) / g; }));
        }
    }
};

struct BenchCharChannel {
    BenchResidualPath path;
    // Tube (C-8, C-10)
    BenchKeytrackedTilt tubePre, tubePost;
    PolyphaseHalfbandOversampler tubeOs;
    TubeStage tube;
    std::array<float, 2> tubeDelay{};
    // Fold (C-12)
    WavefolderProcessor fold;
    BenchKeytrackedTilt foldPost;
    float foldMk = 1.0f, foldC = 1.0f;
    // Rectify (C-13, C-19)
    BenchAdaa2<BenchRampLaw, BenchRectScalar> rectRamp;
    BenchAdaa2<BenchAbsLaw, BenchRectScalar> rectAbs;
    BenchKeytrackedTilt rectPost;
    float rectWet = 1.0f, rectB = 1.0f, rectMk = 1.0f, rectLinC = 0.0f, rectPrev = 0.0f;
    // Bite (C-8, C-11)
    BenchKeytrackedTilt bitePre, bitePost;
    BenchAdaa2<BenchDiodeLaw, double> biteAdaa;
    float biteG = 1.0f, biteC = 1.0f, bitePrev = 0.0f;
    // Crush (FR-051, D-6)
    BenchKeytrackedTilt crushPost;
    std::array<BenchHoldEngine, 2> crushEng{};
    float crushTheta = 0.0f, crushInc = 0.0f, crushFrac = 0.0f, crushBits = -1.0f, crushPrev = 0.0f;
    int crushLo = 4, crushHi = 5;
    // Fracture (C-5)
    BenchKeytrackedTilt fracPost;
    TanhADAA fracAdaa;
    float fracPhi = 0.0f, fracInc = 0.0f, fracA = 1.0f, fracG = 2.0f, fracBeta = 0.5f, fracC = 1.0f, fracPrev = 0.0f;
    // C-24 rotating phasor: (fracRc, fracRs) = (cos, sin)(pi * phi); per-F-sample rotation by delta = pi * fracInc
    // (fracCd, fracSd) and the half-step (fracCh, fracSh) for the midpoint sine.
    float fracRc = 1.0f, fracRs = 0.0f, fracCd = 1.0f, fracSd = 0.0f, fracCh = 1.0f, fracSh = 0.0f;
    // Smoothed controls
    OnePoleSmoother amountSm, toneSm;
    float appliedA = -1.0f, appliedT = -1.0f, appliedF0 = -1.0f;
};

class BenchCharacter {
public:
    void prepare(double fs, BenchAlgo algo) {
        fs_ = fs;
        algo_ = algo;
        tables_.fill();
        const double fF = fs * static_cast<double>(kBenchF);
        for (std::size_t c = 0; c < 2; ++c) {
            auto& ch = ch_[c];
            ch.path.prepare(fs);
            // C-23: every tilt runs at the base rate fs.
            ch.tubePre.prepare(fs);
            ch.tubePre.configure(1.0f, 6.0f);
            ch.tubePost.prepare(fs);
            ch.tubePost.configure(2.0f, 6.0f);
            ch.tubeOs.setFactor(2);
            ch.tube.prepare(2.0 * fF, 2);
            ch.tube.setSaturationAmount(1.0f);
            ch.tube.setBias(0.0f);
            ch.fold.prepare(fF, kBenchF);
            ch.fold.setModel(WavefolderModel::Serge);
            ch.fold.setSymmetry(0.0f);
            ch.fold.setMix(1.0f);
            ch.foldPost.prepare(fs);
            ch.foldPost.configure(2.0f, 12.0f);
            ch.rectPost.prepare(fs);
            ch.rectPost.configure(2.0f, 12.0f);
            ch.bitePre.prepare(fs);
            ch.bitePre.configure(1.0f, 6.0f);
            ch.bitePost.prepare(fs);
            ch.bitePost.configure(2.0f, 6.0f);
            ch.crushPost.prepare(fs);
            ch.crushPost.configure(2.0f, 12.0f);
            for (auto& eng : ch.crushEng) {
                eng.bc.prepare(fF);
                eng.bc.setDither(0.0f);
                eng.bc.setSeed(0x5F3759DFu ^ static_cast<std::uint32_t>(c));
            }
            ch.fracPost.prepare(fs);
            ch.fracPost.configure(2.0f, 12.0f);
            ch.fracAdaa.setDrive(1.0f);
            ch.amountSm.configure(10.0f, static_cast<float>(fs));
            ch.toneSm.configure(10.0f, static_cast<float>(fs));
        }
    }
    void setLinked(bool linked) noexcept { linked_ = linked; }
    void setAmount(float a) noexcept { for (auto& ch : ch_) ch.amountSm.setTarget(a); }
    void setTone(float t) noexcept { for (auto& ch : ch_) ch.toneSm.setTarget(t); }
    void snapControls() noexcept {
        for (auto& ch : ch_) {
            ch.amountSm.snapToTarget();
            ch.toneSm.snapToTarget();
        }
    }

    // f0PerSample == nullptr -> f0Static.
    void processBlock(float* left, float* right, std::size_t n, std::uint64_t absStart, const float* f0PerSample,
                      float f0Static) noexcept {
        std::array<float*, 2> io{left, right};
        const std::size_t nCh = linked_ ? 1 : 2;   // linked: channel 0 runs, channel 1 is its copy (plan S2)
        if (algo_ == BenchAlgo::Fracture) {         // C-24: re-normalise the rotating phasor once per block
            for (std::size_t c = 0; c < nCh; ++c) {
                auto& ch = ch_[c];
                const float g = 1.5f - 0.5f * (ch.fracRc * ch.fracRc + ch.fracRs * ch.fracRs);
                ch.fracRc *= g;
                ch.fracRs *= g;
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            const float f0 = f0PerSample != nullptr ? f0PerSample[i] : f0Static;
            const bool tick = ((absStart + i) % kBenchCtrlInterval) == 0;
            for (std::size_t c = 0; c < nCh; ++c) {
                float& s = io[c][i];
                s = processSample(ch_[c], s, f0, tick);
            }
            if (linked_) right[i] = left[i];
        }
    }
    [[nodiscard]] float peak() const noexcept { return peak_; }

private:
    void applyControls(BenchCharChannel& ch, float a, float t, float f0) noexcept {
        const bool f0Changed = f0 != ch.appliedF0;
        ch.appliedA = a;
        ch.appliedT = t;
        ch.appliedF0 = f0;
        switch (algo_) {
            case BenchAlgo::Tube: {
                ch.tubePre.setTone(t);
                ch.tubePost.setTone(t);
                if (f0Changed) {
                    ch.tubePre.setFundamentalHz(f0);
                    ch.tubePost.setFundamentalHz(f0);
                }
                ch.tube.setInputGain(-6.0f + 18.0f * a);
                const float c = benchLerp(tables_.cTube, a);
                ch.tube.setOutputGain(std::clamp(-20.0f * std::log10(c), -24.0f, 24.0f));
                break;
            }
            case BenchAlgo::Fold: {
                const float theta = 0.1f + 2.4f * a;
                ch.fold.setFoldAmount(theta);
                ch.foldMk = kBenchP / std::sin(std::min(theta, kHalfPi));
                ch.foldC = benchLerp(tables_.cFold, a);
                ch.foldPost.setTone(t);
                if (f0Changed) ch.foldPost.setFundamentalHz(f0);
                break;
            }
            case BenchAlgo::Rectify: {
                ch.rectWet = std::min(1.0f, a / 0.4f);
                const float x = std::clamp((a - 0.4f) / 0.6f, 0.0f, 1.0f);
                ch.rectB = x * x * (3.0f - 2.0f * x);
                const float b = ch.rectB;
                const float ac2 = (1.0f + b * b) / 4.0f - (1.0f + b) * (1.0f + b) / (kPi * kPi);
                ch.rectMk = std::sqrt(0.5f / ac2);
                ch.rectLinC = (1.0f - ch.rectWet) + ch.rectWet * ch.rectMk * (1.0f - b) * 0.5f;
                ch.rectPost.setTone(t);
                if (f0Changed) ch.rectPost.setFundamentalHz(f0);
                break;
            }
            case BenchAlgo::Bite: {
                ch.bitePre.setTone(t);
                ch.bitePost.setTone(t);
                if (f0Changed) {
                    ch.bitePre.setFundamentalHz(f0);
                    ch.bitePost.setFundamentalHz(f0);
                }
                ch.biteG = std::pow(10.0f, (-12.0f + 30.0f * a) / 20.0f);
                ch.biteC = benchLerp(tables_.cBite, a);
                break;
            }
            case BenchAlgo::Crush: {
                const float fF = static_cast<float>(fs_) * static_cast<float>(kBenchF);
                const float m = 64.0f * std::pow(1.0f / 16.0f, a);
                const float mEff = std::min(m, fF / (4.0f * f0));
                ch.crushLo = static_cast<int>(std::floor(mEff));
                ch.crushHi = ch.crushLo + 1;
                ch.crushFrac = mEff - static_cast<float>(ch.crushLo);
                ch.crushEng[static_cast<std::size_t>(ch.crushLo & 1)].m = static_cast<float>(ch.crushLo);
                ch.crushEng[static_cast<std::size_t>(ch.crushHi & 1)].m = static_cast<float>(ch.crushHi);
                const float bits = 12.0f - 8.0f * a;
                if (bits != ch.crushBits) {
                    ch.crushBits = bits;
                    ch.crushEng[0].bc.setBitDepth(bits);
                    ch.crushEng[1].bc.setBitDepth(bits);
                }
                ch.crushInc = f0 / fF;
                ch.crushPost.setTone(t);
                if (f0Changed) ch.crushPost.setFundamentalHz(f0);
                break;
            }
            case BenchAlgo::Fracture: {
                ch.fracA = a;
                ch.fracG = 0.5f + 1.5f * a;
                ch.fracBeta = 0.5f * a;
                ch.fracC = benchLerp(tables_.cFrac, a);
                ch.fracInc = f0 / (static_cast<float>(fs_) * static_cast<float>(kBenchF));
                ch.fracPost.setTone(t);
                if (f0Changed) {   // C-24 re-key: the only place std::sin / std::cos run for Fracture
                    ch.fracPost.setFundamentalHz(f0);
                    const double d = kBenchPiD * static_cast<double>(ch.fracInc);
                    const double th = kBenchPiD * static_cast<double>(ch.fracPhi);
                    ch.fracCd = static_cast<float>(std::cos(d));
                    ch.fracSd = static_cast<float>(std::sin(d));
                    ch.fracCh = static_cast<float>(std::cos(0.5 * d));
                    ch.fracSh = static_cast<float>(std::sin(0.5 * d));
                    ch.fracRc = static_cast<float>(std::cos(th));
                    ch.fracRs = static_cast<float>(std::sin(th));
                }
                break;
            }
        }
    }

    float processSample(BenchCharChannel& ch, float x, float f0, bool tick) noexcept {
        const float a = ch.amountSm.process();
        const float t = ch.toneSm.process();
        if (tick && (a != ch.appliedA || t != ch.appliedT || f0 != ch.appliedF0)) applyControls(ch, a, t, f0);
        // C-23: pre-tilts at the base rate, before the upsample.
        float xin = x;
        if (algo_ == BenchAlgo::Tube) xin = ch.tubePre.process(x);
        else if (algo_ == BenchAlgo::Bite) xin = ch.bitePre.process(x);
        std::array<float, kBenchF> u{}, lin{}, res{};
        ch.path.upsample(xin, u.data());
        switch (algo_) {
            case BenchAlgo::Tube:
                for (std::size_t k = 0; k < kBenchF; ++k) {
                    std::array<float, 2> b2{};
                    ch.tubeOs.upsample(u[k], b2.data());
                    ch.tube.process(b2.data(), 2);
                    const float y = ch.tubeOs.downsample(b2.data());
                    const float l = ch.tubeDelay[1];   // l = u[n - 2] at F*fs
                    ch.tubeDelay[1] = ch.tubeDelay[0];
                    ch.tubeDelay[0] = u[k];
                    lin[k] = l;
                    res[k] = y - l;
                }
                break;
            case BenchAlgo::Fold:
                for (std::size_t k = 0; k < kBenchF; ++k) {
                    float buf = u[k] / kBenchP;
                    ch.fold.process(&buf, 1);
                    const float y = buf * ch.foldMk;
                    const float l = ch.foldC * u[k];
                    lin[k] = l;
                    res[k] = y - l;
                }
                break;
            case BenchAlgo::Rectify:
                for (std::size_t k = 0; k < kBenchF; ++k) {
                    const auto uk = static_cast<BenchRectScalar>(u[k]);
                    const auto rr = static_cast<float>(ch.rectRamp.process(uk));
                    const auto ra = static_cast<float>(ch.rectAbs.process(uk));
                    const float wetPart = ch.rectWet * ch.rectMk * ((1.0f - ch.rectB) * rr + ch.rectB * ra);
                    const float y = (1.0f - ch.rectWet) * ch.rectPrev + wetPart;
                    const float l = ch.rectLinC * ch.rectPrev;
                    ch.rectPrev = u[k];
                    lin[k] = l;
                    res[k] = y - l;
                }
                break;
            case BenchAlgo::Bite:
                for (std::size_t k = 0; k < kBenchF; ++k) {
                    const auto v = static_cast<double>(ch.biteG * u[k] / kBenchP);
                    const float y = static_cast<float>(ch.biteAdaa.process(v)) * kBenchP / ch.biteC;
                    const float l = ch.bitePrev;
                    ch.bitePrev = u[k];
                    lin[k] = l;
                    res[k] = y - l;
                }
                break;
            case BenchAlgo::Crush:
                for (std::size_t k = 0; k < kBenchF; ++k) {
                    const float thPrev = ch.crushTheta;
                    const float th = thPrev + ch.crushInc;
                    auto& lo = ch.crushEng[static_cast<std::size_t>(ch.crushLo & 1)];
                    auto& hi = ch.crushEng[static_cast<std::size_t>(ch.crushHi & 1)];
                    const float yLo = lo.process(thPrev, th, ch.crushInc, ch.crushPrev, u[k]);
                    const float yHi = hi.process(thPrev, th, ch.crushInc, ch.crushPrev, u[k]);
                    ch.crushTheta = th >= 1.0f ? th - 1.0f : th;
                    ch.crushPrev = u[k];
                    const float y = (1.0f - ch.crushFrac) * yLo + ch.crushFrac * yHi;
                    lin[k] = 0.0f;   // l = 0: everything is residual
                    res[k] = y;
                }
                break;
            case BenchAlgo::Fracture:
                for (std::size_t k = 0; k < kBenchF; ++k) {
                    float phi = ch.fracPhi + ch.fracInc;   // phase kept for the re-key re-seed
                    if (phi >= 2.0f) phi -= 2.0f;
                    ch.fracPhi = phi;
                    // C-24: rotate (cos, sin)(pi * phi) by delta; sMid = sin(pi * phi - delta / 2).
                    const float rc = ch.fracRc * ch.fracCd - ch.fracRs * ch.fracSd;
                    const float rs = ch.fracRs * ch.fracCd + ch.fracRc * ch.fracSd;
                    ch.fracRc = rc;
                    ch.fracRs = rs;
                    const float s = rs;
                    const float sMid = rs * ch.fracCh - rc * ch.fracSh;
                    const float sigma = benchTanhR(4.0f * s) * kBenchInvTanh4;
                    const float a2 = ch.fracA;
                    const float v = ch.fracG * (u[k] * ((1.0f - a2) + a2 * sigma) / kBenchP + ch.fracBeta * s);
                    const float y =
                        (kBenchP / ch.fracG) * (ch.fracAdaa.process(v) - benchTanhR(ch.fracG * ch.fracBeta * sMid));
                    const float l = (1.0f - a2) * ch.fracC * (u[k] + ch.fracPrev) * 0.5f;
                    ch.fracPrev = u[k];
                    lin[k] = l;
                    res[k] = y - l;
                }
                break;
        }
        // C-23: post-tilt at the base rate on the round-tripped (phase-matched) output.
        const float zr = ch.path.downsample(lin.data(), res.data());
        float z = zr;
        switch (algo_) {
            case BenchAlgo::Tube: z = ch.tubePost.process(zr); break;
            case BenchAlgo::Fold: z = ch.foldPost.process(zr); break;
            case BenchAlgo::Rectify: z = ch.rectPost.process(zr); break;
            case BenchAlgo::Bite: z = ch.bitePost.process(zr); break;
            case BenchAlgo::Crush: z = ch.crushPost.process(zr); break;
            case BenchAlgo::Fracture: z = ch.fracPost.process(zr); break;
        }
        const float e = 1.0f;   // engaged (bypass ramp settled)
        float out = (1.0f - e) * x + e * z;
        out = std::clamp(out, -6.0f, 6.0f);
        peak_ = std::max(peak_, std::abs(out));
        return out;
    }

    std::array<BenchCharChannel, 2> ch_{};
    BenchCharTables tables_{};
    BenchAlgo algo_ = BenchAlgo::Tube;
    double fs_ = kBenchFs;
    float peak_ = 0.0f;
    bool linked_ = false;
};

// ---------------------------------------------------------------- S6 ShapeMorph prototype -----------------------
struct BenchShapeChannel {
    BenchResidualPath path;
    TanhADAA roundAdaa;
    SVF roundLp;
    SVF hollowNotch, hollowBand;
    // While BITE and HOLLOW are both on (s > 0.5) the shared round trip carries BITE's base-rate emphasis (C-23), so
    // HOLLOW's input lb is emphasised. This first-order de-emphasis stand-in (one OnePoleLP, the cost of the exact
    // first-order inverse shelf) restores it; the exact form is a T013 design item, surfaced in run-log/T001.txt.
    BenchKeytrackedTilt hollowDeemph;
    BenchAdaa2<BenchDiodeLaw, double> biteAdaa;
    BenchKeytrackedTilt biteTilt;
    float uPrev = 0.0f;
    bool roundOn = false, hollowOn = false, biteOn = false;
    float appliedS = -1.0f, appliedF0 = -1.0f;
    float gB = 1.0f, cB = 1.0f;
};

class BenchShapeMorph {
public:
    void prepare(double fs) {
        for (std::size_t i = 0; i < 65; ++i) {
            const double t2 = static_cast<double>(i) / 64.0;
            cBite_[i] = static_cast<float>(benchDiodeH1(std::pow(10.0, (-12.0 + 27.0 * t2) / 20.0)));
        }
        for (std::size_t i = 0; i < loudDb_.size(); ++i)   // model seed range 1.31...3.76 dB (S10.6); cost-equivalent
            loudDb_[i] = 1.31f + (3.76f - 1.31f) * static_cast<float>(i) / 100.0f;
        for (auto& ch : ch_) {
            ch.path.prepare(fs);
            ch.roundAdaa.setDrive(0.8f);
            // C-23: ROUND's low-pass, HOLLOW's notch + band, BITE's emphasis tilt all at the base rate fs.
            ch.roundLp.prepare(fs);
            ch.roundLp.setMode(SVFMode::Lowpass);
            ch.roundLp.setResonance(0.70710678f);
            ch.hollowNotch.prepare(fs);
            ch.hollowNotch.setResonance(1.0f);
            ch.hollowBand.prepare(fs);
            ch.hollowBand.setResonance(3.0f);
            ch.hollowDeemph.prepare(fs);
            ch.hollowDeemph.configure(4.0f, 12.0f);
            ch.biteTilt.prepare(fs);
            ch.biteTilt.configure(4.0f, 12.0f);
        }
        shapeSm_.configure(10.0f, static_cast<float>(fs));
    }
    void setLinked(bool linked) noexcept { linked_ = linked; }
    void setShape(float s) noexcept { shapeSm_.setTarget(s); }
    void snapControls() noexcept { shapeSm_.snapToTarget(); }

    void processBlock(float* left, float* right, std::size_t n, std::uint64_t absStart, const float* f0PerSample,
                      float f0Static) noexcept {
        std::array<float*, 2> io{left, right};
        for (std::size_t i = 0; i < n; ++i) {
            const float f0 = f0PerSample != nullptr ? f0PerSample[i] : f0Static;
            const bool tick = ((absStart + i) % kBenchCtrlInterval) == 0;
            const float s = shapeSm_.process();
            const float t = std::min(1.0f, 2.0f * s);
            const float t2 = std::max(0.0f, 2.0f * s - 1.0f);
            const float wR = s <= 0.5f ? 1.0f - t : 0.0f;
            const float wH = s <= 0.5f ? t : 1.0f - t2;
            const float wB = t2;
            if (tick && s != loudS_) {   // z *= 10^{L(s)/20}, recomputed on a changed s
                loudS_ = s;
                const float pos = s * 100.0f;
                const auto j = static_cast<std::size_t>(std::min(pos, 99.0f));
                const float fr = pos - static_cast<float>(j);
                loudGain_ = std::pow(10.0f, (loudDb_[j] + fr * (loudDb_[j + 1] - loudDb_[j])) / 20.0f);
            }
            const std::size_t nCh = linked_ ? 1 : 2;   // linked: channel 0 runs, channel 1 is its copy (plan S2)
            for (std::size_t c = 0; c < nCh; ++c) {
                auto& ch = ch_[c];
                bool rekey = false;   // a regime whose weight goes 0 -> > 0 is reset and re-keyed (S6.3)
                if ((wR > 0.0f) != ch.roundOn) {
                    ch.roundOn = wR > 0.0f;
                    if (ch.roundOn) {
                        ch.roundAdaa.reset();
                        ch.roundLp.reset();
                        rekey = true;
                    }
                }
                if ((wH > 0.0f) != ch.hollowOn) {
                    ch.hollowOn = wH > 0.0f;
                    if (ch.hollowOn) {
                        ch.hollowNotch.reset();
                        ch.hollowBand.reset();
                        ch.hollowDeemph.reset();
                        rekey = true;
                    }
                }
                if ((wB > 0.0f) != ch.biteOn) {
                    ch.biteOn = wB > 0.0f;
                    if (ch.biteOn) {
                        ch.biteAdaa.reset();
                        ch.biteTilt.reset();
                        ch.hollowDeemph.reset();
                        rekey = true;
                    }
                }
                if (rekey || (tick && (s != ch.appliedS || f0 != ch.appliedF0))) applyControls(ch, t, t2, s, f0);
                float& xs = io[c][i];
                const float x = xs;
                // C-23: BITE's emphasis tilt at the base rate before the upsample (w_R and w_B are never both > 0,
                // so at most one nonlinear regime shares the round trip with HOLLOW's linear path).
                const float xin = ch.biteOn ? ch.biteTilt.process(x) : x;
                std::array<float, kBenchF> u{}, lin{}, res{};
                ch.path.upsample(xin, u.data());
                const float wNl = ch.roundOn ? wR : (ch.biteOn ? wB : 0.0f);
                for (std::size_t k = 0; k < kBenchF; ++k) {   // only the nonlinearity runs at F*fs
                    float yNl = 0.0f;
                    if (ch.roundOn) {
                        yNl = (kBenchP / 0.8f) * ch.roundAdaa.process(u[k] / kBenchP);
                    } else if (ch.biteOn) {
                        const auto v = static_cast<double>(ch.gB * u[k] / kBenchP);
                        yNl = static_cast<float>(ch.biteAdaa.process(v)) * kBenchP / ch.cB;
                    }
                    const float l = ch.uPrev;   // l = 1 * u[n - 1] for every regime (unit linear tap)
                    ch.uPrev = u[k];
                    lin[k] = l;
                    res[k] = wNl * (yNl - l);
                }
                float lb = 0.0f, rb = 0.0f;
                ch.path.downsampleSplit(lin.data(), res.data(), lb, rb);
                const float nl = wNl * lb + rb;   // round-tripped, weighted nonlinear regime
                float z = 0.0f;
                if (ch.roundOn) z += ch.roundLp.process(nl);   // ROUND's low-pass at the base rate
                else if (ch.biteOn) z += nl;
                if (ch.hollowOn) {   // HOLLOW: base-rate filters on the decimated linear path, weighted by w_H
                    const float uh = ch.biteOn ? ch.hollowDeemph.process(lb) : lb;
                    z += wH * (ch.hollowNotch.processMulti(uh).notch + ch.hollowBand.processMulti(uh).band);
                }
                z *= loudGain_;
                const float e = 1.0f;   // engaged (C-22: the path runs warm regardless)
                float out = (1.0f - e) * x + e * z;
                out = std::clamp(out, -6.0f, 6.0f);
                peak_ = std::max(peak_, std::abs(out));
                xs = out;
            }
            if (linked_) right[i] = left[i];
        }
    }
    [[nodiscard]] float peak() const noexcept { return peak_; }

private:
    void applyControls(BenchShapeChannel& ch, float t, float t2, float s, float f0) noexcept {
        ch.appliedS = s;
        ch.appliedF0 = f0;
        if (ch.roundOn) ch.roundLp.setCutoff(10.0f * std::exp2(t) * f0);   // k_R = 10 * 2^t
        if (ch.hollowOn) {
            ch.hollowNotch.setCutoff(3.0f * f0);
            ch.hollowBand.setCutoff(9.0f * f0);
        }
        if (ch.biteOn) {
            ch.biteTilt.setTone(0.5f * (1.0f + t2));   // +12 * t2 dB emphasis around 4 * f0
            ch.biteTilt.setFundamentalHz(f0);
            if (ch.hollowOn) {   // de-emphasis stand-in for HOLLOW's input (cost-equivalent, see the channel)
                ch.hollowDeemph.setTone(0.5f * (1.0f - t2));
                ch.hollowDeemph.setFundamentalHz(f0);
            }
            ch.gB = std::pow(10.0f, (-12.0f + 27.0f * t2) / 20.0f);
            ch.cB = benchLerp(cBite_, t2);
        }
    }

    std::array<BenchShapeChannel, 2> ch_{};
    OnePoleSmoother shapeSm_;
    BenchTable cBite_{};
    std::array<float, 101> loudDb_{};
    float loudS_ = -1.0f, loudGain_ = 1.0f, peak_ = 0.0f;
    bool linked_ = false;
};

// ---------------------------------------------------------------- driver ------------------------------------------
constexpr std::size_t kBenchInputLen = 96000;   // 2 s, a multiple of 64
std::vector<float> gInL, gInR;
volatile float gSink = 0.0f;
bool gLinked = false;   // --linked: L == R input (R = copy of L), zero offsets, one channel processed

void makeInput() {
    gInL.assign(kBenchInputLen, 0.0f);
    gInR.assign(kBenchInputLen, 0.0f);
    std::vector<double> l(kBenchInputLen, 0.0), r(kBenchInputLen, 0.0);
    for (int k = 1; k <= 64; ++k) {
        const double w = 2.0 * kBenchPiD * k * kBenchE1 / kBenchFs;
        const double off = 2.0 * kBenchPiD * k * 0.37;   // R offset by 0.37 period
        for (std::size_t n = 0; n < kBenchInputLen; ++n) {
            l[n] += std::sin(w * static_cast<double>(n)) / k;
            r[n] += std::sin(w * static_cast<double>(n) + off) / k;
        }
    }
    double pk = 0.0;
    for (double v : l) pk = std::max(pk, std::abs(v));
    for (std::size_t n = 0; n < kBenchInputLen; ++n) {
        gInL[n] = static_cast<float>(l[n] / pk * kBenchP);
        gInR[n] = static_cast<float>(r[n] / pk * kBenchP);
    }
}

// Drop f0: 48 st -> 0 linear over 60 ms, re-triggered every 250 ms, held on the 16-sample grid.
constexpr std::size_t kBenchDropPeriod = 12000;   // 250 ms
std::vector<float> gDropF0;
void makeDropF0() {
    gDropF0.assign(kBenchDropPeriod, 0.0f);
    for (std::size_t n = 0; n < kBenchDropPeriod; ++n) {
        const std::size_t g = n - (n % 16);
        const double tt = static_cast<double>(g) / kBenchFs;
        const double st = tt < 0.060 ? 48.0 * (1.0 - tt / 0.060) : 0.0;
        gDropF0[n] = static_cast<float>(kBenchE1 * std::exp2(st / 12.0));
    }
}

double benchTriangle(double phase) {
    const double p = phase - std::floor(phase);
    return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p;
}

enum class CellKind { CharOnly, ShapeOnly, Ramp, Drop };

// Returns ns per base sample (stereo).
double runCell(CellKind kind, BenchAlgo algo, float shape, double seconds) {
    auto chr = std::make_unique<BenchCharacter>();
    auto shp = std::make_unique<BenchShapeMorph>();
    const bool useChar = kind != CellKind::ShapeOnly;
    const bool useShape = kind != CellKind::CharOnly;
    chr->setLinked(gLinked);
    shp->setLinked(gLinked);
    if (useChar) {
        chr->prepare(kBenchFs, algo);
        chr->setAmount(1.0f);
        chr->setTone(0.5f);
        chr->snapControls();
    }
    if (useShape) {
        shp->prepare(kBenchFs);
        shp->setShape(shape);
        shp->snapControls();
    }
    std::array<float, kBenchBlock> bl{}, br{}, f0Block{};
    const auto f0Static = static_cast<float>(kBenchE1);
    std::uint64_t absN = 0;
    std::size_t pos = 0;
    double sum = 0.0;
    auto block = [&]() {
        std::copy_n(gInL.data() + pos, kBenchBlock, bl.data());
        std::copy_n((gLinked ? gInL : gInR).data() + pos, kBenchBlock, br.data());
        const float* f0p = nullptr;
        if (kind == CellKind::Drop) {
            for (std::size_t i = 0; i < kBenchBlock; ++i) f0Block[i] = gDropF0[(absN + i) % kBenchDropPeriod];
            f0p = f0Block.data();
        }
        if (kind == CellKind::Ramp) {   // targets follow triangles: the smoothed values move every sample
            const double tsec = static_cast<double>(absN) / kBenchFs;
            chr->setAmount(static_cast<float>(0.9 + 0.1 * benchTriangle(tsec / 0.37)));
            chr->setTone(static_cast<float>(0.5 + 0.3 * benchTriangle(tsec / 0.29)));
            shp->setShape(std::clamp(static_cast<float>(shape + 0.1 * benchTriangle(tsec / 0.43)), 0.0f, 1.0f));
        }
        if (useShape) shp->processBlock(bl.data(), br.data(), kBenchBlock, absN, f0p, f0Static);
        if (useChar) chr->processBlock(bl.data(), br.data(), kBenchBlock, absN, f0p, f0Static);
        sum += static_cast<double>(bl[kBenchBlock - 1]) + static_cast<double>(br[kBenchBlock - 1]);
        absN += kBenchBlock;
        pos += kBenchBlock;
        if (pos >= kBenchInputLen) pos = 0;
    };
    for (int w = 0; w < 750; ++w) block();   // 1 s of audio warm-up (smoothers, DC, ADAA state)
    const std::uint64_t startAbs = absN;
    const auto t0 = std::chrono::steady_clock::now();
    double elapsed = 0.0;
    do {
        for (int w = 0; w < 64; ++w) block();
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    } while (elapsed < seconds);
    gSink = gSink + static_cast<float>(sum) + (useChar ? chr->peak() : 0.0f) + (useShape ? shp->peak() : 0.0f);
    return elapsed * 1.0e9 / static_cast<double>(absN - startAbs);
}

double pctCore(double ns) { return ns * 48000.0 / 1.0e7; }

}  // namespace

int main(int argc, char** argv) {
    _mm_setcsr(_mm_getcsr() | 0x8040u);   // FTZ | DAZ (host precondition, plan S8 / R-7)
    double seconds = 3.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quick") == 0) seconds = 0.2;
        else if (std::strcmp(argv[i], "--linked") == 0) gLinked = true;
        else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) seconds = std::atof(argv[++i]);
    }
    makeInput();
    makeDropF0();
    const char* mode = gLinked ? "linked" : "unlinked";
    std::printf("P5BENCH %s epoch=2 fs=48000 F=%zu block=%zu ctrl_interval=%llu rect_adaa=%s seconds_per_cell=%.2f\n",
                mode, kBenchF, kBenchBlock, static_cast<unsigned long long>(kBenchCtrlInterval), kBenchRectName,
                seconds);
    {   // C-24: measured error of the rational tanh (float evaluation) over a 10001-point grid on [-4, 4]
        double maxErr = 0.0, at = 0.0;
        for (int i = 0; i <= 10000; ++i) {
            const double xg = -4.0 + 8.0 * static_cast<double>(i) / 10000.0;
            const double err = std::abs(static_cast<double>(benchTanhR(static_cast<float>(xg))) - std::tanh(xg));
            if (err > maxErr) {
                maxErr = err;
                at = xg;
            }
        }
        std::printf("tanh_approx pade76 grid=10001 range=[-4,4] max_abs_err %.3e at x=%.4f (bar 1e-4)\n", maxErr, at);
    }

    double worstChar = 0.0;
    std::size_t worstCharIdx = 0;
    for (std::size_t a = 0; a < 6; ++a) {
        const double ns = runCell(CellKind::CharOnly, static_cast<BenchAlgo>(a), 0.0f, seconds);
        std::printf("cell char_%s ns_per_sample %.2f pct_core %.4f\n", kBenchAlgoNames[a], ns, pctCore(ns));
        std::fflush(stdout);
        if (ns > worstChar) {
            worstChar = ns;
            worstCharIdx = a;
        }
    }
    const std::array<float, 5> shapes{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    double worstShape = 0.0;
    float worstShapePos = 0.0f;
    for (float s : shapes) {
        const double ns = runCell(CellKind::ShapeOnly, BenchAlgo::Tube, s, seconds);
        std::printf("cell shape_%.2f ns_per_sample %.2f pct_core %.4f\n", static_cast<double>(s), ns, pctCore(ns));
        std::fflush(stdout);
        if (ns > worstShape) {
            worstShape = ns;
            worstShapePos = s;
        }
    }
    const auto wc = static_cast<BenchAlgo>(worstCharIdx);
    const double nsRamp = runCell(CellKind::Ramp, wc, worstShapePos, seconds);
    std::printf("cell ramp ns_per_sample %.2f pct_core %.4f (char_%s x shape_%.2f)\n", nsRamp, pctCore(nsRamp),
                kBenchAlgoNames[worstCharIdx], static_cast<double>(worstShapePos));
    std::fflush(stdout);
    const double nsDrop = runCell(CellKind::Drop, wc, worstShapePos, seconds);
    std::printf("cell drop ns_per_sample %.2f pct_core %.4f (char_%s x shape_%.2f)\n", nsDrop, pctCore(nsDrop),
                kBenchAlgoNames[worstCharIdx], static_cast<double>(worstShapePos));
    std::printf("worst_char char_%s pct_core %.4f\n", kBenchAlgoNames[worstCharIdx], pctCore(worstChar));
    std::printf("worst_shape shape_%.2f pct_core %.4f\n", static_cast<double>(worstShapePos), pctCore(worstShape));
    std::printf("total_%s pct_core %.4f (worst char + worst shape; bar 2.0 gates the linked total)\n", mode,
                pctCore(worstChar + worstShape));
    std::printf("sink %g\n", static_cast<double>(gSink));
    return 0;
}
