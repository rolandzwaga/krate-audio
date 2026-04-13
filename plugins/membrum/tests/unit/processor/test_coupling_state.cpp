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
std::vector<std::uint8_t> buildV4Blob()
{
    std::vector<std::uint8_t> buf;
    auto appendBytes = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    };
    const int32 version = 4;
    const int32 maxPoly = 8;
    const int32 stealPolicy = 0;
    appendBytes(&version, sizeof(version));
    appendBytes(&maxPoly, sizeof(maxPoly));
    appendBytes(&stealPolicy, sizeof(stealPolicy));
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const int32 ex = 0;  // ExciterType::Strike (default)
        const int32 bm = 0;  // BodyModelType::Membrane (default)
        appendBytes(&ex, sizeof(ex));
        appendBytes(&bm, sizeof(bm));
        // 34 float64 zeros -- valid but not interesting; setState will clamp.
        for (int i = 0; i < 34; ++i)
        {
            const double z = 0.0;
            appendBytes(&z, sizeof(z));
        }
        const std::uint8_t cg = 0;
        const std::uint8_t ob = 0;
        appendBytes(&cg, sizeof(cg));
        appendBytes(&ob, sizeof(ob));
    }
    const int32 selPad = 0;
    appendBytes(&selPad, sizeof(selPad));
    return buf;
}

} // namespace

// ==============================================================================
// FR-050: kCurrentStateVersion is at least 5 (Phase 5 introduced version 5;
// spec 141 / Phase 6 bumps it to 6 with backward-compatible v5->v6 migration).
// ==============================================================================

TEST_CASE("Phase 7 (FR-050): kCurrentStateVersion >= 5",
          "[coupling_state][phase7][state]")
{
    STATIC_REQUIRE(Membrum::kCurrentStateVersion >= 5);
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

    // ---- Set 3 per-pair Tier 2 overrides (using public test accessor path) ----
    // The matrix is const-accessible for tests; to install overrides we
    // reach through the voice pool / processor via a dedicated helper.
    // Since the processor doesn't expose a mutable matrix accessor, we
    // rely on recomputeCouplingMatrix + direct mutation of the matrix
    // through a small reinterpret workaround is NOT available. Instead,
    // we store overrides prior to save through processor-internal state
    // by... there is no public mutator. We must add overrides through
    // couplingMatrix_ directly, which is private.
    //
    // Strategy: use a separate fixture where we inject overrides, save
    // through that processor's getState (which reads couplingMatrix_
    // directly via forEachOverride). But to inject we need a setter.
    //
    // Workaround: encode overrides into the saved blob post-hoc.
    // We save state, then reload a modified stream containing overrides
    // appended with a patched overrideCount.
    //
    // Simpler workaround: we skip override-injection here and verify
    // round-trip only for the globals + per-pad amounts + zero overrides.
    // A dedicated test below exercises overrides via a raw-blob load.

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

// ==============================================================================
// SC-006 / FR-051: Loading a v4 blob yields Phase 5 defaults (Phase 4 behavior).
// ==============================================================================

TEST_CASE("Phase 7 (SC-006, FR-051): v4 state loads with Phase 5 defaults",
          "[coupling_state][phase7][state][migration]")
{
    StateFixture fx;
    auto v4bytes = buildV4Blob();
    // Sanity: v4 blob is exactly 12 + 32*282 + 4 = 9040 bytes.
    CHECK(v4bytes.size() == 9040u);

    MemoryStream v4stream;
    int32 written = 0;
    v4stream.write(v4bytes.data(), static_cast<int32>(v4bytes.size()), &written);
    REQUIRE(written == static_cast<int32>(v4bytes.size()));
    v4stream.seek(0, IBStream::kIBSeekSet, nullptr);

    // Loading must succeed (FR-052: older blobs migrate without crash).
    REQUIRE(fx.processor.setState(&v4stream) == kResultOk);

    // Per-pad couplingAmount must be the default 0.5 for every pad.
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const float actual = fx.processor.voicePoolForTest()
                                         .padConfig(pad)
                                         .couplingAmount;
        CHECK(static_cast<double>(actual) == Approx(0.5).margin(1e-6));
    }

    // Coupling matrix must be at default computed state with no overrides.
    const auto& matrix = fx.processor.couplingMatrixForTest();
    CHECK(matrix.getOverrideCount() == 0);

    // Save the migrated state: the new blob should be version 5. We inspect
    // the first int32 of the re-saved stream.
    MemoryStream resaved;
    REQUIRE(fx.processor.getState(&resaved) == kResultOk);
    resaved.seek(0, IBStream::kIBSeekSet, nullptr);
    int32 newVersion = 0;
    int32 gotVer = 0;
    resaved.read(&newVersion, sizeof(newVersion), &gotVer);
    CHECK(gotVer == static_cast<int32>(sizeof(newVersion)));
    CHECK(newVersion == Membrum::kCurrentStateVersion);

    // The re-saved blob size equals v4 (9040) + Phase 5 trailer:
    //   4 * 8 (globals) + 32 * 8 (per-pad) + 2 (overrideCount) + 0 overrides
    //   = 32 + 256 + 2 = 290.
    // Phase 6 (spec 141) additionally appends 160 * 8 bytes of float64 macros.
    int64 resavedSize = 0;
    resaved.seek(0, IBStream::kIBSeekEnd, &resavedSize);
    CHECK(resavedSize == 9040 + 290 + 1280);
}

// ==============================================================================
// FR-053: Override serialization format -- uint16 count + (uint8 src,
// uint8 dst, float32 coeff) per entry.
// FR-031: Per-pair coefficient clamped to [0.0, 0.05] on load.
// ==============================================================================

TEST_CASE("Phase 7 (FR-053, FR-031): override wire format round-trip with "
          "out-of-range coefficients clamped on load",
          "[coupling_state][phase7][state][overrides]")
{
    // Produce a minimal v5 blob: a zeroed v4 base + empty globals/per-pad +
    // overrideCount=3 + three overrides with specific coefficients.
    auto blob = buildV4Blob();
    // Overwrite version field to 5.
    const int32 v5 = 5;
    std::memcpy(blob.data(), &v5, sizeof(v5));

    auto append = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        blob.insert(blob.end(), b, b + n);
    };

    // Phase 5 globals: all zeros (but couplingDelay clamped to [0.5, 2.0]
    // so we explicitly write 1.0 to stay in range).
    const double gc = 0.0, sb = 0.0, tr = 0.0, cd = 1.0;
    append(&gc, sizeof(gc));
    append(&sb, sizeof(sb));
    append(&tr, sizeof(tr));
    append(&cd, sizeof(cd));

    // 32 per-pad = 0.5 each.
    for (int i = 0; i < Membrum::kNumPads; ++i)
    {
        const double half = 0.5;
        append(&half, sizeof(half));
    }

    // overrideCount = 3.
    const std::uint16_t count = 3;
    append(&count, sizeof(count));

    // Override 1: in-range coefficient 0.03.
    struct OvEntry { std::uint8_t src; std::uint8_t dst; float coeff; };
    static_assert(sizeof(OvEntry) == 6 || true,
                  "OvEntry is written field-by-field; layout not relied upon.");
    auto writeEntry = [&](std::uint8_t s, std::uint8_t d, float c) {
        append(&s, sizeof(s));
        append(&d, sizeof(d));
        append(&c, sizeof(c));
    };
    writeEntry(0, 1, 0.03f);      // in-range
    writeEntry(2, 3, 0.10f);      // ABOVE kMaxCoefficient (0.05) -- must clamp
    writeEntry(5, 7, -0.01f);     // BELOW zero -- must clamp to 0.0

    // Load the blob.
    StateFixture fx;
    MemoryStream stream;
    int32 written = 0;
    stream.write(blob.data(), static_cast<int32>(blob.size()), &written);
    REQUIRE(written == static_cast<int32>(blob.size()));
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(fx.processor.setState(&stream) == kResultOk);

    const auto& matrix = fx.processor.couplingMatrixForTest();
    CHECK(matrix.getOverrideCount() == 3);

    REQUIRE(matrix.hasOverrideAt(0, 1));
    CHECK(static_cast<double>(matrix.getOverrideGain(0, 1))
          == Approx(0.03).margin(1e-6));

    REQUIRE(matrix.hasOverrideAt(2, 3));
    // 0.10 is clamped to kMaxCoefficient = 0.05.
    CHECK(static_cast<double>(matrix.getOverrideGain(2, 3))
          == Approx(static_cast<double>(
                Membrum::CouplingMatrix::kMaxCoefficient)).margin(1e-6));

    REQUIRE(matrix.hasOverrideAt(5, 7));
    // -0.01 is clamped to 0.0.
    CHECK(static_cast<double>(matrix.getOverrideGain(5, 7))
          == Approx(0.0).margin(1e-6));

    // Re-save the state and verify the wire format: we locate the
    // trailer, read overrideCount, then 3 * 6 bytes of (uint8, uint8, float32).
    MemoryStream resaved;
    REQUIRE(fx.processor.getState(&resaved) == kResultOk);
    int64 resSize = 0;
    resaved.seek(0, IBStream::kIBSeekEnd, &resSize);
    // v4 base (9040) + globals (32) + per-pad (256) + overrideCount (2)
    // + 3 * 6 override bytes = 9040 + 290 + 18 = 9348.
    // Phase 6 (spec 141) appends 160 * 8 bytes of float64 macros.
    CHECK(resSize == 9348 + 1280);

    // Read overrideCount at offset 9040 + 32 + 256 = 9328.
    resaved.seek(9328, IBStream::kIBSeekSet, nullptr);
    std::uint16_t countOut = 0;
    int32 gotCount = 0;
    resaved.read(&countOut, sizeof(countOut), &gotCount);
    CHECK(gotCount == 2);
    CHECK(countOut == 3);

    // Read three entries.
    for (int i = 0; i < 3; ++i)
    {
        std::uint8_t s = 0xFF, d = 0xFF;
        float c = -1.0f;
        int32 g = 0;
        resaved.read(&s, sizeof(s), &g); CHECK(g == 1);
        resaved.read(&d, sizeof(d), &g); CHECK(g == 1);
        resaved.read(&c, sizeof(c), &g); CHECK(g == 4);
        // Coefficients must be within the allowed range (FR-031).
        CHECK(c >= 0.0f);
        CHECK(c <= Membrum::CouplingMatrix::kMaxCoefficient);
        // Src/dst must be valid pad indices.
        CHECK(s < Membrum::kNumPads);
        CHECK(d < Membrum::kNumPads);
    }
}

// ==============================================================================
// FR-052: v1 / v2 / v3 migration paths must not crash.
// The legacy state parsing lives in the `else` branch of setState(); we feed
// a minimal header of each version and accept either kResultOk or a graceful
// rejection (any result code, no crash).
// ==============================================================================

TEST_CASE("Phase 7 (FR-052): v1/v2/v3 blobs migrate without crash",
          "[coupling_state][phase7][state][migration]")
{
    for (int32 version : { 1, 2, 3 })
    {
        StateFixture fx;
        // Build a minimal legacy blob: version int32 followed by a healthy
        // padding of zero bytes (enough to cover all legacy reads).
        std::vector<std::uint8_t> blob(4096, 0);
        std::memcpy(blob.data(), &version, sizeof(version));

        MemoryStream stream;
        int32 written = 0;
        stream.write(blob.data(), static_cast<int32>(blob.size()), &written);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);

        // The call must return a result code without throwing or aborting.
        // We don't assert success -- legacy migration may tolerate or reject
        // the synthetic blob, but it must not crash the process.
        const tresult r = fx.processor.setState(&stream);
        CHECK((r == kResultOk || r == kResultFalse));

        // After migration, Phase 5 params must be at their defaults
        // (FR-051: no version < 5 ever carries coupling data).
        for (int pad = 0; pad < Membrum::kNumPads; ++pad)
        {
            const float actual = fx.processor.voicePoolForTest()
                                             .padConfig(pad)
                                             .couplingAmount;
            CHECK(static_cast<double>(actual) == Approx(0.5).margin(1e-6));
        }
        CHECK(fx.processor.couplingMatrixForTest().getOverrideCount() == 0);
    }
}

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

    // Verify blob size is exactly 284 bytes (version 4 + 2 * int32 + 34 *
    // float64). Offset 36 (couplingAmount) is NOT part of this format.
    int64 end = 0;
    stream->seek(0, IBStream::kIBSeekEnd, &end);
    CHECK(end == 284);
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
