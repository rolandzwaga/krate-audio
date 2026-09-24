#pragma once

// ==============================================================================
// Vorago - Audio Processor (audio thread)   Plan section 2.5.1
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation. This header includes
// NO controller header.
//
// The class declaration is FINAL (T005). The bodies in processor.cpp start as
// stubs and are replaced, in sequence, by T011-T016.
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"  // Vst::Event (dispatchEvent signature)
#include "parameters/global_params.h"
#include "parameters/macro_params.h"

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Vorago {

class Processor : public Steinberg::Vst::AudioEffect {
public:
    /// Plan section 3.2. 1024 x 8 B = 8 KiB; keeps sizeof(Processor) far below
    /// FR-064's 64 KiB.
    static constexpr std::size_t kMaxEventsPerBlock = 1024;

    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    // getTailSamples(): NOT overridden -> SDK default kNoTail (Clarification Q6).

    // ---- test-access seams: const, allocation-free, never called by process() ----
    [[nodiscard]] const Krate::DSP::VoragoEngine* engineForTest() const noexcept {  // FR-026a
        return engine_.get();
    }
    [[nodiscard]] const Krate::DSP::CavernVerb* cavernForTest() const noexcept {  // SC-013
        return cavern_.get();
    }
    [[nodiscard]] std::uint32_t setPolyphonyCallCountForTest() const noexcept {  // FR-024a.1
        return setPolyphonyCalls_;
    }
    [[nodiscard]] std::uint32_t lastSliceCountForTest() const noexcept {  // FR-026
        return lastSliceCount_;
    }
    [[nodiscard]] float masterGainValueForTest() const noexcept {  // P-2
        return masterGain_.getCurrentValue();
    }
    [[nodiscard]] const GlobalParams& globalParamsForTest() const noexcept {  // SC-009
        return globalParams_;
    }
    [[nodiscard]] const MacroParams& macroParamsForTest() const noexcept {  // SC-009
        return macroParams_;
    }

    // FR-024 steps 5-6 (master gain, then the engine's output stage with the
    // limiter LAST), in place. renderSlice() calls it after the engine and the
    // cavern; it is public so SC-006's discrimination arm (ruling B-1,
    // 2026-09-24) can drive it with a signal of known level, which the six-voice
    // render cannot supply (its unity peak is ~0.24). Audio thread only.
    void renderGainAndOutputStage(float* outL, float* outR, std::size_t n) noexcept;

private:
    struct EventSlot {           // plan section 3.2
        std::int32_t offset;     // clamped to [0, numSamples - 1]
        std::int32_t listIndex;  // index into data.inputEvents
    };

    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void pushGlobalParams() noexcept;
    std::size_t buildEventOrder(Steinberg::Vst::IEventList* events, std::size_t total) noexcept;
    void dispatchEvent(const Steinberg::Vst::Event& e) noexcept;
    void renderSlice(float* outL, float* outR, std::size_t n) noexcept;

    std::unique_ptr<Krate::DSP::VoragoEngine> engine_;  // FR-022: NEVER by value / stack
    std::unique_ptr<Krate::DSP::CavernVerb> cavern_;    // FR-022
    Krate::DSP::VoragoMacroMatrix macros_{};            // FR-022: by value; never written (FR-042)

    GlobalParams globalParams_{};
    MacroParams macroParams_{};  // INERT

    Krate::DSP::OnePoleSmoother masterGain_{1.0f};  // P-2: constructed at 1.0f on purpose
    bool snapGainPending_ = true;                   // FR-024a.2 first-block snap
    bool prepared_ = false;
    std::size_t lastPushedPolyphony_ = 0;
    std::uint32_t setPolyphonyCalls_ = 0;  // written only on the setup/process thread
    std::uint32_t lastSliceCount_ = 0;
    std::array<EventSlot, kMaxEventsPerBlock> eventOrder_{};
};

// FR-064: unique_ptr ownership keeps the object small; tests still heap-allocate it.
static_assert(sizeof(Processor) < 64u * 1024u, "FR-064: the 808 KB engine must live on the heap");

}  // namespace Vorago
