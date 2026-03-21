#pragma once

// ==============================================================================
// XorShift32 - Deterministic xorshift32 PRNG for per-voice noise
// ==============================================================================
// Layer 0 Core | Namespace: Krate::DSP
//
// Fast, deterministic pseudo-random numbers suitable for audio applications.
// Uses 3 XOR/shift operations per sample with no stdlib dependency.
// Deterministic output enables golden-reference testing.
//
// Seeding uses a multiplicative hash to ensure good inter-voice distribution.
// Raw voiceId values (0, 1, 2...) would produce correlated sequences.
//
// Real-Time Safety: All methods are noexcept, branchless (except absorbing-
// state guard in seed), and allocation-free.
// ==============================================================================

#include <cstdint>

namespace Krate {
namespace DSP {

struct XorShift32 {
    uint32_t state = 0x12345678u;

    /// Seed the generator with a voice-unique hash.
    /// @param voiceId Voice index (0-based)
    void seed(uint32_t voiceId) noexcept
    {
        state = 0x9E3779B9u ^ (voiceId * 0x85EBCA6Bu);
        if (state == 0)
            state = 0x9E3779B9u; // Prevent absorbing state
    }

    /// Generate next 32-bit random value.
    [[nodiscard]] uint32_t next() noexcept
    {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return state = x;
    }

    /// Generate random float in [0.0, 1.0).
    [[nodiscard]] float nextFloat() noexcept
    {
        return static_cast<float>(next()) * 2.3283064365386963e-10f;
    }

    /// Generate random float in [-1.0, 1.0).
    [[nodiscard]] float nextFloatSigned() noexcept
    {
        return nextFloat() * 2.0f - 1.0f;
    }
};

} // namespace DSP
} // namespace Krate
