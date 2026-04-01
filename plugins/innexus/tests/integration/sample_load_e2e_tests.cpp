// ==============================================================================
// Innexus End-to-End Sample Load → Analysis → Resynthesis Tests
// ==============================================================================
// Verifies the full pipeline: WAV file on disk → SampleAnalyzer → Processor →
// pitched audio output. This catches bugs that unit tests miss because they
// inject synthetic SampleAnalysis data via testInjectAnalysis().
//
// Tests:
//   1. SampleAnalyzer produces valid harmonic frames from a sine-wave WAV
//   2. Resynthesized output contains energy at the expected frequency
//   3. Different MIDI notes produce different pitches (tuning)
//   4. Multi-harmonic sample analysis preserves harmonic structure
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"
#include "dsp/sample_analyzer.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/core/midi_utils.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

// =============================================================================
// WAV File Writer (minimal, 16-bit PCM mono)
// =============================================================================

struct WavHeader {
    char riff[4]       = {'R','I','F','F'};
    uint32_t fileSize  = 0;
    char wave[4]       = {'W','A','V','E'};
    char fmt[4]        = {'f','m','t',' '};
    uint32_t fmtSize   = 16;
    uint16_t audioFmt  = 1; // PCM
    uint16_t channels  = 1;
    uint32_t sampleRate = 44100;
    uint32_t byteRate  = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 16;
    char data[4]       = {'d','a','t','a'};
    uint32_t dataSize  = 0;
};

static std::string writeSineWav(float frequency, float durationSec,
                                float amplitude = 0.8f,
                                uint32_t sampleRate = 44100)
{
    namespace fs = std::filesystem;

    auto tempDir = fs::temp_directory_path() / "innexus_tests";
    fs::create_directories(tempDir);

    char filename[128];
    std::snprintf(filename, sizeof(filename), "sine_%.0fHz.wav", frequency);
    auto path = tempDir / filename;

    const auto numSamples = static_cast<size_t>(
        static_cast<float>(sampleRate) * durationSec);

    std::vector<int16_t> samples(numSamples);
    const float twoPiF = 2.0f * 3.14159265358979f * frequency;
    for (size_t i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float val = amplitude * std::sin(twoPiF * t);
        // Clamp and convert to int16
        samples[i] = static_cast<int16_t>(val * 32767.0f);
    }

    WavHeader hdr;
    hdr.sampleRate = sampleRate;
    hdr.channels = 1;
    hdr.bitsPerSample = 16;
    hdr.blockAlign = hdr.channels * (hdr.bitsPerSample / 8);
    hdr.byteRate = hdr.sampleRate * hdr.blockAlign;
    hdr.dataSize = static_cast<uint32_t>(numSamples * sizeof(int16_t));
    hdr.fileSize = 36 + hdr.dataSize;

    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    ofs.write(reinterpret_cast<const char*>(samples.data()),
              static_cast<std::streamsize>(numSamples * sizeof(int16_t)));
    ofs.close();

    return path.string();
}

/// Write a WAV containing multiple harmonics (fundamental + overtones)
static std::string writeHarmonicWav(float f0, int numHarmonics,
                                     float durationSec,
                                     float amplitude = 0.5f,
                                     uint32_t sampleRate = 44100)
{
    namespace fs = std::filesystem;

    auto tempDir = fs::temp_directory_path() / "innexus_tests";
    fs::create_directories(tempDir);

    char filename[128];
    std::snprintf(filename, sizeof(filename), "harmonic_%.0fHz_%dh.wav",
                  f0, numHarmonics);
    auto path = tempDir / filename;

    const auto numSamples = static_cast<size_t>(
        static_cast<float>(sampleRate) * durationSec);

    std::vector<int16_t> samples(numSamples);
    for (size_t i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float val = 0.0f;
        for (int h = 1; h <= numHarmonics; ++h)
        {
            float hAmp = amplitude / static_cast<float>(h); // 1/n rolloff
            val += hAmp * std::sin(2.0f * 3.14159265358979f * f0 *
                                   static_cast<float>(h) * t);
        }
        // Clamp
        if (val > 1.0f) val = 1.0f;
        if (val < -1.0f) val = -1.0f;
        samples[i] = static_cast<int16_t>(val * 32767.0f);
    }

    WavHeader hdr;
    hdr.sampleRate = sampleRate;
    hdr.channels = 1;
    hdr.bitsPerSample = 16;
    hdr.blockAlign = hdr.channels * (hdr.bitsPerSample / 8);
    hdr.byteRate = hdr.sampleRate * hdr.blockAlign;
    hdr.dataSize = static_cast<uint32_t>(numSamples * sizeof(int16_t));
    hdr.fileSize = 36 + hdr.dataSize;

    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    ofs.write(reinterpret_cast<const char*>(samples.data()),
              static_cast<std::streamsize>(numSamples * sizeof(int16_t)));
    ofs.close();

    return path.string();
}

// =============================================================================
// Goertzel Algorithm — detect energy at a specific frequency
// =============================================================================

/// Returns the magnitude (linear) at the target frequency.
static float goertzelMagnitude(const float* signal, size_t length,
                               float targetFreq, float sampleRate)
{
    const float k = targetFreq * static_cast<float>(length) / sampleRate;
    const float omega = 2.0f * 3.14159265358979f * k / static_cast<float>(length);
    const float coeff = 2.0f * std::cos(omega);

    float s0 = 0.0f;
    float s1 = 0.0f;
    float s2 = 0.0f;

    for (size_t i = 0; i < length; ++i)
    {
        s0 = signal[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return std::sqrt(std::max(power, 0.0f)) / static_cast<float>(length);
}

/// Find the dominant frequency in a signal by scanning a frequency range.
static float findDominantFrequency(const float* signal, size_t length,
                                    float sampleRate,
                                    float minFreq = 60.0f,
                                    float maxFreq = 2000.0f,
                                    float step = 2.0f)
{
    float bestFreq = 0.0f;
    float bestMag = 0.0f;

    for (float f = minFreq; f <= maxFreq; f += step)
    {
        float mag = goertzelMagnitude(signal, length, f, sampleRate);
        if (mag > bestMag)
        {
            bestMag = mag;
            bestFreq = f;
        }
    }

    return bestFreq;
}

// =============================================================================
// Minimal IEventList for MIDI events
// =============================================================================

class TestEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override
    {
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getEventCount() override
    {
        return static_cast<int32>(events_.size());
    }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size()))
            return kInvalidArgument;
        e = events_[static_cast<size_t>(index)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override
    {
        events_.push_back(e);
        return kResultOk;
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
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }

    void addNoteOff(int16 pitch, int32 sampleOffset = 0)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = pitch;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

// =============================================================================
// Minimal parameter change infrastructure for E2E tests
// =============================================================================

class E2EParamValueQueue : public IParamValueQueue {
public:
    E2EParamValueQueue(ParamID id, ParamValue val) : id_(id), value_(val) {}
    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32, int32& sampleOffset, ParamValue& value) override {
        sampleOffset = 0; value = value_; return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
private:
    ParamID id_; ParamValue value_;
};

class E2EParameterChanges : public IParameterChanges {
public:
    void addChange(ParamID id, ParamValue val) { queues_.emplace_back(id, val); }
    int32 PLUGIN_API getParameterCount() override { return static_cast<int32>(queues_.size()); }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
        if (index < 0 || index >= static_cast<int32>(queues_.size())) return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    void clear() { queues_.clear(); }
private:
    std::vector<E2EParamValueQueue> queues_;
};

// =============================================================================
// Test Fixture: Processor + audio buffers
// =============================================================================

struct E2EFixture {
    Innexus::Processor processor;
    TestEventList events;
    E2EParameterChanges paramChanges;
    float outL[4096] = {};
    float outR[4096] = {};

    static constexpr double sampleRate = 44100.0;
    static constexpr int32 blockSize = 512;

    E2EFixture()
    {
        processor.initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = blockSize;
        setup.sampleRate = sampleRate;
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~E2EFixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    /// Process one block and return the output.
    void processBlock()
    {
        ProcessData data{};
        data.symbolicSampleSize = kSample32;
        data.numSamples = blockSize;
        data.numInputs = 0;
        data.inputs = nullptr;

        AudioBusBuffers outBus{};
        outBus.numChannels = 2;
        float* outs[2] = {outL, outR};
        outBus.channelBuffers32 = outs;
        data.numOutputs = 1;
        data.outputs = &outBus;

        data.inputEvents = &events;
        data.outputEvents = nullptr;
        // Apply pending parameter changes if any
        data.inputParameterChanges = paramChanges.getParameterCount() > 0
            ? &paramChanges : nullptr;
        data.outputParameterChanges = nullptr;

        std::memset(outL, 0, sizeof(float) * static_cast<size_t>(blockSize));
        std::memset(outR, 0, sizeof(float) * static_cast<size_t>(blockSize));

        processor.process(data);
        events.clear();
        paramChanges.clear();
    }

    /// Process N blocks and accumulate output into a buffer.
    std::vector<float> processBlocks(int numBlocks)
    {
        std::vector<float> accumulated;
        accumulated.reserve(static_cast<size_t>(numBlocks * blockSize));
        for (int i = 0; i < numBlocks; ++i)
        {
            processBlock();
            for (int s = 0; s < blockSize; ++s)
                accumulated.push_back(outL[s]);
        }
        return accumulated;
    }

    float getMaxAmplitude()
    {
        float maxAmp = 0.0f;
        for (int s = 0; s < blockSize; ++s)
        {
            float a = std::abs(outL[s]);
            if (a > maxAmp) maxAmp = a;
        }
        return maxAmp;
    }
};

} // anonymous namespace

// =============================================================================
// TEST 1: SampleAnalyzer produces valid harmonic frames from a sine WAV
// =============================================================================

TEST_CASE("E2E: SampleAnalyzer extracts f0 from sine WAV",
          "[innexus][e2e][analyzer]")
{
    constexpr float kSineFreq = 440.0f;
    constexpr float kDuration = 1.0f; // 1 second

    // Step 1: Write a WAV file with a pure 440 Hz sine
    std::string wavPath = writeSineWav(kSineFreq, kDuration);
    REQUIRE(!wavPath.empty());
    REQUIRE(std::filesystem::exists(wavPath));

    // Step 2: Run the SampleAnalyzer
    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(wavPath);

    // Wait for analysis (should take <1s for a 1s file)
    for (int i = 0; i < 100; ++i)
    {
        if (analyzer.isComplete()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE(analyzer.isComplete());

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);

    // Step 3: Verify the analysis
    INFO("Total frames: " << result->totalFrames);
    REQUIRE(result->totalFrames > 0);
    REQUIRE(result->sampleRate == Approx(44100.0f));
    REQUIRE(result->hopTimeSec > 0.0f);

    // Check that most frames have a reasonable f0 near 440 Hz
    int framesWithGoodF0 = 0;
    for (size_t i = 0; i < result->frames.size(); ++i)
    {
        const auto& frame = result->frames[i];
        if (frame.f0Confidence > 0.5f &&
            frame.f0 > 400.0f && frame.f0 < 480.0f)
        {
            ++framesWithGoodF0;
        }
    }

    float goodRatio = static_cast<float>(framesWithGoodF0) /
                      static_cast<float>(result->frames.size());
    INFO("Frames with f0 near 440Hz: " << framesWithGoodF0
         << "/" << result->frames.size()
         << " (" << (goodRatio * 100.0f) << "%)");
    // At least 50% of frames should detect f0 correctly
    // (first/last frames may have onset/offset artifacts)
    REQUIRE(goodRatio > 0.5f);

    // Check that frames have at least 1 partial
    int framesWithPartials = 0;
    for (const auto& frame : result->frames)
    {
        if (frame.numPartials > 0 && frame.globalAmplitude > 0.001f)
            ++framesWithPartials;
    }
    INFO("Frames with partials: " << framesWithPartials);
    REQUIRE(framesWithPartials > 0);

    // Cleanup
    std::filesystem::remove(wavPath);
}

// =============================================================================
// TEST 2: Full pipeline — sine WAV → analyze → note-on → pitched output
// =============================================================================

TEST_CASE("E2E: Sine WAV produces pitched output matching MIDI note",
          "[innexus][e2e][resynthesis]")
{
    constexpr float kSineFreq = 440.0f;
    constexpr float kDuration = 1.0f;

    // Step 1: Generate and analyze a 440 Hz sine WAV
    std::string wavPath = writeSineWav(kSineFreq, kDuration);
    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(wavPath);

    for (int i = 0; i < 100; ++i)
    {
        if (analyzer.isComplete()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE(analyzer.isComplete());

    auto analysis = analyzer.takeResult();
    REQUIRE(analysis != nullptr);
    REQUIRE(analysis->totalFrames > 0);

    // Step 2: Inject analysis into processor
    E2EFixture fix;
    fix.processor.testInjectAnalysis(analysis.release());

    // Step 3: Send MIDI note-on for A4 (note 69 = 440 Hz)
    fix.events.addNoteOn(69, 1.0f);

    // Process enough blocks for oscillators to settle (~50ms = ~4 blocks at 512)
    auto warmup = fix.processBlocks(4);

    // Process more blocks to capture steady-state output
    auto output = fix.processBlocks(16);

    // Step 4: Verify output has energy
    float maxAmp = *std::max_element(output.begin(), output.end(),
        [](float a, float b) { return std::abs(a) < std::abs(b); });
    maxAmp = std::abs(maxAmp);
    INFO("Max amplitude: " << maxAmp);
    REQUIRE(maxAmp > 0.01f); // Should have audible output

    // Step 5: Verify the dominant frequency is near 440 Hz
    float dominant = findDominantFrequency(
        output.data(), output.size(),
        static_cast<float>(E2EFixture::sampleRate));
    INFO("Dominant frequency: " << dominant << " Hz (expected ~440 Hz)");
    REQUIRE(dominant > 400.0f);
    REQUIRE(dominant < 480.0f);

    std::filesystem::remove(wavPath);
}

// =============================================================================
// TEST 3: Different MIDI notes produce different pitches (tuning)
// =============================================================================

TEST_CASE("E2E: Different MIDI notes produce different pitches",
          "[innexus][e2e][tuning]")
{
    constexpr float kSineFreq = 440.0f;
    constexpr float kDuration = 1.0f;

    // Analyze a 440 Hz sine WAV
    std::string wavPath = writeSineWav(kSineFreq, kDuration);
    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(wavPath);

    for (int i = 0; i < 100; ++i)
    {
        if (analyzer.isComplete()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE(analyzer.isComplete());

    auto analysis = analyzer.takeResult();
    REQUIRE(analysis != nullptr);

    // We need to duplicate the analysis for two separate tests
    // since testInjectAnalysis takes ownership
    auto analysis2 = std::make_unique<Innexus::SampleAnalysis>(*analysis);

    // --- Test with A4 (note 69 = 440 Hz) ---
    // Disable residual so noise shaped at the analysis frequency (440 Hz)
    // doesn't interfere with pitch detection of transposed harmonics.
    float freqA4 = 0.0f;
    {
        E2EFixture fix;
        fix.processor.testInjectAnalysis(analysis.release());
        fix.paramChanges.addChange(Innexus::kResidualLevelId, 0.0);
        fix.events.addNoteOn(69, 1.0f); // A4
        fix.processBlocks(4); // warmup
        auto output = fix.processBlocks(16);
        freqA4 = findDominantFrequency(
            output.data(), output.size(),
            static_cast<float>(E2EFixture::sampleRate));
        INFO("A4 dominant freq: " << freqA4);
        REQUIRE(freqA4 > 400.0f);
        REQUIRE(freqA4 < 480.0f);
    }

    // --- Test with A3 (note 57 = 220 Hz) ---
    float freqA3 = 0.0f;
    {
        E2EFixture fix;
        fix.processor.testInjectAnalysis(analysis2.release());
        fix.paramChanges.addChange(Innexus::kResidualLevelId, 0.0);
        fix.events.addNoteOn(57, 1.0f); // A3
        fix.processBlocks(4); // warmup
        auto output = fix.processBlocks(16);
        freqA3 = findDominantFrequency(
            output.data(), output.size(),
            static_cast<float>(E2EFixture::sampleRate),
            100.0f, 1000.0f, 1.0f);
        INFO("A3 dominant freq: " << freqA3);
        REQUIRE(freqA3 > 190.0f);
        REQUIRE(freqA3 < 250.0f);
    }

    // The ratio should be close to 2:1 (octave)
    float ratio = freqA4 / freqA3;
    INFO("Frequency ratio A4/A3: " << ratio << " (expected ~2.0)");
    REQUIRE(ratio > 1.8f);
    REQUIRE(ratio < 2.2f);

    std::filesystem::remove(wavPath);
}

// =============================================================================
// TEST 4: Multi-harmonic WAV preserves harmonic structure
// =============================================================================

TEST_CASE("E2E: Multi-harmonic WAV analysis detects multiple partials",
          "[innexus][e2e][harmonics]")
{
    constexpr float kF0 = 220.0f;
    constexpr int kNumHarmonics = 8;
    constexpr float kDuration = 1.0f;

    std::string wavPath = writeHarmonicWav(kF0, kNumHarmonics, kDuration);
    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(wavPath);

    for (int i = 0; i < 100; ++i)
    {
        if (analyzer.isComplete()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE(analyzer.isComplete());

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);
    REQUIRE(result->totalFrames > 0);

    // Find a frame with good confidence in the middle of the file
    const auto& midFrame = result->frames[result->frames.size() / 2];
    INFO("Mid frame f0: " << midFrame.f0
         << " Hz, confidence: " << midFrame.f0Confidence
         << ", numPartials: " << midFrame.numPartials);

    // Should detect at least 3 partials for an 8-harmonic signal
    REQUIRE(midFrame.numPartials >= 3);

    // f0 should be near 220 Hz
    if (midFrame.f0Confidence > 0.3f)
    {
        REQUIRE(midFrame.f0 > 180.0f);
        REQUIRE(midFrame.f0 < 260.0f);
    }

    // Check that the first few partials have decreasing amplitude
    // (matches our 1/n rolloff in the test signal)
    if (midFrame.numPartials >= 3)
    {
        bool firstIsLoudest = midFrame.partials[0].amplitude >=
                              midFrame.partials[1].amplitude;
        INFO("Partial 1 amp: " << midFrame.partials[0].amplitude
             << ", Partial 2 amp: " << midFrame.partials[1].amplitude);
        CHECK(firstIsLoudest);
    }

    std::filesystem::remove(wavPath);
}


// =============================================================================
// TEST 6: Output is NOT noise — signal has clear spectral peaks
// =============================================================================

TEST_CASE("E2E: Resynthesized output is tonal (not noise)",
          "[innexus][e2e][quality]")
{
    constexpr float kSineFreq = 440.0f;

    std::string wavPath = writeSineWav(kSineFreq, 1.0f);
    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(wavPath);

    for (int i = 0; i < 100; ++i)
    {
        if (analyzer.isComplete()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE(analyzer.isComplete());

    auto analysis = analyzer.takeResult();
    REQUIRE(analysis != nullptr);

    E2EFixture fix;
    fix.processor.testInjectAnalysis(analysis.release());
    fix.events.addNoteOn(69, 1.0f); // A4
    fix.processBlocks(4); // warmup
    auto output = fix.processBlocks(32); // ~370ms of audio

    float sr = static_cast<float>(E2EFixture::sampleRate);

    // Measure energy at the expected fundamental (440 Hz)
    float magAt440 = goertzelMagnitude(output.data(), output.size(), 440.0f, sr);

    // Measure energy at several off-frequencies (should be much lower)
    float magAt300 = goertzelMagnitude(output.data(), output.size(), 300.0f, sr);
    float magAt600 = goertzelMagnitude(output.data(), output.size(), 600.0f, sr);
    float magAt1000 = goertzelMagnitude(output.data(), output.size(), 1000.0f, sr);

    INFO("Magnitude at 440 Hz: " << magAt440);
    INFO("Magnitude at 300 Hz: " << magAt300);
    INFO("Magnitude at 600 Hz: " << magAt600);
    INFO("Magnitude at 1000 Hz: " << magAt1000);

    // For a pure sine analyzed and resynthesized, most energy should be at 440
    // The ratio of peak to off-frequency should be significant
    // (For noise, energy is roughly equal across frequencies)
    REQUIRE(magAt440 > 0.001f); // Non-trivial energy at fundamental
    REQUIRE(magAt440 > magAt300 * 2.0f); // At least 2x more than off-freq
    REQUIRE(magAt440 > magAt1000 * 2.0f);

    std::filesystem::remove(wavPath);
}
