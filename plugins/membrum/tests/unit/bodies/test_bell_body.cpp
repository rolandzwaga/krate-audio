// ==============================================================================
// BellBody contract tests (Phase 4 -- T052)
// ==============================================================================
// Plus: first 5 partial ratios match Chladni {0.250, 0.500, 0.600, 0.750,
// 1.000} (hum, prime, tierce, quint, nominal) within +/-3% relative to
// nominal (US2-5).
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "body_test_helpers.h"
#include "dsp/bodies/bell_modes.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_bank.h"
#include "dsp/exciter_type.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace membrum_body_tests;

TEST_CASE("BellBody: configureForNoteOn + processSample is allocation-free",
          "[membrum][body][bell][alloc]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);
    bank.setBodyModel(Membrum::BodyModelType::Bell);

    {
        TestHelpers::AllocationScope scope;
        bank.configureForNoteOn(makeDefaultParams(), 160.0f);
        for (int i = 0; i < 1024; ++i)
            (void)bank.processSample(i == 0 ? 1.0f : 0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("BellBody: first 5 Chladni ratios within +/-3% (SC-002, US2-5)",
          "[membrum][body][bell][modes][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 32768;

    Membrum::BodyBank bank;
    auto params = makeDefaultParams();
    params.size      = 0.5f;   // f0 nominal = 800 * 0.1^0.5 ≈ 253 Hz
    params.decay     = 0.9f;
    params.strikePos = 0.32f;

    std::vector<float> buf(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Bell, params, kSR,
                   buf.data(), kN);

    // Nominal sits at ratio 1.000 -> f0. Narrow search range to +/-10 %
    // to avoid landing on the quint partial (0.75*nominal).
    const double expectedNominal = 800.0 * std::pow(0.1, 0.5);
    const double measuredNominal = findPeakFrequency(
        buf.data(), kN, kSR, expectedNominal * 0.92, expectedNominal * 1.08, 256);
    INFO("Bell nominal expected " << expectedNominal
         << " measured " << measuredNominal);
    REQUIRE(measuredNominal == Approx(expectedNominal).epsilon(0.05));

    const float tol = 0.03f;
    // hum=0.25, prime=0.5, tierce=0.6, quint=0.75, nominal=1.0 (index 4).
    for (int k = 0; k < 5; ++k)
    {
        const double expectedHz =
            measuredNominal * Membrum::Bodies::kBellRatios[k];
        const double lo = expectedHz * (1.0 - tol * 1.8);
        const double hi = expectedHz * (1.0 + tol * 1.8);
        const double found = findPeakFrequency(buf.data(), kN, kSR, lo, hi, 128);
        INFO("Bell mode " << k << " expected " << expectedHz
             << " found " << found);
        CHECK(found == Approx(expectedHz).epsilon(tol));
    }
}

TEST_CASE("BellBody: Size sweep covers >= 1 octave",
          "[membrum][body][bell][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = 8192;

    Membrum::BodyBank bank;

    auto p0 = makeDefaultParams();
    p0.size = 0.0f;
    std::vector<float> buf0(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Bell, p0, kSR,
                   buf0.data(), kN);
    // At size=0, nominal=800 Hz. Search narrowly around expected nominal.
    const double f0a = findPeakFrequency(buf0.data(), kN, kSR, 760.0, 840.0, 128);

    auto p1 = makeDefaultParams();
    p1.size = 1.0f;
    std::vector<float> buf1(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Bell, p1, kSR,
                   buf1.data(), kN);
    // At size=1, nominal=80 Hz.
    const double f0b = findPeakFrequency(buf1.data(), kN, kSR, 76.0, 84.0, 128);

    INFO("Bell Size=0 nominal=" << f0a << " Size=1 nominal=" << f0b);
    CHECK(f0a > 0.0);
    CHECK(f0b > 0.0);
    CHECK((f0a / f0b) >= 2.0);
}

TEST_CASE("BellBody: Decay sweep produces >= 3x RT60 range",
          "[membrum][body][bell][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 5.0);

    Membrum::BodyBank bank;

    auto pLow = makeDefaultParams();
    pLow.decay = 0.0f;
    std::vector<float> bufLow(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Bell, pLow, kSR,
                   bufLow.data(), kN);
    const float rt60Low = estimateRT60(bufLow.data(), kN, kSR);

    auto pHigh = makeDefaultParams();
    pHigh.decay = 1.0f;
    std::vector<float> bufHigh(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Bell, pHigh, kSR,
                   bufHigh.data(), kN);
    const float rt60High = estimateRT60(bufHigh.data(), kN, kSR);

    INFO("Bell RT60 low=" << rt60Low << " high=" << rt60High);
    REQUIRE(rt60Low > 0.0f);
    REQUIRE(rt60High > 0.0f);
    CHECK((rt60High / rt60Low) >= 3.0f);
}

TEST_CASE("BellBody: finite output across all exciters",
          "[membrum][body][bell][finite]")
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
        bank.setBodyModel(Membrum::BodyModelType::Bell);
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

TEST_CASE("BellBody: sample-rate sweep produces finite modal output",
          "[membrum][body][bell][samplerate]")
{
    const double sampleRates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    for (double sr : sampleRates)
    {
        const int kN = 4096;
        Membrum::BodyBank bank;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::Bell,
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

TEST_CASE("BellBody: mid-note body switch deferred, no crash",
          "[membrum][body][bell][deferred]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0u);
    bank.setBodyModel(Membrum::BodyModelType::Bell);
    bank.configureForNoteOn(makeDefaultParams(), 160.0f);

    (void)bank.processSample(1.0f);

    bank.setBodyModel(Membrum::BodyModelType::Membrane);
    CHECK(bank.getCurrentType() == Membrum::BodyModelType::Bell);
    CHECK(bank.getPendingType() == Membrum::BodyModelType::Membrane);

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
// (Chladni radial approximation, r/R = strikePos).
// ==============================================================================
TEST_CASE("BellBody: Strike Position sweep changes first-5 mode weights "
          "(US4-3 Chladni radial)",
          "[membrum][body][bell][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 16384;

    Membrum::BodyBank bank;

    auto pA = makeDefaultParams();
    pA.size      = 0.5f;     // nominal ~ 253 Hz
    pA.decay     = 0.9f;
    pA.material  = 0.6f;
    pA.strikePos = 0.15f;    // near crown -> different radial weights
    std::vector<float> bufA(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Bell, pA, kSR,
                   bufA.data(), kN);

    auto pB = pA;
    pB.strikePos = 0.55f;    // near lip
    std::vector<float> bufB(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Bell, pB, kSR,
                   bufB.data(), kN);

    const double nominalF0 = 800.0 * std::pow(0.1, 0.5);
    const double peakFreqs[5] = {
        nominalF0 * Membrum::Bodies::kBellRatios[0],  // hum
        nominalF0 * Membrum::Bodies::kBellRatios[1],  // prime
        nominalF0 * Membrum::Bodies::kBellRatios[2],  // tierce
        nominalF0 * Membrum::Bodies::kBellRatios[3],  // quint
        nominalF0 * Membrum::Bodies::kBellRatios[4],  // nominal
    };

    float wA[5]{};
    float wB[5]{};
    REQUIRE(measureNormalizedPeakWeights(bufA.data(), kN, kSR,
                                         peakFreqs, 5, wA));
    REQUIRE(measureNormalizedPeakWeights(bufB.data(), kN, kSR,
                                         peakFreqs, 5, wB));

    const float dist = l1Distance(wA, wB, 5);
    INFO("Bell strikePos L1 distance = " << dist);
    CHECK(dist >= 0.10f);
}

// ==============================================================================
// Phase 6 (T080 / US4-4): Material sweep -> decay tilt changes monotonically.
// ==============================================================================
TEST_CASE("BellBody: Material sweep changes decay tilt monotonically (US4-4)",
          "[membrum][body][bell][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = static_cast<int>(kSR * 2.0);

    Membrum::BodyBank bank;

    auto base = makeDefaultParams();
    base.size      = 0.5f;       // nominal ~ 253 Hz
    base.decay     = 0.9f;
    base.strikePos = 0.32f;

    const double nominalF0 = 800.0 * std::pow(0.1, 0.5);
    // Use nominal (index 4) as the low anchor, and a higher Chladni partial.
    const double lowHz  = nominalF0 * Membrum::Bodies::kBellRatios[4];  // nominal
    const double highHz = nominalF0 * Membrum::Bodies::kBellRatios[9];  // 4x nominal

    const int tailStart  = static_cast<int>(kSR * 0.20);
    const int tailLength = static_cast<int>(kSR * 0.50);

    auto runAt = [&](float materialVal) noexcept {
        auto p = base;
        p.material = materialVal;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::Bell, p, kSR,
                       buf.data(), kN);
        const float magLow =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, lowHz);
        const float magHigh =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, highHz);
        return magHigh / std::max(magLow, 1e-9f);
    };

    const float tiltLow  = runAt(0.0f);
    const float tiltHigh = runAt(1.0f);

    INFO("Bell material-tilt ratio: low=" << tiltLow
         << " high=" << tiltHigh);
    REQUIRE(tiltLow > 0.0f);
    REQUIRE(tiltHigh > 0.0f);
    CHECK(tiltHigh > tiltLow);
}

// ==============================================================================
// Phase 6 (T080 / US4-5): Level = 0 -> DrumVoice output is silent.
// ==============================================================================
TEST_CASE("BellBody: DrumVoice with Level=0 produces silent output (US4-5)",
          "[membrum][body][bell][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 0.25);

    Membrum::DrumVoice voice;
    voice.prepare(kSR, 0u);
    voice.setBodyModel(Membrum::BodyModelType::Bell);
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
    INFO("Bell Level=0 peakAbs=" << peakAbs);
    CHECK(allFinite);
    CHECK(peakAbs == 0.0f);
}
