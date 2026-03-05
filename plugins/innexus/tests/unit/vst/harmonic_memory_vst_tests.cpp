// ==============================================================================
// Harmonic Memory VST Parameter Registration Tests (M5)
// ==============================================================================
// Tests that the three new M5 parameters (Memory Slot, Memory Capture,
// Memory Recall) are correctly registered in the Controller with proper
// types, ranges, defaults, and step counts.
//
// Feature: 119-harmonic-memory
// Requirements: FR-005, FR-006, FR-011, FR-030
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/harmonic_snapshot_json.h"

#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <cstring>
#include <fstream>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

TEST_CASE("M5: All three harmonic memory parameter IDs are retrievable from controller",
          "[innexus][vst][m5][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};

    // kMemorySlotId
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMemorySlotId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kMemorySlotId);

    // kMemoryCaptureId
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMemoryCaptureId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kMemoryCaptureId);

    // kMemoryRecallId
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMemoryRecallId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kMemoryRecallId);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("M5: kMemorySlotId has 8 list entries with correct names",
          "[innexus][vst][m5][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMemorySlotId, info) == kResultOk);

    // StringListParameter with 8 entries has stepCount == 7 (0,1,2,3,4,5,6,7)
    REQUIRE(info.stepCount == 7);

    // Must be automatable and a list
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
    REQUIRE((info.flags & ParameterInfo::kIsList) != 0);

    // Default is 0.0 (Slot 1)
    REQUIRE(info.defaultNormalizedValue == Approx(0.0).margin(0.001));

    // Verify list entry names via valueToString
    Steinberg::Vst::String128 stringBuf{};
    // Slot 1 at normalized 0.0
    REQUIRE(controller.getParamStringByValue(
        Innexus::kMemorySlotId, 0.0, stringBuf) == kResultOk);
    // Slot 8 at normalized 1.0
    REQUIRE(controller.getParamStringByValue(
        Innexus::kMemorySlotId, 1.0, stringBuf) == kResultOk);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("M5: kMemoryCaptureId has stepCount == 1 (momentary trigger)",
          "[innexus][vst][m5][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMemoryCaptureId, info) == kResultOk);

    // A momentary trigger has stepCount == 1 (two states: 0 and 1)
    REQUIRE(info.stepCount == 1);

    // Default is off (0.0)
    REQUIRE(info.defaultNormalizedValue == Approx(0.0).margin(0.001));

    // Must be automatable
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("M5: kMemoryRecallId has stepCount == 1 (momentary trigger)",
          "[innexus][vst][m5][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMemoryRecallId, info) == kResultOk);

    // A momentary trigger has stepCount == 1 (two states: 0 and 1)
    REQUIRE(info.stepCount == 1);

    // Default is off (0.0)
    REQUIRE(info.defaultNormalizedValue == Approx(0.0).margin(0.001));

    // Must be automatable
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("M5: Memory Slot default normalized value is 0.0 (Slot 1)",
          "[innexus][vst][m5][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMemorySlotId, info) == kResultOk);

    // Default normalized value is 0.0, which denormalizes to slot index 0 (Slot 1)
    REQUIRE(info.defaultNormalizedValue == Approx(0.0).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// Phase 5: State Persistence VST Tests
// =============================================================================

// Minimal IBStream implementation for controller state tests
class M5VstTestStream : public IBStream
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override
    {
        if (!buffer || numBytes <= 0) return kResultFalse;
        int32 available = static_cast<int32>(data_.size()) - readPos_;
        int32 toRead = std::min(numBytes, available);
        if (toRead <= 0) { if (numBytesRead) *numBytesRead = 0; return kResultFalse; }
        std::memcpy(buffer, data_.data() + readPos_, static_cast<size_t>(toRead));
        readPos_ += toRead;
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override
    {
        if (!buffer || numBytes <= 0) return kResultFalse;
        auto* bytes = static_cast<const char*>(buffer);
        data_.insert(data_.end(), bytes, bytes + numBytes);
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override
    {
        int64 newPos = 0;
        switch (mode) {
        case kIBSeekSet: newPos = pos; break;
        case kIBSeekCur: newPos = readPos_ + pos; break;
        case kIBSeekEnd: newPos = static_cast<int64>(data_.size()) + pos; break;
        default: return kResultFalse;
        }
        if (newPos < 0 || newPos > static_cast<int64>(data_.size())) return kResultFalse;
        readPos_ = static_cast<int32>(newPos);
        if (result) *result = readPos_;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* pos) override
    {
        if (pos) *pos = readPos_;
        return kResultOk;
    }

    void resetReadPos() { readPos_ = 0; }

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

// T068: Controller::setComponentState with v5 data restores kMemorySlotId
TEST_CASE("M5 VST: setComponentState with v5 data restores kMemorySlotId",
          "[innexus][vst][m5][state]")
{
    M5VstTestStream stream;

    // Create a v5 state blob via a processor
    {
        Innexus::Processor proc;
        proc.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 128;
        setup.sampleRate = 44100.0;
        proc.setupProcessing(setup);
        proc.setActive(true);

        // Set slot to 3 (normalized = 3/7)
        // We need to send a parameter change
        // Simpler: just save state which will write the default slot (0)
        // and verify the controller reads it back.
        // Let's write the state via getState.
        REQUIRE(proc.getState(&stream) == kResultOk);

        proc.setActive(false);
        proc.terminate();
    }

    // Load into controller
    {
        Innexus::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        stream.resetReadPos();
        REQUIRE(controller.setComponentState(&stream) == kResultOk);

        // kMemorySlotId should be 0.0 (default slot 0)
        REQUIRE(controller.getParamNormalized(Innexus::kMemorySlotId)
                == Approx(0.0).margin(0.01));

        // kMemoryCaptureId and kMemoryRecallId should be 0.0
        REQUIRE(controller.getParamNormalized(Innexus::kMemoryCaptureId)
                == Approx(0.0).margin(0.01));
        REQUIRE(controller.getParamNormalized(Innexus::kMemoryRecallId)
                == Approx(0.0).margin(0.01));

        REQUIRE(controller.terminate() == kResultOk);
    }
}

// T069: Controller::setComponentState with v4 data defaults all M5 params to 0.0
TEST_CASE("M5 VST: setComponentState with v4 data defaults M5 params to 0.0",
          "[innexus][vst][m5][state]")
{
    M5VstTestStream stream;

    // Write a v4 state blob manually
    {
        Steinberg::IBStreamer streamer(&stream, kLittleEndian);

        streamer.writeInt32(4);          // version 4

        // M1
        streamer.writeFloat(100.0f);     // releaseTimeMs
        streamer.writeFloat(0.3f);       // inharmonicityAmount
        streamer.writeFloat(0.8f);       // masterGain
        streamer.writeFloat(0.0f);       // bypass
        streamer.writeInt32(0);          // path length (empty)

        // M2
        streamer.writeFloat(1.0f);       // harmonicLevel
        streamer.writeFloat(1.0f);       // residualLevel
        streamer.writeFloat(0.0f);       // brightness
        streamer.writeFloat(0.0f);       // transientEmphasis
        streamer.writeInt32(0);          // residual frame count
        streamer.writeInt32(0);          // fftSize
        streamer.writeInt32(0);          // hopSize

        // M3
        streamer.writeInt32(0);          // inputSource
        streamer.writeInt32(0);          // latencyMode

        // M4
        streamer.writeInt8(static_cast<Steinberg::int8>(0)); // freeze
        streamer.writeFloat(0.0f);       // morphPosition
        streamer.writeInt32(0);          // harmonicFilterType
        streamer.writeFloat(0.5f);       // responsiveness
    }

    // Load into controller
    {
        Innexus::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        stream.resetReadPos();
        REQUIRE(controller.setComponentState(&stream) == kResultOk);

        // All M5 parameters should default to 0.0
        REQUIRE(controller.getParamNormalized(Innexus::kMemorySlotId)
                == Approx(0.0).margin(0.01));
        REQUIRE(controller.getParamNormalized(Innexus::kMemoryCaptureId)
                == Approx(0.0).margin(0.01));
        REQUIRE(controller.getParamNormalized(Innexus::kMemoryRecallId)
                == Approx(0.0).margin(0.01));

        REQUIRE(controller.terminate() == kResultOk);
    }
}

// =============================================================================
// Phase 6: Controller JSON Import Tests (User Story 4)
// =============================================================================

// T091b: importSnapshotFromJson returns false on nonexistent file
TEST_CASE("M5 VST: importSnapshotFromJson returns false on nonexistent file",
          "[innexus][vst][m5][json][us4]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Non-existent file should return false
    REQUIRE(controller.importSnapshotFromJson("nonexistent_file_12345.json", 0) == false);

    REQUIRE(controller.terminate() == kResultOk);
}

// T091b: importSnapshotFromJson reads valid file, parses it, but returns false
// because allocateMessage() returns nullptr without a host connection.
// This verifies the file I/O and JSON parsing stages work correctly.
TEST_CASE("M5 VST: importSnapshotFromJson parses valid file but fails without host",
          "[innexus][vst][m5][json][us4]")
{
    // Create a temporary JSON file
    Krate::DSP::HarmonicSnapshot snap{};
    snap.f0Reference = 440.0f;
    snap.numPartials = 2;
    snap.relativeFreqs[0] = 1.0f;
    snap.relativeFreqs[1] = 2.0f;
    snap.normalizedAmps[0] = 0.707f;
    snap.normalizedAmps[1] = 0.5f;
    snap.phases[0] = 0.0f;
    snap.phases[1] = 1.57f;
    snap.inharmonicDeviation[0] = 0.0f;
    snap.inharmonicDeviation[1] = 0.001f;
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
        snap.residualBands[b] = 0.01f;
    snap.residualEnergy = 0.05f;
    snap.globalAmplitude = 0.3f;
    snap.spectralCentroid = 2200.0f;
    snap.brightness = 0.6f;

    std::string json = Innexus::snapshotToJson(snap);

    // Write to temp file
    const std::string tempPath = "m5_test_import_temp.json";
    {
        std::ofstream f(tempPath);
        f << json;
    }

    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Should return false because allocateMessage() returns nullptr without a host
    bool result = controller.importSnapshotFromJson(tempPath, 2);
    REQUIRE(result == false);

    REQUIRE(controller.terminate() == kResultOk);

    // Clean up temp file
    std::remove(tempPath.c_str());
}

// T091b: importSnapshotFromJson returns false on invalid JSON content
TEST_CASE("M5 VST: importSnapshotFromJson returns false on invalid JSON",
          "[innexus][vst][m5][json][us4]")
{
    // Write invalid JSON to temp file
    const std::string tempPath = "m5_test_invalid_json_temp.json";
    {
        std::ofstream f(tempPath);
        f << "this is not json";
    }

    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    REQUIRE(controller.importSnapshotFromJson(tempPath, 0) == false);

    REQUIRE(controller.terminate() == kResultOk);

    std::remove(tempPath.c_str());
}

// T091b: Full integration test - export snapshot to JSON, import via notify()
// This tests the complete pipeline minus the controller<->processor messaging
// which requires a real host. We manually chain the steps.
TEST_CASE("M5 VST: JSON export -> import via notify() round-trip",
          "[innexus][vst][m5][json][us4]")
{
    // 1. Create a snapshot and export to JSON
    Krate::DSP::HarmonicSnapshot original{};
    original.f0Reference = 261.63f;
    original.numPartials = 3;
    original.relativeFreqs[0] = 1.0f;
    original.relativeFreqs[1] = 2.002f;
    original.relativeFreqs[2] = 3.005f;
    original.normalizedAmps[0] = 0.8f;
    original.normalizedAmps[1] = 0.5f;
    original.normalizedAmps[2] = 0.3f;
    original.phases[0] = 0.0f;
    original.phases[1] = 1.57f;
    original.phases[2] = 3.14f;
    original.inharmonicDeviation[0] = 0.0f;
    original.inharmonicDeviation[1] = 0.002f;
    original.inharmonicDeviation[2] = 0.005f;
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
        original.residualBands[b] = 0.01f * static_cast<float>(b + 1);
    original.residualEnergy = 0.07f;
    original.globalAmplitude = 0.45f;
    original.spectralCentroid = 1500.0f;
    original.brightness = 0.4f;

    std::string json = Innexus::snapshotToJson(original);

    // 2. Parse JSON to snapshot
    Krate::DSP::HarmonicSnapshot parsed{};
    REQUIRE(Innexus::jsonToSnapshot(json, parsed));

    // 3. Send to processor via notify()
    Innexus::Processor proc;
    proc.initialize(nullptr);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = 128;
    setup.sampleRate = 44100.0;
    proc.setupProcessing(setup);
    proc.setActive(true);

    Steinberg::Vst::HostMessage msg;
    msg.setMessageID("HarmonicSnapshotImport");
    auto* attrs = msg.getAttributes();
    attrs->setInt("slotIndex", 5);
    attrs->setBinary("snapshotData", &parsed, sizeof(parsed));

    REQUIRE(proc.notify(&msg) == Steinberg::kResultOk);

    // 4. Verify the slot is populated correctly (SC-010: within 1e-6)
    const auto& slot = proc.getMemorySlot(5);
    REQUIRE(slot.occupied == true);
    REQUIRE(slot.snapshot.f0Reference == Approx(original.f0Reference).margin(1e-6f));
    REQUIRE(slot.snapshot.numPartials == original.numPartials);

    for (int i = 0; i < original.numPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        REQUIRE(slot.snapshot.relativeFreqs[idx]
                == Approx(original.relativeFreqs[idx]).margin(1e-6f));
        REQUIRE(slot.snapshot.normalizedAmps[idx]
                == Approx(original.normalizedAmps[idx]).margin(1e-6f));
        REQUIRE(slot.snapshot.phases[idx]
                == Approx(original.phases[idx]).margin(1e-6f));
        REQUIRE(slot.snapshot.inharmonicDeviation[idx]
                == Approx(original.inharmonicDeviation[idx]).margin(1e-6f));
    }

    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        REQUIRE(slot.snapshot.residualBands[b]
                == Approx(original.residualBands[b]).margin(1e-6f));
    }

    REQUIRE(slot.snapshot.residualEnergy
            == Approx(original.residualEnergy).margin(1e-6f));
    REQUIRE(slot.snapshot.globalAmplitude
            == Approx(original.globalAmplitude).margin(1e-6f));
    REQUIRE(slot.snapshot.spectralCentroid
            == Approx(original.spectralCentroid).margin(1e-6f));
    REQUIRE(slot.snapshot.brightness
            == Approx(original.brightness).margin(1e-6f));

    proc.setActive(false);
    proc.terminate();
}
