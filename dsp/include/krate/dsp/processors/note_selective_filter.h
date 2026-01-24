// ==============================================================================
// Layer 2: DSP Processor - Note-Selective Filter
// ==============================================================================
// Applies filtering only to audio matching specific note classes (C, C#, D, etc.),
// passing non-matching notes through dry. Uses pitch detection to identify the
// current note, then crossfades between dry and filtered signal.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, atomics)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (filter always hot, denormal prevention)
// - Principle XIII: Test-First Development
//
// Reference: specs/093-note-selective-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/primitives/pitch_detector.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>

#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// NoDetectionMode Enumeration (FR-022 through FR-025)
// =============================================================================

/// @brief Behavior when pitch detection fails or confidence is below threshold.
///
/// Determines how the filter responds when no valid pitch can be detected,
/// such as during silence, noise, or unpitched transients.
enum class NoDetectionMode : uint8_t {
    Dry = 0,       ///< Pass dry signal when no pitch detected (default)
    Filtered = 1,  ///< Apply filter regardless of detection
    LastState = 2  ///< Maintain previous filtering state
};

// =============================================================================
// NoteSelectiveFilter Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Note-selective dynamic filter
///
/// A filter that processes only notes matching a configurable note class set
/// (C, C#, D, etc.), passing non-matching notes through dry. Uses pitch
/// detection to determine the current note, then crossfades between dry and
/// filtered signal based on whether the detected note matches the target set.
///
/// @par Key Features
/// - Note class selection via bitset (12 notes: C through B)
/// - Configurable tolerance for pitch matching (1-49 cents, default 49)
/// - Smooth crossfade transitions (0.5-50ms, default 5ms)
/// - Continuous filter processing (always hot) for click-free transitions
/// - Block-rate note matching updates for stability (~512 samples)
/// - Thread-safe parameter setters via atomics
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated)
/// - Principle III: Modern C++ (C++20, atomics)
/// - Principle IX: Layer 2 (composes PitchDetector, SVF, OnePoleSmoother)
///
/// @par Usage Example
/// @code
/// NoteSelectiveFilter filter;
/// filter.prepare(48000.0, 512);
///
/// // Enable filtering for C and G notes
/// std::bitset<12> notes;
/// notes.set(0);  // C
/// notes.set(7);  // G
/// filter.setTargetNotes(notes);
///
/// filter.setCutoff(500.0f);
/// filter.setResonance(4.0f);
///
/// // In process callback
/// for (auto& sample : buffer) {
///     sample = filter.process(sample);
/// }
/// @endcode
class NoteSelectiveFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief Default confidence threshold for pitch detection (FR-020)
    static constexpr float kDefaultConfidenceThreshold = 0.3f;

    /// @brief Default note tolerance in cents (FR-009)
    static constexpr float kDefaultNoteTolerance = 49.0f;

    /// @brief Minimum note tolerance in cents
    static constexpr float kMinNoteTolerance = 1.0f;

    /// @brief Maximum note tolerance in cents (prevents overlapping zones)
    static constexpr float kMaxNoteTolerance = 49.0f;

    /// @brief Default crossfade time in milliseconds (FR-012)
    static constexpr float kDefaultCrossfadeTimeMs = 5.0f;

    /// @brief Minimum crossfade time in milliseconds (FR-013)
    static constexpr float kMinCrossfadeTimeMs = 0.5f;

    /// @brief Maximum crossfade time in milliseconds (FR-013)
    static constexpr float kMaxCrossfadeTimeMs = 50.0f;

    /// @brief Default filter cutoff in Hz
    static constexpr float kDefaultCutoffHz = 1000.0f;

    /// @brief Minimum filter cutoff in Hz (FR-015)
    static constexpr float kMinCutoffHz = 20.0f;

    /// @brief Default filter resonance (Butterworth Q)
    static constexpr float kDefaultResonance = 0.7071067811865476f;

    /// @brief Minimum filter resonance (FR-016)
    static constexpr float kMinResonance = 0.1f;

    /// @brief Maximum filter resonance (FR-016)
    static constexpr float kMaxResonance = 30.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor
    NoteSelectiveFilter() noexcept = default;

    /// @brief Destructor
    ~NoteSelectiveFilter() = default;

    // Non-copyable (contains filter state and atomics)
    NoteSelectiveFilter(const NoteSelectiveFilter&) = delete;
    NoteSelectiveFilter& operator=(const NoteSelectiveFilter&) = delete;

    // Movable
    NoteSelectiveFilter(NoteSelectiveFilter&&) noexcept = default;
    NoteSelectiveFilter& operator=(NoteSelectiveFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate (FR-001, FR-003)
    /// @param sampleRate Audio sample rate in Hz (clamped to >= 1000)
    /// @param maxBlockSize Block size for note matching updates (default 512)
    /// @note Call before any processing; call again if sample rate changes
    void prepare(double sampleRate, int maxBlockSize = 512) noexcept;

    /// @brief Reset internal state without changing parameters (FR-002)
    /// @note Clears pitch detector, filter, crossfade, and note matching state
    void reset() noexcept;

    // =========================================================================
    // Note Selection (FR-004 through FR-008)
    // =========================================================================

    /// @brief Set which note classes to filter (FR-004)
    /// @param notes Bitset where bit 0=C, 1=C#, 2=D, ..., 11=B
    /// @note Thread-safe (atomic write)
    void setTargetNotes(std::bitset<12> notes) noexcept;

    /// @brief Enable or disable filtering for a single note class (FR-005)
    /// @param noteClass Note class 0-11 (0=C, 1=C#, ..., 11=B)
    /// @param enabled true to enable filtering, false to disable
    /// @note Thread-safe (atomic read-modify-write)
    void setTargetNote(int noteClass, bool enabled) noexcept;

    /// @brief Disable filtering for all note classes (FR-006)
    /// @note Thread-safe (atomic write)
    void clearAllNotes() noexcept;

    /// @brief Enable filtering for all note classes (FR-007)
    /// @note Thread-safe (atomic write)
    void setAllNotes() noexcept;

    // =========================================================================
    // Pitch Matching (FR-009 through FR-011)
    // =========================================================================

    /// @brief Set note tolerance for pitch matching (FR-009, FR-010)
    /// @param cents Tolerance in cents, clamped to [1, 49]
    /// @note 49 cents max prevents overlapping tolerance zones
    /// @note Thread-safe (atomic write)
    void setNoteTolerance(float cents) noexcept;

    // =========================================================================
    // Crossfade Control (FR-012 through FR-014)
    // =========================================================================

    /// @brief Set crossfade transition time (FR-012, FR-013)
    /// @param ms Time in milliseconds, clamped to [0.5, 50]
    /// @note Time represents 99% settling (5 time constants)
    /// @note Thread-safe
    void setCrossfadeTime(float ms) noexcept;

    // =========================================================================
    // Filter Configuration (FR-015 through FR-018)
    // =========================================================================

    /// @brief Set filter cutoff frequency (FR-015)
    /// @param hz Cutoff in Hz, clamped to [20, sampleRate * 0.45]
    /// @note Thread-safe (atomic write)
    void setCutoff(float hz) noexcept;

    /// @brief Set filter resonance/Q (FR-016)
    /// @param q Q factor, clamped to [0.1, 30]
    /// @note 0.7071 = Butterworth (flat), higher = more resonant
    /// @note Thread-safe (atomic write)
    void setResonance(float q) noexcept;

    /// @brief Set filter type (FR-017)
    /// @param type SVFMode (Lowpass, Highpass, Bandpass, etc.)
    /// @note Thread-safe (atomic write)
    void setFilterType(SVFMode type) noexcept;

    // =========================================================================
    // Pitch Detection Configuration (FR-019 through FR-021)
    // =========================================================================

    /// @brief Set pitch detection frequency range (FR-019)
    /// @param minHz Minimum frequency (clamped to [50, maxHz])
    /// @param maxHz Maximum frequency (clamped to [minHz, 1000])
    /// @note Thread-safe
    void setDetectionRange(float minHz, float maxHz) noexcept;

    /// @brief Set confidence threshold for pitch validity (FR-020)
    /// @param threshold Value from 0.0 (accept all) to 1.0 (very strict)
    /// @note Default: 0.3 - balanced between sensitivity and stability
    /// @note Thread-safe (atomic write)
    void setConfidenceThreshold(float threshold) noexcept;

    // =========================================================================
    // No-Detection Behavior (FR-022 through FR-025)
    // =========================================================================

    /// @brief Set behavior when no valid pitch is detected (FR-022)
    /// @param mode NoDetectionMode::Dry, Filtered, or LastState
    /// @note Thread-safe (atomic write)
    void setNoDetectionBehavior(NoDetectionMode mode) noexcept;

    // =========================================================================
    // Processing (FR-026 through FR-035)
    // =========================================================================

    /// @brief Process a single sample (FR-026)
    /// @param input Input audio sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Returns input unchanged if not prepared
    /// @note Returns 0 and resets state on NaN/Inf input
    /// @note Real-time safe: noexcept, no allocations (FR-033)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place (FR-027)
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, no allocations (FR-033)
    void processBlock(float* buffer, int numSamples) noexcept;

    // =========================================================================
    // State Query (FR-031, FR-032)
    // =========================================================================

    /// @brief Get last detected note class (FR-031)
    /// @return Note class 0-11, or -1 if no valid pitch detected
    /// @note Thread-safe read
    [[nodiscard]] int getDetectedNoteClass() const noexcept;

    /// @brief Check if filter is currently being applied (FR-032)
    /// @return true if crossfade > 0.5 (more filtered than dry)
    /// @note Thread-safe read
    [[nodiscard]] bool isCurrentlyFiltering() const noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] std::bitset<12> getTargetNotes() const noexcept;
    [[nodiscard]] float getNoteTolerance() const noexcept;
    [[nodiscard]] float getCrossfadeTime() const noexcept;
    [[nodiscard]] float getCutoff() const noexcept;
    [[nodiscard]] float getResonance() const noexcept;
    [[nodiscard]] SVFMode getFilterType() const noexcept;
    [[nodiscard]] float getConfidenceThreshold() const noexcept;
    [[nodiscard]] NoDetectionMode getNoDetectionBehavior() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Update note matching state (called at block rate)
    void updateNoteMatching() noexcept;

    /// @brief Clamp cutoff frequency to valid range
    [[nodiscard]] float clampCutoff(float hz) const noexcept;

    // =========================================================================
    // Composed Components
    // =========================================================================

    PitchDetector pitchDetector_;       ///< Autocorrelation pitch detector
    SVF filter_;                         ///< Main audio filter (TPT SVF)
    OnePoleSmoother crossfadeSmoother_;  ///< Smoothing for dry/wet transitions

    // =========================================================================
    // Atomic Configuration (Thread-Safe UI Updates - FR-035)
    // =========================================================================

    std::atomic<uint16_t> targetNotes_{0};  ///< Bitset as uint16 for atomicity
    std::atomic<float> noteTolerance_{kDefaultNoteTolerance};
    std::atomic<float> crossfadeTimeMs_{kDefaultCrossfadeTimeMs};
    std::atomic<float> cutoffHz_{kDefaultCutoffHz};
    std::atomic<float> resonance_{kDefaultResonance};
    std::atomic<int> filterType_{static_cast<int>(SVFMode::Lowpass)};
    std::atomic<float> confidenceThreshold_{kDefaultConfidenceThreshold};
    std::atomic<int> noDetectionMode_{static_cast<int>(NoDetectionMode::Dry)};
    std::atomic<float> minHz_{PitchDetector::kMinFrequency};
    std::atomic<float> maxHz_{PitchDetector::kMaxFrequency};

    // =========================================================================
    // Non-Atomic State (Audio Thread Only)
    // =========================================================================

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    int lastDetectedNote_ = -1;          ///< Last valid note class
    bool lastFilteringState_ = false;    ///< For LastState mode
    float currentCrossfade_ = 0.0f;      ///< Current crossfade value (for query)
    std::size_t samplesSinceNoteUpdate_ = 0;
    std::size_t blockUpdateInterval_ = 512;  ///< Samples between note updates
};

// =============================================================================
// Inline Implementation - Lifecycle
// =============================================================================

inline void NoteSelectiveFilter::prepare(double sampleRate, int maxBlockSize) noexcept {
    sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;

    // Configure pitch detector
    pitchDetector_.prepare(sampleRate_, PitchDetector::kDefaultWindowSize);

    // Configure filter
    filter_.prepare(sampleRate_);
    filter_.setMode(static_cast<SVFMode>(filterType_.load(std::memory_order_relaxed)));
    filter_.setCutoff(clampCutoff(cutoffHz_.load(std::memory_order_relaxed)));
    filter_.setResonance(resonance_.load(std::memory_order_relaxed));

    // Configure crossfade smoother
    crossfadeSmoother_.configure(
        crossfadeTimeMs_.load(std::memory_order_relaxed),
        static_cast<float>(sampleRate_));
    crossfadeSmoother_.snapTo(0.0f);

    // Set block update interval
    blockUpdateInterval_ = static_cast<std::size_t>(maxBlockSize > 0 ? maxBlockSize : 512);
    samplesSinceNoteUpdate_ = 0;

    // Reset state
    lastDetectedNote_ = -1;
    lastFilteringState_ = false;
    currentCrossfade_ = 0.0f;

    prepared_ = true;
}

inline void NoteSelectiveFilter::reset() noexcept {
    pitchDetector_.reset();
    filter_.reset();
    crossfadeSmoother_.reset();
    crossfadeSmoother_.snapTo(0.0f);

    lastDetectedNote_ = -1;
    lastFilteringState_ = false;
    currentCrossfade_ = 0.0f;
    samplesSinceNoteUpdate_ = 0;
}

// =============================================================================
// Inline Implementation - Note Selection
// =============================================================================

inline void NoteSelectiveFilter::setTargetNotes(std::bitset<12> notes) noexcept {
    targetNotes_.store(static_cast<uint16_t>(notes.to_ulong()),
                       std::memory_order_relaxed);
}

inline void NoteSelectiveFilter::setTargetNote(int noteClass, bool enabled) noexcept {
    if (noteClass < 0 || noteClass > 11) return;

    uint16_t current = targetNotes_.load(std::memory_order_relaxed);
    uint16_t mask = static_cast<uint16_t>(1u << noteClass);

    if (enabled) {
        targetNotes_.store(current | mask, std::memory_order_relaxed);
    } else {
        targetNotes_.store(current & ~mask, std::memory_order_relaxed);
    }
}

inline void NoteSelectiveFilter::clearAllNotes() noexcept {
    targetNotes_.store(0, std::memory_order_relaxed);
}

inline void NoteSelectiveFilter::setAllNotes() noexcept {
    targetNotes_.store(0x0FFF, std::memory_order_relaxed);  // Bits 0-11 set
}

// =============================================================================
// Inline Implementation - Pitch Matching
// =============================================================================

inline void NoteSelectiveFilter::setNoteTolerance(float cents) noexcept {
    noteTolerance_.store(std::clamp(cents, kMinNoteTolerance, kMaxNoteTolerance),
                         std::memory_order_relaxed);
}

// =============================================================================
// Inline Implementation - Crossfade Control
// =============================================================================

inline void NoteSelectiveFilter::setCrossfadeTime(float ms) noexcept {
    float clamped = std::clamp(ms, kMinCrossfadeTimeMs, kMaxCrossfadeTimeMs);
    crossfadeTimeMs_.store(clamped, std::memory_order_relaxed);
    if (prepared_) {
        crossfadeSmoother_.configure(clamped, static_cast<float>(sampleRate_));
    }
}

// =============================================================================
// Inline Implementation - Filter Configuration
// =============================================================================

inline void NoteSelectiveFilter::setCutoff(float hz) noexcept {
    float clamped = clampCutoff(hz);
    cutoffHz_.store(clamped, std::memory_order_relaxed);
    if (prepared_) {
        filter_.setCutoff(clamped);
    }
}

inline void NoteSelectiveFilter::setResonance(float q) noexcept {
    float clamped = std::clamp(q, kMinResonance, kMaxResonance);
    resonance_.store(clamped, std::memory_order_relaxed);
    if (prepared_) {
        filter_.setResonance(clamped);
    }
}

inline void NoteSelectiveFilter::setFilterType(SVFMode type) noexcept {
    filterType_.store(static_cast<int>(type), std::memory_order_relaxed);
    if (prepared_) {
        filter_.setMode(type);
    }
}

// =============================================================================
// Inline Implementation - Pitch Detection Configuration
// =============================================================================

inline void NoteSelectiveFilter::setDetectionRange(float minHz, float maxHz) noexcept {
    float clampedMin = std::clamp(minHz, PitchDetector::kMinFrequency,
                                  PitchDetector::kMaxFrequency);
    float clampedMax = std::clamp(maxHz, clampedMin, PitchDetector::kMaxFrequency);
    minHz_.store(clampedMin, std::memory_order_relaxed);
    maxHz_.store(clampedMax, std::memory_order_relaxed);
}

inline void NoteSelectiveFilter::setConfidenceThreshold(float threshold) noexcept {
    confidenceThreshold_.store(std::clamp(threshold, 0.0f, 1.0f),
                               std::memory_order_relaxed);
}

// =============================================================================
// Inline Implementation - No-Detection Behavior
// =============================================================================

inline void NoteSelectiveFilter::setNoDetectionBehavior(NoDetectionMode mode) noexcept {
    noDetectionMode_.store(static_cast<int>(mode), std::memory_order_relaxed);
}

// =============================================================================
// Inline Implementation - Processing
// =============================================================================

inline float NoteSelectiveFilter::process(float input) noexcept {
    if (!prepared_) return input;

    // Handle NaN/Inf (FR-033 edge case)
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // Push to pitch detector
    pitchDetector_.push(input);
    ++samplesSinceNoteUpdate_;

    // Block-rate note matching update (FR-030)
    if (samplesSinceNoteUpdate_ >= blockUpdateInterval_) {
        updateNoteMatching();
        samplesSinceNoteUpdate_ = 0;
    }

    // Always process through filter - keeps state hot (FR-029)
    float filtered = filter_.process(input);

    // Apply crossfade (FR-028)
    float crossfade = crossfadeSmoother_.process();
    currentCrossfade_ = crossfade;

    float output = (1.0f - crossfade) * input + crossfade * filtered;

    // Flush denormals (FR-034)
    return detail::flushDenormal(output);
}

inline void NoteSelectiveFilter::processBlock(float* buffer, int numSamples) noexcept {
    if (buffer == nullptr || numSamples <= 0) return;

    for (int i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

inline void NoteSelectiveFilter::updateNoteMatching() noexcept {
    float frequency = pitchDetector_.getDetectedFrequency();
    float confidence = pitchDetector_.getConfidence();
    float threshold = confidenceThreshold_.load(std::memory_order_relaxed);

    // Check detection range
    float minF = minHz_.load(std::memory_order_relaxed);
    float maxF = maxHz_.load(std::memory_order_relaxed);
    bool inRange = (frequency >= minF && frequency <= maxF);

    float crossfadeTarget = 0.0f;  // Default: dry

    if (confidence >= threshold && inRange) {
        // Valid pitch detected (FR-011, FR-036)
        // frequencyToNoteClass: maps frequency to note class 0-11
        // frequencyToCentsDeviation: returns cents deviation from nearest note center (0-50 range)
        int noteClass = frequencyToNoteClass(frequency);
        float centsDeviation = std::abs(frequencyToCentsDeviation(frequency));
        float tolerance = noteTolerance_.load(std::memory_order_relaxed);

        // Check if note is in target set and within tolerance
        std::bitset<12> targets(targetNotes_.load(std::memory_order_relaxed));
        bool noteMatches = noteClass >= 0 && noteClass < 12 &&
                          targets.test(static_cast<std::size_t>(noteClass)) &&
                          centsDeviation <= tolerance;

        crossfadeTarget = noteMatches ? 1.0f : 0.0f;
        lastDetectedNote_ = noteClass;
        lastFilteringState_ = noteMatches;
    } else {
        // No valid pitch - apply NoDetectionMode (FR-022-025)
        auto mode = static_cast<NoDetectionMode>(
            noDetectionMode_.load(std::memory_order_relaxed));

        switch (mode) {
            case NoDetectionMode::Dry:
                crossfadeTarget = 0.0f;
                break;
            case NoDetectionMode::Filtered:
                crossfadeTarget = 1.0f;
                break;
            case NoDetectionMode::LastState:
                crossfadeTarget = lastFilteringState_ ? 1.0f : 0.0f;
                break;
        }
        lastDetectedNote_ = -1;
    }

    crossfadeSmoother_.setTarget(crossfadeTarget);
}

// =============================================================================
// Inline Implementation - State Query
// =============================================================================

inline int NoteSelectiveFilter::getDetectedNoteClass() const noexcept {
    return lastDetectedNote_;
}

inline bool NoteSelectiveFilter::isCurrentlyFiltering() const noexcept {
    return currentCrossfade_ > 0.5f;
}

// =============================================================================
// Inline Implementation - Parameter Getters
// =============================================================================

inline std::bitset<12> NoteSelectiveFilter::getTargetNotes() const noexcept {
    return std::bitset<12>(targetNotes_.load(std::memory_order_relaxed));
}

inline float NoteSelectiveFilter::getNoteTolerance() const noexcept {
    return noteTolerance_.load(std::memory_order_relaxed);
}

inline float NoteSelectiveFilter::getCrossfadeTime() const noexcept {
    return crossfadeTimeMs_.load(std::memory_order_relaxed);
}

inline float NoteSelectiveFilter::getCutoff() const noexcept {
    return cutoffHz_.load(std::memory_order_relaxed);
}

inline float NoteSelectiveFilter::getResonance() const noexcept {
    return resonance_.load(std::memory_order_relaxed);
}

inline SVFMode NoteSelectiveFilter::getFilterType() const noexcept {
    return static_cast<SVFMode>(filterType_.load(std::memory_order_relaxed));
}

inline float NoteSelectiveFilter::getConfidenceThreshold() const noexcept {
    return confidenceThreshold_.load(std::memory_order_relaxed);
}

inline NoDetectionMode NoteSelectiveFilter::getNoDetectionBehavior() const noexcept {
    return static_cast<NoDetectionMode>(noDetectionMode_.load(std::memory_order_relaxed));
}

inline bool NoteSelectiveFilter::isPrepared() const noexcept {
    return prepared_;
}

// =============================================================================
// Inline Implementation - Internal Helpers
// =============================================================================

inline float NoteSelectiveFilter::clampCutoff(float hz) const noexcept {
    float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
    return std::clamp(hz, kMinCutoffHz, maxCutoff);
}

} // namespace DSP
} // namespace Krate
