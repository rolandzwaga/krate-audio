// ==============================================================================
// Engine OSC Parameter Routing Tests (068-osc-type-params, Phase 3)
// ==============================================================================
// Tests that RuinaeEngine::setOscAParam() / setOscBParam() forward to all
// active voices for a representative sample of OscParam values.
//
// Feature: 068-osc-type-params
// User Story: US2 (Type-Specific Parameter Routing for All Types)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/ruinae_engine.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "parameters/dropdown_mappings.h"
#include "vst_event_list.h"
#include "vst_param_changes.h"
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/systems/oscillator_types.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

static constexpr double kSampleRate = 44100.0;
static constexpr size_t kBlockSize = 512;

static std::unique_ptr<RuinaeEngine> createEngine() {
    auto engine = std::make_unique<RuinaeEngine>();
    engine->prepare(kSampleRate, kBlockSize);
    return engine;
}

static float computeRMS(const float* buf, size_t n) {
    if (n == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(buf[i]) * buf[i];
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

// =============================================================================
// Tests: RuinaeEngine setOscAParam / setOscBParam
// =============================================================================

TEST_CASE("RuinaeEngine setOscAParam forwards to voices",
          "[engine][osc-param][us2]") {
    auto engine = createEngine();

    // Set up a single-voice scenario
    engine->setPolyphony(1);

    // Configure OSC A as PolyBLEP, set waveform
    engine->setOscAType(OscType::PolyBLEP);
    engine->setMixPosition(0.0f);  // OSC A only

    // Set waveform to Sawtooth via engine setter
    engine->setOscAParam(OscParam::Waveform, 1.0f);

    // Play a note
    engine->noteOn(60, 100);  // Middle C, velocity 100

    // Process a few blocks to get audio
    std::vector<float> left(kBlockSize, 0.0f);
    std::vector<float> right(kBlockSize, 0.0f);
    engine->processBlock(left.data(), right.data(), kBlockSize);
    engine->processBlock(left.data(), right.data(), kBlockSize);

    float rmsL = computeRMS(left.data(), kBlockSize);
    // Should produce audible output
    REQUIRE(rmsL > 0.001f);
}

TEST_CASE("RuinaeEngine setOscBParam forwards to voices independently",
          "[engine][osc-param][us2]") {
    auto engine = createEngine();
    engine->setPolyphony(1);

    // Set OSC A as PolyBLEP Sine, OSC B as PolyBLEP Sawtooth
    engine->setOscAType(OscType::PolyBLEP);
    engine->setOscBType(OscType::PolyBLEP);
    engine->setOscAParam(OscParam::Waveform, 0.0f);  // Sine
    engine->setOscBParam(OscParam::Waveform, 1.0f);  // Sawtooth
    engine->setMixPosition(1.0f);  // OSC B only

    engine->noteOn(60, 100);

    std::vector<float> left(kBlockSize, 0.0f);
    std::vector<float> right(kBlockSize, 0.0f);
    engine->processBlock(left.data(), right.data(), kBlockSize);
    engine->processBlock(left.data(), right.data(), kBlockSize);

    float rmsL = computeRMS(left.data(), kBlockSize);
    REQUIRE(rmsL > 0.001f);
}

TEST_CASE("RuinaeEngine setOscAParam handles representative params for all types",
          "[engine][osc-param][us2]") {
    auto engine = createEngine();
    engine->setPolyphony(1);
    engine->setMixPosition(0.0f);  // OSC A only

    SECTION("ChaosAmount forwarded to Chaos oscillator") {
        engine->setOscAType(OscType::Chaos);
        engine->setOscAParam(OscParam::ChaosAmount, 0.8f);
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        REQUIRE(computeRMS(left.data(), kBlockSize) > 0.001f);
    }

    SECTION("NoiseColor forwarded to Noise oscillator") {
        engine->setOscAType(OscType::Noise);
        engine->setOscAParam(OscParam::NoiseColor, 1.0f);  // Pink
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        REQUIRE(computeRMS(left.data(), kBlockSize) > 0.001f);
    }

    SECTION("FormantVowel forwarded to Formant oscillator") {
        engine->setOscAType(OscType::Formant);
        engine->setOscAParam(OscParam::FormantVowel, 3.0f);  // O
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        REQUIRE(computeRMS(left.data(), kBlockSize) > 0.001f);
    }

    SECTION("SpectralPitchShift forwarded to SpectralFreeze oscillator") {
        engine->setOscAType(OscType::SpectralFreeze);
        engine->setOscAParam(OscParam::SpectralPitchShift, 12.0f);  // +12 semitones
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        // SpectralFreeze needs more blocks
        for (int i = 0; i < 4; ++i) {
            engine->processBlock(left.data(), right.data(), kBlockSize);
        }
        // May or may not produce output depending on freeze state, but should not crash
    }

    SECTION("ParticleDensity forwarded to Particle oscillator") {
        engine->setOscAType(OscType::Particle);
        engine->setOscAParam(OscParam::ParticleDensity, 32.0f);
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        for (int i = 0; i < 4; ++i) {
            engine->processBlock(left.data(), right.data(), kBlockSize);
        }
        // Should not crash; particle output is timing-dependent
    }
}

// =============================================================================
// R-022: the Vowel selector reaches the Formant oscillator through the processor
// =============================================================================
// applyParamsToEngine() re-sends every oscillator parameter each block, Vowel
// first and Morph second. FormantOscillator::setMorphPosition() overrode the
// vowel, so OSC A always played the morph position (A at the default Morph 0)
// whatever the Vowel dropdown said.

namespace {

/// Render OSC A as a Formant oscillator (OSC B muted by the mix) through the
/// processor, with the vowel and morph sent as parameter changes, and return
/// the last kTail samples of the left channel.
std::vector<float> renderFormantThroughProcessor(int vowel, float morph) {
    constexpr size_t kBlock = 512;
    constexpr int kBlocks = 160;   // ~1.9 s at 44.1 kHz
    constexpr size_t kTail = 16384;

    Ruinae::Processor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultTrue);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlock);
    REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultTrue);
    REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);

    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = channels;

    Krate::Test::EventList events;
    Krate::Test::ParameterChanges params;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kBlock);
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputParameterChanges = &params;
    data.inputEvents = &events;

    params.addChange(Ruinae::kOscATypeId,
        static_cast<double>(OscType::Formant) / (Ruinae::kOscTypeCount - 1));
    params.addChange(Ruinae::kOscAFormantVowelId, static_cast<double>(vowel) / 4.0);
    params.addChange(Ruinae::kOscAFormantMorphId, static_cast<double>(morph) / 4.0);
    params.addChange(Ruinae::kMixerPositionId, 0.0);  // OSC A only
    events.addNoteOn(43, 0.8f);                        // G2, 98 Hz: harmonic 4 at 392 Hz

    std::vector<float> all;
    all.reserve(kBlock * kBlocks);
    for (int b = 0; b < kBlocks; ++b) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        params.clear();
        events.clear();
        all.insert(all.end(), outL.begin(), outL.end());
    }
    processor.setActive(false);
    processor.terminate();
    return {all.end() - static_cast<std::ptrdiff_t>(kTail), all.end()};
}

/// Frequency of the strongest spectral peak between loHz and hiHz (Hann window).
float strongestPeakHz(const std::vector<float>& signal, float loHz, float hiHz) {
    const size_t n = signal.size();
    std::vector<float> window(n);
    Window::generateHann(window.data(), n);
    std::vector<float> windowed(n);
    for (size_t i = 0; i < n; ++i) windowed[i] = signal[i] * window[i];
    FFT fft;
    fft.prepare(n);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());
    const float binHz = static_cast<float>(kSampleRate) / static_cast<float>(n);
    size_t best = 0;
    float bestMag = -1.0f;
    for (size_t k = static_cast<size_t>(loHz / binHz); k <= static_cast<size_t>(hiHz / binHz); ++k) {
        const float mag = spectrum[k].magnitude();
        if (mag > bestMag) { bestMag = mag; best = k; }
    }
    return static_cast<float>(best) * binHz;
}

} // namespace

TEST_CASE("Formant vowel survives the per-block parameter fan-out", "[vst][osc-params]") {
    const auto vowelA = renderFormantThroughProcessor(0, 0.0f);
    const auto vowelO = renderFormantThroughProcessor(3, 0.0f);

    REQUIRE(computeRMS(vowelO.data(), vowelO.size()) > 0.001f);

    float maxDiff = 0.0f;
    for (size_t i = 0; i < vowelA.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(vowelA[i] - vowelO[i]));
    }
    INFO("max |vowel O - vowel A| = " << maxDiff);
    REQUIRE(maxDiff > 1e-3f);

    // Vowel O's F1 is 400 Hz (kVowelFormants5); vowel A's is 600 Hz. The
    // strongest formant peak (above the 98 Hz fundamental) must sit within 15 %
    // of vowel O's F1.
    constexpr float kVowelOF1 = 400.0f;
    const float peakO = strongestPeakHz(vowelO, 200.0f, 4000.0f);
    const float peakA = strongestPeakHz(vowelA, 200.0f, 4000.0f);
    INFO("strongest peak: vowel O " << peakO << " Hz, vowel A " << peakA << " Hz");
    REQUIRE(std::abs(peakO - kVowelOF1) <= 0.15f * kVowelOF1);
}
