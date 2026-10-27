// ==============================================================================
// Profundum - parameter wiring (Phase 11 T015, T016)
// ==============================================================================
// Profundum_DefaultsMatchEngine (SC-007, FR-003): a fresh prepared processor's
//   engine equals the engine defaults; a host re-sending every registered
//   default changes nothing, bitwise.
// Profundum_EveryParamReachesEngine (SC-008, FR-010 - FR-014): a 72-row oracle,
//   norm 0 and norm 1 per row, read back from the engine; no cross-talk between
//   leaves; the Edit Target re-routes; every Sync Rate entry lands (FR-012).
// Profundum_EditTargetAddressing (SC-009 parts 1-3: the processor, T015; parts
//   4-6: the controller mirror, Edit Target re-display and endpoint notify, T016).
//
// Every equality is same-binary, same-process (no float golden across binaries).
// ==============================================================================

#include "profundum_test_fixture.h"

#include "controller/controller.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace P = ::Profundum;
using ProfEngine = Krate::DSP::ProfundumEngine;
using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;
using AnchorMorphMode = Krate::DSP::BassMorphEngine::AnchorMorphMode;
using Krate::DSP::detail::morph_state_io::leafBitsEqual;
using Steinberg::Vst::ParamID;
using Scripted = ProcessorFixture::ScriptedEvent;

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::uint8_t kDepthLeaf = 0;        // recipe.depth (ID 200)
constexpr std::uint8_t kCyclePhaseLeaf = 45;  // motion.cyclePhaseDeg (ID 908), [0, 360)

[[nodiscard]] std::uint32_t bitsOf(float x) noexcept {
    return std::bit_cast<std::uint32_t>(x);
}

[[nodiscard]] std::unique_ptr<ProcessorFixture> makePrepared() {
    auto fx = std::make_unique<ProcessorFixture>();
    REQUIRE(fx->prepare(kFs, 4096) == Steinberg::kResultOk);
    return fx;
}

[[nodiscard]] P::LeafBits engineBits(const ProcessorFixture& fx, Endpoint e) {
    REQUIRE(fx.engine() != nullptr);
    return P::leafBitsOf(fx.engine()->getEndpoint(e));
}

// -----------------------------------------------------------------------------
// SC-008 oracle (explicit: the leaf of every E row is typed here by hand, so the
// table cross-checks kSoundRows instead of reading it back)
// -----------------------------------------------------------------------------
enum class Read : std::uint8_t {
    Leaf,
    SyncPair,
    MonoPriority,
    Legato,
    Portamento,
    PortamentoMode,
    GlideMode,
    NoteMemory,
    Retrigger,
    BendRange,
    Position,
    RampMs,
    AnchorMode,
    PitchBend,
};

struct Oracle {
    ParamID id;
    Read read;
    std::uint8_t leaf;   // Leaf / SyncPair (note leaf)
    std::uint8_t leaf2;  // SyncPair modifier leaf
};

constexpr std::uint8_t kNo = P::kNoLeaf;

// clang-format off
constexpr std::array<Oracle, 72> kOracle{{
    // 104 Mass
    {.id = P::kMassId, .read = Read::Leaf, .leaf = 52, .leaf2 = kNo},
    // 200-208 Core
    {.id = P::kCoreDepthId, .read = Read::Leaf, .leaf = 0, .leaf2 = kNo},
    {.id = P::kCoreBodyId, .read = Read::Leaf, .leaf = 1, .leaf2 = kNo},
    {.id = P::kCoreBodyCurvatureId, .read = Read::Leaf, .leaf = 2, .leaf2 = kNo},
    {.id = P::kCoreBodyEmphasisId, .read = Read::Leaf, .leaf = 3, .leaf2 = kNo},
    {.id = P::kCoreEdgeId, .read = Read::Leaf, .leaf = 4, .leaf2 = kNo},
    {.id = P::kCoreShiftId, .read = Read::Leaf, .leaf = 5, .leaf2 = kNo},
    {.id = P::kCoreExcitationMixId, .read = Read::Leaf, .leaf = 6, .leaf2 = kNo},
    {.id = P::kCoreExcitationDriveId, .read = Read::Leaf, .leaf = 7, .leaf2 = kNo},
    {.id = P::kCoreExcitationAsymmetryId, .read = Read::Leaf, .leaf = 8, .leaf2 = kNo},
    // 300-306 Anchor
    {.id = P::kAnchorWaveformId, .read = Read::Leaf, .leaf = 55, .leaf2 = kNo},
    {.id = P::kAnchorModeId, .read = Read::Leaf, .leaf = 56, .leaf2 = kNo},
    {.id = P::kAnchorLevelId, .read = Read::Leaf, .leaf = 57, .leaf2 = kNo},
    {.id = P::kAnchorPunchBoostId, .read = Read::Leaf, .leaf = 58, .leaf2 = kNo},
    {.id = P::kAnchorPunchTimeId, .read = Read::Leaf, .leaf = 59, .leaf2 = kNo},
    {.id = P::kAnchorGhostTimeId, .read = Read::Leaf, .leaf = 60, .leaf2 = kNo},
    {.id = P::kAnchorFoldFeedId, .read = Read::Leaf, .leaf = 61, .leaf2 = kNo},
    // 400-403 Drop
    {.id = P::kDropAmountId, .read = Read::Leaf, .leaf = 14, .leaf2 = kNo},
    {.id = P::kDropTimeId, .read = Read::Leaf, .leaf = 15, .leaf2 = kNo},
    {.id = P::kDropShapeId, .read = Read::Leaf, .leaf = 16, .leaf2 = kNo},
    {.id = P::kDropReboundId, .read = Read::Leaf, .leaf = 17, .leaf2 = kNo},
    // 420-426 Amp envelope
    {.id = P::kAmpAttackId, .read = Read::Leaf, .leaf = 18, .leaf2 = kNo},
    {.id = P::kAmpDecayId, .read = Read::Leaf, .leaf = 19, .leaf2 = kNo},
    {.id = P::kAmpSustainId, .read = Read::Leaf, .leaf = 20, .leaf2 = kNo},
    {.id = P::kAmpReleaseId, .read = Read::Leaf, .leaf = 21, .leaf2 = kNo},
    {.id = P::kAmpAttackCurveId, .read = Read::Leaf, .leaf = 22, .leaf2 = kNo},
    {.id = P::kAmpDecayCurveId, .read = Read::Leaf, .leaf = 23, .leaf2 = kNo},
    {.id = P::kAmpReleaseCurveId, .read = Read::Leaf, .leaf = 24, .leaf2 = kNo},
    // 500-501 Fold / Shape
    {.id = P::kFoldAmountId, .read = Read::Leaf, .leaf = 9, .leaf2 = kNo},
    {.id = P::kFoldShapeId, .read = Read::Leaf, .leaf = 10, .leaf2 = kNo},
    // 600-602 Character
    {.id = P::kCharacterId, .read = Read::Leaf, .leaf = 11, .leaf2 = kNo},
    {.id = P::kCharacterAmountId, .read = Read::Leaf, .leaf = 12, .leaf2 = kNo},
    {.id = P::kCharacterToneId, .read = Read::Leaf, .leaf = 13, .leaf2 = kNo},
    // 700-701 Mass dynamics
    {.id = P::kDynamicsTransientAttackId, .read = Read::Leaf, .leaf = 53, .leaf2 = kNo},
    {.id = P::kDynamicsTransientSustainId, .read = Read::Leaf, .leaf = 54, .leaf2 = kNo},
    // 800-809 Stereo field
    {.id = P::kFieldImageModeId, .read = Read::Leaf, .leaf = 25, .leaf2 = kNo},
    {.id = P::kFieldSpreadId, .read = Read::Leaf, .leaf = 26, .leaf2 = kNo},
    {.id = P::kFieldGravityId, .read = Read::Leaf, .leaf = 27, .leaf2 = kNo},
    {.id = P::kFieldCrossoverId, .read = Read::Leaf, .leaf = 28, .leaf2 = kNo},
    {.id = P::kFieldCurveId, .read = Read::Leaf, .leaf = 29, .leaf2 = kNo},
    {.id = P::kFieldMotionDepthId, .read = Read::Leaf, .leaf = 30, .leaf2 = kNo},
    {.id = P::kFieldMotionRateId, .read = Read::Leaf, .leaf = 31, .leaf2 = kNo},
    {.id = P::kFieldMotionSourceId, .read = Read::Leaf, .leaf = 32, .leaf2 = kNo},
    {.id = P::kFieldSyncId, .read = Read::Leaf, .leaf = 33, .leaf2 = kNo},
    {.id = P::kFieldSyncRateId, .read = Read::SyncPair, .leaf = 34, .leaf2 = 35},
    // 900-914 Motion
    {.id = P::kMotionSourceId, .read = Read::Leaf, .leaf = 36, .leaf2 = kNo},
    {.id = P::kMotionTargetId, .read = Read::Leaf, .leaf = 37, .leaf2 = kNo},
    {.id = P::kMotionAmountId, .read = Read::Leaf, .leaf = 38, .leaf2 = kNo},
    {.id = P::kMotionDepthId, .read = Read::Leaf, .leaf = 39, .leaf2 = kNo},
    {.id = P::kMotionCycleRateId, .read = Read::Leaf, .leaf = 40, .leaf2 = kNo},
    {.id = P::kMotionCycleShapeId, .read = Read::Leaf, .leaf = 41, .leaf2 = kNo},
    {.id = P::kMotionCycleSyncId, .read = Read::Leaf, .leaf = 42, .leaf2 = kNo},
    {.id = P::kMotionCycleSyncRateId, .read = Read::SyncPair, .leaf = 43, .leaf2 = 44},
    {.id = P::kMotionCyclePhaseId, .read = Read::Leaf, .leaf = 45, .leaf2 = kNo},
    {.id = P::kMotionCycleRetriggerId, .read = Read::Leaf, .leaf = 46, .leaf2 = kNo},
    {.id = P::kMotionCycleFadeInId, .read = Read::Leaf, .leaf = 47, .leaf2 = kNo},
    {.id = P::kMotionPolarityId, .read = Read::Leaf, .leaf = 48, .leaf2 = kNo},
    {.id = P::kMotionDriftSmoothnessId, .read = Read::Leaf, .leaf = 49, .leaf2 = kNo},
    {.id = P::kMotionEnvAttackId, .read = Read::Leaf, .leaf = 50, .leaf2 = kNo},
    {.id = P::kMotionEnvDecayId, .read = Read::Leaf, .leaf = 51, .leaf2 = kNo},
    // 410-417 note behaviour (non-morphed)
    {.id = P::kNoteMonoPriorityId, .read = Read::MonoPriority, .leaf = kNo, .leaf2 = kNo},
    {.id = P::kNoteLegatoId, .read = Read::Legato, .leaf = kNo, .leaf2 = kNo},
    {.id = P::kNotePortamentoId, .read = Read::Portamento, .leaf = kNo, .leaf2 = kNo},
    {.id = P::kNotePortamentoModeId, .read = Read::PortamentoMode, .leaf = kNo, .leaf2 = kNo},
    {.id = P::kNoteGlideModeId, .read = Read::GlideMode, .leaf = kNo, .leaf2 = kNo},
    {.id = P::kNoteMemoryId, .read = Read::NoteMemory, .leaf = kNo, .leaf2 = kNo},
    {.id = P::kNoteRetriggerId, .read = Read::Retrigger, .leaf = kNo, .leaf2 = kNo},
    {.id = P::kNotePitchBendRangeId, .read = Read::BendRange, .leaf = kNo, .leaf2 = kNo},
    // 105, 1002, 1000 morph control
    {.id = P::kMorphPositionId, .read = Read::Position, .leaf = kNo, .leaf2 = kNo},
    {.id = P::kMorphRampTimeId, .read = Read::RampMs, .leaf = kNo, .leaf2 = kNo},
    {.id = P::kMorphAnchorModeId, .read = Read::AnchorMode, .leaf = kNo, .leaf2 = kNo},
    // 4 performance
    {.id = P::kPitchBendId, .read = Read::PitchBend, .leaf = kNo, .leaf2 = kNo},
}};
// clang-format on

using Words = std::pair<std::uint32_t, std::uint32_t>;

[[nodiscard]] Words readBack(const ProfEngine& e, const Oracle& o) {
    switch (o.read) {
        case Read::Leaf:
            return {P::leafBitsOf(e.getEndpoint(Endpoint::A))[o.leaf], 0u};
        case Read::SyncPair: {
            const P::LeafBits b = P::leafBitsOf(e.getEndpoint(Endpoint::A));
            return {b[o.leaf], b[o.leaf2]};
        }
        case Read::MonoPriority:
            return {static_cast<std::uint32_t>(e.monoPriority()), 0u};
        case Read::Legato:
            return {e.legato() ? 1u : 0u, 0u};
        case Read::Portamento:
            return {bitsOf(e.portamentoTimeMs()), 0u};
        case Read::PortamentoMode:
            return {static_cast<std::uint32_t>(e.portamentoMode()), 0u};
        case Read::GlideMode:
            return {static_cast<std::uint32_t>(e.glideMode()), 0u};
        case Read::NoteMemory:
            return {static_cast<std::uint32_t>(e.noteMemoryMode()), 0u};
        case Read::Retrigger:
            return {static_cast<std::uint32_t>(e.noteRetrigger()), 0u};
        case Read::BendRange:
            return {bitsOf(e.pitchBendRange()), 0u};
        case Read::Position:
            return {bitsOf(e.morphPosition()), 0u};
        case Read::RampMs:
            return {bitsOf(e.morphRampMs()), 0u};
        case Read::AnchorMode:
            return {static_cast<std::uint32_t>(e.anchorMorphMode()), 0u};
        case Read::PitchBend:
            return {bitsOf(e.pitchBend()), 0u};
    }
    return {0xFFFFFFFFu, 0xFFFFFFFFu};
}

// norm 0 -> lo / first entry; norm 1 -> hi / last entry.
[[nodiscard]] Words expectedAt(const Oracle& o, bool hi) {
    switch (o.read) {
        case Read::Leaf: {
            const P::FieldInfo& f = P::lawOf(o.leaf);
            if (f.kind != P::MorphState::Kind::F32) {
                return {hi ? static_cast<std::uint32_t>(f.maxEnum) : 0u, 0u};
            }
            if (!hi) {
                return {bitsOf(f.lo), 0u};
            }
            // Cycle Phase is [0, 360): norm 1 reads the float just below 360.
            return {bitsOf(o.leaf == kCyclePhaseLeaf ? std::nextafter(360.0f, 0.0f) : f.hi), 0u};
        }
        case Read::SyncPair: {
            const auto& m = Krate::DSP::kNoteValueDropdownMapping[hi ? 29u : 0u];
            return {static_cast<std::uint32_t>(m.note), static_cast<std::uint32_t>(m.modifier)};
        }
        case Read::MonoPriority:
            return {static_cast<std::uint32_t>(hi ? Krate::DSP::MonoMode::HighNote : Krate::DSP::MonoMode::LastNote),
                    0u};
        case Read::Legato:
            return {hi ? 1u : 0u, 0u};
        case Read::Portamento:
            return {bitsOf(hi ? 10000.0f : 0.0f), 0u};
        case Read::PortamentoMode:
            return {static_cast<std::uint32_t>(hi ? Krate::DSP::PortaMode::LegatoOnly : Krate::DSP::PortaMode::Always),
                    0u};
        case Read::GlideMode:
            return {static_cast<std::uint32_t>(hi ? Krate::DSP::GlideMode::Quantized
                                                  : Krate::DSP::GlideMode::ConstantTime),
                    0u};
        case Read::NoteMemory:
            return {static_cast<std::uint32_t>(hi ? Krate::DSP::NoteMemory::Mode::OscillatorContinuous
                                                  : Krate::DSP::NoteMemory::Mode::AlwaysRetrigger),
                    0u};
        case Read::Retrigger:
            return {static_cast<std::uint32_t>(hi ? Krate::DSP::NoteMemory::Retrigger::Continue
                                                  : Krate::DSP::NoteMemory::Retrigger::Reset),
                    0u};
        case Read::BendRange:
            return {bitsOf(hi ? 24.0f : 0.0f), 0u};
        case Read::Position:
            return {bitsOf(hi ? 1.0f : 0.0f), 0u};
        case Read::RampMs:
            return {bitsOf(hi ? 2000.0f : 5.0f), 0u};
        case Read::AnchorMode:
            return {static_cast<std::uint32_t>(hi ? AnchorMorphMode::Punch : AnchorMorphMode::Lock), 0u};
        case Read::PitchBend:
            return {bitsOf(hi ? 1.0f : -1.0f), 0u};
    }
    return {0u, 0u};
}

[[nodiscard]] bool isEndpointRow(const Oracle& o) noexcept {
    return o.read == Read::Leaf || o.read == Read::SyncPair;
}

// Every leaf of A other than the row's own, and all 62 of B, are unchanged.
[[nodiscard]] bool noCrossTalk(const Oracle& o, const P::LeafBits& beforeA, const P::LeafBits& beforeB,
                               const P::LeafBits& afterA, const P::LeafBits& afterB) {
    bool ok = true;
    for (std::size_t i = 0; i < P::kNumLeaves; ++i) {
        const bool own = i == o.leaf || (o.leaf2 != kNo && i == o.leaf2);
        if (!own && beforeA[i] != afterA[i]) {
            UNSCOPED_INFO("cross-talk into A leaf " << i);
            ok = false;
        }
        if (beforeB[i] != afterB[i]) {
            UNSCOPED_INFO("cross-talk into B leaf " << i);
            ok = false;
        }
    }
    return ok;
}

// Heap Controller, initialised with a null context, terminated on scope exit.
struct WiringController {
    Steinberg::IPtr<P::Controller> ctl = Steinberg::owned(new P::Controller());

    WiringController() { REQUIRE(ctl->initialize(nullptr) == Steinberg::kResultOk); }
    ~WiringController() { ctl->terminate(); }
    WiringController(const WiringController&) = delete;
    WiringController& operator=(const WiringController&) = delete;
    WiringController(WiringController&&) = delete;
    WiringController& operator=(WiringController&&) = delete;
};

// -----------------------------------------------------------------------------
// SC-009 parts 4-6 (T016): the normalised value an E row displays for a mirror
// state. Sync Rate pairs read syncRateIndexOf / 29 (FR-012); every other row
// normalizedFromLeafWord of its leaf.
// -----------------------------------------------------------------------------
[[nodiscard]] double mirrorNormalized(const Krate::DSP::MorphState& s, const P::SoundParamRow& row) {
    const P::LeafBits bits = P::leafBitsOf(s);
    if (row.leaf2 != kNo) {
        const int idx = P::syncRateIndexOf(bits[row.leaf], bits[row.leaf2]);
        REQUIRE(idx >= 0);
        return static_cast<double>(idx) / static_cast<double>(Krate::DSP::kNoteValueDropdownCount - 1);
    }
    return P::normalizedFromLeafWord(row.leaf, bits[row.leaf]);
}

// The number of the 60 E rows whose displayed value differs from the mirror value by more than 1e-9.
[[nodiscard]] int displayMismatches(P::Controller& ctl, const Krate::DSP::MorphState& s) {
    int bad = 0;
    for (const P::SoundParamRow& row : P::kSoundRows) {
        const double want = mirrorNormalized(s, row);
        const double got = ctl.getParamNormalized(row.id);
        if (std::fabs(got - want) > 1e-9) {
            UNSCOPED_INFO("E row " << row.id << ": displayed " << got << ", mirror " << want);
            ++bad;
        }
    }
    return bad;
}

// The number of the 60 E rows whose mirror values differ between @p a and @p b by more than 1e-9 (non-vacuity).
[[nodiscard]] int rowsThatDiffer(const Krate::DSP::MorphState& a, const Krate::DSP::MorphState& b) {
    int n = 0;
    for (const P::SoundParamRow& row : P::kSoundRows) {
        if (std::fabs(mirrorNormalized(a, row) - mirrorNormalized(b, row)) > 1e-9) {
            ++n;
        }
    }
    return n;
}

// begin + perform + end calls recorded for the 60 E rows.
[[nodiscard]] int eRowEditCalls(const RecordingComponentHandler& h) {
    int n = 0;
    for (const P::SoundParamRow& row : P::kSoundRows) {
        n += h.editCallsFor(row.id);
    }
    return n;
}

[[nodiscard]] int totalPerformEdits(const RecordingComponentHandler& h) {
    int n = 0;
    for (const auto& entry : h.performEdits) {
        n += entry.second;
    }
    return n;
}

}  // namespace

// =============================================================================
// SC-007 / FR-003
// =============================================================================
TEST_CASE("Profundum_DefaultsMatchEngine", "[profundum][params]") {
    auto fx = makePrepared();
    fx->ensureCapture(kBlock);
    REQUIRE(fx->processBlock(kBlock) == Steinberg::kResultOk);

    const auto checkDefaults = [&fx]() {
        const ProfEngine& e = *fx->engine();
        CHECK(leafBitsEqual(e.getEndpoint(Endpoint::A), Krate::DSP::MorphState{}));
        CHECK(leafBitsEqual(e.getEndpoint(Endpoint::B), Krate::DSP::MorphState{}));
        CHECK(e.monoPriority() == Krate::DSP::MonoMode::LastNote);
        CHECK(e.legato() == false);
        CHECK(bitsOf(e.portamentoTimeMs()) == bitsOf(0.0f));
        CHECK(e.portamentoMode() == Krate::DSP::PortaMode::Always);
        CHECK(e.glideMode() == Krate::DSP::GlideMode::ConstantTime);
        CHECK(e.noteMemoryMode() == Krate::DSP::NoteMemory::Mode::AlwaysRetrigger);
        CHECK(e.noteRetrigger() == Krate::DSP::NoteMemory::Retrigger::Reset);
        CHECK(bitsOf(e.pitchBendRange()) == bitsOf(2.0f));
        CHECK(bitsOf(e.pitchBend()) == bitsOf(0.0f));
        CHECK(bitsOf(e.morphPosition()) == bitsOf(0.0f));
        CHECK(bitsOf(e.morphRampMs()) == bitsOf(40.0f));
        CHECK(e.anchorMorphMode() == AnchorMorphMode::Blend);
        CHECK(fx->proc->endpointRejectionsForTest() == 0u);
    };

    checkDefaults();

    SECTION("HostResendsDefaults") {
        // FR-003: every registered parameter's default, read from a controller, in ONE block.
        std::vector<std::pair<ParamID, double>> defaults;
        {
            const WiringController h;
            const Steinberg::int32 count = h.ctl->getParameterCount();
            REQUIRE(count == 93);
            for (Steinberg::int32 i = 0; i < count; ++i) {
                Steinberg::Vst::ParameterInfo info{};
                REQUIRE(h.ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
                defaults.emplace_back(info.id, info.defaultNormalizedValue);
            }
        }
        MultiParamChanges changes;
        changes.reserve(defaults.size());
        for (const auto& d : defaults) {
            changes.addQueue(d.first).addTestPoint(0, d.second);
        }
        fx->ensureCapture(2 * kBlock);
        REQUIRE(fx->processBlock(kBlock, nullptr, &changes) == Steinberg::kResultOk);
        REQUIRE(fx->processBlock(kBlock) == Steinberg::kResultOk);

        checkDefaults();
    }
}

// =============================================================================
// SC-008 / FR-010 - FR-014
// =============================================================================
TEST_CASE("Profundum_EveryParamReachesEngine", "[profundum][params]") {
    auto fx = makePrepared();

    int passes = 0;
    for (const Oracle& o : kOracle) {
        INFO("ID " << o.id);
        const P::LeafBits beforeA = engineBits(*fx, Endpoint::A);
        const P::LeafBits beforeB = engineBits(*fx, Endpoint::B);

        REQUIRE(fx->setParam(o.id, 0.0) == Steinberg::kResultOk);
        const Words lo = readBack(*fx->engine(), o);
        const Words wantLo = expectedAt(o, false);
        CHECK(lo.first == wantLo.first);
        CHECK(lo.second == wantLo.second);
        bool ok = lo == wantLo;
        if (isEndpointRow(o)) {
            const bool clean = noCrossTalk(o, beforeA, beforeB, engineBits(*fx, Endpoint::A),
                                           engineBits(*fx, Endpoint::B));
            CHECK(clean);
            ok = ok && clean;
        }

        REQUIRE(fx->setParam(o.id, 1.0) == Steinberg::kResultOk);
        const Words hi = readBack(*fx->engine(), o);
        const Words wantHi = expectedAt(o, true);
        CHECK(hi.first == wantHi.first);
        CHECK(hi.second == wantHi.second);
        ok = ok && hi == wantHi;
        if (isEndpointRow(o)) {
            const bool clean = noCrossTalk(o, beforeA, beforeB, engineBits(*fx, Endpoint::A),
                                           engineBits(*fx, Endpoint::B));
            CHECK(clean);
            ok = ok && clean;
        }

        if (ok) {
            ++passes;
        }
    }
    REQUIRE(passes == 72);
    CHECK(fx->proc->endpointRejectionsForTest() == 0u);

    SECTION("EditTargetRoutes") {
        // 1001 -> B, then a Depth edit lands in B and A is unchanged.
        REQUIRE(fx->setParam(P::kMorphEditTargetId, 1.0) == Steinberg::kResultOk);
        const P::LeafBits beforeA = engineBits(*fx, Endpoint::A);
        const P::LeafBits beforeB = engineBits(*fx, Endpoint::B);
        constexpr double kDepthNorm = 0.37;
        const std::uint32_t want = P::leafWordFromNormalized(kDepthLeaf, kDepthNorm);
        REQUIRE(want != beforeB[kDepthLeaf]);  // non-vacuity: the edit changes B's word

        REQUIRE(fx->setParam(P::kCoreDepthId, kDepthNorm) == Steinberg::kResultOk);
        const P::LeafBits afterA = engineBits(*fx, Endpoint::A);
        const P::LeafBits afterB = engineBits(*fx, Endpoint::B);
        bool ok = afterB[kDepthLeaf] == want && afterA == beforeA;
        for (std::size_t i = 0; i < P::kNumLeaves; ++i) {
            if (i != kDepthLeaf && afterB[i] != beforeB[i]) {
                ok = false;
            }
        }
        CHECK(afterB[kDepthLeaf] == want);
        CHECK(afterA == beforeA);
        if (ok) {
            ++passes;
        }
        REQUIRE(passes == 73);
    }

    SECTION("SyncRateAll30") {
        struct SyncRow {
            ParamID id;
            std::uint8_t noteLeaf;
            std::uint8_t modLeaf;
        };
        constexpr std::array<SyncRow, 2> kSyncRows{{
            {.id = P::kFieldSyncRateId, .noteLeaf = 34, .modLeaf = 35},
            {.id = P::kMotionCycleSyncRateId, .noteLeaf = 43, .modLeaf = 44},
        }};
        for (const SyncRow& row : kSyncRows) {
            for (int idx = 0; idx < Krate::DSP::kNoteValueDropdownCount; ++idx) {
                INFO("ID " << row.id << " index " << idx);
                REQUIRE(fx->setParam(row.id, static_cast<double>(idx) / 29.0) == Steinberg::kResultOk);
                const P::LeafBits a = engineBits(*fx, Endpoint::A);
                const auto& m = Krate::DSP::kNoteValueDropdownMapping[static_cast<std::size_t>(idx)];
                CHECK(a[row.noteLeaf] == static_cast<std::uint32_t>(m.note));
                CHECK(a[row.modLeaf] == static_cast<std::uint32_t>(m.modifier));
                CHECK(P::syncRateIndexOf(a[row.noteLeaf], a[row.modLeaf]) == idx);
            }
        }
    }
}

// =============================================================================
// SC-009 parts 1-3 (FR-011): Edit Target addressing on the processor;
// parts 4-6 (FR-021, FR-022, FR-024, FR-025): the controller mirror and re-display
// =============================================================================
TEST_CASE("Profundum_EditTargetAddressing", "[profundum][params]") {
    constexpr double kDepthNorm = 0.9;
    const std::uint32_t mappedDepth = P::leafWordFromNormalized(kDepthLeaf, kDepthNorm);
    REQUIRE(mappedDepth != P::kDefaultLeafBits[kDepthLeaf]);  // non-vacuity

    SECTION("DepthEditTargetA") {
        // (1) target A (the default), Depth 0.9 -> A's depth is the mapped 0.9, B unchanged.
        auto fx = makePrepared();
        REQUIRE(fx->setParam(P::kCoreDepthId, kDepthNorm) == Steinberg::kResultOk);
        CHECK(engineBits(*fx, Endpoint::A)[kDepthLeaf] == mappedDepth);
        CHECK(bitsOf(fx->engine()->getEndpoint(Endpoint::A).recipe.depth) == mappedDepth);
        CHECK(leafBitsEqual(fx->engine()->getEndpoint(Endpoint::B), Krate::DSP::MorphState{}));
    }

    SECTION("SwitchAloneChangesNothing") {
        // (2) two processors, the same 1 s stream (E1 note); one switches 1001 -> 1 at
        // block 10 with no other edit: every leaf and every sample identical.
        constexpr std::size_t kTotal = 48000;
        const std::vector<Scripted> base{
            Scripted{.sample = 0, .kind = Scripted::Kind::NoteOn, .pitch = kE1, .velocity = 100.0f / 127.0f}};
        std::vector<Scripted> switched = base;
        switched.push_back(Scripted{.sample = static_cast<Steinberg::int64>(10 * kBlock),
                                    .kind = Scripted::Kind::Param,
                                    .id = P::kMorphEditTargetId,
                                    .value = 1.0});

        auto ref = makePrepared();
        auto sw = makePrepared();
        ref->renderScript(base, kTotal, kBlock);
        sw->renderScript(switched, kTotal, kBlock);

        REQUIRE(ref->capturedL.size() == kTotal);
        REQUIRE(sw->capturedL.size() == kTotal);
        REQUIRE(peakOf(ref->capturedL) > 0.0f);  // non-vacuity: the stream sounds

        CHECK(leafBitsEqual(ref->engine()->getEndpoint(Endpoint::A), sw->engine()->getEndpoint(Endpoint::A)));
        CHECK(leafBitsEqual(ref->engine()->getEndpoint(Endpoint::B), sw->engine()->getEndpoint(Endpoint::B)));
        const auto bitsEqual = [](float x, float y) { return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y); };
        CHECK(std::ranges::equal(ref->capturedL, sw->capturedL, bitsEqual));
        CHECK(std::ranges::equal(ref->capturedR, sw->capturedR, bitsEqual));

        // The switch did take effect: a Depth edit now lands in B.
        REQUIRE(sw->setParam(P::kCoreDepthId, kDepthNorm) == Steinberg::kResultOk);
        CHECK(engineBits(*sw, Endpoint::B)[kDepthLeaf] == mappedDepth);
        CHECK(leafBitsEqual(sw->engine()->getEndpoint(Endpoint::A), Krate::DSP::MorphState{}));
    }

    SECTION("EditTargetFirst") {
        // (3) one block, queues [200 = 0.9, 1001 = 1] in that order: B's depth is the
        // mapped 0.9, A unchanged (FR-011: Edit Target first).
        auto fx = makePrepared();
        REQUIRE(fx->setParamsInBlock({{P::kCoreDepthId, kDepthNorm}, {P::kMorphEditTargetId, 1.0}}) ==
                Steinberg::kResultOk);
        CHECK(engineBits(*fx, Endpoint::B)[kDepthLeaf] == mappedDepth);
        CHECK(leafBitsEqual(fx->engine()->getEndpoint(Endpoint::A), Krate::DSP::MorphState{}));
    }

    // ---- SC-009 parts 4-6 (T016; FR-021, FR-022, FR-024, FR-025 controller side) ----
    using Krate::DSP::MorphState;
    using Steinberg::Vst::kParamValuesChanged;

    SECTION("ControllerRedisplay") {
        // (4) the B mirror replaced from an endpoint message, then 1001 -> B: all 60 E values equal B's.
        ControllerFixture cf;
        const MorphState b = randomValidMorphState(0x9B);
        REQUIRE(rowsThatDiffer(b, MorphState{}) > 0);  // non-vacuity: B is not the default display

        REQUIRE(cf.controller->notify(makeEndpointMessage(Endpoint::B, b)) == Steinberg::kResultOk);
        CHECK(leafBitsEqual(cf.controller->mirrorForTest(Endpoint::B), b));
        CHECK(leafBitsEqual(cf.controller->mirrorForTest(Endpoint::A), MorphState{}));
        // Target is still A: the B message does not touch the display.
        CHECK(displayMismatches(*cf.controller.get(), MorphState{}) == 0);

        REQUIRE(cf.controller->setParamNormalized(P::kMorphEditTargetId, 1.0) == Steinberg::kResultTrue);
        CHECK(displayMismatches(*cf.controller.get(), b) == 0);
    }

    SECTION("NoHostEdits") {
        // (5) the re-display issues no beginEdit / performEdit / endEdit for the 60 E rows.
        ControllerFixture cf;
        const MorphState b = randomValidMorphState(0x9B);
        REQUIRE(cf.controller->notify(makeEndpointMessage(Endpoint::B, b)) == Steinberg::kResultOk);
        REQUIRE(cf.controller->setParamNormalized(P::kMorphEditTargetId, 1.0) == Steinberg::kResultTrue);
        REQUIRE(displayMismatches(*cf.controller.get(), b) == 0);  // the re-display happened
        CHECK(eRowEditCalls(cf.handler) == 0);
        CHECK(totalPerformEdits(cf.handler) == 0);
    }

    SECTION("OneRestartAfterRedisplay") {
        // (6) exactly one restartComponent(kParamValuesChanged) per switch, issued AFTER the re-display.
        ControllerFixture cf;
        const MorphState a = randomValidMorphState(0x9A);
        const MorphState b = randomValidMorphState(0x9B);
        REQUIRE(rowsThatDiffer(a, b) > 0);  // non-vacuity: the two mirrors display differently

        REQUIRE(cf.controller->notify(makeEndpointMessage(Endpoint::A, a)) == Steinberg::kResultOk);
        REQUIRE(cf.controller->notify(makeEndpointMessage(Endpoint::B, b)) == Steinberg::kResultOk);
        REQUIRE(cf.handler.restartFlags.empty());  // an endpoint message never restarts

        const MorphState* expected = nullptr;
        std::vector<int> mismatchesAtRestart;
        P::Controller& ctl = *cf.controller.get();
        cf.handler.onRestart = [&](Steinberg::int32 /*flags*/) {
            REQUIRE(expected != nullptr);
            mismatchesAtRestart.push_back(displayMismatches(ctl, *expected));
        };

        const int redisplays0 = cf.controller->redisplayCountForTest();

        // A -> B
        expected = &b;
        REQUIRE(cf.controller->setParamNormalized(P::kMorphEditTargetId, 1.0) == Steinberg::kResultTrue);
        REQUIRE(cf.handler.restartFlags.size() == 1u);
        CHECK(cf.handler.restartFlags[0] == kParamValuesChanged);
        CHECK(cf.controller->redisplayCountForTest() == redisplays0 + 1);

        // Same index again: not a switch, no re-display, no restart.
        REQUIRE(cf.controller->setParamNormalized(P::kMorphEditTargetId, 1.0) == Steinberg::kResultTrue);
        CHECK(cf.handler.restartFlags.size() == 1u);
        CHECK(cf.controller->redisplayCountForTest() == redisplays0 + 1);

        // B -> A
        expected = &a;
        REQUIRE(cf.controller->setParamNormalized(P::kMorphEditTargetId, 0.0) == Steinberg::kResultTrue);
        REQUIRE(cf.handler.restartFlags.size() == 2u);
        CHECK(cf.handler.restartFlags[1] == kParamValuesChanged);
        CHECK(cf.controller->redisplayCountForTest() == redisplays0 + 2);

        // At the moment of each restart the display already showed the newly selected mirror.
        REQUIRE(mismatchesAtRestart.size() == 2u);
        CHECK(mismatchesAtRestart[0] == 0);
        CHECK(mismatchesAtRestart[1] == 0);
        CHECK(displayMismatches(*cf.controller.get(), a) == 0);
        CHECK(totalPerformEdits(cf.handler) == 0);
        CHECK(eRowEditCalls(cf.handler) == 0);
    }

    SECTION("RejectedBlobKeepsMirror") {
        ControllerFixture cf;
        const MorphState b = randomValidMorphState(0x9B);
        REQUIRE(cf.controller->notify(makeEndpointMessage(Endpoint::B, b)) == Steinberg::kResultOk);
        REQUIRE(leafBitsEqual(cf.controller->mirrorForTest(Endpoint::B), b));

        // A blob whose version byte is flipped is rejected: the B mirror keeps its prior value.
        auto corrupt = serializedMorphState(randomValidMorphState(0x9C));
        corrupt[0] = static_cast<std::byte>(std::to_integer<std::uint8_t>(corrupt[0]) ^ 0xFFu);
        CHECK(cf.controller->notify(makeEndpointMessage(Endpoint::B, std::span<const std::byte>(corrupt))) ==
              Steinberg::kResultFalse);
        CHECK(leafBitsEqual(cf.controller->mirrorForTest(Endpoint::B), b));

        // A message for the current Edit Target (A) re-displays, with no restart call and no host edit.
        const MorphState a = randomValidMorphState(0x9A);
        REQUIRE(rowsThatDiffer(a, MorphState{}) > 0);  // non-vacuity: the values do change
        REQUIRE(displayMismatches(*cf.controller.get(), MorphState{}) == 0);
        const int redisplays0 = cf.controller->redisplayCountForTest();
        REQUIRE(cf.controller->notify(makeEndpointMessage(Endpoint::A, a)) == Steinberg::kResultOk);
        CHECK(leafBitsEqual(cf.controller->mirrorForTest(Endpoint::A), a));
        CHECK(displayMismatches(*cf.controller.get(), a) == 0);
        CHECK(cf.controller->redisplayCountForTest() == redisplays0 + 1);
        CHECK(cf.handler.restartFlags.empty());
        CHECK(totalPerformEdits(cf.handler) == 0);
    }
}
