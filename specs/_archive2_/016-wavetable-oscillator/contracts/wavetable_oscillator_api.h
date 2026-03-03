// ==============================================================================
// API Contract: WavetableOscillator (Layer 1)
// ==============================================================================
// This file defines the public API for wavetable_oscillator.h.
// It is a design artifact, NOT compiled code.
//
// Location: dsp/include/krate/dsp/primitives/wavetable_oscillator.h
// Layer: 1 (depends on Layer 0 only: wavetable_data.h, interpolation.h,
//           phase_utils.h, math_constants.h, db_utils.h)
// Namespace: Krate::DSP
//
// Interface mirrors PolyBlepOscillator for interchangeability.
//
// Reference: specs/016-wavetable-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/wavetable_data.h>
#include <krate/dsp/core/interpolation.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// WavetableOscillator Class (FR-029 through FR-052)
// =============================================================================

/// @brief Wavetable playback oscillator with automatic mipmap selection.
///
/// Reads from a mipmapped WavetableData structure using cubic Hermite
/// interpolation. Automatically selects and crossfades between mipmap
/// levels based on playback frequency to prevent aliasing. Supports
/// phase modulation (PM) and frequency modulation (FM).
///
/// The WavetableOscillator follows the same lifecycle and phase interface
/// as PolyBlepOscillator for interchangeability in downstream components
/// (FM Operator, PD Oscillator, Vector Mixer).
///
/// @par Memory Model
/// Holds a non-owning pointer to WavetableData. The caller is responsible
/// for ensuring the WavetableData outlives the oscillator. Multiple
/// oscillators can share the same WavetableData (~90 KB) for polyphonic
/// usage.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
/// No internal synchronization.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe: no allocation,
/// no exceptions, no blocking, no I/O.
///
/// @par Usage
/// @code
/// WavetableData sawTable;
/// generateMipmappedSaw(sawTable);
///
/// WavetableOscillator osc;
/// osc.prepare(44100.0);
/// osc.setWavetable(&sawTable);
/// osc.setFrequency(440.0f);
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = osc.process();
/// }
/// @endcode
class WavetableOscillator {
public:
    // =========================================================================
    // Lifecycle (FR-030, FR-031)
    // =========================================================================

    WavetableOscillator() noexcept = default;
    ~WavetableOscillator() = default;

    // Copyable and movable (value semantics, pointer is non-owning)
    WavetableOscillator(const WavetableOscillator&) noexcept = default;
    WavetableOscillator& operator=(const WavetableOscillator&) noexcept = default;
    WavetableOscillator(WavetableOscillator&&) noexcept = default;
    WavetableOscillator& operator=(WavetableOscillator&&) noexcept = default;

    /// @brief Initialize the oscillator for the given sample rate.
    /// Resets all internal state. NOT real-time safe.
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0, 48000.0, 96000.0)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset phase and modulation state without changing configuration.
    /// Resets: phase to 0, phaseWrapped to false, FM/PM offsets to 0.
    /// Preserves: frequency, sample rate, wavetable pointer.
    /// Real-time safe.
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-032, FR-033)
    // =========================================================================

    /// @brief Set the wavetable data for playback.
    /// Non-owning pointer; nullptr produces silence.
    /// The caller must ensure the WavetableData outlives this oscillator.
    /// @param table Pointer to immutable wavetable data (nullptr = silence)
    void setWavetable(const WavetableData* table) noexcept;

    /// @brief Set the oscillator frequency in Hz.
    /// Silently clamped to [0, sampleRate/2) to prevent aliasing.
    /// NaN/Inf inputs are treated as 0 Hz.
    /// @param hz Frequency in Hz
    void setFrequency(float hz) noexcept;

    // =========================================================================
    // Processing (FR-034, FR-035, FR-035a, FR-036, FR-037, FR-038)
    // =========================================================================

    /// @brief Generate and return one sample of wavetable output.
    ///
    /// Processing flow:
    /// 1. If table_ is nullptr, return 0.0f
    /// 2. Compute effective frequency (base + FM), guard NaN, clamp to [0, sr/2)
    /// 3. Compute effective phase (base + PM/2pi), wrap to [0, 1)
    /// 4. Select fractional mipmap level via selectMipmapLevelFractional()
    /// 5. If fractional part near integer (< 0.05 or > 0.95): single cubic Hermite
    ///    lookup. Otherwise: two lookups from adjacent levels, linearly blended.
    /// 6. Advance phase via PhaseAccumulator::advance()
    /// 7. Reset FM/PM offsets (non-accumulating)
    /// 8. Sanitize output (NaN -> 0.0, clamp to [-2.0, 2.0])
    ///
    /// @return Audio sample, nominally in [-1, 1]
    /// @note Real-time safe, noexcept
    [[nodiscard]] float process() noexcept;

    /// @brief Generate numSamples into the buffer at constant frequency.
    ///
    /// Mipmap level is computed once at the start. Result is identical to
    /// calling process() numSamples times.
    ///
    /// @param output Pointer to output buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate (0 = no-op)
    /// @note Real-time safe, noexcept
    void processBlock(float* output, size_t numSamples) noexcept;

    /// @brief Generate numSamples with per-sample frequency modulation.
    ///
    /// Effective frequency for sample i = baseFrequency + fmBuffer[i].
    /// Mipmap level selection happens per-sample.
    ///
    /// @param output Pointer to output buffer (must hold numSamples floats)
    /// @param fmBuffer Per-sample FM offset in Hz
    /// @param numSamples Number of samples to generate (0 = no-op)
    /// @note Real-time safe, noexcept
    void processBlock(float* output, const float* fmBuffer, size_t numSamples) noexcept;

    // =========================================================================
    // Phase Access (FR-039, FR-040, FR-041) -- matches PolyBlepOscillator
    // =========================================================================

    /// @brief Get the current phase position.
    /// @return Phase in [0, 1), representing position in the waveform cycle
    [[nodiscard]] double phase() const noexcept;

    /// @brief Check if the most recent process() call produced a phase wrap.
    /// @return true if the phase crossed from near-1.0 to near-0.0
    [[nodiscard]] bool phaseWrapped() const noexcept;

    /// @brief Force the phase to a specific position.
    /// Value is wrapped to [0, 1) if outside range.
    /// @param newPhase Phase position (default: 0.0)
    void resetPhase(double newPhase = 0.0) noexcept;

    // =========================================================================
    // Modulation Inputs (FR-042, FR-043) -- matches PolyBlepOscillator
    // =========================================================================

    /// @brief Add a phase modulation offset for the current sample.
    /// Converted from radians to normalized [0, 1) internally (offset / 2pi).
    /// Does NOT accumulate between samples -- set before each process() call.
    /// @param radians Phase offset in radians
    void setPhaseModulation(float radians) noexcept;

    /// @brief Add a frequency modulation offset for the current sample.
    /// Effective frequency is clamped to [0, sampleRate/2).
    /// Does NOT accumulate between samples -- set before each process() call.
    /// @param hz Frequency offset in Hz (can be negative)
    void setFrequencyModulation(float hz) noexcept;

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Read a sample from a single mipmap level using cubic Hermite.
    /// @param level Mipmap level index
    /// @param normalizedPhase Phase in [0, 1)
    /// @return Interpolated sample value
    [[nodiscard]] float readLevel(size_t level, double normalizedPhase) const noexcept;

    /// @brief Branchless output sanitization (FR-051).
    /// NaN detection via bit manipulation (works with -ffast-math),
    /// clamp to [-2.0, 2.0].
    [[nodiscard]] static float sanitize(float x) noexcept;

    // =========================================================================
    // Member Variables (cache-friendly layout, hot-path data first)
    // =========================================================================

    PhaseAccumulator phaseAcc_;              // Phase state (16 bytes)
    float sampleRate_ = 0.0f;               // Sample rate in Hz
    float frequency_ = 440.0f;              // Base frequency in Hz
    float fmOffset_ = 0.0f;                 // FM offset in Hz (per-sample, reset after use)
    float pmOffset_ = 0.0f;                 // PM offset in radians (per-sample, reset after use)
    const WavetableData* table_ = nullptr;  // Non-owning pointer to wavetable data
    bool phaseWrapped_ = false;             // Last process() produced a phase wrap

    // Total size: ~48 bytes (fits in one cache line)
};

} // namespace DSP
} // namespace Krate
