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
#include <krate/dsp/core/pitch_utils.h>

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
#include <array>
#include <cmath>
#include <limits>
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
    /// Additive only (see "Additive level normalisation" below): the RMS of the
    /// normalised partial sum inside the oscillator, relative to one full-amplitude
    /// partial (RMS 0.806, peak 1.14). -5.7 dB, so the worst crest factor measured
    /// (4.52) peaks at 4.52 * 0.806 * 0.52 = 1.89, 5 % under the oscillator's +-2
    /// clamp. processBlock multiplies the output back up by 1 / kAdditiveLevel.
    static constexpr float kAdditiveLevel = 0.52f;

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

    void prepare(double sampleRate, size_t /*maxBlockSize*/) noexcept override {
        sampleRate_ = sampleRate;

        // Types with FFT-based processing use reduced 1024-point FFT per voice
        if constexpr (std::is_same_v<OscT, AdditiveOscillator>) {
            osc_.prepare(sampleRate, 1024);
            prepareAdditiveLevel();
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
            if constexpr (std::is_same_v<OscT, AdditiveOscillator>) {
                updateAdditiveLevel();  // the partials below Nyquist follow the pitch
            }
        }
        // Group C: Dual frequency (sync) -- use stored ratio instead of hardcoded 2x
        else if constexpr (std::is_same_v<OscT, SyncOscillator>) {
            osc_.setMasterFrequency(hz);
            osc_.setSlaveFrequency(hz * slaveRatio_);
        }
        // Group D: Frozen spectrum, pitched by shifting rather than by phase rate.
        // The frozen content is a fixed reference sine, so the played note is
        // expressed as a pitch shift away from that reference.
        else if constexpr (std::is_same_v<OscT, SpectralFreezeOscillator>) {
            applySpectralFreezePitch();
        }
        // Group E: No frequency control (Noise)
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
                    updateAdditiveLevel();
                    break;
                case OscParam::AdditiveSpectralTilt:
                    osc_.setSpectralTilt(value);
                    if (!detail::isNaN(value) && !detail::isInf(value)) {
                        additive_.tilt = std::clamp(value, kAdditiveMinTilt, kAdditiveMaxTilt);
                    }
                    updateAdditiveLevel();
                    break;
                case OscParam::AdditiveInharmonicity:
                    osc_.setInharmonicity(value);
                    if (!detail::isNaN(value) && !detail::isInf(value)) {
                        additive_.inharm = std::clamp(value, 0.0f, kAdditiveMaxInharm);
                    }
                    updateAdditiveLevel();
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
                    // An offset on top of note tracking, not a replacement for
                    // it -- applySpectralFreezePitch() sums the two.
                    spectralPitchOffsetSemitones_ = value;
                    applySpectralFreezePitch();
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
                case OscParam::NoiseColor: {
                    // setColor() resets the colouring filters on every call, and
                    // hosts re-send parameters every block: forwarding an
                    // unchanged colour restarted brown noise from zero each
                    // block, so its level followed the block size.
                    const auto color = static_cast<NoiseColor>(static_cast<int>(value));
                    if (color != osc_.color()) {
                        osc_.setColor(color);
                    }
                    break;
                }
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
                return 2.0f;    // recalibrated: chaos now produces audio-rate output
            else if constexpr (std::is_same_v<OscT, FormantOscillator>)
                return 2.7f;    // delta was -8.6 dB → +8.6 dB compensation
            else if constexpr (std::is_same_v<OscT, ParticleOscillator>)
                return 1.63f;   // delta was -4.3 dB → +4.3 dB compensation
            else if constexpr (std::is_same_v<OscT, WavetableOscillator>)
                return 1.47f;   // delta was -3.4 dB → +3.4 dB compensation
            else if constexpr (std::is_same_v<OscT, AdditiveOscillator>)
                return 1.0f / kAdditiveLevel;  // undoes the in-oscillator headroom (see kAdditiveLevel):
                                               // one partial plays as before, any count at its RMS
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

    /// @brief Pitch of the synthetic spectrum frozen at prepare time.
    ///
    /// Note tracking is expressed relative to this, so the two must agree.
    static constexpr float kSpectralFreezeReferenceHz = 440.0f;

    /// @brief Feed a synthetic sine wave and freeze it for immediate output.
    void initSpectralFreeze() noexcept {
        if constexpr (std::is_same_v<OscT, SpectralFreezeOscillator>) {
            constexpr size_t kFreezeBlockSize = 2048;
            float sineBuffer[kFreezeBlockSize];
            for (size_t i = 0; i < kFreezeBlockSize; ++i) {
                sineBuffer[i] = std::sin(
                    kTwoPi * kSpectralFreezeReferenceHz * static_cast<float>(i)
                    / static_cast<float>(sampleRate_));
            }
            osc_.freeze(sineBuffer, kFreezeBlockSize);
            applySpectralFreezePitch();
        }
    }

    /// @brief Drive the frozen spectrum to the played note, plus any offset.
    ///
    /// The frozen content cannot be re-captured per note: freeze() restarts the
    /// overlap-add pipeline, and setFrequency() is called every block for glide
    /// and pitch modulation, so re-freezing would click and cost an FFT per
    /// block. Shifting the existing spectrum is both cheap and continuous.
    void applySpectralFreezePitch() noexcept {
        if constexpr (std::is_same_v<OscT, SpectralFreezeOscillator>) {
            // Guard the logarithm; a voice can be left at 0 Hz between notes.
            const float noteSemitones =
                (currentFrequency_ > 0.0f)
                    ? ratioToSemitones(currentFrequency_ / kSpectralFreezeReferenceHz)
                    : 0.0f;
            // setPitchShift clamps, so an extreme sum flattens out rather than
            // producing a nonsense ratio.
            osc_.setPitchShift(noteSemitones + spectralPitchOffsetSemitones_);
        }
    }

    // =========================================================================
    // Additive level normalisation
    // =========================================================================
    //
    // AdditiveOscillator gives every partial amplitude 1 (times the tilt) and
    // hard-clamps the sum at +-2, so above one partial the raw oscillator clips
    // (128 partials at +4 dB/oct: 96 % of samples on the clamp). The adapter
    // scales every partial by g = kAdditiveLevel / sqrt(sum of t_n^2) over the
    // partials that sound (t_n = the tilt factor; partials at or above Nyquist
    // are skipped by the oscillator). Partials at distinct frequencies add in
    // power, so the sum has the same RMS at any count and tilt: kAdditiveLevel
    // times that of one full-amplitude partial.
    //
    // The partials start at quadratic phases (phi_n = frac(n^2 * golden / 2)),
    // which spread their peaks instead of lining them up at the start of each
    // cycle as zero phases do. Over 1-128 partials, tilt -24..+12 dB/oct and
    // 65-1047 Hz, the worst crest factor measured over 2 s was 3.5 for harmonic
    // partials and 4.52 with inharmonicity (whose drifting phases tend to random
    // ones); with zero phases it was 6.9.

    /// Tilt clamp of AdditiveOscillator::setSpectralTilt / setInharmonicity.
    static constexpr float kAdditiveMinTilt = -24.0f;
    static constexpr float kAdditiveMaxTilt = 12.0f;
    static constexpr float kAdditiveMaxInharm = 0.1f;


    void prepareAdditiveLevel() noexcept {
        if constexpr (std::is_same_v<OscT, AdditiveOscillator>) {
            constexpr auto kMax = AdditiveOscillator::kMaxPartials;
            constexpr double kHalfGolden = 0.5 * 0.6180339887498949;
            for (size_t n = 1; n <= kMax; ++n) {
                const double phase = static_cast<double>(n * n) * kHalfGolden;
                osc_.setPartialPhase(n, static_cast<float>(phase - std::floor(phase)));
            }
            osc_.reset();  // initial phases take effect at reset()
            additive_.tiltForSums = std::numeric_limits<float>::lowest();  // force a rebuild
            additive_.appliedGain = -1.0f;
            updateAdditiveLevel();
        }
    }

    void updateAdditiveLevel() noexcept {
        if constexpr (std::is_same_v<OscT, AdditiveOscillator>) {
            auto& a = additive_;
            constexpr auto kMax = AdditiveOscillator::kMaxPartials;
            if (a.tilt != a.tiltForSums) {
                // Prefix sums of t_n^2, t_n = 10^(tilt * log2(n) / 20) as the oscillator applies it.
                a.tiltSqPrefix[0] = 0.0f;
                for (size_t n = 1; n <= kMax; ++n) {
                    const float t = (a.tilt == 0.0f || n == 1)
                        ? 1.0f
                        : std::pow(10.0f, a.tilt * std::log2(static_cast<float>(n)) / 20.0f);
                    a.tiltSqPrefix[n] = a.tiltSqPrefix[n - 1] + t * t;
                }
                a.tiltForSums = a.tilt;
            }

            // Partials that sound: the first numPartials whose (stretched)
            // frequency is below Nyquist; frequency rises with n, so stop at the first one above.
            const float f0 = osc_.fundamental();
            const size_t count = std::min(osc_.numPartials(), kMax);
            if (f0 != a.countedF0 || count != a.countedLimit || a.inharm != a.countedInharm) {
                const float nyquist = static_cast<float>(sampleRate_ * 0.5);
                size_t sounding = 0;
                while (sounding < count) {
                    const auto n = static_cast<float>(sounding + 1);
                    if (n * f0 * std::sqrt(1.0f + a.inharm * n * n) >= nyquist) break;
                    ++sounding;
                }
                a.sounding = sounding;
                a.countedF0 = f0;
                a.countedLimit = count;
                a.countedInharm = a.inharm;
            }
            const size_t sounding = a.sounding;

            const float gain = (sounding > 0) ? kAdditiveLevel / std::sqrt(a.tiltSqPrefix[sounding])
                                              : kAdditiveLevel;
            if (gain != a.appliedGain) {
                for (size_t n = 1; n <= kMax; ++n) osc_.setPartialAmplitude(n, gain);
                a.appliedGain = gain;
            }
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

    // SpectralFreeze pitch offset from OscParam::SpectralPitchShift, summed with
    // note tracking by applySpectralFreezePitch(). Unused by other types.
    float spectralPitchOffsetSemitones_{0.0f};

    // Resource pointer for WavetableOscillator (non-owning, set at construction).
    // Unused by other oscillator types but only wastes 8 bytes per adapter.
    WavetableData* wavetable_{nullptr};

    // Additive level normalisation state (updateAdditiveLevel); an empty struct for other types.
    struct AdditiveLevel {
        float tilt{0.0f};      ///< spectral tilt in dB/oct, clamped as the oscillator clamps it
        float inharm{0.0f};    ///< inharmonicity, clamped as the oscillator clamps it
        float tiltForSums{std::numeric_limits<float>::lowest()};  ///< tilt the prefix sums hold
        std::array<float, AdditiveOscillator::kMaxPartials + 1> tiltSqPrefix{};  ///< sum of t_n^2 for n <= i
        float countedF0{-1.0f};    ///< inputs of the cached sounding count
        size_t countedLimit{0};
        float countedInharm{-1.0f};
        size_t sounding{0};        ///< partials below Nyquist
        float appliedGain{-1.0f};  ///< amplitude last written to every partial
    };
    struct NoAdditiveLevel {};
    std::conditional_t<std::is_same_v<OscT, AdditiveOscillator>, AdditiveLevel, NoAdditiveLevel> additive_{};
};

} // namespace Krate::DSP
