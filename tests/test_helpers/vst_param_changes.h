#pragma once

// =============================================================================
// Shared VST3 parameter-change test mocks
// =============================================================================
// Minimal, header-only implementations of IParamValueQueue / IParameterChanges
// for feeding parameter automation into a processor's process() call from tests.
//
// These were previously copy-pasted (with only class-name / FUnknown-style
// differences) into ~50 plugin test files under names like MockParamValueQueue,
// SimpleParamValueQueue, MockParameterChanges, MockParamChangesWithData, etc.
// Consolidated here; migrated files typically alias the local names:
//
//     using MockParamValueQueue   = Krate::Test::ParamValueQueue;
//     using MockParameterChanges  = Krate::Test::ParameterChanges;   // empty or with data
//
// SCOPE: these model the common "single point per parameter, sampleOffset 0"
// input case. Output-CAPTURING queue variants (that record addPoint() writes
// the processor makes, e.g. ArpOutputParamQueue) are a genuinely different mock
// and must NOT be replaced with these — keep those local.
//
// The FUnknown methods are intentionally non-refcounting: the mocks are meant to
// live on the stack for the duration of a single process() call and are never
// retained by the processor.
// =============================================================================

#include <pluginterfaces/vst/ivstparameterchanges.h>

#include <vector>

namespace Krate::Test {

// -----------------------------------------------------------------------------
// ParamValueQueue — one parameter, a single automation point (sampleOffset 0).
// -----------------------------------------------------------------------------
class ParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    ParamValueQueue(Steinberg::Vst::ParamID id, double value)
        : id_(id), value_(value) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }

    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index,
                                           Steinberg::int32& sampleOffset,
                                           Steinberg::Vst::ParamValue& value) override {
        if (index != 0) {
            return Steinberg::kResultFalse;
        }
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 /*sampleOffset*/,
                                           Steinberg::Vst::ParamValue /*value*/,
                                           Steinberg::int32& /*index*/) override {
        return Steinberg::kResultFalse;
    }

private:
    Steinberg::Vst::ParamID id_;
    double value_;
};

// -----------------------------------------------------------------------------
// ParameterChanges — container holding zero or more single-point queues.
// Default-constructed instance models an empty IParameterChanges (count 0).
// Call addChange(id, value) to inject parameter automation.
// -----------------------------------------------------------------------------
class ParameterChanges : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size())) {
            return nullptr;
        }
        return &queues_[static_cast<size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID& /*id*/, Steinberg::int32& /*index*/) override {
        return nullptr;
    }

    // Test-side helper: queue a single-point change for `id` at value `value`.
    // Stored by value so that clear()+addChange reuse the vector's capacity (no
    // per-call heap allocation after warm-up) — some processor tests assert zero
    // audio-thread allocations across a block-rate automation loop. Callers must
    // not interleave addChange() with getParameterData(); the mock is filled,
    // then read during a single process() call.
    void addChange(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

    // Compatibility synonyms for addChange(). The pre-consolidation mocks used a
    // handful of different names for the same operation; these let migrated files
    // keep their call sites unchanged via a `using` alias.
    void add(Steinberg::Vst::ParamID id, double value) { addChange(id, value); }
    void addParam(Steinberg::Vst::ParamID id, double value) { addChange(id, value); }

    // setChange() replaces any queued changes with a single one (some mocks
    // delivered exactly one active parameter per process() call).
    void setChange(Steinberg::Vst::ParamID id, double value) {
        clear();
        addChange(id, value);
    }

    void clear() { queues_.clear(); }

private:
    std::vector<ParamValueQueue> queues_;
};

}  // namespace Krate::Test
