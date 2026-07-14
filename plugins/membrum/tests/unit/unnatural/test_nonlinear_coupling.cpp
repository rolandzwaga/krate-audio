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

// Goertzel magnitude at a single frequency (for harmonic measurement).
double goertzelMag(const float* samples, int start, int len, double sampleRate, double hz)
{
    const double w     = 2.0 * 3.14159265358979323846 * hz / sampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const double s = static_cast<double>(samples[start + i]) + coeff * s1 - s2;
        s2 = s1; s1 = s;
    }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    return std::sqrt(re * re + im * im);
}

} // namespace

// ==============================================================================
// M-3 (AUDIT-signal-path) -- continuity in amount: a tiny coupling amount
// produces a proportionally tiny change in STEADY STATE. The old design ran
// the WHOLE signal through recipSqrt whenever amount != 0, so even amount=0.02
// compressed a held tone by ~10% (recipSqrt(0.5)=0.447) -- a discontinuous
// jump from the exact amount=0 bypass. The AM-only redesign adds only the
// (shaped - body) excess scaled by amount, so the steady-state deviation
// scales with amount and is small for small amount.
// ==============================================================================

TEST_CASE("UnnaturalZone NonlinearCoupling -- small amount is near-bypass in steady state (M-3)",
          "[UnnaturalZone][NonlinearCoupling][M3][continuity]")
{
    Membrum::NonlinearCoupling coupling;
    coupling.prepare(kSampleRate);
    coupling.setAmount(0.02f);
    coupling.setVelocity(1.0f);

    const double omega = 2.0 * 3.14159265358979323846 * 1000.0 / kSampleRate;
    // Warm up so the RMS follower settles to steady state (dEnv -> 0).
    for (int i = 0; i < 8820; ++i)
        (void) coupling.processSample(0.5f * static_cast<float>(std::sin(omega * i)));

    // Measure steady-state deviation over the next window.
    constexpr int kN = 4410;
    double sumIn = 0.0, sumDiff = 0.0;
    for (int i = 8820; i < 8820 + kN; ++i)
    {
        const float in  = 0.5f * static_cast<float>(std::sin(omega * i));
        const float out = coupling.processSample(in);
        sumIn   += static_cast<double>(in) * in;
        sumDiff += static_cast<double>(out - in) * (out - in);
    }
    const double relDev = std::sqrt(sumDiff / std::max(sumIn, 1e-30));
    INFO("steady-state RMS deviation at amount=0.02: " << relDev);
    // With amount=0.02 the steady-state change must be small (was ~0.10 when
    // the whole signal went through recipSqrt regardless of amount).
    CHECK(relDev < 0.05);
}

// ==============================================================================
// M-4 (AUDIT-signal-path) -- the coupling amount has SUSTAINED authority: in a
// held (steady-state) tone, raising amount must measurably increase harmonic
// brightening. The old design's AM term was driven by dEnv, which -> 0 in
// sustain, so steady-state output was recipSqrt(body) INDEPENDENT of amount:
// amount=0.3 and amount=1.0 produced identical steady-state harmonics. The
// env-LEVEL-driven redesign makes the waveshaper drive scale with amount, so
// more amount = more odd-harmonic content that persists through the sustain.
// ==============================================================================

TEST_CASE("UnnaturalZone NonlinearCoupling -- amount drives sustained brightening (M-4)",
          "[UnnaturalZone][NonlinearCoupling][M4][sustained]")
{
    const double omega = 2.0 * 3.14159265358979323846 * 1000.0 / kSampleRate;

    auto steadyThirdHarmonicRatio = [&](float amount) {
        Membrum::NonlinearCoupling coupling;
        coupling.prepare(kSampleRate);
        coupling.setAmount(amount);
        coupling.setVelocity(1.0f);
        // Warm up to steady state.
        for (int i = 0; i < 8820; ++i)
            (void) coupling.processSample(0.6f * static_cast<float>(std::sin(omega * i)));
        constexpr int kN = 8820;
        std::vector<float> out(static_cast<std::size_t>(kN), 0.0f);
        for (int i = 0; i < kN; ++i)
            out[static_cast<std::size_t>(i)] =
                coupling.processSample(0.6f * static_cast<float>(std::sin(omega * (i + 8820))));
        const double fund = goertzelMag(out.data(), 0, kN, kSampleRate, 1000.0);
        const double h3   = goertzelMag(out.data(), 0, kN, kSampleRate, 3000.0);
        return h3 / std::max(fund, 1e-30);
    };

    const double low  = steadyThirdHarmonicRatio(0.30f);
    const double high = steadyThirdHarmonicRatio(1.00f);
    INFO("steady-state 3rd-harmonic ratio: amount=0.30 -> " << low
         << ", amount=1.00 -> " << high);
    // Raising amount must increase sustained harmonic content meaningfully.
    CHECK(high > low * 1.3);
}

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

    // Phase 8A.5 (commit 89cf0c64) decoupled the amp envelope from voice
    // lifetime: sustain=1.0 means the body now rings indefinitely until
    // noteOff. Without a noteOff the RMS envelope follower plateaus and
    // NonlinearCoupling's dEnv term collapses to ~0 in the late window,
    // defeating the time-variance this test exercises. Issue noteOff at
    // 200 ms so the release-phase damp(0.997) (~50 ms body T60) creates
    // fresh dEnv transience inside the 300-400 ms late window.
    constexpr int numSamples    = 22050; // 500 ms
    constexpr int noteOffSample = 8820;  // 200 ms
    voice.noteOn(1.0f);
    std::vector<float> buf(static_cast<std::size_t>(numSamples), 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        if (i == noteOffSample) voice.noteOff();
        buf[static_cast<std::size_t>(i)] = voice.process();
    }

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
// Phase 4 (CRASH-REDESIGN-PLAN.md) -- the brightening is DELAYED: the
// waveshaper drive rides a slow, velocity-dependent envelope follower, so the
// added-harmonic energy PEAKS tens of ms AFTER the strike (the wave-turbulence
// cascade) instead of spiking at the onset edge. Softer hits peak later.
// ==============================================================================

// Added-harmonic (nonlinearity) energy = |output - input| RMS, measured in an
// EARLY window (~0-25 ms, the onset edge) and a LATE window (~150-200 ms). A
// delayed bloom has late > early (the brightening BUILDS); the old fixed-5 ms
// attack front-loaded it (early >= late). Sustained sine so the follower
// attack -- not an input decay -- sets the build shape.
namespace {
struct BuildShape { double earlyRms; double lateRms; };
BuildShape addedEnergyBuild(float velocity, double sr)
{
    Membrum::NonlinearCoupling c;
    c.prepare(sr);
    c.setAmount(0.6f);
    c.setVelocity(velocity);

    const int    n     = static_cast<int>(sr * 0.30);   // 300 ms
    const double omega = 2.0 * 3.14159265358979323846 * 200.0 / sr;
    const int    earlyStart = 0;
    const int    earlyEnd   = static_cast<int>(sr * 0.025);
    const int    lateStart  = static_cast<int>(sr * 0.150);
    const int    lateEnd    = static_cast<int>(sr * 0.200);
    double earlySq = 0.0; int earlyN = 0;
    double lateSq  = 0.0; int lateN  = 0;
    for (int k = 0; k < n; ++k)
    {
        const float in  = static_cast<float>(std::sin(omega * k));  // constant amplitude
        const float out = c.processSample(in);
        const double d  = static_cast<double>(out) - in;
        if (k >= earlyStart && k < earlyEnd) { earlySq += d * d; ++earlyN; }
        if (k >= lateStart  && k < lateEnd)  { lateSq  += d * d; ++lateN;  }
    }
    return { std::sqrt(earlySq / std::max(1, earlyN)),
             std::sqrt(lateSq  / std::max(1, lateN)) };
}
} // namespace

TEST_CASE("UnnaturalZone NonlinearCoupling -- brightening is delayed after onset",
          "[UnnaturalZone][NonlinearCoupling][bloom]")
{
    const BuildShape hard = addedEnergyBuild(1.0f, kSampleRate);
    const BuildShape soft = addedEnergyBuild(0.3f, kSampleRate);
    INFO("hard early=" << hard.earlyRms << " late=" << hard.lateRms
         << " | soft early=" << soft.earlyRms << " late=" << soft.lateRms);
    // Hard hit: brightening BUILDS -- the late window has more added harmonic
    // energy than the onset edge (delayed cascade, not a front spike). The
    // build is modest because recipSqrt shapes even at unit drive (the baseline
    // floor); the env-dependent bloom rides on top. With the old fixed-5 ms
    // attack this ratio was ~1.0 (or inverted); the slow attack makes it > 1.
    CHECK(hard.lateRms > hard.earlyRms * 1.08);
    // Softer hit builds even more gradually: its early/late ratio is smaller
    // (slower follower attack), so its onset edge is relatively quieter.
    const double hardRatio = hard.earlyRms / std::max(1e-9, hard.lateRms);
    const double softRatio = soft.earlyRms / std::max(1e-9, soft.lateRms);
    CHECK(softRatio <= hardRatio + 0.05);
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
