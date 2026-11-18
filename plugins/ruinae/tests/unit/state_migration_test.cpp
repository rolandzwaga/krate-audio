// ==============================================================================
// Unit Test: State Migration and Forward Compatibility
// ==============================================================================
// Verifies that unknown future versions and truncated streams are handled
// safely with fail-closed defaults.
//
// Reference: specs/045-plugin-shell/spec.md FR-017
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/lfo_params.h"
#include "drain_preset_transfer.h"

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/modulation_types.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

// =============================================================================
// Helper: create and initialize a Processor
// =============================================================================

static std::unique_ptr<Ruinae::Processor> makeProcessor() {
    auto p = std::make_unique<Ruinae::Processor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Unknown future version loads with defaults", "[state][migration]") {
    // Create a stream with version 999 followed by garbage
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    streamer.writeInt32(999); // Unknown future version
    streamer.writeFloat(42.0f); // Some data that should be ignored
    streamer.writeFloat(99.0f);

    auto proc = makeProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Should return kResultTrue (fail closed with safe defaults)
    auto result = proc->setState(&stream);
    REQUIRE(result == Steinberg::kResultTrue);
    drainPresetTransfer(proc.get());

    // Verify the processor still works (save state and check it's valid)
    Steinberg::MemoryStream outStream;
    auto saveResult = proc->getState(&outStream);
    REQUIRE(saveResult == Steinberg::kResultTrue);

    // The saved state should have the current version
    outStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer outStreamer(&outStream, kLittleEndian);
    Steinberg::int32 savedVersion = 0;
    REQUIRE(outStreamer.readInt32(savedVersion));
    REQUIRE(savedVersion == Ruinae::kCurrentStateVersion);

    proc->terminate();
}

TEST_CASE("Empty stream loads with defaults", "[state][migration]") {
    Steinberg::MemoryStream emptyStream;

    auto proc = makeProcessor();

    // Should return kResultTrue (empty stream, keep defaults)
    auto result = proc->setState(&emptyStream);
    REQUIRE(result == Steinberg::kResultTrue);
    drainPresetTransfer(proc.get());

    // Processor should still be functional
    Steinberg::MemoryStream outStream;
    proc->getState(&outStream);

    Steinberg::int64 size = 0;
    outStream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    REQUIRE(size > 4); // Should have version + defaults

    proc->terminate();
}

TEST_CASE("Truncated v1 stream loads partial defaults", "[state][migration]") {
    // Create a v1 stream that's truncated after just the global params
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    streamer.writeInt32(1); // version
    // Write only global params (4 values) then stop
    streamer.writeFloat(1.5f);  // masterGain
    streamer.writeInt32(0);     // voiceMode (Poly)
    streamer.writeInt32(4);     // polyphony
    streamer.writeInt32(1);     // softLimit (true)
    // Stream ends here -- rest of packs are missing

    auto proc = makeProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Should return kResultTrue (truncated but handled gracefully)
    auto result = proc->setState(&stream);
    REQUIRE(result == Steinberg::kResultTrue);
    drainPresetTransfer(proc.get());

    proc->terminate();
}

TEST_CASE("Version 1 state loads without midiOut field", "[state][migration][version]") {
    // Save a default-state v2 stream from a fresh processor
    auto proc = makeProcessor();
    Steinberg::MemoryStream v2Stream;
    REQUIRE(proc->getState(&v2Stream) == Steinberg::kResultTrue);

    // Read the full v2 data
    Steinberg::int64 v2Size = 0;
    v2Stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &v2Size);
    std::vector<uint8_t> v2Data(static_cast<size_t>(v2Size));
    v2Stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::int32 bytesRead = 0;
    v2Stream.read(v2Data.data(), static_cast<Steinberg::int32>(v2Size), &bytesRead);

    // Patch version from 2 to 1 (first 4 bytes, little-endian int32)
    Steinberg::int32 v1 = 1;
    std::memcpy(v2Data.data(), &v1, sizeof(v1));

    // Truncate last 4 bytes (the midiOut int32 that v1 wouldn't have)
    auto v1Size = v2Data.size() - 4;
    Steinberg::MemoryStream v1Stream;
    Steinberg::int32 bytesWritten = 0;
    v1Stream.write(v2Data.data(), static_cast<Steinberg::int32>(v1Size), &bytesWritten);
    v1Stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Load the v1 stream into a fresh processor
    auto proc2 = makeProcessor();
    auto result = proc2->setState(&v1Stream);
    REQUIRE(result == Steinberg::kResultTrue);
    drainPresetTransfer(proc2.get());

    // Verify the processor still works by saving and checking version
    Steinberg::MemoryStream outStream;
    REQUIRE(proc2->getState(&outStream) == Steinberg::kResultTrue);

    outStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer outStreamer(&outStream, kLittleEndian);
    Steinberg::int32 savedVersion = 0;
    REQUIRE(outStreamer.readInt32(savedVersion));
    REQUIRE(savedVersion == Ruinae::kCurrentStateVersion);

    proc->terminate();
    proc2->terminate();
}

TEST_CASE("setState does not crash on any stream content", "[state][migration]") {
    auto proc = makeProcessor();

    // Random garbage data
    Steinberg::MemoryStream garbageStream;
    Steinberg::IBStreamer streamer(&garbageStream, kLittleEndian);
    streamer.writeInt32(1); // valid version
    // Write some random floats (less than full state)
    for (int i = 0; i < 5; ++i) {
        streamer.writeFloat(static_cast<float>(i) * 0.1f);
    }

    garbageStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    auto result = proc->setState(&garbageStream);
    REQUIRE(result == Steinberg::kResultTrue);
    drainPresetTransfer(proc.get());

    proc->terminate();
}

// =============================================================================
// R-147: a corrupt OSC PhaseMod in a state must not hang the audio thread
// =============================================================================

namespace {

std::vector<uint8_t> streamBytes(Steinberg::MemoryStream& stream) {
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::int32 bytesRead = 0;
    stream.read(bytes.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return bytes;
}

/// Saves the state of a fresh processor after one block that sets `paramId` to `normalized`.
std::vector<uint8_t> stateAfterParam(Steinberg::Vst::ParamID paramId, double normalized) {
    auto proc = makeProcessor();
    Krate::Test::ParameterChanges changes;
    changes.addChange(paramId, normalized);
    Steinberg::Vst::ProcessData data{};
    data.numSamples = 0;
    data.inputParameterChanges = &changes;
    proc->process(data);

    Steinberg::MemoryStream out;
    REQUIRE(proc->getState(&out) == Steinberg::kResultTrue);
    auto bytes = streamBytes(out);
    proc->terminate();
    return bytes;
}

float floatAt(const std::vector<uint8_t>& bytes, size_t offset) {
    float v = 0.0f;
    std::memcpy(&v, bytes.data() + offset, sizeof(v));
    return v;
}

}  // namespace

TEST_CASE("setState clamps out-of-range oscillator floats", "[state][robustness]") {
    // Locate OSC B PhaseMod in the stream: normalized 0.25 stores -0.5, 0.75 stores +0.5.
    const auto blobLo = stateAfterParam(Ruinae::kOscBPhaseModId, 0.25);
    const auto blobHi = stateAfterParam(Ruinae::kOscBPhaseModId, 0.75);
    REQUIRE(blobLo.size() == blobHi.size());
    size_t phaseModOffset = 0;
    int matches = 0;
    for (size_t i = 4; i + sizeof(float) <= blobLo.size(); ++i) {
        if (floatAt(blobLo, i) == -0.5f && floatAt(blobHi, i) == 0.5f) {
            phaseModOffset = i;
            ++matches;
        }
    }
    REQUIRE(matches == 1);

    // A corrupt blob: OSC B PhaseMod = 1e30 (a finite float, so no NaN/Inf guard catches it).
    auto corrupt = blobLo;
    const float huge = 1e30f;
    std::memcpy(corrupt.data() + phaseModOffset, &huge, sizeof(huge));

    constexpr Steinberg::int32 kBlock = 512;
    auto proc = makeProcessor();
    REQUIRE(proc->setActive(true) == Steinberg::kResultTrue);
    Steinberg::MemoryStream in;
    Steinberg::int32 written = 0;
    in.write(corrupt.data(), static_cast<Steinberg::int32>(corrupt.size()), &written);
    in.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc->setState(&in) == Steinberg::kResultTrue);
    drainPresetTransfer(proc.get());

    std::vector<float> outL(static_cast<size_t>(kBlock), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlock), 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channels;
    Krate::Test::EventList events;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = kBlock;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputEvents = &events;

    // On the unfixed code the first block with a sounding voice never returns.
    int nonFinite = 0;
    float peak = 0.0f;
    for (int b = 0; b < 10; ++b) {
        events.clear();
        if (b == 0) events.addNoteOn(48, 0.8f);  // C3, held
        proc->process(data);
        for (const float* ch : {outL.data(), outR.data()}) {
            for (Steinberg::int32 i = 0; i < kBlock; ++i) {
                if (!Krate::DSP::detail::isFinite(ch[i])) {
                    ++nonFinite;
                } else {
                    peak = std::max(peak, std::fabs(ch[i]));
                }
            }
        }
    }
    CHECK(nonFinite == 0);
    CHECK(peak <= 1.0f);

    Steinberg::MemoryStream out;
    REQUIRE(proc->getState(&out) == Steinberg::kResultTrue);
    const auto saved = streamBytes(out);
    REQUIRE(saved.size() == corrupt.size());
    const float loadedPhaseMod = floatAt(saved, phaseModOffset);
    CHECK(loadedPhaseMod >= -1.0f);
    CHECK(loadedPhaseMod <= 1.0f);

    proc->setActive(static_cast<Steinberg::TBool>(false));
    proc->terminate();
}

TEST_CASE("setState of the audit's garbage stream returns within one block", "[state][robustness]") {
    // The audit probe "state-garbage:2" (f:/tmp/ruinae-audit/garbage2.bin): 2000 bytes from
    // mt19937 seed 1234 (third draw), with a valid version-8 header. Its OSC B PhaseMod bytes
    // decode to a huge finite float, and the first block after a note-on never returned.
    std::mt19937 rng(1234);
    std::vector<uint8_t> junk;
    for (int g = 0; g <= 2; ++g) {
        junk.assign(static_cast<size_t>(200) + (static_cast<size_t>(900) * static_cast<size_t>(g)), 0);
        for (auto& c : junk) c = static_cast<uint8_t>(rng() & 0xFF);
    }
    const Steinberg::int32 version = 8;
    std::memcpy(junk.data(), &version, sizeof(version));

    constexpr Steinberg::int32 kBlock = 512;
    auto proc = makeProcessor();
    REQUIRE(proc->setActive(true) == Steinberg::kResultTrue);
    Steinberg::MemoryStream in;
    Steinberg::int32 written = 0;
    in.write(junk.data(), static_cast<Steinberg::int32>(junk.size()), &written);
    in.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc->setState(&in);  // the verdict on a garbage stream is not this case's subject
    drainPresetTransfer(proc.get());

    std::vector<float> outL(static_cast<size_t>(kBlock), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlock), 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channels;
    Krate::Test::EventList events;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = kBlock;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputEvents = &events;

    int blocks = 0;
    for (int b = 0; b < 10; ++b) {
        events.clear();
        if (b == 0) events.addNoteOn(48, 0.8f);
        proc->process(data);
        ++blocks;
    }
    CHECK(blocks == 10);

    proc->setActive(static_cast<Steinberg::TBool>(false));
    proc->terminate();
}

// =============================================================================
// R-170..R-174: a NaN float in a state must not reach the DSP
// =============================================================================

namespace {

using ParamList = std::vector<std::pair<Steinberg::Vst::ParamID, double>>;

/// Saves the state of a fresh processor after one block that applies `changes`.
std::vector<uint8_t> stateWithChanges(const ParamList& changes) {
    auto proc = makeProcessor();
    Krate::Test::ParameterChanges pc;
    for (const auto& [id, value] : changes) pc.addChange(id, value);
    Steinberg::Vst::ProcessData data{};
    data.numSamples = 0;
    data.inputParameterChanges = &pc;
    proc->process(data);

    Steinberg::MemoryStream out;
    REQUIRE(proc->getState(&out) == Steinberg::kResultTrue);
    auto bytes = streamBytes(out);
    proc->terminate();
    return bytes;
}

/// The one stream offset whose float reads `storedA` after `base` + {id, normA} and
/// `storedB` after `base` + {id, normB}.
size_t locateFloat(const ParamList& base, Steinberg::Vst::ParamID id,
                   double normA, float storedA, double normB, float storedB) {
    ParamList a = base;
    a.emplace_back(id, normA);
    ParamList b = base;
    b.emplace_back(id, normB);
    const auto blobA = stateWithChanges(a);
    const auto blobB = stateWithChanges(b);
    REQUIRE(blobA.size() == blobB.size());
    size_t offset = 0;
    int matches = 0;
    for (size_t i = 4; i + sizeof(float) <= blobA.size(); ++i) {
        if (floatAt(blobA, i) == storedA && floatAt(blobB, i) == storedB) {
            offset = i;
            ++matches;
        }
    }
    INFO("param " << id);
    REQUIRE(matches == 1);
    return offset;
}

struct RenderResult {
    int nonFinite = 0;
    float peak = 0.0f;
};

/// Holds a C major chord from the first block and renders `blocks` blocks with the
/// transport playing at 120 BPM.
RenderResult renderChord(Ruinae::Processor& proc, int blocks) {
    constexpr Steinberg::int32 kBlock = 512;
    std::vector<float> outL(static_cast<size_t>(kBlock), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlock), 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channels;
    Krate::Test::EventList events;
    Steinberg::Vst::ProcessContext ctx{};
    ctx.state = Steinberg::Vst::ProcessContext::kPlaying | Steinberg::Vst::ProcessContext::kTempoValid |
                Steinberg::Vst::ProcessContext::kTimeSigValid;
    ctx.tempo = 120.0;
    ctx.timeSigNumerator = 4;
    ctx.timeSigDenominator = 4;
    ctx.sampleRate = 44100.0;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = kBlock;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputEvents = &events;
    data.processContext = &ctx;

    RenderResult r;
    for (int b = 0; b < blocks; ++b) {
        events.clear();
        if (b == 0) {
            events.addNoteOn(48, 0.8f);
            events.addNoteOn(52, 0.8f);
            events.addNoteOn(55, 0.8f);
        }
        proc.process(data);
        for (const float* ch : {outL.data(), outR.data()}) {
            for (Steinberg::int32 i = 0; i < kBlock; ++i) {
                if (!Krate::DSP::detail::isFinite(ch[i])) {
                    ++r.nonFinite;
                } else {
                    r.peak = std::max(r.peak, std::fabs(ch[i]));
                }
            }
        }
        ctx.projectTimeSamples += kBlock;
        ctx.projectTimeMusic += static_cast<double>(kBlock) / 44100.0 * 2.0;
    }
    return r;
}

void loadBlob(Ruinae::Processor& proc, std::vector<uint8_t> blob) {
    Steinberg::MemoryStream in;
    Steinberg::int32 written = 0;
    in.write(blob.data(), static_cast<Steinberg::int32>(blob.size()), &written);
    in.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc.setState(&in) == Steinberg::kResultTrue);
    drainPresetTransfer(&proc);
}

}  // namespace

TEST_CASE("setState sanitises non-finite floats", "[state][robustness]") {
    using namespace Ruinae;
    // Every field below reaches DSP state that a NaN latches: LFO 1 routed to the
    // global filter cutoff (global filter on), the Hall reverb, the filter envelope's
    // Bezier attack curve, the trance gate, and the free-running MIDI arp.
    const double srcLfo1 = static_cast<double>(static_cast<int>(Krate::DSP::ModSource::LFO1)) /
                           static_cast<double>(kModSourceCount - 1);
    const ParamList base = {
        {kGlobalFilterEnabledId, 1.0},
        {kGlobalFilterCutoffId, 0.5},
        {kModMatrixSlot0SourceId, srcLfo1},
        {kModMatrixSlot0DestId, 0.0},     // Global Filter Cutoff
        {kModMatrixSlot0AmountId, 0.75},  // +0.5
        {kLFO1SyncId, 0.0},
        {kReverbEnabledId, 1.0},
        {kReverbTypeId, 1.0},  // Hall
        {static_cast<Steinberg::Vst::ParamID>(kFilterEnvBaseId + 7), 1.0},  // Bezier on
        {kTranceGateEnabledId, 1.0},
        {kArpOperatingModeId, 1.0 / 3.0},  // MIDI
        {kArpTempoSyncId, 0.0},
    };

    struct Field {
        const char* name;
        size_t offset;
    };
    const std::vector<Field> fields = {
        {.name = "LFO 1 rate",
         .offset = locateFloat(base, kLFO1RateId, 0.25, lfoRateFromNormalized(0.25), 0.75,
                               lfoRateFromNormalized(0.75))},
        {.name = "reverb size", .offset = locateFloat(base, kReverbSizeId, 0.25, 0.25f, 0.75, 0.75f)},
        {.name = "filter env attack Bezier cp1 y",
         .offset = locateFloat(base, static_cast<Steinberg::Vst::ParamID>(kFilterEnvBaseId + 11), 0.25,
                               0.25f, 0.75, 0.75f)},
        {.name = "trance gate depth",
         .offset = locateFloat(base, kTranceGateDepthId, 0.25, 0.25f, 0.75, 0.75f)},
        {.name = "arp free rate", .offset = locateFloat(base, kArpFreeRateId, 0.0, 0.5f, 1.0, 50.0f)},
        {.name = "arp velocity lane speed",
         .offset = locateFloat(base, kArpVelocityLaneSpeedId, 0.0, 0.25f, 1.0, 4.0f)},
    };

    const auto clean = stateWithChanges(base);
    constexpr std::uint32_t kNaNBits = 0x7FC00000u;
    constexpr int kBlocks = 87;  // ~1 s at 44.1 kHz

    // Each field alone, then all of them at once.
    for (size_t pick = 0; pick <= fields.size(); ++pick) {
        const bool all = pick == fields.size();
        DYNAMIC_SECTION("NaN in " << (all ? "every field" : fields[pick].name)) {
            auto corrupt = clean;
            for (size_t f = 0; f < fields.size(); ++f) {
                if (all || f == pick) {
                    std::memcpy(corrupt.data() + fields[f].offset, &kNaNBits, sizeof(kNaNBits));
                }
            }

            auto proc = makeProcessor();
            REQUIRE(proc->setActive(true) == Steinberg::kResultTrue);
            loadBlob(*proc, corrupt);

            // The loaded parameters are finite (getState writes the stored atomics back).
            Steinberg::MemoryStream out;
            REQUIRE(proc->getState(&out) == Steinberg::kResultTrue);
            const auto saved = streamBytes(out);
            REQUIRE(saved.size() == corrupt.size());
            for (const auto& field : fields) {
                INFO(field.name << " loads as " << floatAt(saved, field.offset));
                CHECK(Krate::DSP::detail::isFinite(floatAt(saved, field.offset)));
            }

            const auto first = renderChord(*proc, kBlocks);
            CHECK(first.nonFinite == 0);
            CHECK(first.peak > 1e-3f);

            // A clean state afterwards plays normally: nothing stayed stuck-silent.
            loadBlob(*proc, clean);
            const auto second = renderChord(*proc, kBlocks);
            CHECK(second.nonFinite == 0);
            CHECK(second.peak > 1e-3f);

            proc->setActive(static_cast<Steinberg::TBool>(false));
            proc->terminate();
        }
    }
}

// =============================================================================
// R-182: Spectral Shift had no audio path before state v9. Older states stored
// 0.0 for a knob shown at 50 %; they load the centre (no shift), their sound.
// =============================================================================

namespace {

/// The shift the processor stores after loading `blob` (setState + snapshot drain).
float processorShiftAfterLoad(const std::vector<uint8_t>& blob, size_t shiftOffset) {
    auto proc = makeProcessor();
    loadBlob(*proc, blob);
    Steinberg::MemoryStream out;
    REQUIRE(proc->getState(&out) == Steinberg::kResultTrue);
    const auto saved = streamBytes(out);
    proc->terminate();
    REQUIRE(saved.size() > shiftOffset + sizeof(float));
    return floatAt(saved, shiftOffset);
}

/// The normalized Spectral Shift the controller shows after setComponentState(blob).
double controllerShiftAfterLoad(std::vector<uint8_t> blob) {
    auto* ctrl = new Ruinae::Controller();
    REQUIRE(ctrl->initialize(nullptr) == Steinberg::kResultTrue);
    Steinberg::MemoryStream in;
    Steinberg::int32 written = 0;
    in.write(blob.data(), static_cast<Steinberg::int32>(blob.size()), &written);
    in.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&in) == Steinberg::kResultTrue);
    const double value = ctrl->getParamNormalized(Ruinae::kMixerShiftId);
    ctrl->terminate();
    ctrl->release();
    return value;
}

}  // namespace

TEST_CASE("State v8 loads spectral shift as 0.5", "[state][migration]") {
    using namespace Ruinae;
    const size_t shiftOffset = locateFloat({}, kMixerShiftId, 0.25, 0.25f, 0.75, 0.75f);

    for (const float stored : {0.0f, 0.7f}) {
        DYNAMIC_SECTION("version 8 storing " << stored) {
            auto v8 = stateWithChanges({});
            const Steinberg::int32 version8 = 8;
            std::memcpy(v8.data(), &version8, sizeof(version8));
            std::memcpy(v8.data() + shiftOffset, &stored, sizeof(stored));

            CHECK(processorShiftAfterLoad(v8, shiftOffset) == 0.5f);
            CHECK(controllerShiftAfterLoad(v8) == 0.5);
        }
    }

    SECTION("the current version keeps the stored shift") {
        const auto current = stateWithChanges({{kMixerShiftId, 0.25}});
        CHECK(processorShiftAfterLoad(current, shiftOffset) == 0.25f);
        CHECK(controllerShiftAfterLoad(current) == 0.25);
    }
}

// =============================================================================
// R-022: before state v9 the Formant Vowel was ignored and Morph alone set the
// vowel position. Since v9 Morph is an offset from the Vowel, so older states
// load Vowel A and keep their Morph: the position they were saved with.
// =============================================================================

namespace {

/// The normalized values the controller shows for `ids` after setComponentState(blob).
std::vector<double> controllerParamsAfterLoad(std::vector<uint8_t> blob,
                                              const std::vector<Steinberg::Vst::ParamID>& ids) {
    auto* ctrl = new Ruinae::Controller();
    REQUIRE(ctrl->initialize(nullptr) == Steinberg::kResultTrue);
    Steinberg::MemoryStream in;
    Steinberg::int32 written = 0;
    in.write(blob.data(), static_cast<Steinberg::int32>(blob.size()), &written);
    in.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&in) == Steinberg::kResultTrue);
    std::vector<double> values;
    for (const auto id : ids) values.push_back(ctrl->getParamNormalized(id));
    ctrl->terminate();
    ctrl->release();
    return values;
}

/// The processor's state after loading `blob` (setState + snapshot drain), saved again.
std::vector<uint8_t> processorStateAfterLoad(const std::vector<uint8_t>& blob) {
    auto proc = makeProcessor();
    loadBlob(*proc, blob);
    Steinberg::MemoryStream out;
    REQUIRE(proc->getState(&out) == Steinberg::kResultTrue);
    auto saved = streamBytes(out);
    proc->terminate();
    return saved;
}

}  // namespace

TEST_CASE("State v8 loads formant vowel as A and keeps morph", "[state][migration]") {
    using namespace Ruinae;
    const std::vector<Steinberg::Vst::ParamID> ids = {
        kOscAFormantVowelId, kOscAFormantMorphId, kOscBFormantVowelId, kOscBFormantMorphId};
    constexpr double kVowelO = 3.0 / 4.0;
    constexpr double kMorph22 = 2.2 / 4.0;
    const auto saved = stateWithChanges({{kOscAFormantVowelId, kVowelO},
                                         {kOscAFormantMorphId, kMorph22},
                                         {kOscBFormantVowelId, kVowelO},
                                         {kOscBFormantMorphId, kMorph22}});

    SECTION("version 8: vowel A, morph 2.2 on the processor and the controller") {
        auto v8 = saved;
        const Steinberg::int32 version8 = 8;
        std::memcpy(v8.data(), &version8, sizeof(version8));

        // The processor's loaded atomics, read back through a current-version save.
        const auto fromProcessor = controllerParamsAfterLoad(processorStateAfterLoad(v8), ids);
        const auto fromController = controllerParamsAfterLoad(v8, ids);
        for (const auto& values : {fromProcessor, fromController}) {
            CHECK(values[0] == 0.0);
            CHECK(std::abs(values[1] - kMorph22) < 1e-6);
            CHECK(values[2] == 0.0);
            CHECK(std::abs(values[3] - kMorph22) < 1e-6);
        }
    }

    SECTION("the current version keeps vowel O") {
        const auto fromProcessor = controllerParamsAfterLoad(processorStateAfterLoad(saved), ids);
        const auto fromController = controllerParamsAfterLoad(saved, ids);
        for (const auto& values : {fromProcessor, fromController}) {
            CHECK(values[0] == kVowelO);
            CHECK(std::abs(values[1] - kMorph22) < 1e-6);
            CHECK(values[2] == kVowelO);
            CHECK(std::abs(values[3] - kMorph22) < 1e-6);
        }
    }
}
