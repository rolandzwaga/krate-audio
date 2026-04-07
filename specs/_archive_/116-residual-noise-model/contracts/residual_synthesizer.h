// ==============================================================================
// CONTRACT: ResidualSynthesizer
// ==============================================================================
// Location: dsp/include/krate/dsp/processors/residual_synthesizer.h
// Layer: 2 (Processors)
// Namespace: Krate::DSP
// Dependencies: Layer 0-1 only (FFT, OverlapAdd, SpectralBuffer, Xorshift32,
//               OnePoleSmoother, window_functions)
//
// This is the API contract -- the actual implementation will match these
// signatures. Comments describe the intended behavior and requirements.
// ==============================================================================

#pragma once

#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/stft.h>           // OverlapAdd
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/smoother.h>        // OnePoleSmoother
#include <krate/dsp/core/random.h>                // Xorshift32

#include <vector>

namespace Krate {
namespace DSP {

/// @brief Resynthesizes the noise (stochastic) component in real time from
///        stored ResidualFrame data.
///
/// Algorithm (per frame):
///   1. Generate white noise (fftSize samples) from deterministic PRNG (FR-013, FR-030)
///   2. Forward FFT the noise via fft_ (a dedicated FFT member -- OverlapAdd encapsulates
///      its own FFT internally and does NOT expose a forward transform interface)
///   3. Multiply noise spectrum by interpolated spectral envelope from ResidualFrame (FR-014)
///   4. Apply brightness tilt to envelope (FR-022, user parameter)
///   5. Scale by frame energy * transient emphasis (FR-016, FR-023)
///   6. Feed shaped spectrum to OverlapAdd for overlap-add reconstruction (FR-015, FR-017)
///
/// Output is pulled sample-by-sample via process() or block-wise via processBlock().
///
/// @note REAL-TIME SAFE after prepare(). No allocations, locks, exceptions, or I/O
///       on the audio thread (FR-020). All buffers pre-allocated during prepare().
class ResidualSynthesizer {
public:
    /// PRNG seed constant -- reset on every prepare() for deterministic output (FR-030)
    static constexpr uint32_t kPrngSeed = 12345;

    ResidualSynthesizer() noexcept = default;
    ~ResidualSynthesizer() noexcept = default;

    // Non-copyable, movable
    ResidualSynthesizer(const ResidualSynthesizer&) = delete;
    ResidualSynthesizer& operator=(const ResidualSynthesizer&) = delete;
    ResidualSynthesizer(ResidualSynthesizer&&) noexcept = default;
    ResidualSynthesizer& operator=(ResidualSynthesizer&&) noexcept = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare the synthesizer for real-time operation.
    ///
    /// Pre-allocates all buffers. Resets PRNG to kPrngSeed.
    /// Must be called before any audio processing.
    ///
    /// @param fftSize    FFT size (must match analysis short-window FFT size)
    /// @param hopSize    Hop size (must match analysis short-window hop size)
    /// @param sampleRate Audio sample rate in Hz
    ///
    /// @note NOT real-time safe (allocates memory).
    void prepare(size_t fftSize, size_t hopSize, float sampleRate) noexcept;

    /// @brief Reset synthesis state (OverlapAdd, PRNG, smoothers).
    ///
    /// Reseeds PRNG to kPrngSeed for deterministic output.
    ///
    /// @note Real-time safe (no allocations).
    void reset() noexcept;

    // =========================================================================
    // Frame Loading (Real-Time Safe)
    // =========================================================================

    /// @brief Load a new ResidualFrame for synthesis.
    ///
    /// Called at each frame boundary (every hopSize samples), synchronized
    /// with the harmonic oscillator bank's frame advancement.
    ///
    /// Generates one frame of shaped noise:
    ///   1. Fill noiseBuffer_ with rng_.nextFloat() calls
    ///   2. Forward FFT noiseBuffer_ via fft_ into spectralBuffer_
    ///   3. interpolateEnvelope() + applyBrightnessTilt() -> envelopeBuffer_
    ///   4. Multiply spectralBuffer_ bins by envelopeBuffer_ * energyScale
    ///   5. Call overlapAdd_.synthesize(spectralBuffer_) then pullSamples() into outputBuffer_
    ///
    /// @param frame            The ResidualFrame for the current time position
    /// @param brightness       Spectral tilt parameter [-1.0, +1.0] (0.0 = neutral)
    /// @param transientEmphasis  Energy boost during transients [0.0, 2.0] (0.0 = no boost)
    ///
    /// @note Real-time safe (no allocations). All buffers pre-allocated.
    void loadFrame(
        const ResidualFrame& frame,
        float brightness = 0.0f,
        float transientEmphasis = 0.0f) noexcept;

    // =========================================================================
    // Audio Output (Real-Time Safe)
    // =========================================================================

    /// @brief Generate one output sample.
    ///
    /// Pulls from the OverlapAdd output buffer. Returns 0.0 if no frames
    /// have been loaded or the buffer is empty.
    ///
    /// @return One sample of resynthesized noise
    /// @note Real-time safe, noexcept
    [[nodiscard]] float process() noexcept;

    /// @brief Generate a block of output samples.
    ///
    /// @param output    Destination buffer
    /// @param numSamples  Number of samples to generate
    /// @note Real-time safe, noexcept
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] size_t fftSize() const noexcept;
    [[nodiscard]] size_t hopSize() const noexcept;

private:
    // --- Internal methods ---

    /// @brief Interpolate 16-band spectral envelope to FFT-bin resolution.
    ///
    /// Uses piecewise-linear interpolation between log-spaced breakpoints.
    /// Result is written to envelopeBuffer_ (numBins floats).
    void interpolateEnvelope(
        const std::array<float, kResidualBands>& bandEnergies) noexcept;

    /// @brief Apply spectral tilt (brightness) to the interpolated envelope.
    ///
    /// tilt(k) = 1.0 + brightness * (2.0 * k/(numBins-1) - 1.0)
    /// Clamped to >= 0.0.
    void applyBrightnessTilt(float brightness) noexcept;

    // --- Internal state ---
    FFT fft_;                              ///< Forward FFT for noise spectrum (step 2 of loadFrame)
    OverlapAdd overlapAdd_;                ///< Overlap-add reconstruction (step 6 of loadFrame)
    SpectralBuffer spectralBuffer_;
    Xorshift32 rng_{kPrngSeed};

    std::vector<float> noiseBuffer_;       ///< White noise (fftSize samples)
    std::vector<float> envelopeBuffer_;    ///< Interpolated envelope (numBins floats)
    std::vector<float> outputBuffer_;      ///< Per-sample output staging buffer

    float sampleRate_ = 0.0f;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    size_t numBins_ = 0;
    bool prepared_ = false;
    bool frameLoaded_ = false;
};

} // namespace DSP
} // namespace Krate
