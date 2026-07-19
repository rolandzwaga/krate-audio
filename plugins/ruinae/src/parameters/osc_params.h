#pragma once

// ==============================================================================
// Oscillator Parameters (shared by OSC A, ID 100-199, and OSC B, ID 200-299)
// ==============================================================================
// One implementation of the oscillator parameter surface, keyed off the block
// base ParamID. osc_a_params.h and osc_b_params.h are thin shims that bind the
// base and the display prefix, so call sites keep their existing names.

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <krate/dsp/systems/oscillator_types.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// Parameter Offsets Within an Oscillator's ID Block
// =============================================================================
// OSC A and OSC B lay out their parameters identically -- every kOscB*Id is
// exactly kOscA*Id + 100 -- so one implementation keyed off the block base
// serves both banks. The static_asserts below make that assumption a build
// error rather than a silent misroute if the two blocks ever diverge.

enum OscParamOffset : Steinberg::Vst::ParamID {
    kOscTypeOffset = 0,
    kOscTuneOffset = 1,
    kOscFineOffset = 2,
    kOscLevelOffset = 3,
    kOscPhaseOffset = 4,
    kOscWaveformOffset = 10,
    kOscPulseWidthOffset = 11,
    kOscPhaseModOffset = 12,
    kOscFreqModOffset = 13,
    kOscPDWaveformOffset = 14,
    kOscPDDistortionOffset = 15,
    kOscSyncRatioOffset = 16,
    kOscSyncWaveformOffset = 17,
    kOscSyncModeOffset = 18,
    kOscSyncAmountOffset = 19,
    kOscSyncPulseWidthOffset = 20,
    kOscAdditivePartialsOffset = 21,
    kOscAdditiveTiltOffset = 22,
    kOscAdditiveInharmOffset = 23,
    kOscChaosAttractorOffset = 24,
    kOscChaosAmountOffset = 25,
    kOscChaosCouplingOffset = 26,
    kOscChaosOutputOffset = 27,
    kOscParticleScatterOffset = 28,
    kOscParticleDensityOffset = 29,
    kOscParticleLifetimeOffset = 30,
    kOscParticleSpawnModeOffset = 31,
    kOscParticleEnvTypeOffset = 32,
    kOscParticleDriftOffset = 33,
    kOscFormantVowelOffset = 34,
    kOscFormantMorphOffset = 35,
    kOscSpectralPitchOffset = 36,
    kOscSpectralTiltOffset = 37,
    kOscSpectralFormantOffset = 38,
    kOscNoiseColorOffset = 39,
};

static_assert(kOscBTypeId - kOscATypeId == kOscBNoiseColorId - kOscANoiseColorId,
              "OSC A and OSC B parameter blocks must be identically spaced");
static_assert(kOscATypeId + kOscNoiseColorOffset == kOscANoiseColorId,
              "OscParamOffset must match the OSC A id block");
static_assert(kOscBTypeId + kOscNoiseColorOffset == kOscBNoiseColorId,
              "OscParamOffset must match the OSC B id block");

// =============================================================================
// ParamID-to-OscParam Lookup Table (shared by OSC A and OSC B)
// =============================================================================
// Maps parameter ID offset (0-29) to OscParam enum value.
// Index: paramId - 110 (OSC A) or paramId - 210 (OSC B)

inline constexpr Krate::DSP::OscParam kParamIdToOscParam[] = {
    Krate::DSP::OscParam::Waveform,            // offset 0  -> 110/210
    Krate::DSP::OscParam::PulseWidth,           // offset 1  -> 111/211
    Krate::DSP::OscParam::PhaseModulation,      // offset 2  -> 112/212
    Krate::DSP::OscParam::FrequencyModulation,  // offset 3  -> 113/213
    Krate::DSP::OscParam::PDWaveform,           // offset 4  -> 114/214
    Krate::DSP::OscParam::PDDistortion,         // offset 5  -> 115/215
    Krate::DSP::OscParam::SyncSlaveRatio,       // offset 6  -> 116/216
    Krate::DSP::OscParam::SyncSlaveWaveform,    // offset 7  -> 117/217
    Krate::DSP::OscParam::SyncMode,             // offset 8  -> 118/218
    Krate::DSP::OscParam::SyncAmount,           // offset 9  -> 119/219
    Krate::DSP::OscParam::SyncSlavePulseWidth,  // offset 10 -> 120/220
    Krate::DSP::OscParam::AdditiveNumPartials,  // offset 11 -> 121/221
    Krate::DSP::OscParam::AdditiveSpectralTilt, // offset 12 -> 122/222
    Krate::DSP::OscParam::AdditiveInharmonicity,// offset 13 -> 123/223
    Krate::DSP::OscParam::ChaosAttractor,       // offset 14 -> 124/224
    Krate::DSP::OscParam::ChaosAmount,          // offset 15 -> 125/225
    Krate::DSP::OscParam::ChaosCoupling,        // offset 16 -> 126/226
    Krate::DSP::OscParam::ChaosOutput,          // offset 17 -> 127/227
    Krate::DSP::OscParam::ParticleScatter,      // offset 18 -> 128/228
    Krate::DSP::OscParam::ParticleDensity,      // offset 19 -> 129/229
    Krate::DSP::OscParam::ParticleLifetime,     // offset 20 -> 130/230
    Krate::DSP::OscParam::ParticleSpawnMode,    // offset 21 -> 131/231
    Krate::DSP::OscParam::ParticleEnvType,      // offset 22 -> 132/232
    Krate::DSP::OscParam::ParticleDrift,        // offset 23 -> 133/233
    Krate::DSP::OscParam::FormantVowel,         // offset 24 -> 134/234
    Krate::DSP::OscParam::FormantMorph,         // offset 25 -> 135/235
    Krate::DSP::OscParam::SpectralPitchShift,   // offset 26 -> 136/236
    Krate::DSP::OscParam::SpectralTilt,         // offset 27 -> 137/237
    Krate::DSP::OscParam::SpectralFormantShift, // offset 28 -> 138/238
    Krate::DSP::OscParam::NoiseColor,           // offset 29 -> 139/239
};
inline constexpr size_t kOscTypeSpecificParamCount =
    sizeof(kParamIdToOscParam) / sizeof(kParamIdToOscParam[0]);

// =============================================================================
// Title Builder
// =============================================================================

/// Joins the bank prefix and a parameter name into a host-visible title, e.g.
/// "OSC A " + "Tune". Registration is not real-time and the temporary lives for
/// the duration of the addParameter() call it is passed to.
struct OscParamTitle {
    Steinberg::Vst::String128 text{};

    OscParamTitle(const Steinberg::Vst::TChar* prefix,
                  const Steinberg::Vst::TChar* name) noexcept {
        Steinberg::UString(text, 128).assign(prefix).append(name);
    }

    operator const Steinberg::Vst::TChar*() const noexcept { return text; }
};

// =============================================================================
// OscParams Struct
// =============================================================================

struct OscParams {
    // Existing fields (100-104)
    std::atomic<int> type{0};                // OscType enum (0-9)
    std::atomic<float> tuneSemitones{0.0f};  // -24 to +24
    std::atomic<float> fineCents{0.0f};      // -100 to +100
    std::atomic<float> level{1.0f};          // 0-1
    std::atomic<float> phase{0.0f};          // 0-1

    // Type-specific fields (110-139) -- 068-osc-type-params

    // PolyBLEP (waveform/pulseWidth unique; phaseMod/freqMod shared with Wavetable)
    std::atomic<int> waveform{1};             // OscWaveform (default Sawtooth=1)
    std::atomic<float> pulseWidth{0.5f};      // 0.01-0.99
    std::atomic<float> phaseMod{0.0f};        // -1.0 to +1.0
    std::atomic<float> freqMod{0.0f};         // -1.0 to +1.0

    // Phase Distortion
    std::atomic<int> pdWaveform{0};           // PDWaveform (default Saw=0)
    std::atomic<float> pdDistortion{0.0f};    // 0.0-1.0

    // Sync
    std::atomic<float> syncRatio{2.0f};       // 1.0-8.0
    std::atomic<int> syncWaveform{1};         // OscWaveform (default Sawtooth=1)
    std::atomic<int> syncMode{0};             // SyncMode (default Hard=0)
    std::atomic<float> syncAmount{1.0f};      // 0.0-1.0
    std::atomic<float> syncPulseWidth{0.5f};  // 0.01-0.99

    // Additive
    std::atomic<int> additivePartials{16};    // 1-128
    std::atomic<float> additiveTilt{0.0f};    // -24 to +24 dB/oct
    std::atomic<float> additiveInharm{0.0f};  // 0.0-1.0

    // Chaos
    std::atomic<int> chaosAttractor{0};       // ChaosAttractor (default Lorenz=0)
    std::atomic<float> chaosAmount{0.5f};     // 0.0-1.0
    std::atomic<float> chaosCoupling{0.0f};   // 0.0-1.0
    std::atomic<int> chaosOutput{0};          // 0=X, 1=Y, 2=Z

    // Particle
    std::atomic<float> particleScatter{3.0f};   // 0.0-12.0 st
    std::atomic<float> particleDensity{16.0f};  // 1-64
    std::atomic<float> particleLifetime{200.0f}; // 5-2000 ms
    std::atomic<int> particleSpawnMode{0};      // SpawnMode (default Regular=0)
    std::atomic<int> particleEnvType{0};        // GrainEnvelopeType (default Hann=0)
    std::atomic<float> particleDrift{0.0f};     // 0.0-1.0

    // Formant
    std::atomic<int> formantVowel{0};           // Vowel (default A=0)
    std::atomic<float> formantMorph{0.0f};      // 0.0-4.0

    // Spectral Freeze
    std::atomic<float> spectralPitch{0.0f};     // -24 to +24 st
    std::atomic<float> spectralTilt{0.0f};      // -12 to +12 dB/oct
    std::atomic<float> spectralFormant{0.0f};   // -12 to +12 st

    // Noise
    std::atomic<int> noiseColor{0};             // NoiseColor (default White=0)
};

// =============================================================================
// handleOscAParamChange
// =============================================================================

inline void handleOscParamChange(
    OscParams& params, Steinberg::Vst::ParamID base,
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    switch (id - base) {
        // --- Existing parameters (100-104) ---
        case kOscTypeOffset:
            params.type.store(
                std::clamp(static_cast<int>(value * (kOscTypeCount - 1) + 0.5), 0, kOscTypeCount - 1),
                std::memory_order_relaxed);
            break;
        case kOscTuneOffset:
            params.tuneSemitones.store(
                std::clamp(static_cast<float>(value * 48.0 - 24.0), -24.0f, 24.0f),
                std::memory_order_relaxed);
            break;
        case kOscFineOffset:
            params.fineCents.store(
                std::clamp(static_cast<float>(value * 200.0 - 100.0), -100.0f, 100.0f),
                std::memory_order_relaxed);
            break;
        case kOscLevelOffset:
            params.level.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kOscPhaseOffset:
            params.phase.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;

        // --- Type-specific parameters (110-139) ---

        // PolyBLEP / Wavetable shared
        case kOscWaveformOffset:
            params.waveform.store(
                std::clamp(static_cast<int>(value * 4 + 0.5), 0, 4),
                std::memory_order_relaxed);
            break;
        case kOscPulseWidthOffset:
            params.pulseWidth.store(
                static_cast<float>(0.01 + value * 0.98),
                std::memory_order_relaxed);
            break;
        case kOscPhaseModOffset:
            params.phaseMod.store(
                static_cast<float>(value * 2.0 - 1.0),
                std::memory_order_relaxed);
            break;
        case kOscFreqModOffset:
            params.freqMod.store(
                static_cast<float>(value * 2.0 - 1.0),
                std::memory_order_relaxed);
            break;

        // Phase Distortion
        case kOscPDWaveformOffset:
            params.pdWaveform.store(
                std::clamp(static_cast<int>(value * 7 + 0.5), 0, 7),
                std::memory_order_relaxed);
            break;
        case kOscPDDistortionOffset:
            params.pdDistortion.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;

        // Sync
        case kOscSyncRatioOffset:
            params.syncRatio.store(
                static_cast<float>(1.0 + value * 7.0),
                std::memory_order_relaxed);
            break;
        case kOscSyncWaveformOffset:
            params.syncWaveform.store(
                std::clamp(static_cast<int>(value * 4 + 0.5), 0, 4),
                std::memory_order_relaxed);
            break;
        case kOscSyncModeOffset:
            params.syncMode.store(
                std::clamp(static_cast<int>(value * 2 + 0.5), 0, 2),
                std::memory_order_relaxed);
            break;
        case kOscSyncAmountOffset:
            params.syncAmount.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;
        case kOscSyncPulseWidthOffset:
            params.syncPulseWidth.store(
                static_cast<float>(0.01 + value * 0.98),
                std::memory_order_relaxed);
            break;

        // Additive
        case kOscAdditivePartialsOffset:
            params.additivePartials.store(
                std::clamp(static_cast<int>(value * 127 + 0.5) + 1, 1, 128),
                std::memory_order_relaxed);
            break;
        case kOscAdditiveTiltOffset:
            params.additiveTilt.store(
                static_cast<float>(value * 48.0 - 24.0),
                std::memory_order_relaxed);
            break;
        case kOscAdditiveInharmOffset:
            params.additiveInharm.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;

        // Chaos
        case kOscChaosAttractorOffset:
            params.chaosAttractor.store(
                std::clamp(static_cast<int>(value * 4 + 0.5), 0, 4),
                std::memory_order_relaxed);
            break;
        case kOscChaosAmountOffset:
            params.chaosAmount.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;
        case kOscChaosCouplingOffset:
            params.chaosCoupling.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;
        case kOscChaosOutputOffset:
            params.chaosOutput.store(
                std::clamp(static_cast<int>(value * 2 + 0.5), 0, 2),
                std::memory_order_relaxed);
            break;

        // Particle
        case kOscParticleScatterOffset:
            params.particleScatter.store(
                static_cast<float>(value * 12.0),
                std::memory_order_relaxed);
            break;
        case kOscParticleDensityOffset:
            params.particleDensity.store(
                static_cast<float>(1.0 + value * 63.0),
                std::memory_order_relaxed);
            break;
        case kOscParticleLifetimeOffset:
            params.particleLifetime.store(
                static_cast<float>(5.0 + value * 1995.0),
                std::memory_order_relaxed);
            break;
        case kOscParticleSpawnModeOffset:
            params.particleSpawnMode.store(
                std::clamp(static_cast<int>(value * 2 + 0.5), 0, 2),
                std::memory_order_relaxed);
            break;
        case kOscParticleEnvTypeOffset:
            params.particleEnvType.store(
                std::clamp(static_cast<int>(value * 5 + 0.5), 0, 5),
                std::memory_order_relaxed);
            break;
        case kOscParticleDriftOffset:
            params.particleDrift.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;

        // Formant
        case kOscFormantVowelOffset:
            params.formantVowel.store(
                std::clamp(static_cast<int>(value * 4 + 0.5), 0, 4),
                std::memory_order_relaxed);
            break;
        case kOscFormantMorphOffset:
            params.formantMorph.store(
                static_cast<float>(value * 4.0),
                std::memory_order_relaxed);
            break;

        // Spectral Freeze
        case kOscSpectralPitchOffset:
            params.spectralPitch.store(
                static_cast<float>(value * 48.0 - 24.0),
                std::memory_order_relaxed);
            break;
        case kOscSpectralTiltOffset:
            params.spectralTilt.store(
                static_cast<float>(value * 24.0 - 12.0),
                std::memory_order_relaxed);
            break;
        case kOscSpectralFormantOffset:
            params.spectralFormant.store(
                static_cast<float>(value * 24.0 - 12.0),
                std::memory_order_relaxed);
            break;

        // Noise
        case kOscNoiseColorOffset:
            params.noiseColor.store(
                std::clamp(static_cast<int>(value * 5 + 0.5), 0, 5),
                std::memory_order_relaxed);
            break;

        default: break;
    }
}

// =============================================================================
// registerOscParams
// =============================================================================

inline void registerOscParams(Steinberg::Vst::ParameterContainer& parameters,
                             Steinberg::Vst::ParamID base,
                             const Steinberg::Vst::TChar* prefix) {
    using namespace Steinberg::Vst;

    // --- Existing parameters (100-104) ---
    parameters.addParameter(createDropdownParameter(
        OscParamTitle(prefix, STR16("Type")), (base + kOscTypeOffset),
        kOscTypeStrings, kOscTypeCount
    ));
    parameters.addParameter(OscParamTitle(prefix, STR16("Tune")), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscTuneOffset));
    parameters.addParameter(OscParamTitle(prefix, STR16("Fine")), STR16("ct"), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscFineOffset));
    parameters.addParameter(OscParamTitle(prefix, STR16("Level")), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, (base + kOscLevelOffset));
    parameters.addParameter(OscParamTitle(prefix, STR16("Phase")), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, (base + kOscPhaseOffset));

    // --- Type-specific parameters (110-139) ---

    // PolyBLEP: Waveform dropdown (default Sawtooth=1, normalized = 1/4 = 0.25)
    parameters.addParameter(createDropdownParameterWithDefault(
        OscParamTitle(prefix, STR16("Waveform")), (base + kOscWaveformOffset), 1,
        kOscWaveformStrings, kOscWaveformCount
    ));
    // PolyBLEP: Pulse Width (default 0.5)
    parameters.addParameter(OscParamTitle(prefix, STR16("Pulse Width")), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscPulseWidthOffset));
    // Shared PolyBLEP/Wavetable: Phase Mod (default 0.0 -> normalized 0.5)
    parameters.addParameter(OscParamTitle(prefix, STR16("Phase Mod")), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscPhaseModOffset));
    // Shared PolyBLEP/Wavetable: Freq Mod (default 0.0 -> normalized 0.5)
    parameters.addParameter(OscParamTitle(prefix, STR16("Freq Mod")), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscFreqModOffset));

    // PD: Waveform dropdown (default Saw=0)
    parameters.addParameter(createDropdownParameter(
        OscParamTitle(prefix, STR16("PD Waveform")), (base + kOscPDWaveformOffset),
        kPDWaveformStrings, kPDWaveformCount
    ));
    // PD: Distortion (default 0.0)
    parameters.addParameter(OscParamTitle(prefix, STR16("PD Distortion")), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, (base + kOscPDDistortionOffset));

    // Sync: Ratio (default 2.0 -> normalized (2-1)/7 = 0.143)
    parameters.addParameter(OscParamTitle(prefix, STR16("Sync Ratio")), STR16(""), 0, 1.0 / 7.0,
        ParameterInfo::kCanAutomate, (base + kOscSyncRatioOffset));
    // Sync: Slave Waveform (default Sawtooth=1)
    parameters.addParameter(createDropdownParameterWithDefault(
        OscParamTitle(prefix, STR16("Sync Waveform")), (base + kOscSyncWaveformOffset), 1,
        kOscWaveformStrings, kOscWaveformCount
    ));
    // Sync: Mode (default Hard=0)
    parameters.addParameter(createDropdownParameter(
        OscParamTitle(prefix, STR16("Sync Mode")), (base + kOscSyncModeOffset),
        kSyncModeStrings, kSyncModeCount
    ));
    // Sync: Amount (default 1.0)
    parameters.addParameter(OscParamTitle(prefix, STR16("Sync Amount")), STR16(""), 0, 1.0,
        ParameterInfo::kCanAutomate, (base + kOscSyncAmountOffset));
    // Sync: Slave PW (default 0.5)
    parameters.addParameter(OscParamTitle(prefix, STR16("Sync PW")), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscSyncPulseWidthOffset));

    // Additive: Num Partials (default 16 -> normalized (16-1)/127 = 0.118)
    parameters.addParameter(OscParamTitle(prefix, STR16("Partials")), STR16(""), 0, 15.0 / 127.0,
        ParameterInfo::kCanAutomate, (base + kOscAdditivePartialsOffset));
    // Additive: Spectral Tilt (default 0.0 -> normalized 0.5)
    parameters.addParameter(OscParamTitle(prefix, STR16("Tilt")), STR16("dB/oct"), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscAdditiveTiltOffset));
    // Additive: Inharmonicity (default 0.0)
    parameters.addParameter(OscParamTitle(prefix, STR16("Inharmonicity")), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, (base + kOscAdditiveInharmOffset));

    // Chaos: Attractor (default Lorenz=0)
    parameters.addParameter(createDropdownParameter(
        OscParamTitle(prefix, STR16("Attractor")), (base + kOscChaosAttractorOffset),
        kChaosAttractorStrings, kChaosAttractorCount
    ));
    // Chaos: Amount (default 0.5)
    parameters.addParameter(OscParamTitle(prefix, STR16("Chaos Amount")), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscChaosAmountOffset));
    // Chaos: Coupling (default 0.0)
    parameters.addParameter(OscParamTitle(prefix, STR16("Coupling")), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, (base + kOscChaosCouplingOffset));
    // Chaos: Output (default X=0)
    parameters.addParameter(createDropdownParameter(
        OscParamTitle(prefix, STR16("Output")), (base + kOscChaosOutputOffset),
        kChaosOutputStrings, kChaosOutputCount
    ));

    // Particle: Scatter (default 3.0 -> normalized 3/12 = 0.25)
    parameters.addParameter(OscParamTitle(prefix, STR16("Scatter")), STR16("st"), 0, 3.0 / 12.0,
        ParameterInfo::kCanAutomate, (base + kOscParticleScatterOffset));
    // Particle: Density (default 16 -> normalized (16-1)/63 = 0.238)
    parameters.addParameter(OscParamTitle(prefix, STR16("Density")), STR16(""), 0, 15.0 / 63.0,
        ParameterInfo::kCanAutomate, (base + kOscParticleDensityOffset));
    // Particle: Lifetime (default 200 -> normalized (200-5)/1995 = 0.0977)
    parameters.addParameter(OscParamTitle(prefix, STR16("Lifetime")), STR16("ms"), 0, 195.0 / 1995.0,
        ParameterInfo::kCanAutomate, (base + kOscParticleLifetimeOffset));
    // Particle: Spawn Mode (default Regular=0)
    parameters.addParameter(createDropdownParameter(
        OscParamTitle(prefix, STR16("Spawn Mode")), (base + kOscParticleSpawnModeOffset),
        kParticleSpawnModeStrings, kParticleSpawnModeCount
    ));
    // Particle: Envelope Type (default Hann=0)
    parameters.addParameter(createDropdownParameter(
        OscParamTitle(prefix, STR16("Envelope")), (base + kOscParticleEnvTypeOffset),
        kParticleEnvTypeStrings, kParticleEnvTypeCount
    ));
    // Particle: Drift (default 0.0)
    parameters.addParameter(OscParamTitle(prefix, STR16("Drift")), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, (base + kOscParticleDriftOffset));

    // Formant: Vowel (default A=0)
    parameters.addParameter(createDropdownParameter(
        OscParamTitle(prefix, STR16("Vowel")), (base + kOscFormantVowelOffset),
        kFormantVowelStrings, kFormantVowelCount
    ));
    // Formant: Morph (default 0.0)
    parameters.addParameter(OscParamTitle(prefix, STR16("Morph")), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, (base + kOscFormantMorphOffset));

    // Spectral Freeze: Pitch Shift (default 0.0 -> normalized 0.5)
    parameters.addParameter(OscParamTitle(prefix, STR16("Pitch Shift")), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscSpectralPitchOffset));
    // Spectral Freeze: Tilt (default 0.0 -> normalized 0.5)
    parameters.addParameter(OscParamTitle(prefix, STR16("Spectral Tilt")), STR16("dB/oct"), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscSpectralTiltOffset));
    // Spectral Freeze: Formant Shift (default 0.0 -> normalized 0.5)
    parameters.addParameter(OscParamTitle(prefix, STR16("Formant Shift")), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, (base + kOscSpectralFormantOffset));

    // Noise: Color (default White=0)
    parameters.addParameter(createDropdownParameter(
        OscParamTitle(prefix, STR16("Color")), (base + kOscNoiseColorOffset),
        kNoiseColorStrings, kNoiseColorCount
    ));
}

// =============================================================================
// formatOscAParam
// =============================================================================

inline Steinberg::tresult formatOscParam(
    Steinberg::Vst::ParamID base, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value, Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id - base) {
        case kOscTuneOffset: {
            snprintf(text, sizeof(text), "%+.0f st", value * 48.0 - 24.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscFineOffset: {
            snprintf(text, sizeof(text), "%+.0f ct", value * 200.0 - 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscLevelOffset:
        case kOscPhaseOffset: {
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // --- Type-specific display formatting ---

        case kOscPulseWidthOffset:
        case kOscSyncPulseWidthOffset: {
            snprintf(text, sizeof(text), "%.2f", 0.01 + value * 0.98);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscPhaseModOffset:
        case kOscFreqModOffset: {
            snprintf(text, sizeof(text), "%+.2f", value * 2.0 - 1.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscPDDistortionOffset:
        case kOscSyncAmountOffset:
        case kOscChaosAmountOffset:
        case kOscChaosCouplingOffset:
        case kOscAdditiveInharmOffset:
        case kOscParticleDriftOffset: {
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscSyncRatioOffset: {
            snprintf(text, sizeof(text), "%.2fx", 1.0 + value * 7.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscAdditivePartialsOffset: {
            int partials = std::clamp(static_cast<int>(value * 127 + 0.5) + 1, 1, 128);
            snprintf(text, sizeof(text), "%d", partials);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscAdditiveTiltOffset: {
            snprintf(text, sizeof(text), "%+.1f dB/oct", value * 48.0 - 24.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscParticleScatterOffset: {
            snprintf(text, sizeof(text), "%.1f st", value * 12.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscParticleDensityOffset: {
            snprintf(text, sizeof(text), "%.1f", 1.0 + value * 63.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscParticleLifetimeOffset: {
            snprintf(text, sizeof(text), "%.0f ms", 5.0 + value * 1995.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscFormantMorphOffset: {
            snprintf(text, sizeof(text), "%.2f", value * 4.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscSpectralPitchOffset: {
            snprintf(text, sizeof(text), "%+.1f st", value * 48.0 - 24.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscSpectralTiltOffset: {
            snprintf(text, sizeof(text), "%+.1f dB/oct", value * 24.0 - 12.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscSpectralFormantOffset: {
            snprintf(text, sizeof(text), "%+.1f st", value * 24.0 - 12.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        default: break;
    }
    return Steinberg::kResultFalse;
}

// =============================================================================
// saveOscParams / loadOscParams
// =============================================================================

inline void saveOscParams(const OscParams& params, Steinberg::IBStreamer& streamer) {
    // Existing fields
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tuneSemitones.load(std::memory_order_relaxed));
    streamer.writeFloat(params.fineCents.load(std::memory_order_relaxed));
    streamer.writeFloat(params.level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.phase.load(std::memory_order_relaxed));

    // Type-specific fields (068-osc-type-params) -- appended after existing fields
    // PolyBLEP / Wavetable shared
    streamer.writeInt32(params.waveform.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pulseWidth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.phaseMod.load(std::memory_order_relaxed));
    streamer.writeFloat(params.freqMod.load(std::memory_order_relaxed));
    // Phase Distortion
    streamer.writeInt32(params.pdWaveform.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pdDistortion.load(std::memory_order_relaxed));
    // Sync
    streamer.writeFloat(params.syncRatio.load(std::memory_order_relaxed));
    streamer.writeInt32(params.syncWaveform.load(std::memory_order_relaxed));
    streamer.writeInt32(params.syncMode.load(std::memory_order_relaxed));
    streamer.writeFloat(params.syncAmount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.syncPulseWidth.load(std::memory_order_relaxed));
    // Additive
    streamer.writeInt32(params.additivePartials.load(std::memory_order_relaxed));
    streamer.writeFloat(params.additiveTilt.load(std::memory_order_relaxed));
    streamer.writeFloat(params.additiveInharm.load(std::memory_order_relaxed));
    // Chaos
    streamer.writeInt32(params.chaosAttractor.load(std::memory_order_relaxed));
    streamer.writeFloat(params.chaosAmount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.chaosCoupling.load(std::memory_order_relaxed));
    streamer.writeInt32(params.chaosOutput.load(std::memory_order_relaxed));
    // Particle
    streamer.writeFloat(params.particleScatter.load(std::memory_order_relaxed));
    streamer.writeFloat(params.particleDensity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.particleLifetime.load(std::memory_order_relaxed));
    streamer.writeInt32(params.particleSpawnMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.particleEnvType.load(std::memory_order_relaxed));
    streamer.writeFloat(params.particleDrift.load(std::memory_order_relaxed));
    // Formant
    streamer.writeInt32(params.formantVowel.load(std::memory_order_relaxed));
    streamer.writeFloat(params.formantMorph.load(std::memory_order_relaxed));
    // Spectral Freeze
    streamer.writeFloat(params.spectralPitch.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spectralTilt.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spectralFormant.load(std::memory_order_relaxed));
    // Noise
    streamer.writeInt32(params.noiseColor.load(std::memory_order_relaxed));
}

inline bool loadOscParams(OscParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    // Existing fields (required -- return false if missing)
    if (!streamer.readInt32(intVal)) return false;
    params.type.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.tuneSemitones.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.fineCents.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.level.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.phase.store(floatVal, std::memory_order_relaxed);

    // Type-specific fields (068-osc-type-params)
    // When readFloat()/readInt32() returns false, the old preset lacks these fields;
    // keep the struct's spec-defined default and continue without error (FR-012).

    // PolyBLEP / Wavetable shared
    if (streamer.readInt32(intVal))
        params.waveform.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.pulseWidth.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.phaseMod.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.freqMod.store(floatVal, std::memory_order_relaxed);
    // Phase Distortion
    if (streamer.readInt32(intVal))
        params.pdWaveform.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.pdDistortion.store(floatVal, std::memory_order_relaxed);
    // Sync
    if (streamer.readFloat(floatVal))
        params.syncRatio.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.syncWaveform.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.syncMode.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.syncAmount.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.syncPulseWidth.store(floatVal, std::memory_order_relaxed);
    // Additive
    if (streamer.readInt32(intVal))
        params.additivePartials.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.additiveTilt.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.additiveInharm.store(floatVal, std::memory_order_relaxed);
    // Chaos
    if (streamer.readInt32(intVal))
        params.chaosAttractor.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.chaosAmount.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.chaosCoupling.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.chaosOutput.store(intVal, std::memory_order_relaxed);
    // Particle
    if (streamer.readFloat(floatVal))
        params.particleScatter.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.particleDensity.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.particleLifetime.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.particleSpawnMode.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.particleEnvType.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.particleDrift.store(floatVal, std::memory_order_relaxed);
    // Formant
    if (streamer.readInt32(intVal))
        params.formantVowel.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.formantMorph.store(floatVal, std::memory_order_relaxed);
    // Spectral Freeze
    if (streamer.readFloat(floatVal))
        params.spectralPitch.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.spectralTilt.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.spectralFormant.store(floatVal, std::memory_order_relaxed);
    // Noise
    if (streamer.readInt32(intVal))
        params.noiseColor.store(intVal, std::memory_order_relaxed);

    return true;
}

template<typename SetParamFunc>
inline void loadOscParamsToController(
    Steinberg::Vst::ParamID base, Steinberg::IBStreamer& streamer,
    SetParamFunc setParam) {
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    // Existing fields
    if (streamer.readInt32(intVal))
        setParam((base + kOscTypeOffset), static_cast<double>(intVal) / (kOscTypeCount - 1));
    if (streamer.readFloat(floatVal))
        setParam((base + kOscTuneOffset), static_cast<double>((floatVal + 24.0f) / 48.0f));
    if (streamer.readFloat(floatVal))
        setParam((base + kOscFineOffset), static_cast<double>((floatVal + 100.0f) / 200.0f));
    if (streamer.readFloat(floatVal))
        setParam((base + kOscLevelOffset), static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam((base + kOscPhaseOffset), static_cast<double>(floatVal));

    // Type-specific fields (068-osc-type-params)
    // Reverse denormalization: convert DSP-domain values back to normalized [0,1]

    // PolyBLEP: Waveform (int 0-4 -> normalized /4)
    if (streamer.readInt32(intVal))
        setParam((base + kOscWaveformOffset), static_cast<double>(intVal) / 4.0);
    // PolyBLEP: PulseWidth (0.01-0.99 -> normalized (pw-0.01)/0.98)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscPulseWidthOffset), static_cast<double>((floatVal - 0.01f) / 0.98f));
    // PhaseMod (-1 to +1 -> normalized (pm+1)/2)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscPhaseModOffset), static_cast<double>((floatVal + 1.0f) / 2.0f));
    // FreqMod (-1 to +1 -> normalized (fm+1)/2)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscFreqModOffset), static_cast<double>((floatVal + 1.0f) / 2.0f));

    // PD: Waveform (int 0-7 -> normalized /7)
    if (streamer.readInt32(intVal))
        setParam((base + kOscPDWaveformOffset), static_cast<double>(intVal) / 7.0);
    // PD: Distortion (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscPDDistortionOffset), static_cast<double>(floatVal));

    // Sync: Ratio (1-8 -> normalized (r-1)/7)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscSyncRatioOffset), static_cast<double>((floatVal - 1.0f) / 7.0f));
    // Sync: Waveform (int 0-4 -> normalized /4)
    if (streamer.readInt32(intVal))
        setParam((base + kOscSyncWaveformOffset), static_cast<double>(intVal) / 4.0);
    // Sync: Mode (int 0-2 -> normalized /2)
    if (streamer.readInt32(intVal))
        setParam((base + kOscSyncModeOffset), static_cast<double>(intVal) / 2.0);
    // Sync: Amount (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscSyncAmountOffset), static_cast<double>(floatVal));
    // Sync: PulseWidth (0.01-0.99 -> normalized (pw-0.01)/0.98)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscSyncPulseWidthOffset), static_cast<double>((floatVal - 0.01f) / 0.98f));

    // Additive: Partials (int 1-128 -> normalized (p-1)/127)
    if (streamer.readInt32(intVal))
        setParam((base + kOscAdditivePartialsOffset), static_cast<double>(intVal - 1) / 127.0);
    // Additive: Tilt (-24 to +24 -> normalized (t+24)/48)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscAdditiveTiltOffset), static_cast<double>((floatVal + 24.0f) / 48.0f));
    // Additive: Inharmonicity (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscAdditiveInharmOffset), static_cast<double>(floatVal));

    // Chaos: Attractor (int 0-4 -> normalized /4)
    if (streamer.readInt32(intVal))
        setParam((base + kOscChaosAttractorOffset), static_cast<double>(intVal) / 4.0);
    // Chaos: Amount (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscChaosAmountOffset), static_cast<double>(floatVal));
    // Chaos: Coupling (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscChaosCouplingOffset), static_cast<double>(floatVal));
    // Chaos: Output (int 0-2 -> normalized /2)
    if (streamer.readInt32(intVal))
        setParam((base + kOscChaosOutputOffset), static_cast<double>(intVal) / 2.0);

    // Particle: Scatter (0-12 -> normalized /12)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscParticleScatterOffset), static_cast<double>(floatVal / 12.0f));
    // Particle: Density (1-64 -> normalized (d-1)/63)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscParticleDensityOffset), static_cast<double>((floatVal - 1.0f) / 63.0f));
    // Particle: Lifetime (5-2000 -> normalized (lt-5)/1995)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscParticleLifetimeOffset), static_cast<double>((floatVal - 5.0f) / 1995.0f));
    // Particle: SpawnMode (int 0-2 -> normalized /2)
    if (streamer.readInt32(intVal))
        setParam((base + kOscParticleSpawnModeOffset), static_cast<double>(intVal) / 2.0);
    // Particle: EnvType (int 0-5 -> normalized /5)
    if (streamer.readInt32(intVal))
        setParam((base + kOscParticleEnvTypeOffset), static_cast<double>(intVal) / 5.0);
    // Particle: Drift (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscParticleDriftOffset), static_cast<double>(floatVal));

    // Formant: Vowel (int 0-4 -> normalized /4)
    if (streamer.readInt32(intVal))
        setParam((base + kOscFormantVowelOffset), static_cast<double>(intVal) / 4.0);
    // Formant: Morph (0-4 -> normalized /4)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscFormantMorphOffset), static_cast<double>(floatVal / 4.0f));

    // Spectral: Pitch (-24 to +24 -> normalized (p+24)/48)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscSpectralPitchOffset), static_cast<double>((floatVal + 24.0f) / 48.0f));
    // Spectral: Tilt (-12 to +12 -> normalized (t+12)/24)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscSpectralTiltOffset), static_cast<double>((floatVal + 12.0f) / 24.0f));
    // Spectral: Formant (-12 to +12 -> normalized (f+12)/24)
    if (streamer.readFloat(floatVal))
        setParam((base + kOscSpectralFormantOffset), static_cast<double>((floatVal + 12.0f) / 24.0f));

    // Noise: Color (int 0-5 -> normalized /5)
    if (streamer.readInt32(intVal))
        setParam((base + kOscNoiseColorOffset), static_cast<double>(intVal) / 5.0);
}

} // namespace Ruinae
