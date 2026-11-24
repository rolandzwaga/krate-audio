// ==============================================================================
// ToneShaper core tests (Phase 7 — T091)
// ==============================================================================
// tone_shaper_contract.md §Test coverage requirements items 2–5:
//   2. Filter envelope sweep — LP filter modulated by ADSR; centroid rises then falls.
//   3. Drive harmonic generation — THD increases with drive amount; |peak| ≤ 0 dBFS.
//   4. Wavefolder odd harmonics — 3rd/5th/7th harmonic magnitudes increase with fold.
//   5. Allocation-detector — all ToneShaper methods zero-alloc.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/drum_voice.h"
#include "dsp/pad_config.h"
#include "dsp/pad_config_apply.h"
#include "dsp/tone_shaper.h"

#include <allocation_detector.h>
#include <test_signals.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr double kPi         = 3.14159265358979323846;

std::vector<float> makeSine(int n, float hz, double sr)
{
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    const double omega = 2.0 * kPi * static_cast<double>(hz) / sr;
    for (int i = 0; i < n; ++i)
        buf[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(omega * static_cast<double>(i)));
    return buf;
}

std::vector<float> makePinkNoise(int n, std::uint32_t seed = 1234u)
{
    // Voss-McCartney approximation simplified: white noise with a few filters.
    // For the filter sweep test we only need broadband content.
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        buf[static_cast<std::size_t>(i)] = dist(rng) * 0.3f;
    return buf;
}

// Goertzel magnitude at a specific frequency.
double goertzelMag(const float* samples, int n, double sr, double hz)
{
    const double omega = 2.0 * kPi * hz / sr;
    const double coeff = 2.0 * std::cos(omega);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double s = static_cast<double>(samples[i]) + coeff * s1 - s2;
        s2 = s1; s1 = s;
    }
    const double real = s1 - s2 * std::cos(omega);
    const double imag = s2 * std::sin(omega);
    return std::sqrt(real * real + imag * imag);
}

double computePeak(const std::vector<float>& buf)
{
    double peak = 0.0;
    for (float s : buf)
        peak = std::max(peak, static_cast<double>(std::abs(s)));
    return peak;
}

// Approximate THD relative to 1 kHz fundamental (sum of 2nd..5th harmonic power).
double computeTHD(const std::vector<float>& buf, double sr, double fundamentalHz)
{
    const int n = static_cast<int>(buf.size());
    const double fundMag = goertzelMag(buf.data(), n, sr, fundamentalHz);
    if (fundMag < 1e-9) return 0.0;
    double harmSum = 0.0;
    for (int k = 2; k <= 5; ++k)
        harmSum += goertzelMag(buf.data(), n, sr, fundamentalHz * k);
    return harmSum / fundMag;
}

} // namespace

TEST_CASE("ToneShaper: Drive harmonic generation increases with drive amount (US5-3)",
          "[membrum][tone_shaper][drive]")
{
    const int kN = 4410;  // 100 ms
    const auto input = makeSine(kN, 1000.0f, kSampleRate);

    auto renderWithDrive = [&](float driveAmt) {
        Membrum::ToneShaper ts;
        ts.prepare(kSampleRate);
        ts.setDriveAmount(driveAmt);
        ts.setFoldAmount(0.0f);
        ts.setFilterCutoff(20000.0f);
        ts.setFilterEnvAmount(0.0f);
        ts.setPitchEnvTimeMs(0.0f);
        ts.noteOn(1.0f);

        std::vector<float> out(static_cast<std::size_t>(kN), 0.0f);
        for (int i = 0; i < kN; ++i)
            out[static_cast<std::size_t>(i)] = ts.processSample(input[static_cast<std::size_t>(i)]);
        return out;
    };

    const auto out0 = renderWithDrive(0.0f);
    const auto out5 = renderWithDrive(0.5f);
    const auto out1 = renderWithDrive(1.0f);

    const double thd0 = computeTHD(out0, kSampleRate, 1000.0);
    const double thd5 = computeTHD(out5, kSampleRate, 1000.0);
    const double thd1 = computeTHD(out1, kSampleRate, 1000.0);

    INFO("THD: drive=0 -> " << thd0 << ", drive=0.5 -> " << thd5 << ", drive=1.0 -> " << thd1);

    // At drive=0 the chain is bypassed, so THD should be (near) zero.
    CHECK(thd0 < 1e-3);
    // THD should increase monotonically with drive amount.
    CHECK(thd5 > thd0);
    CHECK(thd1 > thd5);

    // Peak output bounded (soft-saturated). Allows small overshoot since
    // the blend between dry and shaped signal can produce amplitudes
    // slightly above the pure tanh range when dry/wet phase is coherent.
    CHECK(computePeak(out1) <= 1.1);
}

TEST_CASE("ToneShaper: Drive is a flavour saturator, not a makeup compressor (M-2)",
          "[membrum][tone_shaper][drive][makeup]")
{
    // M-2 (AUDIT-signal-path): the Drive stage maps amount -> internal gain
    // 1..10 into recipSqrt with no makeup, so small-signal content is boosted
    // up to ~10x while peaks pin to +/-1 -> the stage acts as a hidden
    // compressor/limiter instead of a flavour saturator. The fix adds a
    // post-shaper makeup of 1/slope-at-zero (= 1 / internalDrive) so the
    // unity (small-signal) region stays at ~unity gain and Drive changes
    // TIMBRE (harmonics), not LEVEL.
    //
    // This test feeds a very small sine (well inside the linear region of the
    // shaper) and asserts the RMS gain stays ~1.0 across drive amounts. Before
    // the fix the gain tracks the internal drive (up to ~10x) and this fails.
    const int kN = 4410;
    constexpr float kSmallAmp = 0.004f; // deep in the shaper's linear region
    auto input = makeSine(kN, 1000.0f, kSampleRate);
    for (auto& s : input) s *= kSmallAmp;

    auto rmsGain = [&](float driveAmt) {
        Membrum::ToneShaper ts;
        ts.prepare(kSampleRate);
        ts.setDriveAmount(driveAmt);
        ts.setFoldAmount(0.0f);
        ts.setFilterCutoff(20000.0f);
        ts.setFilterEnvAmount(0.0f);
        ts.setPitchEnvTimeMs(0.0f);
        ts.noteOn(1.0f);

        double sumIn = 0.0, sumOut = 0.0;
        for (int i = 0; i < kN; ++i)
        {
            const float in  = input[static_cast<std::size_t>(i)];
            const float out = ts.processSample(in);
            sumIn  += static_cast<double>(in) * in;
            sumOut += static_cast<double>(out) * out;
        }
        return std::sqrt(sumOut / std::max(sumIn, 1e-30));
    };

    const double g025 = rmsGain(0.25f);
    const double g050 = rmsGain(0.50f);
    const double g100 = rmsGain(1.00f);
    INFO("small-signal RMS gain: drive=0.25 -> " << g025
         << ", 0.50 -> " << g050 << ", 1.00 -> " << g100);

    // Unity region must stay ~unity regardless of drive amount.
    CHECK(g025 == Approx(1.0).margin(0.20));
    CHECK(g050 == Approx(1.0).margin(0.20));
    CHECK(g100 == Approx(1.0).margin(0.20));
}

TEST_CASE("ToneShaper: Wavefolder generates odd harmonics (US5-4)",
          "[membrum][tone_shaper][fold]")
{
    const int kN = 8820;  // 200 ms for better frequency resolution
    const auto input = makeSine(kN, 1000.0f, kSampleRate);

    auto render = [&](float foldAmt) {
        Membrum::ToneShaper ts;
        ts.prepare(kSampleRate);
        ts.setDriveAmount(0.0f);
        ts.setFoldAmount(foldAmt);
        ts.setFilterCutoff(20000.0f);
        ts.setFilterEnvAmount(0.0f);
        ts.setPitchEnvTimeMs(0.0f);
        ts.noteOn(1.0f);
        std::vector<float> out(static_cast<std::size_t>(kN), 0.0f);
        for (int i = 0; i < kN; ++i)
            out[static_cast<std::size_t>(i)] = ts.processSample(input[static_cast<std::size_t>(i)]);
        return out;
    };

    const auto out0 = render(0.0f);
    const auto out1 = render(1.0f);

    const double h3_0 = goertzelMag(out0.data(), kN, kSampleRate, 3000.0);
    const double h5_0 = goertzelMag(out0.data(), kN, kSampleRate, 5000.0);
    const double h7_0 = goertzelMag(out0.data(), kN, kSampleRate, 7000.0);

    const double h3_1 = goertzelMag(out1.data(), kN, kSampleRate, 3000.0);
    const double h5_1 = goertzelMag(out1.data(), kN, kSampleRate, 5000.0);
    const double h7_1 = goertzelMag(out1.data(), kN, kSampleRate, 7000.0);

    INFO("3rd: " << h3_0 << " -> " << h3_1);
    INFO("5th: " << h5_0 << " -> " << h5_1);
    INFO("7th: " << h7_0 << " -> " << h7_1);

    // Wavefolding should substantially increase odd harmonics.
    CHECK(h3_1 > h3_0 * 2.0);
    CHECK(h5_1 > h5_0 * 2.0);
    CHECK(h7_1 > h7_0 * 2.0);
}

TEST_CASE("ToneShaper: Filter envelope sweep modulates LP cutoff (US5-2)",
          "[membrum][tone_shaper][filter_env]")
{
    const int kN = static_cast<int>(kSampleRate * 0.2);  // 200 ms
    const auto input = makePinkNoise(kN);

    Membrum::ToneShaper ts;
    ts.prepare(kSampleRate);
    ts.setDriveAmount(0.0f);
    ts.setFoldAmount(0.0f);
    ts.setFilterType(Membrum::ToneShaperFilterType::Lowpass);
    ts.setFilterCutoff(200.0f);     // base cutoff very low
    ts.setFilterResonance(0.2f);
    ts.setFilterEnvAmount(1.0f);    // full upward sweep
    ts.setFilterEnvAttackMs(5.0f);
    ts.setFilterEnvDecayMs(100.0f);
    ts.setFilterEnvSustain(0.0f);
    ts.setPitchEnvTimeMs(0.0f);
    ts.noteOn(1.0f);

    std::vector<float> out(static_cast<std::size_t>(kN), 0.0f);
    for (int i = 0; i < kN; ++i)
        out[static_cast<std::size_t>(i)] = ts.processSample(input[static_cast<std::size_t>(i)]);

    // Measure high-frequency energy (3 kHz) at three snapshots:
    //   t ≈ 1 ms    (filter still closed, lowpass blocks 3 kHz)
    //   t ≈ 10 ms   (near attack peak, lots of 3 kHz)
    //   t ≈ 150 ms  (after decay, filter back to low cutoff)
    // Hann-windowed: with a rectangular 3 ms window the resonant 200 Hz
    // content leaks into the 3 kHz bin and dominates the t=150 ms snapshot.
    // (The snapshots only differed before T1.15 because the SVF still glided
    // down from 20 kHz at t=10 ms.)
    auto windowMag = [&](int startSample, int lengthSamples, double hz) {
        std::vector<float> seg(static_cast<std::size_t>(lengthSamples), 0.0f);
        for (int i = 0; i < lengthSamples; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * (static_cast<double>(i) + 0.5)
                                                  / static_cast<double>(lengthSamples));
            seg[static_cast<std::size_t>(i)] =
                static_cast<float>(out[static_cast<std::size_t>(startSample + i)] * w);
        }
        return goertzelMag(seg.data(), lengthSamples, kSampleRate, hz);
    };

    const int lenWin  = static_cast<int>(kSampleRate * 0.003);  // 3 ms windows
    const double mag_start = windowMag(0, lenWin, 3000.0);
    const double mag_peak  = windowMag(static_cast<int>(kSampleRate * 0.010), lenWin, 3000.0);
    const double mag_end   = windowMag(static_cast<int>(kSampleRate * 0.150), lenWin, 3000.0);

    INFO("3 kHz magnitude: t=0 -> " << mag_start
         << ", t=10ms -> " << mag_peak
         << ", t=150ms -> " << mag_end);

    // Envelope is rising at t=10ms so 3 kHz content should be substantially
    // higher than at both the start and the end.
    CHECK(mag_peak > mag_start);
    CHECK(mag_peak > mag_end);
}

TEST_CASE("ToneShaper: all methods are zero-allocation",
          "[membrum][tone_shaper][alloc]")
{
    Membrum::ToneShaper ts;
    ts.prepare(kSampleRate);
    ts.setDriveAmount(0.5f);
    ts.setFoldAmount(0.3f);
    ts.setFilterCutoff(2000.0f);
    ts.setFilterResonance(0.4f);
    ts.setFilterEnvAmount(0.5f);
    ts.setPitchEnvStartHz(160.0f);
    ts.setPitchEnvEndHz(50.0f);
    ts.setPitchEnvTimeMs(20.0f);

    // noteOn() is an audio-thread setter (triggers envelopes, latches natural
    // fundamental) and must be zero-allocation per T091 contract.
    {
        TestHelpers::AllocationScope scope;
        ts.noteOn(0.8f);
        CHECK(scope.getAllocationCount() == 0);
    }

    {
        TestHelpers::AllocationScope scope;
        for (int i = 0; i < 1024; ++i)
        {
            (void)ts.processPitchEnvelope();
            (void)ts.processSample(0.5f * std::sin(static_cast<float>(i) * 0.1f));
        }
        CHECK(scope.getAllocationCount() == 0);
    }

    // Parameter setters during processing (audio thread setters must be noexcept
    // and allocation-free).
    {
        TestHelpers::AllocationScope scope;
        ts.setDriveAmount(0.2f);
        ts.setFoldAmount(0.1f);
        ts.setFilterCutoff(1500.0f);
        ts.setFilterResonance(0.6f);
        ts.setFilterEnvAmount(0.3f);
        ts.setFilterEnvAttackMs(2.0f);
        ts.setFilterEnvDecayMs(50.0f);
        ts.setFilterEnvSustain(0.2f);
        ts.setFilterEnvReleaseMs(100.0f);
        ts.setPitchEnvStartHz(150.0f);
        ts.setPitchEnvEndHz(45.0f);
        ts.setPitchEnvTimeMs(25.0f);
        ts.setPitchEnvCurve(Membrum::ToneShaperCurve::Linear);
        ts.noteOff();
        CHECK(scope.getAllocationCount() == 0);
    }
}

// MB-047: VoicePool applies the whole PadConfig to a voice at every note-on on
// the audio thread. Each pitch-envelope setter used to rebuild the LUTs
// (256 powf per table), so an unchanged pad paid 8 rebuilds per note-on.
TEST_CASE("ToneShaper: pitch-envelope tables are rebuilt only when an input changes",
          "[tone_shaper][rt-safety]")
{
    Membrum::DrumVoice voice;
    voice.prepare(48000.0);

    Membrum::PadConfig cfg{};
    cfg.tsPitchEnvTime        = 0.1f;   // 50 ms
    cfg.tsPitchEnvKneeEnabled = 1.0f;
    cfg.tsPitchEnvStart       = 0.6f;
    cfg.tsPitchEnvEnd         = 0.2f;

    // Warm-up: the first apply after prepare legitimately builds the tables.
    Membrum::applyPadConfigToVoice(voice, cfg);
    voice.noteOn(1.0f);
    const std::uint32_t warm = voice.toneShaper().pitchEnvRebuildCountForTest();

    // Same config, next note-on: nothing changed, nothing is rebuilt.
    Membrum::applyPadConfigToVoice(voice, cfg);
    voice.noteOn(1.0f);
    const std::uint32_t same = voice.toneShaper().pitchEnvRebuildCountForTest();
    INFO("rebuilds for an unchanged pad: " << (same - warm));
    CHECK(same == warm);

    // One input changes: exactly one rebuild.
    cfg.tsPitchEnvEnd = 0.3f;
    Membrum::applyPadConfigToVoice(voice, cfg);
    voice.noteOn(1.0f);
    const std::uint32_t changed = voice.toneShaper().pitchEnvRebuildCountForTest();
    INFO("rebuilds after one changed input: " << (changed - same));
    CHECK(changed == same + 1u);

    // Disabled envelope (time 0): a changed input builds nothing.
    cfg.tsPitchEnvTime = 0.0f;
    cfg.tsPitchEnvEnd  = 0.4f;
    Membrum::applyPadConfigToVoice(voice, cfg);
    voice.noteOn(1.0f);
    const std::uint32_t disabled = voice.toneShaper().pitchEnvRebuildCountForTest();
    INFO("rebuilds with the envelope disabled: " << (disabled - changed));
    CHECK(disabled == changed);
}

// MB-042 / MB-265: the SVF coefficient smoother used to carry g, k and the mode
// mix from whatever the slot played last (20 kHz lowpass on a fresh instance),
// so a hit at LP 300 Hz passed its attack almost unfiltered.
TEST_CASE("ToneShaper: a note starts at its own filter cutoff and type",
          "[tone_shaper][filter]")
{
    constexpr double kSr     = 48000.0;
    constexpr int    kWin    = 240;   // 5 ms at 48 kHz
    constexpr int    kNoiseN = 4800;  // 100 ms at 48 kHz

    std::vector<float> noise(static_cast<std::size_t>(kNoiseN), 0.0f);
    TestHelpers::generateWhiteNoise(noise.data(), noise.size());

    const auto rmsOf = [](const std::vector<float>& x, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i)
            s += static_cast<double>(x[static_cast<std::size_t>(i)])
                 * static_cast<double>(x[static_cast<std::size_t>(i)]);
        return std::sqrt(s / static_cast<double>(n));
    };
    const auto dbOf = [](double r) { return 20.0 * std::log10(std::max(r, 1e-30)); };

    const auto playNote = [&](Membrum::ToneShaper& ts, Membrum::ToneShaperFilterType type,
                              float cutoffHz, int n) {
        ts.setFilterType(type);
        ts.setFilterCutoff(cutoffHz);
        ts.setFilterEnvAmount(0.0f);
        ts.noteOn(1.0f);
        std::vector<float> out(static_cast<std::size_t>(n), 0.0f);
        for (int i = 0; i < n; ++i)
            out[static_cast<std::size_t>(i)] =
                ts.processSample(noise[static_cast<std::size_t>(i)]);
        return out;
    };

    const double inDb = dbOf(rmsOf(noise, kWin));

    Membrum::ToneShaper fresh;
    fresh.prepare(kSr);
    const auto freshOut =
        playNote(fresh, Membrum::ToneShaperFilterType::Lowpass, 300.0f, kWin);
    const double freshDb = dbOf(rmsOf(freshOut, kWin));

    SECTION("fresh")
    {
        INFO("input 0-5 ms " << inDb << " dB, output " << freshDb << " dB");
        REQUIRE(freshDb <= inDb - 20.0);
    }

    SECTION("after a highpass note")
    {
        // Same instance: the fresh LP note, then a highpass note, then LP again.
        (void)playNote(fresh, Membrum::ToneShaperFilterType::Highpass, 14000.0f, kNoiseN);
        const auto out =
            playNote(fresh, Membrum::ToneShaperFilterType::Lowpass, 300.0f, kWin);
        const double afterDb = dbOf(rmsOf(out, kWin));
        INFO("fresh " << freshDb << " dB, after a highpass note " << afterDb << " dB");
        REQUIRE(std::abs(afterDb - freshDb) <= 0.5);
    }
}
