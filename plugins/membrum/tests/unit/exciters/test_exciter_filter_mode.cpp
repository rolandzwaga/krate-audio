// ==============================================================================
// T1.26 (MB-273): NoiseBurst, Clap and Friction run their burst through a
// shared SVF whose cutoff each trigger sets before calling snapToTarget().
// With smoothing off, snapToTarget() used to restore the mix targets of the
// mode current at prepare() (Lowpass), so the Bandpass that NoiseBurst and
// Clap requested never played. The shared svf.h now keeps the targets equal
// to the live mix, and NoiseBurst and Clap declare the Lowpass they have
// always shipped with (user ruling T1.26, option A: kits bit-identical).
//
// Each section renders one hit at 48 kHz and holds it against a reference
// SVF in the exciter's DECLARED mode, configured before prepare() (so every
// target comes from the final settings), on the same pre-filter source,
// within 1e-6:
//   NoiseBurst: Lowpass, Q 2, cutoff 800 x 12^v
//   Clap:       Lowpass, Q 1, cutoff 1200 + 1000 v
//   Friction:   Bandpass, Q 1.5, cutoff 400 x 20^v
// The sources mirror each exciter's prepare / trigger / process
// (noise_burst_exciter.h, clap_exciter.h, friction_exciter.h).
//
// Friction's part of MB-273 is refuted: its trigger calls the SVF's reset()
// after snapToTarget(), and reset() recomputes the mix from the mode, so it
// ran as a Bandpass before the svf.h fix too. Its section passed on the
// unchanged code and stays as a regression guard.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "dsp/exciters/clap_exciter.h"
#include "dsp/exciters/friction_exciter.h"
#include "dsp/exciters/noise_burst_exciter.h"

#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/bow_exciter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

constexpr double kModeSampleRate = 48000.0;
constexpr std::size_t kModeRenderSamples = 8192;  // 170 ms: every burst ends inside it
constexpr std::uint32_t kModeVoiceId = 0u;
constexpr float kModePi = std::numbers::pi_v<float>;

/// NoiseBurstExciter's pre-filter signal for one hit on a fresh exciter
/// (prepare, setContactMs(contactNorm), trigger(velocity)).
[[nodiscard]] std::vector<float> renderNoiseBurstSource(float velocity, float contactNorm)
{
    Krate::DSP::NoiseOscillator noise;
    noise.prepare(kModeSampleRate);
    noise.setColor(Krate::DSP::NoiseColor::Violet);
    noise.setSeed(0xB357u ^ kModeVoiceId);
    noise.reset();

    const float contactMs = 2.0f + std::clamp(contactNorm, 0.0f, 1.0f) * 13.0f;
    const float durationMs = std::max(0.5f, contactMs * (1.2f - 0.4f * velocity));
    const float samplesF = durationMs * 1e-3f * static_cast<float>(kModeSampleRate);
    const int burstTotal = std::max(2, static_cast<int>(std::lround(samplesF)));
    const int riseSamples = std::max(1, burstTotal / 5);
    const int decaySamples = std::max(1, burstTotal - riseSamples);
    const float decayK = 6.9078f / static_cast<float>(decaySamples);
    const float amplitude = 0.2f + 0.8f * velocity;

    std::vector<float> source(static_cast<std::size_t>(burstTotal));
    for (int n = 0; n < burstTotal; ++n)
    {
        float env;
        if (n < riseSamples)
        {
            const float t = static_cast<float>(n) / static_cast<float>(riseSamples);
            env = 0.5f * (1.0f - std::cos(kModePi * t));
        }
        else
        {
            env = std::exp(-decayK * static_cast<float>(n - riseSamples));
        }
        source[static_cast<std::size_t>(n)] = noise.process() * amplitude * env;
    }
    return source;
}

/// ClapExciter's pre-filter signal for one hit on a fresh exciter.
[[nodiscard]] std::vector<float> renderClapSource(float velocity)
{
    Krate::DSP::NoiseOscillator noise;
    noise.prepare(kModeSampleRate);
    noise.setColor(Krate::DSP::NoiseColor::White);
    noise.setSeed(0xC1A9u ^ kModeVoiceId);
    noise.reset();

    using Clap = Membrum::ClapExciter;
    const auto sr = static_cast<float>(kModeSampleRate);
    const int riseSamples =
        std::max(1, static_cast<int>(std::lround(Clap::kBurstRiseMs * 1.0e-3f * sr)));
    const int decaySamples =
        std::max(1, static_cast<int>(std::lround(Clap::kBurstDecayMs * 1.0e-3f * sr)));
    const float decayK = 6.9078f / static_cast<float>(decaySamples);
    const int burstSpan = riseSamples + decaySamples;

    std::array<int, Clap::kNumBursts> onsets{};
    int total = 0;
    for (std::size_t k = 0; k < onsets.size(); ++k)
    {
        onsets[k] = static_cast<int>(std::lround(Clap::kOnsetMs[k] * 1.0e-3f * sr));
        total = std::max(total, onsets[k] + burstSpan);
    }
    const float amplitude = 0.2f + 0.8f * velocity;

    std::vector<float> source(static_cast<std::size_t>(total));
    for (int i = 0; i < total; ++i)
    {
        float env = 0.0f;
        for (std::size_t k = 0; k < onsets.size(); ++k)
        {
            const int n = i - onsets[k];
            if (n < 0 || n >= burstSpan)
                continue;
            float e;
            if (n < riseSamples)
            {
                const float t = static_cast<float>(n) / static_cast<float>(riseSamples);
                e = 0.5f * (1.0f - std::cos(kModePi * t));
            }
            else
            {
                e = std::exp(-decayK * static_cast<float>(n - riseSamples));
            }
            env += Clap::kRelAmp[k] * e;
        }
        source[static_cast<std::size_t>(i)] = noise.process() * amplitude * env;
    }
    return source;
}

/// FrictionExciter's pre-filter signal: the same bow core, envelope, pressure,
/// speed, position and auto-release as friction_exciter.h (Friction Pressure
/// 0). The vector ends where the exciter stops.
[[nodiscard]] std::vector<float> renderFrictionSource(float velocity)
{
    const auto sr = static_cast<float>(kModeSampleRate);
    Krate::DSP::BowExciter core;
    core.prepare(kModeSampleRate);
    core.reset();
    Krate::DSP::ADSREnvelope envelope;
    envelope.prepare(sr);
    envelope.setAttack(1.0f);
    envelope.setDecay(40.0f);
    envelope.setSustain(0.0f);
    envelope.setRelease(5.0f);
    envelope.reset();
    const int autoReleaseSamples = static_cast<int>(0.041f * sr);

    core.setPressure(std::clamp(0.1f + (0.5f - 0.1f) * velocity, 0.0f, 1.0f));
    core.setSpeed(0.2f + (0.8f - 0.2f) * velocity);
    core.setPosition(0.13f);
    envelope.reset();
    envelope.gate(true);
    core.trigger(velocity);

    std::vector<float> source;
    source.reserve(kModeRenderSamples);
    for (std::size_t i = 0; i < kModeRenderSamples; ++i)
    {
        if (static_cast<int>(i) == autoReleaseSamples)
            envelope.gate(false);
        core.setEnvelopeValue(envelope.process());
        source.push_back(core.process(0.0f));
        if (!envelope.isActive())
            break;
    }
    return source;
}

/// The source through a reference SVF configured before prepare(), then
/// scaled by the exciter's output gain; zero after the source ends (each
/// exciter returns 0 once its burst is done).
[[nodiscard]] std::vector<float> filterReference(const std::vector<float>& source,
                                                 Krate::DSP::SVFMode mode, float q,
                                                 float cutoff, float outputGain)
{
    Krate::DSP::SVF reference;
    reference.setMode(mode);
    reference.setResonance(q);
    reference.setCutoff(cutoff);
    reference.prepare(kModeSampleRate);
    std::vector<float> out(kModeRenderSamples, 0.0f);
    for (std::size_t i = 0; i < source.size() && i < out.size(); ++i)
        out[i] = reference.process(source[i]) * outputGain;
    return out;
}

template <typename Exciter>
[[nodiscard]] std::vector<float> renderHit(Exciter& exciter, float velocity)
{
    std::vector<float> hit(kModeRenderSamples, 0.0f);
    exciter.trigger(velocity);
    for (auto& s : hit)
        s = exciter.process(0.0f);
    return hit;
}

void checkMatchesReference(const char* name, float velocity, const std::vector<float>& hit,
                           const std::vector<float>& expected)
{
    float maxDiff = 0.0f;
    float peak = 0.0f;
    for (std::size_t i = 0; i < hit.size(); ++i)
    {
        maxDiff = std::max(maxDiff, std::abs(hit[i] - expected[i]));
        peak = std::max(peak, std::abs(expected[i]));
    }
    INFO(name << " v=" << velocity << ": max abs diff to the reference SVF in the declared mode = "
              << maxDiff << " (reference peak " << peak << ")");
    CHECK(peak > 1e-4f);  // the comparison is not vacuous (Friction v=0.3 peaks at 4.4e-4)
    CHECK(maxDiff <= 1e-6f);
}

} // namespace

TEST_CASE("NoiseBurst, Clap and Friction exciters run their filter in the declared mode",
          "[exciter][svf]")
{
    using Krate::DSP::SVFMode;
    const float velocity = GENERATE(0.3f, 1.0f);

    SECTION("NoiseBurst: Lowpass, Q 2, cutoff 800 x 12^v")
    {
        constexpr float kContact = 1.0f;  // longest burst
        Membrum::NoiseBurstExciter exciter;
        exciter.prepare(kModeSampleRate, kModeVoiceId);
        exciter.setContactMs(kContact);
        const auto hit = renderHit(exciter, velocity);
        const auto expected =
            filterReference(renderNoiseBurstSource(velocity, kContact), SVFMode::Lowpass, 2.0f,
                            800.0f * std::pow(12.0f, velocity), 1.0f);
        checkMatchesReference("NoiseBurst", velocity, hit, expected);
    }

    SECTION("Clap: Lowpass, Q 1, cutoff 1200 + 1000 v")
    {
        Membrum::ClapExciter exciter;
        exciter.prepare(kModeSampleRate, kModeVoiceId);
        const auto hit = renderHit(exciter, velocity);
        const auto expected = filterReference(renderClapSource(velocity), SVFMode::Lowpass, 1.0f,
                                              1200.0f + 1000.0f * velocity, 1.0f);
        checkMatchesReference("Clap", velocity, hit, expected);
    }

    SECTION("Friction: Bandpass, Q 1.5, cutoff 400 x 20^v")
    {
        Membrum::FrictionExciter exciter;
        exciter.prepare(kModeSampleRate, kModeVoiceId);
        const auto hit = renderHit(exciter, velocity);
        // Output gain = velocity x cutoff / 8 kHz (friction_exciter.h, T1.17).
        const float cutoff = 400.0f * std::pow(20.0f, velocity);
        const auto expected = filterReference(renderFrictionSource(velocity), SVFMode::Bandpass,
                                              1.5f, cutoff, velocity * (cutoff / 8000.0f));
        checkMatchesReference("Friction", velocity, hit, expected);
    }
}
