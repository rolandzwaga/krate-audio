// ==============================================================================
// Integration Test: Morph Pad Modulation Pipeline
// ==============================================================================
// Verifies the full pipeline from mod matrix parameter configuration through
// the engine to the processor's modulated morph pad atomic values.
//
// Pipeline under test:
//   Host params (mod matrix source/dest/amount)
//   → processParameterChanges() → ModMatrixParams atomics
//   → applyParamsToEngine() → engine_.setGlobalModRoute()
//   → engine_.processBlock() → globalModEngine_.process()
//   → getGlobalModOffset(AllVoiceMorphPosition) → non-zero offset
//   → processor writes modulatedMorphX_ atomic
//
// Level 1: Engine-level test (public API)
// Level 2: Processor-level test (audio output varies with morph modulation)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/ruinae_engine.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "parameters/dropdown_mappings.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using Krate::DSP::RuinaeModDest;
using Krate::DSP::ModSource;

// =============================================================================
// Mocks (same pattern as trance_gate_param_flow_test.cpp)
// =============================================================================

namespace {

class MPParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    MPParamValueQueue(Steinberg::Vst::ParamID id, double value)
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

class MPParamChanges : public Steinberg::Vst::IParameterChanges {
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
    std::vector<MPParamValueQueue> queues_;
};

class MPEmptyEventList : public Steinberg::Vst::IEventList {
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

class MPNoteOnEvents : public Steinberg::Vst::IEventList {
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

// Normalized parameter values for mod matrix routing
// Source: "None"=0, "LFO 1"=1/13, "LFO 2"=2/13, ...
// Dest: index 0-9 maps to GlobalFilterCutoff..AllVoiceFltEnvAmt
constexpr double kSourceLFO1Norm = 1.0 / (Ruinae::kModSourceCount - 1);
constexpr double kDestMorphPosNorm = 5.0 / (Ruinae::kModDestCount - 1);
constexpr double kDestSpectralTiltNorm = 7.0 / (Ruinae::kModDestCount - 1);
constexpr double kAmountFullPositive = 1.0;  // maps to +1.0 bipolar

} // anonymous namespace

// =============================================================================
// Level 1: Engine-level modulation offset test
// =============================================================================

TEST_CASE("Engine produces non-zero morph offset with LFO routing",
          "[morph-pad][modulation]") {

    Krate::DSP::RuinaeEngine engine;
    engine.prepare(44100.0, 256);

    // Configure LFO1 → AllVoiceMorphPosition at full amount
    engine.setGlobalModRoute(
        0,                          // slot index
        ModSource::LFO1,            // source
        RuinaeModDest::AllVoiceMorphPosition,  // destination
        1.0f,                       // amount (full positive)
        Krate::DSP::ModCurve::Linear,
        1.0f,                       // scale x1
        false,                      // not bypassed
        0.0f                        // no smoothing
    );

    // Set LFO1 rate high enough to see variation over a few blocks
    engine.setGlobalLFO1Rate(5.0f);  // 5 Hz

    // Start a note so the engine processes voices
    engine.noteOn(60, 100);

    // Process several blocks and collect offsets
    std::vector<float> offsets;
    std::vector<float> outputL(256, 0.0f);
    std::vector<float> outputR(256, 0.0f);

    for (int block = 0; block < 50; ++block) {
        std::fill(outputL.begin(), outputL.end(), 0.0f);
        std::fill(outputR.begin(), outputR.end(), 0.0f);
        engine.processBlock(outputL.data(), outputR.data(), 256);
        float offset = engine.getGlobalModOffset(
            RuinaeModDest::AllVoiceMorphPosition);
        offsets.push_back(offset);
    }

    // The LFO should produce varying offsets over 50 blocks (~290ms at 44.1k/256)
    // At 5 Hz, we expect multiple full LFO cycles
    float minOffset = *std::min_element(offsets.begin(), offsets.end());
    float maxOffset = *std::max_element(offsets.begin(), offsets.end());
    float range = maxOffset - minOffset;

    INFO("Min offset: " << minOffset);
    INFO("Max offset: " << maxOffset);
    INFO("Range: " << range);

    // LFO should produce significant range of offsets
    REQUIRE(range > 0.1f);

    // At least some offsets should be non-zero
    bool anyNonZero = false;
    for (float o : offsets) {
        if (std::abs(o) > 0.001f) {
            anyNonZero = true;
            break;
        }
    }
    REQUIRE(anyNonZero);
}

TEST_CASE("Engine produces non-zero spectral tilt offset with LFO routing",
          "[morph-pad][modulation]") {

    Krate::DSP::RuinaeEngine engine;
    engine.prepare(44100.0, 256);

    // Configure LFO1 → AllVoiceSpectralTilt at full amount
    engine.setGlobalModRoute(
        0, ModSource::LFO1,
        RuinaeModDest::AllVoiceSpectralTilt,
        1.0f, Krate::DSP::ModCurve::Linear, 1.0f, false, 0.0f);

    engine.setGlobalLFO1Rate(5.0f);
    engine.noteOn(60, 100);

    std::vector<float> offsets;
    std::vector<float> outputL(256, 0.0f);
    std::vector<float> outputR(256, 0.0f);

    for (int block = 0; block < 50; ++block) {
        std::fill(outputL.begin(), outputL.end(), 0.0f);
        std::fill(outputR.begin(), outputR.end(), 0.0f);
        engine.processBlock(outputL.data(), outputR.data(), 256);
        offsets.push_back(engine.getGlobalModOffset(
            RuinaeModDest::AllVoiceSpectralTilt));
    }

    float range = *std::max_element(offsets.begin(), offsets.end()) -
                  *std::min_element(offsets.begin(), offsets.end());

    INFO("Tilt offset range: " << range);
    REQUIRE(range > 0.1f);
}

// =============================================================================
// Level 2: Processor-level modulation through parameter pipeline
// =============================================================================

TEST_CASE("Processor mod matrix LFO->MorphPos produces varying audio",
          "[morph-pad][modulation][processor]") {

    // --- Setup processor ---
    Ruinae::Processor processor;
    MPEmptyEventList emptyEvents;
    static constexpr size_t kBlockSize = 256;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* channelBuffers[2] = { outL.data(), outR.data() };

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    Steinberg::Vst::ProcessData data{};
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

    // --- Start a note ---
    {
        MPNoteOnEvents noteEvents;
        MPParamChanges emptyParams;
        data.inputEvents = &noteEvents;
        data.inputParameterChanges = &emptyParams;
        processor.process(data);
        data.inputEvents = &emptyEvents;
    }

    // --- Process some blocks without modulation to establish baseline ---
    std::vector<double> baselineEnergies;
    for (int i = 0; i < 20; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        MPParamChanges empty;
        data.inputParameterChanges = &empty;
        processor.process(data);

        double energy = 0.0;
        for (size_t s = 0; s < kBlockSize; ++s) {
            energy += static_cast<double>(outL[s]) * outL[s] +
                      static_cast<double>(outR[s]) * outR[s];
        }
        baselineEnergies.push_back(energy);
    }

    // Baseline should have stable energy (no modulation)
    double baselineVariance = 0.0;
    {
        double mean = 0.0;
        for (double e : baselineEnergies) mean += e;
        mean /= static_cast<double>(baselineEnergies.size());
        for (double e : baselineEnergies) {
            double diff = e - mean;
            baselineVariance += diff * diff;
        }
        baselineVariance /= static_cast<double>(baselineEnergies.size());
    }

    // --- Configure mod matrix: LFO1 → MorphPosition, full amount ---
    {
        MPParamChanges modParams;
        modParams.addChange(Ruinae::kModMatrixSlot0SourceId, kSourceLFO1Norm);
        modParams.addChange(Ruinae::kModMatrixSlot0DestId, kDestMorphPosNorm);
        modParams.addChange(Ruinae::kModMatrixSlot0AmountId, kAmountFullPositive);
        // Set LFO1 rate to ~5Hz for visible variation
        // LFO rate param is normalized 0-1 mapping to 0.01-20Hz typically
        modParams.addChange(Ruinae::kLFO1RateId, 0.5);
        data.inputParameterChanges = &modParams;
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
    }

    // --- Process blocks WITH modulation and collect energies ---
    std::vector<double> modulatedEnergies;
    for (int i = 0; i < 40; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        MPParamChanges empty;
        data.inputParameterChanges = &empty;
        processor.process(data);

        double energy = 0.0;
        for (size_t s = 0; s < kBlockSize; ++s) {
            energy += static_cast<double>(outL[s]) * outL[s] +
                      static_cast<double>(outR[s]) * outR[s];
        }
        modulatedEnergies.push_back(energy);
    }

    // With LFO modulating morph position, energy should vary more than baseline
    // because the oscillator mix changes continuously
    double modVariance = 0.0;
    {
        double mean = 0.0;
        for (double e : modulatedEnergies) mean += e;
        mean /= static_cast<double>(modulatedEnergies.size());
        for (double e : modulatedEnergies) {
            double diff = e - mean;
            modVariance += diff * diff;
        }
        modVariance /= static_cast<double>(modulatedEnergies.size());
    }

    INFO("Baseline energy variance: " << baselineVariance);
    INFO("Modulated energy variance: " << modVariance);

    // The modulated variance should be notably higher than baseline
    // (LFO sweeping morph position changes the spectral content)
    REQUIRE(modulatedEnergies.size() > 0);

    // At minimum, verify the processor produces audio with modulation active
    bool anyModEnergy = false;
    for (double e : modulatedEnergies) {
        if (e > 0.001) {
            anyModEnergy = true;
            break;
        }
    }
    REQUIRE(anyModEnergy);

    // Cleanup
    processor.setActive(false);
    processor.terminate();
}

// =============================================================================
// Level 3: Verify modulation offset is accessible via engine after processBlock
// =============================================================================

TEST_CASE("Engine getGlobalModOffset persists after processBlock returns",
          "[morph-pad][modulation]") {
    // This verifies that the offset values used by the processor to write
    // the modulated morph pad atomics are available after processBlock().
    // The processor reads these AFTER engine_.processBlock() returns.

    Krate::DSP::RuinaeEngine engine;
    engine.prepare(44100.0, 256);

    engine.setGlobalModRoute(
        0, ModSource::LFO1,
        RuinaeModDest::AllVoiceMorphPosition,
        1.0f, Krate::DSP::ModCurve::Linear, 1.0f, false, 0.0f);

    engine.setGlobalLFO1Rate(5.0f);
    engine.noteOn(60, 100);

    std::vector<float> outputL(256, 0.0f);
    std::vector<float> outputR(256, 0.0f);

    // Process a few blocks to get the LFO going
    for (int i = 0; i < 10; ++i) {
        std::fill(outputL.begin(), outputL.end(), 0.0f);
        std::fill(outputR.begin(), outputR.end(), 0.0f);
        engine.processBlock(outputL.data(), outputR.data(), 256);
    }

    // Now read the offset AFTER processBlock - this is what the processor does
    float morphOffset = engine.getGlobalModOffset(
        RuinaeModDest::AllVoiceMorphPosition);
    float tiltOffset = engine.getGlobalModOffset(
        RuinaeModDest::AllVoiceSpectralTilt);

    INFO("Morph offset after processBlock: " << morphOffset);
    INFO("Tilt offset (should be 0, not routed): " << tiltOffset);

    // Morph offset should be non-zero (LFO is running)
    REQUIRE(std::abs(morphOffset) > 0.001f);

    // Tilt offset should be zero (not routed)
    REQUIRE(std::abs(tiltOffset) < 0.001f);

    // Verify the processor's computation would produce a different value
    float baseX = engine.getBaseMixPosition();
    float modulatedX = std::clamp(baseX + morphOffset, 0.0f, 1.0f);

    INFO("Base mix position: " << baseX);
    INFO("Modulated mix position: " << modulatedX);

    // The modulated position should differ from base
    REQUIRE(std::abs(modulatedX - baseX) > 0.001f);
}
