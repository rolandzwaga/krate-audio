#pragma once

// ==============================================================================
// Membrum Processor -- Phase 4
// ==============================================================================
// Phase 4 transforms the processor from a single-template drum synth into a
// 32-pad drum machine. Per-pad parameters are stored in PadConfig[32] inside
// VoicePool. The processor dispatches per-pad parameter changes by computing
// padIndex/offset from the parameter ID. Global proxy IDs (100-252) are
// no-ops in the processor (they are controller-only proxies).
// ==============================================================================

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include "dsp/drum_voice.h"
#include "dsp/pad_config.h"
#include "voice_pool/voice_pool.h"

#include <array>
#include <atomic>

namespace Membrum {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor();

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    /// Load a kit preset blob (v4 format without selectedPadIndex, 9036 bytes).
    /// Does NOT modify selectedPadIndex.
    Steinberg::tresult loadKitPreset(Steinberg::IBStream* stream);
    Steinberg::tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                              Steinberg::Vst::BusDirection dir,
                                              Steinberg::int32 index,
                                              Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;

    // Test-only accessors (Phase 4)
    VoicePool& voicePoolForTest() noexcept { return voicePool_; }
    const VoicePool& voicePoolForTest() const noexcept { return voicePool_; }
    void setSelectedPadIndexForTest(int idx) noexcept { selectedPadIndex_ = idx; }
    int selectedPadIndexForTest() const noexcept { return selectedPadIndex_; }

private:
    void processParameterChanges(Steinberg::Vst::IParameterChanges* paramChanges);
    void processEvents(Steinberg::Vst::IEventList* events);

    // DSP
    VoicePool voicePool_;
    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 2048;

    // ---- Global-only parameters (not per-pad) ----
    std::atomic<int> maxPolyphony_{8};           // FR-111 -- [4, 16]
    std::atomic<int> voiceStealingPolicy_{0};    // FR-120 -- VoiceStealingPolicy int

    // ---- Phase 4: per-pad parameters are stored in VoicePool::padConfigs_ ----
    // No individual atomic<float> fields for material/size/decay etc. anymore.
    // The processor dispatches per-pad parameter changes directly to VoicePool.

    // ---- Phase 4: bus activation tracking ----
    std::array<bool, kMaxOutputBuses> busActive_ = {true};  // bus 0 always active

    // ---- Phase 4: selected pad index (for state only, not used in audio) ----
    int selectedPadIndex_ = 0;
};

} // namespace Membrum
