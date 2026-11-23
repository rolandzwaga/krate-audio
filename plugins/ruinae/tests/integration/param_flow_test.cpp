// ==============================================================================
// Integration Test: Parameter Changes Flow from Host to Engine
// ==============================================================================
// Verifies that parameter changes flow through the Processor's parameter queue,
// are denormalized, stored in atomics, and applied to the engine.
//
// Reference: specs/045-plugin-shell/spec.md FR-005, FR-006, FR-007, US3
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "vst_param_changes.h"
#include "vst_event_list.h"

using Catch::Approx;

// =============================================================================
// Mock: Single Parameter Value Queue
// =============================================================================

// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using SingleParamValueQueue = Krate::Test::ParamValueQueue;
using TestParamChanges = Krate::Test::ParameterChanges;


// =============================================================================
// Mock: Parameter Changes Container
// =============================================================================



// =============================================================================
// Mock: Empty Event List
// =============================================================================

// IEventList mock consolidated into tests/test_helpers/vst_event_list.h
using EmptyEventList = Krate::Test::EventList;


// =============================================================================
// Test Fixture
// =============================================================================

struct ParamFlowFixture {
    Ruinae::Processor processor;
    EmptyEventList events;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channelBuffers[2];
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    static constexpr size_t kBlockSize = 256;

    ParamFlowFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
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
        data.inputEvents = &events;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);

        // Enable delay and reverb (effects chain defaults to disabled)
        TestParamChanges enableParams;
        enableParams.addChange(Ruinae::kDelayEnabledId, 1.0);
        enableParams.addChange(Ruinae::kReverbEnabledId, 1.0);
        processWithParams(enableParams);
    }

    ~ParamFlowFixture() {
        processor.setActive(false);
        processor.terminate();
    }

    void processWithParams(TestParamChanges& params) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &params;
        processor.process(data);
    }
};

// =============================================================================
// Tests
// =============================================================================

// -----------------------------------------------------------------------------
// Mono Portamento Time (R-121, R-031)
// -----------------------------------------------------------------------------
// applyArpParams overwrote the engine's portamento time with the arp Slide Time
// (60 ms default) every block, whatever the arp mode, so the Mono Portamento Time
// knob had no effect: mono always glided 60 ms.

namespace {

/// Render a sine oscillator in Mono, legato, arp Off, at 48 kHz: note 60 from
/// sample 0, note 72 from `secondAt` while 60 is still held. Returns the mono output.
std::vector<float> renderMonoGlide(double portamentoNorm, int64_t secondAt, int64_t total) {
    constexpr double kRate = 48000.0;
    constexpr size_t kBlock = 128;
    Ruinae::Processor processor;
    processor.initialize(nullptr);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kRate;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlock);
    processor.setupProcessing(setup);
    processor.setActive(static_cast<Steinberg::TBool>(true));

    std::vector<float> outL(kBlock);
    std::vector<float> outR(kBlock);
    std::vector<float> rendered;
    rendered.reserve(static_cast<size_t>(total));
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = channels;
    EmptyEventList events;
    EmptyEventList outEvents;
    TestParamChanges params;

    for (int64_t pos = 0; pos < total; pos += static_cast<int64_t>(kBlock)) {
        const auto n = static_cast<Steinberg::int32>(
            std::min<int64_t>(static_cast<int64_t>(kBlock), total - pos));
        events.clear();
        outEvents.clear();
        params.clear();
        if (pos == 0) {
            // Sine oscillator A only, filter open, no effects, arp off.
            params.addChange(Ruinae::kOscATypeId, 0.0);
            params.addChange(Ruinae::kOscAWaveformId, 0.0);
            params.addChange(Ruinae::kOscALevelId, 1.0);
            params.addChange(Ruinae::kOscATuneId, 0.5);
            params.addChange(Ruinae::kOscAFineId, 0.5);
            params.addChange(Ruinae::kOscBLevelId, 0.0);
            params.addChange(Ruinae::kMixerModeId, 0.0);
            params.addChange(Ruinae::kMixerPositionId, 0.0);
            params.addChange(Ruinae::kFilterTypeId, 0.0);
            params.addChange(Ruinae::kFilterCutoffId, 1.0);
            params.addChange(Ruinae::kFilterKeyTrackId, 0.0);
            params.addChange(Ruinae::kDistortionTypeId, 0.0);
            params.addChange(Ruinae::kTranceGateEnabledId, 0.0);
            params.addChange(Ruinae::kAmpEnvSustainId, 1.0);
            params.addChange(Ruinae::kDelayEnabledId, 0.0);
            params.addChange(Ruinae::kReverbEnabledId, 0.0);
            params.addChange(Ruinae::kHarmonizerEnabledId, 0.0);
            params.addChange(Ruinae::kGlobalFilterEnabledId, 0.0);
            params.addChange(Ruinae::kSettingsTuningReferenceId, 0.5);
            params.addChange(Ruinae::kArpOperatingModeId, 0.0);
            params.addChange(Ruinae::kVoiceModeId, 1.0);
            params.addChange(Ruinae::kMonoLegatoId, 1.0);
            params.addChange(Ruinae::kMonoPortaModeId, 0.0);
            params.addChange(Ruinae::kMonoPortamentoTimeId, portamentoNorm);
            events.addNoteOn(60, 0.8f, 0);
        }
        if (secondAt >= pos && secondAt < pos + n) {
            events.addNoteOn(72, 0.8f, static_cast<Steinberg::int32>(secondAt - pos));
        }
        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = n;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputParameterChanges = &params;
        data.inputEvents = &events;
        data.outputEvents = &outEvents;
        data.processContext = nullptr;
        processor.process(data);
        for (Steinberg::int32 i = 0; i < n; ++i) {
            rendered.push_back(0.5f * (outL[static_cast<size_t>(i)] + outR[static_cast<size_t>(i)]));
        }
    }
    processor.setActive(static_cast<Steinberg::TBool>(false));
    processor.terminate();
    return rendered;
}

/// Mean frequency over [a, b) from interpolated rising zero crossings (sine input).
double zeroCrossingHz(const std::vector<float>& x, size_t a, size_t b, double sampleRate) {
    double first = -1.0;
    double last = -1.0;
    int periods = -1;
    for (size_t i = std::max<size_t>(a, 1); i < std::min(b, x.size()); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = static_cast<double>(i - 1)
                + static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
            if (first < 0.0) first = t;
            last = t;
            ++periods;
        }
    }
    if (periods < 1) return 0.0;
    return sampleRate * static_cast<double>(periods) / (last - first);
}

/// MIDI note number (fractional) of a frequency, A4 = 440 Hz.
double hzToNote(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

}  // namespace

TEST_CASE("Mono portamento time controls the glide", "[integration][params][mono]") {
    constexpr double kRate = 48000.0;
    constexpr int64_t kSecondAt = 24000;  // 0.5 s: note 60 has settled

    SECTION("A: portamento 500 ms is still far from the target 100 ms after the second note") {
        const double norm = std::cbrt(500.0 / 5000.0);  // cubic mapping, 0-5000 ms
        const auto out = renderMonoGlide(norm, kSecondAt, kSecondAt + 28800);
        const double before = zeroCrossingHz(out, static_cast<size_t>(kSecondAt - 4800),
                                             static_cast<size_t>(kSecondAt), kRate);
        // 90-110 ms after the second note
        const double hz = zeroCrossingHz(out, static_cast<size_t>(kSecondAt + 4320),
                                         static_cast<size_t>(kSecondAt + 5280), kRate);
        // 550-570 ms after the second note: a 500 ms glide has ended
        const double hzEnd = zeroCrossingHz(out, static_cast<size_t>(kSecondAt + 26400),
                                            static_cast<size_t>(kSecondAt + 27360), kRate);
        REQUIRE(before > 0.0);  // not vacuous: the notes sound
        REQUIRE(hz > 0.0);
        REQUIRE(hzEnd > 0.0);
        const double noteBefore = hzToNote(before);
        const double note = hzToNote(hz);
        const double centsEnd = 100.0 * (hzToNote(hzEnd) - 72.0);
        INFO("note before: " << noteBefore << ", note 100 ms after the second note: " << note
             << ", cents from 72 after 560 ms: " << centsEnd);
        CHECK(std::abs(noteBefore - 60.0) <= 0.05);
        CHECK(note < 60.0 + 0.9 * 12.0);      // below 90 % of the way to 72
        // The glide is linear in pitch over the set time: 1/5 of the way at 100 ms
        CHECK(std::abs(note - (60.0 + 12.0 * 100.0 / 500.0)) <= 0.3);
        CHECK(std::abs(centsEnd) <= 5.0);     // and it ends on time
    }

    SECTION("B: portamento 0 reaches the target within 2 ms") {
        const auto out = renderMonoGlide(0.0, kSecondAt, kSecondAt + 9600);
        // 2-12 ms after the second note
        const double hz = zeroCrossingHz(out, static_cast<size_t>(kSecondAt + 96),
                                         static_cast<size_t>(kSecondAt + 576), kRate);
        REQUIRE(hz > 0.0);
        const double cents = 100.0 * (hzToNote(hz) - 72.0);
        INFO("cents from note 72, 2-12 ms after the second note: " << cents);
        CHECK(std::abs(cents) <= 5.0);
    }
}

TEST_CASE("Parameter changes affect subsequent audio blocks", "[param_flow][integration]") {
    ParamFlowFixture f;

    // First, play a note with default settings
    TestParamChanges emptyParams;

    // Create an event list with noteOn
    class NoteOnEvents : public Steinberg::Vst::IEventList {
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

    NoteOnEvents noteEvents;
    f.data.inputEvents = &noteEvents;
    f.data.inputParameterChanges = &emptyParams;
    f.processor.process(f.data);

    // Process enough blocks for the note to settle (the effects chain adds no latency)
    EmptyEventList noEvents;
    f.data.inputEvents = &noEvents;
    for (int i = 0; i < 30; ++i) {
        std::fill(f.outL.begin(), f.outL.end(), 0.0f);
        std::fill(f.outR.begin(), f.outR.end(), 0.0f);
        f.processor.process(f.data);
    }

    // Record peak before parameter change
    float peakBefore = 0.0f;
    std::fill(f.outL.begin(), f.outL.end(), 0.0f);
    std::fill(f.outR.begin(), f.outR.end(), 0.0f);
    f.processor.process(f.data);
    for (size_t i = 0; i < f.kBlockSize; ++i) {
        float absL = std::abs(f.outL[i]);
        if (absL > peakBefore) peakBefore = absL;
    }

    // Now set master gain to zero
    TestParamChanges gainChange;
    gainChange.addChange(Ruinae::kMasterGainId, 0.0);
    f.data.inputParameterChanges = &gainChange;
    std::fill(f.outL.begin(), f.outL.end(), 0.0f);
    std::fill(f.outR.begin(), f.outR.end(), 0.0f);
    f.processor.process(f.data);

    // Process enough blocks for gain=0 to take effect through the latency pipeline
    TestParamChanges noParamChanges;
    f.data.inputParameterChanges = &noParamChanges;
    float peakAfter = 0.0f;
    for (int i = 0; i < 30; ++i) {
        std::fill(f.outL.begin(), f.outL.end(), 0.0f);
        std::fill(f.outR.begin(), f.outR.end(), 0.0f);
        f.processor.process(f.data);
    }
    // The output after gain=0 should be much quieter
    for (size_t i = 0; i < f.kBlockSize; ++i) {
        float absL = std::abs(f.outL[i]);
        if (absL > peakAfter) peakAfter = absL;
    }

    // Audio was present before
    CHECK(peakBefore > 0.001f);
    // Audio should be silent or near-silent after gain=0
    CHECK(peakAfter < 0.001f);
}

// =============================================================================
// SC-002: Section param change produces measurable output difference
// =============================================================================
// For each major audio-path section, verifies that changing a representative
// parameter from its default actually alters the output waveform. This ensures
// applyParamsToEngine() forwards values to the engine for that section.
// =============================================================================

TEST_CASE("Section param changes produce measurable output difference", "[param_flow][sc002][integration]") {
    // --- Helper lambdas ---
    auto makeNoteOnEvents = []() {
        struct NoteOnList : public Steinberg::Vst::IEventList {
            Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
                return Steinberg::kNoInterface;
            }
            Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
            Steinberg::uint32 PLUGIN_API release() override { return 1; }
            Steinberg::int32 PLUGIN_API getEventCount() override { return done_ ? 0 : 1; }
            Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                                    Steinberg::Vst::Event& e) override {
                if (index != 0 || done_) return Steinberg::kResultFalse;
                e = {};
                e.type = Steinberg::Vst::Event::kNoteOnEvent;
                e.sampleOffset = 0;
                e.noteOn.channel = 0;
                e.noteOn.pitch = 60;
                e.noteOn.velocity = 0.8f;
                e.noteOn.noteId = -1;
                done_ = true;
                return Steinberg::kResultTrue;
            }
            Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override {
                return Steinberg::kResultTrue;
            }
            bool done_ = false;
        };
        return NoteOnList{};
    };

    // Compute peak and sum-of-squares for comparing output shape
    auto computeStats = [](const std::vector<float>& buf) -> std::pair<float, double> {
        float peak = 0.0f;
        double energy = 0.0;
        for (float s : buf) {
            float a = std::abs(s);
            if (a > peak) peak = a;
            energy += static_cast<double>(s) * s;
        }
        return {peak, energy};
    };

    // Collect output after a note has been playing for enough blocks to pass latency
    auto collectOutput = [&](ParamFlowFixture& fix, TestParamChanges* paramChange = nullptr) {
        // Process enough blocks for stabilization (the effects chain adds no latency)
        TestParamChanges empty;
        TestParamChanges& pc = paramChange ? *paramChange : empty;
        fix.data.inputParameterChanges = &pc;
        for (int i = 0; i < 30; ++i) {
            std::fill(fix.outL.begin(), fix.outL.end(), 0.0f);
            std::fill(fix.outR.begin(), fix.outR.end(), 0.0f);
            fix.processor.process(fix.data);
            // After first block, clear param changes (they've been consumed)
            if (i == 0) fix.data.inputParameterChanges = &empty;
        }
        // Capture one final block
        std::fill(fix.outL.begin(), fix.outL.end(), 0.0f);
        std::fill(fix.outR.begin(), fix.outR.end(), 0.0f);
        fix.processor.process(fix.data);
        return computeStats(fix.outL);
    };

    struct SectionTest {
        const char* name;
        Steinberg::Vst::ParamID paramId;
        double value;
    };

    // Representative parameter per section that directly affects the audio path.
    // Each uses a value that deviates significantly from the default.
    SectionTest sections[] = {
        {"OSC A Type",       Ruinae::kOscATypeId,         0.9},    // Noise vs PolyBLEP
        {"OSC A Level",      Ruinae::kOscALevelId,        0.0},    // Silence vs full
        {"Filter Cutoff",    Ruinae::kFilterCutoffId,     0.0},    // 20 Hz vs default
        {"Distortion Type",  Ruinae::kDistortionTypeId,   0.5},    // Non-clean type vs Clean
        {"Delay Mix",        Ruinae::kDelayMixId,         1.0},    // Full wet vs dry
        {"Reverb Mix",       Ruinae::kReverbMixId,        1.0},    // Full wet vs dry
    };

    for (const auto& sec : sections) {
        SECTION(sec.name) {
            // Create fresh fixture each section (independent processor state)
            ParamFlowFixture f;

            // Start a note
            auto noteEvents = makeNoteOnEvents();
            f.data.inputEvents = &noteEvents;
            TestParamChanges emptyPC;
            f.data.inputParameterChanges = &emptyPC;
            f.processor.process(f.data);

            // Switch to no events
            EmptyEventList noEvents;
            f.data.inputEvents = &noEvents;

            // Collect baseline output (default params)
            auto [peakDefault, energyDefault] = collectOutput(f);

            // Now apply the section's parameter change
            TestParamChanges sectionChange;
            sectionChange.addChange(sec.paramId, sec.value);
            auto [peakChanged, energyChanged] = collectOutput(f, &sectionChange);

            // At least one of peak or energy must differ by >5%
            bool peakDiffers = std::abs(peakDefault - peakChanged) > 0.05f * std::max(peakDefault, peakChanged);
            bool energyDiffers = std::abs(energyDefault - energyChanged) > 0.05 * std::max(energyDefault, energyChanged);

            INFO("Section: " << sec.name);
            INFO("Peak default: " << peakDefault << ", changed: " << peakChanged);
            INFO("Energy default: " << energyDefault << ", changed: " << energyChanged);
            CHECK((peakDiffers || energyDiffers));
        }
    }
}
