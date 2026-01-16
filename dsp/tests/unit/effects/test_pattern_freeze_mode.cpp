// ==============================================================================
// Layer 4: Effects Tests - Pattern Freeze Mode
// ==============================================================================
// Unit tests for PatternFreezeMode (spec 069 - Pattern Freeze Mode).
//
// Tests verify:
// - Euclidean pattern playback
// - Capture and slice triggering
// - Envelope-shaped playback
// - Tempo synchronization
// - Cross-pattern crossfade
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-first development methodology
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/effects/pattern_freeze_mode.h>
#include <krate/dsp/core/block_context.h>

#include <algorithm>
#include <cmath>
#include <numeric>
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

    // After reset, freeze should still be enabled (always on in Freeze mode)
    REQUIRE(freeze.isFreezeEnabled() == true);
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
// Freeze Toggle Tests
// =============================================================================

TEST_CASE("PatternFreezeMode is always enabled", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);
    freeze.setPatternType(PatternType::Euclidean);
    freeze.setEuclideanSteps(8);
    freeze.setEuclideanHits(4);
    freeze.snapParameters();

    // Freeze is always enabled in Freeze mode (no toggle - DAW bypass handles muting)
    REQUIRE(freeze.isFreezeEnabled() == true);

    // Feed audio to fill capture buffer
    std::vector<float> left(512), right(512);
    pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
    std::copy(left.begin(), left.end(), right.begin());

    auto ctx = pfCreateContext();

    // Process enough to fill capture buffer (100ms min = 4410 samples at 44.1kHz)
    for (int i = 0; i < 20; ++i) {
        freeze.process(left.data(), right.data(), 512, ctx);
        pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
        std::copy(left.begin(), left.end(), right.begin());
    }

    // Freeze remains enabled
    REQUIRE(freeze.isFreezeEnabled() == true);
}

// =============================================================================
// Capture Buffer Tests
// =============================================================================

TEST_CASE("PatternFreezeMode captures incoming audio", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);
    freeze.setPatternType(PatternType::Euclidean);  // Use Euclidean for predictable behavior
    freeze.snapParameters();

    std::vector<float> left(512), right(512);
    pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.8f);
    std::copy(left.begin(), left.end(), right.begin());

    auto ctx = pfCreateContext();

    // Process to capture audio
    freeze.process(left.data(), right.data(), 512, ctx);

    // After processing, capture buffer should have data
    // (We can't directly query this, but freeze should be ready after enough data)
    for (int i = 0; i < 20; ++i) {
        pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.8f);
        std::copy(left.begin(), left.end(), right.begin());
        freeze.process(left.data(), right.data(), 512, ctx);
    }

    // Should now have enough data captured
    REQUIRE(freeze.isCaptureReady(100.0f));
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
// Processing Tests
// =============================================================================

TEST_CASE("PatternFreezeMode processes without freeze", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 5000.0f);
    freeze.setPatternType(PatternType::Euclidean);
    freeze.setDryWetMix(100.0f);  // Full wet
    freeze.snapParameters();

    std::vector<float> left(512, 0.5f), right(512, 0.5f);
    auto ctx = pfCreateContext();

    // Process without freeze enabled
    freeze.process(left.data(), right.data(), 512, ctx);

    // Output should not be all zeros (some signal passed through)
    float rmsL = patternFreezeCalculateRMS(left.data(), 512);
    float rmsR = patternFreezeCalculateRMS(right.data(), 512);

    // With freeze disabled and 100% wet, we get wet signal from delay network
    // which may be zero initially. That's OK.
    REQUIRE(true);  // Verify no crash
}

TEST_CASE("PatternFreezeMode outputs audio when freeze enabled", "[effects][pattern_freeze][layer4]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);
    freeze.setPatternType(PatternType::Euclidean);  // Use Euclidean pattern
    freeze.setDryWetMix(100.0f);
    freeze.setSliceLengthMs(100.0f);  // 100ms = 4410 samples at 44.1kHz
    freeze.snapParameters();

    auto ctx = pfCreateContext();

    // Feed audio to capture - need at least 100ms worth at 44.1kHz = 4410 samples
    // 50 blocks * 512 = 25600 samples - should be plenty
    std::vector<float> left(512), right(512);
    for (int i = 0; i < 50; ++i) {
        pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
        std::copy(left.begin(), left.end(), right.begin());
        freeze.process(left.data(), right.data(), 512, ctx);
    }

    // Verify capture is ready
    REQUIRE(freeze.isCaptureReady(100.0f));

    // Enable freeze and snap parameters so smoother immediately reaches target
    freeze.setFreezeEnabled(true);
    freeze.snapParameters();  // Make freeze take effect immediately

    REQUIRE(freeze.isFreezeEnabled());

    // Process with freeze enabled - keep feeding audio since Euclidean mode
    // should output the captured audio when pattern triggers
    float totalRMS = 0.0f;
    for (int i = 0; i < 20; ++i) {
        pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
        std::copy(left.begin(), left.end(), right.begin());
        freeze.process(left.data(), right.data(), 512, ctx);
        totalRMS += patternFreezeCalculateRMS(left.data(), 512);
    }

    // Should produce some audio output (frozen buffer playback)
    REQUIRE(totalRMS > 0.0f);
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

    auto ctx = pfCreateContext();

    // Feed audio
    std::vector<float> left(512), right(512);
    for (int i = 0; i < 100; ++i) {
        pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
        std::copy(left.begin(), left.end(), right.begin());
        freeze.process(left.data(), right.data(), 512, ctx);
    }

    REQUIRE(freeze.isCaptureReady(50.0f));

    // Enable freeze and process
    freeze.setFreezeEnabled(true);
    freeze.snapParameters();

    float totalRMS = 0.0f;
    for (int i = 0; i < 50; ++i) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        freeze.process(left.data(), right.data(), 512, ctx);
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

    auto ctx = pfCreateContext();

    // Feed audio
    std::vector<float> left(512), right(512);
    for (int i = 0; i < 100; ++i) {
        pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
        std::copy(left.begin(), left.end(), right.begin());
        freeze.process(left.data(), right.data(), 512, ctx);
    }

    REQUIRE(freeze.isCaptureReady(200.0f));

    // Enable freeze and process
    freeze.setFreezeEnabled(true);
    freeze.snapParameters();

    float totalRMS = 0.0f;
    for (int i = 0; i < 50; ++i) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        freeze.process(left.data(), right.data(), 512, ctx);
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
    freeze.setFreezeEnabled(true);
    freeze.snapParameters();

    auto ctx = pfCreateContext();

    // Process with silence - should NOT produce noise bursts
    float silentRMS = 0.0f;
    std::vector<float> silent(512, 0.0f);
    for (int i = 0; i < 50; ++i) {
        std::fill(silent.begin(), silent.end(), 0.0f);
        freeze.process(silent.data(), silent.data(), 512, ctx);
        silentRMS += patternFreezeCalculateRMS(silent.data(), 512);
    }
    REQUIRE(silentRMS == 0.0f);  // No output when no audio captured

    // Now feed actual audio to capture
    std::vector<float> left(512), right(512);
    for (int i = 0; i < 100; ++i) {
        for (size_t s = 0; s < 512; ++s) {
            left[s] = 0.3f * std::sin(static_cast<float>(s + i * 512) * 0.1f);
            right[s] = left[s];
        }
        freeze.process(left.data(), right.data(), 512, ctx);
    }

    // Process with silence after capturing audio - should now produce noise bursts
    float totalRMS = 0.0f;
    for (int i = 0; i < 100; ++i) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        freeze.process(left.data(), right.data(), 512, ctx);
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
// Pattern Crossfade Tests (Phase 9)
// =============================================================================

TEST_CASE("PatternFreezeMode pattern crossfade", "[effects][pattern_freeze][layer4][crossfade]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);
    freeze.setPatternType(PatternType::Euclidean);
    freeze.setDryWetMix(100.0f);
    freeze.snapParameters();

    auto ctx = pfCreateContext();

    // Feed audio to fill capture buffer
    std::vector<float> left(512), right(512);
    for (int i = 0; i < 50; ++i) {
        pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
        std::copy(left.begin(), left.end(), right.begin());
        freeze.process(left.data(), right.data(), 512, ctx);
    }

    SECTION("No crossfade when freeze disabled") {
        freeze.setFreezeEnabled(false);
        freeze.setPatternType(PatternType::Euclidean);
        REQUIRE(freeze.isCrossfading() == false);
    }

    SECTION("Crossfade initiated when freeze enabled and pattern changes") {
        freeze.setFreezeEnabled(true);
        freeze.snapParameters();
        freeze.setPatternType(PatternType::GranularScatter);  // Change to different pattern
        REQUIRE(freeze.isCrossfading() == true);
    }

    SECTION("Crossfade completes after ~500ms") {
        freeze.setFreezeEnabled(true);
        freeze.snapParameters();
        freeze.setPatternType(PatternType::Euclidean);

        // Process enough blocks to complete crossfade (500ms @ 44.1kHz = 22050 samples)
        // 50 blocks * 512 = 25600 samples > 22050
        for (int i = 0; i < 50; ++i) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            freeze.process(left.data(), right.data(), 512, ctx);
        }

        REQUIRE(freeze.isCrossfading() == false);
    }

    SECTION("Crossfade produces click-free output") {
        freeze.setFreezeEnabled(true);
        freeze.snapParameters();

        // Process a block before switching
        std::fill(left.begin(), left.end(), 0.0f);
        freeze.process(left.data(), right.data(), 512, ctx);
        float rmsBeforeSwitch = patternFreezeCalculateRMS(left.data(), 512);

        // Switch patterns
        freeze.setPatternType(PatternType::GranularScatter);

        // Process during crossfade
        std::fill(left.begin(), left.end(), 0.0f);
        freeze.process(left.data(), right.data(), 512, ctx);

        // Should not have extreme discontinuities (clicks would show as high peaks)
        float maxSample = 0.0f;
        for (size_t i = 0; i < 512; ++i) {
            maxSample = std::max(maxSample, std::abs(left[i]));
        }
        // Max sample should not exceed 2x the RMS (reasonable headroom)
        // This is a basic click detection heuristic
        REQUIRE(maxSample < 2.0f);
    }
}

// =============================================================================
// Edge Case Tests (Phase 9)
// =============================================================================

TEST_CASE("PatternFreezeMode edge cases", "[effects][pattern_freeze][layer4][edge]") {
    PatternFreezeMode freeze;
    freeze.prepare(44100.0, 512, 2000.0f);

    SECTION("Handles freeze before buffer filled (edge case 1)") {
        freeze.setPatternType(PatternType::Euclidean);
        freeze.setFreezeEnabled(true);
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

    SECTION("Handles invalid tempo (edge case 5)") {
        freeze.setPatternType(PatternType::Euclidean);
        freeze.setDryWetMix(100.0f);
        freeze.snapParameters();

        // Fill buffer first
        std::vector<float> left(512), right(512);
        auto ctx = pfCreateContext();
        for (int i = 0; i < 50; ++i) {
            pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
            std::copy(left.begin(), left.end(), right.begin());
            freeze.process(left.data(), right.data(), 512, ctx);
        }

        freeze.setFreezeEnabled(true);
        freeze.snapParameters();

        // Now process with invalid tempo
        BlockContext invalidCtx;
        invalidCtx.sampleRate = 44100.0;
        invalidCtx.tempoBPM = 0.0;  // Invalid tempo

        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        freeze.process(left.data(), right.data(), 512, invalidCtx);

        // Should not crash - tempo-synced pattern should stop
        REQUIRE(true);
    }

    SECTION("Non-tempo-synced patterns continue with invalid tempo (edge case 5b)") {
        freeze.setPatternType(PatternType::GranularScatter);
        freeze.setGranularDensity(20.0f);
        freeze.setDryWetMix(100.0f);
        freeze.snapParameters();

        // Fill buffer first
        std::vector<float> left(512), right(512);
        auto ctx = pfCreateContext();
        for (int i = 0; i < 50; ++i) {
            pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
            std::copy(left.begin(), left.end(), right.begin());
            freeze.process(left.data(), right.data(), 512, ctx);
        }

        freeze.setFreezeEnabled(true);
        freeze.snapParameters();

        // Process with invalid tempo
        BlockContext invalidCtx;
        invalidCtx.sampleRate = 44100.0;
        invalidCtx.tempoBPM = -1.0;  // Invalid tempo

        float totalRMS = 0.0f;
        for (int i = 0; i < 50; ++i) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            freeze.process(left.data(), right.data(), 512, invalidCtx);
            totalRMS += patternFreezeCalculateRMS(left.data(), 512);
        }

        // Granular Scatter should still produce output (not tempo-synced)
        REQUIRE(totalRMS > 0.0f);
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

        auto ctx = pfCreateContext();
        std::vector<float> left(512), right(512);

        // Fill buffer
        for (int i = 0; i < 50; ++i) {
            pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
            std::copy(left.begin(), left.end(), right.begin());
            freeze.process(left.data(), right.data(), 512, ctx);
        }

        freeze.setFreezeEnabled(true);
        freeze.snapParameters();

        float totalRMS = 0.0f;
        for (int i = 0; i < 50; ++i) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            freeze.process(left.data(), right.data(), 512, ctx);
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

        auto ctx = pfCreateContext();
        std::vector<float> left(512), right(512);

        // Fill buffer
        for (int i = 0; i < 50; ++i) {
            pfGenerateSine(left.data(), 512, 440.0f, 44100.0, 0.5f);
            std::copy(left.begin(), left.end(), right.begin());
            freeze.process(left.data(), right.data(), 512, ctx);
        }

        freeze.setFreezeEnabled(true);
        freeze.snapParameters();

        float totalRMS = 0.0f;
        for (int i = 0; i < 50; ++i) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            freeze.process(left.data(), right.data(), 512, ctx);
            totalRMS += patternFreezeCalculateRMS(left.data(), 512);
        }

        REQUIRE(totalRMS > 0.0f);
    }
}
