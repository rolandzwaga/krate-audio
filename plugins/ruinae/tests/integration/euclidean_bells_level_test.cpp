// ==============================================================================
// Integration Test: Euclidean_Bells Preset Level Analysis
// ==============================================================================
// Investigates user-reported issue: preset sounds incredibly faint.
// Loads the preset, plays a note, and measures output levels to find
// where the signal drops.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "drain_preset_transfer.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

class EvtList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getEventCount() override {
        return static_cast<Steinberg::int32>(events_.size());
    }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                            Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(events_.size()))
            return Steinberg::kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events_.push_back(e);
        return Steinberg::kResultTrue;
    }
    void addNoteOn(int16_t pitch, float velocity, int32_t sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = -1;
        events_.push_back(e);
    }
    void addNoteOff(int16_t pitch, int32_t sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }
private:
    std::vector<Steinberg::Vst::Event> events_;
};

class EmptyParamChanges : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getParameterCount() override { return 0; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32) override {
        return nullptr;
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override {
        return nullptr;
    }
};

std::vector<char> loadVstPresetComponentState(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto fileSize = f.tellg();
    f.seekg(0);
    std::vector<char> fileData(static_cast<size_t>(fileSize));
    f.read(fileData.data(), fileSize);
    if (fileSize < 48) return {};
    if (std::memcmp(fileData.data(), "VST3", 4) != 0) return {};
    int64_t listOffset = 0;
    std::memcpy(&listOffset, fileData.data() + 40, 8);
    constexpr int64_t kHeaderSize = 48;
    if (listOffset <= kHeaderSize || listOffset > fileSize) return {};
    auto stateSize = static_cast<size_t>(listOffset - kHeaderSize);
    return std::vector<char>(fileData.begin() + kHeaderSize,
                             fileData.begin() + kHeaderSize + static_cast<ptrdiff_t>(stateSize));
}

std::filesystem::path findPreset(const char* relativePath) {
    const std::string prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const auto& prefix : prefixes) {
        auto p = std::filesystem::path(prefix + relativePath);
        if (std::filesystem::exists(p)) return std::filesystem::canonical(p);
    }
    return {};
}

float computeRMS(const float* buffer, size_t numSamples) {
    double sum = 0.0;
    for (size_t i = 0; i < numSamples; ++i)
        sum += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(numSamples)));
}

float findPeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::abs(buffer[i]));
    return peak;
}

float toDBFS(float linear) {
    if (linear <= 0.0f) return -120.0f;
    return 20.0f * std::log10(linear);
}

} // anonymous namespace

TEST_CASE("Euclidean_Bells preset level analysis",
          "[preset][euclidean-bells][integration][level-analysis]") {

    auto presetPath = findPreset(
        "plugins/ruinae/resources/presets/Rhythmic/Euclidean_Bells.vstpreset");
    if (presetPath.empty()) {
        WARN("Euclidean_Bells.vstpreset not found — skipping");
        return;
    }

    auto stateBytes = loadVstPresetComponentState(presetPath);
    REQUIRE(!stateBytes.empty());

    // Set up processor
    Ruinae::Processor processor;
    processor.initialize(nullptr);

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
    processor.setupProcessing(setup);
    processor.setActive(true);

    // Decode key params from binary state for diagnostics
    {
        const char* d = stateBytes.data();
        size_t sz = stateBytes.size();
        size_t off = 0;
        auto readF = [&]() -> float {
            float v = 0; if (off + 4 <= sz) { std::memcpy(&v, d + off, 4); off += 4; } return v;
        };
        auto readI = [&]() -> int32_t {
            int32_t v = 0; if (off + 4 <= sz) { std::memcpy(&v, d + off, 4); off += 4; } return v;
        };

        int32_t version = readI();
        WARN("State version: " << version);

        // Global params
        float masterGain = readF();
        int32_t voiceMode = readI();
        int32_t polyphony = readI();
        int32_t softLimit = readI();
        float width = readF();
        float spread = readF();
        WARN("=== Key Preset Parameters ===");
        WARN("Master gain: " << masterGain << " (linear, 1.0 = 0dB, range 0-2)");
        WARN("Voice mode: " << voiceMode << " (0=poly, 1=mono)");
        WARN("Polyphony: " << polyphony);
        WARN("Soft limit: " << softLimit);
        WARN("Width: " << width << " | Spread: " << spread);

        // OscA: type(i32), tuneSemi(f), fineCents(f), level(f), phase(f), then many type-specific
        int32_t oscAType = readI();
        float oscATuneSemi = readF();
        float oscAFineCents = readF();
        float oscALevel = readF();
        float oscAPhase = readF();
        WARN("OscA type: " << oscAType << " | level: " << oscALevel
             << " | tuneSemi: " << oscATuneSemi << " | fineCents: " << oscAFineCents);
        // Skip remaining OscA type-specific params (waveform, pw, phaseMod, freqMod, pd*, sync*,
        // additive*, chaos*, particle*, formant*, spectral*, noise) = 26 more fields
        // i32, f, f, f, i32, f, f, i32, i32, f, f, i32, f, f, i32, f, f, i32, f, f, f, i32, i32, f, i32, f, f, f, i32
        for (int i = 0; i < 26; ++i) readF(); // all 4 bytes each

        // OscB: same layout as OscA
        int32_t oscBType = readI();
        float oscBTuneSemi = readF();
        float oscBFineCents = readF();
        float oscBLevel = readF();
        readF(); // phase
        WARN("OscB type: " << oscBType << " | level: " << oscBLevel
             << " | tuneSemi: " << oscBTuneSemi);
        for (int i = 0; i < 26; ++i) readF(); // skip type-specific

        // Too complex to manually decode the rest correctly — let's just report
        // what we have so far and look at signal chain in the engine.
        WARN("(Remaining params omitted — binary layout too complex for manual decode)");
    }

    // Load preset state
    {
        Steinberg::MemoryStream loadStream;
        loadStream.write(const_cast<char*>(stateBytes.data()),
            static_cast<Steinberg::int32>(stateBytes.size()), nullptr);
        loadStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        processor.setState(&loadStream);
        drainPresetTransfer(&processor);
    }

    // Audio buffers
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    EvtList events;
    EmptyParamChanges emptyParams;

    // Provide tempo context (important for euclidean/arp presets)
    Steinberg::Vst::ProcessContext ctx{};
    ctx.state = Steinberg::Vst::ProcessContext::kTempoValid |
                Steinberg::Vst::ProcessContext::kTimeSigValid |
                Steinberg::Vst::ProcessContext::kPlaying |
                Steinberg::Vst::ProcessContext::kProjectTimeMusicValid |
                Steinberg::Vst::ProcessContext::kBarPositionValid;
    ctx.tempo = 120.0;
    ctx.timeSigNumerator = 4;
    ctx.timeSigDenominator = 4;
    ctx.projectTimeMusic = 0.0;
    ctx.barPositionMusic = 0.0;
    ctx.sampleRate = kSampleRate;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &emptyParams;
    data.inputEvents = &events;
    data.processContext = &ctx;

    // Play C4 for 3 seconds, then 1 second tail
    constexpr int kBlocksPerSecond = 86;
    constexpr int kNoteBlocks = kBlocksPerSecond * 3;
    constexpr int kTailBlocks = kBlocksPerSecond * 1;
    constexpr int kTotalBlocks = kNoteBlocks + kTailBlocks;

    float overallPeak = 0.0f;
    float overallRmsMax = 0.0f;

    struct BlockInfo {
        int block;
        float timeS;
        float rmsL, rmsR, peakL, peakR;
    };
    std::vector<BlockInfo> timeline;
    timeline.reserve(kTotalBlocks);

    // NoteOn
    events.addNoteOn(60, 0.8f, 0);

    for (int b = 0; b < kTotalBlocks; ++b) {
        if (b == kNoteBlocks) {
            events.addNoteOff(60, 0);
        }

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        processor.process(data);
        events.clear();

        // Advance transport
        ctx.projectTimeMusic += static_cast<double>(kBlockSize) / kSampleRate * (ctx.tempo / 60.0);

        float rL = computeRMS(outL.data(), kBlockSize);
        float rR = computeRMS(outR.data(), kBlockSize);
        float pL = findPeak(outL.data(), kBlockSize);
        float pR = findPeak(outR.data(), kBlockSize);

        float rmsMono = std::max(rL, rR);
        float peakMono = std::max(pL, pR);
        overallPeak = std::max(overallPeak, peakMono);
        overallRmsMax = std::max(overallRmsMax, rmsMono);

        timeline.push_back({b,
            static_cast<float>(b * kBlockSize) / static_cast<float>(kSampleRate),
            rL, rR, pL, pR});
    }

    // Print timeline (every 8th block ≈ 93ms)
    WARN("=== Euclidean_Bells Level Analysis ===");
    WARN("Note: C4 (MIDI 60) | Velocity: 0.8 | Duration: 3s + 1s tail");
    WARN("Overall peak: " << toDBFS(overallPeak) << " dBFS ("
         << overallPeak << " linear)");
    WARN("Overall RMS max: " << toDBFS(overallRmsMax) << " dBFS");
    WARN("");
    WARN("Block | Time(s) | RMS L(dB) | RMS R(dB) | Peak L(dB) | Peak R(dB)");
    WARN("------|---------|-----------|-----------|------------|----------");

    for (size_t i = 0; i < timeline.size(); i += 8) {
        auto& t = timeline[i];
        char line[128];
        snprintf(line, sizeof(line), "%5d | %7.3f | %9.1f | %9.1f | %10.1f | %10.1f",
                 t.block, t.timeS,
                 toDBFS(t.rmsL), toDBFS(t.rmsR),
                 toDBFS(t.peakL), toDBFS(t.peakR));
        WARN(line);
    }

    // Flag if output is very quiet
    if (overallPeak < 0.01f) {
        WARN("");
        WARN("*** OUTPUT IS EXTREMELY FAINT (peak < -40 dBFS) ***");
        WARN("Peak linear value: " << overallPeak);
    } else if (overallPeak < 0.1f) {
        WARN("");
        WARN("*** OUTPUT IS QUIET (peak < -20 dBFS) ***");
        WARN("Peak linear value: " << overallPeak);
    }

    CHECK(overallPeak > 0.0f);

    processor.setActive(false);
    processor.terminate();
}

TEST_CASE("Default processor level baseline",
          "[preset][baseline][integration][level-analysis]") {

    Ruinae::Processor processor;
    processor.initialize(nullptr);

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
    processor.setupProcessing(setup);
    processor.setActive(true);

    // NO preset loaded — default parameters

    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    EvtList events;
    EmptyParamChanges emptyParams;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &emptyParams;
    data.inputEvents = &events;
    data.processContext = nullptr;

    float overallPeak = 0.0f;

    events.addNoteOn(60, 0.8f, 0);

    for (int b = 0; b < 86; ++b) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        events.clear();

        float pL = findPeak(outL.data(), kBlockSize);
        float pR = findPeak(outR.data(), kBlockSize);
        overallPeak = std::max(overallPeak, std::max(pL, pR));
    }

    WARN("=== Default Processor Baseline ===");
    WARN("Overall peak: " << toDBFS(overallPeak) << " dBFS (" << overallPeak << " linear)");

    CHECK(overallPeak > 0.0f);

    processor.setActive(false);
    processor.terminate();
}
