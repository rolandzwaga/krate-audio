#pragma once

// ==============================================================================
// Profundum - shared processor test fixture (Phase 10 T006, plan 4.2)
// ==============================================================================
// Defines ONLY what the shared mocks lack (FR-063):
//   - Krate::Test::EventList (tests/test_helpers/vst_event_list.h) is reused for
//     every event list;
//   - Krate::Test::ParameterChanges / ParamValueQueue (tests/test_helpers/
//     vst_param_changes.h) are reused for every single-point, offset-0 change;
//   - MultiPointParamValueQueue / MultiParamChanges carry several automation
//     points at arbitrary offsets (FR-043 "last point wins"), which the shared
//     single-point ParameterChanges cannot.
//
// FR-064: the Processor and every ProfundumEngine live on the heap
// (unique_ptr), never as stack locals.
//
// NAMESPACE HAZARD: Krate::DSP::ProfundumTest exists in the dsp tests. The test
// namespace here is ProfundumPluginTest, plugin types are always spelled
// ::Profundum::..., and no TU may write `using namespace Krate::DSP::ProfundumTest`.
//
// ALLOCATION BEHAVIOUR: processBlock() builds ProcessData / AudioBusBuffers on the
// stack and writes into buffers sized once by the ctor / prepare(). It appends to
// capturedL / capturedR but NEVER grows them: call reserveCapture() first
// (renderScript() reserves for itself). The event list and the MultiParamChanges
// containers keep their capacity across clear(), so they are allocation-free once
// warm.
//
// PORTABILITY (plan 4.5): never std::isnan / std::isinf / std::isfinite; non-finite
// values come from bit patterns through a volatile (FR-062).
// ==============================================================================

#include "engine/profundum_engine_config.h"
#include "processor/processor.h"

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace ProfundumPluginTest {

// -----------------------------------------------------------------------------
// Note sets (plan 4.2)
// -----------------------------------------------------------------------------
inline constexpr Steinberg::int16 kE1 = 28;
// E1 A1 E2 C3: the Phase 9 POLY4 set (profundum_engine_perf_test.cpp:118).
inline constexpr std::array<Steinberg::int16, 4> kChord4{28, 33, 40, 48};
// SC-006.
inline constexpr std::array<Steinberg::int16, 8> kChord8{28, 33, 40, 45, 52, 57, 64, 69};

// -----------------------------------------------------------------------------
// MultiPointParamValueQueue - a parameter queue carrying an arbitrary number of
// automation points at arbitrary offsets, reported in insertion order.
// (model: plugins/vorago/tests/vorago_test_fixture.h:60)
// -----------------------------------------------------------------------------
class MultiPointParamValueQueue final : public Steinberg::Vst::IParamValueQueue {
public:
    explicit MultiPointParamValueQueue(Steinberg::Vst::ParamID id) : id_(id) {}

    // Reuse the object (and its point storage) for another parameter.
    void reset(Steinberg::Vst::ParamID id) {
        id_ = id;
        points_.clear();  // keeps capacity
    }

    void addTestPoint(Steinberg::int32 offset, double value) {
        points_.push_back(Point{offset, value});
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }

    Steinberg::int32 PLUGIN_API getPointCount() override {
        return static_cast<Steinberg::int32>(points_.size());
    }

    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index,
                                           Steinberg::int32& sampleOffset,
                                           Steinberg::Vst::ParamValue& value) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(points_.size())) {
            return Steinberg::kResultFalse;
        }
        const Point& p = points_[static_cast<std::size_t>(index)];
        sampleOffset = p.offset;
        value = p.value;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 /*sampleOffset*/,
                                           Steinberg::Vst::ParamValue /*value*/,
                                           Steinberg::int32& /*index*/) override {
        return Steinberg::kResultFalse;
    }

private:
    struct Point {
        Steinberg::int32 offset;
        double value;
    };

    Steinberg::Vst::ParamID id_;
    std::vector<Point> points_;
};

// -----------------------------------------------------------------------------
// MultiParamChanges - IParameterChanges over multi-point queues.
// (model: plugins/vorago/tests/vorago_test_fixture.h:123)
//
// clear() does NOT destroy the queues: it drops the active count so the next
// block reuses the same objects and their point storage. The reference returned
// by addQueue() / queueFor() is invalidated by a later call that grows the
// vector: call reserve() up front, or finish filling one queue before adding
// the next.
// -----------------------------------------------------------------------------
class MultiParamChanges final : public Steinberg::Vst::IParameterChanges {
public:
    void clear() noexcept { activeCount_ = 0; }

    void reserve(std::size_t n) { queues_.reserve(n); }

    MultiPointParamValueQueue& addQueue(Steinberg::Vst::ParamID id) {
        if (activeCount_ == queues_.size()) {
            queues_.emplace_back(id);  // grows only until warm
        }
        MultiPointParamValueQueue& q = queues_[activeCount_];
        ++activeCount_;
        q.reset(id);
        return q;
    }

    // The active queue for @p id, or a new one (one queue per ID per block, as a
    // host delivers it).
    MultiPointParamValueQueue& queueFor(Steinberg::Vst::ParamID id) {
        for (std::size_t i = 0; i < activeCount_; ++i) {
            if (queues_[i].getParameterId() == id) {
                return queues_[i];
            }
        }
        return addQueue(id);
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(activeCount_);
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(activeCount_)) {
            return nullptr;
        }
        return &queues_[static_cast<std::size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID& /*id*/, Steinberg::int32& /*index*/) override {
        return nullptr;
    }

private:
    std::vector<MultiPointParamValueQueue> queues_;
    std::size_t activeCount_ = 0;
};

// -----------------------------------------------------------------------------
// CountingConnectionPoint - counts the IMessages sent to it (SC-014).
// -----------------------------------------------------------------------------
class CountingConnectionPoint final : public Steinberg::Vst::IConnectionPoint {
public:
    int notifications = 0;

    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* /*other*/) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* /*other*/) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* /*message*/) override {
        ++notifications;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

// -----------------------------------------------------------------------------
// ProcessorFixture (model: plugins/vorago/tests/vorago_test_fixture.h:191)
// -----------------------------------------------------------------------------
struct ProcessorFixture {
    // Guard words either side of every output channel buffer. A processor that
    // writes outside [0, numSamples) fails the REQUIRE in processBlock().
    static constexpr std::size_t kGuardWords = 8;
    static constexpr float kGuardValue = -8.5e17f;  // finite, never a legal sample
    static constexpr std::size_t kDefaultBufferSamples = 4096;

    std::unique_ptr<::Profundum::Processor> proc = std::make_unique<::Profundum::Processor>();

    // processBlock() APPENDS each rendered block here. Never grown by the
    // fixture: reserveCapture() first.
    std::vector<float> capturedL, capturedR;

    // silenceFlags of the output bus after the last processBlock() call.
    Steinberg::uint64 lastSilenceFlags = 0;

    struct ScriptedEvent {
        enum class Kind { NoteOn, NoteOff, Param };

        Steinberg::int64 sample = 0;  // absolute sample position in the render
        Kind kind = Kind::NoteOn;
        Steinberg::int16 pitch = 0;
        float velocity = 0.0f;        // normalized [0, 1] (NoteOn)
        Steinberg::Vst::ParamID id = 0;
        double value = 0.0;           // normalized (Param)
    };

    // Initialised, NOT prepared. The buffers are sized for 4096 samples so
    // processBlock() is usable on an unprepared processor (FR-030 cases).
    explicit ProcessorFixture(Steinberg::FUnknown* hostContext = nullptr) {
        REQUIRE(proc->initialize(hostContext) == Steinberg::kResultOk);
        sizeBuffers(kDefaultBufferSamples);
    }

    // setupProcessing({kRealtime, kSample32, maxBlock, sr}) then setActive(true).
    // Returns the setupProcessing result, so a refused setup can be asserted by
    // the caller. Sizes both channel buffers to max(maxBlock, 4096) + guards.
    Steinberg::tresult prepare(double sr = 48000.0, Steinberg::int32 maxBlock = 4096) {
        REQUIRE(maxBlock > 0);
        sizeBuffers(std::max(static_cast<std::size_t>(maxBlock), kDefaultBufferSamples));

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.maxSamplesPerBlock = maxBlock;
        setup.sampleRate = sr;
        const Steinberg::tresult r = proc->setupProcessing(setup);
        proc->setActive(true);
        return r;
    }

    // One stereo process() call of n samples. ProcessData / AudioBusBuffers live
    // on the stack; guard words are rewritten before and REQUIREd intact after.
    Steinberg::tresult processBlock(std::size_t n, Steinberg::Vst::IEventList* ev = nullptr,
                                    Steinberg::Vst::IParameterChanges* pc = nullptr,
                                    Steinberg::Vst::ProcessContext* ctx = nullptr) {
        REQUIRE(n <= bufferSamples_);
        REQUIRE(capturedL.size() + n <= capturedL.capacity());
        REQUIRE(capturedR.size() + n <= capturedR.capacity());

        writeGuards(storageL_);
        writeGuards(storageR_);

        std::array<float*, 2> channels{audioL(), audioR()};

        Steinberg::Vst::AudioBusBuffers outBus{};
        outBus.numChannels = 2;
        outBus.silenceFlags = 0;
        outBus.channelBuffers32 = channels.data();

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(n);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.inputParameterChanges = pc;
        data.outputParameterChanges = nullptr;
        data.inputEvents = ev;
        data.outputEvents = nullptr;
        data.processContext = ctx;

        const Steinberg::tresult r = proc->process(data);
        lastSilenceFlags = outBus.silenceFlags;

        REQUIRE(guardsIntact(storageL_));
        REQUIRE(guardsIntact(storageR_));

        capturedL.insert(capturedL.end(), audioL(), audioL() + n);
        capturedR.insert(capturedR.end(), audioR(), audioR() + n);
        return r;
    }

    // A parameter-only call: numOutputs = 0, numSamples = 0.
    Steinberg::tresult processNoOutputs(Steinberg::Vst::IParameterChanges* pc) {
        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = 0;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 0;
        data.outputs = nullptr;
        data.inputParameterChanges = pc;
        data.outputParameterChanges = nullptr;
        data.inputEvents = nullptr;
        data.outputEvents = nullptr;
        data.processContext = nullptr;
        return proc->process(data);
    }

    void reserveCapture(std::size_t totalSamples) {
        capturedL.reserve(totalSamples);
        capturedR.reserve(totalSamples);
    }

    // Renders totalSamples in host blocks of hostBlock samples (the final block
    // is truncated to fit). Each scripted event lands in the block containing
    // its sample, at the in-block offset sample - blockStart; events of one
    // block keep their script order. Param events become points of one queue
    // per ID per block. Reserves the capture itself before rendering.
    void renderScript(std::span<const ScriptedEvent> script, std::size_t totalSamples,
                      std::size_t hostBlock) {
        REQUIRE(hostBlock > 0u);
        reserveCapture(capturedL.size() + totalSamples);

        std::size_t start = 0;
        while (start < totalSamples) {
            const std::size_t n = std::min(hostBlock, totalSamples - start);

            scriptEvents_.clear();
            scriptParams_.clear();
            for (const ScriptedEvent& s : script) {
                if (s.sample < 0) {
                    continue;
                }
                const auto at = static_cast<std::size_t>(s.sample);
                if (at < start || at >= start + n) {
                    continue;
                }
                const auto offset = static_cast<Steinberg::int32>(at - start);
                switch (s.kind) {
                    case ScriptedEvent::Kind::NoteOn:
                        scriptEvents_.addNoteOn(s.pitch, s.velocity, offset);
                        break;
                    case ScriptedEvent::Kind::NoteOff:
                        scriptEvents_.addNoteOff(s.pitch, offset);
                        break;
                    case ScriptedEvent::Kind::Param:
                        scriptParams_.queueFor(s.id).addTestPoint(offset, s.value);
                        break;
                }
            }

            REQUIRE(processBlock(n, &scriptEvents_, &scriptParams_) == Steinberg::kResultOk);
            start += n;
        }
        scriptEvents_.clear();
        scriptParams_.clear();
    }

    [[nodiscard]] float* audioL() noexcept { return storageL_.data() + kGuardWords; }
    [[nodiscard]] float* audioR() noexcept { return storageR_.data() + kGuardWords; }

    // Sizes both channel buffers to samples + 2 * kGuardWords and writes the guards.
    void sizeBuffers(std::size_t samples) {
        bufferSamples_ = samples;
        const std::size_t total = samples + 2u * kGuardWords;
        storageL_.assign(total, 0.0f);
        storageR_.assign(total, 0.0f);
        writeGuards(storageL_);
        writeGuards(storageR_);
    }

    static void writeGuards(std::vector<float>& v) noexcept {
        for (std::size_t i = 0; i < kGuardWords; ++i) {
            v[i] = kGuardValue;
            v[v.size() - 1u - i] = kGuardValue;
        }
    }

    [[nodiscard]] static bool guardsIntact(const std::vector<float>& v) noexcept {
        if (v.size() < 2u * kGuardWords) {
            return false;
        }
        for (std::size_t i = 0; i < kGuardWords; ++i) {
            if (v[i] != kGuardValue || v[v.size() - 1u - i] != kGuardValue) {
                return false;
            }
        }
        return true;
    }

private:
    std::vector<float> storageL_, storageR_;  // guard | audio | guard
    std::size_t bufferSamples_ = 0;
    Krate::Test::EventList scriptEvents_;     // renderScript's per-block list, reused
    MultiParamChanges scriptParams_;          // renderScript's per-block changes, reused
};

// -----------------------------------------------------------------------------
// DirectEngine - the reference arm (SC-008, SC-015, SC-016.4, the P-5 probe):
// a bare ProfundumEngine prepared with ProfundumEngineConfig{} plus the output
// gain smoother the processor applies after the engine.
// -----------------------------------------------------------------------------
struct DirectEvent {
    enum class Kind { NoteOn, NoteOff, VoiceMode, Polyphony };

    std::size_t sample = 0;  // absolute sample position in the render
    Kind kind = Kind::NoteOn;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;  // MIDI 1..127 (NoteOn)
    Krate::DSP::ProfundumEngine::VoiceMode mode = Krate::DSP::ProfundumEngine::VoiceMode::Mono;
    std::size_t polyphony = Krate::DSP::ProfundumEngine::kDefaultPolyphony;
};

struct DirectEngine {
    std::unique_ptr<Krate::DSP::ProfundumEngine> e =
        std::make_unique<Krate::DSP::ProfundumEngine>();
    Krate::DSP::OnePoleSmoother g{1.0f};
    std::vector<float> outL, outR;

    explicit DirectEngine(double sampleRate = 48000.0) {
        e->prepare(sampleRate, Krate::DSP::ProfundumEngineConfig{});
        g.configure(::Profundum::kOutputGainSmoothMs, static_cast<float>(sampleRate));
    }

    // Overwrites outL / outR with totalSamples of output. The render is split
    // ONLY at event samples; every event at a sample is applied (in script
    // order) before that sample is rendered. Events at or after totalSamples are
    // ignored. The script must be sorted by sample (non-decreasing).
    void render(std::span<const DirectEvent> script, std::size_t totalSamples) {
        outL.assign(totalSamples, 0.0f);
        outR.assign(totalSamples, 0.0f);

        std::size_t idx = 0;
        std::size_t pos = 0;
        while (pos < totalSamples) {
            while (idx < script.size() && script[idx].sample <= pos) {
                REQUIRE((idx == 0 || script[idx - 1].sample <= script[idx].sample));
                apply(script[idx]);
                ++idx;
            }
            const std::size_t next =
                idx < script.size() ? std::min(script[idx].sample, totalSamples) : totalSamples;
            e->processBlock(outL.data() + pos, outR.data() + pos, next - pos);
            for (std::size_t i = pos; i < next; ++i) {
                const float gain = g.process();
                outL[i] *= gain;
                outR[i] *= gain;
            }
            pos = next;
        }
    }

private:
    void apply(const DirectEvent& ev) noexcept {
        switch (ev.kind) {
            case DirectEvent::Kind::NoteOn:
                e->noteOn(ev.note, ev.velocity);
                break;
            case DirectEvent::Kind::NoteOff:
                e->noteOff(ev.note);
                break;
            case DirectEvent::Kind::VoiceMode:
                e->setVoiceMode(ev.mode);
                break;
            case DirectEvent::Kind::Polyphony:
                e->setPolyphony(ev.polyphony);
                break;
        }
    }
};

// -----------------------------------------------------------------------------
// Helpers. `to == kToEnd` means "to the end of the span(s)".
// -----------------------------------------------------------------------------
inline constexpr std::size_t kToEnd = std::numeric_limits<std::size_t>::max();

// Exponent test on the bit pattern: fast-math immune (never std::isfinite).
[[nodiscard]] inline bool isFiniteBits(float x) noexcept {
    return (std::bit_cast<std::uint32_t>(x) & 0x7F800000u) != 0x7F800000u;
}

// Non-finite values built from bit patterns through a volatile (FR-062).
[[nodiscard]] inline float makeNaN() noexcept {
    volatile std::uint32_t bits = 0x7FC00000u;
    return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
}

[[nodiscard]] inline float makePosInf() noexcept {
    volatile std::uint32_t bits = 0x7F800000u;
    return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
}

[[nodiscard]] inline double makeNaND() noexcept {
    volatile std::uint64_t bits = 0x7FF8000000000000ULL;
    return std::bit_cast<double>(static_cast<std::uint64_t>(bits));
}

// RMS in dBFS over [from, min(to, x.size())). Exact zero (or an empty range)
// returns -200.0.
[[nodiscard]] inline double rmsDb(std::span<const float> x, std::size_t from = 0,
                                  std::size_t to = kToEnd) {
    const std::size_t end = std::min(to, x.size());
    REQUIRE(from <= end);
    if (from == end) {
        return -200.0;
    }
    double sum = 0.0;
    for (std::size_t i = from; i < end; ++i) {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    if (sum == 0.0) {
        return -200.0;
    }
    return 10.0 * std::log10(sum / static_cast<double>(end - from));
}

[[nodiscard]] inline float peakOf(std::span<const float> x) noexcept {
    float peak = 0.0f;
    for (const float v : x) {
        peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

[[nodiscard]] inline float maxAbsDiff(std::span<const float> a, std::span<const float> b,
                                      std::size_t from = 0, std::size_t to = kToEnd) {
    const std::size_t end = std::min(to, std::min(a.size(), b.size()));
    REQUIRE(from <= end);
    float worst = 0.0f;
    for (std::size_t i = from; i < end; ++i) {
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    }
    return worst;
}

// The count of a[i] != b[i] over the common length: an explicit mismatch count,
// never a golden (FR-065).
[[nodiscard]] inline std::size_t mismatches(std::span<const float> a,
                                            std::span<const float> b) noexcept {
    const std::size_t end = std::min(a.size(), b.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < end; ++i) {
        if (a[i] != b[i]) {
            ++count;
        }
    }
    return count;
}

}  // namespace ProfundumPluginTest
