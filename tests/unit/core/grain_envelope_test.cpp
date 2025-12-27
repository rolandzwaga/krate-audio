// Layer 0: Core Utility Tests - Grain Envelope
// Part of Granular Delay feature (spec 034)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/core/grain_envelope.h"

#include <array>
#include <cmath>
#include <numeric>

using namespace Iterum::DSP;
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
