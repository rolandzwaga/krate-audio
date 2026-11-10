// ==============================================================================
// Processor::process runs with FTZ / DAZ set whatever the calling thread's floating-point mode
// ==============================================================================
// Krate::DSP::ScopedDenormalMode at the top of process(), as Disrumpo, Innexus, Membrum, Profundum, Seraphis and
// Vorago do. Gradus's only float DSP is the audition monitor (PolyBLEP Sine / Saw / Square through a linear ADSR,
// no filter, no feedback): it has no decaying recursion that passes through the denormal range on its own, so the
// guard is there for consistency. Deterministic check: with the test thread's FTZ / DAZ cleared, a held note
// auditioned at a subnormal Audition Volume (normalized 1e-40, a subnormal float) must come out as 0 (FTZ flushes the
// conversion, DAZ reads a subnormal operand as 0), and the caller's mode is restored after every process(). x86 only
// (MXCSR).

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "vst_event_list.h"
#include "vst_param_changes.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>
#include <xmmintrin.h>

TEST_CASE("process() flushes denormals whatever the caller's floating-point mode", "[gradus][numerics][denormal]") {
    constexpr double kSampleRate = 44100.0;
    constexpr Steinberg::int32 kBlock = 512;
    constexpr int kBlocks = 87;  // about 1 s of 1/4 steps at 120 BPM
    constexpr unsigned int kFtzDaz = 0x8000u | 0x0040u;

    auto proc = std::make_unique<Gradus::Processor>();
    REQUIRE(proc->initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup{.processMode = Steinberg::Vst::kRealtime,
                                       .symbolicSampleSize = Steinberg::Vst::kSample32,
                                       .maxSamplesPerBlock = kBlock,
                                       .sampleRate = kSampleRate};
    REQUIRE(proc->setupProcessing(setup) == Steinberg::kResultOk);
    REQUIRE(proc->setActive(static_cast<Steinberg::TBool>(true)) == Steinberg::kResultOk);

    std::vector<float> outL(static_cast<size_t>(kBlock), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlock), 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channels;

    Steinberg::Vst::ProcessContext ctx{};
    ctx.state = Steinberg::Vst::ProcessContext::kPlaying | Steinberg::Vst::ProcessContext::kTempoValid;
    ctx.tempo = 120.0;
    ctx.sampleRate = kSampleRate;

    Krate::Test::ParameterChanges params;
    Krate::Test::EventList inEvents;
    Krate::Test::EventList outEvents;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = kBlock;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &params;
    data.inputEvents = &inEvents;
    data.outputEvents = &outEvents;
    data.processContext = &ctx;

    volatile double tinyVolume = 1e-40;  // Audition Volume normalized; as a float it is subnormal
    size_t denormalOut = 0;
    size_t nonZeroOut = 0;
    size_t noteOnsOut = 0;
    unsigned int modeAfter = 0;

    const unsigned int saved = _mm_getcsr();
    _mm_setcsr(saved & ~kFtzDaz);
    for (int b = 0; b < kBlocks; ++b) {
        params.clear();
        inEvents.clear();
        outEvents.clear();
        if (b == 0) {
            params.add(Gradus::kArpSourceModeId, 0.0);  // Live
            params.add(Gradus::kArpTempoSyncId, 1.0);
            params.add(Gradus::kArpNoteValueId, 13.0 / 29.0);  // 1/4
            params.add(Gradus::kAuditionEnabledId, 1.0);
            params.add(Gradus::kAuditionVolumeId, tinyVolume);
            params.add(Gradus::kAuditionDecayId, 1.0);
            inEvents.addNoteOn(60, 0.8f);
        }
        proc->process(data);
        modeAfter |= _mm_getcsr() & kFtzDaz;
        for (Steinberg::int32 i = 0; i < outEvents.getEventCount(); ++i) {
            Steinberg::Vst::Event e{};
            if (outEvents.getEvent(i, e) == Steinberg::kResultOk && e.type == Steinberg::Vst::Event::kNoteOnEvent) {
                ++noteOnsOut;
            }
        }
        for (const float* out : {outL.data(), outR.data()}) {
            for (Steinberg::int32 i = 0; i < kBlock; ++i) {
                const float v = out[i];
                if (v != 0.0f) ++nonZeroOut;
                if (v != 0.0f && std::fabs(v) < std::numeric_limits<float>::min()) ++denormalOut;
            }
        }
        ctx.projectTimeSamples += kBlock;
    }
    _mm_setcsr(saved);

    proc->setActive(static_cast<Steinberg::TBool>(false));
    proc->terminate();

    std::printf("FTZ/DAZ: %zu auditioned note-ons at a subnormal Audition Volume -> %zu non-zero, %zu denormal output "
                "samples of %d; caller's FTZ/DAZ bits after process() 0x%x\n",
                noteOnsOut, nonZeroOut, denormalOut, 2 * kBlock * kBlocks, modeAfter);
    CHECK(noteOnsOut > 0u);  // the arp played, so the monitor rendered
    CHECK(modeAfter == 0u);  // the caller's mode is restored
    CHECK(denormalOut == 0u);
    CHECK(nonZeroOut == 0u);
}

#endif
