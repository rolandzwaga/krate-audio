// ==============================================================================
// Layer 0: Core Utility - Euclidean Pattern Generator
// ==============================================================================
// Implements the Bjorklund/Euclidean rhythm algorithm for Pattern Freeze Mode.
//
// The Euclidean algorithm distributes k pulses among n steps as evenly as
// possible, producing rhythmic patterns found in traditional music worldwide:
// - E(3,8) = Tresillo (Cuban/Afro-Cuban)
// - E(5,8) = Cinquillo
// - E(5,12) = West African bell pattern
//
// Uses the accumulator method from Paul Batchelor's sndkit for simplicity
// and real-time safety (no allocation, O(n) generation, O(1) lookup).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocation)
// - Principle III: Modern C++ (constexpr, static functions)
// - Principle IX: Layer 0 (no dependencies on higher layers)
//
// Reference:
// - Toussaint's "The Euclidean Algorithm Generates Traditional Musical Rhythms"
// - Paul Batchelor's sndkit: https://paulbatchelor.github.io/sndkit/euclid/
// - specs/069-pattern-freeze/research.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cstdint>

namespace Krate::DSP {

/// @brief Euclidean/Bjorklund rhythm pattern generator
///
/// Generates rhythmic patterns using the Euclidean algorithm, which distributes
/// a given number of pulses (hits) across a given number of steps as evenly as
/// possible. The patterns are returned as bitmasks for O(1) lookup.
///
/// @note All methods are static, constexpr, and noexcept for real-time safety.
/// @note Maximum of 32 steps supported (fits in uint32_t bitmask).
///
/// @example
/// @code
/// // Generate tresillo pattern: 3 hits in 8 steps
/// uint32_t pattern = EuclideanPattern::generate(3, 8, 0);
///
/// // Check if step 0 is a hit
/// bool hit = EuclideanPattern::isHit(pattern, 0, 8);  // true
///
/// // Check if step 1 is a hit
/// hit = EuclideanPattern::isHit(pattern, 1, 8);  // false
/// @endcode
class EuclideanPattern {
public:
    /// @brief Minimum number of steps in a pattern
    static constexpr int kMinSteps = 2;

    /// @brief Maximum number of steps in a pattern (limited by uint32_t bitmask)
    static constexpr int kMaxSteps = 32;

    /// @brief Generate a Euclidean pattern as a bitmask
    ///
    /// Uses the Bresenham-style accumulator method: for each step i, add pulses
    /// to an accumulator. When accumulator >= steps, we have a hit and subtract
    /// steps. This distributes pulses as evenly as possible across steps.
    ///
    /// @param pulses Number of hits/pulses (0 to steps)
    /// @param steps Total number of steps (2 to 32)
    /// @param rotation Pattern rotation offset (0 to steps-1)
    /// @return Bitmask where bit i is set if step i is a hit
    ///
    /// @note Pulses is clamped to [0, steps]
    /// @note Steps is clamped to [kMinSteps, kMaxSteps]
    /// @note Rotation is taken modulo steps
    [[nodiscard]] static constexpr uint32_t generate(int pulses, int steps,
                                                      int rotation = 0) noexcept {
        // Clamp steps to valid range
        steps = std::clamp(steps, kMinSteps, kMaxSteps);

        // Clamp pulses to [0, steps]
        pulses = std::clamp(pulses, 0, steps);

        // Handle edge cases
        if (pulses == 0) {
            return 0u;  // No hits
        }
        if (pulses >= steps) {
            // All steps are hits: set lowest 'steps' bits
            return (1u << steps) - 1u;
        }

        // Wrap rotation to valid range
        rotation = ((rotation % steps) + steps) % steps;

        // Generate pattern using Bresenham-style accumulator
        // Start accumulator at steps to ensure first position is a hit
        // (standard Euclidean pattern convention: step 0 is always a hit when rotation=0)
        uint32_t pattern = 0u;
        int accumulator = steps;  // Start full to trigger hit at position 0

        for (int i = 0; i < steps; ++i) {
            if (accumulator >= steps) {
                accumulator -= steps;
                // Apply rotation: read from rotated position, write to position i
                // rotation shifts the pattern to the right (later positions)
                const int srcPos = ((i - rotation) % steps + steps) % steps;
                // We set bit at srcPos in unrotated, which becomes bit i in rotated
                pattern |= (1u << i);
            }
            accumulator += pulses;
        }

        // Apply rotation by rotating the bitmask
        if (rotation != 0) {
            const uint32_t mask = (1u << steps) - 1u;
            uint32_t rotated = ((pattern >> rotation) | (pattern << (steps - rotation))) & mask;
            return rotated;
        }

        return pattern;
    }

    /// @brief Check if a step position is a hit in the pattern
    ///
    /// @param pattern The pattern bitmask from generate()
    /// @param position Step position to check (0 to steps-1)
    /// @param steps Total number of steps in the pattern
    /// @return true if the position is a hit, false otherwise
    ///
    /// @note Returns false for out-of-bounds positions (negative or >= steps)
    [[nodiscard]] static constexpr bool isHit(uint32_t pattern, int position,
                                               int steps) noexcept {
        // Bounds check
        if (position < 0 || position >= steps || steps > kMaxSteps) {
            return false;
        }

        // Check bit at position
        return (pattern >> position) & 1u;
    }

    /// @brief Count the number of hits in a pattern
    ///
    /// @param pattern The pattern bitmask from generate()
    /// @return Number of bits set (hits) in the pattern
    [[nodiscard]] static constexpr int countHits(uint32_t pattern) noexcept {
        // Brian Kernighan's bit counting algorithm
        int count = 0;
        while (pattern) {
            pattern &= (pattern - 1);  // Clear lowest set bit
            ++count;
        }
        return count;
    }
};

}  // namespace Krate::DSP
