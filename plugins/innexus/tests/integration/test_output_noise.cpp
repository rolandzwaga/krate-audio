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
// Test 0: Raw MCF precision — float32 vs float64 vs measurement method
// =============================================================================
TEST_CASE("Noise isolation: MCF precision and measurement validation",
          "[innexus][noise][diagnostic]")
{
    constexpr double kSR = 44100.0;
    constexpr float kFreq = 440.0f;
    constexpr size_t kN = 4096;
    std::vector<float> harmonics = {kFreq};

    // --- A: float32 MCF (no renorm) ---
    float snrF32 = 0.0f;
    {
        float epsilon = 2.0f * std::sin(3.14159265f * kFreq / static_cast<float>(kSR));
        float s = 0.0f, c = 1.0f;
        for (size_t i = 0; i < 8192; ++i) {
            float sNew = s + epsilon * c;
            float cNew = c - epsilon * sNew;
            s = sNew; c = cNew;
        }
        std::vector<float> buf(kN);
        for (size_t i = 0; i < kN; ++i) {
            float sNew = s + epsilon * c;
            float cNew = c - epsilon * sNew;
            buf[i] = sNew;
            s = sNew; c = cNew;
        }
        snrF32 = measureSnrDb(buf, kSR, harmonics);
        INFO("[float32 MCF] SNR = " << snrF32 << " dB");
    }

    // --- B: float64 MCF, cast to float32 for measurement ---
    float snrF64 = 0.0f;
    {
        double epsilon = 2.0 * std::sin(3.14159265358979323846 * static_cast<double>(kFreq) / kSR);
        double s = 0.0, c = 1.0;
        for (size_t i = 0; i < 8192; ++i) {
            double sNew = s + epsilon * c;
            double cNew = c - epsilon * sNew;
            s = sNew; c = cNew;
        }
        std::vector<float> buf(kN);
        for (size_t i = 0; i < kN; ++i) {
            double sNew = s + epsilon * c;
            double cNew = c - epsilon * sNew;
            buf[i] = static_cast<float>(sNew);
            s = sNew; c = cNew;
        }
        snrF64 = measureSnrDb(buf, kSR, harmonics);
        INFO("[float64 MCF] SNR = " << snrF64 << " dB");
    }

    // --- C: Reference std::sin (perfect, no recursion) ---
    float snrRef = 0.0f;
    {
        std::vector<float> buf(kN);
        double phase = 0.0;
        double phaseInc = 2.0 * 3.14159265358979323846 * static_cast<double>(kFreq) / kSR;
        // Advance past settle period
        phase += phaseInc * 8192.0;
        for (size_t i = 0; i < kN; ++i) {
            buf[i] = static_cast<float>(std::sin(phase));
            phase += phaseInc;
        }
        snrRef = measureSnrDb(buf, kSR, harmonics);
        INFO("[std::sin reference] SNR = " << snrRef << " dB");
    }

    INFO("float32 MCF: " << snrF32 << " dB");
    INFO("float64 MCF: " << snrF64 << " dB");
    INFO("std::sin ref: " << snrRef << " dB");

    // float64 should be dramatically better than float32
    // std::sin should be the best (no recursive error)
    // If float32 is already >80 dB, the noise is in our measurement, not the MCF
    // At the FFT measurement ceiling, all three converge to ~50.6 dB
    CHECK(std::abs(snrF64 - snrF32) < 1.0f);
    CHECK(snrRef >= snrF64);

    // Guard: float32 MCF should be at least 45 dB (regression)
    REQUIRE(snrF32 > 45.0f);
}

// =============================================================================
// Test 0b: Static vs varying analysis frames through full processor
// =============================================================================
TEST_CASE("Noise isolation: static frames vs varying frames",
          "[innexus][noise][diagnostic]")
{
    constexpr float kF0 = 440.0f;
    constexpr int kPartials = 8;
    constexpr float kBaseAmp = 0.4f;

    // Build a single static frame
    Krate::DSP::HarmonicFrame staticFrame{};
    staticFrame.f0 = kF0;
    staticFrame.f0Confidence = 1.0f;
    staticFrame.numPartials = kPartials;
    staticFrame.globalAmplitude = kBaseAmp;
    staticFrame.noisiness = 0.0f;
    for (int p = 0; p < kPartials; ++p) {
        auto& partial = staticFrame.partials[static_cast<size_t>(p)];
        partial.harmonicIndex = p + 1;
        partial.frequency = kF0 * static_cast<float>(p + 1);
        partial.amplitude = kBaseAmp / static_cast<float>(p + 1);
        partial.relativeFrequency = static_cast<float>(p + 1);
        partial.stability = 1.0f;
        partial.age = 20;
    }

    auto runWithAnalysis = [&](Innexus::SampleAnalysis* analysis) -> float
    {
        Innexus::Processor proc;
        proc.initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = kTestBlockSize;
        setup.sampleRate = kTestSampleRate;
        proc.setupProcessing(setup);
        proc.setActive(true);
        proc.testInjectAnalysis(analysis);

        NoiseTestEventList events;
        std::vector<float> outL(kTestBlockSize, 0.0f);
        std::vector<float> outR(kTestBlockSize, 0.0f);
        float* channels[2] = {outL.data(), outR.data()};
        AudioBusBuffers outBus{}; outBus.numChannels = 2; outBus.channelBuffers32 = channels;
        ProcessData data{};
        data.processMode = kRealtime; data.symbolicSampleSize = kSample32;
        data.numSamples = kTestBlockSize; data.numOutputs = 1; data.outputs = &outBus;
        data.inputEvents = &events;

        events.addNoteOn(69, 0.8f);
        proc.process(data);
        events.clear();

        for (int b = 0; b < 50; ++b) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            proc.process(data);
        }

        constexpr size_t kN = 4096;
        constexpr int kBlocks = kN / kTestBlockSize;
        std::vector<float> buf(kN);
        for (int b = 0; b < kBlocks; ++b) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            proc.process(data);
            std::copy(outL.begin(), outL.end(),
                      buf.begin() + static_cast<size_t>(b) * kTestBlockSize);
        }

        proc.setActive(false);
        proc.terminate();

        std::vector<float> harmonics;
        for (int h = 1; h <= kPartials; ++h)
            harmonics.push_back(kF0 * static_cast<float>(h));
        return measureSnrDb(buf, kTestSampleRate, harmonics);
    };

    // --- A: All frames identical (perfectly static) ---
    float snrStatic;
    {
        auto* analysis = new Innexus::SampleAnalysis();
        analysis->sampleRate = static_cast<float>(kTestSampleRate);
        analysis->hopTimeSec = static_cast<float>(kTestBlockSize) / static_cast<float>(kTestSampleRate);
        analysis->analysisFFTSize = 1024;
        analysis->analysisHopSize = 512;
        for (int f = 0; f < 200; ++f) {
            analysis->frames.push_back(staticFrame);
            Krate::DSP::ResidualFrame rf{};
            analysis->residualFrames.push_back(rf);
        }
        analysis->totalFrames = analysis->frames.size();
        analysis->filePath = "test_static.wav";
        snrStatic = runWithAnalysis(analysis);
        INFO("[Static frames] SNR = " << snrStatic << " dB");
    }

    // --- B: Frames with ±5% random amplitude variation per partial ---
    float snrVarying;
    {
        auto* analysis = new Innexus::SampleAnalysis();
        analysis->sampleRate = static_cast<float>(kTestSampleRate);
        analysis->hopTimeSec = static_cast<float>(kTestBlockSize) / static_cast<float>(kTestSampleRate);
        analysis->analysisFFTSize = 1024;
        analysis->analysisHopSize = 512;

        uint32_t rng = 42;
        for (int f = 0; f < 200; ++f) {
            Krate::DSP::HarmonicFrame frame = staticFrame;
            for (int p = 0; p < kPartials; ++p) {
                // Simple LCG for deterministic "randomness"
                rng = rng * 1664525u + 1013904223u;
                float jitter = (static_cast<float>(rng >> 16) / 32768.0f - 1.0f) * 0.05f;
                frame.partials[static_cast<size_t>(p)].amplitude *= (1.0f + jitter);
            }
            analysis->frames.push_back(frame);
            Krate::DSP::ResidualFrame rf{};
            analysis->residualFrames.push_back(rf);
        }
        analysis->totalFrames = analysis->frames.size();
        analysis->filePath = "test_varying.wav";
        snrVarying = runWithAnalysis(analysis);
        INFO("[Varying frames ±5%] SNR = " << snrVarying << " dB");
    }

    INFO("Static frames SNR: " << snrStatic << " dB");
    INFO("Varying frames SNR: " << snrVarying << " dB");
    INFO("Difference: " << (snrStatic - snrVarying) << " dB");

    // Static should be clean (near measurement ceiling)
    REQUIRE(snrStatic > 40.0f);
    // If varying is much worse, frame variation is the noise source
}

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

// =============================================================================
// Signal path noise tracing: measure SNR with each processor stage disabled
// to identify which stage introduces inter-harmonic noise.
// =============================================================================

TEST_CASE("Noise tracing: identify which processor stage adds noise",
          "[innexus][noise][diagnostic][tracing]")
{
    constexpr double kSR = 44100.0;
    constexpr float kF0 = 440.0f;
    constexpr int kPartials = 8;
    constexpr float kBaseAmp = 0.4f;

    // Build a single clean HarmonicFrame
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

    std::vector<float> harmonics;
    for (int h = 1; h <= kPartials; ++h)
        harmonics.push_back(kF0 * static_cast<float>(h));

    // Helper: run full processor with given parameter overrides, return SNR
    auto runWithParams = [&](
        const std::vector<std::pair<Steinberg::Vst::ParamID, double>>& overrides,
        const char* label) -> float
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
        AudioBusBuffers outBus{};
        outBus.numChannels = 2;
        outBus.channelBuffers32 = channels;
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kTestBlockSize;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.inputEvents = &events;

        // Apply parameter overrides
        for (const auto& [id, val] : overrides)
            params.addChange(id, val);
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

        // Capture 4096 samples
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

        proc.setActive(false);
        proc.terminate();

        float snr = measureSnrDb(buf, kSR, harmonics);
        INFO("[" << label << "] SNR = " << snr << " dB");
        return snr;
    };

    // Baseline: all noise sources disabled
    float snrBaseline = runWithParams({
        {Innexus::kResidualLevelId, 0.0},
        {Innexus::kPhysModelMixId, 0.0},
        {Innexus::kWarmthId, 0.0},
        {Innexus::kCouplingId, 0.0},
        {Innexus::kEntropyId, 0.0},
        {Innexus::kStabilityId, 0.0},
        {Innexus::kEvolutionEnableId, 0.0},
        {Innexus::kMod1EnableId, 0.0},
        {Innexus::kMod2EnableId, 0.0},
        {Innexus::kStereoSpreadId, 0.0},
        {Innexus::kDetuneSpreadId, 0.0},
        {Innexus::kSympatheticAmountId, 0.0},
    }, "Baseline (all features off)");

    // Test each feature independently to find which degrades SNR
    SECTION("Sympathetic resonance") {
        float snr = runWithParams({
            {Innexus::kResidualLevelId, 0.0},
            {Innexus::kPhysModelMixId, 0.0},
            {Innexus::kSympatheticAmountId, 0.5},
        }, "Sympathetic resonance at 0.5");
        INFO("SNR drop from baseline: " << (snrBaseline - snr) << " dB");
    }

    SECTION("Warmth") {
        float snr = runWithParams({
            {Innexus::kResidualLevelId, 0.0},
            {Innexus::kPhysModelMixId, 0.0},
            {Innexus::kWarmthId, 0.5},
        }, "Warmth at 0.5");
        INFO("SNR drop from baseline: " << (snrBaseline - snr) << " dB");
    }

    SECTION("Default settings (only residual+phys off)") {
        float snr = runWithParams({
            {Innexus::kResidualLevelId, 0.0},
            {Innexus::kPhysModelMixId, 0.0},
        }, "Default (only residual+phys off)");
        INFO("SNR drop from baseline: " << (snrBaseline - snr) << " dB");
        INFO("This should match the failing test's 37.5 dB");
    }

    // The baseline with ALL features off should match bare osc bank SNR
    REQUIRE(snrBaseline > 40.0f);
}
