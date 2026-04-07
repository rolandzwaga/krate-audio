// ==============================================================================
// API Contract: BodyResonance (Spec 131)
// ==============================================================================
// This file defines the public API contract for the BodyResonance processor.
// It is NOT compiled -- it serves as a design contract for implementation.
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>

#include <array>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Number of modal resonator modes per body preset.
static constexpr size_t kBodyModeCount = 8;

/// Number of reference body presets (small/medium/large).
static constexpr size_t kBodyPresetCount = 3;

/// Number of FDN delay lines.
static constexpr size_t kBodyFDNLines = 4;

// =============================================================================
// BodyMode - Single modal resonance specification
// =============================================================================

struct BodyMode {
    float freq;     ///< Center frequency in Hz
    float gain;     ///< Relative gain (negative for anti-phase coupling)
    float qWood;    ///< Q factor at material=0 (wood character)
    float qMetal;   ///< Q factor at material=1 (metal character)
};

// =============================================================================
// BodyResonance - Hybrid Modal Bank + FDN Body Coloring Processor
// =============================================================================

/// @brief Post-resonator body coloring processor (Layer 2).
///
/// Signal chain:
///   input -> coupling filter -> crossover LP -> modal bank --+
///                            -> crossover HP -> FDN ---------+--> sum
///   -> radiation HPF -> energy passivity (structural) -> dry/wet mix -> output
///
/// @par Thread Safety
/// setParams() must be called from the audio thread (no atomics needed for
/// per-voice processors). process()/processBlock() are NOT thread-safe.
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
///
/// @par Constitution Compliance
/// - Principle II: No allocations in audio path
/// - Principle IX: Layer 2 (depends on Layer 0/1 only)
/// - Principle X: Impulse-invariant biquad design, linear fractional delay
class BodyResonance {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    BodyResonance() noexcept = default;
    ~BodyResonance() noexcept = default;

    // Non-copyable (contains filter state arrays), movable
    BodyResonance(const BodyResonance&) = delete;
    BodyResonance& operator=(const BodyResonance&) = delete;
    BodyResonance(BodyResonance&&) noexcept = default;
    BodyResonance& operator=(BodyResonance&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Initialize for given sample rate. Must be called before processing.
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @note NOT real-time safe (configures smoothers)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all filter states and delay buffers.
    /// @note Real-time safe
    void reset() noexcept;

    /// @brief Check if prepare() has been called.
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set all body resonance parameters.
    /// @param size Body size (0.0=small/violin, 0.5=medium/guitar, 1.0=large/cello)
    /// @param material Material character (0.0=wood, 1.0=metal)
    /// @param mix Dry/wet blend (0.0=bypass, 1.0=fully colored)
    /// @note Call once per block before processBlock(), or per-sample before process()
    void setParams(float size, float material, float mix) noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Mono input sample
    /// @return Mono colored output sample
    /// @note Real-time safe (noexcept, no allocation)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples.
    /// @param input Input buffer (numSamples elements)
    /// @param output Output buffer (numSamples elements, may alias input)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe. Performs per-block coefficient updates internally.
    void processBlock(const float* input, float* output,
                      size_t numSamples) noexcept;
};

}  // namespace DSP
}  // namespace Krate
