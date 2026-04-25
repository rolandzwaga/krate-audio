// Verify Phase 8F bug-fix: applyPadConfigToSlot must propagate per-pad
// tone-shaper / pitch-envelope settings into the voice on note-on.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"

TEST_CASE("Per-pad pitch envelope reaches the voice's toneShaper", "[voice_pool][phase8f]")
{
    Membrum::VoicePool pool;
    pool.prepare(48000.0, 256);
    pool.setMaxPolyphony(8);

    // Pad 0 (MIDI 36): pitchEnv 200 Hz -> 50 Hz over 100 ms.
    // 200 Hz norm = log(200/20)/log(100) ~= 0.5
    // 50 Hz norm  = log(50/20)/log(100)  ~= 0.199
    // 100 ms / 500 = 0.2
    pool.setPadConfigField(0, Membrum::kPadTSPitchEnvStart, 0.5f);
    pool.setPadConfigField(0, Membrum::kPadTSPitchEnvEnd,   0.199f);
    pool.setPadConfigField(0, Membrum::kPadTSPitchEnvTime,  0.2f);

    // Pad 5 (MIDI 41): pitchEnv 1000 Hz -> 250 Hz over 50 ms.
    // 1000 Hz norm = log(1000/20)/log(100) ~= 0.85
    // 250 Hz norm  = log(250/20)/log(100)  ~= 0.55
    pool.setPadConfigField(5, Membrum::kPadTSPitchEnvStart, 0.85f);
    pool.setPadConfigField(5, Membrum::kPadTSPitchEnvEnd,   0.55f);
    pool.setPadConfigField(5, Membrum::kPadTSPitchEnvTime,  0.1f);

    // Trigger pad 0 then pad 5; capture each active voice's toneShaper state.
    pool.noteOn(36, 1.0f);
    float pad0Start = -1.0f, pad0End = -1.0f, pad0Time = -1.0f;
    pool.forEachMainVoice([&](Membrum::DrumVoice& v) {
        if (pad0Start < 0.0f) {
            pad0Start = v.toneShaper().getPitchEnvStartHz();
            pad0End   = v.toneShaper().getPitchEnvEndHz();
            pad0Time  = v.toneShaper().getPitchEnvTimeMs();
        }
    });

    pool.noteOn(41, 1.0f);
    float pad5Start = -1.0f, pad5End = -1.0f, pad5Time = -1.0f;
    bool seenPad0 = false;
    pool.forEachMainVoice([&](Membrum::DrumVoice& v) {
        // Two voices are now active; pick the second-distinct one.
        const float s = v.toneShaper().getPitchEnvStartHz();
        if (!seenPad0 && std::abs(s - pad0Start) < 1.0f) {
            seenPad0 = true;
            return;
        }
        if (pad5Start < 0.0f) {
            pad5Start = s;
            pad5End   = v.toneShaper().getPitchEnvEndHz();
            pad5Time  = v.toneShaper().getPitchEnvTimeMs();
        }
    });

    INFO("pad 0: start=" << pad0Start << " end=" << pad0End << " time=" << pad0Time);
    INFO("pad 5: start=" << pad5Start << " end=" << pad5End << " time=" << pad5Time);

    using Catch::Matchers::WithinAbs;
    CHECK_THAT(pad0Start, WithinAbs(200.0f, 1.0f));
    CHECK_THAT(pad0End,   WithinAbs(50.0f,  1.0f));
    CHECK_THAT(pad0Time,  WithinAbs(100.0f, 1.0f));
    CHECK_THAT(pad5Start, WithinAbs(1000.0f, 5.0f));
    CHECK_THAT(pad5End,   WithinAbs(250.0f,  2.0f));
    CHECK_THAT(pad5Time,  WithinAbs(50.0f,   1.0f));
}

// Phase 8F: prove that two pads with different `size` and pitchEnv produce
// different rendered audio. If this check passes but the user still hears
// identical toms in their host, the failure is downstream of the
// processor (host preset routing, dll caching, etc.), not the audio path.
TEST_CASE("Two pads with distinct size + pitchEnv render distinct audio",
          "[voice_pool][phase8f][render]")
{
    Membrum::VoicePool pool;
    pool.prepare(48000.0, 256);
    pool.setMaxPolyphony(8);

    auto configurePad = [&](int padIdx, float sizeNorm,
                            float pitchStartNorm, float pitchEndNorm,
                            float pitchTimeNorm) {
        pool.setPadConfigField(padIdx, Membrum::kPadSize,           sizeNorm);
        pool.setPadConfigField(padIdx, Membrum::kPadMaterial,       0.30f);
        pool.setPadConfigField(padIdx, Membrum::kPadDecay,          0.40f);
        pool.setPadConfigField(padIdx, Membrum::kPadStrikePosition, 0.30f);
        pool.setPadConfigField(padIdx, Membrum::kPadLevel,          0.80f);
        pool.setPadConfigField(padIdx, Membrum::kPadTSPitchEnvStart, pitchStartNorm);
        pool.setPadConfigField(padIdx, Membrum::kPadTSPitchEnvEnd,   pitchEndNorm);
        pool.setPadConfigField(padIdx, Membrum::kPadTSPitchEnvTime,  pitchTimeNorm);
    };

    // Pad 0: very large body (size=0.9 -> nat f0 ~63 Hz), low pitch env.
    configurePad(0, 0.90f, 0.30f /*~80Hz*/, 0.20f /*~50Hz*/, 0.30f /*150ms*/);
    // Pad 5: small body (size=0.4 -> nat f0 ~199 Hz), high pitch env.
    configurePad(5, 0.40f, 0.85f /*~1000Hz*/, 0.55f /*~250Hz*/, 0.30f);

    auto renderPad = [&](int midi) {
        for (int n = 36; n <= 67; ++n)
            pool.noteOff(static_cast<std::uint8_t>(n));
        pool.noteOn(static_cast<std::uint8_t>(midi), 1.0f);
        constexpr int kBlocks = 20; // 20 * 256 = ~107 ms
        std::vector<float> samples;
        samples.reserve(kBlocks * 256);
        std::array<float, 256> outL{};
        std::array<float, 256> outR{};
        for (int i = 0; i < kBlocks; ++i) {
            outL.fill(0.0f);
            outR.fill(0.0f);
            pool.processBlock(outL.data(), outR.data(),
                              static_cast<int>(outL.size()));
            for (int s = 0; s < 256; ++s) samples.push_back(outL[s]);
        }
        return samples;
    };

    const auto pad0Audio = renderPad(36);
    const auto pad5Audio = renderPad(41);

    // Total energies should differ -- different fundamentals + different
    // body sizes produce measurably different output.
    double e0 = 0.0, e5 = 0.0;
    for (float s : pad0Audio) e0 += static_cast<double>(s) * s;
    for (float s : pad5Audio) e5 += static_cast<double>(s) * s;
    INFO("pad0 energy=" << e0 << " pad5 energy=" << e5);
    CHECK(std::abs(e0 - e5) > 0.001);

    // Sample-by-sample: at least one sample must differ noticeably.
    REQUIRE(pad0Audio.size() == pad5Audio.size());
    double maxAbsDiff = 0.0;
    for (std::size_t i = 0; i < pad0Audio.size(); ++i) {
        const double d = std::abs(static_cast<double>(pad0Audio[i])
                                  - static_cast<double>(pad5Audio[i]));
        if (d > maxAbsDiff) maxAbsDiff = d;
    }
    INFO("max sample diff = " << maxAbsDiff);
    CHECK(maxAbsDiff > 1e-4);
}
