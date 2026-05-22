// Diagnostic: render orchestral timpani kick (pad 0) at velocity 1.0 and
// report peak amplitude. The user reports distortion that doesn't respond
// to preset-level cuts. This bypasses preset loading and configures the
// processor directly so we can see what the post-chain softClip is doing.

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Membrum;

namespace {

double toLogNorm(double hz) {
    return std::log(hz / 20.0) / std::log(100.0);
}

void configureTimpaniKick(VoicePool& pool, float level)
{
    pool.setPadConfigSelector(0, kPadExciterType, static_cast<int>(ExciterType::Mallet));
    pool.setPadConfigSelector(0, kPadBodyModel,   static_cast<int>(BodyModelType::Membrane));
    pool.setPadConfigField(0, kPadMaterial,           0.30f);
    pool.setPadConfigField(0, kPadSize,               0.92f);
    pool.setPadConfigField(0, kPadDecay,              0.85f);
    pool.setPadConfigField(0, kPadLevel,              level);
    pool.setPadConfigField(0, kPadAirLoading,         0.45f);
    pool.setPadConfigField(0, kPadCouplingStrength,   0.25f);
    pool.setPadConfigField(0, kPadSecondaryEnabled,   1.0f);
    pool.setPadConfigField(0, kPadSecondarySize,      0.65f);
    pool.setPadConfigField(0, kPadSecondaryMaterial,  0.50f);
    pool.setPadConfigField(0, kPadTensionModAmt,      0.12f);
    pool.setPadConfigField(0, kPadClickLayerMix,        0.32f);
    pool.setPadConfigField(0, kPadClickLayerContactMs,  0.28f);
    pool.setPadConfigField(0, kPadClickLayerBrightness, 0.30f);
    pool.setPadConfigField(0, kPadNoiseLayerMix,      0.12f);
    pool.setPadConfigField(0, kPadBodyDampingB1,      0.30f);
    pool.setPadConfigField(0, kPadBodyDampingB3,      0.10f);
    pool.setPadConfigField(0, kPadCouplingAmount,     0.60f);
    pool.setPadConfigField(0, kPadTSPitchEnvStart,
        static_cast<float>(toLogNorm(180)));
    pool.setPadConfigField(0, kPadTSPitchEnvEnd,
        static_cast<float>(toLogNorm(85)));
    pool.setPadConfigField(0, kPadTSPitchEnvTime,     0.10f);
    pool.setPadConfigField(0, kPadTSPitchEnvCurve,    0.5f);
    pool.setPadConfigField(0, kPadEnabled,            1.0f);
}

} // namespace

TEST_CASE("Orchestral timpani kick: bisect distortion source", "[diagnose_orch]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kBlock = 64;
    constexpr int    kTotalSamples = 24000;  // 0.5 s

    struct Scenario {
        const char* name;
        bool        disableSecondary;
        bool        disableNoise;
        bool        disableClick;
        bool        disablePitchEnv;
        bool        disableTension;
        bool        disableCoupling;
    };
    const Scenario scenarios[] = {
        {"baseline",         false, false, false, false, false, false},
        {"no-secondary",     true,  false, false, false, false, false},
        {"no-noise-click",   false, true,  true,  false, false, false},
        {"no-pitch-env",     false, false, false, true,  false, false},
        {"no-tension",       false, false, false, false, true,  false},
        {"no-coupling",      false, false, false, false, false, true },
        {"body-only",        true,  true,  true,  true,  true,  true },
    };

    std::printf("[diagnose_orch] bisecting distortion source (level=0.10)\n");
    for (const auto& sc : scenarios)
    {
        VoicePool pool;
        pool.prepare(kSR, kBlock);
        pool.setMaxPolyphony(1);
        configureTimpaniKick(pool, 0.10f);
        if (sc.disableSecondary) pool.setPadConfigField(0, kPadSecondaryEnabled, 0.0f);
        if (sc.disableNoise)     pool.setPadConfigField(0, kPadNoiseLayerMix,    0.0f);
        if (sc.disableClick)     pool.setPadConfigField(0, kPadClickLayerMix,    0.0f);
        if (sc.disablePitchEnv)  pool.setPadConfigField(0, kPadTSPitchEnvTime,   0.0f);
        if (sc.disableTension)   pool.setPadConfigField(0, kPadTensionModAmt,    0.0f);
        if (sc.disableCoupling) {
            pool.setPadConfigField(0, kPadCouplingStrength, 0.0f);
            pool.setPadConfigField(0, kPadCouplingAmount,   0.0f);
        }
        pool.noteOn(36, 1.0f);

        std::vector<float> outL(kBlock), outR(kBlock);
        float peak = 0.0f;
        double sumSq = 0.0;
        int    samplesNearClip = 0;

        for (int s = 0; s < kTotalSamples; s += kBlock) {
            pool.processBlock(outL.data(), outR.data(), kBlock);
            for (int i = 0; i < kBlock; ++i) {
                const float a = std::abs(outL[i]);
                peak = std::max(peak, a);
                sumSq += outL[i] * outL[i];
                if (a >= 0.99f) ++samplesNearClip;
            }
        }
        const float rms = std::sqrt(sumSq / kTotalSamples);

        std::printf("[diagnose_orch] %-18s peak=%.4f  rms=%.4f  "
                    "samples>=0.99: %d  %s\n",
                    sc.name, static_cast<double>(peak),
                    static_cast<double>(rms), samplesNearClip,
                    samplesNearClip > 50 ? "CLIPPING" : "clean");
    }
}
