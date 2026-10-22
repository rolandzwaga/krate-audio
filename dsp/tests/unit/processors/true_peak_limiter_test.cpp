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

    // Skip a few samples for the IIR detector to settle (zero latency -> no
    // look-ahead delay to skip).
    const int skip = 64;
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

// Profundum Phase 9 T029: the halfband detector (opt-in) under the same bars as the IIR detector.
TEST_CASE("TruePeakLimiter halfband detector bounds a hot signal to the ceiling",
          "[true_peak_limiter][gain_staging][profundum]")
{
    const int n = 4096;
    TruePeakLimiter lim;
    lim.prepare(kSr, n);
    lim.setCeilingDb(-1.0f);
    lim.setHalfbandDetector(true);
    REQUIRE(lim.halfbandDetector());
    const float ceil = lim.getCeilingLinear();

    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i)
    {
        const float s = 2.0f * std::sin(2.0f * 3.14159265f * 1000.0f
                                        * static_cast<float>(i) / static_cast<float>(kSr));
        L[i] = s;
        R[i] = s;
    }
    lim.processBlock(L.data(), R.data(), n);

    const int skip = 64;
    float samplePeak = 0.0f;
    std::vector<float> tail;
    for (int i = skip; i < n; ++i)
    {
        samplePeak = std::max(samplePeak, std::fabs(L[i]));
        tail.push_back(L[i]);
    }
    REQUIRE(samplePeak <= ceil + 1.0e-4f);   // sample-level guarantee, exact
    const float tp = measureTruePeak(tail);
    REQUIRE(tp <= 1.0f);
    REQUIRE(tp <= ceil + 0.06f);             // within ~0.5 dB of the -1 dBTP target

    // Quiet input is untouched (gain stays 1) and the output is not delayed.
    TruePeakLimiter quiet;
    quiet.prepare(kSr, n);
    quiet.setHalfbandDetector(true);
    std::vector<float> qL(n, 0.0f), qR(n, 0.0f), qIn(n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        qIn[i] = 0.25f * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / static_cast<float>(kSr));
        qL[i] = qIn[i];
        qR[i] = qIn[i];
    }
    quiet.processBlock(qL.data(), qR.data(), n);
    for (int i = 0; i < n; ++i)
        REQUIRE(qL[i] == Approx(qIn[i]).margin(1.0e-6));
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

    // Zero latency + below threshold => unity gain, output equals input at the
    // SAME index, bit-exact.
    for (int i = 0; i < n; ++i)
        REQUIRE(L[i] == Approx(in[i]).margin(1.0e-6));
}

TEST_CASE("TruePeakLimiter adds no latency (output not delayed)", "[true_peak_limiter][gain_staging]")
{
    const int n = 256;
    TruePeakLimiter lim;
    lim.prepare(kSr, n);
    lim.setCeilingDb(-1.0f);

    std::vector<float> L(n, 0.0f), R(n, 0.0f);
    L[100] = 0.5f; R[100] = 0.5f;  // below the ceiling -> passes at unity
    lim.processBlock(L.data(), R.data(), n);

    REQUIRE(std::fabs(L[100]) > 0.4f);            // energy at the SAME index
    REQUIRE(L[99] == Approx(0.0f).margin(1.0e-6)); // nothing leaks earlier
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

    const int skip = 8;
    for (int i = skip; i < n; ++i)
    {
        REQUIRE(std::isfinite(L[i]));
        REQUIRE(std::isfinite(R[i]));
        REQUIRE(std::fabs(L[i]) <= ceil + 1.0e-4f);
        REQUIRE(std::fabs(R[i]) <= ceil + 1.0e-4f);
    }
}

// S1: a host block larger than the prepared maxBlockSize must not overflow the
// oversampling scratch buffers (processBlock chunks internally).
TEST_CASE("TruePeakLimiter handles blocks larger than maxBlockSize",
          "[true_peak_limiter][gain_staging]")
{
    TruePeakLimiter lim;
    lim.prepare(kSr, /*maxBlockSize*/ 64);  // deliberately small
    lim.setCeilingDb(-1.0f);
    const float ceil = lim.getCeilingLinear();

    const int n = 300;  // > maxBlockSize -> exercises internal chunking
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i)
    {
        const float s = 2.0f * std::sin(2.0f * 3.14159265f * 1000.0f
                                        * static_cast<float>(i) / static_cast<float>(kSr));
        L[i] = s; R[i] = s;
    }

    lim.processBlock(L.data(), R.data(), n);  // must not crash / overflow

    for (int i = 96; i < n; ++i)
    {
        REQUIRE(std::isfinite(L[i]));
        REQUIRE(std::fabs(L[i]) <= ceil + 1.0e-4f);
    }
}


// The header's sample-level guarantee is exact: |out| <= ceiling with no margin. A gain of
// ceiling / tp applied to a sample with |sample| == tp can round one ulp ABOVE the ceiling in
// float (found by the Profundum Phase 6 chain gates, SC-014(c) / SC-021), so this sweeps many
// settled DC levels above the ceiling, where tp == |sample| and the rounding is exposed.
TEST_CASE("TruePeakLimiter sample-level ceiling is exact (no float overshoot)",
          "[true_peak_limiter][gain_staging]")
{
    constexpr int kLevels = 2000;
    constexpr int n = 256;
    int over = 0;
    float worst = 0.0f;
    float ceil = 0.0f;
    for (int k = 0; k < kLevels; ++k)
    {
        TruePeakLimiter lim;
        lim.prepare(kSr, n);
        lim.setCeilingDb(-1.0f);
        ceil = lim.getCeilingLinear();
        const float level = 0.9f + 3.1f * static_cast<float>(k) / static_cast<float>(kLevels);
        std::vector<float> L(n, level), R(n, -level);
        lim.processBlock(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i)
        {
            for (const float v : {L[i], R[i]})
            {
                const float a = std::fabs(v);
                worst = std::max(worst, a);
                if (a > ceil)
                    ++over;
            }
        }
    }
    INFO("samples above the ceiling: " << over << ", worst |out| - ceiling = " << (worst - ceil));
    CHECK(over == 0);
}

// -----------------------------------------------------------------------------
// Profundum Phase 9 G14-1 (user ruling 2026-10-21): the opt-in look-ahead
// -----------------------------------------------------------------------------

namespace {
/// A hot, asymmetric, harmonic-rich bass tone: the material on which the zero-look-ahead limiter's
/// sample-by-sample gain steps read +1.02 dBTP (SC-013) and -46 dBFS of off-lattice content (SC-021).
std::vector<float> hotAsymmetricTone(int n)
{
    std::vector<float> x(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSr);
        float v = 0.0f;
        for (int h = 1; h <= 40; ++h)
            v += std::sin(2.0f * 3.14159265f * 41.2f * static_cast<float>(h) * t + 0.3f * static_cast<float>(h))
                 / static_cast<float>(h);
        v = v > 0.0f ? 2.6f * v : 1.1f * v;   // asymmetric, well over the ceiling
        x[static_cast<std::size_t>(i)] = v;
    }
    return x;
}
}  // namespace

TEST_CASE("TruePeakLimiter look-ahead is off by default and bit-identical to the zero-latency path",
          "[true_peak_limiter][gain_staging][profundum]")
{
    const int n = 8192;
    TruePeakLimiter a, b;
    a.prepare(kSr, 512);
    b.setLookaheadSamples(0);
    b.prepare(kSr, 512);
    REQUIRE(a.lookaheadSamples() == 0u);
    REQUIRE(b.lookaheadSamples() == 0u);
    const std::vector<float> in = hotAsymmetricTone(n);
    std::vector<float> aL = in, aR = in, bL = in, bR = in;
    a.processBlock(aL.data(), aR.data(), n);
    b.processBlock(bL.data(), bR.data(), n);
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(aL[static_cast<std::size_t>(i)] == bL[static_cast<std::size_t>(i)]);
        REQUIRE(aR[static_cast<std::size_t>(i)] == bR[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("TruePeakLimiter look-ahead delays the audio by N samples and passes quiet input bit for bit",
          "[true_peak_limiter][gain_staging][profundum]")
{
    const int n = 4096;
    const std::size_t N = 64;
    TruePeakLimiter lim;
    lim.setLookaheadSamples(N);
    lim.prepare(kSr, 512);
    REQUIRE(lim.lookaheadSamples() == N);
    std::vector<float> in(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        in[static_cast<std::size_t>(i)] =
            0.25f * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / static_cast<float>(kSr));
    std::vector<float> L = in, R = in;
    lim.processBlock(L.data(), R.data(), n);
    for (std::size_t i = 0; i < N; ++i)
        REQUIRE(L[i] == 0.0f);   // the delay line's initial silence
    for (std::size_t i = N; i < static_cast<std::size_t>(n); ++i)
    {
        REQUIRE(L[i] == in[i - N]);
        REQUIRE(R[i] == in[i - N]);
    }
    lim.setLookaheadSamples(TruePeakLimiter::kMaxLookaheadSamples + 5);
    lim.prepare(kSr, 512);
    REQUIRE(lim.lookaheadSamples() == TruePeakLimiter::kMaxLookaheadSamples);   // clamped
}

TEST_CASE("TruePeakLimiter look-ahead bounds the true peak where the zero-latency path overshoots",
          "[true_peak_limiter][gain_staging][profundum]")
{
    const int n = 48000;
    const std::vector<float> in = hotAsymmetricTone(n);

    TruePeakLimiter zero;
    zero.prepare(kSr, 512);
    zero.setCeilingDb(-1.0f);
    zero.setHalfbandDetector(true);
    std::vector<float> zL = in, zR = in;
    zero.processBlock(zL.data(), zR.data(), n);

    TruePeakLimiter la;
    la.setLookaheadSamples(64);
    la.prepare(kSr, 512);
    la.setCeilingDb(-1.0f);
    la.setHalfbandDetector(true);
    std::vector<float> lL = in, lR = in;
    la.processBlock(lL.data(), lR.data(), n);

    const float ceil = la.getCeilingLinear();
    const int skip = 4800;   // past the release settling
    float zPeak = 0.0f, lPeak = 0.0f;
    std::vector<float> zTail, lTail;
    for (int i = skip; i < n; ++i)
    {
        zPeak = std::max(zPeak, std::fabs(zL[static_cast<std::size_t>(i)]));
        lPeak = std::max(lPeak, std::fabs(lL[static_cast<std::size_t>(i)]));
        zTail.push_back(zL[static_cast<std::size_t>(i)]);
        lTail.push_back(lL[static_cast<std::size_t>(i)]);
    }
    REQUIRE(zPeak <= ceil + 1.0e-4f);   // both keep the sample-level guarantee
    REQUIRE(lPeak <= ceil + 1.0e-4f);
    const float zTp = measureTruePeak(zTail);
    const float lTp = measureTruePeak(lTail);
    INFO("true peak: zero-look-ahead " << 20.0f * std::log10(zTp) << " dBTP, look-ahead 64 "
                                        << 20.0f * std::log10(lTp) << " dBTP, ceiling " << 20.0f * std::log10(ceil));
    REQUIRE(lTp <= ceil * 1.0116f);   // within 0.1 dB of the ceiling (SC-013's allowance)
    REQUIRE(lTp < zTp);               // and below what the stepping gain lets through
}
