// ==============================================================================
// Profundum - Copy, Compare and processor -> controller sync (Phase 11 T020)
// ==============================================================================
// Profundum_CopyEndpoint (SC-013, FR-030): copyEndpoint(from) sends a request;
//   the audio thread copies the slots at the next process(); the controller's
//   mirror is replaced from the processor's destination-endpoint message only.
// Profundum_CompareDoesNotWriteState (SC-014, FR-031 - FR-033): compare A drives
//   position 0 and renders like a position-0 reference; getState during the
//   compare is byte-identical to the stream taken before it; setState and
//   setActive(false) reset the compare to A.B.
// Profundum_MorphModOutsideEndpoints (Phase 14 T012; SC-022, FR-018): the nine Morph Modulation parameters
//   (1003-1011) are not endpoint parameters: Copy A <-> B and Edit Target switches leave the controller's nine
//   values and the processor's stored block (read through getState) bit-unchanged.
// Profundum_ProcessorControllerSync (SC-025, FR-025): process() on a dedicated
//   "audio" thread, flushControllerSync() / onTimer() on the test thread; the
//   Edit Target pull (immediate and delayed reply) and the timer lifecycle.
//
// Every equality is same-binary, same-process (no float golden across binaries).
// Plugin types are spelled ::Profundum::...; this TU includes no dsp/tests helper.
// ==============================================================================

#include "profundum_test_fixture.h"

#include "band_level_metric.h"

#include "parameters/morph_mod_params.h"
#include "parameters/morph_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include <allocation_detector.h>

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace P = ::Profundum;
using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;
using Krate::DSP::MorphState;
using Krate::DSP::detail::morph_state_io::leafBitsEqual;
using Steinberg::Vst::kParamValuesChanged;

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 512;

// Leaf indices used by the tests, checked against the law table by name.
constexpr std::uint8_t kDepthLeaf = 0;
constexpr std::uint8_t kEdgeLeaf = 4;
constexpr std::uint8_t kExcitationMixLeaf = 6;
constexpr std::uint8_t kFoldLeaf = 9;
constexpr std::uint8_t kCharacterAmountLeaf = 12;
static_assert(std::string_view(P::lawOf(kDepthLeaf).name) == "recipe.depth");
static_assert(std::string_view(P::lawOf(kEdgeLeaf).name) == "recipe.edge");
static_assert(std::string_view(P::lawOf(kExcitationMixLeaf).name) == "excitation.mix");
static_assert(std::string_view(P::lawOf(kFoldLeaf).name) == "foldAmount");
static_assert(std::string_view(P::lawOf(kCharacterAmountLeaf).name) == "characterAmount");

[[nodiscard]] Endpoint other(Endpoint e) noexcept {
    return e == Endpoint::A ? Endpoint::B : Endpoint::A;
}

[[nodiscard]] MorphState slotState(const P::Processor& proc, Endpoint e) {
    return P::morphStateFromLeafBits(slotsOf(proc, e));
}

[[nodiscard]] std::uint32_t mirrorLeaf(const ConnectedPair& cp, Endpoint e, std::uint8_t leaf) {
    return P::leafBitsOf(cp.cf.controller->mirrorForTest(e))[leaf];
}

// Prepares the pair at 48 kHz and loads endpoints a / b through setState (which flushes them to the controller).
// No block is rendered. The recorders are cleared before setState, so they hold exactly what setState sent.
void loadEndpoints(ConnectedPair& cp, const MorphState& a, const MorphState& b) {
    REQUIRE(cp.fx.prepare(kFs, 4096) == Steinberg::kResultOk);
    const StateBytes bytes = withEndpoints(writeV2Stream(*cp.fx.proc), a, b);
    cp.clearRecords();
    REQUIRE(loadStateBytes(*cp.fx.proc, bytes) == Steinberg::kResultOk);
}

void renderOneBlock(ProcessorFixture& fx) {
    fx.ensureCapture(kBlock);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
}

// Two timbre-distinct endpoints whose envelopes, motion and field stay at the MorphState{} defaults, so a
// position-0 render depends on A alone (SC-014). Built through the leaf laws, so both are valid by construction.
[[nodiscard]] MorphState timbreState(double depth, double edge, double mix, double fold, double character) {
    P::LeafBits bits = P::kDefaultLeafBits;
    bits[kDepthLeaf] = P::leafWordFromNormalized(kDepthLeaf, depth);
    bits[kEdgeLeaf] = P::leafWordFromNormalized(kEdgeLeaf, edge);
    bits[kExcitationMixLeaf] = P::leafWordFromNormalized(kExcitationMixLeaf, mix);
    bits[kFoldLeaf] = P::leafWordFromNormalized(kFoldLeaf, fold);
    bits[kCharacterAmountLeaf] = P::leafWordFromNormalized(kCharacterAmountLeaf, character);
    const MorphState s = P::morphStateFromLeafBits(bits);
    REQUIRE(Krate::DSP::isValidMorphState(s));
    return s;
}

// -----------------------------------------------------------------------------
// The "audio" thread (SC-025): `blocks` process() calls of kBlock samples on a
// dedicated std::thread; the first carries @p changes (may be null). Everything
// is built before the thread starts and no Catch macro runs on it. The
// allocation count covers that thread only (ThreadScopedAllocationScope).
// -----------------------------------------------------------------------------
struct AudioRun {
    std::thread::id thread;
    std::size_t allocations = 0;
    int failures = 0;
};

[[nodiscard]] AudioRun runOnAudioThread(ProcessorFixture& fx, Steinberg::Vst::IParameterChanges* changes,
                                        int blocks = 1) {
    std::array<float*, 2> channels{fx.audioL(), fx.audioR()};
    Steinberg::Vst::AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.silenceFlags = 0;
    bus.channelBuffers32 = channels.data();
    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = static_cast<Steinberg::int32>(kBlock);
    d.numInputs = 0;
    d.inputs = nullptr;
    d.numOutputs = 1;
    d.outputs = &bus;
    d.inputParameterChanges = nullptr;
    d.outputParameterChanges = nullptr;
    d.inputEvents = nullptr;
    d.outputEvents = nullptr;
    d.processContext = nullptr;
    P::Processor& proc = *fx.proc;

    AudioRun run;
    std::thread audio([&]() noexcept {
        TestHelpers::ThreadScopedAllocationScope scope;  // counts this thread's allocations only
        run.thread = std::this_thread::get_id();
        for (int b = 0; b < blocks; ++b) {
            d.inputParameterChanges = (b == 0) ? changes : nullptr;
            if (proc.process(d) != Steinberg::kResultOk) {
                ++run.failures;
            }
        }
        run.allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    });
    audio.join();
    return run;
}

// SC-013, one direction: copyEndpoint(from) on a connected pair holding A / B.
void runCopyArm(Endpoint from) {
    ConnectedPair cp;
    const MorphState a = randomValidMorphState(0x13Au);
    const MorphState b = randomValidMorphState(0x13Bu);
    REQUIRE_FALSE(leafBitsEqual(a, b));  // non-vacuity: the copy changes the destination
    loadEndpoints(cp, a, b);
    renderOneBlock(cp.fx);
    cp.fx.proc->flushControllerSync();

    const Endpoint to = other(from);
    const MorphState& src = (from == Endpoint::A) ? a : b;
    const MorphState& dstBefore = (from == Endpoint::A) ? b : a;
    REQUIRE(leafBitsEqual(cp.cf.controller->mirrorForTest(from), src));
    REQUIRE(leafBitsEqual(cp.cf.controller->mirrorForTest(to), dstBefore));
    REQUIRE(cp.fx.engine() != nullptr);

    const double editTargetBefore = cp.cf.controller->getParamNormalized(P::kMorphEditTargetId);
    const double positionBefore = cp.cf.controller->getParamNormalized(P::kMorphPositionId);
    const float enginePositionBefore = cp.fx.engine()->morphPosition();
    const int dirtyBefore = cp.cf.handler.dirtyCount;
    cp.clearRecords();

    cp.cf.controller->copyEndpoint(from);
    CHECK(cp.toProcessor.count(P::kMsgCopyEndpoint) == 1u);
    CHECK(cp.toProcessor.count(P::kMsgCopyEndpoint, "from", static_cast<Steinberg::int64>(from)) == 1u);
    CHECK(cp.cf.handler.dirtyCount == dirtyBefore + 1);
    // The controller never copies locally: the destination mirror is unchanged until the processor reports.
    CHECK(leafBitsEqual(cp.cf.controller->mirrorForTest(to), dstBefore));
    CHECK(cp.toController.count(P::kMsgEndpoint) == 0u);

    renderOneBlock(cp.fx);
    cp.fx.proc->flushControllerSync();

    // The processor: destination slots and engine endpoint equal the source; the source is untouched.
    CHECK(leafBitsEqual(slotState(*cp.fx.proc, to), src));
    CHECK(leafBitsEqual(cp.fx.engine()->getEndpoint(to), src));
    CHECK(leafBitsEqual(slotState(*cp.fx.proc, from), src));
    // The controller: the destination mirror was replaced from the processor's kMsgEndpoint message.
    CHECK(leafBitsEqual(cp.cf.controller->mirrorForTest(to), src));
    CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", static_cast<Steinberg::int64>(to)) == 1u);
    CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", static_cast<Steinberg::int64>(from)) == 0u);
    const auto expectedBlob = serializedMorphState(src);
    bool blobSeen = false;
    for (const auto& r : cp.toController.records()) {
        if (r.id == P::kMsgEndpoint && r.hasBlob && r.blob.size() == expectedBlob.size() &&
            std::equal(r.blob.begin(), r.blob.end(), expectedBlob.begin())) {
            blobSeen = true;
        }
    }
    CHECK(blobSeen);
    CHECK(cp.cf.handler.dirtyCount == dirtyBefore + 1);  // exactly once

    // Copy changes neither Edit Target nor Morph Position.
    CHECK(cp.cf.controller->getParamNormalized(P::kMorphEditTargetId) == editTargetBefore);
    CHECK(cp.cf.controller->getParamNormalized(P::kMorphPositionId) == positionBefore);
    CHECK(std::bit_cast<std::uint32_t>(cp.fx.engine()->morphPosition()) ==
          std::bit_cast<std::uint32_t>(enginePositionBefore));
}

}  // namespace

// =============================================================================
// SC-013 - Copy A -> B / B -> A
// =============================================================================
TEST_CASE("Profundum_CopyEndpoint", "[profundum][morph]") {
    SECTION("AToB") {
        runCopyArm(Endpoint::A);
    }
    SECTION("BToA") {
        runCopyArm(Endpoint::B);
    }
}

// =============================================================================
// SC-022 (Phase 14 T012; FR-018) - Morph Modulation lives outside the endpoints
// =============================================================================
namespace {

constexpr std::size_t kMmCount = 9;

/// The controller's nine normalised values (as bits) and the processor's stored block (the MorphModParams atomics,
/// written word for word by getState at kStateMorphModOffset).
struct MmOutsideSnapshot {
    std::array<std::uint64_t, kMmCount> ctl{};
    std::array<std::uint32_t, kMmCount> proc{};
};

[[nodiscard]] Steinberg::Vst::ParamID mmIdOf(std::size_t i) noexcept {
    return static_cast<Steinberg::Vst::ParamID>(P::kMorphModSourceId) + static_cast<Steinberg::Vst::ParamID>(i);
}

[[nodiscard]] MmOutsideSnapshot mmOutsideSnapshot(ConnectedPair& cp) {
    MmOutsideSnapshot snap;
    for (std::size_t i = 0; i < kMmCount; ++i) {
        snap.ctl[i] = std::bit_cast<std::uint64_t>(cp.cf.controller->getParamNormalized(mmIdOf(i)));
    }
    const StateBytes st = writeV2Stream(*cp.fx.proc);
    REQUIRE(st.size() == P::kStateV3Bytes);
    for (std::size_t i = 0; i < kMmCount; ++i) {
        snap.proc[i] = readU32(st, P::kStateMorphModOffset + 4u * i);
    }
    return snap;
}

}  // namespace

TEST_CASE("Profundum_MorphModOutsideEndpoints", "[profundum][morphmod][morph]") {
    // Statically outside: 62 sound rows (Phase 15: + Stiffness, Phase) (redisplayEditTarget and the copy iterate exactly these), none of them in
    // 1003-1011, and no route in 1003-1011 is a Sound route.
    STATIC_REQUIRE(P::kSoundRows.size() == 62);
    for (const P::SoundParamRow& row : P::kSoundRows) {
        INFO("row " << row.id);
        CHECK((row.id < 1003u || row.id > 1011u));
    }
    for (std::size_t i = 0; i < kMmCount; ++i) {
        INFO("id " << mmIdOf(i));
        CHECK(P::kParamRoutes[mmIdOf(i)].kind != P::RouteKind::Sound);
        CHECK(P::kParamRoutes[mmIdOf(i)].kind == P::RouteKind::MorphMod);
    }

    ConnectedPair cp;
    const MorphState a = randomValidMorphState(0x22Au);
    const MorphState b = randomValidMorphState(0x22Bu);
    REQUIRE_FALSE(leafBitsEqual(a, b));  // non-vacuity: each copy changes its destination
    loadEndpoints(cp, a, b);

    // The nine at non-defaults (Source Cycle, Amount -0.6, Attack 50 ms, Decay 900 ms, Sustain 0.4, Shape Square,
    // Rate 3 Hz, Sync On, Sync Rate index 7), delivered as a host does: to the processor as parameter changes, to
    // the controller through setParamNormalized.
    const std::array<double, kMmCount> nine{
        2.0 / 4.0,
        0.2,
        P::normalizedFromPlain(P::kMorphModTimeLaw, 50.0),
        P::normalizedFromPlain(P::kMorphModTimeLaw, 900.0),
        0.4,
        3.0 / 5.0,
        P::normalizedFromPlain(P::kMorphModRateLaw, 3.0),
        1.0,
        7.0 / 29.0,
    };
    {
        MultiParamChanges pc;
        pc.reserve(kMmCount);
        for (std::size_t i = 0; i < kMmCount; ++i) {
            pc.addQueue(mmIdOf(i)).addTestPoint(0, nine[i]);
            REQUIRE(cp.cf.controller->setParamNormalized(mmIdOf(i), nine[i]) == Steinberg::kResultTrue);
        }
        cp.fx.ensureCapture(kBlock);
        REQUIRE(cp.fx.processBlock(kBlock, nullptr, &pc) == Steinberg::kResultOk);
    }
    cp.fx.proc->flushControllerSync();

    const MmOutsideSnapshot base = mmOutsideSnapshot(cp);
    // Teeth: the values are held off their defaults on both sides.
    CHECK(base.proc[0] == 2u);                                  // Source Cycle
    CHECK(std::bit_cast<float>(base.proc[1]) < -0.5f);          // Amount -0.6
    CHECK(base.proc[5] == 3u);                                  // Shape Square
    CHECK(base.proc[7] == 1u);                                  // Sync On
    CHECK(base.proc[8] == 7u);                                  // Sync Rate index 7
    for (std::size_t i = 0; i < kMmCount; ++i) {
        INFO("id " << mmIdOf(i));
        const Steinberg::Vst::Parameter* param = cp.cf.controller->getParameterObject(mmIdOf(i));
        REQUIRE(param != nullptr);
        CHECK(std::bit_cast<std::uint64_t>(param->getInfo().defaultNormalizedValue) != base.ctl[i]);
    }

    const auto expectUnchanged = [&](const char* step) {
        INFO("after " << step);
        const MmOutsideSnapshot now = mmOutsideSnapshot(cp);
        CHECK(now.ctl == base.ctl);
        CHECK(now.proc == base.proc);
    };

    // Copy A -> B, then B -> A: each one replaces the destination (non-vacuity), never the nine.
    for (const Endpoint from : {Endpoint::A, Endpoint::B}) {
        cp.clearRecords();
        cp.cf.controller->copyEndpoint(from);
        REQUIRE(cp.toProcessor.count(P::kMsgCopyEndpoint) == 1u);
        renderOneBlock(cp.fx);
        cp.fx.proc->flushControllerSync();
        CHECK(leafBitsEqual(cp.cf.controller->mirrorForTest(other(from)), slotState(*cp.fx.proc, from)));
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", static_cast<Steinberg::int64>(other(from))) == 1u);
        expectUnchanged(from == Endpoint::A ? "Copy A to B" : "Copy B to A");
    }

    // Edit Target A -> B -> A, on the processor (a parameter change) and on the controller (the 1001 hook, whose
    // redisplayEditTarget re-displays the 60 sound rows and nothing else).
    for (const double target : {1.0, 0.0}) {
        REQUIRE(cp.fx.setParam(P::kMorphEditTargetId, target, kBlock) == Steinberg::kResultOk);
        cp.fx.proc->flushControllerSync();
        const int redisplays0 = cp.cf.controller->redisplayCountForTest();
        REQUIRE(cp.cf.controller->setParamNormalized(P::kMorphEditTargetId, target) == Steinberg::kResultTrue);
        CHECK(cp.cf.controller->redisplayCountForTest() > redisplays0);  // non-vacuity: the re-display ran
        expectUnchanged(target == 1.0 ? "Edit Target B" : "Edit Target A");
    }
}

// =============================================================================
// SC-014 - Compare never writes state
// =============================================================================
TEST_CASE("Profundum_CompareDoesNotWriteState", "[profundum][morph]") {
    const MorphState a = timbreState(0.2, 0.1, 0.0, 0.0, 0.0);
    const MorphState b = timbreState(0.9, 0.9, 0.8, 0.6, 0.5);
    REQUIRE_FALSE(leafBitsEqual(a, b));

    ConnectedPair cp;
    loadEndpoints(cp, a, b);
    REQUIRE(cp.fx.proc->compareModeForTest() == P::CompareMode::AB);

    SECTION("CompareA") {
        // Reference: the same endpoints and note stream at position 0, never in compare.
        ProcessorFixture ref;
        REQUIRE(ref.prepare(kFs, 4096) == Steinberg::kResultOk);
        REQUIRE(loadStateBytes(*ref.proc, withEndpoints(writeV2Stream(*ref.proc), a, b)) == Steinberg::kResultOk);

        // E1 MONO (the default voice mode) held for the whole render; the compared processor sits at 0.6.
        using Ev = ProcessorFixture::ScriptedEvent;
        const std::array<Ev, 2> testScript{
            Ev{.sample = 0, .kind = Ev::Kind::NoteOn, .pitch = kE1, .velocity = 100.0f / 127.0f},
            Ev{.sample = 0, .kind = Ev::Kind::Param, .id = P::kMorphPositionId, .value = 0.6},
        };
        const std::array<Ev, 1> refScript{
            Ev{.sample = 0, .kind = Ev::Kind::NoteOn, .pitch = kE1, .velocity = 100.0f / 127.0f},
        };
        constexpr std::size_t kPre = 47u * kBlock;    // ~0.5 s at 0.6
        constexpr std::size_t kPost = 113u * kBlock;  // ~1.2 s after entering compare
        cp.fx.capturedL.clear();
        cp.fx.capturedR.clear();
        cp.fx.renderScript(std::span<const Ev>(testScript), kPre, kBlock);
        ref.renderScript(std::span<const Ev>(refScript), kPre, kBlock);
        REQUIRE(cp.fx.engine()->morphPosition() == 0.6f);

        const StateBytes before = writeV2Stream(*cp.fx.proc);
        cp.cf.controller->setCompare(P::CompareMode::A);
        REQUIRE(cp.fx.proc->compareModeForTest() == P::CompareMode::A);
        CHECK(cp.cf.controller->compareModeForTest() == P::CompareMode::A);  // the processor's reply

        cp.fx.renderScript(std::span<const Ev>{}, kPost, kBlock);
        ref.renderScript(std::span<const Ev>{}, kPost, kBlock);
        CHECK(cp.fx.engine()->morphPosition() == 0.0f);  // compare A drives position 0 ...

        // ... and renders like the position-0 reference over [140 ms, 1140 ms) after entering compare.
        const std::size_t from = kPre + static_cast<std::size_t>(0.140 * kFs);
        const auto len = static_cast<std::size_t>(1.0 * kFs);
        REQUIRE(cp.fx.capturedL.size() >= from + len);
        REQUIRE(ref.capturedL.size() >= from + len);
        const auto win = [&](const std::vector<float>& v) { return std::span<const float>(v).subspan(from, len); };
        const double compareDiff = bandLevelDifferenceDb(win(cp.fx.capturedL), win(cp.fx.capturedR),
                                                         win(ref.capturedL), win(ref.capturedR), kFs, false);
        // Non-vacuity: before the compare (position 0.6) the same metric separates the two renders.
        const auto preFrom = static_cast<std::size_t>(0.2 * kFs);
        const std::size_t preLen = kPre - preFrom;
        const auto pre = [&](const std::vector<float>& v) {
            return std::span<const float>(v).subspan(preFrom, preLen);
        };
        const double preDiff = bandLevelDifferenceDb(pre(cp.fx.capturedL), pre(cp.fx.capturedR),
                                                     pre(ref.capturedL), pre(ref.capturedR), kFs, false);
        INFO("compare-A difference " << compareDiff << " dB; pre-compare (position 0.6) difference " << preDiff
                                     << " dB");
        REQUIRE(rmsDb(win(ref.capturedL)) > -60.0);  // the reference is audible in the window
        REQUIRE(preDiff > 0.1);
        CHECK(compareDiff <= 0.1);

        // getState during the compare is byte-identical to the stream taken just before entering it.
        const StateBytes during = writeV2Stream(*cp.fx.proc);
        CHECK(during == before);

        // Back at A.B: the engine target returns to the registered 0.6.
        cp.cf.controller->setCompare(P::CompareMode::AB);
        REQUIRE(cp.fx.proc->compareModeForTest() == P::CompareMode::AB);
        renderOneBlock(cp.fx);
        CHECK(cp.fx.engine()->morphPosition() == 0.6f);

        // setState resets the compare to A.B (FR-033).
        cp.cf.controller->setCompare(P::CompareMode::B);
        REQUIRE(cp.fx.proc->compareModeForTest() == P::CompareMode::B);
        REQUIRE(loadStateBytes(*cp.fx.proc, before) == Steinberg::kResultOk);
        CHECK(cp.fx.proc->compareModeForTest() == P::CompareMode::AB);
        CHECK(cp.cf.controller->compareModeForTest() == P::CompareMode::AB);
    }

    SECTION("SetActiveFalseResets") {
        cp.cf.controller->setCompare(P::CompareMode::B);
        REQUIRE(cp.fx.proc->compareModeForTest() == P::CompareMode::B);
        renderOneBlock(cp.fx);
        REQUIRE(cp.fx.proc->setActive(0u) == Steinberg::kResultOk);
        CHECK(cp.fx.proc->compareModeForTest() == P::CompareMode::AB);
        CHECK(cp.cf.controller->compareModeForTest() == P::CompareMode::AB);
        REQUIRE(cp.fx.proc->setActive(1u) == Steinberg::kResultOk);
    }
}

// =============================================================================
// SC-025 - Processor-authoritative sync
// =============================================================================
TEST_CASE("Profundum_ProcessorControllerSync", "[profundum][morph]") {
    const MorphState a = randomValidMorphState(0x25Au);
    const MorphState b = randomValidMorphState(0x25Bu);
    REQUIRE_FALSE(leafBitsEqual(a, b));

    ConnectedPair cp;
    loadEndpoints(cp, a, b);

    SECTION("SetState") {
        // setState sent both endpoint blobs (201 bytes each) and the compare mode A.B.
        CHECK(cp.toController.count(P::kMsgEndpoint) == 2u);
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 0) == 1u);
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 1) == 1u);
        for (const auto& r : cp.toController.records()) {
            if (r.id == P::kMsgEndpoint) {
                CHECK(r.hasBlob);
                CHECK(r.blob.size() == Krate::DSP::kMorphStateBytes);
            }
        }
        CHECK(cp.toController.count(P::kMsgCompareMode, "mode", static_cast<Steinberg::int64>(P::CompareMode::AB)) ==
              1u);
        CHECK(leafBitsEqual(cp.cf.controller->mirrorForTest(Endpoint::A), a));
        CHECK(leafBitsEqual(cp.cf.controller->mirrorForTest(Endpoint::B), b));
        CHECK(cp.cf.controller->compareModeForTest() == P::CompareMode::AB);
    }

    SECTION("UnobservedEdit") {
        // A Depth edit delivered to the processor only (target A); the controller never sees it.
        const std::uint32_t expected = P::leafWordFromNormalized(kDepthLeaf, 0.37);
        REQUIRE(P::leafBitsOf(a)[kDepthLeaf] != expected);  // non-vacuity
        MultiParamChanges pc;
        pc.reserve(1);
        pc.addQueue(P::kCoreDepthId).addTestPoint(0, 0.37);
        cp.clearRecords();
        const AudioRun run = runOnAudioThread(cp.fx, &pc);
        REQUIRE(run.failures == 0);
        REQUIRE(cp.fx.proc->endpointSlotForTest(Endpoint::A, kDepthLeaf) == expected);
        CHECK(cp.toController.count(P::kMsgEndpoint) == 0u);  // the audio thread never sends

        cp.fx.proc->flushControllerSync();
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 0) == 1u);
        CHECK(mirrorLeaf(cp, Endpoint::A, kDepthLeaf) == cp.fx.proc->endpointSlotForTest(Endpoint::A, kDepthLeaf));
        CHECK(cp.sendsOnThread(run.thread) == 0u);
        CHECK(run.allocations == 0u);
    }

    SECTION("Copy") {
        cp.clearRecords();
        cp.cf.controller->copyEndpoint(Endpoint::A);
        const AudioRun run = runOnAudioThread(cp.fx, nullptr);
        REQUIRE(run.failures == 0);
        cp.fx.proc->flushControllerSync();

        CHECK(cp.toController.count(P::kMsgEndpoint) == 1u);
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 1) == 1u);
        for (const auto& r : cp.toController.records()) {
            if (r.id == P::kMsgEndpoint) {
                CHECK(r.blob.size() == Krate::DSP::kMorphStateBytes);
            }
        }
        CHECK(leafBitsEqual(cp.cf.controller->mirrorForTest(Endpoint::B), a));
        CHECK(cp.sendsOnThread(run.thread) == 0u);
        CHECK(run.allocations == 0u);
    }

    SECTION("CompareReset") {
        cp.cf.controller->setCompare(P::CompareMode::B);
        REQUIRE(cp.cf.controller->compareModeForTest() == P::CompareMode::B);  // non-vacuity
        const AudioRun run = runOnAudioThread(cp.fx, nullptr);
        REQUIRE(run.failures == 0);
        cp.clearRecords();

        REQUIRE(cp.fx.proc->setActive(0u) == Steinberg::kResultOk);
        CHECK(cp.toController.count(P::kMsgCompareMode, "mode", static_cast<Steinberg::int64>(P::CompareMode::AB)) ==
              1u);
        CHECK(cp.cf.controller->compareModeForTest() == P::CompareMode::AB);
        CHECK(cp.fx.proc->compareModeForTest() == P::CompareMode::AB);
        REQUIRE(cp.fx.proc->setActive(1u) == Steinberg::kResultOk);
        CHECK(cp.sendsOnThread(run.thread) == 0u);
        CHECK(run.allocations == 0u);
    }

    SECTION("MassOffsetSendsNothing") {
        MultiParamChanges pc;
        pc.reserve(2);
        pc.addQueue(P::kModWheelId).addTestPoint(0, 0.5);
        pc.addQueue(P::kChannelPressureId).addTestPoint(0, 0.25);
        cp.clearRecords();
        const AudioRun run = runOnAudioThread(cp.fx, &pc);
        REQUIRE(run.failures == 0);
        // Non-vacuity: the offset reached the engine.
        REQUIRE(std::bit_cast<std::uint32_t>(cp.fx.engine()->getEndpoint(Endpoint::A).mass) !=
                std::bit_cast<std::uint32_t>(a.mass));
        cp.fx.proc->flushControllerSync();
        CHECK(cp.toController.count(P::kMsgEndpoint) == 0u);
        CHECK(leafBitsEqual(cp.cf.controller->mirrorForTest(Endpoint::A), a));
        CHECK(run.allocations == 0u);
    }

    SECTION("EditTargetPull") {
        // A stale mirror: a Depth edit to B delivered to the processor only, no flush.
        MultiParamChanges pc;
        pc.reserve(2);
        pc.addQueue(P::kMorphEditTargetId).addTestPoint(0, 1.0);
        pc.addQueue(P::kCoreDepthId).addTestPoint(0, 0.61);
        const AudioRun run = runOnAudioThread(cp.fx, &pc);
        REQUIRE(run.failures == 0);
        const std::uint32_t slotB = cp.fx.proc->endpointSlotForTest(Endpoint::B, kDepthLeaf);
        REQUIRE(slotB == P::leafWordFromNormalized(kDepthLeaf, 0.61));
        REQUIRE(mirrorLeaf(cp, Endpoint::B, kDepthLeaf) != slotB);  // the mirror is stale

        double depthAtRestart = -1.0;
        P::Controller& ctl = *cp.cf.controller.get();
        cp.cf.handler.onRestart = [&](Steinberg::int32 /*flags*/) {
            depthAtRestart = ctl.getParamNormalized(P::kCoreDepthId);
        };
        cp.clearRecords();
        REQUIRE(cp.cf.controller->setParamNormalized(P::kMorphEditTargetId, 1.0) == Steinberg::kResultTrue);

        CHECK(cp.toProcessor.count(P::kMsgRequestSync) == 1u);
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 0) == 1u);
        CHECK(cp.toController.count(P::kMsgEndpoint, "endpoint", 1) == 1u);
        CHECK(cp.toController.count(P::kMsgCompareMode) == 1u);
        CHECK(mirrorLeaf(cp, Endpoint::B, kDepthLeaf) == slotB);
        const double expectedDepth = P::normalizedFromLeafWord(kDepthLeaf, slotB);
        CHECK(cp.cf.controller->getParamNormalized(P::kCoreDepthId) == expectedDepth);
        REQUIRE(cp.cf.handler.restartFlags.size() == 1u);
        CHECK(cp.cf.handler.restartFlags[0] == kParamValuesChanged);
        CHECK(depthAtRestart == expectedDepth);  // the re-display already used the processor's slot
        CHECK(cp.sendsOnThread(run.thread) == 0u);
    }

    SECTION("EditTargetPullDelayedReply") {
        MultiParamChanges pc;
        pc.reserve(2);
        pc.addQueue(P::kMorphEditTargetId).addTestPoint(0, 1.0);
        pc.addQueue(P::kCoreDepthId).addTestPoint(0, 0.61);
        const AudioRun run = runOnAudioThread(cp.fx, &pc);
        REQUIRE(run.failures == 0);
        const std::uint32_t slotB = cp.fx.proc->endpointSlotForTest(Endpoint::B, kDepthLeaf);
        const std::uint32_t staleB = mirrorLeaf(cp, Endpoint::B, kDepthLeaf);
        REQUIRE(staleB != slotB);

        cp.clearRecords();
        cp.toController.hold = true;  // the host delivers the reply later
        const int redisplays0 = cp.cf.controller->redisplayCountForTest();
        REQUIRE(cp.cf.controller->setParamNormalized(P::kMorphEditTargetId, 1.0) == Steinberg::kResultTrue);

        CHECK(cp.toProcessor.count(P::kMsgRequestSync) == 1u);
        CHECK(cp.toController.count(P::kMsgEndpoint) == 2u);  // recorded and held
        CHECK(mirrorLeaf(cp, Endpoint::B, kDepthLeaf) == staleB);
        CHECK(cp.cf.controller->getParamNormalized(P::kCoreDepthId) ==
              P::normalizedFromLeafWord(kDepthLeaf, staleB));
        REQUIRE(cp.cf.handler.restartFlags.size() == 1u);
        CHECK(cp.cf.controller->redisplayCountForTest() == redisplays0 + 1);

        cp.toController.releaseHeld();
        CHECK(mirrorLeaf(cp, Endpoint::B, kDepthLeaf) == slotB);
        CHECK(cp.cf.controller->redisplayCountForTest() == redisplays0 + 2);
        REQUIRE(cp.cf.handler.restartFlags.size() == 2u);
        CHECK(cp.cf.handler.restartFlags[0] == kParamValuesChanged);
        CHECK(cp.cf.handler.restartFlags[1] == kParamValuesChanged);
        CHECK(cp.cf.controller->getParamNormalized(P::kCoreDepthId) == P::normalizedFromLeafWord(kDepthLeaf, slotB));
        CHECK(leafBitsEqual(cp.cf.controller->mirrorForTest(Endpoint::A), slotState(*cp.fx.proc, Endpoint::A)));
        CHECK(leafBitsEqual(cp.cf.controller->mirrorForTest(Endpoint::B), slotState(*cp.fx.proc, Endpoint::B)));
        CHECK(cp.sendsOnThread(run.thread) == 0u);
    }

    SECTION("TimerArm") {
        // The production path: an unobserved Depth edit, one block, then the timer callback (never
        // flushControllerSync() directly).
        const std::uint32_t expected = P::leafWordFromNormalized(kDepthLeaf, 0.23);
        REQUIRE(P::leafBitsOf(a)[kDepthLeaf] != expected);
        MultiParamChanges pc;
        pc.reserve(1);
        pc.addQueue(P::kCoreDepthId).addTestPoint(0, 0.23);
        const AudioRun run = runOnAudioThread(cp.fx, &pc);
        REQUIRE(run.failures == 0);
        REQUIRE(cp.fx.proc->endpointSlotForTest(Endpoint::A, kDepthLeaf) == expected);
        REQUIRE(mirrorLeaf(cp, Endpoint::A, kDepthLeaf) != expected);  // nothing flushed yet

        cp.fx.proc->onTimer(nullptr);
        CHECK(mirrorLeaf(cp, Endpoint::A, kDepthLeaf) == expected);
        CHECK(cp.sendsOnThread(run.thread) == 0u);
        CHECK(run.allocations == 0u);

        // Timer lifecycle on a separate processor: created by initialize, released by terminate.
        auto proc = std::make_unique<P::Processor>();
        REQUIRE(proc->initialize(&cp.host) == Steinberg::kResultOk);
        const bool timerAfterInitialize = proc->hasSyncTimerForTest();
        REQUIRE(proc->terminate() == Steinberg::kResultOk);
        CHECK_FALSE(proc->hasSyncTimerForTest());
#if SMTG_OS_LINUX
        static_cast<void>(timerAfterInitialize);
        SKIP("SDK Timer::create returns nullptr without a factory (timer.cpp:346-351)");
#else
        CHECK(timerAfterInitialize);
#endif
    }
}
