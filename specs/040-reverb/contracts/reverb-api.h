// ==============================================================================
// API Contract: Dattorro Plate Reverb
// ==============================================================================
// This file defines the public API contract for the Reverb class.
// Implementation details are omitted -- this is the interface specification only.
//
// File: dsp/include/krate/dsp/effects/reverb.h
// Namespace: Krate::DSP
// Layer: 4 (Effects)
// ==============================================================================

#pragma once

namespace Krate {
namespace DSP {

/// @brief Parameter structure for the Dattorro plate reverb.
///
/// All parameters have well-defined ranges and defaults. Pass to
/// Reverb::setParams() to update all parameters atomically.
struct ReverbParams {
    float roomSize = 0.5f;      ///< Decay control [0.0, 1.0]
    float damping = 0.5f;       ///< HF absorption [0.0, 1.0]
    float width = 1.0f;         ///< Stereo decorrelation [0.0, 1.0]
    float mix = 0.3f;           ///< Dry/wet blend [0.0, 1.0]
    float preDelayMs = 0.0f;    ///< Pre-delay in ms [0.0, 100.0]
    float diffusion = 0.7f;     ///< Input diffusion amount [0.0, 1.0]
    bool freeze = false;        ///< Infinite sustain mode
    float modRate = 0.5f;       ///< Tank LFO rate in Hz [0.0, 2.0]
    float modDepth = 0.0f;      ///< Tank LFO depth [0.0, 1.0]
};

/// @brief Dattorro plate reverb effect (Layer 4).
///
/// Implements the Dattorro plate reverb algorithm with:
/// - Input bandwidth filter + 4-stage input diffusion
/// - Pre-delay (0-100ms)
/// - Figure-eight tank topology with cross-coupled decay loops
/// - LFO-modulated allpass diffusion in tank
/// - Freeze mode for infinite sustain
/// - Multi-tap stereo output with mid-side width control
///
/// @par Usage
/// @code
/// Reverb reverb;
/// reverb.prepare(44100.0);
///
/// ReverbParams params;
/// params.roomSize = 0.7f;
/// params.mix = 0.4f;
/// reverb.setParams(params);
///
/// // In audio callback:
/// reverb.processBlock(leftBuffer, rightBuffer, numSamples);
/// @endcode
class Reverb {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Creates unprepared instance.
    Reverb() noexcept = default;

    /// @brief Prepare the reverb for processing.
    ///
    /// Allocates all internal delay lines, initializes filters and LFO.
    /// Must be called before process()/processBlock().
    ///
    /// @param sampleRate Sample rate in Hz [8000, 192000]
    /// @post Instance is prepared and ready for processing.
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all internal state to silence.
    ///
    /// Clears delay lines, filter states, LFO phase, and tank feedback.
    /// Does not deallocate memory. After reset, the instance is still
    /// prepared and ready for immediate processing.
    void reset() noexcept;

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Update all reverb parameters.
    ///
    /// Parameters are applied with smoothing (no clicks/pops).
    /// Thread-safe: can be called from any thread (UI, automation).
    ///
    /// @param params Parameter struct with all values
    void setParams(const ReverbParams& params) noexcept;

    // =========================================================================
    // Processing (real-time safe)
    // =========================================================================

    /// @brief Process a single stereo sample pair in-place.
    ///
    /// @param[in,out] left  Left channel sample (modified in-place)
    /// @param[in,out] right Right channel sample (modified in-place)
    ///
    /// @note noexcept, allocation-free, real-time safe
    /// @pre prepare() has been called
    void process(float& left, float& right) noexcept;

    /// @brief Process a block of stereo samples in-place.
    ///
    /// @param[in,out] left      Left channel buffer (modified in-place)
    /// @param[in,out] right     Right channel buffer (modified in-place)
    /// @param         numSamples Number of samples in each buffer
    ///
    /// @note noexcept, allocation-free, real-time safe
    /// @pre prepare() has been called
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Check if the reverb has been prepared.
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;
};

} // namespace DSP
} // namespace Krate
