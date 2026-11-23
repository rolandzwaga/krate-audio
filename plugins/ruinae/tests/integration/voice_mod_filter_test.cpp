// ==============================================================================
// Integration Test: Voice Modulation → Filter Cutoff (End-to-End)
// ==============================================================================
// Verifies that per-voice modulation routes (e.g. ENV3 → FilterCutoff) actually
// affect audio output when configured through the processor's voiceRoutes_ and
// processed through the full audio pipeline.
//
// Bug: ENV3 → FilterCutoff route produces no audible change in sound.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "drain_preset_transfer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numeric>
#include <vector>
#include "vst_param_changes.h"
#include "vst_event_list.h"

using Catch::Approx;

// =============================================================================
// Mock classes (same pattern as processor_audio_test.cpp)
// =============================================================================

namespace {

// IEventList mock consolidated into tests/test_helpers/vst_event_list.h
using MockEventList = Krate::Test::EventList;


// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using MockParameterChanges = Krate::Test::ParameterChanges;
using MockParamValueQueue = Krate::Test::ParamValueQueue;
using MockParamChangesWithData = Krate::Test::ParameterChanges;






// =============================================================================
// Helper: Send a VoiceModRouteUpdate message to the processor via notify()
// =============================================================================

void sendVoiceModRoute(Ruinae::Processor& proc, int slotIndex,
                       int source, int destination, double amount,
                       int curve = 0, double smoothMs = 0.0,
                       int scale = 2, int bypass = 0, int active = 1)
{
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("VoiceModRouteUpdate");
    auto* attrs = msg->getAttributes();
    attrs->setInt("slotIndex", static_cast<Steinberg::int64>(slotIndex));
    attrs->setInt("source", static_cast<Steinberg::int64>(source));
    attrs->setInt("destination", static_cast<Steinberg::int64>(destination));
    attrs->setFloat("amount", amount);
    attrs->setInt("curve", static_cast<Steinberg::int64>(curve));
    attrs->setFloat("smoothMs", smoothMs);
    attrs->setInt("scale", static_cast<Steinberg::int64>(scale));
    attrs->setInt("bypass", static_cast<Steinberg::int64>(bypass));
    attrs->setInt("active", static_cast<Steinberg::int64>(active));
    proc.notify(msg);
}

// =============================================================================
// Helpers
// =============================================================================

static float computeRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

static bool hasNonZeroSamples(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return true;
    }
    return false;
}

/// Process multiple blocks, accumulating output into a single buffer.
/// Returns the concatenated left-channel output.
static std::vector<float> processBlocks(
    Ruinae::Processor& processor,
    MockEventList& events,
    Steinberg::Vst::IParameterChanges* paramChanges,
    int numBlocks,
    size_t blockSize = 512)
{
    std::vector<float> accumulated;
    accumulated.reserve(static_cast<size_t>(numBlocks) * blockSize);

    std::vector<float> outL(blockSize, 0.0f);
    std::vector<float> outR(blockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(blockSize);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = paramChanges;
    data.inputEvents = &events;
    data.processContext = nullptr;

    MockParameterChanges emptyParams;

    for (int b = 0; b < numBlocks; ++b) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        processor.process(data);

        accumulated.insert(accumulated.end(), outL.begin(), outL.end());

        // Clear events after first block (noteOn only needs to be sent once)
        if (b == 0) {
            events.clear();
            data.inputParameterChanges = &emptyParams;
        }
    }

    return accumulated;
}

} // anonymous namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Voice mod route ENV3 -> FilterCutoff affects audio output",
          "[voice-mod][filter][integration]") {
    // =========================================================================
    // Setup: Two identical processors, one with ENV3->FilterCutoff route
    // =========================================================================
    auto initProc = []() {
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = 512;
        proc->setupProcessing(setup);
        proc->setActive(true);
        return proc;
    };

    auto procWithMod = initProc();
    auto procNoMod = initProc();

    // =========================================================================
    // Configure: Set filter cutoff LOW so modulation has room to move it
    // =========================================================================
    // Filter cutoff parameter range: 20-20000 Hz
    // Normalized 0.0 = 20 Hz, 1.0 = 20000 Hz
    // Set cutoff low (~200 Hz) so ENV3 pushing it up is clearly audible
    constexpr double kLowCutoffNorm = 0.1; // ~200 Hz

    // =========================================================================
    // Configure: Add ENV3 → FilterCutoff voice mod route on procWithMod
    // =========================================================================
    // Send ENV3 -> FilterCutoff voice mod route via IMessage (same path as UI)
    sendVoiceModRoute(*procWithMod,
        /*slotIndex=*/0,
        /*source=*/2,        // Env3
        /*destination=*/0,   // FilterCutoff
        /*amount=*/1.0);     // Full positive

    // =========================================================================
    // Process: Set filter cutoff, play a note, process ~200ms of audio
    // =========================================================================
    constexpr int kNumBlocks = 20; // ~232ms at 512 samples/block, 44.1kHz

    // Process with modulation
    std::vector<float> outputWithMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f); // Middle C

        MockParamChangesWithData params;
        params.addChange(Ruinae::kFilterCutoffId, kLowCutoffNorm);

        outputWithMod = processBlocks(*procWithMod, events, &params, kNumBlocks);
    }

    // Process without modulation (identical except no voice route)
    std::vector<float> outputNoMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f); // Middle C

        MockParamChangesWithData params;
        params.addChange(Ruinae::kFilterCutoffId, kLowCutoffNorm);

        outputNoMod = processBlocks(*procNoMod, events, &params, kNumBlocks);
    }

    // =========================================================================
    // Verify: Both produce audio
    // =========================================================================
    REQUIRE(hasNonZeroSamples(outputWithMod.data(), outputWithMod.size()));
    REQUIRE(hasNonZeroSamples(outputNoMod.data(), outputNoMod.size()));

    float rmsWithMod = computeRMS(outputWithMod.data(), outputWithMod.size());
    float rmsNoMod = computeRMS(outputNoMod.data(), outputNoMod.size());

    INFO("RMS with ENV3->FilterCutoff mod: " << rmsWithMod);
    INFO("RMS without mod route: " << rmsNoMod);

    // =========================================================================
    // Verify: The modulation route MUST change the output
    // =========================================================================
    // ENV3 at sustain should push the filter cutoff up from ~200 Hz,
    // letting through significantly more harmonics. The RMS of the
    // modulated output should be measurably higher than unmodulated.
    //
    // If this fails, the voice mod route is not being applied.
    // =========================================================================

    // The outputs must be different (modulation has an effect)
    bool outputsDiffer = false;
    for (size_t i = 0; i < outputWithMod.size() && i < outputNoMod.size(); ++i) {
        if (std::abs(outputWithMod[i] - outputNoMod[i]) > 1e-6f) {
            outputsDiffer = true;
            break;
        }
    }

    // PRIMARY ASSERTION: ENV3 -> FilterCutoff route must change the audio
    REQUIRE(outputsDiffer);

    // SECONDARY: Modulated output should have more energy (filter opens up)
    REQUIRE(rmsWithMod > rmsNoMod * 1.1f);
}

TEST_CASE("Voice mod route ENV3 -> FilterCutoff with negative amount darkens sound",
          "[voice-mod][filter][integration]") {
    // Same test but with negative amount: ENV3 should push cutoff DOWN
    auto makeProc = []() {
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = 512;
        proc->setupProcessing(setup);
        proc->setActive(true);
        return proc;
    };

    auto procWithMod = makeProc();
    auto procNoMod = makeProc();

    // High cutoff so negative modulation can pull it down
    constexpr double kHighCutoffNorm = 0.9;

    // Pin resonance to the bottom of its range. This test measures how much
    // energy a closing cutoff removes, and a resonant peak partially offsets
    // that as the cutoff sweeps down -- so leaving resonance at whatever the
    // default happens to be makes the measurement depend on an unrelated
    // parameter's default rather than on the modulation under test.
    constexpr double kNoResonanceNorm = 0.0;

    // Negative amount: ENV3 should close the filter
    sendVoiceModRoute(*procWithMod,
        /*slotIndex=*/0,
        /*source=*/2,        // Env3
        /*destination=*/0,   // FilterCutoff
        /*amount=*/-1.0);    // Full negative

    constexpr int kNumBlocks = 20;

    std::vector<float> outputWithMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f);
        MockParamChangesWithData params;
        params.addChange(Ruinae::kFilterCutoffId, kHighCutoffNorm);
        params.addChange(Ruinae::kFilterResonanceId, kNoResonanceNorm);
        outputWithMod = processBlocks(*procWithMod, events, &params, kNumBlocks);
    }

    std::vector<float> outputNoMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f);
        MockParamChangesWithData params;
        params.addChange(Ruinae::kFilterCutoffId, kHighCutoffNorm);
        params.addChange(Ruinae::kFilterResonanceId, kNoResonanceNorm);
        outputNoMod = processBlocks(*procNoMod, events, &params, kNumBlocks);
    }

    REQUIRE(hasNonZeroSamples(outputWithMod.data(), outputWithMod.size()));
    REQUIRE(hasNonZeroSamples(outputNoMod.data(), outputNoMod.size()));

    float rmsWithMod = computeRMS(outputWithMod.data(), outputWithMod.size());
    float rmsNoMod = computeRMS(outputNoMod.data(), outputNoMod.size());

    INFO("RMS with negative ENV3->FilterCutoff: " << rmsWithMod);
    INFO("RMS without mod: " << rmsNoMod);

    // Outputs must differ
    bool outputsDiffer = false;
    for (size_t i = 0; i < outputWithMod.size() && i < outputNoMod.size(); ++i) {
        if (std::abs(outputWithMod[i] - outputNoMod[i]) > 1e-6f) {
            outputsDiffer = true;
            break;
        }
    }

    REQUIRE(outputsDiffer);

    // Negative modulation should reduce energy (filter closes)
    REQUIRE(rmsWithMod < rmsNoMod * 0.9f);
}

// =============================================================================
// Processor-level regression: OscAPitch modulation through full pipeline
// =============================================================================
// This test verifies that OscAPitch modulation survives the applyParamsToEngine
// overwrite cycle. The processor calls setOscATuneSemitones() every block which
// resets oscillator frequencies. The pitch mod offset must persist through this.

TEST_CASE("Voice mod route ENV3 -> OscAPitch affects audio through processor",
          "[voice-mod][pitch][integration]") {
    auto initProc = []() {
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = 512;
        proc->setupProcessing(setup);
        proc->setActive(true);
        return proc;
    };

    auto procWithMod = initProc();
    auto procNoMod = initProc();

    // ENV3 -> OscAPitch (destination=5), full positive amount
    sendVoiceModRoute(*procWithMod,
        /*slotIndex=*/0,
        /*source=*/2,        // Env3
        /*destination=*/5,   // OscAPitch
        /*amount=*/1.0);     // Full positive

    constexpr int kNumBlocks = 30;

    std::vector<float> outputWithMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f);
        MockParameterChanges emptyParams;
        outputWithMod = processBlocks(*procWithMod, events, &emptyParams, kNumBlocks);
    }

    std::vector<float> outputNoMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f);
        MockParameterChanges emptyParams;
        outputNoMod = processBlocks(*procNoMod, events, &emptyParams, kNumBlocks);
    }

    REQUIRE(hasNonZeroSamples(outputWithMod.data(), outputWithMod.size()));
    REQUIRE(hasNonZeroSamples(outputNoMod.data(), outputNoMod.size()));

    bool differ = false;
    for (size_t i = 0; i < outputWithMod.size() && i < outputNoMod.size(); ++i) {
        if (std::abs(outputWithMod[i] - outputNoMod[i]) > 1e-6f) {
            differ = true;
            break;
        }
    }

    // PRIMARY ASSERTION: ENV3 -> OscAPitch must change the audio output
    REQUIRE(differ);
}

// =============================================================================
// R-045, R-044, R-053: voice-route Curve, Smooth and Scale, read back from the
// sounding voice's route offsets (destination units: semitones, Q).
// =============================================================================

namespace {

constexpr double kRouteSampleRate = 44100.0;

class VoiceRouteTestProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::engine;
};

std::unique_ptr<VoiceRouteTestProcessor> makeRouteProcessor() {
    auto proc = std::make_unique<VoiceRouteTestProcessor>();
    proc->initialize(nullptr);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kRouteSampleRate;
    setup.maxSamplesPerBlock = 512;
    proc->setupProcessing(setup);
    proc->setActive(true);
    return proc;
}

/// The one active voice (each case plays a single note).
const Krate::DSP::RuinaeVoice& soundingVoice(const VoiceRouteTestProcessor& proc) {
    const auto& engine = proc.engine();
    for (size_t i = 0; i < Krate::DSP::RuinaeEngine::kMaxPolyphony; ++i) {
        if (engine.getVoice(i).isActive()) return engine.getVoice(i);
    }
    FAIL("no active voice");
    return engine.getVoice(0);
}

/// Plays `pitch` at `velocity` with `params` in the first block, renders `numBlocks`
/// blocks of `blockSize` samples and calls `afterBlock(blockIndex)` after each block.
template <typename AfterBlock>
void playNote(Ruinae::Processor& proc, float velocity, MockParamChangesWithData& params,
              int numBlocks, size_t blockSize, AfterBlock&& afterBlock) {
    MockEventList events;
    events.addNoteOn(60, velocity);
    std::vector<float> outL(blockSize, 0.0f);
    std::vector<float> outR(blockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;
    MockParameterChanges emptyParams;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(blockSize);
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &params;
    data.inputEvents = &events;
    for (int b = 0; b < numBlocks; ++b) {
        proc.process(data);
        afterBlock(b);
        events.clear();
        data.inputParameterChanges = &emptyParams;
    }
}

/// The voice route offset for `dest` after ~0.23 s of a note at `velocity`.
float settledVoiceRouteOffset(VoiceRouteTestProcessor& proc, float velocity,
                              Krate::DSP::VoiceModDest dest) {
    MockParamChangesWithData params;
    playNote(proc, velocity, params, 20, 512, [](int) {});
    return soundingVoice(proc).getModRouteOffset(dest);
}

// Mod matrix slot 0 parameter values (normalized as the host sends them).
constexpr double kSourceMacro1Norm = 5.0 / 14.0;         // ModSource::Macro1, 15 entries
constexpr double kDestAllVoiceResonanceNorm = 8.0 / 14.0; // RuinaeModDest::AllVoiceResonance
constexpr double kScaleX4Norm = 1.0;                      // index 4 of {x0.25 .. x4}
constexpr int kScaleX4 = 4;
constexpr float kBaseResonanceQ = 0.1f;                   // kFilterResonanceId at 0

/// The voice's filter resonance (Q) after ~0.23 s with Macro 1 at `macro` routed to
/// All-Voice Resonance by mod matrix slot 0 at `amount` and `scaleNorm`.
float globalResonanceRoute(float macro, double amount, double scaleNorm) {
    auto proc = makeRouteProcessor();
    MockParamChangesWithData params;
    params.addChange(Ruinae::kFilterResonanceId, 0.0);
    params.addChange(Ruinae::kModMatrixSlot0SourceId, kSourceMacro1Norm);
    params.addChange(Ruinae::kModMatrixSlot0DestId, kDestAllVoiceResonanceNorm);
    params.addChange(Ruinae::kModMatrixSlot0AmountId, (amount + 1.0) / 2.0);
    params.addChange(Ruinae::kModMatrixSlot0ScaleId, scaleNorm);
    params.addChange(Ruinae::kMacro1ValueId, static_cast<double>(macro));
    playNote(*proc, 1.0f, params, 20, 512, [](int) {});
    const float q = soundingVoice(*proc).getFilterResonance();
    proc->setActive(false);
    proc->terminate();
    return q;
}

} // anonymous namespace

TEST_CASE("Voice route curve and smoothing are applied",
          "[integration][modmatrix][voice]") {
    using Krate::DSP::VoiceModDest;

    SECTION("Exponential curve: velocity 0.5 -> cutoff offset 0.25 x 48 st") {
        auto proc = makeRouteProcessor();
        sendVoiceModRoute(*proc, 0, /*Velocity*/ 5, /*FilterCutoff*/ 0, 1.0,
                          /*curve Exponential*/ 1);
        const float offset = settledVoiceRouteOffset(*proc, 0.5f, VoiceModDest::FilterCutoff);
        INFO("cutoff offset " << offset << " st (linear would be ~24)");
        CHECK(offset == Approx(0.25f * 48.0f).margin(0.5f));
        proc->setActive(false);
        proc->terminate();
    }

    SECTION("Smooth 50 ms: a Gate source step rises 10-90 % in 50 ms +/- 20 %") {
        auto proc = makeRouteProcessor();
        sendVoiceModRoute(*proc, 0, /*GateOutput*/ 4, /*FilterCutoff*/ 0, 1.0,
                          /*curve*/ 0, /*smoothMs*/ 50.0);
        constexpr size_t kBlock = 32;
        constexpr int kBlocks = 600;  // ~0.44 s
        std::vector<float> offsets;
        offsets.reserve(kBlocks);
        MockParamChangesWithData params;
        playNote(*proc, 1.0f, params, kBlocks, kBlock, [&](int) {
            offsets.push_back(soundingVoice(*proc).getModRouteOffset(VoiceModDest::FilterCutoff));
        });
        const float finalValue = offsets.back();
        INFO("settled offset " << finalValue << " st");
        REQUIRE(finalValue == Approx(48.0f).margin(0.5f));

        // First crossing of `level` (fraction of the settled value), interpolated
        // between block ends, in ms after the note-on.
        auto crossingMs = [&](float level) {
            const float threshold = level * finalValue;
            float prev = 0.0f;
            for (size_t b = 0; b < offsets.size(); ++b) {
                if (offsets[b] >= threshold) {
                    const float frac = (offsets[b] > prev)
                        ? (threshold - prev) / (offsets[b] - prev) : 1.0f;
                    const double samples = (static_cast<double>(b) + frac) * kBlock;
                    return samples * 1000.0 / kRouteSampleRate;
                }
                prev = offsets[b];
            }
            return 1e9;
        };
        const double riseMs = crossingMs(0.9f) - crossingMs(0.1f);
        INFO("10-90 % rise " << riseMs << " ms");
        CHECK(riseMs >= 40.0);
        CHECK(riseMs <= 60.0);
        proc->setActive(false);
        proc->terminate();
    }
}

TEST_CASE("Voice and global resonance routes have the same full scale",
          "[integration][modmatrix]") {
    // Voice route: Velocity (1.0) -> Filter Resonance at amount 1.
    auto proc = makeRouteProcessor();
    sendVoiceModRoute(*proc, 0, /*Velocity*/ 5, /*FilterResonance*/ 1, 1.0);
    const float voiceQ = settledVoiceRouteOffset(
        *proc, 1.0f, Krate::DSP::VoiceModDest::FilterResonance);
    proc->setActive(false);
    proc->terminate();

    // Global route: Macro 1 (1.0) -> All-Voice Resonance at amount 1.
    const float globalQ = globalResonanceRoute(1.0f, 1.0, /*x1*/ 0.5) - kBaseResonanceQ;

    INFO("voice route " << voiceQ << " Q, global route " << globalQ << " Q");
    CHECK(voiceQ == Approx(10.0f).margin(0.05f));
    CHECK(globalQ == Approx(10.0f).margin(0.05f));
}

TEST_CASE("Scale x4 keeps amount resolution", "[modmatrix]") {
    SECTION("voice route: Velocity 0.2 -> cutoff, x4 at amount 0.5 vs 1.0") {
        auto offsetAt = [](double amount) {
            auto proc = makeRouteProcessor();
            sendVoiceModRoute(*proc, 0, /*Velocity*/ 5, /*FilterCutoff*/ 0, amount,
                              0, 0.0, kScaleX4);
            const float offset = settledVoiceRouteOffset(
                *proc, 0.2f, Krate::DSP::VoiceModDest::FilterCutoff);
            proc->setActive(false);
            proc->terminate();
            return offset;
        };
        const float half = offsetAt(0.5);
        const float full = offsetAt(1.0);
        INFO("x4: amount 0.5 -> " << half << " st, amount 1.0 -> " << full << " st");
        CHECK(full - half > 1.0f);
        CHECK(full == Approx(2.0f * half).epsilon(0.02));
    }

    SECTION("global route: Macro 1 at 0.2 -> All-Voice Resonance, x4 at amount 0.5 vs 1.0") {
        const float half = globalResonanceRoute(0.2f, 0.5, kScaleX4Norm) - kBaseResonanceQ;
        const float full = globalResonanceRoute(0.2f, 1.0, kScaleX4Norm) - kBaseResonanceQ;
        INFO("x4: amount 0.5 -> +" << half << " Q, amount 1.0 -> +" << full << " Q");
        CHECK(full - half > 1.0f);
        CHECK(full == Approx(2.0f * half).epsilon(0.02));
    }
}

// =============================================================================
// R-093: the Voice LFO Rate and Shape parameters reach every voice's LFO
// =============================================================================

TEST_CASE("Voice LFO rate and shape parameters reach every voice",
          "[integration][voice][lfo]") {
    using Krate::DSP::Waveform;
    auto proc = makeRouteProcessor();
    MockParamChangesWithData params;
    SECTION("defaults: 1 Hz sine") {
        playNote(*proc, 1.0f, params, 2, 512, [](int) {});
        for (size_t i = 0; i < Krate::DSP::RuinaeEngine::kMaxPolyphony; ++i) {
            const auto& lfo = proc->engine().getVoice(i).getVoiceLFO();
            CHECK(lfo.frequency() == Approx(1.0f).margin(1e-4f));
            CHECK(lfo.waveform() == Waveform::Sine);
        }
    }
    SECTION("30 Hz square: above the LFO's 20 Hz default clamp") {
        params.addChange(Ruinae::kVoiceLfoRateId, Ruinae::lfoRateToNormalized(30.0f));
        params.addChange(Ruinae::kVoiceLfoShapeId, 3.0 / 5.0);
        playNote(*proc, 1.0f, params, 2, 512, [](int) {});
        for (size_t i = 0; i < Krate::DSP::RuinaeEngine::kMaxPolyphony; ++i) {
            const auto& lfo = proc->engine().getVoice(i).getVoiceLFO();
            INFO("voice " << i);
            CHECK(lfo.frequency() == Approx(30.0f).margin(1e-3f));
            CHECK(lfo.waveform() == Waveform::Square);
        }
    }
    proc->setActive(false);
    proc->terminate();
}
