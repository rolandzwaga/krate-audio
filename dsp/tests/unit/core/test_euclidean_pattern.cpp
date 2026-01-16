// ==============================================================================
// Layer 0: Core Utility Tests - Euclidean Pattern
// ==============================================================================
// Unit tests for EuclideanPattern (spec 069 - Pattern Freeze Mode).
//
// Tests verify:
// - Classic patterns: E(3,8)=tresillo, E(5,8)=cinquillo
// - Rotation behavior
// - Edge cases and bounds
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-first development methodology
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/euclidean_pattern.h>

using namespace Krate::DSP;

// =============================================================================
// Pattern Generation Tests
// =============================================================================

TEST_CASE("EuclideanPattern generates classic tresillo E(3,8)", "[core][euclidean][layer0]") {
    // Tresillo: 3 hits in 8 steps = "X..X..X." = bits 0, 3, 6 set
    // Binary: 01001001 = 0b01001001 = 73 (reading LSB first)
    // Pattern should be: step 0=hit, step 1=rest, step 2=rest, step 3=hit, etc.

    const uint32_t pattern = EuclideanPattern::generate(3, 8, 0);

    // Check individual hits
    REQUIRE(EuclideanPattern::isHit(pattern, 0, 8) == true);   // X
    REQUIRE(EuclideanPattern::isHit(pattern, 1, 8) == false);  // .
    REQUIRE(EuclideanPattern::isHit(pattern, 2, 8) == false);  // .
    REQUIRE(EuclideanPattern::isHit(pattern, 3, 8) == true);   // X
    REQUIRE(EuclideanPattern::isHit(pattern, 4, 8) == false);  // .
    REQUIRE(EuclideanPattern::isHit(pattern, 5, 8) == false);  // .
    REQUIRE(EuclideanPattern::isHit(pattern, 6, 8) == true);   // X
    REQUIRE(EuclideanPattern::isHit(pattern, 7, 8) == false);  // .

    // Verify total hit count is 3
    int hitCount = 0;
    for (int i = 0; i < 8; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 8)) ++hitCount;
    }
    REQUIRE(hitCount == 3);
}

TEST_CASE("EuclideanPattern generates classic cinquillo E(5,8)", "[core][euclidean][layer0]") {
    // Cinquillo: 5 hits in 8 steps = "X.XX.XX." or similar even distribution

    const uint32_t pattern = EuclideanPattern::generate(5, 8, 0);

    // Verify total hit count is 5
    int hitCount = 0;
    for (int i = 0; i < 8; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 8)) ++hitCount;
    }
    REQUIRE(hitCount == 5);
}

TEST_CASE("EuclideanPattern generates full pattern E(n,n)", "[core][euclidean][layer0]") {
    // When hits == steps, every step should be a hit
    const uint32_t pattern = EuclideanPattern::generate(8, 8, 0);

    for (int i = 0; i < 8; ++i) {
        REQUIRE(EuclideanPattern::isHit(pattern, i, 8) == true);
    }
}

TEST_CASE("EuclideanPattern generates empty pattern E(0,n)", "[core][euclidean][layer0]") {
    // When hits == 0, no steps should be hits
    const uint32_t pattern = EuclideanPattern::generate(0, 8, 0);

    for (int i = 0; i < 8; ++i) {
        REQUIRE(EuclideanPattern::isHit(pattern, i, 8) == false);
    }
}

TEST_CASE("EuclideanPattern generates single hit E(1,n)", "[core][euclidean][layer0]") {
    // Single hit should be at position 0 (no rotation)
    const uint32_t pattern = EuclideanPattern::generate(1, 8, 0);

    int hitCount = 0;
    for (int i = 0; i < 8; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 8)) ++hitCount;
    }
    REQUIRE(hitCount == 1);
    REQUIRE(EuclideanPattern::isHit(pattern, 0, 8) == true);  // First step is hit
}

// =============================================================================
// Rotation Tests
// =============================================================================

TEST_CASE("EuclideanPattern rotation shifts pattern correctly", "[core][euclidean][layer0]") {
    // E(3,8) with rotation 1 should shift hits by 1 position
    const uint32_t pattern0 = EuclideanPattern::generate(3, 8, 0);
    const uint32_t pattern1 = EuclideanPattern::generate(3, 8, 1);

    // Both patterns should have same number of hits
    int hitCount0 = 0, hitCount1 = 0;
    for (int i = 0; i < 8; ++i) {
        if (EuclideanPattern::isHit(pattern0, i, 8)) ++hitCount0;
        if (EuclideanPattern::isHit(pattern1, i, 8)) ++hitCount1;
    }
    REQUIRE(hitCount0 == hitCount1);
    REQUIRE(hitCount0 == 3);

    // Patterns should be different (rotated)
    REQUIRE(pattern0 != pattern1);
}

TEST_CASE("EuclideanPattern rotation wraps correctly", "[core][euclidean][layer0]") {
    // Rotation by steps should give same pattern as rotation 0
    const uint32_t pattern0 = EuclideanPattern::generate(3, 8, 0);
    const uint32_t pattern8 = EuclideanPattern::generate(3, 8, 8);

    // Same pattern when rotation equals steps
    REQUIRE(pattern0 == pattern8);
}

TEST_CASE("EuclideanPattern rotation preserves hit count", "[core][euclidean][layer0]") {
    const int steps = 16;
    const int hits = 5;

    for (int rotation = 0; rotation < steps; ++rotation) {
        const uint32_t pattern = EuclideanPattern::generate(hits, steps, rotation);

        int hitCount = 0;
        for (int i = 0; i < steps; ++i) {
            if (EuclideanPattern::isHit(pattern, i, steps)) ++hitCount;
        }

        REQUIRE(hitCount == hits);
    }
}

// =============================================================================
// Edge Cases and Bounds Tests
// =============================================================================

TEST_CASE("EuclideanPattern handles minimum steps", "[core][euclidean][layer0][edge]") {
    // Minimum is 2 steps
    const uint32_t pattern = EuclideanPattern::generate(1, 2, 0);

    int hitCount = 0;
    for (int i = 0; i < 2; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 2)) ++hitCount;
    }
    REQUIRE(hitCount == 1);
}

TEST_CASE("EuclideanPattern handles maximum steps", "[core][euclidean][layer0][edge]") {
    // Maximum is 32 steps
    const uint32_t pattern = EuclideanPattern::generate(7, 32, 0);

    int hitCount = 0;
    for (int i = 0; i < 32; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 32)) ++hitCount;
    }
    REQUIRE(hitCount == 7);
}

TEST_CASE("EuclideanPattern isHit returns false for out-of-bounds position", "[core][euclidean][layer0][edge]") {
    const uint32_t pattern = EuclideanPattern::generate(3, 8, 0);

    // Negative position
    REQUIRE(EuclideanPattern::isHit(pattern, -1, 8) == false);

    // Position >= steps
    REQUIRE(EuclideanPattern::isHit(pattern, 8, 8) == false);
    REQUIRE(EuclideanPattern::isHit(pattern, 100, 8) == false);
}

TEST_CASE("EuclideanPattern handles hits > steps gracefully", "[core][euclidean][layer0][edge]") {
    // When hits > steps, should clamp to steps
    const uint32_t pattern = EuclideanPattern::generate(16, 8, 0);

    // Should still produce valid pattern with at most 8 hits
    int hitCount = 0;
    for (int i = 0; i < 8; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 8)) ++hitCount;
    }
    REQUIRE(hitCount <= 8);
}

// =============================================================================
// Distribution Quality Tests
// =============================================================================

TEST_CASE("EuclideanPattern distributes hits evenly", "[core][euclidean][layer0]") {
    // E(4,16) should place hits at positions 0, 4, 8, 12 (every 4th step)
    const uint32_t pattern = EuclideanPattern::generate(4, 16, 0);

    int hitCount = 0;
    for (int i = 0; i < 16; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 16)) ++hitCount;
    }
    REQUIRE(hitCount == 4);

    // Check that hits are evenly spaced (4 apart)
    int lastHit = -4;  // So first gap is measured correctly
    int gapCount = 0;
    int minGap = 100;
    int maxGap = 0;

    for (int i = 0; i < 16; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 16)) {
            int gap = i - lastHit;
            if (gap < minGap) minGap = gap;
            if (gap > maxGap) maxGap = gap;
            lastHit = i;
            ++gapCount;
        }
    }

    // For perfect distribution, all gaps should be equal (4)
    REQUIRE(minGap == maxGap);
}

TEST_CASE("EuclideanPattern produces African bell pattern E(5,12)", "[core][euclidean][layer0]") {
    // E(5,12) is a traditional West African bell pattern
    const uint32_t pattern = EuclideanPattern::generate(5, 12, 0);

    int hitCount = 0;
    for (int i = 0; i < 12; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 12)) ++hitCount;
    }
    REQUIRE(hitCount == 5);
}

TEST_CASE("EuclideanPattern produces Bossa Nova pattern E(3,16)", "[core][euclidean][layer0]") {
    // E(3,16) is related to Bossa Nova clave
    const uint32_t pattern = EuclideanPattern::generate(3, 16, 0);

    int hitCount = 0;
    for (int i = 0; i < 16; ++i) {
        if (EuclideanPattern::isHit(pattern, i, 16)) ++hitCount;
    }
    REQUIRE(hitCount == 3);
}

// =============================================================================
// Bitmask Representation Tests
// =============================================================================

TEST_CASE("EuclideanPattern bitmask has correct bit positions", "[core][euclidean][layer0]") {
    // For E(2,4), expect hits at positions that create even distribution
    const uint32_t pattern = EuclideanPattern::generate(2, 4, 0);

    // Should have exactly 2 bits set
    int bitCount = 0;
    for (int i = 0; i < 32; ++i) {
        if ((pattern >> i) & 1) ++bitCount;
    }
    REQUIRE(bitCount == 2);
}

TEST_CASE("EuclideanPattern is stateless", "[core][euclidean][layer0]") {
    // Same inputs should always produce same outputs
    const uint32_t p1 = EuclideanPattern::generate(5, 13, 3);
    const uint32_t p2 = EuclideanPattern::generate(5, 13, 3);

    REQUIRE(p1 == p2);
}

// =============================================================================
// Real-Time Safety Tests
// =============================================================================

TEST_CASE("EuclideanPattern functions are noexcept", "[core][euclidean][layer0][realtime]") {
    // Compile-time check: these should be noexcept
    static_assert(noexcept(EuclideanPattern::generate(3, 8, 0)),
                  "generate() must be noexcept");
    static_assert(noexcept(EuclideanPattern::isHit(0u, 0, 8)),
                  "isHit() must be noexcept");
}
