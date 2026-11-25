// ==============================================================================
// Coupling partials (fix plan T2.17: MB-068)
// ==============================================================================
// VoicePool registers a struck voice's sympathetic resonators from
// DrumVoice::getPartialInfo(), right after noteOn (and again for every ringing
// voice once a kit-switch coupling fade ends). The resonators must sit at the
// frequencies the pad settles at:
//   * a pad with a pitch envelope registered the envelope's START frequency
//     times its mode ratios (the default kick: 160 Hz, ~1.7 octaves above the
//     50 Hz it rings at);
//   * a String pad never configures the shared modal bank, so it registered
//     the modes another pad left on the slot, or 0 Hz on a fresh slot.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_model_type.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"
#include "dsp/pad_config.h"
#include "dsp/pad_config_apply.h"

#include <krate/dsp/systems/sympathetic_resonance.h>

#include <array>
#include <cmath>
#include <cstddef>

namespace {

constexpr double kCpSampleRate = 44100.0;
constexpr int    kCpBlock      = 256;

void renderSamples(Membrum::DrumVoice& v, int numSamples)
{
    std::array<float, kCpBlock> buf{};
    while (numSamples > 0)
    {
        const int n = numSamples < kCpBlock ? numSamples : kCpBlock;
        v.processBlock(buf.data(), n);
        numSamples -= n;
    }
}

bool withinOnePercent(float actual, float expected)
{
    return expected > 0.0f && std::fabs(actual - expected) <= 0.01f * expected;
}

// Membrane + Impulse pad sweeping 160 Hz -> 50 Hz over 20 ms.
Membrum::PadConfig pitchEnvelopePad()
{
    Membrum::PadConfig cfg{};
    cfg.exciterType     = Membrum::ExciterType::Impulse;
    cfg.bodyModel       = Membrum::BodyModelType::Membrane;
    cfg.tsPitchEnvStart = 0.4515f;   // 160 Hz
    cfg.tsPitchEnvEnd   = 0.19897f;  // 50 Hz
    cfg.tsPitchEnvTime  = 0.04f;     // 20 ms
    return cfg;
}

// String pad at 110 Hz: 800 x 0.1^size (natural_fundamental.h).
Membrum::PadConfig stringPad110()
{
    Membrum::PadConfig cfg{};
    cfg.exciterType = Membrum::ExciterType::Impulse;
    cfg.bodyModel   = Membrum::BodyModelType::String;
    cfg.size        = 0.8617f;  // log10(800 / 110)
    return cfg;
}

void checkStringHarmonics(const Krate::DSP::SympatheticPartialInfo& info)
{
    for (std::size_t k = 0; k < info.frequencies.size(); ++k)
    {
        const float expected = 110.0f * static_cast<float>(k + 1);
        INFO("partial " << k << " = " << info.frequencies[k] << " Hz, expected "
                        << expected << " Hz");
        REQUIRE(withinOnePercent(info.frequencies[k], expected));
    }
}

} // namespace

TEST_CASE("Coupling partials: a pitch-envelope pad registers its settled modes",
          "[coupling][dsp]")
{
    Membrum::DrumVoice voice;
    voice.prepare(kCpSampleRate, 0u);
    Membrum::applyPadConfigToVoice(voice, pitchEnvelopePad());
    voice.noteOn(1.0f);

    // As VoicePool::registerCouplingPartials does, right after noteOn.
    const auto atNoteOn = voice.getPartialInfo();

    // A kit-switch coupling fade re-registers ringing voices mid-note
    // (VoicePool::registerActiveVoicesWithCoupling): 5 ms in, the sweep is
    // still running.
    renderSamples(voice, static_cast<int>(kCpSampleRate * 0.005));
    const auto midSweep = voice.getPartialInfo();

    // After the 20 ms envelope the bank's targets are the settled modes.
    renderSamples(voice, static_cast<int>(kCpSampleRate * 0.095));
    const auto& bank = voice.bodyBank().getSharedBank();
    REQUIRE(bank.getNumModes() >= Krate::DSP::kSympatheticPartialCount);

    for (int k = 0; k < Krate::DSP::kSympatheticPartialCount; ++k)
    {
        const float settled = bank.getModeFrequency(k);
        const auto  idx     = static_cast<std::size_t>(k);
        INFO("mode " << k << ": settled " << settled << " Hz, at note-on "
                     << atNoteOn.frequencies[idx] << " Hz, mid-sweep "
                     << midSweep.frequencies[idx] << " Hz");
        CHECK(withinOnePercent(atNoteOn.frequencies[idx], settled));
        CHECK(withinOnePercent(midSweep.frequencies[idx], settled));
    }
    // Mode 0 of a Membrane is the 50 Hz the envelope ends at.
    CHECK(withinOnePercent(atNoteOn.frequencies[0], 50.0f));
}

TEST_CASE("Coupling partials: a String pad registers its own harmonics, not the slot's previous modes",
          "[coupling][dsp]")
{
    SECTION("slot last played a 300 Hz Membrane pad")
    {
        Membrum::DrumVoice voice;
        voice.prepare(kCpSampleRate, 0u);
        Membrum::PadConfig membrane{};
        membrane.bodyModel = Membrum::BodyModelType::Membrane;
        membrane.size      = 0.2218f;  // log10(500 / 300): 300 Hz
        Membrum::applyPadConfigToVoice(voice, membrane);
        voice.noteOn(1.0f);
        renderSamples(voice, static_cast<int>(kCpSampleRate * 0.05));

        Membrum::applyPadConfigToVoice(voice, stringPad110());
        voice.noteOn(1.0f);
        checkStringHarmonics(voice.getPartialInfo());
    }

    SECTION("freshly prepared slot")
    {
        Membrum::DrumVoice voice;
        voice.prepare(kCpSampleRate, 0u);
        Membrum::applyPadConfigToVoice(voice, stringPad110());
        voice.noteOn(1.0f);
        checkStringHarmonics(voice.getPartialInfo());
    }
}
