// ==============================================================================
// Tone-shaper filter and voice-slot history (MB-042, MB-265; fix plan T1.15)
// ==============================================================================
// The tone-shaper SVF smooths g, k and its mode mix with a one-pole on
// g = tan(pi fc / fs). Before the fix, ToneShaper::noteOn cleared only the
// integrators, so every hit started from the coefficients the slot's previous
// note left (a 20 kHz lowpass on a never-used slot) and took 20-55 ms to close.
// A pad then sounded different on a fresh slot, on a slot it warmed itself and
// on a slot another pad (another filter type) used last.
//
// Setup follows specs/membrum-audit/probes/audit_filter_glide_probe.cpp.txt,
// scenario A: 48 kHz, block 256, 4 voices, Priority stealing (so a later hit
// re-uses the first hit's slot), note-offs 100 ms after each hit, hits 1 s
// apart. The hat uses Oldest stealing instead, to hit 4 fresh slots and then
// the same 4 warm (see checkHatFreshAgainstWarm).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "processor/processor.h"

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include <krate/dsp/primitives/fft.h>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSr   = 48000.0;
constexpr int    kBs   = 256;
constexpr int    kSec  = 48000;
constexpr int    kOff  = 4800;  // note-off 100 ms after each note-on
constexpr float  kVel  = 100.0f / 127.0f;

struct SlotHistoryRig
{
    Membrum::Processor           processor;
    Krate::Test::EventList       events;
    Krate::Test::ParameterChanges params;
    std::vector<float>           outL;
    std::vector<float>           outR;
    std::array<float*, 2>        chans{};
    AudioBusBuffers              bus{};
    ProcessData                  data{};

    SlotHistoryRig()
        : outL(static_cast<std::size_t>(kBs), 0.0f)
        , outR(static_cast<std::size_t>(kBs), 0.0f)
    {
        chans[0]            = outL.data();
        chans[1]            = outR.data();
        bus.numChannels     = 2;
        bus.channelBuffers32 = chans.data();
        bus.silenceFlags    = 0;

        data.processMode            = kRealtime;
        data.symbolicSampleSize     = kSample32;
        data.numSamples             = kBs;
        data.numOutputs             = 1;
        data.outputs                = &bus;
        data.numInputs              = 0;
        data.inputs                 = nullptr;
        data.inputParameterChanges  = &params;
        data.outputParameterChanges = nullptr;
        data.inputEvents            = &events;
        data.outputEvents           = nullptr;
        data.processContext         = nullptr;

        processor.initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode        = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = kBs;
        setup.sampleRate         = kSr;
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~SlotHistoryRig()
    {
        processor.setActive(false);
        processor.terminate();
    }

    SlotHistoryRig(const SlotHistoryRig&)            = delete;
    SlotHistoryRig& operator=(const SlotHistoryRig&) = delete;
};

struct Ev
{
    int      at    = 0;
    int      kind  = 0;  // 0 note-on, 1 note-off, 2 parameter
    int      pitch = 0;
    ParamID  id    = 0;
    double   value = 0.0;
};

Ev noteOnAt(int at, int pitch) { Ev e; e.at = at; e.kind = 0; e.pitch = pitch; return e; }
Ev noteOffAt(int at, int pitch) { Ev e; e.at = at; e.kind = 1; e.pitch = pitch; return e; }
Ev paramAt(int at, int id, double v)
{
    Ev e; e.at = at; e.kind = 2; e.id = static_cast<ParamID>(id); e.value = v; return e;
}

struct Render
{
    std::vector<float> mono;
    std::vector<int>   hitSlot;  // the main slot each note-on landed on, in order
};

Render render(SlotHistoryRig& rig, const std::vector<Ev>& evs, int numSamples)
{
    Render r;
    r.mono.assign(static_cast<std::size_t>(numSamples), 0.0f);
    auto&       pool = rig.processor.voicePoolForTest();
    std::size_t next = 0;
    for (int b = 0; b < numSamples; b += kBs)
    {
        const int ns = std::min(kBs, numSamples - b);
        rig.events.clear();
        rig.params.clear();
        std::vector<int> onPitches;
        while (next < evs.size() && evs[next].at < b + ns)
        {
            const Ev& e   = evs[next];
            const int off = std::max(0, e.at - b);
            if (e.kind == 0)
            {
                rig.events.addNoteOn(static_cast<int16>(e.pitch), kVel, off);
                onPitches.push_back(e.pitch);
            }
            else if (e.kind == 1)
            {
                rig.events.addNoteOff(static_cast<int16>(e.pitch), off);
            }
            else
            {
                rig.params.addChange(e.id, e.value);
            }
            ++next;
        }
        // A hit is new on a slot when its timestamp moved or the slot left
        // another state (the first hit of a render is stamped at sample 0).
        std::array<std::uint64_t, Membrum::kMaxVoices> prevCount{};
        std::array<Membrum::VoiceSlotState, Membrum::kMaxVoices> prevState{};
        for (int s = 0; s < Membrum::kMaxVoices; ++s)
        {
            prevCount[static_cast<std::size_t>(s)] = pool.voiceMeta(s).noteOnSampleCount;
            prevState[static_cast<std::size_t>(s)] = pool.voiceMeta(s).state;
        }

        std::fill(rig.outL.begin(), rig.outL.end(), 0.0f);
        std::fill(rig.outR.begin(), rig.outR.end(), 0.0f);
        rig.data.numSamples = ns;
        rig.processor.process(rig.data);

        for (const int p : onPitches)
        {
            int slot = -1;
            for (int s = 0; s < Membrum::kMaxVoices; ++s)
            {
                const auto& m = pool.voiceMeta(s);
                if (m.state == Membrum::VoiceSlotState::Active
                    && m.originatingNote == p
                    && (m.noteOnSampleCount != prevCount[static_cast<std::size_t>(s)]
                        || m.state != prevState[static_cast<std::size_t>(s)]))
                    slot = s;
            }
            r.hitSlot.push_back(slot);
        }
        for (int s = 0; s < ns; ++s)
            r.mono[static_cast<std::size_t>(b + s)] =
                0.5f * (rig.outL[static_cast<std::size_t>(s)]
                        + rig.outR[static_cast<std::size_t>(s)]);
    }
    return r;
}

double powerDb(double p) { return p > 1e-30 ? 10.0 * std::log10(p) : -300.0; }

// Mean-square level in dB over x[start, start + len).
double rmsDb(const std::vector<float>& x, int start, int len)
{
    double s = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const double v = x[static_cast<std::size_t>(start + i)];
        s += v * v;
    }
    return powerDb(s / static_cast<double>(len));
}

double peakDb(const std::vector<float>& x, int start, int len)
{
    float pk = 0.0f;
    for (int i = 0; i < len; ++i)
        pk = std::max(pk, std::abs(x[static_cast<std::size_t>(start + i)]));
    return 20.0 * std::log10(std::max(static_cast<double>(pk), 1e-30));
}

// Linear power at or above 2 kHz in x[start, start + len), Hann window,
// zero-padded to 32768 (the probe's bandPowers,
// audit_filter_glide_probe.cpp.txt:202-233).
double hiBandPower(const std::vector<float>& x, int start, int len)
{
    constexpr int kFftN = 32768;
    Krate::DSP::FFT fft;
    fft.prepare(static_cast<std::size_t>(kFftN));
    std::vector<float> frame(static_cast<std::size_t>(kFftN), 0.0f);
    for (int i = 0; i < len; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979f
                                               * (static_cast<float>(i) + 0.5f)
                                               / static_cast<float>(len));
        frame[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(start + i)] * w;
    }
    std::vector<Krate::DSP::Complex> spec(static_cast<std::size_t>(kFftN / 2 + 1));
    fft.forward(frame.data(), spec.data());
    double ph = 0.0;
    for (int k = 1; k <= kFftN / 2; ++k)
    {
        const double f = static_cast<double>(k) * kSr / kFftN;
        if (f < 2000.0)
            continue;
        const double re = spec[static_cast<std::size_t>(k)].real;
        const double im = spec[static_cast<std::size_t>(k)].imag;
        ph += re * re + im * im;
    }
    return ph;
}

double hiBandDb(const std::vector<float>& x, int start, int len)
{
    return powerDb(hiBandPower(x, start, len));
}

// Default-kit hat (note 42), LP 317 Hz. Its NoiseBurst exciter draws new noise
// on every hit (T2.23 keeps that by design), so one hit's first 50 ms above
// 2 kHz scatters by a couple of dB from hit to hit even on identical slots.
// Ruling (user, 2026-11-24): compare the power mean of 4 hits on 4 fresh slots
// (Oldest stealing, 4 voices) against the same 4 slots warm; bars unchanged.
void checkHatFreshAgainstWarm()
{
    constexpr int kNote = 42;
    constexpr int kHits = 4;
    const int     pad   = kNote - 36;
    SlotHistoryRig rig;
    std::vector<Ev> evs;
    evs.push_back(paramAt(0, Membrum::kMaxPolyphonyId, 0.0));   // 4 voices
    evs.push_back(paramAt(0, Membrum::kVoiceStealingId, 0.0));  // Oldest
    evs.push_back(paramAt(0, Membrum::padParamId(pad, Membrum::kPadTSFilterType), 0.0));
    evs.push_back(paramAt(0, Membrum::padParamId(pad, Membrum::kPadTSFilterCutoff), 0.4));
    evs.push_back(paramAt(0, Membrum::padParamId(pad, Membrum::kPadTSFilterEnvAmount), 0.5));
    for (int h = 0; h < 2 * kHits; ++h)
    {
        evs.push_back(noteOnAt(h * kSec, kNote));
        evs.push_back(noteOffAt(h * kSec + kOff, kNote));
    }
    const Render r = render(rig, evs, 2 * kHits * kSec);

    REQUIRE(r.hitSlot.size() == static_cast<std::size_t>(2 * kHits));
    for (int h = 0; h < kHits; ++h)
    {
        const auto fresh = static_cast<std::size_t>(h);
        const auto warm  = static_cast<std::size_t>(h + kHits);
        INFO("hit " << h << ": fresh slot " << r.hitSlot[fresh] << ", warm slot "
                    << r.hitSlot[warm]);
        REQUIRE(r.hitSlot[fresh] >= 0);
        for (int g = 0; g < h; ++g)
            REQUIRE(r.hitSlot[static_cast<std::size_t>(g)] != r.hitSlot[fresh]);
        REQUIRE(r.hitSlot[warm] == r.hitSlot[fresh]);
    }

    const int n500 = kSec / 2;
    const int n50  = kSec / 20;

    // rms500: the plan's single pair, the first fresh slot against itself warm.
    // (The other slots' single hits scatter by up to ~2.5 dB among the FRESH
    // hits alone, which share one history: per-hit noise, not slot history.)
    const double rmsFresh = rmsDb(r.mono, 0, n500);
    const double rmsWarm  = rmsDb(r.mono, kHits * kSec, n500);
    INFO("slot " << r.hitSlot[0] << ": rms500 fresh " << rmsFresh << " / warm " << rmsWarm
                 << " dBFS");
    CHECK(std::abs(rmsFresh - rmsWarm) <= 1.0);

    double hiFresh = 0.0;
    double hiWarm  = 0.0;
    for (int h = 0; h < kHits; ++h)
    {
        hiFresh += hiBandPower(r.mono, h * kSec, n50);
        hiWarm += hiBandPower(r.mono, (h + kHits) * kSec, n50);
    }
    const double hiFreshDb = powerDb(hiFresh / kHits);
    const double hiWarmDb  = powerDb(hiWarm / kHits);
    INFO("note 42: hi50 power mean of " << kHits << " hits, fresh " << hiFreshDb
                                        << " / warm " << hiWarmDb << " dB");
    CHECK(std::abs(hiFreshDb - hiWarmDb) <= 2.0);
}

// Hit 1 on a fresh slot, hit 2 one second later on the same (warmed) slot.
void checkFreshAgainstWarm(int note)
{
    const int pad = note - 36;
    SlotHistoryRig rig;
    std::vector<Ev> evs;
    evs.push_back(paramAt(0, Membrum::kMaxPolyphonyId, 0.0));   // 4 voices
    evs.push_back(paramAt(0, Membrum::kVoiceStealingId, 1.0));  // Priority
    evs.push_back(paramAt(0, Membrum::padParamId(pad, Membrum::kPadTSFilterType), 0.0));
    evs.push_back(paramAt(0, Membrum::padParamId(pad, Membrum::kPadTSFilterCutoff), 0.4));
    evs.push_back(paramAt(0, Membrum::padParamId(pad, Membrum::kPadTSFilterEnvAmount), 0.5));
    evs.push_back(noteOnAt(0, note));
    evs.push_back(noteOffAt(kOff, note));
    evs.push_back(noteOnAt(kSec, note));
    evs.push_back(noteOffAt(kSec + kOff, note));
    const Render r = render(rig, evs, 2 * kSec);

    REQUIRE(r.hitSlot.size() == 2);
    INFO("slots " << r.hitSlot[0] << " / " << r.hitSlot[1]);
    REQUIRE(r.hitSlot[0] >= 0);
    REQUIRE(r.hitSlot[1] == r.hitSlot[0]);

    const int n500 = kSec / 2;
    const int n50  = kSec / 20;
    const double rms1 = rmsDb(r.mono, 0, n500);
    const double rms2 = rmsDb(r.mono, kSec, n500);
    const double hi1  = hiBandDb(r.mono, 0, n50);
    const double hi2  = hiBandDb(r.mono, kSec, n50);
    INFO("note " << note << ": rms500 fresh " << rms1 << " / warm " << rms2
                 << " dBFS; hi50 fresh " << hi1 << " / warm " << hi2 << " dB");
    CHECK(std::abs(rms1 - rms2) <= 1.0);
    CHECK(std::abs(hi1 - hi2) <= 2.0);
}

Membrum::PadConfig lp300ClickPad(float filterType, float cutoffNorm)
{
    Membrum::PadConfig c;
    c.bodyModel          = Membrum::BodyModelType::Membrane;
    c.exciterType        = Membrum::ExciterType::Impulse;
    c.size               = 0.5f;
    c.noiseLayerMix      = 0.0f;
    c.clickLayerMix      = 0.5f;  // click on
    c.tsFilterType       = filterType;
    c.tsFilterCutoff     = cutoffNorm;
    c.tsFilterEnvAmount  = 0.5f;  // amount 0
    return c;
}

// Normalised cutoff for hz under the pad law 20 * 1000^n (pad_config_apply.h).
float cutoffNormFor(double hz)
{
    return static_cast<float>(std::log(hz / 20.0) / std::log(1000.0));
}

} // namespace

TEST_CASE("Tone-shaper filter sounds the same on a fresh and a warm voice slot",
          "[processor][tone_shaper][filter]")
{
    SECTION("pad 42 (default-kit hat), LP 317 Hz, 4 fresh slots against the same 4 warm")
    {
        checkHatFreshAgainstWarm();
    }

    SECTION("pad 38 (default-kit snare), LP 317 Hz")
    {
        checkFreshAgainstWarm(38);
    }

    SECTION("after another pad")
    {
        // MB-042: an LP 300 Hz pad with the click layer on, hit on a fresh
        // slot, hit again, then hit on the slot a highpass pad used last.
        SlotHistoryRig rig;
        auto& pool = rig.processor.voicePoolForTest();
        pool.padConfigMut(0) = lp300ClickPad(0.0f, cutoffNormFor(300.0));    // note 36, LP
        pool.padConfigMut(1) = lp300ClickPad(0.5f, cutoffNormFor(14000.0));  // note 37, HP

        std::vector<Ev> evs;
        evs.push_back(paramAt(0, Membrum::kMaxPolyphonyId, 0.0));
        evs.push_back(paramAt(0, Membrum::kVoiceStealingId, 1.0));
        evs.push_back(noteOnAt(0, 36));
        evs.push_back(noteOffAt(kOff, 36));
        evs.push_back(noteOnAt(kSec, 36));
        evs.push_back(noteOffAt(kSec + kOff, 36));
        evs.push_back(noteOnAt(2 * kSec, 37));
        evs.push_back(noteOffAt(2 * kSec + kOff, 37));
        evs.push_back(noteOnAt(3 * kSec, 36));
        evs.push_back(noteOffAt(3 * kSec + kOff, 36));
        const Render r = render(rig, evs, 4 * kSec);

        REQUIRE(r.hitSlot.size() == 4);
        INFO("slots " << r.hitSlot[0] << " / " << r.hitSlot[1] << " / "
                      << r.hitSlot[2] << " / " << r.hitSlot[3]);
        REQUIRE(r.hitSlot[0] >= 0);
        REQUIRE(r.hitSlot[1] == r.hitSlot[0]);
        REQUIRE(r.hitSlot[2] == r.hitSlot[0]);
        REQUIRE(r.hitSlot[3] == r.hitSlot[0]);

        constexpr int n5 = 240;  // 5 ms
        const double pkFresh  = peakDb(r.mono, 0, n5);
        const double pkRepeat = peakDb(r.mono, kSec, n5);
        const double pkAfter  = peakDb(r.mono, 3 * kSec, n5);
        const double rmFresh  = rmsDb(r.mono, 0, n5);
        const double rmRepeat = rmsDb(r.mono, kSec, n5);
        const double rmAfter  = rmsDb(r.mono, 3 * kSec, n5);
        INFO("0-5 ms peak fresh / repeat / after HP: " << pkFresh << " / " << pkRepeat
             << " / " << pkAfter << " dBFS");
        INFO("0-5 ms RMS  fresh / repeat / after HP: " << rmFresh << " / " << rmRepeat
             << " / " << rmAfter << " dBFS");
        CHECK(std::abs(pkRepeat - pkFresh) <= 0.5);
        CHECK(std::abs(pkAfter - pkFresh) <= 0.5);
        CHECK(std::abs(rmRepeat - rmFresh) <= 0.5);
        CHECK(std::abs(rmAfter - rmFresh) <= 0.5);
    }
}
