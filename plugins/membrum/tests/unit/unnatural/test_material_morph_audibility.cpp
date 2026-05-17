// ==============================================================================
// MaterialMorph end-to-end audibility regression (Phase 11 / spec 145).
//
// Reproduces the user-reported scenario: "I activate Material Morph in the
// Advanced view, hit pad zero, move the XY pad, hit pad zero again. There is
// no audible difference."
//
// Pre-Phase-11 root cause: the modal bank's per-sample soft-clip at +/-0.707
// (`kSoftClipThreshold`) pinned the body output for the entire transient
// regardless of per-mode damping settings. Material's only audible path
// (decay-vs-frequency) lived ABOVE the -3 dBFS clipper ceiling for the
// 100-200 ms window that drum hits are perceived in, so changing Material
// produced an indistinguishable render even when the morph faithfully
// reached the voice.
//
// Phase 11 fix: raise the bank's clip threshold to 0 dBFS for non-feedback
// voices (FeedbackExciter still gets the -3 dBFS safety clip as a documented
// passivity-loss case). The linear region now grows past the body's natural
// modal-sum peak, so damping modulation propagates audibly. Downstream
// `softClip(shaped * env * level)` in DrumVoice still bounds the final voice
// output to [-1, 1].
//
// This test renders the SAME MIDI hit twice with different morph endpoints
// (start=0.0 vs start=1.0) and asserts the resulting audio energies differ
// by at least 5 %. A failure here means the bank clip is masking material
// again or the morph plumbing has regressed.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"
#include "unit/voice_pool/voice_pool_test_helpers.h"

#include <array>
#include <cmath>
#include <cstring>

using namespace Membrum;

namespace {

constexpr int kBlockSize = 256;
constexpr int kNumBlocks = 16;  // ~93 ms at 44.1 kHz, well inside 200 ms morph

double renderHitEnergy(float morphEnabled, float morphStart, float morphEnd)
{
    VoicePool pool;
    pool.prepare(44100.0, kBlockSize);
    pool.setMaxPolyphony(1);
    TestHelpers::setAllPadsBodyModel(pool, BodyModelType::Membrane);
    TestHelpers::setAllPadsExciterType(pool, ExciterType::Impulse);
    TestHelpers::setAllPadsVoiceParams(pool,
        /*material*/0.5f, /*size*/0.5f, /*decay*/0.5f,
        /*strikePos*/0.3f, /*level*/0.8f);

    pool.setPadConfigField(0, kPadMorphEnabled,  morphEnabled);
    pool.setPadConfigField(0, kPadMorphStart,    morphStart);
    pool.setPadConfigField(0, kPadMorphEnd,      morphEnd);
    pool.setPadConfigField(0, kPadMorphDuration, 0.095477f);  // ~200 ms

    // Zero the always-on click + noise layers so the body output dominates
    // the energy measurement (they would otherwise mask the body's modal
    // amplitude with material-independent click/noise energy).
    pool.setPadConfigField(0, kPadNoiseLayerMix, 0.0f);
    pool.setPadConfigField(0, kPadClickLayerMix, 0.0f);

    pool.noteOn(36, 0.5f);  // pad 0 = MIDI 36

    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};
    double e = 0.0;
    for (int b = 0; b < kNumBlocks; ++b)
    {
        std::memset(outL.data(), 0, sizeof(outL));
        std::memset(outR.data(), 0, sizeof(outR));
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i)
            e += static_cast<double>(outL[i]) * outL[i]
               + static_cast<double>(outR[i]) * outR[i];
    }
    return e;
}

} // namespace

TEST_CASE("MaterialMorph (Phase 11): XY pad endpoints produce audibly distinct hits",
          "[membrum][morph][audibility]")
{
    // Morph from material=0 (woody, fast HF decay) toward material=1.
    const double eWoodyStart = renderHitEnergy(
        /*morphEnabled*/1.0f, /*morphStart*/0.0f, /*morphEnd*/1.0f);

    // Morph from material=1 (metallic, slow HF decay) toward material=0.
    const double eMetalStart = renderHitEnergy(
        /*morphEnabled*/1.0f, /*morphStart*/1.0f, /*morphEnd*/0.0f);

    INFO("Woody-start  hit energy = " << eWoodyStart);
    INFO("Metal-start  hit energy = " << eMetalStart);

    const double diff = std::abs(eMetalStart - eWoodyStart);
    const double larger = std::max(eMetalStart, eWoodyStart);
    REQUIRE(larger > 0.0);
    // 5 % spread is the minimum perceptual contract: hitting pad 0 with
    // morph start=0 vs start=1 must produce audibly distinct renders.
    CHECK(diff / larger > 0.05);
}

TEST_CASE("Material parameter (Phase 11): static material 0 vs 1 produce audibly distinct hits",
          "[membrum][material][audibility]")
{
    auto render = [](float material) {
        VoicePool pool;
        pool.prepare(44100.0, kBlockSize);
        pool.setMaxPolyphony(1);
        TestHelpers::setAllPadsBodyModel(pool, BodyModelType::Membrane);
        TestHelpers::setAllPadsExciterType(pool, ExciterType::Impulse);
        TestHelpers::setAllPadsVoiceParams(pool,
            material, /*size*/0.5f, /*decay*/0.5f,
            /*strikePos*/0.3f, /*level*/0.8f);
        pool.setPadConfigField(0, kPadMorphEnabled, 0.0f);
        pool.setPadConfigField(0, kPadNoiseLayerMix, 0.0f);
        pool.setPadConfigField(0, kPadClickLayerMix, 0.0f);
        pool.noteOn(36, 0.5f);
        std::array<float, kBlockSize> outL{};
        std::array<float, kBlockSize> outR{};
        double e = 0.0;
        for (int b = 0; b < kNumBlocks; ++b)
        {
            std::memset(outL.data(), 0, sizeof(outL));
            std::memset(outR.data(), 0, sizeof(outR));
            pool.processBlock(outL.data(), outR.data(), kBlockSize);
            for (int i = 0; i < kBlockSize; ++i)
                e += static_cast<double>(outL[i]) * outL[i]
                   + static_cast<double>(outR[i]) * outR[i];
        }
        return e;
    };

    const double eWoody  = render(0.0f);
    const double eMetal  = render(1.0f);

    INFO("Material=0.0 (woody)    energy = " << eWoody);
    INFO("Material=1.0 (metallic) energy = " << eMetal);

    const double diff = std::abs(eMetal - eWoody);
    const double larger = std::max(eMetal, eWoody);
    REQUIRE(larger > 0.0);
    CHECK(diff / larger > 0.05);
}
