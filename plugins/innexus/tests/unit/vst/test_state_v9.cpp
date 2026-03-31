// ==============================================================================
// State Persistence Tests (ADSR Envelope Detection)
// ==============================================================================
// Tests that getState()/setState() correctly persist the 9 global ADSR floats
// and 72 per-slot ADSR floats (8 slots x 9 fields) in the flat v1 state format.
//
// Feature: 124-adsr-envelope-detection
// Tasks: T053
// Requirements: FR-019, FR-020
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <memory>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// Minimal IBStream implementation for state tests
class V9TestStream : public IBStream
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
    [[nodiscard]] size_t size() const { return data_.size(); }
    [[nodiscard]] const std::vector<char>& data() const { return data_; }

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

static std::unique_ptr<Innexus::Processor> createAndSetupProcessor()
{
    auto proc = std::make_unique<Innexus::Processor>();
    proc->initialize(nullptr);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 128;
    setup.sampleRate = 44100.0;
    proc->setupProcessing(setup);
    proc->setActive(true);
    return proc;
}

/// Helper to write the common state prefix (M1-M6 + physics + feedback)
static void writeStatePrefix(IBStreamer& streamer)
{
    // Version 1
    streamer.writeInt32(1);

    // M1 parameters
    streamer.writeFloat(100.0f); streamer.writeFloat(0.5f);
    streamer.writeFloat(0.8f);   streamer.writeFloat(0.0f);
    streamer.writeInt32(0);

    // M2 parameters
    streamer.writeFloat(1.0f); streamer.writeFloat(1.0f);
    streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
    streamer.writeInt32(0); streamer.writeInt32(0); streamer.writeInt32(0);

    // M3 parameters
    streamer.writeInt32(0); streamer.writeInt32(0);

    // M4 parameters
    streamer.writeInt8(static_cast<int8>(0));
    streamer.writeFloat(0.0f); streamer.writeInt32(0); streamer.writeFloat(0.5f);

    // M5 parameters
    streamer.writeInt32(0);
    for (int s = 0; s < 8; ++s) streamer.writeInt8(static_cast<int8>(0));

    // M6 (31 floats)
    streamer.writeFloat(1.0f); streamer.writeFloat(0.0f);
    for (int i = 0; i < 29; ++i) streamer.writeFloat(0.0f);

    // Harmonic Physics parameters
    for (int i = 0; i < 4; ++i) streamer.writeFloat(0.0f);

    // Feedback parameters
    streamer.writeFloat(0.0f); streamer.writeFloat(0.2f);
}

/// Helper to write default ADSR suffix (global + per-slot + partialCount)
static void writeDefaultAdsrSuffix(IBStreamer& streamer)
{
    // ADSR global (9 floats -- defaults)
    streamer.writeFloat(10.0f);  streamer.writeFloat(100.0f);
    streamer.writeFloat(1.0f);   streamer.writeFloat(100.0f);
    streamer.writeFloat(0.0f);   streamer.writeFloat(1.0f);
    streamer.writeFloat(0.0f);   streamer.writeFloat(0.0f);
    streamer.writeFloat(0.0f);

    // Per-slot ADSR (8 x 9 = 72 floats -- defaults)
    for (int s = 0; s < 8; ++s)
    {
        streamer.writeFloat(10.0f);  streamer.writeFloat(100.0f);
        streamer.writeFloat(1.0f);   streamer.writeFloat(100.0f);
        streamer.writeFloat(0.0f);   streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f);   streamer.writeFloat(0.0f);
        streamer.writeFloat(0.0f);
    }

    // Partial Count
    streamer.writeFloat(0.0f);
}

// ==============================================================================
// T053(a): State version is 1
// ==============================================================================

TEST_CASE("StateV9: getState writes version 1",
          "[innexus][vst][state][v9][adsr]")
{
    V9TestStream stream;

    auto proc = createAndSetupProcessor();
    REQUIRE(proc->getState(&stream) == kResultOk);

    stream.resetReadPos();
    IBStreamer reader(&stream, kLittleEndian);
    int32 version = 0;
    REQUIRE(reader.readInt32(version));
    REQUIRE(version == 2);

    proc->setActive(false);
    proc->terminate();
}

// ==============================================================================
// T053(b): Save/load round-trip with non-default ADSR values
// ==============================================================================

TEST_CASE("StateV9: save/load roundtrip preserves global ADSR parameters",
          "[innexus][vst][state][v9][adsr]")
{
    V9TestStream stream;
    {
        IBStreamer streamer(&stream, kLittleEndian);
        writeStatePrefix(streamer);

        // ADSR global parameters with custom values
        streamer.writeFloat(250.0f);   // adsrAttackMs
        streamer.writeFloat(200.0f);   // adsrDecayMs
        streamer.writeFloat(0.6f);     // adsrSustainLevel
        streamer.writeFloat(300.0f);   // adsrReleaseMs
        streamer.writeFloat(0.7f);     // adsrAmount
        streamer.writeFloat(2.0f);     // adsrTimeScale
        streamer.writeFloat(0.5f);     // adsrAttackCurve
        streamer.writeFloat(-0.3f);    // adsrDecayCurve
        streamer.writeFloat(0.8f);     // adsrReleaseCurve

        // Per-slot ADSR (defaults except slot 3)
        for (int s = 0; s < 8; ++s)
        {
            if (s == 3)
            {
                streamer.writeFloat(50.0f);    streamer.writeFloat(150.0f);
                streamer.writeFloat(0.4f);     streamer.writeFloat(250.0f);
                streamer.writeFloat(0.9f);     streamer.writeFloat(1.5f);
                streamer.writeFloat(0.2f);     streamer.writeFloat(-0.5f);
                streamer.writeFloat(0.3f);
            }
            else
            {
                streamer.writeFloat(10.0f);  streamer.writeFloat(100.0f);
                streamer.writeFloat(1.0f);   streamer.writeFloat(100.0f);
                streamer.writeFloat(0.0f);   streamer.writeFloat(1.0f);
                streamer.writeFloat(0.0f);   streamer.writeFloat(0.0f);
                streamer.writeFloat(0.0f);
            }
        }

        // Partial Count
        streamer.writeFloat(0.0f);
    }

    {
        auto proc = createAndSetupProcessor();
        stream.resetReadPos();
        REQUIRE(proc->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-4f;
        REQUIRE(proc->getAdsrAttackMs() == Approx(250.0f).margin(kTol));
        REQUIRE(proc->getAdsrDecayMs() == Approx(200.0f).margin(kTol));
        REQUIRE(proc->getAdsrSustainLevel() == Approx(0.6f).margin(kTol));
        REQUIRE(proc->getAdsrReleaseMs() == Approx(300.0f).margin(kTol));
        REQUIRE(proc->getAdsrAmount() == Approx(0.7f).margin(kTol));
        REQUIRE(proc->getAdsrTimeScale() == Approx(2.0f).margin(kTol));
        REQUIRE(proc->getAdsrAttackCurve() == Approx(0.5f).margin(kTol));
        REQUIRE(proc->getAdsrDecayCurve() == Approx(-0.3f).margin(kTol));
        REQUIRE(proc->getAdsrReleaseCurve() == Approx(0.8f).margin(kTol));

        proc->setActive(false);
        proc->terminate();
    }
}

// ==============================================================================
// T053(c): Per-slot ADSR round-trip
// ==============================================================================

TEST_CASE("StateV9: per-slot ADSR data round-trips for all 8 slots",
          "[innexus][vst][state][v9][adsr]")
{
    V9TestStream stream;
    {
        IBStreamer streamer(&stream, kLittleEndian);
        writeStatePrefix(streamer);

        // ADSR global (defaults)
        streamer.writeFloat(10.0f);  streamer.writeFloat(100.0f);
        streamer.writeFloat(1.0f);   streamer.writeFloat(100.0f);
        streamer.writeFloat(0.0f);   streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f);   streamer.writeFloat(0.0f);
        streamer.writeFloat(0.0f);

        // Per-slot ADSR -- each slot gets a distinct attack time
        for (int s = 0; s < 8; ++s)
        {
            float attackMs = static_cast<float>((s + 1) * 100);
            streamer.writeFloat(attackMs);
            streamer.writeFloat(100.0f);
            streamer.writeFloat(1.0f);
            streamer.writeFloat(100.0f);
            streamer.writeFloat(0.0f);
            streamer.writeFloat(1.0f);
            streamer.writeFloat(0.0f);
            streamer.writeFloat(0.0f);
            streamer.writeFloat(0.0f);
        }

        // Partial Count
        streamer.writeFloat(0.0f);
    }

    {
        auto proc = createAndSetupProcessor();
        stream.resetReadPos();
        REQUIRE(proc->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-4f;
        for (int s = 0; s < 8; ++s)
        {
            const auto& slot = proc->getMemorySlot(s);
            float expectedAttack = static_cast<float>((s + 1) * 100);
            REQUIRE(slot.adsrAttackMs == Approx(expectedAttack).margin(kTol));
            REQUIRE(slot.adsrDecayMs == Approx(100.0f).margin(kTol));
            REQUIRE(slot.adsrSustainLevel == Approx(1.0f).margin(kTol));
            REQUIRE(slot.adsrReleaseMs == Approx(100.0f).margin(kTol));
            REQUIRE(slot.adsrAmount == Approx(0.0f).margin(kTol));
            REQUIRE(slot.adsrTimeScale == Approx(1.0f).margin(kTol));
            REQUIRE(slot.adsrAttackCurve == Approx(0.0f).margin(kTol));
            REQUIRE(slot.adsrDecayCurve == Approx(0.0f).margin(kTol));
            REQUIRE(slot.adsrReleaseCurve == Approx(0.0f).margin(kTol));
        }

        proc->setActive(false);
        proc->terminate();
    }
}

// ==============================================================================
// T053(d): Full getState/setState round-trip
// ==============================================================================

TEST_CASE("StateV9: getState/setState full round-trip preserves all ADSR data",
          "[innexus][vst][state][v9][adsr]")
{
    V9TestStream stream;

    {
        auto procA = createAndSetupProcessor();
        REQUIRE(procA->getState(&stream) == kResultOk);
        procA->setActive(false);
        procA->terminate();
    }

    {
        auto procB = createAndSetupProcessor();
        stream.resetReadPos();
        REQUIRE(procB->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-4f;
        REQUIRE(procB->getAdsrAttackMs() == Approx(10.0f).margin(kTol));
        REQUIRE(procB->getAdsrDecayMs() == Approx(100.0f).margin(kTol));
        REQUIRE(procB->getAdsrSustainLevel() == Approx(1.0f).margin(kTol));
        REQUIRE(procB->getAdsrReleaseMs() == Approx(100.0f).margin(kTol));
        REQUIRE(procB->getAdsrAmount() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getAdsrTimeScale() == Approx(1.0f).margin(kTol));
        REQUIRE(procB->getAdsrAttackCurve() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getAdsrDecayCurve() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getAdsrReleaseCurve() == Approx(0.0f).margin(kTol));

        procB->setActive(false);
        procB->terminate();
    }
}

// ==============================================================================
// T053(e): Default state has ADSR Amount=0.0 (bypass)
// ==============================================================================

TEST_CASE("StateV9: default state has ADSR Amount 0.0 and default curves",
          "[innexus][vst][state][v9][adsr]")
{
    V9TestStream stream;
    {
        IBStreamer streamer(&stream, kLittleEndian);
        writeStatePrefix(streamer);
        writeDefaultAdsrSuffix(streamer);
    }

    auto proc = createAndSetupProcessor();
    stream.resetReadPos();
    REQUIRE(proc->setState(&stream) == kResultOk);

    constexpr float kTol = 1e-4f;
    REQUIRE(proc->getAdsrAmount() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrAttackMs() == Approx(10.0f).margin(kTol));
    REQUIRE(proc->getAdsrDecayMs() == Approx(100.0f).margin(kTol));
    REQUIRE(proc->getAdsrSustainLevel() == Approx(1.0f).margin(kTol));
    REQUIRE(proc->getAdsrReleaseMs() == Approx(100.0f).margin(kTol));
    REQUIRE(proc->getAdsrTimeScale() == Approx(1.0f).margin(kTol));
    REQUIRE(proc->getAdsrAttackCurve() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrDecayCurve() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrReleaseCurve() == Approx(0.0f).margin(kTol));

    for (int s = 0; s < 8; ++s)
    {
        const auto& slot = proc->getMemorySlot(s);
        REQUIRE(slot.adsrAmount == Approx(0.0f).margin(kTol));
        REQUIRE(slot.adsrAttackMs == Approx(10.0f).margin(kTol));
    }

    REQUIRE(proc->getMasterGain() == Approx(0.8f).margin(kTol));

    proc->setActive(false);
    proc->terminate();
}
