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
        currentFrequency_ = hz;

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
        // Group C: Dual frequency (sync) -- use stored ratio instead of hardcoded 2x
        else if constexpr (std::is_same_v<OscT, SyncOscillator>) {
            osc_.setMasterFrequency(hz);
            osc_.setSlaveFrequency(hz * slaveRatio_);
        }
        // Group D: No frequency control (SpectralFreeze, Noise)
        else {
            (void)hz;
        }
    }

    void setParam(OscParam param, float value) noexcept override {
        // PolyBLEP
        if constexpr (std::is_same_v<OscT, PolyBlepOscillator>) {
            switch (param) {
                case OscParam::Waveform:
                    osc_.setWaveform(static_cast<OscWaveform>(static_cast<int>(value)));
                    break;
                case OscParam::PulseWidth:
                    osc_.setPulseWidth(value);
                    break;
                case OscParam::PhaseModulation:
                    osc_.setPhaseModulation(value);
                    break;
                case OscParam::FrequencyModulation:
                    osc_.setFrequencyModulation(value);
                    break;
                default: break;
            }
        }
        // Wavetable (shared PM/FM with PolyBLEP)
        else if constexpr (std::is_same_v<OscT, WavetableOscillator>) {
            switch (param) {
                case OscParam::PhaseModulation:
                    osc_.setPhaseModulation(value);
                    break;
                case OscParam::FrequencyModulation:
                    osc_.setFrequencyModulation(value);
                    break;
                default: break;
            }
        }
        // Phase Distortion
        else if constexpr (std::is_same_v<OscT, PhaseDistortionOscillator>) {
            switch (param) {
                case OscParam::PDWaveform:
                    osc_.setWaveform(static_cast<PDWaveform>(static_cast<int>(value)));
                    break;
                case OscParam::PDDistortion:
                    osc_.setDistortion(value);
                    break;
                default: break;
            }
        }
        // Sync
        else if constexpr (std::is_same_v<OscT, SyncOscillator>) {
            switch (param) {
                case OscParam::SyncSlaveRatio:
                    slaveRatio_ = value;
                    osc_.setSlaveFrequency(currentFrequency_ * value);
                    break;
                case OscParam::SyncSlaveWaveform:
                    osc_.setSlaveWaveform(static_cast<OscWaveform>(static_cast<int>(value)));
                    break;
                case OscParam::SyncMode:
                    osc_.setSyncMode(static_cast<SyncMode>(static_cast<int>(value)));
                    break;
                case OscParam::SyncAmount:
                    osc_.setSyncAmount(value);
                    break;
                case OscParam::SyncSlavePulseWidth:
                    osc_.setSlavePulseWidth(value);
                    break;
                default: break;
            }
        }
        // Additive
        else if constexpr (std::is_same_v<OscT, AdditiveOscillator>) {
            switch (param) {
                case OscParam::AdditiveNumPartials:
                    osc_.setNumPartials(static_cast<size_t>(value));
                    break;
                case OscParam::AdditiveSpectralTilt:
                    osc_.setSpectralTilt(value);
                    break;
                case OscParam::AdditiveInharmonicity:
                    osc_.setInharmonicity(value);
                    break;
                default: break;
            }
        }
        // Chaos
        else if constexpr (std::is_same_v<OscT, ChaosOscillator>) {
            switch (param) {
                case OscParam::ChaosAttractor:
                    osc_.setAttractor(static_cast<ChaosAttractor>(static_cast<int>(value)));
                    break;
                case OscParam::ChaosAmount:
                    osc_.setChaos(value);
                    break;
                case OscParam::ChaosCoupling:
                    osc_.setCoupling(value);
                    break;
                case OscParam::ChaosOutput:
                    osc_.setOutput(static_cast<size_t>(value));
                    break;
                default: break;
            }
        }
        // Particle
        else if constexpr (std::is_same_v<OscT, ParticleOscillator>) {
            switch (param) {
                case OscParam::ParticleScatter:
                    osc_.setFrequencyScatter(value);
                    break;
                case OscParam::ParticleDensity:
                    osc_.setDensity(value);
                    break;
                case OscParam::ParticleLifetime:
                    osc_.setLifetime(value);
                    break;
                case OscParam::ParticleSpawnMode:
                    osc_.setSpawnMode(static_cast<SpawnMode>(static_cast<int>(value)));
                    break;
                case OscParam::ParticleEnvType:
                    osc_.setEnvelopeType(static_cast<GrainEnvelopeType>(static_cast<int>(value)));
                    break;
                case OscParam::ParticleDrift:
                    osc_.setDriftAmount(value);
                    break;
                default: break;
            }
        }
        // Formant
        else if constexpr (std::is_same_v<OscT, FormantOscillator>) {
            switch (param) {
                case OscParam::FormantVowel:
                    osc_.setVowel(static_cast<Vowel>(static_cast<int>(value)));
                    break;
                case OscParam::FormantMorph:
                    osc_.setMorphPosition(value);
                    break;
                default: break;
            }
        }
        // Spectral Freeze
        else if constexpr (std::is_same_v<OscT, SpectralFreezeOscillator>) {
            switch (param) {
                case OscParam::SpectralPitchShift:
                    osc_.setPitchShift(value);
                    break;
                case OscParam::SpectralTilt:
                    osc_.setSpectralTilt(value);
                    break;
                case OscParam::SpectralFormantShift:
                    osc_.setFormantShift(value);
                    break;
                default: break;
            }
        }
        // Noise
        else if constexpr (std::is_same_v<OscT, NoiseOscillator>) {
            switch (param) {
                case OscParam::NoiseColor:
                    osc_.setColor(static_cast<NoiseColor>(static_cast<int>(value)));
                    break;
                default: break;
            }
        }
        // Other types: silent no-op (base class default)
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
    float currentFrequency_{440.0f};

    // SyncOscillator slave-to-master frequency ratio (default 2x).
    // Used by setFrequency() and setParam(SyncSlaveRatio).
    float slaveRatio_{2.0f};

    // Resource pointer for WavetableOscillator (non-owning, set at construction).
    // Unused by other oscillator types but only wastes 8 bytes per adapter.
    WavetableData* wavetable_{nullptr};
};

} // namespace Krate::DSP
