// ==============================================================================
// Gradus Live Mode Byte-Identical Regression Test (spec 142, SC-004 / FR-039b)
// ==============================================================================
// Loads each v2 fixture binary, runs the canonical 60-second MIDI sequence
// (notes 60/64/67 held 5s each + chord 60+64+67 for 30s, plus 15s tail) and
// asserts the emitted MIDI matches the paired golden text file byte-for-byte.
//
// This proves that bumping kNumLanes 9->10 and the lane 10 conditional-inert
// branch do NOT perturb Gradus Live-mode MIDI for pre-existing presets.
// ==============================================================================

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// =====================================================================
// Minimal in-process host shims (mirrors tools/gen_v2_fixtures/host_mocks.h
// but local to the test TU so we don't depend on tools/ headers).
// =====================================================================

class TestEventList : public IEventList {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override {
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getEventCount() override {
        return static_cast<int32>(events_.size());
    }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override {
        if (index < 0 || index >= static_cast<int32>(events_.size()))
            return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultTrue;
    }
    tresult PLUGIN_API addEvent(Event& e) override {
        events_.push_back(e);
        return kResultTrue;
    }
    void addNoteOn(int16 pitch, float vel, int32 sampleOffset) {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = vel;
        e.noteOn.noteId = -1;
        events_.push_back(e);
    }
    void addNoteOff(int16 pitch, int32 sampleOffset) {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

class EmptyParamChanges : public IParameterChanges {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override {
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override { return 0; }
    IParamValueQueue* PLUGIN_API getParameterData(int32) override { return nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override {
        return nullptr;
    }
};

// =====================================================================
// Canonical 60-second test sequence (must match tools/gen_v2_fixtures/common.h)
// =====================================================================
constexpr double kSampleRate = 44100.0;
constexpr int32  kBlockSize  = 512;
constexpr int64_t kTotalSamples = static_cast<int64_t>(60.0 * kSampleRate);
constexpr int kNumBlocks = static_cast<int>(kTotalSamples / kBlockSize);

struct ScheduledEvent {
    int64_t sampleTime;
    bool    isNoteOn;
    int16_t pitch;
    float   velocity;
};

std::vector<ScheduledEvent> makeStandardMidiSequence() {
    std::vector<ScheduledEvent> seq;
    const int64_t s = static_cast<int64_t>(kSampleRate);
    seq.push_back({0LL,         true,  60, 0.8f});
    seq.push_back({5LL  * s,    false, 60, 0.0f});
    seq.push_back({5LL  * s,    true,  64, 0.8f});
    seq.push_back({10LL * s,    false, 64, 0.0f});
    seq.push_back({10LL * s,    true,  67, 0.8f});
    seq.push_back({15LL * s,    false, 67, 0.0f});
    seq.push_back({15LL * s,    true,  60, 0.8f});
    seq.push_back({15LL * s,    true,  64, 0.8f});
    seq.push_back({15LL * s,    true,  67, 0.8f});
    seq.push_back({45LL * s,    false, 60, 0.0f});
    seq.push_back({45LL * s,    false, 64, 0.0f});
    seq.push_back({45LL * s,    false, 67, 0.0f});
    return seq;
}

// Capture MIDI events with absolute sample time.
struct CapturedMidi {
    int64_t absoluteSample;
    bool    isNoteOn;
    int16_t pitch;
    int     velocity;  // 0..127
};

std::string formatCaptured(const std::vector<CapturedMidi>& events)
{
    std::ostringstream ss;
    for (const auto& e : events) {
        char buf[128];
        if (e.isNoteOn) {
            std::snprintf(buf, sizeof(buf), "[%lld] noteOn  %d %d\n",
                static_cast<long long>(e.absoluteSample),
                static_cast<int>(e.pitch), e.velocity);
        } else {
            std::snprintf(buf, sizeof(buf), "[%lld] noteOff %d\n",
                static_cast<long long>(e.absoluteSample),
                static_cast<int>(e.pitch));
        }
        ss << buf;
    }
    return ss.str();
}

std::string readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<char> loadFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    REQUIRE(in.is_open());
    in.seekg(0, std::ios::end);
    auto sz = static_cast<std::streamsize>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<char> data(static_cast<size_t>(sz));
    in.read(data.data(), sz);
    return data;
}

MemoryStream* makeStreamFromBytes(const std::vector<char>& bytes)
{
    auto* stream = new MemoryStream();
    int32 written = 0;
    stream->write(const_cast<char*>(bytes.data()),
        static_cast<int32>(bytes.size()), &written);
    REQUIRE(written == static_cast<int32>(bytes.size()));
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

std::vector<CapturedMidi> driveGradusProcessor(Gradus::Processor& processor)
{
    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);
    float* outChannelBuffers[2] = { outL.data(), outR.data() };
    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannelBuffers;

    TestEventList inEvents;
    TestEventList outEvents;
    EmptyParamChanges emptyParams;

    ProcessContext processContext{};
    processContext.state = ProcessContext::kPlaying
                         | ProcessContext::kTempoValid
                         | ProcessContext::kTimeSigValid
                         | ProcessContext::kProjectTimeMusicValid;
    processContext.tempo = 120.0;
    processContext.timeSigNumerator = 4;
    processContext.timeSigDenominator = 4;
    processContext.sampleRate = kSampleRate;
    processContext.projectTimeMusic = 0.0;
    processContext.projectTimeSamples = 0;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kBlockSize;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &emptyParams;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &inEvents;
    data.outputEvents = &outEvents;
    data.processContext = &processContext;

    auto sequence = makeStandardMidiSequence();
    size_t nextSeqIdx = 0;

    std::vector<CapturedMidi> captured;
    captured.reserve(8192);

    for (int blockIdx = 0; blockIdx < kNumBlocks; ++blockIdx) {
        const int64_t blockStartSample =
            static_cast<int64_t>(blockIdx) * kBlockSize;
        const int64_t blockEndSample = blockStartSample + kBlockSize;

        inEvents.clear();
        while (nextSeqIdx < sequence.size()
            && sequence[nextSeqIdx].sampleTime < blockEndSample)
        {
            const auto& s = sequence[nextSeqIdx];
            const int32 sampleOffset =
                static_cast<int32>(s.sampleTime - blockStartSample);
            if (s.isNoteOn) {
                inEvents.addNoteOn(s.pitch, s.velocity, sampleOffset);
            } else {
                inEvents.addNoteOff(s.pitch, sampleOffset);
            }
            ++nextSeqIdx;
        }

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        outEvents.clear();

        processor.process(data);

        int32 evCount = outEvents.getEventCount();
        for (int32 i = 0; i < evCount; ++i) {
            Event e{};
            if (outEvents.getEvent(i, e) != kResultTrue) continue;
            CapturedMidi c{};
            c.absoluteSample = blockStartSample + e.sampleOffset;
            if (e.type == Event::kNoteOnEvent) {
                c.isNoteOn = true;
                c.pitch    = e.noteOn.pitch;
                c.velocity = std::clamp(
                    static_cast<int>(e.noteOn.velocity * 127.0f + 0.5f),
                    0, 127);
            } else if (e.type == Event::kNoteOffEvent) {
                c.isNoteOn = false;
                c.pitch    = e.noteOff.pitch;
                c.velocity = 0;
            } else {
                continue;
            }
            captured.push_back(c);
        }

        processContext.projectTimeSamples += kBlockSize;
        processContext.projectTimeMusic +=
            static_cast<double>(kBlockSize) / kSampleRate
            * (processContext.tempo / 60.0);
    }

    return captured;
}

void runFixture(const std::string& fixtureName)
{
    const std::filesystem::path fixturesDir{GRADUS_FIXTURES_DIR};
    const auto binPath = fixturesDir /
        ("gradus_v2_preset_" + fixtureName + ".bin");
    const auto txtPath = fixturesDir /
        ("gradus_v2_golden_midi_" + fixtureName + ".txt");

    REQUIRE(std::filesystem::exists(binPath));
    REQUIRE(std::filesystem::exists(txtPath));

    auto stateBytes = loadFile(binPath);
    auto golden = readFile(txtPath);

    auto proc = std::make_unique<Gradus::Processor>();
    proc->initialize(nullptr);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    proc->setupProcessing(setup);

    auto* stream = makeStreamFromBytes(stateBytes);
    REQUIRE(proc->setState(stream) == kResultOk);
    stream->release();

    proc->setActive(true);
    auto captured = driveGradusProcessor(*proc);
    proc->setActive(false);
    proc->terminate();

    auto actual = formatCaptured(captured);

    // INFO captures diagnostics if the assertion fails; the body still runs.
    INFO("fixture: " << fixtureName);
    INFO("actual bytes: " << actual.size()
        << "  golden bytes: " << golden.size());
    REQUIRE(actual == golden);
}

}  // namespace

// =============================================================================
// SC-004: Live-mode MIDI byte-identical post lane 10 extension
// =============================================================================

TEST_CASE("SC-004: Gradus Live mode byte-identical for v2 fixture 'default'",
          "[processor][sequencer][SC-004][regression]")
{
    runFixture("default");
}

TEST_CASE("SC-004: Gradus Live mode byte-identical for v2 fixture 'heavy_lanes'",
          "[processor][sequencer][SC-004][regression]")
{
    runFixture("heavy_lanes");
}

TEST_CASE("SC-004: Gradus Live mode byte-identical for v2 fixture 'midi_delay'",
          "[processor][sequencer][SC-004][regression]")
{
    runFixture("midi_delay");
}
