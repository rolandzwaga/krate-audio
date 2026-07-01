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

#include "public.sdk/source/vst/vstpresetfile.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
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

struct SnareMetrics
{
    double peak;
    double bodyVsHashDb;  // body(150-450 Hz) vs hash(2-7 kHz), power ratio in dB
    double centroid;      // onset spectral centroid (Hz)
};

// Shared body-vs-hash + centroid analysis over an onset render.
SnareMetrics analyzeSnare(const std::vector<float>& s)
{
    const double bodyPow  = bandPower(s, kSampleRate, 150.0, 450.0, 5.0);
    const double hashPow  = bandPower(s, kSampleRate, 2000.0, 7000.0, 25.0);
    const double centroid = spectralCentroid(s, kSampleRate, 100.0, 12000.0, 25.0);
    return { peakAbs(s), toDb(bodyPow) - toDb(hashPow), centroid };
}

// Load the component-state chunk of a .vstpreset into the processor
// (mirrors the snare diagnostic's loader). Returns false if the file is absent.
bool loadPresetComponentState(Membrum::Processor& proc, const std::string& path)
{
    auto* stream = Steinberg::Vst::FileStream::open(path.c_str(), "rb");
    if (!stream) return false;
    bool ok = false;
    {
        Steinberg::Vst::PresetFile pf(stream);
        if (pf.readChunkList() && pf.seekToComponentState()) {
            const auto* entry = pf.getEntry(Steinberg::Vst::kComponentState);
            if (entry) {
                auto comp = Steinberg::owned(new Steinberg::Vst::ReadOnlyBStream(
                    stream, entry->offset, entry->size));
                comp->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                ok = (proc.setState(comp) == Steinberg::kResultOk);
            }
        }
    }
    stream->release();
    return ok;
}

} // namespace

TEST_CASE("Snare has audible body, not just hi-hat noise",
          "[membrum][processor][snare][balance]")
{
    Fixture fix;

    const int16 kSnareMidi = 38;  // pad 2, DefaultKit Snare template
    const int kOnset = 4096;      // ~85 ms onset window at 48 kHz

    const auto snare = fix.render(kSnareMidi, kOnset);
    const SnareMetrics m = analyzeSnare(snare);

    REQUIRE(m.peak > 1e-4);  // it actually made sound

    INFO("body - hash = " << m.bodyVsHashDb << " dB (>= 0 dB required)");
    INFO("onset spectral centroid = " << m.centroid << " Hz (< 1500 Hz required)");

    // (1) The pitched body must at least equal the mid/high hash. A snare whose
    //     body sits below the 2-7 kHz hump (NoiseBurst + click + bright wires)
    //     reads as a hi-hat; a real snare is body-dominant at the onset.
    CHECK(m.bodyVsHashDb >= 0.0);

    // (2) The onset must be body-weighted, not noise-weighted: centroid below
    //     the ~1.5 kHz body/noise crossover of a real snare.
    CHECK(m.centroid < 1500.0);
}

TEST_CASE("Factory Acoustic Studio Kit snare has audible body, not hi-hat",
          "[membrum][processor][snare][balance][preset]")
{
    // The factory kits are generated independently of default_kit.h
    // (tools/membrum_preset_generator.cpp). This guards the SHIPPED preset path:
    // the generator's Acoustic snare was the exact noise-dominant recipe the
    // investigation flagged (and that the [.snare-diag] test loads to reproduce
    // the bug). Renders the regenerated preset's snare and applies the same
    // body-dominance gates so a regression in the generator can't re-ship a
    // hi-hat snare.
    const std::string presetPath =
        std::string(MEMBRUM_RESOURCES_DIR) +
        "/presets/Kit Presets/Acoustic/Acoustic Studio Kit.vstpreset";

    Fixture fix;
    REQUIRE(loadPresetComponentState(fix.processor, presetPath));

    const int16 kSnareMidi = 38;
    const int kOnset = 4096;  // ~85 ms

    const auto snare = fix.render(kSnareMidi, kOnset);
    const SnareMetrics m = analyzeSnare(snare);

    REQUIRE(m.peak > 1e-4);

    INFO("factory body - hash = " << m.bodyVsHashDb << " dB (>= 0 dB required)");
    INFO("factory onset centroid = " << m.centroid << " Hz (< 1500 Hz required)");

    CHECK(m.bodyVsHashDb >= 0.0);
    CHECK(m.centroid < 1500.0);
}
