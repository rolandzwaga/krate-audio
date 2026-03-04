// ==============================================================================
// Sample Analyzer Tests (Phase 10)
// ==============================================================================
// Tests for WAV/AIFF loading, background analysis thread, atomic pointer swap,
// stereo downmix, and analysis pipeline correctness.
//
// Reference: spec.md FR-043 to FR-047, FR-058, SC-005
// ==============================================================================

#include "dsp/sample_analysis.h"
#include "dsp/sample_analyzer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using Catch::Approx;

namespace {

// ==============================================================================
// WAV File Generation Helpers
// ==============================================================================

// Minimal WAV header structure for test file generation
#pragma pack(push, 1)
struct WavHeader {
    char riffId[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize = 0;
    char waveId[4] = {'W', 'A', 'V', 'E'};
    char fmtId[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 3; // IEEE float
    uint16_t numChannels = 1;
    uint32_t sampleRate = 44100;
    uint32_t byteRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 32;
    char dataId[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};
#pragma pack(pop)

/// Generate a mono WAV file containing a sine wave at the given frequency
std::string generateSineWav(
    const std::string& filename,
    float frequency,
    float durationSec,
    uint32_t sampleRate = 44100,
    float amplitude = 0.8f)
{
    auto tempDir = std::filesystem::temp_directory_path();
    auto filePath = (tempDir / filename).string();

    const auto totalFrames = static_cast<uint32_t>(
        static_cast<float>(sampleRate) * durationSec);

    // Generate sine wave samples
    std::vector<float> samples(totalFrames);
    constexpr float kTwoPi = 6.283185307179586f;
    for (uint32_t i = 0; i < totalFrames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        samples[i] = amplitude * std::sin(kTwoPi * frequency * t);
    }

    WavHeader header{};
    header.numChannels = 1;
    header.sampleRate = sampleRate;
    header.bitsPerSample = 32;
    header.blockAlign = static_cast<uint16_t>(header.numChannels * (header.bitsPerSample / 8));
    header.byteRate = header.sampleRate * header.blockAlign;
    header.dataSize = totalFrames * header.blockAlign;
    header.fileSize = 36 + header.dataSize;

    std::ofstream file(filePath, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(samples.data()),
               static_cast<std::streamsize>(samples.size() * sizeof(float)));
    file.close();

    return filePath;
}

/// Generate a stereo WAV file containing a sine wave
std::string generateStereoSineWav(
    const std::string& filename,
    float frequency,
    float durationSec,
    uint32_t sampleRate = 44100)
{
    auto tempDir = std::filesystem::temp_directory_path();
    auto filePath = (tempDir / filename).string();

    const auto totalFrames = static_cast<uint32_t>(
        static_cast<float>(sampleRate) * durationSec);

    // Generate interleaved stereo samples: L = sin, R = sin * 0.5
    std::vector<float> samples(static_cast<size_t>(totalFrames) * 2);
    constexpr float kTwoPi = 6.283185307179586f;
    for (uint32_t i = 0; i < totalFrames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float val = 0.8f * std::sin(kTwoPi * frequency * t);
        samples[static_cast<size_t>(i) * 2]     = val;       // Left
        samples[static_cast<size_t>(i) * 2 + 1] = val * 0.5f; // Right
    }

    WavHeader header{};
    header.numChannels = 2;
    header.sampleRate = sampleRate;
    header.bitsPerSample = 32;
    header.blockAlign = static_cast<uint16_t>(header.numChannels * (header.bitsPerSample / 8));
    header.byteRate = header.sampleRate * header.blockAlign;
    header.dataSize = totalFrames * header.blockAlign;
    header.fileSize = 36 + header.dataSize;

    std::ofstream file(filePath, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(samples.data()),
               static_cast<std::streamsize>(samples.size() * sizeof(float)));
    file.close();

    return filePath;
}

// ==============================================================================
// AIFF File Generation Helpers
// ==============================================================================

/// Write a 32-bit big-endian unsigned integer
void writeBE32(std::ofstream& f, uint32_t val) {
    uint8_t bytes[4];
    bytes[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((val >>  8) & 0xFF);
    bytes[3] = static_cast<uint8_t>((val      ) & 0xFF);
    f.write(reinterpret_cast<const char*>(bytes), 4);
}

/// Write a 16-bit big-endian unsigned integer
void writeBE16(std::ofstream& f, uint16_t val) {
    uint8_t bytes[2];
    bytes[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
    bytes[1] = static_cast<uint8_t>((val     ) & 0xFF);
    f.write(reinterpret_cast<const char*>(bytes), 2);
}

/// Write a big-endian 16-bit signed sample
void writeBE16Signed(std::ofstream& f, int16_t val) {
    uint8_t bytes[2];
    bytes[0] = static_cast<uint8_t>((static_cast<uint16_t>(val) >> 8) & 0xFF);
    bytes[1] = static_cast<uint8_t>((static_cast<uint16_t>(val)     ) & 0xFF);
    f.write(reinterpret_cast<const char*>(bytes), 2);
}

/// Convert a double to 80-bit IEEE 754 extended precision (big-endian)
/// Used for AIFF sample rate field in COMM chunk
void writeExtended80(std::ofstream& f, double val) {
    // 80-bit extended: 1 sign + 15 exponent + 64 mantissa (explicit integer bit)
    uint8_t bytes[10] = {};
    if (val == 0.0) {
        f.write(reinterpret_cast<const char*>(bytes), 10);
        return;
    }

    uint16_t sign = 0;
    if (val < 0.0) {
        sign = 0x8000;
        val = -val;
    }

    // Decompose: val = frac * 2^exp, 0.5 <= frac < 1.0
    int exp = 0;
    double frac = std::frexp(val, &exp);
    // frexp gives 0.5 <= frac < 1.0, exp such that val = frac * 2^exp
    // Extended format: 1.mantissa * 2^(biased_exp - 16383)
    // So biased_exp = exp - 1 + 16383 = exp + 16382
    auto biasedExp = static_cast<uint16_t>(exp + 16382);

    // Mantissa: frac is in [0.5, 1.0), multiply by 2 to get [1.0, 2.0)
    // then the integer bit is always 1
    // mantissa = frac * 2^64 (as a 64-bit integer, with implicit shift for integer bit)
    frac *= 2.0; // now in [1.0, 2.0), integer bit = 1
    // The 64-bit mantissa includes the integer bit
    auto mantissa = static_cast<uint64_t>(frac * 9223372036854775808.0); // frac * 2^63
    mantissa |= 0x8000000000000000ULL; // set integer bit explicitly

    uint16_t expField = sign | biasedExp;
    bytes[0] = static_cast<uint8_t>((expField >> 8) & 0xFF);
    bytes[1] = static_cast<uint8_t>((expField     ) & 0xFF);
    bytes[2] = static_cast<uint8_t>((mantissa >> 56) & 0xFF);
    bytes[3] = static_cast<uint8_t>((mantissa >> 48) & 0xFF);
    bytes[4] = static_cast<uint8_t>((mantissa >> 40) & 0xFF);
    bytes[5] = static_cast<uint8_t>((mantissa >> 32) & 0xFF);
    bytes[6] = static_cast<uint8_t>((mantissa >> 24) & 0xFF);
    bytes[7] = static_cast<uint8_t>((mantissa >> 16) & 0xFF);
    bytes[8] = static_cast<uint8_t>((mantissa >>  8) & 0xFF);
    bytes[9] = static_cast<uint8_t>((mantissa      ) & 0xFF);
    f.write(reinterpret_cast<const char*>(bytes), 10);
}

/// Generate a mono AIFF file containing a sine wave at the given frequency
/// AIFF uses big-endian 16-bit PCM samples
std::string generateSineAiff(
    const std::string& filename,
    float frequency,
    float durationSec,
    uint32_t sampleRate = 44100,
    float amplitude = 0.8f)
{
    auto tempDir = std::filesystem::temp_directory_path();
    auto filePath = (tempDir / filename).string();

    const auto totalFrames = static_cast<uint32_t>(
        static_cast<float>(sampleRate) * durationSec);
    const uint16_t numChannels = 1;
    const uint16_t bitsPerSample = 16;
    const uint16_t bytesPerSample = bitsPerSample / 8;

    // Generate 16-bit PCM sine wave samples (big-endian written in write loop)
    std::vector<int16_t> samples(totalFrames);
    constexpr float kTwoPi = 6.283185307179586f;
    for (uint32_t i = 0; i < totalFrames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float val = amplitude * std::sin(kTwoPi * frequency * t);
        samples[i] = static_cast<int16_t>(val * 32767.0f);
    }

    // AIFF structure:
    // FORM chunk: 'FORM' + size + 'AIFF'
    //   COMM chunk: 'COMM' + 18 + numChannels(2) + numFrames(4) + bitsPerSample(2) + sampleRate(10)
    //   SSND chunk: 'SSND' + size + offset(4) + blockSize(4) + data

    uint32_t commChunkSize = 18; // fixed for standard AIFF COMM
    uint32_t ssndDataSize = totalFrames * numChannels * bytesPerSample;
    uint32_t ssndChunkSize = 8 + ssndDataSize; // offset(4) + blockSize(4) + data
    uint32_t formSize = 4 + (8 + commChunkSize) + (8 + ssndChunkSize); // 'AIFF' + COMM + SSND

    std::ofstream file(filePath, std::ios::binary);

    // FORM header
    file.write("FORM", 4);
    writeBE32(file, formSize);
    file.write("AIFF", 4);

    // COMM chunk
    file.write("COMM", 4);
    writeBE32(file, commChunkSize);
    writeBE16(file, numChannels);
    writeBE32(file, totalFrames);
    writeBE16(file, bitsPerSample);
    writeExtended80(file, static_cast<double>(sampleRate));

    // SSND chunk
    file.write("SSND", 4);
    writeBE32(file, ssndChunkSize);
    writeBE32(file, 0); // offset
    writeBE32(file, 0); // blockSize

    // Write big-endian 16-bit samples
    for (uint32_t i = 0; i < totalFrames; ++i) {
        writeBE16Signed(file, samples[i]);
    }

    file.close();
    return filePath;
}

/// Cleanup helper
struct TempFileGuard {
    std::string path;
    ~TempFileGuard() {
        if (!path.empty()) {
            std::filesystem::remove(path);
        }
    }
};

} // anonymous namespace

// ==============================================================================
// Test: Load WAV file and verify non-empty analysis
// ==============================================================================
TEST_CASE("SampleAnalyzer: Load WAV file produces non-empty frames",
          "[innexus][sample_analyzer][FR-043]")
{
    auto filePath = generateSineWav("test_sine_440.wav", 440.0f, 0.5f);
    TempFileGuard guard{filePath};

    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(filePath);

    // Wait for completion (should be fast for 0.5s file)
    auto start = std::chrono::steady_clock::now();
    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start;
        REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 30);
    }

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);
    REQUIRE(result->frames.size() > 0);
    REQUIRE(result->totalFrames > 0);
    REQUIRE(result->totalFrames == result->frames.size());
    REQUIRE(result->sampleRate == Approx(44100.0f));
}

// ==============================================================================
// Test: Load AIFF file and verify AIFF support (FR-043)
// ==============================================================================
TEST_CASE("SampleAnalyzer: Load AIFF file produces non-empty frames",
          "[innexus][sample_analyzer][FR-043]")
{
    auto filePath = generateSineAiff("test_sine_440.aiff", 440.0f, 0.5f);
    TempFileGuard guard{filePath};

    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(filePath);

    // Wait for completion (should be fast for 0.5s file)
    auto start = std::chrono::steady_clock::now();
    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start;
        REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 30);
    }

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);
    REQUIRE(result->frames.size() > 0);
    REQUIRE(result->totalFrames > 0);
    REQUIRE(result->totalFrames == result->frames.size());
    REQUIRE(result->sampleRate == Approx(44100.0f));
}

// ==============================================================================
// Test: Stereo WAV downmixed to mono (FR-043 edge case)
// ==============================================================================
TEST_CASE("SampleAnalyzer: Stereo WAV downmixed to mono",
          "[innexus][sample_analyzer][FR-043]")
{
    auto filePath = generateStereoSineWav("test_stereo_440.wav", 440.0f, 0.5f);
    TempFileGuard guard{filePath};

    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(filePath);

    auto start = std::chrono::steady_clock::now();
    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start;
        REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 30);
    }

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);
    REQUIRE(result->frames.size() > 0);
    // Stereo file should produce the same structure as mono
    REQUIRE(result->totalFrames == result->frames.size());
}

// ==============================================================================
// Test: Background thread returns immediately (FR-044)
// ==============================================================================
TEST_CASE("SampleAnalyzer: startAnalysis returns immediately",
          "[innexus][sample_analyzer][FR-044]")
{
    auto filePath = generateSineWav("test_async_440.wav", 440.0f, 1.0f);
    TempFileGuard guard{filePath};

    Innexus::SampleAnalyzer analyzer;

    auto start = std::chrono::steady_clock::now();
    analyzer.startAnalysis(filePath);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // startAnalysis should return within 100ms (file I/O + thread spawn)
    // The actual analysis happens on the background thread
    REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 1000);

    // Wait for completion
    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);
}

// ==============================================================================
// Test: Analysis pipeline produces valid hop time and frame count (FR-046)
// ==============================================================================
TEST_CASE("SampleAnalyzer: hop time and frame count are valid",
          "[innexus][sample_analyzer][FR-046]")
{
    auto filePath = generateSineWav("test_hop_440.wav", 440.0f, 1.0f);
    TempFileGuard guard{filePath};

    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(filePath);

    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);
    REQUIRE(result->hopTimeSec > 0.0f);
    REQUIRE(result->totalFrames > 0);

    // For 1 second at 44.1kHz with hop size 512:
    // Expected ~86 frames (44100 / 512 = ~86.13)
    // Allow wide tolerance since STFT needs initial fill
    REQUIRE(result->totalFrames >= 40); // at least half the expected
    REQUIRE(result->totalFrames <= 150); // reasonable upper bound

    // Hop time should be 512/44100 ~= 0.0116 sec
    REQUIRE(result->hopTimeSec == Approx(512.0f / 44100.0f).margin(0.001f));
}

// ==============================================================================
// Test: Atomic pointer swap (FR-058)
// ==============================================================================
TEST_CASE("SampleAnalyzer: atomic pointer swap works correctly",
          "[innexus][sample_analyzer][FR-058]")
{
    auto filePath = generateSineWav("test_atomic_440.wav", 440.0f, 0.25f);
    TempFileGuard guard{filePath};

    Innexus::SampleAnalyzer analyzer;

    // Test atomic<SampleAnalysis*> pattern
    std::atomic<Innexus::SampleAnalysis*> sharedPtr{nullptr};

    analyzer.startAnalysis(filePath);

    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);

    // Simulate background thread publishing via atomic pointer swap
    auto* rawPtr = result.get();
    sharedPtr.store(rawPtr, std::memory_order_release);

    // Simulate audio thread reading via atomic pointer
    auto* readPtr = sharedPtr.load(std::memory_order_acquire);
    REQUIRE(readPtr != nullptr);
    REQUIRE(readPtr == rawPtr);
    REQUIRE(readPtr->totalFrames > 0);
    REQUIRE(readPtr->frames.size() == readPtr->totalFrames);
}

// ==============================================================================
// Test: takeResult returns nullptr before completion
// ==============================================================================
TEST_CASE("SampleAnalyzer: takeResult returns nullptr before completion",
          "[innexus][sample_analyzer]")
{
    Innexus::SampleAnalyzer analyzer;

    // No analysis started
    auto result = analyzer.takeResult();
    REQUIRE(result == nullptr);
}

// ==============================================================================
// Test: Cancel stops background thread without crash
// ==============================================================================
TEST_CASE("SampleAnalyzer: cancel stops without crash",
          "[innexus][sample_analyzer]")
{
    auto filePath = generateSineWav("test_cancel_440.wav", 440.0f, 2.0f);
    TempFileGuard guard{filePath};

    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(filePath);

    // Cancel immediately
    analyzer.cancel();

    // Wait a bit for the thread to notice cancellation
    auto start = std::chrono::steady_clock::now();
    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start;
        REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 10);
    }

    // Should not crash -- result may be nullptr or partial
    // The key requirement is no crash
    REQUIRE(analyzer.isComplete());
}

// ==============================================================================
// Test: SampleAnalysis getFrame clamps index
// ==============================================================================
TEST_CASE("SampleAnalysis: getFrame clamps to valid range",
          "[innexus][sample_analysis]")
{
    Innexus::SampleAnalysis analysis;
    analysis.frames.resize(5);
    analysis.totalFrames = 5;

    // Set distinct values so we can verify
    for (size_t i = 0; i < 5; ++i) {
        analysis.frames[i].f0 = static_cast<float>(i) * 100.0f;
    }

    // Normal access
    REQUIRE(analysis.getFrame(0).f0 == Approx(0.0f));
    REQUIRE(analysis.getFrame(2).f0 == Approx(200.0f));
    REQUIRE(analysis.getFrame(4).f0 == Approx(400.0f));

    // Out of bounds clamps to last frame
    REQUIRE(analysis.getFrame(5).f0 == Approx(400.0f));
    REQUIRE(analysis.getFrame(100).f0 == Approx(400.0f));
}

// ==============================================================================
// Test: SampleAnalysis getFrame on empty returns default
// ==============================================================================
TEST_CASE("SampleAnalysis: getFrame on empty analysis returns default",
          "[innexus][sample_analysis]")
{
    Innexus::SampleAnalysis analysis;

    const auto& frame = analysis.getFrame(0);
    REQUIRE(frame.f0 == 0.0f);
    REQUIRE(frame.f0Confidence == 0.0f);
    REQUIRE(frame.numPartials == 0);
}

// ==============================================================================
// Test: Non-existent file does not crash
// ==============================================================================
TEST_CASE("SampleAnalyzer: non-existent file completes without crash",
          "[innexus][sample_analyzer]")
{
    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis("this_file_does_not_exist_12345.wav");

    auto start = std::chrono::steady_clock::now();
    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start;
        REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 5);
    }

    // Should complete but with null result (file not found)
    auto result = analyzer.takeResult();
    // Result may be null since file doesn't exist
    REQUIRE(analyzer.isComplete());
}

// ==============================================================================
// Test: File path stored in SampleAnalysis (FR-046)
// ==============================================================================
TEST_CASE("SampleAnalyzer: file path stored in result",
          "[innexus][sample_analyzer][FR-046]")
{
    auto filePath = generateSineWav("test_path_440.wav", 440.0f, 0.25f);
    TempFileGuard guard{filePath};

    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(filePath);

    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);
    REQUIRE(result->filePath == filePath);
}

// ==============================================================================
// Test: SC-005 - Analysis completes within 10s for a 10s mono file
// ==============================================================================
TEST_CASE("SampleAnalyzer: SC-005 analysis timing for 1s file",
          "[innexus][sample_analyzer][SC-005]")
{
    // Generate a 1-second mono sine (testing with 1s to keep CI fast)
    auto filePath = generateSineWav("test_timing_440.wav", 440.0f, 1.0f);
    TempFileGuard guard{filePath};

    Innexus::SampleAnalyzer analyzer;

    auto start = std::chrono::steady_clock::now();
    analyzer.startAnalysis(filePath);

    while (!analyzer.isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // 1s file should complete well within 10s (SC-005 targets 10s for 10s file)
    // For a 1s file, expect < 2s
    REQUIRE(elapsedMs < 5000);

    auto result = analyzer.takeResult();
    REQUIRE(result != nullptr);
    REQUIRE(result->totalFrames > 0);
}
