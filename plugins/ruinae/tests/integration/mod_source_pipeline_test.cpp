// ==============================================================================
// Integration Test: Mod Source Pipeline - All Sources via Parameter Pipeline
// ==============================================================================
// Tests that EVERY mod source produces non-zero modulation when routed through
// the full parameter pipeline (normalized params → processor → engine → output).
//
// Bug report: Rungler and Macros don't modulate Global Filter Cutoff, but LFO1
// does. This test isolates whether the bug is in the parameter pipeline or
// the Controller/UI layer.
//
// Strategy:
// 1. For each ModSource, compute the SAME normalized values the Controller would
// 2. Send them to the Processor via mock IParameterChanges
// 3. Process audio blocks with global filter enabled
// 4. Verify modulation offset is non-zero (via output level difference)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "parameters/dropdown_mappings.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <krate/dsp/core/modulation_types.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Mock: Single Parameter Value Queue
// =============================================================================

class PipelineParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    PipelineParamValueQueue(Steinberg::Vst::ParamID id, double value)
        : paramId_(id), value_(value) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }

    Steinberg::tresult PLUGIN_API getPoint(
        Steinberg::int32 index,
        Steinberg::int32& sampleOffset,
        Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(
        Steinberg::int32, Steinberg::Vst::ParamValue,
        Steinberg::int32&) override {
        return Steinberg::kResultFalse;
    }

private:
    Steinberg::Vst::ParamID paramId_;
    double value_;
};

// =============================================================================
// Mock: Parameter Changes Container
// =============================================================================

class PipelineParamChanges : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override {
        return nullptr;
    }

    void addChange(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

private:
    std::vector<PipelineParamValueQueue> queues_;
};

// =============================================================================
// Mock: Empty Event List
// =============================================================================

class PipelineEmptyEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getEventCount() override { return 0; }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32,
                                            Steinberg::Vst::Event&) override {
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override {
        return Steinberg::kResultTrue;
    }
};

// =============================================================================
// Mock: NoteOn Event List (fires once)
// =============================================================================

class PipelineNoteOnEvents : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getEventCount() override { return sent_ ? 0 : 1; }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                            Steinberg::Vst::Event& e) override {
        if (index != 0 || sent_) return Steinberg::kResultFalse;
        e = {};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = 0;
        e.noteOn.channel = 0;
        e.noteOn.pitch = 60;
        e.noteOn.velocity = 0.8f;
        e.noteOn.noteId = -1;
        sent_ = true;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override {
        return Steinberg::kResultTrue;
    }
    bool sent_ = false;
};

// =============================================================================
// Helper: compute RMS of a buffer
// =============================================================================

static float computeRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

// =============================================================================
// Helper: compute the same normalized source value the Controller would send
// =============================================================================
// The controller does:
//   int dspSrcIdx = uiGridIndex + 1;  (skip None at 0)
//   double srcNorm = dspSrcIdx / (kModSourceCount - 1);
//
// For our test, we use the DSP ModSource enum directly:
//   srcNorm = static_cast<int>(modSource) / 13.0

static double sourceToNormalized(ModSource src) {
    return static_cast<double>(static_cast<int>(src)) /
           static_cast<double>(Ruinae::kModSourceCount - 1);
}

// Destination normalized: index / (kModDestCount - 1)
// Index 0 = GlobalFilterCutoff
static double destToNormalized(int destIndex) {
    return static_cast<double>(destIndex) /
           static_cast<double>(Ruinae::kModDestCount - 1);
}

// Amount normalized: bipolar [-1,+1] mapped to [0,1]
// amount 1.0 → normalized 1.0
// amount 0.0 → normalized 0.5
// amount -1.0 → normalized 0.0
static double amountToNormalized(float amount) {
    return static_cast<double>((amount + 1.0f) / 2.0f);
}

// =============================================================================
// Test fixture
// =============================================================================

struct ModSourcePipelineFixture {
    Ruinae::Processor processor;
    PipelineEmptyEventList emptyEvents;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channelBuffers[2];
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    static constexpr size_t kBlockSize = 512;
    static constexpr int kWarmUpBlocks = 20;

    ModSourcePipelineFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
        channelBuffers[0] = outL.data();
        channelBuffers[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputEvents = &emptyEvents;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~ModSourcePipelineFixture() {
        processor.setActive(false);
        processor.terminate();
    }

    void processWithParams(PipelineParamChanges& params) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &params;
        processor.process(data);
    }

    void processEmpty() {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        PipelineParamChanges empty;
        data.inputParameterChanges = &empty;
        processor.process(data);
    }

    // Measure total RMS over multiple blocks (after warm-up)
    float measureRms(int measureBlocks = 30) {
        float totalRms = 0.0f;
        int counted = 0;
        for (int i = 0; i < measureBlocks; ++i) {
            processEmpty();
            if (i >= 5) {  // skip warm-up
                totalRms += computeRMS(outL.data(), kBlockSize);
                counted++;
            }
        }
        return (counted > 0) ? totalRms / static_cast<float>(counted) : 0.0f;
    }
};

// =============================================================================
// DIAGNOSTIC TEST: Verify all mod sources produce modulation via param pipeline
// =============================================================================
// This is the main diagnostic test. It routes each source to GlobalFilterCutoff
// via the parameter pipeline and checks whether modulation actually occurs.

TEST_CASE("Mod source pipeline: each source produces modulation via param pipeline",
          "[mod-source-pipeline][integration][diagnostic]") {

    // Test each mod source by routing it through the parameter pipeline
    struct SourceTestCase {
        ModSource source;
        const char* name;
    };

    // All non-None sources
    const SourceTestCase sources[] = {
        {ModSource::LFO1,          "LFO1"},
        {ModSource::LFO2,          "LFO2"},
        {ModSource::EnvFollower,   "EnvFollower"},
        {ModSource::Random,        "Random"},
        {ModSource::Macro1,        "Macro1"},
        {ModSource::Macro2,        "Macro2"},
        {ModSource::Macro3,        "Macro3"},
        {ModSource::Macro4,        "Macro4"},
        {ModSource::Chaos,         "Chaos"},
        {ModSource::Rungler,       "Rungler"},
        {ModSource::SampleHold,    "SampleHold"},
        {ModSource::PitchFollower, "PitchFollower"},
        {ModSource::Transient,     "Transient"},
    };

    for (const auto& tc : sources) {
        DYNAMIC_SECTION("Source: " << tc.name) {
            // -----------------------------------------------------------
            // Setup: fresh processor with global filter enabled at 1kHz
            // -----------------------------------------------------------
            ModSourcePipelineFixture f;

            // Enable global filter (lowpass at 1kHz)
            PipelineParamChanges setupParams;
            setupParams.addChange(Ruinae::kGlobalFilterEnabledId, 1.0);
            setupParams.addChange(Ruinae::kGlobalFilterCutoffId, 0.3);  // ~1kHz
            setupParams.addChange(Ruinae::kGlobalFilterTypeId, 0.0);    // Lowpass
            setupParams.addChange(Ruinae::kGlobalFilterResonanceId, 0.2);
            // Disable effects to isolate filter behavior
            setupParams.addChange(Ruinae::kDelayEnabledId, 0.0);
            setupParams.addChange(Ruinae::kReverbEnabledId, 0.0);
            setupParams.addChange(Ruinae::kPhaserEnabledId, 0.0);
            f.processWithParams(setupParams);

            // -----------------------------------------------------------
            // Step 1: Measure baseline RMS (no modulation)
            // -----------------------------------------------------------
            {
                PipelineNoteOnEvents noteOn;
                f.data.inputEvents = &noteOn;
                f.processEmpty();
                f.data.inputEvents = &f.emptyEvents;
            }

            // Warm up and measure baseline
            float baselineRms = f.measureRms(40);

            // -----------------------------------------------------------
            // Step 2: Set mod route via parameter pipeline
            // -----------------------------------------------------------
            // Use slot 0: Source → GlobalFilterCutoff, amount = +1.0

            double srcNorm = sourceToNormalized(tc.source);
            double dstNorm = destToNormalized(0);  // GlobalFilterCutoff
            double amtNorm = amountToNormalized(1.0f);  // Full positive

            PipelineParamChanges modParams;
            // Mod matrix slot 0: source, dest, amount
            modParams.addChange(Ruinae::kModMatrixSlot0SourceId, srcNorm);
            modParams.addChange(Ruinae::kModMatrixSlot0DestId, dstNorm);
            modParams.addChange(Ruinae::kModMatrixSlot0AmountId, amtNorm);

            // Source-specific setup: make the source produce a non-zero value
            switch (tc.source) {
                case ModSource::LFO1:
                    modParams.addChange(Ruinae::kLFO1RateId, 0.5);  // ~2Hz
                    modParams.addChange(Ruinae::kLFO1ShapeId, 0.0);  // Sine
                    break;
                case ModSource::LFO2:
                    modParams.addChange(Ruinae::kLFO2RateId, 0.5);
                    modParams.addChange(Ruinae::kLFO2ShapeId, 0.0);
                    break;
                case ModSource::Macro1:
                    modParams.addChange(Ruinae::kMacro1ValueId, 1.0);  // Full value
                    break;
                case ModSource::Macro2:
                    modParams.addChange(Ruinae::kMacro2ValueId, 1.0);
                    break;
                case ModSource::Macro3:
                    modParams.addChange(Ruinae::kMacro3ValueId, 1.0);
                    break;
                case ModSource::Macro4:
                    modParams.addChange(Ruinae::kMacro4ValueId, 1.0);
                    break;
                case ModSource::Rungler:
                    // Set fast oscillator freqs for quick CV generation
                    modParams.addChange(Ruinae::kRunglerOsc1FreqId, 0.8);  // ~25Hz
                    modParams.addChange(Ruinae::kRunglerOsc2FreqId, 0.9);  // ~50Hz
                    modParams.addChange(Ruinae::kRunglerDepthId, 0.5);
                    modParams.addChange(Ruinae::kRunglerBitsId, 0.33);  // 8 bits
                    break;
                case ModSource::Chaos:
                    modParams.addChange(Ruinae::kChaosModRateId, 0.5);
                    break;
                default:
                    // EnvFollower, Random, SampleHold, PitchFollower, Transient
                    // will either produce output from audio input or internal state
                    break;
            }

            f.processWithParams(modParams);

            // -----------------------------------------------------------
            // Step 3: Measure RMS with modulation active
            // -----------------------------------------------------------
            float modulatedRms = f.measureRms(40);

            // -----------------------------------------------------------
            // Step 4: Report and verify
            // -----------------------------------------------------------
            INFO("Source: " << tc.name);
            INFO("  srcNorm = " << srcNorm);
            INFO("  dstNorm = " << dstNorm);
            INFO("  amtNorm = " << amtNorm);
            INFO("  Denormalized source = " << static_cast<int>(srcNorm * 13 + 0.5));
            INFO("  Expected ModSource = " << static_cast<int>(tc.source));
            INFO("  Baseline RMS  = " << baselineRms);
            INFO("  Modulated RMS = " << modulatedRms);
            INFO("  RMS difference = " << std::abs(modulatedRms - baselineRms));

            // For sources that should produce DC or time-varying output,
            // the RMS should differ from baseline.
            // Note: some sources (EnvFollower, PitchFollower, Transient) need
            // audio input to produce output, so they might show zero change.
            // We'll still log them for diagnostics.
            bool isInputDependent = (tc.source == ModSource::EnvFollower ||
                                     tc.source == ModSource::PitchFollower ||
                                     tc.source == ModSource::Transient);

            if (!isInputDependent) {
                // These sources should definitely produce modulation
                float rmsDiff = std::abs(modulatedRms - baselineRms);
                INFO("FAIL: Source " << tc.name << " produced no modulation! "
                     "RMS diff = " << rmsDiff);
                REQUIRE(rmsDiff > 0.0001f);
            }
        }
    }
}

// =============================================================================
// FOCUSED TEST: Macro1 DC source via parameter pipeline
// =============================================================================
// Macro1 is the simplest case: set value to 1.0, route to GlobalFilterCutoff.
// If this fails, the parameter pipeline is broken for sources other than LFO.

TEST_CASE("Mod source pipeline: Macro1 DC source modulates global filter cutoff",
          "[mod-source-pipeline][integration][macro]") {

    ModSourcePipelineFixture f;

    // Enable global filter
    PipelineParamChanges setupParams;
    setupParams.addChange(Ruinae::kGlobalFilterEnabledId, 1.0);
    setupParams.addChange(Ruinae::kGlobalFilterCutoffId, 0.2);  // Low cutoff
    setupParams.addChange(Ruinae::kGlobalFilterTypeId, 0.0);     // Lowpass
    setupParams.addChange(Ruinae::kGlobalFilterResonanceId, 0.3);
    setupParams.addChange(Ruinae::kDelayEnabledId, 0.0);
    setupParams.addChange(Ruinae::kReverbEnabledId, 0.0);
    setupParams.addChange(Ruinae::kPhaserEnabledId, 0.0);
    f.processWithParams(setupParams);

    // Play a note
    {
        PipelineNoteOnEvents noteOn;
        f.data.inputEvents = &noteOn;
        f.processEmpty();
        f.data.inputEvents = &f.emptyEvents;
    }

    // Measure baseline (no modulation, low cutoff → muffled)
    float baselineRms = f.measureRms(30);

    // Now route Macro1 → GlobalFilterCutoff with amount = +1.0
    // And set Macro1 value to 1.0 (should open the filter)
    PipelineParamChanges modParams;

    // Controller normalized values:
    // Macro1 = ModSource::Macro1 = 5
    // srcNorm = 5 / 13 = 0.384615...
    double srcNorm = sourceToNormalized(ModSource::Macro1);
    double dstNorm = destToNormalized(0);  // GlobalFilterCutoff
    double amtNorm = amountToNormalized(1.0f);  // Full positive

    modParams.addChange(Ruinae::kModMatrixSlot0SourceId, srcNorm);
    modParams.addChange(Ruinae::kModMatrixSlot0DestId, dstNorm);
    modParams.addChange(Ruinae::kModMatrixSlot0AmountId, amtNorm);
    modParams.addChange(Ruinae::kMacro1ValueId, 1.0);  // Full macro value

    // Log the exact values being sent
    INFO("srcNorm = " << srcNorm << " (should denorm to 5 = Macro1)");
    INFO("dstNorm = " << dstNorm << " (should denorm to 0 = GlobalFilterCutoff)");
    INFO("amtNorm = " << amtNorm << " (should denorm to +1.0)");
    INFO("Denormalized source = " << static_cast<int>(srcNorm * 13 + 0.5));
    INFO("Denormalized dest = " << static_cast<int>(dstNorm * 7 + 0.5));

    f.processWithParams(modParams);

    // Measure with modulation (Macro1=1.0 should push cutoff HIGH → brighter)
    float modulatedRms = f.measureRms(30);

    INFO("Baseline RMS (low cutoff, no mod):  " << baselineRms);
    INFO("Modulated RMS (Macro1 → high cutoff): " << modulatedRms);
    INFO("RMS difference: " << (modulatedRms - baselineRms));

    // With Macro1 at full value opening the filter, output should be louder
    REQUIRE(baselineRms > 0.0001f);  // Must have audio
    REQUIRE(modulatedRms > baselineRms * 1.05f);  // Modulation must have effect
}

// =============================================================================
// FOCUSED TEST: LFO1 via parameter pipeline (known working, control test)
// =============================================================================

TEST_CASE("Mod source pipeline: LFO1 modulates global filter cutoff (control test)",
          "[mod-source-pipeline][integration][lfo]") {

    ModSourcePipelineFixture f;

    // Enable global filter
    PipelineParamChanges setupParams;
    setupParams.addChange(Ruinae::kGlobalFilterEnabledId, 1.0);
    setupParams.addChange(Ruinae::kGlobalFilterCutoffId, 0.2);
    setupParams.addChange(Ruinae::kGlobalFilterTypeId, 0.0);
    setupParams.addChange(Ruinae::kGlobalFilterResonanceId, 0.3);
    setupParams.addChange(Ruinae::kDelayEnabledId, 0.0);
    setupParams.addChange(Ruinae::kReverbEnabledId, 0.0);
    setupParams.addChange(Ruinae::kPhaserEnabledId, 0.0);
    f.processWithParams(setupParams);

    // Play a note
    {
        PipelineNoteOnEvents noteOn;
        f.data.inputEvents = &noteOn;
        f.processEmpty();
        f.data.inputEvents = &f.emptyEvents;
    }

    // Measure baseline
    float baselineRms = f.measureRms(30);

    // Route LFO1 → GlobalFilterCutoff
    double srcNorm = sourceToNormalized(ModSource::LFO1);
    double dstNorm = destToNormalized(0);
    double amtNorm = amountToNormalized(1.0f);

    PipelineParamChanges modParams;
    modParams.addChange(Ruinae::kModMatrixSlot0SourceId, srcNorm);
    modParams.addChange(Ruinae::kModMatrixSlot0DestId, dstNorm);
    modParams.addChange(Ruinae::kModMatrixSlot0AmountId, amtNorm);
    modParams.addChange(Ruinae::kLFO1RateId, 0.3);  // Slow enough to see effect

    INFO("srcNorm = " << srcNorm << " (should denorm to 1 = LFO1)");
    INFO("Denormalized source = " << static_cast<int>(srcNorm * 13 + 0.5));

    f.processWithParams(modParams);

    // LFO produces a time-varying signal. Measure variance across blocks.
    std::vector<float> blockRms;
    for (int i = 0; i < 40; ++i) {
        f.processEmpty();
        blockRms.push_back(computeRMS(f.outL.data(), ModSourcePipelineFixture::kBlockSize));
    }

    // Find min/max RMS to detect variation (LFO should cause filter sweep)
    float minRms = *std::min_element(blockRms.begin(), blockRms.end());
    float maxRms = *std::max_element(blockRms.begin(), blockRms.end());

    INFO("Baseline RMS: " << baselineRms);
    INFO("Min block RMS with LFO: " << minRms);
    INFO("Max block RMS with LFO: " << maxRms);
    INFO("RMS range (max-min): " << (maxRms - minRms));

    REQUIRE(baselineRms > 0.0001f);
    // LFO should cause filter sweep → RMS variation across blocks
    REQUIRE((maxRms - minRms) > 0.001f);
}

// =============================================================================
// DIAGNOSTIC: Print denormalized values for all sources
// =============================================================================
// Pure math test — no audio, just verify the normalization round-trip.

TEST_CASE("Mod source pipeline: normalization round-trip for all sources",
          "[mod-source-pipeline][unit][normalization]") {

    for (int srcEnum = 0; srcEnum < static_cast<int>(kModSourceCount); ++srcEnum) {
        [[maybe_unused]] auto src = static_cast<ModSource>(srcEnum);

        // What the controller would compute
        double srcNorm = static_cast<double>(srcEnum) /
                         static_cast<double>(Ruinae::kModSourceCount - 1);

        // What the processor would recover
        int recovered = std::clamp(
            static_cast<int>(srcNorm * (Ruinae::kModSourceCount - 1) + 0.5),
            0, Ruinae::kModSourceCount - 1);

        INFO("ModSource " << srcEnum << ": norm=" << srcNorm
             << ", recovered=" << recovered);
        REQUIRE(recovered == srcEnum);
    }
}

// =============================================================================
// DIAGNOSTIC: Print denormalized values for all destinations
// =============================================================================

TEST_CASE("Mod source pipeline: normalization round-trip for all destinations",
          "[mod-source-pipeline][unit][normalization]") {

    for (int dstIdx = 0; dstIdx < Ruinae::kModDestCount; ++dstIdx) {
        double dstNorm = static_cast<double>(dstIdx) /
                         static_cast<double>(Ruinae::kModDestCount - 1);

        int recovered = std::clamp(
            static_cast<int>(dstNorm * (Ruinae::kModDestCount - 1) + 0.5),
            0, Ruinae::kModDestCount - 1);

        INFO("Dest " << dstIdx << ": norm=" << dstNorm
             << ", recovered=" << recovered);
        REQUIRE(recovered == dstIdx);
    }
}

// =============================================================================
// DIAGNOSTIC: Inspect atomic values after handleModMatrixParamChange
// =============================================================================
// This test calls handleModMatrixParamChange directly with the normalized values
// the Controller would send, then reads back the atomic storage to verify.

TEST_CASE("Mod source pipeline: handleModMatrixParamChange stores correct values",
          "[mod-source-pipeline][unit][param-storage]") {

    Ruinae::ModMatrixParams params;

    // Simulate Controller setting slot 0: Macro1 → GlobalFilterCutoff, amount=+1.0
    double srcNorm = sourceToNormalized(ModSource::Macro1);  // 5/13
    double dstNorm = destToNormalized(0);                     // 0/7
    double amtNorm = amountToNormalized(1.0f);                // 1.0

    // These are the actual param IDs the controller would use
    Ruinae::handleModMatrixParamChange(params, Ruinae::kModMatrixSlot0SourceId, srcNorm);
    Ruinae::handleModMatrixParamChange(params, Ruinae::kModMatrixSlot0DestId, dstNorm);
    Ruinae::handleModMatrixParamChange(params, Ruinae::kModMatrixSlot0AmountId, amtNorm);

    int storedSrc = params.slots[0].source.load(std::memory_order_relaxed);
    int storedDst = params.slots[0].dest.load(std::memory_order_relaxed);
    float storedAmt = params.slots[0].amount.load(std::memory_order_relaxed);

    INFO("srcNorm = " << srcNorm);
    INFO("dstNorm = " << dstNorm);
    INFO("amtNorm = " << amtNorm);
    INFO("Stored source = " << storedSrc << " (expected " << static_cast<int>(ModSource::Macro1) << " = Macro1)");
    INFO("Stored dest = " << storedDst << " (expected 0 = GlobalFilterCutoff index)");
    INFO("Stored amount = " << storedAmt << " (expected 1.0)");

    REQUIRE(storedSrc == static_cast<int>(ModSource::Macro1));
    REQUIRE(storedDst == 0);
    REQUIRE(storedAmt == Approx(1.0f).margin(0.01f));

    // Now verify what the processor does with these values
    auto modSrc = static_cast<ModSource>(storedSrc);
    auto modDst = Ruinae::modDestFromIndex(storedDst);

    INFO("Reconstructed ModSource = " << static_cast<int>(modSrc)
         << " (Macro1=" << static_cast<int>(ModSource::Macro1) << ")");
    INFO("Reconstructed RuinaeModDest = " << static_cast<uint32_t>(modDst)
         << " (GlobalFilterCutoff=" << static_cast<uint32_t>(RuinaeModDest::GlobalFilterCutoff) << ")");

    REQUIRE(modSrc == ModSource::Macro1);
    REQUIRE(modDst == RuinaeModDest::GlobalFilterCutoff);
}

// =============================================================================
// DIAGNOSTIC: Test ALL sources through handleModMatrixParamChange
// =============================================================================

TEST_CASE("Mod source pipeline: all sources stored correctly via handleModMatrixParamChange",
          "[mod-source-pipeline][unit][param-storage]") {

    for (int srcEnum = 1; srcEnum < static_cast<int>(kModSourceCount); ++srcEnum) {
        auto expected = static_cast<ModSource>(srcEnum);
        DYNAMIC_SECTION("Source enum " << srcEnum) {
            Ruinae::ModMatrixParams params;

            double srcNorm = sourceToNormalized(expected);
            Ruinae::handleModMatrixParamChange(params, Ruinae::kModMatrixSlot0SourceId, srcNorm);

            int stored = params.slots[0].source.load(std::memory_order_relaxed);
            auto recovered = static_cast<ModSource>(stored);

            INFO("Enum=" << srcEnum << " norm=" << srcNorm << " stored=" << stored);
            // Print stderr for immediate console visibility
            fprintf(stderr, "  Source enum %d: norm=%.10f -> stored=%d (expected %d) %s\n",
                    srcEnum, srcNorm, stored, srcEnum,
                    (stored == srcEnum) ? "OK" : "MISMATCH!");

            REQUIRE(stored == srcEnum);
            REQUIRE(recovered == expected);
        }
    }
}
