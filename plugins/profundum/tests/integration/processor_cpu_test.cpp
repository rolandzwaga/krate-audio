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
//
// All six arms run in ONE interleaved trial loop. best_X = the minimum over
// trials of ns per kBlocksPerTrial blocks; pct_X = best_X / (100 x 512 / 48000 x
// 1e9) x 100.
//
// The TU is deliberately NOT in the -fno-fast-math list (tests/CMakeLists.txt).
//
// NAMESPACE HAZARD (profundum_test_fixture.h): plugin types are spelled
// ::Profundum::..., and Krate::DSP::ProfundumTest is never opened.
// ==============================================================================

#include "plugin_ids.h"
#include "profundum_test_fixture.h"

#include <vst_event_list.h>

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
#include <vector>

using namespace ProfundumPluginTest;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kWarmupBlocks = 200;
constexpr std::size_t kTrials = 16;
constexpr std::size_t kBlocksPerTrial = 100;

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
    for (std::size_t trial = 0; trial < kTrials; ++trial) {
        timeArm(pMono, bestPMono);
        timeArm(dMono, bestDMono);
        timeArm(pPoly, bestPPoly);
        timeArm(dPoly, bestDPoly);
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
    WARN("Phase 9 reference MONO " << kPhase9MonoShowcasePct << " % / POLY4 "
                                   << kPhase9PolyShowcasePct
                                   << " %: showcase setting, not comparable (P-1)");

    REQUIRE(bestPMono <= kWrapperOverheadCeiling * bestDMono);
    REQUIRE(bestPPoly <= kWrapperOverheadCeiling * bestDPoly);
    REQUIRE(pctPMono <= kMonoPctBar);
    REQUIRE(pctPPoly <= kPolyPctBar);
    REQUIRE(pctPMaxMono <= kMonoPctBar);  // Q-A (user ruling 2026-10-22)
    REQUIRE(pctPMaxPoly <= kPolyPctBar);  // Q-A (user ruling 2026-10-22)
}
