// ==============================================================================
// Distortion Types for Disrumpo Plugin
// ==============================================================================
// DistortionType enum covering all 26 distortion algorithms, category mapping,
// oversampling recommendations, and display names.
//
// Namespace: Disrumpo (plugin-specific, NOT Krate::DSP)
//
// Reference: specs/003-distortion-integration/spec.md FR-DI-001, FR-DI-005
// ==============================================================================

#pragma once

#include <cstdint>

namespace Disrumpo {

// =============================================================================
// DistortionType Enumeration (FR-DI-001)
// =============================================================================

/// @brief All 26 distortion algorithms available in Disrumpo.
///
/// Types are grouped by category in the enum definition:
/// - Saturation (D01-D06): Classic analog-style saturation
/// - Wavefold (D07-D09): Wavefolding with different models
/// - Rectify (D10-D11): Full-wave and half-wave rectification
/// - Digital (D12-D14, D18-D19): Lo-fi digital effects
/// - Dynamic (D15): Time-varying distortion
/// - Hybrid (D16-D17, D26): Combined distortion techniques
/// - Experimental (D20-D25): Novel/complex algorithms
enum class DistortionType : uint8_t {
    // Saturation (D01-D06)
    SoftClip = 0,       ///< D01 - tanh-based soft saturation
    HardClip,           ///< D02 - Digital hard clipping
    Tube,               ///< D03 - Tube stage emulation
    Tape,               ///< D04 - Tape saturator
    Fuzz,               ///< D05 - Germanium fuzz
    AsymmetricFuzz,     ///< D06 - Silicon fuzz with bias control

    // Wavefold (D07-D09)
    SineFold,           ///< D07 - Sine wavefolder (Serge model)
    TriangleFold,       ///< D08 - Triangle wavefolder (Simple model)
    SergeFold,          ///< D09 - Serge-style wavefolder (Lockhart model)

    // Rectify (D10-D11)
    FullRectify,        ///< D10 - Full-wave rectification
    HalfRectify,        ///< D11 - Half-wave rectification

    // Digital (D12-D14, D18-D19)
    Bitcrush,           ///< D12 - Bit depth reduction
    SampleReduce,       ///< D13 - Sample rate reduction
    Quantize,           ///< D14 - Quantization distortion
    Aliasing,           ///< D18 - Intentional aliasing
    BitwiseMangler,     ///< D19 - Bit rotation and XOR

    // Dynamic (D15)
    Temporal,           ///< D15 - Time-varying distortion

    // Hybrid (D16-D17, D26)
    RingSaturation,     ///< D16 - Ring modulation + saturation
    FeedbackDist,       ///< D17 - Feedback-based distortion
    AllpassResonant,    ///< D26 - Resonant allpass saturation

    // Experimental (D20-D25)
    Chaos,              ///< D20 - Chaotic attractor waveshaping
    Formant,            ///< D21 - Formant filtering + distortion
    Granular,           ///< D22 - Granular distortion
    Spectral,           ///< D23 - FFT-domain distortion
    Fractal,            ///< D24 - Fractal/iterative distortion
    Stochastic,         ///< D25 - Noise-modulated distortion

    COUNT               ///< Sentinel for iteration (26 types)
};

/// @brief Total number of distortion types.
constexpr int kDistortionTypeCount = static_cast<int>(DistortionType::COUNT);

// =============================================================================
// DistortionCategory Enumeration
// =============================================================================

/// @brief Category groupings for UI organization and morphing.
enum class DistortionCategory : uint8_t {
    Saturation = 0,     ///< D01-D06
    Wavefold,           ///< D07-D09
    Rectify,            ///< D10-D11
    Digital,            ///< D12-D14, D18-D19
    Dynamic,            ///< D15
    Hybrid,             ///< D16-D17, D26
    Experimental        ///< D20-D25
};

// =============================================================================
// Category Mapping (FR-DI-001)
// =============================================================================

/// @brief Get the category for a distortion type.
/// @param type The distortion type
/// @return Category enum value
constexpr DistortionCategory getCategory(DistortionType type) noexcept {
    switch (type) {
        // Saturation (D01-D06)
        case DistortionType::SoftClip:
        case DistortionType::HardClip:
        case DistortionType::Tube:
        case DistortionType::Tape:
        case DistortionType::Fuzz:
        case DistortionType::AsymmetricFuzz:
            return DistortionCategory::Saturation;

        // Wavefold (D07-D09)
        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
            return DistortionCategory::Wavefold;

        // Rectify (D10-D11)
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
            return DistortionCategory::Rectify;

        // Digital (D12-D14, D18-D19)
        case DistortionType::Bitcrush:
        case DistortionType::SampleReduce:
        case DistortionType::Quantize:
        case DistortionType::Aliasing:
        case DistortionType::BitwiseMangler:
            return DistortionCategory::Digital;

        // Dynamic (D15)
        case DistortionType::Temporal:
            return DistortionCategory::Dynamic;

        // Hybrid (D16-D17, D26)
        case DistortionType::RingSaturation:
        case DistortionType::FeedbackDist:
        case DistortionType::AllpassResonant:
            return DistortionCategory::Hybrid;

        // Experimental (D20-D25)
        case DistortionType::Chaos:
        case DistortionType::Formant:
        case DistortionType::Granular:
        case DistortionType::Spectral:
        case DistortionType::Fractal:
        case DistortionType::Stochastic:
            return DistortionCategory::Experimental;

        default:
            return DistortionCategory::Saturation;
    }
}

// =============================================================================
// Oversampling Recommendations (FR-DI-005)
// =============================================================================

/// @brief Get recommended oversampling factor for a distortion type.
///
/// Returns the factor that provides acceptable aliasing suppression for
/// each type's harmonic generation characteristics. Digital/lo-fi types
/// return 1 because aliasing is intentional.
///
/// @param type The distortion type
/// @return Recommended oversampling factor (1, 2, or 4)
constexpr int getRecommendedOversample(DistortionType type) noexcept {
    switch (type) {
        // 4x types - strong harmonics or frequency doubling
        case DistortionType::HardClip:
        case DistortionType::Fuzz:
        case DistortionType::AsymmetricFuzz:
        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
        case DistortionType::RingSaturation:
        case DistortionType::AllpassResonant:
            return 4;

        // 1x types - aliasing is intentional or FFT-domain
        case DistortionType::Bitcrush:
        case DistortionType::SampleReduce:
        case DistortionType::Quantize:
        case DistortionType::Aliasing:
        case DistortionType::BitwiseMangler:
        case DistortionType::Spectral:
            return 1;

        // 2x types - moderate harmonics (default)
        case DistortionType::SoftClip:
        case DistortionType::Tube:
        case DistortionType::Tape:
        case DistortionType::Temporal:
        case DistortionType::FeedbackDist:
        case DistortionType::Chaos:
        case DistortionType::Formant:
        case DistortionType::Granular:
        case DistortionType::Fractal:
        case DistortionType::Stochastic:
        default:
            return 2;
    }
}

// =============================================================================
// Display Names
// =============================================================================

/// @brief Get human-readable display name for a distortion type.
/// @param type The distortion type
/// @return C-string display name
constexpr const char* getTypeName(DistortionType type) noexcept {
    switch (type) {
        case DistortionType::SoftClip:       return "Soft Clip";
        case DistortionType::HardClip:       return "Hard Clip";
        case DistortionType::Tube:           return "Tube";
        case DistortionType::Tape:           return "Tape";
        case DistortionType::Fuzz:           return "Fuzz";
        case DistortionType::AsymmetricFuzz: return "Asymmetric Fuzz";
        case DistortionType::SineFold:       return "Sine Fold";
        case DistortionType::TriangleFold:   return "Triangle Fold";
        case DistortionType::SergeFold:      return "Serge Fold";
        case DistortionType::FullRectify:    return "Full Rectify";
        case DistortionType::HalfRectify:    return "Half Rectify";
        case DistortionType::Bitcrush:       return "Bitcrush";
        case DistortionType::SampleReduce:   return "Sample Reduce";
        case DistortionType::Quantize:       return "Quantize";
        case DistortionType::Aliasing:       return "Aliasing";
        case DistortionType::BitwiseMangler: return "Bitwise Mangler";
        case DistortionType::Temporal:       return "Temporal";
        case DistortionType::RingSaturation: return "Ring Saturation";
        case DistortionType::FeedbackDist:   return "Feedback";
        case DistortionType::AllpassResonant:return "Allpass Resonant";
        case DistortionType::Chaos:          return "Chaos";
        case DistortionType::Formant:        return "Formant";
        case DistortionType::Granular:       return "Granular";
        case DistortionType::Spectral:       return "Spectral";
        case DistortionType::Fractal:        return "Fractal";
        case DistortionType::Stochastic:     return "Stochastic";
        default:                             return "Unknown";
    }
}

/// @brief Get display name for a category.
/// @param category The distortion category
/// @return C-string display name
constexpr const char* getCategoryName(DistortionCategory category) noexcept {
    switch (category) {
        case DistortionCategory::Saturation:   return "Saturation";
        case DistortionCategory::Wavefold:     return "Wavefold";
        case DistortionCategory::Rectify:      return "Rectify";
        case DistortionCategory::Digital:      return "Digital";
        case DistortionCategory::Dynamic:      return "Dynamic";
        case DistortionCategory::Hybrid:       return "Hybrid";
        case DistortionCategory::Experimental: return "Experimental";
        default:                               return "Unknown";
    }
}

// =============================================================================
// DistortionFamily Enumeration (FR-016)
// =============================================================================

/// @brief Family groupings for morph interpolation strategy.
///
/// Different families use different interpolation methods during morphing:
/// - Same-family morphs: Use family-specific interpolation (single processor)
/// - Cross-family morphs: Use parallel processing with equal-power crossfade
///
/// Per spec FR-016: Seven families with specific interpolation methods.
enum class DistortionFamily : uint8_t {
    Saturation = 0,     ///< D01-D06: Transfer function interpolation
    Wavefold,           ///< D07-D09: Parameter interpolation
    Digital,            ///< D12-D14, D18-D19: Parameter interpolation
    Rectify,            ///< D10-D11: Parameter interpolation
    Dynamic,            ///< D15, D17: Parameter interpolation + envelope coupling
    Hybrid,             ///< D16, D26: Parallel blend with output crossfade
    Experimental        ///< D20-D25: Parallel blend with output crossfade
};

/// @brief Total number of distortion families.
constexpr int kDistortionFamilyCount = 7;

// =============================================================================
// Family Mapping (FR-016)
// =============================================================================

/// @brief Get the family for a distortion type.
///
/// Maps DistortionType to DistortionFamily for morph interpolation strategy.
/// Per spec FR-016 family-type mapping table.
///
/// @param type The distortion type
/// @return Family enum value
constexpr DistortionFamily getFamily(DistortionType type) noexcept {
    switch (type) {
        // Saturation (D01-D06) - Transfer function interpolation
        case DistortionType::SoftClip:
        case DistortionType::HardClip:
        case DistortionType::Tube:
        case DistortionType::Tape:
        case DistortionType::Fuzz:
        case DistortionType::AsymmetricFuzz:
            return DistortionFamily::Saturation;

        // Wavefold (D07-D09) - Parameter interpolation
        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
            return DistortionFamily::Wavefold;

        // Rectify (D10-D11) - Parameter interpolation
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
            return DistortionFamily::Rectify;

        // Digital (D12-D14, D18-D19) - Parameter interpolation
        case DistortionType::Bitcrush:
        case DistortionType::SampleReduce:
        case DistortionType::Quantize:
        case DistortionType::Aliasing:
        case DistortionType::BitwiseMangler:
            return DistortionFamily::Digital;

        // Dynamic (D15, D17) - Parameter interpolation + envelope coupling
        case DistortionType::Temporal:
        case DistortionType::FeedbackDist:
            return DistortionFamily::Dynamic;

        // Hybrid (D16, D26) - Parallel blend with output crossfade
        case DistortionType::RingSaturation:
        case DistortionType::AllpassResonant:
            return DistortionFamily::Hybrid;

        // Experimental (D20-D25) - Parallel blend with output crossfade
        case DistortionType::Chaos:
        case DistortionType::Formant:
        case DistortionType::Granular:
        case DistortionType::Spectral:
        case DistortionType::Fractal:
        case DistortionType::Stochastic:
            return DistortionFamily::Experimental;

        default:
            return DistortionFamily::Saturation;
    }
}

/// @brief Get display name for a family.
/// @param family The distortion family
/// @return C-string display name
constexpr const char* getFamilyName(DistortionFamily family) noexcept {
    switch (family) {
        case DistortionFamily::Saturation:   return "Saturation";
        case DistortionFamily::Wavefold:     return "Wavefold";
        case DistortionFamily::Rectify:      return "Rectify";
        case DistortionFamily::Digital:      return "Digital";
        case DistortionFamily::Dynamic:      return "Dynamic";
        case DistortionFamily::Hybrid:       return "Hybrid";
        case DistortionFamily::Experimental: return "Experimental";
        default:                             return "Unknown";
    }
}

// =============================================================================
// MorphMode Enumeration (FR-003, FR-004, FR-005)
// =============================================================================

/// @brief Morph mode defines how cursor position maps to node weights.
///
/// Per spec:
/// - FR-003: 1D Linear mode - nodes arranged on single axis
/// - FR-004: 2D Planar mode - nodes occupy XY positions
/// - FR-005: 2D Radial mode - position defined by angle and distance
enum class MorphMode : uint8_t {
    Linear1D = 0,   ///< Single axis A-B-C-D interpolation using morphX only
    Planar2D,       ///< XY position in node space (2D inverse distance)
    Radial2D        ///< Angle + distance from center (polar coordinates)
};

/// @brief Total number of morph modes.
constexpr int kMorphModeCount = 3;

/// @brief Get display name for a morph mode.
/// @param mode The morph mode
/// @return C-string display name
constexpr const char* getMorphModeName(MorphMode mode) noexcept {
    switch (mode) {
        case MorphMode::Linear1D: return "Linear";
        case MorphMode::Planar2D: return "Planar";
        case MorphMode::Radial2D: return "Radial";
        default:                  return "Unknown";
    }
}

} // namespace Disrumpo
