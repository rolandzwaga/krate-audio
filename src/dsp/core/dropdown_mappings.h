// ==============================================================================
// Dropdown Mappings - Type-Safe UI to DSP Conversion
// ==============================================================================
// Type-safe mapping functions from UI dropdown indices to DSP enum values.
// These mappings provide explicit, auditable conversion instead of fragile
// direct casts that assume enum values match dropdown indices.
//
// Why explicit mappings matter:
// - Enum values may not start at 0 or be contiguous
// - UI dropdown order may differ from logical enum order
// - Direct casts are silent failures if enum/dropdown desync
// - Explicit mappings are testable and self-documenting
//
// This header centralizes all enums that need UI dropdown mapping:
// - BBDChipModel: BBD chip era selection
// - LRRatio: Ping-pong L/R timing ratios
// - TimingPattern: Multi-tap rhythm patterns
// - SpatialPattern: Multi-tap pan/level patterns
//
// Constitution Compliance:
// - Principle III: Modern C++ (constexpr, type-safe)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum::DSP {

// =============================================================================
// Dropdown Count Constants
// =============================================================================

/// @brief Number of BBD Era options in dropdown (MN3005, MN3007, MN3205, SAD1024)
inline constexpr int kBBDEraDropdownCount = 4;

/// @brief Number of L/R Ratio options in dropdown
inline constexpr int kLRRatioDropdownCount = 7;

/// @brief Number of Timing Pattern options in dropdown
inline constexpr int kTimingPatternDropdownCount = 20;

/// @brief Number of Spatial Pattern options in dropdown
inline constexpr int kSpatialPatternDropdownCount = 7;

// =============================================================================
// BBDChipModel Enumeration
// =============================================================================

/// @brief BBD chip era selection for analog delay emulation
///
/// Different BBD chips have characteristic frequency responses and noise:
/// - MN3005: Panasonic 4096-stage (Memory Man era) - widest bandwidth, lowest noise
/// - MN3007: Panasonic 1024-stage - medium-dark character
/// - MN3205: Panasonic 4096-stage budget - darker, noisier
/// - SAD1024: Reticon 1024-stage early chip - most noise, limited bandwidth
enum class BBDChipModel : uint8_t {
    MN3005 = 0,   ///< Panasonic 4096-stage (Memory Man) - widest BW, lowest noise
    MN3007 = 1,   ///< Panasonic 1024-stage - medium-dark character
    MN3205 = 2,   ///< Panasonic 4096-stage budget - darker, noisier
    SAD1024 = 3   ///< Reticon 1024-stage early chip - most noise, limited BW
};

// =============================================================================
// LRRatio Enumeration (PingPong delay L/R timing ratios)
// =============================================================================

/// @brief Preset L/R timing ratios for polyrhythmic ping-pong effects
///
/// Each ratio defines multipliers for left and right delay times:
/// - OneToOne: Classic even ping-pong (L=1.0, R=1.0)
/// - TwoToOne: Right is double speed (L=1.0, R=0.5)
/// - ThreeToTwo: Polyrhythmic triplet feel (L=1.0, R=0.667)
/// - FourToThree: Subtle polyrhythm (L=1.0, R=0.75)
/// - OneToTwo: Left is double speed (L=0.5, R=1.0)
/// - TwoToThree: Inverse triplet feel (L=0.667, R=1.0)
/// - ThreeToFour: Inverse subtle polyrhythm (L=0.75, R=1.0)
enum class LRRatio : uint8_t {
    OneToOne = 0,      ///< 1:1 - Classic even ping-pong
    TwoToOne = 1,      ///< 2:1 - R is double speed
    ThreeToTwo = 2,    ///< 3:2 - Polyrhythmic triplet feel
    FourToThree = 3,   ///< 4:3 - Subtle polyrhythm
    OneToTwo = 4,      ///< 1:2 - L is double speed
    TwoToThree = 5,    ///< 2:3 - Inverse triplet feel
    ThreeToFour = 6    ///< 3:4 - Inverse subtle polyrhythm
};

// =============================================================================
// TimingPattern Enumeration (MultiTap rhythm patterns)
// =============================================================================

/// @brief Tap timing patterns for multi-tap delay
///
/// Basic note values map to rhythmic divisions of the beat.
/// Mathematical patterns provide non-rhythmic options.
enum class TimingPattern : uint8_t {
    // Rhythmic patterns - basic note values
    WholeNote = 0,
    HalfNote,
    QuarterNote,
    EighthNote,
    SixteenthNote,
    ThirtySecondNote,

    // Rhythmic patterns - dotted variants
    DottedHalf,
    DottedQuarter,
    DottedEighth,
    DottedSixteenth,

    // Rhythmic patterns - triplet variants
    TripletHalf,
    TripletQuarter,
    TripletEighth,
    TripletSixteenth,

    // Mathematical patterns
    GoldenRatio,      ///< Each tap = previous * 1.618
    Fibonacci,        ///< Taps follow 1, 1, 2, 3, 5, 8... sequence
    Exponential,      ///< Taps at 1x, 2x, 4x, 8x... base time
    PrimeNumbers,     ///< Taps at 2x, 3x, 5x, 7x, 11x... base time
    LinearSpread,     ///< Equal spacing from min to max time

    // Custom pattern
    Custom            ///< User-defined time ratios
};

// =============================================================================
// SpatialPattern Enumeration (MultiTap pan/level patterns)
// =============================================================================

/// @brief Spatial distribution patterns for multi-tap delay
///
/// Controls pan position and level distribution across taps.
enum class SpatialPattern : uint8_t {
    Cascade = 0,      ///< Pan sweeps L->R across taps
    Alternating,      ///< Pan alternates L, R, L, R...
    Centered,         ///< All taps center pan
    WideningStereo,   ///< Pan spreads progressively wider
    DecayingLevel,    ///< Each tap -3dB from previous
    FlatLevel,        ///< All taps equal level
    Custom            ///< User-defined pan/level
};

// =============================================================================
// BBD Era Dropdown Mapping
// =============================================================================

/// @brief Convert dropdown index to BBDChipModel enum
/// @param index Dropdown index (0-3)
/// @return BBDChipModel enum value, defaults to MN3005 for out-of-range
/// @note Lookup table approach for O(1) access and explicit mapping
inline constexpr BBDChipModel getBBDEraFromDropdown(int index) noexcept {
    // Explicit lookup table - order matches UI dropdown
    constexpr BBDChipModel kLookup[] = {
        BBDChipModel::MN3005,   // Index 0: Matsushita 4096-stage (classic)
        BBDChipModel::MN3007,   // Index 1: Matsushita 1024-stage (short)
        BBDChipModel::MN3205,   // Index 2: Matsushita 4096-stage (later)
        BBDChipModel::SAD1024   // Index 3: Reticon 1024-stage (different character)
    };

    if (index < 0 || index >= kBBDEraDropdownCount) {
        return BBDChipModel::MN3005;  // Safe default
    }

    return kLookup[index];
}

// =============================================================================
// L/R Ratio Dropdown Mapping (PingPong)
// =============================================================================

/// @brief Convert dropdown index to LRRatio enum
/// @param index Dropdown index (0-6)
/// @return LRRatio enum value, defaults to OneToOne for out-of-range
inline constexpr LRRatio getLRRatioFromDropdown(int index) noexcept {
    // Explicit lookup table - order matches UI dropdown
    constexpr LRRatio kLookup[] = {
        LRRatio::OneToOne,      // Index 0: 1:1
        LRRatio::TwoToOne,      // Index 1: 2:1
        LRRatio::ThreeToTwo,    // Index 2: 3:2
        LRRatio::FourToThree,   // Index 3: 4:3
        LRRatio::OneToTwo,      // Index 4: 1:2
        LRRatio::TwoToThree,    // Index 5: 2:3
        LRRatio::ThreeToFour    // Index 6: 3:4
    };

    if (index < 0 || index >= kLRRatioDropdownCount) {
        return LRRatio::OneToOne;  // Safe default
    }

    return kLookup[index];
}

// =============================================================================
// Timing Pattern Dropdown Mapping (MultiTap)
// =============================================================================

/// @brief Convert dropdown index to TimingPattern enum
/// @param index Dropdown index (0-19)
/// @return TimingPattern enum value, defaults to QuarterNote for out-of-range
inline constexpr TimingPattern getTimingPatternFromDropdown(int index) noexcept {
    // Explicit lookup table - order matches UI dropdown
    constexpr TimingPattern kLookup[] = {
        // Basic note values (0-5)
        TimingPattern::WholeNote,       // Index 0
        TimingPattern::HalfNote,        // Index 1
        TimingPattern::QuarterNote,     // Index 2
        TimingPattern::EighthNote,      // Index 3
        TimingPattern::SixteenthNote,   // Index 4
        TimingPattern::ThirtySecondNote,// Index 5

        // Dotted variants (6-9)
        TimingPattern::DottedHalf,      // Index 6
        TimingPattern::DottedQuarter,   // Index 7
        TimingPattern::DottedEighth,    // Index 8
        TimingPattern::DottedSixteenth, // Index 9

        // Triplet variants (10-13)
        TimingPattern::TripletHalf,     // Index 10
        TimingPattern::TripletQuarter,  // Index 11
        TimingPattern::TripletEighth,   // Index 12
        TimingPattern::TripletSixteenth,// Index 13

        // Mathematical patterns (14-18)
        TimingPattern::GoldenRatio,     // Index 14
        TimingPattern::Fibonacci,       // Index 15
        TimingPattern::Exponential,     // Index 16
        TimingPattern::PrimeNumbers,    // Index 17
        TimingPattern::LinearSpread,    // Index 18

        // Custom (19)
        TimingPattern::Custom           // Index 19
    };

    if (index < 0 || index >= kTimingPatternDropdownCount) {
        return TimingPattern::QuarterNote;  // Safe default (most common)
    }

    return kLookup[index];
}

// =============================================================================
// Spatial Pattern Dropdown Mapping (MultiTap)
// =============================================================================

/// @brief Convert dropdown index to SpatialPattern enum
/// @param index Dropdown index (0-6)
/// @return SpatialPattern enum value, defaults to Centered for out-of-range
inline constexpr SpatialPattern getSpatialPatternFromDropdown(int index) noexcept {
    // Explicit lookup table - order matches UI dropdown
    constexpr SpatialPattern kLookup[] = {
        SpatialPattern::Cascade,        // Index 0: L->R sweep
        SpatialPattern::Alternating,    // Index 1: L-R-L-R ping-pong
        SpatialPattern::Centered,       // Index 2: All taps centered
        SpatialPattern::WideningStereo, // Index 3: Narrow->Wide spread
        SpatialPattern::DecayingLevel,  // Index 4: Decreasing levels
        SpatialPattern::FlatLevel,      // Index 5: Equal levels
        SpatialPattern::Custom          // Index 6: User-defined
    };

    if (index < 0 || index >= kSpatialPatternDropdownCount) {
        return SpatialPattern::Centered;  // Safe default (neutral)
    }

    return kLookup[index];
}

} // namespace Iterum::DSP
