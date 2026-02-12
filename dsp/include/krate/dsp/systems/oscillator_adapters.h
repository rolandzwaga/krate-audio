// ==============================================================================
// Layer 3: System Component - Oscillator Adapters
// ==============================================================================
// Thin adapter wrappers that adapt each oscillator type's unique API to the
// common OscillatorSlot virtual interface. Each adapter uses if-constexpr
// compile-time dispatch to handle API differences.
//
// Feature: 041-ruinae-voice-architecture
// Layer: 3 (Systems)
// Dependencies:
//   - Layer 0: core/math_constants.h
//   - Layer 1: primitives/polyblep_oscillator.h, primitives/wavetable_oscillator.h,
//              primitives/noise_oscillator.h, primitives/wavetable_generator.h,
//              primitives/minblep_table.h
//   - Layer 2: processors/phase_distortion_oscillator.h, processors/sync_oscillator.h,
//              processors/additive_oscillator.h, processors/chaos_oscillator.h,
//              processors/particle_oscillator.h, processors/formant_oscillator.h,
//              processors/spectral_freeze_oscillator.h
//   - Layer 3: systems/oscillator_slot.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in processBlock)
// - Principle III: Modern C++ (C++20, if constexpr, type traits)
// - Principle IX: Layer 3 (depends on Layers 0, 1, 2, 3)
// - Principle XIV: ODR Prevention (unique class names verified)
//
// Reference: specs/041-ruinae-voice-architecture/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/systems/oscillator_slot.h>

// Layer 0
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

#include <cmath>
#include <type_traits>

namespace Krate::DSP {

// =============================================================================
// Shared Oscillator Resources (FR-002)
// =============================================================================

/// @brief Shared resources for oscillator adapters.
///
/// WavetableData and MinBlepTable are shared across all oscillator slots
/// within a voice to avoid per-slot duplication.
struct OscillatorResources {
    WavetableData* wavetable{nullptr};
    const MinBlepTable* minBlepTable{nullptr};
};

// =============================================================================
// OscillatorAdapter Template (FR-002, FR-003, FR-004)
// =============================================================================

/// @brief Adapter wrapping a concrete oscillator type to the OscillatorSlot
///        virtual interface.
///
/// Uses if-constexpr to handle API differences between oscillator types:
/// - Frequency control: setFrequency vs setFundamental vs dual-freq vs no-op
/// - Prepare signature: (double) vs (double, size_t)
/// - Latency reporting: 0 vs FFT size
///
/// @tparam OscT The concrete oscillator type to wrap
template <typename OscT>
class OscillatorAdapter final : public OscillatorSlot {
public:
    OscillatorAdapter() noexcept = default;

    /// @brief Construct with shared resources.
    ///
    /// For SyncOscillator, re-initializes with the MinBlepTable pointer.
    /// For WavetableOscillator, stores the wavetable pointer for prepare().
    /// For other types, resources are ignored.
    explicit OscillatorAdapter(const OscillatorResources& resources) noexcept {
        if constexpr (std::is_same_v<OscT, SyncOscillator>) {
            if (resources.minBlepTable) {
                osc_ = SyncOscillator(resources.minBlepTable);
            }
        }
        if constexpr (std::is_same_v<OscT, WavetableOscillator>) {
            wavetable_ = resources.wavetable;
        }
    }

    ~OscillatorAdapter() noexcept override = default;

    // Non-copyable (some oscillator types are non-copyable)
    OscillatorAdapter(const OscillatorAdapter&) = delete;
    OscillatorAdapter& operator=(const OscillatorAdapter&) = delete;
    OscillatorAdapter(OscillatorAdapter&&) noexcept = default;
    OscillatorAdapter& operator=(OscillatorAdapter&&) noexcept = default;

    // =========================================================================
    // OscillatorSlot Interface Implementation
    // =========================================================================

    void prepare(double sampleRate, size_t maxBlockSize) noexcept override {
        sampleRate_ = sampleRate;

        // Types with FFT-based processing use reduced 1024-point FFT per voice
        if constexpr (std::is_same_v<OscT, AdditiveOscillator>) {
            osc_.prepare(sampleRate, 1024);
        } else if constexpr (std::is_same_v<OscT, SpectralFreezeOscillator>) {
            osc_.prepare(sampleRate, 1024);
            initSpectralFreeze();
        } else {
            osc_.prepare(sampleRate);
        }

        // Post-prepare setup for types with external resources
        if constexpr (std::is_same_v<OscT, WavetableOscillator>) {
            if (wavetable_) {
                osc_.setWavetable(wavetable_);
            }
        }
        if constexpr (std::is_same_v<OscT, SyncOscillator>) {
            osc_.setSlaveWaveform(OscWaveform::Sawtooth);
        }
    }

    void reset() noexcept override {
        osc_.reset();
    }

    void setFrequency(float hz) noexcept override {
        // Group A: Standard setFrequency
        if constexpr (std::is_same_v<OscT, PolyBlepOscillator> ||
                      std::is_same_v<OscT, WavetableOscillator> ||
                      std::is_same_v<OscT, PhaseDistortionOscillator> ||
                      std::is_same_v<OscT, ChaosOscillator> ||
                      std::is_same_v<OscT, ParticleOscillator>) {
            osc_.setFrequency(hz);
        }
        // Group B: setFundamental
        else if constexpr (std::is_same_v<OscT, AdditiveOscillator> ||
                           std::is_same_v<OscT, FormantOscillator>) {
            osc_.setFundamental(hz);
        }
        // Group C: Dual frequency (sync)
        else if constexpr (std::is_same_v<OscT, SyncOscillator>) {
            osc_.setMasterFrequency(hz);
            osc_.setSlaveFrequency(hz * 2.0f);
        }
        // Group D: No frequency control (SpectralFreeze, Noise)
        else {
            (void)hz;
        }
    }

    void processBlock(float* output, size_t numSamples) noexcept override {
        osc_.processBlock(output, numSamples);

        // Per-type gain compensation to equalize perceived loudness across
        // all oscillator types. Reference: PolyBLEP sawtooth at 440 Hz.
        // Measured RMS deltas and computed linear gain = 10^(deltaDb/20).
        // See selectable_oscillator_test.cpp "RMS levels" test for verification.
        constexpr float kGain = []() constexpr {
            if constexpr (std::is_same_v<OscT, ChaosOscillator>)
                return 10.0f;   // was 8.0; delta was -2.9 dB, now ~-1.0 dB
            else if constexpr (std::is_same_v<OscT, FormantOscillator>)
                return 2.7f;    // delta was -8.6 dB → +8.6 dB compensation
            else if constexpr (std::is_same_v<OscT, ParticleOscillator>)
                return 1.63f;   // delta was -4.3 dB → +4.3 dB compensation
            else if constexpr (std::is_same_v<OscT, WavetableOscillator>)
                return 1.47f;   // delta was -3.4 dB → +3.4 dB compensation
            else
                return 1.0f;    // no compensation needed
        }();

        if constexpr (kGain != 1.0f) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] *= kGain;
            }
        }
    }

    [[nodiscard]] size_t getLatencySamples() const noexcept override {
        if constexpr (std::is_same_v<OscT, AdditiveOscillator>) {
            return osc_.latency();
        } else if constexpr (std::is_same_v<OscT, SpectralFreezeOscillator>) {
            return osc_.getLatencySamples();
        } else {
            return 0;
        }
    }

    /// @brief Access the underlying oscillator for type-specific configuration.
    OscT& getOscillator() noexcept { return osc_; }
    const OscT& getOscillator() const noexcept { return osc_; }

private:
    // =========================================================================
    // SpectralFreeze Initialization Helper
    // =========================================================================

    /// @brief Feed a synthetic sine wave and freeze it for immediate output.
    void initSpectralFreeze() noexcept {
        if constexpr (std::is_same_v<OscT, SpectralFreezeOscillator>) {
            constexpr size_t kFreezeBlockSize = 2048;
            float sineBuffer[kFreezeBlockSize];
            constexpr float kFreq = 440.0f;
            for (size_t i = 0; i < kFreezeBlockSize; ++i) {
                sineBuffer[i] = std::sin(
                    kTwoPi * kFreq * static_cast<float>(i)
                    / static_cast<float>(sampleRate_));
            }
            osc_.freeze(sineBuffer, kFreezeBlockSize);
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    OscT osc_;
    double sampleRate_{44100.0};

    // Resource pointer for WavetableOscillator (non-owning, set at construction).
    // Unused by other oscillator types but only wastes 8 bytes per adapter.
    WavetableData* wavetable_{nullptr};
};

} // namespace Krate::DSP
