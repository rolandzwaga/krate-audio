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
    // Phase 2 reference: bare DrumVoice.
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate, /*voiceId=*/0u);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.3f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);
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
