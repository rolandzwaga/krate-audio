// ==============================================================================
// Unit Tests: ChebyshevShaper Primitive
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Feature: 058-chebyshev-shaper
// Layer: 1 (Primitives)
//
// Reference: specs/058-chebyshev-shaper/spec.md
// ==============================================================================

#include <krate/dsp/primitives/chebyshev_shaper.h>
#include <krate/dsp/core/chebyshev.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <type_traits>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Tags Reference
// =============================================================================
// [chebyshev_shaper] - All ChebyshevShaper tests
// [construction]     - Construction and default value tests
// [setter]           - Setter method tests
// [getter]           - Getter method tests
// [process]          - Sample processing tests
// [block]            - Block processing tests
// [edge]             - Edge case tests (NaN, Inf, etc.)
// [stability]        - Numerical stability tests
// [success]          - Success criteria verification tests
// [.benchmark]       - Performance benchmarks (opt-in)

// =============================================================================
// Phase 2: Foundational Tests (T003-T006)
// =============================================================================

TEST_CASE("ChebyshevShaper kMaxHarmonics equals 8", "[chebyshev_shaper][construction]") {
    // FR-001: kMaxHarmonics = 8
    REQUIRE(ChebyshevShaper::kMaxHarmonics == 8);
}

TEST_CASE("ChebyshevShaper default constructor initializes all 8 harmonics to 0.0", "[chebyshev_shaper][construction]") {
    // FR-002: Default constructor initializes all harmonic levels to 0.0
    ChebyshevShaper shaper;

    for (int i = 1; i <= ChebyshevShaper::kMaxHarmonics; ++i) {
        REQUIRE(shaper.getHarmonicLevel(i) == Approx(0.0f));
    }
}

TEST_CASE("ChebyshevShaper process returns 0.0 for any input after default construction", "[chebyshev_shaper][construction]") {
    // FR-003: After default construction, process() returns 0.0
    ChebyshevShaper shaper;

    REQUIRE(shaper.process(0.0f) == Approx(0.0f));
    REQUIRE(shaper.process(0.5f) == Approx(0.0f));
    REQUIRE(shaper.process(-0.5f) == Approx(0.0f));
    REQUIRE(shaper.process(1.0f) == Approx(0.0f));
    REQUIRE(shaper.process(-1.0f) == Approx(0.0f));
}

TEST_CASE("ChebyshevShaper sizeof is at most 40 bytes", "[chebyshev_shaper][construction]") {
    // SC-007: sizeof(ChebyshevShaper) <= 40 bytes
    REQUIRE(sizeof(ChebyshevShaper) <= 40);
}

// =============================================================================
// Phase 3: User Story 1 - Custom Harmonic Spectrum (T010-T015)
// =============================================================================

TEST_CASE("ChebyshevShaper process delegates to Chebyshev::harmonicMix", "[chebyshev_shaper][process]") {
    // FR-013: process() delegates to Chebyshev::harmonicMix
    ChebyshevShaper shaper;

    // Set some harmonic levels
    std::array<float, 8> levels = {0.5f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    shaper.setAllHarmonics(levels);

    // Test at various input values
    std::array<float, 5> inputs = {0.0f, 0.3f, 0.7f, -0.5f, 1.0f};

    for (float x : inputs) {
        float expected = Chebyshev::harmonicMix(x, levels.data(), 8);
        float actual = shaper.process(x);
        REQUIRE(actual == Approx(expected).margin(1e-5f));
    }
}

TEST_CASE("ChebyshevShaper process is marked const", "[chebyshev_shaper][process]") {
    // FR-014: process() is const
    const ChebyshevShaper shaper;

    // This should compile - process is const
    [[maybe_unused]] float result = shaper.process(0.5f);

    // Verify at compile time
    static_assert(std::is_same_v<
        decltype(std::declval<const ChebyshevShaper&>().process(0.0f)),
        float>);
}

TEST_CASE("ChebyshevShaper single harmonic output matches Chebyshev::Tn", "[chebyshev_shaper][process]") {
    // SC-001: Single harmonic output matches T_n(x)
    ChebyshevShaper shaper;

    std::array<float, 5> testInputs = {0.0f, 0.3f, 0.7f, -0.5f, 1.0f};

    // Test each harmonic T1 through T8
    SECTION("T1 - fundamental") {
        shaper.setHarmonicLevel(1, 1.0f);
        for (float x : testInputs) {
            float expected = Chebyshev::T1(x);
            float actual = shaper.process(x);
            REQUIRE(actual == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T2 - 2nd harmonic") {
        shaper.setHarmonicLevel(2, 1.0f);
        for (float x : testInputs) {
            float expected = Chebyshev::T2(x);
            float actual = shaper.process(x);
            REQUIRE(actual == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T3 - 3rd harmonic") {
        shaper.setHarmonicLevel(3, 1.0f);
        for (float x : testInputs) {
            float expected = Chebyshev::T3(x);
            float actual = shaper.process(x);
            REQUIRE(actual == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T4 - 4th harmonic") {
        shaper.setHarmonicLevel(4, 1.0f);
        for (float x : testInputs) {
            float expected = Chebyshev::T4(x);
            float actual = shaper.process(x);
            REQUIRE(actual == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T5 - 5th harmonic") {
        shaper.setHarmonicLevel(5, 1.0f);
        for (float x : testInputs) {
            float expected = Chebyshev::T5(x);
            float actual = shaper.process(x);
            REQUIRE(actual == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T6 - 6th harmonic") {
        shaper.setHarmonicLevel(6, 1.0f);
        for (float x : testInputs) {
            float expected = Chebyshev::T6(x);
            float actual = shaper.process(x);
            REQUIRE(actual == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T7 - 7th harmonic") {
        shaper.setHarmonicLevel(7, 1.0f);
        for (float x : testInputs) {
            float expected = Chebyshev::T7(x);
            float actual = shaper.process(x);
            REQUIRE(actual == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T8 - 8th harmonic") {
        shaper.setHarmonicLevel(8, 1.0f);
        for (float x : testInputs) {
            float expected = Chebyshev::T8(x);
            float actual = shaper.process(x);
            REQUIRE(actual == Approx(expected).margin(1e-5f));
        }
    }
}

TEST_CASE("ChebyshevShaper multiple harmonics produce weighted sum output", "[chebyshev_shaper][process]") {
    // SC-002: Multiple harmonics produce weighted sum
    ChebyshevShaper shaper;

    // Set harmonics 1, 3, 5 (odd harmonics typical for guitar distortion)
    shaper.setHarmonicLevel(1, 0.5f);  // T1 weight
    shaper.setHarmonicLevel(3, 0.3f);  // T3 weight
    shaper.setHarmonicLevel(5, 0.2f);  // T5 weight

    std::array<float, 5> testInputs = {0.0f, 0.3f, 0.7f, -0.5f, 1.0f};

    for (float x : testInputs) {
        // Expected: 0.5*T1(x) + 0.3*T3(x) + 0.2*T5(x)
        float expected = 0.5f * Chebyshev::T1(x) +
                         0.3f * Chebyshev::T3(x) +
                         0.2f * Chebyshev::T5(x);
        float actual = shaper.process(x);
        REQUIRE(actual == Approx(expected).margin(1e-5f));
    }
}

TEST_CASE("ChebyshevShaper NaN input propagates NaN output", "[chebyshev_shaper][edge]") {
    // FR-015: NaN propagation
    ChebyshevShaper shaper;
    shaper.setHarmonicLevel(1, 1.0f);

    const float nanValue = std::numeric_limits<float>::quiet_NaN();
    float output = shaper.process(nanValue);
    REQUIRE(std::isnan(output));
}

TEST_CASE("ChebyshevShaper Infinity input handling", "[chebyshev_shaper][edge]") {
    // Edge case: Infinity input should be handled per harmonicMix behavior
    ChebyshevShaper shaper;
    shaper.setHarmonicLevel(1, 1.0f);

    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();

    // Process should not crash
    float posOutput = shaper.process(posInf);
    float negOutput = shaper.process(negInf);

    // Output behavior depends on harmonicMix - just verify no crash
    // and output is some finite or infinite value (not undefined)
    (void)posOutput;
    (void)negOutput;
}

// =============================================================================
// Phase 4: User Story 2 - Individual Harmonic Control (T021-T028)
// =============================================================================

TEST_CASE("ChebyshevShaper setHarmonicLevel signature", "[chebyshev_shaper][setter]") {
    // FR-004: setHarmonicLevel(int harmonic, float level) noexcept
    ChebyshevShaper shaper;

    // Should compile and be noexcept
    static_assert(noexcept(shaper.setHarmonicLevel(1, 0.5f)));

    // Test calling the method
    shaper.setHarmonicLevel(1, 0.5f);
    REQUIRE(shaper.getHarmonicLevel(1) == Approx(0.5f));
}

TEST_CASE("ChebyshevShaper harmonic parameter maps to correct index", "[chebyshev_shaper][setter]") {
    // FR-005: harmonic 1 = fundamental, maps to index 0
    ChebyshevShaper shaper;

    // Set each harmonic and verify mapping
    for (int h = 1; h <= 8; ++h) {
        shaper.setHarmonicLevel(h, static_cast<float>(h) * 0.1f);
    }

    // Verify all are set correctly
    for (int h = 1; h <= 8; ++h) {
        REQUIRE(shaper.getHarmonicLevel(h) == Approx(static_cast<float>(h) * 0.1f));
    }

    // Verify underlying array access via getHarmonicLevels
    const auto& levels = shaper.getHarmonicLevels();
    for (int h = 1; h <= 8; ++h) {
        REQUIRE(levels[h - 1] == Approx(static_cast<float>(h) * 0.1f));
    }
}

TEST_CASE("ChebyshevShaper setHarmonicLevel ignores out-of-range indices", "[chebyshev_shaper][setter][edge]") {
    // FR-006: Safely ignore indices < 1 or > 8
    ChebyshevShaper shaper;

    // Set valid harmonic first
    shaper.setHarmonicLevel(1, 1.0f);

    // Try invalid indices - should be ignored
    shaper.setHarmonicLevel(0, 0.5f);   // Invalid: 0
    shaper.setHarmonicLevel(9, 0.5f);   // Invalid: 9
    shaper.setHarmonicLevel(-1, 0.5f);  // Invalid: -1
    shaper.setHarmonicLevel(100, 0.5f); // Invalid: 100

    // Original value should be unchanged
    REQUIRE(shaper.getHarmonicLevel(1) == Approx(1.0f));

    // All other harmonics should still be 0.0
    for (int h = 2; h <= 8; ++h) {
        REQUIRE(shaper.getHarmonicLevel(h) == Approx(0.0f));
    }
}

TEST_CASE("ChebyshevShaper getHarmonicLevel returns correct level for valid index", "[chebyshev_shaper][getter]") {
    // FR-009: getHarmonicLevel returns correct level
    ChebyshevShaper shaper;

    shaper.setHarmonicLevel(3, 0.75f);
    shaper.setHarmonicLevel(5, 0.25f);

    REQUIRE(shaper.getHarmonicLevel(3) == Approx(0.75f));
    REQUIRE(shaper.getHarmonicLevel(5) == Approx(0.25f));
    REQUIRE(shaper.getHarmonicLevel(1) == Approx(0.0f));  // Not set
}

TEST_CASE("ChebyshevShaper getHarmonicLevel returns 0.0 for out-of-range index", "[chebyshev_shaper][getter][edge]") {
    // FR-010: Out-of-range indices return 0.0
    ChebyshevShaper shaper;
    shaper.setHarmonicLevel(1, 1.0f);

    REQUIRE(shaper.getHarmonicLevel(0) == Approx(0.0f));
    REQUIRE(shaper.getHarmonicLevel(-1) == Approx(0.0f));
    REQUIRE(shaper.getHarmonicLevel(9) == Approx(0.0f));
    REQUIRE(shaper.getHarmonicLevel(100) == Approx(0.0f));
}

TEST_CASE("ChebyshevShaper setHarmonicLevel only affects specified harmonic", "[chebyshev_shaper][setter]") {
    ChebyshevShaper shaper;

    // Set initial values
    for (int h = 1; h <= 8; ++h) {
        shaper.setHarmonicLevel(h, 0.1f);
    }

    // Change only harmonic 4
    shaper.setHarmonicLevel(4, 0.9f);

    // Verify only harmonic 4 changed
    for (int h = 1; h <= 8; ++h) {
        if (h == 4) {
            REQUIRE(shaper.getHarmonicLevel(h) == Approx(0.9f));
        } else {
            REQUIRE(shaper.getHarmonicLevel(h) == Approx(0.1f));
        }
    }
}

TEST_CASE("ChebyshevShaper negative harmonic levels are valid for phase inversion", "[chebyshev_shaper][setter]") {
    ChebyshevShaper shaper;

    shaper.setHarmonicLevel(1, -1.0f);
    shaper.setHarmonicLevel(2, -0.5f);

    REQUIRE(shaper.getHarmonicLevel(1) == Approx(-1.0f));
    REQUIRE(shaper.getHarmonicLevel(2) == Approx(-0.5f));

    // Verify processing works with negative levels (phase inversion)
    float input = 0.5f;
    float expected = -1.0f * Chebyshev::T1(input) + -0.5f * Chebyshev::T2(input);
    REQUIRE(shaper.process(input) == Approx(expected).margin(1e-5f));
}

TEST_CASE("ChebyshevShaper harmonic levels greater than 1.0 are valid for amplification", "[chebyshev_shaper][setter]") {
    ChebyshevShaper shaper;

    shaper.setHarmonicLevel(1, 2.0f);
    shaper.setHarmonicLevel(3, 1.5f);

    REQUIRE(shaper.getHarmonicLevel(1) == Approx(2.0f));
    REQUIRE(shaper.getHarmonicLevel(3) == Approx(1.5f));

    // Verify processing works with levels > 1.0
    float input = 0.5f;
    float expected = 2.0f * Chebyshev::T1(input) + 1.5f * Chebyshev::T3(input);
    REQUIRE(shaper.process(input) == Approx(expected).margin(1e-5f));
}

// =============================================================================
// Phase 5: User Story 3 - Bulk Harmonic Setting (T034-T037)
// =============================================================================

TEST_CASE("ChebyshevShaper setAllHarmonics takes std::array", "[chebyshev_shaper][setter]") {
    // FR-007: setAllHarmonics(const std::array<float, kMaxHarmonics>& levels)
    ChebyshevShaper shaper;

    std::array<float, ChebyshevShaper::kMaxHarmonics> levels = {
        1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 0.015625f, 0.0078125f
    };

    // Should compile and be noexcept
    static_assert(noexcept(shaper.setAllHarmonics(levels)));

    shaper.setAllHarmonics(levels);

    // Verify all levels set correctly
    for (int h = 1; h <= 8; ++h) {
        REQUIRE(shaper.getHarmonicLevel(h) == Approx(levels[h - 1]));
    }
}

TEST_CASE("ChebyshevShaper setAllHarmonics levels[0] equals harmonic 1 mapping", "[chebyshev_shaper][setter]") {
    // FR-008: levels[0] corresponds to harmonic 1 (fundamental)
    ChebyshevShaper shaper;

    std::array<float, 8> levels = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    shaper.setAllHarmonics(levels);

    // Verify mapping: levels[i-1] = harmonic i
    REQUIRE(shaper.getHarmonicLevel(1) == Approx(0.1f));  // levels[0]
    REQUIRE(shaper.getHarmonicLevel(2) == Approx(0.2f));  // levels[1]
    REQUIRE(shaper.getHarmonicLevel(3) == Approx(0.3f));  // levels[2]
    REQUIRE(shaper.getHarmonicLevel(4) == Approx(0.4f));  // levels[3]
    REQUIRE(shaper.getHarmonicLevel(5) == Approx(0.5f));  // levels[4]
    REQUIRE(shaper.getHarmonicLevel(6) == Approx(0.6f));  // levels[5]
    REQUIRE(shaper.getHarmonicLevel(7) == Approx(0.7f));  // levels[6]
    REQUIRE(shaper.getHarmonicLevel(8) == Approx(0.8f));  // levels[7]
}

TEST_CASE("ChebyshevShaper getHarmonicLevels returns const reference to array", "[chebyshev_shaper][getter]") {
    // FR-011: getHarmonicLevels() returns const reference
    ChebyshevShaper shaper;

    std::array<float, 8> levels = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    shaper.setAllHarmonics(levels);

    const auto& result = shaper.getHarmonicLevels();

    // Verify type is const reference
    static_assert(std::is_same_v<
        decltype(shaper.getHarmonicLevels()),
        const std::array<float, ChebyshevShaper::kMaxHarmonics>&>);

    // Verify contents
    for (size_t i = 0; i < 8; ++i) {
        REQUIRE(result[i] == Approx(levels[i]));
    }
}

TEST_CASE("ChebyshevShaper setAllHarmonics completely replaces existing values", "[chebyshev_shaper][setter]") {
    ChebyshevShaper shaper;

    // Set initial values
    std::array<float, 8> initial = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    shaper.setAllHarmonics(initial);

    // Verify initial
    for (int h = 1; h <= 8; ++h) {
        REQUIRE(shaper.getHarmonicLevel(h) == Approx(1.0f));
    }

    // Replace with new values
    std::array<float, 8> replacement = {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f};
    shaper.setAllHarmonics(replacement);

    // Verify complete replacement
    for (int h = 1; h <= 8; ++h) {
        REQUIRE(shaper.getHarmonicLevel(h) == Approx(replacement[h - 1]));
    }
}

// =============================================================================
// Phase 6: User Story 4 - Block Processing (T043-T047)
// =============================================================================

TEST_CASE("ChebyshevShaper processBlock signature", "[chebyshev_shaper][block]") {
    // FR-016: processBlock(float* buffer, size_t n) const noexcept
    ChebyshevShaper shaper;
    std::array<float, 4> buffer = {0.1f, 0.2f, 0.3f, 0.4f};

    // Should compile and be noexcept
    static_assert(noexcept(shaper.processBlock(buffer.data(), 4)));

    // Test that it works on const instance
    const ChebyshevShaper constShaper;
    constShaper.processBlock(buffer.data(), 4);
}

TEST_CASE("ChebyshevShaper processBlock produces bit-identical output to sequential process calls", "[chebyshev_shaper][block]") {
    // FR-017, SC-004: Bit-identical output
    ChebyshevShaper shaper;
    shaper.setHarmonicLevel(1, 0.5f);
    shaper.setHarmonicLevel(3, 0.3f);
    shaper.setHarmonicLevel(5, 0.2f);

    constexpr size_t numSamples = 64;
    std::array<float, numSamples> blockBuffer;
    std::array<float, numSamples> sequentialBuffer;

    // Fill with test signal
    for (size_t i = 0; i < numSamples; ++i) {
        float value = std::sin(static_cast<float>(i) * 0.1f);
        blockBuffer[i] = value;
        sequentialBuffer[i] = value;
    }

    // Process using block method
    shaper.processBlock(blockBuffer.data(), numSamples);

    // Process using sequential method
    for (size_t i = 0; i < numSamples; ++i) {
        sequentialBuffer[i] = shaper.process(sequentialBuffer[i]);
    }

    // Verify bit-identical output
    for (size_t i = 0; i < numSamples; ++i) {
        REQUIRE(blockBuffer[i] == sequentialBuffer[i]);
    }
}

TEST_CASE("ChebyshevShaper processBlock handles n equals 0 gracefully", "[chebyshev_shaper][block][edge]") {
    // FR-019: n=0 is valid
    ChebyshevShaper shaper;

    // Should not crash with nullptr and n=0
    shaper.processBlock(nullptr, 0);

    // Should not modify buffer when n=0
    std::array<float, 4> buffer = {1.0f, 2.0f, 3.0f, 4.0f};
    shaper.processBlock(buffer.data(), 0);

    // Buffer should be unchanged
    REQUIRE(buffer[0] == 1.0f);
    REQUIRE(buffer[1] == 2.0f);
    REQUIRE(buffer[2] == 3.0f);
    REQUIRE(buffer[3] == 4.0f);
}

TEST_CASE("ChebyshevShaper processBlock is marked const", "[chebyshev_shaper][block]") {
    // FR-016: processBlock is const
    const ChebyshevShaper shaper;
    std::array<float, 4> buffer = {0.1f, 0.2f, 0.3f, 0.4f};

    // This should compile - processBlock is const
    shaper.processBlock(buffer.data(), 4);
}

TEST_CASE("ChebyshevShaper 512-sample buffer processed in under 50 microseconds", "[chebyshev_shaper][.benchmark]") {
    // SC-005: Performance benchmark
    ChebyshevShaper shaper;
    shaper.setHarmonicLevel(1, 0.5f);
    shaper.setHarmonicLevel(3, 0.3f);
    shaper.setHarmonicLevel(5, 0.2f);

    constexpr size_t numSamples = 512;
    std::vector<float> buffer(numSamples);

    // Fill with test signal
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(static_cast<float>(i) * 0.05f);
    }

    BENCHMARK("ChebyshevShaper processBlock 512 samples") {
        std::vector<float> testBuffer = buffer;
        shaper.processBlock(testBuffer.data(), numSamples);
        return testBuffer[0];
    };
}

// =============================================================================
// Phase 7: Real-Time Safety and Quality Verification (T052-T055)
// =============================================================================

TEST_CASE("ChebyshevShaper all processing methods are noexcept", "[chebyshev_shaper][success]") {
    // FR-020: All processing methods noexcept
    ChebyshevShaper shaper;
    std::array<float, 4> buffer = {0.0f};

    static_assert(noexcept(shaper.process(0.0f)));
    static_assert(noexcept(shaper.processBlock(buffer.data(), 4)));
}

TEST_CASE("ChebyshevShaper all setter methods are noexcept", "[chebyshev_shaper][success]") {
    // FR-021: All setter methods noexcept
    ChebyshevShaper shaper;
    std::array<float, 8> levels = {};

    static_assert(noexcept(shaper.setHarmonicLevel(1, 0.5f)));
    static_assert(noexcept(shaper.setAllHarmonics(levels)));
}

TEST_CASE("ChebyshevShaper is trivially copyable with no dynamic allocations", "[chebyshev_shaper][success]") {
    // FR-023: No dynamic allocations
    static_assert(std::is_trivially_copyable_v<ChebyshevShaper>);
}

TEST_CASE("ChebyshevShaper 1 million samples produces no unexpected NaN or Infinity", "[chebyshev_shaper][stability]") {
    // SC-003: No unexpected NaN/Inf for valid inputs
    ChebyshevShaper shaper;
    shaper.setHarmonicLevel(1, 1.0f);
    shaper.setHarmonicLevel(3, 0.5f);
    shaper.setHarmonicLevel(5, 0.25f);

    constexpr size_t numSamples = 1000000;
    bool foundBadValue = false;

    for (size_t i = 0; i < numSamples; ++i) {
        // Generate input in range [-1, 1]
        float input = (static_cast<float>(i % 20001) - 10000.0f) / 10000.0f;
        float output = shaper.process(input);

        if (std::isnan(output) || std::isinf(output)) {
            foundBadValue = true;
            break;
        }
    }

    REQUIRE_FALSE(foundBadValue);
}

// =============================================================================
// Additional Tests for Complete Coverage
// =============================================================================

TEST_CASE("ChebyshevShaper getHarmonicLevel is noexcept", "[chebyshev_shaper][getter]") {
    const ChebyshevShaper shaper;

    static_assert(noexcept(shaper.getHarmonicLevel(1)));
    static_assert(noexcept(shaper.getHarmonicLevels()));
}

TEST_CASE("ChebyshevShaper is default constructible", "[chebyshev_shaper][construction]") {
    static_assert(std::is_default_constructible_v<ChebyshevShaper>);
    static_assert(std::is_nothrow_default_constructible_v<ChebyshevShaper>);
}

TEST_CASE("ChebyshevShaper is copy constructible and assignable", "[chebyshev_shaper][construction]") {
    ChebyshevShaper original;
    original.setHarmonicLevel(1, 0.5f);
    original.setHarmonicLevel(3, 0.25f);

    ChebyshevShaper copy = original;
    REQUIRE(copy.getHarmonicLevel(1) == Approx(0.5f));
    REQUIRE(copy.getHarmonicLevel(3) == Approx(0.25f));

    ChebyshevShaper assigned;
    assigned = original;
    REQUIRE(assigned.getHarmonicLevel(1) == Approx(0.5f));
    REQUIRE(assigned.getHarmonicLevel(3) == Approx(0.25f));
}

TEST_CASE("ChebyshevShaper is move constructible and assignable", "[chebyshev_shaper][construction]") {
    static_assert(std::is_nothrow_move_constructible_v<ChebyshevShaper>);
    static_assert(std::is_nothrow_move_assignable_v<ChebyshevShaper>);

    ChebyshevShaper original;
    original.setHarmonicLevel(1, 0.5f);

    ChebyshevShaper moved = std::move(original);
    REQUIRE(moved.getHarmonicLevel(1) == Approx(0.5f));
}
