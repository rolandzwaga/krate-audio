// ==============================================================================
// State v9 Persistence Tests (ADSR Envelope Detection)
// ==============================================================================
// Tests that getState()/setState() correctly persist the 9 global ADSR floats
// and 72 per-slot ADSR floats (8 slots x 9 fields) in version 9 format,
// and that loading a v8 or v7 state initializes ADSR to safe defaults
// (Amount=0.0, curves=0.0, times=10/100/1.0/100, TimeScale=1.0).
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

// Helper to create a processor, set it up, and activate it
static std::unique_ptr<Innexus::Processor> createAndSetupV9Processor()
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

/// Helper to build a complete v8 state blob (no ADSR data)
static void writeV8StateBlob(V9TestStream& stream,
                             float feedbackAmount = 0.0f,
                             float feedbackDecay = 0.2f)
{
    IBStreamer streamer(&stream, kLittleEndian);

    // Version 8
    streamer.writeInt32(8);

    // M1 parameters
    streamer.writeFloat(100.0f);     // releaseTimeMs
    streamer.writeFloat(0.5f);       // inharmonicityAmount
    streamer.writeFloat(0.8f);       // masterGain
    streamer.writeFloat(0.0f);       // bypass
    streamer.writeInt32(0);          // path length (empty)

    // M2 parameters
    streamer.writeFloat(1.0f);       // harmonicLevel (plain)
    streamer.writeFloat(1.0f);       // residualLevel (plain)
    streamer.writeFloat(0.0f);       // brightness (plain)
    streamer.writeFloat(0.0f);       // transientEmphasis (plain)
    streamer.writeInt32(0);          // residual frame count
    streamer.writeInt32(0);          // fftSize
    streamer.writeInt32(0);          // hopSize

    // M3 parameters
    streamer.writeInt32(0);          // inputSource
    streamer.writeInt32(0);          // latencyMode

    // M4 parameters
    streamer.writeInt8(static_cast<int8>(0)); // freeze
    streamer.writeFloat(0.0f);       // morphPosition
    streamer.writeInt32(0);          // harmonicFilterType
    streamer.writeFloat(0.5f);       // responsiveness

    // M5 parameters
    streamer.writeInt32(0);          // selected slot
    for (int s = 0; s < 8; ++s)
        streamer.writeInt8(static_cast<int8>(0)); // unoccupied

    // M6 parameters (31 floats -- defaults)
    streamer.writeFloat(1.0f);   // timbralBlend (default=1.0)
    streamer.writeFloat(0.0f);   // stereoSpread
    streamer.writeFloat(0.0f);   // evolutionEnable
    streamer.writeFloat(0.0f);   // evolutionSpeed
    streamer.writeFloat(0.5f);   // evolutionDepth (default=0.5)
    streamer.writeFloat(0.0f);   // evolutionMode
    streamer.writeFloat(0.0f);   // mod1Enable
    streamer.writeFloat(0.0f);   // mod1Waveform
    streamer.writeFloat(0.0f);   // mod1Rate
    streamer.writeFloat(0.0f);   // mod1Depth
    streamer.writeFloat(0.0f);   // mod1RangeStart
    streamer.writeFloat(1.0f);   // mod1RangeEnd (default=1.0)
    streamer.writeFloat(0.0f);   // mod1Target
    streamer.writeFloat(0.0f);   // mod2Enable
    streamer.writeFloat(0.0f);   // mod2Waveform
    streamer.writeFloat(0.0f);   // mod2Rate
    streamer.writeFloat(0.0f);   // mod2Depth
    streamer.writeFloat(0.0f);   // mod2RangeStart
    streamer.writeFloat(1.0f);   // mod2RangeEnd (default=1.0)
    streamer.writeFloat(0.0f);   // mod2Target
    streamer.writeFloat(0.0f);   // detuneSpread
    streamer.writeFloat(0.0f);   // blendEnable
    for (int i = 0; i < 8; ++i)
        streamer.writeFloat(0.0f); // blendSlotWeights
    streamer.writeFloat(0.0f);   // blendLiveWeight

    // v7: Harmonic Physics parameters
    streamer.writeFloat(0.0f);   // warmth
    streamer.writeFloat(0.0f);   // coupling
    streamer.writeFloat(0.0f);   // stability
    streamer.writeFloat(0.0f);   // entropy

    // v8: Feedback Loop parameters
    streamer.writeFloat(feedbackAmount);
    streamer.writeFloat(feedbackDecay);

    // NO v9 data -- this is a v8 format blob
}

/// Helper to build a complete v7 state blob (no feedback, no ADSR)
static void writeV7StateBlob(V9TestStream& stream)
{
    IBStreamer streamer(&stream, kLittleEndian);

    // Version 7
    streamer.writeInt32(7);

    // M1 parameters
    streamer.writeFloat(100.0f);     // releaseTimeMs
    streamer.writeFloat(0.5f);       // inharmonicityAmount
    streamer.writeFloat(0.8f);       // masterGain
    streamer.writeFloat(0.0f);       // bypass
    streamer.writeInt32(0);          // path length (empty)

    // M2 parameters
    streamer.writeFloat(1.0f);       // harmonicLevel (plain)
    streamer.writeFloat(1.0f);       // residualLevel (plain)
    streamer.writeFloat(0.0f);       // brightness (plain)
    streamer.writeFloat(0.0f);       // transientEmphasis (plain)
    streamer.writeInt32(0);          // residual frame count
    streamer.writeInt32(0);          // fftSize
    streamer.writeInt32(0);          // hopSize

    // M3 parameters
    streamer.writeInt32(0);          // inputSource
    streamer.writeInt32(0);          // latencyMode

    // M4 parameters
    streamer.writeInt8(static_cast<int8>(0)); // freeze
    streamer.writeFloat(0.0f);       // morphPosition
    streamer.writeInt32(0);          // harmonicFilterType
    streamer.writeFloat(0.5f);       // responsiveness

    // M5 parameters
    streamer.writeInt32(0);          // selected slot
    for (int s = 0; s < 8; ++s)
        streamer.writeInt8(static_cast<int8>(0)); // unoccupied

    // M6 parameters (31 floats -- defaults)
    streamer.writeFloat(1.0f);   // timbralBlend (default=1.0)
    streamer.writeFloat(0.0f);   // stereoSpread
    streamer.writeFloat(0.0f);   // evolutionEnable
    streamer.writeFloat(0.0f);   // evolutionSpeed
    streamer.writeFloat(0.5f);   // evolutionDepth (default=0.5)
    streamer.writeFloat(0.0f);   // evolutionMode
    streamer.writeFloat(0.0f);   // mod1Enable
    streamer.writeFloat(0.0f);   // mod1Waveform
    streamer.writeFloat(0.0f);   // mod1Rate
    streamer.writeFloat(0.0f);   // mod1Depth
    streamer.writeFloat(0.0f);   // mod1RangeStart
    streamer.writeFloat(1.0f);   // mod1RangeEnd (default=1.0)
    streamer.writeFloat(0.0f);   // mod1Target
    streamer.writeFloat(0.0f);   // mod2Enable
    streamer.writeFloat(0.0f);   // mod2Waveform
    streamer.writeFloat(0.0f);   // mod2Rate
    streamer.writeFloat(0.0f);   // mod2Depth
    streamer.writeFloat(0.0f);   // mod2RangeStart
    streamer.writeFloat(1.0f);   // mod2RangeEnd (default=1.0)
    streamer.writeFloat(0.0f);   // mod2Target
    streamer.writeFloat(0.0f);   // detuneSpread
    streamer.writeFloat(0.0f);   // blendEnable
    for (int i = 0; i < 8; ++i)
        streamer.writeFloat(0.0f); // blendSlotWeights
    streamer.writeFloat(0.0f);   // blendLiveWeight

    // v7: Harmonic Physics parameters
    streamer.writeFloat(0.0f);   // warmth
    streamer.writeFloat(0.0f);   // coupling
    streamer.writeFloat(0.0f);   // stability
    streamer.writeFloat(0.0f);   // entropy

    // NO v8 or v9 data
}

// ==============================================================================
// T053(a): State version is 9
// ==============================================================================

TEST_CASE("StateV9: getState writes version 9",
          "[innexus][vst][state][v9][adsr]")
{
    V9TestStream stream;

    auto proc = createAndSetupV9Processor();
    REQUIRE(proc->getState(&stream) == kResultOk);

    // Read back the version from the raw stream
    stream.resetReadPos();
    IBStreamer reader(&stream, kLittleEndian);
    int32 version = 0;
    REQUIRE(reader.readInt32(version));
    REQUIRE(version == 9);

    proc->setActive(false);
    proc->terminate();
}

// ==============================================================================
// T053(b): Save/load round-trip with non-default ADSR values
// ==============================================================================

TEST_CASE("StateV9: save/load roundtrip preserves global ADSR parameters",
          "[innexus][vst][state][v9][adsr]")
{
    // Build a v9 state blob with specific ADSR values by saving from a processor
    // after loading it with a known v9 blob.

    // We'll build the blob manually with known ADSR values.
    V9TestStream stream;
    {
        IBStreamer streamer(&stream, kLittleEndian);

        // Version 9
        streamer.writeInt32(9);

        // M1 parameters
        streamer.writeFloat(100.0f);     // releaseTimeMs
        streamer.writeFloat(0.5f);       // inharmonicityAmount
        streamer.writeFloat(0.8f);       // masterGain
        streamer.writeFloat(0.0f);       // bypass
        streamer.writeInt32(0);          // path length (empty)

        // M2 parameters
        streamer.writeFloat(1.0f);       // harmonicLevel (plain)
        streamer.writeFloat(1.0f);       // residualLevel (plain)
        streamer.writeFloat(0.0f);       // brightness (plain)
        streamer.writeFloat(0.0f);       // transientEmphasis (plain)
        streamer.writeInt32(0);          // residual frame count
        streamer.writeInt32(0);          // fftSize
        streamer.writeInt32(0);          // hopSize

        // M3 parameters
        streamer.writeInt32(0);          // inputSource
        streamer.writeInt32(0);          // latencyMode

        // M4 parameters
        streamer.writeInt8(static_cast<int8>(0)); // freeze
        streamer.writeFloat(0.0f);       // morphPosition
        streamer.writeInt32(0);          // harmonicFilterType
        streamer.writeFloat(0.5f);       // responsiveness

        // M5 parameters
        streamer.writeInt32(0);          // selected slot
        for (int s = 0; s < 8; ++s)
            streamer.writeInt8(static_cast<int8>(0)); // unoccupied

        // M6 parameters (31 floats)
        streamer.writeFloat(1.0f);   // timbralBlend
        streamer.writeFloat(0.0f);   // stereoSpread
        for (int i = 0; i < 29; ++i)
            streamer.writeFloat(0.0f);

        // v7: Harmonic Physics parameters
        streamer.writeFloat(0.0f);   // warmth
        streamer.writeFloat(0.0f);   // coupling
        streamer.writeFloat(0.0f);   // stability
        streamer.writeFloat(0.0f);   // entropy

        // v8: Feedback Loop parameters
        streamer.writeFloat(0.0f);   // feedbackAmount
        streamer.writeFloat(0.2f);   // feedbackDecay

        // v9: ADSR global parameters (9 floats)
        streamer.writeFloat(250.0f);   // adsrAttackMs
        streamer.writeFloat(200.0f);   // adsrDecayMs
        streamer.writeFloat(0.6f);     // adsrSustainLevel
        streamer.writeFloat(300.0f);   // adsrReleaseMs
        streamer.writeFloat(0.7f);     // adsrAmount
        streamer.writeFloat(2.0f);     // adsrTimeScale
        streamer.writeFloat(0.5f);     // adsrAttackCurve
        streamer.writeFloat(-0.3f);    // adsrDecayCurve
        streamer.writeFloat(0.8f);     // adsrReleaseCurve

        // v9: Per-slot ADSR data (8 slots x 9 floats = 72 floats)
        // Slot 0-7: write defaults for all except slot 3
        for (int s = 0; s < 8; ++s)
        {
            if (s == 3)
            {
                // Slot 3 with custom values
                streamer.writeFloat(50.0f);    // adsrAttackMs
                streamer.writeFloat(150.0f);   // adsrDecayMs
                streamer.writeFloat(0.4f);     // adsrSustainLevel
                streamer.writeFloat(250.0f);   // adsrReleaseMs
                streamer.writeFloat(0.9f);     // adsrAmount
                streamer.writeFloat(1.5f);     // adsrTimeScale
                streamer.writeFloat(0.2f);     // adsrAttackCurve
                streamer.writeFloat(-0.5f);    // adsrDecayCurve
                streamer.writeFloat(0.3f);     // adsrReleaseCurve
            }
            else
            {
                // Default values
                streamer.writeFloat(10.0f);    // adsrAttackMs
                streamer.writeFloat(100.0f);   // adsrDecayMs
                streamer.writeFloat(1.0f);     // adsrSustainLevel
                streamer.writeFloat(100.0f);   // adsrReleaseMs
                streamer.writeFloat(0.0f);     // adsrAmount
                streamer.writeFloat(1.0f);     // adsrTimeScale
                streamer.writeFloat(0.0f);     // adsrAttackCurve
                streamer.writeFloat(0.0f);     // adsrDecayCurve
                streamer.writeFloat(0.0f);     // adsrReleaseCurve
            }
        }
    }

    // Load into processor and verify all global ADSR values restored
    {
        auto proc = createAndSetupV9Processor();
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

        // Version 9
        streamer.writeInt32(9);

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

        // v7 physics
        for (int i = 0; i < 4; ++i) streamer.writeFloat(0.0f);
        // v8 feedback
        streamer.writeFloat(0.0f); streamer.writeFloat(0.2f);

        // v9: ADSR global parameters (defaults)
        streamer.writeFloat(10.0f);  streamer.writeFloat(100.0f);
        streamer.writeFloat(1.0f);   streamer.writeFloat(100.0f);
        streamer.writeFloat(0.0f);   streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f);   streamer.writeFloat(0.0f);
        streamer.writeFloat(0.0f);

        // v9: Per-slot ADSR data -- each slot gets a distinct attack time
        for (int s = 0; s < 8; ++s)
        {
            float attackMs = static_cast<float>((s + 1) * 100); // 100, 200, ..., 800
            streamer.writeFloat(attackMs);       // adsrAttackMs
            streamer.writeFloat(100.0f);         // adsrDecayMs
            streamer.writeFloat(1.0f);           // adsrSustainLevel
            streamer.writeFloat(100.0f);         // adsrReleaseMs
            streamer.writeFloat(0.0f);           // adsrAmount
            streamer.writeFloat(1.0f);           // adsrTimeScale
            streamer.writeFloat(0.0f);           // adsrAttackCurve
            streamer.writeFloat(0.0f);           // adsrDecayCurve
            streamer.writeFloat(0.0f);           // adsrReleaseCurve
        }
    }

    // Load and verify each slot's ADSR attack time
    {
        auto proc = createAndSetupV9Processor();
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

    // Save state from processor A with defaults
    {
        auto procA = createAndSetupV9Processor();
        REQUIRE(procA->getState(&stream) == kResultOk);
        procA->setActive(false);
        procA->terminate();
    }

    // Load into processor B and verify ADSR defaults preserved
    {
        auto procB = createAndSetupV9Processor();
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
// T053(e): v8 backward compat -- ADSR defaults to safe values
// ==============================================================================

TEST_CASE("StateV9: loading v8 state defaults ADSR Amount to 0.0 and curves to 0.0",
          "[innexus][vst][state][v9][adsr]")
{
    V9TestStream stream;
    writeV8StateBlob(stream);

    auto proc = createAndSetupV9Processor();
    stream.resetReadPos();
    REQUIRE(proc->setState(&stream) == kResultOk);

    constexpr float kTol = 1e-4f;

    // ADSR params should be at their defaults (bypass)
    REQUIRE(proc->getAdsrAmount() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrAttackMs() == Approx(10.0f).margin(kTol));
    REQUIRE(proc->getAdsrDecayMs() == Approx(100.0f).margin(kTol));
    REQUIRE(proc->getAdsrSustainLevel() == Approx(1.0f).margin(kTol));
    REQUIRE(proc->getAdsrReleaseMs() == Approx(100.0f).margin(kTol));
    REQUIRE(proc->getAdsrTimeScale() == Approx(1.0f).margin(kTol));
    REQUIRE(proc->getAdsrAttackCurve() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrDecayCurve() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrReleaseCurve() == Approx(0.0f).margin(kTol));

    // Per-slot ADSR should also be defaults
    for (int s = 0; s < 8; ++s)
    {
        const auto& slot = proc->getMemorySlot(s);
        REQUIRE(slot.adsrAmount == Approx(0.0f).margin(kTol));
        REQUIRE(slot.adsrAttackMs == Approx(10.0f).margin(kTol));
        REQUIRE(slot.adsrDecayMs == Approx(100.0f).margin(kTol));
        REQUIRE(slot.adsrSustainLevel == Approx(1.0f).margin(kTol));
        REQUIRE(slot.adsrReleaseMs == Approx(100.0f).margin(kTol));
        REQUIRE(slot.adsrTimeScale == Approx(1.0f).margin(kTol));
        REQUIRE(slot.adsrAttackCurve == Approx(0.0f).margin(kTol));
        REQUIRE(slot.adsrDecayCurve == Approx(0.0f).margin(kTol));
        REQUIRE(slot.adsrReleaseCurve == Approx(0.0f).margin(kTol));
    }

    // Verify existing v8 params are still loaded correctly
    REQUIRE(proc->getMasterGain() == Approx(0.8f).margin(kTol));

    proc->setActive(false);
    proc->terminate();
}

// ==============================================================================
// T053(f): v7 backward compat -- ADSR and feedback both default
// ==============================================================================

TEST_CASE("StateV9: loading v7 state defaults ADSR Amount to 0.0 and feedback to defaults",
          "[innexus][vst][state][v9][adsr]")
{
    V9TestStream stream;
    writeV7StateBlob(stream);

    auto proc = createAndSetupV9Processor();
    stream.resetReadPos();
    REQUIRE(proc->setState(&stream) == kResultOk);

    constexpr float kTol = 1e-4f;

    // ADSR params should be at their defaults
    REQUIRE(proc->getAdsrAmount() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrAttackCurve() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrDecayCurve() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrReleaseCurve() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrAttackMs() == Approx(10.0f).margin(kTol));
    REQUIRE(proc->getAdsrDecayMs() == Approx(100.0f).margin(kTol));
    REQUIRE(proc->getAdsrSustainLevel() == Approx(1.0f).margin(kTol));
    REQUIRE(proc->getAdsrReleaseMs() == Approx(100.0f).margin(kTol));
    REQUIRE(proc->getAdsrTimeScale() == Approx(1.0f).margin(kTol));

    // Feedback params should also be at their defaults (no v8 data)
    REQUIRE(proc->getFeedbackAmount() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getFeedbackDecay() == Approx(0.2f).margin(kTol));

    proc->setActive(false);
    proc->terminate();
}

// ==============================================================================
// T053(g): v8 state loaded after v9 state resets ADSR to defaults
// ==============================================================================

TEST_CASE("StateV9: v8 state loaded after v9 state resets ADSR params to defaults",
          "[innexus][vst][state][v9][adsr]")
{
    V9TestStream streamV9;
    V9TestStream streamV8;

    // Build a v9 state blob with non-default ADSR values
    {
        IBStreamer streamer(&streamV9, kLittleEndian);

        streamer.writeInt32(9);
        // M1
        streamer.writeFloat(100.0f); streamer.writeFloat(0.5f);
        streamer.writeFloat(0.8f);   streamer.writeFloat(0.0f);
        streamer.writeInt32(0);
        // M2
        streamer.writeFloat(1.0f); streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
        streamer.writeInt32(0); streamer.writeInt32(0); streamer.writeInt32(0);
        // M3
        streamer.writeInt32(0); streamer.writeInt32(0);
        // M4
        streamer.writeInt8(static_cast<int8>(0));
        streamer.writeFloat(0.0f); streamer.writeInt32(0); streamer.writeFloat(0.5f);
        // M5
        streamer.writeInt32(0);
        for (int s = 0; s < 8; ++s) streamer.writeInt8(static_cast<int8>(0));
        // M6 (31 floats)
        streamer.writeFloat(1.0f); streamer.writeFloat(0.0f);
        for (int i = 0; i < 29; ++i) streamer.writeFloat(0.0f);
        // v7 physics
        for (int i = 0; i < 4; ++i) streamer.writeFloat(0.0f);
        // v8 feedback
        streamer.writeFloat(0.0f); streamer.writeFloat(0.2f);
        // v9: ADSR global
        streamer.writeFloat(500.0f);   // adsrAttackMs (non-default)
        streamer.writeFloat(200.0f);   // adsrDecayMs
        streamer.writeFloat(0.3f);     // adsrSustainLevel
        streamer.writeFloat(400.0f);   // adsrReleaseMs
        streamer.writeFloat(0.9f);     // adsrAmount (non-default)
        streamer.writeFloat(2.5f);     // adsrTimeScale
        streamer.writeFloat(0.7f);     // adsrAttackCurve (non-default)
        streamer.writeFloat(-0.8f);    // adsrDecayCurve
        streamer.writeFloat(0.6f);     // adsrReleaseCurve
        // v9: Per-slot ADSR (72 floats -- defaults)
        for (int s = 0; s < 8; ++s)
        {
            streamer.writeFloat(10.0f); streamer.writeFloat(100.0f);
            streamer.writeFloat(1.0f);  streamer.writeFloat(100.0f);
            streamer.writeFloat(0.0f);  streamer.writeFloat(1.0f);
            streamer.writeFloat(0.0f);  streamer.writeFloat(0.0f);
            streamer.writeFloat(0.0f);
        }
    }

    // Build a v8 state
    writeV8StateBlob(streamV8);

    auto proc = createAndSetupV9Processor();

    // Load v9 state first
    streamV9.resetReadPos();
    REQUIRE(proc->setState(&streamV9) == kResultOk);
    REQUIRE(proc->getAdsrAmount() == Approx(0.9f).margin(1e-4f));
    REQUIRE(proc->getAdsrAttackMs() == Approx(500.0f).margin(1e-4f));

    // Now load v8 state -- should reset ADSR params to defaults
    streamV8.resetReadPos();
    REQUIRE(proc->setState(&streamV8) == kResultOk);

    constexpr float kTol = 1e-4f;
    REQUIRE(proc->getAdsrAmount() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrAttackMs() == Approx(10.0f).margin(kTol));
    REQUIRE(proc->getAdsrDecayMs() == Approx(100.0f).margin(kTol));
    REQUIRE(proc->getAdsrSustainLevel() == Approx(1.0f).margin(kTol));
    REQUIRE(proc->getAdsrReleaseMs() == Approx(100.0f).margin(kTol));
    REQUIRE(proc->getAdsrAttackCurve() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrDecayCurve() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getAdsrReleaseCurve() == Approx(0.0f).margin(kTol));

    proc->setActive(false);
    proc->terminate();
}
