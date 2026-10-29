// ==============================================================================
// Layer 0: Core Utility - Pattern Freeze Type Definitions
// ==============================================================================
// Type definitions for Pattern Freeze Mode (spec 069).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (trivial types, no allocation)
// - Principle III: Modern C++ (enum class, constexpr)
// - Principle IX: Layer 0 (no dependencies on higher layers)
//
// Reference: specs/069-pattern-freeze/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// Pattern Type Enumeration
// =============================================================================

/// Pattern algorithm type for Pattern Freeze Mode
/// @note Maps to UI dropdown and serialization
enum class PatternType : uint8_t {
    Euclidean = 0,      ///< Bjorklund algorithm rhythm patterns
    GranularScatter,    ///< Poisson process random grain triggering
    HarmonicDrones,     ///< Sustained multi-voice playback
    NoiseBursts         ///< Rhythmic filtered noise generation
};

inline constexpr int kPatternTypeCount = 4;
inline constexpr PatternType kDefaultPatternType = PatternType::Euclidean;

/// Get pattern type name for UI display
/// @param type Pattern type enum value
/// @return Human-readable name
[[nodiscard]] constexpr const char* getPatternTypeName(PatternType type) noexcept {
    constexpr const char* kNames[] = {
        "Euclidean",
        "Granular Scatter",
        "Harmonic Drones",
        "Noise Bursts"
    };
    const auto index = static_cast<size_t>(type);
    return (index < kPatternTypeCount) ? kNames[index] : "Unknown";
}

// =============================================================================
// Slice Mode Enumeration
// =============================================================================

/// Determines how slice length is controlled
enum class SliceMode : uint8_t {
    Fixed = 0,    ///< All slices use the configured slice length
    Variable      ///< Slice length varies with pattern (e.g., Euclidean step position)
};

inline constexpr int kSliceModeCount = 2;
inline constexpr SliceMode kDefaultSliceMode = SliceMode::Fixed;

/// Get slice mode name for UI display
/// @param mode Slice mode enum value
/// @return Human-readable name
[[nodiscard]] constexpr const char* getSliceModeName(SliceMode mode) noexcept {
    constexpr const char* kNames[] = {
        "Fixed",
        "Variable"
    };
    const auto index = static_cast<size_t>(mode);
    return (index < kSliceModeCount) ? kNames[index] : "Unknown";
}

// =============================================================================
// Pitch Interval Enumeration
// =============================================================================

/// Musical intervals for Harmonic Drones voices
enum class PitchInterval : uint8_t {
    Unison = 0,     ///< 0 semitones
    MinorThird,     ///< 3 semitones
    MajorThird,     ///< 4 semitones
    Fourth,         ///< 5 semitones (Perfect Fourth)
    Fifth,          ///< 7 semitones (Perfect Fifth)
    Octave          ///< 12 semitones
};

inline constexpr int kPitchIntervalCount = 6;
inline constexpr PitchInterval kDefaultPitchInterval = PitchInterval::Octave;

/// Get semitone offset for pitch interval
/// @param interval Pitch interval enum value
/// @return Interval in semitones
[[nodiscard]] constexpr float getIntervalSemitones(PitchInterval interval) noexcept {
    constexpr float kSemitones[] = { 0.0f, 3.0f, 4.0f, 5.0f, 7.0f, 12.0f };
    const auto index = static_cast<size_t>(interval);
    return (index < kPitchIntervalCount) ? kSemitones[index] : 0.0f;
}

/// Get pitch interval name for UI display
/// @param interval Pitch interval enum value
/// @return Human-readable name
[[nodiscard]] constexpr const char* getPitchIntervalName(PitchInterval interval) noexcept {
    constexpr const char* kNames[] = {
        "Unison",
        "Minor 3rd",
        "Major 3rd",
        "Perfect 4th",
        "Perfect 5th",
        "Octave"
    };
    const auto index = static_cast<size_t>(interval);
    return (index < kPitchIntervalCount) ? kNames[index] : "Unknown";
}

// =============================================================================
// Noise Color Enumeration
// =============================================================================

/// Noise spectrum types for Noise Bursts pattern
/// Maps to NoiseType in noise_generator.h
enum class NoiseColor : uint8_t {
    White = 0,    ///< Flat spectrum (equal energy per Hz)
    Pink,         ///< 1/f spectrum (-3dB/octave)
    Brown,        ///< 1/f^2 spectrum (-6dB/octave, also called red noise)
    Blue,         ///< +3dB/octave (bright, high-frequency emphasis)
    Violet,       ///< +6dB/octave (very bright, aggressive highs)
    Grey,         ///< Inverse A-weighting (perceptually flat loudness)
    Velvet,       ///< Sparse random impulses (smooth, textural)
    RadioStatic   ///< Band-limited ~5kHz (AM radio character)
};

inline constexpr int kNoiseColorCount = 8;
inline constexpr NoiseColor kDefaultNoiseColor = NoiseColor::Pink;

/// Get noise color name for UI display
/// @param color Noise color enum value
/// @return Human-readable name
[[nodiscard]] constexpr const char* getNoiseColorName(NoiseColor color) noexcept {
    constexpr const char* kNames[] = {
        "White",
        "Pink",
        "Brown",
        "Blue",
        "Violet",
        "Grey",
        "Velvet",
        "Radio"
    };
    const auto index = static_cast<size_t>(color);
    return (index < kNoiseColorCount) ? kNames[index] : "Unknown";
}

// =============================================================================
// Envelope Shape Enumeration
// =============================================================================

/// Envelope curve types for slice/grain amplitude shaping
enum class EnvelopeShape : uint8_t {
    Linear = 0,      ///< Triangle/trapezoid with linear attack/release
    Exponential      ///< RC-style curves with punchier attack
};

inline constexpr int kEnvelopeShapeCount = 2;
inline constexpr EnvelopeShape kDefaultEnvelopeShape = EnvelopeShape::Linear;

/// Get envelope shape name for UI display
/// @param shape Envelope shape enum value
/// @return Human-readable name
[[nodiscard]] constexpr const char* getEnvelopeShapeName(EnvelopeShape shape) noexcept {
    constexpr const char* kNames[] = {
        "Linear",
        "Exponential"
    };
    const auto index = static_cast<size_t>(shape);
    return (index < kEnvelopeShapeCount) ? kNames[index] : "Unknown";
}

// =============================================================================
// Capture Length Enumeration (specs/iterum-freeze/design.md 3.1)
// =============================================================================

/// The capture cycle: the note value each window is recorded over
enum class CaptureLength : uint8_t {
    Quarter = 0,   ///< 1/4: one beat
    Half,          ///< 1/2: two beats
    Whole,         ///< 1/1: four beats
    DoubleWhole    ///< 2/1: eight beats
};

inline constexpr int kCaptureLengthCount = 4;
inline constexpr CaptureLength kDefaultCaptureLength = CaptureLength::Whole;

/// Get the capture cycle length in beats (1, 2, 4 or 8)
[[nodiscard]] constexpr double getCaptureLengthBeats(CaptureLength length) noexcept {
    constexpr double kBeats[] = {1.0, 2.0, 4.0, 8.0};
    const auto index = static_cast<size_t>(length);
    return (index < kCaptureLengthCount) ? kBeats[index] : 4.0;
}

/// Get capture length name for UI display ("1/4", "1/2", "1/1", "2/1")
[[nodiscard]] constexpr const char* getCaptureLengthName(CaptureLength length) noexcept {
    constexpr const char* kNames[] = {"1/4", "1/2", "1/1", "2/1"};
    const auto index = static_cast<size_t>(length);
    return (index < kCaptureLengthCount) ? kNames[index] : "Unknown";
}

// =============================================================================
// Dry Mode Enumeration (specs/iterum-freeze/design.md 2.7, 3.1)
// =============================================================================

/// The mix law of the Freeze mode
enum class DryMode : uint8_t {
    Mix = 0,   ///< out = in (1 - m) + wet m (the crossfade)
    Insert     ///< out = in (1 - m g) + wet m (the dry is ducked only while the gate g is open)
};

inline constexpr int kDryModeCount = 2;
inline constexpr DryMode kDefaultDryMode = DryMode::Insert;

// =============================================================================
// Pattern Freeze Constants
// =============================================================================

namespace PatternFreezeConstants {

/// Slice length limits
inline constexpr float kMinSliceLengthMs = 10.0f;
inline constexpr float kMaxSliceLengthMs = 2000.0f;
inline constexpr float kDefaultSliceLengthMs = 200.0f;

/// Euclidean pattern limits
inline constexpr int kMinEuclideanSteps = 2;
inline constexpr int kMaxEuclideanSteps = 32;
inline constexpr int kDefaultEuclideanSteps = 8;
inline constexpr int kDefaultEuclideanHits = 3;
inline constexpr int kDefaultEuclideanRotation = 0;

/// Granular scatter limits
inline constexpr float kMinGranularDensityHz = 1.0f;
inline constexpr float kMaxGranularDensityHz = 50.0f;
inline constexpr float kDefaultGranularDensityHz = 10.0f;
inline constexpr float kMinGranularGrainSizeMs = 10.0f;
inline constexpr float kMaxGranularGrainSizeMs = 500.0f;
inline constexpr float kDefaultGranularGrainSizeMs = 100.0f;
inline constexpr float kDefaultPositionJitter = 0.5f;   // 50%
inline constexpr float kDefaultSizeJitter = 0.25f;      // 25%

/// Harmonic drones limits
inline constexpr int kMinDroneVoices = 1;
inline constexpr int kMaxDroneVoices = 4;
inline constexpr int kDefaultDroneVoices = 2;
inline constexpr float kMinDroneDriftRateHz = 0.1f;
inline constexpr float kMaxDroneDriftRateHz = 2.0f;
inline constexpr float kDefaultDroneDriftRateHz = 0.5f;
inline constexpr float kDefaultDroneDrift = 0.3f;       // 30%

/// Noise filter limits
inline constexpr float kMinNoiseFilterCutoffHz = 20.0f;
inline constexpr float kMaxNoiseFilterCutoffHz = 20000.0f;
inline constexpr float kDefaultNoiseFilterCutoffHz = 2000.0f;
inline constexpr float kDefaultNoiseFilterSweep = 0.5f; // 50%

/// Envelope limits
inline constexpr float kMinEnvelopeAttackMs = 0.0f;
inline constexpr float kMaxEnvelopeAttackMs = 500.0f;
inline constexpr float kDefaultEnvelopeAttackMs = 10.0f;
inline constexpr float kMinEnvelopeReleaseMs = 0.0f;
inline constexpr float kMaxEnvelopeReleaseMs = 2000.0f;
inline constexpr float kDefaultEnvelopeReleaseMs = 100.0f;

/// Capture buffer limits
inline constexpr float kMinCaptureBufferSeconds = 1.0f;
inline constexpr float kMaxCaptureBufferSeconds = 10.0f;
inline constexpr float kMinReadyBufferMs = 200.0f;

/// Maximum polyphony
inline constexpr size_t kMaxSlices = 8;

/// The held window (specs/iterum-freeze/design.md 3.1)
inline constexpr float kMaxCaptureLengthSeconds = 5.0f;        // each cycle buffer (2.1; R4)
inline constexpr float kCaptureSilenceFloor = 1e-3f;           // -60 dBFS: the onset and a silent window (2.2)
inline constexpr float kDuckSmoothingMs = 2.0f;                // gv: OnePoleSmoother 99 % time, tau 0.4 ms (2.7)
inline constexpr float kHoldDuckSmoothingMs = 20.0f;           // gh: OnePoleSmoother 99 % time, tau 4 ms (2.7)
inline constexpr float kNoiseLevelDb = -10.0f;                 // all eight colours (2.5.4; R14, -10.5 missed both bars)
inline constexpr int kDroneSourceHeads = 4;                    // drone source heads, 75 % overlap (2.5.3)
inline constexpr float kDroneMinPassMs = 100.0f;               // shortest source pass (2.5.3)
inline constexpr int kDroneChunkSamples = 64;                  // == PitchShiftProcessor::kSmoothingSubBlockSize (2.5.3)
inline constexpr float kDroneVoiceFadeMs = 20.0f;              // a voice joining or leaving (2.5.3)
inline constexpr float kSpliceSpanMs = 5.0f;                   // findSplice correlation span (2.10)
inline constexpr float kSpliceSearchMs = 5.0f;                 // findSplice search radius; kDroneAlignMs (2.10)
inline constexpr float kSpliceFadeMs = 5.0f;                   // equal-gain splice / seam fade (2.10)
inline constexpr int kBurstVoices = 2;                         // burst voices, each kMaxSliceLengthMs x 2 ch (2.5.4)
inline constexpr float kForcedHeadFadeMs = 5.0f;               // 2.6, and the older burst voice (2.5.4)
inline constexpr double kMaxClockTempoBPM = 1000.0;            // spb tempo ceiling and event-array sizing (4.1, 4.3)

}  // namespace PatternFreezeConstants

}  // namespace Krate::DSP
