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

#include "controller/controller.h"
#include "engine/profundum_engine_config.h"
#include "parameters/sound_param_row.h"
#include "plugin_ids.h"
#include "processor/processor.h"

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <pluginterfaces/base/smartpointer.h>
#include <pluginterfaces/vst/ivstattributes.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/common/memorystream.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
// RecordingConnectionPoint (T020, plan 6.1): an IConnectionPoint that records
// each message's ID, its int attributes ("endpoint", "mode", "from"), its binary
// "blob" attribute and std::this_thread::get_id(), then forwards the message to
// `target->notify`. With `hold` set, a message is recorded and kept (IPtr) instead
// of forwarded; releaseHeld() delivers the kept messages in order (the SC-025
// delayed-reply arm). The record list is mutex-guarded, so a send from an
// unexpected thread is recorded rather than raced.
// -----------------------------------------------------------------------------
class RecordingConnectionPoint final : public Steinberg::Vst::IConnectionPoint {
public:
    struct Record {
        std::string id;
        std::map<std::string, Steinberg::int64> ints;
        bool hasBlob = false;
        std::vector<std::byte> blob;
        std::thread::id thread;
    };

    Steinberg::Vst::IConnectionPoint* target = nullptr;
    bool hold = false;

    [[nodiscard]] std::vector<Record> records() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return records_;
    }
    void clear() {
        const std::lock_guard<std::mutex> lock(mutex_);
        records_.clear();
    }
    /// The number of recorded messages with ID @p id.
    [[nodiscard]] std::size_t count(std::string_view id) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::size_t n = 0;
        for (const Record& r : records_) {
            if (r.id == id) {
                ++n;
            }
        }
        return n;
    }
    /// The number of recorded messages with ID @p id whose int attribute @p key equals @p value.
    [[nodiscard]] std::size_t count(std::string_view id, const std::string& key, Steinberg::int64 value) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::size_t n = 0;
        for (const Record& r : records_) {
            const auto it = r.ints.find(key);
            if (r.id == id && it != r.ints.end() && it->second == value) {
                ++n;
            }
        }
        return n;
    }
    /// The number of recorded messages sent from thread @p t.
    [[nodiscard]] std::size_t sendsOnThread(std::thread::id t) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::size_t n = 0;
        for (const Record& r : records_) {
            if (r.thread == t) {
                ++n;
            }
        }
        return n;
    }
    /// Delivers every held message to the target, in arrival order, and stops holding.
    void releaseHeld() {
        hold = false;
        std::vector<Steinberg::IPtr<Steinberg::Vst::IMessage>> pending;
        pending.swap(held_);
        for (auto& m : pending) {
            if (target != nullptr) {
                static_cast<void>(target->notify(m));
            }
        }
    }

    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* /*other*/) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* /*other*/) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override {
        if (message == nullptr) {
            return Steinberg::kInvalidArgument;
        }
        Record r;
        const Steinberg::FIDString id = message->getMessageID();
        r.id = (id != nullptr) ? std::string(id) : std::string();
        r.thread = std::this_thread::get_id();
        if (Steinberg::Vst::IAttributeList* attrs = message->getAttributes()) {
            for (const char* key : {"endpoint", "mode", "from"}) {
                Steinberg::int64 v = 0;
                if (attrs->getInt(key, v) == Steinberg::kResultOk) {
                    r.ints[key] = v;
                }
            }
            const void* data = nullptr;
            Steinberg::uint32 size = 0;
            if (attrs->getBinary("blob", data, size) == Steinberg::kResultOk && data != nullptr) {
                const auto* bytes = static_cast<const std::byte*>(data);
                r.hasBlob = true;
                r.blob.assign(bytes, bytes + size);
            }
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            records_.push_back(std::move(r));
        }
        if (hold) {
            held_.emplace_back(message);  // IPtr(I*, addRef = true)
            return Steinberg::kResultOk;
        }
        return (target != nullptr) ? target->notify(message) : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

private:
    mutable std::mutex mutex_;
    std::vector<Record> records_;
    std::vector<Steinberg::IPtr<Steinberg::Vst::IMessage>> held_;
};

// -----------------------------------------------------------------------------
// RecordingComponentHandler (T016, plan 6.1): an IComponentHandler +
// IComponentHandler2 that records, per ID, the beginEdit / performEdit / endEdit
// counts, every restartComponent flag in call order and the setDirty(true)
// count. onRestart (optional) runs INSIDE restartComponent, so a test can read
// the controller's state at the moment of the call (SC-009 part 6).
// -----------------------------------------------------------------------------
class RecordingComponentHandler final : public Steinberg::Vst::IComponentHandler,
                                        public Steinberg::Vst::IComponentHandler2 {
public:
    std::map<Steinberg::Vst::ParamID, int> beginEdits;
    std::map<Steinberg::Vst::ParamID, int> performEdits;
    std::map<Steinberg::Vst::ParamID, int> endEdits;
    std::vector<Steinberg::int32> restartFlags;
    int dirtyCount = 0;
    std::function<void(Steinberg::int32)> onRestart;

    // The sum of begin + perform + end calls recorded for @p id.
    [[nodiscard]] int editCallsFor(Steinberg::Vst::ParamID id) const {
        const auto countIn = [id](const std::map<Steinberg::Vst::ParamID, int>& m) {
            const auto it = m.find(id);
            return it == m.end() ? 0 : it->second;
        };
        return countIn(beginEdits) + countIn(performEdits) + countIn(endEdits);
    }

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override {
        ++beginEdits[id];
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue /*value*/) override {
        ++performEdits[id];
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override {
        ++endEdits[id];
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override {
        restartFlags.push_back(flags);
        if (onRestart) {
            onRestart(flags);
        }
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool state) override {
        if (state != 0) {
            ++dirtyCount;
        }
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString /*name*/) override {
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API startGroupEdit() override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API finishGroupEdit() override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler2::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler2*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    // Owned by the fixture (never by the controller's IPtr): reference counting is a no-op.
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

// -----------------------------------------------------------------------------
// ControllerFixture (T016, plan 6.1): a heap Controller, initialised with the
// given context (null by default: allocateMessage() then returns nullptr, so the
// controller sends nothing), with a RecordingComponentHandler attached. The
// handler is declared first, so it outlives the controller; the destructor
// detaches and terminates.
// -----------------------------------------------------------------------------
struct ControllerFixture {
    RecordingComponentHandler handler;
    Steinberg::IPtr<::Profundum::Controller> controller = Steinberg::owned(new ::Profundum::Controller());

    explicit ControllerFixture(Steinberg::FUnknown* context = nullptr) {
        REQUIRE(controller->initialize(context) == Steinberg::kResultOk);
        REQUIRE(controller->setComponentHandler(&handler) == Steinberg::kResultTrue);
    }
    ~ControllerFixture() {
        controller->setComponentHandler(nullptr);
        controller->terminate();
    }
    ControllerFixture(const ControllerFixture&) = delete;
    ControllerFixture& operator=(const ControllerFixture&) = delete;
    ControllerFixture(ControllerFixture&&) = delete;
    ControllerFixture& operator=(ControllerFixture&&) = delete;
};

// -----------------------------------------------------------------------------
// makeEndpointMessage (T016, plan 3.3): the processor -> controller endpoint
// message: ID kMsgEndpoint, int "endpoint" (0 A, 1 B), binary "blob". The bytes
// overload carries any blob as given (a test corrupts one with it); the
// MorphState overload carries the 201 bytes of serializeMorphState.
// -----------------------------------------------------------------------------
[[nodiscard]] inline std::array<std::byte, Krate::DSP::kMorphStateBytes> serializedMorphState(
    const Krate::DSP::MorphState& s) {
    std::array<std::byte, Krate::DSP::kMorphStateBytes> blob{};
    REQUIRE(Krate::DSP::serializeMorphState(s, blob.data(), blob.size()) == Krate::DSP::kMorphStateBytes);
    return blob;
}

[[nodiscard]] inline Steinberg::IPtr<Steinberg::Vst::HostMessage> makeEndpointMessage(
    Krate::DSP::BassMorphEngine::Endpoint e, std::span<const std::byte> blob) {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID(::Profundum::kMsgEndpoint);
    Steinberg::Vst::IAttributeList* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);
    REQUIRE(attrs->setInt("endpoint", static_cast<Steinberg::int64>(e)) == Steinberg::kResultOk);
    REQUIRE(attrs->setBinary("blob", blob.data(), static_cast<Steinberg::uint32>(blob.size())) ==
            Steinberg::kResultOk);
    return msg;
}

[[nodiscard]] inline Steinberg::IPtr<Steinberg::Vst::HostMessage> makeEndpointMessage(
    Krate::DSP::BassMorphEngine::Endpoint e, const Krate::DSP::MorphState& s) {
    const auto blob = serializedMorphState(s);
    return makeEndpointMessage(e, std::span<const std::byte>(blob));
}

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

    // ---- Phase 11 (plan 6.1) ----
    static constexpr std::size_t kParamBlockSamples = 512;

    // One process() block of n samples carrying ONE queue (id, one point at
    // offset 0, value v).
    Steinberg::tresult setParam(Steinberg::Vst::ParamID id, double v, std::size_t n = kParamBlockSamples) {
        return setParamsInBlock({{id, v}}, n);
    }

    // One process() block of n samples carrying one queue per entry, IN THE
    // GIVEN ORDER (a repeated ID makes a second queue), each with one point at
    // offset 0.
    Steinberg::tresult setParamsInBlock(
        std::initializer_list<std::pair<Steinberg::Vst::ParamID, double>> changes,
        std::size_t n = kParamBlockSamples) {
        paramChanges_.clear();
        paramChanges_.reserve(changes.size());
        for (const auto& change : changes) {
            paramChanges_.addQueue(change.first).addTestPoint(0, change.second);
        }
        ensureCapture(n);
        const Steinberg::tresult r = processBlock(n, nullptr, &paramChanges_);
        paramChanges_.clear();
        return r;
    }

    // Grows the capture geometrically so that n more samples fit.
    void ensureCapture(std::size_t n) {
        const std::size_t need = capturedL.size() + n;
        if (capturedL.capacity() < need || capturedR.capacity() < need) {
            reserveCapture(std::max(need, 2u * capturedL.capacity()));
        }
    }

    [[nodiscard]] const Krate::DSP::ProfundumEngine* engine() const noexcept { return proc->engineForTest(); }

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
    MultiParamChanges paramChanges_;          // setParam / setParamsInBlock, reused
};

// -----------------------------------------------------------------------------
// ConnectedPair (T020, plan 6.1): a processor and a controller initialised with
// one HostApplication (so allocateMessage() works on both sides) and wired
// through two RecordingConnectionPoints: the processor's peer forwards to
// controller->notify, the controller's peer to processor->notify. The host is
// declared first and the recorders before the components, so every object a
// component points at outlives it; the destructor disconnects first.
// -----------------------------------------------------------------------------
struct ConnectedPair {
    Steinberg::Vst::HostApplication host;     // hostclasses.h:32 (createInstance -> HostMessage)
    RecordingConnectionPoint toController;    // processor -> controller
    RecordingConnectionPoint toProcessor;     // controller -> processor
    ProcessorFixture fx{&host};               // initialised, NOT prepared
    ControllerFixture cf{&host};

    ConnectedPair() {
        toController.target = cf.controller.get();
        toProcessor.target = fx.proc.get();
        REQUIRE(fx.proc->connect(&toController) == Steinberg::kResultOk);
        REQUIRE(cf.controller->connect(&toProcessor) == Steinberg::kResultOk);
    }
    ~ConnectedPair() {
        fx.proc->disconnect(&toController);
        cf.controller->disconnect(&toProcessor);
    }
    ConnectedPair(const ConnectedPair&) = delete;
    ConnectedPair& operator=(const ConnectedPair&) = delete;
    ConnectedPair(ConnectedPair&&) = delete;
    ConnectedPair& operator=(ConnectedPair&&) = delete;

    void clearRecords() {
        toController.clear();
        toProcessor.clear();
    }
    /// The number of recorded sends, in either direction, made from thread @p t.
    [[nodiscard]] std::size_t sendsOnThread(std::thread::id t) const {
        return toController.sendsOnThread(t) + toProcessor.sendsOnThread(t);
    }
};

// -----------------------------------------------------------------------------
// randomValidMorphState (plan 6.1): a std::mt19937 over every leaf, one
// normalised draw per leaf written through leafWordFromNormalized, so the state
// is valid by construction. The draw is mt19937's raw word / (2^32 - 1), which is
// specified bit-for-bit by the standard (no implementation-defined distribution).
// -----------------------------------------------------------------------------
struct RandomStateConstraints {
    bool motionSourceOffOrEnv = false;  // SC-011: motion.source in {Off, Env}
    bool fieldMotionDepthZero = false;  // SC-011: field.motionDepth = 0
};

inline constexpr std::uint8_t kLeafFieldMotionDepth = 30;
inline constexpr std::uint8_t kLeafMotionSource = 36;
static_assert(std::string_view(::Profundum::lawOf(kLeafFieldMotionDepth).name) == "field.motionDepth");
static_assert(std::string_view(::Profundum::lawOf(kLeafMotionSource).name) == "motion.source");
static_assert(static_cast<int>(Krate::DSP::MorphState::MotionConfig::Source::Off) == 0 &&
              static_cast<int>(Krate::DSP::MorphState::MotionConfig::Source::Env) == 1);

[[nodiscard]] inline Krate::DSP::MorphState randomValidMorphState(std::uint32_t seed,
                                                                  const RandomStateConstraints& constraints = {}) {
    std::mt19937 rng(seed);
    ::Profundum::LeafBits bits{};
    for (std::size_t i = 0; i < ::Profundum::kNumLeaves; ++i) {
        const double v = static_cast<double>(rng()) / 4294967295.0;
        bits[i] = ::Profundum::leafWordFromNormalized(static_cast<std::uint8_t>(i), v);
    }
    if (constraints.motionSourceOffOrEnv) {
        bits[kLeafMotionSource] %= 2u;  // Off (0) or Env (1)
    }
    if (constraints.fieldMotionDepthZero) {
        bits[kLeafFieldMotionDepth] = std::bit_cast<std::uint32_t>(0.0f);
    }
    const Krate::DSP::MorphState s = ::Profundum::morphStateFromLeafBits(bits);
    REQUIRE(Krate::DSP::isValidMorphState(s));
    return s;
}

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

// -----------------------------------------------------------------------------
// State v2 helpers (T017, plan 6.1). Every offset is derived from the
// plugin_ids.h layout constants; no second hand-written layout exists.
// -----------------------------------------------------------------------------
using StateBytes = std::vector<std::byte>;

inline constexpr std::size_t kV2GlobalsAt = 4;                                               // gain, mode, poly, ceiling
inline constexpr std::size_t kV2MacrosAt = kV2GlobalsAt + 16u;                               // four macros
inline constexpr std::size_t kV2NoteAt = ::Profundum::kStateV1Bytes;                         // 36: eight note fields
inline constexpr std::size_t kV2MorphAt = kV2NoteAt + ::Profundum::kStateNoteBlockBytes;     // 68: pos, ramp, mode, target
inline constexpr std::size_t kV2SeedAt = kV2MorphAt + ::Profundum::kStateMorphControlBytes;  // 84
static_assert(kV2MacrosAt + 16u == ::Profundum::kStateV1Bytes, "v1 body: 4 globals + 4 macros");
static_assert(kV2SeedAt + ::Profundum::kStateSeedBytes == ::Profundum::kStateEndpointAOffset,
              "the seed precedes endpoint A");

/// A MemoryStream holding @p bytes, positioned at 0.
[[nodiscard]] inline Steinberg::IPtr<Steinberg::MemoryStream> memoryStreamOf(std::span<const std::byte> bytes) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    Steinberg::int32 written = 0;
    if (!bytes.empty()) {
        // IBStream::write takes a non-const void*; MemoryStream only reads from it.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        REQUIRE(stream->write(const_cast<std::byte*>(bytes.data()), static_cast<Steinberg::int32>(bytes.size()),
                              &written) == Steinberg::kResultOk);
    }
    REQUIRE(std::cmp_equal(written, bytes.size()));
    REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
    return stream;
}

/// A real getState stream of @p proc, as bytes (plan 6.1 writeV2Stream).
[[nodiscard]] inline StateBytes writeV2Stream(::Profundum::Processor& proc) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(proc.getState(stream) == Steinberg::kResultOk);
    const auto size = static_cast<std::size_t>(stream->getSize());
    StateBytes bytes(size);
    if (size > 0u) {
        std::memcpy(bytes.data(), stream->getData(), size);
    }
    return bytes;
}

/// setState of @p bytes (all of them) on @p proc.
inline Steinberg::tresult loadStateBytes(::Profundum::Processor& proc, std::span<const std::byte> bytes) {
    auto stream = memoryStreamOf(bytes);
    return proc.setState(stream);
}

// Little-endian 32-bit field writers / reader over a state byte vector.
inline void patchU32(StateBytes& b, std::size_t at, std::uint32_t v) {
    REQUIRE(at + 4u <= b.size());
    for (std::size_t i = 0; i < 4u; ++i) {
        b[at + i] = static_cast<std::byte>((v >> (8u * i)) & 0xFFu);
    }
}
inline void patchI32(StateBytes& b, std::size_t at, std::int32_t v) { patchU32(b, at, static_cast<std::uint32_t>(v)); }
inline void patchF32(StateBytes& b, std::size_t at, float v) { patchU32(b, at, std::bit_cast<std::uint32_t>(v)); }
[[nodiscard]] inline std::uint32_t readU32(std::span<const std::byte> b, std::size_t at) {
    REQUIRE(at + 4u <= b.size());
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4u; ++i) {
        v |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(b[at + i])) << (8u * i);
    }
    return v;
}

/// @p bytes (a v2 stream) with the two endpoint blobs replaced by serializeMorphState of @p a / @p b.
[[nodiscard]] inline StateBytes withEndpoints(StateBytes bytes, const Krate::DSP::MorphState& a,
                                              const Krate::DSP::MorphState& b) {
    REQUIRE(bytes.size() == ::Profundum::kStateV2Bytes);
    const auto blobA = serializedMorphState(a);
    const auto blobB = serializedMorphState(b);
    std::memcpy(bytes.data() + ::Profundum::kStateEndpointAOffset, blobA.data(), blobA.size());
    std::memcpy(bytes.data() + ::Profundum::kStateEndpointBOffset, blobB.data(), blobB.size());
    return bytes;
}

/// The v1 global block (plan 3.4 order).
struct V1Globals {
    float gain = 1.0f;
    std::int32_t voiceMode = 0;
    std::int32_t polyphony = 4;
    float ceilingDb = -1.0f;
};

/// A 36-byte Phase 10 stream: header kStateV1Version, the globals, the four macros.
[[nodiscard]] inline StateBytes v1Stream(const V1Globals& g, const std::array<float, 4>& macros) {
    StateBytes b(::Profundum::kStateV1Bytes);
    patchI32(b, 0, ::Profundum::kStateV1Version);
    patchF32(b, kV2GlobalsAt + 0u, g.gain);
    patchI32(b, kV2GlobalsAt + 4u, g.voiceMode);
    patchI32(b, kV2GlobalsAt + 8u, g.polyphony);
    patchF32(b, kV2GlobalsAt + 12u, g.ceilingDb);
    for (std::size_t i = 0; i < macros.size(); ++i) {
        patchF32(b, kV2MacrosAt + 4u * i, macros[i]);
    }
    return b;
}

/// The explicit (non-endpoint) v2 fields a test writes into a stream. Defaults are the registered defaults.
struct V2Fields {
    V1Globals globals{};
    std::array<float, 4> macros{0.5f, 0.5f, 0.5f, 0.5f};
    std::array<std::int32_t, 2> priorityLegato{0, 0};   // 410, 411
    float portamentoMs = 0.0f;                          // 412
    std::array<std::int32_t, 4> noteLists{0, 0, 0, 0};  // 413 porta mode, 414 glide, 415 memory, 416 retrigger
    float bendRangeSt = 2.0f;                           // 417
    float position = 0.0f;                              // 105
    float rampMs = 40.0f;                               // 1002
    std::int32_t anchorMorphMode = 1;                   // 1000
    std::int32_t editTarget = 0;                        // 1001
    std::uint32_t seed = Krate::DSP::ProfundumEngine::kDefaultSeed;
};

/// @p bytes (a v2 stream) with every explicit field of @p f written in place (bytes 4-87).
[[nodiscard]] inline StateBytes withFields(StateBytes bytes, const V2Fields& f) {
    REQUIRE(bytes.size() == ::Profundum::kStateV2Bytes);
    patchF32(bytes, kV2GlobalsAt + 0u, f.globals.gain);
    patchI32(bytes, kV2GlobalsAt + 4u, f.globals.voiceMode);
    patchI32(bytes, kV2GlobalsAt + 8u, f.globals.polyphony);
    patchF32(bytes, kV2GlobalsAt + 12u, f.globals.ceilingDb);
    for (std::size_t i = 0; i < f.macros.size(); ++i) {
        patchF32(bytes, kV2MacrosAt + 4u * i, f.macros[i]);
    }
    patchI32(bytes, kV2NoteAt + 0u, f.priorityLegato[0]);
    patchI32(bytes, kV2NoteAt + 4u, f.priorityLegato[1]);
    patchF32(bytes, kV2NoteAt + 8u, f.portamentoMs);
    for (std::size_t i = 0; i < f.noteLists.size(); ++i) {
        patchI32(bytes, kV2NoteAt + 12u + 4u * i, f.noteLists[i]);
    }
    patchF32(bytes, kV2NoteAt + 28u, f.bendRangeSt);
    patchF32(bytes, kV2MorphAt + 0u, f.position);
    patchF32(bytes, kV2MorphAt + 4u, f.rampMs);
    patchI32(bytes, kV2MorphAt + 8u, f.anchorMorphMode);
    patchI32(bytes, kV2MorphAt + 12u, f.editTarget);
    patchU32(bytes, kV2SeedAt, f.seed);
    return bytes;
}

/// Plan 6.2 SC-012 prior state P: random A / B (0x12A / 0x12B), non-default macros and note fields,
/// position 0.7, ramp 200 ms, Punch (Yield), Edit Target B, seed 0x5EED.
inline constexpr std::uint32_t kPriorSeedA = 0x12Au;
inline constexpr std::uint32_t kPriorSeedB = 0x12Bu;
inline constexpr std::uint32_t kPriorSeed = 0x5EEDu;

[[nodiscard]] inline V2Fields priorFieldsP() {
    V2Fields f;
    f.globals = V1Globals{.gain = 0.5f, .voiceMode = 0, .polyphony = 4, .ceilingDb = -1.0f};
    f.macros = {0.1f, 0.2f, 0.8f, 0.9f};
    f.priorityLegato = {2, 1};   // HighNote, legato on
    f.portamentoMs = 250.0f;
    f.noteLists = {1, 2, 3, 1};  // LegatoOnly, Quantized, OscillatorContinuous, Continue
    f.bendRangeSt = 12.0f;
    f.position = 0.7f;
    f.rampMs = 200.0f;
    f.anchorMorphMode = 2;  // Punch (Yield)
    f.editTarget = 1;       // B
    f.seed = kPriorSeed;
    return f;
}

/// Prepares @p fx at 48 kHz, loads P through setState, re-arms the seed (setActive false / true, FR-043) and
/// renders 1 s in 512-sample blocks, so the runtime blob has been published (plan 6.2 SC-012).
inline void makePriorStateP(ProcessorFixture& fx) {
    REQUIRE(fx.prepare(48000.0) == Steinberg::kResultOk);
    const StateBytes bytes =
        withFields(withEndpoints(writeV2Stream(*fx.proc), randomValidMorphState(kPriorSeedA),
                                 randomValidMorphState(kPriorSeedB)),
                   priorFieldsP());
    REQUIRE(loadStateBytes(*fx.proc, bytes) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setActive(0u) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setActive(1u) == Steinberg::kResultOk);
    fx.renderScript(std::span<const ProcessorFixture::ScriptedEvent>{}, 48000u, 512u);
}

/// The 62 slot words of endpoint @p e.
[[nodiscard]] inline ::Profundum::LeafBits slotsOf(const ::Profundum::Processor& proc,
                                                   Krate::DSP::BassMorphEngine::Endpoint e) {
    ::Profundum::LeafBits bits{};
    for (std::size_t i = 0; i < ::Profundum::kNumLeaves; ++i) {
        bits[i] = proc.endpointSlotForTest(e, i);
    }
    return bits;
}

}  // namespace ProfundumPluginTest
