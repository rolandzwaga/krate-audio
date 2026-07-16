// ==============================================================================
// Mode Inject decay envelope -- 06-orchestralKit-fix-plan.md D2
// ==============================================================================
// Regression: ModeInject had NO decay envelope -- any amount > 0 rang as an
// undamped flat plateau (~-20 dBFS) that outlasted the drum body, audible as
// a synthetic "bass note" riding every hit (root cause of the Orchestral-kit
// timpani complaint, and the reason the hard constraint "modeInject > 0 rings
// undamped" existed at all). D2 adds a one-pole decay so the injected series
// dies with the drum:
//   (a) bare ModeInject output must DECAY, not plateau;
//   (b) the envelope follows setDecaySeconds();
//   (c) the amount == 0 exact-bypass contract (FR-052) is preserved;
//   (d) at the DrumVoice level, an inject-heavy short-decay voice must reach
//       silence instead of ringing forever on NoteOn-only hosts.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/unnatural/mode_inject.h"
#include "dsp/drum_voice.h"

#include <cmath>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;

double rmsDb(const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    double acc = 0.0;
    const std::size_t n = end - begin;
    for (std::size_t i = begin; i < end; ++i)
        acc += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    const double rms = std::sqrt(acc / static_cast<double>(n));
    return 20.0 * std::log10(std::max(rms, 1e-12));
}

} // namespace

TEST_CASE("ModeInject output decays instead of ringing undamped", "[unnatural][modeinject][D2]")
{
    Membrum::ModeInject inject;
    inject.prepare(kSampleRate, /*voiceId*/ 7);
    inject.setAmount(1.0f);
    inject.setFundamentalHz(200.0f);
    inject.trigger();

    const std::size_t total = static_cast<std::size_t>(kSampleRate * 4.0);
    std::vector<float> out(total);
    for (auto& s : out)
        s = inject.process();

    const std::size_t head = static_cast<std::size_t>(kSampleRate * 0.1);
    const double headDb = rmsDb(out, 0, head);
    const double tailDb = rmsDb(out, total - head, total);

    INFO("head " << headDb << " dB, tail " << tailDb << " dB");
    // Undamped plateau: tail == head. With the default envelope the 4 s tail
    // must sit at least 60 dB under the first 100 ms.
    REQUIRE(tailDb < headDb - 60.0);
}

TEST_CASE("ModeInject envelope follows setDecaySeconds", "[unnatural][modeinject][D2]")
{
    auto renderTailDb = [](float t60) {
        Membrum::ModeInject inject;
        inject.prepare(kSampleRate, /*voiceId*/ 3);
        inject.setAmount(1.0f);
        inject.setFundamentalHz(150.0f);
        inject.setDecaySeconds(t60);
        inject.trigger();
        const std::size_t total = static_cast<std::size_t>(kSampleRate * 2.0);
        std::vector<float> out(total);
        for (auto& s : out)
            s = inject.process();
        const std::size_t win = static_cast<std::size_t>(kSampleRate * 0.1);
        return rmsDb(out, total - win, total);
    };

    // Shorter T60 -> quieter 2 s tail, by a decisive margin.
    const double shortTail = renderTailDb(0.3f);
    const double longTail  = renderTailDb(3.0f);
    INFO("t60 0.3 s tail " << shortTail << " dB, t60 3 s tail " << longTail << " dB");
    REQUIRE(shortTail < longTail - 24.0);
}

TEST_CASE("ModeInject amount==0 exact bypass survives the envelope", "[unnatural][modeinject][D2]")
{
    Membrum::ModeInject inject;
    inject.prepare(kSampleRate, /*voiceId*/ 1);
    inject.setAmount(0.0f);
    inject.setFundamentalHz(200.0f);
    inject.trigger();
    for (int i = 0; i < 1000; ++i)
        REQUIRE(inject.process() == 0.0f);
}

TEST_CASE("DrumVoice with heavy modeInject reaches silence on NoteOn-only", "[unnatural][modeinject][D2]")
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate, /*voiceId*/ 11);
    voice.setExciterType(Membrum::ExciterType::Mallet);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);
    voice.setMaterial(0.35f);
    voice.setSize(0.7f);
    voice.setDecay(0.3f);       // short drum
    voice.setLevel(0.9f);
    voice.setNoiseLayerMix(0.0f);
    voice.setClickLayerMix(0.0f);
    voice.unnaturalZone().modeInject.setAmount(0.6f);

    voice.noteOn(1.0f);         // NoteOn ONLY -- no noteOff (pad-style host)

    const std::size_t total = static_cast<std::size_t>(kSampleRate * 6.0);
    std::vector<float> out(total, 0.0f);
    std::size_t offset = 0;
    while (offset < total)
    {
        const std::size_t n = std::min<std::size_t>(512, total - offset);
        voice.processBlock(out.data() + offset, static_cast<int>(n));
        offset += n;
    }

    const std::size_t win = static_cast<std::size_t>(kSampleRate * 0.5);
    const double tailDb = rmsDb(out, total - win, total);
    INFO("6 s NoteOn-only tail " << tailDb << " dBFS");
    // Pre-D2 this sat at a flat ~-20..-30 dBFS plateau forever.
    REQUIRE(tailDb < -60.0);
}
