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

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

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

// ==============================================================================
// Engine reset cost per block (Ruinae audit R-004)
// ==============================================================================
// A preset load (drained inside process()) and the end of a Delay On -> Off
// fade used to clear every delay type's full 5 s buffers in that one callback:
// 479 us for Digital + PingPong + Tape alone at 192 kHz, more than the whole
// 333 us of a 64-sample block. Each of those blocks, and each block of the
// deferred clear that replaces it, must fit the budget.
// ==============================================================================

namespace {

constexpr double kResetRate = 192000.0;
constexpr int kResetBlock = 64;
constexpr int kResetTrials = 15;
// 64 samples at 192 kHz.
constexpr double kResetBudgetUs = 1.0e6 * kResetBlock / kResetRate;

class ResetTimingRig {
public:
    ResetTimingRig() {
        REQUIRE(processor_.initialize(nullptr) == Steinberg::kResultOk);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = kResetRate;
        setup.maxSamplesPerBlock = kResetBlock;
        REQUIRE(processor_.setupProcessing(setup) == Steinberg::kResultOk);
        REQUIRE(processor_.setActive(true) == Steinberg::kResultOk);
    }

    ~ResetTimingRig() {
        processor_.setActive(false);
        processor_.terminate();
    }

    ResetTimingRig(const ResetTimingRig&) = delete;
    ResetTimingRig& operator=(const ResetTimingRig&) = delete;
    ResetTimingRig(ResetTimingRig&&) = delete;
    ResetTimingRig& operator=(ResetTimingRig&&) = delete;

    /// One 64-sample block; returns its wall-clock cost in microseconds.
    double block(Steinberg::Vst::IParameterChanges* params = nullptr,
                 Steinberg::Vst::IEventList* events = nullptr) {
        float* channels[2] = {outL_.data(), outR_.data()};
        Steinberg::Vst::AudioBusBuffers out{};
        out.numChannels = 2;
        out.channelBuffers32 = channels;
        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = kResetBlock;
        data.numOutputs = 1;
        data.outputs = &out;
        data.inputParameterChanges = params;
        data.inputEvents = events;
        const auto start = std::chrono::steady_clock::now();
        processor_.process(data);
        const auto stop = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(stop - start).count();
    }

    void blocks(int count) {
        for (int i = 0; i < count; ++i) block();
    }

    void set(Steinberg::Vst::ParamID id, double value) {
        Krate::Test::ParameterChanges changes;
        changes.addChange(id, value);
        block(&changes);
    }

    void noteOn() {
        Krate::Test::EventList events;
        events.addNoteOn(48, 0.9f);
        block(nullptr, &events);
    }

    std::vector<char> state() {
        Steinberg::MemoryStream stream;
        processor_.getState(&stream);
        Steinberg::int64 size = 0;
        stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        std::vector<char> bytes(static_cast<size_t>(size));
        Steinberg::int32 read = 0;
        stream.read(bytes.data(), static_cast<Steinberg::int32>(size), &read);
        return bytes;
    }

    /// setState on the message thread (not timed); the next process() drains it.
    void loadState(const std::vector<char>& bytes) {
        Steinberg::MemoryStream stream;
        stream.write(const_cast<char*>(bytes.data()), // NOLINT(cppcoreguidelines-pro-type-const-cast): IBStream::write takes void*
                     static_cast<Steinberg::int32>(bytes.size()), nullptr);
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        REQUIRE(processor_.setState(&stream) == Steinberg::kResultTrue);
    }

private:
    Ruinae::Processor processor_;
    std::vector<float> outL_ = std::vector<float>(kResetBlock, 0.0f);
    std::vector<float> outR_ = std::vector<float>(kResetBlock, 0.0f);
};

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Blocks a 20 ms slot fade spans at 192 kHz / 64, plus margin.
constexpr int kFadeBlocks = 70;

} // anonymous namespace

TEST_CASE("Preset load and delay disable stay inside the block budget at 192 kHz / 64",
          "[performance][wallclock]") {
    const double bar = 0.6 * kResetBudgetUs;
    const char* const kTypeNames[] = {"Digital", "Tape", "PingPong", "Granular", "Spectral"};

    for (int type = 0; type < 5; ++type) {
        ResetTimingRig rig;
        Krate::Test::ParameterChanges setup;
        setup.addChange(Ruinae::kDelayEnabledId, 1.0);
        setup.addChange(Ruinae::kReverbEnabledId, 1.0);
        setup.addChange(Ruinae::kDelayTypeId, static_cast<double>(type) / 4.0);
        setup.addChange(Ruinae::kDelaySyncId, 0.0);
        setup.addChange(Ruinae::kDelayTimeId, 249.0 / 4999.0);  // 250 ms
        rig.block(&setup);
        // Let the type switch (250 ms pre-warm + 30 ms crossfade) and the fades finish.
        rig.blocks(1000);
        const std::vector<char> preset = rig.state();

        // The process() call that drains a preset snapshot, and the worst of the
        // blocks after it (the deferred line clear and its catch-up run there).
        std::vector<double> presetUs;
        std::vector<double> afterLoadUs;
        for (int t = 0; t < kResetTrials; ++t) {
            rig.noteOn();
            rig.blocks(8);
            rig.loadState(preset);
            presetUs.push_back(rig.block());
            double worst = 0.0;
            for (int b = 0; b < kFadeBlocks; ++b) worst = std::max(worst, rig.block());
            afterLoadUs.push_back(worst);
        }

        // The worst block of a Delay Off -> On -> Off cycle: the fade-out
        // completion, and the clear and catch-up around the re-enable.
        std::vector<double> disableUs;
        for (int t = 0; t < kResetTrials; ++t) {
            Krate::Test::ParameterChanges on;
            on.addChange(Ruinae::kDelayEnabledId, 1.0);
            double worst = rig.block(&on);
            rig.noteOn();
            for (int b = 0; b < kFadeBlocks; ++b) worst = std::max(worst, rig.block());
            Krate::Test::ParameterChanges off;
            off.addChange(Ruinae::kDelayEnabledId, 0.0);
            worst = std::max(worst, rig.block(&off));
            for (int b = 0; b < kFadeBlocks; ++b) worst = std::max(worst, rig.block());
            disableUs.push_back(worst);
        }

        const double presetMedian = median(presetUs);
        const double afterLoadMedian = median(afterLoadUs);
        const double disableMedian = median(disableUs);
        std::printf("%-8s preset-load block %.1f us, worst block after it %.1f us, "
                    "delay-toggle worst block %.1f us (budget %.1f us, 60%% bar %.1f us)\n",
                    kTypeNames[type], presetMedian, afterLoadMedian, disableMedian,
                    kResetBudgetUs, bar);
        CAPTURE(kTypeNames[type], presetMedian, afterLoadMedian, disableMedian, bar);
        CHECK(presetMedian <= bar);
        CHECK(afterLoadMedian <= bar);
        CHECK(disableMedian <= bar);
    }
}
