// ==============================================================================
// Unit Tests: TruePeakLimiter (look-ahead true-peak brickwall)
// ==============================================================================
// Layer 2: DSP Processor Tests
//
// Verifies the Membrum gain-staging Step 1 contract:
//  - a hot signal is bounded to the true-peak ceiling (no overshoot)
//  - quiet (below-threshold) signal passes transparently
//  - reported latency == look-ahead samples
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline (DSP independently testable)
// - Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/primitives/oversampler.h>

#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

constexpr double kSr = 48000.0;

// Independent true-peak meter using a HIGH-quality linear-phase FIR oversampler
// (different from the limiter's internal IIR detector) so the check is not
// self-referential.
float measureTruePeak(const std::vector<float>& x)
{
    Oversampler<4, 1> os;
    os.prepare(kSr, x.size(), OversamplingQuality::High, OversamplingMode::LinearPhase);
    std::vector<float> up(x.size() * 4, 0.0f);
    // upsample() is const-correct on input; copy to a mutable buffer for the API.
    std::vector<float> in = x;
    os.upsample(in.data(), up.data(), in.size(), 0);
    float peak = 0.0f;
    for (float s : up)
        peak = std::max(peak, std::fabs(s));
    return peak;
}

} // namespace

TEST_CASE("TruePeakLimiter bounds a hot signal to the ceiling", "[true_peak_limiter][gain_staging]")
{
    const int n = 4096;
    TruePeakLimiter lim;
    lim.prepare(kSr, n);
    lim.setCeilingDb(-1.0f);          // -1 dBTP, linear ~0.8913
    const float ceil = lim.getCeilingLinear();

    // A loud 1 kHz sine at +6 dBFS (amplitude 2.0) — well over the ceiling.
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i)
    {
        const float s = 2.0f * std::sin(2.0f * 3.14159265f * 1000.0f
                                        * static_cast<float>(i) / static_cast<float>(kSr));
        L[i] = s;
        R[i] = s;
    }

    lim.processBlock(L.data(), R.data(), n);

    // Ignore the look-ahead ramp-in region; check the steady state.
    const int skip = static_cast<int>(lim.getLatencySamples()) + 64;
    float samplePeak = 0.0f;
    std::vector<float> tail;
    for (int i = skip; i < n; ++i)
    {
        samplePeak = std::max(samplePeak, std::fabs(L[i]));
        tail.push_back(L[i]);
    }

    // Sample-level guarantee is exact (tp >= |sample|).
    REQUIRE(samplePeak <= ceil + 1.0e-4f);

    // Independent true-peak (4x FIR) must not exceed full scale, and should sit
    // at/under the ceiling within inter-sample detector tolerance.
    const float tp = measureTruePeak(tail);
    REQUIRE(tp <= 1.0f);                 // never reaches 0 dBFS
    REQUIRE(tp <= ceil + 0.06f);         // within ~0.5 dB of the -1 dBTP target
}

TEST_CASE("TruePeakLimiter passes a quiet signal transparently", "[true_peak_limiter][gain_staging]")
{
    const int n = 2048;
    TruePeakLimiter lim;
    lim.prepare(kSr, n);
    lim.setCeilingDb(-1.0f);

    std::vector<float> in(n), L(n), R(n);
    for (int i = 0; i < n; ++i)
    {
        const float s = 0.1f * std::sin(2.0f * 3.14159265f * 220.0f
                                        * static_cast<float>(i) / static_cast<float>(kSr)); // -20 dBFS
        in[i] = s;
        L[i]  = s;
        R[i]  = s;
    }

    lim.processBlock(L.data(), R.data(), n);

    // Audio path is a pure delay × unity gain when below threshold: output at
    // i+latency must equal input at i (bit-exact, gain stayed 1.0).
    const int lat = static_cast<int>(lim.getLatencySamples());
    for (int i = 0; i < n - lat - 1; ++i)
        REQUIRE(L[i + lat] == Approx(in[i]).margin(1.0e-6));
}

TEST_CASE("TruePeakLimiter reports look-ahead latency", "[true_peak_limiter][gain_staging]")
{
    TruePeakLimiter lim;
    lim.prepare(kSr, 512);
    lim.setLookaheadMs(1.0f);
    REQUIRE(lim.getLatencySamples() == static_cast<std::size_t>(std::lround(0.001 * kSr)));
}

TEST_CASE("TruePeakLimiter stays finite on extreme input", "[true_peak_limiter][gain_staging]")
{
    TruePeakLimiter lim;
    lim.prepare(kSr, 256);
    lim.setCeilingDb(-1.0f);
    const float ceil = lim.getCeilingLinear();

    const int n = 256;
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i) { L[i] = (i % 2 == 0) ? 50.0f : -50.0f; R[i] = -L[i]; }

    lim.processBlock(L.data(), R.data(), n);

    const int skip = static_cast<int>(lim.getLatencySamples()) + 8;
    for (int i = skip; i < n; ++i)
    {
        REQUIRE(std::isfinite(L[i]));
        REQUIRE(std::isfinite(R[i]));
        REQUIRE(std::fabs(L[i]) <= ceil + 1.0e-4f);
        REQUIRE(std::fabs(R[i]) <= ceil + 1.0e-4f);
    }
}
