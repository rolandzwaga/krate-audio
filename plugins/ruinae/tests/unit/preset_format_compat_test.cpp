// ==============================================================================
// Unit Test: Preset Format Compatibility
// ==============================================================================
// Verifies that the preset generator's format header (ruinae_preset_format.h)
// produces byte-identical output to the processor's getState(). This catches
// drift between the two serialization paths — e.g., a field added to the
// processor but missing from the format header.
//
// These tests run in CI as part of ruinae_tests, gating releases.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ruinae_preset_format.h"

#include "processor/processor.h"
#include "plugin_ids.h"
#include "drain_preset_transfer.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <vector>
#include <cstring>

using Catch::Approx;

// =============================================================================
// Helper: create and initialize a Processor
// =============================================================================

static std::unique_ptr<Ruinae::Processor> makeProcessor() {
    auto p = std::make_unique<Ruinae::Processor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

// =============================================================================
// Helper: extract all bytes from a MemoryStream
// =============================================================================

static std::vector<char> extractStreamBytes(Steinberg::MemoryStream& stream) {
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> data(static_cast<size_t>(size));
    Steinberg::int32 bytesRead = 0;
    stream.read(data.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return data;
}

// =============================================================================
// Test 1: Default state size matches processor
// =============================================================================

TEST_CASE("Generator format size matches processor getState",
          "[preset-compat][format]") {
    // Serialize default state using format header
    RuinaeFormat::RuinaePresetState formatState;
    auto formatBytes = formatState.serialize();

    // Get default state from processor
    auto proc = makeProcessor();
    Steinberg::MemoryStream procStream;
    REQUIRE(proc->getState(&procStream) == Steinberg::kResultTrue);
    auto procBytes = extractStreamBytes(procStream);

    // Size must match exactly — any difference means a field was added/removed
    // in the processor but not in the format header (or vice versa)
    INFO("Format header serialized " << formatBytes.size() << " bytes, "
         "processor getState wrote " << procBytes.size() << " bytes");
    REQUIRE(formatBytes.size() == procBytes.size());

    proc->terminate();
}

// =============================================================================
// Test 2: Default state bytes match processor byte-for-byte
// =============================================================================

TEST_CASE("Generator format default bytes match processor getState",
          "[preset-compat][format]") {
    // Serialize default state using format header
    RuinaeFormat::RuinaePresetState formatState;
    auto formatBytes = formatState.serialize();

    // Get default state from processor
    auto proc = makeProcessor();
    Steinberg::MemoryStream procStream;
    REQUIRE(proc->getState(&procStream) == Steinberg::kResultTrue);
    auto procBytes = extractStreamBytes(procStream);

    REQUIRE(formatBytes.size() == procBytes.size());

    // Compare byte-for-byte — catches field reordering, different defaults,
    // or serialization order mismatches
    bool match = std::memcmp(
        formatBytes.data(), procBytes.data(), formatBytes.size()) == 0;

    if (!match) {
        // Find first divergence point for a useful error message
        for (size_t i = 0; i < formatBytes.size(); ++i) {
            if (formatBytes[i] != static_cast<uint8_t>(procBytes[i])) {
                INFO("First byte mismatch at offset " << i
                     << " (format=0x" << std::hex
                     << static_cast<int>(formatBytes[i])
                     << ", processor=0x"
                     << static_cast<int>(static_cast<uint8_t>(procBytes[i]))
                     << ")");
                REQUIRE(false);
                break;
            }
        }
    }

    proc->terminate();
}

// =============================================================================
// Test 3: Sentinel values survive round-trip through processor
// =============================================================================

TEST_CASE("Sentinel values survive round-trip through processor",
          "[preset-compat][format]") {
    // Create format state with distinctive non-default values spread across
    // ALL sections to detect field reordering or offset errors
    RuinaeFormat::RuinaePresetState formatState;

    // Global
    formatState.global.masterGain = 0.42f;
    formatState.global.voiceMode = 1;
    formatState.global.polyphony = 4;
    formatState.global.softLimit = 0;
    formatState.global.width = 1.5f;
    formatState.global.spread = 0.33f;

    // Osc A
    formatState.oscA.type = 3;
    formatState.oscA.level = 0.77f;
    formatState.oscA.tuneSemitones = -7.0f;
    formatState.oscA.fineCents = 12.5f;

    // Osc B
    formatState.oscB.type = 5;
    formatState.oscB.level = 0.33f;
    formatState.oscB.waveform = 2;

    // Mixer
    formatState.mixer.mode = 1;
    formatState.mixer.position = 0.7f;

    // Filter
    formatState.filter.type = 2;
    formatState.filter.cutoffHz = 5000.0f;
    formatState.filter.resonance = 0.6f;

    // Distortion (including ring mod fields that caused the original bug)
    formatState.distortion.type = 3;
    formatState.distortion.drive = 0.55f;
    formatState.distortion.ringFreq = 0.25f;
    formatState.distortion.ringFreqMode = 0;
    formatState.distortion.ringRatio = 0.5f;
    formatState.distortion.ringWaveform = 2;
    formatState.distortion.ringStereoSpread = 0.4f;

    // Trance Gate
    formatState.tranceGate.enabled = 1;
    formatState.tranceGate.numSteps = 8;
    formatState.tranceGate.euclideanEnabled = 1;

    // Amp Env
    formatState.ampEnv.attackMs = 50.0f;
    formatState.ampEnv.decayMs = 200.0f;
    formatState.ampEnv.sustain = 0.5f;
    formatState.ampEnv.releaseMs = 400.0f;

    // Filter Env
    formatState.filterEnv.attackMs = 20.0f;

    // Mod Env
    formatState.modEnv.decayMs = 500.0f;

    // LFO1
    formatState.lfo1.rateHz = 2.5f;
    formatState.lfo1.shape = 3;

    // LFO2
    formatState.lfo2.depth = 0.6f;

    // Chaos Mod
    formatState.chaosMod.type = 1;
    formatState.chaosMod.depth = 0.4f;

    // Mod Matrix (slot 0)
    formatState.modMatrix.slots[0].source = 2;
    formatState.modMatrix.slots[0].dest = 5;
    formatState.modMatrix.slots[0].amount = 0.75f;

    // Global Filter
    formatState.globalFilter.enabled = 1;
    formatState.globalFilter.cutoffHz = 2000.0f;

    // Delay
    formatState.delay.type = 2;
    formatState.delay.timeMs = 333.0f;
    formatState.delay.feedback = 0.6f;
    formatState.delay.mix = 0.3f;

    // Reverb
    formatState.reverb.size = 0.8f;
    formatState.reverb.mix = 0.25f;

    // Mono Mode
    formatState.monoMode.legato = 1;
    formatState.monoMode.portamentoTimeMs = 50.0f;

    // Voice route 0
    formatState.voiceRoutes[0].source = 1;
    formatState.voiceRoutes[0].destination = 3;
    formatState.voiceRoutes[0].amount = 0.5f;
    formatState.voiceRoutes[0].active = 1;

    // FX enable flags
    formatState.delayEnabled = 1;
    formatState.reverbEnabled = 0;
    formatState.modulationType = 1;

    // Phaser
    formatState.phaser.rateHz = 1.5f;
    formatState.phaser.depth = 0.7f;

    // Extended LFO
    formatState.lfo1Ext.phaseOffset = 0.25f;
    formatState.lfo1Ext.retrigger = 0;
    formatState.lfo2Ext.unipolar = 1;

    // Macros
    formatState.macros.values[0] = 0.3f;
    formatState.macros.values[2] = 0.7f;

    // Rungler
    formatState.rungler.depth = 0.5f;
    formatState.rungler.bits = 4;

    // Settings
    formatState.settings.pitchBendRangeSemitones = 12.0f;
    formatState.settings.velocityCurve = 2;

    // Mod sources
    formatState.envFollower.sensitivity = 0.8f;
    formatState.sampleHold.rateHz = 8.0f;
    formatState.random.smoothness = 0.5f;
    formatState.pitchFollower.minHz = 100.0f;
    formatState.transient.sensitivity = 0.3f;

    // Harmonizer
    formatState.harmonizer.harmonyMode = 1;
    formatState.harmonizer.key = 5;
    formatState.harmonizer.scale = 2;
    formatState.harmonizer.voiceInterval[0] = 3;
    formatState.harmonizer.voiceLevelDb[0] = -3.0f;
    formatState.harmonizerEnabled = 0;

    // Arp (including scale mode fields that caused the original bug)
    formatState.arp.operatingMode = 1;
    formatState.arp.mode = 2;
    formatState.arp.octaveRange = 3;
    formatState.arp.scaleType = 3;
    formatState.arp.rootNote = 5;
    formatState.arp.scaleQuantizeInput = 1;
    formatState.arp.spice = 0.45f;
    formatState.arp.humanize = 0.2f;
    formatState.arp.ratchetSwing = 65.0f;

    // Serialize using format header
    auto formatBytes = formatState.serialize();

    // Feed into processor via setState
    Steinberg::MemoryStream loadStream;
    loadStream.write(formatBytes.data(),
        static_cast<Steinberg::int32>(formatBytes.size()), nullptr);
    loadStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    auto proc = makeProcessor();
    REQUIRE(proc->setState(&loadStream) == Steinberg::kResultTrue);
    drainPresetTransfer(proc.get());

    // Read back via getState
    Steinberg::MemoryStream saveStream;
    REQUIRE(proc->getState(&saveStream) == Steinberg::kResultTrue);
    auto procBytes = extractStreamBytes(saveStream);

    // Compare sizes first
    REQUIRE(formatBytes.size() == procBytes.size());

    // Compare byte-for-byte — if ANY sentinel value was written to the wrong
    // offset, or the processor read it into the wrong field, the re-saved
    // stream will differ from the original
    bool match = std::memcmp(
        formatBytes.data(), procBytes.data(), formatBytes.size()) == 0;

    if (!match) {
        // Find first divergence for debugging
        for (size_t i = 0; i < formatBytes.size(); ++i) {
            if (formatBytes[i] != static_cast<uint8_t>(procBytes[i])) {
                INFO("First byte mismatch at offset " << i
                     << " (format=0x" << std::hex
                     << static_cast<int>(formatBytes[i])
                     << ", processor=0x"
                     << static_cast<int>(static_cast<uint8_t>(procBytes[i]))
                     << ")");
                REQUIRE(false);
                break;
            }
        }
    }

    proc->terminate();
}
