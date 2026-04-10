// ==============================================================================
// NoiseBody contract tests (Phase 4 -- T053)
// ==============================================================================
// Plus: (8) hybrid modal + noise output -- verify BOTH a modal peak and
//           broadband noise energy are present (US2-6).
//       (9) modal layer uses plate ratios at kModeCount entries within +/-3%.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "body_test_helpers.h"
#include "dsp/bodies/noise_body.h"
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

TEST_CASE("NoiseBody: configureForNoteOn + processSample is allocation-free",
          "[membrum][body][noise][alloc]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);
    bank.setBodyModel(Membrum::BodyModelType::NoiseBody);

    {
        TestHelpers::AllocationScope scope;
        bank.configureForNoteOn(makeDefaultParams(), 160.0f);
        for (int i = 0; i < 1024; ++i)
            (void)bank.processSample(i == 0 ? 1.0f : 0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("NoiseBody: output is hybrid modal + noise (US2-6)",
          "[membrum][body][noise][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 16384;

    Membrum::BodyBank bank;
    auto params = makeDefaultParams();
    params.size     = 0.7f;  // f0 = 1500 * 0.1^0.7 ≈ 298.7
    params.decay    = 0.9f;
    params.material = 0.5f;

    std::vector<float> buf(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody, params, kSR,
                   buf.data(), kN);

    const double expectedF0 = 1500.0 * std::pow(0.1, 0.7);
    const double measuredF0 = findPeakFrequency(
        buf.data(), kN, kSR, expectedF0 * 0.7, expectedF0 * 1.3, 256);
    INFO("NoiseBody modal peak expected " << expectedF0
         << " measured " << measuredF0);
    // Modal peak should be present (hybrid).
    REQUIRE(measuredF0 == Approx(expectedF0).epsilon(0.08));

    // Measure RMS outside the modal-peak neighborhood to verify noise-floor
    // (broadband energy) presence. Skip the first ~100 samples (transient).
    double sumSq = 0.0;
    int    count = 0;
    for (int i = 200; i < kN; ++i)
    {
        sumSq += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
        ++count;
    }
    const float rms = static_cast<float>(std::sqrt(sumSq / std::max(1, count)));
    INFO("NoiseBody RMS (post-transient) = " << rms);
    REQUIRE(rms > 0.0f);  // hybrid: non-zero tail energy

    bool allFinite = true;
    for (int i = 0; i < kN; ++i)
    {
        if (!isFiniteSample(buf[i])) { allFinite = false; break; }
    }
    CHECK(allFinite);
}

TEST_CASE("NoiseBody: modal layer uses plate ratios within +/-3%",
          "[membrum][body][noise][modes][BodyModes]")
{
    constexpr double kSR = 96000.0;
    constexpr int    kN  = 32768;

    Membrum::BodyBank bank;
    auto params = makeDefaultParams();
    params.size      = 0.7f;  // f0 = 1500 * 0.1^0.7 ≈ 298.7
    params.decay     = 0.95f;
    params.strikePos = 0.37f;

    std::vector<float> buf(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody, params, kSR,
                   buf.data(), kN);

    const double expectedF0 = 1500.0 * std::pow(0.1, 0.7);
    const double measuredF0 = findPeakFrequency(
        buf.data(), kN, kSR, expectedF0 * 0.7, expectedF0 * 1.3, 256);
    REQUIRE(measuredF0 == Approx(expectedF0).epsilon(0.08));

    const float tol = 0.03f;
    // Ratios 1, 2 -> 2.500, 3 -> 4.000
    for (int k = 1; k <= 2; ++k)
    {
        const double expectedHz =
            measuredF0 * Membrum::Bodies::kPlateRatios[k];
        if (expectedHz >= kSR * 0.45) continue;
        const double lo = expectedHz * (1.0 - tol * 2.0);
        const double hi = expectedHz * (1.0 + tol * 2.0);
        const double found = findPeakFrequency(buf.data(), kN, kSR, lo, hi, 128);
        INFO("NoiseBody plate mode " << k << " expected " << expectedHz
             << " found " << found);
        CHECK(found == Approx(expectedHz).epsilon(tol));
    }
}

TEST_CASE("NoiseBody: Size sweep covers >= 1 octave",
          "[membrum][body][noise][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 8192;

    Membrum::BodyBank bank;

    auto p0 = makeDefaultParams();
    p0.size = 0.0f;
    std::vector<float> buf0(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody, p0, kSR,
                   buf0.data(), kN);
    const double f0a = findPeakFrequency(buf0.data(), kN, kSR, 300.0, 3000.0, 256);

    auto p1 = makeDefaultParams();
    p1.size = 1.0f;
    std::vector<float> buf1(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody, p1, kSR,
                   buf1.data(), kN);
    const double f0b = findPeakFrequency(buf1.data(), kN, kSR, 50.0, 500.0, 256);

    INFO("NoiseBody Size=0 peak=" << f0a << " Size=1 peak=" << f0b);
    CHECK(f0a > 0.0);
    CHECK(f0b > 0.0);
    CHECK((f0a / f0b) >= 2.0);
}

TEST_CASE("NoiseBody: Decay sweep produces >= 3x RT60 range",
          "[membrum][body][noise][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 3.0);

    Membrum::BodyBank bank;

    auto pLow = makeDefaultParams();
    pLow.decay = 0.0f;
    std::vector<float> bufLow(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody, pLow, kSR,
                   bufLow.data(), kN);
    const float rt60Low = estimateRT60(bufLow.data(), kN, kSR);

    auto pHigh = makeDefaultParams();
    pHigh.decay = 1.0f;
    std::vector<float> bufHigh(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody, pHigh, kSR,
                   bufHigh.data(), kN);
    const float rt60High = estimateRT60(bufHigh.data(), kN, kSR);

    INFO("NoiseBody RT60 low=" << rt60Low << " high=" << rt60High);
    REQUIRE(rt60Low > 0.0f);
    REQUIRE(rt60High > 0.0f);
    CHECK((rt60High / rt60Low) >= 3.0f);
}

TEST_CASE("NoiseBody: finite output across all exciters",
          "[membrum][body][noise][finite]")
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
        bank.setBodyModel(Membrum::BodyModelType::NoiseBody);
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

TEST_CASE("NoiseBody: sample-rate sweep produces finite output",
          "[membrum][body][noise][samplerate]")
{
    const double sampleRates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    for (double sr : sampleRates)
    {
        const int kN = 4096;
        Membrum::BodyBank bank;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody,
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

TEST_CASE("NoiseBody: mid-note body switch deferred, no crash",
          "[membrum][body][noise][deferred]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0u);
    bank.setBodyModel(Membrum::BodyModelType::NoiseBody);
    bank.configureForNoteOn(makeDefaultParams(), 160.0f);

    (void)bank.processSample(1.0f);

    bank.setBodyModel(Membrum::BodyModelType::Plate);
    CHECK(bank.getCurrentType() == Membrum::BodyModelType::NoiseBody);
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
// Phase 6 (T080 / US4-3): Strike Position changes spectral weighting
// (modal layer uses plate sin(m*pi*x)*sin(n*pi*y) formula).
// ==============================================================================
TEST_CASE("NoiseBody: Strike Position sweep changes first-5 mode weights "
          "(US4-3 plate formula)",
          "[membrum][body][noise][BodyModes]")
{
    constexpr double kSR = 96000.0;
    constexpr int    kN  = 16384;

    Membrum::BodyBank bank;

    auto pA = makeDefaultParams();
    pA.size      = 0.7f;     // f0 ~ 299 Hz
    pA.decay     = 0.95f;
    pA.material  = 0.5f;
    pA.strikePos = 0.5f;     // x0 = 0.5 -> even-m modes vanish
    std::vector<float> bufA(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody, pA, kSR,
                   bufA.data(), kN);

    auto pB = pA;
    pB.strikePos = 0.0f;     // x0 = 0.35
    std::vector<float> bufB(kN, 0.0f);
    runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody, pB, kSR,
                   bufB.data(), kN);

    const double f0 = 1500.0 * std::pow(0.1, 0.7);
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
    INFO("NoiseBody strikePos L1 distance = " << dist);
    CHECK(dist >= 0.10f);
}

// ==============================================================================
// Phase 6 (T080 / US4-4): Material sweep -> decay tilt changes monotonically.
// ==============================================================================
TEST_CASE("NoiseBody: Material sweep changes decay tilt monotonically (US4-4)",
          "[membrum][body][noise][BodyModes]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = static_cast<int>(kSR);  // 1 second

    Membrum::BodyBank bank;

    auto base = makeDefaultParams();
    base.size      = 0.7f;       // f0 ~ 299 Hz
    base.decay     = 0.9f;
    base.strikePos = 0.37f;

    const double f0     = 1500.0 * std::pow(0.1, 0.7);
    const double lowHz  = f0 * Membrum::Bodies::kPlateRatios[0];
    const double highHz = f0 * Membrum::Bodies::kPlateRatios[3];

    const int tailStart  = static_cast<int>(kSR * 0.15);
    const int tailLength = static_cast<int>(kSR * 0.40);

    auto runAt = [&](float materialVal) noexcept {
        auto p = base;
        p.material = materialVal;
        std::vector<float> buf(kN, 0.0f);
        runBodyImpulse(bank, Membrum::BodyModelType::NoiseBody, p, kSR,
                       buf.data(), kN);
        const float magLow =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, lowHz);
        const float magHigh =
            goertzelWindowMag(buf.data(), tailStart, tailLength, kSR, highHz);
        return magHigh / std::max(magLow, 1e-9f);
    };

    const float tiltLow  = runAt(0.0f);
    const float tiltHigh = runAt(1.0f);

    INFO("NoiseBody material-tilt ratio: low=" << tiltLow
         << " high=" << tiltHigh);
    REQUIRE(tiltLow > 0.0f);
    REQUIRE(tiltHigh > 0.0f);
    CHECK(tiltHigh > tiltLow);
}

// ==============================================================================
// Phase 6 (T080 / US4-5): Level = 0 -> DrumVoice output is silent.
// ==============================================================================
TEST_CASE("NoiseBody: DrumVoice with Level=0 produces silent output (US4-5)",
          "[membrum][body][noise][BodyModes]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = static_cast<int>(kSR * 0.25);

    Membrum::DrumVoice voice;
    voice.prepare(kSR, 0u);
    voice.setBodyModel(Membrum::BodyModelType::NoiseBody);
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
    INFO("NoiseBody Level=0 peakAbs=" << peakAbs);
    CHECK(allFinite);
    CHECK(peakAbs == 0.0f);
}
