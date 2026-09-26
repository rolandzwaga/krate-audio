// ==============================================================================
// Vorago Phase 12 - channel pressure (SC-013)
// ==============================================================================
// T034: SECTION "MidiMapping" - the controller exposes IMidiMapping, routing
// CC64 (kCtrlSustainOnOff) to kSustainPedalId and channel aftertouch
// (kAfterTouch) to kChannelPressureId on bus 0, any channel (FR-031, SC-013 (5)).
// T044: the composition SECTIONs (FR-021, FR-032, FR-045, SC-013 (1)-(4), (7)).
// Channel pressure is added to the Pressure knob and clamped to [0, 1] in
// Processor::buildMacroVector(); setState() zeroes sustain and pressure at once
// and the next process() releases the latch.
//
// TOLERANCE: "within 1e-6" is applied as |got - want| <= 1e-6 * max(1, |want|).
// OutputDriveDb carries a +18 dB amount (spec B-7), so one float ulp of the macro value
// (0.4f + 0.3f vs 0.7f) moves it by ~1.1e-6 and its own ulp at ~12.6 dB is ~9.5e-7;
// an absolute 1e-6 on a value of magnitude > 1 would measure float rounding,
// not composition.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "parameters/macro_params.h"
#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/common/memorystream.h"

#include <vst_event_list.h>

#include <krate/dsp/systems/voice_allocator.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>
#include <krate/dsp/systems/vorago_voice.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using Krate::DSP::VoiceState;
using Krate::DSP::VoragoCavernTargets;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoMacro;
using Krate::DSP::VoragoMacroMatrix;
using Krate::DSP::VoragoMacroTarget;
using Krate::DSP::VoragoMacroValues;

constexpr double kSr = 48000.0;
constexpr std::size_t kBlk = 512;
constexpr std::size_t kPressureIndex = static_cast<std::size_t>(VoragoMacro::Pressure);

/// Non-neutral values for the eleven other macros (index 6 = Pressure unused).
constexpr std::array<double, VoragoMacroMatrix::kNumMacros> kOtherMacros{
    0.25, 0.5, 0.375, 0.625, 0.75, 0.125, 0.0, 0.5, 0.25, 0.375, 0.625, 0.5};

[[nodiscard]] bool withinTol(float got, float want) noexcept {
    const double tol = 1.0e-6 * std::max(1.0, std::fabs(static_cast<double>(want)));
    return std::fabs(static_cast<double>(got) - static_cast<double>(want)) <= tol;
}

[[nodiscard]] constexpr bool isCavernTarget(std::size_t t) noexcept {
    return t >= VoragoMacroMatrix::kFirstCavernTarget;
}
[[nodiscard]] constexpr bool isEngineTarget(std::size_t t) noexcept {
    return t >= VoragoMacroMatrix::kFirstEngineTarget && !isCavernTarget(t);
}

/// The getter paired with each Voice/Engine setter in VoragoMacroMatrix::apply()
/// (vorago_macro_matrix.h:1041-1105). Cavern targets are read through
/// computeCavernTargets() instead.
[[nodiscard]] float readTarget(const VoragoEngine& e, VoragoMacroTarget t, std::size_t vi) {
    using T = VoragoMacroTarget;
    const Krate::DSP::VoragoVoice& v = e.getVoice(vi);
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
        default: return 0.0f;
    }
}

/// Every target the Pressure macro's rows write (from the kRows table itself).
[[nodiscard]] std::array<bool, VoragoMacroMatrix::kNumTargets> pressureTargets() {
    std::array<bool, VoragoMacroMatrix::kNumTargets> out{};
    for (const auto& row : VoragoMacroMatrix::kRows) {
        if (row.macro == VoragoMacro::Pressure) {
            out[static_cast<std::size_t>(row.target)] = true;
        }
    }
    return out;
}

/// One arm's read-back after a single process(): every Voice target on every
/// voice < polyphony, every Engine target, the cavern targets and the macro vector.
struct Snapshot {
    std::array<std::vector<float>, VoragoMacroMatrix::kNumTargets> targets;
    VoragoCavernTargets cavern{};
    VoragoMacroValues macros{};
    std::array<float, VoragoMacroMatrix::kNumMacros> atomics{};
};

[[nodiscard]] Snapshot renderArm(double knob, double pressure, bool otherMacrosActive) {
    VoragoTest::ProcessorFixture fx;
    fx.prepare(kSr, static_cast<Steinberg::int32>(kBlk));
    fx.reserveCapture(kBlk);

    VoragoTest::MultiParamChanges pc;
    pc.reserve(VoragoMacroMatrix::kNumMacros + 2u);
    if (otherMacrosActive) {
        for (std::size_t i = 0; i < VoragoMacroMatrix::kNumMacros; ++i) {
            if (i != kPressureIndex) {
                pc.addQueue(static_cast<Steinberg::Vst::ParamID>(::Vorago::kMacroDarknessId + i))
                    .addTestPoint(0, kOtherMacros[i]);
            }
        }
    }
    pc.addQueue(::Vorago::kMacroPressureId).addTestPoint(0, knob);
    pc.addQueue(::Vorago::kChannelPressureId).addTestPoint(0, pressure);
    REQUIRE(fx.processBlock(kBlk, nullptr, &pc) == Steinberg::kResultOk);

    const VoragoEngine* e = fx.proc->engineForTest();
    REQUIRE(e != nullptr);
    Snapshot s;
    for (std::size_t t = 0; t < VoragoMacroMatrix::kFirstCavernTarget; ++t) {
        const auto target = static_cast<VoragoMacroTarget>(t);
        if (isEngineTarget(t)) {
            s.targets[t].push_back(readTarget(*e, target, 0));
        } else {
            for (std::size_t v = 0; v < e->getPolyphony(); ++v) {
                s.targets[t].push_back(readTarget(*e, target, v));
            }
        }
    }
    const VoragoMacroMatrix& m = fx.proc->macrosForTest();
    s.cavern = m.computeCavernTargets();
    s.macros = m.getMacros();
    for (std::size_t i = 0; i < VoragoMacroMatrix::kNumMacros; ++i) {
        s.atomics[i] = ::Vorago::macroField(fx.proc->macroParamsForTest(), static_cast<int>(i))
                           .load();
    }
    return s;
}

[[nodiscard]] std::array<float, VoragoMacroMatrix::kNumMacros> asArray(
    const VoragoMacroValues& v) noexcept {
    return {v.darkness, v.age,  v.density, v.movement, v.gravity, v.entropy,
            v.pressure, v.weight, v.fog,   v.life,     v.depth,   v.mass};
}

[[nodiscard]] std::array<float, VoragoMacroMatrix::kNumCavernTargets> asArray(
    const VoragoCavernTargets& c) noexcept {
    return {c.size, c.darkness, c.decaySeconds, c.fog, c.damperDepth, c.mix, c.width};
}

/// REQUIREs every target in `mask` (true = compare) near-equal between a and b.
void requireTargetsNear(const Snapshot& a, const Snapshot& b,
                        const std::array<bool, VoragoMacroMatrix::kNumTargets>& mask) {
    for (std::size_t t = 0; t < VoragoMacroMatrix::kFirstCavernTarget; ++t) {
        if (!mask[t]) {
            continue;
        }
        REQUIRE(a.targets[t].size() == b.targets[t].size());
        for (std::size_t k = 0; k < a.targets[t].size(); ++k) {
            INFO("target " << t << " slot " << k << " got=" << a.targets[t][k]
                           << " want=" << b.targets[t][k]);
            REQUIRE(withinTol(a.targets[t][k], b.targets[t][k]));
        }
    }
}

}  // namespace

TEST_CASE("Vorago_ChannelPressure", "[vorago][midi]") {
    SECTION("MidiMapping") {  // SC-013 (5), FR-031
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

        Steinberg::Vst::IMidiMapping* mapping = nullptr;
        REQUIRE(controller->queryInterface(Steinberg::Vst::IMidiMapping::iid,
                                           reinterpret_cast<void**>(&mapping)) ==
                Steinberg::kResultOk);
        REQUIRE(mapping != nullptr);

        Steinberg::Vst::ParamID id = 0xFFFFFFFFu;
        CHECK(mapping->getMidiControllerAssignment(0, 0, Steinberg::Vst::kCtrlSustainOnOff, id) ==
              Steinberg::kResultOk);
        CHECK(id == ::Vorago::kSustainPedalId);
        CHECK(id == 4u);

        id = 0xFFFFFFFFu;
        CHECK(mapping->getMidiControllerAssignment(0, 15, Steinberg::Vst::kCtrlSustainOnOff, id) ==
              Steinberg::kResultOk);
        CHECK(id == ::Vorago::kSustainPedalId);

        id = 0xFFFFFFFFu;
        CHECK(mapping->getMidiControllerAssignment(0, 0, Steinberg::Vst::kAfterTouch, id) ==
              Steinberg::kResultOk);
        CHECK(id == ::Vorago::kChannelPressureId);
        CHECK(id == 5u);

        id = 0xFFFFFFFFu;
        CHECK(mapping->getMidiControllerAssignment(0, 15, Steinberg::Vst::kAfterTouch, id) ==
              Steinberg::kResultOk);
        CHECK(id == ::Vorago::kChannelPressureId);

        CHECK(mapping->getMidiControllerAssignment(0, 0, Steinberg::Vst::kCtrlModWheel, id) ==
              Steinberg::kResultFalse);
        CHECK(mapping->getMidiControllerAssignment(1, 0, Steinberg::Vst::kCtrlSustainOnOff, id) ==
              Steinberg::kResultFalse);

        mapping->release();
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    const std::array<bool, VoragoMacroMatrix::kNumTargets> pressureMask = pressureTargets();
    std::size_t numPressureTargets = 0;
    for (const bool b : pressureMask) {
        numPressureTargets += b ? 1u : 0u;
    }
    REQUIRE(numPressureTargets == 5u);  // kRows: EcologyMix, EcologyLoopGain, OutputSaturation, OutputDriveDb, SubToneLevelOffsetDb (spec B-7)
    std::array<bool, VoragoMacroMatrix::kNumTargets> otherMask{};
    for (std::size_t t = 0; t < otherMask.size(); ++t) {
        otherMask[t] = !pressureMask[t];
    }

    SECTION("PressureZeroMacroVectorEqualsAtomics") {  // SC-013 (1)
        const Snapshot s = renderArm(0.35, 0.0, true);
        const auto got = asArray(s.macros);
        for (std::size_t i = 0; i < got.size(); ++i) {
            INFO("macro " << i);
            REQUIRE(got[i] == s.atomics[i]);
        }
        REQUIRE(s.macros.pressure == 0.35f);
    }

    SECTION("FullPressureAtDefaultKnobEqualsKnobOne") {  // SC-013 (2)
        const Snapshot pressured = renderArm(0.0, 1.0, false);
        const Snapshot knobOne = renderArm(1.0, 0.0, false);
        REQUIRE(pressured.macros.pressure == 1.0f);
        REQUIRE(knobOne.macros.pressure == 1.0f);
        requireTargetsNear(pressured, knobOne, pressureMask);
    }

    SECTION("ComposesAdditively") {  // SC-013 (3)
        const Snapshot composed = renderArm(0.4, 0.3, true);
        const Snapshot knobOnly = renderArm(0.7, 0.0, true);
        const Snapshot noPressure = renderArm(0.4, 0.0, true);

        INFO("composed pressure " << composed.macros.pressure << " knob-only "
                                  << knobOnly.macros.pressure);
        REQUIRE(withinTol(composed.macros.pressure, knobOnly.macros.pressure));
        REQUIRE(withinTol(composed.macros.pressure, 0.7f));
        requireTargetsNear(composed, knobOnly, pressureMask);

        // Every other macro is untouched by pressure: its vector entry and every
        // target no Pressure row writes read back identically with pressure 0.
        const auto withP = asArray(composed.macros);
        const auto withoutP = asArray(noPressure.macros);
        for (std::size_t i = 0; i < withP.size(); ++i) {
            if (i == kPressureIndex) {
                continue;
            }
            INFO("macro " << i);
            REQUIRE(withP[i] == withoutP[i]);
        }
        requireTargetsNear(composed, noPressure, otherMask);
        const auto cavWith = asArray(composed.cavern);
        const auto cavWithout = asArray(noPressure.cavern);
        for (std::size_t i = 0; i < cavWith.size(); ++i) {
            INFO("cavern field " << i);
            REQUIRE(withinTol(cavWith[i], cavWithout[i]));
        }
    }

    SECTION("SumIsClampedAtOne") {  // SC-013 (4)
        const Snapshot clamped = renderArm(0.8, 0.5, true);
        const Snapshot knobOne = renderArm(1.0, 0.0, true);
        REQUIRE(clamped.macros.pressure == 1.0f);
        REQUIRE(knobOne.macros.pressure == 1.0f);
        requireTargetsNear(clamped, knobOne, pressureMask);
    }

    SECTION("SetStateZeroesSustainAndPressureUntilNextProcess") {  // SC-013 (7), FR-045
        constexpr Steinberg::int16 kPitch = 60;
        constexpr std::size_t kMaxHoldBlocks = 375;  // 4 s at 48 kHz / 512

        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSr, static_cast<Steinberg::int32>(kBlk));
        fx.reserveCapture(kBlk);
        Krate::Test::EventList ev;
        VoragoTest::MultiParamChanges pc;
        pc.reserve(4);

        auto block = [&]() {
            fx.capturedL.clear();  // keeps capacity
            fx.capturedR.clear();
            REQUIRE(fx.processBlock(kBlk, &ev, &pc) == Steinberg::kResultOk);
            ev.clear();
            pc.clear();
        };
        const VoragoEngine* engine = fx.proc->engineForTest();
        REQUIRE(engine != nullptr);

        // A v2 stream whose Pressure knob is 0.25, then move the knob away.
        pc.addQueue(::Vorago::kMacroPressureId).addTestPoint(0, 0.25);
        block();
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        REQUIRE(fx.proc->getState(stream) == Steinberg::kResultOk);
        pc.addQueue(::Vorago::kMacroPressureId).addTestPoint(0, 0.6);
        block();
        REQUIRE(fx.proc->macrosForTest().getMacros().pressure == 0.6f);

        // One note, held until audible so a release reads Releasing, not Idle.
        ev.addNoteOn(kPitch, 100.0f / 127.0f, 0);
        block();
        std::size_t slot = VoragoEngine::kMaxVoices;
        for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
            if (engine->getVoiceState(i) == VoiceState::Active) {
                REQUIRE(slot == VoragoEngine::kMaxVoices);  // exactly one
                slot = i;
            }
        }
        REQUIRE(slot < VoragoEngine::kMaxVoices);
        std::size_t held = 0;
        while (engine->getVoiceLevel(slot) < Krate::DSP::VoragoVoice::kTailSilenceThreshold &&
               held < kMaxHoldBlocks) {
            block();
            ++held;
        }
        INFO("held blocks = " << held);
        REQUIRE(engine->getVoiceLevel(slot) >= Krate::DSP::VoragoVoice::kTailSilenceThreshold);

        // Pressure 0.7 and pedal down, then the note-off is latched.
        pc.addQueue(::Vorago::kChannelPressureId).addTestPoint(0, 0.7);
        pc.addQueue(::Vorago::kSustainPedalId).addTestPoint(0, 1.0);
        block();
        REQUIRE(fx.proc->latchForTest().isDown());
        REQUIRE(fx.proc->packsForTest().global.sustainPedal.load() == 1.0f);
        REQUIRE(fx.proc->packsForTest().global.channelPressure.load() == 0.7f);
        REQUIRE(fx.proc->macrosForTest().getMacros().pressure == 1.0f);  // 0.6 + 0.7 clamped
        ev.addNoteOff(kPitch, 0);
        block();
        REQUIRE(engine->getVoiceState(slot) == VoiceState::Active);  // latched

        // setState: sustain and pressure are zero IMMEDIATELY; no DSP call yet.
        REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) ==
                Steinberg::kResultOk);
        REQUIRE(fx.proc->setState(stream) == Steinberg::kResultOk);
        REQUIRE(fx.proc->packsForTest().global.sustainPedal.load() == 0.0f);
        REQUIRE(fx.proc->packsForTest().global.channelPressure.load() == 0.0f);
        REQUIRE(fx.proc->packsForTest().macro.pressure.load() == 0.25f);
        REQUIRE(engine->getVoiceState(slot) == VoiceState::Active);

        // One process(): the macro vector is the restored knob plus zero
        // pressure, and the latched note is released.
        block();
        REQUIRE(fx.proc->macrosForTest().getMacros().pressure == 0.25f);
        REQUIRE_FALSE(fx.proc->latchForTest().isDown());
        REQUIRE(engine->getVoiceState(slot) == VoiceState::Releasing);
    }
}
