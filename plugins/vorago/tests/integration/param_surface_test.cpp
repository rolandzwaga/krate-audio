// ==============================================================================
// Vorago Phase 12 - parameter surface reaches the chain (SC-002..SC-005, SC-007, SC-009, SC-017, SC-023 (2))
// ==============================================================================
// Filled by T035 (SC-002 guard), T037-T042, T045.
//
// Shared conventions (plan section 6): 48 kHz, 512-sample blocks, note C2 (36)
// velocity 100, output-domain latency 3072 samples; both arms rendered in this
// process; references come from renderPhase11ReferenceChain - no checked-in golden.
// ==============================================================================

#include "phase11_reference_chain.h"
#include "plugin_ids.h"
#include "unit/param_table_expected.h"
#include "vorago_test_fixture.h"

#include "controller/controller.h"
#include "engine/vorago_engine_config.h"
#include "parameters/param_mapping.h"
#include "parameters/param_routes.h"

#include "pluginterfaces/base/smartpointer.h"
#include "public.sdk/source/common/memorystream.h"

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/systems/voice_allocator.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <render_fingerprint.h>  // SC-007: WARN-only secondary check

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kLatencySamples = 3072;  // FR-033: smear 2048 + cavern 1024
constexpr std::size_t kEightSeconds = std::size_t{8} * 48000u;  // 750 blocks of 512
constexpr float kVelocity100 = 100.0f / 127.0f;     // quantises to 100 (processor.cpp:48-50)

}  // namespace

// SC-002 / C-4 regression GUARD (T035): a fresh processor at its registered
// defaults renders the Phase 11 chain. Green now, and must stay green through
// every later Phase 12 task; a failure means the last change broke C-4.
TEST_CASE("Vorago_Phase12DefaultsMatchPhase11Chain", "[vorago][integration]") {
    VoragoTest::ProcessorFixture fx;
    fx.prepare(kSampleRate, 2048);

    Krate::Test::EventList ev;
    ev.addNoteOn(36, kVelocity100, 0);

    fx.reserveCapture(kEightSeconds);
    for (std::size_t start = 0; start < kEightSeconds; start += kBlock) {
        REQUIRE(fx.processBlock(kBlock, (start == 0) ? &ev : nullptr) == Steinberg::kResultOk);
    }
    REQUIRE(fx.capturedL.size() == kEightSeconds);
    REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedL)));
    REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedR)));

    VoragoTest::RefChainSpec spec{};
    spec.sampleRate = kSampleRate;
    spec.totalSamples = kEightSeconds;
    spec.blockSize = kBlock;
    spec.notes.push_back(VoragoTest::RefChainNote{.at = 0, .note = 36, .velocity = 100, .on = true});
    const VoragoTest::RefChainOutput ref = VoragoTest::renderPhase11ReferenceChain(spec);
    REQUIRE(ref.L.size() == kEightSeconds);
    REQUIRE(ref.R.size() == kEightSeconds);

    const auto refL = std::span<const float>(ref.L);
    const auto refR = std::span<const float>(ref.R);

    // Non-vacuity: the reference is audible past the latency.
    REQUIRE(VoragoTest::peakOf(refL.subspan(kLatencySamples)) >= 1e-4f);
    REQUIRE(VoragoTest::peakOf(refR.subspan(kLatencySamples)) >= 1e-4f);

    const float diffL = VoragoTest::maxAbsDiff(std::span<const float>(fx.capturedL), refL);
    const float diffR = VoragoTest::maxAbsDiff(std::span<const float>(fx.capturedR), refR);
    INFO("maxAbsDiff L = " << diffL << ", R = " << diffR);
    REQUIRE(diffL <= 1e-5f);
    REQUIRE(diffR <= 1e-5f);
}

// ==============================================================================
// T037 - MB and MAC routes (FR-020, FR-021, SC-005, SC-007 counter, SC-004 MB rows)
// ==============================================================================

namespace {

using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoMacro;
using Krate::DSP::VoragoMacroMatrix;
using Krate::DSP::VoragoMacroTarget;

constexpr std::size_t kFourSeconds = std::size_t{4} * 48000u;  // 375 blocks of 512
constexpr std::size_t kMbReadbackSamples = 4800;   // SC-004: 100 ms, >= 2 x kGainRampMs

[[nodiscard]] constexpr bool isEngineTarget(VoragoMacroTarget t) noexcept {
    const auto i = static_cast<std::size_t>(t);
    return i >= VoragoMacroMatrix::kFirstEngineTarget && i < VoragoMacroMatrix::kFirstCavernTarget;
}

[[nodiscard]] constexpr bool isCavernTarget(VoragoMacroTarget t) noexcept {
    return static_cast<std::size_t>(t) >= VoragoMacroMatrix::kFirstCavernTarget;
}

/// The getter paired with each Voice/Engine target's setter in
/// VoragoMacroMatrix::apply() (vorago_macro_matrix.h:1041-1105). Engine targets
/// ignore `voiceIndex`. Cavern targets have no engine getter (0).
[[nodiscard]] float readTarget(const VoragoEngine& e, VoragoMacroTarget t,
                               std::size_t voiceIndex) noexcept {
    using T = VoragoMacroTarget;
    const Krate::DSP::VoragoVoice& v = e.getVoice(voiceIndex);
    switch (t) {
        case T::CloudRichness: return v.getRichness();
        case T::CloudSpectralTiltDb: return v.getSpectralTiltDb();
        case T::CloudMutation: return v.getMutation();
        case T::CloudInharmonicity: return v.getInharmonicity();
        case T::CloudDriftDepthCents: return v.getDriftDepthCents();
        case T::NoiseLevelDb: return v.getNoiseLevelDb();
        case T::NoiseWakeBase: return v.getNoiseWakeBase();
        case T::NoiseWanderRate: return v.getNoiseWanderRate();
        case T::ResonanceGravity: return v.getResonanceGravity();
        case T::ResonanceMix: return v.getResonanceMix();
        case T::ResonanceWanderRate: return v.getResonanceWanderRate();
        case T::EcologyMix: return v.getEcologyMix();
        case T::EcologyLoopGain: return v.getEcologyLoopGain();
        case T::BodyBlend: return v.getBodyBlend();
        case T::BodyDamping: return v.getBodyDamping();
        case T::BodyResonance: return v.getBodyResonance();
        case T::BodyMix: return v.getBodyMix();
        case T::EcosystemDepth: return v.getEcosystemDepth();
        case T::EventRateScale: return v.getEventRateScale();
        case T::BloomDepth: return v.getBloomDepth();
        case T::BloomSpawnRateHz: return v.getBloomSpawnRateHz();
        case T::BreathingDepth: return v.getBreathingDepth();
        case T::BreathingIrregularity: return v.getBreathingIrregularity();
        case T::TidalDepth: return v.getTidalDepth();
        case T::ResonanceOctaveLock: return v.getResonanceOctaveLock();
        case T::SubToneLevelOffsetDb: return e.getSubToneLevelOffsetDb();
        case T::SubTrackingAmount: return e.getSubTrackingAmount();
        case T::SmearAmount: return e.getSmearAmount();
        case T::SmearDecoherence: return e.getSmearDecoherence();
        case T::SmearTilt: return e.getSmearTilt();
        case T::GhostPeakLevel: return e.getGhostPeakLevel();
        case T::AtmosBlur: return e.getAtmosBlur();
        case T::OutputSaturation: return e.getOutputSaturation();
        case T::OutputDriveDb: return e.getOutputDriveDb();
        default: return 0.0f;  // Cavern rows: read through the render instead
    }
}

/// Every Voice target on every i < getPolyphony() plus every Engine target.
[[nodiscard]] std::vector<float> matrixDrivenGetters(const VoragoEngine& e) {
    std::vector<float> out;
    for (std::size_t t = 0; t < VoragoMacroMatrix::kFirstCavernTarget; ++t) {
        const auto target = static_cast<VoragoMacroTarget>(t);
        if (isEngineTarget(target)) {
            out.push_back(readTarget(e, target, 0));
        } else {
            for (std::size_t i = 0; i < e.getPolyphony(); ++i) {
                out.push_back(readTarget(e, target, i));
            }
        }
    }
    return out;
}

/// A second engine prepared exactly as Processor::setupProcessing prepares its
/// own (seed before prepare, the same config, default polyphony), driven by
/// `matrix` once and run for one silent block, as the plugin's first process().
[[nodiscard]] std::unique_ptr<VoragoEngine> makeMatrixDrivenEngine(
    const VoragoMacroMatrix& matrix) {
    auto e = std::make_unique<VoragoEngine>();
    e->setSeed(::Vorago::kEngineSeed);
    e->prepare(kSampleRate, ::Vorago::makeVoragoEngineConfig(::Vorago::kMaxBlockSamples));
    e->setPolyphony(VoragoEngine::kDefaultPolyphony);
    matrix.apply(*e);
    std::vector<float> l(kBlock, 0.0f);
    std::vector<float> r(kBlock, 0.0f);
    e->processStereoBlock(l.data(), r.data(), kBlock);
    e->processOutputStage(l.data(), r.data(), kBlock);
    return e;
}

struct MacroArm {
    VoragoMacro macro;
    float value;
};

/// SC-005 (1): each macro at 1.0 alone, plus Gravity at 0.0 (the bipolar air half).
[[nodiscard]] std::vector<MacroArm> macroArms() {
    std::vector<MacroArm> arms;
    arms.reserve(VoragoMacroMatrix::kNumMacros + 1);
    for (std::size_t k = 0; k < VoragoMacroMatrix::kNumMacros; ++k) {
        arms.push_back(MacroArm{.macro=static_cast<VoragoMacro>(k), .value=1.0f});
    }
    arms.push_back(MacroArm{.macro=VoragoMacro::Gravity, .value=0.0f});
    return arms;
}

[[nodiscard]] Steinberg::Vst::ParamID macroParamId(VoragoMacro m) noexcept {
    return static_cast<Steinberg::Vst::ParamID>(::Vorago::kMacroDarknessId) +
           static_cast<Steinberg::Vst::ParamID>(m);
}

/// Note 36 velocity 100 at sample 0, `pc` in block 0, 4 s in 512-sample blocks.
void renderFourSeconds(VoragoTest::ProcessorFixture& fx, Steinberg::Vst::IParameterChanges* pc) {
    Krate::Test::EventList ev;
    ev.addNoteOn(36, kVelocity100, 0);
    fx.reserveCapture(fx.capturedL.size() + kFourSeconds);
    for (std::size_t start = 0; start < kFourSeconds; start += kBlock) {
        REQUIRE(fx.processBlock(kBlock, (start == 0) ? &ev : nullptr,
                                (start == 0) ? pc : nullptr) == Steinberg::kResultOk);
    }
}

[[nodiscard]] VoragoTest::RefChainSpec fourSecondSpec() {
    VoragoTest::RefChainSpec spec{};
    spec.sampleRate = kSampleRate;
    spec.totalSamples = kFourSeconds;
    spec.blockSize = kBlock;
    spec.notes.push_back(VoragoTest::RefChainNote{.at = 0, .note = 36, .velocity = 100, .on = true});
    return spec;
}

[[nodiscard]] const VoragoTest::ExpectedParamRow* expectedRowFor(
    Steinberg::Vst::ParamID id) noexcept {
    for (const auto& row : VoragoTest::kExpectedParams) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

/// The plain value a normalized `n` denormalizes to along the row's taper
/// (checked-in table, independent of the pack headers).
[[nodiscard]] double plainAt(const VoragoTest::ExpectedParamRow& row, double n) noexcept {
    switch (row.taper) {
        case ::Vorago::Taper::Log:
            return Krate::Plugins::logMapFromNormalized(n, row.minPlain, row.maxPlain);
        case ::Vorago::Taper::OffsetLog:
            return ::Vorago::offsetLogFromNormalized(n, row.minPlain, row.maxPlain, row.eps);
        case ::Vorago::Taper::Linear:
        case ::Vorago::Taper::Discrete:
            break;
    }
    return ::Vorago::linearFromNormalized(n, row.minPlain, row.maxPlain);
}

/// SC-004: within 1e-6 relative of the denormalized value (floored at 1e-3 so a
/// value near zero is not held to an absolute 1e-12).
[[nodiscard]] bool withinRelative(float actual, float reference) noexcept {
    const double tol = 1.0e-6 * std::max(std::fabs(static_cast<double>(reference)), 1.0e-3);
    return std::fabs(static_cast<double>(actual) - static_cast<double>(reference)) <= tol;
}

}  // namespace

// SC-005: the twelve macro knobs drive the matrix (Phase 11's inert macros are
// replaced). Clause 3 is the inversion of Phase 11 SC-023.
TEST_CASE("Vorago_MacrosDriveTheMatrix", "[vorago][integration]") {
    const std::vector<MacroArm> arms = macroArms();
    REQUIRE(arms.size() == 13u);

    for (const MacroArm& arm : arms) {
        const auto macroIndex = static_cast<int>(arm.macro);
        INFO("macro " << macroIndex << " at " << arm.value);

        VoragoMacroMatrix matrix{};
        matrix.setMacro(arm.macro, arm.value);

        // (1) every Voice/Engine getter equals the directly driven engine's, exactly.
        {
            VoragoTest::ProcessorFixture fx;
            fx.prepare(kSampleRate, 2048);
            Krate::Test::ParameterChanges pc;
            pc.addChange(macroParamId(arm.macro), static_cast<double>(arm.value));
            fx.reserveCapture(kBlock);
            REQUIRE(fx.processBlock(kBlock, nullptr, &pc) == Steinberg::kResultOk);

            REQUIRE(fx.proc->macrosForTest().getMacro(arm.macro) == arm.value);

            const auto reference = makeMatrixDrivenEngine(matrix);
            const VoragoEngine* plugin = fx.proc->engineForTest();
            REQUIRE(plugin != nullptr);
            REQUIRE(plugin->getPolyphony() == reference->getPolyphony());

            const std::vector<float> got = matrixDrivenGetters(*plugin);
            const std::vector<float> want = matrixDrivenGetters(*reference);
            REQUIRE(got.size() == want.size());
            for (std::size_t i = 0; i < got.size(); ++i) {
                INFO("getter slot " << i);
                REQUIRE(got[i] == want[i]);
            }
        }

        // (2) the render equals the reference chain driven by that matrix.
        {
            VoragoTest::ProcessorFixture fx;
            fx.prepare(kSampleRate, 2048);
            Krate::Test::ParameterChanges pc;
            pc.addChange(macroParamId(arm.macro), static_cast<double>(arm.value));
            renderFourSeconds(fx, &pc);
            REQUIRE(fx.capturedL.size() == kFourSeconds);

            const MacroArm hookArm = arm;
            const VoragoTest::RefChainOutput ref = VoragoTest::renderPhase11ReferenceChain(
                fourSecondSpec(),
                [hookArm](std::size_t blockIndex, Krate::DSP::VoragoEngine&,
                          Krate::DSP::CavernVerb&, VoragoMacroMatrix& m) noexcept {
                    if (blockIndex == 0) {
                        m.setMacro(hookArm.macro, hookArm.value);
                    }
                });
            const auto refL = std::span<const float>(ref.L);
            const auto refR = std::span<const float>(ref.R);

            const double rmsL = VoragoTest::rmsOf(refL.subspan(kLatencySamples));
            const double rmsR = VoragoTest::rmsOf(refR.subspan(kLatencySamples));
            INFO("reference RMS [3072, end) L=" << rmsL << " R=" << rmsR);
            REQUIRE(rmsL >= 1.0e-4);
            REQUIRE(rmsR >= 1.0e-4);

            const float diffL =
                VoragoTest::maxAbsDiff(std::span<const float>(fx.capturedL), refL);
            const float diffR =
                VoragoTest::maxAbsDiff(std::span<const float>(fx.capturedR), refR);
            INFO("maxAbsDiff L=" << diffL << " R=" << diffR);
            REQUIRE(diffL <= 1.0e-6f);
            REQUIRE(diffR <= 1.0e-6f);
        }
    }

    // (3) inversion of Phase 11 SC-023: all macros at 1.0 vs registered defaults.
    {
        Krate::Test::ParameterChanges allHigh;
        for (Steinberg::Vst::ParamID id = ::Vorago::kMacroDarknessId;
             id <= ::Vorago::kMacroMassId; ++id) {
            allHigh.addChange(id, 1.0);
        }
        VoragoTest::ProcessorFixture m0;
        VoragoTest::ProcessorFixture m1;
        m0.prepare(kSampleRate, 2048);
        m1.prepare(kSampleRate, 2048);
        renderFourSeconds(m0, nullptr);
        renderFourSeconds(m1, &allHigh);

        const double rmsL = VoragoTest::rmsDiff(std::span<const float>(m0.capturedL),
                                                std::span<const float>(m1.capturedL),
                                                kLatencySamples);
        const double rmsR = VoragoTest::rmsDiff(std::span<const float>(m0.capturedR),
                                                std::span<const float>(m1.capturedR),
                                                kLatencySamples);
        WARN("SC-005 (3) rmsDiff(defaults, all macros at 1) L=" << rmsL << " R=" << rmsR);
        REQUIRE(std::max(rmsL, rmsR) > 1.0e-3);
    }
}

// ==============================================================================
// T045 - SC-007 block-size invariance with pedal (carries Phase 11 SC-008 (1))
// ==============================================================================

namespace {

// Phase 11 SC-008 (1) script, copied from unit/midi_event_test.cpp:434-439.
struct InvarianceNote {
    std::size_t at;
    Steinberg::int16 pitch;
    float velocity;
    bool on;
};
constexpr std::array<InvarianceNote, 4> kInvarianceNotes{{
    InvarianceNote{.at=0, .pitch=48, .velocity=1.0f, .on=true},
    InvarianceNote{.at=37111, .pitch=55, .velocity=0.8f, .on=true},
    InvarianceNote{.at=100003, .pitch=48, .velocity=0.0f, .on=false},  // latched: the pedal is down here
    InvarianceNote{.at=150007, .pitch=60, .velocity=0.9f, .on=true},
}};

// C-9 pedal points: down before the note-off of 48, up after it (releases 48).
struct InvariancePedal {
    std::size_t at;
    bool down;
};
constexpr std::array<InvariancePedal, 2> kInvariancePedal{{
    InvariancePedal{.at=90001, .down=true},
    InvariancePedal{.at=130003, .down=false},
}};

// Read-back points (strictly between the pedal/note events above).
constexpr std::size_t kLatchedCheckAt = 115001;   // note-off 48 latched, pedal down
constexpr std::size_t kReleasedCheckAt = 140001;  // pedal up released 48, before note 60

constexpr std::array<std::size_t, 6> kPartitionsOtherThanOne{7, 64, 65, 512, 2048, 4096};

[[nodiscard]] constexpr bool isNonMultipleOfEveryPartition(std::size_t at) noexcept {
    return std::ranges::all_of(kPartitionsOtherThanOne,
                               [at](std::size_t p) { return at % p != 0u; });
}

static_assert(isNonMultipleOfEveryPartition(37111) && isNonMultipleOfEveryPartition(100003) &&
                  isNonMultipleOfEveryPartition(150007),
              "SC-007: note offsets after 0 are non-multiples of every partition");
static_assert(isNonMultipleOfEveryPartition(90001) && isNonMultipleOfEveryPartition(130003),
              "SC-007: pedal offsets are non-multiples of every partition");
static_assert(90001u < 100003u && 100003u < kLatchedCheckAt && kLatchedCheckAt < 130003u &&
                  130003u < kReleasedCheckAt && kReleasedCheckAt < 150007u,
              "SC-007: pedal down -> note-off (latched) -> pedal up (released) -> next note");
static_assert(65u % 64u != 0u,
              "SC-007: a 65-sample partition boundary falls inside a 64-sample control chunk");

struct InvarianceRun {
    std::vector<float> l;
    std::vector<float> r;
    std::size_t activeWhileLatched = 0;
    bool pedalDownWhileLatched = false;
    std::size_t activeAfterPedalUp = 0;
    bool pedalDownAfterPedalUp = true;
    std::uint32_t eventFreeSliceCount = 0;  // only for probeSlices
};

[[nodiscard]] std::size_t countActiveSlots(const VoragoEngine& e) noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
        if (e.getVoiceState(i) == Krate::DSP::VoiceState::Active) {
            ++n;
        }
    }
    return n;
}

/// Normalized value of a linear row's plain value (checked-in table ranges).
[[nodiscard]] double linearNormalizedFor(Steinberg::Vst::ParamID id, double plain) {
    const VoragoTest::ExpectedParamRow* row = expectedRowFor(id);
    REQUIRE(row != nullptr);
    REQUIRE(row->taper == ::Vorago::Taper::Linear);
    return ::Vorago::linearToNormalized(plain, row->minPlain, row->maxPlain);
}

/// The SC-007 script at host block `block`: at sample 0 only, all twelve macros
/// 0.7 (Gravity 0.8), MB 201 -> -6 dB/oct and VP 206 -> 0.5; notes and pedal
/// points at their absolute positions. `probeSlices` appends one event-free
/// block after the render and records FR-026's slice count.
[[nodiscard]] InvarianceRun renderInvariance(std::size_t block, bool probeSlices) {
    const double tiltN = linearNormalizedFor(::Vorago::kCloudTiltId, -6.0);
    const double gravityN = linearNormalizedFor(::Vorago::kCloudSpectralGravityId, 0.5);

    VoragoTest::ProcessorFixture fx;
    fx.prepare(kSampleRate, static_cast<Steinberg::int32>(block));
    fx.reserveCapture(kFourSeconds + block);

    VoragoTest::MultiParamChanges pc;
    pc.reserve(16);  // 12 macros + 201 + 206 + pedal; addQueue never grows past this
    Krate::Test::EventList ev;
    InvarianceRun out{};

    std::size_t start = 0;
    while (start < kFourSeconds) {
        const std::size_t n = std::min(block, kFourSeconds - start);
        pc.clear();
        ev.clear();

        if (start == 0) {
            for (Steinberg::Vst::ParamID id = ::Vorago::kMacroDarknessId;
                 id <= ::Vorago::kMacroMassId; ++id) {
                pc.addQueue(id).addTestPoint(0, (id == ::Vorago::kMacroGravityId) ? 0.8 : 0.7);
            }
            pc.addQueue(::Vorago::kCloudTiltId).addTestPoint(0, tiltN);
            pc.addQueue(::Vorago::kCloudSpectralGravityId).addTestPoint(0, gravityN);
        }

        VoragoTest::MultiPointParamValueQueue* pedalQueue = nullptr;
        for (const InvariancePedal& p : kInvariancePedal) {
            if (p.at >= start && p.at < start + n) {
                if (pedalQueue == nullptr) {
                    pedalQueue = &pc.addQueue(::Vorago::kSustainPedalId);
                }
                pedalQueue->addTestPoint(static_cast<Steinberg::int32>(p.at - start),
                                         p.down ? 1.0 : 0.0);
            }
        }

        for (const InvarianceNote& note : kInvarianceNotes) {
            if (note.at >= start && note.at < start + n) {
                const auto offset = static_cast<Steinberg::int32>(note.at - start);
                if (note.on) {
                    ev.addNoteOn(note.pitch, note.velocity, offset);
                } else {
                    ev.addNoteOff(note.pitch, offset);
                }
            }
        }

        REQUIRE(fx.processBlock(n, (ev.getEventCount() > 0) ? &ev : nullptr,
                                (pc.getParameterCount() > 0) ? &pc : nullptr) ==
                Steinberg::kResultOk);

        const auto covers = [start, n](std::size_t at) { return at >= start && at < start + n; };
        if (covers(kLatchedCheckAt) || covers(kReleasedCheckAt)) {
            const VoragoEngine* engine = fx.proc->engineForTest();
            REQUIRE(engine != nullptr);
            if (covers(kLatchedCheckAt)) {
                out.activeWhileLatched = countActiveSlots(*engine);
                out.pedalDownWhileLatched = fx.proc->latchForTest().isDown();
            } else {
                out.activeAfterPedalUp = countActiveSlots(*engine);
                out.pedalDownAfterPedalUp = fx.proc->latchForTest().isDown();
            }
        }
        start += n;
    }
    REQUIRE(fx.capturedL.size() == kFourSeconds);
    REQUIRE(fx.capturedR.size() == kFourSeconds);

    if (probeSlices) {
        // FR-026's seam: one event-free block of `block` samples.
        REQUIRE(fx.processBlock(block) == Steinberg::kResultOk);
        out.eventFreeSliceCount = fx.proc->lastSliceCountForTest();
    }

    out.l.assign(fx.capturedL.begin(),
                 fx.capturedL.begin() + static_cast<std::ptrdiff_t>(kFourSeconds));
    out.r.assign(fx.capturedR.begin(),
                 fx.capturedR.begin() + static_cast<std::ptrdiff_t>(kFourSeconds));
    return out;
}

/// The pedal script did what it claims: note 48's note-off latched (still
/// Active alongside 55), then the pedal-up released it (55 alone Active).
void requirePedalScriptLatched(const InvarianceRun& run) {
    REQUIRE(run.pedalDownWhileLatched);
    REQUIRE(run.activeWhileLatched == 2u);
    REQUIRE_FALSE(run.pedalDownAfterPedalUp);
    REQUIRE(run.activeAfterPedalUp == 1u);
}

}  // namespace

// SC-007 counter: exactly one setMacros / apply / applyCavernTargets per
// process(), however many events the block carries (never per slice).
TEST_CASE("Vorago_MacroPushOncePerProcess", "[vorago][integration]") {
    SECTION("Counter") {
        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, 2048);

        constexpr std::array<std::size_t, 4> kEventCounts = {0u, 1u, 64u, 1024u};
        fx.reserveCapture(kEventCounts.size() * kBlock);
        for (const std::size_t count : kEventCounts) {
            INFO("events in block: " << count);
            Krate::Test::EventList ev;
            for (std::size_t k = 0; k < count; ++k) {
                const auto pitch = static_cast<Steinberg::int16>(36u + (k / 2u) % 24u);
                const auto offset = static_cast<Steinberg::int32>(k % kBlock);
                if ((k % 2u) == 0u) {
                    ev.addNoteOn(pitch, kVelocity100, offset);
                } else {
                    ev.addNoteOff(pitch, offset);
                }
            }
            REQUIRE(ev.getEventCount() == static_cast<Steinberg::int32>(count));

            const std::uint64_t before = fx.proc->macroPushCountForTest();
            REQUIRE(fx.processBlock(kBlock, &ev) == Steinberg::kResultOk);
            REQUIRE(fx.proc->macroPushCountForTest() == before + 1u);
        }
    }

    SECTION("BlockSizeInvariance") {  // SC-007 full (T045), carries Phase 11 SC-008 (1)
        const auto wallStart = std::chrono::steady_clock::now();

        const InvarianceRun reference = renderInvariance(kBlock, false);
        requirePedalScriptLatched(reference);

        const std::span<const float> refL(reference.l);
        const std::span<const float> refR(reference.r);
        const float refPeak = std::max(VoragoTest::peakOf(refL.subspan(kLatencySamples)),
                                       VoragoTest::peakOf(refR.subspan(kLatencySamples)));
        WARN("SC-007 reference (512) peak over [3072, end) = " << refPeak);
        REQUIRE(refPeak >= 1.0e-4f);  // P-6 precondition: lengthen the script, never the bound

        const auto refFpL = Krate::DSP::TestUtils::fingerprintRender(refL);
        const auto refFpR = Krate::DSP::TestUtils::fingerprintRender(refR);

        constexpr std::array<std::size_t, 6> kPartitions{1, 7, 64, 65, 2048, 4096};
        for (const std::size_t block : kPartitions) {
            INFO("host block " << block);
            const InvarianceRun run = renderInvariance(block, block == 4096u);
            requirePedalScriptLatched(run);
            if (block == 4096u) {
                // FR-026 sub-division: an event-free 4096 block renders as 2 slices.
                REQUIRE(run.eventFreeSliceCount == 2u);
            }

            const std::span<const float> runL(run.l);
            const std::span<const float> runR(run.r);
            const float dl = VoragoTest::maxAbsDiff(runL, refL);
            const float dr = VoragoTest::maxAbsDiff(runR, refR);
            WARN("SC-007 block " << block << " maxAbsDiff vs 512: L=" << dl << " R=" << dr);
            REQUIRE(dl <= 1.0e-5f);
            REQUIRE(dr <= 1.0e-5f);

            // Secondary, WARN-only.
            const auto cmpL = Krate::DSP::TestUtils::compareFingerprints(
                Krate::DSP::TestUtils::fingerprintRender(runL), refFpL);
            const auto cmpR = Krate::DSP::TestUtils::compareFingerprints(
                Krate::DSP::TestUtils::fingerprintRender(runR), refFpR);
            if (!cmpL.withinTolerance() || !cmpR.withinTolerance()) {
                WARN("SC-007 fingerprint drift at block " << block << ": L " << cmpL.detail
                                                          << " | R " << cmpR.detail);
            }
        }

        const double wall =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
        WARN("SC-007 BlockSizeInvariance wall time = " << wall << " s");
    }
}

// ==============================================================================
// T038 - VP route (FR-020, SC-004 VP rows)
// ==============================================================================

namespace {

constexpr std::array<Steinberg::int16, 6> kSixHeldNotes = {36, 43, 48, 55, 60, 67};
constexpr double kVpCombFundamentalHz = 440.0;  // SC-004: below the 44.1 kHz ceiling

/// The one change a VP row makes: its normalized value and the plain value it
/// denormalizes to (the list index for discrete rows).
struct VpProbe {
    double normalized;
    double plain;
};

[[nodiscard]] bool isInRange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamID first,
                             Steinberg::Vst::ParamID last) noexcept {
    return id >= first && id <= last;
}

/// SC-004 VP values: comb fundamental 440 Hz; other continuous rows at 0.8 of
/// the range along the taper; discrete rows at the index after the default.
[[nodiscard]] VpProbe vpProbeFor(const VoragoTest::ExpectedParamRow& row) {
    if (row.taper == ::Vorago::Taper::Discrete) {
        const int count = static_cast<int>(row.stepCount) + 1;
        const auto defaultIndex =
            static_cast<int>(std::lround(row.defaultNormalized * (count - 1)));
        const int index = (defaultIndex + 1) % count;
        return VpProbe{.normalized=::Vorago::indexToNormalized(index, count), .plain=static_cast<double>(index)};
    }
    if (isInRange(row.id, ::Vorago::kNoiseSlot0CombFundamentalId,
                  ::Vorago::kNoiseSlot3CombFundamentalId)) {
        const double n =
            Krate::Plugins::logMapToNormalized(kVpCombFundamentalHz, row.minPlain, row.maxPlain);
        return VpProbe{.normalized=n, .plain=plainAt(row, n)};
    }
    const double n = (std::fabs(row.defaultNormalized - 0.8) < 1.0e-6) ? 0.2 : 0.8;
    return VpProbe{.normalized=n, .plain=plainAt(row, n)};
}

/// Reads the VP destination of `id` on one voice and compares it with `plain`:
/// relative 1e-6 for continuous rows, exact for discrete rows.
void requireVpReadback(const Krate::DSP::VoragoVoice& v, Steinberg::Vst::ParamID id,
                       double plain) {
    const auto want = static_cast<float>(plain);
    const auto index = static_cast<int>(std::lround(plain));
    if (id == ::Vorago::kCloudStereoSpreadId) {
        REQUIRE(withinRelative(v.cloud().getStereoSpread(), want));
    } else if (id == ::Vorago::kCloudSpectralGravityId) {
        REQUIRE(withinRelative(v.cloud().getSpectralGravity(), want));
    } else if (isInRange(id, ::Vorago::kNoiseSlot0ModelId, ::Vorago::kNoiseSlot3ModelId)) {
        const std::size_t slot = id - ::Vorago::kNoiseSlot0ModelId;
        REQUIRE(v.noise().getSourceModel(slot) ==
                static_cast<Krate::DSP::NoiseOrganismModel>(index));
    } else if (isInRange(id, ::Vorago::kNoiseSlot0TypeId, ::Vorago::kNoiseSlot3TypeId)) {
        // Direct probe: the same change set the slot's model to Direct, so the
        // effective type IS the requested one (noise_organism.h:1225-1240).
        const std::size_t slot = id - ::Vorago::kNoiseSlot0TypeId;
        REQUIRE(v.noise().getSourceModel(slot) == Krate::DSP::NoiseOrganismModel::Direct);
        REQUIRE(v.noise().getSourceNoiseType(slot) ==
                ::Vorago::kNoiseTypeByIndex[static_cast<std::size_t>(index)]);
    } else if (isInRange(id, ::Vorago::kNoiseSlot0CombFundamentalId,
                         ::Vorago::kNoiseSlot3CombFundamentalId)) {
        const std::size_t slot = id - ::Vorago::kNoiseSlot0CombFundamentalId;
        INFO("getCombFundamental=" << v.noise().getCombFundamental(slot) << " want=" << want);
        REQUIRE(withinRelative(v.noise().getCombFundamental(slot), want));
    } else if (isInRange(id, ::Vorago::kNoiseSlot0CombSpreadId,
                         ::Vorago::kNoiseSlot3CombSpreadId)) {
        const std::size_t slot = id - ::Vorago::kNoiseSlot0CombSpreadId;
        REQUIRE(withinRelative(v.noise().getCombSpread(slot), want));
    } else if (isInRange(id, ::Vorago::kNoiseSlot0CombFeedbackId,
                         ::Vorago::kNoiseSlot3CombFeedbackId)) {
        const std::size_t slot = id - ::Vorago::kNoiseSlot0CombFeedbackId;
        REQUIRE(withinRelative(v.noise().getCombFeedback(slot), want));
    } else if (id == ::Vorago::kResonanceAnchorModeId) {
        REQUIRE(v.resonance().getAnchorMode() ==
                static_cast<Krate::DSP::ResonanceDriftNetwork::AnchorMode>(index));
    } else if (isInRange(id, ::Vorago::kEcologyLoop0FilterModeId,
                         ::Vorago::kEcologyLoop5FilterModeId)) {
        const std::size_t loop = id - ::Vorago::kEcologyLoop0FilterModeId;
        REQUIRE(v.ecology().getLoopFilterMode(loop) ==
                static_cast<Krate::DSP::FeedbackEcology::FilterMode>(index));
    } else if (id == ::Vorago::kBodyMaterialAId) {
        REQUIRE(v.bodyA().getMaterial() ==
                static_cast<Krate::DSP::ContinuousBody::BodyMaterial>(index));
    } else if (id == ::Vorago::kBodyMaterialBId) {
        REQUIRE(v.bodyB().getMaterial() ==
                static_cast<Krate::DSP::ContinuousBody::BodyMaterial>(index));
    } else {
        FAIL("VP ID without a read-back: " << id);
    }
}

/// Block 0: polyphony 6 and six held notes, so every slot sounds; then `change`
/// at the start of the next block and `samples` more in 512-sample blocks.
void renderSixVoicesThenChange(VoragoTest::ProcessorFixture& fx,
                               Steinberg::Vst::IParameterChanges* change, std::size_t samples) {
    fx.reserveCapture(kBlock + samples);
    Krate::Test::ParameterChanges setup;
    setup.addChange(::Vorago::kPolyphonyId, 1.0);
    Krate::Test::EventList notes;
    for (const Steinberg::int16 p : kSixHeldNotes) {
        notes.addNoteOn(p, kVelocity100, 0);
    }
    REQUIRE(fx.processBlock(kBlock, &notes, &setup) == Steinberg::kResultOk);

    std::size_t rendered = 0;
    while (rendered < samples) {
        const std::size_t len = std::min(kBlock, samples - rendered);
        REQUIRE(fx.processBlock(len, nullptr, (rendered == 0) ? change : nullptr) ==
                Steinberg::kResultOk);
        rendered += len;
    }
}

}  // namespace

// ==============================================================================
// T040 - Cavern rows (SC-004): the 7 MB-cavern IDs (1100-1106) and 9 CV IDs (1107-1115)
// ==============================================================================

namespace {

/// SC-004 cavern probe: 0.8 normalized (0.2 where 0.8 IS the default, so the
/// change is never a no-op); Freeze at index 1 (On).
[[nodiscard]] double cavernProbeNormalized(const VoragoTest::ExpectedParamRow& row) noexcept {
    if (row.taper == ::Vorago::Taper::Discrete) {
        return ::Vorago::indexToNormalized(1, ::Vorago::kSpaceFreezeCount);
    }
    if (row.id == ::Vorago::kSpaceDamperRateId) {
        // Range end (1 Hz). The damper modulation's audible footprint grows with
        // its rate: under the B-5 white-noise stimulus 0.39 Hz (normalized 0.8)
        // measured 1.8e-4 RMS at 4/8/16 s, 1 Hz measured 3.75e-3 (2026-09-25).
        return 1.0;
    }
    return (std::fabs(row.defaultNormalized - 0.8) < 1.0e-6) ? 0.2 : 0.8;
}

/// The CavernVerb setter a CV ID drives (cavern_verb.h:632-738), applied by hand
/// to the reference chain. Freeze: `plain` is the list index (1 = On).
void applyCvSetterByHand(Krate::DSP::CavernVerb& c, Steinberg::Vst::ParamID id,
                         float plain) noexcept {
    switch (id) {
        case ::Vorago::kSpaceDensityId: c.setDensity(plain); break;
        case ::Vorago::kSpaceDimensionalityId: c.setDimensionality(plain); break;
        case ::Vorago::kSpaceBreathId: c.setBreath(plain); break;
        case ::Vorago::kSpaceEarlySizeId: c.setEarlySizeMs(plain); break;
        case ::Vorago::kSpaceEarlyLevelId: c.setEarlyLevel(plain); break;
        case ::Vorago::kSpaceEarlyAbsorptionId: c.setEarlyAbsorption(plain); break;
        case ::Vorago::kSpaceEarlySendId: c.setEarlySend(plain); break;
        case ::Vorago::kSpaceDamperRateId: c.setDamperRate(plain); break;
        case ::Vorago::kSpaceFreezeId: c.setFreeze(plain != 0.0f); break;
        default: break;
    }
}

/// The matrix target an MB ID bases (kMbRoutes), if any.
[[nodiscard]] std::optional<VoragoMacroTarget> mbTargetOf(Steinberg::Vst::ParamID id) noexcept {
    for (const ::Vorago::MbRouteEntry& r : ::Vorago::kMbRoutes) {
        if (r.id == id) {
            return r.target;
        }
    }
    return std::nullopt;
}

}  // namespace

// ==============================================================================
// T041 - shared helpers: pack-side denormalization, the every-ID probe and the
// full read-back (FR-022, SC-003, SC-004 re-prepare; the read-back is reused by
// SC-009 in T042)
// ==============================================================================

namespace {

/// Every pack by value, never touched by a Processor (SC-003's pack-side arm).
struct LocalPacks {
    ::Vorago::GlobalParams global{};
    ::Vorago::MacroParams macro{};
    ::Vorago::CloudParams cloud{};
    ::Vorago::NoiseParams noise{};
    ::Vorago::ResonanceParams resonance{};
    ::Vorago::EcologyParams ecology{};
    ::Vorago::SubParams sub{};
    ::Vorago::SmearParams smear{};
    ::Vorago::EventsParams events{};
    ::Vorago::EcosystemParams ecosystem{};
    ::Vorago::BodyParams body{};
    ::Vorago::SpaceParams space{};
    ::Vorago::EnvelopeParams envelope{};
    ::Vorago::BloomParams bloom{};
    ::Vorago::GhostParams ghost{};
    ::Vorago::LifeParams life{};

    [[nodiscard]] ::Vorago::Processor::PacksForTest view() const noexcept {
        return ::Vorago::Processor::PacksForTest{.global=global,   .macro=macro,     .cloud=cloud, .noise=noise,
                                                 .resonance=resonance, .ecology=ecology,  .sub=sub,   .smear=smear,
                                                 .events=events,   .ecosystem=ecosystem, .body=body,  .space=space,
                                                 .envelope=envelope, .bloom=bloom,     .ghost=ghost, .life=life};
    }
};

/// The band dispatch of Processor::processParameterChanges, onto LocalPacks.
void storeViaPack(LocalPacks& p, Steinberg::Vst::ParamID id, double n) noexcept {
    namespace Vg = ::Vorago;
    if (id < Vg::kGlobalParamRangeEnd) {
        Vg::handleGlobalParamChange(p.global, id, n);
    } else if (id < Vg::kMacroParamRangeEnd) {
        Vg::handleMacroParamChange(p.macro, id, n);
    } else if (id < Vg::kCloudParamRangeEnd) {
        Vg::handleCloudParamChange(p.cloud, id, n);
    } else if (id < Vg::kNoiseParamRangeEnd) {
        Vg::handleNoiseParamChange(p.noise, id, n);
    } else if (id < Vg::kResonanceParamRangeEnd) {
        Vg::handleResonanceParamChange(p.resonance, id, n);
    } else if (id < Vg::kEcologyParamRangeEnd) {
        Vg::handleEcologyParamChange(p.ecology, id, n);
    } else if (id < Vg::kSubParamRangeEnd) {
        Vg::handleSubParamChange(p.sub, id, n);
    } else if (id < Vg::kSmearParamRangeEnd) {
        Vg::handleSmearParamChange(p.smear, id, n);
    } else if (id < Vg::kEventsParamRangeEnd) {
        Vg::handleEventsParamChange(p.events, id, n);
    } else if (id < Vg::kEcosystemParamRangeEnd) {
        Vg::handleEcosystemParamChange(p.ecosystem, id, n);
    } else if (id < Vg::kBodyParamRangeEnd) {
        Vg::handleBodyParamChange(p.body, id, n);
    } else if (id < Vg::kSpaceParamRangeEnd) {
        Vg::handleSpaceParamChange(p.space, id, n);
    } else if (id < Vg::kEnvelopeParamRangeEnd) {
        Vg::handleEnvelopeParamChange(p.envelope, id, n);
    } else if (id < Vg::kBloomParamRangeEnd) {
        Vg::handleBloomParamChange(p.bloom, id, n);
    } else if (id < Vg::kGhostParamRangeEnd) {
        Vg::handleGhostParamChange(p.ghost, id, n);
    } else if (id < Vg::kLifeParamRangeEnd) {
        Vg::handleLifeParamChange(p.life, id, n);
    }
}

/// The plain value a pack holds for `id`: the float atomic, or the int atomic
/// (list index; polyphony is the voice count 1-6). nullopt for an unknown ID.
[[nodiscard]] std::optional<double> storedPlain(const ::Vorago::Processor::PacksForTest& p,
                                                Steinberg::Vst::ParamID id) {
    namespace Vg = ::Vorago;
    const auto f = [](const std::atomic<float>& a) {
        return static_cast<double>(a.load(std::memory_order_relaxed));
    };
    const auto i = [](const std::atomic<int>& a) {
        return static_cast<double>(a.load(std::memory_order_relaxed));
    };
    if (id >= Vg::kMacroDarknessId && id <= Vg::kMacroMassId) {
        return f(Vg::macroField(p.macro, static_cast<int>(id - Vg::kMacroDarknessId)));
    }
    if (isInRange(id, Vg::kNoiseSlot0ModelId, Vg::kNoiseSlot3ModelId)) {
        return i(p.noise.model[id - Vg::kNoiseSlot0ModelId]);
    }
    if (isInRange(id, Vg::kNoiseSlot0TypeId, Vg::kNoiseSlot3TypeId)) {
        return i(p.noise.type[id - Vg::kNoiseSlot0TypeId]);
    }
    if (isInRange(id, Vg::kNoiseSlot0CombFundamentalId, Vg::kNoiseSlot3CombFundamentalId)) {
        return f(p.noise.combFundamentalHz[id - Vg::kNoiseSlot0CombFundamentalId]);
    }
    if (isInRange(id, Vg::kNoiseSlot0CombSpreadId, Vg::kNoiseSlot3CombSpreadId)) {
        return f(p.noise.combSpread[id - Vg::kNoiseSlot0CombSpreadId]);
    }
    if (isInRange(id, Vg::kNoiseSlot0CombFeedbackId, Vg::kNoiseSlot3CombFeedbackId)) {
        return f(p.noise.combFeedback[id - Vg::kNoiseSlot0CombFeedbackId]);
    }
    if (isInRange(id, Vg::kEcologyLoop0FilterModeId, Vg::kEcologyLoop5FilterModeId)) {
        return i(p.ecology.loopFilterMode[id - Vg::kEcologyLoop0FilterModeId]);
    }
    switch (id) {
        case Vg::kMasterGainId: return f(p.global.masterGain);
        case Vg::kPolyphonyId: return i(p.global.polyphony);
        case Vg::kSeedId: return i(p.global.seedIndex);
        case Vg::kOutputSaturationId: return f(p.global.outputSaturation);
        case Vg::kSustainPedalId: return f(p.global.sustainPedal);
        case Vg::kChannelPressureId: return f(p.global.channelPressure);
        case Vg::kCloudRichnessId: return f(p.cloud.richness);
        case Vg::kCloudTiltId: return f(p.cloud.tiltDb);
        case Vg::kCloudMutationId: return f(p.cloud.mutation);
        case Vg::kCloudInharmonicityId: return f(p.cloud.inharmonicity);
        case Vg::kCloudDriftDepthId: return f(p.cloud.driftCents);
        case Vg::kCloudStereoSpreadId: return f(p.cloud.stereoSpread);
        case Vg::kCloudSpectralGravityId: return f(p.cloud.spectralGravity);
        case Vg::kNoiseLevelId: return f(p.noise.levelDb);
        case Vg::kNoiseWakeId: return f(p.noise.wake);
        case Vg::kNoiseWanderRateId: return f(p.noise.wanderRateHz);
        case Vg::kResonanceGravityId: return f(p.resonance.gravity);
        case Vg::kResonanceMixId: return f(p.resonance.mix);
        case Vg::kResonanceWanderRateId: return f(p.resonance.wanderRateHz);
        case Vg::kResonanceAnchorModeId: return i(p.resonance.anchorMode);
        case Vg::kEcologyMixId: return f(p.ecology.mix);
        case Vg::kEcologyLoopGainId: return f(p.ecology.loopGain);
        case Vg::kSubLevelOffsetId: return f(p.sub.levelOffsetDb);
        case Vg::kSubTrackingId: return f(p.sub.tracking);
        case Vg::kSubDiv2LevelId: return f(p.sub.div2LevelDb);
        case Vg::kSubDiv4LevelId: return f(p.sub.div4LevelDb);
        case Vg::kSubFifthBelowLevelId: return f(p.sub.fifthBelowLevelDb);
        case Vg::kSmearAmountId: return f(p.smear.amount);
        case Vg::kSmearDecoherenceId: return f(p.smear.decoherence);
        case Vg::kSmearTiltId: return f(p.smear.tilt);
        case Vg::kEventsRateScaleId: return f(p.events.eventRateScale);
        case Vg::kEcosystemDepthId: return f(p.ecosystem.depth);
        case Vg::kBodyBlendId: return f(p.body.blend);
        case Vg::kBodyDampingId: return f(p.body.damping);
        case Vg::kBodyResonanceId: return f(p.body.resonance);
        case Vg::kBodyMixId: return f(p.body.mix);
        case Vg::kBodyMaterialAId: return i(p.body.materialA);
        case Vg::kBodyMaterialBId: return i(p.body.materialB);
        case Vg::kSpaceSizeId: return f(p.space.size);
        case Vg::kSpaceDarknessId: return f(p.space.darkness);
        case Vg::kSpaceDecayId: return f(p.space.decaySeconds);
        case Vg::kSpaceFogId: return f(p.space.fog);
        case Vg::kSpaceDamperDepthId: return f(p.space.damperDepth);
        case Vg::kSpaceMixId: return f(p.space.mix);
        case Vg::kSpaceWidthId: return f(p.space.width);
        case Vg::kSpaceDensityId: return f(p.space.density);
        case Vg::kSpaceDimensionalityId: return f(p.space.dimensionality);
        case Vg::kSpaceBreathId: return f(p.space.breath);
        case Vg::kSpaceEarlySizeId: return f(p.space.earlySizeMs);
        case Vg::kSpaceEarlyLevelId: return f(p.space.earlyLevel);
        case Vg::kSpaceEarlyAbsorptionId: return f(p.space.earlyAbsorption);
        case Vg::kSpaceEarlySendId: return f(p.space.earlySend);
        case Vg::kSpaceDamperRateId: return f(p.space.damperRate);
        case Vg::kSpaceFreezeId: return i(p.space.freeze);
        case Vg::kEnvelopeModeId: return i(p.envelope.mode);
        case Vg::kEnvelopeStage0TimeId: return f(p.envelope.stage0TimeMs);
        case Vg::kEnvelopeStage1TimeId: return f(p.envelope.stage1TimeMs);
        case Vg::kEnvelopeStage2TimeId: return f(p.envelope.stage2TimeMs);
        case Vg::kEnvelopeStage3TimeId: return f(p.envelope.stage3TimeMs);
        case Vg::kEnvelopeReleaseId: return f(p.envelope.releaseMs);
        case Vg::kEnvelopeGrowthDurationId: return f(p.envelope.growthDurationSeconds);
        case Vg::kBloomDepthId: return f(p.bloom.depth);
        case Vg::kBloomSpawnRateId: return f(p.bloom.spawnRateHz);
        case Vg::kGhostPeakLevelId: return f(p.ghost.peakLevel);
        case Vg::kGhostBlurId: return f(p.ghost.blur);
        case Vg::kGhostReverseProbabilityId: return f(p.ghost.reverseProbability);
        case Vg::kGhostEventTriggersId: return i(p.ghost.eventTriggers);
        case Vg::kLifeBreathingDepthId: return f(p.life.breathingDepth);
        case Vg::kLifeBreathingIrregularityId: return f(p.life.breathingIrregularity);
        case Vg::kLifeTidalDepthId: return f(p.life.tidalDepth);
        default: return std::nullopt;
    }
}

/// SC-003: `n` denormalized by the owning pack's handler, on packs no Processor
/// touched. The pack is first driven to the opposite end of the range, so a
/// handler that ignores the ID cannot pass on the constructor default.
[[nodiscard]] double packDenormalized(Steinberg::Vst::ParamID id, double n) {
    auto packs = std::make_unique<LocalPacks>();
    storeViaPack(*packs, id, (n < 0.5) ? 1.0 : 0.0);
    storeViaPack(*packs, id, n);
    const std::optional<double> v = storedPlain(packs->view(), id);
    REQUIRE(v.has_value());
    return *v;
}

[[nodiscard]] bool isDiscreteRow(const VoragoTest::ExpectedParamRow& row) noexcept {
    return row.taper == ::Vorago::Taper::Discrete;
}

[[nodiscard]] int discreteIndex(double plain) noexcept {
    return static_cast<int>(std::lround(plain));
}

/// The VoragoVoiceParams field behind a VP ID: the float, or the enum's value
/// (noise type: its index in kNoiseTypeByIndex, -1 if absent).
[[nodiscard]] double vpStructValue(const Krate::DSP::VoragoVoiceParams& p,
                                   Steinberg::Vst::ParamID id) {
    namespace Vg = ::Vorago;
    if (id == Vg::kCloudStereoSpreadId) {
        return p.stereoSpread;
    }
    if (id == Vg::kCloudSpectralGravityId) {
        return p.cloudSpectralGravity;
    }
    if (isInRange(id, Vg::kNoiseSlot0ModelId, Vg::kNoiseSlot3ModelId)) {
        return static_cast<int>(p.noiseModel[id - Vg::kNoiseSlot0ModelId]);
    }
    if (isInRange(id, Vg::kNoiseSlot0TypeId, Vg::kNoiseSlot3TypeId)) {
        const Krate::DSP::NoiseType t = p.noiseType[id - Vg::kNoiseSlot0TypeId];
        for (std::size_t k = 0; k < Vg::kNoiseTypeByIndex.size(); ++k) {
            if (Vg::kNoiseTypeByIndex[k] == t) {
                return static_cast<double>(k);
            }
        }
        return -1.0;
    }
    if (isInRange(id, Vg::kNoiseSlot0CombFundamentalId, Vg::kNoiseSlot3CombFundamentalId)) {
        return p.noiseCombFundamentalHz[id - Vg::kNoiseSlot0CombFundamentalId];
    }
    if (isInRange(id, Vg::kNoiseSlot0CombSpreadId, Vg::kNoiseSlot3CombSpreadId)) {
        return p.noiseCombSpread[id - Vg::kNoiseSlot0CombSpreadId];
    }
    if (isInRange(id, Vg::kNoiseSlot0CombFeedbackId, Vg::kNoiseSlot3CombFeedbackId)) {
        return p.noiseCombFeedback[id - Vg::kNoiseSlot0CombFeedbackId];
    }
    if (id == Vg::kResonanceAnchorModeId) {
        return static_cast<int>(p.resonanceAnchorMode);
    }
    if (isInRange(id, Vg::kEcologyLoop0FilterModeId, Vg::kEcologyLoop5FilterModeId)) {
        return static_cast<int>(p.ecologyLoopFilterMode[id - Vg::kEcologyLoop0FilterModeId]);
    }
    if (id == Vg::kBodyMaterialAId) {
        return static_cast<int>(p.bodyMaterialA);
    }
    if (id == Vg::kBodyMaterialBId) {
        return static_cast<int>(p.bodyMaterialB);
    }
    FAIL("VP ID without a VoragoVoiceParams field: " << id);
    return 0.0;
}

[[nodiscard]] float macroValueOf(const Krate::DSP::VoragoMacroValues& m, int k) noexcept {
    switch (k) {
        case 0: return m.darkness;
        case 1: return m.age;
        case 2: return m.density;
        case 3: return m.movement;
        case 4: return m.gravity;
        case 5: return m.entropy;
        case 6: return m.pressure;
        case 7: return m.weight;
        case 8: return m.fog;
        case 9: return m.life;
        case 10: return m.depth;
        case 11: return m.mass;
        default: return -1.0f;
    }
}

/// SC-003's Direct probe, built without a Processor: the §6.1 engine with every
/// slot's model set to Direct (types left at VoragoVoiceParams{}), six notes on
/// polyphony 6 so every slot processes, 4 800 samples so the model duck
/// (noise_organism.h:460-462) has settled.
[[nodiscard]] std::unique_ptr<VoragoEngine> makeDirectProbeEngine() {
    auto e = std::make_unique<VoragoEngine>();
    e->setSeed(::Vorago::kEngineSeed);
    e->prepare(kSampleRate, ::Vorago::makeVoragoEngineConfig(::Vorago::kMaxBlockSamples));
    e->setPolyphony(VoragoEngine::kMaxVoices);
    Krate::DSP::VoragoVoiceParams p{};
    p.noiseModel.fill(Krate::DSP::NoiseOrganismModel::Direct);
    e->applyVoiceParams(p);
    for (const Steinberg::int16 note : kSixHeldNotes) {
        e->noteOn(static_cast<std::uint8_t>(note), 100);
    }
    const VoragoMacroMatrix matrix{};
    std::vector<float> l(kBlock, 0.0f);
    std::vector<float> r(kBlock, 0.0f);
    for (std::size_t done = 0; done < kMbReadbackSamples; done += kBlock) {
        const std::size_t n = std::min(kBlock, kMbReadbackSamples - done);
        matrix.apply(*e);
        e->processStereoBlock(l.data(), r.data(), n);
        e->processOutputStage(l.data(), r.data(), n);
    }
    return e;
}

/// One persisted ID's non-default probe: the normalized value sent and the
/// plain value it denormalizes to (list index for discrete rows).
struct ProbeValue {
    Steinberg::Vst::ParamID id;
    double normalized;
    double plain;
};

/// SurvivesReprepare / SC-009 model choice: Direct (0) on slots 0, 1 and 3 so
/// their type rows are observable through the effective type; slot 2 (Direct
/// by default) moves to FilteredWind (1). Every entry differs from the default
/// {1, 2, 0, 3} (noise_params.h:83).
constexpr std::array<int, 4> kProbeNoiseModels = {0, 0, 1, 0};
constexpr int kProbeSeedIndex = 7;

/// Every persisted ID (106: all but sustain 4 and pressure 5) at the per-ID
/// non-default value of the MB / VP / ENG / Cavern sections; macros and master
/// gain at 0.8 of the range.
[[nodiscard]] std::vector<ProbeValue> surfaceProbe() {
    namespace Vg = ::Vorago;
    std::vector<ProbeValue> out;
    for (const VoragoTest::ExpectedParamRow& row : VoragoTest::kExpectedParams) {
        const Steinberg::Vst::ParamID id = row.id;
        if (id == Vg::kSustainPedalId || id == Vg::kChannelPressureId) {
            continue;  // FR-045: never persisted
        }
        const int count = static_cast<int>(row.stepCount) + 1;
        const std::optional<Vg::Route> route = Vg::routeOf(id);
        REQUIRE(route.has_value());

        double n = 0.0;
        if (id == Vg::kPolyphonyId) {
            n = 1.0;  // 6 voices
        } else if (id == Vg::kSeedId) {
            n = Vg::indexToNormalized(kProbeSeedIndex, count);
        } else if (isInRange(id, Vg::kNoiseSlot0ModelId, Vg::kNoiseSlot3ModelId)) {
            n = Vg::indexToNormalized(kProbeNoiseModels[id - Vg::kNoiseSlot0ModelId], count);
        } else if (*route == Vg::Route::VP) {
            n = vpProbeFor(row).normalized;
        } else if (id == Vg::kEnvelopeModeId || id == Vg::kGhostEventTriggersId ||
                   id == Vg::kSpaceFreezeId) {
            n = Vg::indexToNormalized(1, count);  // Growth / On / On
        } else if (id == Vg::kGhostReverseProbabilityId) {
            n = 0.6;
        } else {
            REQUIRE_FALSE(isDiscreteRow(row));
            n = (std::fabs(row.defaultNormalized - 0.8) < 1.0e-6) ? 0.2 : 0.8;
        }
        const double plain = isDiscreteRow(row)
            ? static_cast<double>(Vg::indexFromNormalized(n, count))
            : plainAt(row, n);
        const double defaultPlain = isDiscreteRow(row)
            ? static_cast<double>(Vg::indexFromNormalized(row.defaultNormalized, count))
            : plainAt(row, row.defaultNormalized);
        INFO("probe ID " << id << " n=" << n << " plain=" << plain << " default=" << defaultPlain);
        REQUIRE_FALSE(withinRelative(static_cast<float>(plain),
                                     static_cast<float>(defaultPlain)));  // non-vacuity
        out.push_back(ProbeValue{.id=id, .normalized=n, .plain=plain});
    }
    return out;
}

[[nodiscard]] double probePlain(const std::vector<ProbeValue>& probe, Steinberg::Vst::ParamID id) {
    for (const ProbeValue& p : probe) {
        if (p.id == id) {
            return p.plain;
        }
    }
    FAIL("no probe value for ID " << id);
    return 0.0;
}

/// A standalone engine at `sr` driven by a matrix carrying the plugin's own
/// bases and macros, with the six notes held for `samples` - the reference for
/// the matrix-driven getters when the macros are not neutral.
[[nodiscard]] std::unique_ptr<VoragoEngine> makeMatrixMirrorEngine(
    const VoragoMacroMatrix& pluginMatrix, std::size_t polyphony, double sr,
    std::size_t samples) {
    VoragoMacroMatrix m{};
    for (std::size_t t = 0; t < VoragoMacroMatrix::kNumTargets; ++t) {
        const auto target = static_cast<VoragoMacroTarget>(t);
        m.setTargetBase(target, pluginMatrix.getTargetBase(target));
    }
    m.setMacros(pluginMatrix.getMacros());

    auto e = std::make_unique<VoragoEngine>();
    e->setSeed(::Vorago::kEngineSeed);
    e->prepare(sr, ::Vorago::makeVoragoEngineConfig(::Vorago::kMaxBlockSamples));
    e->setPolyphony(polyphony);
    for (const Steinberg::int16 note : kSixHeldNotes) {
        e->noteOn(static_cast<std::uint8_t>(note), 100);
    }
    std::vector<float> l(kBlock, 0.0f);
    std::vector<float> r(kBlock, 0.0f);
    for (std::size_t done = 0; done < samples; done += kBlock) {
        const std::size_t n = std::min(kBlock, samples - done);
        m.apply(*e);
        e->processStereoBlock(l.data(), r.data(), n);
        e->processOutputStage(l.data(), r.data(), n);
    }
    return e;
}

/// The FULL read-back of a surfaceProbe() (SC-004 re-prepare arm; SC-009 in
/// T042 reuses it): every route's destination on the processor's chain after
/// `renderedSamples` at `sr` with the six notes held.
///
/// Not observable, so not read back here: the eight CV floats (1107-1114) -
/// CavernVerb exposes no getter for them (cavern_verb.h:770-822); the SECTION
/// "Cavern" proves them through the render instead. Freeze IS read back.
void requireSurfaceReadback(const ::Vorago::Processor& proc, const std::vector<ProbeValue>& probe,
                            double sr, std::size_t renderedSamples) {
    namespace Vg = ::Vorago;
    using Krate::DSP::VoragoVoice;
    const VoragoEngine* engine = proc.engineForTest();
    REQUIRE(engine != nullptr);
    const Krate::DSP::CavernVerb* cavern = proc.cavernForTest();
    REQUIRE(cavern != nullptr);
    const VoragoMacroMatrix& matrix = proc.macrosForTest();

    // ---- Local / global ENG ----
    INFO("master gain " << proc.masterGainValueForTest());
    REQUIRE(withinRelative(proc.masterGainValueForTest(),
                           static_cast<float>(probePlain(probe, Vg::kMasterGainId))));
    const int polyIndex = discreteIndex(probePlain(probe, Vg::kPolyphonyId));
    REQUIRE(engine->getPolyphony() == static_cast<std::size_t>(polyIndex + 1));
    const int seedIndex = discreteIndex(probePlain(probe, Vg::kSeedId));
    REQUIRE(engine->getSeed() == Vg::kVoragoSeedValues[static_cast<std::size_t>(seedIndex)]);

    // ---- MAC ----
    for (int k = 0; std::cmp_less(k, VoragoMacroMatrix::kNumMacros); ++k) {
        const auto id = static_cast<Steinberg::Vst::ParamID>(Vg::kMacroDarknessId + k);
        INFO("macro ID " << id);
        REQUIRE(withinRelative(matrix.getMacro(static_cast<VoragoMacro>(k)),
                               static_cast<float>(probePlain(probe, id))));
    }

    // ---- MB: bases, then the matrix-driven getters vs a mirror engine ----
    const auto mirror = makeMatrixMirrorEngine(matrix, engine->getPolyphony(), sr, renderedSamples);
    for (const Vg::MbRouteEntry& route : Vg::kMbRoutes) {
        INFO("MB ID " << route.id);
        const auto want = static_cast<float>(probePlain(probe, route.id));
        INFO("getTargetBase=" << matrix.getTargetBase(route.target) << " want=" << want);
        REQUIRE(withinRelative(matrix.getTargetBase(route.target), want));
        if (isCavernTarget(route.target)) {
            continue;
        }
        if (isEngineTarget(route.target)) {
            REQUIRE(withinRelative(readTarget(*engine, route.target, 0),
                                   readTarget(*mirror, route.target, 0)));
        } else {
            for (std::size_t i = 0; i < engine->getPolyphony(); ++i) {
                INFO("voice " << i << " got=" << readTarget(*engine, route.target, i)
                              << " mirror=" << readTarget(*mirror, route.target, i));
                REQUIRE(withinRelative(readTarget(*engine, route.target, i),
                                       readTarget(*mirror, route.target, i)));
            }
        }
    }

    // ---- VP: every slot < kMaxVoices ----
    std::size_t typeRowsSeen = 0;
    for (const Vg::ParamRouteEntry& entry : Vg::kParamRoutes) {
        if (entry.route != Vg::Route::VP) {
            continue;
        }
        const double plain = probePlain(probe, entry.id);
        const bool isTypeRow = isInRange(entry.id, Vg::kNoiseSlot0TypeId, Vg::kNoiseSlot3TypeId);
        for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
            INFO("VP ID " << entry.id << " voice " << i);
            const VoragoVoice& v = engine->getVoice(i);
            if (isTypeRow) {
                // The effective type is only the requested one on a Direct slot
                // (noise_organism.h:1225-1240); the probe makes slots 0, 1, 3 Direct.
                const std::size_t slot = entry.id - Vg::kNoiseSlot0TypeId;
                if (kProbeNoiseModels[slot] != 0) {
                    continue;
                }
                ++typeRowsSeen;
            }
            requireVpReadback(v, entry.id, plain);
        }
    }
    REQUIRE(typeRowsSeen == 3u * VoragoEngine::kMaxVoices);

    // ---- ENG: envelope (slot-0 getters and every slot), sub tones, ghost ----
    const auto mode = static_cast<VoragoVoice::EnvelopeMode>(
        discreteIndex(probePlain(probe, Vg::kEnvelopeModeId)));
    constexpr std::array<Steinberg::Vst::ParamID, 4> kStageIds = {
        Vg::kEnvelopeStage0TimeId, Vg::kEnvelopeStage1TimeId, Vg::kEnvelopeStage2TimeId,
        Vg::kEnvelopeStage3TimeId};
    const auto releaseMs = static_cast<float>(probePlain(probe, Vg::kEnvelopeReleaseId));
    const auto growthS = static_cast<float>(probePlain(probe, Vg::kEnvelopeGrowthDurationId));
    REQUIRE(engine->getEnvelopeMode() == mode);
    for (std::size_t st = 0; st < kStageIds.size(); ++st) {
        INFO("stage " << st);
        REQUIRE(withinRelative(engine->getEnvelopeStageTimeMs(static_cast<int>(st)),
                               static_cast<float>(probePlain(probe, kStageIds[st]))));
    }
    REQUIRE(withinRelative(engine->getEnvelopeReleaseMs(), releaseMs));
    REQUIRE(withinRelative(engine->getGrowthDurationSeconds(), growthS));
    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
        INFO("voice " << i);
        const VoragoVoice& v = engine->getVoice(i);
        REQUIRE(v.getEnvelopeMode() == mode);
        for (std::size_t st = 0; st < kStageIds.size(); ++st) {
            INFO("stage " << st);
            REQUIRE(withinRelative(v.getEnvelopeStageTimeMs(static_cast<int>(st)),
                                   static_cast<float>(probePlain(probe, kStageIds[st]))));
        }
        REQUIRE(withinRelative(v.getEnvelopeReleaseMs(), releaseMs));
        REQUIRE(withinRelative(v.getGrowthDurationSeconds(), growthS));
    }
    constexpr std::array<Steinberg::Vst::ParamID, 3> kSubToneIds = {
        Vg::kSubDiv2LevelId, Vg::kSubDiv4LevelId, Vg::kSubFifthBelowLevelId};
    for (std::size_t t = 0; t < kSubToneIds.size(); ++t) {
        INFO("tone " << t);
        REQUIRE(withinRelative(engine->getSubToneLevelDb(t),
                               static_cast<float>(probePlain(probe, kSubToneIds[t]))));
    }
    REQUIRE(withinRelative(engine->getGhostReverseProbability(),
                           static_cast<float>(probePlain(probe, Vg::kGhostReverseProbabilityId))));
    REQUIRE(engine->getGhostEventTriggers() ==
            (discreteIndex(probePlain(probe, Vg::kGhostEventTriggersId)) != 0));

    // ---- CV: freeze ----
    REQUIRE(cavern->isFrozen() == (discreteIndex(probePlain(probe, Vg::kSpaceFreezeId)) != 0));
}

}  // namespace

// SC-004: every route reaches the chain. T037 fills the MB section; the VP, ENG
// and Cavern sections follow (T038-T040).
TEST_CASE("Vorago_EveryRouteReachesTheChain", "[vorago][integration]") {
    SECTION("MB") {
        constexpr std::array<Steinberg::int16, 6> kHeldNotes = {36, 43, 48, 55, 60, 67};
        std::size_t checked = 0;

        for (const ::Vorago::MbRouteEntry& route : ::Vorago::kMbRoutes) {
            if (isCavernTarget(route.target)) {
                continue;  // MB-Cavern rows: SECTION "Cavern" (T040)
            }
            ++checked;
            INFO("MB ID " << route.id);

            const VoragoTest::ExpectedParamRow* row = expectedRowFor(route.id);
            REQUIRE(row != nullptr);
            REQUIRE(row->taper != ::Vorago::Taper::Discrete);

            // 0.8 of the plain range along the taper. Where 0.8 IS the default
            // (Ecology Loop Gain: 0.72 of [0, 0.9]) the change would be a no-op
            // and prove nothing, so that row moves to 0.2 instead.
            const double n = (std::fabs(row->defaultNormalized - 0.8) < 1.0e-6) ? 0.2 : 0.8;
            const auto want = static_cast<float>(plainAt(*row, n));
            const auto defaultPlain = static_cast<float>(plainAt(*row, row->defaultNormalized));
            REQUIRE_FALSE(withinRelative(want, defaultPlain));  // non-vacuity

            VoragoTest::ProcessorFixture fx;
            fx.prepare(kSampleRate, 2048);
            fx.reserveCapture(kBlock + kMbReadbackSamples);

            // Block 0: polyphony 6 and six held notes, so every slot sounds.
            Krate::Test::ParameterChanges setup;
            setup.addChange(::Vorago::kPolyphonyId, 1.0);
            Krate::Test::EventList notes;
            for (const Steinberg::int16 p : kHeldNotes) {
                notes.addNoteOn(p, kVelocity100, 0);
            }
            REQUIRE(fx.processBlock(kBlock, &notes, &setup) == Steinberg::kResultOk);

            // The one change, then 4 800 samples.
            Krate::Test::ParameterChanges change;
            change.addChange(route.id, n);
            std::size_t rendered = 0;
            while (rendered < kMbReadbackSamples) {
                const std::size_t len = std::min(kBlock, kMbReadbackSamples - rendered);
                REQUIRE(fx.processBlock(len, nullptr, (rendered == 0) ? &change : nullptr) ==
                        Steinberg::kResultOk);
                rendered += len;
            }

            const VoragoEngine* engine = fx.proc->engineForTest();
            REQUIRE(engine != nullptr);
            REQUIRE(engine->getPolyphony() == VoragoEngine::kMaxVoices);

            const float base = fx.proc->macrosForTest().getTargetBase(route.target);
            INFO("getTargetBase=" << base << " want=" << want);
            REQUIRE(withinRelative(base, want));

            if (isEngineTarget(route.target)) {
                const float got = readTarget(*engine, route.target, 0);
                INFO("engine getter=" << got << " want=" << want);
                REQUIRE(withinRelative(got, want));
            } else {
                for (std::size_t i = 0; i < engine->getPolyphony(); ++i) {
                    const float got = readTarget(*engine, route.target, i);
                    INFO("voice " << i << " getter=" << got << " want=" << want);
                    REQUIRE(withinRelative(got, want));
                }
            }
        }
        REQUIRE(checked == 32u);
    }

    SECTION("VP") {
        std::size_t checked = 0;

        for (const ::Vorago::ParamRouteEntry& entry : ::Vorago::kParamRoutes) {
            if (entry.route != ::Vorago::Route::VP) {
                continue;
            }
            ++checked;
            const Steinberg::Vst::ParamID id = entry.id;
            INFO("VP ID " << id);

            const VoragoTest::ExpectedParamRow* row = expectedRowFor(id);
            REQUIRE(row != nullptr);
            const VpProbe probe = vpProbeFor(*row);
            const double defaultPlain = (row->taper == ::Vorago::Taper::Discrete)
                ? std::round(row->defaultNormalized * row->stepCount)
                : plainAt(*row, row->defaultNormalized);
            INFO("normalized=" << probe.normalized << " plain=" << probe.plain
                               << " default=" << defaultPlain);
            REQUIRE_FALSE(withinRelative(static_cast<float>(probe.plain),
                                         static_cast<float>(defaultPlain)));  // non-vacuity

            VoragoTest::ProcessorFixture fx;
            fx.prepare(kSampleRate, 2048);

            Krate::Test::ParameterChanges change;
            change.addChange(id, probe.normalized);
            if (id >= ::Vorago::kNoiseSlot0TypeId && id <= ::Vorago::kNoiseSlot3TypeId) {
                // Direct probe: the type is only effective on a Direct slot.
                change.addChange(::Vorago::kNoiseSlot0ModelId + (id - ::Vorago::kNoiseSlot0TypeId),
                                 0.0);
            }
            renderSixVoicesThenChange(fx, &change, kMbReadbackSamples);

            const VoragoEngine* engine = fx.proc->engineForTest();
            REQUIRE(engine != nullptr);
            REQUIRE(engine->getPolyphony() == VoragoEngine::kMaxVoices);

            // Every slot, not just the sounding ones (applyVoiceParams bound).
            for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
                INFO("voice " << i);
                requireVpReadback(engine->getVoice(i), id, probe.plain);
            }
        }
        REQUIRE(checked == 31u);
    }

    // T039 (FR-023): envelope, sub tones, ghost reverse / triggers and the seed
    // reach the engine through the direct ENG setters.
    SECTION("ENG") {
        using Krate::DSP::VoragoVoice;
        namespace V = ::Vorago;

        // Continuous rows at 0.8 normalized along the checked-in taper.
        const auto plainAt08 = [](Steinberg::Vst::ParamID id) {
            const VoragoTest::ExpectedParamRow* row = expectedRowFor(id);
            REQUIRE(row != nullptr);
            REQUIRE(row->taper != V::Taper::Discrete);
            return static_cast<float>(plainAt(*row, 0.8));
        };
        constexpr std::array<Steinberg::Vst::ParamID, 4> kStageIds = {
            V::kEnvelopeStage0TimeId, V::kEnvelopeStage1TimeId, V::kEnvelopeStage2TimeId,
            V::kEnvelopeStage3TimeId};
        constexpr std::array<Steinberg::Vst::ParamID, 3> kSubToneIds = {
            V::kSubDiv2LevelId, V::kSubDiv4LevelId, V::kSubFifthBelowLevelId};
        constexpr int kSeedProbeIndex = 7;

        std::array<float, 4> wantStageMs{};
        for (std::size_t st = 0; st < kStageIds.size(); ++st) {
            wantStageMs[st] = plainAt08(kStageIds[st]);
            REQUIRE_FALSE(withinRelative(wantStageMs[st], VoragoVoice::kDefaultStageTimesMs[st]));
        }
        const float wantReleaseMs = plainAt08(V::kEnvelopeReleaseId);
        REQUIRE_FALSE(withinRelative(wantReleaseMs, VoragoVoice::kDefaultReleaseMs));
        const float wantGrowthS = plainAt08(V::kEnvelopeGrowthDurationId);
        REQUIRE_FALSE(withinRelative(wantGrowthS, VoragoVoice::kDefaultGrowthDurationSeconds));
        std::array<float, 3> wantSubDb{};
        for (std::size_t t = 0; t < kSubToneIds.size(); ++t) {
            wantSubDb[t] = plainAt08(kSubToneIds[t]);
            REQUIRE_FALSE(withinRelative(wantSubDb[t],
                                         Krate::DSP::SubharmonicEngine::kDefaultToneLevelDb[t]));
        }
        REQUIRE(V::kVoragoSeedValues[static_cast<std::size_t>(kSeedProbeIndex)] !=
                V::kVoragoSeedValues[0]);

        Krate::Test::ParameterChanges change;
        change.addChange(V::kEnvelopeModeId, V::indexToNormalized(1, V::kNumEnvelopeModes));  // Growth
        for (const Steinberg::Vst::ParamID id : kStageIds) {
            change.addChange(id, 0.8);
        }
        change.addChange(V::kEnvelopeReleaseId, 0.8);
        change.addChange(V::kEnvelopeGrowthDurationId, 0.8);
        for (const Steinberg::Vst::ParamID id : kSubToneIds) {
            change.addChange(id, 0.8);
        }
        change.addChange(V::kGhostReverseProbabilityId, 0.6);
        change.addChange(V::kGhostEventTriggersId,
                         V::indexToNormalized(1, V::kGhostEventTriggersCount));  // On
        change.addChange(V::kSeedId, V::indexToNormalized(kSeedProbeIndex, V::kNumSeeds));

        // Every ENG route ID is exercised here (polyphony by the helper's block 0).
        constexpr std::array<Steinberg::Vst::ParamID, 14> kCovered = {
            V::kPolyphonyId,          V::kSeedId,
            V::kSubDiv2LevelId,       V::kSubDiv4LevelId,
            V::kSubFifthBelowLevelId, V::kEnvelopeModeId,
            V::kEnvelopeStage0TimeId, V::kEnvelopeStage1TimeId,
            V::kEnvelopeStage2TimeId, V::kEnvelopeStage3TimeId,
            V::kEnvelopeReleaseId,    V::kEnvelopeGrowthDurationId,
            V::kGhostReverseProbabilityId, V::kGhostEventTriggersId};
        std::size_t engRoutes = 0;
        for (const V::ParamRouteEntry& entry : V::kParamRoutes) {
            if (entry.route != V::Route::ENG) {
                continue;
            }
            ++engRoutes;
            INFO("ENG ID " << entry.id);
            REQUIRE(std::find(kCovered.begin(), kCovered.end(), entry.id) != kCovered.end());
        }
        REQUIRE(engRoutes == kCovered.size());

        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, 2048);
        renderSixVoicesThenChange(fx, &change, kMbReadbackSamples);

        const VoragoEngine* engine = fx.proc->engineForTest();
        REQUIRE(engine != nullptr);
        REQUIRE(engine->getPolyphony() == VoragoEngine::kMaxVoices);

        // Envelope: slot-0 getters AND every slot (the fan-out bound is kMaxVoices).
        REQUIRE(engine->getEnvelopeMode() == VoragoVoice::EnvelopeMode::Growth);
        for (std::size_t st = 0; st < kStageIds.size(); ++st) {
            const float got = engine->getEnvelopeStageTimeMs(static_cast<int>(st));
            INFO("stage " << st << " got=" << got << " want=" << wantStageMs[st]);
            REQUIRE(withinRelative(got, wantStageMs[st]));
        }
        REQUIRE(withinRelative(engine->getEnvelopeReleaseMs(), wantReleaseMs));
        REQUIRE(withinRelative(engine->getGrowthDurationSeconds(), wantGrowthS));
        for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
            INFO("voice " << i);
            const VoragoVoice& v = engine->getVoice(i);
            REQUIRE(v.getEnvelopeMode() == VoragoVoice::EnvelopeMode::Growth);
            for (std::size_t st = 0; st < kStageIds.size(); ++st) {
                INFO("stage " << st);
                REQUIRE(withinRelative(v.getEnvelopeStageTimeMs(static_cast<int>(st)),
                                       wantStageMs[st]));
            }
            REQUIRE(withinRelative(v.getEnvelopeReleaseMs(), wantReleaseMs));
            REQUIRE(withinRelative(v.getGrowthDurationSeconds(), wantGrowthS));
        }

        // Sub tones: the owner level equals the parameter (matrix offset at 0 dB).
        REQUIRE(engine->getSubToneLevelOffsetDb() == 0.0f);
        for (std::size_t t = 0; t < kSubToneIds.size(); ++t) {
            const float got = engine->subharmonic().getToneLevelDb(t);
            INFO("tone " << t << " got=" << got << " want=" << wantSubDb[t]);
            REQUIRE(withinRelative(engine->getSubToneLevelDb(t), wantSubDb[t]));
            REQUIRE(withinRelative(got, wantSubDb[t]));
        }

        // Ghost.
        REQUIRE(engine->atmosphere().getGrainReverseProbability() == 0.6f);
        REQUIRE(engine->getGhostEventTriggers());

        // Seed.
        REQUIRE(engine->getSeed() ==
                V::kVoragoSeedValues[static_cast<std::size_t>(kSeedProbeIndex)]);
    }

    // T040: the 16 cavern IDs (1100-1115). Each is a 4 s render with one held
    // C2 from sample 0 and the change in block 0, against the reference chain
    // with the same cavern setter (CV) or matrix base (MB-cavern) applied by hand
    // before the first block.
    SECTION("Cavern") {
        namespace V = ::Vorago;
        const VoragoTest::RefChainOutput defaultRef =
            VoragoTest::renderPhase11ReferenceChain(fourSecondSpec());
        REQUIRE(defaultRef.L.size() == kFourSeconds);
        REQUIRE(defaultRef.R.size() == kFourSeconds);

        std::size_t checked = 0;
        std::size_t cvRows = 0;
        for (Steinberg::Vst::ParamID id = V::kSpaceSizeId; id <= V::kSpaceFreezeId; ++id) {
            ++checked;
            INFO("cavern ID " << id);

            const std::optional<V::Route> route = V::routeOf(id);
            REQUIRE(route.has_value());
            const std::optional<VoragoMacroTarget> mbTarget = mbTargetOf(id);
            if (*route == V::Route::MB) {
                REQUIRE(mbTarget.has_value());
                REQUIRE(isCavernTarget(*mbTarget));
            } else {
                REQUIRE(*route == V::Route::CV);
                REQUIRE_FALSE(mbTarget.has_value());
                ++cvRows;
            }

            const VoragoTest::ExpectedParamRow* row = expectedRowFor(id);
            REQUIRE(row != nullptr);
            const double n = cavernProbeNormalized(*row);
            const bool isFreeze = (id == V::kSpaceFreezeId);

            // The plain value the pack stores (what the processor pushes), checked
            // against the independent table and against the default (non-vacuity).
            float plain = 0.0f;
            if (isFreeze) {
                plain = static_cast<float>(V::indexFromNormalized(n, V::kSpaceFreezeCount));
                REQUIRE(plain == 1.0f);
            } else {
                plain = static_cast<float>(V::spaceFloatFromNormalized(id, n));
                REQUIRE(withinRelative(plain, static_cast<float>(plainAt(*row, n))));
                REQUIRE_FALSE(withinRelative(
                    plain, static_cast<float>(plainAt(*row, row->defaultNormalized))));
            }
            INFO("normalized=" << n << " plain=" << plain);

            // Plugin arm.
            VoragoTest::ProcessorFixture fx;
            fx.prepare(kSampleRate, 2048);
            Krate::Test::ParameterChanges pc;
            pc.addChange(id, n);
            renderFourSeconds(fx, &pc);
            REQUIRE(fx.capturedL.size() == kFourSeconds);
            REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedL)));
            REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedR)));

            // Reference arm: the same change applied by hand before block 0.
            const Steinberg::Vst::ParamID hookId = id;
            const float hookPlain = plain;
            const std::optional<VoragoMacroTarget> hookTarget = mbTarget;
            const VoragoTest::RefChainOutput ref = VoragoTest::renderPhase11ReferenceChain(
                fourSecondSpec(),
                [hookId, hookPlain, hookTarget](std::size_t blockIndex, Krate::DSP::VoragoEngine&,
                                                Krate::DSP::CavernVerb& cavern,
                                                VoragoMacroMatrix& m) noexcept {
                    if (blockIndex != 0) {
                        return;
                    }
                    if (hookTarget.has_value()) {
                        m.setTargetBase(*hookTarget, hookPlain);
                    } else {
                        applyCvSetterByHand(cavern, hookId, hookPlain);
                    }
                });
            const auto refL = std::span<const float>(ref.L);
            const auto refR = std::span<const float>(ref.R);

            // Precondition: the reference is audible past the latency.
            const double rmsL = VoragoTest::rmsOf(refL.subspan(kLatencySamples));
            const double rmsR = VoragoTest::rmsOf(refR.subspan(kLatencySamples));
            INFO("reference RMS [3072, end) L=" << rmsL << " R=" << rmsR);
            REQUIRE(rmsL >= 1.0e-4);
            REQUIRE(rmsR >= 1.0e-4);

            // Non-vacuity (spec B-5, 2026-09-25): the change is audible on the
            // cavern itself under a broadband stimulus. A single held C2 through
            // the whole chain cannot show it for the darker controls (Darkness,
            // Damper Depth/Rate, Early Absorption measured 3e-6..1.6e-4 RMS at
            // 30 s against a 4.6e-3 RMS reference), so two CavernVerbs prepared
            // like the plugin's are fed 4 s of identical seeded white noise, one
            // at the default and one with this ID's change applied the way the
            // processor applies it (matrix base for MB rows, setter for CV rows).
            // The bound is the spec's 1e-3 RMS, unchanged.
            {
                Krate::DSP::CavernVerb base;
                Krate::DSP::CavernVerb probe;
                base.prepare(kSampleRate, V::makeVoragoCavernConfig(V::kMaxBlockSamples));
                probe.prepare(kSampleRate, V::makeVoragoCavernConfig(V::kMaxBlockSamples));
                VoragoMacroMatrix baseMatrix;
                VoragoMacroMatrix probeMatrix;
                if (hookTarget.has_value()) {
                    probeMatrix.setTargetBase(*hookTarget, hookPlain);
                }
                V::applyCavernTargets(base, baseMatrix.computeCavernTargets());
                V::applyCavernTargets(probe, probeMatrix.computeCavernTargets());
                if (!hookTarget.has_value()) {
                    applyCvSetterByHand(probe, hookId, hookPlain);
                }
                std::mt19937 rng(12012u);
                std::vector<float> inL(kFourSeconds);
                std::vector<float> inR(kFourSeconds);
                for (std::size_t i = 0; i < kFourSeconds; ++i) {
                    // Portable uniform [-0.25, 0.25): mt19937's output sequence is
                    // specified, std::uniform_real_distribution's is not.
                    inL[i] = (static_cast<float>(rng() >> 8) / 16777216.0f - 0.5f) * 0.5f;
                    inR[i] = (static_cast<float>(rng() >> 8) / 16777216.0f - 0.5f) * 0.5f;
                }
                std::vector<float> outBaseL(kFourSeconds);
                std::vector<float> outBaseR(kFourSeconds);
                std::vector<float> outProbeL(kFourSeconds);
                std::vector<float> outProbeR(kFourSeconds);
                constexpr std::size_t kNvBlock = 512;
                for (std::size_t done = 0; done < kFourSeconds; done += kNvBlock) {
                    const std::size_t n2 = std::min(kNvBlock, kFourSeconds - done);
                    base.processStereoBlock(inL.data() + done, inR.data() + done,
                                            outBaseL.data() + done, outBaseR.data() + done, n2);
                    probe.processStereoBlock(inL.data() + done, inR.data() + done,
                                             outProbeL.data() + done, outProbeR.data() + done, n2);
                }
                const double nvL = VoragoTest::rmsDiff(std::span<const float>(outProbeL),
                                                       std::span<const float>(outBaseL),
                                                       kLatencySamples);
                const double nvR = VoragoTest::rmsDiff(std::span<const float>(outProbeR),
                                                       std::span<const float>(outBaseR),
                                                       kLatencySamples);
                INFO("component non-vacuity rmsDiff(probe cavern, default cavern) on white noise L="
                     << nvL << " R=" << nvR);
                REQUIRE(std::max(nvL, nvR) > 1.0e-3);
            }

            // The plugin renders the reference.
            const float diffL = VoragoTest::maxAbsDiff(std::span<const float>(fx.capturedL), refL);
            const float diffR = VoragoTest::maxAbsDiff(std::span<const float>(fx.capturedR), refR);
            INFO("maxAbsDiff L=" << diffL << " R=" << diffR);
            REQUIRE(diffL <= 1.0e-6f);
            REQUIRE(diffR <= 1.0e-6f);

            if (isFreeze) {
                const Krate::DSP::CavernVerb* cavern = fx.proc->cavernForTest();
                REQUIRE(cavern != nullptr);
                REQUIRE(cavern->isFrozen());
            }
        }
        REQUIRE(checked == 16u);
        REQUIRE(cvRows == 9u);
    }

    // T041 (FR-022, R-3): only a SECOND setupProcessing() exercises
    // pushAllSurfaces(Scope::Reprepared) - the first pushes VP regardless (the
    // generation tracker starts at the sentinel) - and it runs
    // VoragoVoice::prepare(), which resets every VP field and envelope value.
    // Without the VP / ENG / CV invalidation this read-back fails.
    SECTION("SurvivesReprepare") {
        const std::vector<ProbeValue> probe = surfaceProbe();
        REQUIRE(probe.size() == 106u);

        struct Rate {
            double sampleRate;
            std::size_t samples;  // >= 100 ms at that rate
        };
        constexpr std::array<Rate, 2> kRates = {{{.sampleRate=44100.0, .samples=4410u}, {.sampleRate=96000.0, .samples=9600u}}};

        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, 2048);
        fx.reserveCapture(kBlock + 4410u + 9600u);

        // Block 0: every persisted ID non-default, six notes.
        Krate::Test::ParameterChanges all;
        for (const ProbeValue& p : probe) {
            all.addChange(p.id, p.normalized);
        }
        Krate::Test::EventList notes;
        for (const Steinberg::int16 p : kSixHeldNotes) {
            notes.addNoteOn(p, kVelocity100, 0);
        }
        REQUIRE(fx.processBlock(kBlock, &notes, &all) == Steinberg::kResultOk);

        for (const Rate& rate : kRates) {
            INFO("re-prepared at " << rate.sampleRate << " Hz");
            REQUIRE(fx.proc->setActive(false) == Steinberg::kResultOk);
            fx.prepare(rate.sampleRate, 2048);  // setupProcessing + setActive(true)

            Krate::Test::EventList restrike;
            for (const Steinberg::int16 p : kSixHeldNotes) {
                restrike.addNoteOn(p, kVelocity100, 0);
            }
            std::size_t rendered = 0;
            while (rendered < rate.samples) {
                const std::size_t len = std::min(kBlock, rate.samples - rendered);
                REQUIRE(fx.processBlock(len, (rendered == 0) ? &restrike : nullptr) ==
                        Steinberg::kResultOk);
                rendered += len;
            }
            requireSurfaceReadback(*fx.proc, probe, rate.sampleRate, rate.samples);
        }
    }
}

// ==============================================================================
// T041 - SC-003, SC-023 (2), SC-017
// ==============================================================================

// SC-003: every registered default (controller getParameterInfo) denormalized by
// its pack equals a reference NO Phase 12 push has touched - the plugin's own
// prepared chain would read a wrong default back as itself (plan 6.3).
TEST_CASE("Vorago_RegisteredDefaultsMatchEngine", "[vorago][integration]") {
    namespace Vg = ::Vorago;
    using Krate::DSP::VoragoVoice;

    auto controller = Steinberg::owned(new Vg::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    const Steinberg::int32 paramCount = controller->getParameterCount();
    REQUIRE(paramCount == 108);
    const auto registeredDefault = [&controller, paramCount](Steinberg::Vst::ParamID id) {
        for (Steinberg::int32 k = 0; k < paramCount; ++k) {
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(controller->getParameterInfo(k, info) == Steinberg::kResultOk);
            if (info.id == id) {
                return info.defaultNormalizedValue;
            }
        }
        FAIL("ID not registered: " << id);
        return 0.0;
    };

    // References, all built WITHOUT a Processor.
    const VoragoMacroMatrix freshMatrix{};                         // literal bases
    const auto refEngine = makeMatrixDrivenEngine(freshMatrix);    // plan 6.1 engine
    const auto probeEngine = makeDirectProbeEngine();              // noise-type rows
    const Krate::DSP::VoragoVoiceParams vpDefaults{};
    const Krate::DSP::VoragoMacroValues macroDefaults{};
    auto refCavern = std::make_unique<Krate::DSP::CavernVerb>();
    refCavern->prepare(kSampleRate, Vg::makeVoragoCavernConfig(Vg::kMaxBlockSamples));
    REQUIRE(refEngine->getPolyphony() == VoragoEngine::kDefaultPolyphony);

    SECTION("RegisteredDefaults") {
        std::size_t rows = 0;

        // ---- MB (39) ----
        for (const Vg::MbRouteEntry& route : Vg::kMbRoutes) {
            ++rows;
            const auto plain = static_cast<float>(packDenormalized(route.id, registeredDefault(route.id)));
            INFO("MB ID " << route.id << " default plain " << plain);
            REQUIRE(withinRelative(plain, freshMatrix.getTargetBase(route.target)));
            if (isCavernTarget(route.target)) {
                continue;
            }
            if (isEngineTarget(route.target)) {
                REQUIRE(withinRelative(plain, readTarget(*refEngine, route.target, 0)));
            } else {
                for (std::size_t i = 0; i < refEngine->getPolyphony(); ++i) {
                    INFO("voice " << i);
                    REQUIRE(withinRelative(plain, readTarget(*refEngine, route.target, i)));
                }
            }
        }

        for (const Vg::ParamRouteEntry& entry : Vg::kParamRoutes) {
            const Steinberg::Vst::ParamID id = entry.id;
            const VoragoTest::ExpectedParamRow* row = expectedRowFor(id);
            REQUIRE(row != nullptr);
            INFO("ID " << id);
            switch (entry.route) {
                case Vg::Route::VP: {  // ---- VP (31) ----
                    ++rows;
                    const double plain = packDenormalized(id, registeredDefault(id));
                    const double ref = vpStructValue(vpDefaults, id);
                    INFO("default plain " << plain << " VoragoVoiceParams{} " << ref);
                    if (isDiscreteRow(*row)) {
                        REQUIRE(discreteIndex(plain) == discreteIndex(ref));
                    } else {
                        REQUIRE(withinRelative(static_cast<float>(plain), static_cast<float>(ref)));
                    }
                    if (isInRange(id, Vg::kNoiseSlot0TypeId, Vg::kNoiseSlot3TypeId)) {
                        // C-4: the literal Brown, and the Direct probe.
                        REQUIRE(Vg::kNoiseTypeByIndex[static_cast<std::size_t>(discreteIndex(plain))] ==
                                Krate::DSP::NoiseType::Brown);
                        for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
                            INFO("probe voice " << i);
                            requireVpReadback(probeEngine->getVoice(i), id, plain);
                        }
                    } else {
                        for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
                            INFO("voice " << i);
                            requireVpReadback(refEngine->getVoice(i), id, plain);
                        }
                    }
                    break;
                }
                case Vg::Route::ENG: {  // ---- ENG (14) ----
                    ++rows;
                    const double plain = packDenormalized(id, registeredDefault(id));
                    const auto plainF = static_cast<float>(plain);
                    INFO("default plain " << plain);
                    if (id == Vg::kPolyphonyId) {
                        REQUIRE(discreteIndex(plain) == 4);
                        REQUIRE(VoragoEngine::kDefaultPolyphony == 4u);
                    } else if (id == Vg::kSeedId) {
                        REQUIRE(discreteIndex(plain) == 0);
                        REQUIRE(Vg::kVoragoSeedValues[0] == 1u);
                        REQUIRE(Vg::kVoragoSeedValues[0] == Vg::kEngineSeed);
                    } else if (id == Vg::kSubDiv2LevelId || id == Vg::kSubDiv4LevelId ||
                               id == Vg::kSubFifthBelowLevelId) {
                        const std::size_t t = id - Vg::kSubDiv2LevelId;
                        REQUIRE(withinRelative(plainF,
                                               Krate::DSP::SubharmonicEngine::kDefaultToneLevelDb[t]));
                    } else if (id == Vg::kEnvelopeModeId) {
                        REQUIRE(static_cast<VoragoVoice::EnvelopeMode>(discreteIndex(plain)) ==
                                VoragoVoice::EnvelopeMode::Standard);
                    } else if (isInRange(id, Vg::kEnvelopeStage0TimeId, Vg::kEnvelopeStage3TimeId)) {
                        const std::size_t st = id - Vg::kEnvelopeStage0TimeId;
                        REQUIRE(withinRelative(plainF, VoragoVoice::kDefaultStageTimesMs[st]));
                    } else if (id == Vg::kEnvelopeReleaseId) {
                        REQUIRE(withinRelative(plainF, VoragoVoice::kDefaultReleaseMs));
                    } else if (id == Vg::kEnvelopeGrowthDurationId) {
                        REQUIRE(withinRelative(plainF, VoragoVoice::kDefaultGrowthDurationSeconds));
                    } else if (id == Vg::kGhostReverseProbabilityId) {
                        REQUIRE(plainF == 0.0f);
                    } else if (id == Vg::kGhostEventTriggersId) {
                        REQUIRE(discreteIndex(plain) == 0);  // off
                    } else {
                        FAIL("ENG ID without a reference: " << id);
                    }
                    break;
                }
                case Vg::Route::CV: {  // ---- CV: freeze only (1) ----
                    if (id != Vg::kSpaceFreezeId) {
                        break;
                    }
                    ++rows;
                    const double plain = packDenormalized(id, registeredDefault(id));
                    REQUIRE((discreteIndex(plain) != 0) == refCavern->isFrozen());
                    REQUIRE_FALSE(refCavern->isFrozen());
                    break;
                }
                case Vg::Route::MAC: {  // ---- MAC (12) ----
                    if (id < Vg::kMacroDarknessId || id > Vg::kMacroMassId) {
                        break;  // channel pressure: not a knob default
                    }
                    ++rows;
                    const auto plain = static_cast<float>(packDenormalized(id, registeredDefault(id)));
                    const float ref = macroValueOf(macroDefaults, static_cast<int>(id - Vg::kMacroDarknessId));
                    INFO("default plain " << plain << " VoragoMacroValues{} " << ref);
                    REQUIRE(withinRelative(plain, ref));
                    break;
                }
                case Vg::Route::MB:     // counted above, over kMbRoutes
                case Vg::Route::Local:  // master gain / sustain: not SC-003 rows
                    break;
            }
        }

        INFO("SC-003 rows: " << rows);
        REQUIRE(rows == 97u);
    }

    // Push integrity (not the default proof): the same references read back from
    // a processor after setupProcessing + one process().
    SECTION("PushIntegrity") {
        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, 2048);
        fx.reserveCapture(kBlock);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        const VoragoEngine* plugin = fx.proc->engineForTest();
        REQUIRE(plugin != nullptr);
        const VoragoMacroMatrix& matrix = fx.proc->macrosForTest();
        std::size_t rows = 0;

        // ---- MB (39) ----
        for (const Vg::MbRouteEntry& route : Vg::kMbRoutes) {
            ++rows;
            INFO("MB ID " << route.id);
            REQUIRE(withinRelative(matrix.getTargetBase(route.target),
                                   freshMatrix.getTargetBase(route.target)));
            if (isCavernTarget(route.target)) {
                continue;
            }
            if (isEngineTarget(route.target)) {
                REQUIRE(withinRelative(readTarget(*plugin, route.target, 0),
                                       readTarget(*refEngine, route.target, 0)));
            } else {
                REQUIRE(plugin->getPolyphony() == refEngine->getPolyphony());
                for (std::size_t i = 0; i < plugin->getPolyphony(); ++i) {
                    INFO("voice " << i);
                    REQUIRE(withinRelative(readTarget(*plugin, route.target, i),
                                           readTarget(*refEngine, route.target, i)));
                }
            }
        }

        const auto noisePacks = fx.proc->packsForTest();
        for (const Vg::ParamRouteEntry& entry : Vg::kParamRoutes) {
            const Steinberg::Vst::ParamID id = entry.id;
            INFO("ID " << id);
            if (entry.route == Vg::Route::VP) {  // ---- VP (31) ----
                ++rows;
                const double ref = vpStructValue(vpDefaults, id);
                if (isInRange(id, Vg::kNoiseSlot0TypeId, Vg::kNoiseSlot3TypeId)) {
                    const std::size_t slot = id - Vg::kNoiseSlot0TypeId;
                    const int stored = noisePacks.noise.type[slot].load(std::memory_order_relaxed);
                    REQUIRE(Vg::kNoiseTypeByIndex[static_cast<std::size_t>(stored)] ==
                            Krate::DSP::NoiseType::Brown);
                    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
                        const Krate::DSP::VoragoVoice& v = plugin->getVoice(i);
                        if (v.noise().getSourceModel(slot) == Krate::DSP::NoiseOrganismModel::Direct) {
                            INFO("Direct slot, voice " << i);
                            REQUIRE(v.noise().getSourceNoiseType(slot) == Krate::DSP::NoiseType::Brown);
                        }
                    }
                } else {
                    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
                        INFO("voice " << i);
                        requireVpReadback(plugin->getVoice(i), id, ref);
                    }
                }
            } else if (entry.route == Vg::Route::ENG) {  // ---- ENG (14) ----
                ++rows;
                if (id == Vg::kPolyphonyId) {
                    REQUIRE(plugin->getPolyphony() == 4u);
                } else if (id == Vg::kSeedId) {
                    REQUIRE(plugin->getSeed() == 1u);
                } else if (id == Vg::kSubDiv2LevelId || id == Vg::kSubDiv4LevelId ||
                           id == Vg::kSubFifthBelowLevelId) {
                    const std::size_t t = id - Vg::kSubDiv2LevelId;
                    REQUIRE(withinRelative(plugin->getSubToneLevelDb(t),
                                           Krate::DSP::SubharmonicEngine::kDefaultToneLevelDb[t]));
                } else if (id == Vg::kEnvelopeModeId) {
                    REQUIRE(plugin->getEnvelopeMode() == VoragoVoice::EnvelopeMode::Standard);
                } else if (isInRange(id, Vg::kEnvelopeStage0TimeId, Vg::kEnvelopeStage3TimeId)) {
                    const std::size_t st = id - Vg::kEnvelopeStage0TimeId;
                    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
                        INFO("voice " << i);
                        REQUIRE(withinRelative(
                            plugin->getVoice(i).getEnvelopeStageTimeMs(static_cast<int>(st)),
                            VoragoVoice::kDefaultStageTimesMs[st]));
                    }
                } else if (id == Vg::kEnvelopeReleaseId) {
                    REQUIRE(withinRelative(plugin->getEnvelopeReleaseMs(), VoragoVoice::kDefaultReleaseMs));
                } else if (id == Vg::kEnvelopeGrowthDurationId) {
                    REQUIRE(withinRelative(plugin->getGrowthDurationSeconds(),
                                           VoragoVoice::kDefaultGrowthDurationSeconds));
                } else if (id == Vg::kGhostReverseProbabilityId) {
                    REQUIRE(plugin->getGhostReverseProbability() == 0.0f);
                } else if (id == Vg::kGhostEventTriggersId) {
                    REQUIRE_FALSE(plugin->getGhostEventTriggers());
                } else {
                    FAIL("ENG ID without a reference: " << id);
                }
            } else if (entry.route == Vg::Route::CV && id == Vg::kSpaceFreezeId) {  // ---- CV (1) ----
                ++rows;
                const Krate::DSP::CavernVerb* cavern = fx.proc->cavernForTest();
                REQUIRE(cavern != nullptr);
                REQUIRE(cavern->isFrozen() == refCavern->isFrozen());
            } else if (entry.route == Vg::Route::MAC && id >= Vg::kMacroDarknessId &&
                       id <= Vg::kMacroMassId) {  // ---- MAC (12) ----
                ++rows;
                const int k = static_cast<int>(id - Vg::kMacroDarknessId);
                REQUIRE(matrix.getMacro(static_cast<VoragoMacro>(k)) == macroValueOf(macroDefaults, k));
            }
        }

        INFO("SC-003 push-integrity rows: " << rows);
        REQUIRE(rows == 97u);
    }
}

// SC-023 (2): a host that re-sends every parameter at its current normalized
// value every block changes nothing (trackers compare plain values; the VP
// generation bump re-broadcasts an idempotent applyVoiceParams).
TEST_CASE("Vorago_HostResendsEveryParameter", "[vorago][integration]") {
    constexpr std::size_t kTenSeconds = std::size_t{10} * 48000u;

    auto controller = Steinberg::owned(new ::Vorago::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(controller->getParameterCount() == 108);

    // The host's current value of every ID: its registered default, sent once by
    // both arms in block 0 (the initial sync), then every block by arm B.
    Krate::Test::ParameterChanges current;
    for (Steinberg::int32 k = 0; k < 108; ++k) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(controller->getParameterInfo(k, info) == Steinberg::kResultOk);
        current.addChange(info.id, info.defaultNormalizedValue);
    }
    REQUIRE(current.getParameterCount() == 108);

    VoragoTest::ProcessorFixture armA;
    VoragoTest::ProcessorFixture armB;
    armA.prepare(kSampleRate, 2048);
    armB.prepare(kSampleRate, 2048);
    armA.reserveCapture(kTenSeconds);
    armB.reserveCapture(kTenSeconds);

    Krate::Test::EventList ev;
    ev.addNoteOn(36, kVelocity100, 0);
    for (std::size_t start = 0; start < kTenSeconds; start += kBlock) {
        const std::size_t len = std::min(kBlock, kTenSeconds - start);
        const bool first = (start == 0);
        REQUIRE(armA.processBlock(len, first ? &ev : nullptr, first ? &current : nullptr) ==
                Steinberg::kResultOk);
        REQUIRE(armB.processBlock(len, first ? &ev : nullptr, &current) == Steinberg::kResultOk);
    }
    REQUIRE(armA.capturedL.size() == kTenSeconds);
    REQUIRE(armB.capturedL.size() == kTenSeconds);

    const auto aL = std::span<const float>(armA.capturedL);
    const auto aR = std::span<const float>(armA.capturedR);
    REQUIRE(VoragoTest::allFinite(aL));
    REQUIRE(VoragoTest::allFinite(aR));
    REQUIRE(VoragoTest::allFinite(std::span<const float>(armB.capturedL)));
    REQUIRE(VoragoTest::allFinite(std::span<const float>(armB.capturedR)));
    // Non-vacuity: arm A is audible past the latency.
    REQUIRE(VoragoTest::peakOf(aL.subspan(kLatencySamples)) >= 1e-4f);
    REQUIRE(VoragoTest::peakOf(aR.subspan(kLatencySamples)) >= 1e-4f);

    const float diffL = VoragoTest::maxAbsDiff(aL, std::span<const float>(armB.capturedL));
    const float diffR = VoragoTest::maxAbsDiff(aR, std::span<const float>(armB.capturedR));
    INFO("maxAbsDiff L=" << diffL << " R=" << diffR);
    REQUIRE(diffL <= 1.0e-5f);
    REQUIRE(diffR <= 1.0e-5f);
}

// SC-017 / FR-033: the reported latency is 3072 at every rate whatever any
// parameter holds - every discrete ID at each of its values, every continuous
// ID at 0 and at 1, each change rendered through one block, then re-prepared.
TEST_CASE("Vorago_LatencyIndependentOfParameters", "[vorago][integration]") {
    constexpr std::array<double, 3> kRates = {44100.0, 48000.0, 96000.0};
    constexpr std::size_t kLatencyBlock = 64;

    std::size_t steps = 0;
    for (const VoragoTest::ExpectedParamRow& row : VoragoTest::kExpectedParams) {
        steps += isDiscreteRow(row) ? static_cast<std::size_t>(row.stepCount) + 1u : 2u;
    }

    for (const double sr : kRates) {
        INFO("sample rate " << sr);
        VoragoTest::ProcessorFixture fx;
        fx.prepare(sr, 2048);
        fx.reserveCapture(steps * kLatencyBlock);
        REQUIRE(fx.proc->getLatencySamples() == kLatencySamples);

        Krate::Test::ParameterChanges pc;
        std::size_t checked = 0;
        for (const VoragoTest::ExpectedParamRow& row : VoragoTest::kExpectedParams) {
            const int count = isDiscreteRow(row) ? static_cast<int>(row.stepCount) + 1 : 2;
            for (int k = 0; k < count; ++k) {
                const double n = isDiscreteRow(row) ? ::Vorago::indexToNormalized(k, count)
                                                    : static_cast<double>(k);
                INFO("ID " << row.id << " normalized " << n);
                pc.setChange(row.id, n);
                REQUIRE(fx.processBlock(kLatencyBlock, nullptr, &pc) == Steinberg::kResultOk);
                REQUIRE(fx.proc->getLatencySamples() == kLatencySamples);
                ++checked;
            }
        }
        REQUIRE(checked == steps);

        // Every parameter at its last value across a re-prepare.
        REQUIRE(fx.proc->setActive(false) == Steinberg::kResultOk);
        fx.prepare(sr, 2048);
        REQUIRE(fx.proc->getLatencySamples() == kLatencySamples);
    }
}

// ==============================================================================
// T042 - SC-009
// ==============================================================================

// SC-009 / FR-022: a prepared, rendering processor loads a v2 stream with every
// persisted ID non-default; setState() itself makes no DSP call (the engine still
// carries the old seed right after it returns), and ONE process() - the block that
// consumes the release-store request - re-pushes every route, so the full SC-004
// read-back holds. The block is kMbReadbackSamples long so the master-gain
// smoother has settled onto the loaded value within that single block.
TEST_CASE("Vorago_SetStateAfterPrepareReachesDsp", "[vorago][integration]") {
    const std::vector<ProbeValue> probe = surfaceProbe();
    REQUIRE(probe.size() == 106u);

    // The stream, written by an unprepared processor holding the probe values.
    VoragoTest::ProcessorFixture src;
    {
        Krate::Test::ParameterChanges all;
        for (const ProbeValue& p : probe) {
            all.addChange(p.id, p.normalized);
        }
        REQUIRE(src.processNoOutputs(&all) == Steinberg::kResultOk);
    }
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(src.proc->getState(stream) == Steinberg::kResultOk);
    REQUIRE(stream->getSize() == static_cast<Steinberg::int64>(::Vorago::kStateV2Bytes));
    REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);

    // The rendering processor: registered defaults, polyphony 6, six held notes.
    VoragoTest::ProcessorFixture fx;
    fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kMbReadbackSamples));
    fx.reserveCapture(kBlock + kMbReadbackSamples);
    {
        Krate::Test::ParameterChanges setup;
        setup.addChange(::Vorago::kPolyphonyId, 1.0);
        Krate::Test::EventList notes;
        for (const Steinberg::int16 p : kSixHeldNotes) {
            notes.addNoteOn(p, kVelocity100, 0);
        }
        REQUIRE(fx.processBlock(kBlock, &notes, &setup) == Steinberg::kResultOk);
    }
    const VoragoEngine* engine = fx.proc->engineForTest();
    REQUIRE(engine != nullptr);
    REQUIRE(engine->getSeed() == ::Vorago::kVoragoSeedValues[0]);

    REQUIRE(fx.proc->setState(stream) == Steinberg::kResultOk);
    // No DSP call from setState(): the new seed index is stored, not yet applied.
    REQUIRE(fx.proc->globalParamsForTest().seedIndex.load() == kProbeSeedIndex);
    REQUIRE(engine->getSeed() == ::Vorago::kVoragoSeedValues[0]);

    // One block.
    REQUIRE(fx.processBlock(kMbReadbackSamples) == Steinberg::kResultOk);
    REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedL)));
    REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedR)));

    requireSurfaceReadback(*fx.proc, probe, kSampleRate, kMbReadbackSamples);
}
