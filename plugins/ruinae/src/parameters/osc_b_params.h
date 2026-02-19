#pragma once

// ==============================================================================
// OSC B Parameters (ID 200-299)
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// OscBParams Struct
// =============================================================================

struct OscBParams {
    // Existing fields (200-204)
    std::atomic<int> type{0};
    std::atomic<float> tuneSemitones{0.0f};
    std::atomic<float> fineCents{0.0f};
    std::atomic<float> level{1.0f};
    std::atomic<float> phase{0.0f};

    // Type-specific fields (210-239) -- 068-osc-type-params (mirrors OscAParams)

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
// handleOscBParamChange
// =============================================================================

inline void handleOscBParamChange(
    OscBParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        // --- Existing parameters (200-204) ---
        case kOscBTypeId:
            params.type.store(
                std::clamp(static_cast<int>(value * (kOscTypeCount - 1) + 0.5), 0, kOscTypeCount - 1),
                std::memory_order_relaxed);
            break;
        case kOscBTuneId:
            params.tuneSemitones.store(
                std::clamp(static_cast<float>(value * 48.0 - 24.0), -24.0f, 24.0f),
                std::memory_order_relaxed);
            break;
        case kOscBFineId:
            params.fineCents.store(
                std::clamp(static_cast<float>(value * 200.0 - 100.0), -100.0f, 100.0f),
                std::memory_order_relaxed);
            break;
        case kOscBLevelId:
            params.level.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kOscBPhaseId:
            params.phase.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;

        // --- Type-specific parameters (210-239) ---

        // PolyBLEP / Wavetable shared
        case kOscBWaveformId:
            params.waveform.store(
                std::clamp(static_cast<int>(value * 4 + 0.5), 0, 4),
                std::memory_order_relaxed);
            break;
        case kOscBPulseWidthId:
            params.pulseWidth.store(
                static_cast<float>(0.01 + value * 0.98),
                std::memory_order_relaxed);
            break;
        case kOscBPhaseModId:
            params.phaseMod.store(
                static_cast<float>(value * 2.0 - 1.0),
                std::memory_order_relaxed);
            break;
        case kOscBFreqModId:
            params.freqMod.store(
                static_cast<float>(value * 2.0 - 1.0),
                std::memory_order_relaxed);
            break;

        // Phase Distortion
        case kOscBPDWaveformId:
            params.pdWaveform.store(
                std::clamp(static_cast<int>(value * 7 + 0.5), 0, 7),
                std::memory_order_relaxed);
            break;
        case kOscBPDDistortionId:
            params.pdDistortion.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;

        // Sync
        case kOscBSyncRatioId:
            params.syncRatio.store(
                static_cast<float>(1.0 + value * 7.0),
                std::memory_order_relaxed);
            break;
        case kOscBSyncWaveformId:
            params.syncWaveform.store(
                std::clamp(static_cast<int>(value * 4 + 0.5), 0, 4),
                std::memory_order_relaxed);
            break;
        case kOscBSyncModeId:
            params.syncMode.store(
                std::clamp(static_cast<int>(value * 2 + 0.5), 0, 2),
                std::memory_order_relaxed);
            break;
        case kOscBSyncAmountId:
            params.syncAmount.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;
        case kOscBSyncPulseWidthId:
            params.syncPulseWidth.store(
                static_cast<float>(0.01 + value * 0.98),
                std::memory_order_relaxed);
            break;

        // Additive
        case kOscBAdditivePartialsId:
            params.additivePartials.store(
                std::clamp(static_cast<int>(value * 127 + 0.5) + 1, 1, 128),
                std::memory_order_relaxed);
            break;
        case kOscBAdditiveTiltId:
            params.additiveTilt.store(
                static_cast<float>(value * 48.0 - 24.0),
                std::memory_order_relaxed);
            break;
        case kOscBAdditiveInharmId:
            params.additiveInharm.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;

        // Chaos
        case kOscBChaosAttractorId:
            params.chaosAttractor.store(
                std::clamp(static_cast<int>(value * 4 + 0.5), 0, 4),
                std::memory_order_relaxed);
            break;
        case kOscBChaosAmountId:
            params.chaosAmount.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;
        case kOscBChaosCouplingId:
            params.chaosCoupling.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;
        case kOscBChaosOutputId:
            params.chaosOutput.store(
                std::clamp(static_cast<int>(value * 2 + 0.5), 0, 2),
                std::memory_order_relaxed);
            break;

        // Particle
        case kOscBParticleScatterId:
            params.particleScatter.store(
                static_cast<float>(value * 12.0),
                std::memory_order_relaxed);
            break;
        case kOscBParticleDensityId:
            params.particleDensity.store(
                static_cast<float>(1.0 + value * 63.0),
                std::memory_order_relaxed);
            break;
        case kOscBParticleLifetimeId:
            params.particleLifetime.store(
                static_cast<float>(5.0 + value * 1995.0),
                std::memory_order_relaxed);
            break;
        case kOscBParticleSpawnModeId:
            params.particleSpawnMode.store(
                std::clamp(static_cast<int>(value * 2 + 0.5), 0, 2),
                std::memory_order_relaxed);
            break;
        case kOscBParticleEnvTypeId:
            params.particleEnvType.store(
                std::clamp(static_cast<int>(value * 5 + 0.5), 0, 5),
                std::memory_order_relaxed);
            break;
        case kOscBParticleDriftId:
            params.particleDrift.store(
                static_cast<float>(value),
                std::memory_order_relaxed);
            break;

        // Formant
        case kOscBFormantVowelId:
            params.formantVowel.store(
                std::clamp(static_cast<int>(value * 4 + 0.5), 0, 4),
                std::memory_order_relaxed);
            break;
        case kOscBFormantMorphId:
            params.formantMorph.store(
                static_cast<float>(value * 4.0),
                std::memory_order_relaxed);
            break;

        // Spectral Freeze
        case kOscBSpectralPitchId:
            params.spectralPitch.store(
                static_cast<float>(value * 48.0 - 24.0),
                std::memory_order_relaxed);
            break;
        case kOscBSpectralTiltId:
            params.spectralTilt.store(
                static_cast<float>(value * 24.0 - 12.0),
                std::memory_order_relaxed);
            break;
        case kOscBSpectralFormantId:
            params.spectralFormant.store(
                static_cast<float>(value * 24.0 - 12.0),
                std::memory_order_relaxed);
            break;

        // Noise
        case kOscBNoiseColorId:
            params.noiseColor.store(
                std::clamp(static_cast<int>(value * 5 + 0.5), 0, 5),
                std::memory_order_relaxed);
            break;

        default: break;
    }
}

// =============================================================================
// registerOscBParams
// =============================================================================

inline void registerOscBParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // --- Existing parameters (200-204) ---
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B Type"), kOscBTypeId,
        {STR16("PolyBLEP"), STR16("Wavetable"), STR16("Phase Dist"),
         STR16("Sync"), STR16("Additive"), STR16("Chaos"),
         STR16("Particle"), STR16("Formant"), STR16("Spectral Freeze"), STR16("Noise")}
    ));
    parameters.addParameter(STR16("OSC B Tune"), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBTuneId);
    parameters.addParameter(STR16("OSC B Fine"), STR16("ct"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBFineId);
    parameters.addParameter(STR16("OSC B Level"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kOscBLevelId);
    parameters.addParameter(STR16("OSC B Phase"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kOscBPhaseId);

    // --- Type-specific parameters (210-239) ---

    // PolyBLEP: Waveform dropdown (default Sawtooth=1)
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("OSC B Waveform"), kOscBWaveformId, 1,
        {STR16("Sine"), STR16("Sawtooth"), STR16("Square"),
         STR16("Pulse"), STR16("Triangle")}
    ));
    parameters.addParameter(STR16("OSC B Pulse Width"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBPulseWidthId);
    parameters.addParameter(STR16("OSC B Phase Mod"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBPhaseModId);
    parameters.addParameter(STR16("OSC B Freq Mod"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBFreqModId);

    // PD
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B PD Waveform"), kOscBPDWaveformId,
        {STR16("Saw"), STR16("Square"), STR16("Pulse"),
         STR16("DoubleSine"), STR16("HalfSine"),
         STR16("ResSaw"), STR16("ResTri"), STR16("ResTrap")}
    ));
    parameters.addParameter(STR16("OSC B PD Distortion"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kOscBPDDistortionId);

    // Sync
    parameters.addParameter(STR16("OSC B Sync Ratio"), STR16(""), 0, 1.0 / 7.0,
        ParameterInfo::kCanAutomate, kOscBSyncRatioId);
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("OSC B Sync Waveform"), kOscBSyncWaveformId, 1,
        {STR16("Sine"), STR16("Sawtooth"), STR16("Square"),
         STR16("Pulse"), STR16("Triangle")}
    ));
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B Sync Mode"), kOscBSyncModeId,
        {STR16("Hard"), STR16("Reverse"), STR16("Phase Advance")}
    ));
    parameters.addParameter(STR16("OSC B Sync Amount"), STR16(""), 0, 1.0,
        ParameterInfo::kCanAutomate, kOscBSyncAmountId);
    parameters.addParameter(STR16("OSC B Sync PW"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBSyncPulseWidthId);

    // Additive
    parameters.addParameter(STR16("OSC B Partials"), STR16(""), 0, 15.0 / 127.0,
        ParameterInfo::kCanAutomate, kOscBAdditivePartialsId);
    parameters.addParameter(STR16("OSC B Tilt"), STR16("dB/oct"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBAdditiveTiltId);
    parameters.addParameter(STR16("OSC B Inharmonicity"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kOscBAdditiveInharmId);

    // Chaos
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B Attractor"), kOscBChaosAttractorId,
        {STR16("Lorenz"), STR16("Rossler"), STR16("Chua"),
         STR16("Duffing"), STR16("Van der Pol")}
    ));
    parameters.addParameter(STR16("OSC B Chaos Amount"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBChaosAmountId);
    parameters.addParameter(STR16("OSC B Coupling"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kOscBChaosCouplingId);
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B Output"), kOscBChaosOutputId,
        {STR16("X"), STR16("Y"), STR16("Z")}
    ));

    // Particle
    parameters.addParameter(STR16("OSC B Scatter"), STR16("st"), 0, 3.0 / 12.0,
        ParameterInfo::kCanAutomate, kOscBParticleScatterId);
    parameters.addParameter(STR16("OSC B Density"), STR16(""), 0, 15.0 / 63.0,
        ParameterInfo::kCanAutomate, kOscBParticleDensityId);
    parameters.addParameter(STR16("OSC B Lifetime"), STR16("ms"), 0, 195.0 / 1995.0,
        ParameterInfo::kCanAutomate, kOscBParticleLifetimeId);
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B Spawn Mode"), kOscBParticleSpawnModeId,
        {STR16("Regular"), STR16("Random"), STR16("Burst")}
    ));
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B Envelope"), kOscBParticleEnvTypeId,
        {STR16("Hann"), STR16("Trap"), STR16("Sine"),
         STR16("Blackman"), STR16("Linear"), STR16("Exp")}
    ));
    parameters.addParameter(STR16("OSC B Drift"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kOscBParticleDriftId);

    // Formant
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B Vowel"), kOscBFormantVowelId,
        {STR16("A"), STR16("E"), STR16("I"), STR16("O"), STR16("U")}
    ));
    parameters.addParameter(STR16("OSC B Morph"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kOscBFormantMorphId);

    // Spectral Freeze
    parameters.addParameter(STR16("OSC B Pitch Shift"), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBSpectralPitchId);
    parameters.addParameter(STR16("OSC B Spectral Tilt"), STR16("dB/oct"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBSpectralTiltId);
    parameters.addParameter(STR16("OSC B Formant Shift"), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBSpectralFormantId);

    // Noise
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B Color"), kOscBNoiseColorId,
        {STR16("White"), STR16("Pink"), STR16("Brown"),
         STR16("Blue"), STR16("Violet"), STR16("Grey")}
    ));
}

// =============================================================================
// formatOscBParam
// =============================================================================

inline Steinberg::tresult formatOscBParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kOscBTuneId: {
            snprintf(text, sizeof(text), "%+.0f st", value * 48.0 - 24.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBFineId: {
            snprintf(text, sizeof(text), "%+.0f ct", value * 200.0 - 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBLevelId:
        case kOscBPhaseId: {
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // --- Type-specific display formatting ---

        case kOscBPulseWidthId:
        case kOscBSyncPulseWidthId: {
            snprintf(text, sizeof(text), "%.2f", 0.01 + value * 0.98);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBPhaseModId:
        case kOscBFreqModId: {
            snprintf(text, sizeof(text), "%+.2f", value * 2.0 - 1.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBPDDistortionId:
        case kOscBSyncAmountId:
        case kOscBChaosAmountId:
        case kOscBChaosCouplingId:
        case kOscBAdditiveInharmId:
        case kOscBParticleDriftId: {
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBSyncRatioId: {
            snprintf(text, sizeof(text), "%.2fx", 1.0 + value * 7.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBAdditivePartialsId: {
            int partials = std::clamp(static_cast<int>(value * 127 + 0.5) + 1, 1, 128);
            snprintf(text, sizeof(text), "%d", partials);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBAdditiveTiltId: {
            snprintf(text, sizeof(text), "%+.1f dB/oct", value * 48.0 - 24.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBParticleScatterId: {
            snprintf(text, sizeof(text), "%.1f st", value * 12.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBParticleDensityId: {
            snprintf(text, sizeof(text), "%.1f", 1.0 + value * 63.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBParticleLifetimeId: {
            snprintf(text, sizeof(text), "%.0f ms", 5.0 + value * 1995.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBFormantMorphId: {
            snprintf(text, sizeof(text), "%.2f", value * 4.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBSpectralPitchId: {
            snprintf(text, sizeof(text), "%+.1f st", value * 48.0 - 24.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBSpectralTiltId: {
            snprintf(text, sizeof(text), "%+.1f dB/oct", value * 24.0 - 12.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBSpectralFormantId: {
            snprintf(text, sizeof(text), "%+.1f st", value * 24.0 - 12.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        default: break;
    }
    return kResultFalse;
}

// =============================================================================
// saveOscBParams / loadOscBParams
// =============================================================================

inline void saveOscBParams(const OscBParams& params, Steinberg::IBStreamer& streamer) {
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

inline bool loadOscBParams(OscBParams& params, Steinberg::IBStreamer& streamer) {
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
inline void loadOscBParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    // Existing fields
    if (streamer.readInt32(intVal))
        setParam(kOscBTypeId, static_cast<double>(intVal) / (kOscTypeCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kOscBTuneId, static_cast<double>((floatVal + 24.0f) / 48.0f));
    if (streamer.readFloat(floatVal))
        setParam(kOscBFineId, static_cast<double>((floatVal + 100.0f) / 200.0f));
    if (streamer.readFloat(floatVal))
        setParam(kOscBLevelId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kOscBPhaseId, static_cast<double>(floatVal));

    // Type-specific fields (068-osc-type-params)
    // Reverse denormalization: convert DSP-domain values back to normalized [0,1]

    // PolyBLEP: Waveform (int 0-4 -> normalized /4)
    if (streamer.readInt32(intVal))
        setParam(kOscBWaveformId, static_cast<double>(intVal) / 4.0);
    // PolyBLEP: PulseWidth (0.01-0.99 -> normalized (pw-0.01)/0.98)
    if (streamer.readFloat(floatVal))
        setParam(kOscBPulseWidthId, static_cast<double>((floatVal - 0.01f) / 0.98f));
    // PhaseMod (-1 to +1 -> normalized (pm+1)/2)
    if (streamer.readFloat(floatVal))
        setParam(kOscBPhaseModId, static_cast<double>((floatVal + 1.0f) / 2.0f));
    // FreqMod (-1 to +1 -> normalized (fm+1)/2)
    if (streamer.readFloat(floatVal))
        setParam(kOscBFreqModId, static_cast<double>((floatVal + 1.0f) / 2.0f));

    // PD: Waveform (int 0-7 -> normalized /7)
    if (streamer.readInt32(intVal))
        setParam(kOscBPDWaveformId, static_cast<double>(intVal) / 7.0);
    // PD: Distortion (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam(kOscBPDDistortionId, static_cast<double>(floatVal));

    // Sync: Ratio (1-8 -> normalized (r-1)/7)
    if (streamer.readFloat(floatVal))
        setParam(kOscBSyncRatioId, static_cast<double>((floatVal - 1.0f) / 7.0f));
    // Sync: Waveform (int 0-4 -> normalized /4)
    if (streamer.readInt32(intVal))
        setParam(kOscBSyncWaveformId, static_cast<double>(intVal) / 4.0);
    // Sync: Mode (int 0-2 -> normalized /2)
    if (streamer.readInt32(intVal))
        setParam(kOscBSyncModeId, static_cast<double>(intVal) / 2.0);
    // Sync: Amount (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam(kOscBSyncAmountId, static_cast<double>(floatVal));
    // Sync: PulseWidth (0.01-0.99 -> normalized (pw-0.01)/0.98)
    if (streamer.readFloat(floatVal))
        setParam(kOscBSyncPulseWidthId, static_cast<double>((floatVal - 0.01f) / 0.98f));

    // Additive: Partials (int 1-128 -> normalized (p-1)/127)
    if (streamer.readInt32(intVal))
        setParam(kOscBAdditivePartialsId, static_cast<double>(intVal - 1) / 127.0);
    // Additive: Tilt (-24 to +24 -> normalized (t+24)/48)
    if (streamer.readFloat(floatVal))
        setParam(kOscBAdditiveTiltId, static_cast<double>((floatVal + 24.0f) / 48.0f));
    // Additive: Inharmonicity (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam(kOscBAdditiveInharmId, static_cast<double>(floatVal));

    // Chaos: Attractor (int 0-4 -> normalized /4)
    if (streamer.readInt32(intVal))
        setParam(kOscBChaosAttractorId, static_cast<double>(intVal) / 4.0);
    // Chaos: Amount (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam(kOscBChaosAmountId, static_cast<double>(floatVal));
    // Chaos: Coupling (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam(kOscBChaosCouplingId, static_cast<double>(floatVal));
    // Chaos: Output (int 0-2 -> normalized /2)
    if (streamer.readInt32(intVal))
        setParam(kOscBChaosOutputId, static_cast<double>(intVal) / 2.0);

    // Particle: Scatter (0-12 -> normalized /12)
    if (streamer.readFloat(floatVal))
        setParam(kOscBParticleScatterId, static_cast<double>(floatVal / 12.0f));
    // Particle: Density (1-64 -> normalized (d-1)/63)
    if (streamer.readFloat(floatVal))
        setParam(kOscBParticleDensityId, static_cast<double>((floatVal - 1.0f) / 63.0f));
    // Particle: Lifetime (5-2000 -> normalized (lt-5)/1995)
    if (streamer.readFloat(floatVal))
        setParam(kOscBParticleLifetimeId, static_cast<double>((floatVal - 5.0f) / 1995.0f));
    // Particle: SpawnMode (int 0-2 -> normalized /2)
    if (streamer.readInt32(intVal))
        setParam(kOscBParticleSpawnModeId, static_cast<double>(intVal) / 2.0);
    // Particle: EnvType (int 0-5 -> normalized /5)
    if (streamer.readInt32(intVal))
        setParam(kOscBParticleEnvTypeId, static_cast<double>(intVal) / 5.0);
    // Particle: Drift (0-1 identity)
    if (streamer.readFloat(floatVal))
        setParam(kOscBParticleDriftId, static_cast<double>(floatVal));

    // Formant: Vowel (int 0-4 -> normalized /4)
    if (streamer.readInt32(intVal))
        setParam(kOscBFormantVowelId, static_cast<double>(intVal) / 4.0);
    // Formant: Morph (0-4 -> normalized /4)
    if (streamer.readFloat(floatVal))
        setParam(kOscBFormantMorphId, static_cast<double>(floatVal / 4.0f));

    // Spectral: Pitch (-24 to +24 -> normalized (p+24)/48)
    if (streamer.readFloat(floatVal))
        setParam(kOscBSpectralPitchId, static_cast<double>((floatVal + 24.0f) / 48.0f));
    // Spectral: Tilt (-12 to +12 -> normalized (t+12)/24)
    if (streamer.readFloat(floatVal))
        setParam(kOscBSpectralTiltId, static_cast<double>((floatVal + 12.0f) / 24.0f));
    // Spectral: Formant (-12 to +12 -> normalized (f+12)/24)
    if (streamer.readFloat(floatVal))
        setParam(kOscBSpectralFormantId, static_cast<double>((floatVal + 12.0f) / 24.0f));

    // Noise: Color (int 0-5 -> normalized /5)
    if (streamer.readInt32(intVal))
        setParam(kOscBNoiseColorId, static_cast<double>(intVal) / 5.0);
}

} // namespace Ruinae
