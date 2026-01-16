// ==============================================================================
// Pattern Freeze Mode - API Contract
// ==============================================================================
// This file defines the public API contract for PatternFreezeMode.
// Implementation must satisfy all method signatures and guarantees.
//
// Feature Branch: 069-pattern-freeze
// Created: 2026-01-16
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/note_value.h>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// Pattern algorithm type
/// @note Must match UI dropdown order for correct parameter mapping
enum class PatternType : uint8_t {
    Euclidean = 0,      ///< FR-013: Bjorklund algorithm rhythm patterns
    GranularScatter,    ///< FR-013: Random/semi-random grain triggering
    HarmonicDrones,     ///< FR-013: Sustained multi-voice playback
    NoiseBursts         ///< FR-013: Rhythmic filtered noise generation
};

/// Slice length behavior
enum class SliceMode : uint8_t {
    Fixed = 0,    ///< FR-010: All slices use configured slice length
    Variable      ///< FR-011: Slice length varies with pattern
};

/// Musical intervals for Harmonic Drones
/// @note Semitone values: Unison=0, MinorThird=3, MajorThird=4,
///       Fourth=5, Fifth=7, Octave=12
enum class PitchInterval : uint8_t {
    Unison = 0,     ///< FR-044
    MinorThird,     ///< FR-044
    MajorThird,     ///< FR-044
    Fourth,         ///< FR-044
    Fifth,          ///< FR-044
    Octave          ///< FR-044, FR-045 (default)
};

/// Noise spectrum types
enum class NoiseColor : uint8_t {
    White = 0,    ///< FR-052: Flat spectrum
    Pink,         ///< FR-052, FR-053: 1/f spectrum (default)
    Brown         ///< FR-052: 1/f^2 spectrum
};

/// Envelope curve types
enum class EnvelopeShape : uint8_t {
    Linear = 0,      ///< FR-070, FR-071: Triangle/trapezoid (default)
    Exponential      ///< FR-070: RC-style curves
};

/// Filter types for Noise Bursts
enum class FilterType : uint8_t {
    Lowpass = 0,    ///< FR-057 (default)
    Highpass,
    Bandpass
};

// =============================================================================
// PatternFreezeMode API Contract
// =============================================================================

/// @interface IPatternFreezeMode
/// @brief API contract for Pattern Freeze Mode
///
/// This interface defines all public methods that PatternFreezeMode must implement.
/// All methods are noexcept for real-time safety (FR-085).
class IPatternFreezeMode {
public:
    virtual ~IPatternFreezeMode() = default;

    // =========================================================================
    // Lifecycle (FR-076 to FR-080)
    // =========================================================================

    /// @brief Configure for processing
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxDelayMs Maximum delay time in milliseconds
    /// @pre sampleRate > 0, maxBlockSize > 0
    /// @post All buffers allocated, ready for process()
    /// @note FR-076, FR-077: Pre-allocates all memory
    virtual void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept = 0;

    /// @brief Clear all state without deallocation
    /// @post Capture buffer cleared, slices released, pattern state reset
    /// @note FR-078, FR-079: Real-time safe reset
    virtual void reset() noexcept = 0;

    /// @brief Snap all smoothers to current targets
    /// @post All parameter transitions complete immediately
    /// @note FR-080: For preset loading
    virtual void snapParameters() noexcept = 0;

    // =========================================================================
    // Freeze Control
    // =========================================================================

    /// @brief Enable or disable freeze mode
    /// @param enabled true to engage freeze, false to return to normal delay
    /// @post When enabled: pattern triggers from capture buffer
    /// @post When disabled: normal delay processing resumes
    virtual void setFreezeEnabled(bool enabled) noexcept = 0;

    /// @brief Check if freeze is currently enabled
    /// @return true if freeze is engaged
    [[nodiscard]] virtual bool isFreezeEnabled() const noexcept = 0;

    // =========================================================================
    // Pattern Type (FR-012 to FR-015)
    // =========================================================================

    /// @brief Set the active pattern algorithm
    /// @param type Pattern type to use
    /// @post If frozen: crossfade transition begins (~500ms)
    /// @post If not frozen: takes effect on next freeze enable
    /// @note FR-012, FR-015, FR-015a, FR-015b
    virtual void setPatternType(PatternType type) noexcept = 0;

    /// @brief Get the current pattern type
    /// @return Currently active pattern type
    [[nodiscard]] virtual PatternType getPatternType() const noexcept = 0;

    // =========================================================================
    // Slice Parameters (FR-006 to FR-011)
    // =========================================================================

    /// @brief Set slice duration
    /// @param ms Slice length in milliseconds [10, 2000]
    /// @note FR-006, FR-007: Clamped to valid range
    virtual void setSliceLength(float ms) noexcept = 0;

    /// @brief Set slice length mode
    /// @param mode Fixed or Variable
    /// @note FR-009, FR-010, FR-011
    virtual void setSliceMode(SliceMode mode) noexcept = 0;

    // =========================================================================
    // Euclidean Parameters (FR-016 to FR-027)
    // =========================================================================

    /// @brief Set total steps in Euclidean pattern
    /// @param steps Number of steps [2, 32]
    /// @note FR-016, FR-017: Clamped to valid range
    virtual void setEuclideanSteps(int steps) noexcept = 0;

    /// @brief Set number of hits in Euclidean pattern
    /// @param hits Number of triggers [1, steps]
    /// @note FR-019, FR-020: Clamped to [1, current steps]
    virtual void setEuclideanHits(int hits) noexcept = 0;

    /// @brief Set pattern rotation offset
    /// @param rotation Offset [0, steps-1]
    /// @note FR-022, FR-023: Wrapped to valid range
    virtual void setEuclideanRotation(int rotation) noexcept = 0;

    /// @brief Set tempo-synced pattern rate
    /// @param note Note value for step timing
    /// @param mod Optional modifier (dotted, triplet)
    /// @note FR-025: Used by Euclidean and Noise Bursts patterns
    virtual void setPatternRate(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept = 0;

    // =========================================================================
    // Granular Scatter Parameters (FR-028 to FR-039)
    // =========================================================================

    /// @brief Set grain trigger density
    /// @param hz Triggers per second [1, 50]
    /// @note FR-028, FR-029: Clamped to valid range
    virtual void setGranularDensity(float hz) noexcept = 0;

    /// @brief Set position randomization
    /// @param percent Jitter amount [0, 100]
    /// @note FR-031, FR-032: 100% allows any position in slice
    virtual void setGranularPositionJitter(float percent) noexcept = 0;

    /// @brief Set size randomization
    /// @param percent Jitter amount [0, 100]
    /// @note FR-034, FR-035: 100% = +/- 50% of base size
    virtual void setGranularSizeJitter(float percent) noexcept = 0;

    /// @brief Set base grain size
    /// @param ms Grain duration in milliseconds [10, 500]
    /// @note FR-037, FR-038: Clamped to valid range
    virtual void setGranularGrainSize(float ms) noexcept = 0;

    // =========================================================================
    // Harmonic Drones Parameters (FR-040 to FR-050)
    // =========================================================================

    /// @brief Set number of simultaneous voices
    /// @param count Voice count [1, 4]
    /// @note FR-040, FR-041: Clamped to valid range
    virtual void setDroneVoiceCount(int count) noexcept = 0;

    /// @brief Set pitch interval between voices
    /// @param interval Musical interval
    /// @note FR-043, FR-044
    virtual void setDroneInterval(PitchInterval interval) noexcept = 0;

    /// @brief Set drift modulation depth
    /// @param percent Modulation amount [0, 100]
    /// @note FR-046: Affects pitch and amplitude
    virtual void setDroneDrift(float percent) noexcept = 0;

    /// @brief Set drift LFO rate
    /// @param hz LFO frequency [0.1, 2.0]
    /// @note FR-048, FR-049: Clamped to valid range
    virtual void setDroneDriftRate(float hz) noexcept = 0;

    // =========================================================================
    // Noise Bursts Parameters (FR-051 to FR-062)
    // =========================================================================

    /// @brief Set noise spectrum type
    /// @param color White, Pink, or Brown
    /// @note FR-051, FR-052
    virtual void setNoiseColor(NoiseColor color) noexcept = 0;

    /// @brief Set burst rhythm rate
    /// @param note Note value for burst timing
    /// @param mod Optional modifier (dotted, triplet)
    /// @note FR-054: Tempo-synced rhythm
    virtual void setNoiseBurstRate(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept = 0;

    /// @brief Set noise filter mode
    /// @param type LP, HP, or BP
    /// @note FR-056
    virtual void setNoiseFilterType(FilterType type) noexcept = 0;

    /// @brief Set noise filter frequency
    /// @param hz Cutoff frequency [20, 20000]
    /// @note FR-058, FR-059: Clamped to valid range
    virtual void setNoiseFilterCutoff(float hz) noexcept = 0;

    /// @brief Set filter envelope modulation depth
    /// @param percent Sweep amount [0, 100]
    /// @note FR-061: Envelope-controlled filter movement
    virtual void setNoiseFilterSweep(float percent) noexcept = 0;

    // =========================================================================
    // Envelope Parameters (FR-063 to FR-072)
    // =========================================================================

    /// @brief Set slice attack time
    /// @param ms Attack duration [0, 500]
    /// @note FR-063, FR-064: Clamped to valid range
    virtual void setEnvelopeAttack(float ms) noexcept = 0;

    /// @brief Set slice release time
    /// @param ms Release duration [0, 2000]
    /// @note FR-066, FR-067: Clamped to valid range
    virtual void setEnvelopeRelease(float ms) noexcept = 0;

    /// @brief Set envelope curve type
    /// @param shape Linear or Exponential
    /// @note FR-069, FR-070
    virtual void setEnvelopeShape(EnvelopeShape shape) noexcept = 0;

    // =========================================================================
    // Processing Chain Parameters (FR-073, FR-074)
    // =========================================================================

    /// @brief Set pitch shift amount in semitones
    /// @param semitones Pitch shift [-24, +24]
    virtual void setPitchSemitones(float semitones) noexcept = 0;

    /// @brief Set pitch shift fine tune
    /// @param cents Fine adjustment [-100, +100]
    virtual void setPitchCents(float cents) noexcept = 0;

    /// @brief Set shimmer/pitch shift mix
    /// @param percent Shimmer amount [0, 100]
    virtual void setShimmerMix(float percent) noexcept = 0;

    /// @brief Set decay/feedback amount
    /// @param percent Decay amount [0, 100]
    virtual void setDecay(float percent) noexcept = 0;

    /// @brief Set diffusion amount
    /// @param percent Diffusion amount [0, 100]
    virtual void setDiffusionAmount(float percent) noexcept = 0;

    /// @brief Set diffusion size
    /// @param percent Size [0, 100]
    virtual void setDiffusionSize(float percent) noexcept = 0;

    /// @brief Enable or disable filter
    /// @param enabled Filter state
    virtual void setFilterEnabled(bool enabled) noexcept = 0;

    /// @brief Set filter type
    /// @param type LP, HP, or BP
    virtual void setFilterType(FilterType type) noexcept = 0;

    /// @brief Set filter cutoff frequency
    /// @param hz Cutoff frequency [20, 20000]
    virtual void setFilterCutoff(float hz) noexcept = 0;

    /// @brief Set dry/wet mix
    /// @param percent Wet amount [0, 100]
    virtual void setDryWetMix(float percent) noexcept = 0;

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Get capture buffer fill level
    /// @return Percentage [0, 100] of buffer filled
    /// @note FR-005: Reports how much audio is captured
    [[nodiscard]] virtual float getCaptureBufferFillLevel() const noexcept = 0;

    /// @brief Get processing latency
    /// @return Latency in samples
    /// @note SC-009: Should be < 3ms (128 samples @ 44.1kHz)
    [[nodiscard]] virtual size_t getLatencySamples() const noexcept = 0;

    // =========================================================================
    // Processing (FR-081 to FR-085)
    // =========================================================================

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (in/out)
    /// @param right Right channel buffer (in/out)
    /// @param numSamples Number of samples to process
    /// @param ctx Block context with tempo and timing info
    /// @pre prepare() has been called
    /// @pre left and right are valid buffers of at least numSamples
    /// @post Audio is processed according to current settings
    /// @note FR-081, FR-082, FR-083, FR-084, FR-085
    /// @note FR-082a: Tempo-synced patterns stop when tempo invalid
    /// @note FR-085: Real-time safe (no allocation, blocking, exceptions)
    virtual void process(float* left, float* right, size_t numSamples,
                         const BlockContext& ctx) noexcept = 0;
};

// =============================================================================
// Constants
// =============================================================================

/// Minimum slice length in milliseconds (FR-007)
inline constexpr float kPatternFreezeMinSliceLengthMs = 10.0f;

/// Maximum slice length in milliseconds (FR-007)
inline constexpr float kPatternFreezeMaxSliceLengthMs = 2000.0f;

/// Default slice length in milliseconds (FR-008)
inline constexpr float kPatternFreezeDefaultSliceLengthMs = 200.0f;

/// Crossfade duration for pattern type changes (FR-015)
inline constexpr float kPatternFreezeCrossfadeMs = 500.0f;

/// Maximum simultaneous slices/grains (FR-086)
inline constexpr size_t kPatternFreezeMaxSlices = 8;

/// Minimum capture buffer duration in seconds (FR-002)
inline constexpr float kPatternFreezeMinBufferSeconds = 5.0f;

} // namespace Krate::DSP
