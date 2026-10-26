// Profundum Phase 7, task T001 (plan R-1, S12.6, S16 step 1): throwaway CPU microbench of the per-voice pan forwarding
// and the global safety stage, written BEFORE any production header exists. It includes the real reused KrateDSP
// headers and prototypes the new pieces locally (anonymous namespace, Bench prefix) exactly as plan S4-S7 specify:
//   BenchLaw    - plan S4.1 per-interval precompute (log2(f_c,s), uLow, invSpan, gamma = 4^curve, magnitudeScale with
//                 kappa = 0.5) plus the S5 Cycle advance (LFO prepared at fs/32, Sine) and the 20 ms control-rate f_c
//                 smoother; S4.2 / S4.3 per-partial target and slew over k = 1 ... N-1 (log2 table, x, c, s, +0 for
//                 zero entries, Delta = 0.003 slew, bit-compare handoff count)
//   BenchStage  - plan S7.1 per-interval targets (2 f_c,s handed to CrossoverLR4, side gain / mode ramp targets only on
//                 a change) with BrownianDrift::processBlock(32) and one LFO::process() per interval; S7.3 per-sample
//                 loop (non-finite guard, M/S encode, LinearRamp k, OnePoleSmoother g, one CrossoverLR4 on S in
//                 Efficient mode at 0.1 ms smoothing, decode, mask)
//
// Cells (48 kHz unless stated, host block 64, kSawAnchor, 64 partials, every run split on the 32-sample grid):
//   1 base_1v          one ProfundumCore at E1, setPartialPanOffsets(allZero) every interval (baseline)
//   2 law_1v           the same core with BenchLaw at Spread 1, Cycle depth 1 at 1 Hz, the vector handed every interval
//   3a base_4v         cell 1 with four cores at E1, E2, C3, A1
//   3b law_4v          cell 2 with four cores at E1, E2, C3, A1 (one global advance, four per-voice queries)
//   4 stage_static     BenchStage + MOTION advance, Gravity 1, static crossover 120 Hz, stereo distinct L / R
//                      (a kSawAnchor E1 ProfundumCore render, R delayed by 0.37 period)
//   5 stage_sweep      cell 4 with the crossover control a triangle 60 <-> 250 Hz, 100 ms legs, through the 20 ms law
//                      smoother (the stage recomputes coefficients every interval)
//   6 stage_sweep_192k cell 5 at 192 kHz (recorded only)
// Output: "cell <name> ns_per_sample <ns> pct_core <pct>" with pct = ns * 48000 / 1e7 (cell 6: ns * 192000 / 1e7),
// ns per host (stereo) sample. Per voice = law_1v - base_1v; four voices = law_4v - base_4v (run-bench.sh).
// "info" lines report the handoff count per query (proves the worst cadence: one changed vector per interval).
// Flags: --quick (0.2 s per cell, smoke only), --seconds <s>.
// Compile: see run-bench.sh (clang++ -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -fms-runtime-lib=dll
// -Idsp/include -I<highway-src>, plus harmonic_oscillator_bank_simd.cpp and the Release hwy.lib: ProfundumCore's bank
// calls processMcfBatchSIMD, which is not header-only).

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/crossover_filter.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <xmmintrin.h>

namespace {

using namespace Krate::DSP;

constexpr double kBenchFs = 48000.0;
constexpr double kBenchFsHigh = 192000.0;
constexpr std::size_t kBenchBlock = 64;
constexpr std::size_t kBenchInterval = 32;   // == ProfundumCore::kControlInterval (plan S3)
constexpr int kBenchPartials = 64;
constexpr float kBenchE1Hz = 41.20344461410875f;
constexpr float kBenchE2Hz = 82.4068892282175f;
constexpr float kBenchC3Hz = 130.8127826502993f;
constexpr float kBenchA1Hz = 55.0f;

static_assert(kBenchInterval == ProfundumCore::kControlInterval, "bench grid must equal the core's control interval");

// Plan S3 constants (prototyped values).
constexpr float kBenchUpperKneeOctaves = 2.0f;
constexpr float kBenchLowerKneeMaxOctaves = 0.25f;
constexpr float kBenchMaxPanStep = 0.003f;          // C-6
constexpr float kBenchMotionExcursion = 0.5f;       // kappa, Q7
constexpr float kBenchStageMultiple = 2.0f;         // Q3, FR-024
constexpr float kBenchSideFloorGain = 0.5f;         // g_floor seed (C-11)
constexpr float kBenchSideGainSmoothMs = 10.0f;     // FR-027
constexpr float kBenchModeCrossfadeMs = 10.0f;      // FR-052
constexpr float kBenchCrossoverSmoothMs = 20.0f;    // law f_c smoothing (control rate)
constexpr float kBenchPi = 3.14159265358979323846f;

volatile float gSink = 0.0f;

[[nodiscard]] inline bool benchSameBits(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// ------------------------------------------------------------------------------------------------------------------
// BenchLaw (plan S4.1 - S4.3, S5 Cycle)
// ------------------------------------------------------------------------------------------------------------------
struct BenchVoiceState {
    std::array<float, kMaxPartials> previous{};
    float lastF0Hz = 55.0f;
    std::uint64_t handoffCount = 0;
    std::uint64_t queries = 0;
    bool primed = false;
};

class BenchLaw {
public:
    BenchLaw() noexcept {
        for (std::size_t k = 0; k < kMaxPartials; ++k)
            log2Harmonic_[k] = std::log2(static_cast<float>(k + 1));
    }

    void prepare(double fs) noexcept {
        const float controlRate = static_cast<float>(fs / static_cast<double>(kBenchInterval));
        fcSmoother_.configure(kBenchCrossoverSmoothMs, controlRate);
        fcSmoother_.snapTo(crossoverHz_);
        lfo_.prepare(fs / static_cast<double>(kBenchInterval));
        lfo_.setWaveform(Waveform::Sine);
        lfo_.setFrequency(1.0f);   // Cycle at 1 Hz (SC-013(b) worst cadence)
        lfo_.reset();
    }

    // S4.1 + S5: once per interval, globally.
    void advanceControl() noexcept {
        fcSmoother_.setTarget(crossoverHz_);
        fcS_ = fcSmoother_.process();
        const float m = lfo_.process();
        const float lfc = std::log2(fcS_);
        uLow_ = lfc - kBenchLowerKneeMaxOctaves * (1.0f - gravity_);
        const float uHigh = lfc + kBenchUpperKneeOctaves;
        invSpan_ = 1.0f / (uHigh - uLow_);
        gamma_ = std::exp2(2.0f * curve_);
        const float km = kBenchMotionExcursion * depth_;
        magnitudeScale_ = spread_ * (1.0f - km + km * m);
    }

    // S4.2 + S4.3: per voice.
    void computePanVector(BenchVoiceState& st, float f0Hz, std::array<float, kMaxPartials>& out,
                          int numPartials) const noexcept {
        if (detail::isFinite(f0Hz) && f0Hz > 0.0f)
            st.lastF0Hz = f0Hz;
        const float lf0 = std::log2(st.lastF0Hz);
        const auto n = static_cast<std::size_t>(std::clamp(numPartials, 1, static_cast<int>(kMaxPartials)));
        out[0] = 0.0f;   // h1 centred (FR-013)
        for (std::size_t k = 1; k < n; ++k) {
            const float x = (lf0 + log2Harmonic_[k] - uLow_) * invSpan_;
            float c;
            if (x <= 0.0f)
                c = 0.0f;
            else if (x >= 1.0f)
                c = 1.0f;
            else
                c = 0.5f - 0.5f * std::cos(kBenchPi * std::exp2(gamma_ * std::log2(x)));
            const float s = (k % 2 == 0) ? -1.0f : 1.0f;
            const float target = (c > 0.0f && magnitudeScale_ > 0.0f) ? s * magnitudeScale_ * c : 0.0f;   // +0
            if (!st.primed) {
                out[k] = target;
            } else {
                const float d = target - st.previous[k];
                out[k] = std::abs(d) <= kBenchMaxPanStep ? target : st.previous[k] + std::copysign(kBenchMaxPanStep, d);
            }
        }
        for (std::size_t k = n; k < kMaxPartials; ++k)
            out[k] = 0.0f;
        bool changed = false;
        for (std::size_t k = 0; k < kMaxPartials; ++k)
            changed = changed || !benchSameBits(out[k], st.previous[k]);
        if (changed)
            ++st.handoffCount;
        ++st.queries;
        st.primed = true;
        st.previous = out;
    }

    float spread_ = 1.0f;
    float gravity_ = 0.5f;
    float curve_ = 0.5f;
    float depth_ = 1.0f;
    float crossoverHz_ = 120.0f;

private:
    std::array<float, kMaxPartials> log2Harmonic_{};
    OnePoleSmoother fcSmoother_;
    LFO lfo_;
    float fcS_ = 120.0f;
    float uLow_ = 0.0f;
    float invSpan_ = 0.5f;
    float gamma_ = 1.0f;
    float magnitudeScale_ = 0.0f;
};

// ------------------------------------------------------------------------------------------------------------------
// BenchStage (plan S7.1 - S7.3, S5 advance)
// ------------------------------------------------------------------------------------------------------------------
class BenchStage {
public:
    void prepare(double fs) noexcept {
        const float controlRate = static_cast<float>(fs / static_cast<double>(kBenchInterval));
        fcSmoother_.configure(kBenchCrossoverSmoothMs, controlRate);
        fcSmoother_.snapTo(crossoverHz_);
        fcS_ = crossoverHz_;
        crossover_.setSmoothingTime(kMinSmoothingTimeMs);
        crossover_.setTrackingMode(TrackingMode::Efficient);
        crossover_.setCrossoverFrequency(kBenchStageMultiple * fcS_);
        crossover_.prepare(fs);
        drift_.prepare(fs);
        drift_.setSeed(BrownianDrift::kDefaultDriftSeed);
        drift_.setDepth(1.0f);
        drift_.setMean(0.0f);
        const float tau = 30.0f * std::pow(0.5f / 30.0f, 0.3f);   // S5, motionRate 0.3
        drift_.setSmoothness((tau - 0.2f) / 29.8f);
        lfo_.prepare(fs / static_cast<double>(kBenchInterval));
        lfo_.setWaveform(Waveform::Sine);
        lfo_.setFrequency(1.0f);
        lfo_.reset();
        sideGainSm_.configure(kBenchSideGainSmoothMs, static_cast<float>(fs));
        modeRamp_.configure(kBenchModeCrossfadeMs, static_cast<float>(fs));
        drift_.reset();
        sideGainSm_.snapTo(kBenchSideFloorGain * (1.0f - gravity_));
        modeRamp_.snapTo(1.0f);   // SPREAD
        lastSideTarget_ = sideGainSm_.getTarget();
        lastModeTarget_ = 1.0f;
        sideIdle_ = false;
        m_ = 0.0f;
    }

    // S7.1 + S5: once per interval.
    void advanceControl(float crossoverControlHz) noexcept {
        fcSmoother_.setTarget(crossoverControlHz);
        fcS_ = fcSmoother_.process();
        drift_.processBlock(kBenchInterval);
        const float driftValue = drift_.getCurrentValue();
        const float cycleValue = lfo_.process();
        cycleValue_ = cycleValue;
        m_ = driftValue;   // source = Drift (the default); both sources advance every interval (S5)
        crossover_.setCrossoverFrequency(kBenchStageMultiple * fcS_);
        const float sideTarget = kBenchSideFloorGain * (1.0f - gravity_);
        if (!benchSameBits(sideTarget, lastSideTarget_)) {
            sideGainSm_.setTarget(sideTarget);
            lastSideTarget_ = sideTarget;
        }
        const float modeTarget = 1.0f;   // SPREAD
        if (!benchSameBits(modeTarget, lastModeTarget_)) {
            modeRamp_.setTarget(modeTarget);
            lastModeTarget_ = modeTarget;
            sideIdle_ = false;
        }
    }

    // S7.3 per sample.
    void processRun(float* left, float* right, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            const float lIn = left[i];
            const float rIn = right[i];
            const bool badL = !detail::isFinite(lIn);
            const bool badR = !detail::isFinite(rIn);
            const float l = badL ? 0.0f : lIn;
            const float r = badR ? 0.0f : rIn;
            const float mid = (l + r) * 0.5f;
            const float side = (l - r) * 0.5f;
            const float k = modeRamp_.process();
            const float g = sideGainSm_.process();
            float sp = 0.0f;
            if (!sideIdle_) {
                const auto bands = crossover_.process(side);
                sp = k * (g * bands.low + bands.high);
            }
            const float outL = mid + sp;
            const float outR = mid - sp;
            if (!sideIdle_ && k == 0.0f && modeRamp_.getTarget() == 0.0f) {
                sideIdle_ = true;
                crossover_.reset();
            }
            left[i] = badL ? 0.0f : outL;
            right[i] = badR ? 0.0f : outR;
        }
    }

    [[nodiscard]] float motion() const noexcept { return m_ + cycleValue_; }
    [[nodiscard]] float smoothedCrossoverHz() const noexcept { return fcS_; }

    float gravity_ = 1.0f;
    float crossoverHz_ = 120.0f;

private:
    OnePoleSmoother fcSmoother_;
    CrossoverLR4 crossover_;
    BrownianDrift drift_;
    LFO lfo_;
    OnePoleSmoother sideGainSm_;
    LinearRamp modeRamp_;
    float fcS_ = 120.0f;
    float m_ = 0.0f;
    float cycleValue_ = 0.0f;
    float lastSideTarget_ = 0.0f;
    float lastModeTarget_ = 1.0f;
    bool sideIdle_ = false;
};

// ------------------------------------------------------------------------------------------------------------------
// Cores and input
// ------------------------------------------------------------------------------------------------------------------
std::unique_ptr<ProfundumCore> makeCore(double fs, float f0) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs, kBenchPartials);
    core->setControls(SpectralShapeRecipe::kSawAnchor);
    core->setFrequency(f0);
    core->noteOn(f0);
    return core;
}

// A kSawAnchor E1 render (L), R = L delayed by 0.37 period (cyclic over ~103 periods).
struct BenchInput {
    std::vector<float> l;
    std::vector<float> r;
};

BenchInput makeStageInput(double fs) {
    const double period = fs / static_cast<double>(kBenchE1Hz);
    const auto len = static_cast<std::size_t>(std::lround(103.0 * period));
    const auto delay = static_cast<std::size_t>(std::lround(0.37 * period));
    BenchInput in;
    in.l.assign(len, 0.0f);
    in.r.assign(len, 0.0f);
    auto core = makeCore(fs, kBenchE1Hz);
    std::vector<float> scratchR(len, 0.0f);
    // Settle 0.5 s, then render the loop.
    std::array<float, kBenchBlock> wl{};
    std::array<float, kBenchBlock> wr{};
    const auto settleBlocks = static_cast<std::size_t>(fs * 0.5) / kBenchBlock;
    for (std::size_t b = 0; b < settleBlocks; ++b)
        core->processBlock(wl.data(), wr.data(), kBenchBlock);
    for (std::size_t off = 0; off < len; off += kBenchBlock) {
        const std::size_t run = std::min(kBenchBlock, len - off);
        core->processBlock(in.l.data() + off, scratchR.data() + off, run);
    }
    for (std::size_t n = 0; n < len; ++n)
        in.r[n] = in.l[(n + len - delay) % len];
    return in;
}

// ------------------------------------------------------------------------------------------------------------------
// Timing harness
// ------------------------------------------------------------------------------------------------------------------
template <typename BlockFn>
double timeBlocks(double fs, double seconds, BlockFn&& block) {
    const auto warmBlocks = static_cast<int>(fs / static_cast<double>(kBenchBlock));   // 1 s warm-up
    for (int w = 0; w < warmBlocks; ++w)
        block();
    std::uint64_t samples = 0;
    const auto t0 = std::chrono::steady_clock::now();
    double elapsed = 0.0;
    do {
        for (int w = 0; w < 64; ++w)
            block();
        samples += 64u * kBenchBlock;
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    } while (elapsed < seconds);
    return elapsed * 1.0e9 / static_cast<double>(samples);
}

struct LawCellResult {
    double ns = 0.0;
    std::uint64_t handoffs = 0;
    std::uint64_t queries = 0;
};

// Cells 1, 2, 3a, 3b.
LawCellResult runCoreCell(const float* f0s, std::size_t numVoices, bool withLaw, double seconds) {
    std::vector<std::unique_ptr<ProfundumCore>> cores;
    for (std::size_t v = 0; v < numVoices; ++v)
        cores.push_back(makeCore(kBenchFs, f0s[v]));
    auto law = std::make_unique<BenchLaw>();
    law->prepare(kBenchFs);
    std::vector<BenchVoiceState> states(numVoices);
    std::array<float, kMaxPartials> pan{};
    const std::array<float, kMaxPartials> allZero{};
    std::array<float, kBenchBlock> mixL{};
    std::array<float, kBenchBlock> mixR{};
    std::array<float, kBenchBlock> vl{};
    std::array<float, kBenchBlock> vr{};
    double sum = 0.0;

    auto block = [&] {
        mixL.fill(0.0f);
        mixR.fill(0.0f);
        for (std::size_t off = 0; off < kBenchBlock; off += kBenchInterval) {
            if (withLaw)
                law->advanceControl();   // once per interval, globally, before any voice
            for (std::size_t v = 0; v < numVoices; ++v) {
                if (withLaw) {
                    law->computePanVector(states[v], f0s[v], pan, kBenchPartials);
                    cores[v]->setPartialPanOffsets(pan);
                } else {
                    cores[v]->setPartialPanOffsets(allZero);
                }
                cores[v]->processBlock(vl.data() + off, vr.data() + off, kBenchInterval);
                for (std::size_t i = off; i < off + kBenchInterval; ++i) {
                    mixL[i] += vl[i];
                    mixR[i] += vr[i];
                }
            }
        }
        sum += static_cast<double>(mixL[kBenchBlock - 1]) + static_cast<double>(mixR[kBenchBlock - 1]);
    };
    LawCellResult res;
    res.ns = timeBlocks(kBenchFs, seconds, block);
    for (const auto& s : states) {
        res.handoffs += s.handoffCount;
        res.queries += s.queries;
    }
    gSink = gSink + static_cast<float>(sum) + pan[5];
    return res;
}

// Cells 4, 5, 6.
double runStageCell(double fs, bool sweep, double seconds) {
    const BenchInput in = makeStageInput(fs);
    auto stage = std::make_unique<BenchStage>();
    stage->gravity_ = 1.0f;
    stage->crossoverHz_ = sweep ? 60.0f : 120.0f;
    stage->prepare(fs);
    const double intervalSec = static_cast<double>(kBenchInterval) / fs;
    std::uint64_t intervalCount = 0;
    std::size_t pos = 0;
    std::array<float, kBenchBlock> l{};
    std::array<float, kBenchBlock> r{};
    double sum = 0.0;

    auto block = [&] {
        for (std::size_t i = 0; i < kBenchBlock; ++i) {
            l[i] = in.l[pos];
            r[i] = in.r[pos];
            if (++pos >= in.l.size())
                pos = 0;
        }
        for (std::size_t off = 0; off < kBenchBlock; off += kBenchInterval) {
            float control = 120.0f;
            if (sweep) {
                // Triangle 60 <-> 250 Hz, 100 ms legs (200 ms period).
                const double t = std::fmod(static_cast<double>(intervalCount) * intervalSec, 0.2);
                const double tri = t < 0.1 ? t / 0.1 : 2.0 - t / 0.1;
                control = static_cast<float>(60.0 + 190.0 * tri);
            }
            ++intervalCount;
            stage->advanceControl(control);
            stage->processRun(l.data() + off, r.data() + off, kBenchInterval);
        }
        sum += static_cast<double>(l[kBenchBlock - 1]) + static_cast<double>(r[kBenchBlock - 1]);
    };
    const double ns = timeBlocks(fs, seconds, block);
    gSink = gSink + static_cast<float>(sum) + stage->motion() + stage->smoothedCrossoverHz();
    return ns;
}

double pctCore(double ns, double fs) { return ns * fs / 1.0e7; }

}  // namespace

int main(int argc, char** argv) {
    _mm_setcsr(_mm_getcsr() | 0x8040u);   // FTZ | DAZ (host precondition)
    double seconds = 3.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quick") == 0)
            seconds = 0.2;
        else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            seconds = std::atof(argv[++i]);
    }
    std::printf("P7BENCH fs=48000 block=%zu interval=%zu partials=%d seconds_per_cell=%.2f\n", kBenchBlock,
                kBenchInterval, kBenchPartials, seconds);
    std::fflush(stdout);

    const std::array<float, 4> f0s{kBenchE1Hz, kBenchE2Hz, kBenchC3Hz, kBenchA1Hz};

    struct CoreCell {
        const char* name;
        std::size_t voices;
        bool law;
    };
    constexpr std::array<CoreCell, 4> kCoreCells{{{"base_1v", 1, false},
                                                  {"law_1v", 1, true},
                                                  {"base_4v", 4, false},
                                                  {"law_4v", 4, true}}};
    for (const auto& c : kCoreCells) {
        const LawCellResult res = runCoreCell(f0s.data(), c.voices, c.law, seconds);
        std::printf("cell %s ns_per_sample %.2f pct_core %.4f\n", c.name, res.ns, pctCore(res.ns, kBenchFs));
        if (c.law)
            std::printf("info %s handoffs %llu queries %llu\n", c.name,
                        static_cast<unsigned long long>(res.handoffs), static_cast<unsigned long long>(res.queries));
        std::fflush(stdout);
    }

    struct StageCell {
        const char* name;
        double fs;
        bool sweep;
    };
    constexpr std::array<StageCell, 3> kStageCells{{{"stage_static", kBenchFs, false},
                                                    {"stage_sweep", kBenchFs, true},
                                                    {"stage_sweep_192k", kBenchFsHigh, true}}};
    for (const auto& c : kStageCells) {
        const double ns = runStageCell(c.fs, c.sweep, seconds);
        std::printf("cell %s ns_per_sample %.2f pct_core %.4f\n", c.name, ns, pctCore(ns, c.fs));
        std::fflush(stdout);
    }
    std::printf("sink %g\n", static_cast<double>(gSink));
    return 0;
}
