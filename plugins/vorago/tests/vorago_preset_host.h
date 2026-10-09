#pragma once

// ==============================================================================
// Vorago Phase 14 - Catch2-free preset host (FR-021, plan 5.6; T004)
// ==============================================================================
// VoragoTest::PresetHost drives ONE heap-held ::Vorago::Processor the way a host
// does: initialize -> setupProcessing -> setActive, then event-in / stereo-out
// process() calls, and component state in/out through Steinberg::MemoryStream.
//
// Catch2-free: shared by vorago_preset_generator (which does not link Catch2) and
// every preset test TU, so any Catch2 dependency here fails the generator build.
//
// buildPresetComponentState (C2, T027; FR-021, C-4) is THE drive shared by the
// generator and the tree-match test: validate the definition, then one
// process(512) block carrying every point at offset 0, then getState().
//
// ALLOCATION BEHAVIOUR: outL_/outR_ are sized once by prepare() and never
// regrown; process() builds ProcessData / AudioBusBuffers on the stack.
// ==============================================================================

#include "plugin_ids.h"
#include "processor/processor.h"
#include "vorago_preset_defs.h"  // tools/ is on the include path (T027).

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/core/db_utils.h>

#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace VoragoTest {

class PresetHost {
public:
    PresetHost() : proc_(std::make_unique<::Vorago::Processor>()) {}

    ~PresetHost() {
        if (active_) {
            proc_->setActive(false);
            active_ = false;
        }
        if (initialized_) {
            proc_->terminate();
            initialized_ = false;
        }
    }

    PresetHost(const PresetHost&) = delete;
    PresetHost& operator=(const PresetHost&) = delete;

    /// initialize(nullptr) -> setupProcessing({kRealtime, kSample32, maxBlock, sr})
    /// -> setActive(true). Sizes outL_/outR_ to maxBlock ONCE (never regrown).
    [[nodiscard]] Steinberg::tresult prepare(double sampleRate, Steinberg::int32 maxBlock) {
        if (maxBlock <= 0 || initialized_) {
            return Steinberg::kResultFalse;
        }
        Steinberg::tresult r = proc_->initialize(nullptr);
        if (r != Steinberg::kResultOk) {
            return r;
        }
        initialized_ = true;

        Steinberg::Vst::ProcessSetup setup{.processMode = Steinberg::Vst::kRealtime,
                                           .symbolicSampleSize = Steinberg::Vst::kSample32,
                                           .maxSamplesPerBlock = maxBlock,
                                           .sampleRate = sampleRate};
        r = proc_->setupProcessing(setup);
        if (r != Steinberg::kResultOk) {
            return r;
        }
        r = proc_->setActive(true);
        if (r != Steinberg::kResultOk) {
            return r;
        }
        active_ = true;

        outL_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
        outR_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
        return Steinberg::kResultOk;
    }

    /// One event-in / stereo-out process() call of n samples (n <= maxBlock).
    /// outL()/outR() then view exactly those n samples.
    [[nodiscard]] Steinberg::tresult process(std::size_t n, Steinberg::Vst::IEventList* ev,
                                             Steinberg::Vst::IParameterChanges* pc) {
        if (!active_ || n > outL_.size()) {
            return Steinberg::kResultFalse;
        }

        std::array<float*, 2> channels{outL_.data(), outR_.data()};

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

        const Steinberg::tresult r = proc_->process(data);
        lastBlock_ = n;
        return r;
    }

    /// setState() from a MemoryStream holding `comp`, positioned at 0.
    [[nodiscard]] Steinberg::tresult loadState(std::span<const std::uint8_t> comp) {
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        Steinberg::int32 written = 0;
        if (!comp.empty()) {
            // IBStream::write takes a non-const pointer but does not modify the source.
            const Steinberg::tresult w = stream->write(
                const_cast<std::uint8_t*>(comp.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                static_cast<Steinberg::int32>(comp.size()), &written);
            if (w != Steinberg::kResultOk ||
                written != static_cast<Steinberg::int32>(comp.size())) {
                return Steinberg::kResultFalse;
            }
        }
        if (stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) != Steinberg::kResultOk) {
            return Steinberg::kResultFalse;
        }
        return proc_->setState(stream);
    }

    /// getState() into a MemoryStream; `out` receives exactly the written bytes.
    [[nodiscard]] bool saveState(std::vector<std::uint8_t>& out) {
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        if (proc_->getState(stream) != Steinberg::kResultOk) {
            return false;
        }
        const auto size = static_cast<std::size_t>(stream->getSize());
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(stream->getData());
        if (size > 0 && bytes == nullptr) {
            return false;
        }
        out.assign(bytes, bytes + size);
        return true;
    }

    [[nodiscard]] std::span<const float> outL() const noexcept {
        return {outL_.data(), lastBlock_};
    }
    [[nodiscard]] std::span<const float> outR() const noexcept {
        return {outR_.data(), lastBlock_};
    }

    [[nodiscard]] ::Vorago::Processor& processor() noexcept { return *proc_; }

    /// Phase 13c measurement seam (plan 2.9, ruling P2; T066): the engine, mutable,
    /// so a RenderSpec::engineTweak can set a lever after loadState. Tests only;
    /// call between process() blocks on the test thread, never concurrently with
    /// process(). nullptr before prepare().
    [[nodiscard]] Krate::DSP::VoragoEngine* engineForTweak() noexcept {
        // engineForTest() is the only accessor and returns const; the engine is a
        // non-const heap object (Processor::engine_ is a non-const unique_ptr), so
        // casting the constness away is well-defined.
        return const_cast<Krate::DSP::VoragoEngine*>(  // NOLINT(cppcoreguidelines-pro-type-const-cast): plan 2.9 - engine_ is a non-const heap object; engineForTest() is the only accessor
            proc_->engineForTest());
    }

private:
    std::unique_ptr<::Vorago::Processor> proc_;
    std::vector<float> outL_, outR_;
    std::size_t lastBlock_ = 0;
    bool initialized_ = false;
    bool active_ = false;
};

/// C-4, THE drive (FR-021): prepare(48000, 512) -> ONE process(512) carrying every
/// point of `def` at offset 0 -> saveState(comp). Rejects (false, `why` set)
/// before any processing: a value outside [0, 1] or non-finite (bit pattern); any
/// point for kSustainPedalId (4) or kChannelPressureId (5); a duplicate ID.
[[nodiscard]] inline bool buildPresetComponentState(const Vorago::PresetDefs::VoragoPresetDef& def,
                                                    std::vector<std::uint8_t>& comp,
                                                    std::string& why) {
    comp.clear();
    why.clear();
    const std::string presetName(def.name);
    for (std::size_t i = 0; i < def.params.size(); ++i) {
        const Vorago::PresetDefs::ParamSetting& p = def.params[i];
        const std::string where = presetName + ": point " + std::to_string(i) + " (ID " +
                                  std::to_string(p.id) + ")";
        if (!Krate::DSP::detail::isFinite(p.normalized)) {
            why = where + " has a non-finite value";
            return false;
        }
        if (p.normalized < 0.0 || p.normalized > 1.0) {
            why = where + " value " + std::to_string(p.normalized) + " is outside [0, 1]";
            return false;
        }
        if (p.id == ::Vorago::kSustainPedalId || p.id == ::Vorago::kChannelPressureId) {
            why = where + " targets a non-persisted performance controller";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (def.params[j].id == p.id) {
                why = where + " duplicates point " + std::to_string(j);
                return false;
            }
        }
    }

    PresetHost host;
    if (host.prepare(48000.0, 512) != Steinberg::kResultOk) {
        why = presetName + ": PresetHost::prepare failed";
        return false;
    }
    Krate::Test::ParameterChanges changes;
    for (const Vorago::PresetDefs::ParamSetting& p : def.params) {
        changes.addChange(p.id, p.normalized);
    }
    if (host.process(512, nullptr, &changes) != Steinberg::kResultOk) {
        why = presetName + ": the parameter-change block failed to process";
        return false;
    }
    if (!host.saveState(comp)) {
        comp.clear();
        why = presetName + ": getState failed";
        return false;
    }
    return true;
}

}  // namespace VoragoTest
