// ==============================================================================
// Parameter-apply cost (Ruinae audit R-001)
// ==============================================================================
// Processor::applyParamsToEngine() runs on every process() call and pushes every
// envelope setting down to all 16 voices. The ADSR curve setters used to rebuild
// their 256-entry tables on every call (3 envelopes x 3 segments x 16 voices =
// 144 rebuilds per block, measured 635-650 us with Bezier curves), whatever the
// block size. With no parameter change the apply pass must be a small, fixed
// fraction of the audio deadline.
//
// Timing case: runs only in the CPU lane (node tools/run-cpu-tests.js ruinae_tests).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "vst_param_changes.h"

#include <chrono>
#include <cstdio>

namespace {

class ApplyTimingProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::applyParamsToEngine;
    using Ruinae::Processor::processParameterChanges;
};

} // anonymous namespace

TEST_CASE("applyParamsToEngine costs under 5% of a 64-sample block with Bezier envelopes",
          "[performance][wallclock]") {
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlockSize = 64;
    constexpr int kIterations = 10000;
    // 5 % of a 64-sample block at 48 kHz (1333 us) = 66.7 us.
    constexpr double kBarUs = 66.0;

    ApplyTimingProcessor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultOk);
    REQUIRE(processor.setActive(true) == Steinberg::kResultOk);

    // Bezier on for all three envelopes, with distinct non-trivial control points
    // per segment so every setter would have real table work to do.
    Krate::Test::ParameterChanges changes;
    const Steinberg::Vst::ParamID enabledIds[] = {
        Ruinae::kAmpEnvBezierEnabledId, Ruinae::kFilterEnvBezierEnabledId,
        Ruinae::kModEnvBezierEnabledId};
    const Steinberg::Vst::ParamID firstCpIds[] = {
        Ruinae::kAmpEnvBezierAttackCp1XId, Ruinae::kFilterEnvBezierAttackCp1XId,
        Ruinae::kModEnvBezierAttackCp1XId};
    for (int env = 0; env < 3; ++env) {
        changes.addChange(enabledIds[env], 1.0);
        for (int k = 0; k < 12; ++k) {
            changes.addChange(firstCpIds[env] + static_cast<Steinberg::Vst::ParamID>(k),
                              0.1 + 0.07 * static_cast<double>(k));
        }
    }
    processor.processParameterChanges(&changes);
    processor.applyParamsToEngine();  // first apply builds the tables

    // Warm up, then time the steady state (no parameter change).
    for (int i = 0; i < 200; ++i)
        processor.applyParamsToEngine();

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i)
        processor.applyParamsToEngine();
    const auto stop = std::chrono::steady_clock::now();

    const double meanUs =
        std::chrono::duration<double, std::micro>(stop - start).count() / kIterations;
    std::printf("applyParamsToEngine mean (Bezier, no change): %.3f us (bar %.1f us, "
                "60%% = %.1f us)\n", meanUs, kBarUs, 0.6 * kBarUs);
    INFO("mean applyParamsToEngine cost: " << meanUs << " us");

    REQUIRE(meanUs < kBarUs);
    // Wall-clock rule: the dev box must read at most 60 % of the bar.
    REQUIRE(meanUs <= 0.6 * kBarUs);

    REQUIRE(processor.setActive(false) == Steinberg::kResultOk);
    REQUIRE(processor.terminate() == Steinberg::kResultOk);
}
