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
#include "dsp/crossover_network.h"
#include "dsp/band_processor.h"
#include "dsp/band_state.h"
#include "dsp/sweep_processor.h"
#include "dsp/custom_curve.h"
#include "dsp/sweep_lfo.h"
#include "dsp/sweep_envelope.h"

#include <krate/dsp/primitives/sweep_position_buffer.h>
#include <krate/dsp/systems/modulation_engine.h>

#include <array>
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

    // ==========================================================================
    // Band Management (spec 002-band-management)
    // FR-001b: Independent L/R channel processing
    // ==========================================================================

    /// @brief Current band count (1-8)
    std::atomic<int> bandCount_{kDefaultBands};

    /// @brief Crossover networks for L/R channels (FR-001b)
    CrossoverNetwork crossoverL_;
    CrossoverNetwork crossoverR_;

    /// @brief Per-band state (gain, pan, solo, bypass, mute)
    std::array<BandState, kMaxBands> bandStates_{};

    /// @brief Per-band processors for gain/pan/mute
    std::array<BandProcessor, kMaxBands> bandProcessors_{};

    /// @brief Crossover frequency targets (normalized, for smoothing)
    std::array<std::atomic<float>, kMaxBands - 1> crossoverFrequencies_{};

    // ==========================================================================
    // Solo State Tracking
    // ==========================================================================

    /// @brief Track if any band has solo enabled
    /// @return true if any band's solo flag is set
    [[nodiscard]] bool isAnySoloed() const noexcept;

    /// @brief Check if a band should contribute to output
    /// FR-025: Solo silences non-soloed bands
    /// FR-025a: Mute overrides solo
    /// @param bandIndex Band to check (0-7)
    /// @return true if band should contribute to output
    [[nodiscard]] bool shouldBandContribute(int bandIndex) const noexcept;

    // ==========================================================================
    // Sweep System (spec 007-sweep-system)
    // FR-001 to FR-022: Frequency-focused distortion intensity
    // ==========================================================================

    /// @brief Sweep processor for per-band intensity calculation
    SweepProcessor sweepProcessor_;

    /// @brief Custom curve for Custom morph link mode
    CustomCurve customCurve_;

    /// @brief Lock-free buffer for audio-UI sweep position synchronization (FR-046)
    Krate::DSP::SweepPositionBuffer sweepPositionBuffer_;

    /// @brief Current sample position for timing synchronization
    uint64_t samplePosition_ = 0;

    // ==========================================================================
    // Sweep Automation (spec 007-sweep-system, FR-024 to FR-029)
    // ==========================================================================

    /// @brief LFO for sweep frequency modulation (FR-024, FR-025)
    SweepLFO sweepLFO_;

    /// @brief Envelope follower for sweep frequency modulation (FR-026, FR-027)
    SweepEnvelope sweepEnvelope_;

    /// @brief Base sweep frequency before modulation (Hz)
    std::atomic<float> baseSweepFrequency_{1000.0f};

    /// @brief Base sweep width before modulation (normalized [0, 1])
    std::atomic<float> baseSweepWidthNorm_{0.286f};

    /// @brief Base sweep intensity before modulation (normalized [0, 1])
    std::atomic<float> baseSweepIntensityNorm_{0.25f};

    // ==========================================================================
    // MIDI Learn (FR-028, FR-029)
    // ==========================================================================

    /// @brief Flag: processor is listening for MIDI CC events
    bool midiLearnActive_ = false;

    /// @brief Assigned MIDI CC number (0-127), or 128 for none
    int assignedMidiCC_ = 128;

    // ==========================================================================
    // Modulation System (spec 008-modulation-system)
    // ==========================================================================

    /// @brief Modulation engine for all modulation sources and routing
    Krate::DSP::ModulationEngine modulationEngine_;

};

} // namespace Disrumpo
