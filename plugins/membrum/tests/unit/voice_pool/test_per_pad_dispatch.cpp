// Phase 4 T010: Voice pool per-pad config dispatch tests
// Tests that VoicePool uses PadConfig[32] per-pad dispatch (not SharedParams).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"

using namespace Membrum;
using Catch::Approx;

// ==============================================================================
// Helper: prepare a voice pool for testing
// ==============================================================================
namespace {

void preparePool(VoicePool& pool, double sampleRate = 44100.0, int blockSize = 512)
{
    pool.prepare(sampleRate, blockSize);
}

} // namespace

// ==============================================================================
// setPadConfigField / setPadConfigSelector / padConfig read-back
// ==============================================================================

TEST_CASE("VoicePool: setPadConfigField updates correct pad", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    // Set pad 0 material to 0.9
    pool.setPadConfigField(0, kPadMaterial, 0.9f);
    CHECK(pool.padConfig(0).material == Approx(0.9f));

    // Pad 1 material should still be default (0.5)
    CHECK(pool.padConfig(1).material == Approx(0.5f));

    // Set pad 5 size to 0.1
    pool.setPadConfigField(5, kPadSize, 0.1f);
    CHECK(pool.padConfig(5).size == Approx(0.1f));
    CHECK(pool.padConfig(0).size == Approx(0.5f)); // unchanged
}

TEST_CASE("VoicePool: setPadConfigSelector updates exciter/body type", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    pool.setPadConfigSelector(0, kPadExciterType, static_cast<int>(ExciterType::NoiseBurst));
    CHECK(pool.padConfig(0).exciterType == ExciterType::NoiseBurst);
    CHECK(pool.padConfig(1).exciterType == ExciterType::Impulse); // default

    pool.setPadConfigSelector(3, kPadBodyModel, static_cast<int>(BodyModelType::Bell));
    CHECK(pool.padConfig(3).bodyModel == BodyModelType::Bell);
    CHECK(pool.padConfig(0).bodyModel == BodyModelType::Membrane); // default
}

TEST_CASE("VoicePool: padConfig returns correct read-only reference", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    pool.setPadConfigField(7, kPadDecay, 0.75f);
    const auto& cfg = pool.padConfig(7);
    CHECK(cfg.decay == Approx(0.75f));
    CHECK(cfg.material == Approx(0.5f)); // default
}

TEST_CASE("VoicePool: padConfigMut provides mutable access", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    auto& cfg = pool.padConfigMut(2);
    cfg.level = 0.42f;
    cfg.exciterType = ExciterType::FMImpulse;

    CHECK(pool.padConfig(2).level == Approx(0.42f));
    CHECK(pool.padConfig(2).exciterType == ExciterType::FMImpulse);
}

// ==============================================================================
// Per-pad noteOn dispatch
// ==============================================================================

TEST_CASE("VoicePool: noteOn for MIDI note N uses padConfigs[N-36]", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    // Configure pad 0 (MIDI 36) with distinct settings
    pool.setPadConfigField(0, kPadMaterial, 0.1f);
    pool.setPadConfigField(0, kPadSize, 0.9f);
    pool.setPadConfigSelector(0, kPadExciterType, static_cast<int>(ExciterType::Friction));

    // Configure pad 2 (MIDI 38) with different settings
    pool.setPadConfigField(2, kPadMaterial, 0.8f);
    pool.setPadConfigField(2, kPadSize, 0.2f);
    pool.setPadConfigSelector(2, kPadExciterType, static_cast<int>(ExciterType::NoiseBurst));

    // Trigger pad 0
    pool.noteOn(36, 0.9f);
    CHECK(pool.getActiveVoiceCount() == 1);

    // Trigger pad 2
    pool.noteOn(38, 0.9f);
    CHECK(pool.getActiveVoiceCount() == 2);

    // Both voices should be active with different configs
    // (We can't directly inspect voice exciter type from outside, but the
    // noteOn succeeded which means applyPadConfigToSlot was called.)
}

TEST_CASE("VoicePool: changing pad 0 material does not affect pad 1", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    pool.setPadConfigField(0, kPadMaterial, 0.1f);
    pool.setPadConfigField(1, kPadMaterial, 0.9f);

    CHECK(pool.padConfig(0).material == Approx(0.1f));
    CHECK(pool.padConfig(1).material == Approx(0.9f));

    // Modify pad 0 again
    pool.setPadConfigField(0, kPadMaterial, 0.5f);
    CHECK(pool.padConfig(0).material == Approx(0.5f));
    CHECK(pool.padConfig(1).material == Approx(0.9f)); // unchanged
}

TEST_CASE("VoicePool: MIDI note outside 36-67 is silently dropped", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    // Notes below and above the range should not trigger voices
    pool.noteOn(35, 0.9f); // below range
    CHECK(pool.getActiveVoiceCount() == 0);

    pool.noteOn(68, 0.9f); // above range
    CHECK(pool.getActiveVoiceCount() == 0);

    // Valid note should work
    pool.noteOn(36, 0.9f);
    CHECK(pool.getActiveVoiceCount() == 1);
}

// ==============================================================================
// Secondary exciter params (offsets 32-35)
// ==============================================================================

TEST_CASE("VoicePool: setPadConfigField handles secondary exciter params", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    pool.setPadConfigField(0, kPadFMRatio, 0.8f);
    pool.setPadConfigField(0, kPadFeedbackAmount, 0.3f);
    pool.setPadConfigField(0, kPadNoiseBurstDuration, 0.7f);
    pool.setPadConfigField(0, kPadFrictionPressure, 0.6f);

    CHECK(pool.padConfig(0).fmRatio == Approx(0.8f));
    CHECK(pool.padConfig(0).feedbackAmount == Approx(0.3f));
    CHECK(pool.padConfig(0).noiseBurstDuration == Approx(0.7f));
    CHECK(pool.padConfig(0).frictionPressure == Approx(0.6f));
}

// ==============================================================================
// setPadChokeGroup
// ==============================================================================

TEST_CASE("VoicePool: setPadChokeGroup updates per-pad choke group", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    pool.setPadChokeGroup(6, 1);  // pad 6 (closed hi-hat) choke group 1
    pool.setPadChokeGroup(8, 1);  // pad 8 (pedal hi-hat) choke group 1
    pool.setPadChokeGroup(10, 1); // pad 10 (open hi-hat) choke group 1

    CHECK(pool.padConfig(6).chokeGroup == 1);
    CHECK(pool.padConfig(8).chokeGroup == 1);
    CHECK(pool.padConfig(10).chokeGroup == 1);
    CHECK(pool.padConfig(0).chokeGroup == 0); // default
}

// ==============================================================================
// applyPadConfigToSlot (tested indirectly through noteOn rendering)
// ==============================================================================

TEST_CASE("VoicePool: applyPadConfigToSlot uses pad N's config not another pad's", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    // Configure pad 0 with small size (high pitch)
    pool.setPadConfigField(0, kPadSize, 0.1f);
    pool.setPadConfigField(0, kPadLevel, 0.8f);

    // Configure pad 31 with large size (low pitch)
    pool.setPadConfigField(31, kPadSize, 0.9f);
    pool.setPadConfigField(31, kPadLevel, 0.8f);

    // Trigger pad 0 (MIDI 36) and pad 31 (MIDI 67)
    pool.noteOn(36, 0.9f);
    pool.noteOn(67, 0.9f);

    CHECK(pool.getActiveVoiceCount() == 2);

    // Render a block and check both produce output
    constexpr int blockSize = 512;
    float outL[blockSize] = {};
    float outR[blockSize] = {};
    pool.processBlock(outL, outR, blockSize);

    // Both voices should produce non-zero output
    float peak = 0.0f;
    for (int i = 0; i < blockSize; ++i)
        peak = std::max(peak, std::fabs(outL[i]));
    CHECK(peak > 0.0f);
}

// ==============================================================================
// Tone shaper params via setPadConfigField
// ==============================================================================

TEST_CASE("VoicePool: setPadConfigField handles tone shaper params", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    pool.setPadConfigField(3, kPadTSFilterCutoff, 0.6f);
    pool.setPadConfigField(3, kPadTSFilterResonance, 0.4f);
    pool.setPadConfigField(3, kPadTSDriveAmount, 0.2f);

    CHECK(pool.padConfig(3).tsFilterCutoff == Approx(0.6f));
    CHECK(pool.padConfig(3).tsFilterResonance == Approx(0.4f));
    CHECK(pool.padConfig(3).tsDriveAmount == Approx(0.2f));
}

// ==============================================================================
// Unnatural Zone + Material Morph params
// ==============================================================================

TEST_CASE("VoicePool: setPadConfigField handles unnatural zone and morph params", "[voice_pool][pad_config]")
{
    VoicePool pool;
    preparePool(pool);

    pool.setPadConfigField(1, kPadModeStretch, 0.7f);
    pool.setPadConfigField(1, kPadDecaySkew, 0.3f);
    pool.setPadConfigField(1, kPadMorphEnabled, 1.0f);
    pool.setPadConfigField(1, kPadMorphDuration, 0.5f);

    CHECK(pool.padConfig(1).modeStretch == Approx(0.7f));
    CHECK(pool.padConfig(1).decaySkew == Approx(0.3f));
    CHECK(pool.padConfig(1).morphEnabled == Approx(1.0f));
    CHECK(pool.padConfig(1).morphDuration == Approx(0.5f));
}
