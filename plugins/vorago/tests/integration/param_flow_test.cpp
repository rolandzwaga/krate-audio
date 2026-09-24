// ==============================================================================
// Vorago - parameter flow tests (SC-008 (2), SC-019, SC-023)
// ==============================================================================
// Vorago_ParamFlowReachesEngine (T014): the two globals reach the render chain
// (SC-019, with P-2's non-vacuity and snap arms), the twelve macros are inert in
// Phase 11 (SC-023), and parameter timing is block-granular (SC-008 (2)).
// ==============================================================================

#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include "public.sdk/source/common/memorystream.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr float kVelocity100 = 100.0f / 127.0f;
constexpr std::size_t kFourSecondBlocks = 375;  // 4 s at 512
constexpr std::size_t kLatencySamples = 3072;   // FR-033: smear 2048 + cavern 1024

void prepareFixture(VoragoTest::ProcessorFixture& fx) {
    fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
}

// Renders numBlocks blocks of kBlock. `events` is attached to block 0 only;
// `params` to block `paramsAtBlock` only.
void renderBlocks(VoragoTest::ProcessorFixture& fx, std::size_t numBlocks,
                  Steinberg::Vst::IEventList* events, Steinberg::Vst::IParameterChanges* params,
                  std::size_t paramsAtBlock = 0) {
    fx.reserveCapture(fx.capturedL.size() + numBlocks * kBlock);
    for (std::size_t b = 0; b < numBlocks; ++b) {
        REQUIRE(fx.processBlock(kBlock, (b == 0) ? events : nullptr,
                                (b == paramsAtBlock) ? params : nullptr) == Steinberg::kResultOk);
    }
}

// One rendered block carrying only `params` (no events).
void paramBlock(VoragoTest::ProcessorFixture& fx, Steinberg::Vst::IParameterChanges* params) {
    fx.reserveCapture(fx.capturedL.size() + kBlock);
    REQUIRE(fx.processBlock(kBlock, nullptr, params) == Steinberg::kResultOk);
}

[[nodiscard]] float stereoPeak(const VoragoTest::ProcessorFixture& fx, std::size_t from = 0) {
    const auto l = std::span<const float>(fx.capturedL);
    const auto r = std::span<const float>(fx.capturedR);
    REQUIRE(from <= l.size());
    REQUIRE(from <= r.size());
    return std::max(VoragoTest::peakOf(l.subspan(from)), VoragoTest::peakOf(r.subspan(from)));
}

// SC-019.1 script: master gain `gainNorm` in block 0, NoteOn(48, 100) @0, 4 s.
// Returns the stereo peak; `snapValue` receives masterGainValueForTest() read
// right after block 0.
float renderGainScript(double gainNorm, float& snapValue) {
    VoragoTest::ProcessorFixture fx;
    prepareFixture(fx);

    Krate::Test::ParameterChanges pc;
    pc.addChange(::Vorago::kMasterGainId, gainNorm);
    Krate::Test::EventList ev;
    ev.addNoteOn(48, kVelocity100, 0);

    fx.reserveCapture(kFourSecondBlocks * kBlock);
    REQUIRE(fx.processBlock(kBlock, &ev, &pc) == Steinberg::kResultOk);
    snapValue = fx.proc->masterGainValueForTest();
    renderBlocks(fx, kFourSecondBlocks - 1, nullptr, nullptr);

    REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedL)));
    REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedR)));
    return stereoPeak(fx);
}

}  // namespace

TEST_CASE("Vorago_ParamFlowReachesEngine", "[vorago][integration]") {
    SECTION("GainZeroSilences") {
        // SC-019.1: gain 0 reaches the chain.
        float snapAtZero = -1.0f;
        const float peakZero = renderGainScript(0.0, snapAtZero);
        WARN("SC-019.1 gain 0 peak=" << peakZero << " gain after block 0=" << snapAtZero);
        REQUIRE(peakZero < 1.0e-6f);

        // Snap arm (P-2): without the first-block snap a smoother constructed at
        // 1.0f would still read ~0.086 after 512 samples.
        REQUIRE(snapAtZero == 0.0f);

        // Non-vacuity (P-2): the same script at unity gain is audible.
        float snapAtUnity = -1.0f;
        const float peakUnity = renderGainScript(0.5, snapAtUnity);
        WARN("SC-019.1 non-vacuity gain 1.0 peak=" << peakUnity);
        REQUIRE(peakUnity >= 1.0e-4f);
    }

    SECTION("PolyphonyReachesEngine") {
        // SC-019.2
        VoragoTest::ProcessorFixture fx;
        prepareFixture(fx);

        Krate::Test::ParameterChanges low;
        low.addChange(::Vorago::kPolyphonyId, 0.0);
        paramBlock(fx, &low);
        REQUIRE(fx.proc->engineForTest()->getPolyphony() == 1u);

        Krate::Test::ParameterChanges high;
        high.addChange(::Vorago::kPolyphonyId, 1.0);
        paramBlock(fx, &high);
        REQUIRE(fx.proc->engineForTest()->getPolyphony() == 6u);
    }

    SECTION("PolyphonyPushIsEdgeTriggered") {
        // SC-019.3: the seam proves itself - a changed value costs exactly one
        // setPolyphony call, a repeated value none.
        VoragoTest::ProcessorFixture fx;
        prepareFixture(fx);
        const std::uint32_t c0 = fx.proc->setPolyphonyCallCountForTest();

        Krate::Test::ParameterChanges two;
        two.addChange(::Vorago::kPolyphonyId, 0.2);  // -> 2 voices
        paramBlock(fx, &two);
        REQUIRE(fx.proc->engineForTest()->getPolyphony() == 2u);
        REQUIRE(fx.proc->setPolyphonyCallCountForTest() == c0 + 1u);

        paramBlock(fx, &two);  // same value again
        REQUIRE(fx.proc->setPolyphonyCallCountForTest() == c0 + 1u);

        for (int i = 0; i < 3; ++i) {  // no changes at all
            paramBlock(fx, nullptr);
        }
        REQUIRE(fx.proc->setPolyphonyCallCountForTest() == c0 + 1u);
    }

    SECTION("StateBeforePrepare") {
        // SC-019.4: a state loaded before setupProcessing() reaches the engine.
        VoragoTest::ProcessorFixture a;
        prepareFixture(a);
        Krate::Test::ParameterChanges two;
        two.addChange(::Vorago::kPolyphonyId, 0.2);  // -> 2 voices
        paramBlock(a, &two);
        REQUIRE(a.proc->engineForTest()->getPolyphony() == 2u);

        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        REQUIRE(a.proc->getState(stream) == Steinberg::kResultOk);
        REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);

        VoragoTest::ProcessorFixture b;  // initialize only
        REQUIRE(b.proc->setState(stream) == Steinberg::kResultOk);
        prepareFixture(b);
        REQUIRE(b.proc->engineForTest()->getPolyphony() == 2u);
    }

    SECTION("MacrosAreInert") {
        // SC-023: all twelve macros at 1.0 render identically to the defaults.
        Krate::Test::ParameterChanges macrosHigh;
        for (Steinberg::Vst::ParamID id = ::Vorago::kMacroDarknessId;
             id <= ::Vorago::kMacroMassId; ++id) {
            macrosHigh.addChange(id, 1.0);
        }

        VoragoTest::ProcessorFixture m0;
        VoragoTest::ProcessorFixture m1;
        prepareFixture(m0);
        prepareFixture(m1);

        Krate::Test::EventList ev;
        ev.addNoteOn(48, kVelocity100, 0);
        renderBlocks(m0, kFourSecondBlocks, &ev, nullptr);
        renderBlocks(m1, kFourSecondBlocks, &ev, &macrosHigh);

        // The macro atomics really moved (the render is not trivially identical
        // because the changes were dropped).
        REQUIRE(m1.proc->macroParamsForTest().darkness.load() == 1.0f);

        const float precondition = stereoPeak(m0, kLatencySamples);
        WARN("SC-023 precondition peak(M0, [3072, end))=" << precondition);
        REQUIRE(precondition >= 1.0e-4f);

        const float diffL = VoragoTest::maxAbsDiff(std::span<const float>(m0.capturedL),
                                                   std::span<const float>(m1.capturedL));
        const float diffR = VoragoTest::maxAbsDiff(std::span<const float>(m0.capturedR),
                                                   std::span<const float>(m1.capturedR));
        INFO("maxAbsDiff(M0, M1) L=" << diffL << " R=" << diffR);
        REQUIRE(diffL <= 1.0e-5f);
        REQUIRE(diffR <= 1.0e-5f);
    }

    SECTION("ParamTimingIsBlockGranular") {
        // SC-008 (2): a gain change at offset 300 of block N renders exactly like
        // the same change at offset 0 of block N.
        // >= 1 s warm-up (spec SC-008 (2)). Lengthened from 94 blocks (~1 s) to
        // 469 (~5 s), as tasks.md T014 directs ("lengthen warm-up if not"): the
        // 20 s attack leaves a lone velocity-100 note at ~5e-4 RMS in the 1 s
        // window after block 94, under the 1e-3 floor; from ~5 s it is ~2.6e-3.
        constexpr std::size_t kChangeBlock = 469;
        constexpr std::size_t kWindowStart = kChangeBlock * kBlock + kLatencySamples;
        constexpr std::size_t kWindowEnd = kWindowStart + 48000;
        constexpr std::size_t kBlocks = (kWindowEnd + kBlock - 1) / kBlock;
        static_assert(kBlocks * kBlock >= kWindowEnd);

        VoragoTest::MultiParamChanges atOffset300;
        atOffset300.addQueue(::Vorago::kMasterGainId).addTestPoint(300, 0.0);
        VoragoTest::MultiParamChanges atOffset0;
        atOffset0.addQueue(::Vorago::kMasterGainId).addTestPoint(0, 0.0);

        Krate::Test::EventList ev;
        ev.addNoteOn(48, kVelocity100, 0);

        VoragoTest::ProcessorFixture r0;
        VoragoTest::ProcessorFixture r1;
        VoragoTest::ProcessorFixture r2;
        prepareFixture(r0);
        prepareFixture(r1);
        prepareFixture(r2);
        renderBlocks(r0, kBlocks, &ev, nullptr);
        renderBlocks(r1, kBlocks, &ev, &atOffset300, kChangeBlock);
        renderBlocks(r2, kBlocks, &ev, &atOffset0, kChangeBlock);

        const float diffL = VoragoTest::maxAbsDiff(std::span<const float>(r1.capturedL),
                                                   std::span<const float>(r2.capturedL));
        const float diffR = VoragoTest::maxAbsDiff(std::span<const float>(r1.capturedR),
                                                   std::span<const float>(r2.capturedR));
        INFO("maxAbsDiff(R1, R2) L=" << diffL << " R=" << diffR);
        REQUIRE(diffL <= 1.0e-5f);
        REQUIRE(diffR <= 1.0e-5f);

        // Non-vacuity: the change is audible against the unchanged render.
        const double rmsL = VoragoTest::rmsDiff(std::span<const float>(r0.capturedL),
                                                std::span<const float>(r1.capturedL),
                                                kWindowStart, kWindowEnd);
        const double rmsR = VoragoTest::rmsDiff(std::span<const float>(r0.capturedR),
                                                std::span<const float>(r1.capturedR),
                                                kWindowStart, kWindowEnd);
        WARN("SC-008 (2) non-vacuity rmsDiff(R0, R1) L=" << rmsL << " R=" << rmsR);
        REQUIRE(std::max(rmsL, rmsR) > 1.0e-3);
    }
}
