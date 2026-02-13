// ==============================================================================
// Layer 3: System Component - SelectableOscillator
// ==============================================================================
// Pointer-to-base oscillator wrapper with pre-allocated slot pool for the
// Ruinae voice architecture. All 10 oscillator types are pre-allocated at
// prepare() time; setType() swaps the active pointer with zero heap allocation.
//
// Feature: 041-ruinae-voice-architecture
// Layer: 3 (Systems)
// Dependencies:
//   - Layer 0: core/db_utils.h (isNaN, isInf)
//   - Layer 3: systems/oscillator_slot.h, systems/oscillator_adapters.h,
//              systems/oscillator_types.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in setType/processBlock)
// - Principle III: Modern C++ (C++20, virtual dispatch, unique_ptr)
// - Principle IX: Layer 3 (depends on Layers 0, 1, 2, 3)
// - Principle XIV: ODR Prevention (unique class name verified)
//
// Reference: specs/041-ruinae-voice-architecture/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/systems/oscillator_types.h>
#include <krate/dsp/systems/oscillator_slot.h>
#include <krate/dsp/systems/oscillator_adapters.h>

// Layer 0
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

// For fallback resource creation
#include <krate/dsp/primitives/wavetable_generator.h>
#include <krate/dsp/primitives/minblep_table.h>

namespace Krate::DSP {

// =============================================================================
// SelectableOscillator Class (FR-001 through FR-005)
// =============================================================================

/// @brief Pre-allocated oscillator pool with pointer-based type switching.
///
/// All 10 oscillator types are heap-allocated and prepared at prepare() time.
/// setType() swaps the active pointer (zero heap allocation, SC-004 compliant).
///
/// @par Pre-Allocation Strategy
/// On prepare(), creates and prepares one instance of each oscillator type
/// via OscillatorAdapter<T>. Active type is selected by pointer, not by
/// constructing/destroying objects.
///
/// @par Thread Safety
/// Single-threaded model. All methods called from the audio thread.
///
/// @par Real-Time Safety
/// setType() and processBlock() are fully real-time safe (zero allocations).
/// prepare() is NOT real-time safe.
class SelectableOscillator {
public:
    static constexpr size_t kNumOscTypes = static_cast<size_t>(OscType::NumTypes);

    // =========================================================================
    // Lifecycle
    // =========================================================================

    SelectableOscillator() noexcept = default;
    ~SelectableOscillator() noexcept = default;

    // Non-copyable (unique_ptrs are non-copyable)
    SelectableOscillator(const SelectableOscillator&) = delete;
    SelectableOscillator& operator=(const SelectableOscillator&) = delete;
    SelectableOscillator(SelectableOscillator&&) noexcept = default;
    SelectableOscillator& operator=(SelectableOscillator&&) noexcept = default;

    /// @brief Pre-allocate and prepare all oscillator types.
    ///
    /// Creates one OscillatorAdapter for each of the 10 oscillator types,
    /// prepares them all, and sets the active pointer to the default type.
    /// NOT real-time safe (heap allocations).
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size in samples
    /// @param resources Shared oscillator resources (wavetable, minblep table)
    void prepare(double sampleRate, size_t maxBlockSize,
                 OscillatorResources* resources = nullptr) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Create and prepare all 10 oscillator slots
        createAllSlots(resources);
        for (auto& slot : slots_) {
            if (slot) {
                slot->prepare(sampleRate, maxBlockSize);
            }
        }

        // Set active pointer to default type
        active_ = slots_[static_cast<size_t>(activeType_)].get();
        prepared_ = true;

        // Apply current frequency to active slot
        if (active_) {
            active_->setFrequency(currentFrequency_);
        }
    }

    /// @brief Reset the active oscillator state without changing type.
    void reset() noexcept {
        if (active_) {
            active_->reset();
        }
    }

    // =========================================================================
    // Type Selection (FR-002, FR-003, FR-004, FR-005)
    // =========================================================================

    /// @brief Set the active oscillator type (SC-004: zero allocations).
    ///
    /// Switches the active pointer to the pre-allocated slot for the given
    /// type. No heap allocation occurs. If the type is the same as the
    /// current type, this is a no-op (AS-3.1).
    ///
    /// @param type The oscillator type to activate
    void setType(OscType type) noexcept {
        // No-op if same type (AS-3.1)
        if (type == activeType_) {
            return;
        }

        activeType_ = type;

        if (!prepared_) {
            return;
        }

        const auto idx = static_cast<size_t>(type);
        if (idx >= kNumOscTypes || !slots_[idx]) {
            return;
        }

        active_ = slots_[idx].get();

        // Optionally reset phase (but NOT for SpectralFreeze, which was
        // freeze-initialized in prepare and reset() would clear its frozen state)
        if (phaseMode_ == PhaseMode::Reset && type != OscType::SpectralFreeze) {
            active_->reset();
        }

        // Restore frequency
        active_->setFrequency(currentFrequency_);
    }

    /// @brief Get the currently active oscillator type.
    [[nodiscard]] OscType getActiveType() const noexcept {
        return activeType_;
    }

    /// @brief Set the phase mode for type switches.
    void setPhaseMode(PhaseMode mode) noexcept {
        phaseMode_ = mode;
    }

    // =========================================================================
    // Frequency Control
    // =========================================================================

    /// @brief Set the oscillator frequency in Hz.
    ///
    /// NaN/Inf values are silently ignored, preserving the previous frequency.
    ///
    /// @param hz Frequency in Hz
    void setFrequency(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            return;  // Silently ignore invalid values
        }
        currentFrequency_ = hz;
        if (active_) {
            active_->setFrequency(hz);
        }
    }

    // =========================================================================
    // Processing (FR-004)
    // =========================================================================

    /// @brief Generate a block of samples from the active oscillator.
    ///
    /// If not prepared, fills the output with silence.
    ///
    /// @param output Output buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    void processBlock(float* output, size_t numSamples) noexcept {
        if (!prepared_ || output == nullptr || numSamples == 0 || !active_) {
            if (output != nullptr) {
                std::fill(output, output + numSamples, 0.0f);
            }
            return;
        }

        // Clamp to max block size to prevent buffer overruns
        numSamples = std::min(numSamples, maxBlockSize_);

        active_->processBlock(output, numSamples);
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Create all 10 oscillator adapter slots.
    ///
    /// Shared resources (WavetableData, MinBlepTable) are passed via the
    /// OscillatorResources struct and shared across all slots that need them.
    /// If no resources are provided, fallback resources are created internally.
    void createAllSlots(OscillatorResources* resources) noexcept {
        OscillatorResources res;
        if (resources) {
            res = *resources;
        } else {
            // Create fallback resources for standalone use
            if (!fallbackWavetable_) {
                fallbackWavetable_ = std::make_unique<WavetableData>();
                generateMipmappedSaw(*fallbackWavetable_);
            }
            if (!fallbackMinBlep_) {
                fallbackMinBlep_ = std::make_unique<MinBlepTable>();
                fallbackMinBlep_->prepare();
            }
            res.wavetable = fallbackWavetable_.get();
            res.minBlepTable = fallbackMinBlep_.get();
        }

        slots_[static_cast<size_t>(OscType::PolyBLEP)] =
            std::make_unique<OscillatorAdapter<PolyBlepOscillator>>(res);

        slots_[static_cast<size_t>(OscType::Wavetable)] =
            std::make_unique<OscillatorAdapter<WavetableOscillator>>(res);

        slots_[static_cast<size_t>(OscType::PhaseDistortion)] =
            std::make_unique<OscillatorAdapter<PhaseDistortionOscillator>>(res);

        slots_[static_cast<size_t>(OscType::Sync)] =
            std::make_unique<OscillatorAdapter<SyncOscillator>>(res);

        slots_[static_cast<size_t>(OscType::Additive)] =
            std::make_unique<OscillatorAdapter<AdditiveOscillator>>(res);

        slots_[static_cast<size_t>(OscType::Chaos)] =
            std::make_unique<OscillatorAdapter<ChaosOscillator>>(res);

        slots_[static_cast<size_t>(OscType::Particle)] =
            std::make_unique<OscillatorAdapter<ParticleOscillator>>(res);

        slots_[static_cast<size_t>(OscType::Formant)] =
            std::make_unique<OscillatorAdapter<FormantOscillator>>(res);

        slots_[static_cast<size_t>(OscType::SpectralFreeze)] =
            std::make_unique<OscillatorAdapter<SpectralFreezeOscillator>>(res);

        slots_[static_cast<size_t>(OscType::Noise)] =
            std::make_unique<OscillatorAdapter<NoiseOscillator>>(res);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    /// @brief Pre-allocated oscillator slots (one per type).
    std::array<std::unique_ptr<OscillatorSlot>, kNumOscTypes> slots_;

    /// @brief Raw pointer to the currently active slot (non-owning).
    OscillatorSlot* active_{nullptr};

    OscType activeType_{OscType::PolyBLEP};
    PhaseMode phaseMode_{PhaseMode::Reset};
    float currentFrequency_{440.0f};
    double sampleRate_{44100.0};
    size_t maxBlockSize_{512};
    bool prepared_{false};

    /// @brief Fallback resources created when no external resources are provided.
    /// Used for standalone SelectableOscillator instances (e.g., in unit tests).
    std::unique_ptr<WavetableData> fallbackWavetable_;
    std::unique_ptr<MinBlepTable> fallbackMinBlep_;
};

} // namespace Krate::DSP
