// T1.16 (MB-017): the Feedback exciter must run its modal bank below the
// soft-clip rail, like every other exciter, and Feedback Amount must set the
// sustain.
//
// Before the fix a Feedback voice skipped the strike normalisation: the bank
// ran at unity output gain into a 0.707 soft clip, the raw modal sum sat far
// above it, and the voice output was a square pinned at 0.707 x velocity
// (crest 0 dB) whatever the body or the Feedback Amount.

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"

#include <signal_metrics.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace Membrum;

namespace {

constexpr double kSR    = 48000.0;
constexpr int    kBlock = 64;

// Pad 0 as an isolated modal body driven by the given exciter, every other
// layer / character stage OFF (the configureBodyOnly idea of
// test_body_headroom_budget.cpp, with the exciter as a parameter).
void configureBodyOnly(VoicePool& pool, ExciterType exciter, BodyModelType body,
                       float feedbackAmount)
{
    pool.setPadConfigSelector(0, kPadExciterType, static_cast<int>(exciter));
    pool.setPadConfigSelector(0, kPadBodyModel,   static_cast<int>(body));
    pool.setPadConfigField(0, kPadMaterial,        0.30f);
    pool.setPadConfigField(0, kPadSize,            0.80f);
    pool.setPadConfigField(0, kPadDecay,           0.85f);
    pool.setPadConfigField(0, kPadStrikePosition,  0.30f);
    pool.setPadConfigField(0, kPadLevel,           1.0f);
    pool.setPadConfigField(0, kPadFeedbackAmount,  feedbackAmount);
    pool.setPadConfigField(0, kPadNoiseLayerMix,    0.0f);
    pool.setPadConfigField(0, kPadClickLayerMix,    0.0f);
    pool.setPadConfigField(0, kPadSecondaryEnabled, 0.0f);
    pool.setPadConfigField(0, kPadCouplingStrength, 0.0f);
    pool.setPadConfigField(0, kPadCouplingAmount,   0.0f);
    pool.setPadConfigField(0, kPadNonlinearCoupling,0.0f);
    pool.setPadConfigField(0, kPadModeInjectAmount, 0.0f);
    pool.setPadConfigField(0, kPadTSDriveAmount,    0.0f);
    pool.setPadConfigField(0, kPadTSFoldAmount,     0.0f);
    pool.setPadConfigField(0, kPadTSPitchEnvTime,   0.0f);
    pool.setPadConfigField(0, kPadTensionModAmt,    0.0f);
    pool.setPadConfigField(0, kPadMorphEnabled,     0.0f);
    pool.setPadConfigField(0, kPadEnabled,          1.0f);
}

struct Render
{
    std::vector<float> out;
    // Active voice count after each block (index = block number).
    std::vector<int> activeAfterBlock;
};

// One hit on pad 0 (no note-off), `seconds` long.
Render renderHit(BodyModelType body, float velocity, float feedbackAmount, double seconds,
                 ExciterType exciter = ExciterType::Feedback)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setMaxPolyphony(1);
    configureBodyOnly(pool, exciter, body, feedbackAmount);
    pool.noteOn(36, velocity);

    const int total = static_cast<int>(seconds * kSR);
    Render r;
    r.out.assign(static_cast<std::size_t>(total), 0.0f);
    std::vector<float> outL(kBlock), outR(kBlock);
    for (int s = 0; s < total; s += kBlock) {
        pool.processBlock(outL.data(), outR.data(), kBlock);
        const int n = std::min(kBlock, total - s);
        std::copy_n(outL.begin(), n, r.out.begin() + s);
        r.activeAfterBlock.push_back(pool.getActiveVoiceCount());
    }
    return r;
}

float peakOf(const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x) p = std::max(p, std::abs(v));
    return p;
}

float rmsOver(const std::vector<float>& x, double t0, double t1)
{
    const auto a = static_cast<std::size_t>(t0 * kSR);
    const auto b = std::min(x.size(), static_cast<std::size_t>(t1 * kSR));
    double acc = 0.0;
    for (std::size_t i = a; i < b; ++i) acc += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(acc / static_cast<double>(b - a)));
}

// Time (s) at which the 50 ms RMS envelope (5 ms hop) first falls 40 dB below
// its maximum, after that maximum. Returns the render length when it never does.
double fall40dbTime(const std::vector<float>& x)
{
    const auto win = static_cast<std::size_t>(0.050 * kSR);
    const auto hop = static_cast<std::size_t>(0.005 * kSR);
    std::vector<float> env;
    for (std::size_t s = 0; s + win <= x.size(); s += hop) {
        double acc = 0.0;
        for (std::size_t i = s; i < s + win; ++i) acc += static_cast<double>(x[i]) * x[i];
        env.push_back(static_cast<float>(std::sqrt(acc / static_cast<double>(win))));
    }
    const auto maxIt = std::max_element(env.begin(), env.end());
    const float floorLevel = *maxIt * 0.01f;  // -40 dB
    for (auto it = maxIt; it != env.end(); ++it)
        if (*it <= floorLevel)
            return static_cast<double>(std::distance(env.begin(), it)) * 0.005;
    return static_cast<double>(x.size()) / kSR;
}

// Time (s) at the end of the first block after which no voice is active; the
// render length when the voice never retired.
double retireTime(const Render& r)
{
    for (std::size_t b = 0; b < r.activeAfterBlock.size(); ++b)
        if (r.activeAfterBlock[b] == 0)
            return static_cast<double>((b + 1) * kBlock) / kSR;
    return static_cast<double>(r.out.size()) / kSR;
}

const char* bodyName(BodyModelType b)
{
    switch (b) {
    case BodyModelType::Membrane:  return "Membrane";
    case BodyModelType::Plate:     return "Plate";
    case BodyModelType::Shell:     return "Shell";
    case BodyModelType::Bell:      return "Bell";
    case BodyModelType::NoiseBody: return "NoiseBody";
    default:                       return "other";
    }
}

} // namespace

TEST_CASE("Feedback exciter: voice stays under the body budget and off the clip rail on every modal body",
          "[feedback_exciter][gain_staging]")
{
    // kBodyHeadroom (0.5) x level (1.0) x 1.12.
    constexpr float kPeakBar = 0.56f;
    const BodyModelType bodies[] = {BodyModelType::Membrane, BodyModelType::Plate,
                                    BodyModelType::Shell, BodyModelType::Bell,
                                    BodyModelType::NoiseBody};
    const float velocities[] = {0.3f, 0.8f, 1.0f};

    for (BodyModelType body : bodies) {
        float prevPeak = 0.0f;
        for (float v : velocities) {
            const Render r = renderHit(body, v, /*amount*/ 0.5f, /*seconds*/ 1.5);
            const auto a = static_cast<std::size_t>(0.2 * kSR);
            const auto b = static_cast<std::size_t>(1.0 * kSR);
            const float crest = Krate::DSP::TestUtils::SignalMetrics::calculateCrestFactorDb(
                r.out.data() + a, b - a);
            const float peak = peakOf(r.out);
            INFO(bodyName(body) << " v " << v << ": crest(0.2-1.0 s) " << crest
                 << " dB, peak " << peak << ", previous-velocity peak " << prevPeak);
            // (a) off the clip rail: a pinned square has a 0 dB crest.
            CHECK(crest > 3.0f);
            // (b) the full hit stays inside the body's strike budget.
            if (v == 1.0f)
                CHECK(peak <= kPeakBar);
            // (c) a harder hit is never quieter.
            CHECK(peak >= prevPeak);
            prevPeak = peak;
        }
    }
}

TEST_CASE("Feedback exciter: Feedback Amount sets the sustain",
          "[feedback_exciter][gain_staging]")
{
    const BodyModelType bodies[] = {BodyModelType::Membrane, BodyModelType::Plate};
    for (BodyModelType body : bodies) {
        // 8 s, so the -40 dB fall time of a long Plate tail is measured, not
        // clipped at the end of the render.
        const Render r0  = renderHit(body, 0.8f, 0.0f, 8.0);
        const Render r05 = renderHit(body, 0.8f, 0.5f, 8.0);
        const Render r1  = renderHit(body, 0.8f, 1.0f, 8.0);

        const float rms0 = rmsOver(r0.out, 0.2, 1.2);
        const float rms1 = rmsOver(r1.out, 0.2, 1.2);
        const float diffDb = 20.0f * std::log10(std::max(rms1, 1e-12f) / std::max(rms0, 1e-12f));
        const double t0  = fall40dbTime(r0.out);
        const double t05 = fall40dbTime(r05.out);
        const double t1  = fall40dbTime(r1.out);
        INFO(bodyName(body) << ": RMS(0.2-1.2 s) amount 0 = " << rms0 << ", amount 1 = " << rms1
             << " (" << diffDb << " dB); -40 dB fall time 0 / 0.5 / 1 = "
             << t0 << " / " << t05 << " / " << t1 << " s");
        CHECK(std::abs(diffDb) > 3.0f);
        CHECK(t0 < t05);
        CHECK(t05 < t1);

        // The longest sustain still retires with no note-off (user ruling
        // 2026-11-24): within T1.7's 20 s, and no later than the same pad
        // struck by the Mallet exciter (both strike-normalised to the same
        // body budget).
        const Render full   = renderHit(body, 1.0f, 1.0f, 20.0);
        const Render mallet = renderHit(body, 1.0f, 1.0f, 20.0, ExciterType::Mallet);
        const double tFull   = retireTime(full);
        const double tMallet = retireTime(mallet);
        INFO(bodyName(body) << ": retirement at amount 1, v 1: Feedback " << tFull
             << " s, Mallet " << tMallet << " s (20 s = never)");
        CHECK(tFull < 20.0);
        CHECK(tFull <= tMallet);
    }
}
