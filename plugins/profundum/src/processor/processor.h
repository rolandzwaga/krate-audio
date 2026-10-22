#pragma once

// ==============================================================================
// Profundum - Audio Processor (audio thread)   Plan section 2.5
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation. This header includes
// no controller header; processor and controller share only plugin_ids.h and
// the two parameters/*.h packs.
//
// A thin wrapper around Krate::DSP::ProfundumEngine (heap-owned, FR-022): one
// event input, one stereo output, no audio input (FR-021). The only operation
// after the engine's TruePeakLimiter is a smoothed output gain in [0, 1]
// (FR-028). Latency = the limiter look-ahead, 64 samples (FR-032).
//
// The implementation is processor.cpp (plan section 2.5).
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "parameters/global_params.h"
#include "parameters/macro_params.h"

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Profundum {

class Processor : public Steinberg::Vst::AudioEffect {
public:
    static constexpr std::size_t kMaxEventsPerBlock = 1024;  // FR-025; 1024 x 8 B = 8 KiB

    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                     Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs,
                                                     Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    // getTailSamples(): NOT overridden (SDK kNoTail; Phase 11 owns it).

    // ---- FR-033 test seams: const, allocation-free, never called by process() ----
    [[nodiscard]] const Krate::DSP::ProfundumEngine* engineForTest() const noexcept { return engine_.get(); }
    [[nodiscard]] const GlobalParams& globalParamsForTest() const noexcept { return globalParams_; }
    [[nodiscard]] const MacroParams& macroParamsForTest() const noexcept { return macroParams_; }
    [[nodiscard]] float outputGainValueForTest() const noexcept {
        return outputGain_.getCurrentValue();  // smoother.h:191
    }
    [[nodiscard]] std::uint32_t lastSliceCountForTest() const noexcept { return lastSliceCount_; }

private:
    struct EventSlot {
        std::int32_t offset;     // clamped to [0, numSamples - 1]
        std::int32_t listIndex;  // index into data.inputEvents
    };

    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void pushParamsToEngine() noexcept;  // FR-023.5, FR-024.4
    std::size_t buildEventOrder(Steinberg::Vst::IEventList* events, std::size_t total) noexcept;
    void dispatchEvent(const Steinberg::Vst::Event& e) noexcept;
    void applyOutputGain(float* outL, float* outR, std::size_t n) noexcept;

    std::unique_ptr<Krate::DSP::ProfundumEngine> engine_;  // FR-022: heap only
    GlobalParams globalParams_{};
    MacroParams macroParams_{};
    Krate::DSP::OnePoleSmoother outputGain_{1.0f};  // smoother.h:148-155 (explicit initial value)
    bool snapGainPending_ = true;
    bool prepared_ = false;
    std::uint32_t lastSliceCount_ = 0;
    std::array<EventSlot, kMaxEventsPerBlock> eventOrder_{};
};

static_assert(sizeof(Processor) < 64u * 1024u,
              "the ~800 KB engine must live on the heap (FR-022, FR-064)");

}  // namespace Profundum
