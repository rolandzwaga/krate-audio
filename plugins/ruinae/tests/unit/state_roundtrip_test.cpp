// ==============================================================================
// Unit Test: State Round-Trip Persistence
// ==============================================================================
// Verifies that getState() followed by setState() on a new Processor
// preserves all parameter values within acceptable precision.
//
// Reference: specs/045-plugin-shell/spec.md FR-015, FR-016
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <vector>

using Catch::Approx;

#include <cstring>  // std::memcmp

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
// Helpers for parameter injection
// =============================================================================

// Expose processParameterChanges for testing
class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

static std::unique_ptr<TestableProcessor> makeTestableProcessor() {
    auto p = std::make_unique<TestableProcessor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

class SingleParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    SingleParamQueue(Steinberg::Vst::ParamID id, double value)
        : paramId_(id), value_(value) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }

    Steinberg::tresult PLUGIN_API getPoint(
        Steinberg::int32 index,
        Steinberg::int32& sampleOffset,
        Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(
        Steinberg::int32, Steinberg::Vst::ParamValue,
        Steinberg::int32&) override {
        return Steinberg::kResultFalse;
    }

private:
    Steinberg::Vst::ParamID paramId_;
    double value_;
};

class ParamChangeBatch : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override {
        return nullptr;
    }

    void add(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

private:
    std::vector<SingleParamQueue> queues_;
};

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("State round-trip preserves default values", "[state][roundtrip]") {
    auto proc1 = makeProcessor();

    // Save state from default processor
    Steinberg::MemoryStream stream;
    auto saveResult = proc1->getState(&stream);
    REQUIRE(saveResult == Steinberg::kResultTrue);

    // Load into a fresh processor
    auto proc2 = makeProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    auto loadResult = proc2->setState(&stream);
    REQUIRE(loadResult == Steinberg::kResultTrue);

    // Save again from proc2
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);

    // Both streams should be identical (byte-for-byte)
    Steinberg::int64 size1 = 0;
    Steinberg::int64 size2 = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size1);
    stream2.seek(0, Steinberg::IBStream::kIBSeekEnd, &size2);
    REQUIRE(size1 == size2);
    REQUIRE(size1 > 4); // At least version + some data

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("State version is written first", "[state][roundtrip]") {
    auto proc = makeProcessor();

    Steinberg::MemoryStream stream;
    proc->getState(&stream);

    // Read the first int32 -- should be current state version
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    Steinberg::int32 version = 0;
    REQUIRE(streamer.readInt32(version));
    REQUIRE(version == Ruinae::kCurrentStateVersion);

    proc->terminate();
}

TEST_CASE("State round-trip byte equivalence", "[state][roundtrip]") {
    // Verify that save -> load -> save produces identical bytes
    auto proc1 = makeProcessor();

    // First save
    Steinberg::MemoryStream stream1;
    proc1->getState(&stream1);

    // Load into proc2
    auto proc2 = makeProcessor();
    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc2->setState(&stream1);

    // Second save from proc2
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);

    // Compare byte-by-byte
    Steinberg::int64 size1 = 0;
    Steinberg::int64 size2 = 0;
    stream1.seek(0, Steinberg::IBStream::kIBSeekEnd, &size1);
    stream2.seek(0, Steinberg::IBStream::kIBSeekEnd, &size2);
    REQUIRE(size1 == size2);

    // Read all bytes
    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    stream2.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    std::vector<char> data1(static_cast<size_t>(size1));
    std::vector<char> data2(static_cast<size_t>(size2));

    Steinberg::int32 bytesRead1 = 0;
    Steinberg::int32 bytesRead2 = 0;
    stream1.read(data1.data(), static_cast<Steinberg::int32>(size1), &bytesRead1);
    stream2.read(data2.data(), static_cast<Steinberg::int32>(size2), &bytesRead2);

    REQUIRE(bytesRead1 == bytesRead2);
    REQUIRE(data1 == data2);

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("State round-trip preserves non-default values", "[state][roundtrip]") {
    // This test catches the class of bug where getState writes version N
    // but setState has no handler for it -- all params silently revert to
    // defaults. With non-default values, the re-saved stream differs.
    auto proc1 = makeTestableProcessor();

    // Set non-default values across multiple parameter packs.
    // Normalized values at the VST boundary (0.0 to 1.0).
    ParamChangeBatch changes;
    // Global: master gain
    changes.add(Ruinae::kMasterGainId, 0.75);
    // OSC A: type = Noise (index 9, normalized = 9/9 = 1.0)
    changes.add(Ruinae::kOscATypeId, 1.0);
    // OSC A: level
    changes.add(Ruinae::kOscALevelId, 0.6);
    // OSC B: type = Additive (index 3, normalized = 3/9)
    changes.add(Ruinae::kOscBTypeId, 3.0 / 9.0);
    // Mixer: position full B
    changes.add(Ruinae::kMixerPositionId, 0.9);
    // Mixer: shift
    changes.add(Ruinae::kMixerShiftId, 0.7);
    // Filter: cutoff
    changes.add(Ruinae::kFilterCutoffId, 0.8);
    // Amp envelope: attack
    changes.add(Ruinae::kAmpEnvAttackId, 0.4);
    proc1->processParameterChanges(&changes);

    // Save state
    Steinberg::MemoryStream stream1;
    REQUIRE(proc1->getState(&stream1) == Steinberg::kResultTrue);

    // Load into a fresh processor
    auto proc2 = makeTestableProcessor();
    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc2->setState(&stream1) == Steinberg::kResultTrue);

    // Save again from proc2
    Steinberg::MemoryStream stream2;
    REQUIRE(proc2->getState(&stream2) == Steinberg::kResultTrue);

    // Byte-for-byte equivalence: if setState didn't restore the non-default
    // values, proc2 would save defaults and the streams would differ.
    Steinberg::int64 size1 = 0;
    Steinberg::int64 size2 = 0;
    stream1.seek(0, Steinberg::IBStream::kIBSeekEnd, &size1);
    stream2.seek(0, Steinberg::IBStream::kIBSeekEnd, &size2);
    REQUIRE(size1 == size2);

    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    stream2.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    std::vector<char> data1(static_cast<size_t>(size1));
    std::vector<char> data2(static_cast<size_t>(size2));

    Steinberg::int32 bytesRead1 = 0;
    Steinberg::int32 bytesRead2 = 0;
    stream1.read(data1.data(), static_cast<Steinberg::int32>(size1), &bytesRead1);
    stream2.read(data2.data(), static_cast<Steinberg::int32>(size2), &bytesRead2);

    REQUIRE(bytesRead1 == bytesRead2);
    REQUIRE(data1 == data2);

    proc1->terminate();
    proc2->terminate();
}

// =============================================================================
// Helpers: Stream byte extraction and comparison
// =============================================================================

/// Extract all bytes from a MemoryStream into a vector.
static std::vector<char> extractStreamBytes(Steinberg::MemoryStream& stream) {
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> data(static_cast<size_t>(size));
    Steinberg::int32 bytesRead = 0;
    stream.read(data.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return data;
}

/// Perform a save-load-save round-trip and return both saved byte vectors.
/// Returns {original_bytes, round_tripped_bytes}.
static std::pair<std::vector<char>, std::vector<char>>
roundTripState(Ruinae::Processor* proc) {
    Steinberg::MemoryStream stream1;
    REQUIRE(proc->getState(&stream1) == Steinberg::kResultTrue);

    auto proc2 = makeProcessor();
    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc2->setState(&stream1) == Steinberg::kResultTrue);

    Steinberg::MemoryStream stream2;
    REQUIRE(proc2->getState(&stream2) == Steinberg::kResultTrue);

    auto data1 = extractStreamBytes(stream1);
    auto data2 = extractStreamBytes(stream2);

    proc2->terminate();
    return {data1, data2};
}

// =============================================================================
// Arp State Round-Trip Tests (082-presets-polish, Phase 4)
// =============================================================================

TEST_CASE("Arp state round-trip preserves all lane values",
          "[state][roundtrip][arp state round-trip]") {
    auto proc1 = makeTestableProcessor();

    // Set all 6 lanes to non-default lengths and step values
    ParamChangeBatch changes;

    // Enable arp
    changes.add(Ruinae::kArpEnabledId, 1.0);
    // Mode = Down (1): normalized = 1/9
    changes.add(Ruinae::kArpModeId, 1.0 / 9.0);

    // --- Velocity lane: length=8, varied step values ---
    changes.add(Ruinae::kArpVelocityLaneLengthId, (8.0 - 1.0) / 31.0);
    for (int i = 0; i < 8; ++i) {
        double v = 0.1 + 0.1 * i;  // 0.1, 0.2, ..., 0.8
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpVelocityLaneStep0Id + i),
            v);
    }

    // --- Gate lane: length=6, varied step values ---
    changes.add(Ruinae::kArpGateLaneLengthId, (6.0 - 1.0) / 31.0);
    for (int i = 0; i < 6; ++i) {
        // Gate: normalized 0-1 -> 0.01-2.0, so normalized = (gate - 0.01) / 1.99
        double gate = 0.3 + 0.2 * i;  // raw: 0.3, 0.5, 0.7, 0.9, 1.1, 1.3
        double norm = (gate - 0.01) / 1.99;
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpGateLaneStep0Id + i),
            norm);
    }

    // --- Pitch lane: length=7, varied step values ---
    changes.add(Ruinae::kArpPitchLaneLengthId, (7.0 - 1.0) / 31.0);
    int pitchValues[] = {-12, -5, 0, 3, 7, 12, 24};
    for (int i = 0; i < 7; ++i) {
        double norm = (pitchValues[i] + 24.0) / 48.0;
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpPitchLaneStep0Id + i),
            norm);
    }

    // --- Modifier lane: length=4, varied bitmasks ---
    changes.add(Ruinae::kArpModifierLaneLengthId, (4.0 - 1.0) / 31.0);
    int modValues[] = {0x01, 0x05, 0x09, 0x0D};  // Active, Active+Slide, Active+Accent, All
    for (int i = 0; i < 4; ++i) {
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpModifierLaneStep0Id + i),
            modValues[i] / 255.0);
    }
    changes.add(Ruinae::kArpAccentVelocityId, 100.0 / 127.0);
    changes.add(Ruinae::kArpSlideTimeId, 80.0 / 500.0);  // 80ms

    // --- Ratchet lane: length=5, varied values ---
    changes.add(Ruinae::kArpRatchetLaneLengthId, (5.0 - 1.0) / 31.0);
    int ratchetValues[] = {1, 2, 3, 4, 2};
    for (int i = 0; i < 5; ++i) {
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpRatchetLaneStep0Id + i),
            (ratchetValues[i] - 1.0) / 3.0);
    }

    // --- Condition lane: length=8, varied conditions ---
    changes.add(Ruinae::kArpConditionLaneLengthId, (8.0 - 1.0) / 31.0);
    //  0=Always, 3=Prob50, 4=Prob75, 15=First, 16=Fill, 17=!Fill, 1=Prob10, 2=Prob25
    int condValues[] = {0, 3, 4, 15, 16, 17, 1, 2};
    for (int i = 0; i < 8; ++i) {
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpConditionLaneStep0Id + i),
            condValues[i] / 17.0);
    }
    changes.add(Ruinae::kArpFillToggleId, 1.0);  // Fill on

    proc1->processParameterChanges(&changes);

    // Also trigger dice to verify it does NOT get serialized (FR-015)
    {
        ParamChangeBatch diceChange;
        diceChange.add(Ruinae::kArpDiceTriggerId, 1.0);
        proc1->processParameterChanges(&diceChange);
    }

    // Round-trip: save -> load -> save
    auto [data1, data2] = roundTripState(proc1.get());
    REQUIRE(data1.size() == data2.size());
    REQUIRE(data1 == data2);

    // FR-015 regression guard: verify dice overlay is NOT restored.
    // Create a second processor with the SAME arp params but WITHOUT dice trigger.
    // Its saved state should be identical, proving dice isn't in the stream.
    auto proc3 = makeTestableProcessor();
    ParamChangeBatch changes2;
    changes2.add(Ruinae::kArpEnabledId, 1.0);
    changes2.add(Ruinae::kArpModeId, 1.0 / 9.0);
    changes2.add(Ruinae::kArpVelocityLaneLengthId, (8.0 - 1.0) / 31.0);
    for (int i = 0; i < 8; ++i) {
        changes2.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpVelocityLaneStep0Id + i),
            0.1 + 0.1 * i);
    }
    changes2.add(Ruinae::kArpGateLaneLengthId, (6.0 - 1.0) / 31.0);
    for (int i = 0; i < 6; ++i) {
        double gate = 0.3 + 0.2 * i;
        changes2.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpGateLaneStep0Id + i),
            (gate - 0.01) / 1.99);
    }
    changes2.add(Ruinae::kArpPitchLaneLengthId, (7.0 - 1.0) / 31.0);
    for (int i = 0; i < 7; ++i) {
        changes2.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpPitchLaneStep0Id + i),
            (pitchValues[i] + 24.0) / 48.0);
    }
    changes2.add(Ruinae::kArpModifierLaneLengthId, (4.0 - 1.0) / 31.0);
    for (int i = 0; i < 4; ++i) {
        changes2.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpModifierLaneStep0Id + i),
            modValues[i] / 255.0);
    }
    changes2.add(Ruinae::kArpAccentVelocityId, 100.0 / 127.0);
    changes2.add(Ruinae::kArpSlideTimeId, 80.0 / 500.0);
    changes2.add(Ruinae::kArpRatchetLaneLengthId, (5.0 - 1.0) / 31.0);
    for (int i = 0; i < 5; ++i) {
        changes2.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpRatchetLaneStep0Id + i),
            (ratchetValues[i] - 1.0) / 3.0);
    }
    changes2.add(Ruinae::kArpConditionLaneLengthId, (8.0 - 1.0) / 31.0);
    for (int i = 0; i < 8; ++i) {
        changes2.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpConditionLaneStep0Id + i),
            condValues[i] / 17.0);
    }
    changes2.add(Ruinae::kArpFillToggleId, 1.0);
    // NOTE: no dice trigger here
    proc3->processParameterChanges(&changes2);

    Steinberg::MemoryStream streamNoDice;
    proc3->getState(&streamNoDice);
    auto dataNoDice = extractStreamBytes(streamNoDice);

    // The streams from proc1 (with dice) and proc3 (no dice) should be identical
    // because diceTrigger is not serialized (FR-015)
    REQUIRE(data1.size() == dataNoDice.size());
    REQUIRE(data1 == dataNoDice);

    proc1->terminate();
    proc3->terminate();
}

TEST_CASE("Arp state round-trip preserves Euclidean settings",
          "[state][roundtrip][arp state round-trip]") {
    auto proc = makeTestableProcessor();

    ParamChangeBatch changes;
    changes.add(Ruinae::kArpEnabledId, 1.0);
    changes.add(Ruinae::kArpEuclideanEnabledId, 1.0);
    // Hits=5: normalized = 5/32
    changes.add(Ruinae::kArpEuclideanHitsId, 5.0 / 32.0);
    // Steps=13: normalized = (13-2)/30
    changes.add(Ruinae::kArpEuclideanStepsId, (13.0 - 2.0) / 30.0);
    // Rotation=3: normalized = 3/31
    changes.add(Ruinae::kArpEuclideanRotationId, 3.0 / 31.0);
    proc->processParameterChanges(&changes);

    auto [data1, data2] = roundTripState(proc.get());
    REQUIRE(data1.size() == data2.size());
    REQUIRE(data1 == data2);

    // Extra verification: load the state and re-save, then read the Euclidean
    // fields from the arp section to confirm they have the expected raw values.
    // The arp section starts at (totalSize - arpSectionSize).
    // We verify by reading the Euclidean fields from the stream.
    auto proc2 = makeProcessor();
    Steinberg::MemoryStream loadStream;
    loadStream.write(data1.data(), static_cast<Steinberg::int32>(data1.size()), nullptr);
    loadStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc2->setState(&loadStream);

    // Save and read the Euclidean section from the arp data.
    // Arp section is the last 888 bytes of the state.
    Steinberg::MemoryStream verifyStream;
    proc2->getState(&verifyStream);
    auto verifyData = extractStreamBytes(verifyStream);

    // Arp section layout: 11 base (44 bytes) + vel lane (132) + gate (132) +
    // pitch (132) + modifier (140) + ratchet (132) = 712 bytes before Euclidean.
    // Euclidean is 4 int32 = 16 bytes.
    // Arp section total: 888 bytes.
    // Euclidean offset within arp section: 712.
    constexpr size_t kArpSectionSize = 888;
    constexpr size_t kEuclideanOffsetInArp = 712;
    REQUIRE(verifyData.size() > kArpSectionSize);
    size_t arpStart = verifyData.size() - kArpSectionSize;
    size_t eucStart = arpStart + kEuclideanOffsetInArp;

    // Read the 4 Euclidean int32 values
    Steinberg::int32 eucEnabled = 0, eucHits = 0, eucSteps = 0, eucRotation = 0;
    std::memcpy(&eucEnabled, &verifyData[eucStart], 4);
    std::memcpy(&eucHits, &verifyData[eucStart + 4], 4);
    std::memcpy(&eucSteps, &verifyData[eucStart + 8], 4);
    std::memcpy(&eucRotation, &verifyData[eucStart + 12], 4);

    REQUIRE(eucEnabled == 1);
    REQUIRE(eucHits == 5);
    REQUIRE(eucSteps == 13);
    REQUIRE(eucRotation == 3);

    proc->terminate();
    proc2->terminate();
}

TEST_CASE("Arp state round-trip preserves condition values",
          "[state][roundtrip][arp state round-trip]") {
    auto proc = makeTestableProcessor();

    ParamChangeBatch changes;
    changes.add(Ruinae::kArpEnabledId, 1.0);

    // Set condition lane to length=18 and include all 18 TrigCondition variants
    changes.add(Ruinae::kArpConditionLaneLengthId, (18.0 - 1.0) / 31.0);
    // All 18 conditions: Always(0), Prob10(1), Prob25(2), Prob50(3), Prob75(4),
    // Prob90(5), 1:2(6), 2:2(7), 1:3(8), 2:3(9), 3:3(10), 1:4(11), 2:4(12),
    // 3:4(13), 4:4(14), First(15), Fill(16), !Fill(17)
    for (int i = 0; i < 18; ++i) {
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpConditionLaneStep0Id + i),
            static_cast<double>(i) / 17.0);
    }
    proc->processParameterChanges(&changes);

    auto [data1, data2] = roundTripState(proc.get());
    REQUIRE(data1.size() == data2.size());
    REQUIRE(data1 == data2);

    // Verify the condition values in the arp section
    constexpr size_t kArpSectionSize = 888;
    // Condition lane offset within arp: 712 (Euclidean end) + 16 = 728
    constexpr size_t kCondOffsetInArp = 728;
    REQUIRE(data2.size() > kArpSectionSize);
    size_t arpStart = data2.size() - kArpSectionSize;
    size_t condStart = arpStart + kCondOffsetInArp;

    // Read condition lane length
    Steinberg::int32 condLength = 0;
    std::memcpy(&condLength, &data2[condStart], 4);
    REQUIRE(condLength == 18);

    // Read each condition step and verify
    for (int i = 0; i < 18; ++i) {
        Steinberg::int32 condVal = 0;
        std::memcpy(&condVal, &data2[condStart + 4 + i * 4], 4);
        REQUIRE(condVal == i);
    }

    proc->terminate();
}

TEST_CASE("Arp state round-trip preserves modifier bitmasks",
          "[state][roundtrip][arp state round-trip]") {
    auto proc = makeTestableProcessor();

    ParamChangeBatch changes;
    changes.add(Ruinae::kArpEnabledId, 1.0);

    // Set modifier lane with specific bitmask combinations
    changes.add(Ruinae::kArpModifierLaneLengthId, (6.0 - 1.0) / 31.0);
    int bitmasks[] = {0x00, 0x01, 0x03, 0x05, 0x09, 0x0D};
    //                Rest,  Active, Active+Tie, Active+Slide, Active+Accent, All
    for (int i = 0; i < 6; ++i) {
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpModifierLaneStep0Id + i),
            bitmasks[i] / 255.0);
    }
    proc->processParameterChanges(&changes);

    auto [data1, data2] = roundTripState(proc.get());
    REQUIRE(data1.size() == data2.size());
    REQUIRE(data1 == data2);

    // Verify bitmasks in the modifier section of the arp data
    constexpr size_t kArpSectionSize = 888;
    // Modifier lane offset within arp: 44 (base) + 132 (vel) + 132 (gate) + 132 (pitch) = 440
    constexpr size_t kModOffsetInArp = 440;
    REQUIRE(data2.size() > kArpSectionSize);
    size_t arpStart = data2.size() - kArpSectionSize;
    size_t modStart = arpStart + kModOffsetInArp;

    // Read modifier lane length
    Steinberg::int32 modLength = 0;
    std::memcpy(&modLength, &data2[modStart], 4);
    REQUIRE(modLength == 6);

    // Verify each modifier bitmask
    for (int i = 0; i < 6; ++i) {
        Steinberg::int32 modVal = 0;
        std::memcpy(&modVal, &data2[modStart + 4 + i * 4], 4);
        REQUIRE(modVal == bitmasks[i]);
    }

    proc->terminate();
}

TEST_CASE("Arp state round-trip preserves float values bit-identically",
          "[state][roundtrip][arp state round-trip]") {
    auto proc = makeTestableProcessor();

    // Set specific float values: spice=0.73, humanize=0.42, ratchetSwing=62.0
    ParamChangeBatch changes;
    changes.add(Ruinae::kArpEnabledId, 1.0);
    changes.add(Ruinae::kArpSpiceId, 0.73);        // direct: normalized = raw
    changes.add(Ruinae::kArpHumanizeId, 0.42);      // direct: normalized = raw
    // ratchetSwing: raw 50-75, normalized = (val - 50) / 25
    changes.add(Ruinae::kArpRatchetSwingId, (62.0 - 50.0) / 25.0);
    proc->processParameterChanges(&changes);

    // Round-trip
    auto [data1, data2] = roundTripState(proc.get());
    REQUIRE(data1.size() == data2.size());
    REQUIRE(data1 == data2);

    // Verify bit-identical floats using std::memcmp
    // Arp section layout at end of state (888 bytes):
    // Spice/humanize offset: after condition lane (728 + 4 + 32*4 + 4 = 864 bytes from arp start)
    // Actually: condition section = length(4) + 32*steps(128) + fillToggle(4) = 136 bytes
    // Condition starts at offset 728 in arp section, so ends at 728+136 = 864
    // Then spice(4) + humanize(4) + ratchetSwing(4) = 12 bytes at offset 864
    constexpr size_t kArpSectionSize = 888;
    constexpr size_t kSpiceOffsetInArp = 864;
    REQUIRE(data2.size() > kArpSectionSize);
    size_t arpStart = data2.size() - kArpSectionSize;
    size_t spiceOffset = arpStart + kSpiceOffsetInArp;

    float spiceVal = 0.0f, humanizeVal = 0.0f, ratchetSwingVal = 0.0f;
    std::memcpy(&spiceVal, &data2[spiceOffset], 4);
    std::memcpy(&humanizeVal, &data2[spiceOffset + 4], 4);
    std::memcpy(&ratchetSwingVal, &data2[spiceOffset + 8], 4);

    // Bit-identical verification via memcmp
    float expectedSpice = 0.73f;
    float expectedHumanize = 0.42f;
    float expectedRatchetSwing = 62.0f;

    REQUIRE(std::memcmp(&spiceVal, &expectedSpice, sizeof(float)) == 0);
    REQUIRE(std::memcmp(&humanizeVal, &expectedHumanize, sizeof(float)) == 0);
    REQUIRE(std::memcmp(&ratchetSwingVal, &expectedRatchetSwing, sizeof(float)) == 0);

    proc->terminate();
}

TEST_CASE("Pre-arp preset loads with arp disabled",
          "[state][roundtrip][arp state round-trip]") {
    // Create a full default state and truncate it to remove the arp section.
    // This simulates loading a preset from before the arpeggiator was added.
    auto proc1 = makeProcessor();
    Steinberg::MemoryStream fullStream;
    REQUIRE(proc1->getState(&fullStream) == Steinberg::kResultTrue);
    auto fullData = extractStreamBytes(fullStream);

    // The arp section is the last 888 bytes of the state.
    constexpr size_t kArpSectionSize = 888;
    REQUIRE(fullData.size() > kArpSectionSize);
    size_t truncatedSize = fullData.size() - kArpSectionSize;

    // Create a truncated stream (everything before arp params)
    Steinberg::MemoryStream truncStream;
    truncStream.write(fullData.data(),
        static_cast<Steinberg::int32>(truncatedSize), nullptr);
    truncStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Load truncated state into a fresh processor
    auto proc2 = makeProcessor();
    auto result = proc2->setState(&truncStream);
    REQUIRE(result == Steinberg::kResultTrue);

    // Save state from the loaded processor and verify the arp section has defaults
    Steinberg::MemoryStream savedStream;
    proc2->getState(&savedStream);
    auto savedData = extractStreamBytes(savedStream);
    REQUIRE(savedData.size() > kArpSectionSize);

    size_t arpStart = savedData.size() - kArpSectionSize;

    // Read arp enabled flag (first int32 of arp section) -- should be 0 (disabled)
    Steinberg::int32 arpEnabled = 0;
    std::memcpy(&arpEnabled, &savedData[arpStart], 4);
    REQUIRE(arpEnabled == 0);

    // Verify default lane lengths (velocity lane length is at offset 44 in arp section)
    constexpr size_t kVelLenOffsetInArp = 44;
    Steinberg::int32 velLen = 0;
    std::memcpy(&velLen, &savedData[arpStart + kVelLenOffsetInArp], 4);
    REQUIRE(velLen == 16);  // default

    // Gate lane length at offset 44 + 132 = 176
    Steinberg::int32 gateLen = 0;
    std::memcpy(&gateLen, &savedData[arpStart + 176], 4);
    REQUIRE(gateLen == 16);  // default

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("Partial arp preset loads base params and defaults rest",
          "[state][roundtrip][arp state round-trip]") {
    // Create a state blob with only the 11 base arp params (44 bytes),
    // no lane data. This simulates a Phase 3 preset that had only base params.
    auto proc1 = makeTestableProcessor();

    // Set non-default base arp params
    ParamChangeBatch changes;
    changes.add(Ruinae::kArpEnabledId, 1.0);          // enabled = true
    changes.add(Ruinae::kArpModeId, 2.0 / 9.0);       // mode = UpDown (2)
    changes.add(Ruinae::kArpOctaveRangeId, 2.0 / 3.0); // octaveRange = 3
    changes.add(Ruinae::kArpTempoSyncId, 1.0);         // tempoSync = true
    changes.add(Ruinae::kArpNoteValueId, 7.0 / 20.0);  // noteValue = 7 (1/16)
    changes.add(Ruinae::kArpGateLengthId, (60.0 - 1.0) / 199.0); // gateLength = 60%
    proc1->processParameterChanges(&changes);

    // Save full state
    Steinberg::MemoryStream fullStream;
    REQUIRE(proc1->getState(&fullStream) == Steinberg::kResultTrue);
    auto fullData = extractStreamBytes(fullStream);

    // Calculate the truncation point: keep everything before the arp section
    // plus only the 11 base arp params (44 bytes)
    constexpr size_t kArpSectionSize = 888;
    constexpr size_t kArpBaseParamsSize = 44;  // 11 * 4 bytes
    REQUIRE(fullData.size() > kArpSectionSize);
    size_t arpStart = fullData.size() - kArpSectionSize;
    size_t partialSize = arpStart + kArpBaseParamsSize;

    // Create partial stream
    Steinberg::MemoryStream partialStream;
    partialStream.write(fullData.data(),
        static_cast<Steinberg::int32>(partialSize), nullptr);
    partialStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Load into fresh processor
    auto proc2 = makeProcessor();
    auto result = proc2->setState(&partialStream);
    REQUIRE(result == Steinberg::kResultTrue);

    // Save and verify: base params loaded, lanes at defaults
    Steinberg::MemoryStream savedStream;
    proc2->getState(&savedStream);
    auto savedData = extractStreamBytes(savedStream);
    REQUIRE(savedData.size() > kArpSectionSize);

    size_t savedArpStart = savedData.size() - kArpSectionSize;

    // Verify base params were loaded (arp enabled = 1)
    Steinberg::int32 enabled = 0;
    std::memcpy(&enabled, &savedData[savedArpStart], 4);
    REQUIRE(enabled == 1);

    // Verify mode was loaded (mode = 2 = UpDown)
    Steinberg::int32 mode = 0;
    std::memcpy(&mode, &savedData[savedArpStart + 4], 4);
    REQUIRE(mode == 2);

    // Verify lane lengths are at defaults (16) since lane data wasn't in the stream
    constexpr size_t kVelLenOffsetInArp = 44;
    Steinberg::int32 velLen = 0;
    std::memcpy(&velLen, &savedData[savedArpStart + kVelLenOffsetInArp], 4);
    REQUIRE(velLen == 16);  // default

    // Verify velocity steps are at defaults (1.0f)
    float velStep0 = 0.0f;
    std::memcpy(&velStep0, &savedData[savedArpStart + kVelLenOffsetInArp + 4], 4);
    float defaultVel = 1.0f;
    REQUIRE(std::memcmp(&velStep0, &defaultVel, sizeof(float)) == 0);

    proc1->terminate();
    proc2->terminate();
}
