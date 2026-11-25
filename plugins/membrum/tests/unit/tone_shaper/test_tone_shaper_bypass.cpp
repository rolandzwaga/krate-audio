// ==============================================================================
// ToneShaper bypass identity test (Phase 7 — T089)
// ==============================================================================
// tone_shaper_contract.md §Test coverage requirements item 1 / FR-045:
//   Feed 1 kHz sine at 0 dBFS through a ToneShaper with all stages bypassed
//   (drive=0, fold=0, filterCutoff=20000, filterEnvAmount=0, pitchEnvTime=0).
//   RMS difference from input must be ≤ −120 dBFS (bit-close identity).
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/drum_voice.h"
#include "dsp/pad_config.h"
#include "dsp/pad_config_apply.h"
#include "dsp/tone_shaper.h"

#include <krate/dsp/core/random.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int    kLength     = 4410;  // 100 ms of audio
constexpr float  kTestFreq   = 1000.0f;
constexpr double kPi         = 3.14159265358979323846;

std::vector<float> makeSine(int numSamples, float freqHz, double sr)
{
    std::vector<float> buf(static_cast<std::size_t>(numSamples), 0.0f);
    const double omega = 2.0 * kPi * static_cast<double>(freqHz) / sr;
    for (int i = 0; i < numSamples; ++i)
        buf[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(omega * static_cast<double>(i)));
    return buf;
}

double computeRmsDb(const std::vector<float>& a, const std::vector<float>& b)
{
    REQUIRE(a.size() == b.size());
    double sumSq = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += d * d;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(a.size()));
    if (rms <= 1e-30) return -200.0;
    return 20.0 * std::log10(rms);
}

} // namespace

TEST_CASE("ToneShaperBypass: 1 kHz sine through all-bypassed ToneShaper is within "
          "−120 dBFS of input (FR-045)",
          "[membrum][tone_shaper][bypass]")
{
    Membrum::ToneShaper ts;
    ts.prepare(kSampleRate);

    // All stages at bypass values.
    ts.setDriveAmount(0.0f);
    ts.setFoldAmount(0.0f);
    ts.setFilterCutoff(20000.0f);
    ts.setFilterEnvAmount(0.0f);
    ts.setPitchEnvTimeMs(0.0f);

    REQUIRE(ts.isBypassed());

    ts.noteOn(0.8f);

    const auto input = makeSine(kLength, kTestFreq, kSampleRate);
    std::vector<float> output(static_cast<std::size_t>(kLength), 0.0f);
    for (int i = 0; i < kLength; ++i)
        output[static_cast<std::size_t>(i)] =
            ts.processSample(input[static_cast<std::size_t>(i)]);

    const double rmsDb = computeRmsDb(input, output);
    INFO("ToneShaper bypass RMS difference: " << rmsDb << " dBFS");
    CHECK(rmsDb <= -120.0);
}

TEST_CASE("ToneShaperBypass: pitchEnvTimeMs=0 returns naturalFundamentalHz (contract item 6)",
          "[membrum][tone_shaper][bypass]")
{
    Membrum::ToneShaper ts;
    ts.prepare(kSampleRate);

    // Pitch envelope disabled, but natural fundamental set.
    ts.setPitchEnvTimeMs(0.0f);
    ts.setPitchEnvStartHz(160.0f);
    ts.setPitchEnvEndHz(50.0f);
    ts.setNaturalFundamentalHz(158.11f);  // = 500 * 0.1^0.5

    ts.noteOn(0.8f);

    // processPitchEnvelope should return the Size-derived natural fundamental,
    // NOT pitchEnvStartHz_.
    const float firstCall = ts.processPitchEnvelope();
    CHECK(firstCall == Approx(158.11f).epsilon(0.001f));

    // Multiple calls should remain stable. Accumulate worst-case deviation in
    // the loop and assert once after to avoid per-iteration CHECK overhead.
    float worstDeviation = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        const float hz = ts.processPitchEnvelope();
        worstDeviation = std::max(worstDeviation, std::abs(hz - 158.11f));
    }
    CHECK(worstDeviation <= 158.11f * 0.001f);
}

// ==============================================================================
// Fix plan T2.15 (MB-044, MB-013): the FR-045 filter bypass is the Lowpass at
// the top of the cutoff knob only. A Highpass or Bandpass at 20 kHz used to be
// replaced by a full-band passthrough, a 20 dB jump between norm 0.999 and 1.0;
// and the default pad's 20 kHz endpoint came from pow(1000, 1.0), which the
// macOS fast-math rewrite (exp2(x * log2 1000)) lands at 19999.998.
// ==============================================================================

namespace {

constexpr int kNoiseLength = 8192;

std::vector<float> makeSeededNoise()
{
    Krate::DSP::Xorshift32 rng(12345u);
    std::vector<float> buf(static_cast<std::size_t>(kNoiseLength), 0.0f);
    for (auto& s : buf)
        s = 0.5f * rng.nextFloat();
    return buf;
}

std::vector<float> renderShaper(Membrum::ToneShaperFilterType type, float cutoffHz,
                                const std::vector<float>& input)
{
    Membrum::ToneShaper ts;
    ts.prepare(kSampleRate);
    ts.setFilterType(type);
    ts.setFilterCutoff(cutoffHz);
    ts.noteOn(0.8f);
    std::vector<float> out(input.size(), 0.0f);
    for (std::size_t i = 0; i < input.size(); ++i)
        out[i] = ts.processSample(input[i]);
    return out;
}

double rmsDb(const std::vector<float>& x)
{
    double sumSq = 0.0;
    for (const float s : x)
        sumSq += static_cast<double>(s) * static_cast<double>(s);
    const double rms = std::sqrt(sumSq / static_cast<double>(x.size()));
    return 20.0 * std::log10(std::max(rms, 1e-30));
}

} // namespace

TEST_CASE("ToneShaperBypass: only the Lowpass at the top of the cutoff knob is bypassed",
          "[tone_shaper][bypass]")
{
    const auto noise = makeSeededNoise();

    SECTION("Highpass and Bandpass stay continuous between norm 0.999 and 1.0")
    {
        const float cutoff0999 = 20.0f * std::pow(1000.0f, 0.999f);
        for (const auto type : {Membrum::ToneShaperFilterType::Highpass,
                                Membrum::ToneShaperFilterType::Bandpass})
        {
            const double db0999 = rmsDb(renderShaper(type, cutoff0999, noise));
            const double db1000 = rmsDb(renderShaper(type, 20000.0f, noise));
            INFO("type " << static_cast<int>(type) << ": RMS at norm 0.999 = " << db0999
                         << " dBFS, at norm 1.0 = " << db1000 << " dBFS");
            REQUIRE(std::abs(db0999 - db1000) <= 1.0);
        }
    }

    SECTION("Lowpass at 20 kHz is the bit-identical bypass")
    {
        Membrum::ToneShaper ts;
        ts.prepare(kSampleRate);
        ts.setFilterType(Membrum::ToneShaperFilterType::Lowpass);
        ts.setFilterCutoff(20000.0f);
        REQUIRE(ts.isBypassed());

        const auto out =
            renderShaper(Membrum::ToneShaperFilterType::Lowpass, 20000.0f, noise);
        std::size_t differing = 0;
        for (std::size_t i = 0; i < noise.size(); ++i)
            if (out[i] != noise[i])
                ++differing;
        REQUIRE(differing == 0);
    }

    SECTION("A default pad sits exactly on 20000 Hz and is bypassed (MB-013)")
    {
        auto voice = std::make_unique<Membrum::DrumVoice>();
        voice->prepare(kSampleRate, 0u);
        Membrum::applyPadConfigToVoice(*voice, Membrum::PadConfig{});
        REQUIRE(voice->toneShaper().getFilterCutoff() == 20000.0f);
        REQUIRE(voice->toneShaper().isBypassed());
    }
}
