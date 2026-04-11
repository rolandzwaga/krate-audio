// ==============================================================================
// MembraneBody contract tests (Phase 4 -- T048)
// ==============================================================================
// Contract invariants from body_contract.md §Test coverage requirements:
//   1. Allocation-free configureForNoteOn + processSample
//   2. Modal ratios within +/-2% of kMembraneRatios (SC-002)
//   3. Size sweep 0 -> 1 produces fundamental spanning >= 1 octave (US4-1)
//   4. Decay sweep 0 -> 1 produces RT60 >= 3x change (US4-2)
//   5. Finite output with all 6 exciters over 1 s (no NaN/Inf)
//   6. Sample-rate sweep {22050, 44100, 48000, 96000, 192000}
//   7. Mid-note body-switch deferral — no crash, no NaN
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "body_test_helpers.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_bank.h"
#include "dsp/exciter_type.h"
#include "dsp/membrane_modes.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace membrum_body_tests;

TEST_CASE("MembraneBody: configureForNoteOn + processSample is allocation-free",
          "[membrum][body][membrane][alloc]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);
    bank.setBodyModel(Membrum::BodyModelType::Membrane);

    {
        TestHelpers::AllocationScope scope;
        bank.configureForNoteOn(makeDefaultParams(), 160.0f);
        for (int i = 0; i < 1024; ++i)
            (void)bank.processSample(i == 0 ? 1.0f : 0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("MembraneBody: modal ratios within 2% of kMembraneRatios (SC-002)",
          "[membrum][body][membrane][modes][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = 16384;

    Membrum::BodyBank bank;
    auto params = makeDefaultParams();
    params.size = 0.6f;  // fundamental ~125 Hz (500*0.1^0.6 ≈ 125)

    std::vector<float> buf(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Membrane, params, kSR,
                   buf.data(), kN);

    // Find fundamental (mode 0). Expected f0 = 500 * 0.1^0.6 ≈ 125.9 Hz.
    const double expectedF0 = 500.0 * std::pow(0.1, 0.6);
    const double measuredF0 = findPeakFrequency(
        buf.data(), kN, kSR, expectedF0 * 0.7, expectedF0 * 1.3, 256);
    REQUIRE(measuredF0 == Approx(expectedF0).epsilon(0.05));

    // Verify a few higher Bessel ratios show up as peaks within +/-2% tolerance.
    const float tolerance = 0.02f;
    for (int k = 1; k <= 4; ++k)
    {
        const double expectedHz = measuredF0 * Membrum::kMembraneRatios[k];
        const double lo = expectedHz * (1.0 - tolerance * 1.5);
        const double hi = expectedHz * (1.0 + tolerance * 1.5);
        const double foundHz =
            findPeakFrequency(buf.data(), kN, kSR, lo, hi, 128);
        INFO("Mode " << k << " expected " << expectedHz << " found " << foundHz);
        CHECK(foundHz == Approx(expectedHz).epsilon(tolerance));
    }
}

TEST_CASE("MembraneBody: Size sweep covers >= 1 octave (US4-1)",
          "[membrum][body][membrane][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = 8192;

    Membrum::BodyBank bank;

    auto p0 = makeDefaultParams();
    p0.size = 0.0f;
    std::vector<float> buf0(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Membrane, p0, kSR,
                   buf0.data(), kN);
    const double expectedF0Size0 = 500.0;
    const double f0a = findPeakFrequency(
        buf0.data(), kN, kSR, 100.0, 1000.0, 256);

    auto p1 = makeDefaultParams();
    p1.size = 1.0f;
    std::vector<float> buf1(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Membrane, p1, kSR,
                   buf1.data(), kN);
    const double f0b = findPeakFrequency(
        buf1.data(), kN, kSR, 20.0, 200.0, 256);

    INFO("Size=0 f0≈" << f0a << " (expected " << expectedF0Size0 << ")"
         << "  Size=1 f0≈" << f0b);
    CHECK(f0a > 0.0);
    CHECK(f0b > 0.0);
    CHECK((f0a / f0b) >= 2.0);  // at least one octave
}

TEST_CASE("MembraneBody: Decay sweep produces >= 3x RT60 range (US4-2)",
          "[membrum][body][membrane][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 2.0);

    Membrum::BodyBank bank;

    auto pLow = makeDefaultParams();
    pLow.decay = 0.0f;
    std::vector<float> bufLow(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Membrane, pLow, kSR,
                   bufLow.data(), kN);
    const float rt60Low = estimateRT60(bufLow.data(), kN, kSR);

    auto pHigh = makeDefaultParams();
    pHigh.decay = 1.0f;
    std::vector<float> bufHigh(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Membrane, pHigh, kSR,
                   bufHigh.data(), kN);
    const float rt60High = estimateRT60(bufHigh.data(), kN, kSR);

    INFO("RT60 low=" << rt60Low << " high=" << rt60High);
    REQUIRE(rt60Low > 0.0f);
    REQUIRE(rt60High > 0.0f);
    CHECK((rt60High / rt60Low) >= 3.0f);
}

TEST_CASE("MembraneBody: finite output across all exciters (SC-007)",
          "[membrum][body][membrane][finite]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR);  // 1 second

    const Membrum::ExciterType exciters[] = {
        Membrum::ExciterType::Impulse,
        Membrum::ExciterType::Mallet,
        Membrum::ExciterType::NoiseBurst,
        Membrum::ExciterType::Friction,
        Membrum::ExciterType::FMImpulse,
        Membrum::ExciterType::Feedback,
    };

    for (auto et : exciters)
    {
        Membrum::ExciterBank excBank;
        Membrum::BodyBank    bank;
        excBank.prepare(kSR, 0u);
        bank.prepare(kSR, 0u);
        excBank.setExciterType(et);
        bank.setBodyModel(Membrum::BodyModelType::Membrane);
        bank.configureForNoteOn(makeDefaultParams(), 160.0f);
        excBank.trigger(0.8f);

        bool allFinite = true;
        float lastBody = 0.0f;
        for (int i = 0; i < kN; ++i)
        {
            const float exc  = excBank.process(lastBody);
            const float body = bank.processSample(exc);
            lastBody = body;
            if (!isFiniteSample(body))
            {
                allFinite = false;
                break;
            }
        }
        INFO("Exciter=" << static_cast<int>(et));
        CHECK(allFinite);
    }
}

TEST_CASE("MembraneBody: sample-rate sweep produces finite modal output",
          "[membrum][body][membrane][samplerate]")
{
    const double sampleRates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    for (double sr : sampleRates)
    {
        const int kN = 4096;
        Membrum::BodyBank bank;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::Membrane,
                       makeDefaultParams(), sr, buf.data(), kN);

        bool allFinite = true;
        for (int i = 0; i < kN; ++i)
        {
            if (!isFiniteSample(buf[i])) { allFinite = false; break; }
        }
        INFO("sampleRate=" << sr);
        CHECK(allFinite);

        // Fundamental should be reasonable: f0 = 500 * 0.1^0.5 ≈ 158 Hz
        const double expectedF0 = 500.0 * std::pow(0.1, 0.5);
        const double measuredF0 = findPeakFrequency(
            buf.data(), kN, sr, expectedF0 * 0.5, expectedF0 * 2.0, 256);
        CHECK(measuredF0 > 0.0);
    }
}

TEST_CASE("MembraneBody: mid-note body switch deferred, no crash",
          "[membrum][body][membrane][deferred]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0u);
    bank.setBodyModel(Membrum::BodyModelType::Membrane);
    bank.configureForNoteOn(makeDefaultParams(), 160.0f);

    // Kick with impulse, start ringing.
    (void)bank.processSample(1.0f);

    // Request a body-model switch mid-note — should be deferred.
    bank.setBodyModel(Membrum::BodyModelType::Plate);
    CHECK(bank.getCurrentType() == Membrum::BodyModelType::Membrane);
    CHECK(bank.getPendingType() == Membrum::BodyModelType::Plate);

    bool allFinite = true;
    for (int i = 0; i < 1024; ++i)
    {
        const float s = bank.processSample(0.0f);
        if (!isFiniteSample(s)) { allFinite = false; break; }
    }
    CHECK(allFinite);
}

// ==============================================================================
// Phase 6 (T080 / US4-3): Strike Position changes spectral weighting (Bessel).
// ==============================================================================
TEST_CASE("MembraneBody: Strike Position sweep changes first-5 mode weights "
          "(US4-3 Bessel J_m)",
          "[membrum][body][membrane][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = 16384;

    Membrum::BodyBank bank;

    auto pA = makeDefaultParams();
    pA.size      = 0.5f;     // f0 ~ 158 Hz
    pA.decay     = 0.9f;
    pA.material  = 0.7f;
    pA.strikePos = 0.1f;     // near center -> m=0 modes strong
    std::vector<float> bufA(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Membrane, pA, kSR,
                   bufA.data(), kN);

    auto pB = pA;
    pB.strikePos = 0.85f;    // near edge -> higher-m modes relatively stronger
    std::vector<float> bufB(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Membrane, pB, kSR,
                   bufB.data(), kN);

    // Probe the first 5 modal peaks at expected frequencies.
    const double f0 = 500.0 * std::pow(0.1, 0.5);
    const double peakFreqs[5] = {
        f0 * Membrum::kMembraneRatios[0],
        f0 * Membrum::kMembraneRatios[1],
        f0 * Membrum::kMembraneRatios[2],
        f0 * Membrum::kMembraneRatios[3],
        f0 * Membrum::kMembraneRatios[4],
    };

    float wA[5]{};
    float wB[5]{};
    REQUIRE(measureNormalizedPeakWeights(bufA.data(), kN, kSR,
                                         peakFreqs, 5, wA));
    REQUIRE(measureNormalizedPeakWeights(bufB.data(), kN, kSR,
                                         peakFreqs, 5, wB));

    const float dist = l1Distance(wA, wB, 5);
    INFO("Membrane strikePos L1 distance = " << dist);
    // The Bessel strike mapping must produce a clearly audible reweighting
    // (empirically ~0.4 for this sweep; require at least 0.15 headroom).
    CHECK(dist >= 0.15f);
}

// ==============================================================================
// Phase 6 (T080 / US4-4): Material sweep -> decay tilt changes monotonically.
// At low Material, high modes decay faster (spectral tilt darker over time).
// At high Material, modes decay more uniformly (less tilt).
// Metric: ratio of high-mode Goertzel magnitude / low-mode magnitude over a
// tail window. This ratio must INCREASE with Material.
// ==============================================================================
TEST_CASE("MembraneBody: Material sweep changes decay tilt monotonically (US4-4)",
          "[membrum][body][membrane][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR);  // 1 second

    Membrum::BodyBank bank;

    auto base = makeDefaultParams();
    base.size      = 0.5f;       // f0 ~ 158 Hz
    base.decay     = 0.9f;
    base.strikePos = 0.37f;

    const double f0      = 500.0 * std::pow(0.1, 0.5);
    const double lowHz   = f0 * Membrum::kMembraneRatios[0];
    const double highHz  = f0 * Membrum::kMembraneRatios[4];

    // Tail window: skip the first 100 ms (transient) and measure over 300 ms.
    const int tailStart  = static_cast<int>(kSR * 0.10);
    const int tailLength = static_cast<int>(kSR * 0.30);

    auto runAt = [&](float materialVal) noexcept {
        auto p = base;
        p.material = materialVal;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::Membrane, p, kSR,
                       buf.data(), kN);
        const float magLow =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, lowHz);
        const float magHigh =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, highHz);
        // Return high/low ratio. Low ratio => tail dominated by low modes =>
        // more tilt (dark). Higher ratio => flatter spectrum => less tilt.
        return magHigh / std::max(magLow, 1e-9f);
    };

    const float tiltLow  = runAt(0.0f);
    const float tiltHigh = runAt(1.0f);

    INFO("Membrane material-tilt ratio: low=" << tiltLow
         << " high=" << tiltHigh);
    REQUIRE(tiltLow > 0.0f);
    REQUIRE(tiltHigh > 0.0f);
    // Monotonic: high Material keeps more high-mode energy in the tail.
    CHECK(tiltHigh > tiltLow);
}

// ==============================================================================
// Phase 6 (T080 / US4-5): Level = 0 -> DrumVoice output is silent.
// ==============================================================================
TEST_CASE("MembraneBody: DrumVoice with Level=0 produces silent output (US4-5)",
          "[membrum][body][membrane][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 0.25);  // 250 ms

    Membrum::DrumVoice voice;
    voice.prepare(kSR, 0u);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.8f);
    voice.setStrikePosition(0.37f);
    voice.setLevel(0.0f);
    voice.noteOn(1.0f);

    constexpr int kBlock = 64;
    std::array<float, kBlock> block{};
    float peakAbs = 0.0f;
    bool  allFinite = true;
    int remaining = kN;
    while (remaining > 0)
    {
        const int n = remaining < kBlock ? remaining : kBlock;
        voice.processBlock(block.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float s = block[static_cast<size_t>(i)];
            if (!isFiniteSample(s)) allFinite = false;
            const float a = s < 0.0f ? -s : s;
            if (a > peakAbs) peakAbs = a;
        }
        remaining -= n;
    }
    INFO("Membrane Level=0 peakAbs=" << peakAbs);
    CHECK(allFinite);
    CHECK(peakAbs == 0.0f);
}
