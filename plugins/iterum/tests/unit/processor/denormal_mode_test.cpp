// ==============================================================================
// Processor::process runs with FTZ / DAZ set whatever the calling thread's floating-point mode
// ==============================================================================
// Krate::DSP::ScopedDenormalMode at the top of process(), as Disrumpo, Innexus, Membrum, Profundum, Seraphis and
// Vorago do: a delay's feedback tail decays through the denormal range, which the CPU computes slowly unless FTZ / DAZ
// are set, and the host decides whether they are. Deterministic check: with the test thread's FTZ / DAZ cleared, a
// subnormal input on the Digital mode (feedback 60 %, 10 ms, Mix 50 %) must come out as 0 (DAZ reads it as 0), and the
// caller's mode is restored after process(). x86 only (MXCSR).
//
// The processor is heap-allocated: it holds every mode's DSP, and a stack instance risks a stack overflow (Catch2
// reports it as "Unknown exception").

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)

#include "processor/processor.h"
#include "delay_mode.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <xmmintrin.h>

TEST_CASE("process() flushes denormals whatever the caller's floating-point mode", "[iterum][numerics][denormal]") {
    constexpr double kSampleRate = 48000.0;
    constexpr Steinberg::int32 kBlock = 64;
    constexpr int kBlocks = 64;  // 4096 samples
    constexpr unsigned int kFtzDaz = 0x8000u | 0x0040u;

    auto proc = Steinberg::owned(new Iterum::Processor());
    REQUIRE(proc->initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup{.processMode = Steinberg::Vst::kRealtime,
                                       .symbolicSampleSize = Steinberg::Vst::kSample32,
                                       .maxSamplesPerBlock = kBlock,
                                       .sampleRate = kSampleRate};
    REQUIRE(proc->setupProcessing(setup) == Steinberg::kResultOk);
    REQUIRE(proc->setActive(true) == Steinberg::kResultOk);

    Steinberg::Vst::HostProcessData data;
    REQUIRE(data.prepare(*proc, kBlock, Steinberg::Vst::kSample32));
    data.numSamples = kBlock;
    REQUIRE(data.numInputs == 1);
    REQUIRE(data.inputs[0].numChannels == 2);

    // Digital (the default mode), Free time 10 ms, feedback 60 %, Mix 50 %: the input reaches the output dry and
    // through the feedback loop.
    Steinberg::Vst::ParameterChanges changes{8};
    const auto queue = [&changes](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
        Steinberg::int32 index = 0;
        if (auto* q = changes.addParameterData(id, index)) {
            Steinberg::int32 point = 0;
            q->addPoint(0, value, point);
        }
    };
    queue(Iterum::kModeId, static_cast<double>(static_cast<int>(Iterum::DelayMode::Digital)) / 9.0);
    queue(Iterum::kDigitalTimeModeId, 0.0);
    queue(Iterum::kDigitalDelayTimeId, (10.0 - 1.0) / 9999.0);
    queue(Iterum::kDigitalFeedbackId, 0.5);
    queue(Iterum::kDigitalMixId, 0.5);
    data.inputParameterChanges = &changes;

    volatile std::uint32_t bits = 0x00000100u;  // a subnormal float (about 3.6e-43)
    const float denormal = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    size_t denormalOut = 0;
    size_t nonZeroOut = 0;
    unsigned int modeAfter = 0;

    const unsigned int saved = _mm_getcsr();
    _mm_setcsr(saved & ~kFtzDaz);
    for (int b = 0; b < kBlocks; ++b) {
        for (Steinberg::int32 ch = 0; ch < 2; ++ch) {
            for (Steinberg::int32 i = 0; i < kBlock; ++i) data.inputs[0].channelBuffers32[ch][i] = denormal;
        }
        proc->process(data);
        modeAfter |= _mm_getcsr() & kFtzDaz;
        changes.clearQueue();
        for (Steinberg::int32 ch = 0; ch < 2; ++ch) {
            for (Steinberg::int32 i = 0; i < kBlock; ++i) {
                const float v = data.outputs[0].channelBuffers32[ch][i];
                if (v != 0.0f) ++nonZeroOut;
                if (v != 0.0f && std::fabs(v) < std::numeric_limits<float>::min()) ++denormalOut;
            }
        }
    }
    _mm_setcsr(saved);

    data.unprepare();
    proc->setActive(static_cast<Steinberg::TBool>(false));
    proc->terminate();

    std::printf("FTZ/DAZ: denormal input -> %zu non-zero, %zu denormal output samples of %d; caller's FTZ/DAZ bits "
                "after process() 0x%x\n",
                nonZeroOut, denormalOut, 2 * kBlock * kBlocks, modeAfter);
    CHECK(modeAfter == 0u);  // the caller's mode is restored
    CHECK(denormalOut == 0u);
    CHECK(nonZeroOut == 0u);
}

#endif
