// ==============================================================================
// Unit Test: Oscillator State Persistence (Save/Load Round-Trip)
// ==============================================================================
// Verifies:
// - T048: Round-trip save/load preserves all 30 type-specific fields per oscillator
// - T049: Backward compatibility -- old presets (missing new fields) load defaults
//
// Reference: specs/068-osc-type-params/spec.md FR-011, FR-012
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

using Catch::Approx;

// ==============================================================================
// T048: OscAParams Round-Trip Save/Load
// ==============================================================================

TEST_CASE("OscAParams round-trip save/load preserves all fields", "[osc-state][roundtrip]") {
    using namespace Ruinae;

    // Set non-default values for ALL fields
    OscAParams src;
    src.type.store(3, std::memory_order_relaxed);             // Sync
    src.tuneSemitones.store(7.0f, std::memory_order_relaxed);
    src.fineCents.store(-25.0f, std::memory_order_relaxed);
    src.level.store(0.75f, std::memory_order_relaxed);
    src.phase.store(0.5f, std::memory_order_relaxed);

    // PolyBLEP
    src.waveform.store(3, std::memory_order_relaxed);         // Pulse
    src.pulseWidth.store(0.25f, std::memory_order_relaxed);
    src.phaseMod.store(0.6f, std::memory_order_relaxed);
    src.freqMod.store(-0.3f, std::memory_order_relaxed);

    // Phase Distortion
    src.pdWaveform.store(5, std::memory_order_relaxed);       // ResonantSaw
    src.pdDistortion.store(0.7f, std::memory_order_relaxed);

    // Sync
    src.syncRatio.store(3.5f, std::memory_order_relaxed);
    src.syncWaveform.store(2, std::memory_order_relaxed);     // Square
    src.syncMode.store(1, std::memory_order_relaxed);         // Reverse
    src.syncAmount.store(0.8f, std::memory_order_relaxed);
    src.syncPulseWidth.store(0.3f, std::memory_order_relaxed);

    // Additive
    src.additivePartials.store(64, std::memory_order_relaxed);
    src.additiveTilt.store(-6.0f, std::memory_order_relaxed);
    src.additiveInharm.store(0.4f, std::memory_order_relaxed);

    // Chaos
    src.chaosAttractor.store(2, std::memory_order_relaxed);   // Chua
    src.chaosAmount.store(0.7f, std::memory_order_relaxed);
    src.chaosCoupling.store(0.3f, std::memory_order_relaxed);
    src.chaosOutput.store(1, std::memory_order_relaxed);      // Y

    // Particle
    src.particleScatter.store(6.0f, std::memory_order_relaxed);
    src.particleDensity.store(32.0f, std::memory_order_relaxed);
    src.particleLifetime.store(500.0f, std::memory_order_relaxed);
    src.particleSpawnMode.store(1, std::memory_order_relaxed);  // Random
    src.particleEnvType.store(3, std::memory_order_relaxed);    // Blackman
    src.particleDrift.store(0.5f, std::memory_order_relaxed);

    // Formant
    src.formantVowel.store(2, std::memory_order_relaxed);     // I
    src.formantMorph.store(2.5f, std::memory_order_relaxed);

    // Spectral Freeze
    src.spectralPitch.store(12.0f, std::memory_order_relaxed);
    src.spectralTilt.store(-6.0f, std::memory_order_relaxed);
    src.spectralFormant.store(3.0f, std::memory_order_relaxed);

    // Noise
    src.noiseColor.store(4, std::memory_order_relaxed);       // Violet

    // Save
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    saveOscAParams(src, streamer);

    // Load into fresh struct
    OscAParams dst;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(&stream, kLittleEndian);
    bool result = loadOscAParams(dst, reader);
    REQUIRE(result);

    // Verify existing fields
    CHECK(dst.type.load() == 3);
    CHECK(dst.tuneSemitones.load() == Approx(7.0f));
    CHECK(dst.fineCents.load() == Approx(-25.0f));
    CHECK(dst.level.load() == Approx(0.75f));
    CHECK(dst.phase.load() == Approx(0.5f));

    // Verify type-specific fields
    CHECK(dst.waveform.load() == 3);
    CHECK(dst.pulseWidth.load() == Approx(0.25f));
    CHECK(dst.phaseMod.load() == Approx(0.6f));
    CHECK(dst.freqMod.load() == Approx(-0.3f));

    CHECK(dst.pdWaveform.load() == 5);
    CHECK(dst.pdDistortion.load() == Approx(0.7f));

    CHECK(dst.syncRatio.load() == Approx(3.5f));
    CHECK(dst.syncWaveform.load() == 2);
    CHECK(dst.syncMode.load() == 1);
    CHECK(dst.syncAmount.load() == Approx(0.8f));
    CHECK(dst.syncPulseWidth.load() == Approx(0.3f));

    CHECK(dst.additivePartials.load() == 64);
    CHECK(dst.additiveTilt.load() == Approx(-6.0f));
    CHECK(dst.additiveInharm.load() == Approx(0.4f));

    CHECK(dst.chaosAttractor.load() == 2);
    CHECK(dst.chaosAmount.load() == Approx(0.7f));
    CHECK(dst.chaosCoupling.load() == Approx(0.3f));
    CHECK(dst.chaosOutput.load() == 1);

    CHECK(dst.particleScatter.load() == Approx(6.0f));
    CHECK(dst.particleDensity.load() == Approx(32.0f));
    CHECK(dst.particleLifetime.load() == Approx(500.0f));
    CHECK(dst.particleSpawnMode.load() == 1);
    CHECK(dst.particleEnvType.load() == 3);
    CHECK(dst.particleDrift.load() == Approx(0.5f));

    CHECK(dst.formantVowel.load() == 2);
    CHECK(dst.formantMorph.load() == Approx(2.5f));

    CHECK(dst.spectralPitch.load() == Approx(12.0f));
    CHECK(dst.spectralTilt.load() == Approx(-6.0f));
    CHECK(dst.spectralFormant.load() == Approx(3.0f));

    CHECK(dst.noiseColor.load() == 4);
}

// ==============================================================================
// T048: OscBParams Round-Trip Save/Load
// ==============================================================================

TEST_CASE("OscBParams round-trip save/load preserves all fields", "[osc-state][roundtrip]") {
    using namespace Ruinae;

    // Set non-default values for ALL fields
    OscBParams src;
    src.type.store(5, std::memory_order_relaxed);             // Chaos
    src.tuneSemitones.store(-12.0f, std::memory_order_relaxed);
    src.fineCents.store(50.0f, std::memory_order_relaxed);
    src.level.store(0.9f, std::memory_order_relaxed);
    src.phase.store(0.25f, std::memory_order_relaxed);

    // PolyBLEP
    src.waveform.store(4, std::memory_order_relaxed);         // Triangle
    src.pulseWidth.store(0.8f, std::memory_order_relaxed);
    src.phaseMod.store(-0.5f, std::memory_order_relaxed);
    src.freqMod.store(0.9f, std::memory_order_relaxed);

    // Phase Distortion
    src.pdWaveform.store(7, std::memory_order_relaxed);       // ResonantTrapezoid
    src.pdDistortion.store(0.9f, std::memory_order_relaxed);

    // Sync
    src.syncRatio.store(5.0f, std::memory_order_relaxed);
    src.syncWaveform.store(0, std::memory_order_relaxed);     // Sine
    src.syncMode.store(2, std::memory_order_relaxed);         // PhaseAdvance
    src.syncAmount.store(0.3f, std::memory_order_relaxed);
    src.syncPulseWidth.store(0.1f, std::memory_order_relaxed);

    // Additive
    src.additivePartials.store(128, std::memory_order_relaxed);
    src.additiveTilt.store(12.0f, std::memory_order_relaxed);
    src.additiveInharm.store(0.8f, std::memory_order_relaxed);

    // Chaos
    src.chaosAttractor.store(4, std::memory_order_relaxed);   // VanDerPol
    src.chaosAmount.store(0.9f, std::memory_order_relaxed);
    src.chaosCoupling.store(0.6f, std::memory_order_relaxed);
    src.chaosOutput.store(2, std::memory_order_relaxed);      // Z

    // Particle
    src.particleScatter.store(10.0f, std::memory_order_relaxed);
    src.particleDensity.store(48.0f, std::memory_order_relaxed);
    src.particleLifetime.store(1500.0f, std::memory_order_relaxed);
    src.particleSpawnMode.store(2, std::memory_order_relaxed);  // Burst
    src.particleEnvType.store(5, std::memory_order_relaxed);    // Exponential
    src.particleDrift.store(0.9f, std::memory_order_relaxed);

    // Formant
    src.formantVowel.store(4, std::memory_order_relaxed);     // U
    src.formantMorph.store(3.7f, std::memory_order_relaxed);

    // Spectral Freeze
    src.spectralPitch.store(-18.0f, std::memory_order_relaxed);
    src.spectralTilt.store(8.0f, std::memory_order_relaxed);
    src.spectralFormant.store(-10.0f, std::memory_order_relaxed);

    // Noise
    src.noiseColor.store(2, std::memory_order_relaxed);       // Brown

    // Save
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    saveOscBParams(src, streamer);

    // Load into fresh struct
    OscBParams dst;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(&stream, kLittleEndian);
    bool result = loadOscBParams(dst, reader);
    REQUIRE(result);

    // Verify existing fields
    CHECK(dst.type.load() == 5);
    CHECK(dst.tuneSemitones.load() == Approx(-12.0f));
    CHECK(dst.fineCents.load() == Approx(50.0f));
    CHECK(dst.level.load() == Approx(0.9f));
    CHECK(dst.phase.load() == Approx(0.25f));

    // Verify type-specific fields
    CHECK(dst.waveform.load() == 4);
    CHECK(dst.pulseWidth.load() == Approx(0.8f));
    CHECK(dst.phaseMod.load() == Approx(-0.5f));
    CHECK(dst.freqMod.load() == Approx(0.9f));

    CHECK(dst.pdWaveform.load() == 7);
    CHECK(dst.pdDistortion.load() == Approx(0.9f));

    CHECK(dst.syncRatio.load() == Approx(5.0f));
    CHECK(dst.syncWaveform.load() == 0);
    CHECK(dst.syncMode.load() == 2);
    CHECK(dst.syncAmount.load() == Approx(0.3f));
    CHECK(dst.syncPulseWidth.load() == Approx(0.1f));

    CHECK(dst.additivePartials.load() == 128);
    CHECK(dst.additiveTilt.load() == Approx(12.0f));
    CHECK(dst.additiveInharm.load() == Approx(0.8f));

    CHECK(dst.chaosAttractor.load() == 4);
    CHECK(dst.chaosAmount.load() == Approx(0.9f));
    CHECK(dst.chaosCoupling.load() == Approx(0.6f));
    CHECK(dst.chaosOutput.load() == 2);

    CHECK(dst.particleScatter.load() == Approx(10.0f));
    CHECK(dst.particleDensity.load() == Approx(48.0f));
    CHECK(dst.particleLifetime.load() == Approx(1500.0f));
    CHECK(dst.particleSpawnMode.load() == 2);
    CHECK(dst.particleEnvType.load() == 5);
    CHECK(dst.particleDrift.load() == Approx(0.9f));

    CHECK(dst.formantVowel.load() == 4);
    CHECK(dst.formantMorph.load() == Approx(3.7f));

    CHECK(dst.spectralPitch.load() == Approx(-18.0f));
    CHECK(dst.spectralTilt.load() == Approx(8.0f));
    CHECK(dst.spectralFormant.load() == Approx(-10.0f));

    CHECK(dst.noiseColor.load() == 2);
}

// ==============================================================================
// T049: Backward Compatibility -- Old Presets Without Type-Specific Data
// ==============================================================================

TEST_CASE("OscAParams loadOscAParams with old preset (no type-specific data) uses defaults",
          "[osc-state][backward-compat]") {
    using namespace Ruinae;

    // Construct a stream containing ONLY the 5 existing fields (old format)
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer writer(&stream, kLittleEndian);

    // Write only the original 5 fields (simulating an old preset)
    writer.writeInt32(3);       // type = Sync
    writer.writeFloat(5.0f);    // tuneSemitones
    writer.writeFloat(-10.0f);  // fineCents
    writer.writeFloat(0.8f);    // level
    writer.writeFloat(0.3f);    // phase

    // Load into a fresh struct
    OscAParams params;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(&stream, kLittleEndian);
    bool result = loadOscAParams(params, reader);
    REQUIRE(result);

    // Existing fields should be loaded
    CHECK(params.type.load() == 3);
    CHECK(params.tuneSemitones.load() == Approx(5.0f));
    CHECK(params.fineCents.load() == Approx(-10.0f));
    CHECK(params.level.load() == Approx(0.8f));
    CHECK(params.phase.load() == Approx(0.3f));

    // All new fields should retain their spec-defined defaults
    CHECK(params.waveform.load() == 1);           // Sawtooth
    CHECK(params.pulseWidth.load() == Approx(0.5f));
    CHECK(params.phaseMod.load() == Approx(0.0f));
    CHECK(params.freqMod.load() == Approx(0.0f));

    CHECK(params.pdWaveform.load() == 0);          // Saw
    CHECK(params.pdDistortion.load() == Approx(0.0f));

    CHECK(params.syncRatio.load() == Approx(2.0f));
    CHECK(params.syncWaveform.load() == 1);        // Sawtooth
    CHECK(params.syncMode.load() == 0);             // Hard
    CHECK(params.syncAmount.load() == Approx(1.0f));
    CHECK(params.syncPulseWidth.load() == Approx(0.5f));

    CHECK(params.additivePartials.load() == 16);
    CHECK(params.additiveTilt.load() == Approx(0.0f));
    CHECK(params.additiveInharm.load() == Approx(0.0f));

    CHECK(params.chaosAttractor.load() == 0);       // Lorenz
    CHECK(params.chaosAmount.load() == Approx(0.5f));
    CHECK(params.chaosCoupling.load() == Approx(0.0f));
    CHECK(params.chaosOutput.load() == 0);           // X

    CHECK(params.particleScatter.load() == Approx(3.0f));
    CHECK(params.particleDensity.load() == Approx(16.0f));
    CHECK(params.particleLifetime.load() == Approx(200.0f));
    CHECK(params.particleSpawnMode.load() == 0);    // Regular
    CHECK(params.particleEnvType.load() == 0);       // Hann
    CHECK(params.particleDrift.load() == Approx(0.0f));

    CHECK(params.formantVowel.load() == 0);          // A
    CHECK(params.formantMorph.load() == Approx(0.0f));

    CHECK(params.spectralPitch.load() == Approx(0.0f));
    CHECK(params.spectralTilt.load() == Approx(0.0f));
    CHECK(params.spectralFormant.load() == Approx(0.0f));

    CHECK(params.noiseColor.load() == 0);            // White
}

TEST_CASE("OscBParams loadOscBParams with old preset (no type-specific data) uses defaults",
          "[osc-state][backward-compat]") {
    using namespace Ruinae;

    // Construct a stream containing ONLY the 5 existing fields (old format)
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer writer(&stream, kLittleEndian);

    // Write only the original 5 fields (simulating an old preset)
    writer.writeInt32(7);       // type = Formant
    writer.writeFloat(-3.0f);   // tuneSemitones
    writer.writeFloat(20.0f);   // fineCents
    writer.writeFloat(0.5f);    // level
    writer.writeFloat(1.0f);    // phase

    // Load into a fresh struct
    OscBParams params;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(&stream, kLittleEndian);
    bool result = loadOscBParams(params, reader);
    REQUIRE(result);

    // Existing fields should be loaded
    CHECK(params.type.load() == 7);
    CHECK(params.tuneSemitones.load() == Approx(-3.0f));
    CHECK(params.fineCents.load() == Approx(20.0f));
    CHECK(params.level.load() == Approx(0.5f));
    CHECK(params.phase.load() == Approx(1.0f));

    // All new fields should retain their spec-defined defaults
    CHECK(params.waveform.load() == 1);            // Sawtooth
    CHECK(params.pulseWidth.load() == Approx(0.5f));
    CHECK(params.phaseMod.load() == Approx(0.0f));
    CHECK(params.freqMod.load() == Approx(0.0f));

    CHECK(params.pdWaveform.load() == 0);
    CHECK(params.pdDistortion.load() == Approx(0.0f));

    CHECK(params.syncRatio.load() == Approx(2.0f));
    CHECK(params.syncWaveform.load() == 1);
    CHECK(params.syncMode.load() == 0);
    CHECK(params.syncAmount.load() == Approx(1.0f));
    CHECK(params.syncPulseWidth.load() == Approx(0.5f));

    CHECK(params.additivePartials.load() == 16);
    CHECK(params.additiveTilt.load() == Approx(0.0f));
    CHECK(params.additiveInharm.load() == Approx(0.0f));

    CHECK(params.chaosAttractor.load() == 0);
    CHECK(params.chaosAmount.load() == Approx(0.5f));
    CHECK(params.chaosCoupling.load() == Approx(0.0f));
    CHECK(params.chaosOutput.load() == 0);

    CHECK(params.particleScatter.load() == Approx(3.0f));
    CHECK(params.particleDensity.load() == Approx(16.0f));
    CHECK(params.particleLifetime.load() == Approx(200.0f));
    CHECK(params.particleSpawnMode.load() == 0);
    CHECK(params.particleEnvType.load() == 0);
    CHECK(params.particleDrift.load() == Approx(0.0f));

    CHECK(params.formantVowel.load() == 0);
    CHECK(params.formantMorph.load() == Approx(0.0f));

    CHECK(params.spectralPitch.load() == Approx(0.0f));
    CHECK(params.spectralTilt.load() == Approx(0.0f));
    CHECK(params.spectralFormant.load() == Approx(0.0f));

    CHECK(params.noiseColor.load() == 0);
}
