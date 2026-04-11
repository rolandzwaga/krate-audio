// ==============================================================================
// ShellBody contract tests (Phase 4 -- T050)
// ==============================================================================
// Plus: first 6 partial ratios match {1.000, 2.757, 5.404, 8.933, 13.344,
// 18.637} within +/-3% (SC-002, US2-3).
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "body_test_helpers.h"
#include "dsp/bodies/shell_modes.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_bank.h"
#include "dsp/exciter_type.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace membrum_body_tests;

TEST_CASE("ShellBody: configureForNoteOn + processSample is allocation-free",
          "[membrum][body][shell][alloc]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);
    bank.setBodyModel(Membrum::BodyModelType::Shell);

    {
        TestHelpers::AllocationScope scope;
        bank.configureForNoteOn(makeDefaultParams(), 160.0f);
        for (int i = 0; i < 1024; ++i)
            (void)bank.processSample(i == 0 ? 1.0f : 0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("ShellBody: first 6 partial ratios within +/-3% (SC-002, US2-3)",
          "[membrum][body][shell][modes][BodyModes]")
{
    constexpr double kSR = 96000.0;  // high SR so modes 3..5 don't clip Nyquist
    constexpr int    kN  = 32768;

    Membrum::BodyBank bank;
    auto params = makeDefaultParams();
    params.size      = 0.8f;  // f0 = 1500 * 0.1^0.8 ≈ 237.8 Hz
    params.decay     = 0.9f;
    params.strikePos = 0.27f;

    std::vector<float> buf(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Shell, params, kSR,
                   buf.data(), kN);

    const double expectedF0 = 1500.0 * std::pow(0.1, 0.8);
    const double measuredF0 = findPeakFrequency(
        buf.data(), kN, kSR, expectedF0 * 0.7, expectedF0 * 1.3, 256);
    INFO("Shell f0 expected " << expectedF0 << " measured " << measuredF0);
    REQUIRE(measuredF0 == Approx(expectedF0).epsilon(0.05));

    const float tol = 0.03f;
    for (int k = 1; k <= 5; ++k)
    {
        const double expectedHz =
            measuredF0 * Membrum::Bodies::kShellRatios[k];
        if (expectedHz >= kSR * 0.45) continue;
        const double lo = expectedHz * (1.0 - tol * 1.8);
        const double hi = expectedHz * (1.0 + tol * 1.8);
        const double found = findPeakFrequency(buf.data(), kN, kSR, lo, hi, 128);
        INFO("Shell mode " << k << " expected " << expectedHz
             << " found " << found);
        CHECK(found == Approx(expectedHz).epsilon(tol));
    }
}

TEST_CASE("ShellBody: Size sweep covers >= 1 octave",
          "[membrum][body][shell][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = 8192;

    Membrum::BodyBank bank;

    auto p0 = makeDefaultParams();
    p0.size = 0.0f;
    std::vector<float> buf0(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Shell, p0, kSR,
                   buf0.data(), kN);
    const double f0a = findPeakFrequency(buf0.data(), kN, kSR, 300.0, 3000.0, 256);

    auto p1 = makeDefaultParams();
    p1.size = 1.0f;
    std::vector<float> buf1(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Shell, p1, kSR,
                   buf1.data(), kN);
    const double f0b = findPeakFrequency(buf1.data(), kN, kSR, 50.0, 500.0, 256);

    INFO("Shell Size=0 f0=" << f0a << " Size=1 f0=" << f0b);
    CHECK(f0a > 0.0);
    CHECK(f0b > 0.0);
    CHECK((f0a / f0b) >= 2.0);
}

TEST_CASE("ShellBody: Decay sweep produces >= 3x RT60 range",
          "[membrum][body][shell][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 4.0);

    Membrum::BodyBank bank;

    auto pLow = makeDefaultParams();
    pLow.decay = 0.0f;
    std::vector<float> bufLow(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Shell, pLow, kSR,
                   bufLow.data(), kN);
    const float rt60Low = estimateRT60(bufLow.data(), kN, kSR);

    auto pHigh = makeDefaultParams();
    pHigh.decay = 1.0f;
    std::vector<float> bufHigh(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Shell, pHigh, kSR,
                   bufHigh.data(), kN);
    const float rt60High = estimateRT60(bufHigh.data(), kN, kSR);

    INFO("Shell RT60 low=" << rt60Low << " high=" << rt60High);
    REQUIRE(rt60Low > 0.0f);
    REQUIRE(rt60High > 0.0f);
    CHECK((rt60High / rt60Low) >= 3.0f);
}

TEST_CASE("ShellBody: finite output across all exciters",
          "[membrum][body][shell][finite]")
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
        bank.setBodyModel(Membrum::BodyModelType::Shell);
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

TEST_CASE("ShellBody: sample-rate sweep produces finite modal output",
          "[membrum][body][shell][samplerate]")
{
    const double sampleRates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    for (double sr : sampleRates)
    {
        const int kN = 4096;
        Membrum::BodyBank bank;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::Shell,
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

TEST_CASE("ShellBody: mid-note body switch deferred, no crash",
          "[membrum][body][shell][deferred]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0u);
    bank.setBodyModel(Membrum::BodyModelType::Shell);
    bank.configureForNoteOn(makeDefaultParams(), 160.0f);

    (void)bank.processSample(1.0f);

    bank.setBodyModel(Membrum::BodyModelType::Bell);
    CHECK(bank.getCurrentType() == Membrum::BodyModelType::Shell);
    CHECK(bank.getPendingType() == Membrum::BodyModelType::Bell);

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
// (free-free beam sin(k*pi*x/L) approximation, per research.md §4.3).
// ==============================================================================
TEST_CASE("ShellBody: Strike Position sweep changes first-5 mode weights "
          "(US4-3 sin(k*pi*x/L))",
          "[membrum][body][shell][BodyModes]")
{
    constexpr double kSR = 96000.0;
    constexpr int    kN  = 16384;

    Membrum::BodyBank bank;

    auto pA = makeDefaultParams();
    pA.size      = 0.8f;     // f0 ~ 238 Hz
    pA.decay     = 0.9f;
    pA.material  = 0.6f;
    pA.strikePos = 0.2f;
    std::vector<float> bufA(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Shell, pA, kSR,
                   bufA.data(), kN);

    auto pB = pA;
    pB.strikePos = 0.7f;
    std::vector<float> bufB(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::Shell, pB, kSR,
                   bufB.data(), kN);

    const double f0 = 1500.0 * std::pow(0.1, 0.8);
    const double peakFreqs[5] = {
        f0 * Membrum::Bodies::kShellRatios[0],
        f0 * Membrum::Bodies::kShellRatios[1],
        f0 * Membrum::Bodies::kShellRatios[2],
        f0 * Membrum::Bodies::kShellRatios[3],
        f0 * Membrum::Bodies::kShellRatios[4],
    };

    float wA[5]{};
    float wB[5]{};
    REQUIRE(measureNormalizedPeakWeights(bufA.data(), kN, kSR,
                                         peakFreqs, 5, wA));
    REQUIRE(measureNormalizedPeakWeights(bufB.data(), kN, kSR,
                                         peakFreqs, 5, wB));

    const float dist = l1Distance(wA, wB, 5);
    INFO("Shell strikePos L1 distance = " << dist);
    CHECK(dist >= 0.15f);
}

// ==============================================================================
// Phase 6 (T080 / US4-4): Material sweep -> decay tilt changes monotonically.
// ==============================================================================
TEST_CASE("ShellBody: Material sweep changes decay tilt monotonically (US4-4)",
          "[membrum][body][shell][BodyModes]")
{
    constexpr double kSR = 96000.0;
    constexpr int    kN  = static_cast<int>(kSR);  // 1 second

    Membrum::BodyBank bank;

    auto base = makeDefaultParams();
    base.size      = 0.7f;       // f0 ~ 299 Hz
    base.decay     = 0.9f;
    base.strikePos = 0.27f;

    const double f0     = 1500.0 * std::pow(0.1, 0.7);
    const double lowHz  = f0 * Membrum::Bodies::kShellRatios[0];
    const double highHz = f0 * Membrum::Bodies::kShellRatios[3];

    const int tailStart  = static_cast<int>(kSR * 0.05);
    const int tailLength = static_cast<int>(kSR * 0.20);

    auto runAt = [&](float materialVal) noexcept {
        auto p = base;
        p.material = materialVal;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::Shell, p, kSR,
                       buf.data(), kN);
        const float magLow =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, lowHz);
        const float magHigh =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, highHz);
        return magHigh / std::max(magLow, 1e-9f);
    };

    const float tiltLow  = runAt(0.0f);
    const float tiltHigh = runAt(1.0f);

    INFO("Shell material-tilt ratio: low=" << tiltLow
         << " high=" << tiltHigh);
    REQUIRE(tiltLow > 0.0f);
    REQUIRE(tiltHigh > 0.0f);
    CHECK(tiltHigh > tiltLow);
}

// ==============================================================================
// Phase 6 (T080 / US4-5): Level = 0 -> DrumVoice output is silent.
// ==============================================================================
TEST_CASE("ShellBody: DrumVoice with Level=0 produces silent output (US4-5)",
          "[membrum][body][shell][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 0.25);

    Membrum::DrumVoice voice;
    voice.prepare(kSR, 0u);
    voice.setBodyModel(Membrum::BodyModelType::Shell);
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
    INFO("Shell Level=0 peakAbs=" << peakAbs);
    CHECK(allFinite);
    CHECK(peakAbs == 0.0f);
}
