// ==============================================================================
// Profundum - processor CPU test (SC-015; FR-067)   Plan section 4.4, T020
// ==============================================================================
// Profundum_ProcessorCpu is HIDDEN ([.perf]) and wall-clock based: run it ALONE,
// nothing else executing (node tools/run-cpu-tests.js profundum_tests). It is
// written by T020 and run only by T031. A red result is reported to the user,
// never re-run in a loop.
//
//   P-MONO / P-POLY         ::Profundum::Processor, bare proc->process(data) calls.
//                           The first warm-up block carries NoteOn(28) (MONO), or
//                           kChord4 plus a kVoiceModeId = 1 point (POLY). Every
//                           later block has inputEvents = nullptr and
//                           inputParameterChanges = nullptr; buffers pre-built.
//   D-MONO / D-POLY         DirectEngine: engine processBlock(L, R, 512) plus the
//                           per-sample output-gain loop; the same notes, and
//                           setVoiceMode(Poly) for POLY.
//   P-MAX-MONO / P-MAX-POLY as P-MONO / P-POLY with all four macro points at 1.0
//                           in the warm-up block (P-1, GATED: user ruling Q-A
//                           2026-10-22).
//   P-ON-MONO / P-ON-POLY   Phase 12 T032 (SC-013, FR-075): the P-MONO / P-POLY
//                           instance itself with setSpectrumProducerEnabledForTest(true)
//                           set for its timed blocks only (the seam arm: the
//                           producer runs with no open queue). Same bars, same
//                           P / D ratio ceiling against D-MONO / D-POLY.
//
// All eight arms run in ONE interleaved trial loop, each P / P-ON / D triple in
// flipped order on odd trials. best_X = the minimum over trials of ns per kBlocksPerTrial blocks;
// pct_X = best_X / (kBlocksPerTrial x 512 / 48000 x 1e9) x 100. Short trials, many
// of them (Phase 11 SC-020 ruling, 2026-10-22): 16 x 100-block trials read identical
// code from 0.79 to 1.18 P / D on this machine.
//
// The TU is deliberately NOT in the -fno-fast-math list (tests/CMakeLists.txt).
//
// NAMESPACE HAZARD (profundum_test_fixture.h): plugin types are spelled
// ::Profundum::..., and Krate::DSP::ProfundumTest is never opened.
// ==============================================================================

#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"
#include "profundum_test_fixture.h"

#include <vst_event_list.h>

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kWarmupBlocks = 200;
constexpr std::size_t kTrials = 640;          // Phase 11 SC-020 rulings: 10-block trials, 4x the original work
constexpr std::size_t kBlocksPerTrial = 10;
constexpr double kControlRatioCeiling = 1.05;  // SC-020: two readings of the same D path beyond this: INVALID

constexpr double kWrapperOverheadCeiling = 1.05;  // SC-015 ratio gate
constexpr double kMonoPctBar = 8.0;               // Phase 9 MONO bar (%)
constexpr double kPolyPctBar = 25.0;              // Phase 9 POLY4 bar (%)
constexpr double kPhase9MonoShowcasePct = 7.5798;  // recorded only (P-1)
constexpr double kPhase9PolyShowcasePct = 24.005;  // recorded only (P-1)

constexpr std::uint8_t kMidiVelocity = 100;
constexpr float kVelocity = 100.0f / 127.0f;  // quantiseVelocity -> 100

constexpr std::array<Steinberg::Vst::ParamID, 4> kMacroIds{
    ::Profundum::kMacroWeightId, ::Profundum::kMacroDarkBrightId,
    ::Profundum::kMacroTightLooseId, ::Profundum::kMacroCleanViolentId};

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedNs(Clock::time_point t0, Clock::time_point t1) noexcept {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

// The real-time period of kBlocksPerTrial blocks, in ns.
[[nodiscard]] constexpr double trialPeriodNs() noexcept {
    return static_cast<double>(kBlocksPerTrial) * static_cast<double>(kBlock) / kFs * 1.0e9;
}

[[nodiscard]] constexpr double pctOf(double bestNs) noexcept {
    return bestNs / trialPeriodNs() * 100.0;
}

// A stereo ProcessData over caller-owned buffers, built ONCE before timing.
struct CpuProcessCall {
    std::vector<float> left;
    std::vector<float> right;
    std::array<float*, 2> channels{};
    Steinberg::Vst::AudioBusBuffers outBus;
    Steinberg::Vst::ProcessData data;

    explicit CpuProcessCall(std::size_t n) : left(n, 0.0f), right(n, 0.0f) {
        channels = {left.data(), right.data()};

        outBus.numChannels = 2;
        outBus.silenceFlags = 0;
        outBus.channelBuffers32 = channels.data();

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(n);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = nullptr;
        data.outputEvents = nullptr;
        data.processContext = nullptr;
    }

    CpuProcessCall(const CpuProcessCall&) = delete;
    CpuProcessCall& operator=(const CpuProcessCall&) = delete;

    [[nodiscard]] float peak() const noexcept {
        float p = 0.0f;
        for (std::size_t i = 0; i < left.size(); ++i) {
            p = std::max({p, std::fabs(left[i]), std::fabs(right[i])});
        }
        return p;
    }
};

// One processor arm: a prepared fixture plus its pre-built bare call.
struct ProcessorArm {
    ProcessorFixture fixture;
    CpuProcessCall call{kBlock};

    ProcessorArm() {
        REQUIRE(fixture.prepare(kFs, static_cast<Steinberg::int32>(kBlock)) ==
                Steinberg::kResultOk);
        REQUIRE(fixture.proc != nullptr);
    }

    // Warm-up: the first block carries the events and the parameter points, every
    // later block carries neither (they stay detached for the timed blocks).
    void warmUp(Steinberg::Vst::IEventList* events, Steinberg::Vst::IParameterChanges* params) {
        call.data.inputEvents = events;
        call.data.inputParameterChanges = params;
        REQUIRE(fixture.proc->process(call.data) == Steinberg::kResultOk);
        call.data.inputEvents = nullptr;
        call.data.inputParameterChanges = nullptr;
        for (std::size_t b = 1; b < kWarmupBlocks; ++b) {
            fixture.proc->process(call.data);
        }
    }

    void processBlock() noexcept { fixture.proc->process(call.data); }
};

// One direct arm: the bare engine plus the processor's output-gain loop.
struct DirectArm {
    DirectEngine direct{kFs};

    DirectArm() {
        direct.outL.assign(kBlock, 0.0f);
        direct.outR.assign(kBlock, 0.0f);
    }

    void processBlock() noexcept {
        float* l = direct.outL.data();
        float* r = direct.outR.data();
        direct.e->processBlock(l, r, kBlock);
        for (std::size_t s = 0; s < kBlock; ++s) {
            const float gain = direct.g.process();
            l[s] *= gain;
            r[s] *= gain;
        }
    }

    void warmUp() noexcept {
        for (std::size_t b = 0; b < kWarmupBlocks; ++b) {
            processBlock();
        }
    }

    [[nodiscard]] float peak() const noexcept {
        float p = 0.0f;
        for (std::size_t i = 0; i < direct.outL.size(); ++i) {
            p = std::max({p, std::fabs(direct.outL[i]), std::fabs(direct.outR[i])});
        }
        return p;
    }
};

template <typename Arm>
void timeArm(Arm& arm, double& best) {
    const Clock::time_point t0 = Clock::now();
    for (std::size_t b = 0; b < kBlocksPerTrial; ++b) {
        arm.processBlock();
    }
    const Clock::time_point t1 = Clock::now();
    best = std::min(best, elapsedNs(t0, t1));
}

// Phase 12 T032 (SC-013): one producer-on trial of @p arm, on @p proc (the processor @p arm drives). The seam is
// set between process() calls, outside the timed window, and cleared again so the producer-off twin stays off.
template <typename Arm>
void timeArmProducerOn(Arm& arm, ::Profundum::Processor& proc, double& best) {
    proc.setSpectrumProducerEnabledForTest(true);
    timeArm(arm, best);
    proc.setSpectrumProducerEnabledForTest(false);
}

// Phase 12 T032: kWarmupBlocks producer-on blocks of @p arm (discarded); returns the frames the producer published.
template <typename Arm>
[[nodiscard]] std::uint32_t warmUpProducerOn(Arm& arm, ::Profundum::Processor& proc) {
    const std::uint32_t before = proc.spectrumFramesPublishedForTest();
    proc.setSpectrumProducerEnabledForTest(true);
    for (std::size_t b = 0; b < kWarmupBlocks; ++b) {
        arm.processBlock();
    }
    proc.setSpectrumProducerEnabledForTest(false);
    return proc.spectrumFramesPublishedForTest() - before;
}

}  // namespace

TEST_CASE("Profundum_ProcessorCpu", "[profundum][.perf][performance]") {
    using Engine = Krate::DSP::ProfundumEngine;

    // ---------------------------------------------------------------- event lists (built once)
    Krate::Test::EventList monoNotes;
    monoNotes.addNoteOn(kE1, kVelocity, 0);

    Krate::Test::EventList polyNotes;
    for (const Steinberg::int16 pitch : kChord4) {
        polyNotes.addNoteOn(pitch, kVelocity, 0);
    }

    // ---------------------------------------------------------------- parameter points (built once)
    Krate::Test::ParameterChanges polyMode;  // kVoiceModeId = 1 (Poly)
    polyMode.addChange(::Profundum::kVoiceModeId, 1.0);

    Krate::Test::ParameterChanges maxMono;  // all four macros at 1.0
    for (const Steinberg::Vst::ParamID id : kMacroIds) {
        maxMono.addChange(id, 1.0);
    }

    Krate::Test::ParameterChanges maxPoly;  // Poly + all four macros at 1.0
    maxPoly.addChange(::Profundum::kVoiceModeId, 1.0);
    for (const Steinberg::Vst::ParamID id : kMacroIds) {
        maxPoly.addChange(id, 1.0);
    }

    REQUIRE(polyMode.getParameterCount() == 1);
    REQUIRE(maxMono.getParameterCount() == 4);
    REQUIRE(maxPoly.getParameterCount() == 5);

    // ---------------------------------------------------------------- arms (heap processors / engines, FR-064)
    ProcessorArm pMono;
    ProcessorArm pPoly;
    ProcessorArm pMaxMono;
    ProcessorArm pMaxPoly;
    DirectArm dMono;
    DirectArm dPoly;

    // ---------------------------------------------------------------- warm-up (discarded)
    pMono.warmUp(&monoNotes, nullptr);
    pPoly.warmUp(&polyNotes, &polyMode);
    pMaxMono.warmUp(&monoNotes, &maxMono);
    pMaxPoly.warmUp(&polyNotes, &maxPoly);

    dMono.direct.e->noteOn(static_cast<std::uint8_t>(kE1), kMidiVelocity);
    dMono.warmUp();

    dPoly.direct.e->setVoiceMode(Engine::VoiceMode::Poly);
    for (const Steinberg::int16 pitch : kChord4) {
        dPoly.direct.e->noteOn(static_cast<std::uint8_t>(pitch), kMidiVelocity);
    }
    dPoly.warmUp();

    // Phase 12 T032 (SC-013): the producer-off arms really run without the producer (no open queue: the bare
    // fixture is never connected), and the producer-on arm of the same instance really runs it.
    REQUIRE_FALSE(pMono.fixture.proc->spectrumQueueOpenForTest());
    REQUIRE_FALSE(pPoly.fixture.proc->spectrumQueueOpenForTest());
    const std::uint32_t onFramesMono = warmUpProducerOn(pMono, *pMono.fixture.proc);
    const std::uint32_t onFramesPoly = warmUpProducerOn(pPoly, *pPoly.fixture.proc);
    WARN("SC-013 producer-on warm-up frames published: P-ON-MONO " << onFramesMono << ", P-ON-POLY "
                                                                   << onFramesPoly);
    REQUIRE(onFramesMono > 0u);
    REQUIRE(onFramesPoly > 0u);

    // Recorded: every arm is sounding when timing starts (the last warm-up block).
    WARN("SC-015 last warm-up block peak: P-MONO " << pMono.call.peak() << ", P-POLY "
                                                   << pPoly.call.peak() << ", P-MAX-MONO "
                                                   << pMaxMono.call.peak() << ", P-MAX-POLY "
                                                   << pMaxPoly.call.peak() << ", D-MONO "
                                                   << dMono.peak() << ", D-POLY "
                                                   << dPoly.peak());

    // ---------------------------------------------------------------- interleaved trials
    double bestPMono = std::numeric_limits<double>::max();
    double bestPPoly = std::numeric_limits<double>::max();
    double bestPMaxMono = std::numeric_limits<double>::max();
    double bestPMaxPoly = std::numeric_limits<double>::max();
    double bestDMono = std::numeric_limits<double>::max();
    double bestDPoly = std::numeric_limits<double>::max();
    double bestPOnMono = std::numeric_limits<double>::max();  // Phase 12 T032 (SC-013)
    double bestPOnPoly = std::numeric_limits<double>::max();
    for (std::size_t trial = 0; trial < kTrials; ++trial) {
        if (trial % 2 == 0) {
            timeArm(pMono, bestPMono);
            timeArmProducerOn(pMono, *pMono.fixture.proc, bestPOnMono);
            timeArm(dMono, bestDMono);
            timeArm(pPoly, bestPPoly);
            timeArmProducerOn(pPoly, *pPoly.fixture.proc, bestPOnPoly);
            timeArm(dPoly, bestDPoly);
        } else {
            timeArm(dMono, bestDMono);
            timeArmProducerOn(pMono, *pMono.fixture.proc, bestPOnMono);
            timeArm(pMono, bestPMono);
            timeArm(dPoly, bestDPoly);
            timeArmProducerOn(pPoly, *pPoly.fixture.proc, bestPOnPoly);
            timeArm(pPoly, bestPPoly);
        }
        timeArm(pMaxMono, bestPMaxMono);
        timeArm(pMaxPoly, bestPMaxPoly);
    }

    const double pctPMono = pctOf(bestPMono);
    const double pctPPoly = pctOf(bestPPoly);
    const double pctPMaxMono = pctOf(bestPMaxMono);
    const double pctPMaxPoly = pctOf(bestPMaxPoly);
    const double pctDMono = pctOf(bestDMono);
    const double pctDPoly = pctOf(bestDPoly);
    const double ratioMono = bestPMono / bestDMono;
    const double ratioPoly = bestPPoly / bestDPoly;
    const double pctPOnMono = pctOf(bestPOnMono);
    const double pctPOnPoly = pctOf(bestPOnPoly);
    const double ratioOnMono = bestPOnMono / bestDMono;
    const double ratioOnPoly = bestPOnPoly / bestDPoly;

    WARN("SC-015 best ns per " << kBlocksPerTrial << " blocks (512 @ 48 kHz, real time "
                               << trialPeriodNs() << " ns): P-MONO " << bestPMono << ", D-MONO "
                               << bestDMono << ", P-POLY " << bestPPoly << ", D-POLY " << bestDPoly
                               << ", P-MAX-MONO " << bestPMaxMono << ", P-MAX-POLY "
                               << bestPMaxPoly);
    WARN("SC-015 pct P-MONO = " << pctPMono << " % (bar <= " << kMonoPctBar << ")");
    WARN("SC-015 pct P-POLY = " << pctPPoly << " % (bar <= " << kPolyPctBar << ")");
    WARN("SC-015 pct D-MONO = " << pctDMono << " %, pct D-POLY = " << pctDPoly << " % (recorded)");
    WARN("SC-015 ratio P-MONO / D-MONO = " << ratioMono << " (bar <= " << kWrapperOverheadCeiling
                                           << ")");
    WARN("SC-015 ratio P-POLY / D-POLY = " << ratioPoly << " (bar <= " << kWrapperOverheadCeiling
                                           << ")");
    WARN("SC-015 pct P-MAX-MONO (all four macros 1.0) = " << pctPMaxMono << " % (bar <= "
                                                           << kMonoPctBar << ", Q-A gated)");
    WARN("SC-015 pct P-MAX-POLY (all four macros 1.0) = " << pctPMaxPoly << " % (bar <= "
                                                           << kPolyPctBar << ", Q-A gated)");
    WARN("SC-013 best ns per " << kBlocksPerTrial << " blocks: P-ON-MONO " << bestPOnMono << ", P-ON-POLY "
                               << bestPOnPoly << " (producer on, same instance as P-MONO / P-POLY)");
    WARN("SC-013 pct P-ON-MONO = " << pctPOnMono << " % (bar <= " << kMonoPctBar << ")");
    WARN("SC-013 pct P-ON-POLY = " << pctPOnPoly << " % (bar <= " << kPolyPctBar << ")");
    WARN("SC-013 ratio P-ON-MONO / D-MONO = " << ratioOnMono << " (bar <= " << kWrapperOverheadCeiling << ")");
    WARN("SC-013 ratio P-ON-POLY / D-POLY = " << ratioOnPoly << " (bar <= " << kWrapperOverheadCeiling << ")");
    WARN("Phase 9 reference MONO " << kPhase9MonoShowcasePct << " % / POLY4 "
                                   << kPhase9PolyShowcasePct
                                   << " %: showcase setting, not comparable (P-1)");

    REQUIRE(bestPMono <= kWrapperOverheadCeiling * bestDMono);
    REQUIRE(bestPPoly <= kWrapperOverheadCeiling * bestDPoly);
    REQUIRE(pctPMono <= kMonoPctBar);
    REQUIRE(pctPPoly <= kPolyPctBar);
    REQUIRE(pctPMaxMono <= kMonoPctBar);  // Q-A (user ruling 2026-10-22)
    REQUIRE(pctPMaxPoly <= kPolyPctBar);  // Q-A (user ruling 2026-10-22)
    REQUIRE(bestPOnMono <= kWrapperOverheadCeiling * bestDMono);  // Phase 12 T032 (SC-013)
    REQUIRE(bestPOnPoly <= kWrapperOverheadCeiling * bestDPoly);
    REQUIRE(pctPOnMono <= kMonoPctBar);
    REQUIRE(pctPOnPoly <= kPolyPctBar);
}

// ==============================================================================
// Profundum_ProcessorCpuFullSurface (Phase 11 T027; FR-066, SC-020)
// ==============================================================================
// HIDDEN ([.perf]) and wall-clock based, like Profundum_ProcessorCpu above: run
// it ALONE (node tools/run-cpu-tests.js profundum_tests), by T033 only.
//
// It extends the Phase 10 interleaved harness (kFs, kBlock, kWarmupBlocks,
// kTrials, kBlocksPerTrial, ProcessorArm, DirectArm, timeArm; the bars
// kMonoPctBar, kPolyPctBar, kWrapperOverheadCeiling unchanged) to the full
// Phase 11 surface. Every arm is a pair P (processor) / D (engine direct):
//
//   MONO, POLY (4 voices), MAX-MONO, MAX-POLY (all four macros 100-103 at 1.0)
//       Distinct A / B endpoints written through the host surface (Edit Target
//       1001 = A, then B), Morph Position 105 = 0.5, Motion on in both
//       endpoints (A: Cycle -> Mass, B: Drift -> Depth). Timed blocks carry no
//       parameter changes. D gets the processor's own engine endpoints and
//       position, so both sides render the same configuration.
//   AUTO-MONO, AUTO-POLY, AUTO-MAX-MONO, AUTO-MAX-POLY
//       The same configurations plus the automation arm: block k (warm-up and
//       timed alike) carries [1001 = k % 2, 200 = v_k], so the processor
//       assembles and hands the edited endpoint to setEndpoint every block. D
//       calls setEndpoint(k % 2, the identical assembled state) before each of
//       its blocks. v_k cycles through kAutoCycle distinct values, so every
//       block changes the edited endpoint's Depth leaf.
//
// Phase 12 T032 (SC-013, FR-075): every configuration also has a producer-on
// arm, P-ON: the same processor instance as P, with
// setSpectrumProducerEnabledForTest(true) set for its timed blocks only, under
// the same bars and the same P / D ceiling against the same-engine D.
//
// Bars: MONO-class arms <= kMonoPctBar, POLY-class arms <= kPolyPctBar,
// P / D <= kWrapperOverheadCeiling, in every arm. No observer is attached. Every
// percentage is printed with WARN. Never relax a bar or shrink the workload.
// ==============================================================================

namespace {

using SurfaceEndpoint = Krate::DSP::BassMorphEngine::Endpoint;
using SurfaceMotion = Krate::DSP::MorphState::MotionConfig;

constexpr std::size_t kAutoCycle = 16;  // even: block k edits endpoint k % 2 in every cycle
static_assert(kAutoCycle % 2 == 0, "the automation cycle must keep the A / B alternation");
static_assert((kWarmupBlocks - 1) % 2 == 1, "the last warm-up block of an automation arm edits endpoint B");
constexpr double kSurfacePosition = 0.5;

// v_k: kAutoCycle distinct normalised Depth values (consecutive edits of one endpoint always differ).
[[nodiscard]] double autoDepthNorm(std::size_t k) noexcept {
    return 0.2 + 0.6 * static_cast<double>(k % kAutoCycle) / static_cast<double>(kAutoCycle - 1);
}

[[nodiscard]] std::uint8_t surfaceLeafOf(Steinberg::Vst::ParamID id) {
    const ::Profundum::SoundParamRow* row = ::Profundum::soundRowOf(id);
    REQUIRE(row != nullptr);
    return row->leaf;
}

// The normalised value of the plain value @p plain of sound parameter @p id (list rows: the index).
[[nodiscard]] double surfaceNormOf(Steinberg::Vst::ParamID id, double plain) {
    return ::Profundum::normalizedFromPlain(::Profundum::lawOf(surfaceLeafOf(id)), plain);
}

[[nodiscard]] double motionSourceNorm(SurfaceMotion::Source s) {
    return surfaceNormOf(::Profundum::kMotionSourceId, static_cast<double>(static_cast<int>(s)));
}

[[nodiscard]] double motionTargetNorm(SurfaceMotion::Target t) {
    return surfaceNormOf(::Profundum::kMotionTargetId, static_cast<double>(static_cast<int>(t)));
}

// Endpoint A's sound values (Edit Target A must be set in the same block).
void addEndpointAValues(Krate::Test::ParameterChanges& pc) {
    pc.addChange(::Profundum::kCoreDepthId, 0.3);
    pc.addChange(::Profundum::kCoreBodyId, 0.25);
    pc.addChange(::Profundum::kCoreEdgeId, 0.3);
    pc.addChange(::Profundum::kFoldAmountId, 0.25);
    pc.addChange(::Profundum::kCharacterAmountId, 0.35);
    pc.addChange(::Profundum::kMotionSourceId, motionSourceNorm(SurfaceMotion::Source::Cycle));
    pc.addChange(::Profundum::kMotionTargetId, motionTargetNorm(SurfaceMotion::Target::Mass));
    pc.addChange(::Profundum::kMotionAmountId, surfaceNormOf(::Profundum::kMotionAmountId, 1.0));
    pc.addChange(::Profundum::kMotionDepthId, surfaceNormOf(::Profundum::kMotionDepthId, 1.0));
    pc.addChange(::Profundum::kMotionCycleRateId, 0.5);
}

// Endpoint B's sound values (Edit Target B must be set in the same block).
void addEndpointBValues(Krate::Test::ParameterChanges& pc) {
    pc.addChange(::Profundum::kCoreDepthId, 0.8);
    pc.addChange(::Profundum::kCoreBodyId, 0.7);
    pc.addChange(::Profundum::kCoreEdgeId, 0.65);
    pc.addChange(::Profundum::kFoldAmountId, 0.6);
    pc.addChange(::Profundum::kCharacterAmountId, 0.75);
    pc.addChange(::Profundum::kMotionSourceId, motionSourceNorm(SurfaceMotion::Source::Drift));
    pc.addChange(::Profundum::kMotionTargetId, motionTargetNorm(SurfaceMotion::Target::Depth));
    pc.addChange(::Profundum::kMotionAmountId, surfaceNormOf(::Profundum::kMotionAmountId, 0.7));
    pc.addChange(::Profundum::kMotionDepthId, surfaceNormOf(::Profundum::kMotionDepthId, 0.8));
}

struct SurfaceConfig {
    const char* name;
    bool poly;
    bool maxMacros;
    bool automate;
};

constexpr std::array<SurfaceConfig, 8> kSurfaceConfigs{{
    {.name = "MONO", .poly = false, .maxMacros = false, .automate = false},
    {.name = "POLY", .poly = true, .maxMacros = false, .automate = false},
    {.name = "MAX-MONO", .poly = false, .maxMacros = true, .automate = false},
    {.name = "MAX-POLY", .poly = true, .maxMacros = true, .automate = false},
    {.name = "AUTO-MONO", .poly = false, .maxMacros = false, .automate = true},
    {.name = "AUTO-POLY", .poly = true, .maxMacros = false, .automate = true},
    {.name = "AUTO-MAX-MONO", .poly = false, .maxMacros = true, .automate = true},
    {.name = "AUTO-MAX-POLY", .poly = true, .maxMacros = true, .automate = true},
}};

// A processor arm whose blocks optionally carry the automation pool's entry k % kAutoCycle.
struct SurfaceProcessorArm {
    ProcessorArm arm;
    std::array<Krate::Test::ParameterChanges, kAutoCycle>* autoPool = nullptr;  // null: static arm
    std::size_t k = 0;

    void processBlock() noexcept {
        if (autoPool != nullptr) {
            arm.call.data.inputParameterChanges = &(*autoPool)[k % kAutoCycle];
            ++k;
        }
        arm.processBlock();
    }

    void warmUp() noexcept {
        for (std::size_t b = 0; b < kWarmupBlocks; ++b) {
            processBlock();
        }
    }
};

// A direct arm that optionally hands setEndpoint(k % 2, autoStates[k % kAutoCycle]) before each block.
struct SurfaceDirectArm {
    DirectArm arm;
    const std::array<Krate::DSP::MorphState, kAutoCycle>* autoStates = nullptr;  // null: static arm
    Krate::DSP::ProfundumEngine* shared = nullptr;  // set: drive this engine (a processor's own) instead of arm's
    std::size_t k = 0;

    void processBlock() noexcept {
        Krate::DSP::ProfundumEngine& e = (shared != nullptr) ? *shared : *arm.direct.e;
        if (autoStates != nullptr) {
            const SurfaceEndpoint ep = (k % 2 == 1) ? SurfaceEndpoint::B : SurfaceEndpoint::A;
            static_cast<void>(e.setEndpoint(ep, (*autoStates)[k % kAutoCycle]));
            ++k;
        }
        if (shared == nullptr) {
            arm.processBlock();
            return;
        }
        float* l = arm.direct.outL.data();
        float* r = arm.direct.outR.data();
        e.processBlock(l, r, kBlock);
        for (std::size_t s = 0; s < kBlock; ++s) {   // the processor's per-sample output-gain loop, as DirectArm
            const float gain = arm.direct.g.process();
            l[s] *= gain;
            r[s] *= gain;
        }
    }

    void warmUp() noexcept {
        for (std::size_t b = 0; b < kWarmupBlocks; ++b) {
            processBlock();
        }
    }
};

struct SurfacePair {
    SurfaceConfig cfg;
    SurfaceProcessorArm p;
    SurfaceDirectArm d;
    SurfaceDirectArm s;    // D on the SAME engine as P (the ratio's denominator; SC-020 ruling)
    double bestP = std::numeric_limits<double>::max();
    double bestPOn = std::numeric_limits<double>::max();  // Phase 12 T032 (SC-013): P with the producer on
    double bestD = std::numeric_limits<double>::max();   // separate instance: recorded only
    double bestS = std::numeric_limits<double>::max();
    double bestS2 = std::numeric_limits<double>::max();  // control: the same D path timed again

    explicit SurfacePair(const SurfaceConfig& c) : cfg(c) {}

    [[nodiscard]] double pctBar() const noexcept { return cfg.poly ? kPolyPctBar : kMonoPctBar; }
};

// Two configuration blocks through the host surface: block 1 = Edit Target A, voice mode, position,
// macros (MAX arms only) and endpoint A; block 2 = Edit Target B, endpoint B and the note-ons.
void configureProcessor(ProcessorArm& arm, const SurfaceConfig& cfg, Steinberg::Vst::IEventList* notes) {
    Krate::Test::ParameterChanges blockA;
    blockA.addChange(::Profundum::kMorphEditTargetId, 0.0);
    blockA.addChange(::Profundum::kVoiceModeId, cfg.poly ? 1.0 : 0.0);
    blockA.addChange(::Profundum::kMorphPositionId, kSurfacePosition);
    if (cfg.maxMacros) {
        for (const Steinberg::Vst::ParamID id : kMacroIds) {
            blockA.addChange(id, 1.0);
        }
    }
    addEndpointAValues(blockA);

    Krate::Test::ParameterChanges blockB;
    blockB.addChange(::Profundum::kMorphEditTargetId, 1.0);
    addEndpointBValues(blockB);

    arm.call.data.inputParameterChanges = &blockA;
    REQUIRE(arm.fixture.proc->process(arm.call.data) == Steinberg::kResultOk);
    arm.call.data.inputParameterChanges = &blockB;
    arm.call.data.inputEvents = notes;
    REQUIRE(arm.fixture.proc->process(arm.call.data) == Steinberg::kResultOk);
    arm.call.data.inputParameterChanges = nullptr;
    arm.call.data.inputEvents = nullptr;
}

// The same configuration on the bare engine: the processor's endpoints and position, the same notes.
void configureDirect(DirectArm& arm, const SurfaceConfig& cfg, const Krate::DSP::MorphState& a,
                     const Krate::DSP::MorphState& b, float position) {
    using Engine = Krate::DSP::ProfundumEngine;
    Engine& e = *arm.direct.e;
    if (cfg.poly) {
        e.setVoiceMode(Engine::VoiceMode::Poly);
    }
    if (cfg.maxMacros) {
        Krate::DSP::ProfundumMacroValues v;
        v.weight = 1.0f;
        v.darkBright = 1.0f;
        v.tightLoose = 1.0f;
        v.cleanViolent = 1.0f;
        e.setMacros(v);
    }
    REQUIRE(e.setEndpoint(SurfaceEndpoint::A, a));
    REQUIRE(e.setEndpoint(SurfaceEndpoint::B, b));
    e.setMorphPosition(position);
    if (cfg.poly) {
        for (const Steinberg::int16 pitch : kChord4) {
            e.noteOn(static_cast<std::uint8_t>(pitch), kMidiVelocity);
        }
    } else {
        e.noteOn(static_cast<std::uint8_t>(kE1), kMidiVelocity);
    }
}

}  // namespace

TEST_CASE("Profundum_ProcessorCpuFullSurface", "[profundum][.perf][performance]") {
    const std::uint8_t depthLeaf = surfaceLeafOf(::Profundum::kCoreDepthId);

    // ---------------------------------------------------------------- event lists (built once)
    Krate::Test::EventList monoNotes;
    monoNotes.addNoteOn(kE1, kVelocity, 0);

    Krate::Test::EventList polyNotes;
    for (const Steinberg::int16 pitch : kChord4) {
        polyNotes.addNoteOn(pitch, kVelocity, 0);
    }

    // ---------------------------------------------------------------- automation pool (built once, before timing)
    std::array<Krate::Test::ParameterChanges, kAutoCycle> autoPool;
    for (std::size_t k = 0; k < kAutoCycle; ++k) {
        autoPool[k].addChange(::Profundum::kMorphEditTargetId, static_cast<double>(k % 2));
        autoPool[k].addChange(::Profundum::kCoreDepthId, autoDepthNorm(k));
        REQUIRE(autoPool[k].getParameterCount() == 2);
    }

    // ---------------------------------------------------------------- arms (heap processors / engines, FR-064)
    std::vector<std::unique_ptr<SurfacePair>> pairs;
    pairs.reserve(kSurfaceConfigs.size());
    for (const SurfaceConfig& cfg : kSurfaceConfigs) {
        pairs.push_back(std::make_unique<SurfacePair>(cfg));
    }

    // ---------------------------------------------------------------- processor arms: configure + warm-up
    for (auto& pair : pairs) {
        configureProcessor(pair->p.arm, pair->cfg, pair->cfg.poly ? &polyNotes : &monoNotes);
        if (pair->cfg.automate) {
            pair->p.autoPool = &autoPool;
        }
        pair->p.warmUp();
    }

    // The configured endpoints, read from the static MONO arm's engine (its endpoints never change after
    // the two configuration blocks).
    REQUIRE(!pairs.front()->cfg.automate);
    const Krate::DSP::ProfundumEngine* ref = pairs.front()->p.arm.fixture.engine();
    REQUIRE(ref != nullptr);
    const Krate::DSP::MorphState baseA = ref->getEndpoint(SurfaceEndpoint::A);
    const Krate::DSP::MorphState baseB = ref->getEndpoint(SurfaceEndpoint::B);
    const ::Profundum::LeafBits baseBitsA = ::Profundum::leafBitsOf(baseA);
    const ::Profundum::LeafBits baseBitsB = ::Profundum::leafBitsOf(baseB);
    const float position = ref->morphPosition();

    // Non-vacuity: distinct endpoints, Motion on in both, position 0.5.
    REQUIRE(baseBitsA != baseBitsB);
    REQUIRE(baseBitsA[kLeafMotionSource] == static_cast<std::uint32_t>(SurfaceMotion::Source::Cycle));
    REQUIRE(baseBitsB[kLeafMotionSource] == static_cast<std::uint32_t>(SurfaceMotion::Source::Drift));
    REQUIRE(position == static_cast<float>(kSurfacePosition));

    // The direct arms' automation states: block k's endpoint (k % 2) assembled exactly as the processor
    // assembles it (morphStateFromLeafBits of the slot words, the Depth word written by leafWordFromNormalized).
    std::array<Krate::DSP::MorphState, kAutoCycle> autoStates{};
    for (std::size_t k = 0; k < kAutoCycle; ++k) {
        ::Profundum::LeafBits bits = (k % 2 == 1) ? baseBitsB : baseBitsA;
        bits[depthLeaf] = ::Profundum::leafWordFromNormalized(depthLeaf, autoDepthNorm(k));
        autoStates[k] = ::Profundum::morphStateFromLeafBits(bits);
        REQUIRE(Krate::DSP::isValidMorphState(autoStates[k]));
    }

    // ---------------------------------------------------------------- direct arms: configure + warm-up
    for (auto& pair : pairs) {
        configureDirect(pair->d.arm, pair->cfg, baseA, baseB, position);
        pair->s.shared = pair->p.arm.fixture.proc->mutableEngineForTest();
        REQUIRE(pair->s.shared != nullptr);
        if (pair->cfg.automate) {
            pair->d.autoStates = &autoStates;
            pair->s.autoStates = &autoStates;
        }
        pair->d.warmUp();
    }

    // ---------------------------------------------------------------- parity + non-vacuity (before timing)
    const std::uint32_t lastAutoDepthWord =
        ::Profundum::leafWordFromNormalized(depthLeaf, autoDepthNorm(kWarmupBlocks - 1));
    for (const auto& pair : pairs) {
        INFO("arm " << pair->cfg.name);
        const ::Profundum::Processor& proc = *pair->p.arm.fixture.proc;
        const Krate::DSP::ProfundumEngine* pe = pair->p.arm.fixture.engine();
        const Krate::DSP::ProfundumEngine* de = pair->d.arm.direct.e.get();
        REQUIRE(pe != nullptr);
        REQUIRE(proc.endpointRejectionsForTest() == 0u);
        // P and D hold the same endpoints and position: the two sides render the same configuration.
        REQUIRE(::Profundum::leafBitsOf(pe->getEndpoint(SurfaceEndpoint::A)) ==
                ::Profundum::leafBitsOf(de->getEndpoint(SurfaceEndpoint::A)));
        REQUIRE(::Profundum::leafBitsOf(pe->getEndpoint(SurfaceEndpoint::B)) ==
                ::Profundum::leafBitsOf(de->getEndpoint(SurfaceEndpoint::B)));
        REQUIRE(pe->morphPosition() == de->morphPosition());
        if (pair->cfg.automate) {
            // The last warm-up block (k = kWarmupBlocks - 1, odd) edited B's Depth through the host surface.
            REQUIRE(pair->p.k == kWarmupBlocks);
            REQUIRE(pair->d.k == kWarmupBlocks);
            REQUIRE(proc.endpointSlotForTest(SurfaceEndpoint::B, depthLeaf) == lastAutoDepthWord);
            REQUIRE(::Profundum::leafBitsOf(pe->getEndpoint(SurfaceEndpoint::B))[depthLeaf] == lastAutoDepthWord);
        } else {
            REQUIRE(::Profundum::leafBitsOf(pe->getEndpoint(SurfaceEndpoint::A)) == baseBitsA);
            REQUIRE(::Profundum::leafBitsOf(pe->getEndpoint(SurfaceEndpoint::B)) == baseBitsB);
        }
        // Recorded: every arm is sounding when timing starts (the last warm-up block).
        WARN("SC-020 last warm-up block peak " << pair->cfg.name << ": P " << pair->p.arm.call.peak() << ", D "
                                               << pair->d.arm.peak());
    }

    // Phase 12 T032 (SC-013): P really runs without the producer (the bare fixture is never connected, so no queue
    // opens), and P-ON, the same instance, really runs it. After the parity checks: these blocks advance p.k.
    for (auto& pair : pairs) {
        INFO("arm " << pair->cfg.name);
        ::Profundum::Processor& proc = *pair->p.arm.fixture.proc;
        REQUIRE_FALSE(proc.spectrumQueueOpenForTest());
        const std::uint32_t onFrames = warmUpProducerOn(pair->p, proc);
        WARN("SC-013 " << pair->cfg.name << ": producer-on warm-up frames published " << onFrames);
        REQUIRE(onFrames > 0u);
    }

    // ---------------------------------------------------------------- interleaved trials
    for (std::size_t trial = 0; trial < kTrials; ++trial) {
        for (auto& pair : pairs) {
            ::Profundum::Processor& proc = *pair->p.arm.fixture.proc;
            if (trial % 2 == 0) {
                timeArm(pair->p, pair->bestP);
                timeArmProducerOn(pair->p, proc, pair->bestPOn);
                timeArm(pair->s, pair->bestS);
                timeArm(pair->s, pair->bestS2);
            } else {
                timeArm(pair->s, pair->bestS2);
                timeArm(pair->s, pair->bestS);
                timeArmProducerOn(pair->p, proc, pair->bestPOn);
                timeArm(pair->p, pair->bestP);
            }
            timeArm(pair->d, pair->bestD);
        }
    }

    // ---------------------------------------------------------------- report every arm, then gate every arm
    WARN("SC-020 real time per " << kBlocksPerTrial << " blocks (512 @ 48 kHz) = " << trialPeriodNs() << " ns");
    for (const auto& pair : pairs) {
        const double pctP = pctOf(pair->bestP);
        const double pctD = pctOf(pair->bestD);
        const double ratio = pair->bestP / pair->bestS;   // one engine instance (SC-020 ruling)
        WARN("SC-020 " << pair->cfg.name << ": best ns P " << pair->bestP << ", D (same engine) " << pair->bestS
                       << ", D (separate engine) " << pair->bestD << "; pct P = " << pctP << " % (bar <= " << pair->pctBar()
                       << "), pct D separate = " << pctD << " % (recorded); ratio P / D = " << ratio << " (bar <= "
                       << kWrapperOverheadCeiling << ")");
        WARN("SC-013 " << pair->cfg.name << " P-ON (producer on, same instance as P): best ns " << pair->bestPOn
                       << "; pct P-ON = " << pctOf(pair->bestPOn) << " % (bar <= " << pair->pctBar()
                       << "); ratio P-ON / D = " << pair->bestPOn / pair->bestS << " (bar <= "
                       << kWrapperOverheadCeiling << ")");
    }
    for (const auto& pair : pairs) {
        INFO("arm " << pair->cfg.name);
        // Control: two identical engine-direct instances must agree within the ratio bar, or this run measured the
        // machine. INVALID fails and is re-run alone; it never passes (SC-020 ruling).
        const double control = pair->bestS2 / pair->bestS;
        WARN("SC-020 " << pair->cfg.name << ": control S2 / S (same D path twice) = " << control << " (valid within [" << 1.0 / kControlRatioCeiling
                       << ", " << kControlRatioCeiling << "])");
        if (control > kControlRatioCeiling || control < 1.0 / kControlRatioCeiling) {
            FAIL_CHECK("SC-020 " << pair->cfg.name << " INVALID: control S2 / S = " << control
                                 << " (noise floor above the ratio bar); re-run alone on an idle machine");
        }
        CHECK(pctOf(pair->bestP) <= pair->pctBar());
        CHECK(pair->bestP <= kWrapperOverheadCeiling * pair->bestS);
        CHECK(pctOf(pair->bestPOn) <= pair->pctBar());                   // Phase 12 T032 (SC-013)
        CHECK(pair->bestPOn <= kWrapperOverheadCeiling * pair->bestS);
    }
}
