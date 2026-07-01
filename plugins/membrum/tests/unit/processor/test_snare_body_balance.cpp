// ============================================================================
// Snare body-vs-noise balance regression (spec: INVESTIGATION-snare-body)
// ============================================================================
// Guards the "snare sounds like a thin hi-hat" regression. Renders the SHIPPED
// default-kit snare (pad 2, MIDI 38 -- DefaultKit::apply runs in
// Processor::initialize, FR-030) through the full processor and measures the
// onset spectrum. A convincing snare has a definite pitched BODY (150-450 Hz)
// that is NOT buried under the mid/high broadband "hash" (2-7 kHz: NoiseBurst
// bandpass, click, bright wires); when the body sinks below that hash the sound
// reads as a hi-hat.
//
// The four snare fixes (body retune, recipe rebalance, cutoff-tracking noise
// gain, real-excitation strike normalisation) must keep:
//   (1) body-band (150-450 Hz) energy at least equal to the 2-7 kHz hash, and
//   (2) the onset spectral centroid below the ~1.5 kHz body/noise crossover.
//
// Deterministic: fixed PRNG seeds (voiceId-derived) + fixed default kit, so the
// measured values are stable across runs. dB tolerances (not exact FP compares)
// keep it cross-platform per CLAUDE.md.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "dsp/pad_config.h"

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

constexpr int    kBlockSize  = 256;
constexpr double kSampleRate = 48000.0;

class NoteEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size())) return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events_.push_back(e); return kResultOk; }
    void noteOn(int16 midi, float velocity)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = 0;
        e.noteOn.pitch = midi;
        e.noteOn.velocity = velocity;
        e.noteOn.channel = 0;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }
private:
    std::vector<Event> events_;
};

ProcessSetup makeSetup(double sr, int bs)
{
    ProcessSetup s{};
    s.processMode = kRealtime;
    s.symbolicSampleSize = kSample32;
    s.maxSamplesPerBlock = bs;
    s.sampleRate = sr;
    return s;
}

struct Fixture
{
    Membrum::Processor processor;
    NoteEventList events;
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};
    float* outChans[2];
    AudioBusBuffers outBus{};
    ProcessData data{};

    Fixture()
    {
        outChans[0] = outL.data();
        outChans[1] = outR.data();
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outChans;
        outBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &events;
        data.outputEvents = nullptr;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        auto setup = makeSetup(kSampleRate, kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~Fixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void clearBuffers()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
    }

    // Mono (L+R)/2 render of one pad over `numSamples`.
    std::vector<float> render(int16 midi, int numSamples)
    {
        events.clear();
        events.noteOn(midi, 0.8f);
        std::vector<float> mono;
        mono.reserve(static_cast<size_t>(numSamples));
        const int blocks = (numSamples + kBlockSize - 1) / kBlockSize;
        for (int b = 0; b < blocks; ++b)
        {
            clearBuffers();
            processor.process(data);
            events.clear();  // note-on only on the first block
            for (int s = 0; s < kBlockSize && static_cast<int>(mono.size()) < numSamples; ++s)
                mono.push_back(0.5f * (outL[s] + outR[s]));
        }
        return mono;
    }
};

double peakAbs(const std::vector<float>& s)
{
    double p = 0.0;
    for (float x : s) p = std::max(p, std::abs(static_cast<double>(x)));
    return p;
}

constexpr double kPi = 3.14159265358979323846;

// Single-bin Goertzel power (|X(f)|^2) over the whole buffer.
double goertzelPower(const std::vector<float>& x, double sr, double freq)
{
    const double w = 2.0 * kPi * freq / sr;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (float sample : x)
    {
        const double s0 = static_cast<double>(sample) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Summed power across a frequency band [lo, hi] sampled at `step` Hz.
double bandPower(const std::vector<float>& x, double sr, double lo, double hi, double step)
{
    double e = 0.0;
    for (double f = lo; f <= hi; f += step)
        e += goertzelPower(x, sr, f);
    return e;
}

// Power-weighted spectral centroid (Hz) across [lo, hi].
double spectralCentroid(const std::vector<float>& x, double sr, double lo, double hi, double step)
{
    double num = 0.0, den = 0.0;
    for (double f = lo; f <= hi; f += step)
    {
        const double p = goertzelPower(x, sr, f);
        num += f * p;
        den += p;
    }
    return den > 0.0 ? num / den : 0.0;
}

double toDb(double powerRatio) { return 10.0 * std::log10(std::max(powerRatio, 1e-30)); }

} // namespace

TEST_CASE("Snare has audible body, not just hi-hat noise",
          "[membrum][processor][snare][balance]")
{
    Fixture fix;

    const int16 kSnareMidi = 38;  // pad 2, DefaultKit Snare template
    const int kOnset = 4096;      // ~85 ms onset window at 48 kHz

    const auto snare = fix.render(kSnareMidi, kOnset);

    REQUIRE(peakAbs(snare) > 1e-4);  // it actually made sound

    // Body: pitched membrane region (fundamental + low overtones).
    const double bodyPow  = bandPower(snare, kSampleRate, 150.0, 450.0, 5.0);
    // "Hi-hat hash": the mid/high broadband hump that dominates the broken
    // snare -- the NoiseBurst bandpass (~5.8 kHz at v0.8), the click (~3.5 kHz),
    // and the bright wire lowpass all pile up here. This is the band that must
    // NOT out-mass the body if the sound is to read as a snare, not a hi-hat.
    const double hashPow  = bandPower(snare, kSampleRate, 2000.0, 7000.0, 25.0);
    // Onset centroid over the full audible-ish band.
    const double centroid = spectralCentroid(snare, kSampleRate, 100.0, 12000.0, 25.0);

    const double bodyVsHashDb = toDb(bodyPow) - toDb(hashPow);

    INFO("body(150-450Hz) power=" << bodyPow);
    INFO("hash(2-7kHz) power=" << hashPow);
    INFO("body - hash = " << bodyVsHashDb << " dB (>= 0 dB required)");
    INFO("onset spectral centroid = " << centroid << " Hz (< 1500 Hz required)");

    // (1) The pitched body must at least equal the mid/high hash. A snare whose
    //     body sits below the 2-7 kHz hump (NoiseBurst + click + bright wires)
    //     reads as a hi-hat; a real snare is body-dominant at the onset.
    CHECK(bodyVsHashDb >= 0.0);

    // (2) The onset must be body-weighted, not noise-weighted: centroid below
    //     the ~1.5 kHz body/noise crossover of a real snare.
    CHECK(centroid < 1500.0);
}
