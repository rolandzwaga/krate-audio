// ==============================================================================
// Layer 1: DSP Primitive - Spectral Buffer
// ==============================================================================
// Complex spectrum storage with magnitude/phase manipulation for spectral
// processing effects (filtering, freeze, morphing).
//
// Uses a lazy dual-representation (Cartesian + cached polar) with dirty flags
// to eliminate redundant per-bin transcendental math. Bulk conversions happen
// at most once per frame at representation boundaries:
//   FFT write → computePolar (1x sqrt+atan2 per bin)
//   polar reads/writes → O(1) cached lookups
//   IFFT read → syncCartesian (1x cos+sin per bin)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept accessors, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 1 (depends only on Layer 0 / fft.h)
// - Principle XII: Test-First Development
//
// Reference: specs/007-fft-processor/spec.md
// ==============================================================================

#pragma once

#include "fft.h"  // For Complex struct
#include <krate/dsp/core/spectral_simd.h>  // SIMD-accelerated bulk conversions

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// SpectralBuffer Class
// =============================================================================

/// @brief Complex spectrum storage with magnitude/phase manipulation
/// @note Uses lazy dual-representation: Cartesian data_[] for FFT I/O,
///       cached mags_[]/phases_[] for O(1) polar access. Mutable caches
///       follow the standard C++ lazy-evaluation pattern — the logical
///       state of the buffer is unchanged by const operations even though
///       the internal representation may be updated.
class SpectralBuffer {
public:
    SpectralBuffer() noexcept = default;
    ~SpectralBuffer() noexcept = default;

    // Movable
    SpectralBuffer(SpectralBuffer&&) noexcept = default;
    SpectralBuffer& operator=(SpectralBuffer&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare buffer for given FFT size
    /// @param fftSize FFT size (buffer will hold fftSize/2+1 bins)
    /// @note NOT real-time safe (allocates memory)
    void prepare(size_t fftSize) noexcept {
        numBins_ = fftSize / 2 + 1;
        data_.resize(numBins_);
        mags_.resize(numBins_);
        phases_.resize(numBins_);
        reset();
    }

    /// @brief Reset all bins to zero
    /// @note Real-time safe
    void reset() noexcept {
        std::fill(data_.begin(), data_.end(), Complex{0.0f, 0.0f});
        std::fill(mags_.begin(), mags_.end(), 0.0f);
        std::fill(phases_.begin(), phases_.end(), 0.0f);
        polarValid_ = true;
        cartesianValid_ = true;
    }

    // -------------------------------------------------------------------------
    // Polar Access (Magnitude/Phase)
    // -------------------------------------------------------------------------

    /// @brief Get magnitude of bin k: |X[k]|
    [[nodiscard]] float getMagnitude(size_t bin) const noexcept {
        if (bin >= numBins_) return 0.0f;
        ensurePolarValid();
        return mags_[bin];
    }

    /// @brief Get phase of bin k in radians: ∠X[k]
    [[nodiscard]] float getPhase(size_t bin) const noexcept {
        if (bin >= numBins_) return 0.0f;
        ensurePolarValid();
        return phases_[bin];
    }

    /// @brief Set magnitude, preserving phase
    void setMagnitude(size_t bin, float magnitude) noexcept {
        if (bin >= numBins_) return;
        ensurePolarValid();
        mags_[bin] = magnitude;
        cartesianValid_ = false;
    }

    /// @brief Set phase in radians, preserving magnitude
    void setPhase(size_t bin, float phase) noexcept {
        if (bin >= numBins_) return;
        ensurePolarValid();
        phases_[bin] = phase;
        cartesianValid_ = false;
    }

    // -------------------------------------------------------------------------
    // Cartesian Access (Real/Imaginary)
    // -------------------------------------------------------------------------

    /// @brief Get real component of bin k
    [[nodiscard]] float getReal(size_t bin) const noexcept {
        if (bin >= numBins_) return 0.0f;
        ensureCartesianValid();
        return data_[bin].real;
    }

    /// @brief Get imaginary component of bin k
    [[nodiscard]] float getImag(size_t bin) const noexcept {
        if (bin >= numBins_) return 0.0f;
        ensureCartesianValid();
        return data_[bin].imag;
    }

    /// @brief Set both real and imaginary components
    void setCartesian(size_t bin, float real, float imag) noexcept {
        if (bin >= numBins_) return;
        ensureCartesianValid();
        data_[bin].real = real;
        data_[bin].imag = imag;
        polarValid_ = false;
    }

    // -------------------------------------------------------------------------
    // Raw Access
    // -------------------------------------------------------------------------

    /// @brief Direct mutable access to complex data array
    /// @note Syncs Cartesian from polar if needed (pointer may be used for
    ///       reading, e.g. fft.inverse). Invalidates polar cache since the
    ///       caller may modify Cartesian data through the returned pointer.
    [[nodiscard]] Complex* data() noexcept {
        ensureCartesianValid();
        polarValid_ = false;
        return data_.data();
    }

    /// @brief Direct const access to complex data array (for IFFT input)
    /// @note Triggers Cartesian reconstruction from polar cache if needed.
    [[nodiscard]] const Complex* data() const noexcept {
        ensureCartesianValid();
        return data_.data();
    }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    /// @brief Number of bins (N/2+1)
    [[nodiscard]] size_t numBins() const noexcept { return numBins_; }

    /// @brief Check if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept { return numBins_ > 0; }

private:
    // -------------------------------------------------------------------------
    // Lazy Cache Synchronization
    // -------------------------------------------------------------------------

    /// @brief Bulk compute polar (magnitude/phase) from Cartesian data
    /// @note Called lazily on first polar access after Cartesian was modified.
    ///       Uses SIMD-accelerated bulk conversion via Google Highway.
    void ensurePolarValid() const noexcept {
        if (polarValid_) return;
        // reinterpret_cast is safe: Complex is standard-layout with two floats
        computePolarBulk(reinterpret_cast<const float*>(data_.data()),
                         numBins_, mags_.data(), phases_.data());
        polarValid_ = true;
    }

    /// @brief Bulk reconstruct Cartesian data from polar (magnitude/phase)
    /// @note Called lazily on first Cartesian access after polar was modified.
    ///       Uses SIMD-accelerated bulk conversion via Google Highway.
    void ensureCartesianValid() const noexcept {
        if (cartesianValid_) return;
        // reinterpret_cast is safe: Complex is standard-layout with two floats
        reconstructCartesianBulk(mags_.data(), phases_.data(),
                                  numBins_,
                                  reinterpret_cast<float*>(data_.data()));
        cartesianValid_ = true;
    }

    // -------------------------------------------------------------------------
    // Data Members
    // -------------------------------------------------------------------------

    // Cartesian representation (AoS) — used for FFT I/O.
    // Mutable because it serves as a cache when polar is the authoritative
    // representation (standard C++ lazy-evaluation pattern).
    mutable std::vector<Complex> data_;

    // Cached polar representation — populated lazily from data_[]
    mutable std::vector<float> mags_;
    mutable std::vector<float> phases_;

    // Dirty flags for lazy synchronization
    mutable bool polarValid_ = true;
    mutable bool cartesianValid_ = true;

    size_t numBins_ = 0;
};

} // namespace DSP
} // namespace Krate
