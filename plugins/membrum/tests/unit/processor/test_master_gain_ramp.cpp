// ==============================================================================
// Master gain ramps instead of stepping at the block boundary (MB-059, fix plan T2.10)
// ==============================================================================
// The master gain is applied after the bus limiter, so two renders that differ
// only in their master gain differ by exactly the applied gain: r[n] = A[n] / B[n]
// is the gain curve of render A relative to render B. Render A changes the
// master from norm 0 (-24 dB) to norm 1 (+12 dB) at sample 2048; render B keeps
// norm 0. Before the fix the whole x63.1 (+36 dB) arrived in one sample (or, after
// T2.3, across one host block, so the curve depended on the block size).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kMgRate = 44100.0;
constexpr std::size_t kMgChangeSample = 2048;
constexpr std::size_t kMgTotalSamples = kMgChangeSample + 4 * 512;
constexpr double kMgTargetRatio = 63.0957344480193;   // 10^(36/20)
constexpr std::size_t kMgRampWindow = 441;           // 10 ms at 44.1 kHz

// Renders the default kit's kick (note 36, velocity 0.2: below the bus
// limiter) with master norm 0 from block 0; when `change` is set the master
// goes to norm 1 in the block that starts at kMgChangeSample. Left channel.
std::vector<float> renderKick(int32 blockSize, bool change)
{
    auto proc = std::make_unique<Membrum::Processor>();
    REQUIRE(proc->initialize(nullptr) == kResultOk);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = kMgRate;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    REQUIRE(proc->setActive(true) == kResultOk);

    std::vector<float> out;
    out.reserve(kMgTotalSamples);
    std::vector<float> outL(static_cast<std::size_t>(blockSize));
    std::vector<float> outR(static_cast<std::size_t>(blockSize));
    Krate::Test::EventList events;
    Krate::Test::ParameterChanges params;

    for (std::size_t start = 0; start < kMgTotalSamples; start += static_cast<std::size_t>(blockSize))
    {
        events.clear();
        params.clear();
        if (start == 0)
        {
            events.addNoteOn(36, 0.2f, 0);
            params.addChange(Membrum::kMasterGainId, 0.0);
        }
        if (change && start == kMgChangeSample)
            params.addChange(Membrum::kMasterGainId, 1.0);

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        std::array<float*, 2> chans{outL.data(), outR.data()};
        AudioBusBuffers bus{};
        bus.numChannels = 2;
        bus.channelBuffers32 = chans.data();
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = blockSize;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputEvents = &events;
        data.inputParameterChanges = &params;
        REQUIRE(proc->process(data) == kResultOk);
        out.insert(out.end(), outL.begin(), outL.end());
    }

    proc->setActive(false);
    proc->terminate();
    return out;
}

// r[n] = A[n] / B[n] where |B[n]| > 1e-5; the max() sentinel (undefined) elsewhere.
std::vector<double> gainCurve(const std::vector<float>& a, const std::vector<float>& b)
{
    std::vector<double> r(a.size(), std::numeric_limits<double>::max());
    for (std::size_t n = 0; n < a.size(); ++n)
    {
        if (std::fabs(b[n]) > 1.0e-5f)
            r[n] = static_cast<double>(a[n]) / static_cast<double>(b[n]);
    }
    return r;
}

bool defined(double x) { return x != std::numeric_limits<double>::max(); }

} // namespace

TEST_CASE("Master gain ramps instead of stepping at the block boundary", "[processor][master_gain]")
{
    const auto a = renderKick(512, true);
    const auto b = renderKick(512, false);
    REQUIRE(a.size() == kMgTotalSamples);
    const auto r = gainCurve(a, b);

    // Enough defined points around the change to see the curve.
    std::size_t definedCount = 0;
    for (std::size_t n = kMgChangeSample - 64; n < kMgChangeSample + kMgRampWindow; ++n)
        definedCount += defined(r[n]) ? 1U : 0U;
    INFO("defined ratio points around the change: " << definedCount);
    REQUIRE(definedCount > 400);

    // Before the change both renders play the same gain.
    for (std::size_t n = 0; n < kMgChangeSample; ++n)
    {
        if (defined(r[n]))
        {
            INFO("n " << n << " r " << r[n]);
            REQUIRE(std::fabs(r[n] - 1.0) <= 1.0e-4);
        }
    }

    // Largest per-sample gain ratio across the change.
    double maxStepRatio = 0.0;
    std::size_t maxAt = 0;
    for (std::size_t n = kMgChangeSample - 64; n + 1 < r.size(); ++n)
    {
        if (defined(r[n]) && defined(r[n + 1]))
        {
            const double q = r[n + 1] / r[n];
            if (q > maxStepRatio)
            {
                maxStepRatio = q;
                maxAt = n;
            }
        }
    }
    INFO("largest per-sample gain ratio " << maxStepRatio << " at sample " << maxAt);
    CHECK(maxStepRatio <= 1.01);

    // The target is reached within 0.1 % no later than 10 ms after the change
    // and held for the rest of the render.
    std::size_t outside = 0;
    double worst = 0.0;
    for (std::size_t n = kMgChangeSample + kMgRampWindow; n < r.size(); ++n)
    {
        if (defined(r[n]))
        {
            const double dev = std::fabs(r[n] / kMgTargetRatio - 1.0);
            worst = std::max(worst, dev);
            outside += dev > 1.0e-3 ? 1U : 0U;
        }
    }
    INFO("worst relative deviation from x63.1 after 10 ms: " << worst);
    CHECK(outside == 0);

    SECTION("block-size independent")
    {
        const auto a128 = renderKick(128, true);
        const auto b128 = renderKick(128, false);
        REQUIRE(a128.size() == kMgTotalSamples);
        const auto r128 = gainCurve(a128, b128);
        double worstDiff = 0.0;
        std::size_t compared = 0;
        for (std::size_t n = 0; n < r.size(); ++n)
        {
            if (defined(r[n]) && defined(r128[n]))
            {
                worstDiff = std::max(worstDiff, std::fabs(r128[n] / r[n] - 1.0));
                ++compared;
            }
        }
        INFO("compared " << compared << " points, worst relative difference " << worstDiff);
        REQUIRE(compared > kMgTotalSamples / 2);
        CHECK(worstDiff <= 1.0e-4);
    }
}
