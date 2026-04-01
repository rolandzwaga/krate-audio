// ==============================================================================
// Output Noise Diagnostic Test
// ==============================================================================
// End-to-end test: injects a clean harmonic analysis at default settings,
// captures output, and measures harmonic-to-noise ratio via FFT.
// If inter-harmonic energy is high relative to harmonic peaks, the synth
// is adding noise that wasn't in the input.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/primitives/fft.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

// =============================================================================
// Minimal test infrastructure (same pattern as pipeline integration tests)
// =============================================================================

static constexpr double kTestSampleRate = 44100.0;
static constexpr int32 kTestBlockSize = 512;

class NoiseTestEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size()))
            return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultTrue;
    }
    tresult PLUGIN_API addEvent(Event& e) override
    {
        events_.push_back(e);
        return kResultTrue;
    }

    void addNoteOn(int16 pitch, float velocity, int32 sampleOffset = 0)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = pitch;
        e.noteOn.tuning = 0.0f;
        e.noteOn.length = 0;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

class NoiseTestParamValueQueue : public IParamValueQueue
{
public:
    NoiseTestParamValueQueue(ParamID id, ParamValue val) : id_(id), value_(val) {}
    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32, int32& sampleOffset, ParamValue& value) override
    {
        sampleOffset = 0;
        value = value_;
        return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    ParamID id_;
    ParamValue value_;
};

class NoiseTestParameterChanges : public IParameterChanges
{
public:
    void addChange(ParamID id, ParamValue val) { queues_.emplace_back(id, val); }
    int32 PLUGIN_API getParameterCount() override { return static_cast<int32>(queues_.size()); }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
    {
        if (index < 0 || index >= static_cast<int32>(queues_.size())) return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    void clear() { queues_.clear(); }

private:
    std::vector<NoiseTestParamValueQueue> queues_;
};

// =============================================================================
// Test: Output noise analysis at default settings
// =============================================================================

TEST_CASE("Innexus output noise diagnostic: default settings, clean harmonic input",
          "[innexus][noise][diagnostic]")
{
    // --- Setup processor with all defaults ---
    Innexus::Processor proc;
    proc.initialize(nullptr);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kTestBlockSize;
    setup.sampleRate = kTestSampleRate;
    proc.setupProcessing(setup);
    proc.setActive(true);

    // --- Create a perfectly clean harmonic analysis ---
    // 8 harmonics of A4 (440 Hz), 1/n amplitude series, zero noise
    constexpr float kF0 = 440.0f;
    constexpr int kNumPartials = 8;
    constexpr int kNumFrames = 100; // enough for sustained note
    constexpr float kBaseAmp = 0.4f;

    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = static_cast<float>(kTestSampleRate);
    analysis->hopTimeSec = static_cast<float>(kTestBlockSize) / static_cast<float>(kTestSampleRate);
    analysis->analysisFFTSize = 1024;
    analysis->analysisHopSize = 512;

    for (int f = 0; f < kNumFrames; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = kF0;
        frame.f0Confidence = 1.0f;
        frame.numPartials = kNumPartials;
        frame.globalAmplitude = kBaseAmp;
        frame.spectralCentroid = 1200.0f;
        frame.brightness = 0.5f;
        frame.noisiness = 0.0f; // perfectly clean

        for (int p = 0; p < kNumPartials; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = kF0 * static_cast<float>(p + 1);
            partial.amplitude = kBaseAmp / static_cast<float>(p + 1);
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.inharmonicDeviation = 0.0f;
            partial.stability = 1.0f;
            partial.age = 20;
            partial.phase = 0.0f;
        }
        analysis->frames.push_back(frame);

        // Residual frames with ZERO energy (clean signal has no noise)
        Krate::DSP::ResidualFrame rf{};
        rf.totalEnergy = 0.0f;
        for (auto& b : rf.bandEnergies) b = 0.0f;
        rf.transientFlag = false;
        analysis->residualFrames.push_back(rf);
    }
    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_clean_harmonic.wav";
    proc.testInjectAnalysis(analysis);

    // --- Process: trigger note, let settle, capture output ---
    NoiseTestEventList events;
    NoiseTestParameterChanges paramChanges;

    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);
    float* outChannels[2] = {outL.data(), outR.data()};

    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannels;
    outputBus.silenceFlags = 0;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kTestBlockSize;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &events;
    data.outputEvents = nullptr;

    // Note on: A4 (MIDI 69)
    events.addNoteOn(69, 0.8f);
    data.inputParameterChanges = nullptr;
    proc.process(data);
    events.clear();

    // Let smoothers and oscillators settle (50 blocks ≈ 0.6 sec)
    for (int b = 0; b < 50; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        proc.process(data);
    }

    // Capture 8 blocks of steady-state output (4096 samples = good FFT size)
    constexpr int kCaptureBlocks = 8;
    constexpr size_t kCaptureSamples = kCaptureBlocks * kTestBlockSize; // 4096
    std::vector<float> capturedL(kCaptureSamples);

    for (int b = 0; b < kCaptureBlocks; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        proc.process(data);
        std::copy(outL.begin(), outL.end(),
                  capturedL.begin() + static_cast<size_t>(b) * kTestBlockSize);
    }

    // --- Sanity: verify we have signal ---
    float totalRms = 0.0f;
    for (float s : capturedL) totalRms += s * s;
    totalRms = std::sqrt(totalRms / static_cast<float>(kCaptureSamples));
    INFO("Total output RMS: " << totalRms);
    REQUIRE(totalRms > 1e-4f); // must have signal

    // --- FFT the captured output ---
    // Use 4096-point FFT for good frequency resolution (~10.7 Hz/bin)
    constexpr size_t kFFTSize = 4096;
    static_assert(kCaptureSamples == kFFTSize, "Capture size must match FFT size");

    // Apply Hann window
    std::vector<float> windowed(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * static_cast<float>(i)
                                           / static_cast<float>(kFFTSize)));
        windowed[i] = capturedL[i] * w;
    }

    Krate::DSP::FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Krate::DSP::Complex> spectrum(kFFTSize / 2 + 1);
    fft.forward(windowed.data(), spectrum.data());

    // Compute magnitude spectrum
    const size_t numBins = kFFTSize / 2 + 1;
    std::vector<float> magnitudes(numBins);
    for (size_t k = 0; k < numBins; ++k)
        magnitudes[k] = std::sqrt(spectrum[k].real * spectrum[k].real +
                                   spectrum[k].imag * spectrum[k].imag);

    // --- Measure harmonic vs inter-harmonic energy ---
    const float binFreqHz = static_cast<float>(kTestSampleRate) / static_cast<float>(kFFTSize);
    const float halfBinWidth = 3.0f; // ±3 bins around each harmonic peak

    float harmonicEnergy = 0.0f;
    float noiseEnergy = 0.0f;
    std::vector<bool> isHarmonicBin(numBins, false);

    // Mark bins near each harmonic as "harmonic"
    for (int h = 1; h <= kNumPartials; ++h)
    {
        float harmonicFreq = kF0 * static_cast<float>(h);
        int centerBin = static_cast<int>(std::round(harmonicFreq / binFreqHz));
        int lo = std::max(0, centerBin - static_cast<int>(halfBinWidth));
        int hi = std::min(static_cast<int>(numBins) - 1,
                          centerBin + static_cast<int>(halfBinWidth));
        for (int k = lo; k <= hi; ++k)
            isHarmonicBin[static_cast<size_t>(k)] = true;
    }

    // Also exclude DC (bin 0) and near-Nyquist
    isHarmonicBin[0] = true;
    if (numBins > 1) isHarmonicBin[numBins - 1] = true;

    for (size_t k = 0; k < numBins; ++k)
    {
        float mag2 = magnitudes[k] * magnitudes[k];
        if (isHarmonicBin[k])
            harmonicEnergy += mag2;
        else
            noiseEnergy += mag2;
    }

    float snrDb = 10.0f * std::log10(harmonicEnergy / std::max(noiseEnergy, 1e-30f));

    INFO("Harmonic energy: " << harmonicEnergy);
    INFO("Inter-harmonic (noise) energy: " << noiseEnergy);
    INFO("SNR: " << snrDb << " dB");

    // --- Report top 5 noise bins for diagnosis ---
    struct NoiseBin { size_t bin; float freq; float mag; };
    std::vector<NoiseBin> noiseBins;
    for (size_t k = 1; k < numBins - 1; ++k)
    {
        if (!isHarmonicBin[k])
            noiseBins.push_back({k, static_cast<float>(k) * binFreqHz, magnitudes[k]});
    }
    std::sort(noiseBins.begin(), noiseBins.end(),
              [](const NoiseBin& a, const NoiseBin& b) { return a.mag > b.mag; });

    for (size_t i = 0; i < std::min<size_t>(5, noiseBins.size()); ++i)
    {
        INFO("Noise bin #" << (i + 1) << ": bin=" << noiseBins[i].bin
             << " freq=" << noiseBins[i].freq << " Hz"
             << " mag=" << noiseBins[i].mag);
    }

    // Clean additive synth with 8 harmonics should achieve good SNR.
    // Before fix: ~32 dB (periodic renorm noise). After fix: ~42 dB.
    // Threshold guards against regression.
    REQUIRE(snrDb > 40.0f);

    // Cleanup
    proc.setActive(false);
    proc.terminate();
}

// =============================================================================
// Isolation test: HarmonicOscillatorBank alone (no plugin processor)
// =============================================================================

#include <krate/dsp/processors/harmonic_oscillator_bank.h>

/// Measure SNR of a signal buffer using FFT. Returns SNR in dB.
/// harmonicFreqs: list of expected harmonic frequencies
/// halfBinWidth: bins around each harmonic to count as "signal"
static float measureSnrDb(const std::vector<float>& signal,
                          double sampleRate,
                          const std::vector<float>& harmonicFreqs,
                          float halfBinWidth = 3.0f)
{
    const size_t N = signal.size();
    // Hann window
    std::vector<float> windowed(N);
    for (size_t i = 0; i < N; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * static_cast<float>(i)
                                           / static_cast<float>(N)));
        windowed[i] = signal[i] * w;
    }

    Krate::DSP::FFT fft;
    fft.prepare(N);
    std::vector<Krate::DSP::Complex> spectrum(N / 2 + 1);
    fft.forward(windowed.data(), spectrum.data());

    const size_t numBins = N / 2 + 1;
    const float binFreqHz = static_cast<float>(sampleRate) / static_cast<float>(N);

    std::vector<bool> isHarmonic(numBins, false);
    isHarmonic[0] = true; // exclude DC
    if (numBins > 1) isHarmonic[numBins - 1] = true; // exclude Nyquist

    for (float hf : harmonicFreqs)
    {
        int center = static_cast<int>(std::round(hf / binFreqHz));
        int lo = std::max(0, center - static_cast<int>(halfBinWidth));
        int hi = std::min(static_cast<int>(numBins) - 1, center + static_cast<int>(halfBinWidth));
        for (int k = lo; k <= hi; ++k)
            isHarmonic[static_cast<size_t>(k)] = true;
    }

    float harmE = 0.0f, noiseE = 0.0f;
    for (size_t k = 0; k < numBins; ++k)
    {
        float mag2 = spectrum[k].real * spectrum[k].real + spectrum[k].imag * spectrum[k].imag;
        if (isHarmonic[k]) harmE += mag2;
        else noiseE += mag2;
    }

    return 10.0f * std::log10(harmE / std::max(noiseE, 1e-30f));
}

// =============================================================================
// Test 0: Raw MCF oscillator math (no oscillator bank wrapper)
// =============================================================================
// =============================================================================
// Test 1: Oscillator bank, single load — regression test for MCF noise
// =============================================================================
TEST_CASE("Noise isolation: oscillator bank alone produces clean output",
          "[innexus][noise][diagnostic]")
{
    constexpr double kSR = 44100.0;
    constexpr float kF0 = 440.0f;
    constexpr int kPartials = 8;
    constexpr float kBaseAmp = 0.4f;

    // Build a single HarmonicFrame
    Krate::DSP::HarmonicFrame frame{};
    frame.f0 = kF0;
    frame.f0Confidence = 1.0f;
    frame.numPartials = kPartials;
    frame.globalAmplitude = kBaseAmp;
    frame.noisiness = 0.0f;

    for (int p = 0; p < kPartials; ++p)
    {
        auto& partial = frame.partials[static_cast<size_t>(p)];
        partial.harmonicIndex = p + 1;
        partial.frequency = kF0 * static_cast<float>(p + 1);
        partial.amplitude = kBaseAmp / static_cast<float>(p + 1);
        partial.relativeFrequency = static_cast<float>(p + 1);
        partial.inharmonicDeviation = 0.0f;
        partial.stability = 1.0f;
        partial.age = 20;
        partial.phase = 0.0f;
    }

    // --- Test A: Bare oscillator bank, single frame, sustained ---
    {
        Krate::DSP::HarmonicOscillatorBank osc;
        osc.prepare(kSR);
        osc.loadFrame(frame, kF0);

        // Let settle
        for (int i = 0; i < 4096; ++i)
        {
            float l = 0.0f, r = 0.0f;
            osc.processStereo(l, r);
        }

        // Capture 4096 samples
        constexpr size_t kN = 4096;
        std::vector<float> buf(kN);
        for (size_t i = 0; i < kN; ++i)
        {
            float l = 0.0f, r = 0.0f;
            osc.processStereo(l, r);
            buf[i] = l;
        }

        std::vector<float> harmonics;
        for (int h = 1; h <= kPartials; ++h)
            harmonics.push_back(kF0 * static_cast<float>(h));

        float snr = measureSnrDb(buf, kSR, harmonics);
        INFO("[Bare osc bank, single load] SNR = " << snr << " dB");
        // 45 dB = ~32x less noise power than signal. Regression guard against
        // reintroducing periodic renormalization (which dropped this to ~32 dB).
        REQUIRE(snr > 45.0f);
    }

    // --- Test B: Oscillator bank with repeated loadFrame (same data) ---
    // Simulates frame-by-frame reloading as the processor does
    {
        Krate::DSP::HarmonicOscillatorBank osc;
        osc.prepare(kSR);
        osc.loadFrame(frame, kF0);

        // Process with periodic frame reloads (every 512 samples)
        for (int i = 0; i < 4096; ++i)
        {
            if (i % 512 == 0)
                osc.loadFrame(frame, kF0);
            float l = 0.0f, r = 0.0f;
            osc.processStereo(l, r);
        }

        constexpr size_t kN = 4096;
        std::vector<float> buf(kN);
        for (size_t i = 0; i < kN; ++i)
        {
            if (i % 512 == 0)
                osc.loadFrame(frame, kF0);
            float l = 0.0f, r = 0.0f;
            osc.processStereo(l, r);
            buf[i] = l;
        }

        std::vector<float> harmonics;
        for (int h = 1; h <= kPartials; ++h)
            harmonics.push_back(kF0 * static_cast<float>(h));

        float snr = measureSnrDb(buf, kSR, harmonics);
        INFO("[Osc bank, periodic reload] SNR = " << snr << " dB");
        REQUIRE(snr > 45.0f);
    }

    // --- Test C: Full processor, default settings, all non-harmonic features off ---
    // Harmonic physics warmth/coupling/entropy at 0, no modulators, no resonator
    {
        Innexus::Processor proc;
        proc.initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = kTestBlockSize;
        setup.sampleRate = kSR;
        proc.setupProcessing(setup);
        proc.setActive(true);

        auto* analysis = new Innexus::SampleAnalysis();
        analysis->sampleRate = static_cast<float>(kSR);
        analysis->hopTimeSec = static_cast<float>(kTestBlockSize) / static_cast<float>(kSR);
        analysis->analysisFFTSize = 1024;
        analysis->analysisHopSize = 512;

        for (int f = 0; f < 100; ++f)
        {
            analysis->frames.push_back(frame);
            Krate::DSP::ResidualFrame rf{};
            analysis->residualFrames.push_back(rf);
        }
        analysis->totalFrames = analysis->frames.size();
        analysis->filePath = "test.wav";
        proc.testInjectAnalysis(analysis);

        NoiseTestEventList events;
        NoiseTestParameterChanges params;
        std::vector<float> outL(kTestBlockSize, 0.0f);
        std::vector<float> outR(kTestBlockSize, 0.0f);
        float* channels[2] = {outL.data(), outR.data()};
        AudioBusBuffers outBus{}; outBus.numChannels = 2; outBus.channelBuffers32 = channels;
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kTestBlockSize;
        data.numOutputs = 1; data.outputs = &outBus;
        data.inputEvents = &events;

        // Zero out anything that could add noise
        params.addChange(Innexus::kResidualLevelId, 0.0);       // no residual
        params.addChange(Innexus::kPhysModelMixId, 0.0);         // no physical model
        data.inputParameterChanges = &params;

        events.addNoteOn(69, 0.8f);
        proc.process(data);
        events.clear();
        params.clear();
        data.inputParameterChanges = nullptr;

        // Settle
        for (int b = 0; b < 50; ++b)
        {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            proc.process(data);
        }

        // Capture
        constexpr size_t kN = 4096;
        constexpr int kBlocks = static_cast<int>(kN / kTestBlockSize);
        std::vector<float> buf(kN);
        for (int b = 0; b < kBlocks; ++b)
        {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            proc.process(data);
            std::copy(outL.begin(), outL.end(),
                      buf.begin() + static_cast<size_t>(b) * kTestBlockSize);
        }

        std::vector<float> harmonics;
        for (int h = 1; h <= kPartials; ++h)
            harmonics.push_back(kF0 * static_cast<float>(h));

        float snr = measureSnrDb(buf, kSR, harmonics);
        INFO("[Full processor, residual=0, physModel=0] SNR = " << snr << " dB");
        REQUIRE(snr > 40.0f);

        proc.setActive(false);
        proc.terminate();
    }
}
