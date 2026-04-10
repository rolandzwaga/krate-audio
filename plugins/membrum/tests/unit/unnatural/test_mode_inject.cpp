// ==============================================================================
// Mode Inject tests -- Phase 8, T104
// ==============================================================================
// Covers unnatural_zone_contract.md "Mode Inject" section and FR-052, FR-056.
//
// (a) amount=0.5 phase randomization between triggers: trigger the same voice
//     twice and assert the injected partial waveforms differ in phase between
//     triggers (deterministic per voiceId + trigger count) while the body
//     output's modes are the same phase both times.
// (b) amount=0 exact bypass: process() returns 0.0f without running the bank.
// (c) alloc-free: trigger() + process() zero heap activity (FR-056).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/unnatural/mode_inject.h"
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

} // namespace

// ==============================================================================
// T104(a) -- Phase randomization on every trigger. Two triggers on the same
// voice must produce different early-window samples (phases differ).
// ==============================================================================

TEST_CASE("UnnaturalZone ModeInject -- trigger randomizes partial phases",
          "[UnnaturalZone][ModeInject]")
{
    Membrum::ModeInject inject;
    inject.prepare(kSampleRate, /*voiceId*/ 7u);
    inject.setFundamentalHz(220.0f);
    inject.setAmount(0.5f);

    constexpr int kLen = 441; // 10 ms @ 44.1 kHz
    std::vector<float> first(kLen, 0.0f);
    std::vector<float> second(kLen, 0.0f);

    inject.trigger();
    for (int i = 0; i < kLen; ++i) first[i] = inject.process();

    inject.trigger();
    for (int i = 0; i < kLen; ++i) second[i] = inject.process();

    // Finite outputs.
    bool allFinite = true;
    for (float s : first)  if (!isFiniteSample(s)) { allFinite = false; break; }
    for (float s : second) if (!isFiniteSample(s)) { allFinite = false; break; }
    CHECK(allFinite);

    // Cumulative absolute difference: must be significantly non-zero because
    // the 8 partial phases are randomized independently on each trigger.
    double diff = 0.0;
    for (int i = 0; i < kLen; ++i)
        diff += std::abs(static_cast<double>(first[i]) - static_cast<double>(second[i]));
    INFO("Sum|first - second| = " << diff);
    CHECK(diff > 1e-3);
}

// ==============================================================================
// T104(b) -- amount == 0.0 is an exact bypass: process() returns 0.0f without
// running the oscillator bank (FR-052 zero-leak).
// ==============================================================================

TEST_CASE("UnnaturalZone ModeInject -- amount==0 exact bypass (zero leak)",
          "[UnnaturalZone][ModeInject][DefaultsOff]")
{
    Membrum::ModeInject inject;
    inject.prepare(kSampleRate, /*voiceId*/ 3u);
    inject.setFundamentalHz(440.0f);
    inject.setAmount(0.0f);

    inject.trigger();
    for (int i = 0; i < 100; ++i)
    {
        const float s = inject.process();
        CHECK(s == 0.0f);
    }

    // Even after a previous non-zero amount has left state in the bank,
    // setting amount back to 0 must still return exact zero.
    inject.setAmount(0.7f);
    inject.trigger();
    for (int i = 0; i < 50; ++i)
        (void) inject.process();

    inject.setAmount(0.0f);
    bool allZero = true;
    for (int i = 0; i < 100; ++i)
    {
        if (inject.process() != 0.0f) { allZero = false; break; }
    }
    CHECK(allZero);
}

// ==============================================================================
// T104(c) -- Allocation detector: trigger() + process() + setFundamentalHz()
// are zero-allocation on the audio thread.
// ==============================================================================

TEST_CASE("UnnaturalZone ModeInject -- zero heap allocations on audio thread",
          "[UnnaturalZone][ModeInject][allocation]")
{
    Membrum::ModeInject inject;
    inject.prepare(kSampleRate, /*voiceId*/ 5u);
    inject.setFundamentalHz(220.0f);
    inject.setAmount(0.5f);

    // Warm up once to flush any first-call paths.
    inject.trigger();
    for (int i = 0; i < 128; ++i) (void) inject.process();

    {
        TestHelpers::AllocationScope scope;
        inject.setFundamentalHz(330.0f);
        inject.trigger();
        for (int i = 0; i < 512; ++i) (void) inject.process();
        const size_t count = scope.getAllocationCount();
        INFO("ModeInject process/trigger alloc count = " << count);
        CHECK(count == 0u);
    }
}

// ==============================================================================
// T104 (extra) -- DrumVoice-level defaults-off: enabling the DrumVoice path
// with amount=0 must not perturb the audio output relative to the baseline.
// ==============================================================================

TEST_CASE("UnnaturalZone ModeInject -- DrumVoice amount==0 matches baseline",
          "[UnnaturalZone][ModeInject][DefaultsOff]")
{
    constexpr int numSamples = 4410; // 100 ms

    auto render = [](float amount) {
        Membrum::DrumVoice voice;
        voice.prepare(kSampleRate);
        voice.setMaterial(0.5f);
        voice.setSize(0.5f);
        voice.setDecay(0.3f);
        voice.setStrikePosition(0.3f);
        voice.setLevel(0.8f);
        voice.setExciterType(Membrum::ExciterType::Impulse);
        voice.setBodyModel(Membrum::BodyModelType::Membrane);
        voice.unnaturalZone().modeInject.setAmount(amount);
        voice.noteOn(0.8f);
        std::vector<float> out(static_cast<std::size_t>(numSamples), 0.0f);
        for (int i = 0; i < numSamples; ++i) out[static_cast<std::size_t>(i)] = voice.process();
        return out;
    };

    const auto baseline = render(0.0f);

    // Baseline render must differ from a non-zero amount so the test is
    // exercising a real code path (and not a no-op setter).
    const auto withInject = render(0.7f);
    double diff = 0.0;
    for (std::size_t i = 0; i < baseline.size(); ++i)
        diff += std::abs(baseline[i] - withInject[i]);
    CHECK(diff > 1e-3);

    // Baseline with amount=0.0 must match "no UnnaturalZone touch" path.
    Membrum::DrumVoice pristine;
    pristine.prepare(kSampleRate);
    pristine.setMaterial(0.5f);
    pristine.setSize(0.5f);
    pristine.setDecay(0.3f);
    pristine.setStrikePosition(0.3f);
    pristine.setLevel(0.8f);
    pristine.setExciterType(Membrum::ExciterType::Impulse);
    pristine.setBodyModel(Membrum::BodyModelType::Membrane);
    pristine.noteOn(0.8f);

    double sumSq = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const double p = static_cast<double>(pristine.process());
        const double b = static_cast<double>(baseline[static_cast<std::size_t>(i)]);
        sumSq += (p - b) * (p - b);
    }
    const double rms = std::sqrt(sumSq / numSamples);
    const double rmsDbv = (rms < 1e-12) ? -240.0 : 20.0 * std::log10(rms);
    INFO("pristine vs baseline RMS = " << rmsDbv << " dBFS");
    CHECK(rmsDbv <= -120.0);
}
