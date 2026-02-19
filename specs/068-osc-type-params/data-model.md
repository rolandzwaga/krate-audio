# Phase 1: Data Model - Oscillator Type-Specific Parameters

**Feature**: 068-osc-type-params
**Date**: 2026-02-19

## Entity: OscParam Enum

**Location**: `dsp/include/krate/dsp/systems/oscillator_types.h`
**Layer**: 3 (Systems)

```cpp
enum class OscParam : uint16_t {
    // PolyBLEP (Waveform/PulseWidth unique to PolyBLEP;
    // PhaseModulation/FrequencyModulation shared with Wavetable —
    // value persists when switching between PolyBLEP and Wavetable)
    Waveform = 0,
    PulseWidth,
    PhaseModulation,
    FrequencyModulation,

    // Phase Distortion
    PDWaveform = 10,
    PDDistortion,

    // Sync
    SyncSlaveRatio = 20,
    SyncSlaveWaveform,
    SyncMode,
    SyncAmount,
    SyncSlavePulseWidth,

    // Additive
    AdditiveNumPartials = 30,
    AdditiveSpectralTilt,
    AdditiveInharmonicity,

    // Chaos
    ChaosAttractor = 40,
    ChaosAmount,
    ChaosCoupling,
    ChaosOutput,

    // Particle
    ParticleScatter = 50,
    ParticleDensity,
    ParticleLifetime,
    ParticleSpawnMode,
    ParticleEnvType,
    ParticleDrift,

    // Formant
    FormantVowel = 60,
    FormantMorph,

    // Spectral Freeze
    SpectralPitchShift = 70,
    SpectralTilt,
    SpectralFormantShift,

    // Noise
    NoiseColor = 80,
};
```

**Validation**: Values are `uint16_t`, no overlap, grouped by type with 10-value gaps.
**State Transitions**: N/A (pure enum, no state).

## Entity: OscillatorSlot Extension

**Location**: `dsp/include/krate/dsp/systems/oscillator_slot.h`
**Layer**: 3 (Systems)

**New Method**:
```cpp
virtual void setParam(OscParam param, float value) noexcept { (void)param; (void)value; }
```

**Validation**: Base class is a silent no-op (FR-001). No allocation, no logging, no assertion.

## Entity: OscillatorAdapter<OscT>::setParam() Implementation

**Location**: `dsp/include/krate/dsp/systems/oscillator_adapters.h`
**Layer**: 3 (Systems)

**New Override Method**:
```cpp
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
    // Wavetable (shared PM/FM)
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
    // PhaseDistortion
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
                // value is the ratio (1.0-8.0), multiply by current master freq
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
    // SpectralFreeze
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
```

**Note on SyncOscillator**: The `SyncSlaveRatio` parameter needs special handling because the slave frequency must be expressed as `masterFreq * ratio`. The adapter needs to store the ratio as a member (`slaveRatio_`) and also update slave frequency when `setFrequency()` is called. The existing `setFrequency()` for SyncOscillator in `OscillatorAdapter` hardcodes `hz * 2.0f` -- this needs to change to use `slaveRatio_`.

## Entity: SelectableOscillator Extension

**Location**: `dsp/include/krate/dsp/systems/selectable_oscillator.h`
**Layer**: 3 (Systems)

**New Method**:
```cpp
void setParam(OscParam param, float value) noexcept {
    if (active_) {
        active_->setParam(param, value);
    }
}
```

## Entity: OscAParams / OscBParams Extension

**Location**: `plugins/ruinae/src/parameters/osc_a_params.h`, `osc_b_params.h`
**Layer**: Plugin

**New Fields (30 per struct)**:
```cpp
struct OscAParams {
    // ... existing fields ...

    // PolyBLEP (waveform/pulseWidth unique to PolyBLEP; phaseMod/freqMod shared with Wavetable)
    std::atomic<int> waveform{1};           // OscWaveform (default Sawtooth=1)
    std::atomic<float> pulseWidth{0.5f};    // 0.01-0.99
    std::atomic<float> phaseMod{0.0f};      // -1.0 to +1.0
    std::atomic<float> freqMod{0.0f};       // -1.0 to +1.0

    // Phase Distortion
    std::atomic<int> pdWaveform{0};         // PDWaveform (default Saw=0)
    std::atomic<float> pdDistortion{0.0f};  // 0.0-1.0

    // Sync
    std::atomic<float> syncRatio{2.0f};     // 1.0-8.0
    std::atomic<int> syncWaveform{1};       // OscWaveform (default Sawtooth=1)
    std::atomic<int> syncMode{0};           // SyncMode (default Hard=0)
    std::atomic<float> syncAmount{1.0f};    // 0.0-1.0
    std::atomic<float> syncPulseWidth{0.5f}; // 0.01-0.99

    // Additive
    std::atomic<int> additivePartials{16};  // 1-128
    std::atomic<float> additiveTilt{0.0f};  // -24 to +24 dB/oct
    std::atomic<float> additiveInharm{0.0f}; // 0.0-1.0

    // Chaos
    std::atomic<int> chaosAttractor{0};     // ChaosAttractor (default Lorenz=0)
    std::atomic<float> chaosAmount{0.5f};   // 0.0-1.0
    std::atomic<float> chaosCoupling{0.0f}; // 0.0-1.0
    std::atomic<int> chaosOutput{0};        // 0=X, 1=Y, 2=Z

    // Particle
    std::atomic<float> particleScatter{3.0f}; // 0.0-12.0 st
    std::atomic<float> particleDensity{16.0f}; // 1-64
    std::atomic<float> particleLifetime{200.0f}; // 5-2000 ms
    std::atomic<int> particleSpawnMode{0};  // SpawnMode (default Regular=0)
    std::atomic<int> particleEnvType{0};    // GrainEnvelopeType (default Hann=0)
    std::atomic<float> particleDrift{0.0f}; // 0.0-1.0

    // Formant
    std::atomic<int> formantVowel{0};       // Vowel (default A=0)
    std::atomic<float> formantMorph{0.0f};  // 0.0-4.0

    // Spectral Freeze
    std::atomic<float> spectralPitch{0.0f}; // -24 to +24 st
    std::atomic<float> spectralTilt{0.0f};  // -12 to +12 dB/oct
    std::atomic<float> spectralFormant{0.0f}; // -12 to +12 st

    // Noise
    std::atomic<int> noiseColor{0};         // NoiseColor (default White=0)
};
```

**Validation**: All defaults match the spec (FR-007, FR-012).

## Entity: Parameter ID Assignments

**Location**: `plugins/ruinae/src/plugin_ids.h`

IDs 110-139 for OSC A type-specific, 210-239 for OSC B (mirrored, offset +100).
See spec for the full ID table. 30 IDs per oscillator, fitting within the reserved 110-149 and 210-249 ranges.

## Entity: ParamID-to-OscParam Lookup Table

**Location**: `plugins/ruinae/src/parameters/osc_a_params.h` (inline constexpr)

```cpp
// Maps parameter ID offset (0-29) to OscParam enum value
// Index: paramId - 110 (OSC A) or paramId - 210 (OSC B)
inline constexpr Krate::DSP::OscParam kParamIdToOscParam[] = {
    Krate::DSP::OscParam::Waveform,           // 110/210
    Krate::DSP::OscParam::PulseWidth,          // 111/211
    Krate::DSP::OscParam::PhaseModulation,     // 112/212
    Krate::DSP::OscParam::FrequencyModulation, // 113/213
    Krate::DSP::OscParam::PDWaveform,          // 114/214
    Krate::DSP::OscParam::PDDistortion,        // 115/215
    Krate::DSP::OscParam::SyncSlaveRatio,      // 116/216
    Krate::DSP::OscParam::SyncSlaveWaveform,   // 117/217
    Krate::DSP::OscParam::SyncMode,            // 118/218
    Krate::DSP::OscParam::SyncAmount,          // 119/219
    Krate::DSP::OscParam::SyncSlavePulseWidth, // 120/220
    Krate::DSP::OscParam::AdditiveNumPartials, // 121/221
    Krate::DSP::OscParam::AdditiveSpectralTilt,// 122/222
    Krate::DSP::OscParam::AdditiveInharmonicity,// 123/223
    Krate::DSP::OscParam::ChaosAttractor,      // 124/224
    Krate::DSP::OscParam::ChaosAmount,         // 125/225
    Krate::DSP::OscParam::ChaosCoupling,       // 126/226
    Krate::DSP::OscParam::ChaosOutput,         // 127/227
    Krate::DSP::OscParam::ParticleScatter,     // 128/228
    Krate::DSP::OscParam::ParticleDensity,     // 129/229
    Krate::DSP::OscParam::ParticleLifetime,    // 130/230
    Krate::DSP::OscParam::ParticleSpawnMode,   // 131/231
    Krate::DSP::OscParam::ParticleEnvType,     // 132/232
    Krate::DSP::OscParam::ParticleDrift,       // 133/233
    Krate::DSP::OscParam::FormantVowel,        // 134/234
    Krate::DSP::OscParam::FormantMorph,        // 135/235
    Krate::DSP::OscParam::SpectralPitchShift,  // 136/236
    Krate::DSP::OscParam::SpectralTilt,        // 137/237
    Krate::DSP::OscParam::SpectralFormantShift,// 138/238
    Krate::DSP::OscParam::NoiseColor,          // 139/239
};
inline constexpr size_t kOscTypeSpecificParamCount = sizeof(kParamIdToOscParam) / sizeof(kParamIdToOscParam[0]);
```

## Entity: Dropdown String Arrays (New)

**Location**: `plugins/ruinae/src/parameters/dropdown_mappings.h`

New dropdown arrays needed:
- `kOscWaveformStrings[5]`: Sine/Sawtooth/Square/Pulse/Triangle (shared by PolyBLEP Waveform and Sync Slave Waveform — both use the same OscWaveform enum)
- `kPDWaveformStrings[8]`: Saw/Square/Pulse/DoubleSine/HalfSine/ResSaw/ResTri/ResTrap
- `kSyncModeStrings[3]`: Hard/Reverse/Phase Advance
- `kChaosAttractorStrings[5]`: Lorenz/Rossler/Chua/Duffing/Van der Pol
- `kChaosOutputStrings[3]`: X/Y/Z
- `kParticleSpawnModeStrings[3]`: Regular/Random/Burst
- `kParticleEnvTypeStrings[6]`: Hann/Trap/Sine/Blackman/Linear/Exp
- `kFormantVowelStrings[5]`: A/E/I/O/U
- `kNoiseColorStrings[6]`: White/Pink/Brown/Blue/Violet/Grey

## Relationships

```
OscillatorSlot (base)
    ^
    |  virtual setParam(OscParam, float)
    |
OscillatorAdapter<OscT> (template)
    |
    |  if constexpr dispatch to OscT setters
    |
SelectableOscillator
    |
    |  forwards setParam() to active_ slot
    |
RuinaeVoice
    |
    |  setOscAParam(OscParam, float) / setOscBParam(OscParam, float)
    |
RuinaeEngine
    |
    |  iterates all 16 voices
    |
Processor::applyParamsToEngine()
    |
    |  reads OscAParams/OscBParams atomics, maps to OscParam
    |
OscAParams / OscBParams (atomic storage)
    ^
    |  written by processParameterChanges()
    |
VST Host (normalized 0-1 values)
```
