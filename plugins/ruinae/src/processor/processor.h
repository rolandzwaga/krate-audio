#pragma once

// ==============================================================================
// Audio Processor
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation
// - This is the Processor component (IAudioProcessor + IComponent)
// - MUST be completely separate from Controller
// - MUST function without Controller instantiation
//
// Constitution Principle II: Real-Time Audio Thread Safety
// - NEVER allocate memory in process()
// - NEVER use locks/mutexes
// - Pre-allocate ALL buffers in setupProcessing()
//
// Ruinae is a SYNTHESIZER (Instrument):
// - No audio input bus
// - MIDI event input bus
// - Stereo audio output bus
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>
#include <vector>

namespace Ruinae {

// ==============================================================================
// Processor Class
// ==============================================================================

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override = default;

    // ===========================================================================
    // IPluginBase
    // ===========================================================================

    /// Called when the plugin is first loaded
    Steinberg::tresult PLUGIN_API initialize(FUnknown* context) override;

    /// Called when the plugin is unloaded
    Steinberg::tresult PLUGIN_API terminate() override;

    // ===========================================================================
    // IAudioProcessor
    // ===========================================================================

    /// Called before processing starts - allocate ALL buffers here
    /// Constitution Principle II: Pre-allocate everything in this method
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup) override;

    /// Called when audio processing starts/stops
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;

    /// Main audio processing callback
    /// Constitution Principle II: NO allocations, NO locks, NO exceptions
    Steinberg::tresult PLUGIN_API process(
        Steinberg::Vst::ProcessData& data) override;

    /// Report audio I/O configuration support
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;

    // ===========================================================================
    // IComponent
    // ===========================================================================

    /// Save processor state (called by host for project save)
    /// Constitution Principle I: State sync via setComponentState()
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    /// Restore processor state (called by host for project load)
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;

    // ===========================================================================
    // Factory
    // ===========================================================================

    static FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

protected:
    // ==========================================================================
    // Parameter Handling
    // ==========================================================================

    /// Process parameter changes from the input queue
    /// Called at the start of each process() call
    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes);

    /// Process MIDI events (noteOn, noteOff, etc.)
    void processEvents(Steinberg::Vst::IEventList* events);

private:
    // ==========================================================================
    // Processing State
    // ==========================================================================

    double sampleRate_ = 44100.0;
    Steinberg::int32 maxBlockSize_ = 0;

    // ==========================================================================
    // Parameters (atomic for thread-safe access)
    // Constitution Principle VI: Use std::atomic for simple shared state
    // ==========================================================================

    std::atomic<float> masterGain_{1.0f};
    std::atomic<int> voiceMode_{0};      // 0=Poly, 1=Mono
    std::atomic<int> polyphony_{8};      // 1-16
    std::atomic<bool> softLimit_{true};

    // ==========================================================================
    // DSP Engine
    // ==========================================================================
    // TODO: Add RuinaeEngine when implemented (Phase 6)
    // Krate::DSP::RuinaeEngine engine_;

    // ==========================================================================
    // Scratch Buffers (pre-allocated in setupProcessing)
    // ==========================================================================
    std::vector<float> mixBufferL_;
    std::vector<float> mixBufferR_;
};

} // namespace Ruinae
