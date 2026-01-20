// ==============================================================================
// Layer 0: Core Utilities
// filter_tables.h - Formant Frequency/Bandwidth Tables
// ==============================================================================
// API Contract for specs/070-filter-foundations
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (constexpr, no allocations)
// - Principle III: Modern C++ (constexpr, inline)
// - Principle IX: Layer 0 (no dependencies on other DSP layers)
// ==============================================================================

#pragma once

#include <array>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Vowel Enum (FR-005)
// =============================================================================

/// @brief Vowel selection for type-safe formant table indexing.
///
/// Maps to standard IPA vowel sounds for synthesis applications.
/// Cast to size_t for array indexing: static_cast<size_t>(Vowel::A)
enum class Vowel : uint8_t {
    A = 0,  ///< Open front unrounded vowel [a] as in "father"
    E = 1,  ///< Close-mid front unrounded vowel [e] as in "bed"
    I = 2,  ///< Close front unrounded vowel [i] as in "see"
    O = 3,  ///< Close-mid back rounded vowel [o] as in "go"
    U = 4   ///< Close back rounded vowel [u] as in "boot"
};

/// Number of vowels in the formant table.
inline constexpr size_t kNumVowels = 5;

// =============================================================================
// FormantData Struct (FR-001)
// =============================================================================

/// @brief Formant frequency and bandwidth data for a single vowel.
///
/// Contains the first three formant frequencies (F1, F2, F3) and their
/// corresponding bandwidths (BW1, BW2, BW3). These values are derived from
/// phonetic research and are commonly used in vocal synthesis and formant
/// filtering applications.
///
/// @note F1 relates to tongue height (higher = more open vowel)
/// @note F2 relates to tongue frontness (higher = more front vowel)
/// @note F3 relates to lip rounding and speaker characteristics
struct FormantData {
    float f1;   ///< First formant frequency in Hz (typically 250-800 Hz)
    float f2;   ///< Second formant frequency in Hz (typically 600-2200 Hz)
    float f3;   ///< Third formant frequency in Hz (typically 2200-3000 Hz)
    float bw1;  ///< First formant bandwidth in Hz (typically 40-80 Hz)
    float bw2;  ///< Second formant bandwidth in Hz (typically 60-100 Hz)
    float bw3;  ///< Third formant bandwidth in Hz (typically 100-150 Hz)
};

// =============================================================================
// Formant Table (FR-002, FR-003)
// =============================================================================

/// @brief Formant frequency table for bass male voice.
///
/// Constexpr array containing formant data for 5 vowels (A, E, I, O, U).
/// Values are based on the Csound formant table, which is an industry
/// standard for speech synthesis derived from phonetic research.
///
/// Source: Csound Manual, Appendix Table 3 (Bass voice)
/// Reference: Peterson & Barney (1952), Fant (1972)
///
/// @example
/// ```cpp
/// // Get formant data for vowel 'a'
/// const auto& a = kVowelFormants[static_cast<size_t>(Vowel::A)];
/// float f1 = a.f1;  // 600.0 Hz
/// ```
inline constexpr std::array<FormantData, kNumVowels> kVowelFormants = {{
    // Vowel A: F1=600, F2=1040, F3=2250 Hz
    {600.0f, 1040.0f, 2250.0f, 60.0f, 70.0f, 110.0f},

    // Vowel E: F1=400, F2=1620, F3=2400 Hz
    {400.0f, 1620.0f, 2400.0f, 40.0f, 80.0f, 100.0f},

    // Vowel I: F1=250, F2=1750, F3=2600 Hz
    {250.0f, 1750.0f, 2600.0f, 60.0f, 90.0f, 100.0f},

    // Vowel O: F1=400, F2=750, F3=2400 Hz
    {400.0f, 750.0f, 2400.0f, 40.0f, 80.0f, 100.0f},

    // Vowel U: F1=350, F2=600, F3=2400 Hz
    {350.0f, 600.0f, 2400.0f, 40.0f, 80.0f, 100.0f},
}};

// =============================================================================
// Helper Functions
// =============================================================================

/// @brief Get formant data for a specific vowel.
///
/// Type-safe accessor for the formant table using the Vowel enum.
///
/// @param v Vowel to retrieve formant data for
/// @return Const reference to FormantData for the specified vowel
///
/// @example
/// ```cpp
/// const auto& formant = getFormant(Vowel::I);
/// // Use formant.f1, formant.f2, formant.f3 to configure bandpass filters
/// ```
[[nodiscard]] inline constexpr const FormantData& getFormant(Vowel v) noexcept {
    return kVowelFormants[static_cast<size_t>(v)];
}

} // namespace DSP
} // namespace Krate
