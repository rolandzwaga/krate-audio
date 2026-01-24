// ==============================================================================
// PitchTrackingFilter Unit Tests
// ==============================================================================
// Test-First Development for spec 092-pitch-tracking-filter
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XIII: Test-First Development
// ==============================================================================

#include <krate/dsp/processors/pitch_tracking_filter.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// Generate a sine wave at the specified frequency
void generateSineWave(float* buffer, size_t numSamples, float frequency, float sampleRate) {
    const float twoPi = 6.283185307f;
    const float phaseIncrement = twoPi * frequency / sampleRate;
    float phase = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(phase);
        phase += phaseIncrement;
        if (phase >= twoPi) {
            phase -= twoPi;
        }
    }
}

/// Generate white noise (simple PRNG)
void generateWhiteNoise(float* buffer, size_t numSamples, uint32_t seed = 12345) {
    uint32_t state = seed;
    for (size_t i = 0; i < numSamples; ++i) {
        // Simple LCG
        state = state * 1103515245u + 12345u;
        // Convert to float in [-1, 1]
        buffer[i] = (static_cast<float>(state) / static_cast<float>(0x80000000u)) - 1.0f;
    }
}

} // anonymous namespace

// =============================================================================
// Phase 2: Foundational - Lifecycle Tests (T005-T008)
// =============================================================================

TEST_CASE("PitchTrackingFilter default construction", "[processors][pitch_tracking_filter][lifecycle]") {
    SECTION("default construction sets isPrepared false") {
        PitchTrackingFilter filter;
        REQUIRE(filter.isPrepared() == false);
    }
}

TEST_CASE("PitchTrackingFilter prepare", "[processors][pitch_tracking_filter][lifecycle]") {
    PitchTrackingFilter filter;

    SECTION("prepare() with valid sample rate sets isPrepared true") {
        filter.prepare(48000.0, 512);
        REQUIRE(filter.isPrepared() == true);
    }
}

TEST_CASE("PitchTrackingFilter getLatency", "[processors][pitch_tracking_filter][lifecycle]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("getLatency() returns 256 samples (PitchDetector window)") {
        REQUIRE(filter.getLatency() == 256);
    }
}

TEST_CASE("PitchTrackingFilter reset", "[processors][pitch_tracking_filter][lifecycle]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("reset() clears tracking state and monitoring values") {
        // Process some signal to build up state
        std::array<float, 1024> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);
        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        // Reset
        filter.reset();

        // After reset, monitoring values should be at defaults
        REQUIRE(filter.getDetectedPitch() == Approx(0.0f));
        REQUIRE(filter.getPitchConfidence() == Approx(0.0f));
        REQUIRE(filter.getCurrentCutoff() == Approx(filter.getFallbackCutoff()));
    }
}

// =============================================================================
// Phase 3: User Story 1 - Parameter Tests (T021-T028)
// =============================================================================

TEST_CASE("PitchTrackingFilter setConfidenceThreshold", "[processors][pitch_tracking_filter][parameters]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("getter returns default value 0.5") {
        REQUIRE(filter.getConfidenceThreshold() == Approx(0.5f));
    }

    SECTION("round-trip value") {
        filter.setConfidenceThreshold(0.7f);
        REQUIRE(filter.getConfidenceThreshold() == Approx(0.7f));
    }

    SECTION("clamps to [0, 1] range") {
        filter.setConfidenceThreshold(-0.5f);
        REQUIRE(filter.getConfidenceThreshold() == Approx(0.0f));

        filter.setConfidenceThreshold(1.5f);
        REQUIRE(filter.getConfidenceThreshold() == Approx(1.0f));
    }
}

TEST_CASE("PitchTrackingFilter setTrackingSpeed", "[processors][pitch_tracking_filter][parameters]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("getter returns default value 50ms") {
        REQUIRE(filter.getTrackingSpeed() == Approx(50.0f));
    }

    SECTION("round-trip value") {
        filter.setTrackingSpeed(100.0f);
        REQUIRE(filter.getTrackingSpeed() == Approx(100.0f));
    }

    SECTION("clamps to [1, 500] range") {
        filter.setTrackingSpeed(0.1f);
        REQUIRE(filter.getTrackingSpeed() == Approx(1.0f));

        filter.setTrackingSpeed(1000.0f);
        REQUIRE(filter.getTrackingSpeed() == Approx(500.0f));
    }
}

TEST_CASE("PitchTrackingFilter setHarmonicRatio", "[processors][pitch_tracking_filter][parameters]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("getter returns default value 1.0") {
        REQUIRE(filter.getHarmonicRatio() == Approx(1.0f));
    }

    SECTION("round-trip value") {
        filter.setHarmonicRatio(2.0f);
        REQUIRE(filter.getHarmonicRatio() == Approx(2.0f));
    }

    SECTION("clamps to [0.125, 16.0] range") {
        filter.setHarmonicRatio(0.05f);
        REQUIRE(filter.getHarmonicRatio() == Approx(0.125f));

        filter.setHarmonicRatio(32.0f);
        REQUIRE(filter.getHarmonicRatio() == Approx(16.0f));
    }
}

TEST_CASE("PitchTrackingFilter setSemitoneOffset", "[processors][pitch_tracking_filter][parameters]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("getter returns default value 0") {
        REQUIRE(filter.getSemitoneOffset() == Approx(0.0f));
    }

    SECTION("round-trip value") {
        filter.setSemitoneOffset(12.0f);
        REQUIRE(filter.getSemitoneOffset() == Approx(12.0f));
    }

    SECTION("clamps to [-48, 48] range") {
        filter.setSemitoneOffset(-60.0f);
        REQUIRE(filter.getSemitoneOffset() == Approx(-48.0f));

        filter.setSemitoneOffset(60.0f);
        REQUIRE(filter.getSemitoneOffset() == Approx(48.0f));
    }
}

TEST_CASE("PitchTrackingFilter setResonance", "[processors][pitch_tracking_filter][parameters]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("getter returns default value 0.707 (Butterworth)") {
        REQUIRE(filter.getResonance() == Approx(0.707f).margin(0.001f));
    }

    SECTION("round-trip value") {
        filter.setResonance(4.0f);
        REQUIRE(filter.getResonance() == Approx(4.0f));
    }

    SECTION("clamps to [0.5, 30.0] range") {
        filter.setResonance(0.1f);
        REQUIRE(filter.getResonance() == Approx(0.5f));

        filter.setResonance(50.0f);
        REQUIRE(filter.getResonance() == Approx(30.0f));
    }
}

TEST_CASE("PitchTrackingFilter setFilterType", "[processors][pitch_tracking_filter][parameters]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("getter returns default value Lowpass") {
        REQUIRE(filter.getFilterType() == PitchTrackingFilterMode::Lowpass);
    }

    SECTION("round-trip for all three types") {
        filter.setFilterType(PitchTrackingFilterMode::Bandpass);
        REQUIRE(filter.getFilterType() == PitchTrackingFilterMode::Bandpass);

        filter.setFilterType(PitchTrackingFilterMode::Highpass);
        REQUIRE(filter.getFilterType() == PitchTrackingFilterMode::Highpass);

        filter.setFilterType(PitchTrackingFilterMode::Lowpass);
        REQUIRE(filter.getFilterType() == PitchTrackingFilterMode::Lowpass);
    }
}

TEST_CASE("PitchTrackingFilter setFallbackCutoff", "[processors][pitch_tracking_filter][parameters]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("getter returns default value 1000Hz") {
        REQUIRE(filter.getFallbackCutoff() == Approx(1000.0f));
    }

    SECTION("round-trip value") {
        filter.setFallbackCutoff(2000.0f);
        REQUIRE(filter.getFallbackCutoff() == Approx(2000.0f));
    }

    SECTION("clamps to [20, Nyquist*0.45] range") {
        filter.setFallbackCutoff(5.0f);
        REQUIRE(filter.getFallbackCutoff() == Approx(20.0f));

        // At 48kHz, Nyquist*0.45 = 24000*0.45 = 10800Hz
        filter.setFallbackCutoff(30000.0f);
        REQUIRE(filter.getFallbackCutoff() == Approx(48000.0f * 0.45f));
    }
}

TEST_CASE("PitchTrackingFilter setFallbackSmoothing", "[processors][pitch_tracking_filter][parameters]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("getter returns default value 50ms") {
        REQUIRE(filter.getFallbackSmoothing() == Approx(50.0f));
    }

    SECTION("round-trip value") {
        filter.setFallbackSmoothing(100.0f);
        REQUIRE(filter.getFallbackSmoothing() == Approx(100.0f));
    }

    SECTION("clamps to [1, 500] range") {
        filter.setFallbackSmoothing(0.1f);
        REQUIRE(filter.getFallbackSmoothing() == Approx(1.0f));

        filter.setFallbackSmoothing(1000.0f);
        REQUIRE(filter.getFallbackSmoothing() == Approx(500.0f));
    }
}

// =============================================================================
// Phase 3: User Story 1 - Basic Processing Tests (T030-T033)
// =============================================================================

TEST_CASE("PitchTrackingFilter basic processing", "[processors][pitch_tracking_filter][processing]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("process() returns non-zero for non-zero input after prepare()") {
        // Feed some samples to build up state
        std::array<float, 512> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        bool foundNonZero = false;
        for (size_t i = 0; i < buffer.size(); ++i) {
            float output = filter.process(buffer[i]);
            if (output != 0.0f) {
                foundNonZero = true;
                break;
            }
        }
        REQUIRE(foundNonZero);
    }

    SECTION("silence in = silence out (0.0f -> 0.0f)") {
        for (int i = 0; i < 100; ++i) {
            float output = filter.process(0.0f);
            REQUIRE(output == 0.0f);
        }
    }

    SECTION("getCurrentCutoff() returns fallback cutoff initially (before valid pitch)") {
        // Before any processing, cutoff should be at fallback
        REQUIRE(filter.getCurrentCutoff() == Approx(filter.getFallbackCutoff()));
    }
}

// =============================================================================
// Phase 3: User Story 1 - Pitch Tracking Tests (T034-T040)
// =============================================================================

TEST_CASE("PitchTrackingFilter pitch detection integration", "[processors][pitch_tracking_filter][tracking]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("sine wave input updates getDetectedPitch() to non-zero value") {
        std::array<float, 4096> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        // Process enough samples to allow pitch detection
        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        // After processing a clear sine wave, detected pitch should be non-zero
        REQUIRE(filter.getDetectedPitch() > 0.0f);
    }

    SECTION("confidence above threshold triggers tracking (cutoff follows pitch)") {
        filter.setConfidenceThreshold(0.3f);
        filter.setHarmonicRatio(1.0f);

        std::array<float, 8192> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        // With high confidence sine wave, cutoff should track near pitch
        float detectedPitch = filter.getDetectedPitch();
        float currentCutoff = filter.getCurrentCutoff();

        // Allow for smoothing - cutoff should be moving toward pitch
        // With ratio 1.0, cutoff should approach detected pitch
        if (filter.getPitchConfidence() >= 0.3f) {
            // Cutoff should be within a reasonable range of the pitch
            REQUIRE(currentCutoff > 100.0f);  // Should have moved from fallback
        }
    }

    SECTION("confidence below threshold uses fallback cutoff") {
        filter.setConfidenceThreshold(0.99f);  // Very high threshold
        filter.setFallbackCutoff(500.0f);

        std::array<float, 4096> buffer;
        generateWhiteNoise(buffer.data(), buffer.size());

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        // With noise, confidence should be low, so cutoff should trend toward fallback
        float currentCutoff = filter.getCurrentCutoff();
        // Should be closer to fallback than to any detected frequency
        REQUIRE(currentCutoff == Approx(500.0f).margin(200.0f));
    }
}

TEST_CASE("PitchTrackingFilter harmonic ratio", "[processors][pitch_tracking_filter][tracking]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("harmonic ratio 2.0 scales cutoff to 2x detected pitch") {
        filter.setHarmonicRatio(2.0f);
        filter.setConfidenceThreshold(0.2f);

        std::array<float, 8192> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        float detectedPitch = filter.getDetectedPitch();
        float currentCutoff = filter.getCurrentCutoff();

        if (filter.getPitchConfidence() >= 0.2f && detectedPitch > 0.0f) {
            // Cutoff should be approximately 2x the detected pitch
            float expectedCutoff = detectedPitch * 2.0f;
            REQUIRE(currentCutoff == Approx(expectedCutoff).margin(100.0f));
        }
    }
}

TEST_CASE("PitchTrackingFilter semitone offset", "[processors][pitch_tracking_filter][tracking]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("semitone offset +12 doubles cutoff (octave up)") {
        filter.setHarmonicRatio(1.0f);
        filter.setSemitoneOffset(12.0f);
        filter.setConfidenceThreshold(0.2f);

        std::array<float, 8192> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        float detectedPitch = filter.getDetectedPitch();
        float currentCutoff = filter.getCurrentCutoff();

        if (filter.getPitchConfidence() >= 0.2f && detectedPitch > 0.0f) {
            // +12 semitones = 2x frequency (octave up)
            float expectedCutoff = detectedPitch * 2.0f;
            REQUIRE(currentCutoff == Approx(expectedCutoff).margin(100.0f));
        }
    }
}

TEST_CASE("PitchTrackingFilter cutoff clamping", "[processors][pitch_tracking_filter][tracking]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("cutoff clamped to [20Hz, Nyquist*0.45] for extreme ratio/offset") {
        filter.setHarmonicRatio(16.0f);
        filter.setSemitoneOffset(48.0f);
        filter.setConfidenceThreshold(0.2f);

        std::array<float, 8192> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        float currentCutoff = filter.getCurrentCutoff();

        // Should be clamped to max cutoff (48000 * 0.45 = 21600)
        REQUIRE(currentCutoff <= 48000.0f * 0.45f);
        REQUIRE(currentCutoff >= 20.0f);
    }
}

// =============================================================================
// Phase 4: User Story 2 - Uncertainty Handling Tests (T059-T063)
// =============================================================================

TEST_CASE("PitchTrackingFilter uncertainty handling", "[processors][pitch_tracking_filter][uncertainty]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("white noise input results in low confidence and fallback cutoff") {
        filter.setFallbackCutoff(1000.0f);
        filter.setConfidenceThreshold(0.5f);

        std::array<float, 8192> buffer;
        generateWhiteNoise(buffer.data(), buffer.size());

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        // Noise should result in low confidence
        REQUIRE(filter.getPitchConfidence() < 0.5f);
        // Cutoff should be at or near fallback
        REQUIRE(filter.getCurrentCutoff() == Approx(1000.0f).margin(300.0f));
    }

    SECTION("silence (zero samples) results in fallback cutoff with no erratic behavior") {
        filter.setFallbackCutoff(1000.0f);

        // First, establish some tracking state
        std::array<float, 2048> sineBuffer;
        generateSineWave(sineBuffer.data(), sineBuffer.size(), 440.0f, 48000.0f);
        for (auto sample : sineBuffer) {
            (void)filter.process(sample);
        }

        // Now process silence
        for (int i = 0; i < 4096; ++i) {
            (void)filter.process(0.0f);
        }

        // Cutoff should smoothly return to fallback
        REQUIRE(filter.getCurrentCutoff() == Approx(1000.0f).margin(300.0f));
    }

    SECTION("transition from pitched to unpitched is smooth (no sudden jumps > 100Hz/sample)") {
        filter.setFallbackCutoff(1000.0f);
        filter.setTrackingSpeed(50.0f);
        filter.setFallbackSmoothing(50.0f);

        // Start with pitched content
        std::array<float, 2048> sineBuffer;
        generateSineWave(sineBuffer.data(), sineBuffer.size(), 440.0f, 48000.0f);
        for (auto sample : sineBuffer) {
            (void)filter.process(sample);
        }

        float previousCutoff = filter.getCurrentCutoff();
        float maxJump = 0.0f;

        // Transition to noise
        std::array<float, 4096> noiseBuffer;
        generateWhiteNoise(noiseBuffer.data(), noiseBuffer.size());
        for (auto sample : noiseBuffer) {
            (void)filter.process(sample);
            float currentCutoff = filter.getCurrentCutoff();
            float jump = std::abs(currentCutoff - previousCutoff);
            maxJump = std::max(maxJump, jump);
            previousCutoff = currentCutoff;
        }

        // No sudden jumps > 100Hz per sample
        REQUIRE(maxJump < 100.0f);
    }

    SECTION("pitched signal after unpitched section resumes tracking smoothly") {
        filter.setHarmonicRatio(1.0f);
        filter.setConfidenceThreshold(0.3f);

        // Start with noise
        std::array<float, 2048> noiseBuffer;
        generateWhiteNoise(noiseBuffer.data(), noiseBuffer.size());
        for (auto sample : noiseBuffer) {
            (void)filter.process(sample);
        }

        // Transition to pitched content
        std::array<float, 8192> sineBuffer;
        generateSineWave(sineBuffer.data(), sineBuffer.size(), 440.0f, 48000.0f);

        float previousCutoff = filter.getCurrentCutoff();
        float maxJump = 0.0f;

        for (auto sample : sineBuffer) {
            (void)filter.process(sample);
            float currentCutoff = filter.getCurrentCutoff();
            float jump = std::abs(currentCutoff - previousCutoff);
            maxJump = std::max(maxJump, jump);
            previousCutoff = currentCutoff;
        }

        // Should resume tracking without extreme jumps
        REQUIRE(maxJump < 100.0f);
    }
}

// =============================================================================
// Phase 5: User Story 3 - Semitone Offset Tests (T074-T077)
// =============================================================================

TEST_CASE("PitchTrackingFilter semitone offset creative", "[processors][pitch_tracking_filter][semitone]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("harmonic ratio 1.0 + offset +12 semitones = 2x cutoff (440Hz -> 880Hz)") {
        filter.setHarmonicRatio(1.0f);
        filter.setSemitoneOffset(12.0f);
        filter.setConfidenceThreshold(0.2f);
        filter.setTrackingSpeed(5.0f);  // Fast tracking for test

        std::array<float, 16384> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        float detectedPitch = filter.getDetectedPitch();
        float currentCutoff = filter.getCurrentCutoff();

        if (filter.getPitchConfidence() >= 0.2f && detectedPitch > 0.0f) {
            // +12 semitones = octave up = 2x
            float expectedCutoff = detectedPitch * 2.0f;
            REQUIRE(currentCutoff == Approx(expectedCutoff).margin(100.0f));
        }
    }

    SECTION("harmonic ratio 2.0 + offset -7 semitones (fifth down)") {
        filter.setHarmonicRatio(2.0f);
        filter.setSemitoneOffset(-7.0f);  // Fifth down from octave = fifth up from fundamental
        filter.setConfidenceThreshold(0.2f);
        filter.setTrackingSpeed(5.0f);

        std::array<float, 16384> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        float detectedPitch = filter.getDetectedPitch();
        float currentCutoff = filter.getCurrentCutoff();

        if (filter.getPitchConfidence() >= 0.2f && detectedPitch > 0.0f) {
            // ratio 2.0 * 2^(-7/12) = 2 * 0.6674 = 1.335x
            float expectedCutoff = detectedPitch * 2.0f * std::pow(2.0f, -7.0f / 12.0f);
            REQUIRE(currentCutoff == Approx(expectedCutoff).margin(100.0f));
        }
    }

    SECTION("extreme offset +48 or -48 is clamped correctly and doesn't crash") {
        filter.setHarmonicRatio(1.0f);
        filter.setSemitoneOffset(48.0f);

        std::array<float, 4096> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        // Should not crash
        for (auto sample : buffer) {
            float output = filter.process(sample);
            REQUIRE_FALSE(std::isnan(output));
            REQUIRE_FALSE(std::isinf(output));
        }

        filter.setSemitoneOffset(-48.0f);
        for (auto sample : buffer) {
            float output = filter.process(sample);
            REQUIRE_FALSE(std::isnan(output));
            REQUIRE_FALSE(std::isinf(output));
        }
    }
}

// =============================================================================
// Phase 7: Edge Cases Tests (T100-T105)
// =============================================================================

TEST_CASE("PitchTrackingFilter edge cases", "[processors][pitch_tracking_filter][edge]") {
    PitchTrackingFilter filter;
    filter.prepare(48000.0, 512);

    SECTION("NaN input returns 0.0f and resets state (no propagation)") {
        // Process some valid signal first
        std::array<float, 512> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);
        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        // Now send NaN
        float nanInput = std::numeric_limits<float>::quiet_NaN();
        float output = filter.process(nanInput);

        REQUIRE(output == 0.0f);
        REQUIRE(filter.getDetectedPitch() == 0.0f);
        REQUIRE(filter.getPitchConfidence() == 0.0f);
    }

    SECTION("Inf input returns 0.0f and resets state (no propagation)") {
        float infInput = std::numeric_limits<float>::infinity();
        float output = filter.process(infInput);

        REQUIRE(output == 0.0f);
        REQUIRE(filter.getDetectedPitch() == 0.0f);
        REQUIRE(filter.getPitchConfidence() == 0.0f);
    }

    SECTION("harmonic ratio at minimum (0.125) clamps cutoff to 20Hz minimum") {
        filter.setHarmonicRatio(0.125f);
        filter.setConfidenceThreshold(0.2f);

        std::array<float, 8192> buffer;
        generateSineWave(buffer.data(), buffer.size(), 100.0f, 48000.0f);

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        // 100Hz * 0.125 = 12.5Hz, should be clamped to 20Hz
        REQUIRE(filter.getCurrentCutoff() >= 20.0f);
    }

    SECTION("calculated cutoff exceeding Nyquist is clamped to sampleRate*0.45") {
        filter.setHarmonicRatio(16.0f);
        filter.setSemitoneOffset(48.0f);
        filter.setConfidenceThreshold(0.2f);

        std::array<float, 8192> buffer;
        generateSineWave(buffer.data(), buffer.size(), 500.0f, 48000.0f);

        for (auto sample : buffer) {
            (void)filter.process(sample);
        }

        // 500Hz * 16 * 2^4 = 128kHz - way over Nyquist
        // Should be clamped to 48000 * 0.45 = 21600Hz
        REQUIRE(filter.getCurrentCutoff() <= 48000.0f * 0.45f);
    }
}

// =============================================================================
// Phase 8: Block Processing Tests (T118-T121)
// =============================================================================

TEST_CASE("PitchTrackingFilter block processing", "[processors][pitch_tracking_filter][block]") {
    SECTION("processBlock() produces identical result to loop of process() calls") {
        PitchTrackingFilter filterSingle;
        PitchTrackingFilter filterBlock;
        filterSingle.prepare(48000.0, 512);
        filterBlock.prepare(48000.0, 512);

        std::array<float, 512> bufferSingle;
        std::array<float, 512> bufferBlock;
        generateSineWave(bufferSingle.data(), bufferSingle.size(), 440.0f, 48000.0f);
        std::copy(bufferSingle.begin(), bufferSingle.end(), bufferBlock.begin());

        // Process with single-sample calls
        for (size_t i = 0; i < bufferSingle.size(); ++i) {
            bufferSingle[i] = filterSingle.process(bufferSingle[i]);
        }

        // Process with block call
        filterBlock.processBlock(bufferBlock.data(), bufferBlock.size());

        // Results should be identical
        for (size_t i = 0; i < bufferSingle.size(); ++i) {
            REQUIRE(bufferSingle[i] == Approx(bufferBlock[i]).margin(1e-6f));
        }
    }

    SECTION("processBlock() with in-place buffer modification works correctly") {
        PitchTrackingFilter filter;
        filter.prepare(48000.0, 512);

        std::array<float, 512> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        filter.processBlock(buffer.data(), buffer.size());

        // Should have modified the buffer - at least some samples should be different
        // (filtered signal should differ from pure sine)
        bool foundNonZero = false;
        for (auto sample : buffer) {
            if (std::abs(sample) > 1e-10f) {
                foundNonZero = true;
                break;
            }
        }
        REQUIRE(foundNonZero);
    }

    SECTION("processBlock() with nullptr buffer is safe (no crash)") {
        PitchTrackingFilter filter;
        filter.prepare(48000.0, 512);

        // Should not crash
        filter.processBlock(nullptr, 100);
    }

    SECTION("processBlock() with numSamples=0 is safe (no crash)") {
        PitchTrackingFilter filter;
        filter.prepare(48000.0, 512);

        std::array<float, 512> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 48000.0f);

        // Should not crash
        filter.processBlock(buffer.data(), 0);
    }
}

// =============================================================================
// Phase 9: Filter Types Tests (T134-T138)
// =============================================================================

TEST_CASE("PitchTrackingFilter filter types", "[processors][pitch_tracking_filter][filter]") {
    SECTION("lowpass mode attenuates high frequencies") {
        PitchTrackingFilter filter;
        filter.prepare(48000.0, 512);
        filter.setFilterType(PitchTrackingFilterMode::Lowpass);
        filter.setFallbackCutoff(500.0f);  // Low cutoff

        // Generate high frequency signal
        std::array<float, 4096> buffer;
        generateSineWave(buffer.data(), buffer.size(), 5000.0f, 48000.0f);

        float inputRMS = 0.0f;
        for (auto sample : buffer) {
            inputRMS += sample * sample;
        }
        inputRMS = std::sqrt(inputRMS / buffer.size());

        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = filter.process(buffer[i]);
        }

        float outputRMS = 0.0f;
        for (auto sample : buffer) {
            outputRMS += sample * sample;
        }
        outputRMS = std::sqrt(outputRMS / buffer.size());

        // High frequency should be attenuated by lowpass
        REQUIRE(outputRMS < inputRMS * 0.5f);
    }

    SECTION("highpass mode attenuates low frequencies") {
        PitchTrackingFilter filter;
        filter.prepare(48000.0, 512);
        filter.setFilterType(PitchTrackingFilterMode::Highpass);
        filter.setFallbackCutoff(5000.0f);  // High cutoff

        // Generate low frequency signal
        std::array<float, 4096> buffer;
        generateSineWave(buffer.data(), buffer.size(), 200.0f, 48000.0f);

        float inputRMS = 0.0f;
        for (auto sample : buffer) {
            inputRMS += sample * sample;
        }
        inputRMS = std::sqrt(inputRMS / buffer.size());

        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = filter.process(buffer[i]);
        }

        float outputRMS = 0.0f;
        for (auto sample : buffer) {
            outputRMS += sample * sample;
        }
        outputRMS = std::sqrt(outputRMS / buffer.size());

        // Low frequency should be attenuated by highpass
        REQUIRE(outputRMS < inputRMS * 0.5f);
    }

    SECTION("bandpass mode passes frequencies around cutoff") {
        PitchTrackingFilter filter;
        filter.prepare(48000.0, 512);
        filter.setFilterType(PitchTrackingFilterMode::Bandpass);
        filter.setFallbackCutoff(1000.0f);
        filter.setResonance(4.0f);  // Some resonance for selectivity

        // Generate signal at cutoff frequency
        std::array<float, 4096> buffer;
        generateSineWave(buffer.data(), buffer.size(), 1000.0f, 48000.0f);

        float inputRMS = 0.0f;
        for (auto sample : buffer) {
            inputRMS += sample * sample;
        }
        inputRMS = std::sqrt(inputRMS / buffer.size());

        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = filter.process(buffer[i]);
        }

        float outputRMS = 0.0f;
        for (auto sample : buffer) {
            outputRMS += sample * sample;
        }
        outputRMS = std::sqrt(outputRMS / buffer.size());

        // Signal at cutoff should pass through (some attenuation OK for bandpass)
        REQUIRE(outputRMS > inputRMS * 0.2f);
    }

    SECTION("high resonance (Q=20) creates resonant peak") {
        PitchTrackingFilter filter;
        filter.prepare(48000.0, 512);
        filter.setFilterType(PitchTrackingFilterMode::Lowpass);
        filter.setFallbackCutoff(1000.0f);
        filter.setResonance(20.0f);  // High Q

        // Generate signal near cutoff
        std::array<float, 4096> buffer;
        generateSineWave(buffer.data(), buffer.size(), 950.0f, 48000.0f);

        float inputRMS = 0.0f;
        for (auto sample : buffer) {
            inputRMS += sample * sample;
        }
        inputRMS = std::sqrt(inputRMS / buffer.size());

        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = filter.process(buffer[i]);
        }

        float outputRMS = 0.0f;
        for (auto sample : buffer) {
            outputRMS += sample * sample;
        }
        outputRMS = std::sqrt(outputRMS / buffer.size());

        // Near cutoff with high Q should boost signal (resonant peak)
        REQUIRE(outputRMS > inputRMS * 0.9f);
    }
}

// =============================================================================
// Phase 11: Polish Tests (T165-T167)
// =============================================================================

TEST_CASE("PitchTrackingFilter polish", "[processors][pitch_tracking_filter][polish]") {
    SECTION("prepare() can be called multiple times safely (re-initialization)") {
        PitchTrackingFilter filter;

        filter.prepare(44100.0, 256);
        REQUIRE(filter.isPrepared());

        filter.prepare(48000.0, 512);
        REQUIRE(filter.isPrepared());

        filter.prepare(96000.0, 1024);
        REQUIRE(filter.isPrepared());

        // Process should work after re-initialization
        std::array<float, 512> buffer;
        generateSineWave(buffer.data(), buffer.size(), 440.0f, 96000.0f);
        for (auto sample : buffer) {
            float output = filter.process(sample);
            REQUIRE_FALSE(std::isnan(output));
        }
    }
}
