// ==============================================================================
// Unit Test: Oscillator Type-Specific Parameters
// ==============================================================================
// Verifies:
// - T010: OscAParams / OscBParams struct defaults for all 30 new fields
// - T011: handleOscAParamChange() / handleOscBParamChange() denormalization
//
// Reference: specs/068-osc-type-params/spec.md FR-007, FR-008
//            specs/068-osc-type-params/contracts/parameter-routing.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"

using Catch::Approx;

// ==============================================================================
// T010: OscAParams Struct Defaults
// ==============================================================================

TEST_CASE("OscAParams type-specific defaults", "[osc-params][defaults]") {
    Ruinae::OscAParams params;

    SECTION("Existing field defaults are intact") {
        CHECK(params.type.load() == 0);
        CHECK(params.tuneSemitones.load() == Approx(0.0f));
        CHECK(params.fineCents.load() == Approx(0.0f));
        CHECK(params.level.load() == Approx(1.0f));
        CHECK(params.phase.load() == Approx(0.0f));
    }

    SECTION("PolyBLEP defaults") {
        CHECK(params.waveform.load() == 1);        // Sawtooth
        CHECK(params.pulseWidth.load() == Approx(0.5f));
        CHECK(params.phaseMod.load() == Approx(0.0f));
        CHECK(params.freqMod.load() == Approx(0.0f));
    }

    SECTION("Phase Distortion defaults") {
        CHECK(params.pdWaveform.load() == 0);       // Saw
        CHECK(params.pdDistortion.load() == Approx(0.0f));
    }

    SECTION("Sync defaults") {
        CHECK(params.syncRatio.load() == Approx(2.0f));
        CHECK(params.syncWaveform.load() == 1);     // Sawtooth
        CHECK(params.syncMode.load() == 0);          // Hard
        CHECK(params.syncAmount.load() == Approx(1.0f));
        CHECK(params.syncPulseWidth.load() == Approx(0.5f));
    }

    SECTION("Additive defaults") {
        CHECK(params.additivePartials.load() == 16);
        CHECK(params.additiveTilt.load() == Approx(0.0f));
        CHECK(params.additiveInharm.load() == Approx(0.0f));
    }

    SECTION("Chaos defaults") {
        CHECK(params.chaosAttractor.load() == 0);    // Lorenz
        CHECK(params.chaosAmount.load() == Approx(0.5f));
        CHECK(params.chaosCoupling.load() == Approx(0.0f));
        CHECK(params.chaosOutput.load() == 0);        // X
    }

    SECTION("Particle defaults") {
        CHECK(params.particleScatter.load() == Approx(3.0f));
        CHECK(params.particleDensity.load() == Approx(16.0f));
        CHECK(params.particleLifetime.load() == Approx(200.0f));
        CHECK(params.particleSpawnMode.load() == 0);  // Regular
        CHECK(params.particleEnvType.load() == 0);     // Hann
        CHECK(params.particleDrift.load() == Approx(0.0f));
    }

    SECTION("Formant defaults") {
        CHECK(params.formantVowel.load() == 0);       // A
        CHECK(params.formantMorph.load() == Approx(0.0f));
    }

    SECTION("Spectral Freeze defaults") {
        CHECK(params.spectralPitch.load() == Approx(0.0f));
        CHECK(params.spectralTilt.load() == Approx(0.0f));
        CHECK(params.spectralFormant.load() == Approx(0.0f));
    }

    SECTION("Noise defaults") {
        CHECK(params.noiseColor.load() == 0);          // White
    }
}

// ==============================================================================
// T010: OscBParams Struct Defaults (mirror)
// ==============================================================================

TEST_CASE("OscBParams type-specific defaults", "[osc-params][defaults]") {
    Ruinae::OscBParams params;

    SECTION("Existing field defaults are intact") {
        CHECK(params.type.load() == 0);
        CHECK(params.tuneSemitones.load() == Approx(0.0f));
        CHECK(params.fineCents.load() == Approx(0.0f));
        CHECK(params.level.load() == Approx(1.0f));
        CHECK(params.phase.load() == Approx(0.0f));
    }

    SECTION("PolyBLEP defaults") {
        CHECK(params.waveform.load() == 1);
        CHECK(params.pulseWidth.load() == Approx(0.5f));
        CHECK(params.phaseMod.load() == Approx(0.0f));
        CHECK(params.freqMod.load() == Approx(0.0f));
    }

    SECTION("Phase Distortion defaults") {
        CHECK(params.pdWaveform.load() == 0);
        CHECK(params.pdDistortion.load() == Approx(0.0f));
    }

    SECTION("Sync defaults") {
        CHECK(params.syncRatio.load() == Approx(2.0f));
        CHECK(params.syncWaveform.load() == 1);
        CHECK(params.syncMode.load() == 0);
        CHECK(params.syncAmount.load() == Approx(1.0f));
        CHECK(params.syncPulseWidth.load() == Approx(0.5f));
    }

    SECTION("Additive defaults") {
        CHECK(params.additivePartials.load() == 16);
        CHECK(params.additiveTilt.load() == Approx(0.0f));
        CHECK(params.additiveInharm.load() == Approx(0.0f));
    }

    SECTION("Chaos defaults") {
        CHECK(params.chaosAttractor.load() == 0);
        CHECK(params.chaosAmount.load() == Approx(0.5f));
        CHECK(params.chaosCoupling.load() == Approx(0.0f));
        CHECK(params.chaosOutput.load() == 0);
    }

    SECTION("Particle defaults") {
        CHECK(params.particleScatter.load() == Approx(3.0f));
        CHECK(params.particleDensity.load() == Approx(16.0f));
        CHECK(params.particleLifetime.load() == Approx(200.0f));
        CHECK(params.particleSpawnMode.load() == 0);
        CHECK(params.particleEnvType.load() == 0);
        CHECK(params.particleDrift.load() == Approx(0.0f));
    }

    SECTION("Formant defaults") {
        CHECK(params.formantVowel.load() == 0);
        CHECK(params.formantMorph.load() == Approx(0.0f));
    }

    SECTION("Spectral Freeze defaults") {
        CHECK(params.spectralPitch.load() == Approx(0.0f));
        CHECK(params.spectralTilt.load() == Approx(0.0f));
        CHECK(params.spectralFormant.load() == Approx(0.0f));
    }

    SECTION("Noise defaults") {
        CHECK(params.noiseColor.load() == 0);
    }
}

// ==============================================================================
// T011: handleOscAParamChange() Denormalization
// ==============================================================================

TEST_CASE("handleOscAParamChange denormalization", "[osc-params][denorm]") {
    using namespace Ruinae;
    OscAParams params;

    SECTION("Waveform (110): dropdown int 0-4") {
        handleOscAParamChange(params, kOscAWaveformId, 0.0);
        CHECK(params.waveform.load() == 0);  // Sine
        handleOscAParamChange(params, kOscAWaveformId, 0.5);
        CHECK(params.waveform.load() == 2);  // Square
        handleOscAParamChange(params, kOscAWaveformId, 0.75);
        CHECK(params.waveform.load() == 3);  // Pulse
        handleOscAParamChange(params, kOscAWaveformId, 1.0);
        CHECK(params.waveform.load() == 4);  // Triangle
    }

    SECTION("PulseWidth (111): 0.01-0.99") {
        handleOscAParamChange(params, kOscAPulseWidthId, 0.0);
        CHECK(params.pulseWidth.load() == Approx(0.01f).margin(0.001f));
        handleOscAParamChange(params, kOscAPulseWidthId, 0.5);
        CHECK(params.pulseWidth.load() == Approx(0.5f).margin(0.001f));
        handleOscAParamChange(params, kOscAPulseWidthId, 1.0);
        CHECK(params.pulseWidth.load() == Approx(0.99f).margin(0.001f));
    }

    SECTION("PhaseMod (112): -1.0 to +1.0") {
        handleOscAParamChange(params, kOscAPhaseModId, 0.0);
        CHECK(params.phaseMod.load() == Approx(-1.0f).margin(0.001f));
        handleOscAParamChange(params, kOscAPhaseModId, 0.5);
        CHECK(params.phaseMod.load() == Approx(0.0f).margin(0.001f));
        handleOscAParamChange(params, kOscAPhaseModId, 1.0);
        CHECK(params.phaseMod.load() == Approx(1.0f).margin(0.001f));
    }

    SECTION("FreqMod (113): -1.0 to +1.0") {
        handleOscAParamChange(params, kOscAFreqModId, 0.0);
        CHECK(params.freqMod.load() == Approx(-1.0f).margin(0.001f));
        handleOscAParamChange(params, kOscAFreqModId, 0.5);
        CHECK(params.freqMod.load() == Approx(0.0f).margin(0.001f));
        handleOscAParamChange(params, kOscAFreqModId, 1.0);
        CHECK(params.freqMod.load() == Approx(1.0f).margin(0.001f));
    }

    SECTION("PDWaveform (114): dropdown int 0-7") {
        handleOscAParamChange(params, kOscAPDWaveformId, 0.0);
        CHECK(params.pdWaveform.load() == 0);
        handleOscAParamChange(params, kOscAPDWaveformId, 1.0);
        CHECK(params.pdWaveform.load() == 7);
    }

    SECTION("PDDistortion (115): identity 0-1") {
        handleOscAParamChange(params, kOscAPDDistortionId, 0.0);
        CHECK(params.pdDistortion.load() == Approx(0.0f));
        handleOscAParamChange(params, kOscAPDDistortionId, 0.7);
        CHECK(params.pdDistortion.load() == Approx(0.7f).margin(0.001f));
        handleOscAParamChange(params, kOscAPDDistortionId, 1.0);
        CHECK(params.pdDistortion.load() == Approx(1.0f));
    }

    SECTION("SyncRatio (116): 1.0-8.0") {
        handleOscAParamChange(params, kOscASyncRatioId, 0.0);
        CHECK(params.syncRatio.load() == Approx(1.0f).margin(0.001f));
        handleOscAParamChange(params, kOscASyncRatioId, 1.0);
        CHECK(params.syncRatio.load() == Approx(8.0f).margin(0.001f));
    }

    SECTION("SyncWaveform (117): dropdown int 0-4") {
        handleOscAParamChange(params, kOscASyncWaveformId, 0.0);
        CHECK(params.syncWaveform.load() == 0);
        handleOscAParamChange(params, kOscASyncWaveformId, 1.0);
        CHECK(params.syncWaveform.load() == 4);
    }

    SECTION("SyncMode (118): dropdown int 0-2") {
        handleOscAParamChange(params, kOscASyncModeId, 0.0);
        CHECK(params.syncMode.load() == 0);
        handleOscAParamChange(params, kOscASyncModeId, 1.0);
        CHECK(params.syncMode.load() == 2);
    }

    SECTION("SyncAmount (119): identity 0-1") {
        handleOscAParamChange(params, kOscASyncAmountId, 0.0);
        CHECK(params.syncAmount.load() == Approx(0.0f));
        handleOscAParamChange(params, kOscASyncAmountId, 1.0);
        CHECK(params.syncAmount.load() == Approx(1.0f));
    }

    SECTION("SyncPulseWidth (120): 0.01-0.99") {
        handleOscAParamChange(params, kOscASyncPulseWidthId, 0.0);
        CHECK(params.syncPulseWidth.load() == Approx(0.01f).margin(0.001f));
        handleOscAParamChange(params, kOscASyncPulseWidthId, 1.0);
        CHECK(params.syncPulseWidth.load() == Approx(0.99f).margin(0.001f));
    }

    SECTION("AdditivePartials (121): int 1-128") {
        handleOscAParamChange(params, kOscAAdditivePartialsId, 0.0);
        CHECK(params.additivePartials.load() == 1);
        handleOscAParamChange(params, kOscAAdditivePartialsId, 1.0);
        CHECK(params.additivePartials.load() == 128);
    }

    SECTION("AdditiveTilt (122): -24 to +24 dB/oct") {
        handleOscAParamChange(params, kOscAAdditiveTiltId, 0.0);
        CHECK(params.additiveTilt.load() == Approx(-24.0f).margin(0.01f));
        handleOscAParamChange(params, kOscAAdditiveTiltId, 0.5);
        CHECK(params.additiveTilt.load() == Approx(0.0f).margin(0.01f));
        handleOscAParamChange(params, kOscAAdditiveTiltId, 1.0);
        CHECK(params.additiveTilt.load() == Approx(24.0f).margin(0.01f));
    }

    SECTION("AdditiveInharm (123): identity 0-1") {
        handleOscAParamChange(params, kOscAAdditiveInharmId, 0.0);
        CHECK(params.additiveInharm.load() == Approx(0.0f));
        handleOscAParamChange(params, kOscAAdditiveInharmId, 1.0);
        CHECK(params.additiveInharm.load() == Approx(1.0f));
    }

    SECTION("ChaosAttractor (124): dropdown int 0-4") {
        handleOscAParamChange(params, kOscAChaosAttractorId, 0.0);
        CHECK(params.chaosAttractor.load() == 0);
        handleOscAParamChange(params, kOscAChaosAttractorId, 1.0);
        CHECK(params.chaosAttractor.load() == 4);
    }

    SECTION("ChaosAmount (125): identity 0-1") {
        handleOscAParamChange(params, kOscAChaosAmountId, 0.5);
        CHECK(params.chaosAmount.load() == Approx(0.5f));
    }

    SECTION("ChaosCoupling (126): identity 0-1") {
        handleOscAParamChange(params, kOscAChaosCouplingId, 0.3);
        CHECK(params.chaosCoupling.load() == Approx(0.3f).margin(0.001f));
    }

    SECTION("ChaosOutput (127): dropdown int 0-2") {
        handleOscAParamChange(params, kOscAChaosOutputId, 0.0);
        CHECK(params.chaosOutput.load() == 0);
        handleOscAParamChange(params, kOscAChaosOutputId, 1.0);
        CHECK(params.chaosOutput.load() == 2);
    }

    SECTION("ParticleScatter (128): 0-12 st") {
        handleOscAParamChange(params, kOscAParticleScatterId, 0.0);
        CHECK(params.particleScatter.load() == Approx(0.0f));
        handleOscAParamChange(params, kOscAParticleScatterId, 1.0);
        CHECK(params.particleScatter.load() == Approx(12.0f));
    }

    SECTION("ParticleDensity (129): 1.0-64.0 continuous float") {
        handleOscAParamChange(params, kOscAParticleDensityId, 0.0);
        CHECK(params.particleDensity.load() == Approx(1.0f));
        handleOscAParamChange(params, kOscAParticleDensityId, 1.0);
        CHECK(params.particleDensity.load() == Approx(64.0f));
    }

    SECTION("ParticleLifetime (130): 5-2000 ms") {
        handleOscAParamChange(params, kOscAParticleLifetimeId, 0.0);
        CHECK(params.particleLifetime.load() == Approx(5.0f));
        handleOscAParamChange(params, kOscAParticleLifetimeId, 1.0);
        CHECK(params.particleLifetime.load() == Approx(2000.0f));
    }

    SECTION("ParticleSpawnMode (131): dropdown int 0-2") {
        handleOscAParamChange(params, kOscAParticleSpawnModeId, 0.0);
        CHECK(params.particleSpawnMode.load() == 0);
        handleOscAParamChange(params, kOscAParticleSpawnModeId, 1.0);
        CHECK(params.particleSpawnMode.load() == 2);
    }

    SECTION("ParticleEnvType (132): dropdown int 0-5") {
        handleOscAParamChange(params, kOscAParticleEnvTypeId, 0.0);
        CHECK(params.particleEnvType.load() == 0);
        handleOscAParamChange(params, kOscAParticleEnvTypeId, 1.0);
        CHECK(params.particleEnvType.load() == 5);
    }

    SECTION("ParticleDrift (133): identity 0-1") {
        handleOscAParamChange(params, kOscAParticleDriftId, 0.3);
        CHECK(params.particleDrift.load() == Approx(0.3f).margin(0.001f));
    }

    SECTION("FormantVowel (134): dropdown int 0-4") {
        handleOscAParamChange(params, kOscAFormantVowelId, 0.0);
        CHECK(params.formantVowel.load() == 0);
        handleOscAParamChange(params, kOscAFormantVowelId, 1.0);
        CHECK(params.formantVowel.load() == 4);
    }

    SECTION("FormantMorph (135): 0-4") {
        handleOscAParamChange(params, kOscAFormantMorphId, 0.0);
        CHECK(params.formantMorph.load() == Approx(0.0f));
        handleOscAParamChange(params, kOscAFormantMorphId, 1.0);
        CHECK(params.formantMorph.load() == Approx(4.0f));
    }

    SECTION("SpectralPitch (136): -24 to +24 st") {
        handleOscAParamChange(params, kOscASpectralPitchId, 0.0);
        CHECK(params.spectralPitch.load() == Approx(-24.0f).margin(0.01f));
        handleOscAParamChange(params, kOscASpectralPitchId, 0.5);
        CHECK(params.spectralPitch.load() == Approx(0.0f).margin(0.01f));
        handleOscAParamChange(params, kOscASpectralPitchId, 1.0);
        CHECK(params.spectralPitch.load() == Approx(24.0f).margin(0.01f));
    }

    SECTION("SpectralTilt (137): -12 to +12 dB/oct") {
        handleOscAParamChange(params, kOscASpectralTiltId, 0.0);
        CHECK(params.spectralTilt.load() == Approx(-12.0f).margin(0.01f));
        handleOscAParamChange(params, kOscASpectralTiltId, 0.5);
        CHECK(params.spectralTilt.load() == Approx(0.0f).margin(0.01f));
        handleOscAParamChange(params, kOscASpectralTiltId, 1.0);
        CHECK(params.spectralTilt.load() == Approx(12.0f).margin(0.01f));
    }

    SECTION("SpectralFormant (138): -12 to +12 st") {
        handleOscAParamChange(params, kOscASpectralFormantId, 0.0);
        CHECK(params.spectralFormant.load() == Approx(-12.0f).margin(0.01f));
        handleOscAParamChange(params, kOscASpectralFormantId, 0.5);
        CHECK(params.spectralFormant.load() == Approx(0.0f).margin(0.01f));
        handleOscAParamChange(params, kOscASpectralFormantId, 1.0);
        CHECK(params.spectralFormant.load() == Approx(12.0f).margin(0.01f));
    }

    SECTION("NoiseColor (139): dropdown int 0-5") {
        handleOscAParamChange(params, kOscANoiseColorId, 0.0);
        CHECK(params.noiseColor.load() == 0);
        handleOscAParamChange(params, kOscANoiseColorId, 1.0);
        CHECK(params.noiseColor.load() == 5);
    }
}

// ==============================================================================
// T011: handleOscBParamChange() Denormalization (representative subset)
// ==============================================================================

TEST_CASE("handleOscBParamChange denormalization", "[osc-params][denorm]") {
    using namespace Ruinae;
    OscBParams params;

    SECTION("Waveform (210): dropdown int 0-4") {
        handleOscBParamChange(params, kOscBWaveformId, 0.0);
        CHECK(params.waveform.load() == 0);
        handleOscBParamChange(params, kOscBWaveformId, 1.0);
        CHECK(params.waveform.load() == 4);
    }

    SECTION("PulseWidth (211): 0.01-0.99") {
        handleOscBParamChange(params, kOscBPulseWidthId, 0.0);
        CHECK(params.pulseWidth.load() == Approx(0.01f).margin(0.001f));
        handleOscBParamChange(params, kOscBPulseWidthId, 1.0);
        CHECK(params.pulseWidth.load() == Approx(0.99f).margin(0.001f));
    }

    SECTION("PhaseMod (212): -1.0 to +1.0") {
        handleOscBParamChange(params, kOscBPhaseModId, 0.0);
        CHECK(params.phaseMod.load() == Approx(-1.0f).margin(0.001f));
        handleOscBParamChange(params, kOscBPhaseModId, 1.0);
        CHECK(params.phaseMod.load() == Approx(1.0f).margin(0.001f));
    }

    SECTION("SyncRatio (216): 1.0-8.0") {
        handleOscBParamChange(params, kOscBSyncRatioId, 0.0);
        CHECK(params.syncRatio.load() == Approx(1.0f).margin(0.001f));
        handleOscBParamChange(params, kOscBSyncRatioId, 1.0);
        CHECK(params.syncRatio.load() == Approx(8.0f).margin(0.001f));
    }

    SECTION("AdditivePartials (221): int 1-128") {
        handleOscBParamChange(params, kOscBAdditivePartialsId, 0.0);
        CHECK(params.additivePartials.load() == 1);
        handleOscBParamChange(params, kOscBAdditivePartialsId, 1.0);
        CHECK(params.additivePartials.load() == 128);
    }

    SECTION("ChaosAttractor (224): dropdown int 0-4") {
        handleOscBParamChange(params, kOscBChaosAttractorId, 0.0);
        CHECK(params.chaosAttractor.load() == 0);
        handleOscBParamChange(params, kOscBChaosAttractorId, 1.0);
        CHECK(params.chaosAttractor.load() == 4);
    }

    SECTION("ParticleDensity (229): 1.0-64.0 continuous float") {
        handleOscBParamChange(params, kOscBParticleDensityId, 0.0);
        CHECK(params.particleDensity.load() == Approx(1.0f));
        handleOscBParamChange(params, kOscBParticleDensityId, 1.0);
        CHECK(params.particleDensity.load() == Approx(64.0f));
    }

    SECTION("FormantVowel (234): dropdown int 0-4") {
        handleOscBParamChange(params, kOscBFormantVowelId, 0.0);
        CHECK(params.formantVowel.load() == 0);
        handleOscBParamChange(params, kOscBFormantVowelId, 1.0);
        CHECK(params.formantVowel.load() == 4);
    }

    SECTION("NoiseColor (239): dropdown int 0-5") {
        handleOscBParamChange(params, kOscBNoiseColorId, 0.0);
        CHECK(params.noiseColor.load() == 0);
        handleOscBParamChange(params, kOscBNoiseColorId, 1.0);
        CHECK(params.noiseColor.load() == 5);
    }
}

// ==============================================================================
// T016: kParamIdToOscParam Lookup Table Validation
// ==============================================================================

TEST_CASE("kParamIdToOscParam lookup table correctness", "[osc-params][lookup]") {
    using namespace Ruinae;
    using Krate::DSP::OscParam;

    CHECK(kOscTypeSpecificParamCount == 30);

    // Spot-check key entries
    CHECK(kParamIdToOscParam[0] == OscParam::Waveform);
    CHECK(kParamIdToOscParam[1] == OscParam::PulseWidth);
    CHECK(kParamIdToOscParam[2] == OscParam::PhaseModulation);
    CHECK(kParamIdToOscParam[3] == OscParam::FrequencyModulation);
    CHECK(kParamIdToOscParam[4] == OscParam::PDWaveform);
    CHECK(kParamIdToOscParam[5] == OscParam::PDDistortion);
    CHECK(kParamIdToOscParam[6] == OscParam::SyncSlaveRatio);
    CHECK(kParamIdToOscParam[11] == OscParam::AdditiveNumPartials);
    CHECK(kParamIdToOscParam[14] == OscParam::ChaosAttractor);
    CHECK(kParamIdToOscParam[18] == OscParam::ParticleScatter);
    CHECK(kParamIdToOscParam[24] == OscParam::FormantVowel);
    CHECK(kParamIdToOscParam[26] == OscParam::SpectralPitchShift);
    CHECK(kParamIdToOscParam[29] == OscParam::NoiseColor);
}
