// ==============================================================================
// Pitch Envelope 808 kick test (Phase 7 — T090)
// ==============================================================================
// SC-009 / tone_shaper_contract.md §808 kick test:
//   Configure Pitch Env Start=160 Hz, End=50 Hz, Time=20 ms, Curve=Exp.
//   Trigger Impulse + Membrane at velocity 100.
//   Measure fundamental pitch via short-time FFT over the first 50 ms.
//   At t = 20 ms, the fundamental MUST be within ±10% of 50 Hz.
//
// Also covers:
//   - Disabled envelope returns Size-derived fundamental (contract item 6).
//   - Zero-duration edge case (contract item 7): no NaN, no divide-by-zero.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "dsp/tone_shaper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr double kPi         = 3.14159265358979323846;

// Goertzel-based frequency magnitude at the given target Hz over a window.
double goertzelMagnitude(const float* samples, int numSamples,
                         double sampleRate, double targetHz)
{
    const double omega = 2.0 * kPi * targetHz / sampleRate;
    const double coeff = 2.0 * std::cos(omega);
    double s1 = 0.0;
    double s2 = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const double s = static_cast<double>(samples[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double real = s1 - s2 * std::cos(omega);
    const double imag = s2 * std::sin(omega);
    return std::sqrt(real * real + imag * imag);
}

// Scan a frequency range and return the peak frequency via Goertzel scan.
double peakFrequencyInWindow(const float* samples, int numSamples,
                             double sampleRate, double fMin, double fMax, int numBins)
{
    double bestHz  = fMin;
    double bestMag = 0.0;
    for (int i = 0; i < numBins; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(numBins - 1);
        const double f = fMin + t * (fMax - fMin);
        const double m = goertzelMagnitude(samples, numSamples, sampleRate, f);
        if (m > bestMag)
        {
            bestMag = m;
            bestHz  = f;
        }
    }
    return bestHz;
}

bool hasNaNOrInf(const std::vector<float>& buf)
{
    for (float s : buf)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &s, sizeof(bits));
        const std::uint32_t expMask = 0x7F800000u;
        const std::uint32_t mantMask = 0x007FFFFFu;
        const bool isInfOrNaN = (bits & expMask) == expMask;
        if (isInfOrNaN)
            return true;
        (void)mantMask;
    }
    return false;
}

} // namespace

TEST_CASE("808Kick: PitchEnv Start=160 End=50 Time=20 ms gives ~50 Hz at t=20 ms (SC-009)",
          "[membrum][tone_shaper][pitch_env][808Kick]")
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate);

    // Default patch params.
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.5f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);

    // Configure the pitch envelope.
    voice.toneShaper().setPitchEnvStartHz(160.0f);
    voice.toneShaper().setPitchEnvEndHz(50.0f);
    voice.toneShaper().setPitchEnvTimeMs(20.0f);
    voice.toneShaper().setPitchEnvCurve(Membrum::ToneShaperCurve::Exponential);

    voice.noteOn(100.0f / 127.0f);

    // Render 200 ms to get a good post-sweep analysis window.
    const int kTotalSamples = static_cast<int>(kSampleRate * 0.200);
    std::vector<float> buf(static_cast<std::size_t>(kTotalSamples), 0.0f);
    for (int i = 0; i < kTotalSamples; ++i)
        buf[static_cast<std::size_t>(i)] = voice.process();

    REQUIRE_FALSE(hasNaNOrInf(buf));

    // At t=20 ms the pitch envelope has just finished sweeping from 160 Hz
    // to 50 Hz. Subsequent samples ring at the 50 Hz fundamental. Analyse
    // the post-sweep window (20..100 ms = 80 ms = 3528 samples → ~12.5 Hz
    // DFT bin width, sub-bin accuracy via dense Goertzel scan).
    const int analysisStart = static_cast<int>(kSampleRate * 0.020);
    const int analysisEnd   = static_cast<int>(kSampleRate * 0.100);
    const int analysisLen   = analysisEnd - analysisStart;
    REQUIRE(analysisLen > 0);

    // Scan 30..80 Hz for the peak; this covers 50 Hz ± 60% with room to spare.
    const double measuredHz =
        peakFrequencyInWindow(buf.data() + analysisStart, analysisLen, kSampleRate,
                              30.0, 80.0, 512);

    INFO("Measured fundamental post-sweep (20..100 ms): " << measuredHz
         << " Hz (expected 50 Hz ± 10%)");
    // ±10% of 50 Hz = [45, 55]
    CHECK(measuredHz >= 45.0);
    CHECK(measuredHz <= 55.0);
}

TEST_CASE("808Kick: disabled pitch envelope returns Size-derived fundamental",
          "[membrum][tone_shaper][pitch_env]")
{
    Membrum::ToneShaper ts;
    ts.prepare(kSampleRate);

    ts.setPitchEnvTimeMs(0.0f);
    ts.setPitchEnvStartHz(160.0f);
    ts.setPitchEnvEndHz(50.0f);

    // Size-derived for size=0.5 is 500 * 0.1^0.5 = 158.1138... Hz.
    const float natural = 500.0f * std::pow(0.1f, 0.5f);
    ts.setNaturalFundamentalHz(natural);
    ts.noteOn(1.0f);

    const float hz = ts.processPitchEnvelope();
    CHECK(hz == Approx(natural).epsilon(1e-5f));

    // Should NOT interpolate (repeat call returns same value). Accumulate
    // worst-case deviation in the loop and assert once after.
    float worstDeviation = 0.0f;
    for (int i = 0; i < 500; ++i)
    {
        const float current = ts.processPitchEnvelope();
        worstDeviation = std::max(worstDeviation, std::abs(current - natural));
    }
    CHECK(worstDeviation <= natural * 1e-5f);
}

TEST_CASE("808Kick: zero-duration edge case produces no NaN/Inf",
          "[membrum][tone_shaper][pitch_env][edge]")
{
    Membrum::ToneShaper ts;
    ts.prepare(kSampleRate);

    // Set a tiny non-zero time; should clamp to 1 ms internally.
    ts.setPitchEnvTimeMs(0.001f);
    ts.setPitchEnvStartHz(160.0f);
    ts.setPitchEnvEndHz(50.0f);
    ts.setPitchEnvCurve(Membrum::ToneShaperCurve::Exponential);

    ts.noteOn(1.0f);

    bool sawInvalid = false;
    float minHz = 1e9f;
    float maxHz = -1e9f;
    for (int i = 0; i < 2000; ++i)
    {
        const float hz = ts.processPitchEnvelope();
        std::uint32_t bits = 0;
        std::memcpy(&bits, &hz, sizeof(bits));
        const bool isInfOrNaN = (bits & 0x7F800000u) == 0x7F800000u;
        if (isInfOrNaN) sawInvalid = true;
        if (hz < minHz) minHz = hz;
        if (hz > maxHz) maxHz = hz;
    }
    CHECK_FALSE(sawInvalid);
    CHECK(minHz >= 30.0f);
    CHECK(maxHz <= 200.0f);
}
