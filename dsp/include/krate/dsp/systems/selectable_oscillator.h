// ==============================================================================
// Layer 3: System Component - SelectableOscillator
// ==============================================================================
// Variant-based oscillator wrapper with lazy initialization for the Ruinae
// voice architecture. Wraps all 10 oscillator types in a std::variant and
// delegates processing via visitor dispatch.
//
// Feature: 041-ruinae-voice-architecture
// Layer: 3 (Systems)
// Dependencies:
//   - Layer 0: core/db_utils.h (isNaN, isInf)
//   - Layer 1: primitives/polyblep_oscillator.h, primitives/wavetable_oscillator.h,
//              primitives/noise_oscillator.h
//   - Layer 2: processors/phase_distortion_oscillator.h, processors/sync_oscillator.h,
//              processors/additive_oscillator.h, processors/chaos_oscillator.h,
//              processors/particle_oscillator.h, processors/formant_oscillator.h,
//              processors/spectral_freeze_oscillator.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in processBlock)
// - Principle III: Modern C++ (C++20, std::variant, visitor dispatch)
// - Principle IX: Layer 3 (depends on Layers 0, 1, 2)
// - Principle XIV: ODR Prevention (unique class name verified)
//
// Reference: specs/041-ruinae-voice-architecture/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/systems/ruinae_types.h>

// Layer 0
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

// Layer 1 oscillators
#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/primitives/wavetable_oscillator.h>
#include <krate/dsp/primitives/wavetable_generator.h>
#include <krate/dsp/primitives/minblep_table.h>
#include <krate/dsp/primitives/noise_oscillator.h>

// Layer 2 oscillators
#include <krate/dsp/processors/phase_distortion_oscillator.h>
#include <krate/dsp/processors/sync_oscillator.h>
#include <krate/dsp/processors/additive_oscillator.h>
#include <krate/dsp/processors/chaos_oscillator.h>
#include <krate/dsp/processors/particle_oscillator.h>
#include <krate/dsp/processors/formant_oscillator.h>
#include <krate/dsp/processors/spectral_freeze_oscillator.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

namespace Krate::DSP {

// =============================================================================
// OscillatorVariant Type Alias (FR-002, FR-003)
// =============================================================================

/// @brief Type-erased oscillator storage using std::variant.
///
/// std::monostate represents the unprepared/empty state.
/// Only the active oscillator type is initialized at any time (lazy init).
using OscillatorVariant = std::variant<
    std::monostate,
    PolyBlepOscillator,
    WavetableOscillator,
    PhaseDistortionOscillator,
    SyncOscillator,
    AdditiveOscillator,
    ChaosOscillator,
    ParticleOscillator,
    FormantOscillator,
    SpectralFreezeOscillator,
    NoiseOscillator
>;

// =============================================================================
// SelectableOscillator Class (FR-001 through FR-005)
// =============================================================================

/// @brief Variant-based oscillator wrapper with lazy initialization.
///
/// Wraps all 10 oscillator types in a std::variant and delegates processing
/// via visitor dispatch, following the DistortionRack pattern.
///
/// @par Lazy Initialization
/// On prepare(), only the default type (PolyBLEP) is constructed and prepared.
/// When setType() is called with a different type, the new type is emplaced
/// and prepared on-the-fly. Non-FFT types cause zero heap allocation.
///
/// @par Thread Safety
/// Single-threaded model. All methods called from the audio thread.
///
/// @par Real-Time Safety
/// processBlock() is fully real-time safe for non-FFT oscillator types.
/// prepare() is NOT real-time safe.
class SelectableOscillator {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    SelectableOscillator() noexcept = default;
    ~SelectableOscillator() noexcept = default;

    // Non-copyable (some variant alternatives are non-copyable)
    SelectableOscillator(const SelectableOscillator&) = delete;
    SelectableOscillator& operator=(const SelectableOscillator&) = delete;
    SelectableOscillator(SelectableOscillator&&) noexcept = default;
    SelectableOscillator& operator=(SelectableOscillator&&) noexcept = default;

    /// @brief Initialize the oscillator for the given sample rate and block size.
    ///
    /// Constructs and prepares the default oscillator type (PolyBLEP).
    /// NOT real-time safe.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size in samples
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        prepared_ = true;

        // Construct and prepare default type
        emplaceAndPrepare(activeType_);
        applyFrequency();
    }

    /// @brief Reset the active oscillator state without changing type.
    void reset() noexcept {
        std::visit(ResetVisitor{}, oscillator_);
    }

    // =========================================================================
    // Type Selection (FR-002, FR-003, FR-004, FR-005)
    // =========================================================================

    /// @brief Set the active oscillator type.
    ///
    /// If the type is the same as the current type, this is a no-op (AS-3.1).
    /// Otherwise, the new type is emplaced and prepared.
    /// The current frequency setting is preserved.
    ///
    /// @param type The oscillator type to activate
    void setType(OscType type) noexcept {
        // No-op if same type (AS-3.1)
        if (type == activeType_ && !std::holds_alternative<std::monostate>(oscillator_)) {
            return;
        }

        activeType_ = type;

        if (!prepared_) {
            return;
        }

        emplaceAndPrepare(type);

        // Optionally reset phase (but NOT for SpectralFreeze, which was just
        // freeze-initialized in emplaceAndPrepare and reset() would clear
        // its frozen state)
        if (phaseMode_ == PhaseMode::Reset && type != OscType::SpectralFreeze) {
            std::visit(ResetVisitor{}, oscillator_);
        }

        // Restore frequency
        applyFrequency();
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
        applyFrequency();
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
        if (!prepared_ || output == nullptr || numSamples == 0) {
            if (output != nullptr) {
                std::fill(output, output + numSamples, 0.0f);
            }
            return;
        }

        // Clamp to max block size to prevent buffer overruns
        numSamples = std::min(numSamples, maxBlockSize_);

        std::visit(ProcessBlockVisitor{output, numSamples}, oscillator_);
    }

private:
    // =========================================================================
    // Visitor Structs
    // =========================================================================

    /// @brief Visitor for resetting oscillator state.
    struct ResetVisitor {
        void operator()(std::monostate&) const noexcept {}

        void operator()(PolyBlepOscillator& osc) const noexcept { osc.reset(); }
        void operator()(WavetableOscillator& osc) const noexcept { osc.reset(); }
        void operator()(PhaseDistortionOscillator& osc) const noexcept { osc.reset(); }
        void operator()(SyncOscillator& osc) const noexcept { osc.reset(); }
        void operator()(AdditiveOscillator& osc) const noexcept { osc.reset(); }
        void operator()(ChaosOscillator& osc) const noexcept { osc.reset(); }
        void operator()(ParticleOscillator& osc) const noexcept { osc.reset(); }
        void operator()(FormantOscillator& osc) const noexcept { osc.reset(); }
        void operator()(SpectralFreezeOscillator& osc) const noexcept { osc.reset(); }
        void operator()(NoiseOscillator& osc) const noexcept { osc.reset(); }
    };

    /// @brief Visitor for preparing oscillators.
    struct PrepareVisitor {
        double sampleRate;
        size_t maxBlockSize;

        void operator()(std::monostate&) const noexcept {}

        void operator()(PolyBlepOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
        void operator()(WavetableOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
        void operator()(PhaseDistortionOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
        void operator()(SyncOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
        void operator()(AdditiveOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
        void operator()(ChaosOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
        void operator()(ParticleOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
        void operator()(FormantOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
        void operator()(SpectralFreezeOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
        void operator()(NoiseOscillator& osc) const noexcept {
            osc.prepare(sampleRate);
        }
    };

    /// @brief Visitor for setting frequency.
    struct SetFrequencyVisitor {
        float hz;

        void operator()(std::monostate&) const noexcept {}

        void operator()(PolyBlepOscillator& osc) const noexcept {
            osc.setFrequency(hz);
        }
        void operator()(WavetableOscillator& osc) const noexcept {
            osc.setFrequency(hz);
        }
        void operator()(PhaseDistortionOscillator& osc) const noexcept {
            osc.setFrequency(hz);
        }
        void operator()(SyncOscillator& osc) const noexcept {
            // SyncOscillator uses setMasterFrequency + setSlaveFrequency
            osc.setMasterFrequency(hz);
            osc.setSlaveFrequency(hz * 2.0f);  // Default: slave at 2x master
        }
        void operator()(AdditiveOscillator& osc) const noexcept {
            osc.setFundamental(hz);
        }
        void operator()(ChaosOscillator& osc) const noexcept {
            osc.setFrequency(hz);
        }
        void operator()(ParticleOscillator& osc) const noexcept {
            osc.setFrequency(hz);
        }
        void operator()(FormantOscillator& osc) const noexcept {
            osc.setFundamental(hz);
        }
        void operator()(SpectralFreezeOscillator& osc) const noexcept {
            // SpectralFreezeOscillator uses setPitchShift(semitones)
            // We map frequency to a pitch shift relative to the fundamental
            // For standard usage: no pitch shift (0 semitones)
            (void)osc;  // No direct frequency control; pitch shift handled separately
        }
        void operator()(NoiseOscillator&) const noexcept {
            // NoiseOscillator has no frequency control
        }
    };

    /// @brief Visitor for block processing.
    struct ProcessBlockVisitor {
        float* output;
        size_t numSamples;

        void operator()(std::monostate&) const noexcept {
            std::fill(output, output + numSamples, 0.0f);
        }

        void operator()(PolyBlepOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
        void operator()(WavetableOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
        void operator()(PhaseDistortionOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
        void operator()(SyncOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
        void operator()(AdditiveOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
        void operator()(ChaosOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
        void operator()(ParticleOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
        void operator()(FormantOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
        void operator()(SpectralFreezeOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
        void operator()(NoiseOscillator& osc) const noexcept {
            osc.processBlock(output, numSamples);
        }
    };

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Emplace a new oscillator type and prepare it.
    ///
    /// Special handling for types that need extra initialization:
    /// - Wavetable: generates a default sawtooth wavetable
    /// - Sync: sets slave waveform to sawtooth
    /// - SpectralFreeze: feeds a synthetic sine to freeze
    void emplaceAndPrepare(OscType type) noexcept {
        switch (type) {
            case OscType::PolyBLEP:
                oscillator_.emplace<PolyBlepOscillator>();
                break;
            case OscType::Wavetable: {
                // Create default wavetable if not already available
                if (!defaultWavetable_) {
                    defaultWavetable_ = std::make_unique<WavetableData>();
                    generateMipmappedSaw(*defaultWavetable_);
                }
                auto& wt = oscillator_.emplace<WavetableOscillator>();
                wt.prepare(sampleRate_);
                wt.setWavetable(defaultWavetable_.get());
                return;  // Already prepared via special path
            }
            case OscType::PhaseDistortion:
                oscillator_.emplace<PhaseDistortionOscillator>();
                break;
            case OscType::Sync: {
                // Create MinBlepTable if not already available
                if (!minBlepTable_) {
                    minBlepTable_ = std::make_unique<MinBlepTable>();
                    minBlepTable_->prepare();
                }
                auto& sync = oscillator_.emplace<SyncOscillator>(minBlepTable_.get());
                sync.prepare(sampleRate_);
                sync.setSlaveWaveform(OscWaveform::Sawtooth);
                return;  // Already prepared via special path
            }
            case OscType::Additive:
                oscillator_.emplace<AdditiveOscillator>();
                break;
            case OscType::Chaos:
                oscillator_.emplace<ChaosOscillator>();
                break;
            case OscType::Particle:
                oscillator_.emplace<ParticleOscillator>();
                break;
            case OscType::Formant:
                oscillator_.emplace<FormantOscillator>();
                break;
            case OscType::SpectralFreeze: {
                auto& sf = oscillator_.emplace<SpectralFreezeOscillator>();
                sf.prepare(sampleRate_);
                // Feed a synthetic sine wave and freeze it so the oscillator
                // produces output immediately
                initSpectralFreeze(sf);
                return;  // Already prepared via special path
            }
            case OscType::Noise:
                oscillator_.emplace<NoiseOscillator>();
                break;
            default:
                oscillator_.emplace<PolyBlepOscillator>();
                break;
        }

        // Prepare the newly emplaced oscillator (generic path)
        std::visit(PrepareVisitor{sampleRate_, maxBlockSize_}, oscillator_);
    }

    /// @brief Initialize SpectralFreezeOscillator by feeding it a sine wave and freezing.
    void initSpectralFreeze(SpectralFreezeOscillator& sf) noexcept {
        // Generate a short sine wave buffer and freeze it.
        // Use a fixed-size stack buffer matching the default FFT size.
        constexpr size_t kFreezeBlockSize = 2048;
        float sineBuffer[kFreezeBlockSize];
        const float freq = currentFrequency_;
        const double sr = sampleRate_;
        for (size_t i = 0; i < kFreezeBlockSize; ++i) {
            sineBuffer[i] = std::sin(kTwoPi * freq * static_cast<float>(i)
                                     / static_cast<float>(sr));
        }
        sf.freeze(sineBuffer, kFreezeBlockSize);
    }

    /// @brief Apply the current frequency to the active oscillator.
    void applyFrequency() noexcept {
        std::visit(SetFrequencyVisitor{currentFrequency_}, oscillator_);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    OscillatorVariant oscillator_;
    OscType activeType_{OscType::PolyBLEP};
    PhaseMode phaseMode_{PhaseMode::Reset};
    float currentFrequency_{440.0f};
    double sampleRate_{44100.0};
    size_t maxBlockSize_{512};
    bool prepared_{false};

    /// @brief Default wavetable for Wavetable oscillator type.
    /// Lazily created on first use of OscType::Wavetable.
    std::unique_ptr<WavetableData> defaultWavetable_;

    /// @brief MinBLEP table for Sync oscillator type.
    /// Lazily created on first use of OscType::Sync.
    std::unique_ptr<MinBlepTable> minBlepTable_;
};

} // namespace Krate::DSP
