// ==============================================================================
// Coupling State Tests -- State v5 round-trip, v4 migration, preset exclusion
//
// Phase 7 (spec 140): Covers FR-050 (kCurrentStateVersion == 5),
// FR-051 (v4 migration to Phase 4 behavior), FR-052 (older blob migration
// without crash), FR-053 (override serialization format), FR-031 (per-pair
// coefficient clamp [0, 0.05]), SC-005 (zero-loss round-trip), SC-006
// (v4 blob yields all Phase 5 defaults).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/coupling_matrix.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "voice_pool/voice_pool.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// ==============================================================================
// Minimal parameter-change plumbing (mirrors test_coupling_integration.cpp)
// Used to drive Phase 5 globals (which are not directly settable on Processor).
// ==============================================================================

namespace {

class SingleParamQueueST : public IParamValueQueue
{
public:
    SingleParamQueueST(ParamID id, ParamValue value) : id_(id), value_(value) {}
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& value) override
    {
        if (index != 0) return kResultFalse;
        sampleOffset = 0;
        value = value_;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
private:
    ParamID id_;
    ParamValue value_;
};

class MultiParamChangesST : public IParameterChanges
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override
    {
        return static_cast<int32>(queues_.size());
    }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
    {
        if (index < 0 || index >= static_cast<int32>(queues_.size())) return nullptr;
        return queues_[static_cast<size_t>(index)].get();
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override
    {
        return nullptr;
    }
    void add(ParamID id, ParamValue value)
    {
        queues_.push_back(std::make_unique<SingleParamQueueST>(id, value));
    }
private:
    std::vector<std::unique_ptr<SingleParamQueueST>> queues_;
};

// Fixture: initialized processor ready for state save/load.
struct StateFixture
{
    Membrum::Processor processor;
    std::vector<float> outL;
    std::vector<float> outR;
    float* outCh[2];
    AudioBusBuffers outBus{};
    ProcessData data{};

    StateFixture()
        : outL(128, 0.0f), outR(128, 0.0f)
    {
        outCh[0] = outL.data();
        outCh[1] = outR.data();
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outCh;
        outBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = 128;
        data.numOutputs = 1;
        data.outputs = &outBus;

        processor.initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 128;
        setup.sampleRate = 44100.0;
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~StateFixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    // Send a single parameter change via a zero-length-equivalent process block.
    void applyParam(ParamID id, ParamValue normalized)
    {
        MultiParamChangesST changes;
        changes.add(id, normalized);
        data.inputParameterChanges = &changes;
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        data.inputParameterChanges = nullptr;
    }
};

// Build a synthetic v4 state blob matching the getState() layout pre-Phase-5:
//   [int32 version=4][int32 maxPoly][int32 stealPolicy]
//   32 * ([int32 exciter][int32 body][34 * float64][uint8 cg][uint8 ob])
//   [int32 selectedPadIndex]
// Total: 12 + 32 * 282 + 4 = 9040 bytes.
} // namespace

// ==============================================================================
// kCurrentStateVersion sanity check. Pre-release reset pins this at 1; the
// previous "FR-050 / >= 5" assertion no longer applies because legacy state
// support has been dropped.
// ==============================================================================

TEST_CASE("kCurrentStateVersion is the pinned pre-release value",
          "[coupling_state][phase7][state]")
{
    STATIC_REQUIRE(Membrum::kCurrentStateVersion == 1);
}

// ==============================================================================
// SC-005 / FR-053: Non-default Phase 5 values round-trip through save/load.
// ==============================================================================

TEST_CASE("Phase 7 (SC-005): state v5 round-trip with non-default globals + "
          "per-pad amounts + overrides",
          "[coupling_state][phase7][state]")
{
    StateFixture fx;

    // ---- Set global Phase 5 params via parameter changes ----
    fx.applyParam(Membrum::kGlobalCouplingId, 0.7);   // -> 0.7
    fx.applyParam(Membrum::kSnareBuzzId,      0.4);   // -> 0.4
    fx.applyParam(Membrum::kTomResonanceId,   0.3);   // -> 0.3
    // Coupling delay normalized [0..1] maps to [0.5..2.0] ms.
    // 1.5 ms = 0.5 + clamped * 1.5 -> clamped = 2/3.
    fx.applyParam(Membrum::kCouplingDelayId,  2.0 / 3.0);

    // ---- Set 32 distinct per-pad couplingAmounts ----
    std::array<float, Membrum::kNumPads> expectedPerPad{};
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        // Choose values that will uniquely identify each pad post-roundtrip,
        // within [0, 1]. Steps of ~0.03 + offset to avoid defaults.
        const float v = 0.01f + static_cast<float>(pad) * (0.98f / 31.0f);
        expectedPerPad[static_cast<size_t>(pad)] = v;
        fx.processor.voicePoolForTest().padConfigMut(pad).couplingAmount = v;
    }
    // Bake per-pad into matrix (normally done via param change).
    // recomputeCouplingMatrix is private; triggering a Tier 1 knob change
    // refreshes the matrix with current per-pad amounts.
    fx.applyParam(Membrum::kSnareBuzzId, 0.4);

    // (Tier 2 per-pair overrides were removed in v14; this test now verifies
    // only the globals + per-pad amounts round-trip.)

    // ---- Save state to stream ----
    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);

    // ---- Reset processor state by creating a new fixture, then load ----
    StateFixture fx2;
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(fx2.processor.setState(&stream) == kResultOk);

    // ---- Verify globals round-tripped ----
    // We can't read the atomics directly, but we can read them back via
    // getState output and compare stream bytes. The public surface that
    // exposes these values is another getState() into a second stream.
    MemoryStream roundTripStream;
    REQUIRE(fx2.processor.getState(&roundTripStream) == kResultOk);

    // Cheaper: compare the two serialized blobs byte-for-byte. They must
    // be identical for a true round-trip (zero loss -- SC-005).
    int64 size1 = 0;
    int64 size2 = 0;
    stream.seek(0, IBStream::kIBSeekEnd, &size1);
    roundTripStream.seek(0, IBStream::kIBSeekEnd, &size2);
    CHECK(size1 == size2);

    std::vector<std::uint8_t> bytes1(static_cast<size_t>(size1));
    std::vector<std::uint8_t> bytes2(static_cast<size_t>(size2));
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    roundTripStream.seek(0, IBStream::kIBSeekSet, nullptr);
    int32 got1 = 0, got2 = 0;
    stream.read(bytes1.data(), static_cast<int32>(size1), &got1);
    roundTripStream.read(bytes2.data(), static_cast<int32>(size2), &got2);
    REQUIRE(got1 == static_cast<int32>(size1));
    REQUIRE(got2 == static_cast<int32>(size2));
    CHECK(bytes1 == bytes2);

    // Spot check: per-pad couplingAmount values loaded into the second
    // processor match expected values (within float64 tolerance).
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const float actual = fx2.processor.voicePoolForTest()
                                          .padConfig(pad)
                                          .couplingAmount;
        CHECK(static_cast<double>(actual) ==
              Approx(static_cast<double>(expectedPerPad[static_cast<size_t>(pad)]))
                  .margin(1e-6));
    }
}

// (Override wire-format round-trip test removed along with the Tier 2
// coupling-matrix override layer in v14. Legacy v6..v13 blobs that carry an
// override block still load (the codec parses-and-discards those bytes), but
// no path produces overrides anymore.)

// (FR-052 v1/v2/v3 migration-without-crash test removed along with pre-v6
// migration code paths.)

// ==============================================================================
// T044b (FR-022): per-pad sound presets MUST NOT carry couplingAmount.
// Per-pad presets save individual pad sounds (e.g. "Kick 808"); coupling is a
// kit-level concern. Saving a pad preset then reloading it must leave the
// pad's couplingAmount at whatever it was before the load (i.e. NOT clobbered
// by the preset blob).
// ==============================================================================

TEST_CASE("Phase 6 (T044b): per-pad preset excludes couplingAmount (FR-022)",
          "[coupling][phase6][preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Select pad 2 and set a distinctive couplingAmount we want to preserve.
    const int pad = 2;
    const auto padCouplingId = static_cast<ParamID>(
        Membrum::padParamId(pad, Membrum::kPadCouplingAmount));
    controller.setParamNormalized(Membrum::kSelectedPadId,
                                  static_cast<double>(pad) / 31.0);
    controller.setParamNormalized(padCouplingId, 0.9);
    const double couplingBeforeSave = controller.getParamNormalized(padCouplingId);
    REQUIRE(couplingBeforeSave == Approx(0.9).margin(1e-6));

    // Save the per-pad preset stream.
    IBStream* stream = controller.padPresetStateProvider();
    REQUIRE(stream != nullptr);

    // Verify blob size is exactly 420 bytes (v6: version 4 + 2 * int32 + 51 *
    // float64). Offset 36 (couplingAmount) is NOT part of this format.
    int64 end = 0;
    stream->seek(0, IBStream::kIBSeekEnd, &end);
    CHECK(end == 420);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    // Now change the pad's couplingAmount to a different value before reload.
    controller.setParamNormalized(padCouplingId, 0.2);
    const double couplingBeforeLoad = controller.getParamNormalized(padCouplingId);
    REQUIRE(couplingBeforeLoad == Approx(0.2).margin(1e-6));

    // Reload the preset -- couplingAmount must NOT be restored by this load.
    // It must stay at 0.2 (the pre-load value) rather than revert to 0.9.
    const bool ok = controller.padPresetLoadProvider(stream);
    CHECK(ok);

    const double couplingAfterLoad = controller.getParamNormalized(padCouplingId);
    CHECK(couplingAfterLoad == Approx(0.2).margin(1e-6));
    CHECK(couplingAfterLoad != Approx(0.9).margin(1e-6));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}
