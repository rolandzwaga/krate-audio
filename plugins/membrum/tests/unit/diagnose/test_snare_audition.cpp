// ============================================================================
// Snare wire-balance audition (one-off, tagged [.snare-audition], hidden)
// ============================================================================
// The body fix landed but killed the snare-wire sizzle (user: "hollow piece of
// wood"). A real snare = struck body + BRIGHT broadband wire buzz on top. This
// renders the default-kit snare (pad 2, MIDI 38) with several wire/damping
// balances so we can A/B and pick the one that reads as a snare, then bake the
// winner into default_kit.h + the preset generator.
//
// Run:  membrum_tests.exe "[.snare-audition]"
// WAVs land in F:/tmp/  (snare_cand_*.wav)
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "dsp/pad_config.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "vst_param_changes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int    kBlockSize  = 256;
constexpr double kSampleRate = 48000.0;
constexpr int    kTailBlocks = 244;  // ~1.3 s

using MultiParamChanges = Krate::Test::ParameterChanges;

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

ProcessSetup makeSetup()
{
    ProcessSetup s{};
    s.processMode = kRealtime;
    s.symbolicSampleSize = kSample32;
    s.maxSamplesPerBlock = kBlockSize;
    s.sampleRate = kSampleRate;
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
        data.inputEvents = &events;
        processor.initialize(nullptr);
        auto s = makeSetup();
        processor.setupProcessing(s);
        processor.setActive(true);
    }
    ~Fixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void block()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
    }

    void sendParams(const std::vector<std::pair<ParamID, ParamValue>>& params)
    {
        MultiParamChanges changes;
        for (const auto& p : params) changes.add(p.first, p.second);
        data.inputParameterChanges = &changes;
        events.clear();
        block();
        data.inputParameterChanges = nullptr;
    }

    // Two hits (~0.65 s apart) so the wire tail vs body is audible.
    std::vector<float> render(int16 midi)
    {
        std::vector<float> mono;
        for (int hit = 0; hit < 2; ++hit)
        {
            events.clear();
            events.noteOn(midi, 0.9f);
            for (int b = 0; b < kTailBlocks / 2; ++b)
            {
                block();
                events.clear();
                for (int s = 0; s < kBlockSize; ++s)
                    mono.push_back(0.5f * (outL[s] + outR[s]));
            }
        }
        return mono;
    }
};

void writeWav(const std::string& path, const std::vector<float>& samples)
{
    const std::uint32_t numSamples = static_cast<std::uint32_t>(samples.size());
    const std::uint32_t dataSize   = numSamples * 2;
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    std::uint32_t chunkSize = 36 + dataSize;
    f.write(reinterpret_cast<const char*>(&chunkSize), 4);
    f.write("WAVEfmt ", 8);
    std::uint32_t fmtSize = 16; f.write(reinterpret_cast<const char*>(&fmtSize), 4);
    std::uint16_t fmt = 1, nch = 1, bps = 16;
    f.write(reinterpret_cast<const char*>(&fmt), 2);
    f.write(reinterpret_cast<const char*>(&nch), 2);
    std::uint32_t srate = static_cast<std::uint32_t>(kSampleRate);
    f.write(reinterpret_cast<const char*>(&srate), 4);
    std::uint32_t byteRate = srate * 2; f.write(reinterpret_cast<const char*>(&byteRate), 4);
    std::uint16_t ba = 2; f.write(reinterpret_cast<const char*>(&ba), 2);
    f.write(reinterpret_cast<const char*>(&bps), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&dataSize), 4);
    for (float s : samples) {
        const std::int16_t i = static_cast<std::int16_t>(
            std::max(-1.0f, std::min(1.0f, s)) * 32767.0f);
        f.write(reinterpret_cast<const char*>(&i), 2);
    }
}

constexpr double kPi = 3.14159265358979323846;
double goertzelPow(const std::vector<float>& x, double f)
{
    const double w = 2.0 * kPi * f / kSampleRate, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (float v : x) { double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
double bandPow(const std::vector<float>& x, double lo, double hi, double step)
{
    double e = 0; for (double f = lo; f <= hi; f += step) e += goertzelPow(x, f); return e;
}
double rms(const std::vector<float>& x)
{
    double s = 0; for (float v : x) s += double(v) * v; return std::sqrt(s / std::max<size_t>(1, x.size()));
}

} // namespace

TEST_CASE("Snare wire-balance audition", "[membrum][diagnose][.snare-audition]")
{
    const int16 kMidi = 38;  // pad 2, DefaultKit Snare
    const std::string outDir = "F:/tmp/";
    std::filesystem::create_directories(outDir);

    // Calibration check: render the BAKED default-kit snare (no overrides) and
    // compare to the auditioned wire_high target (full-RMS ~0.0189). Adjust
    // default_kit noiseLayerGain until it matches.
    Fixture fix;  // fresh processor -> default kit (baked snare)
    const auto wav = fix.render(kMidi);
    writeWav(outDir + "snare_baked.wav", wav);
    const double body = bandPow(wav, 150.0, 450.0, 5.0);
    const double wire = bandPow(wav, 4000.0, 10000.0, 25.0);
    WARN("baked default snare: full-RMS=" << rms(wav)
         << " bodyBand(150-450)=" << body
         << " wireBand(4-10k)=" << wire
         << "  (wire_high target: full-RMS ~0.0189)");

    CHECK(true);
}
