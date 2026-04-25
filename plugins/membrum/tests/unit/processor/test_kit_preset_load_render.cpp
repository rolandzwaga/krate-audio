// End-to-end test that simulates the kit-preset-load flow:
//   1. Construct a fresh Processor.
//   2. Send per-pad parameter changes through the host's
//      `IParameterChanges` interface (mirrors what kitPresetLoadProvider
//      emits via setAndNotify -> performEdit -> host -> processor).
//   3. Trigger noteOn for two different pads.
//   4. Verify the rendered audio differs.
//
// If this test fails, the bug is in the kit-preset-load -> processor
// dispatch path, not anywhere downstream.
#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"

#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int kBlockSize = 256;
constexpr double kSampleRate = 48000.0;

class SingleParamQueue : public IParamValueQueue
{
public:
    SingleParamQueue(ParamID id, ParamValue value) : id_(id), value_(value) {}
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32, int32& sampleOffset, ParamValue& value) override
    {
        sampleOffset = 0;
        value = value_;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
private:
    ParamID id_;
    ParamValue value_;
};

class MultiParamChanges : public IParameterChanges
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override { return static_cast<int32>(queues_.size()); }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
    {
        if (index < 0 || index >= static_cast<int32>(queues_.size())) return nullptr;
        return queues_[static_cast<size_t>(index)].get();
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
    void add(ParamID id, ParamValue value)
    {
        queues_.push_back(std::make_unique<SingleParamQueue>(id, value));
    }
private:
    std::vector<std::unique_ptr<SingleParamQueue>> queues_;
};

class NoteEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size())) return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override
    {
        events_.push_back(e);
        return kResultOk;
    }
    void noteOn(int16 midi, float velocity)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = 0;
        e.noteOn.pitch = midi;
        e.noteOn.velocity = velocity;
        e.noteOn.channel = 0;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }
private:
    std::vector<Event> events_;
};

ProcessSetup makeSetup(double sr, int bs)
{
    ProcessSetup s{};
    s.processMode = kRealtime;
    s.symbolicSampleSize = kSample32;
    s.maxSamplesPerBlock = bs;
    s.sampleRate = sr;
    return s;
}

struct Fixture
{
    Membrum::Processor processor;
    NoteEventList events;
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};
    float* outChans[2];
    AudioBusBuffers outBus{};
    ProcessData data{};

    Fixture()
    {
        outChans[0] = outL.data();
        outChans[1] = outR.data();
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outChans;
        outBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &events;
        data.outputEvents = nullptr;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        auto setup = makeSetup(kSampleRate, kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~Fixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void clearBuffers()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
    }

    void processBlock()
    {
        clearBuffers();
        processor.process(data);
    }

    void sendParams(const std::vector<std::pair<ParamID, ParamValue>>& params)
    {
        MultiParamChanges changes;
        for (const auto& p : params) changes.add(p.first, p.second);
        data.inputParameterChanges = &changes;
        events.clear();
        clearBuffers();
        processor.process(data);
        data.inputParameterChanges = nullptr;
    }
};

double rms(const std::vector<float>& s)
{
    double e = 0.0;
    for (float x : s) e += static_cast<double>(x) * x;
    return std::sqrt(e / std::max<size_t>(1, s.size()));
}

} // namespace

TEST_CASE("Kit-preset load: per-pad params dispatched via processParameterChanges "
          "produce distinct audio per pad",
          "[processor][kit_preset][phase8f]")
{
    Fixture fix;

    const int pad0 = 0;
    const int pad5 = 5;
    fix.sendParams({
        // Pad 0: big body, low pitch envelope.
        {static_cast<ParamID>(Membrum::padParamId(pad0, Membrum::kPadSize)),       0.85},
        {static_cast<ParamID>(Membrum::padParamId(pad0, Membrum::kPadMaterial)),   0.20},
        {static_cast<ParamID>(Membrum::padParamId(pad0, Membrum::kPadDecay)),      0.55},
        {static_cast<ParamID>(Membrum::padParamId(pad0, Membrum::kPadLevel)),      0.85},
        {static_cast<ParamID>(Membrum::padParamId(pad0, Membrum::kPadTSPitchEnvStart)), 0.5},
        {static_cast<ParamID>(Membrum::padParamId(pad0, Membrum::kPadTSPitchEnvEnd)),   0.2},
        {static_cast<ParamID>(Membrum::padParamId(pad0, Membrum::kPadTSPitchEnvTime)),  0.3},
        {static_cast<ParamID>(Membrum::padParamId(pad0, Membrum::kPadEnabled)),    1.0},
        // Pad 5: small body, high pitch envelope.
        {static_cast<ParamID>(Membrum::padParamId(pad5, Membrum::kPadSize)),       0.40},
        {static_cast<ParamID>(Membrum::padParamId(pad5, Membrum::kPadMaterial)),   0.80},
        {static_cast<ParamID>(Membrum::padParamId(pad5, Membrum::kPadDecay)),      0.30},
        {static_cast<ParamID>(Membrum::padParamId(pad5, Membrum::kPadLevel)),      0.85},
        {static_cast<ParamID>(Membrum::padParamId(pad5, Membrum::kPadTSPitchEnvStart)), 0.85},
        {static_cast<ParamID>(Membrum::padParamId(pad5, Membrum::kPadTSPitchEnvEnd)),   0.55},
        {static_cast<ParamID>(Membrum::padParamId(pad5, Membrum::kPadTSPitchEnvTime)),  0.3},
        {static_cast<ParamID>(Membrum::padParamId(pad5, Membrum::kPadEnabled)),    1.0},
    });

    // Confirm the parameter dispatch actually wrote to padConfigs_.
    const auto& cfgPad0 = fix.processor.voicePoolForTest().padConfig(pad0);
    const auto& cfgPad5 = fix.processor.voicePoolForTest().padConfig(pad5);
    INFO("pad0 size=" << cfgPad0.size << " pitchEnvStart=" << cfgPad0.tsPitchEnvStart);
    INFO("pad5 size=" << cfgPad5.size << " pitchEnvStart=" << cfgPad5.tsPitchEnvStart);
    CHECK(std::abs(cfgPad0.size - 0.85f) < 0.01f);
    CHECK(std::abs(cfgPad5.size - 0.40f) < 0.01f);
    CHECK(std::abs(cfgPad0.tsPitchEnvStart - 0.5f) < 0.01f);
    CHECK(std::abs(cfgPad5.tsPitchEnvStart - 0.85f) < 0.01f);

    auto renderPad = [&](int16 midi) {
        fix.events.clear();
        fix.events.noteOn(midi, 1.0f);
        std::vector<float> samples;
        for (int b = 0; b < 30; ++b) {
            fix.processBlock();
            for (int s = 0; s < kBlockSize; ++s) samples.push_back(fix.outL[s]);
        }
        fix.events.clear();
        return samples;
    };

    const auto pad0Audio = renderPad(36);
    const auto pad5Audio = renderPad(41);

    REQUIRE(pad0Audio.size() == pad5Audio.size());
    double maxAbsDiff = 0.0;
    for (size_t i = 0; i < pad0Audio.size(); ++i) {
        const double d = std::abs(static_cast<double>(pad0Audio[i])
                                   - static_cast<double>(pad5Audio[i]));
        if (d > maxAbsDiff) maxAbsDiff = d;
    }
    INFO("pad0 RMS=" << rms(pad0Audio) << " pad5 RMS=" << rms(pad5Audio)
         << " max sample diff=" << maxAbsDiff);
    CHECK(maxAbsDiff > 1e-3);
}

// Verifies the kit-preset-load chain (controller -> performEdit -> host ->
// processParameterChanges -> setPadConfigField -> applyPadConfigToSlot ->
// DrumVoice.noteOn -> updateMembraneFundamental -> ModalResonatorBank) by
// inspecting the bank's installed mode frequencies after a noteOn for two
// pads with very different pitch-envelope targets. The bank is the closest
// observable point to the rendered audio; if the modes differ here, the
// per-pad pitch env propagation is verified end-to-end.
//
// Audio-level pitch estimation was attempted earlier but proved unreliable
// because the modal bank's internal softclip safety limiter
// (kSoftClipThreshold = 0.707) saturates the summed-mode signal into a
// quasi-square-wave that masks the per-mode frequencies in zero-crossing
// pitch detection. The bank-level check is the correct invariant.
TEST_CASE("Kit-preset load: per-pad pitch envelope reaches the modal bank "
          "with distinct frequencies per pad",
          "[processor][kit_preset][phase8f][pitch]")
{
    Fixture fix;

    const int padLow  = 5;   // MIDI 41 -- low tom
    const int padHigh = 14;  // MIDI 50 -- high tom
    fix.sendParams({
        // Low tom: large body, low pitch envelope target ~80 Hz.
        {static_cast<ParamID>(Membrum::padParamId(padLow, Membrum::kPadSize)),    0.85},
        {static_cast<ParamID>(Membrum::padParamId(padLow, Membrum::kPadMaterial)), 0.20},
        {static_cast<ParamID>(Membrum::padParamId(padLow, Membrum::kPadDecay)),   0.55},
        {static_cast<ParamID>(Membrum::padParamId(padLow, Membrum::kPadLevel)),   0.85},
        {static_cast<ParamID>(Membrum::padParamId(padLow, Membrum::kPadTSPitchEnvStart)), 0.30}, // ~80 Hz
        {static_cast<ParamID>(Membrum::padParamId(padLow, Membrum::kPadTSPitchEnvEnd)),   0.30},
        {static_cast<ParamID>(Membrum::padParamId(padLow, Membrum::kPadTSPitchEnvTime)),  0.05},
        {static_cast<ParamID>(Membrum::padParamId(padLow, Membrum::kPadEnabled)), 1.0},
        // High tom: small body, much higher pitch envelope target ~400 Hz.
        {static_cast<ParamID>(Membrum::padParamId(padHigh, Membrum::kPadSize)),    0.40},
        {static_cast<ParamID>(Membrum::padParamId(padHigh, Membrum::kPadMaterial)), 0.50},
        {static_cast<ParamID>(Membrum::padParamId(padHigh, Membrum::kPadDecay)),   0.30},
        {static_cast<ParamID>(Membrum::padParamId(padHigh, Membrum::kPadLevel)),   0.85},
        {static_cast<ParamID>(Membrum::padParamId(padHigh, Membrum::kPadTSPitchEnvStart)), 0.65}, // ~400 Hz
        {static_cast<ParamID>(Membrum::padParamId(padHigh, Membrum::kPadTSPitchEnvEnd)),   0.65},
        {static_cast<ParamID>(Membrum::padParamId(padHigh, Membrum::kPadTSPitchEnvTime)),  0.05},
        {static_cast<ParamID>(Membrum::padParamId(padHigh, Membrum::kPadEnabled)), 1.0},
    });

    // Sanity: confirm both pads' cfg actually got the values we sent.
    const auto& cfgLow  = fix.processor.voicePoolForTest().padConfig(padLow);
    const auto& cfgHigh = fix.processor.voicePoolForTest().padConfig(padHigh);
    REQUIRE(std::abs(cfgLow.size  - 0.85f) < 0.01f);
    REQUIRE(std::abs(cfgHigh.size - 0.40f) < 0.01f);
    REQUIRE(std::abs(cfgLow.tsPitchEnvStart  - 0.30f) < 0.01f);
    REQUIRE(std::abs(cfgHigh.tsPitchEnvStart - 0.65f) < 0.01f);

    // Read back the bank's mode 0 frequency for one pad. Triggers a noteOn,
    // runs one block (configures the bank), and inspects the active voice.
    auto bankMode0Hz = [&](int16 midi) -> float {
        // Drain any prior voice tails so they don't bleed in.
        for (int n = 36; n <= 67; ++n) {
            fix.events.clear();
            fix.events.noteOn(static_cast<int16>(n), 0.0f);
            fix.processBlock();
        }
        fix.events.clear();
        for (int b = 0; b < 50; ++b) fix.processBlock();
        fix.events.clear();
        fix.events.noteOn(midi, 1.0f);
        fix.processBlock();
        auto& vp = fix.processor.voicePoolForTest();
        float result = -1.0f;
        vp.forEachMainVoice([&](Membrum::DrumVoice& v) {
            if (result >= 0.0f) return;
            const auto& bank = v.bodyBank().getSharedBank();
            if (bank.getNumActiveModes() == 0) return;
            result = bank.getModeFrequency(0);
        });
        return result;
    };

    const float lowMode0  = bankMode0Hz(41);
    const float highMode0 = bankMode0Hz(50);
    INFO("Low pad mode0=" << lowMode0 << " Hz, high pad mode0=" << highMode0 << " Hz");

    // ~80 Hz vs ~400 Hz; allow modest slack for log-scale denormalisation.
    CHECK(std::abs(lowMode0  -  80.0f) <  5.0f);
    CHECK(std::abs(highMode0 - 400.0f) < 10.0f);
    CHECK(highMode0 > lowMode0 * 4.0f);
}
