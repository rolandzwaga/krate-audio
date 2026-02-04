// ==============================================================================
// Layer 1: DSP Primitive - MinBLEP Table
// ==============================================================================
// Precomputed minimum-phase band-limited step function table for high-quality
// discontinuity correction in sync oscillators and beyond.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (sample/consume/addBlep: noexcept, no alloc)
// - Principle III: Modern C++ (C++20, [[nodiscard]], RAII)
// - Principle IX: Layer 1 (depends on Layer 0 + Layer 1 only)
// - Principle XII: Test-First Development
//
// Reference: specs/017-minblep-table/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/interpolation.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

/// @brief Precomputed minimum-phase band-limited step function table.
///
/// Generates and stores a minBLEP table for high-quality discontinuity
/// correction in sync oscillators and beyond. The table is generated once
/// during initialization via prepare(), then used as read-only lookup data
/// during real-time audio processing.
///
/// @par Memory Model
/// Owns the table data (std::vector<float>). After prepare(), the table
/// is immutable. Multiple Residual instances can safely read from the
/// same table without synchronization.
///
/// @par Thread Safety
/// prepare() is NOT real-time safe (allocates memory, performs FFT).
/// sample() is real-time safe (read-only, no allocation, noexcept).
/// Single-threaded ownership model for prepare(). Table data is safe
/// for concurrent reads after prepare() returns.
class MinBlepTable {
public:
    MinBlepTable() noexcept = default;
    ~MinBlepTable() = default;

    // Non-copyable (owns large table), movable
    MinBlepTable(const MinBlepTable&) = delete;
    MinBlepTable& operator=(const MinBlepTable&) = delete;
    MinBlepTable(MinBlepTable&&) noexcept = default;
    MinBlepTable& operator=(MinBlepTable&&) noexcept = default;

    // =========================================================================
    // Lifecycle (FR-001, FR-002)
    // =========================================================================

    /// @brief Generate the minBLEP table. NOT real-time safe.
    ///
    /// Algorithm (FR-003):
    /// 1. Generate Blackman-windowed sinc (BLIT)
    /// 2. Apply minimum-phase transform via cepstral method (BEFORE integration)
    /// 3. Integrate to produce minBLEP
    /// 4. Normalize: scale so final sample = 1.0, clamp first to 0.0
    /// 5. Store as oversampled polyphase table
    ///
    /// @param oversamplingFactor Sub-sample resolution (default 64)
    /// @param zeroCrossings Sinc lobes per side (default 8)
    inline void prepare(size_t oversamplingFactor = 64, size_t zeroCrossings = 8) {
        // FR-006: Handle invalid parameters gracefully
        if (oversamplingFactor == 0 || zeroCrossings == 0) {
            table_.clear();
            length_ = 0;
            oversamplingFactor_ = 0;
            prepared_ = false;
            return;
        }

        oversamplingFactor_ = oversamplingFactor;
        length_ = zeroCrossings * 2;

        // =====================================================================
        // Step 1: Generate Blackman-windowed sinc (BLIT)
        // =====================================================================
        const size_t sincLength = zeroCrossings * oversamplingFactor * 2 + 1;
        std::vector<float> sinc(sincLength, 0.0f);

        // Generate windowed sinc
        const auto halfLen = static_cast<float>(sincLength / 2);
        for (size_t n = 0; n < sincLength; ++n) {
            float x = static_cast<float>(n) - halfLen;
            if (std::abs(x) < 1e-7f) {
                sinc[n] = 1.0f;
            } else {
                float piX = kPi * x / static_cast<float>(oversamplingFactor);
                sinc[n] = std::sin(piX) / piX;
            }
        }

        // Apply Blackman window (FR-004)
        std::vector<float> window(sincLength);
        Window::generateBlackman(window.data(), sincLength);
        for (size_t n = 0; n < sincLength; ++n) {
            sinc[n] *= window[n];
        }

        // =====================================================================
        // Step 2: Minimum-phase transform via cepstral method (FR-005)
        // MUST be applied to impulse BEFORE integration (Brandt et al.)
        // =====================================================================

        // Zero-pad to next power-of-2 FFT size
        size_t fftSize = std::bit_ceil(sincLength);
        if (fftSize < kMinFFTSize) {
            fftSize = kMinFFTSize;
        }
        if (fftSize > kMaxFFTSize) {
            fftSize = kMaxFFTSize;
        }

        FFT fft;
        fft.prepare(fftSize);

        const size_t numBins = fft.numBins();

        // Zero-padded input
        std::vector<float> padded(fftSize, 0.0f);
        const size_t copyLen = std::min(sincLength, fftSize);
        std::copy_n(sinc.data(), copyLen, padded.data());

        // Step 2a-2b: Forward FFT
        std::vector<Complex> spectrum(numBins);
        fft.forward(padded.data(), spectrum.data());

        // Step 2c: Compute log-magnitude
        // Build a real-valued log-magnitude spectrum for full N points
        // then IFFT to get cepstrum
        std::vector<Complex> logMagSpectrum(numBins);
        for (size_t k = 0; k < numBins; ++k) {
            float mag = spectrum[k].magnitude();
            logMagSpectrum[k] = {std::log(mag + 1e-10f), 0.0f};
        }

        // Step 2d: Inverse FFT of log-magnitude to cepstrum domain
        std::vector<float> cepstrum(fftSize, 0.0f);
        fft.inverse(logMagSpectrum.data(), cepstrum.data());

        // Step 2e: Apply cepstral window
        // bin[0] and bin[N/2] unchanged
        // bins[1..N/2-1] doubled
        // bins[N/2+1..N-1] zeroed
        const size_t halfN = fftSize / 2;
        for (size_t n = 1; n < halfN; ++n) {
            cepstrum[n] *= 2.0f;
        }
        // bin[halfN] unchanged
        for (size_t n = halfN + 1; n < fftSize; ++n) {
            cepstrum[n] = 0.0f;
        }

        // Step 2f: Forward FFT of windowed cepstrum back to frequency domain
        std::vector<Complex> minPhaseSpectrum(numBins);
        fft.forward(cepstrum.data(), minPhaseSpectrum.data());

        // Step 2g: Complex exponential to undo the log
        // spectrum[k] = exp(cepstrum[k]) where cepstrum[k] is complex
        for (size_t k = 0; k < numBins; ++k) {
            float r = minPhaseSpectrum[k].real;
            float i = minPhaseSpectrum[k].imag;
            float expR = std::exp(r);
            minPhaseSpectrum[k] = {expR * std::cos(i), expR * std::sin(i)};
        }

        // Step 2h: Inverse FFT to obtain minimum-phase sinc
        std::vector<float> minPhaseSinc(fftSize, 0.0f);
        fft.inverse(minPhaseSpectrum.data(), minPhaseSinc.data());

        // =====================================================================
        // Step 3: Integrate the minimum-phase sinc to produce minBLEP
        // =====================================================================
        // Cumulative sum, truncated to the original sinc length
        const size_t integrationLen = std::min(sincLength, fftSize);
        std::vector<float> minBlep(integrationLen, 0.0f);
        float runningSum = 0.0f;
        for (size_t n = 0; n < integrationLen; ++n) {
            runningSum += minPhaseSinc[n];
            minBlep[n] = runningSum;
        }

        // =====================================================================
        // Step 4: Normalize
        // =====================================================================
        // Scale so final sample = 1.0
        float lastVal = minBlep[integrationLen - 1];
        if (std::abs(lastVal) > 1e-20f) {
            float scale = 1.0f / lastVal;
            for (size_t n = 0; n < integrationLen; ++n) {
                minBlep[n] *= scale;
            }
        }
        // Clamp first sample to 0.0 to prevent pre-echo clicks
        minBlep[0] = 0.0f;

        // =====================================================================
        // Step 5: Store as flat polyphase table
        // =====================================================================
        // table_[index * oversamplingFactor + subIndex]
        const size_t tableSize = length_ * oversamplingFactor_;
        table_.resize(tableSize);

        for (size_t idx = 0; idx < length_; ++idx) {
            for (size_t sub = 0; sub < oversamplingFactor_; ++sub) {
                size_t srcIndex = idx * oversamplingFactor_ + sub;
                if (srcIndex < integrationLen) {
                    table_[idx * oversamplingFactor_ + sub] = minBlep[srcIndex];
                } else {
                    table_[idx * oversamplingFactor_ + sub] = 1.0f;
                }
            }
        }

        // Ensure exact boundary values after polyphase storage
        // FR-027: sample(0.0, 0) == 0.0 and sample(0.0, length-1) == 1.0
        table_[0] = 0.0f;
        // Force the coarse grid point at the last index to exactly 1.0
        table_[(length_ - 1) * oversamplingFactor_] = 1.0f;
        // Also force the very last entry
        table_[tableSize - 1] = 1.0f;

        prepared_ = true;
    }

    // =========================================================================
    // Table Query - Real-Time Safe (FR-008 through FR-014)
    // =========================================================================

    /// @brief Look up interpolated minBLEP value at sub-sample position.
    ///
    /// @param subsampleOffset Fractional position within sample [0, 1), clamped
    /// @param index Output-rate sample index [0, length())
    /// @return Interpolated table value. Returns 1.0 if index >= length().
    ///         Returns 0.0 if not prepared.
    [[nodiscard]] inline float sample(float subsampleOffset, size_t index) const noexcept {
        // FR-013: Return 0.0 if not prepared
        if (!prepared_ || length_ == 0) {
            return 0.0f;
        }

        // FR-012: Return 1.0 if beyond table
        if (index >= length_) {
            return 1.0f;
        }

        // FR-011: Clamp subsampleOffset to [0, 1)
        if (subsampleOffset < 0.0f) {
            subsampleOffset = 0.0f;
        }
        if (subsampleOffset >= 1.0f) {
            subsampleOffset = 1.0f - 1e-7f;
        }

        // FR-009: Compute polyphase table position
        float scaledOffset = subsampleOffset * static_cast<float>(oversamplingFactor_);
        auto subIndex = static_cast<size_t>(scaledOffset);
        float frac = scaledOffset - static_cast<float>(subIndex);

        // Ensure subIndex is in bounds
        if (subIndex >= oversamplingFactor_) {
            subIndex = oversamplingFactor_ - 1;
            frac = 0.0f;
        }

        size_t tableIdx = index * oversamplingFactor_ + subIndex;

        // Get the two adjacent oversampled values for interpolation
        float val0 = table_[tableIdx];

        // Next entry: either next sub-sample or first sub-sample of next index
        size_t nextIdx = tableIdx + 1;
        float val1 = 1.0f;  // Default: settled value beyond table
        if (nextIdx < table_.size()) {
            val1 = table_[nextIdx];
        }

        // FR-010: Linear interpolation
        return Interpolation::linearInterpolate(val0, val1, frac);
    }

    // =========================================================================
    // Query Methods (FR-015, FR-016)
    // =========================================================================

    /// @brief Number of output-rate samples in the table (= zeroCrossings * 2).
    [[nodiscard]] inline size_t length() const noexcept {
        return length_;
    }

    /// @brief Whether prepare() has been called successfully.
    [[nodiscard]] inline bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Nested Residual Struct (FR-017 through FR-024)
    // =========================================================================

    /// @brief Ring buffer for mixing minBLEP corrections into oscillator output.
    ///
    /// @par Usage Pattern
    /// @code
    /// MinBlepTable table;
    /// table.prepare();
    /// MinBlepTable::Residual residual(table);
    ///
    /// // When discontinuity occurs:
    /// residual.addBlep(subsampleOffset, amplitude);
    ///
    /// // Each sample:
    /// output[n] = naiveOutput + residual.consume();
    /// @endcode
    struct Residual {
        /// @brief Construct from a prepared MinBlepTable (FR-018).
        /// Allocates ring buffer of table.length() samples. NOT real-time safe.
        explicit Residual(const MinBlepTable& table)
            : table_(&table)
            , buffer_(table.length(), 0.0f)
            , readIdx_(0) {
        }

        /// @brief Default constructor (no table, consume returns 0.0).
        Residual() noexcept = default;
        ~Residual() = default;

        Residual(const Residual&) = default;
        Residual& operator=(const Residual&) = default;
        Residual(Residual&&) noexcept = default;
        Residual& operator=(Residual&&) noexcept = default;

        /// @brief Stamp a scaled minBLEP correction into the ring buffer.
        ///
        /// Correction formula: correction[i] = amplitude * (table.sample(offset, i) - 1.0)
        /// Corrections are accumulated (added to existing buffer contents).
        /// NaN/Inf amplitude treated as 0.0 (FR-037).
        inline void addBlep(float subsampleOffset, float amplitude) noexcept {
            // FR-037: Handle NaN/Inf amplitude
            if (detail::isNaN(amplitude) || detail::isInf(amplitude)) {
                return;
            }

            if (table_ == nullptr || buffer_.empty()) {
                return;
            }

            const size_t len = buffer_.size();
            for (size_t i = 0; i < len; ++i) {
                // FR-019: correction = amplitude * (table.sample(offset, i) - 1.0)
                float tableVal = table_->sample(subsampleOffset, i);
                float correction = amplitude * (tableVal - 1.0f);
                // FR-020: Accumulate (add to existing contents)
                buffer_[(readIdx_ + i) % len] += correction;
            }
        }

        /// @brief Extract next correction value from the ring buffer (FR-021).
        ///
        /// Returns buffer[readIdx], clears it to 0.0, advances readIdx.
        /// Returns 0.0 if buffer is empty or no corrections pending.
        [[nodiscard]] inline float consume() noexcept {
            if (buffer_.empty()) {
                return 0.0f;
            }

            float value = buffer_[readIdx_];
            buffer_[readIdx_] = 0.0f;
            readIdx_ = (readIdx_ + 1) % buffer_.size();
            return value;
        }

        /// @brief Clear all pending corrections (FR-022).
        inline void reset() noexcept {
            std::fill(buffer_.begin(), buffer_.end(), 0.0f);
            readIdx_ = 0;
        }

    private:
        const MinBlepTable* table_ = nullptr;
        std::vector<float> buffer_;
        size_t readIdx_ = 0;
    };

private:
    std::vector<float> table_;       ///< Flat polyphase table [length * oversamplingFactor]
    size_t length_ = 0;             ///< Output-rate length (zeroCrossings * 2)
    size_t oversamplingFactor_ = 0; ///< Sub-sample resolution
    bool prepared_ = false;         ///< prepare() called successfully
};

} // namespace DSP
} // namespace Krate
