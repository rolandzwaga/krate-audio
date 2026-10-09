// ==============================================================================
// Vorago - held-note render tests (SC-005, SC-006, SC-024)
// ==============================================================================
// Vorago_ProcessorRendersHeldNote. T008 owns SECTION("CavernTargetsArePushed")
// (SC-024: applyCavernTargets pushes all seven VoragoCavernTargets fields, in
// FR-034a order, and the push audibly changes the cavern). T014 adds the other
// two sections to this case: HeldNoteIsAudible (SC-005) and
// OutputNeverExceedsCeiling (SC-006, with its discrimination arm).
// ==============================================================================

#include "engine/vorago_engine_config.h"
#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

namespace {

constexpr double kCavernTestSampleRate = 48000.0;
constexpr std::size_t kCavernTestBlock = 512;
constexpr std::size_t kCavernToneSamples = 48000;     // 1 s of 110 Hz
constexpr std::size_t kCavernTotalSamples = 144000;   // + 2 s of zeros
constexpr float kCavernToneHz = 110.0f;
constexpr float kCavernToneAmp = 0.25f;

struct CavernCapture {
    std::vector<float> left;
    std::vector<float> right;
};

// Held-note renders through the real Processor::process() (SC-005, SC-006).
constexpr double kRenderSampleRate = 48000.0;
constexpr std::size_t kRenderBlock = 512;
constexpr float kVelocity100 = 100.0f / 127.0f;
constexpr std::size_t kHeldNoteBlocks = 750;      // 8 s at 512 (SC-005)
constexpr std::size_t kCeilingBlocks = 2813;      // 30 s at 512 (SC-006)
constexpr std::size_t kLastSecondSamples = 48000;
constexpr float kOutputCeiling = 0.9661f;         // 10^(-0.3/20) = 0.96605, vorago_engine.h:203
constexpr float kDiscriminationFloor = 0.49f;     // 0.9661 / 2 - NEVER lowered
// Ruling B-1 (2026-09-24): the discrimination arm drives renderGainAndOutputStage
// directly with a 0.6-amplitude tone. The six-voice render peaks at ~0.24 at unity
// gain (best 5 s window 0.28 over 100 s; velocity 127 changes nothing), so the
// gain-2.0 render never reaches the limiter and cannot tell "gain before the
// limiter" from "gain after it". 0.6 x 2.0 = 1.2 > 0.9661 can.
constexpr float kProbeAmp = 0.6f;
constexpr float kProbeHz = 110.0f;
constexpr std::size_t kProbeBlocks = 40;          // ~0.43 s: limiter lookahead settles

// Renders numBlocks blocks of kRenderBlock. `events` and `params` are attached to
// block 0 only; every later block carries neither.
void renderBlocks(VoragoTest::ProcessorFixture& fx, std::size_t numBlocks,
                  Steinberg::Vst::IEventList* events, Steinberg::Vst::IParameterChanges* params) {
    fx.reserveCapture(fx.capturedL.size() + numBlocks * kRenderBlock);
    for (std::size_t b = 0; b < numBlocks; ++b) {
        const bool first = (b == 0);
        REQUIRE(fx.processBlock(kRenderBlock, first ? events : nullptr,
                                first ? params : nullptr) == Steinberg::kResultOk);
    }
}

struct CeilingRun {
    float peakL = 0.0f;
    float peakR = 0.0f;
    bool finite = false;
    double wallSeconds = 0.0;
};

// SC-006 script: polyphony normalized 1.0 (-> 6) and master gain `gainNorm` in
// block 0's changes; six notes at velocity 100 at offset 0; 30 s.
CeilingRun renderCeilingScript(double gainNorm) {
    VoragoTest::ProcessorFixture fx;
    fx.prepare(kRenderSampleRate, static_cast<Steinberg::int32>(kRenderBlock));

    Krate::Test::ParameterChanges pc;
    pc.addChange(::Vorago::kPolyphonyId, 1.0);
    pc.addChange(::Vorago::kMasterGainId, gainNorm);

    Krate::Test::EventList ev;
    for (const Steinberg::int16 pitch :
         std::initializer_list<Steinberg::int16>{36, 40, 43, 47, 50, 53}) {
        ev.addNoteOn(pitch, kVelocity100, 0);
    }

    const auto t0 = std::chrono::steady_clock::now();
    renderBlocks(fx, kCeilingBlocks, &ev, &pc);
    const auto t1 = std::chrono::steady_clock::now();

    CeilingRun run;
    run.peakL = VoragoTest::peakOf(std::span<const float>(fx.capturedL));
    run.peakR = VoragoTest::peakOf(std::span<const float>(fx.capturedR));
    run.finite = VoragoTest::allFinite(std::span<const float>(fx.capturedL)) &&
                 VoragoTest::allFinite(std::span<const float>(fx.capturedR));
    run.wallSeconds = std::chrono::duration<double>(t1 - t0).count();
    return run;
}

// B-1 probe: prepare, snap the master gain to `gainNorm` through one silent
// process() block (the first block after prepare snaps the smoother, FR-024a.2),
// then push a 0.6-amplitude tone through steps 5-6 alone for kProbeBlocks blocks
// and return the peak of the LAST block (the limiter's lookahead has settled).
CeilingRun renderProbeThroughGainAndOutputStage(double gainNorm) {
    VoragoTest::ProcessorFixture fx;
    fx.prepare(kRenderSampleRate, static_cast<Steinberg::int32>(kRenderBlock));

    Krate::Test::ParameterChanges pc;
    pc.addChange(::Vorago::kMasterGainId, gainNorm);
    renderBlocks(fx, 1, nullptr, &pc);

    std::vector<float> l(kRenderBlock, 0.0f);
    std::vector<float> r(kRenderBlock, 0.0f);
    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * kProbeHz / kRenderSampleRate;
    CeilingRun run;
    run.finite = true;
    for (std::size_t b = 0; b < kProbeBlocks; ++b) {
        for (std::size_t s = 0; s < kRenderBlock; ++s) {
            const float v = kProbeAmp * static_cast<float>(std::sin(phase));
            l[s] = v;
            r[s] = v;
            phase += step;
        }
        fx.proc->renderGainAndOutputStage(l.data(), r.data(), kRenderBlock);
        run.finite = run.finite && VoragoTest::allFinite(std::span<const float>(l)) &&
                     VoragoTest::allFinite(std::span<const float>(r));
    }
    run.peakL = VoragoTest::peakOf(std::span<const float>(l));
    run.peakR = VoragoTest::peakOf(std::span<const float>(r));
    return run;
}

}  // namespace

TEST_CASE("Vorago_ProcessorRendersHeldNote", "[vorago][integration]") {
    SECTION("CavernTargetsArePushed") {
        // --- Config helpers carry the shipped defaults (FR-053) -------------
        const auto engineCfg = ::Vorago::makeVoragoEngineConfig(2048);
        REQUIRE(engineCfg.maxBlockSamples == 2048u);
        REQUIRE(engineCfg.smearEnabled == true);
        REQUIRE(engineCfg.smearFftSize == 2048u);
        REQUIRE(engineCfg.atmosGhostReverseProbability == 0.0f);
        REQUIRE(engineCfg.atmosGhostEventTriggers == false);

        const auto cavernCfg = ::Vorago::makeVoragoCavernConfig(2048);
        REQUIRE(cavernCfg.maxBlockSamples == 2048u);
        REQUIRE(cavernCfg.seed == 1u);
        REQUIRE(cavernCfg.numChannels == 8u);
        REQUIRE(cavernCfg.spectralDiffusionEnabled == true);
        REQUIRE(cavernCfg.diffusionFftSize == 1024u);

        // --- Three caverns: A pushed by helper, B by hand, C untouched ------
        auto cavernA = std::make_unique<Krate::DSP::CavernVerb>();
        auto cavernB = std::make_unique<Krate::DSP::CavernVerb>();
        auto cavernC = std::make_unique<Krate::DSP::CavernVerb>();
        cavernA->prepare(kCavernTestSampleRate, ::Vorago::makeVoragoCavernConfig(kCavernTestBlock));
        cavernB->prepare(kCavernTestSampleRate, ::Vorago::makeVoragoCavernConfig(kCavernTestBlock));
        cavernC->prepare(kCavernTestSampleRate, ::Vorago::makeVoragoCavernConfig(kCavernTestBlock));

        // Input: 1 s of 110 Hz sine at 0.25 on both channels, then 2 s of zeros.
        std::vector<float> input(kCavernTotalSamples, 0.0f);
        const double phaseInc =
            2.0 * std::numbers::pi * static_cast<double>(kCavernToneHz) / kCavernTestSampleRate;
        for (std::size_t i = 0; i < kCavernToneSamples; ++i) {
            input[i] = kCavernToneAmp *
                       static_cast<float>(std::sin(phaseInc * static_cast<double>(i)));
        }

        const Krate::DSP::VoragoCavernTargets targets{.size = 0.9f,
                                                      .darkness = 0.2f,
                                                      .decaySeconds = 5.0f,
                                                      .fog = 0.8f,
                                                      .damperDepth = 0.9f,
                                                      .mix = 0.5f,
                                                      .width = 0.3f};

        CavernCapture capA;
        CavernCapture capB;
        CavernCapture capC;
        for (CavernCapture* c : {&capA, &capB, &capC}) {
            c->left.assign(kCavernTotalSamples, 0.0f);
            c->right.assign(kCavernTotalSamples, 0.0f);
        }

        for (std::size_t start = 0; start < kCavernTotalSamples; start += kCavernTestBlock) {
            const std::size_t n = std::min(kCavernTestBlock, kCavernTotalSamples - start);
            const float* in = input.data() + start;

            ::Vorago::applyCavernTargets(*cavernA, targets);

            // FR-034a order, by hand.
            cavernB->setSize(targets.size);
            cavernB->setDarkness(targets.darkness);
            cavernB->setDecaySeconds(targets.decaySeconds);
            cavernB->setFog(targets.fog);
            cavernB->setDamperDepth(targets.damperDepth);
            cavernB->setMix(targets.mix);
            cavernB->setWidth(targets.width);

            cavernA->processStereoBlock(in, in, capA.left.data() + start,
                                        capA.right.data() + start, n);
            cavernB->processStereoBlock(in, in, capB.left.data() + start,
                                        capB.right.data() + start, n);
            cavernC->processStereoBlock(in, in, capC.left.data() + start,
                                        capC.right.data() + start, n);
        }

        REQUIRE(VoragoTest::allFinite(std::span<const float>(capA.left)));
        REQUIRE(VoragoTest::allFinite(std::span<const float>(capA.right)));

        // Clause 1: the helper is exactly the seven hand-set setters.
        const float diffL = VoragoTest::maxAbsDiff(std::span<const float>(capA.left),
                                                   std::span<const float>(capB.left), 0,
                                                   kCavernTotalSamples);
        const float diffR = VoragoTest::maxAbsDiff(std::span<const float>(capA.right),
                                                   std::span<const float>(capB.right), 0,
                                                   kCavernTotalSamples);
        INFO("maxAbsDiff(A, B) L=" << diffL << " R=" << diffR);
        REQUIRE(diffL <= 1.0e-6f);
        REQUIRE(diffR <= 1.0e-6f);

        // Clause 2: the push has an effect against the untouched control.
        const double rmsL = VoragoTest::rmsDiff(std::span<const float>(capA.left),
                                                std::span<const float>(capC.left), 0,
                                                kCavernTotalSamples);
        const double rmsR = VoragoTest::rmsDiff(std::span<const float>(capA.right),
                                                std::span<const float>(capC.right), 0,
                                                kCavernTotalSamples);
        INFO("rmsDiff(A, C) L=" << rmsL << " R=" << rmsR);
        REQUIRE(rmsL > 1.0e-3);
        REQUIRE(rmsR > 1.0e-3);
    }

    SECTION("HeldNoteIsAudible") {
        // SC-005: registered defaults (no parameter changes), NoteOn(48, 100) at
        // offset 0 of block 0, 8 s. The last second must be audible.
        VoragoTest::ProcessorFixture fx;
        fx.prepare(kRenderSampleRate, static_cast<Steinberg::int32>(kRenderBlock));

        Krate::Test::EventList ev;
        ev.addNoteOn(48, kVelocity100, 0);

        const auto t0 = std::chrono::steady_clock::now();
        renderBlocks(fx, kHeldNoteBlocks, &ev, nullptr);
        const auto t1 = std::chrono::steady_clock::now();

        const std::size_t total = kHeldNoteBlocks * kRenderBlock;
        REQUIRE(fx.capturedL.size() == total);
        REQUIRE(fx.capturedR.size() == total);
        REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedL)));
        REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedR)));

        const auto lastL =
            std::span<const float>(fx.capturedL).subspan(total - kLastSecondSamples);
        const auto lastR =
            std::span<const float>(fx.capturedR).subspan(total - kLastSecondSamples);
        const float peakL = VoragoTest::peakOf(lastL);
        const float peakR = VoragoTest::peakOf(lastR);
        WARN("SC-005 last-second peak L=" << peakL << " R=" << peakR
                                          << " rms L=" << VoragoTest::rmsOf(lastL)
                                          << " R=" << VoragoTest::rmsOf(lastR) << " wall="
                                          << std::chrono::duration<double>(t1 - t0).count()
                                          << " s");
        REQUIRE(std::max(peakL, peakR) >= 1.0e-4f);
    }

    SECTION("OutputNeverExceedsCeiling") {
        // SC-006: six voices at master gain x2 never exceed the limiter ceiling.
        const CeilingRun hot = renderCeilingScript(1.0);
        WARN("SC-006 gain 2.0 peak L=" << hot.peakL << " R=" << hot.peakR
                                       << " wall=" << hot.wallSeconds << " s");
        REQUIRE(hot.finite);
        REQUIRE(hot.peakL <= kOutputCeiling);
        REQUIRE(hot.peakR <= kOutputCeiling);

        // Recorded, not gated (B-1): the six-voice unity render, for compliance.md.
        const CeilingRun unity = renderCeilingScript(0.5);
        WARN("SC-006 gain 1.0 six-voice peak (recorded) L=" << unity.peakL
                                                            << " R=" << unity.peakR
                                                            << " wall=" << unity.wallSeconds
                                                            << " s");
        REQUIRE(unity.finite);

        // Discrimination arm (B-1): a 0.6-amplitude tone through steps 5-6 alone.
        // At unity it must reach half the ceiling; at gain 2.0 the pre-limiter
        // level is 1.2 > 0.9661, so a bounded result proves the limiter sits
        // AFTER the gain. With the gain after the limiter, the gain-2 peak would
        // be 2 x the unity peak >= 0.98 > 0.9661.
        const CeilingRun probeUnity = renderProbeThroughGainAndOutputStage(0.5);
        const CeilingRun probeHot = renderProbeThroughGainAndOutputStage(1.0);
        WARN("SC-006 probe 0.6 tone: unity peak L=" << probeUnity.peakL << " R="
                                                    << probeUnity.peakR << " | gain 2.0 peak L="
                                                    << probeHot.peakL << " R=" << probeHot.peakR);
        REQUIRE(probeUnity.finite);
        REQUIRE(probeHot.finite);
        REQUIRE(std::max(probeUnity.peakL, probeUnity.peakR) >= kDiscriminationFloor);
        REQUIRE(probeHot.peakL <= kOutputCeiling);
        REQUIRE(probeHot.peakR <= kOutputCeiling);
        REQUIRE(std::max(probeHot.peakL, probeHot.peakR) >
                std::max(probeUnity.peakL, probeUnity.peakR));
    }
}
