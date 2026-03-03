// ==============================================================================
// API Contract: MinBLEP Table
// ==============================================================================
// This is a design contract, NOT the implementation. It defines the public API
// that the implementation must satisfy.
//
// Location: dsp/include/krate/dsp/primitives/minblep_table.h
// Namespace: Krate::DSP
// Layer: 1 (Primitives) -- depends on Layer 0 and Layer 1 only
//
// Dependencies:
//   Layer 0: core/window_functions.h, core/math_constants.h, core/interpolation.h
//   Layer 1: primitives/fft.h
//   Stdlib: <algorithm>, <cmath>, <cstddef>, <vector>
//
// Reference: specs/017-minblep-table/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/interpolation.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>

#include <algorithm>
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
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (sample/consume/addBlep: noexcept, no alloc)
/// - Principle III: Modern C++ (C++20, [[nodiscard]], RAII)
/// - Principle IX: Layer 1 (depends on Layer 0 + Layer 1 only)
/// - Principle XII: Test-First Development
class MinBlepTable {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002)
    // =========================================================================

    MinBlepTable() noexcept = default;
    ~MinBlepTable() = default;

    // Non-copyable (owns large table), movable
    MinBlepTable(const MinBlepTable&) = delete;
    MinBlepTable& operator=(const MinBlepTable&) = delete;
    MinBlepTable(MinBlepTable&&) noexcept = default;
    MinBlepTable& operator=(MinBlepTable&&) noexcept = default;

    /// @brief Generate the minBLEP table. NOT real-time safe.
    ///
    /// Algorithm (FR-003):
    /// 1. Generate Blackman-windowed sinc (BLIT)
    /// 2. Integrate to produce BLEP
    /// 3. Minimum-phase transform via cepstral method
    /// 4. Normalize: scale so final sample = 1.0, clamp first to 0.0
    /// 5. Store as oversampled polyphase table
    ///
    /// @param oversamplingFactor Sub-sample resolution (default 64)
    /// @param zeroCrossings Sinc lobes per side (default 8)
    /// @note If oversamplingFactor==0 or zeroCrossings==0, no table generated (FR-006)
    void prepare(size_t oversamplingFactor = 64, size_t zeroCrossings = 8);

    // =========================================================================
    // Table Query - Real-Time Safe (FR-008 through FR-014)
    // =========================================================================

    /// @brief Look up interpolated minBLEP value at sub-sample position.
    ///
    /// @param subsampleOffset Fractional position within sample [0, 1), clamped (FR-011)
    /// @param index Output-rate sample index [0, length())
    /// @return Interpolated table value. Returns 1.0 if index >= length() (FR-012).
    ///         Returns 0.0 if not prepared (FR-013).
    /// @note Uses linear interpolation between oversampled entries (FR-010)
    /// @note Real-time safe: no allocation, no exceptions, no blocking (FR-014)
    [[nodiscard]] float sample(float subsampleOffset, size_t index) const noexcept;

    // =========================================================================
    // Query Methods (FR-015, FR-016)
    // =========================================================================

    /// @brief Number of output-rate samples in the table (= zeroCrossings * 2).
    [[nodiscard]] size_t length() const noexcept;

    /// @brief Whether prepare() has been called successfully.
    [[nodiscard]] bool isPrepared() const noexcept;

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
    ///
    /// @par Thread Safety
    /// Single-threaded. All methods are real-time safe (FR-023).
    struct Residual {
        /// @brief Construct from a prepared MinBlepTable (FR-018).
        /// Allocates ring buffer of table.length() samples. NOT real-time safe.
        explicit Residual(const MinBlepTable& table);

        /// @brief Default constructor (no table, consume returns 0.0).
        Residual() noexcept = default;
        ~Residual() = default;

        Residual(const Residual&) = default;
        Residual& operator=(const Residual&) = default;
        Residual(Residual&&) noexcept = default;
        Residual& operator=(Residual&&) noexcept = default;

        /// @brief Stamp a scaled minBLEP correction into the ring buffer (FR-019, FR-020).
        ///
        /// Correction formula: correction[i] = amplitude * (table.sample(offset, i) - 1.0)
        /// Corrections are accumulated (added to existing buffer contents).
        /// NaN/Inf amplitude treated as 0.0 (FR-037).
        ///
        /// @param subsampleOffset Sub-sample position of discontinuity [0, 1)
        /// @param amplitude Height of the discontinuity (can be negative)
        void addBlep(float subsampleOffset, float amplitude) noexcept;

        /// @brief Extract next correction value from the ring buffer (FR-021).
        ///
        /// Returns buffer[readIdx], clears it to 0.0, advances readIdx.
        /// Returns 0.0 if buffer is empty or no corrections pending (FR-036).
        ///
        /// @return Correction value to add to oscillator output
        [[nodiscard]] float consume() noexcept;

        /// @brief Clear all pending corrections (FR-022).
        void reset() noexcept;

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
