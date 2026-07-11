// ==============================================================================
// krate-render -- offline audio render + feature extraction CLI (Membrum)
// ==============================================================================
// Usage:
//   krate-render [--note N] [--velocity V] [--seconds S] [--sr RATE]
//                [--block N] [--param ID=VALUE]... [--out PATH] [--quiet]
//
// Renders the Membrum Processor (note-on at t=0) to a stereo float WAV and
// prints a JSON feature summary (peak/RMS in dBFS, spectral centroid, per-band
// energy fractions) to stdout. Exit non-zero on error.
// ==============================================================================

// dr_wav is vendored third-party code; silence its warnings under our /W4 posture.
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4244 4267 4996 4701 4703)
#endif
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include "processor/processor.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <audio_features.h>
#include <enable_ftz_daz.h>
#include <vst_param_changes.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

// Processor is instantiated directly (no factory), so provide a stub.
extern "C" {
Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return nullptr; }
}

namespace {

// Minimal IEventList carrying a single note-on (mirrors the membrum test harness).
class NoteEventList : public IEventList {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override {
        if (index < 0 || index >= static_cast<int32>(events_.size())) return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override {
        events_.push_back(e);
        return kResultOk;
    }
    void noteOn(int16 midi, float velocity) {
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

struct Args {
    int16 note = 60;
    float velocity = 1.0f;
    double seconds = 2.0;
    double sampleRate = 48000.0;
    int block = 512;
    std::string out = "render.wav";
    bool quiet = false;
    std::vector<std::pair<ParamID, ParamValue>> params;
};

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "krate-render: %s needs a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--note") { const char* v = next("--note"); if (!v) return false; a.note = static_cast<int16>(std::atoi(v)); }
        else if (arg == "--velocity") { const char* v = next("--velocity"); if (!v) return false; a.velocity = static_cast<float>(std::atof(v)); }
        else if (arg == "--seconds") { const char* v = next("--seconds"); if (!v) return false; a.seconds = std::atof(v); }
        else if (arg == "--sr") { const char* v = next("--sr"); if (!v) return false; a.sampleRate = std::atof(v); }
        else if (arg == "--block") { const char* v = next("--block"); if (!v) return false; a.block = std::atoi(v); }
        else if (arg == "--out") { const char* v = next("--out"); if (!v) return false; a.out = v; }
        else if (arg == "--quiet") { a.quiet = true; }
        else if (arg == "--param") {
            const char* v = next("--param"); if (!v) return false;
            std::string s = v;
            auto eq = s.find('=');
            if (eq == std::string::npos) { std::fprintf(stderr, "krate-render: --param expects ID=VALUE\n"); return false; }
            a.params.emplace_back(static_cast<ParamID>(std::strtoul(s.substr(0, eq).c_str(), nullptr, 10)),
                                  std::atof(s.substr(eq + 1).c_str()));
        } else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: krate-render [--note N] [--velocity V] [--seconds S] [--sr RATE] "
                        "[--block N] [--param ID=VALUE]... [--out PATH] [--quiet]\n");
            std::exit(0);
        } else {
            std::fprintf(stderr, "krate-render: unknown arg '%s'\n", arg.c_str());
            return false;
        }
    }
    if (a.seconds <= 0.0 || a.sampleRate < 8000.0 || a.block < 1) {
        std::fprintf(stderr, "krate-render: invalid seconds/sr/block\n");
        return false;
    }
    return true;
}

void printJson(const Args& a, const Krate::Test::AudioFeatures& f) {
    std::printf("{\n");
    std::printf("  \"plugin\": \"membrum\",\n");
    std::printf("  \"note\": %d,\n", a.note);
    std::printf("  \"velocity\": %.3f,\n", a.velocity);
    std::printf("  \"sampleRate\": %.1f,\n", a.sampleRate);
    std::printf("  \"durationSec\": %.4f,\n", f.durationSec);
    std::printf("  \"peakDbfs\": %.3f,\n", f.peakDbfs);
    std::printf("  \"rmsDbfs\": %.3f,\n", f.rmsDbfs);
    std::printf("  \"spectralCentroidHz\": %.2f,\n", f.centroidHz);
    std::printf("  \"bandEnergyFraction\": {\n");
    std::printf("    \"20-100\": %.4f,\n", f.band[0]);
    std::printf("    \"100-500\": %.4f,\n", f.band[1]);
    std::printf("    \"500-2k\": %.4f,\n", f.band[2]);
    std::printf("    \"2k-8k\": %.4f,\n", f.band[3]);
    std::printf("    \"8k-nyq\": %.4f\n", f.band[4]);
    std::printf("  }\n");
    std::printf("}\n");
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) return 2;

    enableFTZDAZ();

    Membrum::Processor proc;
    if (proc.initialize(nullptr) != kResultOk) {
        std::fprintf(stderr, "krate-render: processor.initialize failed\n");
        return 1;
    }

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = args.block;
    setup.sampleRate = args.sampleRate;
    if (proc.setupProcessing(setup) != kResultOk) {
        std::fprintf(stderr, "krate-render: setupProcessing failed\n");
        return 1;
    }
    proc.setActive(true);

    const size_t totalSamples = static_cast<size_t>(args.seconds * args.sampleRate);
    std::vector<float> outL(static_cast<size_t>(args.block));
    std::vector<float> outR(static_cast<size_t>(args.block));
    float* outChans[2] = {outL.data(), outR.data()};

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChans;
    outBus.silenceFlags = 0;

    NoteEventList events;
    events.noteOn(args.note, args.velocity);

    Krate::Test::ParameterChanges paramChanges;
    for (const auto& p : args.params) paramChanges.add(p.first, p.second);

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.numInputs = 0;
    data.inputs = nullptr;

    std::vector<float> renderedL, renderedR;
    renderedL.reserve(totalSamples);
    renderedR.reserve(totalSamples);

    size_t done = 0;
    bool firstBlock = true;
    while (done < totalSamples) {
        const int thisBlock = static_cast<int>(std::min<size_t>(static_cast<size_t>(args.block), totalSamples - done));
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        data.numSamples = thisBlock;
        data.inputEvents = firstBlock ? &events : nullptr;
        data.inputParameterChanges = (firstBlock && !args.params.empty()) ? &paramChanges : nullptr;
        data.outputParameterChanges = nullptr;
        data.outputEvents = nullptr;
        data.processContext = nullptr;

        proc.process(data);

        for (int i = 0; i < thisBlock; ++i) {
            renderedL.push_back(outL[static_cast<size_t>(i)]);
            renderedR.push_back(outR[static_cast<size_t>(i)]);
        }
        done += static_cast<size_t>(thisBlock);
        firstBlock = false;
    }

    proc.setActive(false);
    proc.terminate();

    // Write interleaved stereo float WAV.
    std::vector<float> interleaved(renderedL.size() * 2);
    for (size_t i = 0; i < renderedL.size(); ++i) {
        interleaved[i * 2] = renderedL[i];
        interleaved[i * 2 + 1] = renderedR[i];
    }
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 2;
    fmt.sampleRate = static_cast<drwav_uint32>(args.sampleRate);
    fmt.bitsPerSample = 32;
    drwav wav;
    if (!drwav_init_file_write(&wav, args.out.c_str(), &fmt, nullptr)) {
        std::fprintf(stderr, "krate-render: cannot open '%s' for writing\n", args.out.c_str());
        return 1;
    }
    drwav_write_pcm_frames(&wav, renderedL.size(), interleaved.data());
    drwav_uninit(&wav);

    // Feature summary over the mono sum.
    std::vector<float> mono(renderedL.size());
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (renderedL[i] + renderedR[i]);
    Krate::Test::AudioFeatures feats = Krate::Test::extractAudioFeatures(mono, args.sampleRate);

    if (!args.quiet)
        std::fprintf(stderr, "krate-render: wrote %s (%zu frames)\n", args.out.c_str(), renderedL.size());
    printJson(args, feats);
    return 0;
}
