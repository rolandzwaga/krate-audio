// ==============================================================================
// Layer 4: Effects Tests - Pattern Freeze Mode
// ==============================================================================
// Unit tests for PatternFreezeMode (spec 069 - Pattern Freeze Mode; redesigned by specs/iterum-freeze/design.md).
//
// Tests verify:
// - Euclidean pattern playback
// - Capture and slice triggering
// - Envelope-shaped playback
// - Tempo synchronization
// - The held window, Hold, the Dry law and the transport rules of the Freeze redesign (design.md section 7, rows
//   D1, D4-D14, D16, D17; tags [freeze-held], [freeze-transport], [freeze-drone] for the new groups)
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-first development methodology
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/effects/pattern_freeze_mode.h>
#include <krate/dsp/core/block_context.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/processors/noise_generator.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// @brief Calculate RMS of a buffer
float patternFreezeCalculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

/// @brief Generate a simple sine wave
void pfGenerateSine(float* buffer, size_t size, float frequency, double sampleRate, float amplitude = 1.0f) {
    const float phase_inc = static_cast<float>(2.0 * 3.14159265358979323846 * frequency / sampleRate);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(static_cast<float>(i) * phase_inc);
    }
}

/// @brief Create default BlockContext
BlockContext pfCreateContext(double sampleRate = 44100.0, double tempoBPM = 120.0) {
    BlockContext ctx;
    ctx.sampleRate = sampleRate;
    ctx.tempoBPM = tempoBPM;
    return ctx;
}

/// @brief A BlockContext of a running transport at 120 BPM whose song position is the block's first sample
BlockContext pfPlayingContext(size_t startSample, size_t numSamples, double sampleRate = 44100.0) {
    return BlockContext{.sampleRate = sampleRate,
                        .blockSize = numSamples,
                        .tempoBPM = 120.0,
                        .isPlaying = true,
                        .transportPositionSamples = static_cast<int64_t>(startSample),
                        .projectTimeMusic = static_cast<double>(startSample) * 120.0 / (60.0 * sampleRate),
                        .projectTimeMusicValid = true};
}

/// @brief Feeds one complete Capture 1/1 cycle at 120 BPM (2 s plus one block of a 440 Hz sine at 0.5) with the
/// transport playing from beat 0, so the first boundary of design 2.2 has captured a window. @p pos is the song
/// position in samples, advanced past what was fed.
void pfFeedCycle(PatternFreezeMode& freeze, size_t& pos) {
    std::vector<float> left(512), right(512);
    const size_t end = 88200 + 512;
    while (pos < end) {
        for (size_t s = 0; s < 512; ++s) {
            left[s] = 0.5f * std::sin(static_cast<float>(6.283185307179586 * 440.0 * static_cast<double>(pos + s) / 44100.0));
        }
        std::copy(left.begin(), left.end(), right.begin());
        freeze.process(left.data(), right.data(), 512, pfPlayingContext(pos, 512));
        pos += 512;
    }
}

}  // anonymous namespace

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("PatternFreezeMode prepares correctly", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);

    REQUIRE(freeze.isPrepared());
}

TEST_CASE("PatternFreezeMode reset clears state", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);

    // Feed some audio
    std::vector<float> left(512), right(512);
    pfGenerateSine(left.data(), 512, 440.0f, 44100.0);
    pfGenerateSine(right.data(), 512, 440.0f, 44100.0);

    auto ctx = pfCreateContext();
    for (int i = 0; i < 10; ++i) {
        freeze.process(left.data(), right.data(), 512, ctx);
    }

    freeze.reset();

    // After reset no window exists (design 7.1, mode-entry rule)
    REQUIRE(freeze.hasWindow() == false);
}

// =============================================================================
// Pattern Type Tests
// =============================================================================

TEST_CASE("PatternFreezeMode supports all pattern types", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);

    SECTION("Euclidean pattern") {
        freeze.setPatternType(PatternType::Euclidean);
        REQUIRE(freeze.getPatternType() == PatternType::Euclidean);
    }

    SECTION("Granular Scatter pattern") {
        freeze.setPatternType(PatternType::GranularScatter);
        REQUIRE(freeze.getPatternType() == PatternType::GranularScatter);
    }

    SECTION("Harmonic Drones pattern") {
        freeze.setPatternType(PatternType::HarmonicDrones);
        REQUIRE(freeze.getPatternType() == PatternType::HarmonicDrones);
    }

    SECTION("Noise Bursts pattern") {
        freeze.setPatternType(PatternType::NoiseBursts);
        REQUIRE(freeze.getPatternType() == PatternType::NoiseBursts);
    }
}

// =============================================================================
// Euclidean Pattern Tests
// =============================================================================

TEST_CASE("PatternFreezeMode Euclidean pattern parameters", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);
    freeze.setPatternType(PatternType::Euclidean);

    SECTION("Sets Euclidean steps") {
        freeze.setEuclideanSteps(16);
        REQUIRE(freeze.getEuclideanSteps() == 16);
    }

    SECTION("Sets Euclidean hits") {
        freeze.setEuclideanHits(5);
        REQUIRE(freeze.getEuclideanHits() == 5);
    }

    SECTION("Sets Euclidean rotation") {
        freeze.setEuclideanRotation(3);
        REQUIRE(freeze.getEuclideanRotation() == 3);
    }

    SECTION("Clamps steps to valid range") {
        freeze.setEuclideanSteps(1);  // Below minimum
        REQUIRE(freeze.getEuclideanSteps() >= 2);

        freeze.setEuclideanSteps(100);  // Above maximum
        REQUIRE(freeze.getEuclideanSteps() <= 32);
    }

    SECTION("Clamps hits to steps") {
        freeze.setEuclideanSteps(8);
        freeze.setEuclideanHits(20);  // More than steps
        REQUIRE(freeze.getEuclideanHits() <= 8);
    }
}

// =============================================================================
// Capture Buffer Tests
// =============================================================================

TEST_CASE("PatternFreezeMode captures incoming audio", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);
    freeze.setPatternType(PatternType::Euclidean);  // Use Euclidean for predictable behavior
    freeze.setCaptureLength(CaptureLength::Whole);
    freeze.snapParameters();

    // One complete cycle at 120 BPM, Capture 1/1, with the transport playing: the first boundary captures
    size_t pos = 0;
    pfFeedCycle(freeze, pos);

    // Should now hold a window
    REQUIRE(freeze.hasWindow());
}

// =============================================================================
// Slice Length Tests
// =============================================================================

TEST_CASE("PatternFreezeMode slice length parameter", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);

    SECTION("Sets slice length") {
        freeze.setSliceLengthMs(200.0f);
        REQUIRE(freeze.getSliceLengthMs() == Approx(200.0f));
    }

    SECTION("Clamps to valid range") {
        freeze.setSliceLengthMs(1.0f);  // Too short
        REQUIRE(freeze.getSliceLengthMs() >= PatternFreezeConstants::kMinSliceLengthMs);

        freeze.setSliceLengthMs(100000.0f);  // Too long
        REQUIRE(freeze.getSliceLengthMs() <= PatternFreezeConstants::kMaxSliceLengthMs);
    }
}

// =============================================================================
// Envelope Tests
// =============================================================================

TEST_CASE("PatternFreezeMode envelope parameters", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);

    SECTION("Sets envelope attack") {
        freeze.setEnvelopeAttackMs(20.0f);
        REQUIRE(freeze.getEnvelopeAttackMs() == Approx(20.0f));
    }

    SECTION("Sets envelope release") {
        freeze.setEnvelopeReleaseMs(50.0f);
        REQUIRE(freeze.getEnvelopeReleaseMs() == Approx(50.0f));
    }

    SECTION("Sets envelope shape") {
        freeze.setEnvelopeShape(EnvelopeShape::Exponential);
        REQUIRE(freeze.getEnvelopeShape() == EnvelopeShape::Exponential);
    }
}

// =============================================================================
// Mix Tests
// =============================================================================

TEST_CASE("PatternFreezeMode mix parameters", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);

    SECTION("Defaults to 100 % mix (design 3.3)") {
        REQUIRE(PatternFreezeMode::kDefaultDryWetMix == Approx(100.0f));
        REQUIRE(freeze.getDryWetMix() == Approx(100.0f));
    }

    SECTION("Sets dry/wet mix") {
        freeze.setDryWetMix(75.0f);
        REQUIRE(freeze.getDryWetMix() == Approx(75.0f));
    }

    SECTION("Clamps mix to valid range") {
        freeze.setDryWetMix(-10.0f);
        REQUIRE(freeze.getDryWetMix() >= 0.0f);

        freeze.setDryWetMix(150.0f);
        REQUIRE(freeze.getDryWetMix() <= 100.0f);
    }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("PatternFreezeMode handles zero-length blocks", "[effects][pattern_freeze][layer4][edge]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);
    freeze.snapParameters();

    std::vector<float> left(512), right(512);
    auto ctx = pfCreateContext();

    // Should not crash with zero samples
    freeze.process(left.data(), right.data(), 0, ctx);

    REQUIRE(true);  // Just verify no crash
}

TEST_CASE("PatternFreezeMode handles null buffers", "[effects][pattern_freeze][layer4][edge]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);
    freeze.snapParameters();

    auto ctx = pfCreateContext();

    // Should not crash with null pointers
    freeze.process(nullptr, nullptr, 512, ctx);

    REQUIRE(true);  // Just verify no crash
}

// =============================================================================
// Granular Scatter Tests (User Story 2)
// =============================================================================

TEST_CASE("PatternFreezeMode Granular Scatter parameters", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);
    freeze.setPatternType(PatternType::GranularScatter);

    SECTION("Sets granular density") {
        freeze.setGranularDensity(15.0f);
        REQUIRE(freeze.getGranularDensity() == Approx(15.0f));
    }

    SECTION("Sets position jitter") {
        freeze.setGranularPositionJitter(0.75f);
        REQUIRE(freeze.getGranularPositionJitter() == Approx(0.75f));
    }

    SECTION("Sets size jitter") {
        freeze.setGranularSizeJitter(0.5f);
        REQUIRE(freeze.getGranularSizeJitter() == Approx(0.5f));
    }

    SECTION("Sets grain size") {
        freeze.setGranularGrainSize(150.0f);
        REQUIRE(freeze.getGranularGrainSize() == Approx(150.0f));
    }

    SECTION("Clamps density to valid range") {
        freeze.setGranularDensity(0.1f);  // Too low
        REQUIRE(freeze.getGranularDensity() >= 1.0f);

        freeze.setGranularDensity(100.0f);  // Too high
        REQUIRE(freeze.getGranularDensity() <= 50.0f);
    }
}

TEST_CASE("PatternFreezeMode Granular Scatter produces output", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);
    freeze.setPatternType(PatternType::GranularScatter);
    freeze.setGranularDensity(20.0f);  // 20Hz density
    freeze.setGranularGrainSize(50.0f);  // 50ms grains
    freeze.setDryWetMix(100.0f);
    freeze.snapParameters();

    // Feed one complete cycle with the transport playing, so a window exists
    size_t pos = 0;
    pfFeedCycle(freeze, pos);

    std::vector<float> left(512), right(512);
    float totalRMS = 0.0f;
    for (int i = 0; i < 50; ++i) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        freeze.process(left.data(), right.data(), 512, pfPlayingContext(pos, 512));
        pos += 512;
        totalRMS += patternFreezeCalculateRMS(left.data(), 512);
    }

    // Should produce some output due to grain triggering
    REQUIRE(totalRMS > 0.0f);
}

// =============================================================================
// Harmonic Drones Tests (User Story 3)
// =============================================================================

TEST_CASE("PatternFreezeMode Harmonic Drones parameters", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);
    freeze.setPatternType(PatternType::HarmonicDrones);

    SECTION("Sets drone voice count") {
        freeze.setDroneVoiceCount(3);
        REQUIRE(freeze.getDroneVoiceCount() == 3);
    }

    SECTION("Sets drone interval") {
        freeze.setDroneInterval(PitchInterval::Fifth);
        REQUIRE(freeze.getDroneInterval() == PitchInterval::Fifth);
    }

    SECTION("Sets drone drift") {
        freeze.setDroneDrift(0.5f);
        REQUIRE(freeze.getDroneDrift() == Approx(0.5f));
    }

    SECTION("Sets drone drift rate") {
        freeze.setDroneDriftRate(0.8f);
        REQUIRE(freeze.getDroneDriftRate() == Approx(0.8f));
    }

    SECTION("Clamps voice count to valid range") {
        freeze.setDroneVoiceCount(0);  // Too low
        REQUIRE(freeze.getDroneVoiceCount() >= 1);

        freeze.setDroneVoiceCount(10);  // Too high
        REQUIRE(freeze.getDroneVoiceCount() <= 4);
    }
}

TEST_CASE("PatternFreezeMode Harmonic Drones produces output", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);
    freeze.setPatternType(PatternType::HarmonicDrones);
    freeze.setDroneVoiceCount(2);
    freeze.setDroneInterval(PitchInterval::Octave);
    freeze.setSliceLengthMs(200.0f);
    freeze.setDryWetMix(100.0f);
    freeze.snapParameters();

    // Feed one complete cycle with the transport playing, so a window exists
    size_t pos = 0;
    pfFeedCycle(freeze, pos);

    std::vector<float> left(512), right(512);
    float totalRMS = 0.0f;
    for (int i = 0; i < 50; ++i) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        freeze.process(left.data(), right.data(), 512, pfPlayingContext(pos, 512));
        pos += 512;
        totalRMS += patternFreezeCalculateRMS(left.data(), 512);
    }

    // Should produce continuous drone output
    REQUIRE(totalRMS > 0.0f);
}

// =============================================================================
// Noise Bursts Tests (User Story 4)
// =============================================================================

TEST_CASE("PatternFreezeMode Noise Bursts parameters", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);
    freeze.setPatternType(PatternType::NoiseBursts);

    SECTION("Sets noise color") {
        freeze.setNoiseColor(NoiseColor::Pink);
        REQUIRE(freeze.getNoiseColor() == NoiseColor::Pink);

        freeze.setNoiseColor(NoiseColor::White);
        REQUIRE(freeze.getNoiseColor() == NoiseColor::White);

        freeze.setNoiseColor(NoiseColor::Brown);
        REQUIRE(freeze.getNoiseColor() == NoiseColor::Brown);
    }

    SECTION("Sets noise burst rate") {
        freeze.setNoiseBurstRate(NoteValue::Quarter);
        REQUIRE(freeze.getNoiseBurstRate() == NoteValue::Quarter);
    }

    SECTION("Sets noise filter type") {
        freeze.setNoiseFilterType(FilterType::Highpass);
        REQUIRE(freeze.getNoiseFilterType() == FilterType::Highpass);
    }

    SECTION("Sets noise filter cutoff") {
        freeze.setNoiseFilterCutoff(2000.0f);
        REQUIRE(freeze.getNoiseFilterCutoff() == Approx(2000.0f));
    }

    SECTION("Sets noise filter sweep") {
        freeze.setNoiseFilterSweep(0.75f);
        REQUIRE(freeze.getNoiseFilterSweep() == Approx(0.75f));
    }
}

TEST_CASE("PatternFreezeMode Noise Bursts requires captured audio content", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);
    freeze.setPatternType(PatternType::NoiseBursts);
    freeze.setNoiseColor(NoiseColor::Pink);
    freeze.setNoiseBurstRate(NoteValue::Sixteenth);
    freeze.setNoiseFilterCutoff(5000.0f);
    freeze.setDryWetMix(100.0f);
    freeze.snapParameters();

    // Process with silence - should NOT produce noise bursts
    size_t pos = 0;
    float silentRMS = 0.0f;
    std::vector<float> silent(512, 0.0f);
    for (int i = 0; i < 50; ++i) {
        std::fill(silent.begin(), silent.end(), 0.0f);
        freeze.process(silent.data(), silent.data(), 512, pfPlayingContext(pos, 512));
        pos += 512;
        silentRMS += patternFreezeCalculateRMS(silent.data(), 512);
    }
    REQUIRE(silentRMS == 0.0f);  // No output when no audio captured

    // Now feed one complete cycle of actual audio with the transport playing
    std::vector<float> left(512), right(512);
    const size_t feedEnd = pos + 88200 + 512;
    while (pos < feedEnd) {
        for (size_t s = 0; s < 512; ++s) {
            left[s] = 0.3f * std::sin(static_cast<float>(pos + s) * 0.1f);
            right[s] = left[s];
        }
        freeze.process(left.data(), right.data(), 512, pfPlayingContext(pos, 512));
        pos += 512;
    }

    // Process with silence after capturing audio - should now produce noise bursts
    float totalRMS = 0.0f;
    for (int i = 0; i < 100; ++i) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        freeze.process(left.data(), right.data(), 512, pfPlayingContext(pos, 512));
        pos += 512;
        totalRMS += patternFreezeCalculateRMS(left.data(), 512);
    }

    // Should now produce noise bursts (captured audio has content)
    REQUIRE(totalRMS > 0.0f);
}

// =============================================================================
// Real-Time Safety Tests
// =============================================================================

TEST_CASE("PatternFreezeMode process is noexcept", "[effects][pattern_freeze][layer4][realtime]") {
    PatternFreezeMode freeze;
    std::vector<float> left(512), right(512);
    BlockContext ctx;

    static_assert(noexcept(freeze.process(left.data(), right.data(), 512, ctx)),
                  "process() must be noexcept");
}

// =============================================================================
// Edge Case Tests (Phase 9)
// =============================================================================

TEST_CASE("PatternFreezeMode edge cases", "[effects][pattern_freeze][layer4][edge]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);

    SECTION("Handles freeze before buffer filled (edge case 1)") {
        freeze.setPatternType(PatternType::Euclidean);
        freeze.snapParameters();

        // Process immediately without filling buffer
        std::vector<float> left(512, 0.5f), right(512, 0.5f);
        auto ctx = pfCreateContext();
        freeze.process(left.data(), right.data(), 512, ctx);

        // Should not crash, and output should be silent or minimal
        float rms = patternFreezeCalculateRMS(left.data(), 512);
        // Output should be close to zero since buffer isn't ready
        REQUIRE(rms < 1.0f);
    }

    SECTION("Slice length clamped to buffer size (edge case 3)") {
        freeze.setSliceLengthMs(100000.0f);  // Much larger than buffer
        REQUIRE(freeze.getSliceLengthMs() <= PatternFreezeConstants::kMaxSliceLengthMs);
    }

    SECTION("Euclidean hits clamped to steps (edge case 8)") {
        freeze.setEuclideanSteps(8);
        freeze.setEuclideanHits(100);  // More than steps
        REQUIRE(freeze.getEuclideanHits() <= 8);
    }
}

// =============================================================================
// Envelope Shaping Tests (Phase 9)
// =============================================================================

TEST_CASE("PatternFreezeMode envelope shaping", "[effects][pattern_freeze][layer4][envelope]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);
    freeze.setPatternType(PatternType::Euclidean);

    SECTION("Envelope attack clamped to valid range") {
        freeze.setEnvelopeAttackMs(-10.0f);
        REQUIRE(freeze.getEnvelopeAttackMs() >= 0.0f);

        freeze.setEnvelopeAttackMs(1000.0f);
        REQUIRE(freeze.getEnvelopeAttackMs() <= PatternFreezeConstants::kMaxEnvelopeAttackMs);
    }

    SECTION("Envelope release clamped to valid range") {
        freeze.setEnvelopeReleaseMs(-10.0f);
        REQUIRE(freeze.getEnvelopeReleaseMs() >= 0.0f);

        freeze.setEnvelopeReleaseMs(10000.0f);
        REQUIRE(freeze.getEnvelopeReleaseMs() <= PatternFreezeConstants::kMaxEnvelopeReleaseMs);
    }

    SECTION("Linear envelope shape produces output") {
        // Use Granular Scatter for reliable grain triggering
        freeze.setPatternType(PatternType::GranularScatter);
        freeze.setGranularDensity(20.0f);
        freeze.setEnvelopeShape(EnvelopeShape::Linear);
        REQUIRE(freeze.getEnvelopeShape() == EnvelopeShape::Linear);

        // Process and verify
        freeze.setDryWetMix(100.0f);
        freeze.snapParameters();

        // Feed one complete cycle with the transport playing, so a window exists
        size_t pos = 0;
        pfFeedCycle(freeze, pos);

        std::vector<float> left(512), right(512);
        float totalRMS = 0.0f;
        for (int i = 0; i < 50; ++i) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            freeze.process(left.data(), right.data(), 512, pfPlayingContext(pos, 512));
            pos += 512;
            totalRMS += patternFreezeCalculateRMS(left.data(), 512);
        }

        REQUIRE(totalRMS > 0.0f);
    }

    SECTION("Exponential envelope shape produces output") {
        // Use Granular Scatter for reliable grain triggering
        freeze.setPatternType(PatternType::GranularScatter);
        freeze.setGranularDensity(20.0f);
        freeze.setEnvelopeShape(EnvelopeShape::Exponential);
        REQUIRE(freeze.getEnvelopeShape() == EnvelopeShape::Exponential);

        // Process and verify
        freeze.setDryWetMix(100.0f);
        freeze.snapParameters();

        // Feed one complete cycle with the transport playing, so a window exists
        size_t pos = 0;
        pfFeedCycle(freeze, pos);

        std::vector<float> left(512), right(512);
        float totalRMS = 0.0f;
        for (int i = 0; i < 50; ++i) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            freeze.process(left.data(), right.data(), 512, pfPlayingContext(pos, 512));
            pos += 512;
            totalRMS += patternFreezeCalculateRMS(left.data(), 512);
        }

        REQUIRE(totalRMS > 0.0f);
    }
}

// =============================================================================
// Freeze redesign (specs/iterum-freeze/design.md section 7): shared helpers
// =============================================================================
// Every render below is a fresh PatternFreezeMode configured before prepare() (so the Mix smoother starts on its
// target) and again after it, driven like a host: the input is copied into the output buffers and each block is
// processed in place with a BlockContext whose song position advances at the current tempo. "wet" is the output of
// a Mix 100 % / Dry = Mix render (design 7, the shared helper of D4-D11).

namespace {

constexpr double kPfSr = 44100.0;
constexpr double kPfTwoPi = 6.283185307179586476925286766559;
constexpr size_t kPfBlock = 512;
constexpr size_t kPfWin = 441;                       // 10 ms at 44.1 kHz
constexpr double kPfSineRms = 0.35355339059327373;   // RMS of a 0.5 sine: the reference of the active fraction

/// Samples of @p seconds at 44.1 kHz.
[[nodiscard]] size_t pfAt(double seconds) {
    return static_cast<size_t>(std::llround(seconds * kPfSr));
}

struct PfSeg {
    double fromSec;
    double hz;
    float amp;
};

/// Phase-continuous tones: from each segment's start the tone is @c hz at @c amp (amp 0 = silence). Before the
/// first segment the signal is silent.
[[nodiscard]] std::vector<float> pfTones(size_t total, const std::vector<PfSeg>& segs, double phase0 = 0.0) {
    std::vector<float> x(total, 0.0f);
    double phase = phase0;
    size_t seg = 0;
    for (size_t i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / kPfSr;
        if (segs.empty() || t < segs.front().fromSec) {
            continue;
        }
        while (seg + 1 < segs.size() && t >= segs[seg + 1].fromSec) {
            ++seg;
        }
        x[i] = static_cast<float>(static_cast<double>(segs[seg].amp) * std::sin(phase));
        phase += kPfTwoPi * segs[seg].hz / kPfSr;
    }
    return x;
}

/// The probe input of design 6.1, extended: 233 Hz [0, 2 s), 311 [2, 4 s), 415 [4, 6 s), 523 Hz from 6 s, 0.5.
[[nodiscard]] std::vector<float> pfProbeTones(size_t total) {
    return pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 311.0, 0.5f}, {4.0, 415.0, 0.5f}, {6.0, 523.0, 0.5f}});
}

/// One tone per beat at 120 BPM, cycling through @p tones, 0.5, phase-continuous.
[[nodiscard]] std::vector<float> pfBeatTones(size_t total, std::initializer_list<double> tones) {
    std::vector<PfSeg> segs;
    const std::vector<double> t(tones);
    const size_t beats = total / 22050 + 1;
    for (size_t b = 0; b < beats; ++b) {
        segs.push_back({static_cast<double>(b) * 0.5, t[b % t.size()], 0.5f});
    }
    return pfTones(total, segs);
}

/// The four-tone bar of D6: beats 220 / 330 / 440 / 550 Hz, repeated from reset.
[[nodiscard]] std::vector<float> pfFourToneBars(size_t total) {
    return pfBeatTones(total, {220.0, 330.0, 440.0, 550.0});
}

[[nodiscard]] std::vector<float> pfConstant(size_t total, float value) {
    return std::vector<float>(total, value);
}

/// The fresh defaults of design 3.3, measured wet-only (Mix 100 %, Dry = Mix) unless a case says Insert.
void pfFreshDefaults(PatternFreezeMode& m) {
    m.setPatternType(PatternType::Euclidean);
    m.setSliceLengthMs(PatternFreezeConstants::kDefaultSliceLengthMs);
    m.setSliceMode(SliceMode::Fixed);
    m.setEuclideanSteps(8);
    m.setEuclideanHits(3);
    m.setEuclideanRotation(0);
    m.setNoteValue(NoteValue::Eighth);
    m.setNoteModifier(NoteModifier::None);
    m.setEnvelopeAttackMs(PatternFreezeConstants::kDefaultEnvelopeAttackMs);
    m.setEnvelopeReleaseMs(PatternFreezeConstants::kDefaultEnvelopeReleaseMs);
    m.setEnvelopeShape(EnvelopeShape::Linear);
    m.setDryWetMix(100.0f);
    m.setDryMode(DryMode::Mix);
    m.setCaptureLength(CaptureLength::Whole);
    m.setHold(false);
}

void pfInsertDefaults(PatternFreezeMode& m) {
    pfFreshDefaults(m);
    m.setDryMode(DryMode::Insert);
}

/// A PatternFreezeMode driven like a host (see the section comment above).
class PfRig {
public:
    using Configure = std::function<void(PatternFreezeMode&)>;

    PfRig(std::vector<float> input, const Configure& configure, size_t maxBlock = 8192)
        : in_(std::move(input)), outL_(in_), outR_(in_), mode_(std::make_unique<PatternFreezeMode>()) {
        configure(*mode_);
        mode_->prepare(kPfSr, maxBlock, 5000.0f);
        configure(*mode_);
    }
    PfRig(PfRig&&) = default;
    PfRig& operator=(PfRig&&) = default;
    PfRig(const PfRig&) = delete;
    PfRig& operator=(const PfRig&) = delete;

    double bpm = 120.0;
    bool playing = true;
    bool valid = true;
    double beat = 0.0;  ///< song position of sample pos()

    void stop() {
        playing = false;
        valid = false;
    }
    void play(double fromBeat) {
        playing = true;
        valid = true;
        beat = fromBeat;
    }
    void locate(double toBeat) { beat = toBeat; }

    [[nodiscard]] double spb() const { return kPfSr * 60.0 / bpm; }
    [[nodiscard]] size_t pos() const { return cursor_; }
    [[nodiscard]] size_t size() const { return in_.size(); }

    /// One block of @p n samples at pos().
    void processBlock(size_t n) {
        n = std::min(n, in_.size() - cursor_);
        if (n == 0) {
            return;
        }
        const BlockContext ctx{.sampleRate = kPfSr,
                               .blockSize = n,
                               .tempoBPM = bpm,
                               .isPlaying = playing,
                               .transportPositionSamples = static_cast<int64_t>(cursor_),
                               .projectTimeMusic = beat,
                               .projectTimeMusicValid = valid};
        mode_->process(&outL_[cursor_], &outR_[cursor_], n, ctx);
        cursor_ += n;
        beat += static_cast<double>(n) / spb();
    }

    /// Processes up to sample @p end (exclusive) in blocks of @p block, the last one cut at @p end.
    void runTo(size_t end, size_t block = kPfBlock) {
        end = std::min(end, in_.size());
        while (cursor_ < end) {
            processBlock(std::min(block, end - cursor_));
        }
    }
    void runToSec(double seconds, size_t block = kPfBlock) { runTo(pfAt(seconds), block); }

    /// As runTo, calling @p hook(mode, blockStart) before every block.
    template <typename Hook>
    void runEach(size_t end, size_t block, Hook hook) {
        end = std::min(end, in_.size());
        while (cursor_ < end) {
            hook(*mode_, cursor_);
            processBlock(std::min(block, end - cursor_));
        }
    }

    PatternFreezeMode& mode() { return *mode_; }
    [[nodiscard]] const std::vector<float>& in() const { return in_; }
    [[nodiscard]] const std::vector<float>& out() const { return outL_; }
    [[nodiscard]] const std::vector<float>& outR() const { return outR_; }

private:
    std::vector<float> in_;
    std::vector<float> outL_;
    std::vector<float> outR_;
    std::unique_ptr<PatternFreezeMode> mode_;
    size_t cursor_ = 0;
};

/// The same scenario rendered under Insert and under Dry = Mix (the wet).
struct PfPair {
    PfRig insert;
    PfRig wet;
};

template <typename Script>
[[nodiscard]] PfPair pfRenderPair(const std::vector<float>& input, const PfRig::Configure& configure, Script script) {
    PfRig insert(input, [&](PatternFreezeMode& m) {
        configure(m);
        m.setDryMode(DryMode::Insert);
    });
    PfRig wet(input, [&](PatternFreezeMode& m) {
        configure(m);
        m.setDryMode(DryMode::Mix);
    });
    script(insert);
    script(wet);
    return PfPair{std::move(insert), std::move(wet)};
}

// ---- analysis ---------------------------------------------------------------------------------------------------

[[nodiscard]] double pfEnergy(const std::vector<float>& x, size_t a, size_t b) {
    double e = 0.0;
    for (size_t i = a; i < std::min(b, x.size()); ++i) {
        e += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return e;
}

[[nodiscard]] double pfRms(const std::vector<float>& x, size_t a, size_t b) {
    return b > a ? std::sqrt(pfEnergy(x, a, b) / static_cast<double>(b - a)) : 0.0;
}

[[nodiscard]] double pfDbfs(double rms) { return 20.0 * std::log10(std::max(rms, 1e-30)); }

[[nodiscard]] std::vector<float> pfDiff(const std::vector<float>& a, const std::vector<float>& b) {
    std::vector<float> d(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        d[i] = a[i] - b[i];
    }
    return d;
}

/// max |a[i] - b[i]| over [from, to)
[[nodiscard]] double pfMaxAbsDiff(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to) {
    double m = 0.0;
    for (size_t i = from; i < std::min(to, a.size()); ++i) {
        m = std::max(m, static_cast<double>(std::abs(a[i] - b[i])));
    }
    return m;
}

/// Goertzel: the squared amplitude of the component of @p x at @p hz over [a, b) (0.25 for a 0.5 sine).
[[nodiscard]] double pfTonePower(const std::vector<float>& x, size_t a, size_t b, double hz) {
    const double w = kPfTwoPi * hz / kPfSr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (size_t i = a; i < b; ++i) {
        const double s0 = static_cast<double>(x[i]) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    const double n = static_cast<double>(b - a);
    return (re * re + im * im) * 4.0 / (n * n);
}

/// The tone of @p tones with the largest Goertzel power over [a, b).
[[nodiscard]] double pfDominant(const std::vector<float>& x, size_t a, size_t b, std::initializer_list<double> tones) {
    double best = 0.0;
    double bestPower = -1.0;
    for (const double f : tones) {
        const double p = pfTonePower(x, a, b, f);
        if (p > bestPower) {
            bestPower = p;
            best = f;
        }
    }
    return best;
}

[[nodiscard]] double pfMaxStep(const std::vector<float>& x, size_t a, size_t b) {
    double m = 0.0;
    for (size_t i = std::max<size_t>(a, 1); i < std::min(b, x.size()); ++i) {
        m = std::max(m, static_cast<double>(std::abs(x[i] - x[i - 1])));
    }
    return m;
}

[[nodiscard]] double pfPeak(const std::vector<float>& x, size_t a, size_t b) {
    double m = 0.0;
    for (size_t i = a; i < std::min(b, x.size()); ++i) {
        m = std::max(m, static_cast<double>(std::abs(x[i])));
    }
    return m;
}

[[nodiscard]] std::vector<double> pfWindowRms(const std::vector<float>& x, size_t a, size_t b, size_t win = kPfWin) {
    std::vector<double> r;
    for (size_t s = a; s + win <= b; s += win) {
        r.push_back(pfRms(x, s, s + win));
    }
    return r;
}

/// Fraction of 10 ms windows over [a, b) whose RMS is above @p db re @p refRms.
[[nodiscard]] double pfActiveFraction(const std::vector<float>& x, size_t a, size_t b, double refRms,
                                      double db = -40.0) {
    const auto r = pfWindowRms(x, a, b);
    const double thr = refRms * std::pow(10.0, db / 20.0);
    const auto active = std::count_if(r.begin(), r.end(), [thr](double v) { return v > thr; });
    return r.empty() ? 0.0 : static_cast<double>(active) / static_cast<double>(r.size());
}

/// max / min of the 10 ms RMS over [a, b), in dB.
[[nodiscard]] double pfDepthMaxMinDb(const std::vector<float>& x, size_t a, size_t b) {
    const auto r = pfWindowRms(x, a, b);
    const auto [lo, hi] = std::minmax_element(r.begin(), r.end());
    return 20.0 * std::log10(std::max(*hi, 1e-12) / std::max(*lo, 1e-12));
}

/// p95 / p5 of the 10 ms RMS over [a, b), in dB.
[[nodiscard]] double pfDepthP95P5Db(const std::vector<float>& x, size_t a, size_t b) {
    auto r = pfWindowRms(x, a, b);
    std::sort(r.begin(), r.end());
    const double p5 = r[static_cast<size_t>(0.05 * static_cast<double>(r.size() - 1))];
    const double p95 = r[static_cast<size_t>(0.95 * static_cast<double>(r.size() - 1))];
    return 20.0 * std::log10(std::max(p95, 1e-12) / std::max(p5, 1e-12));
}

/// The first index in [from, to) with |x| above @p thr, or @p to.
[[nodiscard]] size_t pfFirstAbove(const std::vector<float>& x, size_t from, size_t to, float thr = 1e-6f) {
    for (size_t i = from; i < std::min(to, x.size()); ++i) {
        if (std::abs(x[i]) > thr) {
            return i;
        }
    }
    return to;
}

/// The best normalised cross-correlation of @p wet against @p in delayed by 0..600 ms, over [a, b) (the probe's r).
[[nodiscard]] double pfBestLagCorrelation(const std::vector<float>& wet, const std::vector<float>& in, size_t a,
                                          size_t b) {
    double bestR = 0.0;
    for (size_t lag = 0; lag <= 26460 && lag <= a; lag += 21) {
        double num = 0.0;
        double ea = 0.0;
        double eb = 0.0;
        for (size_t i = a; i < b; ++i) {
            const double w = wet[i];
            const double x = in[i - lag];
            num += w * x;
            ea += w * w;
            eb += x * x;
        }
        bestR = std::max(bestR, num / std::sqrt(std::max(ea * eb, 1e-20)));
    }
    return bestR;
}

/// Instantaneous frequency estimates of @p x over [a, b): the time between positive-going zero crossings
/// (linearly interpolated), averaged over @p periods periods per estimate.
[[nodiscard]] std::vector<double> pfZeroCrossingHz(const std::vector<float>& x, size_t a, size_t b, size_t periods) {
    std::vector<double> crossings;
    for (size_t i = std::max<size_t>(a, 1); i < b; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double frac = static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
            crossings.push_back(static_cast<double>(i - 1) + frac);
        }
    }
    std::vector<double> hz;
    for (size_t k = 0; k + periods < crossings.size(); ++k) {
        hz.push_back(static_cast<double>(periods) * kPfSr / (crossings[k + periods] - crossings[k]));
    }
    return hz;
}

[[nodiscard]] double pfCents(double hz, double ref) { return 1200.0 * std::log2(hz / ref); }

/// The clean maximum sample step of a 0.5 sine at @p hz (design 7: 0.5 x 2 pi x f / 44100).
[[nodiscard]] double pfCleanStep(double hz) { return 0.5 * kPfTwoPi * hz / kPfSr; }

/// The largest sample step of an ideal sum of sines {amplitude, Hz} at any relative phase: the sum of each sine's own
/// largest step, 2 a sin(pi f / sr) (design R13: a step bar is 2 x this; a voice's phase against the root is the
/// shifter's, so every phase is possible).
[[nodiscard]] double pfIdealStep(std::initializer_list<std::pair<double, double>> sines) {
    double step = 0.0;
    for (const auto& [amp, hz] : sines) {
        step += 2.0 * amp * std::sin(0.5 * kPfTwoPi * hz / kPfSr);
    }
    return step;
}

/// The drone level of one voice of @p voices on a 0.5 window: 0.5 / sqrt(voices) (design 2.5.3).
[[nodiscard]] double pfDroneVoiceAmp(int voices) { return 0.5 / std::sqrt(static_cast<double>(voices)); }

/// The frequency @p cents above @p hz.
[[nodiscard]] double pfDetune(double hz, double cents) { return hz * std::pow(2.0, cents / 1200.0); }

/// R12: the peak of the Goertzel power of @p x over [a, b) within +/- 10 cents of @p hz, in 0.5-cent steps.
struct PfPeak {
    double cents;  ///< re @p hz
    double power;
};
[[nodiscard]] PfPeak pfPeakSearch(const std::vector<float>& x, size_t a, size_t b, double hz) {
    PfPeak best{0.0, -1.0};
    for (int k = -20; k <= 20; ++k) {
        const double c = 0.5 * static_cast<double>(k);
        const double p = pfTonePower(x, a, b, pfDetune(hz, c));
        if (p > best.power) {
            best = {c, p};
        }
    }
    return best;
}

/// The [freeze-transport] group's rig: fresh defaults, Attack 0, wet only, a DC 0.5 input, so a slice's first
/// sample is its onset and the wet is exactly 0 between slices.
[[nodiscard]] PfRig pfTransportRig(size_t total, const PfRig::Configure& extra = {}) {
    return {pfConstant(total, 0.5f), [extra](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setEnvelopeAttackMs(0.0f);
        if (extra) {
            extra(m);
        }
    }};
}

}  // anonymous namespace

// =============================================================================
// D1: the three cycle buffers (design 2.1)
// =============================================================================

TEST_CASE("PatternFreezeMode cycle buffers store each cycle from its downbeat and read forward",
          "[effects][pattern_freeze][layer4]") {
    // Capture 1/4 at 120 BPM: a cycle is one beat, 22050 samples. The boundary at 22050 captures the partial
    // window from the onset; the one at 44100 captures the complete cycle [22050, 44100).
    const size_t total = 44100 + 512;
    std::vector<float> ramp(total);
    for (size_t t = 0; t < total; ++t) {
        ramp[t] = static_cast<float>(static_cast<double>(t) / 1e6);
    }
    PfRig rig(ramp, [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setCaptureLength(CaptureLength::Quarter);
    });
    rig.runTo(total);
    auto& m = rig.mode();

    REQUIRE(m.hasWindow());
    REQUIRE(m.getWindowFrom() == 0);
    REQUIRE(m.getWindowLengthSamples() == 22050);
    for (const size_t k : {size_t{0}, size_t{1}, size_t{22049}}) {
        float l = -1.0f;
        float r = -1.0f;
        m.peekWindow(k, l, r);
        INFO("window offset " << k);
        CHECK(l == Approx(ramp[22050 + k]).margin(1e-7));
        CHECK(r == Approx(ramp[22050 + k]).margin(1e-7));
    }
    float l = -1.0f;
    float r = -1.0f;
    m.readWindowLinear(100.5, l, r);
    const double mean = 0.5 * (static_cast<double>(ramp[22150]) + static_cast<double>(ramp[22151]));
    CHECK(l == Approx(mean).margin(1e-7));
    CHECK(r == Approx(mean).margin(1e-7));
}

TEST_CASE("PatternFreezeMode a window latches its peak", "[effects][pattern_freeze][layer4]") {
    // Capture 1/4: a 0.5 sine for two beats, then zero input.
    const size_t total = 66150 + 512;
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {1.0, 0.0, 0.0f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setCaptureLength(CaptureLength::Quarter);
    });
    rig.runTo(44100 + 512);  // the window captured at beat 2: the complete cycle [22050, 44100)
    REQUIRE(rig.mode().hasWindow());
    CHECK(rig.mode().getWindowPeak() == Approx(0.5).margin(1e-3));
    CHECK(rig.mode().isWindowSilent() == false);

    rig.runTo(total);  // the window captured at beat 3: the silent cycle [44100, 66150)
    CHECK(rig.mode().isWindowSilent() == true);
}

// =============================================================================
// D4: the held window (design 2.2)
// =============================================================================

TEST_CASE("PatternFreezeMode holds the previous capture cycle", "[effects][pattern_freeze][layer4][freeze-held]") {
    const size_t total = pfAt(3.6);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 311.0, 0.5f}}), pfFreshDefaults);
    rig.runTo(total);
    const auto& wet = rig.out();

    const double p233 = pfTonePower(wet, pfAt(2.5), pfAt(3.5), 233.0);
    const double p311 = pfTonePower(wet, pfAt(2.5), pfAt(3.5), 311.0);
    // R11: the held-note check carries "held, not live".
    INFO("wet power over [2.5, 3.5 s): 233 Hz " << p233 << ", 311 Hz " << p311);
    CHECK(p233 > 10.0 * p311);

    // R11: r is printed, never asserted (a held pure tone lines up in phase with the input of the bar before, so
    // ideal output reads about 0.288).
    const double r = pfBestLagCorrelation(wet, rig.in(), pfAt(2.0), pfAt(3.0));
    std::printf("holds the previous capture cycle: r against the input over [2, 3 s) = %.3f (printed, R11)\n", r);
}

TEST_CASE("PatternFreezeMode captures at every boundary at 128 BPM", "[effects][pattern_freeze][layer4][freeze-held]") {
    // 8 bars of 233 Hz at 128 BPM: a bar is 82687.5 samples; beat 32 is sample 661500.
    const size_t total = 661500 + 512;
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), pfFreshDefaults);
    rig.bpm = 128.0;

    std::vector<long long> completeLengths;
    long long lastCount = 0;
    while (rig.pos() < total) {
        rig.runTo(rig.pos() + kPfBlock);
        const auto count = static_cast<long long>(rig.mode().getCaptureCount());
        if (count != lastCount) {
            REQUIRE(count == lastCount + 1);
            if (rig.mode().getWindowFrom() == 0) {
                completeLengths.push_back(static_cast<long long>(rig.mode().getWindowLengthSamples()));
            }
            lastCount = count;
        }
    }
    CHECK(rig.mode().getCaptureCount() == 8);
    CHECK(completeLengths.size() >= 7);
    for (const long long len : completeLengths) {
        INFO("complete window length " << len);
        CHECK((len == 82687 || len == 82688));
    }
}

TEST_CASE("PatternFreezeMode a capped cycle starts on its downbeat", "[effects][pattern_freeze][layer4][freeze-held]") {
    // 80 BPM, Capture 2/1: a cycle is 6 s (264600 samples), the window its first 5 s. A unit impulse on the first
    // sample of every cycle over a 233 Hz bed (the capped row of design 6.1).
    const size_t cycle = 264600;
    const size_t total = cycle + 512;
    auto input = pfTones(total, {{0.0, 233.0, 0.1f}});
    input[0] = 1.0f;
    input[cycle] = 1.0f;
    PfRig rig(input, [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setCaptureLength(CaptureLength::DoubleWhole);
    });
    rig.bpm = 80.0;
    rig.runTo(total);

    REQUIRE(rig.mode().hasWindow());
    CHECK(rig.mode().getWindowLengthSamples() == 220500);
    float l = 0.0f;
    float r = 0.0f;
    rig.mode().peekWindow(0, l, r);
    CHECK(l == Approx(1.0).margin(1e-6));
    CHECK(r == Approx(1.0).margin(1e-6));
}

TEST_CASE("PatternFreezeMode a silent cycle replaces the window", "[effects][pattern_freeze][layer4][freeze-held]") {
    SECTION("1/1: music [0, 2 s), silence from 2 s") {
        const size_t total = pfAt(6.0);
        PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}}), pfFreshDefaults);
        rig.runTo(pfAt(4.0));
        const auto before = static_cast<long long>(rig.mode().getCaptureCount());
        CHECK(rig.mode().isWindowSilent() == false);
        rig.runTo(pfAt(4.0) + kPfBlock);
        CHECK(static_cast<long long>(rig.mode().getCaptureCount()) == before + 1);
        while (rig.pos() < total) {
            INFO("block at " << rig.pos());
            CHECK(rig.mode().isWindowSilent() == true);
            rig.runTo(rig.pos() + kPfBlock);
        }
        const double wetDb = pfDbfs(pfRms(rig.out(), pfAt(4.4), pfAt(6.0)));
        INFO("wet over [4.4, 6 s): " << wetDb << " dBFS");
        CHECK(wetDb < -90.0);
    }

    SECTION("1/1 with Hold on at 1.9 s: the music stays") {
        const size_t total = pfAt(6.0);
        PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}}), pfFreshDefaults);
        rig.runToSec(1.9);
        rig.mode().setHold(true);
        rig.runTo(total);
        CHECK(rig.mode().isWindowSilent() == false);
        const double p233 = pfTonePower(rig.out(), pfAt(4.4), pfAt(6.0), 233.0);
        const double p311 = pfTonePower(rig.out(), pfAt(4.4), pfAt(6.0), 311.0);
        INFO("wet over [4.4, 6 s): 233 Hz " << p233 << ", 311 Hz " << p311);
        CHECK(p233 > 10.0 * p311);
        CHECK(pfDbfs(pfRms(rig.out(), pfAt(4.4), pfAt(6.0))) > -40.0);
    }

    SECTION("2/1: music [0, 4 s), silence from 4 s: the window goes silent at the 8 s boundary") {
        const size_t total = pfAt(8.0) + kPfBlock;
        PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {4.0, 0.0, 0.0f}}), [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setCaptureLength(CaptureLength::DoubleWhole);
        });
        rig.runTo(pfAt(8.0));
        REQUIRE(rig.mode().hasWindow());
        CHECK(rig.mode().isWindowSilent() == false);
        rig.runTo(total);
        CHECK(rig.mode().isWindowSilent() == true);
    }
}

TEST_CASE("PatternFreezeMode 20 s of silence, then music", "[effects][pattern_freeze][layer4][freeze-held]") {
    SECTION("free-run: the first hit after the onset plays the onset") {
        const size_t total = pfAt(21.0);
        PfRig rig(pfTones(total, {{20.0, 330.0, 0.5f}}), pfFreshDefaults);
        rig.stop();
        rig.runToSec(20.0);
        CHECK(rig.mode().getCaptureCount() == 0);
        CHECK(rig.mode().hasWindow() == false);
        CHECK(pfDbfs(pfRms(rig.out(), 0, pfAt(20.0))) < -120.0);
        rig.runTo(total);
        // The free-running grid puts beat 40 at 20 s; the span is valid at 20.2 s, so the first hit after it is
        // step 3 at 20.75 s.
        const size_t hit = pfAt(20.75);
        CHECK(pfDominant(rig.out(), hit, hit + 8820, {330.0, 165.0, 233.0, 311.0, 415.0, 660.0}) == 330.0);
        CHECK(pfDbfs(pfRms(rig.out(), hit, hit + 8820)) > -30.0);
    }

    SECTION("30 s stopped, then a play edge from beat 0 that finds no window") {
        const size_t total = pfAt(31.0);
        PfRig rig(pfTones(total, {{30.0, 330.0, 0.5f}}), pfFreshDefaults);
        rig.stop();
        rig.runToSec(30.0);
        CHECK(rig.mode().getCaptureCount() == 0);
        CHECK(pfDbfs(pfRms(rig.out(), 0, pfAt(30.0))) < -120.0);
        rig.play(0.0);
        rig.runTo(total);
        const size_t hit = pfAt(30.75);
        CHECK(pfDbfs(pfRms(rig.out(), pfAt(30.0), hit)) < -90.0);
        CHECK(pfDominant(rig.out(), hit, hit + 8820, {330.0, 165.0, 233.0, 311.0, 415.0, 660.0}) == 330.0);
        CHECK(pfDbfs(pfRms(rig.out(), hit, hit + 8820)) > -30.0);
    }
}

TEST_CASE("PatternFreezeMode entry 0.1 beat after a bar line", "[effects][pattern_freeze][layer4][freeze-held]") {
    // projectTimeMusic from 0.1: the span is valid at beat 0.5 and the hit at beat 1.5 (sample 30870) plays the onset.
    const size_t hit = 30870;
    const size_t total = hit + 8820 + 512;
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), pfFreshDefaults);
    rig.play(0.1);
    rig.runTo(total);
    const auto& wet = rig.out();
    const auto& in = rig.in();

    const double wetRms = pfRms(wet, hit, hit + 8820);
    const double inRms = pfRms(in, hit, hit + 8820);
    INFO("hit RMS " << pfDbfs(wetRms) << " dBFS, input " << pfDbfs(inRms) << " dBFS");
    CHECK(wetRms > 0.1 * inRms);

    // The slice is the input from the onset: over its plateau it correlates with the input that starts there.
    const size_t onset = pfFirstAbove(in, 0, total, 1e-3f);
    double best = 0.0;
    for (size_t lag = 0; lag <= 4; ++lag) {
        double num = 0.0;
        double ea = 0.0;
        double eb = 0.0;
        for (size_t k = 441; k < 4410; ++k) {
            const double w = wet[hit + k];
            const double x = in[onset + lag + k];
            num += w * x;
            ea += w * w;
            eb += x * x;
        }
        best = std::max(best, num / std::sqrt(std::max(ea * eb, 1e-20)));
    }
    INFO("correlation of the hit with the onset " << best);
    CHECK(best > 0.999);
}

TEST_CASE("PatternFreezeMode a Capture change mid-cycle dirties it", "[effects][pattern_freeze][layer4][freeze-held]") {
    // 1/1 -> 2/1 at beat 6 (beat 2 of the bar [4, 8), with the window captured at beat 4 in force): the 2/1
    // boundary at beat 8 does not capture, the one at beat 16 does.
    const size_t total = 352800 + 512;
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), pfFreshDefaults);
    rig.runTo(132300);
    REQUIRE(rig.mode().getCaptureCount() == 1);
    rig.mode().setCaptureLength(CaptureLength::DoubleWhole);
    rig.runTo(176400 + 512);
    CHECK(rig.mode().getCaptureCount() == 1);
    rig.runTo(total);
    CHECK(rig.mode().getCaptureCount() == 2);
}

TEST_CASE("PatternFreezeMode no capture before the onset", "[effects][pattern_freeze][layer4][freeze-held]") {
    const size_t total = pfAt(10.0);
    PfRig rig(pfConstant(total, 0.0f), pfFreshDefaults);
    rig.runTo(total);
    CHECK(rig.mode().hasWindow() == false);
    CHECK(rig.mode().getCaptureCount() == 0);
}

TEST_CASE("PatternFreezeMode a silent window triggers nothing", "[effects][pattern_freeze][layer4][freeze-held]") {
    // Music [0, 2 s), silence [2, 4 s), music from 4 s: over [4, 6 s) the window is the silent bar.
    const size_t total = pfAt(6.0) + kPfBlock;
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}, {4.0, 311.0, 0.5f}}), pfFreshDefaults);
    rig.runToSec(4.0);
    const auto before = rig.mode().getSliceTriggerCount();
    rig.runToSec(6.0);
    CHECK(rig.mode().getSliceTriggerCount() == before);
    // Not vacuous: the hit on the 6 s bar line reads the music of [4, 6 s).
    rig.runTo(total);
    CHECK(rig.mode().getSliceTriggerCount() > before);
}

TEST_CASE("PatternFreezeMode play after a silent stop stutters the new music, never the old or silence",
          "[effects][pattern_freeze][layer4][freeze-held]") {
    // Stopped (free-run): 233 Hz for 2 s, then zero input for two cycles; a play edge at projectTimeMusic 0.0 at
    // 6 s with 311 Hz from there.
    const size_t play = pfAt(6.0);
    const size_t total = play + pfAt(4.0);
    const auto input = pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}, {6.0, 311.0, 0.5f}});
    auto script = [play, total](PfRig& rig) {
        rig.stop();
        rig.runTo(play);
        rig.play(0.0);
        rig.runTo(play + kPfBlock);
        CHECK(rig.mode().hasWindow() == false);
        rig.runTo(total);
    };
    auto pair = pfRenderPair(input, pfFreshDefaults, script);

    // Insert 100: the wet (out - in) of bar 1 is silent until the first hit that follows the onset by needSpan.
    const auto outMinusIn = pfDiff(pair.insert.out(), pair.insert.in());
    const double gapDb = pfDbfs(pfRms(outMinusIn, play, play + pfAt(0.75)));
    INFO("out - in over [play, play + 0.75 s): " << gapDb << " dBFS");
    CHECK(gapDb < -90.0);

    // Every slice of bar 1 (steps 3 and 6) and of bar 2 reads the new music: 311 Hz, nothing at 233 Hz.
    const auto& wet = pair.wet.out();
    for (const double t : {0.75, 1.5, 2.0, 2.75, 3.5}) {
        const size_t a = play + pfAt(t);
        const double p311 = pfTonePower(wet, a, a + 8820, 311.0);
        const double p233 = pfTonePower(wet, a, a + 8820, 233.0);
        INFO("slice at play + " << t << " s: 311 Hz " << p311 << ", 233 Hz " << p233);
        CHECK(pfDominant(wet, a, a + 8820, {233.0, 311.0, 415.0}) == 311.0);
        CHECK(p233 < 1e-3 * p311);
    }
}

TEST_CASE("PatternFreezeMode play after music, without a silent stop",
          "[effects][pattern_freeze][layer4][freeze-held]") {
    // Stopped (free-run): 233 Hz for 2 s (the window in force is that music), zero input for 0.5 s, then a play
    // edge at projectTimeMusic 0.0 with 311 Hz: the play edge drops the 233 Hz window.
    const size_t play = pfAt(2.5);
    const size_t total = play + pfAt(4.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}, {2.5, 311.0, 0.5f}}), pfFreshDefaults);
    rig.stop();
    rig.runTo(play);
    REQUIRE(rig.mode().hasWindow());
    rig.play(0.0);
    rig.runTo(play + kPfBlock);
    CHECK(rig.mode().hasWindow() == false);
    rig.runTo(total);

    const auto& wet = rig.out();
    for (const double t : {0.75, 1.5, 2.0, 2.75, 3.5}) {
        const size_t a = play + pfAt(t);
        const double p311 = pfTonePower(wet, a, a + 8820, 311.0);
        const double p233 = pfTonePower(wet, a, a + 8820, 233.0);
        INFO("slice at play + " << t << " s: 311 Hz " << p311 << ", 233 Hz " << p233);
        CHECK(pfDominant(wet, a, a + 8820, {233.0, 311.0, 415.0}) == 311.0);
        CHECK(p233 < 1e-3 * p311);
    }
}

// =============================================================================
// D5: Hold (design 2.3)
// =============================================================================

TEST_CASE("PatternFreezeMode Hold freezes the window", "[effects][pattern_freeze][layer4][freeze-held]") {
    const size_t total = pfAt(6.5);
    PfRig rig(pfProbeTones(total), pfFreshDefaults);
    rig.runToSec(1.9);
    rig.mode().setHold(true);
    CHECK(rig.mode().isHeld());
    rig.runToSec(4.0);
    const auto held = rig.mode().getCaptureCount();
    CHECK(rig.mode().isHoldEngaged());
    rig.runTo(pfAt(4.0) + kPfBlock);
    CHECK(rig.mode().getCaptureCount() == held);
    rig.runToSec(5.0);
    rig.mode().setHold(false);
    rig.runToSec(6.0);
    const auto released = static_cast<long long>(rig.mode().getCaptureCount());
    rig.runTo(pfAt(6.0) + kPfBlock);
    CHECK(static_cast<long long>(rig.mode().getCaptureCount()) == released + 1);
    rig.runTo(total);

    CHECK(pfDominant(rig.out(), pfAt(4.5), pfAt(5.5), {233.0, 311.0, 415.0}) == 233.0);
    CHECK(pfDominant(rig.out(), pfAt(6.0), pfAt(6.2), {233.0, 311.0, 415.0, 523.0}) == 415.0);
}

TEST_CASE("PatternFreezeMode the release returns the input at once", "[effects][pattern_freeze][layer4][freeze-held]") {
    // Insert 100: Hold at 1.9 s, release at 5 s. From 5.14 s (the 100 ms release of the sounding slice plus 10 tau of
    // gh) to the 6 s capture the output is the input; no held-tone hit follows the release.
    const size_t total = pfAt(8.0);
    PfRig rig(pfProbeTones(total), pfInsertDefaults);
    rig.runToSec(1.9);
    rig.mode().setHold(true);
    rig.runToSec(5.0);
    rig.mode().setHold(false);
    rig.runTo(total);

    const double gap = pfMaxAbsDiff(rig.out(), rig.in(), pfAt(5.14), pfAt(6.0));
    INFO("max |out - in| over [5.14, 6 s): " << gap);
    CHECK(gap <= 1e-6);
    const auto outMinusIn = pfDiff(rig.out(), rig.in());
    const double p233 = pfTonePower(outMinusIn, pfAt(5.14), pfAt(8.0), 233.0);
    INFO("233 Hz power of out - in over [5.14, 8 s): " << p233 << " (input 0.25)");
    CHECK(p233 < 1e-6 * 0.25);
}

TEST_CASE("PatternFreezeMode Hold at 2/1 respects the release rule", "[effects][pattern_freeze][layer4][freeze-held]") {
    const size_t total = pfAt(12.0) + kPfBlock;
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setCaptureLength(CaptureLength::DoubleWhole);
    });
    rig.runToSec(3.9);
    rig.mode().setHold(true);
    rig.runTo(pfAt(4.0) + kPfBlock);
    CHECK(rig.mode().getCaptureCount() == 1);
    CHECK(rig.mode().isHoldEngaged());
    rig.runToSec(8.0);
    const auto at8 = rig.mode().getCaptureCount();
    rig.runTo(pfAt(8.0) + kPfBlock);
    CHECK(rig.mode().getCaptureCount() == at8);
    rig.runToSec(9.0);
    rig.mode().setHold(false);
    rig.runToSec(12.0);
    const auto at12 = static_cast<long long>(rig.mode().getCaptureCount());
    rig.runTo(total);
    CHECK(static_cast<long long>(rig.mode().getCaptureCount()) == at12 + 1);
}

TEST_CASE("PatternFreezeMode Hold before any window waits for the first capture",
          "[effects][pattern_freeze][layer4][freeze-held]") {
    const size_t total = pfAt(8.0) + kPfBlock;
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 311.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setHold(true);
    });
    rig.runToSec(2.0);
    CHECK(rig.mode().getCaptureCount() == 0);
    rig.runTo(pfAt(2.0) + kPfBlock);
    CHECK(rig.mode().getCaptureCount() == 1);
    rig.runTo(total);
    CHECK(rig.mode().getCaptureCount() == 1);

    const auto& wet = rig.out();
    CHECK(pfDbfs(pfRms(wet, 0, pfAt(0.75))) < -90.0);
    CHECK(pfRms(wet, pfAt(0.75), pfAt(0.95)) > 0.1 * kPfSineRms);
    const double p233 = pfTonePower(wet, pfAt(2.5), pfAt(8.0), 233.0);
    const double p311 = pfTonePower(wet, pfAt(2.5), pfAt(8.0), 311.0);
    INFO("wet after 2.5 s: 233 Hz " << p233 << ", 311 Hz " << p311);
    CHECK(p233 > 10.0 * p311);
}

TEST_CASE("PatternFreezeMode a restored Hold with 500 ms slices is audible",
          "[effects][pattern_freeze][layer4][freeze-held]") {
    const size_t total = pfAt(6.5);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 311.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setSliceLengthMs(500.0f);
        m.setHold(true);
    });
    rig.runTo(total);
    const auto& wet = rig.out();

    const double active = pfActiveFraction(wet, pfAt(2.0), pfAt(6.0), kPfSineRms);
    INFO("active fraction over [2, 6 s): " << active);
    CHECK(active > 0.8);
    for (const double t : {2.0, 2.75, 3.5, 4.0, 4.75, 5.5}) {
        INFO("hit at " << t << " s");
        CHECK(pfDominant(wet, pfAt(t), pfAt(t + 0.5), {233.0, 311.0}) == 233.0);
    }
}

TEST_CASE("PatternFreezeMode Hold sustains slices until the next hit", "[effects][pattern_freeze][layer4][freeze-held]") {
    // Insert 100 at the fresh defaults, Hold at 1.9 s (engaged at the 2 s capture): a continuous held bar.
    const size_t total = pfAt(6.0);
    PfRig rig(pfProbeTones(total), pfInsertDefaults);
    rig.runToSec(1.9);
    rig.mode().setHold(true);
    rig.runTo(total);
    const auto& out = rig.out();

    const double active = pfActiveFraction(out, pfAt(2.2), pfAt(6.0), kPfSineRms);
    INFO("active fraction of the output over [2.2, 6 s): " << active);
    CHECK(active >= 0.8);
    const double step = pfMaxStep(out, pfAt(2.2), pfAt(6.0));
    INFO("max sample step " << step << ", bar " << 2.0 * pfCleanStep(233.0));
    CHECK(step <= 2.0 * pfCleanStep(233.0));
}

TEST_CASE("PatternFreezeMode Hold waits on a silent window", "[effects][pattern_freeze][layer4][freeze-held]") {
    // Music [0, 2 s), silence [2, 4 s), music from 4 s; Hold on at 4.1 s, while the window is the silent bar.
    const size_t total = pfAt(6.0) + kPfBlock;
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}, {4.0, 311.0, 0.5f}}), pfInsertDefaults);
    rig.runToSec(4.1);
    rig.mode().setHold(true);
    while (rig.pos() < pfAt(6.0)) {
        INFO("block at " << rig.pos());
        CHECK(rig.mode().isHoldEngaged() == false);
        rig.runTo(std::min(rig.pos() + kPfBlock, pfAt(6.0)));
    }
    rig.runTo(total);
    CHECK(rig.mode().isHoldEngaged() == true);
    const double diff = pfMaxAbsDiff(rig.out(), rig.in(), pfAt(4.1), pfAt(6.0));
    INFO("max |out - in| over [4.1, 6 s): " << diff);
    CHECK(diff <= 1e-6);
}

// =============================================================================
// D6: Fixed / Variable (design 2.4)
// =============================================================================

TEST_CASE("PatternFreezeMode Fixed replays one slice, Variable plays the slice at the step's position",
          "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(6.0);
    PfRig fixed(pfFourToneBars(total), pfFreshDefaults);
    PfRig variable(pfFourToneBars(total), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setSliceMode(SliceMode::Variable);
    });
    fixed.runTo(total);
    variable.runTo(total);

    // Bar 2: the window is the first, partial one, so both modes read the onset.
    for (const double t : {2.0, 2.75, 3.5}) {
        INFO("bar 2 hit at " << t << " s");
        CHECK(pfDominant(fixed.out(), pfAt(t), pfAt(t + 0.2), {220.0, 330.0, 440.0, 550.0}) == 220.0);
        CHECK(pfDominant(variable.out(), pfAt(t), pfAt(t + 0.2), {220.0, 330.0, 440.0, 550.0}) == 220.0);
    }

    // Bar 3, Fixed: three sample-identical copies of the downbeat (plateau 10 ms .. 100 ms of each slice).
    const size_t h0 = pfAt(4.0);
    for (const double t : {4.75, 5.5}) {
        const size_t h = pfAt(t);
        double maxDiff = 0.0;
        for (size_t k = 441; k < 4410; ++k) {
            maxDiff = std::max(maxDiff, static_cast<double>(std::abs(fixed.out()[h0 + k] - fixed.out()[h + k])));
        }
        INFO("Fixed hit at " << t << " s against the downbeat hit: max diff " << maxDiff);
        CHECK(maxDiff < 1e-6);
        CHECK(pfDominant(fixed.out(), h, h + 8820, {220.0, 330.0, 440.0, 550.0}) == 220.0);
    }

    // Bar 3, Variable: steps 0, 3 and 6 read the beat under them in the held bar.
    CHECK(pfDominant(variable.out(), pfAt(4.0), pfAt(4.2), {220.0, 330.0, 440.0, 550.0}) == 220.0);
    CHECK(pfDominant(variable.out(), pfAt(4.75), pfAt(4.95), {220.0, 330.0, 440.0, 550.0}) == 330.0);
    CHECK(pfDominant(variable.out(), pfAt(5.5), pfAt(5.7), {220.0, 330.0, 440.0, 550.0}) == 550.0);
}

TEST_CASE("PatternFreezeMode Euclidean hits fire in no other pattern type", "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(4.0);
    auto granular = [](int hits) {
        return [hits](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::GranularScatter);
            m.setEuclideanHits(hits);
        };
    };
    PfRig three(pfProbeTones(total), granular(3));
    PfRig none(pfProbeTones(total), granular(0));
    three.runTo(total);
    none.runTo(total);
    CHECK(pfMaxAbsDiff(three.out(), none.out(), 0, total) == 0.0);
    CHECK(pfMaxAbsDiff(three.outR(), none.outR(), 0, total) == 0.0);
}

// =============================================================================
// D7: Granular Scatter (design 2.5.2)
// =============================================================================

TEST_CASE("PatternFreezeMode Granular density averages the set rate within 20 %", "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(20.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::GranularScatter);
        m.setGranularDensity(10.0f);
    });
    rig.runTo(total);
    const auto grains = static_cast<long long>(rig.mode().getGrainTriggerCount());
    INFO("grains in 20 s at 10 Hz: " << grains);
    CHECK(grains >= 160);
    CHECK(grains <= 240);
}

TEST_CASE("PatternFreezeMode Position Jitter 0 scatters inside a Slice Length region",
          "[effects][pattern_freeze][layer4]") {
    // Fixed, Slice Length 200 ms, jitter 0, a steady tone that starts at full level (cosine), so the onset is
    // sample 0. 8-sample blocks attribute every grain to its block; a block that triggers two is skipped.
    const size_t total = pfAt(30.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}, kPfTwoPi / 4.0), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::GranularScatter);
        m.setGranularPositionJitter(0.0f);
        m.setGranularDensity(20.0f);
    });
    const size_t thirdCapture = pfAt(6.0);
    size_t recorded = 0;
    while (rig.pos() < total && recorded < 400) {
        const size_t start = rig.pos();
        const auto before = static_cast<long long>(rig.mode().getGrainTriggerCount());
        rig.processBlock(8);
        const auto after = static_cast<long long>(rig.mode().getGrainTriggerCount());
        if (after - before != 1) {
            continue;
        }
        ++recorded;
        const auto s = static_cast<long long>(rig.mode().getLastGrainStart());
        INFO("grain triggered at sample " << start << " starts at cycle offset " << s);
        if (start >= thirdCapture) {
            CHECK(s >= -4410);
            CHECK(s < 4410);
        } else {
            CHECK(s >= 0);
            CHECK(s < 8820);
        }
    }
    CHECK(recorded == 400);
}

TEST_CASE("PatternFreezeMode Position Jitter widens the region to the whole window, and no grain starts in the last grain length",
          "[effects][pattern_freeze][layer4]") {
    // A 2 s window held from 4.1 s (the complete bar [2, 4 s)); the held boundary at 6 s clears the contiguity, so
    // from 6 s on the readable range is the window itself, [0, 88200). Size Jitter 0: every grain is 4410 samples,
    // so no start may lie past hi - grainLength = 83790.
    constexpr long long kHiMinusGrain = 88200 - 4410;
    for (const float jitter : {0.0f, 0.5f, 1.0f}) {
        INFO("position jitter " << jitter);
        const size_t total = pfAt(20.0);
        PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}, kPfTwoPi / 4.0), [jitter](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::GranularScatter);
            m.setGranularPositionJitter(jitter);
            m.setGranularSizeJitter(0.0f);
            m.setGranularGrainSize(100.0f);
            m.setGranularDensity(50.0f);
        });
        rig.runToSec(4.1);
        rig.mode().setHold(true);
        rig.runToSec(6.0);
        std::vector<double> starts;
        while (rig.pos() < total && starts.size() < 400) {
            const auto before = static_cast<long long>(rig.mode().getGrainTriggerCount());
            rig.processBlock(8);
            if (static_cast<long long>(rig.mode().getGrainTriggerCount()) - before == 1) {
                starts.push_back(static_cast<double>(rig.mode().getLastGrainStart()));
            }
        }
        REQUIRE(starts.size() == 400);
        const auto [lo, hi] = std::minmax_element(starts.begin(), starts.end());
        CHECK(*lo >= 0.0);
        CHECK(*hi <= static_cast<double>(kHiMinusGrain));
        if (jitter == 0.5f) {
            const double mean = std::accumulate(starts.begin(), starts.end(), 0.0) / 400.0;
            double var = 0.0;
            for (const double s : starts) {
                var += (s - mean) * (s - mean);
            }
            const double sd = std::sqrt(var / 400.0);
            const double expected = 1.1 * kPfSr / std::sqrt(12.0);  // R = 0.2 + 0.5 x 1.8 = 1.1 s
            INFO("start standard deviation " << sd << " samples, expected " << expected);
            CHECK(sd >= 0.85 * expected);
            CHECK(sd <= 1.15 * expected);
        }
        if (jitter == 1.0f) {
            INFO("starts span [" << *lo << ", " << *hi << "]");
            CHECK(*lo <= 0.05 * 88200.0);
            CHECK(*hi >= static_cast<double>(kHiMinusGrain) - 0.05 * 88200.0);
        }
    }
}

TEST_CASE("PatternFreezeMode no grain splices under its envelope", "[effects][pattern_freeze][layer4]") {
    // Granular defaults; 233 Hz for one bar, then 311 Hz, phase-continuous at the bar line.
    const size_t total = pfAt(8.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 311.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::GranularScatter);
    });
    rig.runTo(total);
    const double step = pfMaxStep(rig.out(), 0, total);
    INFO("max sample step " << step << ", bar " << 2.0 * pfCleanStep(311.0));
    CHECK(step <= 2.0 * pfCleanStep(311.0));
}

// =============================================================================
// D8: Harmonic Drones (design 2.5.3)
// =============================================================================

TEST_CASE("PatternFreezeMode Drones are in tune and independent of the block size",
          "[effects][pattern_freeze][layer4][freeze-drone]") {
    const size_t total = pfAt(6.0);
    auto drones = [](float drift) {
        return [drift](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::HarmonicDrones);
            m.setDroneVoiceCount(2);
            m.setDroneInterval(PitchInterval::Octave);
            m.setDroneDrift(drift);
        };
    };

    SECTION("the upper voice sits an octave above the root at the root's level (Drift 0)") {
        PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), drones(0.0f));
        rig.runTo(total);
        // R12: each peak is found within +/- 10 cents in 0.5-cent steps; the upper voice within 3 cents of 466 Hz.
        const PfPeak root = pfPeakSearch(rig.out(), pfAt(3.0), pfAt(6.0), 233.0);
        const PfPeak upper = pfPeakSearch(rig.out(), pfAt(3.0), pfAt(6.0), 466.0);
        const double db = 10.0 * std::log10(std::max(upper.power, 1e-30) / std::max(root.power, 1e-30));
        std::printf("Drones in tune: root peak %+.1f cents re 233 Hz, upper peak %+.1f cents re 466 Hz at %+.2f dB "
                    "re the root\n",
                    root.cents, upper.cents, db);
        INFO("upper voice peak " << upper.cents << " cents re 466 Hz, " << db << " dB re the 233 Hz peak");
        CHECK(std::abs(upper.cents) <= 3.0);
        CHECK(std::abs(db) <= 1.5);
    }

    SECTION("64- and 512-sample blocks render the same drone (Drift 30 %)") {
        PfRig small(pfTones(total, {{0.0, 233.0, 0.5f}}), drones(PatternFreezeConstants::kDefaultDroneDrift));
        PfRig large(pfTones(total, {{0.0, 233.0, 0.5f}}), drones(PatternFreezeConstants::kDefaultDroneDrift));
        small.runTo(total, 64);
        large.runTo(total, 512);
        const double ratio = pfRms(pfDiff(small.out(), large.out()), 0, total) / pfRms(large.out(), 0, total);
        INFO("RMS(diff 64 vs 512) / RMS: " << ratio);
        CHECK(ratio < 1e-3);
    }
}

TEST_CASE("PatternFreezeMode the drone level is steady", "[effects][pattern_freeze][layer4][freeze-drone]") {
    // Octave, Drift 0, Fixed, Voices 1 and 2, a stationary window tone at every 1 Hz from 200 to 260 Hz plus 311 Hz.
    const size_t total = pfAt(6.0);
    std::vector<double> tones;
    for (int f = 200; f <= 260; ++f) {
        tones.push_back(static_cast<double>(f));
    }
    tones.push_back(311.0);
    for (const int voices : {1, 2}) {
        for (const double f : tones) {
            PfRig rig(pfTones(total, {{0.0, f, 0.5f}}), [voices](PatternFreezeMode& m) {
                pfFreshDefaults(m);
                m.setPatternType(PatternType::HarmonicDrones);
                m.setDroneVoiceCount(voices);
                m.setDroneInterval(PitchInterval::Octave);
                m.setDroneDrift(0.0f);
            });
            rig.runTo(total);
            const double depth = pfDepthMaxMinDb(rig.out(), pfAt(3.0), pfAt(6.0));
            const double level = pfDbfs(pfRms(rig.out(), pfAt(3.0), pfAt(6.0))) -
                                 pfDbfs(pfRms(rig.in(), pfAt(3.0), pfAt(6.0)));
            INFO("voices " << voices << ", window tone " << f << " Hz: depth " << depth << " dB, level " << level
                           << " dB");
            CHECK(depth <= 3.0);
            CHECK(std::abs(level) <= 2.0);
        }
    }
}

TEST_CASE("PatternFreezeMode Drones on a drum window", "[effects][pattern_freeze][layer4][freeze-drone]") {
    // The D8 drum bar, repeated: per beat a kick (60 Hz, 80 ms exponential decay, peak 0.8), on the off-beat a hat
    // (white noise, 20 ms decay, peak 0.25), under a 233 Hz pad at 0.05.
    const size_t total = pfAt(6.0);
    std::vector<float> drum(total, 0.0f);
    uint32_t noise = 0x1234567u;
    for (size_t i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / kPfSr;
        const double inBeat = std::fmod(t, 0.5);
        const double kick = 0.8 * std::exp(-inBeat / 0.080) * std::sin(kPfTwoPi * 60.0 * inBeat);
        noise = noise * 1664525u + 1013904223u;
        const double white = static_cast<double>(noise >> 8) / 8388608.0 - 1.0;
        const double hat = inBeat >= 0.25 ? 0.25 * std::exp(-(inBeat - 0.25) / 0.020) * white : 0.0;
        const double pad = 0.05 * std::sin(kPfTwoPi * 233.0 * t);
        drum[i] = static_cast<float>(kick + hat + pad);
    }
    PfRig rig(drum, [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::HarmonicDrones);
        m.setDroneVoiceCount(1);
        m.setDroneDrift(0.0f);
    });
    rig.runTo(total);
    const double droneDepth = pfDepthP95P5Db(rig.out(), pfAt(3.0), pfAt(6.0));
    const double windowDepth = pfDepthP95P5Db(drum, pfAt(2.0), pfAt(4.0));
    std::printf("Drones on a drum window: drone p95/p5 depth %.1f dB, drum window %.1f dB, difference %.1f dB\n",
                droneDepth, windowDepth, windowDepth - droneDepth);
    INFO("drone depth " << droneDepth << " dB, drum window depth " << windowDepth << " dB");
    CHECK(droneDepth <= windowDepth - 6.0);
}

TEST_CASE("PatternFreezeMode no seam click at Attack 0", "[effects][pattern_freeze][layer4][freeze-drone]") {
    const size_t total = pfAt(6.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::HarmonicDrones);
        m.setDroneVoiceCount(2);
        m.setDroneInterval(PitchInterval::Octave);
        m.setDroneDrift(0.0f);
        m.setEnvelopeAttackMs(0.0f);
    });
    rig.runTo(total);
    // The ideal two-tone sum: 233 and 466 Hz, each 0.5 / sqrt(2).
    const double ideal = (0.5 / std::sqrt(2.0)) * kPfTwoPi * (233.0 + 466.0) / kPfSr;
    const double step = pfMaxStep(rig.out(), 0, total);
    INFO("max sample step " << step << ", ideal two-tone maximum " << ideal);
    CHECK(step <= 2.0 * ideal);
}

TEST_CASE("PatternFreezeMode re-anchoring never clicks", "[effects][pattern_freeze][layer4][freeze-drone]") {
    // Variable, Capture 1/4 (a re-capture every beat) on the four-tone bar of D6.
    const size_t total = pfAt(8.0);
    PfRig rig(pfFourToneBars(total), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::HarmonicDrones);
        m.setSliceMode(SliceMode::Variable);
        m.setCaptureLength(CaptureLength::Quarter);
    });
    rig.runTo(total);
    // R13: the ideal output is the root plus the upper voice at its exact interval of the window's tone (Octave, 2
    // voices, at the default Drift's widest detune), at the designed voice level; the window's highest tone is 550 Hz.
    const double a = pfDroneVoiceAmp(PatternFreezeConstants::kDefaultDroneVoices);
    const double maxCents = 50.0 * static_cast<double>(PatternFreezeConstants::kDefaultDroneDrift);
    const double ideal = pfIdealStep({{a, 550.0}, {a, pfDetune(1100.0, maxCents)}});
    const double step = pfMaxStep(rig.out(), 0, total);
    const double peak = pfPeak(rig.out(), 0, total);
    std::printf("re-anchoring never clicks: max step %.4f, ideal %.4f (bar %.4f), peak %.4f\n", step, ideal,
                2.0 * ideal, peak);
    INFO("max sample step " << step << ", ideal output's largest step " << ideal << ", peak " << peak);
    CHECK(peak > 0.0);
    CHECK(step <= 2.0 * ideal);
}

TEST_CASE("PatternFreezeMode Drift 100 % swings the upper voice +/- 50 cents",
          "[effects][pattern_freeze][layer4][freeze-drone]") {
    const size_t total = pfAt(7.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::HarmonicDrones);
        m.setDroneVoiceCount(2);
        m.setDroneInterval(PitchInterval::Octave);
        m.setDroneDrift(1.0f);
        m.setDroneDriftRate(0.5f);
    });
    rig.runTo(total);

    // Each component through two cascaded Biquad bandpasses, Q 8, then zero crossings over 4 s.
    auto isolate = [&rig, total](float hz) {
        Biquad a;
        Biquad b;
        a.configure(FilterType::Bandpass, hz, 8.0f, 0.0f, static_cast<float>(kPfSr));
        b.configure(FilterType::Bandpass, hz, 8.0f, 0.0f, static_cast<float>(kPfSr));
        std::vector<float> y(total);
        for (size_t i = 0; i < total; ++i) {
            y[i] = b.process(a.process(rig.out()[i]));
        }
        return y;
    };
    const auto upper = pfZeroCrossingHz(isolate(466.0f), pfAt(3.0), pfAt(7.0), 8);
    const auto root = pfZeroCrossingHz(isolate(233.0f), pfAt(3.0), pfAt(7.0), 8);
    REQUIRE_FALSE(upper.empty());
    REQUIRE_FALSE(root.empty());

    const auto [uLo, uHi] = std::minmax_element(upper.begin(), upper.end());
    const double upperMin = pfCents(*uLo, 466.0);
    const double upperMax = pfCents(*uHi, 466.0);
    INFO("upper voice spans " << upperMin << " .. " << upperMax << " cents re 466 Hz");
    CHECK(upperMax == Approx(50.0).margin(10.0));
    CHECK(upperMin == Approx(-50.0).margin(10.0));

    const auto [rLo, rHi] = std::minmax_element(root.begin(), root.end());
    INFO("root spans " << pfCents(*rLo, 233.0) << " .. " << pfCents(*rHi, 233.0) << " cents re 233 Hz");
    CHECK(std::abs(pfCents(*rLo, 233.0)) <= 2.0);
    CHECK(std::abs(pfCents(*rHi, 233.0)) <= 2.0);
}

TEST_CASE("PatternFreezeMode Octave x 4 voices folds the top voice down", "[effects][pattern_freeze][layer4][freeze-drone]") {
    const size_t total = pfAt(6.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::HarmonicDrones);
        m.setDroneVoiceCount(4);
        m.setDroneInterval(PitchInterval::Octave);
        m.setDroneDrift(0.0f);
    });
    rig.runTo(total);
    // R12: every voice's peak is found within +/- 10 cents in 0.5-cent steps and lies within 3 cents of its interval.
    const PfPeak root = pfPeakSearch(rig.out(), pfAt(3.0), pfAt(6.0), 233.0);
    const double p233 = root.power;
    INFO("root peak " << root.cents << " cents re 233 Hz");
    CHECK(std::abs(root.cents) <= 3.0);
    for (const double f : {116.5, 466.0, 932.0}) {
        const PfPeak voice = pfPeakSearch(rig.out(), pfAt(3.0), pfAt(6.0), f);
        const double db = 10.0 * std::log10(std::max(voice.power, 1e-30) / std::max(p233, 1e-30));
        std::printf("Octave x 4: %.1f Hz voice peak %+.1f cents at %+.2f dB re 233 Hz\n", f, voice.cents, db);
        INFO(f << " Hz voice: peak " << voice.cents << " cents, " << db << " dB re 233 Hz");
        CHECK(std::abs(voice.cents) <= 3.0);
        CHECK(std::abs(db) <= 3.0);
    }
    const double p1864 = pfTonePower(rig.out(), pfAt(3.0), pfAt(6.0), 1864.0);
    INFO("1864 Hz power " << p1864 << ", 233 Hz power " << p233);
    CHECK(p1864 < 1e-3 * p233);
}

TEST_CASE("PatternFreezeMode a Unison voice never reaches the shifter's pass-through",
          "[effects][pattern_freeze][layer4][freeze-drone]") {
    // Unison, 2 voices; Drift automated 0 -> 100 % -> 0 in 2 s ramps over 10 s.
    const size_t total = pfAt(10.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::HarmonicDrones);
        m.setDroneVoiceCount(2);
        m.setDroneInterval(PitchInterval::Unison);
        m.setDroneDrift(0.0f);
    });
    rig.runEach(total, kPfBlock, [](PatternFreezeMode& m, size_t start) {
        const double phase = std::fmod(static_cast<double>(start) / kPfSr, 4.0);
        m.setDroneDrift(static_cast<float>(phase < 2.0 ? phase / 2.0 : (4.0 - phase) / 2.0));
    });
    // R13: the ideal output is the root plus the Unison voice at its current detune, at the designed voice level;
    // the widest detune (Drift 100 %) is 1 + 49 = 50 cents (design 2.5.3).
    const double a = pfDroneVoiceAmp(2);
    const double ideal = pfIdealStep({{a, 233.0}, {a, pfDetune(233.0, 50.0)}});
    const double step = pfMaxStep(rig.out(), 0, total);
    const double peak = pfPeak(rig.out(), 0, total);
    std::printf("Unison never reaches the pass-through: max step %.4f, ideal %.4f (bar %.4f), peak %.4f\n", step,
                ideal, 2.0 * ideal, peak);
    INFO("max sample step " << step << ", ideal output's largest step " << ideal << ", peak " << peak);
    CHECK(peak > 0.0);
    CHECK(step <= 2.0 * ideal);
}

// =============================================================================
// D9: Noise Bursts (design 2.5.4)
// =============================================================================

TEST_CASE("PatternFreezeMode Noise-only bursts follow the song", "[effects][pattern_freeze][layer4]") {
    auto noise = [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::NoiseBursts);
    };

    SECTION("transport playing: noise-only bursts go on after the music") {
        const size_t total = pfAt(12.0);
        PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}}), noise);
        rig.runTo(total);
        const double db = pfDbfs(pfRms(rig.out(), pfAt(4.5), pfAt(12.0)));
        const double active = pfActiveFraction(rig.out(), pfAt(4.5), pfAt(12.0), kPfSineRms);
        INFO("wet over [4.5, 12 s): " << db << " dBFS, active " << active);
        CHECK(db >= -30.0);
        CHECK(db <= -14.0);
        CHECK(active == Approx(0.80).margin(0.04));
    }

    SECTION("transport stopped: silent two cycles after the last input") {
        const size_t total = pfAt(12.0);
        PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}}), noise);
        rig.stop();
        rig.runTo(total);
        const double db = pfDbfs(pfRms(rig.out(), pfAt(6.5), pfAt(12.0)));
        INFO("wet over [6.5, 12 s): " << db << " dBFS");
        CHECK(db < -90.0);
    }

    SECTION("from reset with zero input and the transport playing: nothing") {
        const size_t total = pfAt(10.0);
        PfRig rig(pfConstant(total, 0.0f), noise);
        rig.runTo(total);
        const double db = pfDbfs(pfRms(rig.out(), 0, total));
        INFO("wet over [0, 10 s): " << db << " dBFS");
        CHECK(db < -90.0);
    }
}

TEST_CASE("PatternFreezeMode Noise Bursts play the slice forward and block-size independent",
          "[effects][pattern_freeze][layer4]") {
    // A chirp per bar, 300 Hz rising 3000 Hz per second: a forward read of the downbeat rises within each burst.
    const size_t total = pfAt(6.0);
    std::vector<float> chirp(total);
    for (size_t i = 0; i < total; ++i) {
        const double tb = std::fmod(static_cast<double>(i) / kPfSr, 2.0);
        chirp[i] = static_cast<float>(0.5 * std::sin(kPfTwoPi * (300.0 * tb + 1500.0 * tb * tb)));
    }

    SECTION("the zero-crossing rate rises within each burst") {
        // The noise is filtered to its lowest cutoff, so the zero crossings are the slice's.
        PfRig rig(chirp, [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::NoiseBursts);
            m.setNoiseFilterCutoff(PatternFreezeConstants::kMinNoiseFilterCutoffHz);
            m.setNoiseFilterSweep(0.0f);
        });
        rig.runTo(total);
        auto crossings = [&rig](size_t a, size_t b) {
            int n = 0;
            for (size_t i = a + 1; i < b; ++i) {
                n += ((rig.out()[i - 1] < 0.0f) != (rig.out()[i] < 0.0f)) ? 1 : 0;
            }
            return n;
        };
        for (int k = 0; k < 8; ++k) {
            const double b = 4.0 + 0.25 * static_cast<double>(k);
            const int first = crossings(pfAt(b + 0.010), pfAt(b + 0.055));
            const int second = crossings(pfAt(b + 0.055), pfAt(b + 0.100));
            INFO("burst at " << b << " s: " << first << " then " << second << " zero crossings");
            CHECK(second > first);
        }
    }

    SECTION("64- and 512-sample blocks render the same bursts") {
        auto noise = [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::NoiseBursts);
        };
        PfRig small(chirp, noise);
        PfRig large(chirp, noise);
        small.runTo(total, 64);
        large.runTo(total, 512);
        const double ratio = pfRms(pfDiff(small.out(), large.out()), 0, total) / pfRms(large.out(), 0, total);
        INFO("RMS(diff 64 vs 512) / RMS: " << ratio);
        CHECK(ratio < 1e-3);
    }
}

TEST_CASE("PatternFreezeMode every colour plays at kNoiseLevelDb", "[effects][pattern_freeze][layer4]") {
    // One 10 ms click at 0.5 at t = 0 (it sets heard_), then zero input, the transport playing; LowPass at its
    // maximum cutoff, Sweep 0: from 4 s the bursts are noise only.
    const size_t total = pfAt(8.0);
    const NoiseType types[8] = {NoiseType::White, NoiseType::Pink,  NoiseType::Brown,  NoiseType::Blue,
                                NoiseType::Violet, NoiseType::Grey, NoiseType::Velvet, NoiseType::RadioStatic};
    double wet[8] = {};
    double ref[8] = {};
    for (int c = 0; c < 8; ++c) {
        auto input = pfConstant(total, 0.0f);
        std::fill(input.begin(), input.begin() + 441, 0.5f);
        PfRig rig(input, [c](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::NoiseBursts);
            m.setNoiseColor(static_cast<NoiseColor>(c));
            m.setNoiseFilterType(FilterType::Lowpass);
            m.setNoiseFilterCutoff(PatternFreezeConstants::kMaxNoiseFilterCutoffHz);
            m.setNoiseFilterSweep(0.0f);
        });
        rig.runTo(total);
        wet[c] = pfRms(rig.out(), pfAt(4.5), total);

        NoiseGenerator gen;
        gen.prepare(static_cast<float>(kPfSr), kPfBlock);
        for (size_t t = 0; t < kNumNoiseTypes; ++t) {
            gen.setNoiseEnabled(static_cast<NoiseType>(t), false);
        }
        gen.setNoiseLevel(types[c], PatternFreezeConstants::kNoiseLevelDb);  // R14: the constant, not a literal
        gen.setNoiseEnabled(types[c], true);
        std::vector<float> buf(total);
        for (size_t s = 0; s < total; s += kPfBlock) {
            gen.process(&buf[s], std::min(kPfBlock, total - s));
        }
        ref[c] = pfRms(buf, pfAt(4.5), total);
    }
    for (int c = 1; c < 8; ++c) {
        const double wetDb = pfDbfs(wet[c]) - pfDbfs(wet[0]);
        const double refDb = pfDbfs(ref[c]) - pfDbfs(ref[0]);
        INFO("colour " << c << ": " << wetDb << " dB re White, the generator alone " << refDb << " dB");
        CHECK(std::abs(wetDb - refDb) <= 1.0);
    }
}

TEST_CASE("PatternFreezeMode overlapping bursts do not click", "[effects][pattern_freeze][layer4]") {
    // The Radio Static rate: Burst Rate 1/16, Slice Length 150 ms, so consecutive bursts overlap.
    const size_t total = pfAt(8.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::NoiseBursts);
        m.setNoiseBurstRate(NoteValue::Sixteenth, NoteModifier::None);
        m.setSliceLengthMs(150.0f);
    });
    rig.runTo(total);
    const double step = pfMaxStep(rig.out(), 0, total);
    const double peak = pfPeak(rig.out(), 0, total);
    INFO("max sample step " << step << ", peak " << peak);
    CHECK(peak > 0.0);
    CHECK(step < 0.05 * peak);
}

TEST_CASE("PatternFreezeMode a capture during a burst does not switch it", "[effects][pattern_freeze][layer4]") {
    // Slice Length 2000 ms, Burst Rate 1/1, Capture 1/4 (four captures during each burst), a new tone every beat.
    // A burst copies its slice at its trigger, so within it only the audio readable then (the window and the
    // contiguous cycle before it) is heard, never a tone recorded after the trigger.
    const size_t total = pfAt(8.0);
    const double tones[8] = {220.0, 277.0, 330.0, 392.0, 466.0, 554.0, 659.0, 784.0};
    PfRig rig(pfBeatTones(total, {220.0, 277.0, 330.0, 392.0, 466.0, 554.0, 659.0, 784.0}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::NoiseBursts);
        m.setSliceLengthMs(2000.0f);
        m.setNoiseBurstRate(NoteValue::Whole, NoteModifier::None);
        m.setCaptureLength(CaptureLength::Quarter);
    });
    rig.runTo(total);
    for (const int beat : {8, 12}) {
        const double b = 0.5 * static_cast<double>(beat);
        const double window = tones[(beat - 1) % 8];
        const double before = tones[(beat - 2) % 8];
        for (const auto& seg : {std::pair{0.05, 0.5}, std::pair{0.5, 1.0}, std::pair{1.0, 1.5}, std::pair{1.5, 1.85}}) {
            const size_t a = pfAt(b + seg.first);
            const size_t e = pfAt(b + seg.second);
            const double dominant =
                pfDominant(rig.out(), a, e, {220.0, 277.0, 330.0, 392.0, 466.0, 554.0, 659.0, 784.0});
            INFO("burst at beat " << beat << ", [" << seg.first << ", " << seg.second << ") s in: dominant "
                                  << dominant << " Hz, trigger-time window " << window << " Hz");
            CHECK((dominant == window || dominant == before));
        }
    }
}

TEST_CASE("PatternFreezeMode a held burst sustains", "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(6.0);
    PfRig rig(pfProbeTones(total), [](PatternFreezeMode& m) {
        pfInsertDefaults(m);
        m.setPatternType(PatternType::NoiseBursts);
    });
    rig.runToSec(1.9);
    rig.mode().setHold(true);
    rig.runTo(total);
    const double active = pfActiveFraction(rig.out(), pfAt(2.2), pfAt(6.0), kPfSineRms);
    INFO("active fraction of the output over [2.2, 6 s): " << active);
    CHECK(active >= 0.8);
}

// =============================================================================
// D10: the Dry law (design 2.7)
// =============================================================================

TEST_CASE("PatternFreezeMode Insert ducks the dry only while a voice plays", "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(6.0);
    auto pair = pfRenderPair(pfProbeTones(total), pfFreshDefaults, [total](PfRig& rig) { rig.runTo(total); });
    const auto& out = pair.insert.out();
    const auto& in = pair.insert.in();
    const auto& wet = pair.wet.out();
    const std::vector<double> hits = {2.0, 2.75, 3.5, 4.0, 4.75, 5.5, 6.0};
    for (size_t h = 0; h + 1 < hits.size(); ++h) {
        // Gap: 10 tau of gv (4 ms) after the 200 ms slice ended, up to the next hit.
        const size_t gapFrom = pfAt(hits[h] + 0.204);
        const size_t gapTo = pfAt(hits[h + 1]);
        INFO("gap after the hit at " << hits[h] << " s");
        CHECK(pfMaxAbsDiff(out, in, gapFrom, gapTo) <= 1e-6);
        // Plateau: 7 tau of gv (2.8 ms) after the 10 ms attack, up to the 100 ms release.
        const size_t plFrom = pfAt(hits[h] + 0.0128);
        const size_t plTo = pfAt(hits[h] + 0.100);
        const double share = pfEnergy(pfDiff(out, wet), plFrom, plTo) / pfEnergy(in, plFrom, plTo);
        INFO("input share over the plateau of the hit at " << hits[h] << " s: " << 10.0 * std::log10(share + 1e-30)
                                                            << " dB");
        CHECK(share < 1e-6);
    }
}

TEST_CASE("PatternFreezeMode Dry = Mix is the crossfade", "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(6.0);
    PfRig wet(pfProbeTones(total), pfFreshDefaults);
    PfRig mixed(pfProbeTones(total), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setDryWetMix(30.0f);
    });
    wet.runTo(total);
    mixed.runTo(total);
    double maxErr = 0.0;
    for (size_t i = 0; i < total; ++i) {
        const double expected = static_cast<double>(mixed.in()[i]) * 0.7 + static_cast<double>(wet.out()[i]) * 0.3;
        maxErr = std::max(maxErr, std::abs(static_cast<double>(mixed.out()[i]) - expected));
    }
    INFO("max |out - (in 0.7 + wet 0.3)| " << maxErr);
    CHECK(maxErr <= 1e-6);
}

TEST_CASE("PatternFreezeMode a Dry Mode toggle never steps the dry gain", "[effects][pattern_freeze][layer4]") {
    // Constant 0.5 input, Mix 100 %, no window: wet is 0, so out / 0.5 is the dry gain (0 under Mix, 1 under Insert)
    PatternFreezeMode m;
    m.prepare(44100.0, 64, 5000.0f);
    pfFreshDefaults(m);
    m.setDryMode(DryMode::Mix);
    m.snapParameters();
    OnePoleSmoother ref;
    ref.configure(20.0f, 44100.0f);
    ref.snapTo(0.0f);
    ref.setTarget(1.0f);
    const double firstStep = static_cast<double>(ref.process());

    std::vector<float> l(64), r(64);
    std::vector<double> gain;
    const BlockContext ctx = pfCreateContext();
    for (int b = 0; b < 192; ++b) {
        m.setDryMode(b >= 64 && b < 128 ? DryMode::Insert : DryMode::Mix);
        std::fill(l.begin(), l.end(), 0.5f);
        std::fill(r.begin(), r.end(), 0.5f);
        m.process(l.data(), r.data(), 64, ctx);
        for (size_t s = 0; s < 64; ++s) {
            gain.push_back(static_cast<double>(l[s]) / 0.5);
        }
    }
    double maxStep = 0.0;
    for (size_t i = 1; i < gain.size(); ++i) {
        maxStep = std::max(maxStep, std::abs(gain[i] - gain[i - 1]));
    }
    INFO("largest per-sample dry-gain step " << maxStep << " against a 20 ms smoother's first step " << firstStep);
    CHECK(maxStep <= firstStep * 1.001);
    CHECK(gain[64 * 127] > 0.99);  // the Insert law arrives
    CHECK(gain.back() < 0.01);     // and the Mix law returns
}

TEST_CASE("PatternFreezeMode Hold mutes the input under Insert", "[effects][pattern_freeze][layer4]") {
    // Hold on at 1.9 s (engaged at the 2 s capture), off at 4.5 s (the release gap runs to the 6 s capture).
    const size_t total = pfAt(6.0);
    auto pair = pfRenderPair(pfProbeTones(total), pfFreshDefaults, [total](PfRig& rig) {
        rig.runToSec(1.9);
        rig.mode().setHold(true);
        rig.runToSec(4.5);
        rig.mode().setHold(false);
        rig.runTo(total);
    });
    const auto& out = pair.insert.out();
    const auto& in = pair.insert.in();
    const auto share = pfDiff(out, pair.wet.out());
    for (size_t s = pfAt(2.028); s + kPfWin <= pfAt(4.5); s += kPfWin) {
        const double ratio = pfEnergy(share, s, s + kPfWin) / pfEnergy(in, s, s + kPfWin);
        INFO("input share in the 10 ms window at " << static_cast<double>(s) / kPfSr << " s: "
                                                    << 10.0 * std::log10(ratio + 1e-30) << " dB");
        CHECK(ratio < 1e-6);
    }
    const double released = pfMaxAbsDiff(out, in, pfAt(4.64), pfAt(6.0));
    INFO("max |out - in| over [4.64, 6 s): " << released);
    CHECK(released <= 1e-6);
}

TEST_CASE("PatternFreezeMode Granular leaves the dry alone under Insert", "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(6.0);
    auto granular = [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::GranularScatter);
    };
    auto pair = pfRenderPair(pfProbeTones(total), granular, [total](PfRig& rig) { rig.runTo(total); });
    double maxErr = 0.0;
    for (size_t i = 0; i < total; ++i) {
        const double dry = static_cast<double>(pair.insert.out()[i]) - static_cast<double>(pair.wet.out()[i]);
        maxErr = std::max(maxErr, std::abs(dry - static_cast<double>(pair.insert.in()[i])));
    }
    INFO("max |out - wet - in| " << maxErr);
    CHECK(maxErr <= 1e-6);
}

TEST_CASE("PatternFreezeMode the duck never steps at Attack 0", "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(6.0);
    auto attack0 = [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setEnvelopeAttackMs(0.0f);
    };
    auto pair = pfRenderPair(pfConstant(total, 0.5f), attack0, [total](PfRig& rig) { rig.runTo(total); });
    const double bound = 1.0 - std::exp(-2500.0 / kPfSr) + 1e-4;
    double maxStep = 0.0;
    double prev = (static_cast<double>(pair.insert.out()[0]) - static_cast<double>(pair.wet.out()[0])) / 0.5;
    for (size_t i = 1; i < total; ++i) {
        const double g = (static_cast<double>(pair.insert.out()[i]) - static_cast<double>(pair.wet.out()[i])) / 0.5;
        maxStep = std::max(maxStep, std::abs(g - prev));
        prev = g;
    }
    INFO("max per-sample change of the dry gain " << maxStep << ", bound " << bound);
    CHECK(maxStep <= bound);
}

TEST_CASE("PatternFreezeMode silence never ducks the dry", "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(6.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}, {4.0, 311.0, 0.5f}}), pfInsertDefaults);
    rig.runTo(total);
    const double ratio = pfEnergy(pfDiff(rig.out(), rig.in()), pfAt(4.0), pfAt(6.0)) /
                         pfEnergy(rig.in(), pfAt(4.0), pfAt(6.0));
    INFO("out - in over [4, 6 s): " << 10.0 * std::log10(ratio + 1e-30) << " dB re in");
    CHECK(ratio < 1e-6);
}

TEST_CASE("PatternFreezeMode a noise-only burst does not duck", "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(6.0);
    auto noise = [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setPatternType(PatternType::NoiseBursts);
    };
    auto pair = pfRenderPair(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}, {4.0, 311.0, 0.5f}}), noise,
                             [total](PfRig& rig) { rig.runTo(total); });
    double maxErr = 0.0;
    for (size_t i = pfAt(4.0); i < total; ++i) {
        const double dry = static_cast<double>(pair.insert.out()[i]) - static_cast<double>(pair.wet.out()[i]);
        maxErr = std::max(maxErr, std::abs(dry - static_cast<double>(pair.insert.in()[i])));
    }
    INFO("max |out - wet - in| over [4, 6 s): " << maxErr);
    CHECK(maxErr <= 1e-6);
}

// =============================================================================
// D11: pattern-type change (design 2.6)
// =============================================================================

TEST_CASE("PatternFreezeMode Pattern change leaves no block-rate artefact", "[effects][pattern_freeze][layer4]") {
    // Drones -> Euclidean at sample 132608 = 259 x 512 = 2072 x 64, on both block grids.
    constexpr size_t kSwitch = 132608;
    const size_t total = kSwitch + 26460 + 512;
    auto render = [total](size_t block) {
        PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::HarmonicDrones);
        });
        rig.runTo(kSwitch, block);
        rig.mode().setPatternType(PatternType::Euclidean);
        rig.runTo(total, block);
        return rig;
    };
    const PfRig small = render(64);
    const PfRig large = render(512);
    const double ratio = pfRms(pfDiff(small.out(), large.out()), kSwitch, kSwitch + 26460) /
                         pfRms(large.out(), kSwitch, kSwitch + 26460);
    INFO("RMS(diff 64 vs 512) / RMS after the switch: " << ratio);
    CHECK(ratio < 1e-3);
    // R13: the ideal output over the span is the drone's exact two-voice octave of the 233 Hz window (at the default
    // Drift's widest detune) fading out over its release, then the Euclidean slice of the 0.5 window; the largest
    // step is the larger of the two.
    const double a = pfDroneVoiceAmp(PatternFreezeConstants::kDefaultDroneVoices);
    const double maxCents = 50.0 * static_cast<double>(PatternFreezeConstants::kDefaultDroneDrift);
    const double ideal =
        std::max(pfIdealStep({{a, 233.0}, {a, pfDetune(466.0, maxCents)}}), pfIdealStep({{0.5, 233.0}}));
    const double step = pfMaxStep(large.out(), kSwitch, kSwitch + 26460);
    const double peak = pfPeak(large.out(), kSwitch, kSwitch + 26460);
    std::printf("Pattern change: max step %.4f, ideal %.4f (bar %.4f), peak %.4f\n", step, ideal, 2.0 * ideal, peak);
    INFO("max sample step " << step << ", ideal output's largest step " << ideal << ", peak " << peak);
    CHECK(step <= 2.0 * ideal);
}

// =============================================================================
// D12: the envelope (design 2.8)
// =============================================================================

TEST_CASE("PatternFreezeMode Linear envelope is linear", "[effects][pattern_freeze][layer4]") {
    // A constant 0.5 window, attack 100 ms, Fixed: the wet 50 ms into the hit at 2 s.
    const size_t total = pfAt(2.5);
    auto shaped = [](EnvelopeShape shape) {
        return [shape](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setEnvelopeAttackMs(100.0f);
            m.setEnvelopeShape(shape);
        };
    };
    SECTION("Linear") {
        PfRig rig(pfConstant(total, 0.5f), shaped(EnvelopeShape::Linear));
        rig.runTo(total);
        CHECK(rig.out()[pfAt(2.0) + 2205] == Approx(0.25).margin(0.01));
    }
    SECTION("Exponential") {
        PfRig rig(pfConstant(total, 0.5f), shaped(EnvelopeShape::Exponential));
        rig.runTo(total);
        CHECK(rig.out()[pfAt(2.0) + 2205] == Approx(0.44).margin(0.01));
    }
}

TEST_CASE("PatternFreezeMode every slice ends at 0", "[effects][pattern_freeze][layer4]") {
    // Release 100 ms, a constant 0.5 window: the last output sample of a hit, a burst and a grain.
    for (const EnvelopeShape shape : {EnvelopeShape::Linear, EnvelopeShape::Exponential}) {
        INFO("shape " << static_cast<int>(shape));
        auto base = [shape](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setEnvelopeReleaseMs(100.0f);
            m.setEnvelopeShape(shape);
        };

        {  // The hit at 2 s: 8820 samples, isolated (the next hit is at 2.75 s).
            const size_t total = pfAt(2.5);
            PfRig rig(pfConstant(total, 0.5f), base);
            rig.runTo(total);
            INFO("hit");
            CHECK(std::abs(rig.out()[pfAt(2.0) + 8819]) <= 1e-7);
        }
        {  // The burst at 2 s: 8820 samples, isolated (the next burst is at 2.25 s).
            const size_t total = pfAt(2.5);
            PfRig rig(pfConstant(total, 0.5f), [&base](PatternFreezeMode& m) {
                base(m);
                m.setPatternType(PatternType::NoiseBursts);
            });
            rig.runTo(total);
            INFO("burst");
            CHECK(std::abs(rig.out()[pfAt(2.0) + 8819]) <= 1e-7);
        }
        {  // Grains of exactly 100 ms (4410 samples) at 1 Hz, found sample by sample; only isolated ones count.
            const size_t total = pfAt(6.5);
            PfRig rig(pfConstant(total, 0.5f), [&base](PatternFreezeMode& m) {
                base(m);
                m.setPatternType(PatternType::GranularScatter);
                m.setGranularDensity(PatternFreezeConstants::kMinGranularDensityHz);
                m.setGranularSizeJitter(0.0f);
                m.setGranularGrainSize(100.0f);
            });
            rig.runToSec(2.0);
            std::vector<size_t> starts;
            while (rig.pos() < pfAt(6.0)) {
                const auto before = rig.mode().getGrainTriggerCount();
                const size_t s = rig.pos();
                rig.processBlock(1);
                if (rig.mode().getGrainTriggerCount() > before) {
                    starts.push_back(s);
                }
            }
            rig.runTo(total);
            int checked = 0;
            for (size_t k = 0; k < starts.size(); ++k) {
                const bool prevFar = k == 0 ? starts[k] >= pfAt(2.25) : starts[k] - starts[k - 1] > 11025;
                const bool nextFar = k + 1 == starts.size() || starts[k + 1] - starts[k] > 11025;
                if (!prevFar || !nextFar) {
                    continue;
                }
                INFO("grain from sample " << starts[k]);
                CHECK(std::abs(rig.out()[starts[k] + 4409]) <= 1e-7);
                ++checked;
            }
            CHECK(checked >= 1);
        }
    }
}

// =============================================================================
// D13: setters act only on a change (design row D13)
// =============================================================================

TEST_CASE("PatternFreezeMode re-sending unchanged parameters every block changes nothing",
          "[effects][pattern_freeze][layer4]") {
    const size_t total = pfAt(4.0);
    for (const PatternType type : {PatternType::Euclidean, PatternType::GranularScatter, PatternType::HarmonicDrones,
                                   PatternType::NoiseBursts}) {
        INFO("pattern type " << static_cast<int>(type));
        auto all = [type](PatternFreezeMode& m) {
            m.setPatternType(type);
            m.setSliceLengthMs(200.0f);
            m.setSliceMode(SliceMode::Fixed);
            m.setEuclideanSteps(8);
            m.setEuclideanHits(3);
            m.setEuclideanRotation(0);
            m.setNoteValue(NoteValue::Eighth);
            m.setNoteModifier(NoteModifier::None);
            m.setEnvelopeAttackMs(10.0f);
            m.setEnvelopeReleaseMs(100.0f);
            m.setEnvelopeShape(EnvelopeShape::Linear);
            m.setDryWetMix(100.0f);
            m.setGranularDensity(10.0f);
            m.setGranularPositionJitter(0.5f);
            m.setGranularSizeJitter(0.25f);
            m.setGranularGrainSize(100.0f);
            m.setDroneVoiceCount(2);
            m.setDroneInterval(PitchInterval::Octave);
            m.setDroneDrift(0.3f);
            m.setDroneDriftRate(0.5f);
            m.setNoiseColor(NoiseColor::Pink);
            m.setNoiseBurstRate(NoteValue::Eighth, NoteModifier::None);
            m.setNoiseFilterType(FilterType::Lowpass);
            m.setNoiseFilterCutoff(2000.0f);
            m.setNoiseFilterSweep(0.5f);
            m.setHold(false);
            m.setCaptureLength(CaptureLength::Whole);
            m.setDryMode(DryMode::Insert);
        };
        PfRig once(pfProbeTones(total), all);
        PfRig every(pfProbeTones(total), all);
        once.runTo(total);
        every.runEach(total, kPfBlock, [&all](PatternFreezeMode& m, size_t) { all(m); });
        CHECK(pfMaxAbsDiff(once.out(), every.out(), 0, total) <= 1e-7);
        CHECK(pfMaxAbsDiff(once.outR(), every.outR(), 0, total) <= 1e-7);
    }
}

// =============================================================================
// D14: transport (design 4.1). Attack 0, a DC window built with the transport playing.
// =============================================================================

TEST_CASE("PatternFreezeMode hits land at the step's sample, phased to projectTimeMusic",
          "[effects][pattern_freeze][layer4][freeze-transport]") {
    // The window is built playing from beat 0; at 4.3 s (no slice sounding) a locate to beat 7.5: the next hit is
    // step 0 at beat 8, 11025 samples later.
    const size_t jump = pfAt(4.3);
    const size_t total = jump + 11025 + 8820 + 512;
    PfRig rig = pfTransportRig(total);
    rig.runTo(jump);
    REQUIRE(rig.mode().hasWindow());
    rig.locate(7.5);
    rig.runTo(total);
    const size_t onset = pfFirstAbove(rig.out(), jump, total);
    INFO("first non-zero wet sample " << onset - jump << " samples after the locate");
    CHECK(onset >= jump + 11024);
    CHECK(onset <= jump + 11026);
}

TEST_CASE("PatternFreezeMode transport stop keeps the pattern running",
          "[effects][pattern_freeze][layer4][freeze-transport]") {
    const size_t total = pfAt(6.0);
    PfRig rig = pfTransportRig(total);
    rig.runToSec(4.5);
    REQUIRE(rig.mode().hasWindow());
    rig.stop();
    rig.runTo(total);
    // The hit before the stop was at 4.0 s; the next arrives 750 ms later, and the one after it 750 ms after that.
    const size_t next = pfFirstAbove(rig.out(), pfAt(4.3), total);
    INFO("next hit at sample " << next);
    CHECK(next >= pfAt(4.75) - 1);
    CHECK(next <= pfAt(4.75) + 1);
    const size_t after = pfFirstAbove(rig.out(), pfAt(5.0), total);
    INFO("following hit at sample " << after);
    CHECK(after >= pfAt(5.5) - 1);
    CHECK(after <= pfAt(5.5) + 1);
    CHECK(rig.out()[pfAt(4.8)] == Approx(0.5).margin(1e-3));  // the window keeps playing
}

TEST_CASE("PatternFreezeMode a locate re-bases without a false trigger",
          "[effects][pattern_freeze][layer4][freeze-transport]") {
    // Playing from beat 32 for 8 beats (a window at beat 36), then a block at beat 100.3: the re-base sets the last
    // fired step to 200, so nothing fires at the jump; the next hit is step 3 at beat 101.5.
    const size_t jump = 176400;
    const size_t total = jump + 26460 + 1000;
    PfRig rig = pfTransportRig(total);
    rig.play(32.0);
    rig.runTo(jump);
    REQUIRE(rig.mode().hasWindow());
    rig.locate(100.3);
    rig.runTo(total);
    const size_t onset = pfFirstAbove(rig.out(), jump, total);
    INFO("first non-zero wet sample " << onset - jump << " samples after the locate");
    CHECK(onset >= jump + 26459);
    CHECK(onset <= jump + 26461);
}

TEST_CASE("PatternFreezeMode a locate to a bar line fires the downbeat",
          "[effects][pattern_freeze][layer4][freeze-transport]") {
    // Playing from beat 32 to beat 37.21 (a window at beat 36), then a block at beat 0.0, still playing.
    const size_t jump = 114881;
    const size_t total = jump + 4096;
    PfRig rig = pfTransportRig(total);
    rig.play(32.0);
    rig.runTo(jump);
    REQUIRE(rig.mode().hasWindow());
    const auto before = rig.mode().getCaptureCount();
    CHECK(rig.out()[jump - 1] == 0.0f);
    rig.locate(0.0);
    rig.runTo(total);
    CHECK(std::abs(rig.out()[jump]) > 1e-6f);
    // The 1.21-beat cycle is under half a capture: the boundary at the jump captures nothing.
    CHECK(rig.mode().getCaptureCount() == before);
}

TEST_CASE("PatternFreezeMode a play edge fires step 0 and sounds nothing",
          "[effects][pattern_freeze][layer4][freeze-transport]") {
    // A window built playing from beat 32; stopped (free-run) from beat 36.5 to 37.21; then a play edge at 0.0 with
    // zero input from there.
    const size_t stopAt = 99225;
    const size_t jump = stopAt + 15656;
    const size_t total = jump + 88200;
    auto input = pfConstant(total, 0.5f);
    std::fill(input.begin() + static_cast<std::ptrdiff_t>(jump), input.end(), 0.0f);
    PfRig rig(input, [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setEnvelopeAttackMs(0.0f);
    });
    rig.play(32.0);
    rig.runTo(stopAt);
    REQUIRE(rig.mode().hasWindow());
    rig.stop();
    rig.runTo(jump);
    rig.play(0.0);
    rig.runTo(jump + kPfBlock);
    CHECK(rig.mode().getStepLastFiredIndex() == 0);
    CHECK(rig.mode().hasWindow() == false);
    rig.runTo(total);
    const double db = pfDbfs(pfRms(rig.out(), jump, total));
    INFO("wet of bar 1 after the play edge: " << db << " dBFS");
    CHECK(db < -90.0);
}

TEST_CASE("PatternFreezeMode a loop wrap retriggers and captures the bar just played",
          "[effects][pattern_freeze][layer4][freeze-transport]") {
    // A four-bar loop, bars at 220 / 330 / 440 / 550 Hz; continuous blocks end exactly at beat 16, then a block at 0.
    const size_t loop = 352800;
    const size_t total = loop + pfAt(2.0);
    std::vector<float> pass(loop);
    {
        const auto one = pfTones(loop, {{0.0, 220.0, 0.5f}, {2.0, 330.0, 0.5f}, {4.0, 440.0, 0.5f}, {6.0, 550.0, 0.5f}});
        std::copy(one.begin(), one.end(), pass.begin());
    }
    std::vector<float> input(total);
    for (size_t i = 0; i < total; ++i) {
        input[i] = pass[i % loop];
    }
    PfRig rig(input, [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setEnvelopeAttackMs(0.0f);
    });
    rig.runTo(loop);
    const auto before = static_cast<long long>(rig.mode().getCaptureCount());
    CHECK(pfPeak(rig.out(), pfAt(7.8), loop) == 0.0);
    rig.locate(0.0);
    rig.runTo(loop + kPfBlock);
    CHECK(static_cast<long long>(rig.mode().getCaptureCount()) == before + 1);
    CHECK(pfFirstAbove(rig.out(), loop, loop + 8, 1e-3f) <= loop + 1);
    rig.runTo(total);
    for (const double t : {0.0, 0.75, 1.5}) {
        const size_t a = loop + pfAt(t);
        INFO("hit at loop start + " << t << " s");
        CHECK(pfDominant(rig.out(), a, a + 8820, {220.0, 330.0, 440.0, 550.0}) == 550.0);
    }
}

TEST_CASE("PatternFreezeMode pre-roll fires on the grid", "[effects][pattern_freeze][layer4][freeze-transport]") {
    // A locate to beat -2.0: step 6 at beat -1.0 and step 0 at beat 0.0, 22050 and 44100 samples later.
    const size_t jump = pfAt(4.3);
    const size_t total = jump + 44100 + 9000;
    PfRig rig = pfTransportRig(total);
    rig.runTo(jump);
    REQUIRE(rig.mode().hasWindow());
    rig.locate(-2.0);
    rig.runTo(total);
    const size_t first = pfFirstAbove(rig.out(), jump, total);
    INFO("first hit " << first - jump << " samples after the locate");
    CHECK(first >= jump + 22049);
    CHECK(first <= jump + 22051);
    const size_t second = pfFirstAbove(rig.out(), jump + 22050 + 8830, total);
    INFO("second hit " << second - jump << " samples after the locate");
    CHECK(second >= jump + 44099);
    CHECK(second <= jump + 44101);
}

TEST_CASE("PatternFreezeMode a tempo change keeps the captures", "[effects][pattern_freeze][layer4][freeze-transport]") {
    // 200 BPM for beats 0..10 (two bars and two beats), then 100 BPM; the host positions stay consistent.
    const size_t change = 132300;  // beat 10 at 200 BPM
    const size_t b12 = change + 52920;
    const size_t b16 = b12 + 105840;
    const size_t b20 = b16 + 105840;
    const size_t total = b20 + 4096;
    PfRig rig = pfTransportRig(total);
    rig.bpm = 200.0;
    rig.runTo(change);
    REQUIRE(rig.mode().hasWindow());
    rig.bpm = 100.0;
    for (const size_t boundary : {b12, b16, b20}) {
        rig.runTo(boundary);
        const auto before = static_cast<long long>(rig.mode().getCaptureCount());
        rig.runTo(boundary + kPfBlock);
        INFO("boundary at sample " << boundary);
        CHECK(static_cast<long long>(rig.mode().getCaptureCount()) == before + 1);
    }
    rig.runTo(total);
    // The hits of the bar [16, 20) at the new step length: beats 16, 17.5 and 19 = 0, 39690 and 79380 samples.
    size_t from = b16 - 4000;
    for (const size_t expected : {b16, b16 + 39690, b16 + 79380}) {
        const size_t onset = pfFirstAbove(rig.out(), from, total);
        INFO("hit expected at sample " << expected << ", found at " << onset);
        CHECK(onset + 1 >= expected);
        CHECK(onset <= expected + 1);
        from = expected + 8830;
    }
}

TEST_CASE("PatternFreezeMode a ritardando keeps one capture per boundary",
          "[effects][pattern_freeze][layer4][freeze-transport]") {
    // A window built at 120 BPM over two bars; from beat 8 a linear ramp 120 -> 60 BPM over two bars (8 beats in
    // D = 16/3 s), then 60 BPM to beat 20. Blocks of 2048; the host position is integrated from the ramp, the
    // tempo is the block-start tempo.
    const double d = 16.0 / 3.0;
    const size_t s0 = 176400;
    const size_t total = s0 + pfAt(d) + pfAt(5.0);
    auto beatAt = [d](double t) { return t <= d ? 8.0 + 2.0 * t - 0.5 * t * t / d : 16.0 + (t - d); };
    auto tempoAt = [d](double t) { return t <= d ? 120.0 - 60.0 * t / d : 60.0; };
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), pfFreshDefaults);
    rig.runTo(s0);
    REQUIRE(rig.mode().hasWindow());
    while (rig.pos() + 2048 <= total) {
        const double t0 = static_cast<double>(rig.pos() - s0) / kPfSr;
        const double t1 = t0 + 2048.0 / kPfSr;
        const double b0 = beatAt(t0);
        const double b1 = beatAt(t1);
        rig.bpm = tempoAt(t0);
        rig.beat = b0;
        const auto before = static_cast<long long>(rig.mode().getCaptureCount());
        rig.processBlock(2048);
        long long crossed = 0;
        for (auto k = static_cast<long long>(std::ceil(b0 / 4.0)); 4.0 * static_cast<double>(k) < b1; ++k) {
            ++crossed;
        }
        INFO("block from beat " << b0 << " to " << b1);
        CHECK(static_cast<long long>(rig.mode().getCaptureCount()) - before == crossed);
        CHECK(rig.mode().getRecFrom() == 0);
        if (b1 > 20.5) {
            break;
        }
    }
}

TEST_CASE("PatternFreezeMode a host tempo above 300 BPM captures", "[effects][pattern_freeze][layer4][freeze-transport]") {
    // 320 BPM, Capture 1/1, 8 bars: a beat is 8268.75 samples, a bar 33075.
    const double spb = kPfSr * 60.0 / 320.0;
    const size_t total = 264600 + 4096;
    PfRig rig = pfTransportRig(total);
    rig.bpm = 320.0;
    while (rig.pos() < 264600 + kPfBlock) {
        const double b0 = rig.beat;
        const auto before = static_cast<long long>(rig.mode().getCaptureCount());
        rig.processBlock(kPfBlock);
        long long crossed = 0;
        for (auto k = static_cast<long long>(std::ceil(b0 / 4.0)); 4.0 * static_cast<double>(k) < rig.beat; ++k) {
            crossed += k >= 1 ? 1 : 0;  // beat 0 comes before any music: it captures nothing
        }
        INFO("block from beat " << b0 << " to " << rig.beat);
        CHECK(static_cast<long long>(rig.mode().getCaptureCount()) - before == crossed);
    }
    CHECK(rig.mode().getCaptureCount() == 8);
    rig.runTo(total);
    // Steps 3 and 6 of bars 2..8 at 1.5 and 3.0 beats (the bar-line slice overlaps the previous one at 320 BPM).
    for (int bar = 1; bar < 8; ++bar) {
        const double barStart = 4.0 * static_cast<double>(bar) * spb;
        for (const double beat : {1.5, 3.0}) {
            const auto expected = static_cast<size_t>(std::llround(barStart + beat * spb));
            const size_t onset = pfFirstAbove(rig.out(), expected - 3000, expected + 3000);
            INFO("bar " << bar << " beat " << beat << ": expected " << expected << ", found " << onset);
            CHECK(onset + 1 >= expected);
            CHECK(onset <= expected + 1);
        }
    }
}

// =============================================================================
// D16: the event arrays (design 4.3)
// =============================================================================

TEST_CASE("PatternFreezeMode every hit fires at 1/64T and 300 BPM in 8192-sample blocks",
          "[effects][pattern_freeze][layer4][freeze-transport]") {
    // Steps 8, Hits 8 (every step a hit), Pattern Rate 1/64T (1/24 beat), 20 blocks of 8192 from beat 0.
    for (const double bpm : {300.0, PatternFreezeConstants::kMaxClockTempoBPM}) {
        INFO("tempo " << bpm << " BPM");
        const size_t total = 20 * 8192;
        PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setEuclideanSteps(8);
            m.setEuclideanHits(8);
            m.setNoteValue(NoteValue::SixtyFourth);
            m.setNoteModifier(NoteModifier::Triplet);
        });
        rig.bpm = bpm;
        rig.runTo(total, 8192);
        const double spb = kPfSr * 60.0 / bpm;
        const double lastBeat = (static_cast<double>(total) - 0.5) / spb;
        uint64_t expected = 0;
        while (static_cast<double>(expected) / 24.0 < lastBeat) {
            ++expected;
        }
        if (bpm == 300.0) {
            CHECK(expected == 446);
        }
        CHECK(rig.mode().getHitCount() == expected);
    }
}

// =============================================================================
// D17: reads across a window edge (design 2.4, 2.10)
// =============================================================================

TEST_CASE("PatternFreezeMode a slice past the window's end reads the following cycle",
          "[effects][pattern_freeze][layer4][freeze-held]") {
    // Variable, 16 x 1/16 with step 15 the only hit (Hits 1, Rotation 1), so the step-15 slice plays alone; 233 Hz
    // for one bar, then 311 Hz, phase-continuous. In the third bar the step-15 slice (beat 3.75, P = 82688) runs
    // 3308 samples (75 ms) past the 88200-sample window into the cycle after it.
    const size_t total = pfAt(6.4);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 311.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setSliceMode(SliceMode::Variable);
        m.setEuclideanSteps(16);
        m.setEuclideanHits(1);
        m.setEuclideanRotation(1);
        m.setNoteValue(NoteValue::Sixteenth);
    });
    rig.runTo(total);
    const size_t start = 259087;
    const size_t end = start + 8822;
    CHECK(pfRms(rig.out(), start + 441, start + 4410) > 0.1 * kPfSineRms);
    const double step = pfMaxStep(rig.out(), start, end);
    INFO("max sample step of the step-15 slice " << step << ", bar " << 2.0 * pfCleanStep(311.0));
    CHECK(step <= 2.0 * pfCleanStep(311.0));
    CHECK(pfDominant(rig.out(), start + 8820 - 3308, start + 8820, {233.0, 311.0}) == 311.0);
}

TEST_CASE("PatternFreezeMode without contiguous audio a read is clamped", "[effects][pattern_freeze][layer4][freeze-held]") {
    // The same, with Hold pressed at 4.1 s so it holds the complete bar [2, 4 s); the held boundary at 6 s clears
    // recFollowsPlay_, so in the bar [6, 8 s) the step-15 slice is clamped to start at length - n = 79380.
    const size_t total = pfAt(8.2);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 311.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setSliceMode(SliceMode::Variable);
        m.setEuclideanSteps(16);
        m.setEuclideanHits(1);
        m.setEuclideanRotation(1);
        m.setNoteValue(NoteValue::Sixteenth);
    });
    rig.runToSec(4.1);
    rig.mode().setHold(true);
    rig.runToSec(7.875);
    rig.runTo(pfAt(7.875) + kPfBlock);
    CHECK(rig.mode().getLastSliceStart() == 88200 - 8820);
    rig.runTo(total);
    const double step = pfMaxStep(rig.out(), pfAt(7.875), pfAt(7.875) + 8820);
    INFO("max sample step " << step << ", bar " << 2.0 * pfCleanStep(311.0));
    CHECK(step <= 2.0 * pfCleanStep(311.0));
}

TEST_CASE("PatternFreezeMode a slice longer than the window splices once, quietly",
          "[effects][pattern_freeze][layer4][freeze-held]") {
    // Capture 1/4 (a 0.5 s window at 120 BPM), Slice Length 2000 ms, one hit per bar so the splice is heard alone.
    const size_t total = pfAt(8.0);
    PfRig rig(pfTones(total, {{0.0, 233.0, 0.5f}}), [](PatternFreezeMode& m) {
        pfFreshDefaults(m);
        m.setCaptureLength(CaptureLength::Quarter);
        m.setSliceLengthMs(2000.0f);
        m.setEuclideanHits(1);
    });
    rig.runTo(total);
    CHECK(pfPeak(rig.out(), 0, total) > 0.1);
    const double step = pfMaxStep(rig.out(), 0, total);
    INFO("max sample step " << step << ", bar " << 2.0 * pfCleanStep(233.0));
    CHECK(step <= 2.0 * pfCleanStep(233.0));
}

// =============================================================================
// D15: the probe of design 6.1 (hidden). It prints the rows of design 6.2.
// =============================================================================

namespace {

/// The probe input of design 6.1: 233 Hz [0, 2 s), 311 Hz [2, 4 s), 415 Hz [4, 6 s), 0.5.
[[nodiscard]] std::vector<float> pfProbeInput(size_t total) {
    return pfTones(total, {{0.0, 233.0, 0.5f}, {2.0, 311.0, 0.5f}, {4.0, 415.0, 0.5f}});
}

/// The one-bar drum loop of D8 (3), repeated.
[[nodiscard]] std::vector<float> pfDrumLoop(size_t total) {
    std::vector<float> drum(total, 0.0f);
    uint32_t noise = 0x1234567u;
    for (size_t i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / kPfSr;
        const double inBeat = std::fmod(t, 0.5);
        const double kick = 0.8 * std::exp(-inBeat / 0.080) * std::sin(kPfTwoPi * 60.0 * inBeat);
        noise = noise * 1664525u + 1013904223u;
        const double white = static_cast<double>(noise >> 8) / 8388608.0 - 1.0;
        const double hat = inBeat >= 0.25 ? 0.25 * std::exp(-(inBeat - 0.25) / 0.020) * white : 0.0;
        const double pad = 0.05 * std::sin(kPfTwoPi * 233.0 * t);
        drum[i] = static_cast<float>(kick + hat + pad);
    }
    return drum;
}

/// A sustained chord at 233.0, 293.7 and 349.2 Hz, 0.2 each.
[[nodiscard]] std::vector<float> pfChord(size_t total) {
    std::vector<float> x(total);
    for (size_t i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / kPfSr;
        x[i] = static_cast<float>(0.2 * (std::sin(kPfTwoPi * 233.0 * t) + std::sin(kPfTwoPi * 293.7 * t) +
                                         std::sin(kPfTwoPi * 349.2 * t)));
    }
    return x;
}

[[nodiscard]] double pfDb(double ratio) { return 10.0 * std::log10(std::max(ratio, 1e-30)); }

/// wet level, peak window and active fraction of 10 ms windows over [a, b), each re the input's window.
void pfProbeLevels(const std::vector<float>& wet, const std::vector<float>& in, size_t a, size_t b, double& levelDb,
                   double& peakDb, double& active) {
    size_t windows = 0;
    size_t on = 0;
    double we = 0.0;
    double ie = 0.0;
    double peak = 0.0;
    for (size_t s = a; s + kPfWin <= b; s += kPfWin) {
        const double w = pfEnergy(wet, s, s + kPfWin);
        const double d = pfEnergy(in, s, s + kPfWin);
        ++windows;
        on += w > d * 1e-4 ? 1 : 0;
        we += w;
        ie += d;
        peak = std::max(peak, w / std::max(d, 1e-12));
    }
    levelDb = pfDb(we / std::max(ie, 1e-30));
    peakDb = pfDb(peak);
    active = windows > 0 ? static_cast<double>(on) / static_cast<double>(windows) : 0.0;
}

/// The audibility metrics of design 6.1 over [a, b): diff (dB), moved (fraction), level (median |dB|).
void pfAudibility(const std::vector<float>& out, const std::vector<float>& in, size_t a, size_t b, double& diffDb,
                  double& moved, double& level) {
    const auto d = pfDiff(out, in);
    diffDb = pfDb(pfEnergy(d, a, b) / std::max(pfEnergy(in, a, b), 1e-30));
    const double inRms = pfRms(in, a, b);
    const double thr = inRms * 0.1;
    size_t windows = 0;
    size_t on = 0;
    std::vector<double> lv;
    for (size_t s = a; s + kPfWin <= b; s += kPfWin) {
        ++windows;
        on += pfRms(d, s, s + kPfWin) > thr ? 1 : 0;
        lv.push_back(std::abs(20.0 * std::log10(std::max(pfRms(out, s, s + kPfWin), 1e-12) /
                                                std::max(pfRms(in, s, s + kPfWin), 1e-12))));
    }
    moved = windows > 0 ? static_cast<double>(on) / static_cast<double>(windows) : 0.0;
    std::sort(lv.begin(), lv.end());
    level = lv.empty() ? 0.0 : lv[lv.size() / 2];
}

/// Audio review round 2: as pfBestLagCorrelation over [a, b), also returning the lag (ms) of the best r, and the r
/// of the same lag when the input is restricted to samples at or after @p liveFrom (zero before), so the part of r
/// that comes from the bar before the pitch change is separated from the part that tracks the live input.
struct PfLagR {
    double r;
    double lagMs;
    double rLiveOnly;
};
[[nodiscard]] PfLagR pfBestLagWithLive(const std::vector<float>& wet, const std::vector<float>& in, size_t a, size_t b,
                                       size_t liveFrom) {
    PfLagR best{0.0, 0.0, 0.0};
    for (size_t lag = 0; lag <= 26460 && lag <= a; lag += 21) {
        double num = 0.0;
        double numLive = 0.0;
        double ea = 0.0;
        double eb = 0.0;
        for (size_t i = a; i < b; ++i) {
            const double w = wet[i];
            const double x = in[i - lag];
            num += w * x;
            numLive += (i - lag) >= liveFrom ? w * x : 0.0;
            ea += w * w;
            eb += x * x;
        }
        const double r = num / std::sqrt(std::max(ea * eb, 1e-20));
        if (r > best.r) {
            best = {r, 1000.0 * static_cast<double>(lag) / kPfSr, numLive / std::sqrt(std::max(ea * eb, 1e-20))};
        }
    }
    return best;
}

/// Audio review round 2: the incoherent sum over bursts k in [k0, k1) of the Goertzel power of each 250 ms burst
/// body [0.25 k + 20 ms, + 180 ms) at @p hz: immune to the inter-burst phase steps that cancel a 1 s Goertzel.
[[nodiscard]] double pfBurstPower(const std::vector<float>& x, int k0, int k1, double hz) {
    double p = 0.0;
    for (int k = k0; k < k1; ++k) {
        const double t0 = 0.25 * k;
        p += pfTonePower(x, pfAt(t0 + 0.02), pfAt(t0 + 0.18), hz);
    }
    return p;
}

}  // anonymous namespace

TEST_CASE("PROBE Freeze signal path at defaults", "[.][probe][freeze-path]") {
    const size_t total = pfAt(6.0);
    const char* names[] = {"Euclidean", "GranularScatter", "HarmonicDrones", "NoiseBursts"};
    const auto input = pfProbeInput(total);

    for (int pt = 0; pt < 4; ++pt) {
        const auto type = static_cast<PatternType>(pt);
        auto config = [type](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(type);
        };
        for (const bool playing : {true, false}) {
            PfRig rig(input, config);
            if (!playing) {
                rig.stop();
            }
            rig.runTo(total);
            double level = 0.0;
            double peak = 0.0;
            double active = 0.0;
            pfProbeLevels(rig.out(), rig.in(), pfAt(1.0), total, level, peak, active);
            const double held = type == PatternType::HarmonicDrones
                                     ? pfDominant(rig.out(), pfAt(2.5), pfAt(3.5), {233.0, 311.0, 415.0, 466.0})
                                     : pfDominant(rig.out(), pfAt(2.5), pfAt(3.5), {233.0, 311.0, 415.0});
            // R11: "held, not live" is the held-note check (the window's 233 Hz over the live 311 Hz, > 10 x in
            // power); r is printed, never asserted.
            const double heldPower = pfTonePower(rig.out(), pfAt(2.5), pfAt(3.5), 233.0);
            const double livePower = pfTonePower(rig.out(), pfAt(2.5), pfAt(3.5), 311.0);
            const double r = pfBestLagCorrelation(rig.out(), rig.in(), pfAt(2.0), pfAt(3.0));
            std::printf("%-16s playing=%d | wet %+6.1f dB re in, peak %+6.1f dB | active %3.0f%% | held %3.0f Hz | "
                        "233 re 311 %+6.1f dB | r=%.2f (printed) | captures %llu\n",
                        names[pt], playing ? 1 : 0, level, peak, 100.0 * active, held, pfDb(heldPower / livePower),
                        r, static_cast<unsigned long long>(rig.mode().getCaptureCount()));
            INFO(names[pt] << " playing=" << playing << ": 233 Hz power " << heldPower << ", 311 Hz " << livePower);
            CHECK(heldPower > 10.0 * livePower);
            // Audio review round 1: the correlation once the input has changed pitch. Over [2.6, 3.6 s) every lag of
            // 0..600 ms reads the input's 311 Hz bar (and over [4.6, 5.6 s) its 415 Hz bar), so a wet that tracks the
            // live input reads high here (the old code: 0.69 at 175 ms) and a held loop of the bar before reads ~0.
            const double rLive2 = pfBestLagCorrelation(rig.out(), rig.in(), pfAt(2.6), pfAt(3.6));
            const double rLive4 = pfBestLagCorrelation(rig.out(), rig.in(), pfAt(4.6), pfAt(5.6));
            const double held4 = pfDominant(rig.out(), pfAt(4.5), pfAt(5.5), {233.0, 311.0, 415.0});
            std::printf("%-16s playing=%d | pitch change: r live [2.6, 3.6 s) %.3f, [4.6, 5.6 s) %.3f | held [4.5, 5.5 s) "
                        "%3.0f Hz (input 415, bar before 311)\n",
                        names[pt], playing ? 1 : 0, rLive2, rLive4, held4);
            // Audio review round 2: where the headline r comes from. Over [2, 3 s) the lags reach back into the
            // 233 Hz bar before 2 s; "live part" is the r contributed by input samples at or after 2 s alone.
            const PfLagR lr = pfBestLagWithLive(rig.out(), rig.in(), pfAt(2.0), pfAt(3.0), pfAt(2.0));
            // The held tone of [4.5, 5.5 s) per 10 ms window: the fraction of active windows whose dominant tone is
            // the bar before (311), the live bar (415) or the old bar (233).
            size_t act4 = 0;
            size_t w311 = 0;
            size_t w415 = 0;
            size_t w233 = 0;
            for (size_t s = pfAt(4.5); s + 4 * kPfWin <= pfAt(5.5); s += 4 * kPfWin) {
                if (pfRms(rig.out(), s, s + 4 * kPfWin) < 0.01 * kPfSineRms) {
                    continue;
                }
                ++act4;
                const double d = pfDominant(rig.out(), s, s + 4 * kPfWin, {233.0, 311.0, 415.0});
                w311 += d == 311.0 ? 1 : 0;
                w415 += d == 415.0 ? 1 : 0;
                w233 += d == 233.0 ? 1 : 0;
            }
            std::printf("%-16s playing=%d | r [2, 3 s) %.2f at lag %5.1f ms, live part %.3f | [4.5, 5.5 s) 40 ms windows "
                        "%zu: 311 %zu, 415 %zu, 233 %zu\n",
                        names[pt], playing ? 1 : 0, lr.r, lr.lagMs, lr.rLiveOnly, act4, w311, w415, w233);
            if (type == PatternType::NoiseBursts) {
                // Burst bodies k = 10..13 lie in [2.5, 3.5 s), 18..21 in [4.5, 5.5 s).
                const double p233a = pfBurstPower(rig.out(), 10, 14, 233.0);
                const double p311a = pfBurstPower(rig.out(), 10, 14, 311.0);
                const double p233b = pfBurstPower(rig.out(), 18, 22, 233.0);
                const double p311b = pfBurstPower(rig.out(), 18, 22, 311.0);
                const double p415b = pfBurstPower(rig.out(), 18, 22, 415.0);
                std::printf("%-16s playing=%d | per-burst power: [2.5, 3.5 s) 233 re 311 %+6.1f dB | [4.5, 5.5 s) "
                            "311 re 233 %+6.1f dB, 311 re 415 %+6.1f dB\n",
                            names[pt], playing ? 1 : 0, pfDb(p233a / p311a), pfDb(p311b / p233b),
                            pfDb(p311b / p415b));
            }
            if (type == PatternType::HarmonicDrones && playing) {
                // Audio review round 1: the default-Drift upper voice searched +/- 30 cents (the R12 +/- 10 window
                // ends at the -10 cent edge at Drift 30 %).
                PfPeak wide{0.0, -1.0};
                for (int k = -60; k <= 60; ++k) {
                    const double c = 0.5 * static_cast<double>(k);
                    const double p = pfTonePower(rig.out(), pfAt(2.5), pfAt(3.5), pfDetune(466.0, c));
                    if (p > wide.power) {
                        wide = {c, p};
                    }
                }
                std::printf("%-16s           | 466 searched +/- 30 cents: peak %+6.1f dB re 233 at %+.1f cents\n",
                            names[pt], pfDb(wide.power / heldPower), wide.cents);
                // R12: the upper voice's peak within +/- 10 cents of 466 Hz, in 0.5-cent steps.
                const PfPeak upper = pfPeakSearch(rig.out(), pfAt(2.5), pfAt(3.5), 466.0);
                std::printf("%-16s           | power re 233 Hz: 466 peak %+6.1f dB at %+.1f cents (R12; Drift 30 %% "
                            "swings it +/- 15 cents), 415 %+6.1f dB\n",
                            names[pt], pfDb(upper.power / heldPower), upper.cents,
                            pfDb(pfTonePower(rig.out(), pfAt(2.5), pfAt(3.5), 415.0) / heldPower));
                // The same at Drift 0, where the R12 search reads the shifter's own pitch.
                PfRig still(input, [type](PatternFreezeMode& m) {
                    pfFreshDefaults(m);
                    m.setPatternType(type);
                    m.setDroneDrift(0.0f);
                });
                still.runTo(total);
                const double still233 = pfTonePower(still.out(), pfAt(2.5), pfAt(3.5), 233.0);
                const PfPeak stillUpper = pfPeakSearch(still.out(), pfAt(2.5), pfAt(3.5), 466.0);
                std::printf("%-16s Drift 0  | power re 233 Hz: 466 peak %+6.1f dB at %+.1f cents (R12: within 3)\n",
                            names[pt], pfDb(stillUpper.power / still233), stillUpper.cents);
            }
        }
    }

    // ---- Direction (audio review round 1): every type reads forward ----------------------------------------------
    // A rising sawtooth (slow rise, one sharp drop per period) has a first difference with a strongly negative
    // skewness; the same audio read backwards has a positive one. Per active 10 ms window of the wet (Mix 100, Dry =
    // Mix) over [2.5, 5.5 s), the sign of that skewness says which way the window was read.
    {
        std::vector<float> saw(total, 0.0f);
        double ph = 0.0;
        for (size_t i = 0; i < total; ++i) {
            const double t = static_cast<double>(i) / kPfSr;
            double hz = 415.0;
            if (t < 2.0) {
                hz = 233.0;
            } else if (t < 4.0) {
                hz = 311.0;
            }
            saw[i] = static_cast<float>(0.5 * (2.0 * ph - 1.0));
            ph += hz / kPfSr;
            ph -= std::floor(ph);
        }
        auto diffSkew = [](const std::vector<float>& x, size_t a, size_t b) {
            double s1 = 0.0;
            double n = 0.0;
            for (size_t i = std::max<size_t>(a, 1); i < b; ++i) {
                s1 += static_cast<double>(x[i]) - static_cast<double>(x[i - 1]);
                n += 1.0;
            }
            const double mean = s1 / std::max(n, 1.0);
            double m2 = 0.0;
            double m3 = 0.0;
            for (size_t i = std::max<size_t>(a, 1); i < b; ++i) {
                const double d = static_cast<double>(x[i]) - static_cast<double>(x[i - 1]) - mean;
                m2 += d * d;
                m3 += d * d * d;
            }
            m2 /= std::max(n, 1.0);
            m3 /= std::max(n, 1.0);
            return m2 > 1e-20 ? m3 / std::pow(m2, 1.5) : 0.0;
        };
        const double inSkew = diffSkew(saw, pfAt(2.5), pfAt(5.5));
        for (int pt = 0; pt < 4; ++pt) {
            for (const auto mode : {SliceMode::Fixed, SliceMode::Variable}) {
                PfRig rig(saw, [pt, mode](PatternFreezeMode& m) {
                    pfFreshDefaults(m);
                    m.setPatternType(static_cast<PatternType>(pt));
                    m.setSliceMode(mode);
                });
                rig.runTo(total);
                const auto& w = rig.out();
                size_t act = 0;
                size_t fwd = 0;
                size_t bwd = 0;
                for (size_t s = pfAt(2.5); s + kPfWin <= pfAt(5.5); s += kPfWin) {
                    if (pfRms(w, s, s + kPfWin) < 0.01 * kPfSineRms) {
                        continue;
                    }
                    ++act;
                    const double k = diffSkew(w, s, s + kPfWin);
                    fwd += k < -0.5 ? 1 : 0;
                    bwd += k > 0.5 ? 1 : 0;
                }
                const double denom = static_cast<double>(std::max<size_t>(act, 1));
                std::printf("Direction %-16s (%-8s) | input skew %+6.2f | wet skew %+6.2f | active windows %3zu: "
                            "forward %5.1f%%, backward %5.1f%%\n",
                            names[pt], mode == SliceMode::Fixed ? "Fixed" : "Variable", inSkew,
                            diffSkew(w, pfAt(2.5), pfAt(5.5)), act, 100.0 * static_cast<double>(fwd) / denom,
                            100.0 * static_cast<double>(bwd) / denom);
            }
        }
    }

    // ---- Which bar each type replays (audio review round 1) -------------------------------------------------------
    // 8 s of the probe tones (233 / 311 / 415 / 523 Hz per bar): the Goertzel power of each bar's tone in the middle
    // second of the next three bars, re the bar before's tone (the one a held loop must play).
    {
        const size_t total8 = pfAt(8.0);
        const auto tones8 = pfProbeTones(total8);
        const double barHz[] = {233.0, 311.0, 415.0, 523.0};
        for (int pt = 0; pt < 4; ++pt) {
            PfRig rig(tones8, [pt](PatternFreezeMode& m) {
                pfFreshDefaults(m);
                m.setPatternType(static_cast<PatternType>(pt));
            });
            rig.runTo(total8);
            std::printf("Replayed bar %-16s |", names[pt]);
            for (int bar = 1; bar <= 3; ++bar) {
                const size_t a = pfAt(2.0 * bar + 0.5);
                const size_t b = pfAt(2.0 * bar + 1.5);
                const double ref = pfTonePower(rig.out(), a, b, barHz[bar - 1]);
                std::printf(" [%.1f, %.1f s) %3.0f Hz:", 2.0 * bar + 0.5, 2.0 * bar + 1.5, barHz[bar - 1]);
                for (int k = 0; k < 4; ++k) {
                    if (k != bar - 1) {
                        std::printf(" %3.0f %+6.1f", barHz[k], pfDb(pfTonePower(rig.out(), a, b, barHz[k]) / ref));
                    }
                }
                std::printf(" |");
            }
            std::printf("\n");
        }
        // Noise Bursts, per burst (every 250 ms from 2 s): the dominant bar tone of each 200 ms burst body.
        PfRig rig(tones8, [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::NoiseBursts);
        });
        rig.runTo(total8);
        std::printf("NoiseBursts per burst from 2 s (dominant tone of [k x 250 ms + 20, + 180 ms)):");
        for (int k = 8; k < 32; ++k) {
            const double t0 = 0.25 * k;
            std::printf(" %.0f", pfDominant(rig.out(), pfAt(t0 + 0.02), pfAt(t0 + 0.18), {233.0, 311.0, 415.0, 523.0}));
        }
        std::printf("\n");
    }

    // ---- Euclidean: hold, Insert, Insert hold -------------------------------------------------------------------
    {
        PfRig rig(input, pfFreshDefaults);
        rig.runToSec(1.9);
        rig.mode().setHold(true);
        rig.runToSec(4.0);
        const auto before = static_cast<unsigned long long>(rig.mode().getCaptureCount());
        rig.runTo(pfAt(4.0) + kPfBlock);
        const auto after = static_cast<unsigned long long>(rig.mode().getCaptureCount());
        rig.runTo(total);
        std::printf("Euclidean hold (1.9 s) | held tone [4.5, 5.5 s) %3.0f Hz (input 415) | captures before 4 s %llu, "
                    "after %llu\n",
                    pfDominant(rig.out(), pfAt(4.5), pfAt(5.5), {233.0, 311.0, 415.0}), before, after);
    }
    {
        auto pair = pfRenderPair(input, pfFreshDefaults, [total](PfRig& rig) { rig.runTo(total); });
        const auto& out = pair.insert.out();
        const auto& in = pair.insert.in();
        const auto& wet = pair.wet.out();
        double gapMax = 0.0;
        double plateauShare = 0.0;
        const std::vector<double> hits = {2.0, 2.75, 3.5, 4.0, 4.75, 5.5, 6.0};
        for (size_t h = 0; h + 1 < hits.size(); ++h) {
            gapMax = std::max(gapMax, pfMaxAbsDiff(out, in, pfAt(hits[h] + 0.204), pfAt(hits[h + 1])));
            const size_t a = pfAt(hits[h] + 0.0128);
            const size_t b = pfAt(hits[h] + 0.100);
            plateauShare = std::max(plateauShare, pfEnergy(pfDiff(out, wet), a, b) / pfEnergy(in, a, b));
        }
        std::printf("Euclidean Insert line | gaps max |out - in| %.2e | plateau input share max %+6.1f dB\n", gapMax,
                    pfDb(plateauShare));
    }
    {
        auto pair = pfRenderPair(input, pfFreshDefaults, [total](PfRig& rig) {
            rig.runToSec(1.9);
            rig.mode().setHold(true);
            rig.runTo(total);
        });
        const auto share = pfDiff(pair.insert.out(), pair.wet.out());
        double worst = 0.0;
        for (size_t s = pfAt(2.2); s + kPfWin <= total; s += kPfWin) {
            worst = std::max(worst, pfEnergy(share, s, s + kPfWin) / pfEnergy(pair.insert.in(), s, s + kPfWin));
        }
        std::printf("Euclidean Insert hold line | input share max from 2.2 s %+6.1f dB | active %3.0f%% over [2.2, 6 s)\n",
                    pfDb(worst), 100.0 * pfActiveFraction(pair.insert.out(), pfAt(2.2), total, kPfSineRms));
    }

    // ---- Audibility at the fresh defaults (Mix 100, Insert), transport playing (R6) --------------------------------
    {
        struct Row {
            const char* name;
            std::vector<float> in;
        };
        const Row rows[] = {{"tone", input}, {"drum", pfDrumLoop(total)}, {"chord", pfChord(total)}};
        for (const auto mode : {SliceMode::Fixed, SliceMode::Variable}) {
            for (const auto& row : rows) {
                PfRig rig(row.in, [mode](PatternFreezeMode& m) {
                    pfInsertDefaults(m);
                    m.setSliceMode(mode);
                });
                rig.runTo(total);
                double diff = 0.0;
                double moved = 0.0;
                double level = 0.0;
                pfAudibility(rig.out(), rig.in(), pfAt(2.0), total, diff, moved, level);
                std::printf("Audibility %-5s (%s) | diff %+6.1f dB | moved %3.0f%% | level %.1f dB\n", row.name,
                            mode == SliceMode::Fixed ? "Fixed" : "Variable", diff, 100.0 * moved, level);
            }
        }
    }

    // ---- Granular Insert line --------------------------------------------------------------------------------------
    {
        auto granular = [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::GranularScatter);
        };
        auto pair = pfRenderPair(input, granular, [total](PfRig& rig) { rig.runTo(total); });
        const auto dry = pfDiff(pair.insert.out(), pair.wet.out());
        std::vector<double> g;
        for (size_t s = pfAt(1.0); s + kPfWin <= total; s += kPfWin) {
            g.push_back(pfRms(dry, s, s + kPfWin) / std::max(pfRms(pair.insert.in(), s, s + kPfWin), 1e-12));
        }
        std::sort(g.begin(), g.end());
        const double depth = 20.0 * std::log10(g[static_cast<size_t>(0.95 * static_cast<double>(g.size() - 1))] /
                                               g[static_cast<size_t>(0.05 * static_cast<double>(g.size() - 1))]);
        std::printf("GranularScatter Insert line | dry gain modulation depth p95/p5 %.3f dB\n", depth);
    }

    // ---- Drones: block, sweep, drum --------------------------------------------------------------------------------
    {
        auto drones = [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::HarmonicDrones);
        };
        PfRig small(input, drones);
        PfRig large(input, drones);
        small.runTo(total, 64);
        large.runTo(total, 512);
        std::printf("HarmonicDrones block | RMS(diff 64 vs 512) / RMS %.2e\n",
                    pfRms(pfDiff(small.out(), large.out()), 0, total) / pfRms(large.out(), 0, total));

        std::vector<double> tones;
        for (int f = 200; f <= 260; ++f) {
            tones.push_back(static_cast<double>(f));
        }
        tones.push_back(233.0);
        tones.push_back(311.0);
        for (const int voices : {1, 2}) {
            double worst = 0.0;
            double worstHz = 0.0;
            double lvlLo = 1e9;
            double lvlHi = -1e9;
            for (const double f : tones) {
                PfRig rig(pfTones(total, {{0.0, f, 0.5f}}), [voices](PatternFreezeMode& m) {
                    pfFreshDefaults(m);
                    m.setPatternType(PatternType::HarmonicDrones);
                    m.setDroneVoiceCount(voices);
                    m.setDroneInterval(PitchInterval::Octave);
                    m.setDroneDrift(0.0f);
                });
                rig.runTo(total);
                const double depth = pfDepthMaxMinDb(rig.out(), pfAt(3.0), total);
                const double lvl =
                    pfDbfs(pfRms(rig.out(), pfAt(3.0), total)) - pfDbfs(pfRms(rig.in(), pfAt(3.0), total));
                if (depth > worst) {
                    worst = depth;
                    worstHz = f;
                }
                lvlLo = std::min(lvlLo, lvl);
                lvlHi = std::max(lvlHi, lvl);
            }
            std::printf("HarmonicDrones sweep voices %d | worst 10 ms depth %.2f dB (at %.0f Hz) | level %+.2f .. %+.2f dB\n",
                        voices, worst, worstHz, lvlLo, lvlHi);
        }

        const auto drum = pfDrumLoop(total);
        PfRig rig(drum, [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::HarmonicDrones);
            m.setDroneVoiceCount(1);
            m.setDroneDrift(0.0f);
        });
        rig.runTo(total);
        const double droneDepth = pfDepthP95P5Db(rig.out(), pfAt(3.0), total);
        const double windowDepth = pfDepthP95P5Db(drum, pfAt(2.0), pfAt(4.0));
        std::printf("HarmonicDrones drum | drone p95/p5 %.1f dB, drum window %.1f dB, difference %.1f dB\n", droneDepth,
                    windowDepth, windowDepth - droneDepth);
    }

    // ---- Noise: block, silence -----------------------------------------------------------------------------------
    {
        auto noise = [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setPatternType(PatternType::NoiseBursts);
        };
        PfRig small(input, noise);
        PfRig large(input, noise);
        small.runTo(total, 64);
        large.runTo(total, 512);
        std::printf("NoiseBursts block | RMS(diff 64 vs 512) / RMS %.2e\n",
                    pfRms(pfDiff(small.out(), large.out()), 0, total) / pfRms(large.out(), 0, total));

        const size_t total12 = pfAt(12.0);
        const auto silence = pfTones(total12, {{0.0, 233.0, 0.5f}, {2.0, 0.0, 0.0f}});
        PfRig playing(silence, noise);
        playing.runTo(total12);
        PfRig stopped(silence, noise);
        stopped.stop();
        stopped.runTo(total12);
        std::printf("NoiseBursts silence | playing: wet [4.5, 6 s) %.1f dBFS, active %3.0f%% | stopped: wet [6.5, 12 s) "
                    "%.1f dBFS\n",
                    pfDbfs(pfRms(playing.out(), pfAt(4.5), pfAt(6.0))),
                    100.0 * pfActiveFraction(playing.out(), pfAt(4.5), pfAt(6.0), kPfSineRms),
                    pfDbfs(pfRms(stopped.out(), pfAt(6.5), total12)));
    }

    // ---- capped (80 BPM, 2/1) and 128 BPM --------------------------------------------------------------------------
    {
        const size_t cycle = 264600;
        auto in = pfTones(cycle + 512, {{0.0, 233.0, 0.1f}});
        in[0] = 1.0f;
        in[cycle] = 1.0f;
        PfRig rig(in, [](PatternFreezeMode& m) {
            pfFreshDefaults(m);
            m.setCaptureLength(CaptureLength::DoubleWhole);
        });
        rig.bpm = 80.0;
        rig.runTo(cycle + 512);
        float l = 0.0f;
        float r = 0.0f;
        rig.mode().peekWindow(0, l, r);
        std::printf("capped (80 BPM, 2/1) | window length %lld | peekWindow(0) %.6f\n",
                    static_cast<long long>(rig.mode().getWindowLengthSamples()), static_cast<double>(l));
    }
    {
        const size_t end = 661500 + 512;
        PfRig rig(pfTones(end, {{0.0, 233.0, 0.5f}}), pfFreshDefaults);
        rig.bpm = 128.0;
        std::vector<long long> lengths;
        unsigned long long last = 0;
        while (rig.pos() < end) {
            rig.runTo(rig.pos() + kPfBlock);
            const auto c = static_cast<unsigned long long>(rig.mode().getCaptureCount());
            if (c != last && rig.mode().getWindowFrom() == 0) {
                lengths.push_back(static_cast<long long>(rig.mode().getWindowLengthSamples()));
            }
            last = c;
        }
        std::sort(lengths.begin(), lengths.end());
        lengths.erase(std::unique(lengths.begin(), lengths.end()), lengths.end());
        std::printf("128 BPM, 8 bars | captures %llu | complete window lengths:", last);
        for (const long long len : lengths) {
            std::printf(" %lld", len);
        }
        std::printf("\n");
    }
}
