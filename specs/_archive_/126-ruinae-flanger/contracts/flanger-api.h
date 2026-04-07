// ==============================================================================
// API Contract: Flanger DSP Processor
// ==============================================================================
// This file defines the PUBLIC interface contract for the Flanger class.
// Implementation must match these signatures exactly.
// Location: dsp/include/krate/dsp/processors/flanger.h
// Namespace: Krate::DSP
// Layer: 2 (Processors)
// Dependencies: Layer 0 (db_utils, note_value), Layer 1 (DelayLine, LFO, OnePoleSmoother)
// ==============================================================================

#pragma once

// Required includes (Layer 0 + Layer 1 only)
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

namespace Krate {
namespace DSP {

class Flanger {
public:
    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr float kMinRate = 0.05f;           // Hz
    static constexpr float kMaxRate = 5.0f;            // Hz
    static constexpr float kDefaultRate = 0.5f;        // Hz

    static constexpr float kMinDelayMs = 0.3f;         // Sweep floor (ms)
    static constexpr float kMaxDelayMs = 4.0f;         // Sweep ceiling (ms)
    static constexpr float kMaxDelayBufferMs = 10.0f;  // Buffer allocation (ms)

    static constexpr float kMinDepth = 0.0f;
    static constexpr float kMaxDepth = 1.0f;
    static constexpr float kDefaultDepth = 0.5f;

    static constexpr float kMinFeedback = -1.0f;
    static constexpr float kMaxFeedback = 1.0f;
    static constexpr float kDefaultFeedback = 0.0f;
    static constexpr float kFeedbackClamp = 0.98f;     // Internal safety clamp

    static constexpr float kMinMix = 0.0f;
    static constexpr float kMaxMix = 1.0f;
    static constexpr float kDefaultMix = 0.5f;

    static constexpr float kMinStereoSpread = 0.0f;    // Degrees
    static constexpr float kMaxStereoSpread = 360.0f;  // Degrees
    static constexpr float kDefaultStereoSpread = 90.0f;

    static constexpr float kSmoothingTimeMs = 5.0f;    // Parameter smoothing

    // =========================================================================
    // Lifecycle
    // =========================================================================

    Flanger() noexcept = default;

    /// @brief Prepare for processing at the given sample rate.
    /// Allocates delay line buffers and configures all internal components.
    /// Must be called before processStereo().
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all internal state (delay lines, LFOs, smoothers, feedback).
    /// Call when starting a new audio stream or after a discontinuity.
    void reset() noexcept;

    /// @brief Check if prepare() has been called.
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set LFO rate. Range: [0.05, 5.0] Hz.
    void setRate(float rateHz) noexcept;
    [[nodiscard]] float getRate() const noexcept;

    /// @brief Set sweep depth. Range: [0.0, 1.0].
    /// Maps to delay sweep from kMinDelayMs to kMaxDelayMs.
    void setDepth(float depth) noexcept;
    [[nodiscard]] float getDepth() const noexcept;

    /// @brief Set feedback amount. Range: [-1.0, +1.0].
    /// Positive = resonant jet-engine sweep; negative = metallic/hollow.
    /// Internally clamped to +/-0.98 for stability.
    void setFeedback(float feedback) noexcept;
    [[nodiscard]] float getFeedback() const noexcept;

    /// @brief Set dry/wet mix. Range: [0.0, 1.0].
    /// Uses true crossfade: output = (1-mix)*dry + mix*wet.
    void setMix(float mix) noexcept;
    [[nodiscard]] float getMix() const noexcept;

    /// @brief Set stereo LFO phase offset. Range: [0, 360] degrees.
    void setStereoSpread(float degrees) noexcept;
    [[nodiscard]] float getStereoSpread() const noexcept;

    /// @brief Set LFO waveform (Sine or Triangle).
    void setWaveform(LFOWaveform wf) noexcept;
    [[nodiscard]] LFOWaveform getWaveform() const noexcept;

    // =========================================================================
    // Tempo Sync
    // =========================================================================

    /// @brief Enable/disable tempo sync for LFO rate.
    void setTempoSync(bool enabled) noexcept;
    [[nodiscard]] bool isTempoSyncEnabled() const noexcept;

    /// @brief Set note value for tempo sync.
    void setNoteValue(NoteValue nv, NoteModifier nm = NoteModifier::Plain) noexcept;

    /// @brief Set host tempo in BPM.
    void setTempo(double bpm) noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a stereo block of audio in-place.
    /// @param left  Pointer to left channel samples (modified in-place).
    /// @param right Pointer to right channel samples (modified in-place).
    /// @param numSamples Number of samples to process.
    ///
    /// Processing topology:
    ///   1. For each sample: smooth parameters, advance LFO
    ///   2. Calculate delay time from LFO + depth
    ///   3. Write input + tanh(feedback * feedbackState) to delay line
    ///   4. Read from delay line at modulated position (linear interpolation)
    ///   5. Apply true crossfade mix: output = (1-mix)*dry + mix*wet
    ///   6. Store wet signal as feedback state (flushed for denormals)
    void processStereo(float* left, float* right, size_t numSamples) noexcept;
};

} // namespace DSP
} // namespace Krate
