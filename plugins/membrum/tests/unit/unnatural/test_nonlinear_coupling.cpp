// ==============================================================================
// Nonlinear Coupling tests -- Phase 8, T105
// ==============================================================================
// Covers unnatural_zone_contract.md "Nonlinear Coupling" section and
// FR-053, FR-056, SC-008.
//
// (a) amount=0.5 spectral centroid varies > 10 % over 500 ms (time-varying
//     character, US6-4).
// (b) every body x every exciter x amount=1 x velocity=1 peak <= 0 dBFS
//     (energy limiter, SC-008).
// (c) amount=0 exact bypass: processSample(x) == x.
// (d) alloc-free: processSample() zero heap.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/unnatural/nonlinear_coupling.h"
#include "dsp/drum_voice.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// Spectral centroid over a 128-bin linear grid.
double spectralCentroid(const float* samples, int start, int len,
                        double sampleRate, double fLow, double fHigh)
{
    constexpr int kBins = 128;
    const double df = (fHigh - fLow) / static_cast<double>(kBins - 1);
    double num = 0.0, den = 0.0;
    for (int b = 0; b < kBins; ++b)
    {
        const double f = fLow + df * static_cast<double>(b);
        const double w = 2.0 * 3.14159265358979323846 * f / sampleRate;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double ang = w * static_cast<double>(i);
            re += samples[start + i] * std::cos(ang);
            im -= samples[start + i] * std::sin(ang);
        }
        const double mag = std::sqrt(re * re + im * im);
        num += f * mag;
        den += mag;
    }
    return den > 0.0 ? num / den : 0.0;
}

} // namespace

// ==============================================================================
// T105(a) -- Spectral centroid varies > 10% between early and late windows
// when NonlinearCoupling amount == 0.5 on a Plate body.
// ==============================================================================

TEST_CASE("UnnaturalZone NonlinearCoupling -- spectral centroid varies when engaged",
          "[UnnaturalZone][NonlinearCoupling]")
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate);
    voice.setMaterial(0.7f);
    voice.setSize(0.5f);
    voice.setDecay(0.8f);
    voice.setStrikePosition(0.37f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Plate);
    voice.unnaturalZone().nonlinearCoupling.setAmount(0.5f);

    constexpr int numSamples = 22050; // 500 ms
    voice.noteOn(1.0f);
    std::vector<float> buf(static_cast<std::size_t>(numSamples), 0.0f);
    for (int i = 0; i < numSamples; ++i) buf[static_cast<std::size_t>(i)] = voice.process();

    // Early window: 0-100 ms.
    const int earlyStart = 0;
    const int earlyLen   = 4410;
    // Late window: 300-400 ms.
    const int lateStart  = 13230;
    const int lateLen    = 4410;

    const double ceEarly =
        spectralCentroid(buf.data(), earlyStart, earlyLen, kSampleRate, 50.0, 8000.0);
    const double ceLate  =
        spectralCentroid(buf.data(), lateStart, lateLen, kSampleRate, 50.0, 8000.0);

    INFO("early centroid = " << ceEarly << " Hz, late centroid = " << ceLate << " Hz");

    // Must differ by > 10% (contract item 6).
    const double relDelta = std::abs(ceEarly - ceLate) /
                            std::max(std::abs(ceEarly), 1e-6);
    INFO("rel delta = " << relDelta);
    CHECK(relDelta > 0.10);
}

// ==============================================================================
// T105(b) -- Energy limiter: every body x every exciter x amount=1 x velocity=1
// -> peak <= 0 dBFS over 1 s (SC-008).
// ==============================================================================

TEST_CASE("UnnaturalZone NonlinearCoupling -- energy limiter peak safety",
          "[UnnaturalZone][NonlinearCoupling][SC008]")
{
    constexpr int numSamples = 44100; // 1 s

    const Membrum::ExciterType   exciters[] = {
        Membrum::ExciterType::Impulse,
        Membrum::ExciterType::Mallet,
        Membrum::ExciterType::NoiseBurst,
        Membrum::ExciterType::Friction,
        Membrum::ExciterType::FMImpulse,
        Membrum::ExciterType::Feedback,
    };
    const Membrum::BodyModelType bodies[] = {
        Membrum::BodyModelType::Membrane,
        Membrum::BodyModelType::Plate,
        Membrum::BodyModelType::Shell,
        Membrum::BodyModelType::String,
        Membrum::BodyModelType::Bell,
        Membrum::BodyModelType::NoiseBody,
    };

    bool allSafe = true;
    float worstPeak = 0.0f;
    for (auto ex : exciters)
    {
        for (auto body : bodies)
        {
            Membrum::DrumVoice voice;
            voice.prepare(kSampleRate);
            voice.setMaterial(0.7f);
            voice.setSize(0.5f);
            voice.setDecay(0.8f);
            voice.setStrikePosition(0.37f);
            voice.setLevel(0.8f);
            voice.setExciterType(ex);
            voice.setBodyModel(body);
            voice.unnaturalZone().nonlinearCoupling.setAmount(1.0f);
            voice.noteOn(1.0f);

            float peak = 0.0f;
            std::array<float, 256> block{};
            for (int b = 0; b < numSamples / 256; ++b)
            {
                voice.processBlock(block.data(), 256);
                for (float s : block)
                {
                    if (!isFiniteSample(s))
                    {
                        allSafe = false;
                        peak = 2.0f;
                        break;
                    }
                    peak = std::max(peak, std::abs(s));
                }
                if (!allSafe) break;
            }
            if (peak > worstPeak) worstPeak = peak;
            if (peak > 1.0f)
            {
                allSafe = false;
                break;
            }
        }
        if (!allSafe) break;
    }

    INFO("Worst peak across all 36 combinations = " << worstPeak);
    CHECK(allSafe);
    CHECK(worstPeak <= 1.0f);
}

// ==============================================================================
// T105(c) -- amount == 0.0 is an exact bypass (processSample(x) == x).
// ==============================================================================

TEST_CASE("UnnaturalZone NonlinearCoupling -- amount==0 exact bypass",
          "[UnnaturalZone][NonlinearCoupling][DefaultsOff]")
{
    Membrum::NonlinearCoupling coupling;
    coupling.prepare(kSampleRate);
    coupling.setAmount(0.0f);
    coupling.setVelocity(1.0f);

    const float xs[] = {0.0f, 0.5f, -0.5f, 0.9f, -0.9f, 0.001f, 1e-6f};
    for (float x : xs)
        CHECK(coupling.processSample(x) == x);

    // Even after previous coupling activity, amount=0 yields exact bypass.
    coupling.setAmount(0.5f);
    for (int i = 0; i < 100; ++i)
        (void) coupling.processSample(0.7f);

    coupling.setAmount(0.0f);
    for (float x : xs)
        CHECK(coupling.processSample(x) == x);
}

// ==============================================================================
// T105(d) -- Allocation detector: processSample is zero-heap.
// ==============================================================================

TEST_CASE("UnnaturalZone NonlinearCoupling -- zero heap allocations on audio thread",
          "[UnnaturalZone][NonlinearCoupling][allocation]")
{
    Membrum::NonlinearCoupling coupling;
    coupling.prepare(kSampleRate);
    coupling.setAmount(0.7f);
    coupling.setVelocity(1.0f);

    // Warm up.
    for (int i = 0; i < 256; ++i) (void) coupling.processSample(0.5f);

    {
        TestHelpers::AllocationScope scope;
        for (int i = 0; i < 2048; ++i)
            (void) coupling.processSample(0.5f + 0.1f * std::sin(0.01f * i));
        const size_t count = scope.getAllocationCount();
        INFO("processSample alloc count = " << count);
        CHECK(count == 0u);
    }
}
