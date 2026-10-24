// ==============================================================================
// Profundum - processor state v2 (Phase 11 T017, T018; FR-040 - FR-046)
// ==============================================================================
// Profundum_StateVersions (SC-012, plan 6.2), processor arms:
//   V1ResetsPhase11Fields  arm 1: a v1 stream on a non-default prior P resets every Phase 11 field
//   CutKeepsPrior          arm 2: a v2 stream cut at k = 0 ... 509 keeps every field after the cut
//   Unchanged              arm 5: the synthesized runtime blob, version 3 refused, rejected endpoints kept
//   PerformanceResetOnLoad FR-046: setState re-centres the hidden Pitch Bend
//   CutKeepsPrior also runs the twin-engine clause (T018): after one block the engine runtime equals that of a
//   twin with the same history that loaded the oracle's fields and a version-0xFF blob.
//   FullStreamAppliesBlob  arm 3 (T018): a full stream's blob is pending before the block (precedence 1) and
//                          applied by it (FR-042)
// Profundum_StateRoundTripV2 (SC-010, processor part; T018) and Profundum_RestoreHandOffNoTear (FR-042 seqlock).
// T019 adds the controller arms (FR-045): ControllerDisplaysB (SC-010 controller part), ControllerV1 (SC-012 arm 1
// controller part) and ControllerCutKeepsPrior (SC-012 arm 4). Every controller load also checks the hidden 4 / 5 / 6 / 7
// re-centre (0.5 / 0 / 0 / 0) and that the load issues no restartComponent and no begin / perform / endEdit.
//
// Every layout offset comes from plugin_ids.h through the fixture (kV2*At, kState*Offset).
// Comparing two serialized STREAMS byte for byte is allowed: they are not float renders.
// ==============================================================================

#include "profundum_test_fixture.h"

#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/performance_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include <allocation_detector.h>
#include <vst_param_changes.h>

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;
using AnchorMorphMode = Krate::DSP::BassMorphEngine::AnchorMorphMode;
using RuntimeBlob = std::array<std::byte, Krate::DSP::BassMorphEngine::kMorphRuntimeBytes>;

constexpr std::size_t kBlock = 512;
constexpr double kFs = 48000.0;

[[nodiscard]] RuntimeBlob blobOf(const StateBytes& b) {
    // P-2 (Phase 12 T019): reads bytes [490, 510) of a getState stream (v3 since Phase 12) or a genuine v2 stream.
    REQUIRE((b.size() == ::Profundum::kStateV2Bytes || b.size() == ::Profundum::kStateV3Bytes));
    RuntimeBlob r{};
    std::memcpy(r.data(), b.data() + ::Profundum::kStateRuntimeOffset, r.size());
    return r;
}

/// The first index in [from, to) where @p a and @p b differ; @p to when they agree.
[[nodiscard]] std::size_t firstMismatch(const StateBytes& a, const StateBytes& b, std::size_t from, std::size_t to) {
    REQUIRE(a.size() >= to);
    REQUIRE(b.size() >= to);
    for (std::size_t i = from; i < to; ++i) {
        if (a[i] != b[i]) {
            return i;
        }
    }
    return to;
}

/// One rendered block (no events, no parameters).
void renderBlock(ProcessorFixture& fx) {
    fx.ensureCapture(kBlock);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
}

/// One rendered block under AllocationScope. Everything is built before the scope; no Catch macro inside it.
[[nodiscard]] std::size_t allocationsInOneBlock(ProcessorFixture& fx) {
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

    ::Profundum::Processor& p = *fx.proc;
    Steinberg::tresult r = Steinberg::kResultFalse;
    std::size_t allocations = 0;
    {
        TestHelpers::AllocationScope scope;
        r = p.process(d);
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    REQUIRE(r == Steinberg::kResultOk);
    return allocations;
}

/// FR-041 (ruling 2026-10-22): the synthesized blob of the registered morph-control values, built by hand.
[[nodiscard]] RuntimeBlob synthesizedBlob(float position, int mode, float rampMs) {
    RuntimeBlob b{};
    const auto putF = [&b](std::size_t at, float v) { std::memcpy(b.data() + at, &v, sizeof(float)); };
    b[::Profundum::kRtVersionOffset] = static_cast<std::byte>(Krate::DSP::BassMorphEngine::kMorphRuntimeFormatVersion);
    putF(::Profundum::kRtTargetOffset, position);
    putF(::Profundum::kRtPositionOffset, position);
    putF(::Profundum::kRtAnchorPositionOffset,
         Krate::DSP::BassMorphEngine::curve(static_cast<AnchorMorphMode>(mode), position));
    b[::Profundum::kRtModeOffset] = static_cast<std::byte>(mode);
    putF(::Profundum::kRtRampOffset, rampMs);
    b[::Profundum::kRtMainMaskOffset] = std::byte{0};
    b[::Profundum::kRtAnchorSelectionOffset] = std::byte{0};
    return b;
}

/// Byte equality of two runtime blobs (a bool, so Catch never has to print std::byte).
[[nodiscard]] bool sameBlob(const RuntimeBlob& a, const RuntimeBlob& b) noexcept {
    return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

[[nodiscard]] float floatAt(const RuntimeBlob& b, std::size_t at) {
    float v = 0.0f;
    std::memcpy(&v, b.data() + at, sizeof(float));
    return v;
}

/// One v2 field: [at, at + size).
struct Field {
    std::size_t at;
    std::size_t size;
};

/// Every v2 field before the runtime blob, in stream order (the version word first).
[[nodiscard]] std::vector<Field> fieldsBeforeBlob() {
    std::vector<Field> f;
    f.push_back({.at = 0, .size = 4});
    for (std::size_t at = kV2GlobalsAt; at < ::Profundum::kStateEndpointAOffset; at += 4u) {
        f.push_back({.at = at, .size = 4});
    }
    f.push_back({.at = ::Profundum::kStateEndpointAOffset, .size = Krate::DSP::kMorphStateBytes});
    f.push_back({.at = ::Profundum::kStateEndpointBOffset, .size = Krate::DSP::kMorphStateBytes});
    return f;
}

/// A non-default v2 stream S, different from P in every field (arm 2).
[[nodiscard]] V2Fields fieldsS() {
    V2Fields f;
    f.globals = V1Globals{.gain = 0.75f, .voiceMode = 1, .polyphony = 2, .ceilingDb = -3.0f};
    f.macros = {0.6f, 0.7f, 0.3f, 0.4f};
    f.priorityLegato = {1, 0};
    f.portamentoMs = 1000.0f;
    f.noteLists = {0, 1, 1, 0};
    f.bendRangeSt = 5.0f;
    f.position = 0.3f;
    f.rampMs = 500.0f;
    f.anchorMorphMode = 0;  // Lock (Hold)
    f.editTarget = 0;       // A
    f.seed = 0xC0DEu;
    return f;
}

/// The engine's own runtime blob right now (serializeMorphRuntime, bass_morph_engine.h).
[[nodiscard]] RuntimeBlob engineRuntimeOf(const ProcessorFixture& fx) {
    REQUIRE(fx.engine() != nullptr);
    RuntimeBlob b{};
    REQUIRE(fx.engine()->serializeMorphRuntime(b.data(), b.size()) == b.size());
    return b;
}

/// The raw 32-bit pattern stored at @p at (byte equality of one float field).
[[nodiscard]] std::uint32_t bitsAt(const RuntimeBlob& b, std::size_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + at, sizeof(v));
    return v;
}

/// Seqlock word @p i of a blob (bytes 4i .. 4i+3), as the processor carries it.
[[nodiscard]] std::uint32_t wordOf(const RuntimeBlob& b, std::size_t i) { return bitsAt(b, 4u * i); }

/// A hand-built version-1 runtime blob (layout: plugin_ids.h kRt*Offset).
[[nodiscard]] RuntimeBlob handBlob(float target, float p, float pa, int mode, float rampMs, std::uint8_t mask,
                                   std::uint8_t anchorSelection) {
    RuntimeBlob b{};
    const auto putF = [&b](std::size_t at, float v) { std::memcpy(b.data() + at, &v, sizeof(float)); };
    b[::Profundum::kRtVersionOffset] = static_cast<std::byte>(Krate::DSP::BassMorphEngine::kMorphRuntimeFormatVersion);
    putF(::Profundum::kRtTargetOffset, target);
    putF(::Profundum::kRtPositionOffset, p);
    putF(::Profundum::kRtAnchorPositionOffset, pa);
    b[::Profundum::kRtModeOffset] = static_cast<std::byte>(mode);
    putF(::Profundum::kRtRampOffset, rampMs);
    b[::Profundum::kRtMainMaskOffset] = static_cast<std::byte>(mask);
    b[::Profundum::kRtAnchorSelectionOffset] = static_cast<std::byte>(anchorSelection);
    return b;
}

/// @p bytes (a v2 stream) with its runtime blob replaced by @p blob.
[[nodiscard]] StateBytes withBlob(StateBytes bytes, const RuntimeBlob& blob) {
    // P-2 (Phase 12 T019): patches bytes [490, 510) of a v2 or v3 stream (same offsets in both).
    REQUIRE((bytes.size() == ::Profundum::kStateV2Bytes || bytes.size() == ::Profundum::kStateV3Bytes));
    std::memcpy(bytes.data() + ::Profundum::kStateRuntimeOffset, blob.data(), blob.size());
    return bytes;
}

[[nodiscard]] bool engineEndpointIsDefault(const ::Profundum::Processor& p, Endpoint e) {
    return Krate::DSP::detail::morph_state_io::leafBitsEqual(p.engineForTest()->getEndpoint(e),
                                                             Krate::DSP::MorphState{});
}

// ---------------------------------------------------------------------------
// Controller helpers (T019, FR-045)
// ---------------------------------------------------------------------------

/// setComponentState of @p bytes (all of them) on @p ctl.
Steinberg::tresult loadControllerBytes(::Profundum::Controller& ctl, std::span<const std::byte> bytes) {
    auto stream = memoryStreamOf(bytes);
    return ctl.setComponentState(stream);
}

/// The twelve non-morphed IDs a v2 stream carries for the controller: 410-417, 105, 1000-1002.
constexpr std::array<Steinberg::Vst::ParamID, 12> kNoteMorphIds{
    ::Profundum::kNoteMonoPriorityId,   ::Profundum::kNoteLegatoId,         ::Profundum::kNotePortamentoId,
    ::Profundum::kNotePortamentoModeId, ::Profundum::kNoteGlideModeId,      ::Profundum::kNoteMemoryId,
    ::Profundum::kNoteRetriggerId,      ::Profundum::kNotePitchBendRangeId, ::Profundum::kMorphPositionId,
    ::Profundum::kMorphAnchorModeId,    ::Profundum::kMorphEditTargetId,    ::Profundum::kMorphRampTimeId};

/// Continuous IDs compared within 1e-9 (FR-045); lists and stepped rows are compared exactly.
[[nodiscard]] bool isContinuousId(Steinberg::Vst::ParamID id) noexcept {
    return id == ::Profundum::kNotePortamentoId || id == ::Profundum::kMorphPositionId ||
           id == ::Profundum::kMorphRampTimeId;
}

/// The normalised value the controller must display for @p id after loading the v2 stream @p b, decoded straight
/// from the bytes (layout: fixture kV2NoteAt / kV2MorphAt). Every value these tests write is in range.
[[nodiscard]] double expectedDisplay(const StateBytes& b, Steinberg::Vst::ParamID id) {
    using namespace ::Profundum;
    const auto idx = [&b](std::size_t at, int maxIndex) {
        return static_cast<double>(static_cast<std::int32_t>(readU32(b, at))) / static_cast<double>(maxIndex);
    };
    const auto f32 = [&b](std::size_t at) { return std::bit_cast<float>(readU32(b, at)); };
    switch (id) {
        case kNoteMonoPriorityId:
            return idx(kV2NoteAt + 0u, kMaxMonoPriorityIndex);
        case kNoteLegatoId:
            return idx(kV2NoteAt + 4u, kMaxLegatoIndex);
        case kNotePortamentoId:
            return normalizedFromPlain(kPortamentoLaw, static_cast<double>(f32(kV2NoteAt + 8u)));
        case kNotePortamentoModeId:
            return idx(kV2NoteAt + 12u, kMaxPortamentoModeIndex);
        case kNoteGlideModeId:
            return idx(kV2NoteAt + 16u, kMaxGlideModeIndex);
        case kNoteMemoryId:
            return idx(kV2NoteAt + 20u, kMaxNoteMemoryIndex);
        case kNoteRetriggerId:
            return idx(kV2NoteAt + 24u, kMaxNoteRetriggerIndex);
        case kNotePitchBendRangeId:
            return static_cast<double>(f32(kV2NoteAt + 28u)) / static_cast<double>(kBendRangeSteps);
        case kMorphPositionId:
            return static_cast<double>(f32(kV2MorphAt + 0u));
        case kMorphRampTimeId:
            return normalizedFromPlain(kMorphRampLaw, static_cast<double>(f32(kV2MorphAt + 4u)));
        case kMorphAnchorModeId:
            return idx(kV2MorphAt + 8u, kNumAnchorMorphModes - 1);
        case kMorphEditTargetId:
            return idx(kV2MorphAt + 12u, kNumEditTargets - 1);
        default:
            break;
    }
    FAIL("expectedDisplay: unexpected ID " << id);
    return 0.0;
}

/// The number of 410-417 / 105 / 1000-1002 whose displayed value differs from what @p expected carries.
[[nodiscard]] int noteMorphMismatches(::Profundum::Controller& ctl, const StateBytes& expected) {
    int bad = 0;
    for (const Steinberg::Vst::ParamID id : kNoteMorphIds) {
        const double want = expectedDisplay(expected, id);
        const double got = ctl.getParamNormalized(id);
        const bool ok = isContinuousId(id) ? std::fabs(got - want) <= 1e-9 : got == want;
        if (!ok) {
            UNSCOPED_INFO("ID " << id << ": displayed " << got << ", expected " << want);
            ++bad;
        }
    }
    return bad;
}

/// The number of 410-417 / 105 / 1000-1002 that do not display their registered default.
[[nodiscard]] int noteMorphNotAtDefault(::Profundum::Controller& ctl) {
    int bad = 0;
    for (const Steinberg::Vst::ParamID id : kNoteMorphIds) {
        const Steinberg::Vst::Parameter* p = ctl.getParameterObject(id);
        REQUIRE(p != nullptr);
        const double want = p->getInfo().defaultNormalizedValue;
        const double got = ctl.getParamNormalized(id);
        if (got != want) {
            UNSCOPED_INFO("ID " << id << ": displayed " << got << ", default " << want);
            ++bad;
        }
    }
    return bad;
}

/// The normalised value an E row displays for mirror state @p s: Sync Rate pairs syncRateIndexOf / 29 (FR-012),
/// every other row normalizedFromLeafWord of its leaf.
[[nodiscard]] double mirrorNormalized(const Krate::DSP::MorphState& s, const ::Profundum::SoundParamRow& row) {
    const ::Profundum::LeafBits bits = ::Profundum::leafBitsOf(s);
    if (row.leaf2 != ::Profundum::kNoLeaf) {
        const int i = ::Profundum::syncRateIndexOf(bits[row.leaf], bits[row.leaf2]);
        REQUIRE(i >= 0);
        return static_cast<double>(i) / static_cast<double>(Krate::DSP::kNoteValueDropdownCount - 1);
    }
    return ::Profundum::normalizedFromLeafWord(row.leaf, bits[row.leaf]);
}

/// The number of the 60 E rows whose displayed value differs from @p s by more than 1e-9.
[[nodiscard]] int eRowMismatches(::Profundum::Controller& ctl, const Krate::DSP::MorphState& s) {
    int bad = 0;
    for (const ::Profundum::SoundParamRow& row : ::Profundum::kSoundRows) {
        const double want = mirrorNormalized(s, row);
        const double got = ctl.getParamNormalized(row.id);
        if (std::fabs(got - want) > 1e-9) {
            UNSCOPED_INFO("E row " << row.id << ": displayed " << got << ", expected " << want);
            ++bad;
        }
    }
    return bad;
}

/// Hidden 4 / 5 / 6 / 7 read 0.5 / 0 / 0 / 0 (FR-046, plan 4.6 step 5).
[[nodiscard]] bool hiddenRecentred(::Profundum::Controller& ctl) {
    return ctl.getParamNormalized(::Profundum::kPitchBendId) == 0.5 &&
           ctl.getParamNormalized(::Profundum::kSustainPedalId) == 0.0 &&
           ctl.getParamNormalized(::Profundum::kModWheelId) == 0.0 &&
           ctl.getParamNormalized(::Profundum::kChannelPressureId) == 0.0;
}

/// Moves the hidden performance parameters off their defaults, so hiddenRecentred has teeth.
void dirtyHidden(::Profundum::Controller& ctl) {
    REQUIRE(ctl.setParamNormalized(::Profundum::kPitchBendId, 1.0) == Steinberg::kResultTrue);
    REQUIRE(ctl.setParamNormalized(::Profundum::kSustainPedalId, 1.0) == Steinberg::kResultTrue);
    REQUIRE(ctl.setParamNormalized(::Profundum::kModWheelId, 1.0) == Steinberg::kResultTrue);
    REQUIRE(ctl.setParamNormalized(::Profundum::kChannelPressureId, 1.0) == Steinberg::kResultTrue);
    REQUIRE_FALSE(hiddenRecentred(ctl));
}

/// The handler saw no restartComponent and no begin / perform / endEdit.
[[nodiscard]] bool handlerSilent(const RecordingComponentHandler& h) {
    return h.restartFlags.empty() && h.beginEdits.empty() && h.performEdits.empty() && h.endEdits.empty();
}

/// The endpoint serialised at @p offset of a v2 stream.
[[nodiscard]] Krate::DSP::MorphState endpointAt(const StateBytes& b, std::size_t offset) {
    REQUIRE(b.size() >= offset + Krate::DSP::kMorphStateBytes);
    Krate::DSP::MorphState s{};
    REQUIRE(Krate::DSP::deserializeMorphState(b.data() + offset, Krate::DSP::kMorphStateBytes, s));
    return s;
}

/// The Edit Target a v2 stream carries.
[[nodiscard]] Endpoint editTargetOf(const StateBytes& b) {
    return readU32(b, kV2MorphAt + 12u) == 0u ? Endpoint::A : Endpoint::B;
}

/// Prior state P as a stream (plan 6.2 SC-012): random A / B (0x12A / 0x12B) and priorFieldsP. The controller reads
/// no runtime blob, so no render is needed.
[[nodiscard]] StateBytes priorStreamP() {
    ProcessorFixture fresh;
    return withFields(withEndpoints(writeV2Stream(*fresh.proc), randomValidMorphState(kPriorSeedA),
                                    randomValidMorphState(kPriorSeedB)),
                      priorFieldsP());
}

/// A controller holding @p prior: loads it and checks the mirror and the display (the baseline every arm departs from).
void holdPrior(ControllerFixture& cf, const StateBytes& prior) {
    REQUIRE(loadControllerBytes(*cf.controller, prior) == Steinberg::kResultOk);
    REQUIRE(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::A),
                                                              endpointAt(prior, ::Profundum::kStateEndpointAOffset)));
    REQUIRE(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::B),
                                                              endpointAt(prior, ::Profundum::kStateEndpointBOffset)));
    REQUIRE(noteMorphMismatches(*cf.controller, prior) == 0);
    REQUIRE(hiddenRecentred(*cf.controller));
    REQUIRE(handlerSilent(cf.handler));
}

}  // namespace

TEST_CASE("Profundum_StateVersions", "[profundum][state]") {
    using namespace ::Profundum;

    SECTION("V1ResetsPhase11Fields") {  // SC-012 arm 1 (FR-044 reset), processor part
        ProcessorFixture p;
        makePriorStateP(p);
        const StateBytes prior = writeV2Stream(*p.proc);
        REQUIRE(prior.size() == kStateV3Bytes);

        const V1Globals g{.gain = 0.3f, .voiceMode = 1, .polyphony = 3, .ceilingDb = -2.0f};
        const std::array<float, 4> macros{0.15f, 0.25f, 0.35f, 0.45f};
        const StateBytes v1 = v1Stream(g, macros);
        REQUIRE(loadStateBytes(*p.proc, v1) == Steinberg::kResultOk);
        renderBlock(p);

        // Globals and macros equal the stream.
        const auto& gp = p.proc->globalParamsForTest();
        CHECK(gp.outputGain.load() == g.gain);
        CHECK(gp.voiceMode.load() == g.voiceMode);
        CHECK(gp.polyphony.load() == g.polyphony);
        CHECK(gp.outputCeilingDb.load() == g.ceilingDb);
        const auto& mp = p.proc->macroParamsForTest();
        CHECK(mp.weight.load() == macros[0]);
        CHECK(mp.darkBright.load() == macros[1]);
        CHECK(mp.tightLoose.load() == macros[2]);
        CHECK(mp.cleanViolent.load() == macros[3]);

        // Both slots and both engine endpoints are MorphState{}.
        CHECK(slotsOf(*p.proc, Endpoint::A) == kDefaultLeafBits);
        CHECK(slotsOf(*p.proc, Endpoint::B) == kDefaultLeafBits);
        CHECK(engineEndpointIsDefault(*p.proc, Endpoint::A));
        CHECK(engineEndpointIsDefault(*p.proc, Endpoint::B));

        // Every note and morph-control getter at its default.
        const Krate::DSP::ProfundumEngine* e = p.proc->engineForTest();
        REQUIRE(e != nullptr);
        const NoteParams defaults{};
        CHECK(static_cast<int>(e->monoPriority()) == defaults.monoPriority.load());
        CHECK(e->legato() == (defaults.legato.load() != 0));
        CHECK(e->portamentoTimeMs() == defaults.portamentoMs.load());
        CHECK(static_cast<int>(e->portamentoMode()) == defaults.portamentoMode.load());
        CHECK(static_cast<int>(e->glideMode()) == defaults.glideMode.load());
        CHECK(static_cast<int>(e->noteMemoryMode()) == defaults.memoryMode.load());
        CHECK(static_cast<int>(e->noteRetrigger()) == defaults.retrigger.load());
        CHECK(e->pitchBendRange() == defaults.bendRangeSt.load());
        CHECK(e->morphPosition() == kDefaultMorphPosition);
        CHECK(e->morphRampMs() == kDefaultMorphRampMs);
        CHECK(static_cast<int>(e->anchorMorphMode()) == kDefaultAnchorMorphModeIndex);

        const StateBytes after = writeV2Stream(*p.proc);
        REQUIRE(after.size() == kStateV3Bytes);
        CHECK(readU32(after, kV2MorphAt + 12u) == static_cast<std::uint32_t>(kDefaultEditTarget));  // Edit Target A
        CHECK(readU32(after, kV2SeedAt) == Krate::DSP::ProfundumEngine::kDefaultSeed);           // LE seed

        // Bytes 0-489 equal a fresh processor's that loaded the same v1 stream and rendered one block.
        // Bytes 490-509 are excluded: P's engine p is still slewing to 0 (plan 4.5.3, inaudible).
        ProcessorFixture q;
        REQUIRE(q.prepare(kFs) == Steinberg::kResultOk);
        REQUIRE(loadStateBytes(*q.proc, v1) == Steinberg::kResultOk);
        renderBlock(q);
        const StateBytes fresh = writeV2Stream(*q.proc);
        REQUIRE(fresh.size() == kStateV3Bytes);
        // Teeth: P held non-default Phase 11 fields before the load.
        REQUIRE(firstMismatch(prior, fresh, kV2NoteAt, kStateRuntimeOffset) != kStateRuntimeOffset);
        const std::size_t mismatch = firstMismatch(after, fresh, 0, kStateRuntimeOffset);
        CAPTURE(mismatch);
        CHECK(mismatch == kStateRuntimeOffset);
    }

    SECTION("CutKeepsPrior") {  // SC-012 arm 2 (FR-044 keep), k = 0 ... 509
        ProcessorFixture source;  // never prepared: S's blob is the synthesized one
        const StateBytes s = withFields(withEndpoints(writeV2Stream(*source.proc), randomValidMorphState(0x13Au),
                                                      randomValidMorphState(0x13Bu)),
                                        fieldsS());
        REQUIRE(s.size() == kStateV3Bytes);
        const std::vector<Field> fields = fieldsBeforeBlob();

        // Teeth: S differs from P in every field but the version word.
        {
            ProcessorFixture ref;
            makePriorStateP(ref);
            const StateBytes pRef = writeV2Stream(*ref.proc);
            for (std::size_t i = 1; i < fields.size(); ++i) {
                CAPTURE(fields[i].at);
                REQUIRE(std::memcmp(s.data() + fields[i].at, pRef.data() + fields[i].at, fields[i].size) != 0);
            }
        }

        // T018 twin clause. The twin depends only on its input stream, which changes only at field edges, so its
        // runtime is cached by stream (about 25 distinct twins instead of 510).
        StateBytes twinKey;
        RuntimeBlob twinRuntime{};

        for (std::size_t k = 0; k < kStateV2Bytes; ++k) {
            CAPTURE(k);
            ProcessorFixture p;
            makePriorStateP(p);
            const StateBytes before = writeV2Stream(*p.proc);
            REQUIRE(before.size() == kStateV3Bytes);
            const std::uint32_t appliedBefore = p.proc->restoreApplyCountForTest();

            const StateBytes cut(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(k));
            const Steinberg::tresult r = loadStateBytes(*p.proc, cut);
            CHECK(r == (k < 4u ? Steinberg::kResultFalse : Steinberg::kResultOk));

            // Before any block: the field oracle over bytes 0-489, P's last published blob over 490-509.
            StateBytes expected = before;
            for (const Field& f : fields) {
                if (f.at + f.size <= k) {
                    std::memcpy(expected.data() + f.at, s.data() + f.at, f.size);
                }
            }
            const StateBytes after = writeV2Stream(*p.proc);
            REQUIRE(after.size() == kStateV3Bytes);
            const std::size_t mismatch = firstMismatch(after, expected, 0, kStateRuntimeOffset);
            CAPTURE(mismatch);
            CHECK(mismatch == kStateRuntimeOffset);
            CHECK(sameBlob(blobOf(after), blobOf(before)));

            CHECK(allocationsInOneBlock(p) == 0u);

            // Twin clause (T018): a twin with history P that loaded a FULL v2 stream holding the oracle's explicit
            // fields and endpoints and a blob whose version byte is 0xFF (never applied). The post-block bytes
            // 490-509 are never part of the byte oracle.
            StateBytes twinStream = expected;
            twinStream[kStateRuntimeOffset + kRtVersionOffset] = std::byte{0xFF};
            if (twinStream != twinKey) {
                ProcessorFixture twin;
                makePriorStateP(twin);
                REQUIRE(loadStateBytes(*twin.proc, twinStream) == Steinberg::kResultOk);
                renderBlock(twin);
                twinRuntime = engineRuntimeOf(twin);
                twinKey = twinStream;
            }
            CHECK(sameBlob(engineRuntimeOf(p), twinRuntime));
            CHECK(p.proc->restoreApplyCountForTest() == appliedBefore);  // a cut stream never restores the blob
        }
    }

    SECTION("FullStreamAppliesBlob") {  // SC-012 arm 3 (k = 510): FR-041 precedence 1, FR-042
        // S: a full stream saved mid-ramp. The source loads S's fields (position 0.3, ramp 500 ms, Lock) on a fresh
        // prepared engine (p = 0); its own loaded blob is the synthesized one of position 0 and is not applied
        // (target mismatch), so p ramps 0 -> 0.3 and 10 blocks (~107 ms) leave it mid-ramp.
        ProcessorFixture source;
        REQUIRE(source.prepare(kFs) == Steinberg::kResultOk);
        const StateBytes seedStream = withFields(withEndpoints(writeV2Stream(*source.proc), randomValidMorphState(0x13Au),
                                                               randomValidMorphState(0x13Bu)),
                                                 fieldsS());
        REQUIRE(loadStateBytes(*source.proc, seedStream) == Steinberg::kResultOk);
        for (int i = 0; i < 10; ++i) {
            renderBlock(source);
        }
        const StateBytes s = writeV2Stream(*source.proc);
        REQUIRE(s.size() == kStateV3Bytes);
        const RuntimeBlob sBlob = blobOf(s);
        REQUIRE(floatAt(sBlob, kRtTargetOffset) == fieldsS().position);
        REQUIRE(bitsAt(sBlob, kRtPositionOffset) != bitsAt(sBlob, kRtTargetOffset));  // saved mid-ramp: p != target

        ProcessorFixture p;
        makePriorStateP(p);
        const std::uint32_t appliedBefore = p.proc->restoreApplyCountForTest();
        REQUIRE(loadStateBytes(*p.proc, s) == Steinberg::kResultOk);

        // Before any block: getState writes the loaded blob (precedence 1), so load-then-save is byte-stable.
        const StateBytes pending = writeV2Stream(*p.proc);
        REQUIRE(pending.size() == kStateV3Bytes);
        CHECK(sameBlob(blobOf(pending), sBlob));
        const std::size_t mismatch = firstMismatch(pending, s, 0, kStateV3Bytes);
        CAPTURE(mismatch);
        CHECK(mismatch == kStateV3Bytes);

        // One block applies it (plan 4.5.1 step 9).
        renderBlock(p);
        CHECK(p.proc->restoreApplyCountForTest() == appliedBefore + 1u);
        CHECK(sameBlob(p.proc->lastRestoreBlobForTest(), sBlob));
        const RuntimeBlob restored = engineRuntimeOf(p);

        // Precondition (teeth): without the blob (a 0xFF twin with the same history and the same fields) the
        // engine's runtime differs from the restored one in p, p_a or the mask byte.
        StateBytes twinStream = s;
        twinStream[kStateRuntimeOffset + kRtVersionOffset] = std::byte{0xFF};
        ProcessorFixture twin;
        makePriorStateP(twin);
        REQUIRE(loadStateBytes(*twin.proc, twinStream) == Steinberg::kResultOk);
        renderBlock(twin);
        const RuntimeBlob twinRuntime = engineRuntimeOf(twin);
        CHECK(twin.proc->restoreApplyCountForTest() == appliedBefore);
        const bool differs = bitsAt(twinRuntime, kRtPositionOffset) != bitsAt(restored, kRtPositionOffset) ||
                             bitsAt(twinRuntime, kRtAnchorPositionOffset) != bitsAt(restored, kRtAnchorPositionOffset) ||
                             twinRuntime[kRtMainMaskOffset] != restored[kRtMainMaskOffset];
        CHECK(differs);
    }


    SECTION("Unchanged") {  // SC-012 arm 5
        // (a) The pre-render blob of a fresh prepared processor is the synthesized one (FR-041),
        //     p_a = curve(mode, position): Lock / Blend / Punch at 0.7 -> 0 / 0.7 / 0.343.
        const std::array<float, 3> kExpectedPa{0.0f, 0.7f, 0.343f};
        for (int mode = 0; mode < kNumAnchorMorphModes; ++mode) {
            CAPTURE(mode);
            ProcessorFixture fx;
            REQUIRE(fx.prepare(kFs) == Steinberg::kResultOk);
            Krate::Test::ParameterChanges pc;
            pc.addChange(kMorphPositionId, 0.7);
            pc.addChange(kMorphAnchorModeId, static_cast<double>(mode) / static_cast<double>(kNumAnchorMorphModes - 1));
            pc.addChange(kMorphRampTimeId, normalizedFromPlain(kMorphRampLaw, 200.0));
            REQUIRE(fx.processNoOutputs(&pc) == Steinberg::kResultOk);  // parameters only: nothing renders

            const StateBytes st = writeV2Stream(*fx.proc);
            REQUIRE(st.size() == kStateV3Bytes);
            const float pos = std::bit_cast<float>(readU32(st, kV2MorphAt));
            const float ramp = std::bit_cast<float>(readU32(st, kV2MorphAt + 4u));
            CHECK(pos == 0.7f);
            CHECK(std::fabs(ramp - 200.0f) <= 0.01f);
            CHECK(readU32(st, kV2MorphAt + 8u) == static_cast<std::uint32_t>(mode));
            const RuntimeBlob blob = blobOf(st);
            CHECK(sameBlob(blob, synthesizedBlob(pos, mode, ramp)));
            CHECK(std::fabs(floatAt(blob, kRtAnchorPositionOffset) - kExpectedPa[static_cast<std::size_t>(mode)]) <=
                  1e-6f);
        }

        // (b) Blend / position 0: the synthesized blob equals a real engine's serializeMorphRuntime after
        //     prepare, which proves the re-stated kRt*Offset constants.
        {
            ProcessorFixture fx;
            REQUIRE(fx.prepare(kFs) == Steinberg::kResultOk);
            const RuntimeBlob synth = blobOf(writeV2Stream(*fx.proc));
            CHECK(sameBlob(synth,
                           synthesizedBlob(kDefaultMorphPosition, kDefaultAnchorMorphModeIndex, kDefaultMorphRampMs)));

            RuntimeBlob own{};
            REQUIRE(fx.engine()->serializeMorphRuntime(own.data(), own.size()) == own.size());
            CHECK(sameBlob(synth, own));

            auto engine = std::make_unique<Krate::DSP::ProfundumEngine>();
            engine->prepare(kFs, makeProfundumEngineConfig());
            RuntimeBlob real{};
            REQUIRE(engine->serializeMorphRuntime(real.data(), real.size()) == real.size());
            CHECK(sameBlob(synth, real));
        }

        // (c) Version 3 -> kResultFalse, getState unchanged byte for byte.
        {
            ProcessorFixture fx;
            REQUIRE(fx.prepare(kFs) == Steinberg::kResultOk);
            const StateBytes base = withFields(withEndpoints(writeV2Stream(*fx.proc), randomValidMorphState(0x21Au),
                                                             randomValidMorphState(0x21Bu)),
                                               priorFieldsP());
            REQUIRE(loadStateBytes(*fx.proc, base) == Steinberg::kResultOk);
            const StateBytes before = writeV2Stream(*fx.proc);

            StateBytes v3 = withFields(withEndpoints(before, randomValidMorphState(0x21Cu),
                                                     randomValidMorphState(0x21Du)),
                                       fieldsS());
            patchI32(v3, 0, kCurrentStateVersion + 1);
            REQUIRE(loadStateBytes(*fx.proc, v3) == Steinberg::kResultFalse);
            const StateBytes after = writeV2Stream(*fx.proc);
            REQUIRE(after.size() == before.size());
            CHECK(firstMismatch(after, before, 0, after.size()) == after.size());
        }

        // (d) A flipped endpoint version byte, or a bool byte of 2, leaves that endpoint's slot at its prior value.
        {
            const Krate::DSP::MorphState a0 = randomValidMorphState(0x31Au);
            const Krate::DSP::MorphState b0 = randomValidMorphState(0x31Bu);
            const Krate::DSP::MorphState a1 = randomValidMorphState(0x31Cu);
            const Krate::DSP::MorphState b1 = randomValidMorphState(0x31Du);
            REQUIRE(leafBitsOf(a0) != leafBitsOf(a1));
            REQUIRE(leafBitsOf(b0) != leafBitsOf(b1));

            std::size_t boolLeaf = kNumLeaves;
            for (std::size_t i = 0; i < kNumLeaves; ++i) {
                if (lawOf(static_cast<std::uint8_t>(i)).kind == Krate::DSP::MorphState::Kind::Bool) {
                    boolLeaf = i;
                    break;
                }
            }
            REQUIRE(boolLeaf < kNumLeaves);

            {
                ProcessorFixture fx;
                const StateBytes base = withEndpoints(writeV2Stream(*fx.proc), a0, b0);
                REQUIRE(loadStateBytes(*fx.proc, base) == Steinberg::kResultOk);
                REQUIRE(slotsOf(*fx.proc, Endpoint::A) == leafBitsOf(a0));

                StateBytes flipped = withEndpoints(base, a1, b1);
                flipped[kStateEndpointAOffset] ^= std::byte{0xFF};
                REQUIRE(loadStateBytes(*fx.proc, flipped) == Steinberg::kResultOk);
                CHECK(slotsOf(*fx.proc, Endpoint::A) == leafBitsOf(a0));  // rejected: prior kept
                CHECK(slotsOf(*fx.proc, Endpoint::B) == leafBitsOf(b1));  // the chain continued
            }
            {
                ProcessorFixture fx;
                const StateBytes base = withEndpoints(writeV2Stream(*fx.proc), a0, b0);
                REQUIRE(loadStateBytes(*fx.proc, base) == Steinberg::kResultOk);
                REQUIRE(slotsOf(*fx.proc, Endpoint::B) == leafBitsOf(b0));

                StateBytes badBool = withEndpoints(base, a1, b1);
                badBool[kStateEndpointBOffset + Krate::DSP::detail::morph_state_io::kLeafOffsets[boolLeaf]] =
                    std::byte{2};
                REQUIRE(loadStateBytes(*fx.proc, badBool) == Steinberg::kResultOk);
                CHECK(slotsOf(*fx.proc, Endpoint::A) == leafBitsOf(a1));
                CHECK(slotsOf(*fx.proc, Endpoint::B) == leafBitsOf(b0));  // rejected: prior kept
            }
        }
    }

    SECTION("ControllerV1") {  // SC-012 arm 1, controller part (FR-044 reset, FR-045)
        const StateBytes prior = priorStreamP();
        ControllerFixture cf;
        holdPrior(cf, prior);
        // Teeth: P displays a non-default value for every one of the twelve IDs, and non-default endpoints.
        for (const Steinberg::Vst::ParamID id : kNoteMorphIds) {
            CAPTURE(id);
            REQUIRE(cf.controller->getParamNormalized(id) !=
                    cf.controller->getParameterObject(id)->getInfo().defaultNormalizedValue);
        }
        REQUIRE_FALSE(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::A),
                                                                        Krate::DSP::MorphState{}));
        REQUIRE_FALSE(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::B),
                                                                        Krate::DSP::MorphState{}));
        dirtyHidden(*cf.controller);

        const StateBytes v1 = v1Stream(V1Globals{.gain = 0.3f, .voiceMode = 1, .polyphony = 3, .ceilingDb = -2.0f},
                                       {0.15f, 0.25f, 0.35f, 0.45f});
        REQUIRE(loadControllerBytes(*cf.controller, v1) == Steinberg::kResultOk);

        CHECK(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::A),
                                                                Krate::DSP::MorphState{}));
        CHECK(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::B),
                                                                Krate::DSP::MorphState{}));
        CHECK(noteMorphNotAtDefault(*cf.controller) == 0);
        CHECK(cf.controller->getParamNormalized(kMorphEditTargetId) == 0.0);  // A
        CHECK(eRowMismatches(*cf.controller, Krate::DSP::MorphState{}) == 0);
        CHECK(hiddenRecentred(*cf.controller));
        CHECK(handlerSilent(cf.handler));
    }

    SECTION("ControllerCutKeepsPrior") {  // SC-012 arm 4 (FR-044 keep, FR-045)
        const StateBytes prior = priorStreamP();
        ProcessorFixture source;  // never prepared: the controller reads no blob
        const StateBytes s = withFields(withEndpoints(writeV2Stream(*source.proc), randomValidMorphState(0x13Au),
                                                      randomValidMorphState(0x13Bu)),
                                        fieldsS());
        REQUIRE(s.size() == kStateV3Bytes);
        const std::vector<Field> fields = fieldsBeforeBlob();
        // Teeth: S differs from P in every displayed field and in both endpoints.
        for (const Steinberg::Vst::ParamID id : kNoteMorphIds) {
            CAPTURE(id);
            REQUIRE(expectedDisplay(s, id) != expectedDisplay(prior, id));
        }
        REQUIRE_FALSE(Krate::DSP::detail::morph_state_io::leafBitsEqual(endpointAt(s, kStateEndpointAOffset),
                                                                        endpointAt(prior, kStateEndpointAOffset)));
        REQUIRE_FALSE(Krate::DSP::detail::morph_state_io::leafBitsEqual(endpointAt(s, kStateEndpointBOffset),
                                                                        endpointAt(prior, kStateEndpointBOffset)));

        // One load checked against @p expected: a v2 stream whose fields and endpoints are what must be displayed.
        const auto checkAgainst = [](ControllerFixture& cf, const StateBytes& expected) {
            const Krate::DSP::MorphState a = endpointAt(expected, kStateEndpointAOffset);
            const Krate::DSP::MorphState b = endpointAt(expected, kStateEndpointBOffset);
            CHECK(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::A), a));
            CHECK(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::B), b));
            CHECK(noteMorphMismatches(*cf.controller, expected) == 0);
            CHECK(eRowMismatches(*cf.controller, editTargetOf(expected) == Endpoint::A ? a : b) == 0);
            CHECK(hiddenRecentred(*cf.controller));
            CHECK(handlerSilent(cf.handler));
        };

        // Cuts in the note block (50), endpoint A (150), endpoint B (300) and the runtime blob (500).
        for (const std::size_t k : {std::size_t{50}, std::size_t{150}, std::size_t{300}, std::size_t{500}}) {
            CAPTURE(k);
            ControllerFixture cf;
            holdPrior(cf, prior);
            dirtyHidden(*cf.controller);

            const StateBytes cut(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(k));
            REQUIRE(loadControllerBytes(*cf.controller, cut) == Steinberg::kResultOk);

            // The field oracle: fields fully inside [0, k) take S's bytes, all others P's (an endpoint needs all 201).
            StateBytes expected = prior;
            for (const Field& f : fields) {
                if (f.at + f.size <= k) {
                    std::memcpy(expected.data() + f.at, s.data() + f.at, f.size);
                }
            }
            checkAgainst(cf, expected);
        }

        // A full S whose endpoint A version byte is flipped: A is rejected (keeps P's), everything else takes S.
        {
            ControllerFixture cf;
            holdPrior(cf, prior);
            dirtyHidden(*cf.controller);

            StateBytes flipped = s;
            flipped[kStateEndpointAOffset] ^= std::byte{0xFF};
            REQUIRE(loadControllerBytes(*cf.controller, flipped) == Steinberg::kResultOk);

            StateBytes expected = s;
            std::memcpy(expected.data() + kStateEndpointAOffset, prior.data() + kStateEndpointAOffset,
                        Krate::DSP::kMorphStateBytes);
            checkAgainst(cf, expected);
        }
    }

    SECTION("PerformanceResetOnLoad") {  // FR-046: Pitch Bend re-centred by setState (v2 and v1)
        ProcessorFixture fx;
        REQUIRE(fx.prepare(kFs) == Steinberg::kResultOk);

        REQUIRE(fx.setParam(kPitchBendId, 1.0) == Steinberg::kResultOk);
        REQUIRE(fx.engine()->pitchBend() == 1.0f);
        const StateBytes v2 = writeV2Stream(*fx.proc);
        REQUIRE(loadStateBytes(*fx.proc, v2) == Steinberg::kResultOk);
        renderBlock(fx);
        CHECK(fx.engine()->pitchBend() == 0.0f);

        REQUIRE(fx.setParam(kPitchBendId, 0.0) == Steinberg::kResultOk);
        REQUIRE(fx.engine()->pitchBend() == -1.0f);
        REQUIRE(loadStateBytes(*fx.proc, v1Stream(V1Globals{}, {0.5f, 0.5f, 0.5f, 0.5f})) == Steinberg::kResultOk);
        renderBlock(fx);
        CHECK(fx.engine()->pitchBend() == 0.0f);
    }
}

// SC-010 (processor part, T018): a saved v2 stream restored into a fresh processor BEFORE setupProcessing /
// setActive(true) is byte-identical after prepare and one block, and the runtime blob (hysteresis state) is applied
// exactly once (FR-042).
TEST_CASE("Profundum_StateRoundTripV2", "[profundum][state]") {
    using namespace ::Profundum;

    SECTION("FullRoundTrip") {
        const Krate::DSP::MorphState a = randomValidMorphState(0x11Au);
        const Krate::DSP::MorphState b = randomValidMorphState(0x11Bu);

        // Source: every note field non-default (priorFieldsP's note block), position 1 with a 40 ms ramp, Edit
        // Target B, seed 0x1234; settled at p = 1 (every selection B by rule 1), then ramped to 0.5 (inside the
        // hysteresis band) and rendered to 2 s.
        ProcessorFixture src;
        REQUIRE(src.prepare(kFs) == Steinberg::kResultOk);
        V2Fields f = priorFieldsP();
        f.position = 1.0f;
        f.rampMs = 40.0f;
        f.anchorMorphMode = kDefaultAnchorMorphModeIndex;
        f.editTarget = 1;
        f.seed = 0x1234u;
        REQUIRE(loadStateBytes(*src.proc, withFields(withEndpoints(writeV2Stream(*src.proc), a, b), f)) ==
                Steinberg::kResultOk);
        ProcessorFixture::ScriptedEvent toHalf{};
        toHalf.sample = 12000;  // 250 ms: p has settled at 1
        toHalf.kind = ProcessorFixture::ScriptedEvent::Kind::Param;
        toHalf.id = kMorphPositionId;
        toHalf.value = 0.5;
        src.renderScript(std::span<const ProcessorFixture::ScriptedEvent>(&toHalf, 1), 96000u, kBlock);

        const StateBytes saved = writeV2Stream(*src.proc);
        REQUIRE(saved.size() == kStateV3Bytes);
        const RuntimeBlob savedBlob = blobOf(saved);
        REQUIRE(floatAt(savedBlob, kRtTargetOffset) == 0.5f);
        REQUIRE(floatAt(savedBlob, kRtPositionOffset) == 0.5f);  // settled
        CHECK(std::to_integer<int>(savedBlob[kRtMainMaskOffset]) != 0);  // teeth: hysteresis state a fresh engine lacks

        // Fresh processor: setState first, then prepare, then one block.
        ProcessorFixture dst;
        REQUIRE(loadStateBytes(*dst.proc, saved) == Steinberg::kResultOk);
        REQUIRE(dst.prepare(kFs) == Steinberg::kResultOk);
        renderBlock(dst);

        const StateBytes again = writeV2Stream(*dst.proc);
        REQUIRE(again.size() == kStateV3Bytes);
        const std::size_t mismatch = firstMismatch(again, saved, 0, kStateV3Bytes);
        CAPTURE(mismatch);
        CHECK(mismatch == kStateV3Bytes);
        CHECK(std::memcmp(again.data(), saved.data(), kStateV3Bytes) == 0);
        CHECK(Krate::DSP::detail::morph_state_io::leafBitsEqual(dst.engine()->getEndpoint(Endpoint::A), a));
        CHECK(Krate::DSP::detail::morph_state_io::leafBitsEqual(dst.engine()->getEndpoint(Endpoint::B), b));
        CHECK(dst.engine()->morphPosition() == src.engine()->morphPosition());
        CHECK(sameBlob(blobOf(again), savedBlob));
        CHECK(sameBlob(engineRuntimeOf(dst), savedBlob));
        CHECK(dst.proc->restoreApplyCountForTest() == 1u);

        // A following Depth edit lands in B (the restored Edit Target); A is unchanged.
        REQUIRE(slotsOf(*dst.proc, Endpoint::B) == leafBitsOf(b));
        REQUIRE(dst.setParam(kCoreDepthId, 0.37) == Steinberg::kResultOk);
        CHECK(slotsOf(*dst.proc, Endpoint::B) != leafBitsOf(b));
        CHECK(slotsOf(*dst.proc, Endpoint::A) == leafBitsOf(a));
    }

    SECTION("ControllerDisplaysB") {  // SC-010 controller part (FR-045)
        // The FullRoundTrip source: random A / B, non-default note fields, position 1 -> 0.5 at the 40 ms ramp,
        // Edit Target B, seed 0x1234, 2 s rendered.
        const Krate::DSP::MorphState a = randomValidMorphState(0x11Au);
        const Krate::DSP::MorphState b = randomValidMorphState(0x11Bu);
        ProcessorFixture src;
        REQUIRE(src.prepare(kFs) == Steinberg::kResultOk);
        V2Fields f = priorFieldsP();
        f.position = 1.0f;
        f.rampMs = 40.0f;
        f.anchorMorphMode = kDefaultAnchorMorphModeIndex;
        f.editTarget = 1;
        f.seed = 0x1234u;
        REQUIRE(loadStateBytes(*src.proc, withFields(withEndpoints(writeV2Stream(*src.proc), a, b), f)) ==
                Steinberg::kResultOk);
        ProcessorFixture::ScriptedEvent toHalf{};
        toHalf.sample = 12000;
        toHalf.kind = ProcessorFixture::ScriptedEvent::Kind::Param;
        toHalf.id = kMorphPositionId;
        toHalf.value = 0.5;
        src.renderScript(std::span<const ProcessorFixture::ScriptedEvent>(&toHalf, 1), 96000u, kBlock);
        const StateBytes saved = writeV2Stream(*src.proc);
        REQUIRE(saved.size() == kStateV3Bytes);

        ControllerFixture cf;
        dirtyHidden(*cf.controller);
        REQUIRE(loadControllerBytes(*cf.controller, saved) == Steinberg::kResultOk);

        CHECK(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::A), a));
        CHECK(Krate::DSP::detail::morph_state_io::leafBitsEqual(cf.controller->mirrorForTest(Endpoint::B), b));
        CHECK(eRowMismatches(*cf.controller, b) == 0);           // the 60 E values display B
        CHECK(eRowMismatches(*cf.controller, a) != 0);           // teeth: A would display differently
        CHECK(noteMorphMismatches(*cf.controller, saved) == 0);  // 410-417, 105, 1000-1002
        CHECK(cf.controller->getParamNormalized(kMorphEditTargetId) == 1.0);
        CHECK(std::fabs(cf.controller->getParamNormalized(kMorphPositionId) - 0.5) <= 1e-9);
        CHECK(hiddenRecentred(*cf.controller));
        CHECK(cf.handler.restartFlags.empty());   // 0 restartComponent during the load
        CHECK(cf.handler.performEdits.empty());   // 0 performEdit during the load
        CHECK(handlerSilent(cf.handler));
    }

    SECTION("TinyPosition") {  // Risk R10: a tiny or denormal position is stored as +0 and still restores
        volatile std::uint32_t denormalBits = 0x00000001u;  // through a volatile: no constant folding
        const std::array<float, 2> tinyValues{1e-20f, std::bit_cast<float>(static_cast<std::uint32_t>(denormalBits))};
        for (std::size_t run = 0; run < tinyValues.size(); ++run) {
            const float tiny = tinyValues[run];
            CAPTURE(run);

            // Through parameter 105: ramp from 0.5 down to the tiny value and settle (40 ms default ramp).
            ProcessorFixture src;
            REQUIRE(src.prepare(kFs) == Steinberg::kResultOk);
            REQUIRE(src.setParam(kMorphPositionId, 0.5) == Steinberg::kResultOk);
            for (int i = 0; i < 24; ++i) {
                renderBlock(src);
            }
            REQUIRE(src.setParam(kMorphPositionId, static_cast<double>(tiny)) == Steinberg::kResultOk);
            for (int i = 0; i < 24; ++i) {
                renderBlock(src);
            }
            const StateBytes saved = writeV2Stream(*src.proc);
            REQUIRE(saved.size() == kStateV3Bytes);
            CHECK(readU32(saved, kV2MorphAt) == 0u);  // the stored position's bits are +0
            CHECK(bitsAt(blobOf(saved), kRtTargetOffset) == 0u);

            // save -> fresh setState -> one block: the blob is applied; save-load-save is byte-identical.
            ProcessorFixture dst;
            REQUIRE(loadStateBytes(*dst.proc, saved) == Steinberg::kResultOk);
            REQUIRE(dst.prepare(kFs) == Steinberg::kResultOk);
            renderBlock(dst);
            CHECK(dst.proc->restoreApplyCountForTest() == 1u);
            const StateBytes again = writeV2Stream(*dst.proc);
            REQUIRE(again.size() == kStateV3Bytes);
            CHECK(firstMismatch(again, saved, 0, kStateV3Bytes) == kStateV3Bytes);

            // The same value hand-written into bytes 68-71 of the stream gives the same result.
            StateBytes hand = saved;
            patchF32(hand, kV2MorphAt, tiny);
            REQUIRE(readU32(hand, kV2MorphAt) == std::bit_cast<std::uint32_t>(tiny));
            ProcessorFixture dst2;
            REQUIRE(loadStateBytes(*dst2.proc, hand) == Steinberg::kResultOk);
            REQUIRE(dst2.prepare(kFs) == Steinberg::kResultOk);
            renderBlock(dst2);
            CHECK(dst2.proc->restoreApplyCountForTest() == 1u);
            const StateBytes again2 = writeV2Stream(*dst2.proc);
            REQUIRE(again2.size() == kStateV3Bytes);
            CHECK(firstMismatch(again2, saved, 0, kStateV3Bytes) == kStateV3Bytes);
        }
    }
}

// FR-042 (plan 4.5.2 "Restore hand-off"): two setState calls racing the audio thread never install a mix of two
// blobs. S1 / S2 share every explicit field and endpoint (so both pass the bitwise target / mode / ramp check)
// and differ in their mid-ramp blobs.
TEST_CASE("Profundum_RestoreHandOffNoTear", "[profundum][state][rt]") {
    using namespace ::Profundum;
    constexpr std::size_t kTearBlock = 128;
    constexpr int kRounds = 2000;
    constexpr int kBlocksPerRound = 8;

    // Endpoints MorphState{} on both sides: equal governing amounts, so only rules 1 and 3 decide a selection and
    // both hand-built blobs are valid (restoreRuntime re-checks them against the endpoints).
    V2Fields f;
    f.position = 0.5f;
    f.rampMs = 200.0f;
    f.anchorMorphMode = kDefaultAnchorMorphModeIndex;
    const RuntimeBlob blob1 = handBlob(0.5f, 0.9f, 0.9f, kDefaultAnchorMorphModeIndex, 200.0f, 0x0Fu, 1u);
    const RuntimeBlob blob2 = handBlob(0.5f, 0.1f, 0.1f, kDefaultAnchorMorphModeIndex, 200.0f, 0x00u, 0u);
    // Word 0 (the version byte and the first three target bytes) is equal by construction: FR-042 applies a blob
    // only when its target equals the registered position bitwise, and S1 / S2 share that position. Words 1-4
    // (target's last byte with p, p with p_a, p_a with mode and ramp, ramp with the two selection bytes) differ.
    REQUIRE(wordOf(blob1, 0) == wordOf(blob2, 0));
    for (std::size_t w = 1; w < ::Profundum::Processor::kRuntimeWords; ++w) {
        CAPTURE(w);
        REQUIRE(wordOf(blob1, w) != wordOf(blob2, w));
    }

    ProcessorFixture fx;
    REQUIRE(fx.prepare(kFs) == Steinberg::kResultOk);
    const StateBytes base =
        withFields(withEndpoints(writeV2Stream(*fx.proc), Krate::DSP::MorphState{}, Krate::DSP::MorphState{}), f);
    const StateBytes s1 = withBlob(base, blob1);
    const StateBytes s2 = withBlob(base, blob2);

    // Precondition: each blob alone is applied by one block.
    for (const StateBytes* st : {&s1, &s2}) {
        ProcessorFixture solo;
        REQUIRE(solo.prepare(kFs) == Steinberg::kResultOk);
        REQUIRE(loadStateBytes(*solo.proc, *st) == Steinberg::kResultOk);
        renderBlock(solo);
        REQUIRE(solo.proc->restoreApplyCountForTest() == 1u);
        REQUIRE(sameBlob(solo.proc->lastRestoreBlobForTest(), blobOf(*st)));
    }

    // Prime: S2 applied, so lastRestoreBlob is a whole blob before the first round.
    REQUIRE(loadStateBytes(*fx.proc, s2) == Steinberg::kResultOk);
    renderBlock(fx);
    REQUIRE(sameBlob(fx.proc->lastRestoreBlobForTest(), blob2));

    // Everything the rounds use is built here: no allocation and no Catch macro on the audio thread.
    auto stream1 = memoryStreamOf(s1);
    auto stream2 = memoryStreamOf(s2);
    std::array<float*, 2> channels{fx.audioL(), fx.audioR()};
    Steinberg::Vst::AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.silenceFlags = 0;
    bus.channelBuffers32 = channels.data();
    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = static_cast<Steinberg::int32>(kTearBlock);
    d.numInputs = 0;
    d.inputs = nullptr;
    d.numOutputs = 1;
    d.outputs = &bus;
    d.inputParameterChanges = nullptr;
    d.outputParameterChanges = nullptr;
    d.inputEvents = nullptr;
    d.outputEvents = nullptr;
    d.processContext = nullptr;
    ::Profundum::Processor& proc = *fx.proc;

    std::size_t allocations = 0;
    int processFailures = 0;
    int mixedAfterJoin = 0;
    int notS2AfterExtraBlock = 0;
    int setStateFailures = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int round = 0; round < kRounds; ++round) {
        std::atomic<bool> started{false};
        std::size_t roundAllocations = 0;
        int roundFailures = 0;
        std::thread audio([&]() noexcept {
            TestHelpers::ThreadScopedAllocationScope scope;  // counts this thread's allocations only
            started.store(true, std::memory_order_release);
            for (int blockIndex = 0; blockIndex < kBlocksPerRound; ++blockIndex) {
                if (proc.process(d) != Steinberg::kResultOk) {
                    ++roundFailures;
                }
            }
            roundAllocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        });
        while (!started.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        static_cast<void>(stream1->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr));
        if (proc.setState(stream1) != Steinberg::kResultOk) {
            ++setStateFailures;
        }
        static_cast<void>(stream2->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr));
        if (proc.setState(stream2) != Steinberg::kResultOk) {
            ++setStateFailures;
        }
        audio.join();

        allocations += roundAllocations;
        processFailures += roundFailures;
        const RuntimeBlob& afterJoin = proc.lastRestoreBlobForTest();
        if (!sameBlob(afterJoin, blob1) && !sameBlob(afterJoin, blob2)) {
            ++mixedAfterJoin;
        }
        if (proc.process(d) != Steinberg::kResultOk) {  // one more block, on the test thread
            ++processFailures;
        }
        if (!sameBlob(proc.lastRestoreBlobForTest(), blob2)) {
            ++notS2AfterExtraBlock;
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    WARN("Profundum_RestoreHandOffNoTear: " << kRounds << " rounds in " << seconds << " s (budget ~10 s)");

    CHECK(setStateFailures == 0);
    CHECK(processFailures == 0);
    CHECK(mixedAfterJoin == 0);
    CHECK(notS2AfterExtraBlock == 0);
    CHECK(allocations == 0u);
}
