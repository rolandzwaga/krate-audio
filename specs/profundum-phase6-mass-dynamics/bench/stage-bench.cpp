// Scratch: per-stage CPU of the shipped Phase 6 headers under MSVC (same flags as dsp_systems_tests Release).
// Not evidence; a profile to locate the 0.77 % (clang prototype) -> 1.05-1.27 % (shipped, pinned) gap.
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/period_peak_detector.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Krate::DSP;

static constexpr double kFs = 48000.0;
static constexpr std::size_t kBlock = 64;
static constexpr double kE1 = 41.2034446;
static volatile float gSink = 0.0f;

static std::vector<float> saw(std::size_t n, std::size_t shift) {
    // Band-limited-ish saw at -12 dBFS RMS: 64 partials, enough for a load profile.
    std::vector<float> y(n + shift, 0.0f);
    for (std::size_t i = 0; i < y.size(); ++i) {
        double s = 0.0;
        const double t = static_cast<double>(i) / kFs;
        for (int k = 1; k <= 64; ++k) s += std::sin(6.283185307179586 * kE1 * k * t) / k;
        y[i] = static_cast<float>(s * 0.3);
    }
    return y;
}

template <typename F>
static double timePct(F&& fn, std::size_t blocks, int reps) {
    double best = 1e9;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t b = 0; b < blocks; ++b) fn(b);
        const auto t1 = std::chrono::steady_clock::now();
        const double audio = static_cast<double>(blocks * kBlock) / kFs;
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count() / audio * 100.0);
    }
    return best;
}

int main(int argc, char** argv) {
    const double seconds = (argc > 1) ? std::atof(argv[1]) : 5.0;
    const int reps = 3;
    const std::size_t periodS = static_cast<std::size_t>(std::llround(kFs / kE1));
    const std::size_t loop = 41 * periodS;
    const std::size_t shift = static_cast<std::size_t>(std::llround(0.37 * kFs / kE1));
    const std::vector<float> src = saw(loop, shift);
    std::vector<float> L(src.begin() + shift, src.begin() + shift + loop);
    std::vector<float> R(src.begin(), src.begin() + loop);
    const std::size_t blocks = static_cast<std::size_t>(seconds * kFs) / kBlock;
    std::vector<float> l(kBlock), r(kBlock);
    std::size_t pos = 0;
    auto fill = [&]() {
        for (std::size_t i = 0; i < kBlock; ++i) { l[i] = L[pos]; r[i] = R[pos]; pos = (pos + 1) % loop; }
    };

    std::printf("P6 stage bench fs=%.0f block=%zu seconds=%.1f reps=%d (pct of one core)\n", kFs, kBlock, seconds, reps);

    {   // copy baseline (fill only)
        const double p = timePct([&](std::size_t) { fill(); gSink += l[0]; }, blocks, reps);
        std::printf("cell fill_only pct %.4f\n", p);
    }
    {
        TransientShaper sh;
        sh.setAttackGainDb(12.0f); sh.setSustainGainDb(-6.0f); sh.setTransientDurationMs(40.0f);
        sh.prepare(kFs, kBlock);
        const double p = timePct([&](std::size_t) { fill(); sh.processBlock(l.data(), r.data(), kBlock); gSink += l[0]; }, blocks, reps);
        std::printf("cell shaper pct %.4f\n", p);
    }
    {
        BassCompressor c;
        c.setAmount(1.0f); c.setDetectorKeyHz(static_cast<float>(kE1));
        c.prepare(kFs, kBlock);
        const double p = timePct([&](std::size_t) { fill(); c.processBlock(l.data(), r.data(), kBlock); gSink += l[0]; }, blocks, reps);
        std::printf("cell compressor pct %.4f\n", p);
    }
    {
        PeriodPeakDetector d;
        d.prepare(kFs, 50.0f);
        const double p = timePct([&](std::size_t) { fill(); float acc = 0.0f; for (std::size_t i = 0; i < kBlock; ++i) acc += d.process(l[i], r[i]); gSink += acc; }, blocks, reps);
        std::printf("cell period_peak_detector_stereo pct %.4f\n", p);
    }
    {
        EnvelopeFollower f; f.prepare(kFs, 0); f.setMode(DetectionMode::Amplitude); f.setAttackTime(0.5f); f.setReleaseTime(40.0f);
        const double p = timePct([&](std::size_t) { fill(); float acc = 0.0f; for (std::size_t i = 0; i < kBlock; ++i) acc += f.processSample(std::abs(l[i])); gSink += acc; }, blocks, reps);
        std::printf("cell envelope_follower_one pct %.4f\n", p);
    }
    {
        DynamicsProcessor dp; dp.prepare(kFs, kBlock); dp.setLookahead(0.0f); dp.setThreshold(-11.0f); dp.setRatio(3.0f); dp.setKneeWidth(6.0f); dp.setAttackTime(10.0f); dp.setReleaseTime(50.0f);
        const double p = timePct([&](std::size_t) { fill(); float acc = 0.0f; for (std::size_t i = 0; i < kBlock; ++i) { (void)dp.processSample(std::abs(l[i])); acc += dp.getCurrentGainReduction(); } gSink += acc; }, blocks, reps);
        std::printf("cell dynamics_processor_sample pct %.4f\n", p);
    }
    {
        const double p = timePct([&](std::size_t) { fill(); float acc = 0.0f; for (std::size_t i = 0; i < kBlock; ++i) { const float db = gainToDb(std::abs(l[i]) + 1e-3f); acc += dbToGain(db * 0.5f); } gSink += acc; }, blocks, reps);
        std::printf("cell gainToDb_plus_dbToGain pct %.4f\n", p);
    }
    {
        MassDynamics md;
        md.setTransientAttackDb(12.0f); md.setTransientSustainDb(-6.0f); md.setTransientDurationMs(40.0f);
        md.setCompressionAmount(1.0f); md.setDetectorKeyHz(static_cast<float>(kE1));
        md.prepare(kFs, kBlock);
        const double p = timePct([&](std::size_t) { fill(); md.processBlock(l.data(), r.data(), kBlock); gSink += l[0]; }, blocks, reps);
        std::printf("cell mass_dynamics pct %.4f\n", p);
    }
    {
        MassLaw law; law.setMass(75.0f); law.prepare(kFs);
        MassLaw::Targets t{};
        const double p = timePct([&](std::size_t) { fill(); for (std::size_t off = 0; off < kBlock; off += 32) { const float m = law.advanceInterval(); t = MassLaw::evaluate(m, law.base()); } gSink += t.depth; }, blocks, reps);
        std::printf("cell mass_law_per_interval pct %.4f\n", p);
    }
    {
        MassDynamics md;
        md.setTransientAttackDb(12.0f); md.setTransientSustainDb(-6.0f); md.setTransientDurationMs(40.0f);
        md.setCompressionAmount(1.0f); md.setDetectorKeyHz(static_cast<float>(kE1));
        md.prepare(kFs, kBlock);
        MassLaw law; law.setMass(75.0f); law.prepare(kFs);
        MassLaw::Targets t{};
        const double p = timePct([&](std::size_t) { fill(); for (std::size_t off = 0; off < kBlock; off += 32) { const float m = law.advanceInterval(); t = MassLaw::evaluate(m, law.base()); } md.processBlock(l.data(), r.data(), kBlock); gSink += l[0] + t.depth; }, blocks, reps);
        std::printf("cell total_gated pct %.4f\n", p);
    }
    std::printf("sink %g\n", static_cast<double>(gSink));
    return 0;
}
