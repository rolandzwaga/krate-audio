// ==============================================================================
// Editor audition events reach the audio thread in order (MB-006, MB-055,
// fix plan T2.4)
// ==============================================================================
// The editor's pad grid sends "AuditionPad" on mouse-down and "AuditionPadOff"
// on mouse-up (controller.cpp, setAuditionCallback / setAuditionOffCallback).
// Processor::notify used to store both into one atomic word that process()
// took once per block, so a click shorter than one host buffer lost its
// note-on, an Off(A) + On(B) in one block lost A's note-off, and a word posted
// while the host was not processing fired on resume.
//
// Messages are sent the way the controller sends them (HostMessage with the
// "midi" and, for a pad-down, "velocity" attributes) between process() calls.
// 48 kHz, 256-sample blocks. Pads are separated by pan: pad A hard left, pad B
// hard right, so each channel carries one pad.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "dsp/pad_config.h"

#include <krate/dsp/core/db_utils.h>

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kAqRate  = 48000.0;
constexpr int32  kAqBlock = 256;
constexpr int    kAqPadA  = 36;  // pad 0, the default kit's kick
constexpr int    kAqPadB  = 38;  // pad 2
constexpr float  kAqMinus60Db = 1.0e-3f;

// "AuditionPad", exactly as the controller's audition callback sends it.
tresult postAuditionOn(Membrum::Processor& processor, int midi, int velocity = 100)
{
    auto msg = Steinberg::owned(new HostMessage());
    msg->setMessageID("AuditionPad");
    auto* attrs = msg->getAttributes();
    attrs->setInt("midi",     static_cast<Steinberg::int64>(midi));
    attrs->setInt("velocity", static_cast<Steinberg::int64>(velocity));
    return processor.notify(msg);
}

// "AuditionPadOff", exactly as the controller's audition-off callback sends it
// (attribute "midi" only).
tresult postAuditionOff(Membrum::Processor& processor, int midi)
{
    auto msg = Steinberg::owned(new HostMessage());
    msg->setMessageID("AuditionPadOff");
    auto* attrs = msg->getAttributes();
    attrs->setInt("midi", static_cast<Steinberg::int64>(midi));
    return processor.notify(msg);
}

// `frictionPadA`: pad A bows (Friction exciter), the one exciter a note-off
// still releases under FR-114 (fix plan T2.9), so its Off is audible.
std::unique_ptr<Membrum::Processor> makeAuditionProcessor(bool frictionPadA = false)
{
    auto processor = std::make_unique<Membrum::Processor>();
    REQUIRE(processor->initialize(nullptr) == kResultOk);
    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kAqBlock;
    setup.sampleRate         = kAqRate;
    REQUIRE(processor->setupProcessing(setup) == kResultOk);
    REQUIRE(processor->setActive(true) == kResultOk);
    processor->setProcessing(true);
    // Pad A hard left, pad B hard right.
    auto& pool = processor->voicePoolForTest();
    pool.setPadConfigField(kAqPadA - 36, Membrum::kPadPan, 0.0f);
    pool.setPadConfigField(kAqPadB - 36, Membrum::kPadPan, 1.0f);
    if (frictionPadA)
        pool.setPadConfigSelector(kAqPadA - 36, Membrum::kPadExciterType,
                                  static_cast<int>(Membrum::ExciterType::Friction));
    processor->publishPadConfigsForTest();
    return processor;
}

void shutdownAuditionProcessor(Membrum::Processor& processor)
{
    processor.setProcessing(false);
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

// One process() call; appends the block to L and R.
void renderAuditionBlock(Membrum::Processor& processor,
                         std::vector<float>& allL, std::vector<float>& allR)
{
    std::vector<float> outL(static_cast<std::size_t>(kAqBlock), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kAqBlock), 0.0f);
    float* chans[2] = {outL.data(), outR.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = chans;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kAqBlock;
    data.numOutputs = 1;
    data.outputs = &bus;
    REQUIRE(processor.process(data) == kResultOk);
    allL.insert(allL.end(), outL.begin(), outL.end());
    allR.insert(allR.end(), outR.begin(), outR.end());
}

float peakOf(const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    float peak = 0.0f;
    for (std::size_t i = begin; i < std::min(end, x.size()); ++i)
        peak = std::max(peak, std::abs(x[i]));
    return peak;
}

// decay60: seconds from the render start to the last sample that is still
// within 60 dB of the signal's peak.
double decay60Seconds(const std::vector<float>& x)
{
    const float peak = peakOf(x, 0, x.size());
    if (peak <= 0.0f)
        return 0.0;
    const float floor = peak * kAqMinus60Db;
    std::size_t last = 0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        if (std::abs(x[i]) >= floor)
            last = i;
    }
    return static_cast<double>(last + 1) / kAqRate;
}

// RMS of (a - b) in dB re the RMS of b (-300 dB for identical signals).
double differenceDb(const std::vector<float>& a, const std::vector<float>& b)
{
    double sumD = 0.0;
    double sumB = 0.0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
    {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumD += d * d;
        sumB += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }
    if (sumD <= 0.0)
        return -300.0;
    return 10.0 * std::log10(sumD / std::max(sumB, 1.0e-30));
}

// One audition event posted before block `block`.
struct AuditionEvent
{
    int block;
    int midi;
    bool on;
};

struct AuditionRender
{
    std::vector<float> left;
    std::vector<float> right;
};

// Renders `blocks` blocks on a fresh processor, posting each event before its
// block (in list order).
AuditionRender renderAuditionSchedule(const std::vector<AuditionEvent>& events, int blocks,
                                      bool frictionPadA = false)
{
    auto processor = makeAuditionProcessor(frictionPadA);
    AuditionRender out;
    out.left.reserve(static_cast<std::size_t>(blocks * kAqBlock));
    out.right.reserve(static_cast<std::size_t>(blocks * kAqBlock));
    for (int b = 0; b < blocks; ++b)
    {
        for (const auto& e : events)
        {
            if (e.block != b)
                continue;
            if (e.on)
                REQUIRE(postAuditionOn(*processor, e.midi) == kResultOk);
            else
                REQUIRE(postAuditionOff(*processor, e.midi) == kResultOk);
        }
        renderAuditionBlock(*processor, out.left, out.right);
    }
    shutdownAuditionProcessor(*processor);
    return out;
}

constexpr int kAqRenderBlocks = 563;  // 3.0 s at 48 kHz / 256

bool withinFivePercent(double value, double reference)
{
    return reference > 0.0 && std::abs(value - reference) <= 0.05 * reference;
}

} // namespace

TEST_CASE("Audition: a pad-down and pad-up between two blocks still sound the pad and release it",
          "[audition][threading]")
{
    // Down and up both arrive before block 0.
    const auto quick = renderAuditionSchedule(
        {{0, kAqPadA, true}, {0, kAqPadA, false}}, kAqRenderBlocks);
    // Reference: the down before block 0, the up before block 1.
    const auto reference = renderAuditionSchedule(
        {{0, kAqPadA, true}, {1, kAqPadA, false}}, kAqRenderBlocks);

    const float firstBlockPeak =
        peakOf(quick.left, 0, static_cast<std::size_t>(kAqBlock));
    const double decayQuick = decay60Seconds(quick.left);
    const double decayRef   = decay60Seconds(reference.left);
    std::printf("[audition-queue] quick click: block-0 peak %.6f, decay60 %.4f s; "
                "reference decay60 %.4f s\n",
                static_cast<double>(firstBlockPeak), decayQuick, decayRef);

    CHECK(firstBlockPeak > kAqMinus60Db);
    CHECK(withinFivePercent(decayQuick, decayRef));
}

TEST_CASE("Audition: an Off for pad A and an On for pad B in one block are both applied",
          "[audition][threading]")
{
    // Pad A bows (Friction): under FR-114 (fix plan T2.9) a note-off releases
    // only a sustaining exciter, so the bow is what makes A's Off audible. The
    // Off lands at 26.7 ms, inside the ~41 ms bow.
    constexpr int kSwitchBlock = 5;
    const auto both = renderAuditionSchedule(
        {{0, kAqPadA, true}, {kSwitchBlock, kAqPadA, false}, {kSwitchBlock, kAqPadB, true}},
        kAqRenderBlocks, /*frictionPadA*/ true);
    // Reference: Off(A) alone in block 5.
    const auto offAlone = renderAuditionSchedule(
        {{0, kAqPadA, true}, {kSwitchBlock, kAqPadA, false}}, kAqRenderBlocks,
        /*frictionPadA*/ true);
    // Reference: A never released.
    const auto noOff = renderAuditionSchedule({{0, kAqPadA, true}}, kAqRenderBlocks,
                                              /*frictionPadA*/ true);

    const auto switchSample = static_cast<std::size_t>(kSwitchBlock * kAqBlock);
    const float bPeakRight = peakOf(both.right, switchSample, both.right.size());
    const double bothVsAlone  = differenceDb(both.left, offAlone.left);
    const double noOffVsAlone = differenceDb(noOff.left, offAlone.left);
    std::printf("[audition-queue] Off(A)+On(B): B right peak %.6f; pad A (left) re Off(A) "
                "alone: with On(B) %.2f dB, with no Off %.2f dB\n",
                static_cast<double>(bPeakRight), bothVsAlone, noOffVsAlone);

    // The bow is a small part of the hit: releasing it early moves pad A's
    // render by about -61 dB. With On(B) in the same block A must match the
    // Off-alone render (measured -140 dB); a lost Off would leave it at the
    // no-Off difference instead.
    CHECK(bPeakRight > kAqMinus60Db);
    CHECK(bothVsAlone <= -120.0);   // A's Off applied as when it arrives alone
    CHECK(noOffVsAlone >= -100.0);  // and the Off changes A's render
}

TEST_CASE("Audition: two pad-downs in one block start two voices", "[audition][threading]")
{
    auto processor = makeAuditionProcessor();
    REQUIRE(postAuditionOn(*processor, kAqPadA) == kResultOk);
    REQUIRE(postAuditionOn(*processor, kAqPadB) == kResultOk);
    std::vector<float> l;
    std::vector<float> r;
    renderAuditionBlock(*processor, l, r);
    const int active = processor->voicePoolForTest().getActiveVoiceCount();
    std::printf("[audition-queue] two pad-downs: %d active voices\n", active);
    CHECK(active == 2);
    shutdownAuditionProcessor(*processor);
}

TEST_CASE("Audition: a request posted while the host is not processing does not fire on resume",
          "[audition][threading]")
{
    auto processor = makeAuditionProcessor();
    std::vector<float> l;
    std::vector<float> r;
    renderAuditionBlock(*processor, l, r);

    SECTION("setProcessing(false) / setProcessing(true)")
    {
        processor->setProcessing(false);
        postAuditionOn(*processor, kAqPadA);
        processor->setProcessing(true);
    }
    SECTION("setActive(false) / setActive(true)")
    {
        processor->setProcessing(false);
        REQUIRE(processor->setActive(false) == kResultOk);
        postAuditionOn(*processor, kAqPadA);
        REQUIRE(processor->setActive(true) == kResultOk);
        processor->setProcessing(true);
    }

    const auto start = l.size();
    renderAuditionBlock(*processor, l, r);
    const float peak = std::max(peakOf(l, start, l.size()), peakOf(r, start, r.size()));
    const int active = processor->voicePoolForTest().getActiveVoiceCount();
    std::printf("[audition-queue] resume: peak %.9f, %d active voices\n",
                static_cast<double>(peak), active);
    CHECK(peak == 0.0f);
    CHECK(active == 0);

    // The queue still works after the discard.
    REQUIRE(postAuditionOn(*processor, kAqPadA) == kResultOk);
    renderAuditionBlock(*processor, l, r);
    CHECK(processor->voicePoolForTest().getActiveVoiceCount() == 1);
    shutdownAuditionProcessor(*processor);
}

TEST_CASE("Audition: message thread and audio thread run concurrently", "[audition][threading]")
{
    constexpr int kPairs = 10000;
    constexpr int kTailBlocks = 1875;  // 10 s at 48 kHz / 256

    auto processor = makeAuditionProcessor();
    std::atomic<bool> producerDone{false};
    std::atomic<int> retries{0};

    // The message thread: 10 000 On / Off pairs over random pads 36..67. A
    // full queue refuses the event (kResultFalse); the producer then yields
    // and posts it again, so every On is followed by its Off.
    std::thread producer([&] {
        std::mt19937 rng(20261125u);
        std::uniform_int_distribution<int> padDist(36, 67);
        for (int i = 0; i < kPairs; ++i)
        {
            const int midi = padDist(rng);
            while (postAuditionOn(*processor, midi) != kResultOk)
            {
                retries.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
            while (postAuditionOff(*processor, midi) != kResultOk)
            {
                retries.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    // The audio thread (this one): process() until the producer ends, then
    // 10 s of blocks.
    std::vector<float> l;
    std::vector<float> r;
    l.reserve(static_cast<std::size_t>(kAqBlock));
    r.reserve(static_cast<std::size_t>(kAqBlock));
    bool allFinite = true;
    long long blocksDuring = 0;
    const auto renderChecked = [&] {
        l.clear();
        r.clear();
        renderAuditionBlock(*processor, l, r);
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            if (!Krate::DSP::detail::isFinite(l[i]) || !Krate::DSP::detail::isFinite(r[i]))
                allFinite = false;
        }
    };
    while (!producerDone.load(std::memory_order_acquire))
    {
        renderChecked();
        ++blocksDuring;
    }
    producer.join();
    for (int b = 0; b < kTailBlocks; ++b)
        renderChecked();

    const int active = processor->voicePoolForTest().getActiveVoiceCount();
    std::printf("[audition-queue] stress: %lld blocks while posting, %d retries, "
                "%d active voices after the 10 s tail, finite %d\n",
                blocksDuring, retries.load(), active, allFinite ? 1 : 0);
    CHECK(allFinite);
    CHECK(active == 0);
    shutdownAuditionProcessor(*processor);
}
