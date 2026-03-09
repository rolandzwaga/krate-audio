// ==============================================================================
// Sidechain WAV Quality Integration Tests
// ==============================================================================
// End-to-end tests that load real WAV files as sidechain input and process
// them through the full analysis/resynthesis pipeline with specific preset
// settings. Diagnoses spectral quality issues with real-world audio.
// ==============================================================================

#include "dr_wav.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/live_analysis_pipeline.h"

#include <krate/dsp/processors/harmonic_types.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

static constexpr double kTestSR = 44100.0;
static constexpr int32 kBlockSize = 512;

// =============================================================================
// WAV Loader
// =============================================================================

struct WavData {
    std::vector<float> mono;   // Mono-downmixed samples
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint64_t totalFrames = 0;
    bool valid = false;
};

static WavData loadWav(const char* path)
{
    WavData result;
    drwav wav;
    if (!drwav_init_file(&wav, path, nullptr))
        return result;

    result.sampleRate = wav.sampleRate;
    result.channels = wav.channels;
    result.totalFrames = wav.totalPCMFrameCount;

    // Read interleaved float samples
    auto totalSamples = result.totalFrames * result.channels;
    std::vector<float> interleaved(totalSamples);
    auto framesRead = drwav_read_pcm_frames_f32(&wav, result.totalFrames,
                                                  interleaved.data());
    drwav_uninit(&wav);

    if (framesRead == 0)
        return result;

    // Downmix to mono
    result.mono.resize(framesRead);
    if (result.channels == 1)
    {
        result.mono.assign(interleaved.begin(),
                           interleaved.begin() + static_cast<ptrdiff_t>(framesRead));
    }
    else
    {
        for (uint64_t i = 0; i < framesRead; ++i)
        {
            float sum = 0.0f;
            for (uint32_t ch = 0; ch < result.channels; ++ch)
                sum += interleaved[i * result.channels + ch];
            result.mono[i] = sum / static_cast<float>(result.channels);
        }
    }

    result.valid = true;
    return result;
}

// =============================================================================
// IBStream implementation for state loading
// =============================================================================

class WavTestStream : public IBStream
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

// =============================================================================
// Helpers
// =============================================================================

static ProcessSetup makeSetup(double sampleRate = kTestSR)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kBlockSize;
    setup.sampleRate = sampleRate;
    return setup;
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

/// Write a full v8 state matching the Ethereal Soprano preset in sidechain mode
static void loadEtherealSopranoSidechainState(Innexus::Processor& proc)
{
    WavTestStream stateStream;
    IBStreamer streamer(&stateStream, kLittleEndian);

    // Version 1
    streamer.writeInt32(1);

    // M1 parameters
    streamer.writeFloat(400.0f);  // releaseTimeMs (Ethereal Soprano)
    streamer.writeFloat(1.0f);    // inharmonicityAmount (default)
    streamer.writeFloat(0.5f);    // masterGain (default)
    streamer.writeFloat(0.0f);    // bypass

    // File path (empty)
    streamer.writeInt32(0);

    // M2 parameters (plain values)
    streamer.writeFloat(1.8f);    // harmonicLevelPlain
    streamer.writeFloat(0.2f);    // residualLevelPlain
    streamer.writeFloat(0.6f);    // brightnessPlain
    streamer.writeFloat(0.0f);    // transientEmphasisPlain

    // M2 residual frames
    streamer.writeInt32(0);       // residualFrameCount
    streamer.writeInt32(0);       // analysisFFTSize
    streamer.writeInt32(0);       // analysisHopSize

    // M3 parameters
    streamer.writeInt32(1);       // inputSource = Sidechain
    streamer.writeInt32(0);       // latencyMode = LowLatency

    // M4 parameters
    streamer.writeInt8(0);        // freeze = off
    streamer.writeFloat(0.0f);    // morphPosition
    streamer.writeInt32(0);       // harmonicFilterType = AllPass
    streamer.writeFloat(0.3f);    // responsiveness

    // M5 parameters
    streamer.writeInt32(0);       // selectedSlot
    for (int s = 0; s < 8; ++s)
        streamer.writeInt8(0);    // 8 empty memory slots

    // M6 parameters
    streamer.writeFloat(1.0f);    // timbralBlend
    streamer.writeFloat(0.6f);    // stereoSpread
    streamer.writeFloat(0.0f);    // evolutionEnable
    streamer.writeFloat(0.0f);    // evolutionSpeed
    streamer.writeFloat(0.5f);    // evolutionDepth
    streamer.writeFloat(0.0f);    // evolutionMode
    streamer.writeFloat(1.0f);    // mod1Enable
    streamer.writeFloat(0.25f);   // mod1Waveform = Tri
    // mod1Rate: normalized from 0.3 Hz -> (0.3 - 0.01) / (20 - 0.01) = 0.0145
    streamer.writeFloat(0.0145f); // mod1Rate
    streamer.writeFloat(0.15f);   // mod1Depth
    streamer.writeFloat(0.0f);    // mod1RangeStart
    streamer.writeFloat(1.0f);    // mod1RangeEnd
    streamer.writeFloat(1.0f);    // mod1Target = Pan
    streamer.writeFloat(0.0f);    // mod2Enable
    streamer.writeFloat(0.0f);    // mod2Waveform
    streamer.writeFloat(0.0f);    // mod2Rate
    streamer.writeFloat(0.0f);    // mod2Depth
    streamer.writeFloat(0.0f);    // mod2RangeStart
    streamer.writeFloat(1.0f);    // mod2RangeEnd
    streamer.writeFloat(0.0f);    // mod2Target
    streamer.writeFloat(0.15f);   // detuneSpread
    streamer.writeFloat(0.0f);    // blendEnable
    for (int i = 0; i < 8; ++i)
        streamer.writeFloat(0.0f); // blendSlotWeights
    streamer.writeFloat(0.0f);    // blendLiveWeight

    // Spec A: Harmonic Physics
    streamer.writeFloat(0.5f);    // warmth
    streamer.writeFloat(0.0f);    // coupling
    streamer.writeFloat(0.0f);    // stability
    streamer.writeFloat(0.0f);    // entropy

    // Spec B: Analysis Feedback Loop
    streamer.writeFloat(0.0f);    // feedbackAmount
    streamer.writeFloat(0.2f);    // feedbackDecay

    // ADSR global defaults
    streamer.writeFloat(10.0f); streamer.writeFloat(100.0f); streamer.writeFloat(1.0f);
    streamer.writeFloat(100.0f); streamer.writeFloat(0.0f); streamer.writeFloat(1.0f);
    streamer.writeFloat(0.0f); streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
    // ADSR per-slot defaults (8 × 9)
    for (int i = 0; i < 8; ++i) {
        streamer.writeFloat(10.0f); streamer.writeFloat(100.0f); streamer.writeFloat(1.0f);
        streamer.writeFloat(100.0f); streamer.writeFloat(0.0f); streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
    }
    // Partial count
    streamer.writeFloat(0.0f);

    stateStream.resetReadPos();
    proc.setState(&stateStream);
}

static float computeRMS(const float* buffer, size_t numSamples)
{
    if (numSamples == 0) return 0.0f;
    float sumSq = 0.0f;
    for (size_t i = 0; i < numSamples; ++i)
        sumSq += buffer[i] * buffer[i];
    return std::sqrt(sumSq / static_cast<float>(numSamples));
}

static float toDB(float linear)
{
    return linear > 1e-10f ? 20.0f * std::log10(linear) : -200.0f;
}

// =============================================================================
// Test: Ethereal Soprano preset with synth-flute-vocals WAV via sidechain
// =============================================================================

TEST_CASE("Sidechain WAV: Ethereal Soprano + synth-flute-vocals quality diagnostic",
          "[sidechain][wav][quality]")
{
    const char* wavPath =
        "C:\\test\\synth-flute-vocals-ethereal-loop_83bpm_D#_minor.wav";

    auto wav = loadWav(wavPath);
    if (!wav.valid)
    {
        WARN("Skipping test: WAV file not found at " << wavPath);
        return;
    }

    INFO("WAV loaded: " << wav.totalFrames << " frames, "
         << wav.sampleRate << " Hz, " << wav.channels << " channels");

    Innexus::Processor proc;
    REQUIRE(proc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup(static_cast<double>(wav.sampleRate));
    proc.setupProcessing(setup);
    proc.setActive(true);
    loadEtherealSopranoSidechainState(proc);

    std::vector<float> outL(kBlockSize), outR(kBlockSize);

    // Phase 1: Feed the full WAV as sidechain warmup (no MIDI yet)
    // to let the analysis pipeline lock onto the signal
    size_t totalSamples = wav.mono.size();
    size_t warmupSamples = std::min(totalSamples, static_cast<size_t>(kTestSR)); // 1 second
    size_t warmupBlocks = warmupSamples / kBlockSize;

    for (size_t b = 0; b < warmupBlocks; ++b)
    {
        size_t offset = b * kBlockSize;
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  wav.mono.data() + offset,
                                  wav.mono.data() + offset,
                                  kBlockSize);
    }

    // Phase 2: Trigger MIDI note and continue feeding sidechain
    proc.onNoteOn(63, 0.8f); // D#4 to match D# minor key

    // Process the rest of the WAV (or loop it)
    size_t synthBlocks = 200; // ~2.3 seconds of synthesis
    std::vector<float> blockRMS(synthBlocks);
    bool anyNaN = false;
    bool anyInf = false;
    float maxPeak = 0.0f;
    float minRMS = 1.0f;
    float maxRMS = 0.0f;

    for (size_t b = 0; b < synthBlocks; ++b)
    {
        // Loop the WAV data for continuous feeding
        size_t wavOffset = ((warmupBlocks + b) * kBlockSize) % totalSamples;
        size_t remaining = totalSamples - wavOffset;

        // Handle wrap-around
        std::vector<float> scBlock(kBlockSize);
        if (remaining >= static_cast<size_t>(kBlockSize))
        {
            std::memcpy(scBlock.data(), wav.mono.data() + wavOffset,
                        kBlockSize * sizeof(float));
        }
        else
        {
            std::memcpy(scBlock.data(), wav.mono.data() + wavOffset,
                        remaining * sizeof(float));
            std::memcpy(scBlock.data() + remaining, wav.mono.data(),
                        (kBlockSize - remaining) * sizeof(float));
        }

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  scBlock.data(), scBlock.data(),
                                  kBlockSize);

        // Analyze output
        float rms = computeRMS(outL.data(), kBlockSize);
        blockRMS[b] = rms;
        if (rms < minRMS) minRMS = rms;
        if (rms > maxRMS) maxRMS = rms;

        for (int s = 0; s < kBlockSize; ++s)
        {
            if (std::isnan(outL[static_cast<size_t>(s)]) ||
                std::isnan(outR[static_cast<size_t>(s)]))
                anyNaN = true;
            if (std::isinf(outL[static_cast<size_t>(s)]) ||
                std::isinf(outR[static_cast<size_t>(s)]))
                anyInf = true;
            float peak = std::max(std::abs(outL[static_cast<size_t>(s)]),
                                  std::abs(outR[static_cast<size_t>(s)]));
            if (peak > maxPeak) maxPeak = peak;
        }
    }

    INFO("=== Synthesis Output Diagnostics ===");
    INFO("Max peak: " << maxPeak << " (" << toDB(maxPeak) << " dBFS)");
    INFO("Min block RMS: " << minRMS << " (" << toDB(minRMS) << " dBFS)");
    INFO("Max block RMS: " << maxRMS << " (" << toDB(maxRMS) << " dBFS)");
    INFO("Dynamic range: " << (toDB(maxRMS) - toDB(minRMS)) << " dB");

    // Log first 20 block RMS values
    INFO("--- Per-block RMS (first 20) ---");
    for (size_t b = 0; b < std::min(synthBlocks, size_t{20}); ++b)
        INFO("Block " << b << ": " << toDB(blockRMS[b]) << " dBFS");

    // Basic sanity checks
    CHECK_FALSE(anyNaN);
    CHECK_FALSE(anyInf);
    CHECK(maxPeak > 0.0f);    // We should produce some output
    CHECK(maxPeak <= 1.0f);   // Soft limiter should prevent clipping

    // Count blocks with audible output
    int audibleBlocks = 0;
    for (size_t b = 0; b < synthBlocks; ++b)
    {
        if (blockRMS[b] > 0.001f) // > -60 dBFS
            ++audibleBlocks;
    }
    INFO("Audible blocks: " << audibleBlocks << " / " << synthBlocks);
    CHECK(audibleBlocks > static_cast<int>(synthBlocks * 0.5));

    proc.setActive(false);
    proc.terminate();
}

// =============================================================================
// Test: Female vocal single note via sidechain
// =============================================================================

TEST_CASE("Sidechain WAV: Female vocal single note quality diagnostic",
          "[sidechain][wav][quality]")
{
    const char* wavPath =
        "C:\\test\\508325__owstu__female-vocal-single-note-a-x.wav";

    auto wav = loadWav(wavPath);
    if (!wav.valid)
    {
        WARN("Skipping test: WAV file not found at " << wavPath);
        return;
    }

    INFO("WAV loaded: " << wav.totalFrames << " frames, "
         << wav.sampleRate << " Hz, " << wav.channels << " channels");

    Innexus::Processor proc;
    REQUIRE(proc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup(static_cast<double>(wav.sampleRate));
    proc.setupProcessing(setup);
    proc.setActive(true);
    loadEtherealSopranoSidechainState(proc);

    std::vector<float> outL(kBlockSize), outR(kBlockSize);
    size_t totalSamples = wav.mono.size();

    // Phase 1: Warmup (~500ms)
    size_t warmupBlocks = std::min(totalSamples / kBlockSize,
                                    static_cast<size_t>(44));
    for (size_t b = 0; b < warmupBlocks; ++b)
    {
        size_t offset = b * kBlockSize;
        if (offset + kBlockSize > totalSamples) break;
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  wav.mono.data() + offset,
                                  wav.mono.data() + offset,
                                  kBlockSize);
    }

    // Phase 2: Trigger MIDI A4 (matches the vocal note)
    proc.onNoteOn(69, 0.8f); // A4 = MIDI 69

    // Phase 3: Continue feeding the rest of the WAV
    size_t remainingBlocks = (totalSamples - warmupBlocks * kBlockSize) / kBlockSize;
    remainingBlocks = std::min(remainingBlocks, size_t{200});

    std::vector<float> blockRMS(remainingBlocks);
    float maxPeak = 0.0f;
    bool anyNaN = false;

    for (size_t b = 0; b < remainingBlocks; ++b)
    {
        size_t offset = (warmupBlocks + b) * kBlockSize;
        if (offset + kBlockSize > totalSamples)
        {
            remainingBlocks = b;
            blockRMS.resize(b);
            break;
        }

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  wav.mono.data() + offset,
                                  wav.mono.data() + offset,
                                  kBlockSize);

        float rms = computeRMS(outL.data(), kBlockSize);
        blockRMS[b] = rms;

        for (int s = 0; s < kBlockSize; ++s)
        {
            if (std::isnan(outL[static_cast<size_t>(s)])) anyNaN = true;
            float peak = std::max(std::abs(outL[static_cast<size_t>(s)]),
                                  std::abs(outR[static_cast<size_t>(s)]));
            if (peak > maxPeak) maxPeak = peak;
        }
    }

    // Phase 4: Feed silence (note still held) - test the decay
    size_t silenceBlocks = 120; // ~1.4 seconds
    std::vector<float> silence(kBlockSize, 0.0f);
    std::vector<float> silenceRMS(silenceBlocks);

    for (size_t b = 0; b < silenceBlocks; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlockWithSidechain(proc, outL.data(), outR.data(),
                                  silence.data(), silence.data(),
                                  kBlockSize);
        silenceRMS[b] = computeRMS(outL.data(), kBlockSize);
    }

    INFO("=== During signal ===");
    INFO("Max peak: " << maxPeak << " (" << toDB(maxPeak) << " dBFS)");
    for (size_t b = 0; b < std::min(remainingBlocks, size_t{20}); ++b)
        INFO("Block " << b << ": " << toDB(blockRMS[b]) << " dBFS");

    INFO("=== After signal ends (silence input, note held) ===");
    for (size_t b = 0; b < std::min(silenceBlocks, size_t{20}); ++b)
        INFO("Silence block " << b << ": " << toDB(silenceRMS[b]) << " dBFS");

    // Check for monotonic decay during silence
    int decreasingBlocks = 0;
    for (size_t b = 1; b < silenceBlocks; ++b)
    {
        if (silenceRMS[b] <= silenceRMS[b - 1] * 1.05f)
            ++decreasingBlocks;
    }
    INFO("Monotonically decreasing silence blocks: " << decreasingBlocks
         << " / " << (silenceBlocks - 1));

    CHECK_FALSE(anyNaN);
    CHECK(maxPeak > 0.0f);
    CHECK(decreasingBlocks > static_cast<int>(silenceBlocks * 0.7));

    proc.setActive(false);
    proc.terminate();
}
