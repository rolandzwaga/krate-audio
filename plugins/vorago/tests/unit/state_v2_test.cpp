// ==============================================================================
// Vorago Phase 12 - state v2 round trip (SC-008)
// ==============================================================================
// T042 (specs/vorago-phase12-parameters/tasks.md; plan 4.9, 6.3; spec C-7,
// FR-040, FR-045). Vorago_StateRoundTripV2:
//   (1) all 106 persisted IDs at seeded (std::mt19937{12008}) non-default values
//       -> getState (kStateV2Bytes) -> fresh setState -> every atomic bit-identical;
//       controller setComponentState normalized within 1e-9; 4 s held-note render
//       of both processors within 1e-5;
//   (2) a hand-built 60-byte v1 stream restores the 14 v1 fields and leaves the
//       92 other persisted fields at their registered defaults - in a fresh AND a
//       pre-dirtied processor / controller (C-7);
//   (3) truncation at every length 0..427 into a fresh and a pre-dirtied instance;
//   (4) version kCurrentStateVersion + 1 -> kResultFalse, no atomic changed;
//   (5) a NaN bit pattern in each float field in turn -> that field unchanged;
//   (6) sustain / channel pressure are never serialized and are zeroed by setState.
//
// Built with -fno-fast-math (tests/CMakeLists.txt). The NaN payload is written as
// raw little-endian bytes, so no NaN float value is ever materialized here.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "parameters/param_mapping.h"
#include "plugin_ids.h"
#include "unit/param_table_expected.h"
#include "vorago_test_fixture.h"

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace {

using Steinberg::Vst::ParamID;
using VoragoTest::ExpectedParamRow;
using VoragoTest::kExpectedParams;

constexpr std::size_t kNumIds = VoragoTest::kNumExpectedParams;  // 108
constexpr std::size_t kNumPersisted = 106;                       // all but 4 and 5

[[nodiscard]] bool isPersisted(ParamID id) noexcept {
    return id != ::Vorago::kSustainPedalId && id != ::Vorago::kChannelPressureId;
}

[[nodiscard]] bool isInRange(ParamID id, ParamID first, ParamID last) noexcept {
    return id >= first && id <= last;
}

// ------------------------------------------------------------------------------
// Atomic access by ID (the field names of every pack header; the same mapping as
// storedPlain() in integration/param_surface_test.cpp)
// ------------------------------------------------------------------------------

[[nodiscard]] const std::atomic<float>* floatAtomicOf(const ::Vorago::Processor::PacksForTest& p,
                                                      ParamID id) noexcept {
    namespace Vg = ::Vorago;
    if (isInRange(id, Vg::kMacroDarknessId, Vg::kMacroMassId)) {
        return &Vg::macroField(p.macro, static_cast<int>(id - Vg::kMacroDarknessId));
    }
    if (isInRange(id, Vg::kNoiseSlot0CombFundamentalId, Vg::kNoiseSlot3CombFundamentalId)) {
        return &p.noise.combFundamentalHz[id - Vg::kNoiseSlot0CombFundamentalId];
    }
    if (isInRange(id, Vg::kNoiseSlot0CombSpreadId, Vg::kNoiseSlot3CombSpreadId)) {
        return &p.noise.combSpread[id - Vg::kNoiseSlot0CombSpreadId];
    }
    if (isInRange(id, Vg::kNoiseSlot0CombFeedbackId, Vg::kNoiseSlot3CombFeedbackId)) {
        return &p.noise.combFeedback[id - Vg::kNoiseSlot0CombFeedbackId];
    }
    switch (id) {
        case Vg::kMasterGainId: return &p.global.masterGain;
        case Vg::kOutputSaturationId: return &p.global.outputSaturation;
        case Vg::kSustainPedalId: return &p.global.sustainPedal;
        case Vg::kChannelPressureId: return &p.global.channelPressure;
        case Vg::kCloudRichnessId: return &p.cloud.richness;
        case Vg::kCloudTiltId: return &p.cloud.tiltDb;
        case Vg::kCloudMutationId: return &p.cloud.mutation;
        case Vg::kCloudInharmonicityId: return &p.cloud.inharmonicity;
        case Vg::kCloudDriftDepthId: return &p.cloud.driftCents;
        case Vg::kCloudStereoSpreadId: return &p.cloud.stereoSpread;
        case Vg::kCloudSpectralGravityId: return &p.cloud.spectralGravity;
        case Vg::kNoiseLevelId: return &p.noise.levelDb;
        case Vg::kNoiseWakeId: return &p.noise.wake;
        case Vg::kNoiseWanderRateId: return &p.noise.wanderRateHz;
        case Vg::kResonanceGravityId: return &p.resonance.gravity;
        case Vg::kResonanceMixId: return &p.resonance.mix;
        case Vg::kResonanceWanderRateId: return &p.resonance.wanderRateHz;
        case Vg::kEcologyMixId: return &p.ecology.mix;
        case Vg::kEcologyLoopGainId: return &p.ecology.loopGain;
        case Vg::kSubLevelOffsetId: return &p.sub.levelOffsetDb;
        case Vg::kSubTrackingId: return &p.sub.tracking;
        case Vg::kSubDiv2LevelId: return &p.sub.div2LevelDb;
        case Vg::kSubDiv4LevelId: return &p.sub.div4LevelDb;
        case Vg::kSubFifthBelowLevelId: return &p.sub.fifthBelowLevelDb;
        case Vg::kSmearAmountId: return &p.smear.amount;
        case Vg::kSmearDecoherenceId: return &p.smear.decoherence;
        case Vg::kSmearTiltId: return &p.smear.tilt;
        case Vg::kEventsRateScaleId: return &p.events.eventRateScale;
        case Vg::kEcosystemDepthId: return &p.ecosystem.depth;
        case Vg::kBodyBlendId: return &p.body.blend;
        case Vg::kBodyDampingId: return &p.body.damping;
        case Vg::kBodyResonanceId: return &p.body.resonance;
        case Vg::kBodyMixId: return &p.body.mix;
        case Vg::kSpaceSizeId: return &p.space.size;
        case Vg::kSpaceDarknessId: return &p.space.darkness;
        case Vg::kSpaceDecayId: return &p.space.decaySeconds;
        case Vg::kSpaceFogId: return &p.space.fog;
        case Vg::kSpaceDamperDepthId: return &p.space.damperDepth;
        case Vg::kSpaceMixId: return &p.space.mix;
        case Vg::kSpaceWidthId: return &p.space.width;
        case Vg::kSpaceDensityId: return &p.space.density;
        case Vg::kSpaceDimensionalityId: return &p.space.dimensionality;
        case Vg::kSpaceBreathId: return &p.space.breath;
        case Vg::kSpaceEarlySizeId: return &p.space.earlySizeMs;
        case Vg::kSpaceEarlyLevelId: return &p.space.earlyLevel;
        case Vg::kSpaceEarlyAbsorptionId: return &p.space.earlyAbsorption;
        case Vg::kSpaceEarlySendId: return &p.space.earlySend;
        case Vg::kSpaceDamperRateId: return &p.space.damperRate;
        case Vg::kEnvelopeStage0TimeId: return &p.envelope.stage0TimeMs;
        case Vg::kEnvelopeStage1TimeId: return &p.envelope.stage1TimeMs;
        case Vg::kEnvelopeStage2TimeId: return &p.envelope.stage2TimeMs;
        case Vg::kEnvelopeStage3TimeId: return &p.envelope.stage3TimeMs;
        case Vg::kEnvelopeReleaseId: return &p.envelope.releaseMs;
        case Vg::kEnvelopeGrowthDurationId: return &p.envelope.growthDurationSeconds;
        case Vg::kBloomDepthId: return &p.bloom.depth;
        case Vg::kBloomSpawnRateId: return &p.bloom.spawnRateHz;
        case Vg::kGhostPeakLevelId: return &p.ghost.peakLevel;
        case Vg::kGhostBlurId: return &p.ghost.blur;
        case Vg::kGhostReverseProbabilityId: return &p.ghost.reverseProbability;
        case Vg::kLifeBreathingDepthId: return &p.life.breathingDepth;
        case Vg::kLifeBreathingIrregularityId: return &p.life.breathingIrregularity;
        case Vg::kLifeTidalDepthId: return &p.life.tidalDepth;
        default: return nullptr;
    }
}

[[nodiscard]] const std::atomic<int>* intAtomicOf(const ::Vorago::Processor::PacksForTest& p,
                                                  ParamID id) noexcept {
    namespace Vg = ::Vorago;
    if (isInRange(id, Vg::kNoiseSlot0ModelId, Vg::kNoiseSlot3ModelId)) {
        return &p.noise.model[id - Vg::kNoiseSlot0ModelId];
    }
    if (isInRange(id, Vg::kNoiseSlot0TypeId, Vg::kNoiseSlot3TypeId)) {
        return &p.noise.type[id - Vg::kNoiseSlot0TypeId];
    }
    if (isInRange(id, Vg::kEcologyLoop0FilterModeId, Vg::kEcologyLoop5FilterModeId)) {
        return &p.ecology.loopFilterMode[id - Vg::kEcologyLoop0FilterModeId];
    }
    switch (id) {
        case Vg::kPolyphonyId: return &p.global.polyphony;
        case Vg::kSeedId: return &p.global.seedIndex;
        case Vg::kResonanceAnchorModeId: return &p.resonance.anchorMode;
        case Vg::kBodyMaterialAId: return &p.body.materialA;
        case Vg::kBodyMaterialBId: return &p.body.materialB;
        case Vg::kSpaceFreezeId: return &p.space.freeze;
        case Vg::kEnvelopeModeId: return &p.envelope.mode;
        case Vg::kGhostEventTriggersId: return &p.ghost.eventTriggers;
        default: return nullptr;
    }
}

/// The atomic behind `id` as raw bits: a float's bit pattern, an int's value.
[[nodiscard]] std::uint32_t atomicBits(const ::Vorago::Processor& proc, ParamID id) {
    const ::Vorago::Processor::PacksForTest p = proc.packsForTest();
    if (const std::atomic<float>* f = floatAtomicOf(p, id)) {
        return std::bit_cast<std::uint32_t>(f->load());
    }
    if (const std::atomic<int>* i = intAtomicOf(p, id)) {
        return static_cast<std::uint32_t>(i->load());
    }
    FAIL("no atomic for ID " << id);
    return 0u;
}

/// Every one of the 108 atomics, in kExpectedParams order.
using Snapshot = std::array<std::uint32_t, kNumIds>;

[[nodiscard]] Snapshot snapshotAll(const ::Vorago::Processor& proc) {
    Snapshot s{};
    for (std::size_t k = 0; k < kNumIds; ++k) {
        s[k] = atomicBits(proc, kExpectedParams[k].id);
    }
    return s;
}

[[nodiscard]] std::size_t rowIndexOf(ParamID id) {
    for (std::size_t k = 0; k < kNumIds; ++k) {
        if (kExpectedParams[k].id == id) {
            return k;
        }
    }
    FAIL("no expected row for ID " << id);
    return 0u;
}

// ------------------------------------------------------------------------------
// The v2 stream layout (spec C-7, plan 4.9), built from the checked-in table, not
// from the pack headers: version, [v1] gain, polyphony, 12 macros, [v2] seed,
// saturation, then every ID >= 200 ascending. Discrete rows are int32.
// ------------------------------------------------------------------------------

struct Field {
    ParamID id;
    bool isInt;
    std::size_t offset;  // byte offset in the stream (after the 4-byte version)
};

[[nodiscard]] std::vector<Field> streamLayout() {
    namespace Vg = ::Vorago;
    std::vector<ParamID> order = {Vg::kMasterGainId, Vg::kPolyphonyId};
    for (ParamID id = Vg::kMacroDarknessId; id <= Vg::kMacroMassId; ++id) {
        order.push_back(id);
    }
    order.push_back(Vg::kSeedId);
    order.push_back(Vg::kOutputSaturationId);
    for (const ExpectedParamRow& row : kExpectedParams) {
        if (row.id >= Vg::kMacroParamRangeEnd) {
            order.push_back(row.id);
        }
    }
    std::vector<Field> out;
    std::size_t offset = 4;
    for (const ParamID id : order) {
        const ExpectedParamRow& row = kExpectedParams[rowIndexOf(id)];
        out.push_back(Field{.id = id, .isInt = row.taper == Vg::Taper::Discrete, .offset = offset});
        offset += 4;
    }
    return out;
}

// ------------------------------------------------------------------------------
// Streams
// ------------------------------------------------------------------------------

[[nodiscard]] std::vector<char> stateBytes(::Vorago::Processor& proc) {
    auto s = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(proc.getState(s) == Steinberg::kResultOk);
    const auto size = static_cast<std::size_t>(s->getSize());
    const char* data = s->getData();
    return {data, data + size};
}

[[nodiscard]] Steinberg::IPtr<Steinberg::MemoryStream> streamOf(const std::vector<char>& bytes,
                                                                std::size_t count) {
    REQUIRE(count <= bytes.size());
    auto s = Steinberg::owned(new Steinberg::MemoryStream());
    if (count > 0) {
        Steinberg::int32 written = 0;
        REQUIRE(s->write(const_cast<char*>(bytes.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                         static_cast<Steinberg::int32>(count), &written) == Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(written, count));
    }
    REQUIRE(s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
    return s;
}

void putLeWord(std::vector<char>& bytes, std::size_t offset, std::uint32_t w) {
    REQUIRE(offset + 4u <= bytes.size());
    for (std::size_t b = 0; b < 4; ++b) {
        bytes[offset + b] = static_cast<char>((w >> (8u * b)) & 0xFFu);
    }
}

// ------------------------------------------------------------------------------
// Seeded values
// ------------------------------------------------------------------------------

using NormalizedSet = std::vector<std::pair<ParamID, double>>;

/// Portable uniform [0, 1] from mt19937 (no distribution: its output is
/// library-specific).
[[nodiscard]] double draw(std::mt19937& rng) {
    return static_cast<double>(rng()) / 4294967295.0;
}

void applyNormalized(VoragoTest::ProcessorFixture& fx, const NormalizedSet& set) {
    Krate::Test::ParameterChanges pc;
    for (const auto& [id, n] : set) {
        pc.addChange(id, n);
    }
    REQUIRE(fx.processNoOutputs(&pc) == Steinberg::kResultOk);
}

/// Drives every persisted ID of `fx` to a seeded normalized value whose stored
/// atomic differs from `avoid`'s (redrawn up to 64 times). Returns the final
/// normalized set so other instances can be put into the same state without
/// setState.
[[nodiscard]] NormalizedSet seedPersisted(VoragoTest::ProcessorFixture& fx, std::uint32_t seed,
                                          const ::Vorago::Processor& avoid) {
    std::mt19937 rng{seed};
    NormalizedSet out;
    for (const ExpectedParamRow& row : kExpectedParams) {
        if (!isPersisted(row.id)) {
            continue;
        }
        bool moved = false;
        double n = 0.0;
        for (int attempt = 0; attempt < 64 && !moved; ++attempt) {
            n = draw(rng);
            applyNormalized(fx, NormalizedSet{{row.id, n}});
            moved = atomicBits(*fx.proc, row.id) != atomicBits(avoid, row.id);
        }
        INFO("ID " << row.id);
        REQUIRE(moved);
        out.emplace_back(row.id, n);
    }
    REQUIRE(out.size() == kNumPersisted);
    return out;
}

/// The normalized value the controller must hold for `row`, from the processor's
/// stored plain value through the checked-in table's taper inverse.
[[nodiscard]] double expectedNormalized(const ExpectedParamRow& row,
                                        const ::Vorago::Processor& proc) {
    namespace Vg = ::Vorago;
    const Vg::Processor::PacksForTest p = proc.packsForTest();
    if (row.taper == Vg::Taper::Discrete) {
        const std::atomic<int>* a = intAtomicOf(p, row.id);
        REQUIRE(a != nullptr);
        int index = a->load();
        if (row.id == Vg::kPolyphonyId) {
            index -= 1;  // stored as the voice count 1-6
        }
        return Vg::indexToNormalized(index, static_cast<int>(row.stepCount) + 1);
    }
    const std::atomic<float>* a = floatAtomicOf(p, row.id);
    REQUIRE(a != nullptr);
    const auto plain = static_cast<double>(a->load());
    switch (row.taper) {
        case Vg::Taper::Log:
            return Krate::Plugins::logMapToNormalized(plain, row.minPlain, row.maxPlain);
        case Vg::Taper::OffsetLog:
            return Vg::offsetLogToNormalized(plain, row.minPlain, row.maxPlain, row.eps);
        case Vg::Taper::Linear:
        case Vg::Taper::Discrete:
            break;
    }
    return Vg::linearToNormalized(plain, row.minPlain, row.maxPlain);
}

/// Sustain down and channel pressure 0.7 (the two non-persisted IDs).
void pressPedalAndPressure(VoragoTest::ProcessorFixture& fx) {
    applyNormalized(fx, NormalizedSet{{::Vorago::kSustainPedalId, 1.0},
                                      {::Vorago::kChannelPressureId, 0.7}});
    REQUIRE(std::bit_cast<std::uint32_t>(fx.proc->globalParamsForTest().sustainPedal.load()) ==
            std::bit_cast<std::uint32_t>(1.0f));
    REQUIRE(std::bit_cast<std::uint32_t>(fx.proc->globalParamsForTest().channelPressure.load()) ==
            std::bit_cast<std::uint32_t>(0.7f));
}

void requirePedalAndPressureZero(const ::Vorago::Processor& proc) {
    REQUIRE(std::bit_cast<std::uint32_t>(proc.globalParamsForTest().sustainPedal.load()) == 0u);
    REQUIRE(std::bit_cast<std::uint32_t>(proc.globalParamsForTest().channelPressure.load()) == 0u);
}

/// 4 s at 48 kHz, 512-sample blocks, note C2 velocity 100 held from sample 0.
void renderHeldNote(VoragoTest::ProcessorFixture& fx) {
    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kFourSeconds = std::size_t{4} * 48000u;
    fx.prepare(48000.0, 2048);
    fx.reserveCapture(kFourSeconds);
    Krate::Test::EventList ev;
    ev.addNoteOn(36, 100.0f / 127.0f, 0);
    for (std::size_t start = 0; start < kFourSeconds; start += kBlock) {
        const std::size_t len = std::min(kBlock, kFourSeconds - start);
        REQUIRE(fx.processBlock(len, (start == 0) ? &ev : nullptr) == Steinberg::kResultOk);
    }
}

}  // namespace

TEST_CASE("Vorago_StateRoundTripV2", "[vorago][state]") {
    namespace Vg = ::Vorago;

    STATIC_REQUIRE(Vg::kCurrentStateVersion == 2);
    STATIC_REQUIRE(Vg::kStateV2Bytes == 428u);

    const std::vector<Field> layout = streamLayout();
    REQUIRE(layout.size() == kNumPersisted);
    REQUIRE(layout.back().offset + 4u == Vg::kStateV2Bytes);  // C-7 sum == the table

    VoragoTest::ProcessorFixture defaults;  // registered defaults (SC-003)
    const Snapshot defaultSnap = snapshotAll(*defaults.proc);

    // The seeded source of every arm: 106 IDs, each off its default.
    VoragoTest::ProcessorFixture src;
    const NormalizedSet srcSet = seedPersisted(src, 12008u, *defaults.proc);
    const Snapshot srcSnap = snapshotAll(*src.proc);
    const std::vector<char> srcBytes = stateBytes(*src.proc);
    REQUIRE(srcBytes.size() == Vg::kStateV2Bytes);

    // A second, pre-dirtied state whose every persisted field differs from src.
    NormalizedSet dirtySet;
    {
        VoragoTest::ProcessorFixture d;
        dirtySet = seedPersisted(d, 12009u, *src.proc);
    }

    SECTION("(1) full round trip") {
        VoragoTest::ProcessorFixture dst;
        REQUIRE(dst.proc->setState(streamOf(srcBytes, srcBytes.size())) == Steinberg::kResultOk);
        const Snapshot dstSnap = snapshotAll(*dst.proc);
        for (std::size_t k = 0; k < kNumIds; ++k) {
            INFO("ID " << kExpectedParams[k].id);
            REQUIRE(dstSnap[k] == srcSnap[k]);
        }
        REQUIRE(stateBytes(*dst.proc) == srcBytes);

        // Controller mirror: every persisted ID at the taper inverse of the plain value.
        auto controller = Steinberg::owned(new Vg::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(controller->setComponentState(streamOf(srcBytes, srcBytes.size())) ==
                Steinberg::kResultOk);
        for (const ExpectedParamRow& row : kExpectedParams) {
            if (!isPersisted(row.id)) {
                continue;
            }
            const double want = expectedNormalized(row, *src.proc);
            const double got = controller->getParamNormalized(row.id);
            INFO("ID " << row.id << " controller=" << got << " expected=" << want);
            REQUIRE(std::fabs(got - want) <= 1.0e-9);
        }
        REQUIRE(controller->terminate() == Steinberg::kResultOk);

        // Both processors render a held note identically.
        renderHeldNote(src);
        renderHeldNote(dst);
        const auto sL = std::span<const float>(src.capturedL);
        const auto sR = std::span<const float>(src.capturedR);
        const auto dL = std::span<const float>(dst.capturedL);
        const auto dR = std::span<const float>(dst.capturedR);
        REQUIRE(VoragoTest::allFinite(sL));
        REQUIRE(VoragoTest::allFinite(sR));
        REQUIRE(VoragoTest::allFinite(dL));
        REQUIRE(VoragoTest::allFinite(dR));
        const float peak = std::max(VoragoTest::peakOf(sL.subspan(3072)),
                                    VoragoTest::peakOf(sR.subspan(3072)));
        INFO("source peak past latency " << peak);
        REQUIRE(peak >= 1.0e-4f);  // precondition: the comparison is not 0 == 0
        const float diffL = VoragoTest::maxAbsDiff(sL, dL);
        const float diffR = VoragoTest::maxAbsDiff(sR, dR);
        INFO("maxAbsDiff L=" << diffL << " R=" << diffR);
        REQUIRE(diffL <= 1.0e-5f);
        REQUIRE(diffR <= 1.0e-5f);
    }

    SECTION("(2) v1 stream") {
        std::vector<char> v1(60, 0);
        putLeWord(v1, 0, 1u);                                       // version 1
        putLeWord(v1, 4, std::bit_cast<std::uint32_t>(0.8f));       // master gain
        putLeWord(v1, 8, 2u);                                       // polyphony
        for (std::size_t i = 0; i < 12; ++i) {
            const float m = 0.05f + 0.07f * static_cast<float>(i);
            putLeWord(v1, 12u + 4u * i, std::bit_cast<std::uint32_t>(m));
        }

        const auto requireV1Fields = [](const Vg::Processor& proc) {
            REQUIRE(std::bit_cast<std::uint32_t>(proc.globalParamsForTest().masterGain.load()) ==
                    std::bit_cast<std::uint32_t>(0.8f));
            REQUIRE(proc.globalParamsForTest().polyphony.load() == 2);
            for (int i = 0; i < 12; ++i) {
                const float m = 0.05f + 0.07f * static_cast<float>(i);
                INFO("macro " << i);
                REQUIRE(std::bit_cast<std::uint32_t>(
                            Vg::macroField(proc.macroParamsForTest(), i).load()) ==
                        std::bit_cast<std::uint32_t>(m));
            }
        };
        const auto isV1Id = [](ParamID id) {
            return id == Vg::kMasterGainId || id == Vg::kPolyphonyId ||
                   isInRange(id, Vg::kMacroDarknessId, Vg::kMacroMassId);
        };

        // Fresh instance: the 92 other persisted fields stay at registered defaults.
        VoragoTest::ProcessorFixture fresh;
        REQUIRE(fresh.proc->setState(streamOf(v1, v1.size())) == Steinberg::kResultOk);
        requireV1Fields(*fresh.proc);
        const Snapshot freshSnap = snapshotAll(*fresh.proc);
        std::size_t others = 0;
        for (std::size_t k = 0; k < kNumIds; ++k) {
            const ParamID id = kExpectedParams[k].id;
            if (isV1Id(id)) {
                continue;
            }
            INFO("ID " << id);
            REQUIRE(freshSnap[k] == defaultSnap[k]);
            others += isPersisted(id) ? 1u : 0u;
        }
        REQUIRE(others == 92u);

        // Dirty instance: C-7 - a v1 load leaves every Phase 12 field at its
        // REGISTERED DEFAULT, not at the previous preset's value (FR-040).
        VoragoTest::ProcessorFixture dirty;
        applyNormalized(dirty, srcSet);  // every persisted field off its default
        const Snapshot before = snapshotAll(*dirty.proc);
        REQUIRE(dirty.proc->setState(streamOf(v1, v1.size())) == Steinberg::kResultOk);
        requireV1Fields(*dirty.proc);
        const Snapshot after = snapshotAll(*dirty.proc);
        std::size_t dirtiedOthers = 0;
        for (std::size_t k = 0; k < kNumIds; ++k) {
            const ParamID id = kExpectedParams[k].id;
            if (isV1Id(id)) {
                continue;
            }
            INFO("ID " << id);
            dirtiedOthers += (isPersisted(id) && before[k] != defaultSnap[k]) ? 1u : 0u;
            REQUIRE(after[k] == defaultSnap[k]);
        }
        REQUIRE(dirtiedOthers == 92u);  // non-vacuity: every Phase 12 field was off default

        // Controller mirror of the same version gate: the 14 v1 values.
        auto controller = Steinberg::owned(new Vg::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(controller->setComponentState(streamOf(v1, v1.size())) == Steinberg::kResultOk);
        REQUIRE(std::fabs(controller->getParamNormalized(Vg::kMasterGainId) - 0.4) <= 1.0e-7);
        REQUIRE(std::fabs(controller->getParamNormalized(Vg::kPolyphonyId) - 0.2) <= 1.0e-9);
        for (int i = 0; i < 12; ++i) {
            const auto id = static_cast<ParamID>(Vg::kMacroDarknessId + i);
            const double want = static_cast<double>(0.05f + 0.07f * static_cast<float>(i));
            INFO("macro " << i);
            REQUIRE(std::fabs(controller->getParamNormalized(id) - want) <= 1.0e-9);
        }
        REQUIRE(controller->terminate() == Steinberg::kResultOk);

        // Dirty controller: after a full v2 load, a v1 load returns every Phase 12
        // persisted ID to its registered default (C-7 mirror).
        auto dirtyCtl = Steinberg::owned(new Vg::Controller());
        REQUIRE(dirtyCtl->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(dirtyCtl->setComponentState(streamOf(srcBytes, srcBytes.size())) ==
                Steinberg::kResultOk);
        REQUIRE(dirtyCtl->setComponentState(streamOf(v1, v1.size())) == Steinberg::kResultOk);
        std::size_t ctlOthers = 0;
        for (const ExpectedParamRow& row : kExpectedParams) {
            if (!isPersisted(row.id) || isV1Id(row.id)) {
                continue;
            }
            const double want = expectedNormalized(row, *defaults.proc);
            const double got = dirtyCtl->getParamNormalized(row.id);
            INFO("ID " << row.id << " controller=" << got << " default=" << want);
            REQUIRE(std::fabs(got - want) <= 1.0e-9);
            ++ctlOthers;
        }
        REQUIRE(ctlOthers == 92u);
        REQUIRE(dirtyCtl->terminate() == Steinberg::kResultOk);
    }

    SECTION("(3) truncation at every offset") {
        for (const bool preDirtied : {false, true}) {
            for (std::size_t cut = 0; cut < Vg::kStateV2Bytes; ++cut) {
                INFO((preDirtied ? "pre-dirtied" : "fresh") << " instance, stream cut at " << cut);
                VoragoTest::ProcessorFixture fx;
                if (preDirtied) {
                    applyNormalized(fx, dirtySet);
                    pressPedalAndPressure(fx);
                }
                const Snapshot before = snapshotAll(*fx.proc);
                const Steinberg::tresult r = fx.proc->setState(streamOf(srcBytes, cut));
                const Snapshot after = snapshotAll(*fx.proc);

                if (cut < 4u) {  // no version: nothing loads, nothing changes
                    REQUIRE(r == Steinberg::kResultFalse);
                    REQUIRE(after == before);
                    continue;
                }
                REQUIRE(r == Steinberg::kResultOk);
                for (const Field& f : layout) {
                    const std::size_t k = rowIndexOf(f.id);
                    INFO("ID " << f.id << " at offset " << f.offset);
                    if (f.offset + 4u <= cut) {
                        REQUIRE(after[k] == srcSnap[k]);  // before the cut: restored
                    } else {
                        REQUIRE(after[k] == before[k]);  // from the cut on: unchanged
                    }
                }
                requirePedalAndPressureZero(*fx.proc);
            }
        }
    }

    SECTION("(4) future version rejected") {
        std::vector<char> future = srcBytes;
        putLeWord(future, 0, static_cast<std::uint32_t>(Vg::kCurrentStateVersion + 1));

        VoragoTest::ProcessorFixture fx;
        applyNormalized(fx, dirtySet);
        pressPedalAndPressure(fx);
        const Snapshot before = snapshotAll(*fx.proc);
        REQUIRE(fx.proc->setState(streamOf(future, future.size())) == Steinberg::kResultFalse);
        REQUIRE(snapshotAll(*fx.proc) == before);  // pedal and pressure included

        auto controller = Steinberg::owned(new Vg::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(controller->setComponentState(streamOf(future, future.size())) ==
                Steinberg::kResultFalse);
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("(5) NaN in each float field") {
        constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
        std::size_t floatFields = 0;
        for (const Field& nanField : layout) {
            if (nanField.isInt) {
                continue;
            }
            ++floatFields;
            INFO("NaN at ID " << nanField.id << " offset " << nanField.offset);
            std::vector<char> bytes = srcBytes;
            putLeWord(bytes, nanField.offset, kQuietNaNBits);

            VoragoTest::ProcessorFixture fx;
            applyNormalized(fx, dirtySet);
            const Snapshot before = snapshotAll(*fx.proc);
            REQUIRE(fx.proc->setState(streamOf(bytes, bytes.size())) == Steinberg::kResultOk);
            const Snapshot after = snapshotAll(*fx.proc);
            for (const Field& f : layout) {
                const std::size_t k = rowIndexOf(f.id);
                INFO("checking ID " << f.id);
                if (f.id == nanField.id) {
                    REQUIRE(after[k] == before[k]);  // rejected: unchanged
                } else {
                    REQUIRE(after[k] == srcSnap[k]);  // the rest loaded
                }
            }
        }
        REQUIRE(floatFields > 0u);
    }

    SECTION("(6) sustain and pressure are never persisted") {
        VoragoTest::ProcessorFixture pressed;
        applyNormalized(pressed, srcSet);
        pressPedalAndPressure(pressed);
        const std::vector<char> pressedBytes = stateBytes(*pressed.proc);
        REQUIRE(pressedBytes.size() == Vg::kStateV2Bytes);
        REQUIRE(pressedBytes == srcBytes);  // src holds pedal 0 / pressure 0

        VoragoTest::ProcessorFixture target;
        pressPedalAndPressure(target);
        REQUIRE(target.proc->setState(streamOf(srcBytes, srcBytes.size())) ==
                Steinberg::kResultOk);
        requirePedalAndPressureZero(*target.proc);
    }
}
