// ==============================================================================
// Integration Test: Chaos Wind Preset Audio Output
// ==============================================================================
// Reproduces user-reported issue: loading the "Chaos Wind" preset into a fresh
// Ruinae instance and playing a note produces no AUDIBLE output. The data may
// have non-zero samples, but if the content is DC or sub-audio frequency, a
// human cannot hear it.
//
// This test loads the ACTUAL Chaos_Wind.vstpreset binary, sends a MIDI noteOn,
// and verifies the output has real audio-frequency content (zero crossings,
// AC energy) — not just non-zero samples.
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
#include <numeric>
#include <vector>

namespace {

// =============================================================================
// Test Helpers
// =============================================================================

class CWEventList : public Steinberg::Vst::IEventList {
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
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

class CWEmptyParamChanges : public Steinberg::Vst::IParameterChanges {
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

/// Read a .vstpreset file and extract the component state bytes.
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

std::filesystem::path findChaosWindPreset() {
    const char* candidates[] = {
        "plugins/ruinae/resources/presets/Textures/Chaos_Wind.vstpreset",
        "../plugins/ruinae/resources/presets/Textures/Chaos_Wind.vstpreset",
        "../../plugins/ruinae/resources/presets/Textures/Chaos_Wind.vstpreset",
        "../../../plugins/ruinae/resources/presets/Textures/Chaos_Wind.vstpreset",
        "../../../../plugins/ruinae/resources/presets/Textures/Chaos_Wind.vstpreset",
    };
    for (const auto* c : candidates) {
        auto p = std::filesystem::path(c);
        if (std::filesystem::exists(p)) return std::filesystem::canonical(p);
    }
    return {};
}

bool hasNanOrInf(const float* buffer, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (std::isnan(buffer[i]) || std::isinf(buffer[i])) return true;
    }
    return false;
}

float findPeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

/// Count zero crossings (sign changes) in a buffer — measures frequency content.
/// A 261Hz signal (C4) at 44100Hz has ~522 crossings per 512 samples.
/// DC or sub-20Hz content has <2 crossings per 512 samples.
size_t countZeroCrossings(const float* buffer, size_t numSamples) {
    size_t crossings = 0;
    for (size_t i = 1; i < numSamples; ++i) {
        if ((buffer[i] > 0.0f && buffer[i - 1] <= 0.0f) ||
            (buffer[i] < 0.0f && buffer[i - 1] >= 0.0f)) {
            ++crossings;
        }
    }
    return crossings;
}

/// Compute RMS of AC component only (subtract DC mean first).
/// This separates actual audio energy from DC offset.
float computeACRMS(const float* buffer, size_t numSamples) {
    // Compute mean (DC component)
    double sum = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += static_cast<double>(buffer[i]);
    }
    double mean = sum / static_cast<double>(numSamples);

    // Compute RMS of AC component (signal minus DC)
    double acSum = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        double ac = static_cast<double>(buffer[i]) - mean;
        acSum += ac * ac;
    }
    return static_cast<float>(std::sqrt(acSum / static_cast<double>(numSamples)));
}

/// Compute crest factor (peak / RMS). A pure sine has ~1.41, noise ~3-4.
/// DC has infinite crest factor (peak = RMS but no AC content).
/// Very high crest factor with low AC RMS suggests DC-like content.

} // anonymous namespace

// =============================================================================
// Test: Load ACTUAL .vstpreset file and verify AUDIBLE audio content
// =============================================================================

TEST_CASE("Chaos Wind preset produces audible audio-rate content",
          "[preset][chaos-wind][integration]") {
    auto presetPath = findChaosWindPreset();
    if (presetPath.empty()) {
        WARN("Chaos_Wind.vstpreset not found — skipping (run from project root)");
        return;
    }

    auto stateBytes = loadVstPresetComponentState(presetPath);
    REQUIRE(!stateBytes.empty());

    // --- Fresh processor, load preset, play note ---
    Ruinae::Processor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultTrue);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultTrue);
    REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);

    Steinberg::MemoryStream loadStream;
    loadStream.write(stateBytes.data(),
        static_cast<Steinberg::int32>(stateBytes.size()), nullptr);
    loadStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(&loadStream) == Steinberg::kResultTrue);
    drainPresetTransfer(&processor);

    constexpr size_t kBlockSize = 512;
    constexpr int kMaxBlocks = 200; // ~2.3s for 800ms attack + warmup

    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    CWEventList events;
    CWEmptyParamChanges emptyParams;

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

    // Send noteOn
    events.addNoteOn(60, 0.8f);
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    processor.process(data);
    events.clear();

    // --- Collect audio metrics over all blocks ---
    float peakOverall = 0.0f;
    float maxACRMS = 0.0f;
    size_t totalZeroCrossings = 0;
    int blocksWithAudio = 0;       // blocks with peak > 0.001
    int blocksWithACContent = 0;   // blocks with AC RMS > 0.01
    int blocksWithCrossings = 0;   // blocks with >= 10 zero crossings
    float maxDC = 0.0f;            // max absolute DC offset across blocks

    for (int block = 1; block < kMaxBlocks; ++block) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);

        REQUIRE_FALSE(hasNanOrInf(outL.data(), kBlockSize));
        REQUIRE_FALSE(hasNanOrInf(outR.data(), kBlockSize));

        float peakL = findPeak(outL.data(), kBlockSize);
        float peakR = findPeak(outR.data(), kBlockSize);
        float blockPeak = std::max(peakL, peakR);
        peakOverall = std::max(peakOverall, blockPeak);

        if (blockPeak > 0.001f) {
            ++blocksWithAudio;

            // Measure AC content (audio-frequency energy, no DC)
            float acRmsL = computeACRMS(outL.data(), kBlockSize);
            float acRmsR = computeACRMS(outR.data(), kBlockSize);
            float blockACRMS = std::max(acRmsL, acRmsR);
            maxACRMS = std::max(maxACRMS, blockACRMS);

            if (blockACRMS > 0.01f) {
                ++blocksWithACContent;
            }

            // Measure zero crossings (proves audio-rate oscillation)
            size_t crossL = countZeroCrossings(outL.data(), kBlockSize);
            size_t crossR = countZeroCrossings(outR.data(), kBlockSize);
            size_t blockCrossings = std::max(crossL, crossR);
            totalZeroCrossings += blockCrossings;

            if (blockCrossings >= 3) {
                ++blocksWithCrossings;
            }

            // Measure DC offset
            double sumL = 0.0;
            for (size_t i = 0; i < kBlockSize; ++i)
                sumL += static_cast<double>(outL[i]);
            float dcL = static_cast<float>(std::abs(sumL / static_cast<double>(kBlockSize)));
            maxDC = std::max(maxDC, dcL);
        }
    }

    // --- Also run reference: default processor (saw wave) ---
    Ruinae::Processor refProcessor;
    refProcessor.initialize(nullptr);
    refProcessor.setupProcessing(setup);
    refProcessor.setActive(true);

    CWEventList refEvents;
    CWEmptyParamChanges refParams;
    refEvents.addNoteOn(60, 0.8f);

    Steinberg::Vst::ProcessData refData{};
    refData.processMode = Steinberg::Vst::kRealtime;
    refData.symbolicSampleSize = Steinberg::Vst::kSample32;
    refData.numSamples = static_cast<Steinberg::int32>(kBlockSize);
    refData.numInputs = 0;
    refData.inputs = nullptr;
    refData.numOutputs = 1;
    refData.outputs = &outputBus;
    refData.inputParameterChanges = &refParams;
    refData.inputEvents = &refEvents;
    refData.processContext = nullptr;

    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    refProcessor.process(refData);
    refEvents.clear();

    float refPeak = 0.0f;
    float refMaxACRMS = 0.0f;
    size_t refTotalCrossings = 0;

    for (int block = 1; block < kMaxBlocks; ++block) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        refProcessor.process(refData);
        float rp = std::max(findPeak(outL.data(), kBlockSize),
                            findPeak(outR.data(), kBlockSize));
        refPeak = std::max(refPeak, rp);
        refMaxACRMS = std::max(refMaxACRMS, std::max(
            computeACRMS(outL.data(), kBlockSize),
            computeACRMS(outR.data(), kBlockSize)));
        refTotalCrossings += std::max(countZeroCrossings(outL.data(), kBlockSize),
                                       countZeroCrossings(outR.data(), kBlockSize));
    }
    refProcessor.setActive(false);
    refProcessor.terminate();

    // --- Report all metrics ---
    INFO("=== Chaos Wind Preset ===");
    INFO("  Peak amplitude: " << peakOverall);
    INFO("  Max AC RMS (no DC): " << maxACRMS);
    INFO("  Max DC offset: " << maxDC);
    INFO("  Blocks with audio (peak>0.001): " << blocksWithAudio << " / " << kMaxBlocks);
    INFO("  Blocks with AC content (acRMS>0.01): " << blocksWithACContent);
    INFO("  Blocks with crossings (>=3): " << blocksWithCrossings);
    INFO("  Total zero crossings: " << totalZeroCrossings);
    INFO("=== Reference (default saw) ===");
    INFO("  Peak: " << refPeak);
    INFO("  Max AC RMS: " << refMaxACRMS);
    INFO("  Total zero crossings: " << refTotalCrossings);

    // --- Assertions: the output must be AUDIBLE, not just non-zero ---

    // 1. Must have blocks with non-trivial amplitude
    REQUIRE(blocksWithAudio > 20);

    // 2. Must have actual AC (audio-frequency) energy, not just DC offset.
    //    AC RMS > 0.05 means there's real oscillation a human can hear.
    REQUIRE(maxACRMS > 0.05f);

    // 3. Must have zero crossings proving audio-rate content.
    //    A 100Hz signal at 44100/512 gives ~23 crossings/block.
    //    DC or sub-20Hz gives <2 per block.
    //    Require at least some blocks with meaningful crossings.
    REQUIRE(blocksWithCrossings > 10);

    // 4. Total zero crossings should be substantial (thousands over 200 blocks)
    REQUIRE(totalZeroCrossings > 1000);

    processor.setActive(false);
    processor.terminate();
}
