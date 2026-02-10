// ==============================================================================
// Integration Test: Controller State Synchronization
// ==============================================================================
// Verifies that Controller::setComponentState() synchronizes all parameters
// to match the Processor state stream.
//
// Reference: specs/045-plugin-shell/spec.md FR-012, US4
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <cmath>

using Catch::Approx;

// =============================================================================
// Helpers
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

static Ruinae::Controller* makeControllerRaw() {
    auto* ctrl = new Ruinae::Controller();
    ctrl->initialize(nullptr);
    return ctrl;
}

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Controller syncs default state from Processor", "[controller_state][integration]") {
    auto proc = makeProcessor();
    auto* ctrl = makeControllerRaw();

    // Save processor state
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    // Sync controller
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&stream) == Steinberg::kResultTrue);

    // Verify key default parameters are synced
    // Master Gain default = 1.0 -> normalized 0.5
    CHECK(ctrl->getParamNormalized(Ruinae::kMasterGainId) == Approx(0.5).margin(0.01));

    // Polyphony default = 8 -> normalized (8-1)/15 = 7/15
    CHECK(ctrl->getParamNormalized(Ruinae::kPolyphonyId) == Approx(7.0 / 15.0).margin(0.01));

    // Soft Limit default = true -> 1.0
    CHECK(ctrl->getParamNormalized(Ruinae::kSoftLimitId) == Approx(1.0).margin(0.01));

    // OSC A Level default = 1.0
    CHECK(ctrl->getParamNormalized(Ruinae::kOscALevelId) == Approx(1.0).margin(0.01));

    // Amp Sustain default = 0.8
    CHECK(ctrl->getParamNormalized(Ruinae::kAmpEnvSustainId) == Approx(0.8).margin(0.01));

    proc->terminate();
    ctrl->terminate();
}

TEST_CASE("Controller syncs non-default state from Processor", "[controller_state][integration]") {
    auto proc = makeProcessor();

    // Manually create a state stream with non-default values
    // We'll use the Processor to save, but first we need to get non-default values in there
    // The simplest way: save default state, create a new processor, load it, verify sync

    // For a real non-default test, we create the stream manually
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);

    // Write version
    streamer.writeInt32(1);

    // Global params: masterGain=1.5, voiceMode=1(Mono), polyphony=4, softLimit=false
    streamer.writeFloat(1.5f);
    streamer.writeInt32(1);
    streamer.writeInt32(4);
    streamer.writeInt32(0);

    // OSC A: type=3(Sync), tune=12.0, fine=50.0, level=0.7, phase=0.25
    streamer.writeInt32(3);
    streamer.writeFloat(12.0f);
    streamer.writeFloat(50.0f);
    streamer.writeFloat(0.7f);
    streamer.writeFloat(0.25f);

    // OSC B: type=0, tune=0, fine=0, level=1.0, phase=0
    streamer.writeInt32(0);
    streamer.writeFloat(0.0f);
    streamer.writeFloat(0.0f);
    streamer.writeFloat(1.0f);
    streamer.writeFloat(0.0f);

    // Mixer: mode=0, position=0.5, tilt=0.0
    streamer.writeInt32(0);
    streamer.writeFloat(0.5f);
    streamer.writeFloat(0.0f);

    // Filter: type=0, cutoff=1000.0, resonance=5.0, envAmount=24.0, keyTrack=0.5
    streamer.writeInt32(0);
    streamer.writeFloat(1000.0f);
    streamer.writeFloat(5.0f);
    streamer.writeFloat(24.0f);
    streamer.writeFloat(0.5f);

    // Distortion: type=1, drive=0.5, character=0.5, mix=1.0
    streamer.writeInt32(1);
    streamer.writeFloat(0.5f);
    streamer.writeFloat(0.5f);
    streamer.writeFloat(1.0f);

    // Trance Gate: enabled=false, numSteps=1, rate=4.0, depth=1.0,
    //              attack=2.0, release=10.0, tempoSync=true, noteValue=default
    streamer.writeInt32(0);
    streamer.writeInt32(1);
    streamer.writeFloat(4.0f);
    streamer.writeFloat(1.0f);
    streamer.writeFloat(2.0f);
    streamer.writeFloat(10.0f);
    streamer.writeInt32(1);
    streamer.writeInt32(0);

    // Amp Env: attack=10, decay=100, sustain=0.8, release=200
    streamer.writeFloat(10.0f);
    streamer.writeFloat(100.0f);
    streamer.writeFloat(0.8f);
    streamer.writeFloat(200.0f);

    // Filter Env: attack=10, decay=100, sustain=0.8, release=200
    streamer.writeFloat(10.0f);
    streamer.writeFloat(100.0f);
    streamer.writeFloat(0.8f);
    streamer.writeFloat(200.0f);

    // Mod Env: attack=10, decay=100, sustain=0.8, release=200
    streamer.writeFloat(10.0f);
    streamer.writeFloat(100.0f);
    streamer.writeFloat(0.8f);
    streamer.writeFloat(200.0f);

    // LFO 1: rate=1.0, shape=0, depth=1.0, sync=false
    streamer.writeFloat(1.0f);
    streamer.writeInt32(0);
    streamer.writeFloat(1.0f);
    streamer.writeInt32(0);

    // LFO 2: rate=1.0, shape=0, depth=1.0, sync=false
    streamer.writeFloat(1.0f);
    streamer.writeInt32(0);
    streamer.writeFloat(1.0f);
    streamer.writeInt32(0);

    // Chaos Mod: rate=1.0, type=0, depth=0.5
    streamer.writeFloat(1.0f);
    streamer.writeInt32(0);
    streamer.writeFloat(0.5f);

    // Mod Matrix: 8 slots, all zeros
    for (int i = 0; i < 8; ++i) {
        streamer.writeInt32(0);  // source
        streamer.writeInt32(0);  // dest
        streamer.writeFloat(0.0f); // amount
    }

    // Global Filter: enabled=false, type=0, cutoff=20000.0, resonance=0.1
    streamer.writeInt32(0);
    streamer.writeInt32(0);
    streamer.writeFloat(20000.0f);
    streamer.writeFloat(0.1f);

    // Freeze: enabled=false, freeze=false
    streamer.writeInt32(0);
    streamer.writeInt32(0);

    // Delay: type=0, time=500.0, feedback=0.4, mix=0.0, sync=false, noteValue=0
    streamer.writeInt32(0);
    streamer.writeFloat(500.0f);
    streamer.writeFloat(0.4f);
    streamer.writeFloat(0.0f);
    streamer.writeInt32(0);
    streamer.writeInt32(0);

    // Reverb: size=0.5, damping=0.5, width=1.0, mix=0.3,
    //         preDelay=0.0, diffusion=0.7, freeze=false, modRate=0.5, modDepth=0.0
    streamer.writeFloat(0.5f);
    streamer.writeFloat(0.5f);
    streamer.writeFloat(1.0f);
    streamer.writeFloat(0.3f);
    streamer.writeFloat(0.0f);
    streamer.writeFloat(0.7f);
    streamer.writeInt32(0);
    streamer.writeFloat(0.5f);
    streamer.writeFloat(0.0f);

    // Mono Mode: priority=0, legato=false, portamento=0.0, portaMode=0
    streamer.writeInt32(0);
    streamer.writeInt32(0);
    streamer.writeFloat(0.0f);
    streamer.writeInt32(0);

    // Now sync controller with this state
    auto* ctrl = makeControllerRaw();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&stream) == Steinberg::kResultTrue);

    // Verify the non-default values are synced
    // Master Gain 1.5 -> normalized 1.5/2.0 = 0.75
    CHECK(ctrl->getParamNormalized(Ruinae::kMasterGainId) == Approx(0.75).margin(0.01));

    // Voice Mode = 1 (Mono)
    CHECK(ctrl->getParamNormalized(Ruinae::kVoiceModeId) == Approx(1.0).margin(0.01));

    // Polyphony = 4 -> normalized (4-1)/15 = 0.2
    CHECK(ctrl->getParamNormalized(Ruinae::kPolyphonyId) == Approx(3.0 / 15.0).margin(0.01));

    // OSC A Level = 0.7
    CHECK(ctrl->getParamNormalized(Ruinae::kOscALevelId) == Approx(0.7).margin(0.01));

    // Filter cutoff 1000.0 Hz -> normalized = log(1000/20)/log(1000)
    double expectedCutoffNorm = std::log(1000.0 / 20.0) / std::log(1000.0);
    CHECK(ctrl->getParamNormalized(Ruinae::kFilterCutoffId) == Approx(expectedCutoffNorm).margin(0.02));

    proc->terminate();
    ctrl->terminate();
}

TEST_CASE("Controller handles empty stream in setComponentState", "[controller_state][integration]") {
    auto* ctrl = makeControllerRaw();
    Steinberg::MemoryStream emptyStream;

    // Should not crash and return kResultTrue
    auto result = ctrl->setComponentState(&emptyStream);
    CHECK(result == Steinberg::kResultTrue);

    ctrl->terminate();
}

TEST_CASE("Controller handles null stream in setComponentState", "[controller_state][integration]") {
    auto* ctrl = makeControllerRaw();

    // Null stream should return kResultFalse
    auto result = ctrl->setComponentState(nullptr);
    CHECK(result == Steinberg::kResultFalse);

    ctrl->terminate();
}

TEST_CASE("Controller round-trip: Processor save -> Controller load", "[controller_state][integration]") {
    auto proc = makeProcessor();
    auto* ctrl = makeControllerRaw();

    // Save default processor state
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    // Load into controller
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&stream) == Steinberg::kResultTrue);

    // All parameters should have valid normalized values (0.0-1.0)
    Steinberg::int32 paramCount = ctrl->getParameterCount();
    for (Steinberg::int32 i = 0; i < paramCount; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        ctrl->getParameterInfo(i, info);
        double norm = ctrl->getParamNormalized(info.id);
        INFO("Parameter " << info.id << " has normalized value " << norm);
        CHECK(norm >= 0.0);
        CHECK(norm <= 1.0);
    }

    proc->terminate();
    ctrl->terminate();
}
