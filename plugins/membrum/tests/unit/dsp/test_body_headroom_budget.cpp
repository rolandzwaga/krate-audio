// N-1 regression: the modal body must stay within its gain budget so the
// per-voice hardClip rail does NOT engage on a single musical hit, and per-
// velocity peak dynamics survive.
//
// Before the fix, the body bank is normalized against getInputGainSum() (the
// t=0 in-phase IMPULSE bound), which under-bounds the resonant buildup of a
// real multi-sample strike by ~3-18x (spike: test_body_resonant_peak.cpp).
// Membrane and Bell consequently arrive at the per-voice hardClip(+/-1.0) rail
// at level 1.0, where BOTH velocity 1.0 and velocity 0.5 pin to ~1.0 -- peak
// velocity response collapses to ~1.0x and the hit distorts.
//
// After the fix (configure-time measured-strike normalization), the body peak
// is bounded to ~kBodyHeadroom, the rail stays disengaged on a single hit, and
// peak(vel 1.0) / peak(vel 0.5) recovers a real (~2x) ratio.

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Membrum;

namespace {

constexpr double kSR    = 48000.0;
constexpr int    kBlock = 64;
constexpr int    kTotal = 24000;  // 0.5 s

// Configure pad 0 as an isolated modal body (Mallet exciter, every other
// layer / character stage OFF) at the given per-pad Level.
void configureBodyOnly(VoicePool& pool, BodyModelType body, float level)
{
    pool.setPadConfigSelector(0, kPadExciterType, static_cast<int>(ExciterType::Mallet));
    pool.setPadConfigSelector(0, kPadBodyModel,   static_cast<int>(body));
    pool.setPadConfigField(0, kPadMaterial,        0.30f);
    pool.setPadConfigField(0, kPadSize,            0.80f);
    pool.setPadConfigField(0, kPadDecay,           0.85f);
    pool.setPadConfigField(0, kPadStrikePosition,  0.30f);
    pool.setPadConfigField(0, kPadLevel,           level);
    // Everything that is not the modal body, OFF:
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

// Render a single hit at the given velocity and return the peak |output|.
float renderPeak(BodyModelType body, float level, float velocity)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setMaxPolyphony(1);
    configureBodyOnly(pool, body, level);
    pool.noteOn(36, velocity);

    std::vector<float> outL(kBlock), outR(kBlock);
    float peak = 0.0f;
    for (int s = 0; s < kTotal; s += kBlock) {
        pool.processBlock(outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            peak = std::max(peak, std::abs(outL[i]));
    }
    return peak;
}

} // namespace

TEST_CASE("N-1: modal body stays under the per-voice rail at level 1.0",
          "[n1][headroom]")
{
    // Membrane and Bell are the bodies the spike showed over-run the rail.
    const BodyModelType bodies[] = {BodyModelType::Membrane, BodyModelType::Bell};

    for (BodyModelType body : bodies) {
        const float peakV1  = renderPeak(body, /*level*/ 1.0f, /*vel*/ 1.0f);
        const float peakV05 = renderPeak(body, /*level*/ 1.0f, /*vel*/ 0.5f);

        // (a) A single musical hit must not slam the per-voice hardClip rail.
        CHECK(peakV1 < 0.95f);

        // (b) Velocity dynamics must survive: a full hit is meaningfully louder
        //     than a half-velocity hit (the rail collapses this to ~1.0x).
        REQUIRE(peakV05 > 1.0e-4f);
        CHECK(peakV1 / peakV05 > 1.5f);
    }
}
