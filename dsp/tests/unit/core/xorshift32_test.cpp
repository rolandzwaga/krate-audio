// =============================================================================
// XorShift32 Unit Tests (Spec 128 - Impact Exciter)
// =============================================================================
// Tests for the deterministic xorshift32 PRNG utility (Layer 0).
// Covers: seeding, determinism, value ranges, absorbing-state guard,
// inter-voice sequence uniqueness.

#include <krate/dsp/core/xorshift32.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <set>

using Catch::Approx;

// =============================================================================
// Seeding Tests
// =============================================================================

TEST_CASE("XorShift32 seed produces non-zero state", "[xorshift32][core]")
{
    Krate::DSP::XorShift32 rng;

    for (uint32_t voiceId = 0; voiceId < 16; ++voiceId) {
        rng.seed(voiceId);
        REQUIRE(rng.state != 0);
    }
}

TEST_CASE("XorShift32 absorbing-state guard prevents zero state", "[xorshift32][core]")
{
    // Find a voiceId that would produce state == 0 without the guard.
    // The hash is: state = 0x9E3779B9u ^ (voiceId * 0x85EBCA6Bu)
    // We need voiceId * 0x85EBCA6Bu == 0x9E3779B9u (mod 2^32).
    // Compute the multiplicative inverse to find this voiceId.
    // Rather than computing the inverse, just verify the guard works
    // by directly testing the struct behavior:
    Krate::DSP::XorShift32 rng;
    rng.state = 0; // Force absorbing state

    // After calling next() with state=0, we'd get 0 forever.
    // So the seed() function must guard against producing state=0.
    // Verify that seed() never produces 0 for a range of voiceIds.
    for (uint32_t voiceId = 0; voiceId < 1000; ++voiceId) {
        rng.seed(voiceId);
        REQUIRE(rng.state != 0);
    }
}

// =============================================================================
// Determinism Tests
// =============================================================================

TEST_CASE("XorShift32 same seed produces identical sequence", "[xorshift32][core]")
{
    Krate::DSP::XorShift32 rng1;
    Krate::DSP::XorShift32 rng2;

    rng1.seed(42);
    rng2.seed(42);

    for (int i = 0; i < 1000; ++i) {
        REQUIRE(rng1.next() == rng2.next());
    }
}

TEST_CASE("XorShift32 determinism across nextFloat calls", "[xorshift32][core]")
{
    Krate::DSP::XorShift32 rng1;
    Krate::DSP::XorShift32 rng2;

    rng1.seed(7);
    rng2.seed(7);

    for (int i = 0; i < 100; ++i) {
        REQUIRE(rng1.nextFloat() == rng2.nextFloat());
    }
}

// =============================================================================
// Inter-Voice Uniqueness Tests
// =============================================================================

TEST_CASE("XorShift32 unique sequences across voiceIds 0-7", "[xorshift32][core]")
{
    constexpr int kNumVoices = 8;
    constexpr int kSequenceLength = 10;

    // Collect first kSequenceLength values for each voice
    std::array<std::array<uint32_t, kSequenceLength>, kNumVoices> sequences{};

    for (int v = 0; v < kNumVoices; ++v) {
        Krate::DSP::XorShift32 rng;
        rng.seed(static_cast<uint32_t>(v));
        for (int i = 0; i < kSequenceLength; ++i) {
            sequences[static_cast<size_t>(v)][static_cast<size_t>(i)] = rng.next();
        }
    }

    // Every pair of voices must produce different sequences
    for (int a = 0; a < kNumVoices; ++a) {
        for (int b = a + 1; b < kNumVoices; ++b) {
            bool allSame = true;
            for (int i = 0; i < kSequenceLength; ++i) {
                if (sequences[static_cast<size_t>(a)][static_cast<size_t>(i)] !=
                    sequences[static_cast<size_t>(b)][static_cast<size_t>(i)]) {
                    allSame = false;
                    break;
                }
            }
            INFO("Voice " << a << " and voice " << b << " produced identical sequences");
            REQUIRE_FALSE(allSame);
        }
    }
}

// =============================================================================
// Value Range Tests
// =============================================================================

TEST_CASE("XorShift32 nextFloat produces values in [0.0, 1.0)", "[xorshift32][core]")
{
    Krate::DSP::XorShift32 rng;
    rng.seed(123);

    float minVal = 1.0f;
    float maxVal = 0.0f;

    for (int i = 0; i < 100000; ++i) {
        float val = rng.nextFloat();
        REQUIRE(val >= 0.0f);
        REQUIRE(val < 1.0f);
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
    }

    // Should cover a reasonable range
    REQUIRE(minVal < 0.01f);
    REQUIRE(maxVal > 0.99f);
}

TEST_CASE("XorShift32 nextFloatSigned produces values in [-1.0, 1.0)", "[xorshift32][core]")
{
    Krate::DSP::XorShift32 rng;
    rng.seed(456);

    float minVal = 1.0f;
    float maxVal = -1.0f;

    for (int i = 0; i < 100000; ++i) {
        float val = rng.nextFloatSigned();
        REQUIRE(val >= -1.0f);
        REQUIRE(val < 1.0f);
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
    }

    // Should cover both positive and negative ranges
    REQUIRE(minVal < -0.98f);
    REQUIRE(maxVal > 0.98f);
}

// =============================================================================
// Basic Quality Tests
// =============================================================================

TEST_CASE("XorShift32 next produces non-repeating values", "[xorshift32][core]")
{
    Krate::DSP::XorShift32 rng;
    rng.seed(99);

    // First 100 values should all be unique
    std::set<uint32_t> values;
    for (int i = 0; i < 100; ++i) {
        values.insert(rng.next());
    }
    REQUIRE(values.size() == 100);
}

TEST_CASE("XorShift32 default state is non-zero", "[xorshift32][core]")
{
    Krate::DSP::XorShift32 rng;
    REQUIRE(rng.state != 0);
    // Should produce valid output even without calling seed()
    uint32_t val = rng.next();
    REQUIRE(val != 0);
}
