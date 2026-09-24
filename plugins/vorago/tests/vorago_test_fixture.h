#pragma once

// ==============================================================================
// Vorago - shared processor test fixture (T006, plan 4.2)
// ==============================================================================
// Defines ONLY what the shared mocks lack (FR-063):
//   - Krate::Test::EventList (tests/test_helpers/vst_event_list.h) is reused for
//     every event list;
//   - Krate::Test::ParameterChanges (tests/test_helpers/vst_param_changes.h) is
//     reused wherever one point at offset 0 suffices. It is single-point by
//     design, so FR-043's "last point wins" and any non-zero point offset need
//     the MultiPointParamValueQueue / MultiParamChanges pair below.
//
// FR-064: the processor is held through a unique_ptr (heap), never a stack local.
//
// NAMESPACE HAZARD: Krate::DSP::TestUtils::Vorago exists
// (tests/test_helpers/vorago_fixtures.h, dsp/tests/unit/systems/vorago_perf_budget.h).
// Plugin types are therefore always spelled ::Vorago::..., and no TU that
// includes this header may write `using namespace Krate::DSP::TestUtils;`.
//
// ALLOCATION BEHAVIOUR: processBlock() builds ProcessData / AudioBusBuffers on the
// stack and writes into buffers sized once by the ctor / prepare(). It appends to
// capturedL / capturedR but NEVER grows them: call reserveCapture() first. The
// event list used by renderScript() and the MultiParamChanges containers keep
// their capacity across clear(), so they are allocation-free once warm.
// ==============================================================================

#include "processor/processor.h"

#include <vst_event_list.h>

#include <krate/dsp/core/db_utils.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace VoragoTest {

// -----------------------------------------------------------------------------
// MultiPointParamValueQueue - a parameter queue carrying an arbitrary number of
// automation points at arbitrary offsets, reported in insertion order.
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
//
// clear() does NOT destroy the queues: it drops the active count so the next
// block reuses the same objects and their point storage. The reference returned
// by addQueue() is invalidated by a later addQueue() that grows the vector -
// call reserve() up front, or finish filling one queue before adding the next.
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
// ProcessorFixture (model: plugins/seraphis/tests/seraphis_test_fixture.h:158)
// -----------------------------------------------------------------------------
struct ProcessorFixture {
    // Guard words either side of every output channel buffer. A processor that
    // writes outside [0, numSamples) fails the REQUIRE in processBlock().
    static constexpr std::size_t kGuardWords = 8;
    static constexpr float kGuardValue = -8.5e17f;  // finite, never a legal sample

    std::unique_ptr<::Vorago::Processor> proc = std::make_unique<::Vorago::Processor>();

    // processBlock() APPENDS each rendered block here. Never grown by the
    // fixture: reserveCapture() first.
    std::vector<float> capturedL, capturedR;

    struct ScriptedEvent {
        std::size_t at;          // absolute sample position in the render
        Steinberg::Vst::Event e; // sampleOffset is overwritten per block
    };

    // Initialised, NOT prepared. The buffers are sized for the default block
    // size so processBlock() is usable on an unprepared processor (FR-030 cases).
    ProcessorFixture() {
        REQUIRE(proc->initialize(nullptr) == Steinberg::kResultOk);
        sizeBuffers(2048);
    }

    // setupProcessing({kRealtime, kSample32, maxBlock, sr}) + setActive(true);
    // sizes both channel buffers once to maxBlock + 2 * kGuardWords.
    void prepare(double sr = 48000.0, Steinberg::int32 maxBlock = 2048) {
        REQUIRE(maxBlock > 0);
        sizeBuffers(static_cast<std::size_t>(maxBlock));

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.maxSamplesPerBlock = maxBlock;
        setup.sampleRate = sr;
        REQUIRE(proc->setupProcessing(setup) == Steinberg::kResultOk);
        REQUIRE(proc->setActive(true) == Steinberg::kResultOk);
    }

    // One stereo process() call of n samples. ProcessData / AudioBusBuffers live
    // on the stack; guard words are rewritten before and REQUIREd intact after.
    Steinberg::tresult processBlock(std::size_t n, Steinberg::Vst::IEventList* ev = nullptr,
                                    Steinberg::Vst::IParameterChanges* pc = nullptr) {
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
        data.processContext = nullptr;

        const Steinberg::tresult r = proc->process(data);

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

    // Renders totalSamples in host blocks that repeat blockPattern cyclically
    // (the final block is truncated to fit). Each scripted event lands in the
    // block containing `at`, at offset at - blockStart; events of one block keep
    // their script order. Reserves the capture itself before rendering.
    void renderScript(std::span<const ScriptedEvent> script, std::size_t totalSamples,
                      std::span<const std::size_t> blockPattern) {
        REQUIRE_FALSE(blockPattern.empty());
        reserveCapture(capturedL.size() + totalSamples);

        std::size_t start = 0;
        std::size_t patternIndex = 0;
        while (start < totalSamples) {
            const std::size_t want = blockPattern[patternIndex];
            REQUIRE(want > 0);
            patternIndex = (patternIndex + 1) % blockPattern.size();
            const std::size_t n = std::min(want, totalSamples - start);

            scriptEvents_.clear();
            for (const ScriptedEvent& s : script) {
                if (s.at >= start && s.at < start + n) {
                    Steinberg::Vst::Event e = s.e;
                    e.sampleOffset = static_cast<Steinberg::int32>(s.at - start);
                    scriptEvents_.addEvent(e);
                }
            }

            REQUIRE(processBlock(n, &scriptEvents_) == Steinberg::kResultOk);
            start += n;
        }
        scriptEvents_.clear();
    }

    [[nodiscard]] float* audioL() noexcept { return storageL_.data() + kGuardWords; }
    [[nodiscard]] float* audioR() noexcept { return storageR_.data() + kGuardWords; }

private:
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

    static bool guardsIntact(const std::vector<float>& v) noexcept {
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

    std::vector<float> storageL_, storageR_;  // guard | audio | guard
    std::size_t bufferSamples_ = 0;
    Krate::Test::EventList scriptEvents_;     // renderScript's per-block list, reused
};

// -----------------------------------------------------------------------------
// Stats helpers. `to == kToEnd` means "to the end of the shorter span".
// -----------------------------------------------------------------------------
inline constexpr std::size_t kToEnd = std::numeric_limits<std::size_t>::max();

[[nodiscard]] inline float peakOf(std::span<const float> x) noexcept {
    float peak = 0.0f;
    for (const float v : x) {
        peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

[[nodiscard]] inline double rmsOf(std::span<const float> x) noexcept {
    if (x.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const float v : x) {
        sum += static_cast<double>(v) * static_cast<double>(v);
    }
    return std::sqrt(sum / static_cast<double>(x.size()));
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

[[nodiscard]] inline double rmsDiff(std::span<const float> a, std::span<const float> b,
                                    std::size_t from = 0, std::size_t to = kToEnd) {
    const std::size_t end = std::min(to, std::min(a.size(), b.size()));
    REQUIRE(from <= end);
    if (from == end) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = from; i < end; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(end - from));
}

// Bit-pattern based (fast-math immune) - never std::isnan / std::isfinite (FR-062).
[[nodiscard]] inline bool allFinite(std::span<const float> x) noexcept {
    for (const float v : x) {
        if (!Krate::DSP::detail::isFinite(v)) {
            return false;
        }
    }
    return true;
}

}  // namespace VoragoTest
