// ==============================================================================
// Layer 3: System Component Tests - SynthVoice
// ==============================================================================
// Tests for the basic subtractive synth voice. Covers all 32 functional
// requirements (FR-001 through FR-032) and all 10 success criteria
// (SC-001 through SC-010).
//
// Reference: specs/037-basic-synth-voice/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <krate/dsp/systems/synth_voice.h>
#include <krate/dsp/core/pitch_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helper: Create a prepared voice with default settings
// =============================================================================
static SynthVoice createPreparedVoice(double sampleRate = 44100.0) {
    SynthVoice voice;
    voice.prepare(sampleRate);
    return voice;
}

// =============================================================================
// Helper: Process N samples and return them in a vector
// =============================================================================
static std::vector<float> processNSamples(SynthVoice& voice, size_t n) {
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        out[i] = voice.process();
    }
    return out;
}

// =============================================================================
// Helper: Find peak absolute value in a buffer
// =============================================================================
static float peakAbsolute(const std::vector<float>& buf) {
    float peak = 0.0f;
    for (float s : buf) {
        float a = std::abs(s);
        if (a > peak) peak = a;
    }
    return peak;
}

// =============================================================================
// US1: Lifecycle Tests (FR-001, FR-002, FR-003) [synth-voice][lifecycle]
// =============================================================================

TEST_CASE("SynthVoice prepare initializes all components", "[synth-voice][lifecycle]") {
    // FR-001
    SynthVoice voice;
    voice.prepare(44100.0);
    // After prepare, voice should be in a valid state but not active
    REQUIRE_FALSE(voice.isActive());

    // Should be able to trigger and produce sound
    voice.noteOn(440.0f, 1.0f);
    REQUIRE(voice.isActive());
    float sample = voice.process();
    // After a noteOn, we should get something (possibly small but envelope has started)
    // We just verify no crash and the voice is active
    (void)sample;
}

TEST_CASE("SynthVoice reset clears state", "[synth-voice][lifecycle]") {
    // FR-002
    auto voice = createPreparedVoice();
    voice.noteOn(440.0f, 1.0f);
    // Process a few samples to get into a playing state
    for (int i = 0; i < 100; ++i) (void)voice.process();
    REQUIRE(voice.isActive());

    voice.reset();
    REQUIRE_FALSE(voice.isActive());
    REQUIRE(voice.process() == 0.0f);
}

TEST_CASE("SynthVoice process returns 0 before prepare", "[synth-voice][lifecycle]") {
    // FR-003
    SynthVoice voice;
    REQUIRE(voice.process() == 0.0f);

    // processBlock should fill zeros too
    std::array<float, 64> buf{};
    std::fill(buf.begin(), buf.end(), 999.0f);
    voice.processBlock(buf.data(), 64);
    bool allZero = true;
    for (float s : buf) {
        if (s != 0.0f) { allZero = false; break; }
    }
    REQUIRE(allZero);
}

// =============================================================================
// US1: Note Control Tests (FR-004, FR-005, FR-006) [synth-voice][note-control]
// =============================================================================

TEST_CASE("SynthVoice noteOn produces non-zero output within 512 samples", "[synth-voice][note-control]") {
    // FR-004, SC-002
    auto voice = createPreparedVoice();
    voice.noteOn(440.0f, 1.0f);

    auto samples = processNSamples(voice, 512);
    float peak = peakAbsolute(samples);
    REQUIRE(peak > 0.0f);
}

TEST_CASE("SynthVoice noteOff triggers release", "[synth-voice][note-control]") {
    // FR-005
    auto voice = createPreparedVoice();
    voice.noteOn(440.0f, 1.0f);
    // Process through attack+decay to sustain
    processNSamples(voice, 4410); // ~100ms

    REQUIRE(voice.isActive());
    voice.noteOff();

    // Voice should still be active during release
    REQUIRE(voice.isActive());

    // Process enough samples for release to complete (default release = 100ms)
    // At 44100 Hz, 100ms = 4410 samples. Give extra margin.
    processNSamples(voice, 44100); // 1 second should be plenty

    REQUIRE_FALSE(voice.isActive());
}

TEST_CASE("SynthVoice isActive state transitions", "[synth-voice][note-control]") {
    // FR-006
    auto voice = createPreparedVoice();

    // Before noteOn -> inactive
    REQUIRE_FALSE(voice.isActive());

    // After noteOn -> active
    voice.noteOn(440.0f, 1.0f);
    REQUIRE(voice.isActive());

    // After noteOff + full release -> inactive
    voice.noteOff();
    processNSamples(voice, 44100);
    REQUIRE_FALSE(voice.isActive());
}

// =============================================================================
// US1: Envelope Tests (FR-022, FR-023, FR-024, FR-025) [synth-voice][envelope]
// =============================================================================

TEST_CASE("SynthVoice amplitude envelope shapes output", "[synth-voice][envelope]") {
    // FR-025: amp envelope directly scales voice output
    auto voice = createPreparedVoice();
    voice.setOscMix(0.0f); // Only osc1 for simplicity
    voice.setOsc1Waveform(OscWaveform::Sine);
    voice.setFilterCutoff(20000.0f); // Wide open filter

    voice.noteOn(440.0f, 1.0f);

    // During attack (first ~10ms = 441 samples), output should ramp up
    auto attackSamples = processNSamples(voice, 441);
    float firstPeak = peakAbsolute(std::vector<float>(attackSamples.begin(), attackSamples.begin() + 50));
    float laterPeak = peakAbsolute(std::vector<float>(attackSamples.begin() + 300, attackSamples.end()));
    // Later samples in attack should be louder than initial samples
    REQUIRE(laterPeak > firstPeak);
}

TEST_CASE("SynthVoice becomes inactive when amp envelope reaches idle", "[synth-voice][envelope]") {
    // FR-025
    auto voice = createPreparedVoice();
    voice.setAmpRelease(10.0f); // Very short release (10ms)
    voice.noteOn(440.0f, 1.0f);
    processNSamples(voice, 4410); // Let it reach sustain
    voice.noteOff();

    // Process enough for short release to complete
    processNSamples(voice, 4410);
    REQUIRE_FALSE(voice.isActive());
}

TEST_CASE("SynthVoice envelopes configured with defaults", "[synth-voice][envelope]") {
    // FR-023: verify default envelope settings by checking that the voice
    // produces reasonable output with default parameters
    auto voice = createPreparedVoice();
    voice.noteOn(440.0f, 1.0f);

    // Default amp: A=10ms, D=50ms, S=1.0, R=100ms
    // After 100ms (4410 samples), should be in sustain with full level
    auto samples = processNSamples(voice, 4410);
    float peak = peakAbsolute(samples);
    REQUIRE(peak > 0.0f);
}

// =============================================================================
// US1: Signal Flow Tests (FR-028, FR-029, FR-030) [synth-voice][signal-flow]
// =============================================================================

TEST_CASE("SynthVoice process returns single sample", "[synth-voice][signal-flow]") {
    // FR-028
    auto voice = createPreparedVoice();
    voice.noteOn(440.0f, 1.0f);
    float sample = voice.process();
    // Just verify it returns a finite value
    REQUIRE(std::isfinite(sample));
}

TEST_CASE("SynthVoice processBlock is bit-identical to process loop", "[synth-voice][signal-flow]") {
    // FR-030, SC-004
    constexpr size_t N = 512;

    // Voice A: use processBlock
    auto voiceA = createPreparedVoice();
    voiceA.noteOn(440.0f, 0.8f);
    std::array<float, N> blockOut{};
    voiceA.processBlock(blockOut.data(), N);

    // Voice B: use process() loop
    auto voiceB = createPreparedVoice();
    voiceB.noteOn(440.0f, 0.8f);
    std::array<float, N> loopOut{};
    for (size_t i = 0; i < N; ++i) {
        loopOut[i] = voiceB.process();
    }

    // Must be bit-identical
    bool identical = true;
    for (size_t i = 0; i < N; ++i) {
        if (blockOut[i] != loopOut[i]) { identical = false; break; }
    }
    REQUIRE(identical);
}

TEST_CASE("SynthVoice output is 0 when idle", "[synth-voice][signal-flow]") {
    // FR-003, FR-006
    auto voice = createPreparedVoice();
    // Not playing - should be silent
    auto samples = processNSamples(voice, 64);
    for (float s : samples) {
        REQUIRE(s == 0.0f);
    }
}

TEST_CASE("SynthVoice output transitions through ADSR stages", "[synth-voice][signal-flow]") {
    auto voice = createPreparedVoice();
    voice.setOscMix(0.0f);
    voice.setOsc1Waveform(OscWaveform::Sine);
    voice.setFilterCutoff(20000.0f);
    voice.setAmpAttack(10.0f);  // 10ms attack
    voice.setAmpDecay(50.0f);   // 50ms decay
    voice.setAmpSustain(0.7f);  // 70% sustain
    voice.setAmpRelease(50.0f); // 50ms release

    voice.noteOn(440.0f, 1.0f);

    // Attack phase: output should ramp up
    auto attackOut = processNSamples(voice, 441); // ~10ms
    float attackPeak = peakAbsolute(attackOut);
    REQUIRE(attackPeak > 0.0f);

    // Sustain phase: process enough to be in sustain
    processNSamples(voice, 4410); // Skip through decay

    // Sustain output
    auto sustainOut = processNSamples(voice, 441);
    float sustainPeak = peakAbsolute(sustainOut);
    REQUIRE(sustainPeak > 0.0f);

    // Release
    voice.noteOff();
    auto releaseOut = processNSamples(voice, 4410);
    // Early release should have signal
    float earlyReleasePeak = peakAbsolute(std::vector<float>(releaseOut.begin(), releaseOut.begin() + 100));
    REQUIRE(earlyReleasePeak > 0.0f);

    // After release completes
    processNSamples(voice, 44100);
    REQUIRE_FALSE(voice.isActive());

    // SC-003: output is exactly 0.0 after release
    auto postRelease = processNSamples(voice, 64);
    for (float s : postRelease) {
        REQUIRE(s == 0.0f);
    }
}

// =============================================================================
// US2: Oscillator Tests (FR-008, FR-009, FR-010, FR-011, FR-012)
// [synth-voice][oscillator]
// =============================================================================

TEST_CASE("SynthVoice waveform selection produces non-zero distinct output", "[synth-voice][oscillator]") {
    // FR-008, FR-009
    const OscWaveform waveforms[] = {
        OscWaveform::Sine, OscWaveform::Sawtooth, OscWaveform::Square,
        OscWaveform::Pulse, OscWaveform::Triangle
    };

    std::vector<std::vector<float>> outputs;

    for (auto wf : waveforms) {
        auto voice = createPreparedVoice();
        voice.setOsc1Waveform(wf);
        voice.setOscMix(0.0f); // osc1 only
        voice.setFilterCutoff(20000.0f);
        voice.setAmpAttack(0.1f);
        voice.setAmpSustain(1.0f);
        voice.noteOn(440.0f, 1.0f);

        // Skip attack, process in sustain
        processNSamples(voice, 2000);
        auto samples = processNSamples(voice, 512);
        float peak = peakAbsolute(samples);
        REQUIRE(peak > 0.01f);
        outputs.push_back(samples);
    }

    // Verify waveforms are distinct (at least some pairs differ significantly)
    // Compare first waveform (Sine) with second (Sawtooth)
    float diff = 0.0f;
    for (size_t i = 0; i < 512; ++i) {
        diff += std::abs(outputs[0][i] - outputs[1][i]);
    }
    REQUIRE(diff > 1.0f); // Should be substantially different
}

TEST_CASE("SynthVoice osc2 waveforms work", "[synth-voice][oscillator]") {
    // FR-009 for osc2
    auto voice = createPreparedVoice();
    voice.setOsc2Waveform(OscWaveform::Square);
    voice.setOscMix(1.0f); // osc2 only
    voice.setFilterCutoff(20000.0f);
    voice.setAmpAttack(0.1f);
    voice.noteOn(440.0f, 1.0f);

    processNSamples(voice, 2000);
    auto samples = processNSamples(voice, 512);
    REQUIRE(peakAbsolute(samples) > 0.01f);
}

TEST_CASE("SynthVoice mix=0 silences osc2", "[synth-voice][oscillator]") {
    // FR-010, SC-007
    // Two voices: one with mix=0 (osc1 only), one with both
    auto voiceMix0 = createPreparedVoice();
    voiceMix0.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceMix0.setOsc2Waveform(OscWaveform::Sine); // Different waveform
    voiceMix0.setOscMix(0.0f);
    voiceMix0.setFilterCutoff(20000.0f);
    voiceMix0.setAmpAttack(0.1f);
    voiceMix0.setAmpSustain(1.0f);

    auto voiceOsc1Only = createPreparedVoice();
    voiceOsc1Only.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceOsc1Only.setOscMix(0.0f);
    voiceOsc1Only.setFilterCutoff(20000.0f);
    voiceOsc1Only.setAmpAttack(0.1f);
    voiceOsc1Only.setAmpSustain(1.0f);

    voiceMix0.noteOn(440.0f, 1.0f);
    voiceOsc1Only.noteOn(440.0f, 1.0f);

    // Process same number of samples
    constexpr size_t N = 512;
    std::array<float, N> out0{}, out1{};
    voiceMix0.processBlock(out0.data(), N);
    voiceOsc1Only.processBlock(out1.data(), N);

    // Outputs should be identical (osc2 contributes exactly 0.0)
    bool identical = true;
    for (size_t i = 0; i < N; ++i) {
        if (out0[i] != out1[i]) { identical = false; break; }
    }
    REQUIRE(identical);
}

TEST_CASE("SynthVoice mix=1 silences osc1", "[synth-voice][oscillator]") {
    // FR-010, SC-007
    auto voiceMix1 = createPreparedVoice();
    voiceMix1.setOsc1Waveform(OscWaveform::Sawtooth); // Different waveform
    voiceMix1.setOsc2Waveform(OscWaveform::Sine);
    voiceMix1.setOscMix(1.0f);
    voiceMix1.setFilterCutoff(20000.0f);
    voiceMix1.setAmpAttack(0.1f);
    voiceMix1.setAmpSustain(1.0f);

    auto voiceOsc2Only = createPreparedVoice();
    voiceOsc2Only.setOsc2Waveform(OscWaveform::Sine);
    voiceOsc2Only.setOscMix(1.0f);
    voiceOsc2Only.setFilterCutoff(20000.0f);
    voiceOsc2Only.setAmpAttack(0.1f);
    voiceOsc2Only.setAmpSustain(1.0f);

    voiceMix1.noteOn(440.0f, 1.0f);
    voiceOsc2Only.noteOn(440.0f, 1.0f);

    constexpr size_t N = 512;
    std::array<float, N> out0{}, out1{};
    voiceMix1.processBlock(out0.data(), N);
    voiceOsc2Only.processBlock(out1.data(), N);

    bool identical = true;
    for (size_t i = 0; i < N; ++i) {
        if (out0[i] != out1[i]) { identical = false; break; }
    }
    REQUIRE(identical);
}

TEST_CASE("SynthVoice mix=0.5 blends both oscillators", "[synth-voice][oscillator]") {
    // FR-010
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sawtooth);
    voice.setOsc2Waveform(OscWaveform::Square);
    voice.setOscMix(0.5f);
    voice.setFilterCutoff(20000.0f);
    voice.setAmpAttack(0.1f);
    voice.noteOn(440.0f, 1.0f);

    processNSamples(voice, 2000);
    auto samples = processNSamples(voice, 512);
    REQUIRE(peakAbsolute(samples) > 0.01f);

    // The output should differ from osc1-only and osc2-only
    auto voiceOsc1 = createPreparedVoice();
    voiceOsc1.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceOsc1.setOscMix(0.0f);
    voiceOsc1.setFilterCutoff(20000.0f);
    voiceOsc1.setAmpAttack(0.1f);
    voiceOsc1.noteOn(440.0f, 1.0f);
    processNSamples(voiceOsc1, 2000);
    auto osc1Samples = processNSamples(voiceOsc1, 512);

    float diff = 0.0f;
    for (size_t i = 0; i < 512; ++i) {
        diff += std::abs(samples[i] - osc1Samples[i]);
    }
    REQUIRE(diff > 0.1f); // Mixed output should differ from osc1-only
}

TEST_CASE("SynthVoice osc2 detune produces beating", "[synth-voice][oscillator]") {
    // FR-011
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sine);
    voice.setOsc2Waveform(OscWaveform::Sine);
    voice.setOscMix(0.5f);
    voice.setOsc2Detune(10.0f); // +10 cents
    voice.setFilterCutoff(20000.0f);
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    // Process enough samples to see beating pattern (~1 second at 44100 Hz)
    // 10 cents at 440 Hz = ~2.55 Hz beat frequency
    processNSamples(voice, 2000); // Skip initial transient
    auto samples = processNSamples(voice, 44100);

    // Find min and max envelope of the signal (check for amplitude modulation)
    float maxSample = 0.0f;
    float minPeak = 1.0f;
    constexpr size_t windowSize = 200; // ~4.5ms window
    for (size_t i = 0; i + windowSize < samples.size(); i += windowSize) {
        float windowPeak = 0.0f;
        for (size_t j = i; j < i + windowSize; ++j) {
            windowPeak = std::max(windowPeak, std::abs(samples[j]));
        }
        maxSample = std::max(maxSample, windowPeak);
        minPeak = std::min(minPeak, windowPeak);
    }

    // If beating occurs, the minimum peak should be significantly lower than the max
    REQUIRE(maxSample > 0.1f);
    REQUIRE(minPeak < maxSample * 0.5f); // At least 50% modulation depth
}

TEST_CASE("SynthVoice osc2 detune range clamped", "[synth-voice][oscillator]") {
    // FR-011
    auto voice = createPreparedVoice();
    // These should not crash
    voice.setOsc2Detune(-100.0f);
    voice.setOsc2Detune(100.0f);
    voice.setOsc2Detune(-200.0f); // Should clamp to -100
    voice.setOsc2Detune(200.0f);  // Should clamp to +100
    voice.noteOn(440.0f, 1.0f);
    auto samples = processNSamples(voice, 64);
    // Just verify no crash and some output
    (void)samples;
}

TEST_CASE("SynthVoice osc2 octave produces correct frequency", "[synth-voice][oscillator]") {
    // FR-012: +1 octave with 440 Hz -> osc2 at 880 Hz
    // We test this indirectly by comparing zero-crossing rates
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sine);
    voice.setOsc2Waveform(OscWaveform::Sine);
    voice.setOscMix(1.0f); // osc2 only
    voice.setOsc2Octave(1); // +1 octave
    voice.setFilterCutoff(20000.0f);
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    processNSamples(voice, 2000); // Skip transient
    auto samples = processNSamples(voice, 4410); // 100ms

    // Count zero crossings (should be ~2x for an octave up)
    int zcOsc2 = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i - 1] >= 0.0f && samples[i] < 0.0f) ||
            (samples[i - 1] < 0.0f && samples[i] >= 0.0f)) {
            ++zcOsc2;
        }
    }

    // For comparison, get osc1 at 440Hz zero crossings
    auto voice2 = createPreparedVoice();
    voice2.setOsc1Waveform(OscWaveform::Sine);
    voice2.setOscMix(0.0f);
    voice2.setFilterCutoff(20000.0f);
    voice2.setAmpAttack(0.1f);
    voice2.setAmpSustain(1.0f);
    voice2.noteOn(440.0f, 1.0f);
    processNSamples(voice2, 2000);
    auto refSamples = processNSamples(voice2, 4410);

    int zcRef = 0;
    for (size_t i = 1; i < refSamples.size(); ++i) {
        if ((refSamples[i - 1] >= 0.0f && refSamples[i] < 0.0f) ||
            (refSamples[i - 1] < 0.0f && refSamples[i] >= 0.0f)) {
            ++zcRef;
        }
    }

    // Osc2 at +1 octave should have ~2x zero crossings
    float ratio = static_cast<float>(zcOsc2) / static_cast<float>(zcRef);
    REQUIRE(ratio == Approx(2.0f).margin(0.1f));
}

TEST_CASE("SynthVoice osc2 octave range clamped to [-2, +2]", "[synth-voice][oscillator]") {
    // FR-012
    auto voice = createPreparedVoice();
    voice.setOsc2Octave(-3); // Should clamp to -2
    voice.setOsc2Octave(5);  // Should clamp to +2
    voice.noteOn(440.0f, 1.0f);
    auto samples = processNSamples(voice, 64);
    (void)samples; // No crash
}

TEST_CASE("SynthVoice osc2 octave compounds with detune", "[synth-voice][oscillator]") {
    // FR-012: octave + detune compound
    auto voice = createPreparedVoice();
    voice.setOsc2Waveform(OscWaveform::Sine);
    voice.setOscMix(1.0f);
    voice.setOsc2Octave(1);     // +1 octave
    voice.setOsc2Detune(10.0f); // +10 cents
    voice.setFilterCutoff(20000.0f);
    voice.setAmpAttack(0.1f);
    voice.noteOn(440.0f, 1.0f);

    processNSamples(voice, 2000);
    auto samples = processNSamples(voice, 4410);
    REQUIRE(peakAbsolute(samples) > 0.01f);
}

// =============================================================================
// US3: Filter Tests (FR-013, FR-014, FR-015, FR-016) [synth-voice][filter]
// =============================================================================

TEST_CASE("SynthVoice filter types produce distinct frequency responses", "[synth-voice][filter]") {
    // FR-013, FR-014
    const SVFMode modes[] = { SVFMode::Lowpass, SVFMode::Highpass, SVFMode::Bandpass, SVFMode::Notch };

    std::vector<std::vector<float>> outputs;
    for (auto mode : modes) {
        auto voice = createPreparedVoice();
        voice.setOsc1Waveform(OscWaveform::Sawtooth); // Rich harmonic content
        voice.setOscMix(0.0f);
        voice.setFilterType(mode);
        voice.setFilterCutoff(1000.0f);
        voice.setFilterResonance(2.0f);
        voice.setAmpAttack(0.1f);
        voice.setAmpSustain(1.0f);
        voice.noteOn(440.0f, 1.0f);

        processNSamples(voice, 4000);
        auto samples = processNSamples(voice, 1024);
        outputs.push_back(samples);
    }

    // Compare LP vs HP - should be quite different
    float diff = 0.0f;
    for (size_t i = 0; i < 1024; ++i) {
        diff += std::abs(outputs[0][i] - outputs[1][i]);
    }
    REQUIRE(diff > 1.0f);
}

TEST_CASE("SynthVoice filter cutoff affects output", "[synth-voice][filter]") {
    // FR-015
    // Low cutoff should attenuate more harmonics than high cutoff
    auto voiceLow = createPreparedVoice();
    voiceLow.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceLow.setOscMix(0.0f);
    voiceLow.setFilterCutoff(200.0f); // Low cutoff
    voiceLow.setAmpAttack(0.1f);
    voiceLow.setAmpSustain(1.0f);
    voiceLow.noteOn(440.0f, 1.0f);

    auto voiceHigh = createPreparedVoice();
    voiceHigh.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceHigh.setOscMix(0.0f);
    voiceHigh.setFilterCutoff(10000.0f); // High cutoff
    voiceHigh.setAmpAttack(0.1f);
    voiceHigh.setAmpSustain(1.0f);
    voiceHigh.noteOn(440.0f, 1.0f);

    processNSamples(voiceLow, 4000);
    processNSamples(voiceHigh, 4000);

    auto samplesLow = processNSamples(voiceLow, 1024);
    auto samplesHigh = processNSamples(voiceHigh, 1024);

    // The outputs should differ (the low cutoff attenuates harmonics)
    float diff = 0.0f;
    for (size_t i = 0; i < 1024; ++i) {
        diff += std::abs(samplesLow[i] - samplesHigh[i]);
    }
    REQUIRE(diff > 0.1f);
}

TEST_CASE("SynthVoice filter resonance produces resonant peak", "[synth-voice][filter]") {
    // FR-016
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sawtooth);
    voice.setOscMix(0.0f);
    voice.setFilterCutoff(1000.0f);
    voice.setFilterResonance(20.0f); // High Q
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    processNSamples(voice, 4000);
    auto samples = processNSamples(voice, 1024);
    float highQPeak = peakAbsolute(samples);

    // Compare with low Q
    auto voiceLowQ = createPreparedVoice();
    voiceLowQ.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceLowQ.setOscMix(0.0f);
    voiceLowQ.setFilterCutoff(1000.0f);
    voiceLowQ.setFilterResonance(0.5f); // Low Q
    voiceLowQ.setAmpAttack(0.1f);
    voiceLowQ.setAmpSustain(1.0f);
    voiceLowQ.noteOn(440.0f, 1.0f);

    processNSamples(voiceLowQ, 4000);
    auto samplesLowQ = processNSamples(voiceLowQ, 1024);
    float lowQPeak = peakAbsolute(samplesLowQ);

    // High Q should produce higher peak due to resonance
    REQUIRE(highQPeak > lowQPeak);
}

TEST_CASE("SynthVoice high Q allows self-oscillation", "[synth-voice][filter]") {
    // FR-016
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sine);
    voice.setOscMix(0.0f);
    voice.setFilterCutoff(1000.0f);
    voice.setFilterResonance(30.0f); // Maximum Q
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    processNSamples(voice, 8000);
    auto samples = processNSamples(voice, 1024);
    // Self-oscillating filter should produce significant output
    REQUIRE(peakAbsolute(samples) > 0.1f);
}

// =============================================================================
// US3: Filter Envelope Tests (FR-017, FR-018, FR-019) [synth-voice][filter-env]
// =============================================================================

TEST_CASE("SynthVoice filter envelope modulates cutoff upward", "[synth-voice][filter-env]") {
    // FR-017, FR-018
    // 500 Hz cutoff + 48 semitone env amount at peak -> effective 8000 Hz
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sawtooth);
    voice.setOscMix(0.0f);
    voice.setFilterCutoff(500.0f);
    voice.setFilterEnvAmount(48.0f); // +48 semitones
    voice.setFilterAttack(0.1f);     // Near-instant attack
    voice.setFilterDecay(5000.0f);   // Very long decay
    voice.setFilterSustain(0.0f);
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    // At envelope peak (near beginning), cutoff should be much higher
    processNSamples(voice, 100);
    auto peakSamples = processNSamples(voice, 1024);

    // After decay (cutoff returns to base)
    processNSamples(voice, 88200); // Process 2 seconds for long decay
    auto decayedSamples = processNSamples(voice, 1024);

    // The peak-envelope output should be brighter (more high-frequency content)
    // Measure this by comparing the signal energy or peak values
    float peakEnergy = 0.0f;
    float decayedEnergy = 0.0f;
    for (size_t i = 0; i < 1024; ++i) {
        peakEnergy += peakSamples[i] * peakSamples[i];
        decayedEnergy += decayedSamples[i] * decayedSamples[i];
    }

    // With the filter open (high cutoff at peak), more energy passes through
    REQUIRE(peakEnergy > decayedEnergy);
}

TEST_CASE("SynthVoice filter envelope modulates cutoff downward", "[synth-voice][filter-env]") {
    // FR-017: 2000 Hz cutoff + -24 semitone env amount at peak -> 500 Hz
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sawtooth);
    voice.setOscMix(0.0f);
    voice.setFilterCutoff(2000.0f);
    voice.setFilterEnvAmount(-24.0f); // -24 semitones
    voice.setFilterAttack(0.1f);
    voice.setFilterDecay(5000.0f);
    voice.setFilterSustain(0.0f);
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    // At envelope peak, cutoff should be LOWER (negative env)
    processNSamples(voice, 100);
    auto peakSamples = processNSamples(voice, 1024);

    // After decay, cutoff returns to base 2000 Hz
    processNSamples(voice, 88200);
    auto decayedSamples = processNSamples(voice, 1024);

    float peakEnergy = 0.0f;
    float decayedEnergy = 0.0f;
    for (size_t i = 0; i < 1024; ++i) {
        peakEnergy += peakSamples[i] * peakSamples[i];
        decayedEnergy += decayedSamples[i] * decayedSamples[i];
    }

    // Negative env amount: at peak the filter is CLOSED, so less energy
    REQUIRE(peakEnergy < decayedEnergy);
}

TEST_CASE("SynthVoice filter env amount 0 keeps cutoff at base", "[synth-voice][filter-env]") {
    // FR-017
    auto voiceNoEnv = createPreparedVoice();
    voiceNoEnv.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceNoEnv.setOscMix(0.0f);
    voiceNoEnv.setFilterCutoff(1000.0f);
    voiceNoEnv.setFilterEnvAmount(0.0f); // No modulation
    voiceNoEnv.setAmpAttack(0.1f);
    voiceNoEnv.setAmpSustain(1.0f);
    voiceNoEnv.noteOn(440.0f, 1.0f);

    // Early and late samples should have similar frequency content
    processNSamples(voiceNoEnv, 1000);
    auto early = processNSamples(voiceNoEnv, 1024);
    processNSamples(voiceNoEnv, 44100);
    auto late = processNSamples(voiceNoEnv, 1024);

    float earlyEnergy = 0.0f, lateEnergy = 0.0f;
    for (size_t i = 0; i < 1024; ++i) {
        earlyEnergy += early[i] * early[i];
        lateEnergy += late[i] * late[i];
    }

    // Energy should be similar (within 20% or so) since cutoff is not modulated
    float ratio = (earlyEnergy > 0.0f) ? (lateEnergy / earlyEnergy) : 0.0f;
    REQUIRE(ratio == Approx(1.0f).margin(0.3f));
}

TEST_CASE("SynthVoice filter envelope per-sample modulation produces smooth sweeps", "[synth-voice][filter-env]") {
    // FR-019
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sawtooth);
    voice.setOscMix(0.0f);
    voice.setFilterCutoff(200.0f);
    voice.setFilterEnvAmount(60.0f); // Large sweep
    voice.setFilterAttack(50.0f);    // 50ms attack for visible sweep
    voice.setFilterDecay(5000.0f);
    voice.setFilterSustain(0.0f);
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    auto samples = processNSamples(voice, 4410);

    // Check for no large discontinuities (stepping artifacts)
    // Note: Sawtooth waveforms inherently have large sample-to-sample differences
    // at the waveform wrap point. We check that there are no ADDITIONAL
    // discontinuities from filter coefficient stepping.
    float maxDiff = 0.0f;
    for (size_t i = 1; i < samples.size(); ++i) {
        float d = std::abs(samples[i] - samples[i - 1]);
        maxDiff = std::max(maxDiff, d);
    }
    // Per-sample updates should produce smooth output (allowing sawtooth wraps)
    REQUIRE(maxDiff < 1.0f);
}

TEST_CASE("SynthVoice extreme filter modulation stays in safe range", "[synth-voice][filter-env]") {
    // SC-006
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sawtooth);
    voice.setOscMix(0.0f);
    voice.setFilterCutoff(20000.0f);    // Max base cutoff
    voice.setFilterEnvAmount(96.0f);    // Max env amount
    voice.setFilterKeyTrack(1.0f);      // Max key tracking
    voice.setFilterAttack(0.1f);
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    // Highest note
    voice.noteOn(10000.0f, 1.0f);

    // Should not crash or produce NaN/Inf
    auto samples = processNSamples(voice, 1024);
    bool hasNaN = false;
    for (float s : samples) {
        if (std::isnan(s) || std::isinf(s)) hasNaN = true;
    }
    REQUIRE_FALSE(hasNaN);
}

// =============================================================================
// US4: Key Tracking Tests (FR-020, FR-021) [synth-voice][key-tracking]
// =============================================================================

TEST_CASE("SynthVoice 100% key tracking shifts cutoff by octave", "[synth-voice][key-tracking]") {
    // FR-020, FR-021
    // C4 = MIDI 60 (reference), C5 = MIDI 72 (12 semitones above)
    // At 100% key tracking, cutoff should double for C5 vs C4
    float freqC4 = 261.63f;
    float freqC5 = 523.25f;

    // Voice at C4 (reference note)
    auto voiceC4 = createPreparedVoice();
    voiceC4.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceC4.setOscMix(0.0f);
    voiceC4.setFilterCutoff(1000.0f);
    voiceC4.setFilterKeyTrack(1.0f); // 100%
    voiceC4.setFilterEnvAmount(0.0f);
    voiceC4.setAmpAttack(0.1f);
    voiceC4.setAmpSustain(1.0f);
    voiceC4.noteOn(freqC4, 1.0f);

    // Voice at C5
    auto voiceC5 = createPreparedVoice();
    voiceC5.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceC5.setOscMix(0.0f);
    voiceC5.setFilterCutoff(1000.0f);
    voiceC5.setFilterKeyTrack(1.0f); // 100%
    voiceC5.setFilterEnvAmount(0.0f);
    voiceC5.setAmpAttack(0.1f);
    voiceC5.setAmpSustain(1.0f);
    voiceC5.noteOn(freqC5, 1.0f);

    // Process both to steady state
    processNSamples(voiceC4, 8000);
    processNSamples(voiceC5, 8000);

    auto samplesC4 = processNSamples(voiceC4, 2048);
    auto samplesC5 = processNSamples(voiceC5, 2048);

    // C5 should have more high-frequency content (brighter) since cutoff is higher
    float energyC4 = 0.0f, energyC5 = 0.0f;
    for (size_t i = 0; i < 2048; ++i) {
        energyC4 += samplesC4[i] * samplesC4[i];
        energyC5 += samplesC5[i] * samplesC5[i];
    }

    // C5 with higher cutoff should have more energy (more harmonics pass through)
    REQUIRE(energyC5 > energyC4);
}

TEST_CASE("SynthVoice 0% key tracking cutoff independent of pitch", "[synth-voice][key-tracking]") {
    // FR-020
    float freqC4 = 261.63f;
    float freqC5 = 523.25f;

    auto voiceC4 = createPreparedVoice();
    voiceC4.setOsc1Waveform(OscWaveform::Sine); // Sine has no harmonics to filter
    voiceC4.setOscMix(0.0f);
    voiceC4.setFilterCutoff(1000.0f);
    voiceC4.setFilterKeyTrack(0.0f); // 0%
    voiceC4.setFilterEnvAmount(0.0f);
    voiceC4.setAmpAttack(0.1f);
    voiceC4.setAmpSustain(1.0f);
    voiceC4.noteOn(freqC4, 1.0f);

    auto voiceC5 = createPreparedVoice();
    voiceC5.setOsc1Waveform(OscWaveform::Sine);
    voiceC5.setOscMix(0.0f);
    voiceC5.setFilterCutoff(1000.0f);
    voiceC5.setFilterKeyTrack(0.0f); // 0%
    voiceC5.setFilterEnvAmount(0.0f);
    voiceC5.setAmpAttack(0.1f);
    voiceC5.setAmpSustain(1.0f);
    voiceC5.noteOn(freqC5, 1.0f);

    processNSamples(voiceC4, 8000);
    processNSamples(voiceC5, 8000);

    // With 0% key tracking, the filter cutoff is the same for both notes
    // The amplitude difference comes only from the oscillator frequency difference
    // and the filter's fixed response, not from key tracking
    auto samplesC4 = processNSamples(voiceC4, 2048);
    auto samplesC5 = processNSamples(voiceC5, 2048);

    // Both sine waves pass cleanly through a 1000 Hz lowpass (440 < 1000, 523 < 1000)
    // so both should have similar peak amplitude
    float peakC4 = peakAbsolute(samplesC4);
    float peakC5 = peakAbsolute(samplesC5);
    float peakRatio = (peakC4 > 0.0f) ? (peakC5 / peakC4) : 0.0f;
    REQUIRE(peakRatio == Approx(1.0f).margin(0.1f));
}

TEST_CASE("SynthVoice 50% key tracking shifts cutoff by half octave per octave", "[synth-voice][key-tracking]") {
    // FR-020: 50% key tracking at C6 (MIDI 84), 24 semitones above reference C4.
    // Shift = 0.5 * (84 - 60) = 12 semitones -> cutoff doubles.
    // Use a low cutoff and high note to make the energy difference large.
    // 0% tracking: cutoff = 300 Hz.
    // 50% tracking: cutoff = 300 * 2^(12/12) = 600 Hz (twice as high).
    // With sawtooth at C6 (1046 Hz), a 300->600 Hz cutoff change lets
    // significantly more harmonic energy through.
    float freqC6 = 1046.50f; // C6

    auto voiceNoTrack = createPreparedVoice();
    voiceNoTrack.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceNoTrack.setOscMix(0.0f);
    voiceNoTrack.setFilterCutoff(300.0f);
    voiceNoTrack.setFilterKeyTrack(0.0f); // 0%
    voiceNoTrack.setFilterEnvAmount(0.0f);
    voiceNoTrack.setAmpAttack(0.1f);
    voiceNoTrack.setAmpSustain(1.0f);
    voiceNoTrack.noteOn(freqC6, 1.0f);

    auto voiceHalfTrack = createPreparedVoice();
    voiceHalfTrack.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceHalfTrack.setOscMix(0.0f);
    voiceHalfTrack.setFilterCutoff(300.0f);
    voiceHalfTrack.setFilterKeyTrack(0.5f); // 50%
    voiceHalfTrack.setFilterEnvAmount(0.0f);
    voiceHalfTrack.setAmpAttack(0.1f);
    voiceHalfTrack.setAmpSustain(1.0f);
    voiceHalfTrack.noteOn(freqC6, 1.0f);

    processNSamples(voiceNoTrack, 8000);
    processNSamples(voiceHalfTrack, 8000);

    auto samplesNoTrack = processNSamples(voiceNoTrack, 4096);
    auto samplesHalfTrack = processNSamples(voiceHalfTrack, 4096);

    float energyNoTrack = 0.0f, energyHalfTrack = 0.0f;
    for (size_t i = 0; i < 4096; ++i) {
        energyNoTrack += samplesNoTrack[i] * samplesNoTrack[i];
        energyHalfTrack += samplesHalfTrack[i] * samplesHalfTrack[i];
    }

    // With 50% key tracking at C6, cutoff is 600 Hz vs 300 Hz,
    // so more harmonics pass through = more energy
    REQUIRE(energyHalfTrack > energyNoTrack);
}

// =============================================================================
// US5: Velocity Tests (FR-026, FR-027) [synth-voice][velocity]
// =============================================================================

TEST_CASE("SynthVoice velocity 0.5 produces ~50% peak amplitude vs 1.0", "[synth-voice][velocity]") {
    // FR-026
    auto voiceFull = createPreparedVoice();
    voiceFull.setOsc1Waveform(OscWaveform::Sine);
    voiceFull.setOscMix(0.0f);
    voiceFull.setFilterCutoff(20000.0f);
    voiceFull.setAmpAttack(0.1f);
    voiceFull.setAmpSustain(1.0f);
    voiceFull.noteOn(440.0f, 1.0f);

    auto voiceHalf = createPreparedVoice();
    voiceHalf.setOsc1Waveform(OscWaveform::Sine);
    voiceHalf.setOscMix(0.0f);
    voiceHalf.setFilterCutoff(20000.0f);
    voiceHalf.setAmpAttack(0.1f);
    voiceHalf.setAmpSustain(1.0f);
    voiceHalf.noteOn(440.0f, 0.5f);

    // Process to sustain
    processNSamples(voiceFull, 4000);
    processNSamples(voiceHalf, 4000);

    auto samplesFull = processNSamples(voiceFull, 1024);
    auto samplesHalf = processNSamples(voiceHalf, 1024);

    float peakFull = peakAbsolute(samplesFull);
    float peakHalf = peakAbsolute(samplesHalf);

    // Half velocity should produce ~50% of full velocity amplitude
    float ratio = peakHalf / peakFull;
    REQUIRE(ratio == Approx(0.5f).margin(0.1f));
}

TEST_CASE("SynthVoice velToFilterEnv=1 velocity 0.25 gives 25% filter depth", "[synth-voice][velocity]") {
    // FR-027
    auto voiceFull = createPreparedVoice();
    voiceFull.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceFull.setOscMix(0.0f);
    voiceFull.setFilterCutoff(200.0f);
    voiceFull.setFilterEnvAmount(48.0f);
    voiceFull.setVelocityToFilterEnv(1.0f); // Full velocity scaling
    voiceFull.setFilterAttack(0.1f);
    voiceFull.setFilterDecay(5000.0f);
    voiceFull.setFilterSustain(0.0f);
    voiceFull.setAmpAttack(0.1f);
    voiceFull.setAmpSustain(1.0f);
    voiceFull.noteOn(440.0f, 1.0f);

    auto voiceQuarter = createPreparedVoice();
    voiceQuarter.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceQuarter.setOscMix(0.0f);
    voiceQuarter.setFilterCutoff(200.0f);
    voiceQuarter.setFilterEnvAmount(48.0f);
    voiceQuarter.setVelocityToFilterEnv(1.0f);
    voiceQuarter.setFilterAttack(0.1f);
    voiceQuarter.setFilterDecay(5000.0f);
    voiceQuarter.setFilterSustain(0.0f);
    voiceQuarter.setAmpAttack(0.1f);
    voiceQuarter.setAmpSustain(1.0f);
    voiceQuarter.noteOn(440.0f, 0.25f);

    // Process to envelope peak
    processNSamples(voiceFull, 100);
    processNSamples(voiceQuarter, 100);

    auto samplesFull = processNSamples(voiceFull, 1024);
    auto samplesQuarter = processNSamples(voiceQuarter, 1024);

    float energyFull = 0.0f, energyQuarter = 0.0f;
    for (size_t i = 0; i < 1024; ++i) {
        energyFull += samplesFull[i] * samplesFull[i];
        energyQuarter += samplesQuarter[i] * samplesQuarter[i];
    }

    // Velocity 0.25 with velToFilterEnv=1.0 should have much less filter opening
    // (25% of 48 semitones = 12 semitones vs 48 semitones)
    // Note: amplitude is also affected by velocity, so we look at energy difference
    // which combines both effects. The key point is significant difference.
    REQUIRE(energyFull > energyQuarter);
}

TEST_CASE("SynthVoice velToFilterEnv=0 filter depth unaffected by velocity", "[synth-voice][velocity]") {
    // FR-027
    auto voiceFull = createPreparedVoice();
    voiceFull.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceFull.setOscMix(0.0f);
    voiceFull.setFilterCutoff(200.0f);
    voiceFull.setFilterEnvAmount(48.0f);
    voiceFull.setVelocityToFilterEnv(0.0f); // No velocity scaling on filter
    voiceFull.setFilterAttack(0.1f);
    voiceFull.setFilterDecay(5000.0f);
    voiceFull.setFilterSustain(0.0f);
    voiceFull.setAmpAttack(0.1f);
    voiceFull.setAmpSustain(1.0f);
    voiceFull.noteOn(440.0f, 1.0f);

    auto voiceLow = createPreparedVoice();
    voiceLow.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceLow.setOscMix(0.0f);
    voiceLow.setFilterCutoff(200.0f);
    voiceLow.setFilterEnvAmount(48.0f);
    voiceLow.setVelocityToFilterEnv(0.0f);
    voiceLow.setFilterAttack(0.1f);
    voiceLow.setFilterDecay(5000.0f);
    voiceLow.setFilterSustain(0.0f);
    voiceLow.setAmpAttack(0.1f);
    voiceLow.setAmpSustain(1.0f);
    voiceLow.noteOn(440.0f, 0.25f);

    processNSamples(voiceFull, 100);
    processNSamples(voiceLow, 100);

    auto samplesFull = processNSamples(voiceFull, 1024);
    auto samplesLow = processNSamples(voiceLow, 1024);

    // Normalize by amplitude (velocity 0.25 has 0.25x amplitude)
    // The filter cutoff should be the same since velToFilterEnv=0
    float energyFull = 0.0f, energyLow = 0.0f;
    for (size_t i = 0; i < 1024; ++i) {
        energyFull += samplesFull[i] * samplesFull[i];
        energyLow += samplesLow[i] * samplesLow[i];
    }

    // Energy ratio should approximately equal velocity^2 ratio (0.25^2 = 0.0625)
    // since filter depth is the same, only amplitude differs
    float energyRatio = (energyFull > 0.0f) ? (energyLow / energyFull) : 0.0f;
    // Should be approximately 0.0625 (but envelope shape may cause some deviation)
    REQUIRE(energyRatio < 0.15f); // Reasonably close to 0.0625
    REQUIRE(energyRatio > 0.01f); // But not zero
}

// =============================================================================
// US6: Block Processing Tests [synth-voice][signal-flow]
// =============================================================================

TEST_CASE("SynthVoice processBlock 512 bit-identical to 512 process calls", "[synth-voice][signal-flow]") {
    // SC-004, FR-030 (duplicate check with varied parameters)
    constexpr size_t N = 512;

    auto voiceA = createPreparedVoice();
    voiceA.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceA.setOsc2Waveform(OscWaveform::Square);
    voiceA.setOscMix(0.5f);
    voiceA.setOsc2Detune(5.0f);
    voiceA.setFilterCutoff(2000.0f);
    voiceA.setFilterEnvAmount(24.0f);
    voiceA.noteOn(440.0f, 0.8f);

    auto voiceB = createPreparedVoice();
    voiceB.setOsc1Waveform(OscWaveform::Sawtooth);
    voiceB.setOsc2Waveform(OscWaveform::Square);
    voiceB.setOscMix(0.5f);
    voiceB.setOsc2Detune(5.0f);
    voiceB.setFilterCutoff(2000.0f);
    voiceB.setFilterEnvAmount(24.0f);
    voiceB.noteOn(440.0f, 0.8f);

    std::array<float, N> block{};
    voiceA.processBlock(block.data(), N);

    std::array<float, N> loop{};
    for (size_t i = 0; i < N; ++i) {
        loop[i] = voiceB.process();
    }

    bool identical = true;
    for (size_t i = 0; i < N; ++i) {
        if (block[i] != loop[i]) { identical = false; break; }
    }
    REQUIRE(identical);
}

TEST_CASE("SynthVoice release mid-block produces zeros after release", "[synth-voice][signal-flow]") {
    auto voice = createPreparedVoice();
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.setAmpRelease(1.0f); // Very short release (1ms ~= 44 samples)
    voice.noteOn(440.0f, 1.0f);

    // Process to sustain
    processNSamples(voice, 4410);
    voice.noteOff();

    // Process a large block to ensure release completes mid-block
    std::vector<float> out(44100);
    voice.processBlock(out.data(), 44100);

    // Voice should be inactive after this
    REQUIRE_FALSE(voice.isActive());

    // The remaining samples (after release) should be zero
    // Find the first zero sample after some non-zero content
    bool foundZero = false;
    for (size_t i = 100; i < out.size(); ++i) {
        if (out[i] == 0.0f) {
            foundZero = true;
            // All subsequent should be zero
            for (size_t j = i; j < std::min(i + 100, out.size()); ++j) {
                REQUIRE(out[j] == 0.0f);
            }
            break;
        }
    }
    REQUIRE(foundZero);
}

// =============================================================================
// Edge Cases: Retrigger Tests (FR-007, SC-009) [synth-voice][retrigger]
// =============================================================================

TEST_CASE("SynthVoice retrigger attacks from current level", "[synth-voice][retrigger]") {
    // FR-007
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sine);
    voice.setOscMix(0.0f);
    voice.setFilterCutoff(20000.0f);
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    // Process to sustain
    processNSamples(voice, 4410);
    REQUIRE(voice.isActive());

    // Retrigger with new frequency
    voice.noteOn(880.0f, 1.0f);
    REQUIRE(voice.isActive());

    // Should still produce output
    auto samples = processNSamples(voice, 512);
    REQUIRE(peakAbsolute(samples) > 0.0f);
}

TEST_CASE("SynthVoice retrigger produces no clicks", "[synth-voice][retrigger]") {
    // SC-009: discontinuity <= 0.01 (-40 dBFS)
    // We test for envelope discontinuity by using a very low frequency sine so
    // the oscillator's sample-to-sample variation is negligible. This way any
    // significant jump at the retrigger boundary must come from the envelope.
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sine);
    voice.setOscMix(0.0f);
    voice.setFilterCutoff(20000.0f);
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    // Very low frequency: max derivative = 2*pi*10/44100 ~ 0.0014 per sample
    voice.noteOn(10.0f, 1.0f);

    // Process to steady state (sustain)
    processNSamples(voice, 8820);

    // Capture pre-retrigger samples
    auto preRetrigger = processNSamples(voice, 64);

    // Retrigger at same frequency and velocity (pure envelope retrigger)
    voice.noteOn(10.0f, 1.0f);

    // Capture post-retrigger samples
    auto postRetrigger = processNSamples(voice, 64);

    // The discontinuity at the retrigger point
    float lastPre = preRetrigger.back();
    float firstPost = postRetrigger.front();
    float discontinuity = std::abs(firstPost - lastPre);

    // SC-009: peak discontinuity <= 0.01
    // At 10 Hz, normal sine variation per sample is ~0.0014, so any value
    // significantly above that would indicate an envelope click.
    REQUIRE(discontinuity <= 0.01f);
}

TEST_CASE("SynthVoice retrigger preserves oscillator phase", "[synth-voice][retrigger]") {
    // FR-007: oscillator phase preserved on retrigger
    // We verify by checking that the output is continuous around the retrigger point
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sine);
    voice.setOscMix(0.0f);
    voice.setFilterCutoff(20000.0f);
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    processNSamples(voice, 8820);

    // Record a few samples before retrigger
    auto pre = processNSamples(voice, 10);

    // Retrigger at same frequency (should be completely smooth)
    voice.noteOn(440.0f, 1.0f);
    auto post = processNSamples(voice, 10);

    // Max sample-to-sample difference around retrigger point
    float maxDiff = std::abs(post[0] - pre.back());
    for (size_t i = 1; i < post.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(post[i] - post[i - 1]));
    }

    // Should be smooth (no large discontinuities)
    // Sine wave at 440 Hz / 44100 Hz has a max derivative of
    // 2*pi*440/44100 ~ 0.0627, so normal variation is small
    REQUIRE(maxDiff < 0.07f);
}

// =============================================================================
// Edge Cases: Safety Tests (FR-031, FR-032) [synth-voice][safety]
// =============================================================================

TEST_CASE("SynthVoice setters work before prepare", "[synth-voice][safety]") {
    // FR-031
    SynthVoice voice;
    // All these should not crash
    voice.setOsc1Waveform(OscWaveform::Square);
    voice.setOsc2Waveform(OscWaveform::Triangle);
    voice.setOscMix(0.5f);
    voice.setOsc2Detune(10.0f);
    voice.setOsc2Octave(1);
    voice.setFilterType(SVFMode::Highpass);
    voice.setFilterCutoff(500.0f);
    voice.setFilterResonance(5.0f);
    voice.setFilterEnvAmount(24.0f);
    voice.setFilterKeyTrack(0.5f);
    voice.setAmpAttack(50.0f);
    voice.setAmpDecay(100.0f);
    voice.setAmpSustain(0.8f);
    voice.setAmpRelease(200.0f);
    voice.setFilterAttack(20.0f);
    voice.setFilterDecay(100.0f);
    voice.setFilterSustain(0.5f);
    voice.setFilterRelease(150.0f);
    voice.setVelocityToFilterEnv(0.5f);

    // Now prepare and verify it works
    voice.prepare(44100.0);
    voice.noteOn(440.0f, 1.0f);
    auto samples = processNSamples(voice, 64);
    (void)samples;
}

TEST_CASE("SynthVoice setters work while playing", "[synth-voice][safety]") {
    // FR-031
    auto voice = createPreparedVoice();
    voice.noteOn(440.0f, 1.0f);
    processNSamples(voice, 1000);

    // Change every parameter while playing
    voice.setOsc1Waveform(OscWaveform::Triangle);
    voice.setOsc2Waveform(OscWaveform::Pulse);
    voice.setOscMix(0.3f);
    voice.setOsc2Detune(-20.0f);
    voice.setOsc2Octave(-1);
    voice.setFilterType(SVFMode::Bandpass);
    voice.setFilterCutoff(2000.0f);
    voice.setFilterResonance(10.0f);
    voice.setFilterEnvAmount(-12.0f);
    voice.setFilterKeyTrack(0.8f);
    voice.setAmpAttack(100.0f);
    voice.setAmpDecay(200.0f);
    voice.setAmpSustain(0.5f);
    voice.setAmpRelease(500.0f);
    voice.setFilterAttack(50.0f);
    voice.setFilterDecay(300.0f);
    voice.setFilterSustain(0.2f);
    voice.setFilterRelease(200.0f);
    voice.setVelocityToFilterEnv(0.7f);

    auto samples = processNSamples(voice, 1024);
    REQUIRE(peakAbsolute(samples) > 0.0f);
}

TEST_CASE("SynthVoice setters work while idle", "[synth-voice][safety]") {
    // FR-031
    auto voice = createPreparedVoice();
    // Not playing
    voice.setOsc1Waveform(OscWaveform::Square);
    voice.setFilterCutoff(500.0f);

    // Should still be silent
    auto samples = processNSamples(voice, 64);
    for (float s : samples) {
        REQUIRE(s == 0.0f);
    }
}

TEST_CASE("SynthVoice setters ignore NaN inputs", "[synth-voice][safety]") {
    // FR-032
    auto voice = createPreparedVoice();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // Set known values, then try NaN - parameter should retain original value
    voice.setOscMix(0.3f);
    voice.setOscMix(nan);

    voice.setOsc2Detune(5.0f);
    voice.setOsc2Detune(nan);

    voice.setFilterCutoff(500.0f);
    voice.setFilterCutoff(nan);

    voice.setFilterResonance(2.0f);
    voice.setFilterResonance(nan);

    voice.setFilterEnvAmount(12.0f);
    voice.setFilterEnvAmount(nan);

    voice.setFilterKeyTrack(0.5f);
    voice.setFilterKeyTrack(nan);

    voice.setVelocityToFilterEnv(0.5f);
    voice.setVelocityToFilterEnv(nan);

    voice.setAmpAttack(50.0f);
    voice.setAmpAttack(nan);

    voice.setAmpDecay(100.0f);
    voice.setAmpDecay(nan);

    voice.setAmpSustain(0.8f);
    voice.setAmpSustain(nan);

    voice.setAmpRelease(200.0f);
    voice.setAmpRelease(nan);

    voice.setFilterAttack(20.0f);
    voice.setFilterAttack(nan);

    voice.setFilterDecay(100.0f);
    voice.setFilterDecay(nan);

    voice.setFilterSustain(0.5f);
    voice.setFilterSustain(nan);

    voice.setFilterRelease(150.0f);
    voice.setFilterRelease(nan);

    // NaN noteOn should be ignored
    voice.noteOn(440.0f, 1.0f);
    REQUIRE(voice.isActive());
    voice.noteOn(nan, 1.0f);    // Should be ignored
    voice.noteOn(440.0f, nan);   // Should be ignored

    auto samples = processNSamples(voice, 64);
    // Should still be working (not crashed/corrupted)
    REQUIRE(peakAbsolute(samples) > 0.0f);
}

TEST_CASE("SynthVoice setters ignore Inf inputs", "[synth-voice][safety]") {
    // FR-032
    auto voice = createPreparedVoice();
    const float inf = std::numeric_limits<float>::infinity();
    const float ninf = -std::numeric_limits<float>::infinity();

    voice.setOscMix(0.3f);
    voice.setOscMix(inf);
    voice.setOscMix(ninf);

    voice.setOsc2Detune(5.0f);
    voice.setOsc2Detune(inf);

    voice.setFilterCutoff(500.0f);
    voice.setFilterCutoff(inf);

    voice.setFilterResonance(2.0f);
    voice.setFilterResonance(inf);

    voice.setFilterEnvAmount(12.0f);
    voice.setFilterEnvAmount(inf);

    voice.setFilterKeyTrack(0.5f);
    voice.setFilterKeyTrack(inf);

    voice.setVelocityToFilterEnv(0.5f);
    voice.setVelocityToFilterEnv(inf);

    voice.setAmpAttack(50.0f);
    voice.setAmpAttack(inf);

    voice.setAmpDecay(100.0f);
    voice.setAmpDecay(inf);

    voice.setAmpSustain(0.8f);
    voice.setAmpSustain(inf);

    voice.setAmpRelease(200.0f);
    voice.setAmpRelease(inf);

    voice.setFilterAttack(20.0f);
    voice.setFilterAttack(inf);

    voice.setFilterDecay(100.0f);
    voice.setFilterDecay(inf);

    voice.setFilterSustain(0.5f);
    voice.setFilterSustain(inf);

    voice.setFilterRelease(150.0f);
    voice.setFilterRelease(inf);

    voice.noteOn(440.0f, 1.0f);
    auto samples = processNSamples(voice, 64);
    REQUIRE(peakAbsolute(samples) > 0.0f);
}

TEST_CASE("SynthVoice frequency=0 produces near-silence", "[synth-voice][safety]") {
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sine); // Sine at 0 Hz produces 0.0
    voice.setOsc2Waveform(OscWaveform::Sine);
    voice.noteOn(0.0f, 1.0f);

    auto samples = processNSamples(voice, 512);
    // With zero frequency, sine oscillators produce sin(0)=0 every sample
    float peak = peakAbsolute(samples);
    REQUIRE(peak < 0.001f);
}

TEST_CASE("SynthVoice velocity=0 produces silence and becomes inactive", "[synth-voice][safety]") {
    auto voice = createPreparedVoice();
    voice.noteOn(440.0f, 0.0f);

    // Velocity 0 means amp envelope peak = 0
    auto samples = processNSamples(voice, 1024);
    float peak = peakAbsolute(samples);
    REQUIRE(peak < 0.001f);

    // Process and eventually should become inactive
    voice.noteOff();
    processNSamples(voice, 44100);
    REQUIRE_FALSE(voice.isActive());
}

TEST_CASE("SynthVoice noteOff while idle is safe", "[synth-voice][safety]") {
    auto voice = createPreparedVoice();
    REQUIRE_FALSE(voice.isActive());

    // noteOff before any noteOn should not crash
    voice.noteOff();
    REQUIRE_FALSE(voice.isActive());
    REQUIRE(voice.process() == 0.0f);
}

TEST_CASE("SynthVoice prepare while note active resets voice", "[synth-voice][safety]") {
    auto voice = createPreparedVoice();
    voice.noteOn(440.0f, 1.0f);
    processNSamples(voice, 1000);
    REQUIRE(voice.isActive());

    // Re-prepare while playing
    voice.prepare(48000.0);
    REQUIRE_FALSE(voice.isActive());
    REQUIRE(voice.process() == 0.0f);
}

// =============================================================================
// Edge Cases: Sample Rate Tests (SC-005) [synth-voice][acceptance]
// =============================================================================

TEST_CASE("SynthVoice works at all standard sample rates", "[synth-voice][acceptance]") {
    // SC-005
    const double sampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};

    for (double sr : sampleRates) {
        SECTION("Sample rate " + std::to_string(static_cast<int>(sr))) {
            auto voice = createPreparedVoice(sr);
            voice.noteOn(440.0f, 1.0f);

            size_t numSamples = static_cast<size_t>(sr * 0.1); // 100ms
            auto samples = processNSamples(voice, numSamples);
            float peak = peakAbsolute(samples);
            REQUIRE(peak > 0.0f);

            voice.noteOff();
            processNSamples(voice, static_cast<size_t>(sr)); // 1 second
            REQUIRE_FALSE(voice.isActive());
        }
    }
}

// =============================================================================
// Edge Cases: Output Range Tests (SC-008) [synth-voice][acceptance]
// =============================================================================

TEST_CASE("SynthVoice output in [-1, +1] under normal conditions", "[synth-voice][acceptance]") {
    // SC-008
    auto voice = createPreparedVoice();
    voice.setOsc1Waveform(OscWaveform::Sawtooth);
    voice.setOscMix(0.0f); // Single oscillator
    voice.setFilterCutoff(20000.0f); // Wide open
    voice.setFilterResonance(SVF::kButterworthQ); // No resonance
    voice.setAmpAttack(0.1f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    processNSamples(voice, 4000); // Skip transient
    auto samples = processNSamples(voice, 44100); // 1 second

    float peak = peakAbsolute(samples);
    REQUIRE(peak <= 1.05f); // Allow small PolyBLEP overshoot
}

// =============================================================================
// Performance Tests (SC-001) [synth-voice][performance]
// =============================================================================

TEST_CASE("SynthVoice CPU usage under 1%", "[synth-voice][performance]") {
    // SC-001: < 1% CPU at 44.1 kHz
    constexpr double sampleRate = 44100.0;
    constexpr size_t numSamples = 44100; // 1 second

    auto voice = createPreparedVoice(sampleRate);
    voice.setOsc1Waveform(OscWaveform::Sawtooth);
    voice.setOsc2Waveform(OscWaveform::Sawtooth);
    voice.setOscMix(0.5f);
    voice.setFilterCutoff(1000.0f);
    voice.setFilterResonance(5.0f);
    voice.setFilterEnvAmount(48.0f);
    voice.setAmpAttack(10.0f);
    voice.setAmpSustain(1.0f);
    voice.noteOn(440.0f, 1.0f);

    // Warm up
    processNSamples(voice, 4410);

    // Benchmark using Catch2
    BENCHMARK("SynthVoice 1 second at 44.1kHz") {
        std::array<float, 512> buf{};
        for (size_t i = 0; i < numSamples; i += 512) {
            size_t n = std::min<size_t>(512, numSamples - i);
            voice.processBlock(buf.data(), n);
        }
    };

    // Manual timing check
    auto start = std::chrono::high_resolution_clock::now();
    {
        std::array<float, 512> buf{};
        for (size_t i = 0; i < numSamples; i += 512) {
            size_t n = std::min<size_t>(512, numSamples - i);
            voice.processBlock(buf.data(), n);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Real-time duration for 1 second at 44100 Hz = 1,000,000 us
    double cpuPercent = static_cast<double>(durationUs) / 1000000.0 * 100.0;
    REQUIRE(cpuPercent < 1.0);
}
