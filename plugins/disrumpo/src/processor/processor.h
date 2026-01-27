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
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>

namespace Disrumpo {

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
    /// FR-010: Accept stereo only, reject non-stereo arrangements
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;

    // ===========================================================================
    // IComponent
    // ===========================================================================

    /// Save processor state (called by host for project save)
    /// FR-018: Serialize all parameters with version field first
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    /// Restore processor state (called by host for project load)
    /// FR-019, FR-021: Handle version migration and corrupted data
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

private:
    // ==========================================================================
    // Processing State
    // ==========================================================================

    // Sample rate for DSP calculations
    double sampleRate_ = 44100.0;

    // ==========================================================================
    // Parameters (atomic for thread-safe access)
    // Constitution Principle VI: Use std::atomic for simple shared state
    // ==========================================================================

    std::atomic<float> inputGain_{0.5f};   // Default: 0 dB (normalized 0.5)
    std::atomic<float> outputGain_{0.5f};  // Default: 0 dB (normalized 0.5)
    std::atomic<float> globalMix_{1.0f};   // Default: 100% wet
};

} // namespace Disrumpo
