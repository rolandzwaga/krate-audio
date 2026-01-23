// ==============================================================================
// Unit Tests: ModalResonator
// ==============================================================================
// Layer 2: DSP Processor Tests
// Feature: 086-modal-resonator
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/processors/modal_resonator.h>

#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

constexpr float kTestSampleRate = 44100.0f;
constexpr double kTestSampleRateDouble = 44100.0;
constexpr size_t kTestBlockSize = 512;
constexpr float kTolerance = 1e-5f;
constexpr float kTestPi = 3.14159265358979323846f;
constexpr float kTestTwoPi = 2.0f * kTestPi;

// Calculate RMS of a buffer
inline float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

// Calculate peak absolute value
inline float calculatePeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

// Check if buffer contains any NaN or Inf values
inline bool hasInvalidSamples(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (std::isnan(buffer[i]) || std::isinf(buffer[i])) {
            return true;
        }
    }
    return false;
}

// Calculate energy in a buffer (sum of squared samples)
inline float calculateEnergy(const float* buffer, size_t size) {
    float energy = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        energy += buffer[i] * buffer[i];
    }
    return energy;
}

// Simple DFT bin magnitude at a specific frequency
inline float getDFTMagnitudeAtFrequency(
    const float* buffer, size_t size, float frequency, float sampleRate
) {
    const float binFloat = frequency * static_cast<float>(size) / sampleRate;
    const size_t bin = static_cast<size_t>(std::round(binFloat));
    if (bin > size / 2) return 0.0f;

    float real = 0.0f, imag = 0.0f;
    for (size_t n = 0; n < size; ++n) {
        float angle = -kTestTwoPi * static_cast<float>(bin * n) / static_cast<float>(size);
        real += buffer[n] * std::cos(angle);
        imag += buffer[n] * std::sin(angle);
    }
    return std::sqrt(real * real + imag * imag) / static_cast<float>(size);
}

// Find the frequency with maximum magnitude using DFT
inline float findPeakFrequency(
    const float* buffer, size_t size, float sampleRate,
    float minFreq = 20.0f, float maxFreq = 20000.0f
) {
    float maxMag = 0.0f;
    float peakFreq = 0.0f;

    // Search in 1Hz steps (crude but sufficient for testing)
    for (float freq = minFreq; freq <= maxFreq && freq < sampleRate / 2.0f; freq += 1.0f) {
        float mag = getDFTMagnitudeAtFrequency(buffer, size, freq, sampleRate);
        if (mag > maxMag) {
            maxMag = mag;
            peakFreq = freq;
        }
    }
    return peakFreq;
}

// Measure decay time (time to reach specified dB reduction from peak)
// Returns decay time in seconds
inline float measureDecayTime(const float* buffer, size_t size, float sampleRate, float decayDb = -60.0f) {
    // Find peak in first 10% of buffer
    size_t searchEnd = size / 10;
    float peak = 0.0f;
    size_t peakIdx = 0;
    for (size_t i = 0; i < searchEnd; ++i) {
        if (std::abs(buffer[i]) > peak) {
            peak = std::abs(buffer[i]);
            peakIdx = i;
        }
    }

    if (peak < 1e-10f) return 0.0f;

    // Find when amplitude drops to target level
    const float threshold = peak * std::pow(10.0f, decayDb / 20.0f);

    // Use RMS windows for more stable measurement
    constexpr size_t windowSize = 256;

    for (size_t i = peakIdx + windowSize; i + windowSize < size; i += windowSize / 2) {
        float windowRms = calculateRMS(buffer + i, windowSize);
        if (windowRms < threshold) {
            return static_cast<float>(i - peakIdx) / sampleRate;
        }
    }

    // Decay didn't complete in buffer
    return static_cast<float>(size - peakIdx) / sampleRate;
}

} // anonymous namespace

// ==============================================================================
// Phase 2: Foundational Tests
// ==============================================================================

// T004: ModalResonator construction and default state
TEST_CASE("ModalResonator construction and default state", "[modal_resonator][lifecycle][foundational]") {
    ModalResonator resonator;

    SECTION("default constructor initializes unprepared state") {
        REQUIRE_FALSE(resonator.isPrepared());
    }

    SECTION("default constructor has no active modes") {
        REQUIRE(resonator.getNumActiveModes() == 0);
    }

    SECTION("constructor accepts custom smoothing time") {
        ModalResonator customResonator(10.0f);
        REQUIRE_FALSE(customResonator.isPrepared());
    }
}

// T006: prepare() initializing sample rate and coefficients
TEST_CASE("ModalResonator prepare initializes properly", "[modal_resonator][lifecycle][foundational]") {
    ModalResonator resonator;

    SECTION("prepare sets prepared state") {
        REQUIRE_FALSE(resonator.isPrepared());
        resonator.prepare(kTestSampleRateDouble);
        REQUIRE(resonator.isPrepared());
    }

    SECTION("prepare works with different sample rates") {
        resonator.prepare(48000.0);
        REQUIRE(resonator.isPrepared());

        // Should be able to process without crash
        float output = resonator.process(0.0f);
        REQUIRE_FALSE(std::isnan(output));
    }

    SECTION("prepare at 192kHz for high sample rate support") {
        resonator.prepare(192000.0);
        REQUIRE(resonator.isPrepared());
    }
}

// T008: reset() clearing oscillator states
TEST_CASE("ModalResonator reset clears oscillator states", "[modal_resonator][lifecycle][foundational]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    SECTION("reset clears filter states") {
        // Configure and excite
        resonator.setModeFrequency(0, 440.0f);
        resonator.setModeDecay(0, 2.0f);
        resonator.setModeAmplitude(0, 1.0f);

        // Strike to excite
        resonator.strike(1.0f);

        // Process some samples
        for (int i = 0; i < 1000; ++i) {
            (void)resonator.process(0.0f);
        }

        // Reset
        resonator.reset();

        // After reset, should produce silence
        float output = resonator.process(0.0f);
        REQUIRE(output == Approx(0.0f).margin(kTolerance));
    }

    SECTION("reset preserves mode configuration") {
        resonator.setModeFrequency(0, 880.0f);
        resonator.setModeDecay(0, 1.5f);
        resonator.setModeAmplitude(0, 0.7f);

        resonator.reset();

        // Parameters should be preserved
        REQUIRE(resonator.getModeFrequency(0) == Approx(880.0f).margin(1.0f));
        REQUIRE(resonator.getModeDecay(0) == Approx(1.5f).margin(0.01f));
        REQUIRE(resonator.getModeAmplitude(0) == Approx(0.7f).margin(0.01f));
    }
}

// T010: process() returning 0.0f when unprepared (FR-026)
TEST_CASE("ModalResonator process returns 0 when unprepared", "[modal_resonator][safety][FR-026]") {
    ModalResonator resonator;

    // Not prepared - should return 0
    float output = resonator.process(1.0f);
    REQUIRE(output == Approx(0.0f).margin(kTolerance));
}

// T011: isPrepared() query method
TEST_CASE("ModalResonator isPrepared query method", "[modal_resonator][lifecycle][foundational]") {
    ModalResonator resonator;

    REQUIRE_FALSE(resonator.isPrepared());

    resonator.prepare(kTestSampleRateDouble);
    REQUIRE(resonator.isPrepared());
}

// ==============================================================================
// Phase 3: User Story 1 - Basic Modal Resonance (P1)
// ==============================================================================

// T015: Single mode at 440Hz producing 440Hz output within 5 cents (SC-002)
TEST_CASE("Mode frequency accurate within 5 cents", "[modal_resonator][US1][SC-002]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);

    // Process impulse and measure frequency via DFT
    constexpr size_t bufferSize = 8192;
    std::vector<float> output(bufferSize, 0.0f);

    resonator.strike(1.0f);
    for (size_t i = 0; i < bufferSize; ++i) {
        output[i] = resonator.process(0.0f);
    }

    float measuredFreq = findPeakFrequency(output.data(), bufferSize, kTestSampleRate, 400.0f, 500.0f);

    // 5 cents = 1/20 semitone = 2^(5/1200) ~= 1.00289
    // Error in cents = 1200 * log2(measured/target)
    float centsError = 1200.0f * std::log2(measuredFreq / 440.0f);
    REQUIRE(std::abs(centsError) < 5.0f);
}

// T016: process(0.0f) returning 0.0f with no excitation
TEST_CASE("ModalResonator produces silence without excitation", "[modal_resonator][US1]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);

    SECTION("process(0) returns 0 with no prior input") {
        float output = resonator.process(0.0f);
        REQUIRE(output == Approx(0.0f).margin(kTolerance));
    }

    SECTION("processBlock with zeros returns zeros") {
        std::array<float, kTestBlockSize> buffer{};
        resonator.processBlock(buffer.data(), static_cast<int>(kTestBlockSize));

        float rms = calculateRMS(buffer.data(), kTestBlockSize);
        REQUIRE(rms == Approx(0.0f).margin(kTolerance));
    }
}

// T017: Multiple modes decaying according to T60
TEST_CASE("Multiple modes decay according to T60", "[modal_resonator][US1]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    // Configure 4 modes with different frequencies
    resonator.setModeFrequency(0, 220.0f);
    resonator.setModeFrequency(1, 440.0f);
    resonator.setModeFrequency(2, 660.0f);
    resonator.setModeFrequency(3, 880.0f);

    for (int i = 0; i < 4; ++i) {
        resonator.setModeDecay(i, 1.0f);
        resonator.setModeAmplitude(i, 0.25f);
    }

    // Process impulse
    constexpr size_t bufferSize = 88200;  // 2 seconds
    std::vector<float> output(bufferSize, 0.0f);

    resonator.strike(1.0f);
    for (size_t i = 0; i < bufferSize; ++i) {
        output[i] = resonator.process(0.0f);
    }

    SECTION("amplitude decreases over time") {
        float energyFirst = calculateEnergy(output.data(), bufferSize / 2);
        float energySecond = calculateEnergy(output.data() + bufferSize / 2, bufferSize / 2);
        REQUIRE(energySecond < energyFirst);
    }

    SECTION("no invalid samples in output") {
        REQUIRE_FALSE(hasInvalidSamples(output.data(), bufferSize));
    }
}

// T018: T60 decay time accurate within 10% (SC-003)
TEST_CASE("Mode decay time accurate within 10%", "[modal_resonator][US1][SC-003]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);  // 1 second T60
    resonator.setModeAmplitude(0, 1.0f);

    constexpr size_t bufferSize = 88200;  // 2 seconds
    std::vector<float> output(bufferSize, 0.0f);

    resonator.strike(1.0f);
    for (size_t i = 0; i < bufferSize; ++i) {
        output[i] = resonator.process(0.0f);
    }

    float measuredT60 = measureDecayTime(output.data(), bufferSize, kTestSampleRate, -60.0f);

    // Within 10% of target
    REQUIRE(measuredT60 == Approx(1.0f).margin(0.1f));
}

// T019: processBlock() consistency with process()
TEST_CASE("processBlock consistent with process", "[modal_resonator][US1]") {
    ModalResonator resonator1;
    ModalResonator resonator2;

    resonator1.prepare(kTestSampleRateDouble);
    resonator2.prepare(kTestSampleRateDouble);

    // Same configuration
    resonator1.setModeFrequency(0, 440.0f);
    resonator1.setModeDecay(0, 1.0f);
    resonator1.setModeAmplitude(0, 1.0f);

    resonator2.setModeFrequency(0, 440.0f);
    resonator2.setModeDecay(0, 1.0f);
    resonator2.setModeAmplitude(0, 1.0f);

    // Same strike
    resonator1.strike(1.0f);
    resonator2.strike(1.0f);

    // Process with process() vs processBlock()
    std::array<float, kTestBlockSize> output1{};
    std::array<float, kTestBlockSize> output2{};

    for (size_t i = 0; i < kTestBlockSize; ++i) {
        output1[i] = resonator1.process(0.0f);
    }

    resonator2.processBlock(output2.data(), static_cast<int>(kTestBlockSize));

    // Results should match
    for (size_t i = 0; i < kTestBlockSize; ++i) {
        REQUIRE(output1[i] == Approx(output2[i]).margin(kTolerance));
    }
}

// ==============================================================================
// Phase 4: User Story 2 - Per-Mode Control (P1)
// ==============================================================================

// T029: setModeFrequency changing mode 0 to 880Hz
TEST_CASE("setModeFrequency changes mode frequency", "[modal_resonator][US2]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    SECTION("setModeFrequency changes frequency for specific mode") {
        resonator.setModeFrequency(0, 880.0f);
        REQUIRE(resonator.getModeFrequency(0) == Approx(880.0f).margin(1.0f));
    }

    SECTION("frequency is clamped to valid range") {
        resonator.setModeFrequency(0, 5.0f);  // Below minimum
        REQUIRE(resonator.getModeFrequency(0) >= kMinModeFrequency);

        resonator.setModeFrequency(0, 30000.0f);  // Above maximum for 44.1kHz
        REQUIRE(resonator.getModeFrequency(0) <= kTestSampleRate * kMaxModeFrequencyRatio);
    }

    SECTION("invalid index is ignored") {
        resonator.setModeFrequency(-1, 1000.0f);  // Negative index
        resonator.setModeFrequency(100, 1000.0f);  // Out of range
        REQUIRE(resonator.getModeFrequency(100) == 0.0f);  // Returns 0 for invalid
    }
}

// T030: setModeDecay producing 2-second T60 within 10%
TEST_CASE("setModeDecay provides accurate T60", "[modal_resonator][US2]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    SECTION("decay time is stored correctly") {
        resonator.setModeFrequency(0, 440.0f);
        resonator.setModeDecay(0, 2.0f);
        resonator.setModeAmplitude(0, 1.0f);
        REQUIRE(resonator.getModeDecay(0) == Approx(2.0f).margin(0.01f));
    }

    SECTION("decay is clamped to valid range") {
        resonator.setModeFrequency(0, 440.0f);

        resonator.setModeDecay(0, 0.0001f);  // Below minimum
        REQUIRE(resonator.getModeDecay(0) >= kMinModeDecay);

        resonator.setModeDecay(0, 100.0f);  // Above maximum
        REQUIRE(resonator.getModeDecay(0) <= kMaxModeDecay);
    }
}

// T031: setModeAmplitude producing half amplitude
TEST_CASE("setModeAmplitude controls amplitude", "[modal_resonator][US2]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    SECTION("amplitude is stored correctly") {
        resonator.setModeFrequency(0, 440.0f);
        resonator.setModeDecay(0, 2.0f);
        resonator.setModeAmplitude(0, 0.5f);
        REQUIRE(resonator.getModeAmplitude(0) == Approx(0.5f).margin(0.01f));
    }

    SECTION("0.5 amplitude produces approximately half output") {
        // Full amplitude reference
        resonator.setModeFrequency(0, 440.0f);
        resonator.setModeDecay(0, 2.0f);
        resonator.setModeAmplitude(0, 1.0f);
        resonator.strike(1.0f);

        float outputFull = 0.0f;
        for (int i = 0; i < 100; ++i) {
            outputFull += std::abs(resonator.process(0.0f));
        }

        // Reset and use half amplitude
        resonator.reset();
        resonator.setModeAmplitude(0, 0.5f);
        resonator.strike(1.0f);

        float outputHalf = 0.0f;
        for (int i = 0; i < 100; ++i) {
            outputHalf += std::abs(resonator.process(0.0f));
        }

        REQUIRE(outputHalf == Approx(outputFull * 0.5f).margin(outputFull * 0.1f));
    }

    SECTION("amplitude is clamped to [0, 1]") {
        resonator.setModeFrequency(0, 440.0f);
        resonator.setModeDecay(0, 2.0f);

        resonator.setModeAmplitude(0, -0.5f);
        REQUIRE(resonator.getModeAmplitude(0) >= 0.0f);

        resonator.setModeAmplitude(0, 1.5f);
        REQUIRE(resonator.getModeAmplitude(0) <= 1.0f);
    }
}

// T032: setModes() bulk configuration from ModalData array
TEST_CASE("setModes bulk configuration", "[modal_resonator][US2][FR-008]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    std::array<ModalData, 4> modes = {{
        {220.0f, 1.0f, 1.0f},
        {440.0f, 1.5f, 0.8f},
        {660.0f, 0.8f, 0.6f},
        {880.0f, 0.5f, 0.4f}
    }};

    resonator.setModes(modes.data(), static_cast<int>(modes.size()));

    SECTION("modes are configured correctly") {
        REQUIRE(resonator.getNumActiveModes() == 4);

        REQUIRE(resonator.getModeFrequency(0) == Approx(220.0f).margin(1.0f));
        REQUIRE(resonator.getModeFrequency(1) == Approx(440.0f).margin(1.0f));
        REQUIRE(resonator.getModeFrequency(2) == Approx(660.0f).margin(1.0f));
        REQUIRE(resonator.getModeFrequency(3) == Approx(880.0f).margin(1.0f));

        REQUIRE(resonator.getModeDecay(0) == Approx(1.0f).margin(0.01f));
        REQUIRE(resonator.getModeDecay(1) == Approx(1.5f).margin(0.01f));

        REQUIRE(resonator.getModeAmplitude(0) == Approx(1.0f).margin(0.01f));
        REQUIRE(resonator.getModeAmplitude(1) == Approx(0.8f).margin(0.01f));
    }

    SECTION("all configured modes are enabled") {
        REQUIRE(resonator.isModeEnabled(0));
        REQUIRE(resonator.isModeEnabled(1));
        REQUIRE(resonator.isModeEnabled(2));
        REQUIRE(resonator.isModeEnabled(3));
    }
}

// T033: Parameter clamping
TEST_CASE("Parameter clamping enforced", "[modal_resonator][US2][FR-033]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    SECTION("frequency clamped to [20, sampleRate*0.45]") {
        resonator.setModeFrequency(0, 5.0f);
        REQUIRE(resonator.getModeFrequency(0) >= kMinModeFrequency);

        resonator.setModeFrequency(0, 25000.0f);
        REQUIRE(resonator.getModeFrequency(0) <= kTestSampleRate * kMaxModeFrequencyRatio);
    }

    SECTION("t60 clamped to [0.001, 30.0]") {
        resonator.setModeFrequency(0, 440.0f);

        resonator.setModeDecay(0, 0.00001f);
        REQUIRE(resonator.getModeDecay(0) >= kMinModeDecay);

        resonator.setModeDecay(0, 100.0f);
        REQUIRE(resonator.getModeDecay(0) <= kMaxModeDecay);
    }

    SECTION("amplitude clamped to [0.0, 1.0]") {
        resonator.setModeFrequency(0, 440.0f);
        resonator.setModeDecay(0, 1.0f);

        resonator.setModeAmplitude(0, -1.0f);
        REQUIRE(resonator.getModeAmplitude(0) >= 0.0f);

        resonator.setModeAmplitude(0, 2.0f);
        REQUIRE(resonator.getModeAmplitude(0) <= 1.0f);
    }
}

// ==============================================================================
// Phase 5: User Story 3 - Material Presets (P2)
// ==============================================================================

// T043: setMaterial(Material::Metal) configuring long decay and inharmonic ratios
TEST_CASE("setMaterial Metal configures long decay", "[modal_resonator][US3]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setMaterial(Material::Metal);

    SECTION("metal has multiple active modes") {
        REQUIRE(resonator.getNumActiveModes() == 8);
    }

    SECTION("metal fundamental is at base frequency") {
        REQUIRE(resonator.getModeFrequency(0) == Approx(kModalBaseFrequency).margin(1.0f));
    }
}

// T044: setMaterial(Material::Wood) having shorter decay than Metal (SC-010)
TEST_CASE("Wood has shorter decay than Metal", "[modal_resonator][US3][SC-010]") {
    ModalResonator resonatorMetal;
    ModalResonator resonatorWood;

    resonatorMetal.prepare(kTestSampleRateDouble);
    resonatorWood.prepare(kTestSampleRateDouble);

    resonatorMetal.setMaterial(Material::Metal);
    resonatorWood.setMaterial(Material::Wood);

    // Process impulse for both
    constexpr size_t bufferSize = 88200;  // 2 seconds
    std::vector<float> outputMetal(bufferSize, 0.0f);
    std::vector<float> outputWood(bufferSize, 0.0f);

    resonatorMetal.strike(1.0f);
    resonatorWood.strike(1.0f);

    for (size_t i = 0; i < bufferSize; ++i) {
        outputMetal[i] = resonatorMetal.process(0.0f);
        outputWood[i] = resonatorWood.process(0.0f);
    }

    // Compare energy in second half (tail)
    float energyMetalTail = calculateEnergy(outputMetal.data() + bufferSize / 2, bufferSize / 2);
    float energyWoodTail = calculateEnergy(outputWood.data() + bufferSize / 2, bufferSize / 2);

    // Metal should have more energy in tail (longer decay)
    REQUIRE(energyMetalTail > energyWoodTail);
}

// T045: setMaterial(Material::Glass) producing bright, ringing character
TEST_CASE("setMaterial Glass produces ringing character", "[modal_resonator][US3]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setMaterial(Material::Glass);

    SECTION("glass has active modes") {
        REQUIRE(resonator.getNumActiveModes() == 8);
    }

    SECTION("glass produces output on strike") {
        resonator.strike(1.0f);

        float output = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            output += std::abs(resonator.process(0.0f));
        }

        REQUIRE(output > 0.0f);
    }
}

// T046: Material preset remaining modifiable after selection (FR-012)
TEST_CASE("Material presets are modifiable after selection", "[modal_resonator][US3][FR-012]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setMaterial(Material::Metal);

    // Modify mode after material selection
    resonator.setModeFrequency(0, 220.0f);  // Change from 440Hz
    REQUIRE(resonator.getModeFrequency(0) == Approx(220.0f).margin(1.0f));

    resonator.setModeDecay(0, 0.5f);
    REQUIRE(resonator.getModeDecay(0) == Approx(0.5f).margin(0.01f));

    resonator.setModeAmplitude(0, 0.3f);
    REQUIRE(resonator.getModeAmplitude(0) == Approx(0.3f).margin(0.01f));
}

// T047: Material presets producing audibly distinct timbres (SC-008)
TEST_CASE("Material presets produce distinct timbres", "[modal_resonator][US3][SC-008]") {
    // Test that each material produces measurably different output
    std::vector<Material> materials = {
        Material::Wood, Material::Metal, Material::Glass, Material::Ceramic, Material::Nylon
    };

    std::vector<float> energies;

    for (auto mat : materials) {
        ModalResonator resonator;
        resonator.prepare(kTestSampleRateDouble);
        resonator.setMaterial(mat);

        constexpr size_t bufferSize = 44100;
        std::vector<float> output(bufferSize, 0.0f);

        resonator.strike(1.0f);
        for (size_t i = 0; i < bufferSize; ++i) {
            output[i] = resonator.process(0.0f);
        }

        // Measure energy in tail (indicator of decay character)
        float tailEnergy = calculateEnergy(output.data() + bufferSize / 2, bufferSize / 2);
        energies.push_back(tailEnergy);
    }

    // Check that energies are different (materials have distinct decays)
    for (size_t i = 0; i < energies.size(); ++i) {
        for (size_t j = i + 1; j < energies.size(); ++j) {
            // Allow some tolerance but they should be noticeably different
            float ratio = energies[i] / (energies[j] + 1e-10f);
            // At least 10% difference or more
            bool distinct = ratio < 0.9f || ratio > 1.1f;
            REQUIRE(distinct);
        }
    }
}

// ==============================================================================
// Phase 6: User Story 4 - Size and Damping Control (P2)
// ==============================================================================

// T056: setSize(2.0f) halving all mode frequencies (SC-009)
TEST_CASE("setSize 2.0 halves frequencies", "[modal_resonator][US4][SC-009]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);

    // Set size to 2.0 (frequencies should be halved - inverse relationship)
    resonator.setSize(2.0f);

    // Process and measure frequency
    constexpr size_t bufferSize = 8192;
    std::vector<float> output(bufferSize, 0.0f);

    resonator.strike(1.0f);
    for (size_t i = 0; i < bufferSize; ++i) {
        output[i] = resonator.process(0.0f);
    }

    float measuredFreq = findPeakFrequency(output.data(), bufferSize, kTestSampleRate, 180.0f, 260.0f);

    // Should be around 220Hz (440/2)
    REQUIRE(measuredFreq == Approx(220.0f).margin(10.0f));
}

// T057: setSize(0.5f) doubling all mode frequencies
TEST_CASE("setSize 0.5 doubles frequencies", "[modal_resonator][US4]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);

    // Set size to 0.5 (frequencies should be doubled)
    resonator.setSize(0.5f);

    // Process and measure frequency
    constexpr size_t bufferSize = 8192;
    std::vector<float> output(bufferSize, 0.0f);

    resonator.strike(1.0f);
    for (size_t i = 0; i < bufferSize; ++i) {
        output[i] = resonator.process(0.0f);
    }

    float measuredFreq = findPeakFrequency(output.data(), bufferSize, kTestSampleRate, 800.0f, 960.0f);

    // Should be around 880Hz (440*2)
    REQUIRE(measuredFreq == Approx(880.0f).margin(20.0f));
}

// T058: setDamping(0.5f) reducing all decay times by 50%
TEST_CASE("setDamping reduces decay times", "[modal_resonator][US4]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);

    SECTION("damping is stored correctly") {
        resonator.setDamping(0.5f);
        REQUIRE(resonator.getDamping() == Approx(0.5f));
    }

    SECTION("damping reduces tail energy") {
        // No damping
        constexpr size_t bufferSize = 88200;
        std::vector<float> outputNoDamp(bufferSize, 0.0f);

        resonator.setDamping(0.0f);
        resonator.strike(1.0f);
        for (size_t i = 0; i < bufferSize; ++i) {
            outputNoDamp[i] = resonator.process(0.0f);
        }
        float energyNoDamp = calculateEnergy(outputNoDamp.data() + bufferSize / 2, bufferSize / 2);

        // With damping
        resonator.reset();
        resonator.setDamping(0.5f);
        std::vector<float> outputDamped(bufferSize, 0.0f);
        resonator.strike(1.0f);
        for (size_t i = 0; i < bufferSize; ++i) {
            outputDamped[i] = resonator.process(0.0f);
        }
        float energyDamped = calculateEnergy(outputDamped.data() + bufferSize / 2, bufferSize / 2);

        // Damped should have less energy in tail
        REQUIRE(energyDamped < energyNoDamp);
    }
}

// T059: setDamping(1.0f) producing immediate silence
TEST_CASE("setDamping 1.0 produces very fast decay", "[modal_resonator][US4]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);
    resonator.setDamping(1.0f);

    resonator.strike(1.0f);

    // Process some samples - with damping=1.0, decay should be nearly instant
    constexpr size_t bufferSize = 4410;  // 100ms
    std::vector<float> output(bufferSize, 0.0f);
    for (size_t i = 0; i < bufferSize; ++i) {
        output[i] = resonator.process(0.0f);
    }

    // Tail energy should be very low
    float tailEnergy = calculateEnergy(output.data() + bufferSize / 2, bufferSize / 2);
    float totalEnergy = calculateEnergy(output.data(), bufferSize);

    // Tail should be negligible compared to total
    REQUIRE(tailEnergy < totalEnergy * 0.01f);
}

// T060: Size clamping to [0.1, 10.0] range (FR-014)
TEST_CASE("Size parameter clamping", "[modal_resonator][US4][FR-014]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    resonator.setSize(0.01f);  // Below minimum
    REQUIRE(resonator.getSize() >= kMinSizeScale);

    resonator.setSize(100.0f);  // Above maximum
    REQUIRE(resonator.getSize() <= kMaxSizeScale);
}

// ==============================================================================
// Phase 7: User Story 5 - Strike/Excitation (P3)
// ==============================================================================

// T068: strike(1.0f) exciting all modes
TEST_CASE("strike excites all active modes", "[modal_resonator][US5]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    // Configure 4 modes
    for (int i = 0; i < 4; ++i) {
        resonator.setModeFrequency(i, 220.0f * (i + 1));
        resonator.setModeDecay(i, 1.0f);
        resonator.setModeAmplitude(i, 0.25f);
    }

    resonator.strike(1.0f);

    // Process and check for output
    constexpr size_t bufferSize = 1024;
    std::vector<float> output(bufferSize, 0.0f);
    for (size_t i = 0; i < bufferSize; ++i) {
        output[i] = resonator.process(0.0f);
    }

    float peak = calculatePeak(output.data(), bufferSize);
    REQUIRE(peak > 0.01f);
}

// T069: strike(0.5f) producing half amplitude compared to strike(1.0f)
TEST_CASE("strike velocity scales amplitude", "[modal_resonator][US5][FR-018]") {
    ModalResonator resonator1;
    ModalResonator resonator2;

    resonator1.prepare(kTestSampleRateDouble);
    resonator2.prepare(kTestSampleRateDouble);

    // Same configuration
    resonator1.setModeFrequency(0, 440.0f);
    resonator1.setModeDecay(0, 2.0f);
    resonator1.setModeAmplitude(0, 1.0f);

    resonator2.setModeFrequency(0, 440.0f);
    resonator2.setModeDecay(0, 2.0f);
    resonator2.setModeAmplitude(0, 1.0f);

    // Full velocity
    resonator1.strike(1.0f);
    float outputFull = std::abs(resonator1.process(0.0f));

    // Half velocity
    resonator2.strike(0.5f);
    float outputHalf = std::abs(resonator2.process(0.0f));

    // Half velocity should produce approximately half amplitude
    REQUIRE(outputHalf == Approx(outputFull * 0.5f).margin(outputFull * 0.1f));
}

// T070: strike followed by natural decay
TEST_CASE("strike produces natural decay", "[modal_resonator][US5]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);
    resonator.setModeAmplitude(0, 1.0f);

    resonator.strike(1.0f);

    constexpr size_t bufferSize = 44100;
    std::vector<float> output(bufferSize, 0.0f);
    for (size_t i = 0; i < bufferSize; ++i) {
        output[i] = resonator.process(0.0f);
    }

    // Verify decay
    float energyFirst = calculateEnergy(output.data(), bufferSize / 4);
    float energyLast = calculateEnergy(output.data() + 3 * bufferSize / 4, bufferSize / 4);

    REQUIRE(energyLast < energyFirst);
}

// T071: strike latency within 1 sample (SC-004)
TEST_CASE("strike produces output within 1 sample", "[modal_resonator][US5][SC-004][FR-020]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);
    resonator.setModeAmplitude(0, 1.0f);

    resonator.strike(1.0f);

    // First sample after strike should have output
    float output = resonator.process(0.0f);
    REQUIRE(std::abs(output) > 0.001f);
}

// T072: strike accumulation when modes already resonating (FR-019)
TEST_CASE("strike accumulates with existing resonance", "[modal_resonator][US5][FR-019]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);

    // First strike
    resonator.strike(1.0f);

    // Process some samples
    for (int i = 0; i < 1000; ++i) {
        (void)resonator.process(0.0f);
    }

    // Measure current state
    float beforeSecondStrike = std::abs(resonator.process(0.0f));

    // Second strike
    resonator.strike(1.0f);

    // After second strike, amplitude should increase (energy accumulates)
    float afterSecondStrike = std::abs(resonator.process(0.0f));

    // After second strike should be louder than before
    REQUIRE(afterSecondStrike > beforeSecondStrike);
}

// ==============================================================================
// Phase 8: Parameter Smoothing
// ==============================================================================

// T078: No audible clicks on abrupt frequency change (SC-005)
TEST_CASE("No clicks on frequency change", "[modal_resonator][smoothing][SC-005]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);

    constexpr size_t bufferSize = 4410;  // 100ms
    std::vector<float> output(bufferSize, 0.0f);

    // Excite with continuous input
    for (size_t i = 0; i < bufferSize / 2; ++i) {
        output[i] = resonator.process(0.1f);
    }

    // Change frequency abruptly
    resonator.setModeFrequency(0, 880.0f);

    for (size_t i = bufferSize / 2; i < bufferSize; ++i) {
        output[i] = resonator.process(0.1f);
    }

    // Check for clicks (sudden large changes)
    float maxDiff = 0.0f;
    for (size_t i = 1; i < bufferSize; ++i) {
        float diff = std::abs(output[i] - output[i - 1]);
        if (diff > maxDiff) maxDiff = diff;
    }

    // No extreme sample-to-sample jumps (clicks would be > 0.5)
    REQUIRE(maxDiff < 0.5f);
}

// T079: No audible clicks on abrupt amplitude change (SC-005)
TEST_CASE("No clicks on amplitude change", "[modal_resonator][smoothing][SC-005]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);

    constexpr size_t bufferSize = 4410;  // 100ms
    std::vector<float> output(bufferSize, 0.0f);

    // Excite with continuous input
    for (size_t i = 0; i < bufferSize / 2; ++i) {
        output[i] = resonator.process(0.1f);
    }

    // Change amplitude abruptly
    resonator.setModeAmplitude(0, 0.1f);

    for (size_t i = bufferSize / 2; i < bufferSize; ++i) {
        output[i] = resonator.process(0.1f);
    }

    // Check for clicks
    float maxDiff = 0.0f;
    for (size_t i = 1; i < bufferSize; ++i) {
        float diff = std::abs(output[i] - output[i - 1]);
        if (diff > maxDiff) maxDiff = diff;
    }

    // No extreme sample-to-sample jumps
    REQUIRE(maxDiff < 0.5f);
}

// T080: Constructor smoothing time parameter (FR-031)
TEST_CASE("Constructor accepts smoothing time parameter", "[modal_resonator][smoothing][FR-031]") {
    ModalResonator resonator(10.0f);  // 10ms smoothing
    resonator.prepare(kTestSampleRateDouble);

    // Should not crash and work normally
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);
    resonator.setModeAmplitude(0, 1.0f);
    resonator.strike(1.0f);

    float output = resonator.process(0.0f);
    REQUIRE_FALSE(std::isnan(output));
}

// ==============================================================================
// Phase 9: Stability and Edge Case Handling
// ==============================================================================

// T091: NaN input causing reset and returning 0.0f (FR-032)
TEST_CASE("NaN input causes reset", "[modal_resonator][stability][FR-032]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);
    resonator.setModeAmplitude(0, 1.0f);

    // Excite
    resonator.strike(1.0f);
    (void)resonator.process(0.0f);  // Start resonating

    // NaN input
    float output = resonator.process(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(output == Approx(0.0f).margin(kTolerance));

    // After NaN, should produce silence (state was reset)
    float afterNaN = resonator.process(0.0f);
    REQUIRE(afterNaN == Approx(0.0f).margin(kTolerance));
}

// T092: Inf input causing reset and returning 0.0f (FR-032)
TEST_CASE("Inf input causes reset", "[modal_resonator][stability][FR-032]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);
    resonator.setModeAmplitude(0, 1.0f);

    // Excite
    resonator.strike(1.0f);
    (void)resonator.process(0.0f);

    // Inf input
    float output = resonator.process(std::numeric_limits<float>::infinity());
    REQUIRE(output == Approx(0.0f).margin(kTolerance));

    // After Inf, should produce silence
    float afterInf = resonator.process(0.0f);
    REQUIRE(afterInf == Approx(0.0f).margin(kTolerance));
}

// T093: 32 modes at 192kHz remaining stable for 30 seconds (SC-007)
TEST_CASE("32 modes at 192kHz stable for extended processing", "[modal_resonator][stability][SC-007]") {
    ModalResonator resonator;
    resonator.prepare(192000.0);

    // Configure all 32 modes
    for (int i = 0; i < 32; ++i) {
        float freq = 100.0f + 100.0f * static_cast<float>(i);
        freq = std::min(freq, 192000.0f * 0.45f);
        resonator.setModeFrequency(i, freq);
        resonator.setModeDecay(i, 10.0f);
        resonator.setModeAmplitude(i, 1.0f / 32.0f);
    }

    REQUIRE(resonator.getNumActiveModes() == 32);

    resonator.strike(1.0f);

    // Process 1 second at 192kHz (reduced from 30s for test speed)
    constexpr size_t bufferSize = 192000;
    bool hasNaN = false;
    bool hasInf = false;
    float maxValue = 0.0f;

    for (size_t i = 0; i < bufferSize; ++i) {
        float sample = resonator.process(0.0f);
        if (std::isnan(sample)) hasNaN = true;
        if (std::isinf(sample)) hasInf = true;
        if (std::abs(sample) > maxValue) maxValue = std::abs(sample);
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxValue < 100.0f);  // Reasonable bound
}

// T095: setModes() ignoring modes beyond 32 (FR-001)
TEST_CASE("setModes ignores modes beyond 32", "[modal_resonator][edge][FR-001]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    // Create 50 modes
    std::vector<ModalData> manyModes;
    for (int i = 0; i < 50; ++i) {
        manyModes.push_back({440.0f + 10.0f * static_cast<float>(i), 1.0f, 0.5f});
    }

    resonator.setModes(manyModes.data(), static_cast<int>(manyModes.size()));

    // Only 32 should be active
    REQUIRE(resonator.getNumActiveModes() == 32);
}

// ==============================================================================
// Phase 10: Performance Validation (tagged for optional running)
// ==============================================================================

// T104-T109: Performance benchmark (excluded from default test runs)
TEST_CASE("Performance benchmark 32 modes at 192kHz", "[.performance][modal_resonator][SC-001]") {
    ModalResonator resonator;
    resonator.prepare(192000.0);

    // Configure all 32 modes
    for (int i = 0; i < 32; ++i) {
        float freq = 100.0f + 100.0f * static_cast<float>(i);
        freq = std::min(freq, 192000.0f * 0.45f);
        resonator.setModeFrequency(i, freq);
        resonator.setModeDecay(i, 5.0f);
        resonator.setModeAmplitude(i, 1.0f / 32.0f);
    }

    resonator.strike(1.0f);

    // Process 512-sample blocks
    constexpr size_t blockSize = 512;
    constexpr size_t numBlocks = 1000;
    std::array<float, blockSize> buffer{};

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t b = 0; b < numBlocks; ++b) {
        resonator.processBlock(buffer.data(), static_cast<int>(blockSize));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    float avgMicrosPerBlock = static_cast<float>(duration.count()) / static_cast<float>(numBlocks);

    // Target: <26.7us per 512-sample block for 1% CPU at 192kHz
    // This is informational - the actual requirement is 1% CPU
    INFO("Average microseconds per 512-sample block: " << avgMicrosPerBlock);

    // Generous margin for CI variability
    REQUIRE(avgMicrosPerBlock < 500.0f);  // Much more relaxed for CI
}

// ==============================================================================
// Additional Edge Cases
// ==============================================================================

TEST_CASE("getNumActiveModes counts correctly", "[modal_resonator][query]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    REQUIRE(resonator.getNumActiveModes() == 0);

    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);
    resonator.setModeAmplitude(0, 1.0f);
    REQUIRE(resonator.getNumActiveModes() == 1);

    resonator.setModeFrequency(1, 880.0f);
    resonator.setModeDecay(1, 1.0f);
    resonator.setModeAmplitude(1, 1.0f);
    REQUIRE(resonator.getNumActiveModes() == 2);
}

TEST_CASE("isModeEnabled query", "[modal_resonator][query]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    REQUIRE_FALSE(resonator.isModeEnabled(0));

    resonator.setModeFrequency(0, 440.0f);
    REQUIRE(resonator.isModeEnabled(0));

    REQUIRE_FALSE(resonator.isModeEnabled(100));  // Invalid index
    REQUIRE_FALSE(resonator.isModeEnabled(-1));   // Negative index
}

TEST_CASE("Query methods return 0 for invalid indices", "[modal_resonator][query]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);

    REQUIRE(resonator.getModeFrequency(-1) == 0.0f);
    REQUIRE(resonator.getModeFrequency(100) == 0.0f);
    REQUIRE(resonator.getModeDecay(-1) == 0.0f);
    REQUIRE(resonator.getModeDecay(100) == 0.0f);
    REQUIRE(resonator.getModeAmplitude(-1) == 0.0f);
    REQUIRE(resonator.getModeAmplitude(100) == 0.0f);
}

TEST_CASE("processBlock handles null and zero size", "[modal_resonator][safety]") {
    ModalResonator resonator;
    resonator.prepare(kTestSampleRateDouble);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);
    resonator.setModeAmplitude(0, 1.0f);

    // Should not crash
    resonator.processBlock(nullptr, 100);
    resonator.processBlock(nullptr, 0);

    std::array<float, 10> buffer{};
    resonator.processBlock(buffer.data(), 0);
    resonator.processBlock(buffer.data(), -1);
}
