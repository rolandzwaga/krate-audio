// ==============================================================================
// CONTRACT: Layer 2 Processor - Sync Oscillator
// ==============================================================================
// Band-limited synchronized oscillator with hard sync, reverse sync, and
// phase advance sync modes. Composes a master PhaseAccumulator with a slave
// PolyBlepOscillator and MinBlepTable::Residual for anti-aliased sync output.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process/processBlock: noexcept, no alloc)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr, RAII)
// - Principle IX: Layer 2 (depends on Layer 0 + Layer 1 only)
// - Principle XII: Test-First Development
//
// Reference: specs/018-oscillator-sync/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/primitives/minblep_table.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// SyncMode Enumeration (FR-001)
// =============================================================================

/// @brief Synchronization mode for the SyncOscillator.
///
/// File-scope enum in Krate::DSP namespace, shared by downstream components.
enum class SyncMode : uint8_t {
    Hard = 0,        ///< Reset slave phase on master wrap (classic hard sync)
    Reverse = 1,     ///< Reverse slave direction on master wrap (soft sync)
    PhaseAdvance = 2 ///< Advance slave phase by fractional amount on master wrap
};

// =============================================================================
// SyncOscillator Class (FR-002)
// =============================================================================

/// @brief Band-limited synchronized oscillator (Layer 2 processor).
///
/// Composes a lightweight master PhaseAccumulator with a slave PolyBlepOscillator
/// and a MinBlepTable::Residual for anti-aliased oscillator synchronization.
///
/// Supports three sync modes:
/// - **Hard**: Classic hard sync. Slave phase is reset to master's fractional
///   position at each master wrap. MinBLEP correction at the discontinuity.
/// - **Reverse**: Slave direction is reversed at each master wrap. MinBLAMP
///   correction at the derivative discontinuity.
/// - **PhaseAdvance**: Slave phase is nudged toward alignment at each master
///   wrap, controlled by syncAmount. MinBLEP correction proportional to the
///   phase advance.
///
/// @par Ownership Model
/// Constructor takes a `const MinBlepTable*` (caller owns lifetime).
/// Multiple SyncOscillator instances can share one MinBlepTable (read-only
/// after prepare). Each instance maintains its own Residual buffer.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe.
///
/// @par Usage
/// @code
/// MinBlepTable table;
/// table.prepare();
///
/// SyncOscillator osc(&table);
/// osc.prepare(44100.0);
/// osc.setMasterFrequency(220.0f);
/// osc.setSlaveFrequency(660.0f);
/// osc.setSlaveWaveform(OscWaveform::Sawtooth);
/// osc.setSyncMode(SyncMode::Hard);
///
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = osc.process();
/// }
/// @endcode
class SyncOscillator {
public:
    // =========================================================================
    // Constructor (FR-002)
    // =========================================================================

    /// @brief Construct with a pointer to a shared MinBlepTable.
    /// @param table Pointer to prepared MinBlepTable (caller owns lifetime).
    ///        May be nullptr; prepare() will validate before use.
    explicit SyncOscillator(const MinBlepTable* table = nullptr) noexcept;

    ~SyncOscillator() = default;

    // Non-copyable (contains Residual with vector), movable
    SyncOscillator(const SyncOscillator&) = default;
    SyncOscillator& operator=(const SyncOscillator&) = default;
    SyncOscillator(SyncOscillator&&) noexcept = default;
    SyncOscillator& operator=(SyncOscillator&&) noexcept = default;

    // =========================================================================
    // Lifecycle (FR-003, FR-004)
    // =========================================================================

    /// @brief Initialize for the given sample rate. NOT real-time safe.
    ///
    /// Prepares the internal slave oscillator, master phase accumulator,
    /// and MinBLEP/MinBLAMP residual buffer. The MinBlepTable must be
    /// prepared before this call.
    ///
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0, 48000.0)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset phase and state without changing configuration.
    ///
    /// Resets: master phase, slave oscillator, residual buffer, direction flag.
    /// Preserves: frequencies, waveform, sync mode, sync amount, sample rate.
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-005 through FR-010)
    // =========================================================================

    /// @brief Set the master oscillator frequency in Hz.
    /// Clamped to [0, sampleRate/2). NaN/Inf treated as 0.0.
    void setMasterFrequency(float hz) noexcept;

    /// @brief Set the slave oscillator frequency in Hz.
    /// Delegates to PolyBlepOscillator::setFrequency().
    void setSlaveFrequency(float hz) noexcept;

    /// @brief Set the slave oscillator waveform.
    /// Delegates to PolyBlepOscillator::setWaveform().
    void setSlaveWaveform(OscWaveform waveform) noexcept;

    /// @brief Set the active sync mode.
    /// Switching mid-stream is safe; phase and direction state are preserved.
    void setSyncMode(SyncMode mode) noexcept;

    /// @brief Set sync intensity [0.0, 1.0].
    /// 0.0 = no sync (slave runs freely). 1.0 = full sync.
    void setSyncAmount(float amount) noexcept;

    /// @brief Set pulse width for the Pulse slave waveform.
    /// Delegates to PolyBlepOscillator::setPulseWidth().
    void setSlavePulseWidth(float width) noexcept;

    // =========================================================================
    // Processing (FR-011, FR-012)
    // =========================================================================

    /// @brief Generate and return one sample of sync oscillator output.
    /// Real-time safe: no allocation, no exceptions, no blocking, no I/O.
    [[nodiscard]] float process() noexcept;

    /// @brief Generate numSamples into the provided buffer.
    /// Result is identical to calling process() that many times.
    void processBlock(float* output, size_t numSamples) noexcept;

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Evaluate the naive (uncorrected) slave waveform at arbitrary phase.
    /// Used for computing discontinuity amplitude at sync reset points.
    [[nodiscard]] static float evaluateWaveform(
        OscWaveform wf, float phase, float pulseWidth) noexcept;

    /// @brief Evaluate the waveform derivative at arbitrary phase.
    /// Used for computing minBLAMP amplitude at reverse sync points.
    [[nodiscard]] static float evaluateWaveformDerivative(
        OscWaveform wf, float phase, float pulseWidth) noexcept;

    /// @brief Branchless output sanitization.
    [[nodiscard]] static float sanitize(float x) noexcept;

    /// @brief Process hard sync event.
    void processHardSync(double subsampleOffset) noexcept;

    /// @brief Process reverse sync event.
    void processReverseSync(double subsampleOffset) noexcept;

    /// @brief Process phase advance sync event.
    void processPhaseAdvanceSync(double subsampleOffset) noexcept;

    // =========================================================================
    // Internal State
    // =========================================================================

    // Table (non-owning)
    const MinBlepTable* table_ = nullptr;

    // Residual buffer for minBLEP/minBLAMP corrections
    MinBlepTable::Residual residual_;

    // Master phase accumulator (timing only)
    PhaseAccumulator masterPhase_;

    // Slave oscillator (generates audible output)
    PolyBlepOscillator slave_;

    // Cached values
    float sampleRate_ = 0.0f;
    float masterFrequency_ = 0.0f;
    float masterIncrement_ = 0.0f;
    float slaveFrequency_ = 0.0f;

    // Configuration
    OscWaveform slaveWaveform_ = OscWaveform::Sine;
    float slavePulseWidth_ = 0.5f;
    SyncMode syncMode_ = SyncMode::Hard;
    float syncAmount_ = 1.0f;

    // Direction state (reverse sync)
    bool reversed_ = false;

    // Lifecycle
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
