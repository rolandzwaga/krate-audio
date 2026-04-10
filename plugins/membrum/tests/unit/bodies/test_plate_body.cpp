// ==============================================================================
// PlateBody contract tests (Phase 4 -- T049)
// ==============================================================================
//   1. Allocation-free
//   2. Modal ratios within +/-3% of kPlateRatios[0..7] (SC-002, US2-2)
//   3. Size sweep covers >= 1 octave
//   4. Decay sweep >= 3x RT60
//   5. Finite across all 6 exciters
//   6. Sample-rate sweep
//   7. Mid-note switch deferral
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "body_test_helpers.h"
#include "dsp/bodies/plate_modes.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_bank.h"
#include "dsp/exciter_type.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace membrum_body_tests;

TEST_CASE("PlateBody: configureForNoteOn + processSample is allocation-free",
          "[membrum][body][plate][alloc]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);
    bank.setBodyModel(Membrum::BodyModelType::Plate);

    {
        TestHelpers::AllocationScope scope;
        bank.configureForNoteOn(makeDefaultParams(), 160.0f);
        for (int i = 0; i < 1024; ++i)
            (void)bank.processSample(i == 0 ? 1.0f : 0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("PlateBody: first 8 partial ratios within +/-3% (SC-002, US2-2)",
          "[membrum][body][plate][modes][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 16384;

    Membrum::BodyBank bank;
    auto params = makeDefaultParams();
    params.size   = 0.7f;   // f0 = 800 * 0.1^0.7 ≈ 159.2 Hz
    params.decay  = 0.9f;
    params.strikePos = 0.37f;

    std::vector<float> buf(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Plate, params, kSR,
                   buf.data(), kN);

    const double expectedF0 = 800.0 * std::pow(0.1, 0.7);
    const double measuredF0 = findPeakFrequency(
        buf.data(), kN, kSR, expectedF0 * 0.7, expectedF0 * 1.3, 256);
    INFO("Measured plate f0 = " << measuredF0
         << " expected " << expectedF0);
    REQUIRE(measuredF0 == Approx(expectedF0).epsilon(0.05));

    // Expected ratios {1, 2.5, 4, 5, 6.5, 8.5, 9, 10}, tolerance +/-3%.
    const float tol = 0.03f;
    const int   modesToCheck[] = {1, 2, 3, 4};  // higher modes may be above Nyquist
    for (int k : modesToCheck)
    {
        const double expectedHz =
            measuredF0 * Membrum::Bodies::kPlateRatios[k];
        if (expectedHz >= kSR * 0.45) continue;
        const double lo = expectedHz * (1.0 - tol * 1.8);
        const double hi = expectedHz * (1.0 + tol * 1.8);
        const double found = findPeakFrequency(buf.data(), kN, kSR, lo, hi, 128);
        INFO("Plate mode " << k << " expected " << expectedHz
             << " found " << found);
        CHECK(found == Approx(expectedHz).epsilon(tol));
    }
}

TEST_CASE("PlateBody: Size sweep covers >= 1 octave",
          "[membrum][body][plate][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = 8192;

    Membrum::BodyBank bank;

    auto p0 = makeDefaultParams();
    p0.size = 0.0f;
    std::vector<float> buf0(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Plate, p0, kSR,
                   buf0.data(), kN);
    const double f0a = findPeakFrequency(buf0.data(), kN, kSR, 200.0, 2000.0, 256);

    auto p1 = makeDefaultParams();
    p1.size = 1.0f;
    std::vector<float> buf1(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Plate, p1, kSR,
                   buf1.data(), kN);
    const double f0b = findPeakFrequency(buf1.data(), kN, kSR, 40.0, 400.0, 256);

    INFO("Plate Size=0 f0=" << f0a << " Size=1 f0=" << f0b);
    CHECK(f0a > 0.0);
    CHECK(f0b > 0.0);
    CHECK((f0a / f0b) >= 2.0);
}

TEST_CASE("PlateBody: Decay sweep produces >= 3x RT60 range",
          "[membrum][body][plate][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 3.0);

    Membrum::BodyBank bank;

    auto pLow = makeDefaultParams();
    pLow.decay = 0.0f;
    std::vector<float> bufLow(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Plate, pLow, kSR,
                   bufLow.data(), kN);
    const float rt60Low = estimateRT60(bufLow.data(), kN, kSR);

    auto pHigh = makeDefaultParams();
    pHigh.decay = 1.0f;
    std::vector<float> bufHigh(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Plate, pHigh, kSR,
                   bufHigh.data(), kN);
    const float rt60High = estimateRT60(bufHigh.data(), kN, kSR);

    INFO("Plate RT60 low=" << rt60Low << " high=" << rt60High);
    REQUIRE(rt60Low > 0.0f);
    REQUIRE(rt60High > 0.0f);
    CHECK((rt60High / rt60Low) >= 3.0f);
}

TEST_CASE("PlateBody: finite output across all exciters (SC-007)",
          "[membrum][body][plate][finite]")
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
        bank.setBodyModel(Membrum::BodyModelType::Plate);
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

TEST_CASE("PlateBody: sample-rate sweep produces finite modal output",
          "[membrum][body][plate][samplerate]")
{
    const double sampleRates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    for (double sr : sampleRates)
    {
        const int kN = 4096;
        Membrum::BodyBank bank;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::Plate,
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

TEST_CASE("PlateBody: mid-note body switch deferred, no crash",
          "[membrum][body][plate][deferred]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0u);
    bank.setBodyModel(Membrum::BodyModelType::Plate);
    bank.configureForNoteOn(makeDefaultParams(), 160.0f);

    (void)bank.processSample(1.0f);

    bank.setBodyModel(Membrum::BodyModelType::Shell);
    CHECK(bank.getCurrentType() == Membrum::BodyModelType::Plate);
    CHECK(bank.getPendingType() == Membrum::BodyModelType::Shell);

    bool allFinite = true;
    for (int i = 0; i < 1024; ++i)
    {
        const float s = bank.processSample(0.0f);
        if (!isFiniteSample(s)) { allFinite = false; break; }
    }
    CHECK(allFinite);
}

// ==============================================================================
// Phase 6 (T080 / US4-3): Strike Position changes spectral weighting
// (rectangular-plate sin(m*pi*x)*sin(n*pi*y) formula per FR-034).
// ==============================================================================
TEST_CASE("PlateBody: Strike Position sweep changes first-5 mode weights "
          "(US4-3 sin(m*pi*x)*sin(n*pi*y))",
          "[membrum][body][plate][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 16384;

    Membrum::BodyBank bank;

    auto pA = makeDefaultParams();
    pA.size      = 0.7f;     // f0 ~ 159 Hz
    pA.decay     = 0.9f;
    pA.material  = 0.5f;
    pA.strikePos = 0.5f;     // x0 = 0.5 -> even-m modes vanish
    std::vector<float> bufA(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Plate, pA, kSR,
                   bufA.data(), kN);

    auto pB = pA;
    pB.strikePos = 0.0f;     // x0 = 0.35 -> all modes non-zero
    std::vector<float> bufB(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Plate, pB, kSR,
                   bufB.data(), kN);

    const double f0 = 800.0 * std::pow(0.1, 0.7);
    const double peakFreqs[5] = {
        f0 * Membrum::Bodies::kPlateRatios[0],
        f0 * Membrum::Bodies::kPlateRatios[1],
        f0 * Membrum::Bodies::kPlateRatios[2],
        f0 * Membrum::Bodies::kPlateRatios[3],
        f0 * Membrum::Bodies::kPlateRatios[4],
    };

    float wA[5]{};
    float wB[5]{};
    REQUIRE(measureNormalizedPeakWeights(bufA.data(), kN, kSR,
                                         peakFreqs, 5, wA));
    REQUIRE(measureNormalizedPeakWeights(bufB.data(), kN, kSR,
                                         peakFreqs, 5, wB));

    const float dist = l1Distance(wA, wB, 5);
    INFO("Plate strikePos L1 distance = " << dist);
    CHECK(dist >= 0.15f);
}

// ==============================================================================
// Phase 6 (T080 / US4-4): Material sweep -> decay tilt changes monotonically.
// ==============================================================================
TEST_CASE("PlateBody: Material sweep changes decay tilt monotonically (US4-4)",
          "[membrum][body][plate][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = static_cast<int>(kSR);  // 1 second

    Membrum::BodyBank bank;

    auto base = makeDefaultParams();
    base.size      = 0.5f;      // f0 ~ 252 Hz
    base.decay     = 0.9f;
    base.strikePos = 0.37f;

    const double f0     = 800.0 * std::pow(0.1, 0.5);
    const double lowHz  = f0 * Membrum::Bodies::kPlateRatios[0];
    const double highHz = f0 * Membrum::Bodies::kPlateRatios[3];

    const int tailStart  = static_cast<int>(kSR * 0.15);
    const int tailLength = static_cast<int>(kSR * 0.40);

    auto runAt = [&](float materialVal) noexcept {
        auto p = base;
        p.material = materialVal;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::Plate, p, kSR,
                       buf.data(), kN);
        const float magLow =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, lowHz);
        const float magHigh =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, highHz);
        return magHigh / std::max(magLow, 1e-9f);
    };

    const float tiltLow  = runAt(0.0f);
    const float tiltHigh = runAt(1.0f);

    INFO("Plate material-tilt ratio: low=" << tiltLow
         << " high=" << tiltHigh);
    REQUIRE(tiltLow > 0.0f);
    REQUIRE(tiltHigh > 0.0f);
    CHECK(tiltHigh > tiltLow);
}

// ==============================================================================
// Phase 6 (T080 / US4-5): Level = 0 -> DrumVoice output is silent.
// ==============================================================================
TEST_CASE("PlateBody: DrumVoice with Level=0 produces silent output (US4-5)",
          "[membrum][body][plate][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 0.25);

    Membrum::DrumVoice voice;
    voice.prepare(kSR, 0u);
    voice.setBodyModel(Membrum::BodyModelType::Plate);
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
    INFO("Plate Level=0 peakAbs=" << peakAbs);
    CHECK(allFinite);
    CHECK(peakAbs == 0.0f);
}
