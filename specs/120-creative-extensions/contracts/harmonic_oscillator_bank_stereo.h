// ==============================================================================
// CONTRACT: HarmonicOscillatorBank Stereo + Detune Extension
// ==============================================================================
// This file defines the API contract for the stereo and detune extensions to
// HarmonicOscillatorBank. It is NOT compiled -- it documents the interface that
// will be added to the existing class in:
//   dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h
//
// Spec: specs/120-creative-extensions/spec.md
// Covers: FR-006 to FR-013 (Stereo Spread), FR-030 to FR-032 (Detune Spread),
//         FR-050 (processStereo API)
// ==============================================================================

#pragma once

// These methods and members are ADDED to the existing HarmonicOscillatorBank class.
// The existing float process() and processBlock() remain unchanged.

namespace Krate::DSP {

// --- New public methods (added to HarmonicOscillatorBank) ---

/// @brief Generate a single stereo output sample (FR-007, FR-050).
///
/// Each partial contributes to left and right channels based on its pan
/// position. Pan positions are set by setStereoSpread() and updated per frame.
///
/// When stereoSpread == 0.0, left == right (mono center, SC-010).
///
/// @param[out] left Left channel output sample
/// @param[out] right Right channel output sample
/// @note Real-time safe
// void processStereo(float& left, float& right) noexcept;

/// @brief Generate a block of stereo output samples (FR-007).
///
/// @param[out] leftOutput Left channel buffer (must hold numSamples)
/// @param[out] rightOutput Right channel buffer (must hold numSamples)
/// @param numSamples Number of samples to generate
/// @note Real-time safe
// void processStereoBlock(float* leftOutput, float* rightOutput, size_t numSamples) noexcept;

/// @brief Set stereo spread amount (FR-006, FR-008, FR-009).
///
/// Recalculates per-partial pan positions and pan coefficients.
/// Odd partials pan left, even partials pan right.
/// Fundamental (partial 1) uses reduced spread (25%) for bass mono compat (FR-009).
///
/// Pan law: constant-power
///   angle = pi/4 + panPosition * pi/4
///   panLeft[n] = cos(angle)
///   panRight[n] = sin(angle)
///
/// @param spread Spread amount [0.0, 1.0]. 0.0 = mono center, 1.0 = max spread.
/// @note Real-time safe (called once per frame, not per sample)
// void setStereoSpread(float spread) noexcept;

/// @brief Set detune spread amount (FR-030, FR-031, FR-032).
///
/// Computes per-partial frequency multipliers for chorus-like detuning.
/// Offset scales with harmonic number, alternating +/- by odd/even.
///
/// Formula: detuneOffset_n = detuneSpread * n * kDetuneMaxCents * direction
///          multiplier_n = pow(2.0, detuneOffset_n / 1200.0)
/// where direction = +1 for odd harmonics, -1 for even harmonics.
///
/// @param spread Detune amount [0.0, 1.0]. 0.0 = no detune, 1.0 = max (15 cents * n).
/// @note Real-time safe (called once per frame, not per sample)
// void setDetuneSpread(float spread) noexcept;

/// @brief Get the current stereo spread value.
// [[nodiscard]] float getStereoSpread() const noexcept;

/// @brief Get the current detune spread value.
// [[nodiscard]] float getDetuneSpread() const noexcept;

// --- New private members (added to HarmonicOscillatorBank) ---

// Per-partial pan and detune arrays (SoA, 32-byte aligned):
// alignas(32) std::array<float, kMaxPartials> panPosition_{};     // [-1, +1]
// alignas(32) std::array<float, kMaxPartials> panLeft_{};          // cos(angle)
// alignas(32) std::array<float, kMaxPartials> panRight_{};         // sin(angle)
// alignas(32) std::array<float, kMaxPartials> detuneMultiplier_{}; // freq multiplier

// Spread/detune state:
// float stereoSpread_ = 0.0f;
// float detuneSpread_ = 0.0f;

// Constants:
// static constexpr float kDetuneMaxCents = 15.0f;
// static constexpr float kFundamentalSpreadScale = 0.25f; // FR-009

} // namespace Krate::DSP
