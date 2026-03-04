// ==============================================================================
// Innexus - Dual-Window STFT Configuration
// ==============================================================================
// Documents and centralizes the two concurrent STFT analysis configurations
// used for harmonic analysis. The long window provides high frequency
// resolution for low-frequency partials (1-4), while the short window
// provides fast temporal tracking for upper harmonics (5+).
//
// Actual STFT instances are owned by SampleAnalyzer (Phase 10).
//
// Constitution Compliance:
// - Principle III: Modern C++ (constexpr configuration, C++20)
// - Principle IX: Plugin-local DSP wiring (not shared library)
//
// Reference: spec.md FR-018 to FR-021
// ==============================================================================

#pragma once

#include <krate/dsp/core/window_functions.h>

#include <cstddef>

namespace Innexus {

// =============================================================================
// Dual-Window STFT Configuration Constants
// =============================================================================

/// @brief Configuration for dual-window STFT analysis
///
/// FR-018: Two concurrent STFT passes with different window sizes
/// FR-019: Both use Blackman-Harris windowing for sidelobe rejection
/// FR-020: Long window at slower update rate than short window
/// FR-021: Both produce spectral magnitude data suitable for peak detection
struct StftWindowConfig {
    size_t fftSize;
    size_t hopSize;
    Krate::DSP::WindowType windowType;
};

// Long window: high frequency resolution for low-frequency partials (1-4)
// Bin spacing: 44100 / 4096 = 10.77 Hz
// Update rate: 44100 / 2048 = 21.53 Hz
inline constexpr StftWindowConfig kLongWindowConfig{
    .fftSize = 4096,
    .hopSize = 2048,
    .windowType = Krate::DSP::WindowType::BlackmanHarris
};

// Short window: fast temporal tracking for upper harmonics (5+)
// Bin spacing: 44100 / 1024 = 43.07 Hz
// Update rate: 44100 / 512 = 86.13 Hz
inline constexpr StftWindowConfig kShortWindowConfig{
    .fftSize = 1024,
    .hopSize = 512,
    .windowType = Krate::DSP::WindowType::BlackmanHarris
};

// Derived constants
inline constexpr size_t kLongWindowNumBins = kLongWindowConfig.fftSize / 2 + 1;   // 2049
inline constexpr size_t kShortWindowNumBins = kShortWindowConfig.fftSize / 2 + 1;  // 513

/// @brief Compute frequency resolution (Hz per bin) for a given FFT size and sample rate
/// @param fftSize FFT size
/// @param sampleRate Sample rate in Hz
/// @return Frequency resolution in Hz
constexpr double binSpacing(size_t fftSize, double sampleRate) noexcept {
    return sampleRate / static_cast<double>(fftSize);
}

/// @brief Compute update rate (frames per second) for a given hop size and sample rate
/// @param hopSize Hop size in samples
/// @param sampleRate Sample rate in Hz
/// @return Update rate in Hz
constexpr double updateRate(size_t hopSize, double sampleRate) noexcept {
    return sampleRate / static_cast<double>(hopSize);
}

} // namespace Innexus
