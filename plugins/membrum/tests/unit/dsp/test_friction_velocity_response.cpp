// T1.17 (MB-019): a Friction pad's voice peak must rise with velocity and stay
// under the body budget.
//
// The voice normalises its body against a strike probe at velocity 1.0. Before
// the fix the Friction exciter's output FELL with velocity (the bow force runs
// past the friction table's peak and the velocity-driven bandpass rejects the
// bow-force onset), so the probe calibrated the weakest hit and every softer
// hit drove the body past kBodyHeadroom into the per-voice hardClip rail:
// Membrane 0.700 at v 0.1, 1.000 at v 0.2-0.7, 0.500 at v 1.0.

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "dsp/drum_voice.h"
#include "dsp/pad_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace Membrum;

namespace {

constexpr double kSR    = 48000.0;
constexpr int    kBlock = 64;
constexpr int    kTotal = 24000;  // 0.5 s

// Pad 0 as an isolated modal body driven by the Friction exciter, every other
// layer / character stage OFF (test_body_headroom_budget.cpp's set-up).
void configureFrictionBody(VoicePool& pool, BodyModelType body)
{
    pool.setPadConfigSelector(0, kPadExciterType, static_cast<int>(ExciterType::Friction));
    pool.setPadConfigSelector(0, kPadBodyModel,   static_cast<int>(body));
    pool.setPadConfigField(0, kPadMaterial,        0.30f);
    pool.setPadConfigField(0, kPadSize,            0.80f);
    pool.setPadConfigField(0, kPadDecay,           0.85f);
    pool.setPadConfigField(0, kPadStrikePosition,  0.30f);
    pool.setPadConfigField(0, kPadLevel,           1.0f);
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

// One hit on pad 0 at the given velocity; returns the peak |output| over 0.5 s.
float renderPeak(BodyModelType body, float velocity)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setMaxPolyphony(1);
    configureFrictionBody(pool, body);
    pool.noteOn(36, velocity);

    std::vector<float> outL(kBlock), outR(kBlock);
    float peak = 0.0f;
    for (int s = 0; s < kTotal; s += kBlock) {
        pool.processBlock(outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            peak = std::max(peak, std::abs(outL[static_cast<std::size_t>(i)]));
    }
    return peak;
}

const char* bodyName(BodyModelType b)
{
    switch (b) {
    case BodyModelType::Membrane: return "Membrane";
    case BodyModelType::Bell:     return "Bell";
    case BodyModelType::Plate:    return "Plate";
    default:                      return "?";
    }
}

} // namespace

TEST_CASE("Friction pads: voice peak rises with velocity and stays under the body budget",
          "[friction_exciter][velocity][gain_staging]")
{
    constexpr float kBudget = DrumVoice::kBodyHeadroom * 1.12f; // 0.56
    constexpr std::array<float, 6> kVel{0.1f, 0.2f, 0.3f, 0.5f, 0.7f, 1.0f};

    for (const BodyModelType body :
         {BodyModelType::Membrane, BodyModelType::Bell, BodyModelType::Plate})
    {
        std::array<float, kVel.size()> peaks{};
        for (std::size_t i = 0; i < kVel.size(); ++i)
            peaks[i] = renderPeak(body, kVel[i]);

        INFO(bodyName(body) << " voice peaks v0.1..1.0: " << peaks[0] << " " << peaks[1]
             << " " << peaks[2] << " " << peaks[3] << " " << peaks[4] << " " << peaks[5]);
        for (std::size_t i = 0; i < kVel.size(); ++i)
        {
            INFO("v " << kVel[i]);
            CHECK(peaks[i] <= kBudget);
            if (i > 0)
                CHECK(peaks[i] >= peaks[i - 1]);
        }

        // Spec 136 SC-005: velocity 127 vs 30 differs by more than 6 dB.
        const float p30  = renderPeak(body, 30.0f / 127.0f);
        const float p127 = renderPeak(body, 127.0f / 127.0f);
        INFO("SC-005 peak(127) " << p127 << " / peak(30) " << p30);
        REQUIRE(p30 > 0.0f);
        CHECK(p127 / p30 > 2.0f);
    }
}
