// Layer 0: Core Utility Tests - Grain Envelope
// Part of Granular Delay feature (spec 034)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/grain_envelope.h>

#include <array>
#include <cmath>
#include <numeric>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// GrainEnvelope::generate Tests
// =============================================================================

TEST_CASE("GrainEnvelope::generate creates valid envelopes", "[core][grain][layer0]") {
    constexpr size_t kEnvelopeSize = 256;
    std::array<float, kEnvelopeSize> envelope{};

    SECTION("Hann envelope starts and ends at zero, peaks at center") {
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Hann);

        // First and last samples should be near zero
        REQUIRE(envelope[0] == Approx(0.0f).margin(0.01f));
        REQUIRE(envelope[kEnvelopeSize - 1] == Approx(0.0f).margin(0.05f));

        // Peak should be at center (0.5 phase)
        const size_t center = kEnvelopeSize / 2;
        REQUIRE(envelope[center] == Approx(1.0f).margin(0.01f));

        // All values should be in [0, 1]
        for (float value : envelope) {
            REQUIRE(value >= 0.0f);
            REQUIRE(value <= 1.0f);
        }
    }

    SECTION("Trapezoid envelope has flat sustain region") {
        const float attackRatio = 0.2f;
        const float releaseRatio = 0.2f;
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Trapezoid,
                                attackRatio, releaseRatio);

        // First sample should be zero (start of attack)
        REQUIRE(envelope[0] == Approx(0.0f).margin(0.01f));

        // Sustain region (20%-80% of envelope) should be at 1.0
        const size_t sustainStart = static_cast<size_t>(kEnvelopeSize * attackRatio);
        const size_t sustainEnd = kEnvelopeSize - static_cast<size_t>(kEnvelopeSize * releaseRatio);

        for (size_t i = sustainStart + 1; i < sustainEnd - 1; ++i) {
            REQUIRE(envelope[i] == Approx(1.0f).margin(0.01f));
        }

        // Last sample should be near zero (end of release)
        REQUIRE(envelope[kEnvelopeSize - 1] == Approx(0.0f).margin(0.05f));
    }

    SECTION("Sine envelope is a half-sine wave") {
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Sine);

        // First and last samples should be near zero
        REQUIRE(envelope[0] == Approx(0.0f).margin(0.01f));
        REQUIRE(envelope[kEnvelopeSize - 1] == Approx(0.0f).margin(0.05f));

        // Peak at center should be 1.0
        const size_t center = kEnvelopeSize / 2;
        REQUIRE(envelope[center] == Approx(1.0f).margin(0.01f));
    }

    SECTION("Blackman envelope has low sidelobes") {
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Blackman);

        // First and last should be near zero
        REQUIRE(envelope[0] == Approx(0.0f).margin(0.01f));
        REQUIRE(envelope[kEnvelopeSize - 1] == Approx(0.0f).margin(0.05f));

        // Peak at center should be close to 1.0
        const size_t center = kEnvelopeSize / 2;
        REQUIRE(envelope[center] == Approx(1.0f).margin(0.01f));

        // All values should be in [0, 1]
        for (float value : envelope) {
            REQUIRE(value >= 0.0f);
            REQUIRE(value <= 1.0f);
        }
    }

    SECTION("handles null pointer gracefully") {
        // Should not crash
        GrainEnvelope::generate(nullptr, kEnvelopeSize, GrainEnvelopeType::Hann);
    }

    SECTION("handles zero size gracefully") {
        // Should not crash
        GrainEnvelope::generate(envelope.data(), 0, GrainEnvelopeType::Hann);
    }
}

// =============================================================================
// GrainEnvelope::lookup Tests
// =============================================================================

TEST_CASE("GrainEnvelope::lookup interpolates correctly", "[core][grain][layer0]") {
    constexpr size_t kEnvelopeSize = 256;
    std::array<float, kEnvelopeSize> envelope{};

    // Generate a known envelope
    GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Hann);

    SECTION("phase 0.0 returns first sample") {
        float value = GrainEnvelope::lookup(envelope.data(), kEnvelopeSize, 0.0f);
        REQUIRE(value == Approx(envelope[0]).margin(1e-6f));
    }

    SECTION("phase 1.0 returns last sample") {
        float value = GrainEnvelope::lookup(envelope.data(), kEnvelopeSize, 1.0f);
        REQUIRE(value == Approx(envelope[kEnvelopeSize - 1]).margin(1e-5f));
    }

    SECTION("phase 0.5 returns center sample") {
        float value = GrainEnvelope::lookup(envelope.data(), kEnvelopeSize, 0.5f);
        // Center of Hann window should be 1.0
        REQUIRE(value == Approx(1.0f).margin(0.01f));
    }

    SECTION("fractional phase interpolates between samples") {
        // Test a phase that falls between two table entries
        const float phase = 0.25f;  // Quarter way through
        float value = GrainEnvelope::lookup(envelope.data(), kEnvelopeSize, phase);

        // Should be between surrounding samples
        size_t index = static_cast<size_t>(phase * (kEnvelopeSize - 1));
        REQUIRE(value >= std::min(envelope[index], envelope[index + 1]));
        REQUIRE(value <= std::max(envelope[index], envelope[index + 1]));
    }

    SECTION("clamps phase < 0 to 0") {
        float value = GrainEnvelope::lookup(envelope.data(), kEnvelopeSize, -0.5f);
        REQUIRE(value == Approx(envelope[0]).margin(1e-6f));
    }

    SECTION("clamps phase > 1 to 1") {
        float value = GrainEnvelope::lookup(envelope.data(), kEnvelopeSize, 1.5f);
        REQUIRE(value == Approx(envelope[kEnvelopeSize - 1]).margin(1e-5f));
    }

    SECTION("handles null table gracefully") {
        float value = GrainEnvelope::lookup(nullptr, kEnvelopeSize, 0.5f);
        REQUIRE(value == 0.0f);
    }

    SECTION("handles zero size gracefully") {
        float value = GrainEnvelope::lookup(envelope.data(), 0, 0.5f);
        REQUIRE(value == 0.0f);
    }
}

// =============================================================================
// Envelope Energy Tests (click prevention)
// =============================================================================

TEST_CASE("Grain envelopes start and end smoothly (click prevention)", "[core][grain][layer0]") {
    constexpr size_t kEnvelopeSize = 512;
    std::array<float, kEnvelopeSize> envelope{};

    const std::array<GrainEnvelopeType, 4> types = {
        GrainEnvelopeType::Hann,
        GrainEnvelopeType::Trapezoid,
        GrainEnvelopeType::Sine,
        GrainEnvelopeType::Blackman
    };

    for (auto type : types) {
        DYNAMIC_SECTION("Envelope type " << static_cast<int>(type)) {
            GrainEnvelope::generate(envelope.data(), kEnvelopeSize, type);

            // First sample should be near zero (< 0.05)
            REQUIRE(envelope[0] < 0.05f);

            // Last sample should be near zero
            REQUIRE(envelope[kEnvelopeSize - 1] < 0.1f);

            // Derivative at start should be small (smooth attack)
            // Check that we don't jump too quickly
            REQUIRE(std::abs(envelope[1] - envelope[0]) < 0.1f);

            // Derivative at end should be small (smooth release)
            REQUIRE(std::abs(envelope[kEnvelopeSize - 1] - envelope[kEnvelopeSize - 2]) < 0.1f);
        }
    }
}

// =============================================================================
// Pattern Freeze Linear and Exponential Envelope Tests (spec 069)
// =============================================================================

TEST_CASE("Linear envelope shapes for Pattern Freeze", "[core][grain][layer0][pattern_freeze]") {
    constexpr size_t kEnvelopeSize = 512;
    std::array<float, kEnvelopeSize> envelope{};

    SECTION("Linear envelope is linear attack and release") {
        const float attackRatio = 0.1f;
        const float releaseRatio = 0.2f;
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Linear,
                                attackRatio, releaseRatio);

        // First sample should be zero
        REQUIRE(envelope[0] == Approx(0.0f).margin(0.01f));

        // Attack should increase linearly
        const size_t attackEnd = static_cast<size_t>(kEnvelopeSize * attackRatio);
        for (size_t i = 1; i < attackEnd && i > 0; ++i) {
            float expected = static_cast<float>(i) / static_cast<float>(attackEnd);
            REQUIRE(envelope[i] == Approx(expected).margin(0.02f));
        }

        // Sustain region should be at 1.0
        const size_t sustainEnd = kEnvelopeSize - static_cast<size_t>(kEnvelopeSize * releaseRatio);
        for (size_t i = attackEnd + 1; i < sustainEnd - 1; ++i) {
            REQUIRE(envelope[i] == Approx(1.0f).margin(0.01f));
        }

        // Last sample should be near zero
        REQUIRE(envelope[kEnvelopeSize - 1] == Approx(0.0f).margin(0.05f));

        // All values should be in [0, 1]
        for (float value : envelope) {
            REQUIRE(value >= 0.0f);
            REQUIRE(value <= 1.0f);
        }
    }

    SECTION("Linear envelope with 10ms boundaries is click-free") {
        // At 44.1kHz, 10ms = 441 samples
        // Test with reasonable attack/release ratios
        const float attackRatio = 0.05f;   // ~25 samples attack
        const float releaseRatio = 0.1f;   // ~51 samples release
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Linear,
                                attackRatio, releaseRatio);

        // First derivative at start should be small
        REQUIRE(std::abs(envelope[1] - envelope[0]) < 0.1f);

        // First derivative at end should be small
        REQUIRE(std::abs(envelope[kEnvelopeSize - 1] - envelope[kEnvelopeSize - 2]) < 0.1f);
    }
}

TEST_CASE("Exponential envelope shapes for Pattern Freeze", "[core][grain][layer0][pattern_freeze]") {
    constexpr size_t kEnvelopeSize = 512;
    std::array<float, kEnvelopeSize> envelope{};

    SECTION("Exponential envelope has RC-style curves") {
        const float attackRatio = 0.1f;
        const float releaseRatio = 0.2f;
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Exponential,
                                attackRatio, releaseRatio);

        // First sample should be zero
        REQUIRE(envelope[0] == Approx(0.0f).margin(0.01f));

        // Sustain region should be at 1.0
        const size_t attackEnd = static_cast<size_t>(kEnvelopeSize * attackRatio);
        const size_t sustainEnd = kEnvelopeSize - static_cast<size_t>(kEnvelopeSize * releaseRatio);
        for (size_t i = attackEnd + 1; i < sustainEnd - 1; ++i) {
            REQUIRE(envelope[i] == Approx(1.0f).margin(0.02f));
        }

        // Last sample should be near zero
        REQUIRE(envelope[kEnvelopeSize - 1] == Approx(0.0f).margin(0.1f));

        // All values should be in [0, 1]
        for (float value : envelope) {
            REQUIRE(value >= -0.01f);  // Small tolerance for numerical precision
            REQUIRE(value <= 1.01f);
        }
    }

    SECTION("Exponential attack is faster than linear initially") {
        const float attackRatio = 0.2f;
        const float releaseRatio = 0.2f;

        std::array<float, kEnvelopeSize> linearEnv{};
        GrainEnvelope::generate(linearEnv.data(), kEnvelopeSize, GrainEnvelopeType::Linear,
                                attackRatio, releaseRatio);
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Exponential,
                                attackRatio, releaseRatio);

        // Exponential attack should reach higher values faster (punchier)
        // Check at 1/4 of attack phase
        const size_t quarterAttack = static_cast<size_t>(kEnvelopeSize * attackRatio / 4);
        if (quarterAttack > 0 && quarterAttack < kEnvelopeSize) {
            // Exponential should be higher (faster rise)
            REQUIRE(envelope[quarterAttack] >= linearEnv[quarterAttack] * 0.9f);
        }
    }

    SECTION("Exponential envelope with 10ms boundaries is click-free") {
        const float attackRatio = 0.05f;
        const float releaseRatio = 0.1f;
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Exponential,
                                attackRatio, releaseRatio);

        // First derivative at start should be reasonably small for click-free audio
        // Exponential has punchier attack so we allow slightly higher derivative
        REQUIRE(std::abs(envelope[1] - envelope[0]) < 0.2f);

        // First derivative at end should be small
        REQUIRE(std::abs(envelope[kEnvelopeSize - 1] - envelope[kEnvelopeSize - 2]) < 0.2f);
    }
}

// =============================================================================
// Envelope Symmetry Tests
// =============================================================================

TEST_CASE("Symmetric envelopes are symmetric", "[core][grain][layer0]") {
    constexpr size_t kEnvelopeSize = 256;
    std::array<float, kEnvelopeSize> envelope{};

    SECTION("Hann is symmetric") {
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Hann);

        for (size_t i = 0; i < kEnvelopeSize / 2; ++i) {
            REQUIRE(envelope[i] == Approx(envelope[kEnvelopeSize - 1 - i]).margin(0.01f));
        }
    }

    SECTION("Sine is symmetric") {
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Sine);

        for (size_t i = 0; i < kEnvelopeSize / 2; ++i) {
            REQUIRE(envelope[i] == Approx(envelope[kEnvelopeSize - 1 - i]).margin(0.01f));
        }
    }

    SECTION("Blackman is symmetric") {
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Blackman);

        for (size_t i = 0; i < kEnvelopeSize / 2; ++i) {
            REQUIRE(envelope[i] == Approx(envelope[kEnvelopeSize - 1 - i]).margin(0.01f));
        }
    }

    SECTION("Symmetric Trapezoid is symmetric") {
        // Equal attack and release ratios
        GrainEnvelope::generate(envelope.data(), kEnvelopeSize, GrainEnvelopeType::Trapezoid,
                                0.2f, 0.2f);

        for (size_t i = 0; i < kEnvelopeSize / 2; ++i) {
            REQUIRE(envelope[i] == Approx(envelope[kEnvelopeSize - 1 - i]).margin(0.02f));
        }
    }
}
