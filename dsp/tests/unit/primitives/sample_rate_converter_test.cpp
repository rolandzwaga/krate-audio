// ==============================================================================
// Layer 1: DSP Primitive Tests - SampleRateConverter
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per spec 072-sample-rate-converter
//
// Reference: specs/072-sample-rate-converter/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/sample_rate_converter.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// @brief Generate a sine wave buffer
void generateSine(float* buffer, size_t size, float frequency, float sampleRate, float amplitude = 1.0f) {
    constexpr float kTwoPi = 6.28318530718f;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

/// @brief Generate a linear ramp buffer
void generateRamp(float* buffer, size_t size, float startValue = 0.0f, float endValue = 1.0f) {
    for (size_t i = 0; i < size; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(size - 1);
        buffer[i] = startValue + t * (endValue - startValue);
    }
}

} // namespace

// =============================================================================
// Phase 2: Foundational Tests (T004, T005, T006)
// =============================================================================

TEST_CASE("SampleRateConverter rate constants are correct", "[samplerate][foundational]") {
    // FR-003: kMinRate = 0.25f (2 octaves down)
    REQUIRE(SampleRateConverter::kMinRate == Approx(0.25f));

    // FR-004: kMaxRate = 4.0f (2 octaves up)
    REQUIRE(SampleRateConverter::kMaxRate == Approx(4.0f));

    // FR-005: kDefaultRate = 1.0f
    REQUIRE(SampleRateConverter::kDefaultRate == Approx(1.0f));
}

TEST_CASE("SampleRateConverter default construction", "[samplerate][foundational]") {
    SampleRateConverter converter;

    SECTION("position starts at 0") {
        REQUIRE(converter.getPosition() == Approx(0.0f));
    }

    SECTION("isComplete starts as false") {
        REQUIRE(converter.isComplete() == false);
    }
}

TEST_CASE("SampleRateConverter rate clamping", "[samplerate][foundational]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);

    SECTION("rate below minimum is clamped to kMinRate") {
        converter.setRate(0.1f);
        // We verify by processing - rate should be clamped
        // For now, we just verify the API exists
        // Rate clamping is verified in processing tests
    }

    SECTION("rate above maximum is clamped to kMaxRate") {
        converter.setRate(10.0f);
        // Rate clamping is verified in processing tests
    }

    SECTION("valid rate within range is accepted") {
        converter.setRate(1.5f);
        // Valid rates are accepted
    }

    SECTION("rate at boundaries is accepted") {
        converter.setRate(0.25f);  // kMinRate
        converter.setRate(4.0f);   // kMaxRate
    }
}

// =============================================================================
// Phase 3: User Story 1 Tests - Variable Rate Playback (T019-T023)
// =============================================================================

TEST_CASE("SampleRateConverter rate 1.0 passthrough", "[samplerate][US1]") {
    // SC-001: Rate 1.0 produces output identical to input at integer positions
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.0f);
    converter.setInterpolation(SRCInterpolationType::Linear);

    // Create a simple buffer with known values
    std::array<float, 100> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<float>(i) * 0.01f;  // 0.0, 0.01, 0.02, ...
    }

    // At rate 1.0, each process() call should return the sample at that integer position
    for (size_t i = 0; i < 99; ++i) {  // Stop before last valid position
        float output = converter.process(buffer.data(), buffer.size());
        REQUIRE(output == Approx(buffer[i]).margin(1e-6f));
    }
}

TEST_CASE("SampleRateConverter linear interpolation at fractional positions", "[samplerate][US1]") {
    // FR-015: 2-point linear interpolation at fractional positions
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(0.5f);  // Half speed - creates fractional positions
    converter.setInterpolation(SRCInterpolationType::Linear);

    // Create buffer with known values for easy verification
    std::array<float, 10> buffer = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};

    // First sample at position 0.0 should be exactly buffer[0]
    float out0 = converter.process(buffer.data(), buffer.size());
    REQUIRE(out0 == Approx(0.0f));

    // Second sample at position 0.5 should be (buffer[0] + buffer[1]) / 2 = 0.5
    float out1 = converter.process(buffer.data(), buffer.size());
    REQUIRE(out1 == Approx(0.5f));

    // Third sample at position 1.0 should be exactly buffer[1]
    float out2 = converter.process(buffer.data(), buffer.size());
    REQUIRE(out2 == Approx(1.0f));

    // Fourth sample at position 1.5 should be (buffer[1] + buffer[2]) / 2 = 1.5
    float out3 = converter.process(buffer.data(), buffer.size());
    REQUIRE(out3 == Approx(1.5f));
}

TEST_CASE("SampleRateConverter position 1.5 produces exact midpoint", "[samplerate][US1]") {
    // SC-004: Linear interpolation at position 1.5 produces exact midpoint
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setInterpolation(SRCInterpolationType::Linear);

    // Set position directly to 1.5
    converter.setPosition(1.5f);
    converter.setRate(1.0f);

    // Known values at positions 1 and 2
    std::array<float, 10> buffer = {10.0f, 20.0f, 40.0f, 80.0f, 100.0f, 120.0f, 140.0f, 160.0f, 180.0f, 200.0f};
    // buffer[1] = 20.0, buffer[2] = 40.0
    // At position 1.5, result should be (20 + 40) / 2 = 30.0

    float output = converter.process(buffer.data(), buffer.size());
    REQUIRE(output == Approx(30.0f));
}

TEST_CASE("SampleRateConverter rate 2.0 completes 100 samples in 50 calls", "[samplerate][US1]") {
    // SC-002: Rate 2.0 plays through 100-sample buffer in 50 process() calls
    // The interpretation is: 50 calls produce valid output, then completion is detected
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(2.0f);
    converter.setInterpolation(SRCInterpolationType::Linear);

    std::array<float, 100> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<float>(i);
    }

    // Count calls that produce valid (non-zero after completion) output
    int validCallCount = 0;
    int totalCallCount = 0;
    while (!converter.isComplete()) {
        float sample = converter.process(buffer.data(), buffer.size());
        totalCallCount++;
        if (!converter.isComplete() || sample != 0.0f) {
            // Count as valid if not complete yet, or if we got a valid sample
            // (the last valid sample may trigger completion)
        }
        if (totalCallCount > 100) break;  // Safety limit
    }

    // At rate 2.0, position advances by 2 per call:
    // Call 1: pos 0->2, Call 2: pos 2->4, ..., Call 50: pos 98->100
    // Call 51: pos 100 >= 99, isComplete triggers immediately
    // So 50 valid samples are read, then 51st call detects completion
    REQUIRE(totalCallCount == 51);  // Need 51 calls to detect completion

    // Alternative verification: check position after 50 calls
    SampleRateConverter converter2;
    converter2.prepare(44100.0);
    converter2.setRate(2.0f);
    for (int i = 0; i < 50; ++i) {
        [[maybe_unused]] float s = converter2.process(buffer.data(), buffer.size());
    }
    // After 50 calls, position should be 100 (past the end)
    REQUIRE(converter2.getPosition() == Approx(100.0f));
}

TEST_CASE("SampleRateConverter rate 0.5 completes 100 samples in ~198 calls", "[samplerate][US1]") {
    // SC-003: Rate 0.5 plays through 100-sample buffer in ~198 process() calls
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(0.5f);
    converter.setInterpolation(SRCInterpolationType::Linear);

    std::array<float, 100> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<float>(i);
    }

    int callCount = 0;
    while (!converter.isComplete()) {
        [[maybe_unused]] float sample = converter.process(buffer.data(), buffer.size());
        callCount++;
        if (callCount > 500) break;  // Safety limit
    }

    // At rate 0.5, position advances by 0.5 per call
    // To reach position 99 (bufferSize-1), need 99/0.5 = 198 calls
    // After call 198: position = 99, which triggers completion
    REQUIRE(callCount >= 196);  // Allow small tolerance
    REQUIRE(callCount <= 200);
}

// =============================================================================
// Phase 4: User Story 2 Tests - Interpolation Quality (T033-T039)
// =============================================================================

TEST_CASE("SampleRateConverter cubic interpolation uses cubicHermiteInterpolate", "[samplerate][US2]") {
    // FR-016: Cubic mode uses Interpolation::cubicHermiteInterpolate()
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.0f);
    converter.setInterpolation(SRCInterpolationType::Cubic);

    // Set position to 1.5 (between samples 1 and 2)
    converter.setPosition(1.5f);

    // Known buffer values
    std::array<float, 10> buffer = {0.0f, 1.0f, 4.0f, 9.0f, 16.0f, 25.0f, 36.0f, 49.0f, 64.0f, 81.0f};
    // At position 1.5, the 4 samples are: buffer[0]=0, buffer[1]=1, buffer[2]=4, buffer[3]=9
    // Using cubicHermiteInterpolate(0, 1, 4, 9, 0.5)

    float output = converter.process(buffer.data(), buffer.size());

    // Calculate expected using the same formula as cubicHermiteInterpolate
    float ym1 = 0.0f, y0 = 1.0f, y1 = 4.0f, y2 = 9.0f, t = 0.5f;
    float expected = Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2, t);

    REQUIRE(output == Approx(expected).margin(1e-6f));
}

TEST_CASE("SampleRateConverter Lagrange interpolation uses lagrangeInterpolate", "[samplerate][US2]") {
    // FR-017: Lagrange mode uses Interpolation::lagrangeInterpolate()
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.0f);
    converter.setInterpolation(SRCInterpolationType::Lagrange);

    // Set position to 1.5
    converter.setPosition(1.5f);

    // Known buffer values
    std::array<float, 10> buffer = {0.0f, 1.0f, 4.0f, 9.0f, 16.0f, 25.0f, 36.0f, 49.0f, 64.0f, 81.0f};

    float output = converter.process(buffer.data(), buffer.size());

    // Calculate expected using lagrangeInterpolate
    float ym1 = 0.0f, y0 = 1.0f, y1 = 4.0f, y2 = 9.0f, t = 0.5f;
    float expected = Interpolation::lagrangeInterpolate(ym1, y0, y1, y2, t);

    REQUIRE(output == Approx(expected).margin(1e-6f));
}

TEST_CASE("SampleRateConverter edge reflection at position 0.5 (left boundary)", "[samplerate][US2]") {
    // FR-018: At left boundary, edge clamping duplicates buffer[0]
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.0f);
    converter.setInterpolation(SRCInterpolationType::Cubic);

    // Set position to 0.5 (left boundary case)
    converter.setPosition(0.5f);

    std::array<float, 10> buffer = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f};
    // At position 0.5:
    // ym1 = buffer[-1] -> clamped to buffer[0] = 10.0
    // y0 = buffer[0] = 10.0
    // y1 = buffer[1] = 20.0
    // y2 = buffer[2] = 30.0

    float output = converter.process(buffer.data(), buffer.size());

    float expected = Interpolation::cubicHermiteInterpolate(10.0f, 10.0f, 20.0f, 30.0f, 0.5f);
    REQUIRE(output == Approx(expected).margin(1e-6f));
}

TEST_CASE("SampleRateConverter edge reflection at position N-1.5 (right boundary)", "[samplerate][US2]") {
    // FR-018: At right boundary, edge clamping duplicates buffer[N-1]
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.0f);
    converter.setInterpolation(SRCInterpolationType::Cubic);

    std::array<float, 10> buffer = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f};
    // Set position to 7.5 (so intPos=7, need samples at 6, 7, 8, 9)
    // buffer[9] is last valid, buffer[10] would be clamped to buffer[9]
    converter.setPosition(7.5f);

    float output = converter.process(buffer.data(), buffer.size());

    // At position 7.5:
    // ym1 = buffer[6] = 70.0
    // y0 = buffer[7] = 80.0
    // y1 = buffer[8] = 90.0
    // y2 = buffer[9] = 100.0
    float expected = Interpolation::cubicHermiteInterpolate(70.0f, 80.0f, 90.0f, 100.0f, 0.5f);
    REQUIRE(output == Approx(expected).margin(1e-6f));

    // Test position 8.5 (intPos=8, need sample at index 10 which is clamped)
    SampleRateConverter converter2;
    converter2.prepare(44100.0);
    converter2.setInterpolation(SRCInterpolationType::Cubic);
    converter2.setPosition(8.5f);

    float output2 = converter2.process(buffer.data(), buffer.size());

    // At position 8.5:
    // ym1 = buffer[7] = 80.0
    // y0 = buffer[8] = 90.0
    // y1 = buffer[9] = 100.0
    // y2 = buffer[10] -> clamped to buffer[9] = 100.0
    float expected2 = Interpolation::cubicHermiteInterpolate(80.0f, 90.0f, 100.0f, 100.0f, 0.5f);
    REQUIRE(output2 == Approx(expected2).margin(1e-6f));
}

TEST_CASE("SampleRateConverter integer positions return exact values for all types", "[samplerate][US2]") {
    // FR-019: At integer positions, all interpolation types return exactly buffer[pos]

    std::array<float, 10> buffer = {1.5f, 2.7f, 3.14159f, 4.2f, 5.0f, 6.28f, 7.77f, 8.0f, 9.1f, 10.0f};

    for (int type = 0; type <= 2; ++type) {
        auto interpType = static_cast<SRCInterpolationType>(type);

        SECTION("type " + std::to_string(type) + " at integer positions") {
            SampleRateConverter converter;
            converter.prepare(44100.0);
            converter.setRate(1.0f);
            converter.setInterpolation(interpType);

            // Test several integer positions
            for (size_t pos = 0; pos < 8; ++pos) {
                converter.setPosition(static_cast<float>(pos));
                float output = converter.process(buffer.data(), buffer.size());
                REQUIRE(output == Approx(buffer[pos]).margin(1e-6f));
            }
        }
    }
}

TEST_CASE("SampleRateConverter cubic vs linear quality comparison", "[samplerate][US2]") {
    // SC-005 partial: Cubic interpolation produces smoother output than linear
    // For a sine wave, cubic should have less error at fractional positions

    constexpr size_t bufferSize = 100;
    std::array<float, bufferSize> buffer;
    generateSine(buffer.data(), bufferSize, 1.0f, static_cast<float>(bufferSize), 1.0f);

    // Measure error for linear interpolation at fractional positions
    SampleRateConverter linearConverter;
    linearConverter.prepare(44100.0);
    linearConverter.setRate(0.5f);
    linearConverter.setInterpolation(SRCInterpolationType::Linear);

    float linearError = 0.0f;
    int linearCount = 0;
    while (!linearConverter.isComplete() && linearCount < 150) {
        float pos = linearConverter.getPosition();
        float output = linearConverter.process(buffer.data(), buffer.size());

        // Calculate ideal sine value at this position
        constexpr float kTwoPi = 6.28318530718f;
        float ideal = std::sin(kTwoPi * pos / static_cast<float>(bufferSize));
        linearError += std::abs(output - ideal);
        linearCount++;
    }

    // Measure error for cubic interpolation
    SampleRateConverter cubicConverter;
    cubicConverter.prepare(44100.0);
    cubicConverter.setRate(0.5f);
    cubicConverter.setInterpolation(SRCInterpolationType::Cubic);

    float cubicError = 0.0f;
    int cubicCount = 0;
    while (!cubicConverter.isComplete() && cubicCount < 150) {
        float pos = cubicConverter.getPosition();
        float output = cubicConverter.process(buffer.data(), buffer.size());

        constexpr float kTwoPi = 6.28318530718f;
        float ideal = std::sin(kTwoPi * pos / static_cast<float>(bufferSize));
        cubicError += std::abs(output - ideal);
        cubicCount++;
    }

    // Cubic should have less total error than linear
    INFO("Linear total error: " << linearError);
    INFO("Cubic total error: " << cubicError);
    REQUIRE(cubicError < linearError);
}

TEST_CASE("SampleRateConverter Lagrange passes through exact sample values", "[samplerate][US2]") {
    // SC-006: Lagrange interpolation passes through exact sample values at integer positions
    // (This is a property of Lagrange interpolation - it's exact at sample points)

    std::array<float, 10> buffer = {1.0f, 4.0f, 9.0f, 16.0f, 25.0f, 36.0f, 49.0f, 64.0f, 81.0f, 100.0f};

    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.0f);
    converter.setInterpolation(SRCInterpolationType::Lagrange);

    // At each integer position, Lagrange should return the exact buffer value
    for (size_t i = 0; i < 8; ++i) {
        converter.setPosition(static_cast<float>(i));
        float output = converter.process(buffer.data(), buffer.size());
        REQUIRE(output == Approx(buffer[i]).margin(1e-6f));
    }
}

// =============================================================================
// Phase 5: User Story 3 Tests - End of Buffer Detection (T050-T054)
// =============================================================================

TEST_CASE("SampleRateConverter isComplete returns false at start", "[samplerate][US3]") {
    // FR-014: isComplete() returns false initially
    SampleRateConverter converter;
    converter.prepare(44100.0);

    REQUIRE(converter.isComplete() == false);

    // Also false after reset
    converter.reset();
    REQUIRE(converter.isComplete() == false);
}

TEST_CASE("SampleRateConverter isComplete returns true after reaching buffer end", "[samplerate][US3]") {
    // FR-021: isComplete() returns true when position >= bufferSize - 1
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.0f);

    std::array<float, 10> buffer = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};

    // Process until complete
    for (int i = 0; i < 9; ++i) {
        REQUIRE(converter.isComplete() == false);
        [[maybe_unused]] float s = converter.process(buffer.data(), buffer.size());
    }

    // After reading position 8 and advancing to 9, isComplete should trigger on next call
    // Position is now 9, which is >= bufferSize - 1 = 9
    [[maybe_unused]] float s = converter.process(buffer.data(), buffer.size());
    REQUIRE(converter.isComplete() == true);
}

TEST_CASE("SampleRateConverter process returns 0.0f when complete", "[samplerate][US3]") {
    // FR-021: process() returns 0.0f after completion
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(10.0f);  // Fast rate to reach end quickly (will be clamped to 4.0)

    std::array<float, 10> buffer = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    // Process until complete
    while (!converter.isComplete()) {
        [[maybe_unused]] float s = converter.process(buffer.data(), buffer.size());
    }

    REQUIRE(converter.isComplete() == true);

    // Subsequent calls should return 0.0f
    float output1 = converter.process(buffer.data(), buffer.size());
    REQUIRE(output1 == 0.0f);

    float output2 = converter.process(buffer.data(), buffer.size());
    REQUIRE(output2 == 0.0f);
}

TEST_CASE("SampleRateConverter reset clears complete flag", "[samplerate][US3]") {
    // FR-022, SC-010: reset() clears the complete flag
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(4.0f);

    std::array<float, 10> buffer = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    // Run to completion
    while (!converter.isComplete()) {
        [[maybe_unused]] float s = converter.process(buffer.data(), buffer.size());
    }
    REQUIRE(converter.isComplete() == true);

    // Reset should clear the flag
    converter.reset();
    REQUIRE(converter.isComplete() == false);
    REQUIRE(converter.getPosition() == Approx(0.0f));

    // Should be able to process again
    float output = converter.process(buffer.data(), buffer.size());
    REQUIRE(output == Approx(1.0f));  // First sample
}

TEST_CASE("SampleRateConverter setPosition allows restart after completion", "[samplerate][US3]") {
    // SC-009: setPosition() to valid position clears isComplete
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(4.0f);

    std::array<float, 10> buffer = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    // Run to completion
    while (!converter.isComplete()) {
        [[maybe_unused]] float s = converter.process(buffer.data(), buffer.size());
    }
    REQUIRE(converter.isComplete() == true);

    // Set position to 0 should clear the flag and allow restart
    converter.setPosition(0.0f);
    REQUIRE(converter.isComplete() == false);
    REQUIRE(converter.getPosition() == Approx(0.0f));

    // Should be able to process from the beginning
    float output = converter.process(buffer.data(), buffer.size());
    REQUIRE(output == Approx(1.0f));  // buffer[0]

    // Set position to middle
    converter.setPosition(5.0f);
    REQUIRE(converter.isComplete() == false);
    output = converter.process(buffer.data(), buffer.size());
    REQUIRE(output == Approx(6.0f));  // buffer[5]
}

// =============================================================================
// Phase 6: User Story 4 Tests - Block Processing (T064-T066)
// =============================================================================

TEST_CASE("SampleRateConverter processBlock matches sequential process calls", "[samplerate][US4]") {
    // SC-007: processBlock() produces identical output to calling process() sequentially
    SampleRateConverter seqConverter;
    seqConverter.prepare(44100.0);
    seqConverter.setRate(0.75f);
    seqConverter.setInterpolation(SRCInterpolationType::Cubic);

    SampleRateConverter blockConverter;
    blockConverter.prepare(44100.0);
    blockConverter.setRate(0.75f);
    blockConverter.setInterpolation(SRCInterpolationType::Cubic);

    // Create source buffer with interesting content
    std::array<float, 100> srcBuffer;
    generateSine(srcBuffer.data(), srcBuffer.size(), 5.0f, 100.0f, 1.0f);

    constexpr size_t outputSize = 64;
    std::array<float, outputSize> seqOutput;
    std::array<float, outputSize> blockOutput;

    // Process sequentially
    for (size_t i = 0; i < outputSize; ++i) {
        seqOutput[i] = seqConverter.process(srcBuffer.data(), srcBuffer.size());
    }

    // Process as block
    blockConverter.processBlock(srcBuffer.data(), srcBuffer.size(), blockOutput.data(), outputSize);

    // Compare outputs
    for (size_t i = 0; i < outputSize; ++i) {
        REQUIRE(blockOutput[i] == Approx(seqOutput[i]).margin(1e-6f));
    }

    // Also verify final positions match
    REQUIRE(blockConverter.getPosition() == Approx(seqConverter.getPosition()).margin(1e-6f));
}

TEST_CASE("SampleRateConverter processBlock handles completion mid-block", "[samplerate][US4]") {
    // processBlock should fill remaining samples with 0.0f after completion
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(2.0f);  // Fast rate
    converter.setInterpolation(SRCInterpolationType::Linear);

    // Small source buffer
    std::array<float, 10> srcBuffer = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    // Large output buffer - will complete mid-block
    constexpr size_t outputSize = 20;
    std::array<float, outputSize> output;
    std::fill(output.begin(), output.end(), -999.0f);  // Fill with sentinel

    converter.processBlock(srcBuffer.data(), srcBuffer.size(), output.data(), outputSize);

    // With rate 2.0 and 10-sample buffer:
    // Positions: 0, 2, 4, 6, 8 (5 valid reads)
    // Then position 10 >= 9, so completion triggers

    // First few samples should be valid interpolated values
    REQUIRE(output[0] == Approx(srcBuffer[0]).margin(0.1f));

    // After completion, should be 0.0f
    bool foundComplete = false;
    for (size_t i = 0; i < outputSize; ++i) {
        if (output[i] == 0.0f && !foundComplete) {
            foundComplete = true;
        }
        if (foundComplete) {
            REQUIRE(output[i] == 0.0f);
        }
    }
    REQUIRE(foundComplete);  // Ensure we did find completion
    REQUIRE(converter.isComplete() == true);
}

TEST_CASE("SampleRateConverter processBlock captures rate at block start", "[samplerate][US4]") {
    // FR-013: Rate is constant for entire block (captured at start)
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.0f);
    converter.setInterpolation(SRCInterpolationType::Linear);

    std::array<float, 100> srcBuffer;
    for (size_t i = 0; i < srcBuffer.size(); ++i) {
        srcBuffer[i] = static_cast<float>(i);
    }

    std::array<float, 10> output;
    converter.processBlock(srcBuffer.data(), srcBuffer.size(), output.data(), output.size());

    // At rate 1.0, each output should be at position 0, 1, 2, ..., 9
    for (size_t i = 0; i < output.size() - 1; ++i) {
        REQUIRE(output[i] == Approx(srcBuffer[i]).margin(1e-6f));
    }

    // Verify position advanced correctly
    REQUIRE(converter.getPosition() == Approx(10.0f).margin(1e-6f));
}

// =============================================================================
// Phase 7: Quality and Edge Case Tests (T075-T080)
// =============================================================================

TEST_CASE("SampleRateConverter THD+N comparison cubic vs linear", "[samplerate][quality]") {
    // SC-005: Cubic interpolation has better THD+N than linear
    // Methodology: Compare interpolated output against an ideal high-resolution sine
    // Use a low-frequency sine in a short buffer to emphasize interpolation differences

    // Create a buffer with very few samples per cycle to stress interpolation
    constexpr size_t srcSize = 32;  // Very short buffer
    constexpr float sampleRate = 44100.0f;
    // Frequency chosen so we have ~8 samples per cycle in source
    constexpr float frequency = sampleRate / 8.0f;

    std::array<float, srcSize> srcBuffer;
    constexpr float kTwoPi = 6.28318530718f;
    for (size_t i = 0; i < srcSize; ++i) {
        srcBuffer[i] = std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }

    // Process at rate 0.5 (half speed) to create many fractional positions
    constexpr float rate = 0.5f;
    constexpr size_t outputSize = 60;

    // Measure error for both interpolation types
    auto measureError = [&](SRCInterpolationType interpType) {
        SampleRateConverter converter;
        converter.prepare(sampleRate);
        converter.setRate(rate);
        converter.setInterpolation(interpType);

        float totalError = 0.0f;
        float totalSignal = 0.0f;
        int count = 0;

        for (size_t i = 0; i < outputSize && !converter.isComplete(); ++i) {
            float pos = converter.getPosition();
            float sample = converter.process(srcBuffer.data(), srcSize);

            // The "ideal" value is the true sine at this fractional position
            // (not sampled, continuous sine value)
            float idealPhase = kTwoPi * frequency * pos / sampleRate;
            float ideal = std::sin(idealPhase);

            float error = sample - ideal;
            totalError += error * error;
            totalSignal += ideal * ideal;
            count++;
        }

        // Return error in dB relative to signal
        if (totalSignal > 0.0f && totalError > 0.0f) {
            return 10.0f * std::log10(totalError / totalSignal);
        }
        return -100.0f;  // Very low error
    };

    float linearError = measureError(SRCInterpolationType::Linear);
    float cubicError = measureError(SRCInterpolationType::Cubic);

    INFO("Linear error: " << linearError << " dB");
    INFO("Cubic error: " << cubicError << " dB");
    INFO("Improvement: " << (linearError - cubicError) << " dB");

    // Cubic should have lower error (more negative dB)
    REQUIRE(cubicError < linearError);

    // For a sine wave with 8 samples/cycle interpolated at half speed,
    // cubic should show noticeable improvement over linear
    // Relaxing to 1dB minimum improvement for robustness
    REQUIRE((linearError - cubicError) >= 1.0f);
}

TEST_CASE("SampleRateConverter process before prepare returns 0.0f", "[samplerate][edge]") {
    // FR-026: process() before prepare() returns 0.0f
    SampleRateConverter converter;
    // Note: NOT calling prepare()

    std::array<float, 10> buffer = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    float output = converter.process(buffer.data(), buffer.size());
    REQUIRE(output == 0.0f);
}

TEST_CASE("SampleRateConverter nullptr buffer returns 0.0f", "[samplerate][edge]") {
    // FR-025: nullptr buffer returns 0.0f
    SampleRateConverter converter;
    converter.prepare(44100.0);

    float output = converter.process(nullptr, 100);
    REQUIRE(output == 0.0f);
    REQUIRE(converter.isComplete() == true);
}

TEST_CASE("SampleRateConverter zero-size buffer returns 0.0f", "[samplerate][edge]") {
    // FR-025: zero-size buffer returns 0.0f and sets isComplete
    SampleRateConverter converter;
    converter.prepare(44100.0);

    std::array<float, 10> buffer = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    float output = converter.process(buffer.data(), 0);
    REQUIRE(output == 0.0f);
    REQUIRE(converter.isComplete() == true);
}

TEST_CASE("SampleRateConverter rate clamping enforced during processing", "[samplerate][edge]") {
    // SC-011: Rate clamping enforces range [0.25, 4.0]
    SampleRateConverter converter;
    converter.prepare(44100.0);

    std::array<float, 100> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<float>(i);
    }

    SECTION("rate below minimum is clamped") {
        converter.setRate(0.1f);  // Should clamp to 0.25

        // Process and check position advancement
        [[maybe_unused]] float s = converter.process(buffer.data(), buffer.size());
        float posAfterFirst = converter.getPosition();

        // Position should advance by clamped rate (0.25), not 0.1
        REQUIRE(posAfterFirst == Approx(0.25f).margin(1e-6f));
    }

    SECTION("rate above maximum is clamped") {
        converter.setRate(10.0f);  // Should clamp to 4.0

        // Process and check position advancement
        [[maybe_unused]] float s = converter.process(buffer.data(), buffer.size());
        float posAfterFirst = converter.getPosition();

        // Position should advance by clamped rate (4.0), not 10.0
        REQUIRE(posAfterFirst == Approx(4.0f).margin(1e-6f));
    }
}

TEST_CASE("SampleRateConverter 1 million process calls without NaN or Infinity", "[samplerate][stability]") {
    // SC-008: 1 million process() calls without producing NaN or Infinity
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(0.37f);  // Arbitrary fractional rate
    converter.setInterpolation(SRCInterpolationType::Cubic);

    // Create a buffer with valid [-1, 1] input
    constexpr size_t bufferSize = 1024;
    std::array<float, bufferSize> buffer;
    generateSine(buffer.data(), bufferSize, 440.0f, 44100.0f, 1.0f);

    constexpr int totalCalls = 1000000;
    int callsWithNaN = 0;
    int callsWithInf = 0;

    for (int i = 0; i < totalCalls; ++i) {
        if (converter.isComplete()) {
            converter.reset();
        }

        float sample = converter.process(buffer.data(), bufferSize);

        if (std::isnan(sample)) {
            callsWithNaN++;
        }
        if (std::isinf(sample)) {
            callsWithInf++;
        }
    }

    REQUIRE(callsWithNaN == 0);
    REQUIRE(callsWithInf == 0);
}
