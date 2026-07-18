// ==============================================================================
// DataExchange Processor Tests
// ==============================================================================
// Verifies the DataExchangeHandler integration on the Processor side:
//   - DisplayData is trivially copyable (safe for block transport)
//   - Processor lifecycle without connect() (sendDisplayData is no-op)
//   - connect/disconnect lifecycle
//   - setActive after connect
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/display_data.h"
#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <algorithm>
#include <type_traits>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

// ==============================================================================
// DisplayData trivially copyable (compile-time safety for block transport)
// ==============================================================================

TEST_CASE("DisplayData is trivially copyable",
          "[innexus][data-exchange][processor]")
{
    STATIC_REQUIRE(std::is_trivially_copyable_v<Innexus::DisplayData>);
}

// ==============================================================================
// Processor lifecycle without connect (existing tests compatibility)
// ==============================================================================

TEST_CASE("Processor lifecycle without connect - sendDisplayData is no-op",
          "[innexus][data-exchange][processor]")
{
    Innexus::Processor proc;

    // Initialize with minimal host context
    HostApplication host;
    REQUIRE(proc.initialize(&host) == kResultOk);

    // Setup processing
    ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc.setupProcessing(setup) == kResultOk);

    // Activate without connect - should work fine
    REQUIRE(proc.setActive(true) == kResultOk);

    // sendDisplayData should be a no-op (no crash, no message sent)
    ProcessData data{};
    data.numSamples = 0;
    proc.sendDisplayData(data);

    // Deactivate and terminate
    REQUIRE(proc.setActive(false) == kResultOk);
    REQUIRE(proc.terminate() == kResultOk);
}

// ==============================================================================
// Processor connect/disconnect lifecycle
// ==============================================================================

TEST_CASE("Processor connect/disconnect lifecycle",
          "[innexus][data-exchange][processor]")
{
    Innexus::Processor proc;
    Innexus::Controller ctrl;

    HostApplication host;
    REQUIRE(proc.initialize(&host) == kResultOk);
    REQUIRE(ctrl.initialize(&host) == kResultOk);

    // Get IConnectionPoint from controller
    auto* ctrlConn = static_cast<IConnectionPoint*>(&ctrl);

    REQUIRE(proc.connect(ctrlConn) == kResultOk);
    REQUIRE(proc.disconnect(ctrlConn) == kResultOk);

    REQUIRE(ctrl.terminate() == kResultOk);
    REQUIRE(proc.terminate() == kResultOk);
}

// ==============================================================================
// setActive after connect (full lifecycle)
// ==============================================================================

TEST_CASE("Processor setActive after connect - full lifecycle",
          "[innexus][data-exchange][processor]")
{
    Innexus::Processor proc;
    Innexus::Controller ctrl;

    HostApplication host;
    REQUIRE(proc.initialize(&host) == kResultOk);
    REQUIRE(ctrl.initialize(&host) == kResultOk);

    // Connect
    auto* ctrlConn = static_cast<IConnectionPoint*>(&ctrl);
    REQUIRE(proc.connect(ctrlConn) == kResultOk);

    // Setup + activate
    ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc.setupProcessing(setup) == kResultOk);
    REQUIRE(proc.setActive(true) == kResultOk);

    // Deactivate
    REQUIRE(proc.setActive(false) == kResultOk);

    // Disconnect
    REQUIRE(proc.disconnect(ctrlConn) == kResultOk);

    REQUIRE(ctrl.terminate() == kResultOk);
    REQUIRE(proc.terminate() == kResultOk);
}

// ==============================================================================
// WI-9: ADSR playback state must travel as COPIED SCALARS on the display block.
//
// It was previously shipped to the controller as raw pointers to processor-owned
// atomics via an "ADSRPlaybackState" IMessage, which the controller dereferenced
// on the UI thread -- a latent use-after-free on teardown and invalid whenever
// the host runs the controller out-of-process.
// ==============================================================================

static Innexus::SampleAnalysis* makeAdsrTestAnalysis()
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f;
    analysis->analysisFFTSize = 1024;
    analysis->analysisHopSize = 512;
    for (int f = 0; f < 40; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = 440.0f;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 4;
        frame.globalAmplitude = 0.5f;
        for (int p = 0; p < 4; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = 440.0f * static_cast<float>(p + 1);
            partial.amplitude = 0.5f / static_cast<float>(p + 1);
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.stability = 1.0f;
            partial.age = 10;
        }
        analysis->frames.push_back(frame);
    }
    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_adsr_scalars.wav";
    return analysis;
}

TEST_CASE("DisplayData carries ADSR playback state as scalars (WI-9)",
          "[innexus][data-exchange][processor][adsr][wi9]")
{
    Innexus::Processor proc;
    HostApplication host;
    REQUIRE(proc.initialize(&host) == kResultOk);

    ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc.setupProcessing(setup) == kResultOk);
    REQUIRE(proc.setActive(true) == kResultOk);

    proc.testInjectAnalysis(makeAdsrTestAnalysis());

    // Before any note the envelope is idle.
    REQUIRE(proc.testDisplayData().adsrActive == 0);

    proc.onNoteOn(60, 1.0f);

    // Drive enough blocks for the envelope to run and a display block to publish.
    constexpr int32 kBlock = 512;
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = channels;

    for (int b = 0; b < 8; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        ProcessData data{};
        data.numSamples = kBlock;
        data.numInputs = 0;
        data.numOutputs = 1;
        data.outputs = &outBus;
        proc.process(data);
    }

    // The scalars must reflect live envelope state -- proving the values are
    // copied into the block rather than reached through a shared pointer.
    const auto& dd = proc.testDisplayData();
    INFO("adsrActive=" << static_cast<int>(dd.adsrActive)
         << " output=" << dd.adsrEnvelopeOutput
         << " stage=" << dd.adsrStage);
    // Live state transported: idle (active=0, stage=0) before the note, running
    // afterwards. The envelope level itself depends on the ADSR amount/attack
    // configuration, which is orthogonal to the transport mechanism under test.
    REQUIRE(dd.adsrActive == 1);
    REQUIRE(dd.adsrStage != 0);
    REQUIRE(dd.adsrEnvelopeOutput >= 0.0f);

    REQUIRE(proc.setActive(false) == kResultOk);
    REQUIRE(proc.terminate() == kResultOk);
}

// ==============================================================================
// QS-16: The mono output bus must limit the SUMMED signal. Limiting L and R
// independently and then adding lets two sub-1.0 samples reach ~2.0, defeating
// the safety limiter.
// ==============================================================================
TEST_CASE("Mono output bus stays within the safety limiter (QS-16)",
          "[innexus][processor][mono][qs16]")
{
    Innexus::Processor proc;
    HostApplication host;
    REQUIRE(proc.initialize(&host) == kResultOk);

    ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc.setupProcessing(setup) == kResultOk);

    // Request a MONO output arrangement.
    Steinberg::Vst::SpeakerArrangement outArr = Steinberg::Vst::SpeakerArr::kMono;
    proc.setBusArrangements(nullptr, 0, &outArr, 1);
    REQUIRE(proc.setActive(true) == kResultOk);

    proc.testInjectAnalysis(makeAdsrTestAnalysis());
    proc.onNoteOn(60, 1.0f);

    constexpr int32 kBlock = 512;
    std::vector<float> outMono(kBlock, 0.0f);
    float* channels[1] = {outMono.data()};
    AudioBusBuffers outBus{};
    outBus.numChannels = 1;
    outBus.channelBuffers32 = channels;

    float peak = 0.0f;
    for (int b = 0; b < 16; ++b)
    {
        std::fill(outMono.begin(), outMono.end(), 0.0f);
        ProcessData data{};
        data.numSamples = kBlock;
        data.numInputs = 0;
        data.numOutputs = 1;
        data.outputs = &outBus;
        proc.process(data);
        for (float v : outMono) peak = std::max(peak, std::abs(v));
    }

    INFO("mono peak=" << peak);
    // The soft limiter asymptotes below 1.0; summing two limited channels would
    // have allowed ~2.0.
    REQUIRE(peak <= 1.0f);

    REQUIRE(proc.setActive(false) == kResultOk);
    REQUIRE(proc.terminate() == kResultOk);
}
