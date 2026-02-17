// ==============================================================================
// API Contract: Identity Phase Locking additions to PhaseVocoderPitchShifter
// ==============================================================================
// Feature: 061-phase-locking
// This file documents the PUBLIC API additions to PhaseVocoderPitchShifter.
// It is NOT a compilable header -- it is a design contract for implementation.
//
// The actual implementation lives in:
//   dsp/include/krate/dsp/processors/pitch_shift_processor.h
// ==============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

/// @brief Phase vocoder pitch shifter with identity phase locking.
///
/// Additions to the existing PhaseVocoderPitchShifter class for identity
/// phase locking (Laroche & Dolson, 1999). These API additions enable
/// toggling phase locking at runtime while maintaining backward compatibility.
///
/// When enabled (default), the phase vocoder preserves vertical phase coherence
/// by detecting spectral peaks, assigning bins to regions of influence, and
/// locking non-peak bin phases relative to their region peak. This dramatically
/// reduces the "phasiness" artifact in the pitch-shifted output.
///
/// When disabled, behavior is identical to the pre-modification basic phase
/// vocoder (per-bin independent phase accumulation).

// ============================================================================
// New Constants (added to PhaseVocoderPitchShifter)
// ============================================================================

// static constexpr std::size_t kMaxBins = 4097;    // 8192/2+1 (max supported FFT)
// static constexpr std::size_t kMaxPeaks = 512;    // Max detectable peaks per frame

// ============================================================================
// New Public Methods (added to PhaseVocoderPitchShifter)
// ============================================================================

/// Enable or disable identity phase locking.
///
/// When enabled (default), the phase vocoder applies identity phase locking
/// to preserve vertical phase coherence, dramatically reducing phasiness.
///
/// When disabled, the phase vocoder reverts to basic per-bin phase accumulation,
/// producing output identical to the pre-modification implementation.
///
/// Toggle is safe during continuous audio processing when called from
/// the same thread as processFrame():
/// - Locked -> Basic: synthPhase_[] re-initialized from analysis phase.
///   Brief single-frame artifact at transition is acceptable.
/// - Basic -> Locked: No special handling. Rotation angle derived fresh.
///
/// @param enabled  true to enable phase locking, false to disable
/// @note NOT thread-safe. `phaseLockingEnabled_` is a plain bool (not
///       std::atomic<bool>). Do NOT call this concurrently with
///       processFrame() from a different thread -- that is a data race
///       (Constitution Principle II). Intended use: call from the audio
///       thread itself, or from a control thread only when the audio
///       thread is not executing processFrame() (e.g., during a host
///       transport stop or between process calls with external locking
///       managed by the caller).
/// @note Real-time safe when called from the audio thread: no allocations, no locks
// void setPhaseLocking(bool enabled) noexcept;

/// Returns the current phase locking state.
///
/// @return true if phase locking is enabled, false otherwise
// [[nodiscard]] bool getPhaseLocking() const noexcept;

// ============================================================================
// New Member Variables (added to PhaseVocoderPitchShifter private section)
// ============================================================================

// Phase locking state (pre-allocated, zero runtime allocation)
// std::array<bool, kMaxBins> isPeak_{};              // 4097 bytes - peak flag per analysis bin
// std::array<uint16_t, kMaxPeaks> peakIndices_{};    // 1024 bytes - peak bin indices (uint16_t: max 4096 < 65535)
// std::size_t numPeaks_ = 0;                         // Count of detected peaks
// std::array<uint16_t, kMaxBins> regionPeak_{};      // 8194 bytes - region-peak assignment per analysis bin
// bool phaseLockingEnabled_ = true;                  // Phase locking toggle (default: enabled)
// bool wasLocked_ = false;                           // Previous frame state (for toggle-to-basic re-init)

// ============================================================================
// Modified Methods
// ============================================================================

/// processFrame() is modified to include:
/// 1. Peak detection in analysis-domain magnitude spectrum (after Step 1)
/// 2. Region-of-influence assignment (after peak detection)
/// 3. Two-pass synthesis: peak bins first (horizontal phase coherence),
///    then non-peak bins (identity phase locking via rotation angle)
///
/// When phaseLockingEnabled_ is false, processFrame() behaves identically
/// to the pre-modification implementation.

/// reset() is modified to additionally clear:
/// - isPeak_ (fill false)
/// - peakIndices_ (fill 0)
/// - numPeaks_ = 0
/// - regionPeak_ (fill 0)
/// - wasLocked_ = false

} // namespace Krate::DSP
