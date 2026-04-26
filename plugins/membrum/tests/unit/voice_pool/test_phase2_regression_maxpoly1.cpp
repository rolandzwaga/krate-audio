// ==============================================================================
// Phase 3.1 -- Phase 2 regression @ maxPolyphony=1
// ==============================================================================
// T3.1.4 -- satisfies FR-187 / SC-028.
//
// Compares the VoicePool output against a standalone `Membrum::DrumVoice`
// rendered in parallel with the same parameters and the same noteOn. At
// maxPolyphony=1 the pool should produce bit-identical audio to the Phase 2
// single-voice path (slot 0 only, no stealing).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/drum_voice.h"
#include "dsp/pad_config.h"
#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 256;

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

} // namespace

TEST_CASE("VoicePool maxPolyphony=1 matches Phase 2 DrumVoice reference",
          "[membrum][voice_pool][phase3_1][regression][phase2_regression]")
{
    // Phase 2 reference: bare DrumVoice. Phase 8G: applyPadConfigToSlot now
    // also writes ALL Phase 7+ fields (Tone Shaper, Unnatural Zone,
    // Material Morph) into the voice on every note-on. The bit-identity
    // contract therefore requires the bare reference to apply EXACTLY the
    // same setter sequence on the same construction-default values, so
    // any normalised-cfg -> denormalised float precision losses (e.g.
    // 0.5f + 0.333333f * 1.5f != 1.0f exactly) appear identically on both
    // sides. Without this, macOS / ARM-Clang FMA precision differences
    // accumulate over the 22050-sample render to break the -90 dBFS
    // threshold. PadConfig defaults are mirrored verbatim so the bare
    // voice ends up bit-identical to a pool voice driven from a fresh
    // PadConfig with the test's noise/click/airLoading overrides applied.
    const Membrum::PadConfig kCfg{}; // PadConfig default ctor.
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate, /*voiceId=*/0u);

    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.3f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);

    // Phase 7 noise layer: cfg defaults except mix=0 to match the pool
    // path's per-pad override (test_helpers zeroes mix on every pad).
    voice.setNoiseLayerMix(0.0f);
    voice.setNoiseLayerCutoff(kCfg.noiseLayerCutoff);
    voice.setNoiseLayerResonance(kCfg.noiseLayerResonance);
    voice.setNoiseLayerDecay(kCfg.noiseLayerDecay);
    voice.setNoiseLayerColor(kCfg.noiseLayerColor);
    // Phase 7 click layer (mix=0 override).
    voice.setClickLayerMix(0.0f);
    voice.setClickLayerContactMs(kCfg.clickLayerContactMs);
    voice.setClickLayerBrightness(kCfg.clickLayerBrightness);
    voice.setNoiseBurstContactMs(kCfg.noiseBurstDuration);
    // Phase 8A damping law sentinels.
    voice.setBodyDampingB1(kCfg.bodyDampingB1);
    voice.setBodyDampingB3(kCfg.bodyDampingB3);
    // Phase 8C air-loading + scatter (zeroed by the test on every pad).
    voice.setAirLoading(0.0f);
    voice.setModeScatter(0.0f);
    // Phase 8D head/shell coupling (defaults).
    voice.setCouplingStrength(kCfg.couplingStrength);
    voice.setSecondaryEnabled(kCfg.secondaryEnabled);
    voice.setSecondarySize(kCfg.secondarySize);
    voice.setSecondaryMaterial(kCfg.secondaryMaterial);
    // Phase 8E tension modulation.
    voice.setTensionModAmt(kCfg.tensionModAmt);

    // Phase 8G: Tone Shaper field propagation. Mirrors
    // VoicePool::applyPadConfigToSlot exactly so denormalised float math
    // matches bit-for-bit on every platform.
    {
        const int filterTypeIdx =
            std::clamp(static_cast<int>(kCfg.tsFilterType * 3.0f), 0, 2);
        voice.toneShaper().setFilterType(
            static_cast<Membrum::ToneShaperFilterType>(filterTypeIdx));
    }
    voice.toneShaper().setFilterCutoff(
        20.0f * std::pow(1000.0f, std::clamp(kCfg.tsFilterCutoff, 0.0f, 1.0f)));
    voice.toneShaper().setFilterResonance(kCfg.tsFilterResonance);
    voice.toneShaper().setFilterEnvAmount(kCfg.tsFilterEnvAmount * 2.0f - 1.0f);
    voice.toneShaper().setFilterEnvAttackMs(kCfg.tsFilterEnvAttack * 500.0f);
    voice.toneShaper().setFilterEnvDecayMs(kCfg.tsFilterEnvDecay * 2000.0f);
    voice.toneShaper().setFilterEnvSustain(kCfg.tsFilterEnvSustain);
    voice.toneShaper().setFilterEnvReleaseMs(kCfg.tsFilterEnvRelease * 2000.0f);
    voice.toneShaper().setDriveAmount(kCfg.tsDriveAmount);
    voice.toneShaper().setFoldAmount(kCfg.tsFoldAmount);
    voice.toneShaper().setPitchEnvStartHz(
        20.0f * std::pow(100.0f, std::clamp(kCfg.tsPitchEnvStart, 0.0f, 1.0f)));
    voice.toneShaper().setPitchEnvEndHz(
        20.0f * std::pow(100.0f, std::clamp(kCfg.tsPitchEnvEnd, 0.0f, 1.0f)));
    voice.toneShaper().setPitchEnvTimeMs(kCfg.tsPitchEnvTime * 500.0f);
    {
        const int curveIdx =
            std::clamp(static_cast<int>(kCfg.tsPitchEnvCurve * 2.0f), 0, 1);
        voice.toneShaper().setPitchEnvCurve(
            static_cast<Membrum::ToneShaperCurve>(curveIdx));
    }

    // Phase 8G: Unnatural Zone propagation (modeStretch normalised over
    // [0.5, 2.0] with 1.0 = physical / Phase 1 bit-identity; decaySkew
    // bipolar over [-1, +1] from the [0, 1] norm).
    voice.unnaturalZone().setModeStretch(
        0.5f + std::clamp(kCfg.modeStretch, 0.0f, 1.0f) * 1.5f);
    voice.unnaturalZone().setDecaySkew(
        std::clamp(kCfg.decaySkew, 0.0f, 1.0f) * 2.0f - 1.0f);
    voice.unnaturalZone().modeInject.setAmount(
        std::clamp(kCfg.modeInjectAmount, 0.0f, 1.0f));
    voice.unnaturalZone().nonlinearCoupling.setAmount(
        std::clamp(kCfg.nonlinearCoupling, 0.0f, 1.0f));

    // Phase 8G: Material Morph propagation.
    voice.unnaturalZone().materialMorph.setEnabled(kCfg.morphEnabled >= 0.5f);
    voice.unnaturalZone().materialMorph.setStart(
        std::clamp(kCfg.morphStart, 0.0f, 1.0f));
    voice.unnaturalZone().materialMorph.setEnd(
        std::clamp(kCfg.morphEnd, 0.0f, 1.0f));
    voice.unnaturalZone().materialMorph.setDurationMs(
        10.0f + std::clamp(kCfg.morphDuration, 0.0f, 1.0f) * 1990.0f);
    voice.unnaturalZone().materialMorph.setCurve(kCfg.morphCurve >= 0.5f);

    voice.noteOn(100.0f / 127.0f);

    // Phase 3 pool at maxPolyphony=1.
    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(1);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.8f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);

    // Phase 7: zero the always-on noise + click layers on all pads so the
    // pool path matches the bare `Membrum::DrumVoice` reference (whose member
    // NoiseLayerParams / ClickLayerParams default to mix=0 via aggregate
    // init, while PadConfig defaults are non-zero for realism).
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        pool.setPadConfigField(pad, Membrum::kPadNoiseLayerMix, 0.0f);
        pool.setPadConfigField(pad, Membrum::kPadClickLayerMix, 0.0f);
        // Phase 8C: zero the per-pad air-loading / mode-scatter overrides so
        // the pool path matches the bare DrumVoice reference (whose member
        // defaults are 0, while PadConfig uses 0.6 air-loading for realism).
        pool.setPadConfigField(pad, Membrum::kPadAirLoading, 0.0f);
        pool.setPadConfigField(pad, Membrum::kPadModeScatter, 0.0f);
    }

    pool.noteOn(36, 100.0f / 127.0f);

    // Render 500 ms through both paths.
    const int totalSamples = static_cast<int>(kSampleRate / 2);
    const int numBlocks = (totalSamples + kBlockSize - 1) / kBlockSize;

    std::vector<float> ref(static_cast<size_t>(numBlocks * kBlockSize), 0.0f);
    std::vector<float> pl(static_cast<size_t>(numBlocks * kBlockSize), 0.0f);
    std::vector<float> plR(static_cast<size_t>(kBlockSize), 0.0f);

    for (int b = 0; b < numBlocks; ++b)
    {
        voice.processBlock(ref.data() + static_cast<size_t>(b * kBlockSize),
                           kBlockSize);

        pool.processBlock(pl.data() + static_cast<size_t>(b * kBlockSize),
                          plR.data(),
                          kBlockSize);
    }

    // Finite-sample check (no NaN/Inf).
    bool allFinite = true;
    for (size_t i = 0; i < pl.size(); ++i)
    {
        if (!isFiniteSample(pl[i]) || !isFiniteSample(ref[i]))
        {
            allFinite = false;
            break;
        }
    }
    REQUIRE(allFinite);

    // RMS of the difference must be <= -90 dBFS.
    double sumSq = 0.0;
    for (size_t i = 0; i < pl.size(); ++i)
    {
        const double d = static_cast<double>(pl[i]) - static_cast<double>(ref[i]);
        sumSq += d * d;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(pl.size()));
    const double rmsDbfs =
        (rms > 0.0) ? 20.0 * std::log10(rms) : -240.0;
    INFO("diff RMS = " << rms << " (" << rmsDbfs << " dBFS)");
    CHECK(rmsDbfs <= -90.0);
}
