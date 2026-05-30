// ==============================================================================
// Krate DSP — Audio Constants
// ==============================================================================
// Layer 0 (core): Recurring audio-domain magic numbers promoted to named,
// single-source-of-truth constants. Header-only, constexpr, stdlib-only.
//
// These replace literals that were scattered across the DSP library
// (see code-smell-audit.md, rank 14). Values are byte-identical to the
// literals they replace, so migrating a call site is behaviour-preserving.
// ==============================================================================

#pragma once

namespace Krate {
namespace DSP {

// -----------------------------------------------------------------------------
// Frequency limits
// -----------------------------------------------------------------------------

/// @brief Upper edge of the audible band (Hz).
///
/// Used as the maximum filter cutoff / oscillator / tone-frequency ceiling
/// throughout the library. Was previously written inline as `20000.0f`.
constexpr float kMaxAudioFreqHz = 20000.0f;

// -----------------------------------------------------------------------------
// Amplitude thresholds
// -----------------------------------------------------------------------------

/// @brief Threshold below which a signal/gain is treated as silent (~-120 dB).
///
/// Use for "is there any signal here?" tests (RMS/peak/gain gates). Was
/// previously written inline as `1e-6f`. NOTE: this is distinct from a generic
/// numeric epsilon — only use it where the quantity is an audio amplitude.
constexpr float kSilenceThreshold = 1e-6f;

/// @brief Tiny additive guard to keep denominators away from zero / flush
/// denormals in coefficient math. Was previously written inline as `1e-12f`.
constexpr float kDenormalGuard = 1e-12f;

}  // namespace DSP
}  // namespace Krate
