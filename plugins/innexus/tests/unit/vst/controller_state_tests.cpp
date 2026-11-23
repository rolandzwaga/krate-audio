// ==============================================================================
// Controller State Round-Trip Tests
// ==============================================================================
// WI-4: Controller::setComponentState() historically skipped the two Sympathetic
// Resonance floats that Processor::getState() writes just before the instance-ID
// trailer. That left kSympatheticAmountId/kSympatheticDecayId at their defaults
// on every reload AND desynchronized the stream by 8 bytes so the SharedDisplay
// instance-ID marker was never recognized. This verifies both are restored.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include "test_helpers/vst_param_changes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

// Minimal in-memory IBStream (read/write/seek) for state round-trips.
class MemStream : public IBStream
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
    const std::vector<char>& bytes() const { return data_; }

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

std::unique_ptr<Innexus::Processor> makeProcessor(FUnknown* context = nullptr)
{
    auto proc = std::make_unique<Innexus::Processor>();
    proc->initialize(context);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 128;
    setup.sampleRate = 44100.0;
    proc->setupProcessing(setup);
    proc->setActive(true);
    return proc;
}

// Apply parameter changes by driving one process() block.
void applyParams(Innexus::Processor& proc, Krate::Test::ParameterChanges& changes)
{
    constexpr int32 kBlock = 128;
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    float* channels[2] = {outL.data(), outR.data()};

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = channels;

    ProcessData data{};
    data.numSamples = kBlock;
    data.numInputs = 0;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputParameterChanges = &changes;

    proc.process(data);
}

} // namespace

TEST_CASE("ControllerState: setComponentState restores Sympathetic params and consumes instance-id (WI-4)",
          "[innexus][vst][state][sympathetic]")
{
    constexpr double kAmount = 0.7;
    constexpr double kDecay = 0.3;

    MemStream stream;

    {
        auto proc = makeProcessor();
        // Sympathetic defaults are amount=0.0, decay=0.5 — set both to non-defaults.
        Krate::Test::ParameterChanges changes;
        changes.addChange(Innexus::kSympatheticAmountId, kAmount);
        changes.addChange(Innexus::kSympatheticDecayId, kDecay);
        applyParams(*proc, changes);

        REQUIRE(proc->getState(&stream) == kResultOk);
        proc->setActive(false);
        proc->terminate();
    }

    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);
    stream.resetReadPos();
    REQUIRE(controller.setComponentState(&stream) == kResultOk);

    constexpr double kTol = 1e-3;
    REQUIRE(controller.getParamNormalized(Innexus::kSympatheticAmountId)
            == Approx(kAmount).margin(kTol));
    REQUIRE(controller.getParamNormalized(Innexus::kSympatheticDecayId)
            == Approx(kDecay).margin(kTol));

    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// One preset-load path (R-200, R-201)
// ==============================================================================
// A preset-browser load used to run a second hand-written parser in the
// controller that never reached Processor::setState: global ADSR, everything
// after Partial Count, the memory-slot snapshots and the per-slot ADSR were
// skipped. Both parsers also skipped the Mod 1/2 tempo-sync fields.
// ==============================================================================

namespace {

// A host's component handler: records every performEdit the controller sends,
// so the test can forward them to the processor as a host would.
class RecordingComponentHandler final : public IComponentHandler
{
public:
    std::vector<std::pair<ParamID, ParamValue>> edits;

    tresult PLUGIN_API beginEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
    {
        edits.emplace_back(id, value);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 /*flags*/) override { return kResultOk; }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid))
        {
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    // Owned by the test: reference counting is a no-op.
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
};

std::vector<char> processorStateBytes(Innexus::Processor& proc)
{
    MemStream stream;
    REQUIRE(proc.getState(&stream) == kResultOk);
    return stream.bytes();
}

// Field-aware comparison of two Processor::getState blobs. The browser path
// delivers values as normalized performEdits that the processor maps back to
// plain (e.g. release time 20 * 250^v), and that round trip is exact under IEEE
// but an ulp off under the macOS leg's -ffast-math (100 ms -> 99.99999 ms). So
// integers and flags must match exactly; floats within 1e-5 relative.
// The walk mirrors getState (processor_state.cpp): after the memory slots,
// everything up to the trailing int32 marker + int64 instance ID is floats.
struct StateDiff
{
    size_t mismatches = 0;
    size_t firstOffset = 0;
    bool layoutOk = true;
};

StateDiff compareProcessorStates(const std::vector<char>& got,
                                 const std::vector<char>& want)
{
    StateDiff diff;
    if (got.size() != want.size())
    {
        diff.layoutOk = false;
        return diff;
    }
    size_t pos = 0;
    const auto fail = [&](size_t at) {
        if (diff.mismatches++ == 0)
            diff.firstOffset = at;
    };
    const auto exact = [&](size_t n) {
        if (pos + n > want.size()) { diff.layoutOk = false; pos = want.size(); return; }
        if (std::memcmp(got.data() + pos, want.data() + pos, n) != 0)
            fail(pos);
        pos += n;
    };
    const auto readI32 = [&](size_t at) {
        int32 v = 0;
        if (at + 4 <= want.size())
            std::memcpy(&v, want.data() + at, 4);
        return v;
    };
    const auto i32 = [&] { const int32 v = readI32(pos); exact(4); return v; };
    const auto i8 = [&] {
        const auto v = (pos < want.size()) ? want[pos] : char{0};
        exact(1);
        return v;
    };
    const auto f32 = [&](size_t n) {
        for (size_t k = 0; k < n; ++k)
        {
            if (pos + 4 > want.size()) { diff.layoutOk = false; pos = want.size(); return; }
            float a = 0.0f;
            float b = 0.0f;
            std::memcpy(&a, got.data() + pos, 4);
            std::memcpy(&b, want.data() + pos, 4);
            const float scale = std::max(std::abs(a), std::abs(b));
            if (a != b && !(std::abs(a - b) <= 1e-5f * scale))
                fail(pos);
            pos += 4;
        }
    };

    i32();                                   // version
    f32(4);                                  // M1
    const int32 pathLen = i32();
    exact(static_cast<size_t>(std::max(pathLen, 0)));
    f32(4);                                  // M2 levels
    const int32 frames = i32();
    i32();                                   // FFT size
    i32();                                   // hop size
    for (int32 fr = 0; fr < frames; ++fr)
    {
        f32(Krate::DSP::kResidualBands + 1);
        i8();                                // transient flag
    }
    i32();                                   // input source
    i32();                                   // latency mode
    i8();                                    // freeze
    f32(1);                                  // morph position
    i32();                                   // harmonic filter type
    f32(1);                                  // responsiveness
    i32();                                   // selected slot
    for (int s = 0; s < 8; ++s)
    {
        if (i8() != 0)                       // occupied
        {
            f32(1);                          // f0 reference
            i32();                           // partial count
            f32(4 * Krate::DSP::kMaxPartials + Krate::DSP::kResidualBands + 4);
        }
    }
    constexpr size_t kTail = 4 + 8;          // marker + instance ID
    if (pos + kTail > want.size() || (want.size() - pos - kTail) % 4 != 0)
    {
        diff.layoutOk = false;
        return diff;
    }
    f32((want.size() - pos - kTail) / 4);
    exact(kTail);
    return diff;
}

void writeBytes(MemStream& stream, const std::vector<char>& bytes)
{
    stream.write(const_cast<char*>(bytes.data()),
                 static_cast<int32>(bytes.size()), nullptr);
    stream.resetReadPos();
}

// Preset A (variantB = false) or B. They differ in global ADSR, voice mode, an
// impact-exciter, a waveguide, a bow, a body and a sympathetic param, Mod 1
// tempo sync, one memory-slot snapshot (B imports one into slot 4) and the
// per-slot ADSR of slot 1 (captured from each preset's global ADSR).
std::vector<char> makePresetState(bool variantB)
{
    auto proc = makeProcessor();
    const auto pick = [variantB](double a, double b) { return variantB ? b : a; };

    Krate::Test::ParameterChanges changes;
    changes.addChange(Innexus::kAdsrAttackId, pick(0.25, 0.5));
    changes.addChange(Innexus::kAdsrDecayId, pick(0.375, 0.625));
    changes.addChange(Innexus::kAdsrSustainId, pick(0.75, 0.25));
    changes.addChange(Innexus::kAdsrReleaseId, pick(0.3125, 0.6875));
    changes.addChange(Innexus::kAdsrAmountId, pick(0.5, 0.875));
    changes.addChange(Innexus::kAdsrTimeScaleId, pick(0.125, 0.75));
    changes.addChange(Innexus::kAdsrAttackCurveId, pick(0.25, 0.75));
    changes.addChange(Innexus::kAdsrDecayCurveId, pick(0.625, 0.375));
    changes.addChange(Innexus::kAdsrReleaseCurveId, pick(0.125, 0.875));
    changes.addChange(Innexus::kVoiceModeId, pick(0.0, 1.0));
    changes.addChange(Innexus::kImpactHardnessId, pick(0.25, 0.75));
    changes.addChange(Innexus::kWaveguideStiffnessId, pick(0.125, 0.625));
    changes.addChange(Innexus::kBowPressureId, pick(0.375, 0.875));
    changes.addChange(Innexus::kBodySizeId, pick(0.25, 0.8125));
    changes.addChange(Innexus::kSympatheticAmountId, pick(0.125, 0.5625));
    changes.addChange(Innexus::kMod1RateSyncId, pick(1.0, 0.0));
    changes.addChange(Innexus::kMod1NoteValueId, pick(4.0 / 20.0, 15.0 / 20.0));
    changes.addChange(Innexus::kMemorySlotId, 1.0 / 7.0);
    applyParams(*proc, changes);

    // Capture into slot 1: stores the global ADSR as that slot's ADSR.
    Krate::Test::ParameterChanges capture;
    capture.addChange(Innexus::kMemoryCaptureId, 1.0);
    applyParams(*proc, capture);
    REQUIRE(proc->getMemorySlot(1).occupied);

    if (variantB)
    {
        Krate::DSP::HarmonicSnapshot snap{};
        snap.f0Reference = 220.0f;
        snap.numPartials = 4;
        for (size_t i = 0; i < 4; ++i)
        {
            snap.relativeFreqs[i] = static_cast<float>(i + 1);
            snap.normalizedAmps[i] = 0.5f;
        }
        snap.globalAmplitude = 0.25f;
        HostMessage msg;
        msg.setMessageID("HarmonicSnapshotImport");
        msg.getAttributes()->setInt("slotIndex", 4);
        msg.getAttributes()->setBinary("snapshotData", &snap, sizeof(snap));
        REQUIRE(proc->notify(&msg) == kResultOk);
    }

    auto bytes = processorStateBytes(*proc);
    proc->setActive(false);
    proc->terminate();
    return bytes;
}

// A connected Controller + Processor pair, as a host wires them.
struct BrowserPair
{
    HostApplication host;
    RecordingComponentHandler handler;
    std::unique_ptr<Innexus::Processor> proc;
    Innexus::Controller controller;

    BrowserPair()
    {
        proc = makeProcessor(&host);
        REQUIRE(controller.initialize(&host) == kResultOk);
        controller.setComponentHandler(&handler);
        controller.connect(static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(proc.get())));
        proc->connect(static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(&controller)));
    }

    ~BrowserPair()
    {
        proc->disconnect(static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(&controller)));
        controller.disconnect(static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(proc.get())));
        controller.setComponentHandler(nullptr);
        controller.terminate();
        proc->setActive(false);
        proc->terminate();
    }

    BrowserPair(const BrowserPair&) = delete;
    BrowserPair& operator=(const BrowserPair&) = delete;

    bool browserLoad(const std::vector<char>& bytes)
    {
        MemStream stream;
        writeBytes(stream, bytes);
        return controller.loadComponentStateWithNotify(&stream);
    }

    // What a host does with the controller's performEdits: hand them to the
    // processor in the next process() call.
    void forwardEditsAndProcess()
    {
        Krate::Test::ParameterChanges changes;
        for (const auto& [id, value] : handler.edits)
            changes.addChange(id, value);
        handler.edits.clear();
        applyParams(*proc, changes);
    }
};

} // namespace

TEST_CASE("Browser load leaves the processor in the same state as a host load",
          "[innexus][presets][state]")
{
    const auto stateA = makePresetState(false);
    const auto stateB = makePresetState(true);
    REQUIRE(stateA != stateB);

    BrowserPair pair;
    REQUIRE(pair.browserLoad(stateA));
    pair.forwardEditsAndProcess();
    REQUIRE(pair.browserLoad(stateB));
    pair.forwardEditsAndProcess();

    auto reference = makeProcessor();
    {
        MemStream stream;
        writeBytes(stream, stateB);
        REQUIRE(reference->setState(&stream) == kResultOk);
    }
    Krate::Test::ParameterChanges none;
    applyParams(*reference, none);

    const auto got = processorStateBytes(*pair.proc);
    const auto want = processorStateBytes(*reference);
    const StateDiff diff = compareProcessorStates(got, want);
    INFO("browser-loaded state " << got.size() << " bytes, host-loaded "
         << want.size() << " bytes, " << diff.mismatches
         << " differing field(s), the first at byte " << diff.firstOffset);
    REQUIRE(diff.layoutOk);
    CHECK(diff.mismatches == 0);

    // Non-vacuity: a 1 % change in the release time (byte 4) is a mismatch.
    auto altered = want;
    float release = 0.0f;
    std::memcpy(&release, altered.data() + 4, 4);
    release *= 1.01f;
    std::memcpy(altered.data() + 4, &release, 4);
    const StateDiff alteredDiff = compareProcessorStates(altered, want);
    CHECK(alteredDiff.layoutOk);
    CHECK(alteredDiff.mismatches == 1);
    CHECK(alteredDiff.firstOffset == 4);

    reference->setActive(false);
    reference->terminate();
}

TEST_CASE("Browser load shows the preset's values in the controller",
          "[innexus][presets][state]")
{
    const auto stateA = makePresetState(false);
    const auto stateB = makePresetState(true);

    BrowserPair pair;
    REQUIRE(pair.browserLoad(stateA));
    REQUIRE(pair.browserLoad(stateB));

    Innexus::Controller reference;
    REQUIRE(reference.initialize(nullptr) == kResultOk);
    {
        MemStream stream;
        writeBytes(stream, stateB);
        REQUIRE(reference.setComponentState(&stream) == kResultOk);
    }

    const int32 count = reference.getParameterCount();
    REQUIRE(count == pair.controller.getParameterCount());
    for (int32 i = 0; i < count; ++i)
    {
        ParameterInfo info{};
        REQUIRE(reference.getParameterInfo(i, info) == kResultOk);
        INFO("parameter id " << info.id);
        CHECK(pair.controller.getParamNormalized(info.id)
              == reference.getParamNormalized(info.id));
    }

    REQUIRE(reference.terminate() == kResultOk);
}

TEST_CASE("Host load restores Mod 1/2 tempo sync and note value in the controller",
          "[innexus][state]")
{
    // Sync defaults to 1.0 (on) and Note Value to index 10 (1/8): use others.
    constexpr double kMod1Note = 3.0 / 20.0;
    constexpr double kMod2Note = 17.0 / 20.0;

    MemStream stream;
    {
        auto proc = makeProcessor();
        Krate::Test::ParameterChanges changes;
        changes.addChange(Innexus::kMod1RateSyncId, 0.0);
        changes.addChange(Innexus::kMod1NoteValueId, kMod1Note);
        changes.addChange(Innexus::kMod2RateSyncId, 0.0);
        changes.addChange(Innexus::kMod2NoteValueId, kMod2Note);
        applyParams(*proc, changes);
        REQUIRE(proc->getState(&stream) == kResultOk);
        proc->setActive(false);
        proc->terminate();
    }

    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);
    stream.resetReadPos();
    REQUIRE(controller.setComponentState(&stream) == kResultOk);

    constexpr double kTol = 1e-6;
    CHECK(controller.getParamNormalized(Innexus::kMod1RateSyncId) == Approx(0.0).margin(kTol));
    CHECK(controller.getParamNormalized(Innexus::kMod1NoteValueId) == Approx(kMod1Note).margin(kTol));
    CHECK(controller.getParamNormalized(Innexus::kMod2RateSyncId) == Approx(0.0).margin(kTol));
    CHECK(controller.getParamNormalized(Innexus::kMod2NoteValueId) == Approx(kMod2Note).margin(kTol));

    REQUIRE(controller.terminate() == kResultOk);
}
