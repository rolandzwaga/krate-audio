// ==============================================================================
// Integration Test: Abyssal Preset Output Level Analysis
// ==============================================================================
// Investigates user-reported issue: the "Abyssal" preset produces very faint,
// nearly inaudible output. This test loads the preset, plays a note, and
// captures detailed audio metrics over time to diagnose the cause.
//
// Abyssal config:
//   OSC A: Chaos (Lorenz), -24 semi, level 0.7, amount 0.4, coupling 0.15
//   OSC B: Brown noise, level 0.2
//   Mixer: position 0.2 (favors A)
//   Filter: 24dB Ladder, cutoff 400Hz, res 0.4
//   Distortion: Tape saturator, drive 0.3
//   Amp Env: A=1500ms D=800ms S=0.8 R=3000ms
//   Reverb: size 0.95, mix 0.45, damping 0.8, diffusion 0.85
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
// Test Helpers (local to this TU)
// =============================================================================

class AbEventList : public Steinberg::Vst::IEventList {
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

class AbEmptyParamChanges : public Steinberg::Vst::IParameterChanges {
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

float findPeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

float computeRMS(const float* buffer, size_t numSamples) {
    double sum = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(numSamples)));
}

float computeACRMS(const float* buffer, size_t numSamples) {
    double sum = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += static_cast<double>(buffer[i]);
    }
    double mean = sum / static_cast<double>(numSamples);

    double acSum = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        double ac = static_cast<double>(buffer[i]) - mean;
        acSum += ac * ac;
    }
    return static_cast<float>(std::sqrt(acSum / static_cast<double>(numSamples)));
}

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

// Convert linear amplitude to dBFS
float toDBFS(float linear) {
    if (linear <= 0.0f) return -120.0f;
    return 20.0f * std::log10(linear);
}

// Helper: set up a processor, load a preset, play note, measure output
struct AudioMetrics {
    float peakOverall = 0.0f;
    float maxRMS = 0.0f;
    float maxACRMS = 0.0f;
    float maxDC = 0.0f;
    size_t totalZeroCrossings = 0;
    int blocksWithAudio = 0;
    int blocksWithACContent = 0;
    int blocksWithCrossings = 0;
    int totalBlocks = 0;
    // Per-phase metrics (before/during/after attack)
    float peakDuringAttack = 0.0f;
    float peakAfterAttack = 0.0f;
    float rmsAfterAttack = 0.0f;
};

AudioMetrics measurePreset(const std::vector<char>& stateBytes, int16_t midiNote,
                            float velocity, int totalBlocks) {
    Ruinae::Processor processor;
    processor.initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    processor.setupProcessing(setup);
    processor.setActive(true);

    if (!stateBytes.empty()) {
        Steinberg::MemoryStream loadStream;
        loadStream.write(const_cast<char*>(stateBytes.data()),
            static_cast<Steinberg::int32>(stateBytes.size()), nullptr);
        loadStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        processor.setState(&loadStream);
        drainPresetTransfer(&processor);
    }

    constexpr size_t kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    AbEventList events;
    AbEmptyParamChanges emptyParams;

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
    events.addNoteOn(midiNote, velocity);
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    processor.process(data);
    events.clear();

    AudioMetrics m{};
    m.totalBlocks = totalBlocks;

    // Attack time in blocks: 1500ms attack at 44100Hz/512 = ~129 blocks
    constexpr int kAttackBlocks = 130;

    for (int block = 1; block < totalBlocks; ++block) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);

        float peakL = findPeak(outL.data(), kBlockSize);
        float peakR = findPeak(outR.data(), kBlockSize);
        float blockPeak = std::max(peakL, peakR);
        m.peakOverall = std::max(m.peakOverall, blockPeak);

        float rmsL = computeRMS(outL.data(), kBlockSize);
        float rmsR = computeRMS(outR.data(), kBlockSize);
        float blockRMS = std::max(rmsL, rmsR);
        m.maxRMS = std::max(m.maxRMS, blockRMS);

        float acRmsL = computeACRMS(outL.data(), kBlockSize);
        float acRmsR = computeACRMS(outR.data(), kBlockSize);
        float blockACRMS = std::max(acRmsL, acRmsR);
        m.maxACRMS = std::max(m.maxACRMS, blockACRMS);

        size_t crossL = countZeroCrossings(outL.data(), kBlockSize);
        size_t crossR = countZeroCrossings(outR.data(), kBlockSize);
        size_t blockCrossings = std::max(crossL, crossR);
        m.totalZeroCrossings += blockCrossings;

        if (blockPeak > 0.001f) ++m.blocksWithAudio;
        if (blockACRMS > 0.01f) ++m.blocksWithACContent;
        if (blockCrossings >= 3) ++m.blocksWithCrossings;

        // DC offset
        double sumL = 0.0;
        for (size_t i = 0; i < kBlockSize; ++i)
            sumL += static_cast<double>(outL[i]);
        float dcL = static_cast<float>(std::abs(sumL / static_cast<double>(kBlockSize)));
        m.maxDC = std::max(m.maxDC, dcL);

        // Phase tracking
        if (block <= kAttackBlocks) {
            m.peakDuringAttack = std::max(m.peakDuringAttack, blockPeak);
        } else {
            m.peakAfterAttack = std::max(m.peakAfterAttack, blockPeak);
            m.rmsAfterAttack = std::max(m.rmsAfterAttack, blockRMS);
        }
    }

    processor.setActive(false);
    processor.terminate();
    return m;
}

} // anonymous namespace

// =============================================================================
// Test: Abyssal preset output level analysis
// =============================================================================

TEST_CASE("Abyssal preset output level analysis",
          "[preset][abyssal][integration][level-analysis]") {
    auto presetPath = findPreset("plugins/ruinae/resources/presets/Textures/Abyssal.vstpreset");
    if (presetPath.empty()) {
        WARN("Abyssal.vstpreset not found — skipping (run from project root)");
        return;
    }

    auto stateBytes = loadVstPresetComponentState(presetPath);
    REQUIRE(!stateBytes.empty());

    // Need enough blocks to get past the 1500ms attack + measure sustain
    // 1500ms attack = ~129 blocks at 44100/512. Run 400 blocks (~4.6s total)
    constexpr int kTotalBlocks = 400;

    SECTION("Abyssal preset with C4 (MIDI 60)") {
        auto m = measurePreset(stateBytes, 60, 0.8f, kTotalBlocks);

        INFO("=== Abyssal Preset (C4/60, vel=0.8) ===");
        INFO("  Peak amplitude: " << m.peakOverall << " (" << toDBFS(m.peakOverall) << " dBFS)");
        INFO("  Max RMS: " << m.maxRMS << " (" << toDBFS(m.maxRMS) << " dBFS)");
        INFO("  Max AC RMS: " << m.maxACRMS << " (" << toDBFS(m.maxACRMS) << " dBFS)");
        INFO("  Max DC offset: " << m.maxDC);
        INFO("  Peak during attack (first 1.5s): " << m.peakDuringAttack
             << " (" << toDBFS(m.peakDuringAttack) << " dBFS)");
        INFO("  Peak after attack (sustain): " << m.peakAfterAttack
             << " (" << toDBFS(m.peakAfterAttack) << " dBFS)");
        INFO("  RMS after attack: " << m.rmsAfterAttack
             << " (" << toDBFS(m.rmsAfterAttack) << " dBFS)");
        INFO("  Blocks with audio (peak>0.001): " << m.blocksWithAudio << " / " << kTotalBlocks);
        INFO("  Blocks with AC content (acRMS>0.01): " << m.blocksWithACContent);
        INFO("  Blocks with crossings (>=3): " << m.blocksWithCrossings);
        INFO("  Total zero crossings: " << m.totalZeroCrossings);

        // Record the levels — this test is diagnostic, not prescriptive
        // A well-designed preset should reach at least -20 dBFS peak during sustain
        CHECK(m.peakAfterAttack > 0.0f); // must produce SOME output
    }

    SECTION("Abyssal preset with C2 (MIDI 36) - already very low") {
        auto m = measurePreset(stateBytes, 36, 0.8f, kTotalBlocks);

        WARN("=== Abyssal Preset (C2/36, vel=0.8) — playing very low ===");
        WARN("  Peak amplitude: " << m.peakOverall << " (" << toDBFS(m.peakOverall) << " dBFS)");
        WARN("  Max RMS: " << m.maxRMS << " (" << toDBFS(m.maxRMS) << " dBFS)");
        WARN("  Max AC RMS: " << m.maxACRMS << " (" << toDBFS(m.maxACRMS) << " dBFS)");
        WARN("  Peak after attack: " << m.peakAfterAttack
             << " (" << toDBFS(m.peakAfterAttack) << " dBFS)");
        WARN("  Blocks with audio: " << m.blocksWithAudio << " / " << kTotalBlocks);
        WARN("  Total zero crossings: " << m.totalZeroCrossings);
    }

    SECTION("Reference: default processor (saw wave, C4)") {
        // Empty state = default init sound
        auto m = measurePreset({}, 60, 0.8f, kTotalBlocks);

        WARN("=== Reference Default (Saw C4, vel=0.8) ===");
        WARN("  Peak amplitude: " << m.peakOverall << " (" << toDBFS(m.peakOverall) << " dBFS)");
        WARN("  Max RMS: " << m.maxRMS << " (" << toDBFS(m.maxRMS) << " dBFS)");
        WARN("  Max AC RMS: " << m.maxACRMS << " (" << toDBFS(m.maxACRMS) << " dBFS)");
        WARN("  Peak after attack: " << m.peakAfterAttack
             << " (" << toDBFS(m.peakAfterAttack) << " dBFS)");
        WARN("  Blocks with audio: " << m.blocksWithAudio << " / " << kTotalBlocks);
        WARN("  Total zero crossings: " << m.totalZeroCrossings);
    }
}
