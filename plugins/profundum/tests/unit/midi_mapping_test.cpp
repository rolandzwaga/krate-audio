// ==============================================================================
// Profundum - MIDI performance mapping (Phase 11 T015, T020, T021)
// ==============================================================================
// Profundum_MassPerformanceOffset (SC-024, FR-055): Mod Wheel (6) and Channel
//   Pressure (7) offset the assembled Mass of BOTH endpoints by
//   c * (hi - lo), c = clamp(6 + 7, 0, 1), clamped to the Mass row of kLawTable;
//   the endpoint slots are never written by the offset.
//   T015 writes the Arithmetic and SlotsUntouched sections; T020 the NeverWritten,
//   MirrorAndNoMessage and ResetOnLoad sections (FR-025, FR-046, FR-056).
//
// Profundum_MidiMapping (SC-015, FR-050 - FR-052; T021): the IMidiMapping table,
//   pitch bend through the voice-0 fCore tap, sustain-latch equivalence (incl. a
//   multi-point pedal queue), reset on load and state independence.
//
// Every equality is same-binary, same-process (no float golden across binaries).
// ==============================================================================

#include "profundum_test_fixture.h"

#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace P = ::Profundum;
using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;

constexpr std::size_t kMassLeafIndex = 52;  // kLawTable "mass" (ID 104)

[[nodiscard]] std::uint32_t bitsOf(float x) noexcept {
    return std::bit_cast<std::uint32_t>(x);
}

[[nodiscard]] std::unique_ptr<ProcessorFixture> makePrepared() {
    auto fx = std::make_unique<ProcessorFixture>();
    REQUIRE(fx->prepare(48000.0, 4096) == Steinberg::kResultOk);
    return fx;
}

// Mass of A and B set through 104, Edit Target A then B; the target ends on A.
void setMasses(ProcessorFixture& fx, double normA, double normB) {
    REQUIRE(fx.setParam(P::kMorphEditTargetId, 0.0) == Steinberg::kResultOk);
    REQUIRE(fx.setParam(P::kMassId, normA) == Steinberg::kResultOk);
    REQUIRE(fx.setParam(P::kMorphEditTargetId, 1.0) == Steinberg::kResultOk);
    REQUIRE(fx.setParam(P::kMassId, normB) == Steinberg::kResultOk);
    REQUIRE(fx.setParam(P::kMorphEditTargetId, 0.0) == Steinberg::kResultOk);
}

[[nodiscard]] float engineMass(const ProcessorFixture& fx, Endpoint e) {
    REQUIRE(fx.engine() != nullptr);
    return fx.engine()->getEndpoint(e).mass;
}

[[nodiscard]] std::uint32_t slotMassWord(const ProcessorFixture& fx, Endpoint e) {
    return fx.proc->endpointSlotForTest(e, kMassLeafIndex);
}

}  // namespace

TEST_CASE("Profundum_MassPerformanceOffset", "[profundum][midi]") {
    auto fx = makePrepared();
    setMasses(*fx, 0.2, 0.8);
    // Preconditions: the slots and the engine hold 20 / 80, no offset yet.
    REQUIRE(slotMassWord(*fx, Endpoint::A) == bitsOf(20.0f));
    REQUIRE(slotMassWord(*fx, Endpoint::B) == bitsOf(80.0f));
    REQUIRE(bitsOf(engineMass(*fx, Endpoint::A)) == bitsOf(20.0f));
    REQUIRE(bitsOf(engineMass(*fx, Endpoint::B)) == bitsOf(80.0f));

    SECTION("Arithmetic") {
        // c = 0.25: 20 + 25 = 45, 80 + 25 -> 100.
        REQUIRE(fx->setParam(P::kModWheelId, 0.25) == Steinberg::kResultOk);
        CHECK(engineMass(*fx, Endpoint::A) == 45.0f);
        CHECK(engineMass(*fx, Endpoint::B) == 100.0f);

        // + Channel Pressure 0.5: c = 0.75 -> 95 / 100.
        REQUIRE(fx->setParam(P::kChannelPressureId, 0.5) == Steinberg::kResultOk);
        CHECK(engineMass(*fx, Endpoint::A) == 95.0f);
        CHECK(engineMass(*fx, Endpoint::B) == 100.0f);

        // Mod Wheel 0.8 + Pressure 0.7: c = 1.5 clamps to 1 -> 100 / 100.
        REQUIRE(fx->setParamsInBlock({{P::kModWheelId, 0.8}, {P::kChannelPressureId, 0.7}}) ==
                Steinberg::kResultOk);
        CHECK(engineMass(*fx, Endpoint::A) == 100.0f);
        CHECK(engineMass(*fx, Endpoint::B) == 100.0f);

        // Both back at 0: the engine's Mass equals the slot values bitwise (the add is skipped).
        REQUIRE(fx->setParamsInBlock({{P::kModWheelId, 0.0}, {P::kChannelPressureId, 0.0}}) ==
                Steinberg::kResultOk);
        CHECK(bitsOf(engineMass(*fx, Endpoint::A)) == slotMassWord(*fx, Endpoint::A));
        CHECK(bitsOf(engineMass(*fx, Endpoint::B)) == slotMassWord(*fx, Endpoint::B));
        CHECK(bitsOf(engineMass(*fx, Endpoint::A)) == bitsOf(20.0f));
        CHECK(bitsOf(engineMass(*fx, Endpoint::B)) == bitsOf(80.0f));

        // Both Mass at 0: Mod Wheel alone at 0.5 -> 50 / 50; Channel Pressure alone at 0.5 -> 50 / 50.
        setMasses(*fx, 0.0, 0.0);
        REQUIRE(bitsOf(engineMass(*fx, Endpoint::A)) == bitsOf(0.0f));
        REQUIRE(bitsOf(engineMass(*fx, Endpoint::B)) == bitsOf(0.0f));
        REQUIRE(fx->setParam(P::kModWheelId, 0.5) == Steinberg::kResultOk);
        CHECK(engineMass(*fx, Endpoint::A) == 50.0f);
        CHECK(engineMass(*fx, Endpoint::B) == 50.0f);
        REQUIRE(fx->setParamsInBlock({{P::kModWheelId, 0.0}, {P::kChannelPressureId, 0.5}}) ==
                Steinberg::kResultOk);
        CHECK(engineMass(*fx, Endpoint::A) == 50.0f);
        CHECK(engineMass(*fx, Endpoint::B) == 50.0f);

        CHECK(fx->proc->endpointRejectionsForTest() == 0u);
    }

    SECTION("SlotsUntouched") {
        // The offset moves the engine's Mass, never the slot word 52.
        REQUIRE(fx->setParam(P::kModWheelId, 0.25) == Steinberg::kResultOk);
        REQUIRE(engineMass(*fx, Endpoint::A) == 45.0f);  // non-vacuity: the offset is applied
        CHECK(slotMassWord(*fx, Endpoint::A) == bitsOf(20.0f));
        CHECK(slotMassWord(*fx, Endpoint::B) == bitsOf(80.0f));

        REQUIRE(fx->setParam(P::kChannelPressureId, 0.5) == Steinberg::kResultOk);
        CHECK(slotMassWord(*fx, Endpoint::A) == bitsOf(20.0f));
        CHECK(slotMassWord(*fx, Endpoint::B) == bitsOf(80.0f));

        REQUIRE(fx->setParamsInBlock({{P::kModWheelId, 0.8}, {P::kChannelPressureId, 0.7}}) ==
                Steinberg::kResultOk);
        CHECK(slotMassWord(*fx, Endpoint::A) == bitsOf(20.0f));
        CHECK(slotMassWord(*fx, Endpoint::B) == bitsOf(80.0f));

        REQUIRE(fx->setParamsInBlock({{P::kModWheelId, 0.0}, {P::kChannelPressureId, 0.0}}) ==
                Steinberg::kResultOk);
        CHECK(slotMassWord(*fx, Endpoint::A) == bitsOf(20.0f));
        CHECK(slotMassWord(*fx, Endpoint::B) == bitsOf(80.0f));
    }

    // ---- T020: never persisted, never sent, reset on load (FR-025, FR-046, FR-056) ----

    SECTION("NeverWritten") {
        // getState bytes do not depend on Mod Wheel / Channel Pressure.
        const StateBytes before = writeV2Stream(*fx->proc);
        REQUIRE(fx->setParam(P::kModWheelId, 0.6) == Steinberg::kResultOk);
        REQUIRE(fx->setParam(P::kChannelPressureId, 0.3) == Steinberg::kResultOk);
        REQUIRE(engineMass(*fx, Endpoint::A) != 20.0f);  // non-vacuity: the offset is applied
        CHECK(slotMassWord(*fx, Endpoint::A) == bitsOf(20.0f));
        CHECK(slotMassWord(*fx, Endpoint::B) == bitsOf(80.0f));
        CHECK(writeV2Stream(*fx->proc) == before);
    }

    SECTION("MirrorAndNoMessage") {
        // Through a connected pair: the mirror carries the persisted Mass, and the offset alone sends nothing.
        ConnectedPair cp;
        REQUIRE(cp.fx.prepare(48000.0, 4096) == Steinberg::kResultOk);
        setMasses(cp.fx, 0.2, 0.8);  // processor-only edits
        cp.fx.proc->flushControllerSync();
        REQUIRE(bitsOf(cp.cf.controller->mirrorForTest(Endpoint::A).mass) == bitsOf(20.0f));
        REQUIRE(bitsOf(cp.cf.controller->mirrorForTest(Endpoint::B).mass) == bitsOf(80.0f));

        cp.clearRecords();
        REQUIRE(cp.fx.setParamsInBlock({{P::kModWheelId, 0.5}, {P::kChannelPressureId, 0.25}}) ==
                Steinberg::kResultOk);
        REQUIRE(engineMass(cp.fx, Endpoint::A) == 95.0f);  // non-vacuity: c = 0.75 reached the engine
        cp.fx.proc->flushControllerSync();
        CHECK(cp.toController.count(P::kMsgEndpoint) == 0u);
        CHECK(bitsOf(cp.cf.controller->mirrorForTest(Endpoint::A).mass) == slotMassWord(cp.fx, Endpoint::A));
        CHECK(bitsOf(cp.cf.controller->mirrorForTest(Endpoint::B).mass) == slotMassWord(cp.fx, Endpoint::B));
        CHECK(bitsOf(cp.cf.controller->mirrorForTest(Endpoint::A).mass) == bitsOf(20.0f));
        CHECK(bitsOf(cp.cf.controller->mirrorForTest(Endpoint::B).mass) == bitsOf(80.0f));
    }

    SECTION("ResetOnLoad") {
        // FR-046: setState returns Mod Wheel / Channel Pressure to 0, so the engine holds the persisted Mass.
        const StateBytes saved = writeV2Stream(*fx->proc);
        REQUIRE(fx->setParamsInBlock({{P::kModWheelId, 1.0}, {P::kChannelPressureId, 1.0}}) ==
                Steinberg::kResultOk);
        REQUIRE(engineMass(*fx, Endpoint::A) == 100.0f);  // non-vacuity
        REQUIRE(engineMass(*fx, Endpoint::B) == 100.0f);
        REQUIRE(loadStateBytes(*fx->proc, saved) == Steinberg::kResultOk);
        fx->ensureCapture(ProcessorFixture::kParamBlockSamples);
        REQUIRE(fx->processBlock(ProcessorFixture::kParamBlockSamples) == Steinberg::kResultOk);
        CHECK(bitsOf(engineMass(*fx, Endpoint::A)) == bitsOf(20.0f));
        CHECK(bitsOf(engineMass(*fx, Endpoint::B)) == bitsOf(80.0f));
    }
}

// =============================================================================
// Profundum_MidiMapping (SC-015; FR-050 - FR-052, FR-046) - T021
// =============================================================================
namespace {

using Ev = ProcessorFixture::ScriptedEvent;

constexpr double kMmFs = 48000.0;
constexpr std::size_t kMmHostBlock = 512;
constexpr Steinberg::int16 kStandInKey = 100;  // a key never played in these renders (slicing stand-in)
constexpr float kMmVelocity = 0.8f;

[[nodiscard]] Ev evNoteOn(Steinberg::int64 at, Steinberg::int16 pitch) {
    Ev e{};
    e.sample = at;
    e.kind = Ev::Kind::NoteOn;
    e.pitch = pitch;
    e.velocity = kMmVelocity;
    return e;
}

[[nodiscard]] Ev evNoteOff(Steinberg::int64 at, Steinberg::int16 pitch) {
    Ev e{};
    e.sample = at;
    e.kind = Ev::Kind::NoteOff;
    e.pitch = pitch;
    return e;
}

[[nodiscard]] Ev evParam(Steinberg::int64 at, Steinberg::Vst::ParamID id, double value) {
    Ev e{};
    e.sample = at;
    e.kind = Ev::Kind::Param;
    e.id = id;
    e.value = value;
    return e;
}

[[nodiscard]] Ev evPedal(Steinberg::int64 at, double value) {
    return evParam(at, P::kSustainPedalId, value);
}

struct StereoRender {
    std::vector<float> l, r;
};

// A fresh prepared processor (48 kHz, 512-sample host blocks), default state (E1 MONO), rendering @p script.
[[nodiscard]] StereoRender renderMm(std::span<const Ev> script, std::size_t total) {
    auto fx = std::make_unique<ProcessorFixture>();
    REQUIRE(fx->prepare(kMmFs, static_cast<Steinberg::int32>(kMmHostBlock)) == Steinberg::kResultOk);
    fx->renderScript(script, total, kMmHostBlock);
    REQUIRE(fx->capturedL.size() == total);
    REQUIRE(fx->capturedR.size() == total);
    return StereoRender{.l = fx->capturedL, .r = fx->capturedR};
}

[[nodiscard]] float maxDiffLR(const StereoRender& a, const StereoRender& b, std::size_t from = 0,
                              std::size_t to = kToEnd) {
    return std::max(maxAbsDiff(a.l, b.l, from, to), maxAbsDiff(a.r, b.r, from, to));
}

// SC-015: the voice-0 fCore tap (per-sample core frequency) averaged over two absolute-sample windows.
struct FCoreWindowObserver final : Krate::DSP::ProfundumEngineObserver {
    std::uint64_t beforeFrom = 0, beforeTo = 0, afterFrom = 0, afterTo = 0;
    double beforeSum = 0.0, afterSum = 0.0;
    std::size_t beforeN = 0, afterN = 0;

    void onVoiceChunk(const Krate::DSP::ProfundumVoiceTaps& t) noexcept override {
        if (t.voice != 0) {
            return;
        }
        const std::size_t n = std::min(t.n, t.fCore.size());
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint64_t s = t.sample + i;
            const double f = static_cast<double>(t.fCore[i]);
            if (s >= beforeFrom && s < beforeTo) {
                beforeSum += f;
                ++beforeN;
            } else if (s >= afterFrom && s < afterTo) {
                afterSum += f;
                ++afterN;
            }
        }
    }
};

}  // namespace

TEST_CASE("Profundum_MidiMapping", "[profundum][midi]") {
    SECTION("AssignmentTable") {  // FR-050
        ControllerFixture cf;
        for (const Steinberg::int16 channel : {Steinberg::int16{0}, Steinberg::int16{15}}) {
            INFO("channel " << channel);
            Steinberg::Vst::ParamID id = 9999;
            REQUIRE(cf.controller->getMidiControllerAssignment(0, channel, 1, id) == Steinberg::kResultOk);
            CHECK(id == 6u);
            id = 9999;
            REQUIRE(cf.controller->getMidiControllerAssignment(0, channel, 64, id) == Steinberg::kResultOk);
            CHECK(id == 5u);
            id = 9999;
            REQUIRE(cf.controller->getMidiControllerAssignment(0, channel, 128, id) == Steinberg::kResultOk);
            CHECK(id == 7u);
            id = 9999;
            REQUIRE(cf.controller->getMidiControllerAssignment(0, channel, 129, id) == Steinberg::kResultOk);
            CHECK(id == 4u);

            CHECK(cf.controller->getMidiControllerAssignment(1, channel, 1, id) == Steinberg::kResultFalse);
            CHECK(cf.controller->getMidiControllerAssignment(0, channel, 7, id) == Steinberg::kResultFalse);
        }
        // The SDK numbering the table is written against (ivstmidicontrollers.h).
        STATIC_REQUIRE(Steinberg::Vst::kCtrlModWheel == 1);
        STATIC_REQUIRE(Steinberg::Vst::kCtrlSustainOnOff == 64);
        STATIC_REQUIRE(Steinberg::Vst::kAfterTouch == 128);
        STATIC_REQUIRE(Steinberg::Vst::kPitchBend == 129);
    }

    SECTION("PitchBend") {  // FR-051: range 12 st, bend +1 -> fCore rises 12 st +- 5 cents
        constexpr std::size_t kBendAt = 141u * kMmHostBlock;  // 72192 (~1.5 s), a host-block start
        constexpr std::size_t kWindow = 24000;                // 500 ms
        constexpr std::size_t kAfterStart = 9600;             // 200 ms
        constexpr std::size_t kTotal = kBendAt + kAfterStart + kWindow;

        FCoreWindowObserver obs;  // outlives the processor
        obs.beforeFrom = kBendAt - kWindow;
        obs.beforeTo = kBendAt;
        obs.afterFrom = kBendAt + kAfterStart;
        obs.afterTo = kBendAt + kAfterStart + kWindow;

        auto fx = std::make_unique<ProcessorFixture>();
        fx->proc->setEngineObserverForTest(&obs);  // FR-067: before setupProcessing
        REQUIRE(fx->prepare(kMmFs, static_cast<Steinberg::int32>(kMmHostBlock)) == Steinberg::kResultOk);

        const std::array<Ev, 4> script{
            evParam(0, P::kVoiceModeId, 0.0),                   // MONO
            evParam(0, P::kNotePitchBendRangeId, 12.0 / 24.0),  // 417 = 12 st
            evNoteOn(0, kE1),
            evParam(static_cast<Steinberg::int64>(kBendAt), P::kPitchBendId, 1.0),
        };
        fx->renderScript(script, kTotal, kMmHostBlock);

        REQUIRE(fx->engine() != nullptr);
        CHECK(fx->engine()->pitchBend() == 1.0f);
        CHECK(fx->engine()->pitchBendRange() == 12.0f);

        REQUIRE(obs.beforeN == kWindow);
        REQUIRE(obs.afterN == kWindow);
        const double before = obs.beforeSum / static_cast<double>(obs.beforeN);
        const double after = obs.afterSum / static_cast<double>(obs.afterN);
        REQUIRE(before > 0.0);
        const double riseSt = 12.0 * std::log2(after / before);
        INFO("fCore mean before " << before << " Hz, after " << after << " Hz, rise " << riseSt << " st");
        CHECK(std::fabs(riseSt - 12.0) <= 0.05);
    }

    SECTION("SustainEquivalence") {  // FR-052, same-binary, max |diff| == 0
        constexpr Steinberg::int64 kPedalDown = 12000;  // 0.25 s
        constexpr Steinberg::int64 kNoteOff = 24000;    // 0.5 s
        constexpr Steinberg::int64 kPedalUp = 48000;    // 1.0 s
        constexpr std::size_t kTotal = 72000;           // 1.5 s

        // (a) note-off while the pedal is down == a pedal-free note-off moved to the pedal-up offset.
        const std::array<Ev, 4> pedalA{evNoteOn(0, kE1), evPedal(kPedalDown, 1.0), evNoteOff(kNoteOff, kE1),
                                       evPedal(kPedalUp, 0.0)};
        const std::array<Ev, 5> refA{evNoteOn(0, kE1), evPedal(kPedalDown, 0.0), evNoteOff(kNoteOff, kStandInKey),
                                     evNoteOff(kPedalUp, kE1), evPedal(kPedalUp, 0.0)};
        const StereoRender pa = renderMm(pedalA, kTotal);
        const StereoRender ra = renderMm(refA, kTotal);
        CHECK(maxDiffLR(pa, ra) == 0.0f);
        CHECK(mismatches(pa.l, ra.l) == 0u);
        CHECK(mismatches(pa.r, ra.r) == 0u);

        // Non-vacuity: the latch moved the release (the un-latched note-off at 0.5 s renders differently).
        const std::array<Ev, 4> plainAt05{evNoteOn(0, kE1), evPedal(kPedalDown, 0.0), evNoteOff(kNoteOff, kE1),
                                          evPedal(kPedalUp, 0.0)};
        const StereoRender na = renderMm(plainAt05, kTotal);
        CHECK(maxDiffLR(pa, na, static_cast<std::size_t>(kNoteOff)) > 0.0f);

        // (b) a note-off with the pedal up == the plain note-off.
        constexpr Steinberg::int64 kPedalUpEarly = 18000;  // 0.375 s, before the note-off
        const std::array<Ev, 4> pedalB{evNoteOn(0, kE1), evPedal(kPedalDown, 1.0), evPedal(kPedalUpEarly, 0.0),
                                       evNoteOff(kNoteOff, kE1)};
        const std::array<Ev, 4> refB{evNoteOn(0, kE1), evPedal(kPedalDown, 0.0), evPedal(kPedalUpEarly, 0.0),
                                     evNoteOff(kNoteOff, kE1)};
        const StereoRender pb = renderMm(pedalB, kTotal);
        const StereoRender rb = renderMm(refB, kTotal);
        CHECK(maxDiffLR(pb, rb) == 0.0f);
        CHECK(mismatches(pb.l, rb.l) == 0u);
        CHECK(mismatches(pb.r, rb.r) == 0u);
    }

    SECTION("MultiPointQueue") {  // FR-052: every point of one block's Sustain queue, at its offset
        constexpr Steinberg::int64 kB = 4 * static_cast<Steinberg::int64>(kMmHostBlock);  // the queue block: 2048
        constexpr Steinberg::int64 kLaterUp = 6 * static_cast<Steinberg::int64>(kMmHostBlock) + 100;  // 3172
        constexpr std::size_t kTotal = 48000;

        // Pedal render: queue [down @ 64, up @ 192, down @ 320] + note-off @ 128 in one block; a re-strike @ 256 and
        // a second note-off @ 384 (latched by the down @ 320) in the same block; a later pedal-up releases it.
        const std::array<Ev, 8> pedal{evNoteOn(0, kE1),
                                      evPedal(kB + 64, 1.0),
                                      evNoteOff(kB + 128, kE1),
                                      evPedal(kB + 192, 0.0),
                                      evNoteOn(kB + 256, kE1),
                                      evPedal(kB + 320, 1.0),
                                      evNoteOff(kB + 384, kE1),
                                      evPedal(kLaterUp, 0.0)};
        // Reference: the first note-off at the up offset (192), the second at the later pedal-up, stand-ins elsewhere.
        const std::array<Ev, 10> ref{evNoteOn(0, kE1),
                                     evPedal(kB + 64, 0.0),
                                     evNoteOff(kB + 128, kStandInKey),
                                     evNoteOff(kB + 192, kE1),
                                     evPedal(kB + 192, 0.0),
                                     evNoteOn(kB + 256, kE1),
                                     evPedal(kB + 320, 0.0),
                                     evNoteOff(kB + 384, kStandInKey),
                                     evNoteOff(kLaterUp, kE1),
                                     evPedal(kLaterUp, 0.0)};
        // Held: as the reference, but E1 is never released after the re-strike (the later note-off is a stand-in).
        const std::array<Ev, 10> held{evNoteOn(0, kE1),
                                      evPedal(kB + 64, 0.0),
                                      evNoteOff(kB + 128, kStandInKey),
                                      evNoteOff(kB + 192, kE1),
                                      evPedal(kB + 192, 0.0),
                                      evNoteOn(kB + 256, kE1),
                                      evPedal(kB + 320, 0.0),
                                      evNoteOff(kB + 384, kStandInKey),
                                      evNoteOff(kLaterUp, kStandInKey),
                                      evPedal(kLaterUp, 0.0)};
        const StereoRender p = renderMm(pedal, kTotal);
        const StereoRender r = renderMm(ref, kTotal);
        const StereoRender h = renderMm(held, kTotal);

        CHECK(maxDiffLR(p, r) == 0.0f);
        CHECK(mismatches(p.l, r.l) == 0u);
        CHECK(mismatches(p.r, r.r) == 0u);

        // The second note-off stays latched: one block later the voice still sounds, exactly as the held render.
        const std::size_t nextBlock = static_cast<std::size_t>(kB) + kMmHostBlock;
        CHECK(maxDiffLR(p, h, 0, static_cast<std::size_t>(kLaterUp)) == 0.0f);
        CHECK(rmsDb(p.l, nextBlock, nextBlock + kMmHostBlock) > -60.0);
        // ...and it releases after the later pedal-up (it departs from the held render only then).
        CHECK(maxDiffLR(p, h, static_cast<std::size_t>(kLaterUp)) > 0.0f);
    }

    SECTION("ResetOnLoad") {  // FR-046 / FR-052: setState centres the bend and lifts the pedal
        auto fx = makePrepared();
        const StateBytes saved = writeV2Stream(*fx->proc);
        REQUIRE(fx->setParamsInBlock({{P::kPitchBendId, 1.0}, {P::kSustainPedalId, 1.0}}) == Steinberg::kResultOk);
        REQUIRE(fx->engine()->pitchBend() == 1.0f);  // non-vacuity
        REQUIRE(fx->proc->sustainDownForTest());
        REQUIRE(loadStateBytes(*fx->proc, saved) == Steinberg::kResultOk);
        fx->ensureCapture(ProcessorFixture::kParamBlockSamples);
        REQUIRE(fx->processBlock(ProcessorFixture::kParamBlockSamples) == Steinberg::kResultOk);
        CHECK(fx->engine()->pitchBend() == 0.0f);
        CHECK_FALSE(fx->proc->sustainDownForTest());
    }

    SECTION("StateIndependentOfPerformance") {  // FR-046: getState never carries the pedal or the bend
        auto fx = makePrepared();
        fx->ensureCapture(ProcessorFixture::kParamBlockSamples);
        REQUIRE(fx->processBlock(ProcessorFixture::kParamBlockSamples) == Steinberg::kResultOk);
        const StateBytes before = writeV2Stream(*fx->proc);
        REQUIRE(fx->setParamsInBlock({{P::kPitchBendId, 1.0}, {P::kSustainPedalId, 1.0}}) == Steinberg::kResultOk);
        REQUIRE(fx->proc->sustainDownForTest());  // non-vacuity
        REQUIRE(fx->engine()->pitchBend() == 1.0f);
        CHECK(writeV2Stream(*fx->proc) == before);
    }
}
