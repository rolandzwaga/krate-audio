// ==============================================================================
// Integration Test: Cathedral_Organ Preset Envelope Analysis
// ==============================================================================
// Investigates user-reported issue: when playing 4 notes in sequence (each ~1s),
// there is a phantom volume boost around halfway through the second note, as if
// some envelope or modulation source is boosting the signal unexpectedly.
//
// This test loads the preset, plays 4 notes sequentially, and prints per-block
// RMS over the entire duration to visualize the amplitude envelope.
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

class CoEventList : public Steinberg::Vst::IEventList {
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

    void addNoteOff(int16_t pitch, int32_t sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        e.noteOff.tuning = 0.0f;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

class CoEmptyParamChanges : public Steinberg::Vst::IParameterChanges {
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
    for (size_t i = 0; i < numSamples; ++i) {
        sum += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(numSamples)));
}

float findPeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

float toDBFS(float linear) {
    if (linear <= 0.0f) return -120.0f;
    return 20.0f * std::log10(linear);
}

} // anonymous namespace

// =============================================================================
// Test: Cathedral_Organ preset envelope analysis (4 sequential notes)
// =============================================================================

TEST_CASE("Cathedral_Organ preset envelope analysis - 4 sequential notes",
          "[preset][cathedral-organ][integration][envelope-analysis]") {

    auto presetPath = findPreset(
        "plugins/ruinae/resources/presets/Pads/Cathedral_Organ.vstpreset");
    if (presetPath.empty()) {
        WARN("Cathedral_Organ.vstpreset not found — skipping (run from project root)");
        return;
    }

    auto stateBytes = loadVstPresetComponentState(presetPath);
    REQUIRE(!stateBytes.empty());

    // -------------------------------------------------------------------------
    // Set up processor
    // -------------------------------------------------------------------------
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

    // Load preset state
    {
        Steinberg::MemoryStream loadStream;
        loadStream.write(const_cast<char*>(stateBytes.data()),
            static_cast<Steinberg::int32>(stateBytes.size()), nullptr);
        loadStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        processor.setState(&loadStream);
        drainPresetTransfer(&processor);
    }

    // -------------------------------------------------------------------------
    // Audio buffers
    // -------------------------------------------------------------------------
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    CoEventList events;
    CoEmptyParamChanges emptyParams;

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

    // -------------------------------------------------------------------------
    // Play 4 notes, each ~1 second, with noteOff before next noteOn
    // Notes: C4(60), D4(62), E4(64), F4(65)
    // 1 second at 44100Hz / 512 samples per block = ~86.1 blocks per second
    // We'll use 86 blocks per note = ~0.998 seconds
    // Plus 1 second of tail after the last note off
    // -------------------------------------------------------------------------
    constexpr int kBlocksPerNote = 86;  // ~1 second
    constexpr int kNumNotes = 4;
    constexpr int kTailBlocks = 86;     // 1 second tail after last noteOff
    constexpr int kTotalBlocks = kBlocksPerNote * kNumNotes + kTailBlocks;

    const int16_t notes[kNumNotes] = {60, 62, 64, 65};  // C4, D4, E4, F4

    // Storage for per-block metrics
    struct BlockMetrics {
        int blockIndex;
        float timeSeconds;
        float rmsL;
        float rmsR;
        float rmsMono;  // max(L, R)
        float peakL;
        float peakR;
        float peakMono;
        const char* event;  // "noteOn X" or "noteOff X" or ""
    };
    std::vector<BlockMetrics> timeline;
    timeline.reserve(static_cast<size_t>(kTotalBlocks));

    // Per-note peak and RMS tracking
    struct NoteMetrics {
        float peakMax = 0.0f;
        float rmsMax = 0.0f;
        // Track RMS in first half vs second half of the note
        float rmsMaxFirstHalf = 0.0f;
        float rmsMaxSecondHalf = 0.0f;
        // Track if there's a boost (RMS in second half > first half)
        bool hasBoost = false;
    };
    NoteMetrics noteMetrics[kNumNotes];

    int globalBlock = 0;

    auto processOneBlock = [&](const char* eventLabel) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        events.clear();

        float rmsL = computeRMS(outL.data(), kBlockSize);
        float rmsR = computeRMS(outR.data(), kBlockSize);
        float peakL = findPeak(outL.data(), kBlockSize);
        float peakR = findPeak(outR.data(), kBlockSize);

        BlockMetrics bm{};
        bm.blockIndex = globalBlock;
        bm.timeSeconds = static_cast<float>(globalBlock * kBlockSize) /
                         static_cast<float>(kSampleRate);
        bm.rmsL = rmsL;
        bm.rmsR = rmsR;
        bm.rmsMono = std::max(rmsL, rmsR);
        bm.peakL = peakL;
        bm.peakR = peakR;
        bm.peakMono = std::max(peakL, peakR);
        bm.event = eventLabel;
        timeline.push_back(bm);

        ++globalBlock;
    };

    // -------------------------------------------------------------------------
    // Play the sequence
    // -------------------------------------------------------------------------
    for (int n = 0; n < kNumNotes; ++n) {
        // NoteOff for previous note + NoteOn for current note on the same block
        if (n > 0) {
            events.addNoteOff(notes[n - 1], 0);
        }
        events.addNoteOn(notes[n], 0.8f, 0);

        char label[32];
        if (n > 0) {
            snprintf(label, sizeof(label), "off %d + on %d", notes[n - 1], notes[n]);
        } else {
            snprintf(label, sizeof(label), "on %d", notes[n]);
        }

        // First block of this note has the event
        processOneBlock(label);

        // Remaining blocks of this note (no events)
        for (int b = 1; b < kBlocksPerNote; ++b) {
            processOneBlock("");
        }
    }

    // NoteOff for last note
    events.addNoteOff(notes[kNumNotes - 1], 0);
    {
        char label[32];
        snprintf(label, sizeof(label), "off %d", notes[kNumNotes - 1]);
        processOneBlock(label);
    }

    // Tail (release phase)
    for (int b = 1; b < kTailBlocks; ++b) {
        processOneBlock("");
    }

    // -------------------------------------------------------------------------
    // Compute per-note metrics
    // -------------------------------------------------------------------------
    for (int n = 0; n < kNumNotes; ++n) {
        int startBlock = n * kBlocksPerNote;
        int endBlock = (n + 1) * kBlocksPerNote;
        int midBlock = startBlock + kBlocksPerNote / 2;

        for (int b = startBlock; b < endBlock && b < static_cast<int>(timeline.size()); ++b) {
            float rms = timeline[static_cast<size_t>(b)].rmsMono;
            float peak = timeline[static_cast<size_t>(b)].peakMono;
            noteMetrics[n].peakMax = std::max(noteMetrics[n].peakMax, peak);
            noteMetrics[n].rmsMax = std::max(noteMetrics[n].rmsMax, rms);
            if (b < midBlock) {
                noteMetrics[n].rmsMaxFirstHalf = std::max(noteMetrics[n].rmsMaxFirstHalf, rms);
            } else {
                noteMetrics[n].rmsMaxSecondHalf = std::max(noteMetrics[n].rmsMaxSecondHalf, rms);
            }
        }
        // A "boost" is when the second half is noticeably louder than the first half
        noteMetrics[n].hasBoost =
            (noteMetrics[n].rmsMaxSecondHalf > noteMetrics[n].rmsMaxFirstHalf * 1.5f);
    }

    // -------------------------------------------------------------------------
    // Print detailed timeline (every 4th block to keep output manageable)
    // -------------------------------------------------------------------------
    WARN("=== Cathedral_Organ Envelope Analysis ===");
    WARN("Preset: Cathedral_Organ | Notes: C4 D4 E4 F4 | Duration: ~1s each | Vel: 0.8");
    WARN("Block size: " << kBlockSize << " | Sample rate: " << kSampleRate);
    WARN("");
    WARN("--- Per-Block RMS Timeline (every 4 blocks = ~46ms) ---");
    WARN("Block | Time(s) | RMS(dBFS) | Peak(dBFS) | Event");
    WARN("------|---------|-----------|------------|------");

    for (size_t i = 0; i < timeline.size(); ++i) {
        // Print every 4th block, plus any block with an event
        bool hasEvent = (timeline[i].event[0] != '\0');
        if (i % 4 == 0 || hasEvent) {
            char line[128];
            snprintf(line, sizeof(line), "%5d | %7.3f | %9.1f | %10.1f | %s",
                     timeline[i].blockIndex,
                     timeline[i].timeSeconds,
                     toDBFS(timeline[i].rmsMono),
                     toDBFS(timeline[i].peakMono),
                     timeline[i].event);
            WARN(line);
        }
    }

    // -------------------------------------------------------------------------
    // Print per-note summary
    // -------------------------------------------------------------------------
    WARN("");
    WARN("--- Per-Note Summary ---");
    for (int n = 0; n < kNumNotes; ++n) {
        char line[256];
        snprintf(line, sizeof(line),
            "Note %d (MIDI %d): Peak %.1f dBFS | RMS max %.1f dBFS | "
            "1st half RMS max %.1f dBFS | 2nd half RMS max %.1f dBFS | Boost: %s",
            n + 1, notes[n],
            toDBFS(noteMetrics[n].peakMax),
            toDBFS(noteMetrics[n].rmsMax),
            toDBFS(noteMetrics[n].rmsMaxFirstHalf),
            toDBFS(noteMetrics[n].rmsMaxSecondHalf),
            noteMetrics[n].hasBoost ? "YES" : "no");
        WARN(line);
    }

    // -------------------------------------------------------------------------
    // Print a simple ASCII "graph" of the amplitude envelope
    // -------------------------------------------------------------------------
    WARN("");
    WARN("--- Amplitude Envelope (ASCII graph, per block) ---");
    WARN("Each char = ~11.6ms (1 block). '|' = note boundary. Scale: 0 to max peak.");

    float maxRMS = 0.0f;
    for (const auto& bm : timeline) {
        maxRMS = std::max(maxRMS, bm.rmsMono);
    }

    if (maxRMS > 0.0f) {
        std::string graphLine;
        graphLine.reserve(timeline.size());

        for (size_t i = 0; i < timeline.size(); ++i) {
            // Mark note boundaries
            if (i > 0 && i % static_cast<size_t>(kBlocksPerNote) == 0 &&
                i < static_cast<size_t>(kBlocksPerNote * kNumNotes + 1)) {
                graphLine += '|';
            } else {
                float normalized = timeline[i].rmsMono / maxRMS;
                if (normalized < 0.05f) graphLine += ' ';
                else if (normalized < 0.15f) graphLine += '.';
                else if (normalized < 0.30f) graphLine += ':';
                else if (normalized < 0.50f) graphLine += '+';
                else if (normalized < 0.70f) graphLine += '#';
                else if (normalized < 0.85f) graphLine += '@';
                else graphLine += 'X';
            }
        }
        WARN(graphLine);
        WARN("Legend: ' '=silence .=quiet :=low +=mid #=high @=loud X=peak |=note boundary");
    }

    // -------------------------------------------------------------------------
    // Diagnostics check: flag unexpected boost patterns
    // -------------------------------------------------------------------------
    WARN("");
    bool anyBoost = false;
    for (int n = 0; n < kNumNotes; ++n) {
        if (noteMetrics[n].hasBoost) {
            WARN("WARNING: Note " << (n + 1) << " (MIDI " << notes[n]
                 << ") shows a volume boost in the second half!");
            anyBoost = true;
        }
    }
    if (!anyBoost) {
        WARN("No obvious volume boost detected in any note's second half.");
        WARN("The issue may be more subtle — check the timeline for gradual increases.");
    }

    // This test is diagnostic, not prescriptive. We just require output.
    CHECK(noteMetrics[0].peakMax > 0.0f);

    processor.setActive(false);
    processor.terminate();
}
