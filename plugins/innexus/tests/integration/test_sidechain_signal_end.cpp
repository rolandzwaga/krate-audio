// ==============================================================================
// Sidechain Signal End Behavior Tests
// ==============================================================================
// Integration tests that reproduce the behavior when sidechain input signal
// stops while a MIDI note is still held. The current behavior is an abrupt
// cutoff or spectral artifacts. These tests document the problem and will
// verify any future graceful-decay solution.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include <krate/dsp/processors/harmonic_types.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

static constexpr double kSR = 44100.0;
static constexpr int32 kBlock = 512;

// =============================================================================
// Helpers (same patterns as sidechain_integration_tests.cpp)
// =============================================================================

class SignalEndTestStream : public IBStream
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override
    {
        if (!buffer || numBytes <= 0) return kResultFalse;
        int32 available = static_cast<int32>(data_.size()) - readPos_;
        int32 toRead = std::min(numBytes, available);
        if (toRead <= 0) { if (numBytesRead) *numBytesRead = 0; return kResultFalse; }
        std::memcpy(buffer, data_.data() + readPos_, static_cast<size_t>(toRead));
        readPos_ += toRead;
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override
    {
        if (!buffer || numBytes <= 0) return kResultFalse;
        auto* bytes = static_cast<const char*>(buffer);
        data_.insert(data_.end(), bytes, bytes + numBytes);
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override
    {
        int64 newPos = 0;
        switch (mode) {
        case kIBSeekSet: newPos = pos; break;
        case kIBSeekCur: newPos = readPos_ + pos; break;
        case kIBSeekEnd: newPos = static_cast<int64>(data_.size()) + pos; break;
        default: return kResultFalse;
        }
        if (newPos < 0 || newPos > static_cast<int64>(data_.size())) return kResultFalse;
        readPos_ = static_cast<int32>(newPos);
        if (result) *result = readPos_;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* pos) override
    {
        if (pos) *pos = readPos_;
        return kResultOk;
    }

    void resetReadPos() { readPos_ = 0; }

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

static ProcessSetup makeSetup()
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kBlock;
    setup.sampleRate = kSR;
    return setup;
}

static void setSidechainMode(Innexus::Processor& proc)
{
    SignalEndTestStream stateStream;
    IBStreamer streamer(&stateStream, kLittleEndian);

    streamer.writeInt32(3);           // state version 3
    streamer.writeFloat(100.0f);      // releaseTimeMs
    streamer.writeFloat(1.0f);        // inharmonicityAmount
    streamer.writeFloat(1.0f);        // masterGain (full)
    streamer.writeFloat(0.0f);        // bypass

    streamer.writeInt32(0);           // pathLen (no file)

    // M2 parameters (plain values)
    streamer.writeFloat(1.0f);        // harmonicLevel plain (0.5 norm * 2)
    streamer.writeFloat(0.0f);        // residualLevel plain (0 - harmonics only)
    streamer.writeFloat(0.0f);        // brightness plain (default)
    streamer.writeFloat(0.0f);        // transientEmphasis plain (default)

    streamer.writeInt32(0);           // residualFrameCount
    streamer.writeInt32(0);           // analysisFFTSize
    streamer.writeInt32(0);           // analysisHopSize

    // M3 parameters
    streamer.writeInt32(1);           // inputSource = Sidechain
    streamer.writeInt32(0);           // latencyMode = LowLatency

    stateStream.resetReadPos();
    proc.setState(&stateStream);
}

static void processBlockWithSidechain(
    Innexus::Processor& proc, float* outL, float* outR,
    const float* scL, const float* scR, int32 numSamples)
{
    ProcessData data{};
    data.numSamples = numSamples;
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;

    AudioBusBuffers inBus{};
    inBus.numChannels = 2;
    float* inChannels[2] = { const_cast<float*>(scL), const_cast<float*>(scR) };
    inBus.channelBuffers32 = inChannels;
    data.numInputs = 1;
    data.inputs = &inBus;

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    float* outChannels[2] = { outL, outR };
    outBus.channelBuffers32 = outChannels;
    data.numOutputs = 1;
    data.outputs = &outBus;

    proc.process(data);
}

static void generateSineWave(float* buffer, size_t numSamples,
                              float freqHz, double sampleRate,
                              float amplitude = 0.8f,
                              size_t startSample = 0)
{
    const double twoPi = 2.0 * 3.14159265358979;
    for (size_t i = 0; i < numSamples; ++i)
    {
        buffer[i] = amplitude * static_cast<float>(
            std::sin(twoPi * freqHz * static_cast<double>(startSample + i) / sampleRate));
    }
}

/// Compute RMS of a buffer
static float computeRMS(const float* buffer, size_t numSamples)
{
    if (numSamples == 0) return 0.0f;
    float sumSq = 0.0f;
    for (size_t i = 0; i < numSamples; ++i)
        sumSq += buffer[i] * buffer[i];
    return std::sqrt(sumSq / static_cast<float>(numSamples));
}

/// Compute peak absolute value
static float computePeak(const float* buffer, size_t numSamples)
{
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::abs(buffer[i]));
    return peak;
}

// =============================================================================
// Test: Document current abrupt-cutoff behavior when sidechain signal ends
// =============================================================================

TEST_CASE("Sidechain signal end: output drops abruptly when input stops",
          "[sidechain][signal-end][behavior]")
{
    // This test documents the CURRENT behavior: when sidechain input goes
    // silent while a MIDI note is held, the output drops to silence abruptly
    // (within 1-2 STFT hops) rather than decaying gracefully.

    Innexus::Processor proc;
    REQUIRE(proc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    proc.setupProcessing(setup);
    proc.setActive(true);
    setSidechainMode(proc);

    constexpr int32 blockSize = kBlock;
    std::vector<float> outL(blockSize), outR(blockSize);

    // Phase 1: Feed 440Hz sine wave for ~500ms to establish steady analysis
    constexpr size_t warmupBlocks = 44; // ~512ms at 512 samples/block, 44100Hz
    std::vector<float> scSignal(blockSize);

    for (size_t b = 0; b < warmupBlocks; ++b)
    {
        generateSineWave(scSignal.data(), blockSize, 440.0f, kSR, 0.8f,
                         b * static_cast<size_t>(blockSize));
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  scSignal.data(), scSignal.data(), blockSize);
    }

    // Trigger MIDI note on
    proc.onNoteOn(60, 1.0f);

    // Phase 2: Continue feeding signal + note for ~200ms to get steady output
    constexpr size_t steadyBlocks = 18; // ~209ms
    float steadyStateRMS = 0.0f;
    float steadyStatePeak = 0.0f;

    for (size_t b = 0; b < steadyBlocks; ++b)
    {
        size_t sampleOffset = (warmupBlocks + b) * static_cast<size_t>(blockSize);
        generateSineWave(scSignal.data(), blockSize, 440.0f, kSR, 0.8f, sampleOffset);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  scSignal.data(), scSignal.data(), blockSize);
    }

    // Measure the last steady-state block
    steadyStateRMS = computeRMS(outL.data(), blockSize);
    steadyStatePeak = computePeak(outL.data(), blockSize);

    INFO("Steady-state RMS: " << steadyStateRMS);
    INFO("Steady-state Peak: " << steadyStatePeak);

    // Verify we have audible output during steady state
    REQUIRE(steadyStateRMS > 0.001f);

    // Phase 3: Abruptly stop sidechain input (send zeros), note still held
    // Track per-block RMS to verify graceful spectral decay
    constexpr size_t silenceBlocks = 120; // ~1.4 seconds of silence
    std::vector<float> silence(blockSize, 0.0f);
    std::vector<float> blockRMSValues(silenceBlocks);

    for (size_t b = 0; b < silenceBlocks; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  silence.data(), silence.data(), blockSize);
        blockRMSValues[b] = computeRMS(outL.data(), blockSize);
    }

    // Verify the output decays gradually (not abruptly)
    // The spectral decay should produce a smooth envelope:
    // - First few blocks: output still near steady state (confidence gate latency)
    // - Then: gradual decay over several hundred ms
    // - Eventually: silence

    // Check that output is monotonically non-increasing (allowing small fluctuations)
    int decreasingBlocks = 0;
    for (size_t b = 1; b < silenceBlocks; ++b)
    {
        if (blockRMSValues[b] <= blockRMSValues[b - 1] * 1.05f) // 5% tolerance
            ++decreasingBlocks;
    }

    INFO("Monotonically decreasing blocks: " << decreasingBlocks
         << " / " << (silenceBlocks - 1));
    // Most blocks should show decreasing output (allowing for initial plateau)
    CHECK(decreasingBlocks > static_cast<int>(silenceBlocks * 0.8));

    proc.setActive(false);
    proc.terminate();
}

TEST_CASE("Sidechain signal end: gradual fade produces better transition than abrupt stop",
          "[sidechain][signal-end][behavior]")
{
    // Compare behavior: abrupt stop vs. natural fade-out of input signal.
    // This helps understand whether a solution in the source signal (fade)
    // or in the plugin's response (internal decay) would be more effective.

    Innexus::Processor proc;
    REQUIRE(proc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    proc.setupProcessing(setup);
    proc.setActive(true);
    setSidechainMode(proc);

    constexpr int32 blockSize = kBlock;
    std::vector<float> outL(blockSize), outR(blockSize);
    std::vector<float> scSignal(blockSize);

    // Warmup: feed 440Hz for ~500ms
    constexpr size_t warmupBlocks = 44;
    for (size_t b = 0; b < warmupBlocks; ++b)
    {
        generateSineWave(scSignal.data(), blockSize, 440.0f, kSR, 0.8f,
                         b * static_cast<size_t>(blockSize));
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  scSignal.data(), scSignal.data(), blockSize);
    }

    proc.onNoteOn(60, 1.0f);

    // Steady state: ~200ms
    constexpr size_t steadyBlocks = 18;
    for (size_t b = 0; b < steadyBlocks; ++b)
    {
        size_t sampleOffset = (warmupBlocks + b) * static_cast<size_t>(blockSize);
        generateSineWave(scSignal.data(), blockSize, 440.0f, kSR, 0.8f, sampleOffset);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  scSignal.data(), scSignal.data(), blockSize);
    }

    float steadyRMS = computeRMS(outL.data(), blockSize);
    INFO("Steady-state RMS: " << steadyRMS);
    REQUIRE(steadyRMS > 0.001f);

    // Gradual fade: reduce amplitude over ~500ms (44 blocks)
    constexpr size_t fadeBlocks = 44;
    std::vector<float> fadeRMSValues(fadeBlocks);

    for (size_t b = 0; b < fadeBlocks; ++b)
    {
        float fadeGain = 1.0f - static_cast<float>(b) / static_cast<float>(fadeBlocks);
        size_t sampleOffset = (warmupBlocks + steadyBlocks + b) * static_cast<size_t>(blockSize);
        generateSineWave(scSignal.data(), blockSize, 440.0f, kSR,
                         0.8f * fadeGain, sampleOffset);

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  scSignal.data(), scSignal.data(), blockSize);
        fadeRMSValues[b] = computeRMS(outL.data(), blockSize);
    }

    // Post-fade silence
    constexpr size_t postFadeBlocks = 20;
    std::vector<float> silence(blockSize, 0.0f);
    std::vector<float> postFadeRMS(postFadeBlocks);

    for (size_t b = 0; b < postFadeBlocks; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  silence.data(), silence.data(), blockSize);
        postFadeRMS[b] = computeRMS(outL.data(), blockSize);
    }

    INFO("=== Output RMS during fade-out ===");
    for (size_t b = 0; b < fadeBlocks; ++b)
    {
        float dbFS = fadeRMSValues[b] > 1e-10f
            ? 20.0f * std::log10(fadeRMSValues[b]) : -200.0f;
        INFO("Fade block " << b << " (gain=" << (1.0f - float(b)/float(fadeBlocks))
             << "): RMS=" << fadeRMSValues[b] << " (" << dbFS << " dBFS)");
    }

    INFO("=== Output RMS after fade completes (silence input) ===");
    for (size_t b = 0; b < postFadeBlocks; ++b)
    {
        float dbFS = postFadeRMS[b] > 1e-10f
            ? 20.0f * std::log10(postFadeRMS[b]) : -200.0f;
        INFO("Post-fade block " << b << ": RMS=" << postFadeRMS[b]
             << " (" << dbFS << " dBFS)");
    }

    // Count how many fade blocks still have audible output
    int audibleFadeBlocks = 0;
    for (size_t b = 0; b < fadeBlocks; ++b)
    {
        if (fadeRMSValues[b] > 0.001f) // above -60 dBFS
            ++audibleFadeBlocks;
    }

    INFO("Audible fade blocks: " << audibleFadeBlocks << " / " << fadeBlocks);

    // The gradual fade should track the input amplitude somewhat.
    // At minimum, output should be audible for more of the fade than the
    // abrupt-stop case.
    CHECK(audibleFadeBlocks > 5);

    proc.setActive(false);
    proc.terminate();
}

TEST_CASE("Sidechain signal end: confidence gate holds last good frame on silence",
          "[sidechain][signal-end][confidence]")
{
    // Verify that the confidence-gated freeze mechanism (FR-052) engages
    // when sidechain goes silent, and what that sounds like.

    Innexus::Processor proc;
    REQUIRE(proc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    proc.setupProcessing(setup);
    proc.setActive(true);
    setSidechainMode(proc);

    constexpr int32 blockSize = kBlock;
    std::vector<float> outL(blockSize), outR(blockSize);
    std::vector<float> scSignal(blockSize);

    // Warmup with 440Hz
    constexpr size_t warmupBlocks = 44;
    for (size_t b = 0; b < warmupBlocks; ++b)
    {
        generateSineWave(scSignal.data(), blockSize, 440.0f, kSR, 0.8f,
                         b * static_cast<size_t>(blockSize));
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  scSignal.data(), scSignal.data(), blockSize);
    }

    proc.onNoteOn(60, 1.0f);

    // Steady state
    constexpr size_t steadyBlocks = 18;
    for (size_t b = 0; b < steadyBlocks; ++b)
    {
        size_t offset = (warmupBlocks + b) * static_cast<size_t>(blockSize);
        generateSineWave(scSignal.data(), blockSize, 440.0f, kSR, 0.8f, offset);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  scSignal.data(), scSignal.data(), blockSize);
    }

    float steadyRMS = computeRMS(outL.data(), blockSize);
    REQUIRE(steadyRMS > 0.001f);

    // Send silence - track decay over ~6 seconds (fundamental tau=0.6s,
    // need ~8.5 tau for -80dB → ~5.1s, plus initial plateau)
    constexpr size_t silenceBlocks = 520; // ~6 seconds
    std::vector<float> silence(blockSize, 0.0f);

    std::vector<float> rmsHistory(silenceBlocks);
    for (size_t b = 0; b < silenceBlocks; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  silence.data(), silence.data(), blockSize);
        rmsHistory[b] = computeRMS(outL.data(), blockSize);
    }

    // Classify blocks into phases
    int frozenBlocks = 0;   // blocks where RMS is within ~6dB of steady state
    int silentBlocks = 0;   // blocks below -60 dBFS
    int decayingBlocks = 0; // blocks between frozen and silent

    for (size_t b = 0; b < silenceBlocks; ++b)
    {
        if (rmsHistory[b] < 0.001f) // < -60 dBFS
            ++silentBlocks;
        else if (rmsHistory[b] > steadyRMS * 0.5f) // within ~6dB
            ++frozenBlocks;
        else
            ++decayingBlocks;
    }

    INFO("After sidechain stops (over ~3 seconds):");
    INFO("  Frozen blocks (within 6dB of steady): " << frozenBlocks);
    INFO("  Decaying blocks: " << decayingBlocks);
    INFO("  Silent blocks (< -60 dBFS): " << silentBlocks);

    // With spectral decay, we expect:
    // - Some frozen blocks (initial plateau before confidence gate triggers)
    // - Significant decaying blocks (the spectral fade-out)
    // - Eventually silent blocks (all partials below threshold)
    CHECK(decayingBlocks > 10);  // meaningful decay period
    CHECK(silentBlocks > 0);     // output eventually reaches silence

    proc.setActive(false);
    proc.terminate();
}
