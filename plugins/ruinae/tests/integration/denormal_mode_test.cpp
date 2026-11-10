// ==============================================================================
// Processor::process runs with FTZ / DAZ set whatever the calling thread's floating-point mode
// ==============================================================================
// Krate::DSP::ScopedDenormalMode at the top of process(), as Disrumpo, Innexus, Membrum, Profundum, Seraphis and
// Vorago do: a note's release and the effects' tails decay through the denormal range, which the CPU computes slowly
// unless FTZ / DAZ are set, and the host decides whether they are. Deterministic check: with the test thread's
// FTZ / DAZ cleared, one note of the default patch played through a subnormal Master Gain (normalized 1e-40, so
// value * 2 converts to a subnormal float of about 2e-40) must come out as 0 (FTZ flushes the conversion, DAZ reads a
// subnormal operand as 0), and the caller's mode is restored after every process(). x86 only (MXCSR).
//
// The default patch's own tail is no probe: it reaches exactly 0 without passing a denormal output sample (measured
// with the guard absent: 67320 non-zero, 0 denormal samples over a note and a 20 s tail).

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "vst_event_list.h"
#include "vst_param_changes.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include <xmmintrin.h>

TEST_CASE("process() flushes denormals whatever the caller's floating-point mode", "[ruinae][numerics][denormal]") {
    constexpr double kSampleRate = 48000.0;
    constexpr Steinberg::int32 kBlock = 512;
    constexpr int kBlocks = 24;  // about 0.26 s of a held note
    constexpr unsigned int kFtzDaz = 0x8000u | 0x0040u;

    auto proc = Steinberg::owned(new Ruinae::Processor());
    REQUIRE(proc->initialize(nullptr) == Steinberg::kResultTrue);
    Steinberg::Vst::ProcessSetup setup{.processMode = Steinberg::Vst::kRealtime,
                                       .symbolicSampleSize = Steinberg::Vst::kSample32,
                                       .maxSamplesPerBlock = kBlock,
                                       .sampleRate = kSampleRate};
    REQUIRE(proc->setupProcessing(setup) == Steinberg::kResultTrue);
    REQUIRE(proc->setActive(true) == Steinberg::kResultTrue);

    std::vector<float> outL(static_cast<size_t>(kBlock), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlock), 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channels;

    Krate::Test::ParameterChanges paramChanges;
    Krate::Test::EventList events;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = kBlock;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &paramChanges;
    data.inputEvents = &events;
    data.processContext = nullptr;

    volatile double tinyGain = 1e-40;  // Master Gain normalized; x 2 is a subnormal float
    size_t denormalOut = 0;
    size_t nonZeroOut = 0;
    unsigned int modeAfter = 0;

    const unsigned int saved = _mm_getcsr();
    _mm_setcsr(saved & ~kFtzDaz);
    for (int b = 0; b < kBlocks; ++b) {
        paramChanges.clear();
        events.clear();
        if (b == 0) {
            paramChanges.addChange(Ruinae::kMasterGainId, tinyGain);
            events.addNoteOn(60, 0.8f);
        }
        proc->process(data);
        modeAfter |= _mm_getcsr() & kFtzDaz;
        for (const float* out : {outL.data(), outR.data()}) {
            for (Steinberg::int32 i = 0; i < kBlock; ++i) {
                const float v = out[i];
                if (v != 0.0f) ++nonZeroOut;
                if (v != 0.0f && std::fabs(v) < std::numeric_limits<float>::min()) ++denormalOut;
            }
        }
    }
    _mm_setcsr(saved);

    proc->setActive(static_cast<Steinberg::TBool>(false));
    proc->terminate();

    std::printf("FTZ/DAZ: note through a subnormal Master Gain -> %zu non-zero, %zu denormal output samples of %d; "
                "caller's FTZ/DAZ bits after process() 0x%x\n",
                nonZeroOut, denormalOut, 2 * kBlock * kBlocks, modeAfter);
    CHECK(modeAfter == 0u);  // the caller's mode is restored
    CHECK(denormalOut == 0u);
    CHECK(nonZeroOut == 0u);
}

#endif
