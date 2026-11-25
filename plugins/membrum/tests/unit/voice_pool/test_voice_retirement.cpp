// ==============================================================================
// Voice retirement (fix plan T1.7: MB-027, MB-009, MB-018, MB-038)
// ==============================================================================
// A voice must retire once its body and envelope are silent. The Feedback
// exciter forces DrumVoice::processBlockSlow, which drives the modal bank per
// sample; before T1.7 nothing on that path called flushSilentModes(), so the
// bank's active-mode count never reached 0 and the voice held its slot for
// good (the audit probe saw 44 active modes on Membrane, 48 on Plate, with an
// output of exactly 0).
//
// T1.8 (MB-106, MB-160, MB-039): the String body runs a waveguide and never
// touches the shared modal bank, so the bank's mode count says nothing about
// whether a String voice still sounds. Before T1.8 a String voice on a fresh
// slot was cut after 257 host blocks (a block-size-dependent time, often while
// still ringing), and one on a slot whose previous note was modal never
// retired and offered the old body's partials to coupling.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_model_type.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"
#include "dsp/pad_config.h"
#include "dsp/pad_config_apply.h"
#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <vector>

namespace {

constexpr double kRetireSampleRate = 48000.0;
constexpr int    kRetireBlock      = 512;
// -90 dBFS: retirement must not cut audible sound.
constexpr float  kInaudiblePeak    = 3.16e-5f;

struct RetirementResult
{
    bool  retired       = false;
    long  retireSample  = -1;   // end of the block in which the voice retired
    float peakBefore    = 0.0f; // peak over the 100 ms before that block, and the block itself
};

// Renders `totalSamples` in blocks of at most kRetireBlock samples. When
// `noteOffAtSample` >= 0 the voice gets noteOff() once that many samples have
// been rendered (the block boundary is split there).
RetirementResult renderUntilRetired(Membrum::DrumVoice& v, long totalSamples,
                                    long noteOffAtSample)
{
    RetirementResult r;
    std::vector<float> block(static_cast<std::size_t>(kRetireBlock), 0.0f);

    // Rolling history of (block length, block peak) covering >= 100 ms.
    const long windowSamples = static_cast<long>(kRetireSampleRate * 0.1);
    std::deque<std::pair<int, float>> history;
    long historySamples = 0;

    long rendered = 0;
    bool noteOffSent = noteOffAtSample < 0;
    while (rendered < totalSamples)
    {
        if (!noteOffSent && rendered >= noteOffAtSample)
        {
            v.noteOff();
            noteOffSent = true;
        }
        long limit = totalSamples - rendered;
        if (!noteOffSent)
            limit = std::min(limit, noteOffAtSample - rendered);
        const int n = static_cast<int>(std::min<long>(kRetireBlock, limit));

        v.processBlock(block.data(), n);
        float blockPeak = 0.0f;
        for (int i = 0; i < n; ++i)
            blockPeak = std::max(blockPeak, std::fabs(block[static_cast<std::size_t>(i)]));
        rendered += n;

        if (!v.isActive())
        {
            float peak = blockPeak;
            for (const auto& h : history)
                peak = std::max(peak, h.second);
            r.retired      = true;
            r.retireSample = rendered;
            r.peakBefore   = peak;
            return r;
        }

        history.emplace_back(n, blockPeak);
        historySamples += n;
        while (!history.empty() && historySamples - history.front().first >= windowSamples)
        {
            historySamples -= history.front().first;
            history.pop_front();
        }
    }
    return r;
}

void checkFeedbackVoiceRetires(Membrum::BodyModelType body, const char* bodyName)
{
    Membrum::DrumVoice v;
    v.prepare(kRetireSampleRate, 0u);

    Membrum::PadConfig cfg{};
    cfg.exciterType = Membrum::ExciterType::Feedback;
    cfg.bodyModel   = body;
    Membrum::applyPadConfigToVoice(v, cfg);
    v.noteOn(1.0f);

    RetirementResult r;
    const char* mode = "";
    SECTION("no note-off, 20 s")
    {
        mode = "no note-off";
        r = renderUntilRetired(v, static_cast<long>(kRetireSampleRate * 20.0), -1);
    }
    SECTION("note-off after 100 ms, 10 s")
    {
        mode = "note-off at 100 ms";
        r = renderUntilRetired(v, static_cast<long>(kRetireSampleRate * 10.0),
                               static_cast<long>(kRetireSampleRate * 0.1));
    }

    const double retireSeconds =
        r.retired ? static_cast<double>(r.retireSample) / kRetireSampleRate : -1.0;
    INFO("body " << bodyName << ", " << mode << ": retired = " << r.retired
                 << ", retirement time = " << retireSeconds << " s"
                 << ", active modes = "
                 << v.getBodyBankForTest().getSharedBank().getNumActiveModes()
                 << ", peak over the 100 ms before retirement = " << r.peakBefore);
    CHECK(r.retired);
    CHECK_FALSE(v.isActive());
    CHECK(r.peakBefore < kInaudiblePeak);
}

} // namespace

TEST_CASE("Retirement: a Feedback-exciter voice retires once its body is silent",
          "[drum_voice][retirement]")
{
    SECTION("Membrane") { checkFeedbackVoiceRetires(Membrum::BodyModelType::Membrane, "Membrane"); }
    SECTION("Plate") { checkFeedbackVoiceRetires(Membrum::BodyModelType::Plate, "Plate"); }
    SECTION("Shell") { checkFeedbackVoiceRetires(Membrum::BodyModelType::Shell, "Shell"); }
    SECTION("Bell") { checkFeedbackVoiceRetires(Membrum::BodyModelType::Bell, "Bell"); }
    SECTION("NoiseBody") { checkFeedbackVoiceRetires(Membrum::BodyModelType::NoiseBody, "NoiseBody"); }
}

TEST_CASE("Retirement: sixteen Feedback hits leave no active voice in the pool",
          "[voice_pool][retirement]")
{
    Membrum::VoicePool pool;
    pool.prepare(kRetireSampleRate, kRetireBlock);
    pool.setMaxPolyphony(16);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Feedback);

    std::vector<float> outL(static_cast<std::size_t>(kRetireBlock), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kRetireBlock), 0.0f);

    for (int note = 36; note <= 51; ++note)
    {
        pool.noteOn(static_cast<std::uint8_t>(note), 1.0f);
        pool.processBlock(outL.data(), outR.data(), kRetireBlock);
    }
    REQUIRE(pool.getActiveVoiceCount() == 16);

    const long blocks = static_cast<long>(kRetireSampleRate * 20.0) / kRetireBlock;
    long lastActiveBlock = -1;
    for (long b = 0; b < blocks; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kRetireBlock);
        if (pool.isAnyVoiceActive())
            lastActiveBlock = b;
    }

    INFO("last block with an active voice: " << lastActiveBlock << " ("
         << static_cast<double>((lastActiveBlock + 1) * kRetireBlock) / kRetireSampleRate
         << " s after the last hit); active voices at the end = "
         << pool.getActiveVoiceCount());
    CHECK(pool.getActiveVoiceCount() == 0);
    CHECK_FALSE(pool.isAnyVoiceActive());
}

// ==============================================================================
// T1.8: String retirement
// ==============================================================================

namespace {

Membrum::PadConfig stringConfig(float decay)
{
    Membrum::PadConfig cfg{};
    cfg.bodyModel   = Membrum::BodyModelType::String;
    cfg.exciterType = Membrum::ExciterType::Impulse;
    cfg.decay       = decay;
    return cfg;
}

struct StringRender
{
    bool  retired          = false;
    long  retireSample     = -1;   // end of the block after which isActive() is false
    long  minus60Sample    = 0;    // one past the last sample above 1e-3 x the render's peak
    float peakBeforeRetire = 0.0f; // peak over the 100 ms before retirement
};

// A fresh String voice, no note-off, 15 s in blocks of `blockSize` samples.
StringRender renderFreshStringVoice(int blockSize)
{
    Membrum::DrumVoice v;
    v.prepare(kRetireSampleRate, 0u);
    Membrum::applyPadConfigToVoice(v, stringConfig(1.0f));
    v.noteOn(1.0f);

    const long total = static_cast<long>(kRetireSampleRate * 15.0);
    std::vector<float> out(static_cast<std::size_t>(total), 0.0f);
    StringRender r;
    long rendered = 0;
    while (rendered < total)
    {
        const int n = static_cast<int>(std::min<long>(blockSize, total - rendered));
        v.processBlock(out.data() + rendered, n);
        rendered += n;
        if (!r.retired && !v.isActive())
        {
            r.retired      = true;
            r.retireSample = rendered;
        }
    }

    float peak = 0.0f;
    for (float s : out)
        peak = std::max(peak, std::fabs(s));
    const float minus60 = peak * 1.0e-3f;
    for (long i = total - 1; i >= 0; --i)
    {
        if (std::fabs(out[static_cast<std::size_t>(i)]) > minus60)
        {
            r.minus60Sample = i + 1;
            break;
        }
    }
    if (r.retired)
    {
        const long window = static_cast<long>(kRetireSampleRate * 0.1);
        for (long i = std::max(0L, r.retireSample - window); i < r.retireSample; ++i)
            r.peakBeforeRetire = std::max(r.peakBeforeRetire,
                                          std::fabs(out[static_cast<std::size_t>(i)]));
    }
    return r;
}

// Membrane note for 512 samples on `v`, optionally a kit switch while it rings,
// then the String config with decay 0.3 and a String note on the same voice.
void membraneThenStringNote(Membrum::DrumVoice& v, bool kitSwitchBetween)
{
    v.prepare(kRetireSampleRate, 0u);
    Membrum::applyPadConfigToVoice(v, Membrum::PadConfig{});
    v.noteOn(1.0f);
    std::vector<float> block(static_cast<std::size_t>(kRetireBlock), 0.0f);
    v.processBlock(block.data(), kRetireBlock);
    REQUIRE(v.getBodyBankForTest().getSharedBank().getNumActiveModes() > 0);

    if (kitSwitchBetween)
        v.resetForKitSwitch();

    Membrum::applyPadConfigToVoice(v, stringConfig(0.3f));
    v.noteOn(1.0f);
}

void checkStringAfterMembraneRetires(bool kitSwitchBetween)
{
    Membrum::DrumVoice v;
    membraneThenStringNote(v, kitSwitchBetween);

    std::vector<float> block(static_cast<std::size_t>(kRetireBlock), 0.0f);
    const long blocks = static_cast<long>(kRetireSampleRate * 20.0) / kRetireBlock;
    long retiredAtBlock = -1;
    for (long b = 0; b < blocks; ++b)
    {
        v.processBlock(block.data(), kRetireBlock);
        if (retiredAtBlock < 0 && !v.isActive())
            retiredAtBlock = b;
    }

    const int modes = v.getBodyBankForTest().getSharedBank().getNumActiveModes();
    INFO("retired after block " << retiredAtBlock << " ("
         << (retiredAtBlock < 0 ? -1.0
                                : static_cast<double>((retiredAtBlock + 1) * kRetireBlock)
                                      / kRetireSampleRate)
         << " s); bank active modes = " << modes);
    CHECK_FALSE(v.isActive());
    CHECK(modes == 0);
}

} // namespace

TEST_CASE("Retirement: a String voice is not cut before its own decay, at any block size",
          "[drum_voice][retirement]")
{
    const StringRender r64   = renderFreshStringVoice(64);
    const StringRender r1024 = renderFreshStringVoice(1024);

    INFO("block 64: retired = " << r64.retired << " at "
         << static_cast<double>(r64.retireSample) / kRetireSampleRate << " s, -60 dB at "
         << static_cast<double>(r64.minus60Sample) / kRetireSampleRate
         << " s, peak over the 100 ms before retirement = " << r64.peakBeforeRetire);
    INFO("block 1024: retired = " << r1024.retired << " at "
         << static_cast<double>(r1024.retireSample) / kRetireSampleRate << " s, -60 dB at "
         << static_cast<double>(r1024.minus60Sample) / kRetireSampleRate
         << " s, peak over the 100 ms before retirement = " << r1024.peakBeforeRetire);
    REQUIRE(r64.retired);
    REQUIRE(r1024.retired);
    CHECK(r64.retireSample >= r64.minus60Sample);
    CHECK(r1024.retireSample >= r1024.minus60Sample);
    // Retirement is checked once per block: agreement within one 1024-sample block.
    CHECK(std::abs(r64.retireSample - r1024.retireSample) <= 1024L);
    CHECK(r64.peakBeforeRetire < kInaudiblePeak);
    CHECK(r1024.peakBeforeRetire < kInaudiblePeak);
}

TEST_CASE("Retirement: a String note after a Membrane note on the same voice retires",
          "[drum_voice][retirement]")
{
    checkStringAfterMembraneRetires(false);
}

TEST_CASE("Retirement: a String note after a kit switch retires",
          "[drum_voice][retirement]")
{
    checkStringAfterMembraneRetires(true);
}

TEST_CASE("Retirement: a String voice offers no stale modal partials to coupling",
          "[drum_voice][retirement]")
{
    Membrum::DrumVoice v;
    membraneThenStringNote(v, false);

    // T2.17 (MB-068): the String voice offers its own harmonics instead, the
    // waveguide f0 800 x 0.1^0.5 = 252.98 Hz at the default Size (the stale
    // Membrane modes start at 500 x 0.1^0.5 = 158.1 Hz).
    const float f0 = 800.0f * std::pow(0.1f, 0.5f);
    const auto info = v.getPartialInfo();
    for (std::size_t k = 0; k < info.frequencies.size(); ++k)
    {
        const float expected = f0 * static_cast<float>(k + 1);
        INFO("partial " << k << " = " << info.frequencies[k] << " Hz, expected "
                        << expected << " Hz");
        CHECK(std::fabs(info.frequencies[k] - expected) <= 0.01f * expected);
    }
}
