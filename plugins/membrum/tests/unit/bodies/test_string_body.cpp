// ==============================================================================
// StringBody contract tests (Phase 4 -- T051)
// ==============================================================================
// Plus: (8) partial ratios are harmonic (integer multiples within +/-1%,
//            US2-4 and SC-002 String row).
//       (9) processSample() ignores sharedBank and uses the internal
//           WaveguideString exclusively (shared-bank isolation).
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "body_test_helpers.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_bank.h"
#include "dsp/exciter_type.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace membrum_body_tests;

TEST_CASE("StringBody: configureForNoteOn + processSample is allocation-free",
          "[membrum][body][string][alloc]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);
    bank.setBodyModel(Membrum::BodyModelType::String);

    {
        TestHelpers::AllocationScope scope;
        bank.configureForNoteOn(makeDefaultParams(), 160.0f);
        for (int i = 0; i < 1024; ++i)
            (void)bank.processSample(i == 0 ? 1.0f : 0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("StringBody: partials are harmonic (integer multiples +/-1%)",
          "[membrum][body][string][modes][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 32768;

    Membrum::BodyBank bank;
    auto params = makeDefaultParams();
    params.size      = 0.6f;   // f0 = 800 * 0.1^0.6 ≈ 201.2 Hz
    params.decay     = 0.9f;
    params.strikePos = 0.3f;
    params.material  = 0.6f;

    std::vector<float> buf(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::String, params, kSR,
                   buf.data(), kN);

    const double expectedF0 = 800.0 * std::pow(0.1, 0.6);
    const double measuredF0 = findPeakFrequency(
        buf.data(), kN, kSR, expectedF0 * 0.7, expectedF0 * 1.3, 256);
    INFO("String f0 expected " << expectedF0 << " measured " << measuredF0);
    REQUIRE(measuredF0 == Approx(expectedF0).epsilon(0.02));

    // Harmonic integer multiples: 2x, 3x, 4x within +/-1%
    const float tol = 0.01f;
    for (int k = 2; k <= 4; ++k)
    {
        const double expectedHz = measuredF0 * static_cast<double>(k);
        if (expectedHz >= kSR * 0.45) continue;
        const double lo = expectedHz * (1.0 - tol * 2.0);
        const double hi = expectedHz * (1.0 + tol * 2.0);
        const double found = findPeakFrequency(buf.data(), kN, kSR, lo, hi, 128);
        INFO("String harmonic " << k << "x expected " << expectedHz
             << " found " << found);
        CHECK(found == Approx(expectedHz).epsilon(tol));
    }
}

TEST_CASE("StringBody: processSample ignores sharedBank reference (FR-023)",
          "[membrum][body][string][isolation]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = 4096;

    // Build a Membrane note, let its modal bank accumulate energy, then
    // switch to String. The String output must NOT be affected by any
    // residual state in the shared bank.
    Membrum::BodyBank bank;
    bank.prepare(kSR, 0u);

    // Run Membrane for a while to load the shared bank with ringing energy.
    bank.setBodyModel(Membrum::BodyModelType::Membrane);
    bank.configureForNoteOn(makeDefaultParams(), 160.0f);
    (void)bank.processSample(1.0f);
    for (int i = 0; i < 256; ++i)
        (void)bank.processSample(0.0f);

    // Now switch to String on next note-on.
    bank.setBodyModel(Membrum::BodyModelType::String);
    bank.configureForNoteOn(makeDefaultParams(), 160.0f);

    // Process with zero excitation — the string should only output the
    // energy left from its own noteOn excitation, NOT the leftover
    // membrane modal energy.
    std::vector<float> buf(kN, 0.0f);
    for (int i = 0; i < kN; ++i)
        buf[i] = bank.processSample(0.0f);

    // The waveguide must be producing reasonable output around its fundamental,
    // not broadband noise that would indicate shared-bank leakage.
    // Peak frequency should be close to f0 = 800 * 0.1^0.5 ≈ 253 Hz.
    const double expectedF0 = 800.0 * std::pow(0.1, 0.5);
    const double measuredF0 = findPeakFrequency(
        buf.data(), kN, kSR, expectedF0 * 0.6, expectedF0 * 1.4, 256);
    CHECK(measuredF0 == Approx(expectedF0).epsilon(0.05));

    bool allFinite = true;
    for (int i = 0; i < kN; ++i)
    {
        if (!isFiniteSample(buf[i])) { allFinite = false; break; }
    }
    CHECK(allFinite);
}

TEST_CASE("StringBody: Size sweep covers >= 1 octave",
          "[membrum][body][string][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = 8192;

    Membrum::BodyBank bank;

    auto p0 = makeDefaultParams();
    p0.size = 0.0f;
    std::vector<float> buf0(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::String, p0, kSR,
                   buf0.data(), kN);
    const double f0a = findPeakFrequency(buf0.data(), kN, kSR, 200.0, 2000.0, 256);

    auto p1 = makeDefaultParams();
    p1.size = 1.0f;
    std::vector<float> buf1(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::String, p1, kSR,
                   buf1.data(), kN);
    const double f0b = findPeakFrequency(buf1.data(), kN, kSR, 40.0, 400.0, 256);

    INFO("String Size=0 f0=" << f0a << " Size=1 f0=" << f0b);
    CHECK(f0a > 0.0);
    CHECK(f0b > 0.0);
    CHECK((f0a / f0b) >= 2.0);
}

TEST_CASE("StringBody: Decay sweep produces >= 3x RT60 range",
          "[membrum][body][string][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 6.0);

    Membrum::BodyBank bank;

    auto pLow = makeDefaultParams();
    pLow.decay = 0.0f;
    std::vector<float> bufLow(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::String, pLow, kSR,
                   bufLow.data(), kN);
    const float rt60Low = estimateRT60(bufLow.data(), kN, kSR);

    auto pHigh = makeDefaultParams();
    pHigh.decay = 1.0f;
    std::vector<float> bufHigh(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::String, pHigh, kSR,
                   bufHigh.data(), kN);
    const float rt60High = estimateRT60(bufHigh.data(), kN, kSR);

    INFO("String RT60 low=" << rt60Low << " high=" << rt60High);
    REQUIRE(rt60Low > 0.0f);
    REQUIRE(rt60High > 0.0f);
    CHECK((rt60High / rt60Low) >= 3.0f);
}

TEST_CASE("StringBody: finite output across all exciters",
          "[membrum][body][string][finite]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR);

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
        bank.setBodyModel(Membrum::BodyModelType::String);
        bank.configureForNoteOn(makeDefaultParams(), 160.0f);
        excBank.trigger(0.8f);

        bool allFinite = true;
        float lastBody = 0.0f;
        for (int i = 0; i < kN; ++i)
        {
            const float exc  = excBank.process(lastBody);
            const float body = bank.processSample(exc);
            lastBody = body;
            if (!isFiniteSample(body)) { allFinite = false; break; }
        }
        INFO("Exciter=" << static_cast<int>(et));
        CHECK(allFinite);
    }
}

TEST_CASE("StringBody: sample-rate sweep produces finite output",
          "[membrum][body][string][samplerate]")
{
    const double sampleRates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    for (double sr : sampleRates)
    {
        const int kN = 4096;
        Membrum::BodyBank bank;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::String,
                       makeDefaultParams(), sr, buf.data(), kN);
        bool allFinite = true;
        for (int i = 0; i < kN; ++i)
        {
            if (!isFiniteSample(buf[i])) { allFinite = false; break; }
        }
        INFO("sampleRate=" << sr);
        CHECK(allFinite);
    }
}

TEST_CASE("StringBody: mid-note body switch deferred, no crash",
          "[membrum][body][string][deferred]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0u);
    bank.setBodyModel(Membrum::BodyModelType::String);
    bank.configureForNoteOn(makeDefaultParams(), 160.0f);

    (void)bank.processSample(1.0f);

    bank.setBodyModel(Membrum::BodyModelType::NoiseBody);
    CHECK(bank.getCurrentType() == Membrum::BodyModelType::String);
    CHECK(bank.getPendingType() == Membrum::BodyModelType::NoiseBody);

    bool allFinite = true;
    for (int i = 0; i < 1024; ++i)
    {
        const float s = bank.processSample(0.0f);
        if (!isFiniteSample(s)) { allFinite = false; break; }
    }
    CHECK(allFinite);
}

// ==============================================================================
// Phase 6 (T080 / US4-3): Strike (pick) Position changes harmonic weighting.
// For a struck/plucked string, harmonic n is suppressed when pickPos = 1/n.
// pickPos=0.5 suppresses all even harmonics; pickPos=0.2 weights harmonics
// more uniformly. The first-5 harmonic normalized weight vectors must differ.
// ==============================================================================
TEST_CASE("StringBody: Strike Position sweep changes harmonic weighting "
          "(US4-3 pick-position)",
          "[membrum][body][string][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 16384;

    Membrum::BodyBank bank;

    auto pA = makeDefaultParams();
    pA.size      = 0.6f;     // f0 ~ 201 Hz
    pA.decay     = 0.9f;
    pA.material  = 0.6f;
    pA.strikePos = 0.5f;     // suppresses even harmonics
    std::vector<float> bufA(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::String, pA, kSR,
                   bufA.data(), kN);

    auto pB = pA;
    pB.strikePos = 0.15f;    // all harmonics present
    std::vector<float> bufB(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::String, pB, kSR,
                   bufB.data(), kN);

    const double f0 = 800.0 * std::pow(0.1, 0.6);
    const double peakFreqs[5] = {
        f0 * 1.0,
        f0 * 2.0,
        f0 * 3.0,
        f0 * 4.0,
        f0 * 5.0,
    };

    float wA[5]{};
    float wB[5]{};
    REQUIRE(measureNormalizedPeakWeights(bufA.data(), kN, kSR,
                                         peakFreqs, 5, wA));
    REQUIRE(measureNormalizedPeakWeights(bufB.data(), kN, kSR,
                                         peakFreqs, 5, wB));

    const float dist = l1Distance(wA, wB, 5);
    INFO("String strikePos (pick) L1 distance = " << dist);
    CHECK(dist >= 0.15f);
}

// ==============================================================================
// Phase 6 (T080 / US4-4): Material sweep -> waveguide loop-filter brightness
// changes decay tilt monotonically. At low Material, the loop filter damps
// high frequencies harder -> tail is darker. At high Material, high
// harmonics persist longer.
// ==============================================================================
TEST_CASE("StringBody: Material sweep changes decay tilt monotonically (US4-4)",
          "[membrum][body][string][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = static_cast<int>(kSR);  // 1 second

    Membrum::BodyBank bank;

    auto base = makeDefaultParams();
    base.size      = 0.6f;       // f0 ~ 201 Hz
    base.decay     = 0.9f;
    base.strikePos = 0.27f;

    const double f0     = 800.0 * std::pow(0.1, 0.6);
    const double lowHz  = f0 * 1.0;
    const double highHz = f0 * 5.0;

    const int tailStart  = static_cast<int>(kSR * 0.15);
    const int tailLength = static_cast<int>(kSR * 0.40);

    auto runAt = [&](float materialVal) noexcept {
        auto p = base;
        p.material = materialVal;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::String, p, kSR,
                       buf.data(), kN);
        const float magLow =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, lowHz);
        const float magHigh =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, highHz);
        return magHigh / std::max(magLow, 1e-9f);
    };

    const float tiltLow  = runAt(0.0f);
    const float tiltHigh = runAt(1.0f);

    INFO("String material-tilt ratio: low=" << tiltLow
         << " high=" << tiltHigh);
    REQUIRE(tiltLow > 0.0f);
    REQUIRE(tiltHigh > 0.0f);
    CHECK(tiltHigh > tiltLow);
}

// ==============================================================================
// Phase 6 (T080 / US4-5): Level = 0 -> DrumVoice output is silent.
// ==============================================================================
TEST_CASE("StringBody: DrumVoice with Level=0 produces silent output (US4-5)",
          "[membrum][body][string][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 0.25);

    Membrum::DrumVoice voice;
    voice.prepare(kSR, 0u);
    voice.setBodyModel(Membrum::BodyModelType::String);
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
    INFO("String Level=0 peakAbs=" << peakAbs);
    CHECK(allFinite);
    CHECK(peakAbs == 0.0f);
}
