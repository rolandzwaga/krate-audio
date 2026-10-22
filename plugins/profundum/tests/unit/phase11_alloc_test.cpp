// ==============================================================================
// Profundum - Phase 11 real-time safety (T026; FR-062, SC-021)
// ==============================================================================
// Profundum_Phase11PathsDoNotAllocate: 64 process() blocks under
// TestHelpers::AllocationScope exercise every FR-062 audio-thread path:
//   - parameter routing of all 84 routed IDs (one point each, values rotating per
//     block), passes 1 and 2, handleSoundParamChange on both endpoints;
//   - Edit Target (1001) switches A <-> B every block;
//   - the copy (requested through processor->notify BEFORE the scope, applied by
//     applyPendingCopy in the first scoped block);
//   - the compare snapshot: A for blocks 0-31, A.B for blocks 32-63 (publishRuntime
//     runs only in A.B);
//   - the restore hand-off (a v2 setState placed BEFORE the scope, applied by
//     applyPendingRestore in the first scoped block);
//   - multi-point Sustain Pedal queues (collectPedalPoints / the latch);
//   - pitch bend (4) and the Mass performance offset (Mod Wheel 6 / Channel
//     Pressure 7, including c == 0 blocks);
//   - the sync flags set by the audio thread (flushControllerSync is NOT called
//     inside a scope).
// The compare switch A -> A.B arrives through notify, which ends with
// flushControllerSync (non-audio, allocating by design, plan 4.7); it is sent
// BETWEEN two scopes, so the 64 blocks are counted by scope 1 (blocks 0-31) plus
// scope 2 (blocks 32-63) and the total must be 0.
//
// Every container (parameter changes, event lists, ProcessData) is built before
// the scopes, and no Catch macro runs inside them.
//
// Teeth: flushControllerSync() called INSIDE a scope with a connected peer and a
// pending sync flag counts > 0, so the detector sees IMessage allocation (and the
// allocation operator overrides of test_main.cpp are live).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

#include "parameters/morph_params.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include <allocation_detector.h>
#include <vst_event_list.h>

#include <krate/dsp/systems/bass_morph_engine.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ProfundumPluginTest {
namespace {

namespace P = ::Profundum;
using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 256;
constexpr std::size_t kBlocks = 64;
constexpr std::size_t kCompareSwitchBlock = 32;  // blocks [0, 32) compare A, [32, 64) compare A.B

/// Every ID the route table routes (FR-010): 84 of them.
[[nodiscard]] std::vector<Steinberg::Vst::ParamID> routedIds() {
    std::vector<Steinberg::Vst::ParamID> ids;
    for (std::size_t id = 0; id < P::kParamRoutes.size(); ++id) {
        if (P::kParamRoutes[id].kind != P::RouteKind::None) {
            ids.push_back(static_cast<Steinberg::Vst::ParamID>(id));
        }
    }
    return ids;
}

/// A deterministic value in [0, 1] that rotates with the block and the ID's position.
[[nodiscard]] double rotatingValue(std::size_t block, std::size_t idx) {
    const std::size_t step = (block * 7u + idx * 3u) % 11u;
    return static_cast<double>(step) / 10.0;
}

/// A ctl -> proc message carrying one int attribute (kMsgCompare "mode").
[[nodiscard]] Steinberg::IPtr<Steinberg::Vst::HostMessage> makeIntMessage(const char* id, const char* key,
                                                                          Steinberg::int64 value) {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID(id);
    Steinberg::Vst::IAttributeList* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);
    REQUIRE(attrs->setInt(key, value) == Steinberg::kResultOk);
    return msg;
}

/// One host block of the scoped run, built before the scope.
struct BlockInput {
    MultiParamChanges changes;
    Krate::Test::EventList events;
};

/// Fills @p in for block @p b: every routed ID once (ID 5 multi-point on odd blocks), Edit Target alternating,
/// Mass offset zero every third block, notes on / off on a cycle of eight blocks. Block 0 carries no parameter
/// change, so the pending restore is checked against the morph fields setState loaded (plan 4.5.2).
void buildBlock(BlockInput& in, std::size_t b, const std::vector<Steinberg::Vst::ParamID>& ids) {
    in.changes.clear();
    in.changes.reserve(ids.size());
    in.events.clear();
    if (b % 8u == 0u) {
        for (std::size_t k = 0; k < kChord4.size(); ++k) {
            in.events.addNoteOn(kChord4[k], 0.8f, static_cast<Steinberg::int32>(10u + 4u * k));
        }
    } else if (b % 8u == 4u) {
        for (std::size_t k = 0; k < kChord4.size(); ++k) {
            in.events.addNoteOff(kChord4[k], static_cast<Steinberg::int32>(100u + 4u * k));
        }
    }
    if (b == 0u) {
        return;
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const Steinberg::Vst::ParamID id = ids[i];
        MultiPointParamValueQueue& q = in.changes.addQueue(id);
        if (id == P::kSustainPedalId && (b % 2u) == 1u) {
            // FR-052: several points in one block, down / up / down / up across the note-off cycle.
            q.addTestPoint(8, 1.0);
            q.addTestPoint(64, 0.0);
            q.addTestPoint(128, 1.0);
            q.addTestPoint(200, 0.0);
            continue;
        }
        double v = rotatingValue(b, i);
        if (id == P::kMorphEditTargetId) {
            v = (b % 2u == 1u) ? 1.0 : 0.0;  // B on odd blocks, A on even ones
        } else if ((id == P::kModWheelId || id == P::kChannelPressureId) && b % 3u == 0u) {
            v = 0.0;  // c == 0: the skipped Mass add (FR-055)
        }
        q.addTestPoint(static_cast<Steinberg::int32>((b + i) % kBlock), v);
    }
}

/// Runs blocks [first, last) of @p inputs through @p proc. No Catch macro, no allocation by the caller.
[[nodiscard]] int runBlocks(P::Processor& proc, std::vector<BlockInput>& inputs, std::size_t first,
                            std::size_t last, Steinberg::Vst::ProcessData& data) noexcept {
    int failures = 0;
    for (std::size_t b = first; b < last; ++b) {
        data.inputParameterChanges = &inputs[b].changes;
        data.inputEvents = &inputs[b].events;
        if (proc.process(data) != Steinberg::kResultOk) {
            ++failures;
        }
    }
    return failures;
}

}  // namespace

TEST_CASE("Profundum_Phase11PathsDoNotAllocate", "[profundum][rt]") {
    ConnectedPair cp;
    REQUIRE(cp.fx.prepare(kFs, static_cast<Steinberg::int32>(kBlock)) == Steinberg::kResultOk);

    // Warm: a few rendered blocks with a note, so first-render work is not attributed to the scoped paths.
    cp.fx.reserveCapture(8u * kBlock);
    {
        Krate::Test::EventList warm;
        warm.addNoteOn(kE1, 0.8f, 0);
        REQUIRE(cp.fx.processBlock(kBlock, &warm) == Steinberg::kResultOk);
        for (int i = 0; i < 3; ++i) {
            REQUIRE(cp.fx.processBlock(kBlock) == Steinberg::kResultOk);
        }
        warm.clear();
        warm.addNoteOff(kE1, 0);
        REQUIRE(cp.fx.processBlock(kBlock, &warm) == Steinberg::kResultOk);
    }
    cp.fx.proc->flushControllerSync();

    SECTION("AllPathsZeroAllocations") {
        const std::vector<Steinberg::Vst::ParamID> ids = routedIds();
        REQUIRE(ids.size() == 84u);

        // The 64 blocks' inputs, built before any scope (their queues keep their capacity).
        std::vector<BlockInput> inputs(kBlocks);
        for (std::size_t b = 0; b < kBlocks; ++b) {
            buildBlock(inputs[b], b, ids);
        }
        REQUIRE(inputs[1].changes.getParameterCount() == 84);

        // A pending setState hand-off: a v2 stream whose runtime blob was published by a rendered processor.
        StateBytes stateBytes;
        {
            ProcessorFixture prior;
            makePriorStateP(prior);
            stateBytes = writeV2Stream(*prior.proc);
        }
        REQUIRE(stateBytes.size() == P::kStateV2Bytes);
        auto stateStream = memoryStreamOf(stateBytes);
        const std::uint32_t restoresBefore = cp.fx.proc->restoreApplyCountForTest();
        REQUIRE(cp.fx.proc->setState(stateStream) == Steinberg::kResultOk);  // ends with a flush (non-audio)

        // The compare A and the copy B -> A, both through processor->notify, before the scope.
        auto compareA = makeIntMessage(P::kMsgCompare, "mode", static_cast<Steinberg::int64>(P::CompareMode::A));
        auto compareAB = makeIntMessage(P::kMsgCompare, "mode", static_cast<Steinberg::int64>(P::CompareMode::AB));
        REQUIRE(cp.fx.proc->notify(compareA) == Steinberg::kResultOk);
        REQUIRE(cp.fx.proc->compareModeForTest() == P::CompareMode::A);
        cp.clearRecords();
        cp.cf.controller->copyEndpoint(Endpoint::B);
        REQUIRE(cp.toProcessor.count(P::kMsgCopyEndpoint) == 1u);
        cp.clearRecords();

        std::array<float*, 2> channels{cp.fx.audioL(), cp.fx.audioR()};
        Steinberg::Vst::AudioBusBuffers bus{};
        bus.numChannels = 2;
        bus.silenceFlags = 0;
        bus.channelBuffers32 = channels.data();
        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlock);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = nullptr;
        data.outputEvents = nullptr;
        data.processContext = nullptr;
        P::Processor& proc = *cp.fx.proc;

        int failures = 0;
        std::size_t countA = 0;
        {
            TestHelpers::AllocationScope scope;
            failures += runBlocks(proc, inputs, 0u, kCompareSwitchBlock, data);
            countA = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        // Non-audio, between the scopes: the compare switch A -> A.B (notify ends with flushControllerSync).
        REQUIRE(cp.fx.proc->notify(compareAB) == Steinberg::kResultOk);
        REQUIRE(cp.fx.proc->compareModeForTest() == P::CompareMode::AB);
        cp.clearRecords();
        std::size_t countAB = 0;
        {
            TestHelpers::AllocationScope scope;
            failures += runBlocks(proc, inputs, kCompareSwitchBlock, kBlocks, data);
            countAB = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        INFO("allocations, compare A blocks 0-31: " << countA << ", compare A.B blocks 32-63: " << countAB);
        CHECK(failures == 0);
        CHECK(countA == 0u);
        CHECK(countAB == 0u);
        REQUIRE(countA + countAB == 0u);

        // Non-vacuity: the restore was applied inside the scope, and the scoped blocks left sync flags set
        // (routing on both Edit Targets): the first flush after the scopes reports both endpoints.
        CHECK(cp.fx.proc->restoreApplyCountForTest() == restoresBefore + 1u);
        CHECK(cp.toController.count(P::kMsgEndpoint) == 0u);  // nothing was sent inside the scopes
        cp.fx.proc->flushControllerSync();
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 0) == 1u);
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 1) == 1u);
    }

    SECTION("Teeth") {
        // A pending sync flag: one E-parameter edit on the Edit Target (writeSoundRow sets syncPending).
        REQUIRE(cp.fx.setParam(P::kCoreDepthId, 0.3, kBlock) == Steinberg::kResultOk);
        // A peer whose notify does not allocate, so the count is the sender's IMessage path alone.
        CountingConnectionPoint peer;
        REQUIRE(cp.fx.proc->disconnect(&cp.toController) == Steinberg::kResultOk);
        REQUIRE(cp.fx.proc->connect(&peer) == Steinberg::kResultOk);

        std::size_t count = 0;
        {
            TestHelpers::AllocationScope scope;
            cp.fx.proc->flushControllerSync();
            count = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        REQUIRE(cp.fx.proc->disconnect(&peer) == Steinberg::kResultOk);
        REQUIRE(cp.fx.proc->connect(&cp.toController) == Steinberg::kResultOk);
        INFO("allocations of one flushControllerSync with a connected peer: " << count);
        CHECK(peer.notifications >= 1);
        CHECK(count > 0u);
    }
}

}  // namespace ProfundumPluginTest
