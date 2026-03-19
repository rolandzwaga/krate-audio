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
// Innexus is an INSTRUMENT:
// - MIDI event input bus
// - Stereo audio output bus (no audio inputs)
// ==============================================================================

#include "plugin_ids.h"
#include "controller/display_data.h"
#include "dsp/sample_analysis.h"
#include "dsp/sample_analyzer.h"
#include "dsp/live_analysis_pipeline.h"
#include "dsp/evolution_engine.h"
#include "dsp/harmonic_blender.h"
#include "dsp/harmonic_modulator.h"
#include "dsp/harmonic_physics.h"
#include "dsp/spectral_decay_envelope.h"

#include <krate/dsp/processors/harmonic_frame_utils.h>
#include <krate/dsp/processors/harmonic_oscillator_bank.h>
#include <krate/dsp/processors/harmonic_snapshot.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_synthesizer.h>
#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/utility/dataexchange.h"

#include <array>
#include <atomic>
#include <cmath>
#include <string>

namespace Innexus {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor();
    ~Processor() override;

    // --- IAudioProcessor ---
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& newSetup) override;
    Steinberg::tresult PLUGIN_API process(
        Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(
        Steinberg::int32 symbolicSampleSize) override;

    // --- IComponent ---
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    // --- IConnectionPoint (DataExchange lifecycle) ---
    Steinberg::tresult PLUGIN_API connect(
        Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API disconnect(
        Steinberg::Vst::IConnectionPoint* other) override;

    // --- IMessage handler (FR-029: JSON import via IMessage) ---
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message) override;

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    // --- MIDI Event Dispatcher Callbacks ---
    // Used by Krate::Plugins::dispatchMidiEvents (midi_event_dispatcher.h)
    void onNoteOn(int16_t pitch, float velocity);
    void onNoteOff(int16_t pitch);
    void onPitchBend(float bipolar);

    // --- Public API for analysis (used by setState to trigger re-analysis) ---
    void loadSample(const std::string& filePath);

    /// @brief Inject a pre-built analysis result (TEST ONLY).
    /// Takes ownership of the pointer. Previous analysis is deleted.
    /// @note This bypasses the background thread for deterministic testing.
    void testInjectAnalysis(SampleAnalysis* analysis)
    {
        auto* old = currentAnalysis_.exchange(analysis, std::memory_order_acq_rel);
        delete old;
    }

    /// @brief Get loaded file path (TEST ONLY, for state round-trip verification).
    const std::string& getLoadedFilePath() const { return loadedFilePath_; }

    /// @brief Get current release time in ms (TEST ONLY, for state round-trip verification).
    float getReleaseTimeMs() const
    {
        return releaseTimeMs_.load(std::memory_order_relaxed);
    }

    /// @brief Get current inharmonicity amount (TEST ONLY, for state round-trip verification).
    float getInharmonicityAmount() const
    {
        return inharmonicityAmount_.load(std::memory_order_relaxed);
    }

    /// @brief Get current master gain (TEST ONLY, for state round-trip verification).
    float getMasterGain() const
    {
        return masterGain_.load(std::memory_order_relaxed);
    }

    /// @brief Get current harmonic level (TEST ONLY).
    float getHarmonicLevel() const
    {
        return harmonicLevel_.load(std::memory_order_relaxed);
    }

    /// @brief Get current residual level (TEST ONLY).
    float getResidualLevel() const
    {
        return residualLevel_.load(std::memory_order_relaxed);
    }

    /// @brief Get current residual brightness (TEST ONLY).
    float getResidualBrightness() const
    {
        return residualBrightness_.load(std::memory_order_relaxed);
    }

    /// @brief Get current transient emphasis (TEST ONLY).
    float getTransientEmphasis() const
    {
        return transientEmphasis_.load(std::memory_order_relaxed);
    }

    /// @brief Get current input source (TEST ONLY).
    float getInputSource() const
    {
        return inputSource_.load(std::memory_order_relaxed);
    }

    /// @brief Get current latency mode (TEST ONLY).
    float getLatencyMode() const
    {
        return latencyMode_.load(std::memory_order_relaxed);
    }

    /// @brief Get current residual frame count from analysis (TEST ONLY).
    /// Returns 0 if no analysis is loaded or analysis has no residual frames.
    size_t getResidualFrameCount() const
    {
        const auto* analysis = currentAnalysis_.load(std::memory_order_acquire);
        if (!analysis) return 0;
        return analysis->residualFrames.size();
    }

    /// @brief Get source crossfade samples remaining (TEST ONLY).
    int getSourceCrossfadeSamplesRemaining() const
    {
        return sourceCrossfadeSamplesRemaining_;
    }

    /// @brief Get source crossfade length in samples (TEST ONLY).
    int getSourceCrossfadeLengthSamples() const
    {
        return sourceCrossfadeLengthSamples_;
    }

    /// @brief Get manual freeze active state (TEST ONLY).
    bool getManualFreezeActive() const { return manualFreezeActive_; }

    /// @brief Get the frozen harmonic frame (TEST ONLY).
    const Krate::DSP::HarmonicFrame& getManualFrozenFrame() const
    {
        return manualFrozenFrame_;
    }

    /// @brief Get the frozen residual frame (TEST ONLY).
    const Krate::DSP::ResidualFrame& getManualFrozenResidualFrame() const
    {
        return manualFrozenResidualFrame_;
    }

    /// @brief Get manual freeze recovery length in samples (TEST ONLY).
    int getManualFreezeRecoveryLengthSamples() const
    {
        return manualFreezeRecoveryLengthSamples_;
    }

    /// @brief Get manual freeze recovery samples remaining (TEST ONLY).
    int getManualFreezeRecoverySamplesRemaining() const
    {
        return manualFreezeRecoverySamplesRemaining_;
    }

    /// @brief Get confidence-gate freeze recovery samples remaining (TEST ONLY).
    int getConfidenceGateFreezeRecoverySamplesRemaining() const
    {
        return freezeRecoverySamplesRemaining_;
    }

    /// @brief Get freeze parameter value (TEST ONLY).
    float getFreeze() const
    {
        return freeze_.load(std::memory_order_relaxed);
    }

    /// @brief Get responsiveness parameter value (TEST ONLY).
    float getResponsiveness() const
    {
        return responsiveness_.load(std::memory_order_relaxed);
    }

    /// @brief Get morph position parameter value (TEST ONLY).
    float getMorphPosition() const
    {
        return morphPosition_.load(std::memory_order_relaxed);
    }

    /// @brief Get current auto-freeze (confidence-gated) state (TEST ONLY).
    bool getAutoFreezeActive() const { return isFrozen_; }

    /// @brief Get morph position smoother (TEST ONLY).
    const Krate::DSP::OnePoleSmoother& getMorphPositionSmoother() const
    {
        return morphPositionSmoother_;
    }

    /// @brief Get the morphed harmonic frame (TEST ONLY).
    const Krate::DSP::HarmonicFrame& getMorphedFrame() const
    {
        return morphedFrame_;
    }

    /// @brief Get the morphed residual frame (TEST ONLY).
    const Krate::DSP::ResidualFrame& getMorphedResidualFrame() const
    {
        return morphedResidualFrame_;
    }

    /// @brief Get current filter type (TEST ONLY).
    int getCurrentFilterType() const { return currentFilterType_; }

    /// @brief Get harmonic filter type from atomic parameter value (TEST ONLY).
    /// Computes the discrete filter type (0-4) from the normalized atomic,
    /// matching the logic in process().
    int getHarmonicFilterTypeFromParam() const
    {
        const float filterNorm = harmonicFilterType_.load(std::memory_order_relaxed);
        return std::clamp(static_cast<int>(std::round(filterNorm * 4.0f)), 0, 4);
    }

    /// @brief Get filter mask array (TEST ONLY).
    const std::array<float, Krate::DSP::kMaxPartials>& getFilterMask() const
    {
        return filterMask_;
    }

    /// @brief Get a memory slot by index (TEST ONLY). Clamps to 0-7.
    const Krate::DSP::MemorySlot& getMemorySlot(int index) const
    {
        return memorySlots_[static_cast<size_t>(std::clamp(index, 0, 7))];
    }

    /// @brief Get selected slot index from normalized parameter (TEST ONLY).
    int getSelectedSlotIndex() const
    {
        float norm = memorySlot_.load(std::memory_order_relaxed);
        return std::clamp(static_cast<int>(std::round(norm * 7.0f)), 0, 7);
    }

    // M6 Test Accessors
    float getTimbralBlend() const { return timbralBlend_.load(std::memory_order_relaxed); }
    float getStereoSpread() const { return stereoSpread_.load(std::memory_order_relaxed); }
    float getEvolutionEnable() const { return evolutionEnable_.load(std::memory_order_relaxed); }
    float getEvolutionSpeed() const { return evolutionSpeed_.load(std::memory_order_relaxed); }
    float getEvolutionDepth() const { return evolutionDepth_.load(std::memory_order_relaxed); }
    float getEvolutionMode() const { return evolutionMode_.load(std::memory_order_relaxed); }
    float getMod1Enable() const { return mod1Enable_.load(std::memory_order_relaxed); }
    float getMod1Waveform() const { return mod1Waveform_.load(std::memory_order_relaxed); }
    float getMod1Rate() const { return mod1Rate_.load(std::memory_order_relaxed); }
    float getMod1Depth() const { return mod1Depth_.load(std::memory_order_relaxed); }
    float getMod1RangeStart() const { return mod1RangeStart_.load(std::memory_order_relaxed); }
    float getMod1RangeEnd() const { return mod1RangeEnd_.load(std::memory_order_relaxed); }
    float getMod1Target() const { return mod1Target_.load(std::memory_order_relaxed); }
    float getMod1RateSync() const { return mod1RateSync_.load(std::memory_order_relaxed); }
    float getMod1NoteValue() const { return mod1NoteValue_.load(std::memory_order_relaxed); }
    float getMod2Enable() const { return mod2Enable_.load(std::memory_order_relaxed); }
    float getMod2Waveform() const { return mod2Waveform_.load(std::memory_order_relaxed); }
    float getMod2Rate() const { return mod2Rate_.load(std::memory_order_relaxed); }
    float getMod2Depth() const { return mod2Depth_.load(std::memory_order_relaxed); }
    float getMod2RangeStart() const { return mod2RangeStart_.load(std::memory_order_relaxed); }
    float getMod2RangeEnd() const { return mod2RangeEnd_.load(std::memory_order_relaxed); }
    float getMod2Target() const { return mod2Target_.load(std::memory_order_relaxed); }
    float getMod2RateSync() const { return mod2RateSync_.load(std::memory_order_relaxed); }
    float getMod2NoteValue() const { return mod2NoteValue_.load(std::memory_order_relaxed); }
    float getDetuneSpread() const { return detuneSpread_.load(std::memory_order_relaxed); }
    float getBlendEnable() const { return blendEnable_.load(std::memory_order_relaxed); }
    float getBlendSlotWeight(int index) const
    {
        return blendSlotWeights_[static_cast<size_t>(std::clamp(index, 0, 7))].load(std::memory_order_relaxed);
    }
    float getBlendLiveWeight() const { return blendLiveWeight_.load(std::memory_order_relaxed); }

    // Harmonic Physics (Spec A) test accessors
    float getWarmth() const { return warmth_.load(std::memory_order_relaxed); }
    float getCoupling() const { return coupling_.load(std::memory_order_relaxed); }
    float getStability() const { return stability_.load(std::memory_order_relaxed); }
    float getEntropy() const { return entropy_.load(std::memory_order_relaxed); }

    // ADSR Envelope (Spec 124) test accessors
    float getAdsrAttackMs() const { return adsrAttackMs_.load(std::memory_order_relaxed); }
    float getAdsrDecayMs() const { return adsrDecayMs_.load(std::memory_order_relaxed); }
    float getAdsrSustainLevel() const { return adsrSustainLevel_.load(std::memory_order_relaxed); }
    float getAdsrReleaseMs() const { return adsrReleaseMs_.load(std::memory_order_relaxed); }
    float getAdsrAmount() const { return adsrAmount_.load(std::memory_order_relaxed); }
    float getAdsrTimeScale() const { return adsrTimeScale_.load(std::memory_order_relaxed); }
    float getAdsrAttackCurve() const { return adsrAttackCurve_.load(std::memory_order_relaxed); }
    float getAdsrDecayCurve() const { return adsrDecayCurve_.load(std::memory_order_relaxed); }
    float getAdsrReleaseCurve() const { return adsrReleaseCurve_.load(std::memory_order_relaxed); }

    // Partial Count test accessor
    float getPartialCount() const { return partialCount_.load(std::memory_order_relaxed); }

    /// @brief Get active partial count from parameter (TEST ONLY).
    /// Denormalizes to {48, 64, 80, 96}.
    int getActivePartialCount() const
    {
        const float norm = partialCount_.load(std::memory_order_relaxed);
        constexpr int kCounts[] = {48, 64, 80, 96};
        int idx = std::clamp(static_cast<int>(std::round(norm * 3.0f)), 0, 3);
        return kCounts[idx];
    }

    // ADSR Playback State (Spec 124: T048) test accessors
    float getAdsrEnvelopeOutput() const { return adsrEnvelopeOutput_.load(std::memory_order_relaxed); }
    int getAdsrStage() const { return adsrStage_.load(std::memory_order_relaxed); }
    bool getAdsrActive() const { return adsrActive_.load(std::memory_order_relaxed); }

    // Analysis Feedback Loop (Spec B) test accessors
    float getFeedbackAmount() const { return feedbackAmount_.load(std::memory_order_relaxed); }
    float getFeedbackDecay() const { return feedbackDecay_.load(std::memory_order_relaxed); }

    /// @brief Get const reference to the feedback buffer (TEST ONLY, for SC-006 verification).
    const std::array<float, 8192>& getFeedbackBuffer() const { return feedbackBuffer_; }

    /// @brief Send display data to controller via IMessage.
    /// Called at end of process() when output is produced.
    /// RT-Safety Note: allocateMessage() is called on the audio thread --
    /// this is the standard VST3 IMessage pattern used throughout the codebase.
    void sendDisplayData(Steinberg::Vst::ProcessData& data);


private:
    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void processEvents(Steinberg::Vst::IEventList* events);
    void handleNoteOn(int noteNumber, float velocity);
    void handleNoteOff();
    void handlePitchBend(float bendSemitones);
    void updateReleaseDecayCoeff();
    void checkForNewAnalysis();
    void cleanupPendingDeletion();
    void applyModulatorAmplitude(bool mod1Enabled, bool mod2Enabled);
    void applyHarmonicPhysics() noexcept;

    // =========================================================================
    // Parameter Atomics
    // =========================================================================
    std::atomic<float> bypass_{0.0f};
    std::atomic<float> masterGain_{1.0f};
    std::atomic<float> releaseTimeMs_{100.0f};
    std::atomic<float> inharmonicityAmount_{1.0f};
    std::atomic<float> partialCount_{0.0f};          // normalized: 0=48, 1/3=64, 2/3=80, 1=96

    // M2 Residual parameters (FR-021, FR-022, FR-023)
    std::atomic<float> harmonicLevel_{0.5f};       // normalized, default = 1.0 plain
    std::atomic<float> residualLevel_{0.5f};       // normalized, default = 1.0 plain
    std::atomic<float> residualBrightness_{0.5f};  // normalized, default = 0.0 plain = neutral
    std::atomic<float> transientEmphasis_{0.0f};   // normalized, default = 0.0 plain

    // M3 Sidechain parameters (FR-002, FR-004)
    std::atomic<float> inputSource_{0.0f};         // 0.0 = Sample, 1.0 = Sidechain
    std::atomic<float> latencyMode_{0.0f};         // 0.0 = LowLatency, 1.0 = HighPrecision

    // M4 Musical Control parameters (FR-001, FR-010, FR-019, FR-029)
    std::atomic<float> freeze_{0.0f};              // 0.0 = off, 1.0 = on
    std::atomic<float> morphPosition_{0.0f};       // 0.0 to 1.0
    std::atomic<float> harmonicFilterType_{0.0f};  // normalized (0-4 mapped)
    std::atomic<float> responsiveness_{0.5f};      // 0.0 to 1.0

    // M5 Harmonic Memory parameters (FR-005, FR-006, FR-011)
    std::atomic<float> memorySlot_{0.0f};             // normalized 0-1, denorm to slot 0-7
    std::atomic<float> memoryCapture_{0.0f};          // momentary trigger
    std::atomic<float> memoryRecall_{0.0f};           // momentary trigger
    float previousCaptureTrigger_ = 0.0f;
    float previousRecallTrigger_ = 0.0f;

    // M6 Creative Extensions parameters (FR-043)
    // Cross-Synthesis
    std::atomic<float> timbralBlend_{1.0f};           // 0.0-1.0, default 1.0
    // Stereo Output
    std::atomic<float> stereoSpread_{0.0f};           // 0.0-1.0, default 0.0
    // Evolution Engine
    std::atomic<float> evolutionEnable_{0.0f};        // 0/1, default 0
    std::atomic<float> evolutionSpeed_{0.0f};         // normalized, plain 0.01-10.0 Hz
    std::atomic<float> evolutionDepth_{0.5f};         // 0.0-1.0, default 0.5
    std::atomic<float> evolutionMode_{0.0f};          // normalized, 0-2
    // Modulator 1
    std::atomic<float> mod1Enable_{0.0f};             // 0/1
    std::atomic<float> mod1Waveform_{0.0f};           // normalized, 0-4
    std::atomic<float> mod1Rate_{0.0f};               // normalized, plain 0.01-20.0 Hz
    std::atomic<float> mod1Depth_{0.0f};              // 0.0-1.0
    std::atomic<float> mod1RangeStart_{0.0f};         // normalized, plain 1-96
    std::atomic<float> mod1RangeEnd_{1.0f};           // normalized, plain 1-96
    std::atomic<float> mod1Target_{0.0f};             // normalized, 0-2
    std::atomic<float> mod1RateSync_{1.0f};            // 0/1, default 1 (synced)
    std::atomic<float> mod1NoteValue_{0.5f};           // normalized, default index 10/20=0.5
    // Modulator 2
    std::atomic<float> mod2Enable_{0.0f};             // 0/1
    std::atomic<float> mod2Waveform_{0.0f};           // normalized, 0-4
    std::atomic<float> mod2Rate_{0.0f};               // normalized, plain 0.01-20.0 Hz
    std::atomic<float> mod2Depth_{0.0f};              // 0.0-1.0
    std::atomic<float> mod2RangeStart_{0.0f};         // normalized, plain 1-96
    std::atomic<float> mod2RangeEnd_{1.0f};           // normalized, plain 1-96
    std::atomic<float> mod2Target_{0.0f};             // normalized, 0-2
    std::atomic<float> mod2RateSync_{1.0f};            // 0/1, default 1 (synced)
    std::atomic<float> mod2NoteValue_{0.5f};           // normalized, default index 10/20=0.5
    // Detune
    std::atomic<float> detuneSpread_{0.0f};           // 0.0-1.0
    // Multi-Source Blend
    std::atomic<float> blendEnable_{0.0f};            // 0/1
    std::array<std::atomic<float>, 8> blendSlotWeights_{}; // each 0.0-1.0
    std::atomic<float> blendLiveWeight_{0.0f};        // 0.0-1.0

    // Harmonic Physics parameters (Spec A: 122-harmonic-physics)
    std::atomic<float> warmth_{0.0f};                 // 0.0-1.0, default 0.0
    std::atomic<float> coupling_{0.0f};               // 0.0-1.0, default 0.0
    std::atomic<float> stability_{0.0f};              // 0.0-1.0, default 0.0
    std::atomic<float> entropy_{0.0f};                // 0.0-1.0, default 0.0

    // Analysis Feedback Loop parameters (Spec B: 123-analysis-feedback-loop)
    std::atomic<float> feedbackAmount_{0.0f};         // 0.0-1.0, default 0.0
    std::atomic<float> feedbackDecay_{0.2f};          // 0.0-1.0, default 0.2

    // ADSR Envelope parameters (Spec 124: 124-adsr-envelope-detection)
    std::atomic<float> adsrAttackMs_{10.0f};          // plain ms, default 10ms
    std::atomic<float> adsrDecayMs_{100.0f};          // plain ms, default 100ms
    std::atomic<float> adsrSustainLevel_{1.0f};       // 0.0-1.0, default 1.0
    std::atomic<float> adsrReleaseMs_{100.0f};        // plain ms, default 100ms
    std::atomic<float> adsrAmount_{0.0f};             // 0.0-1.0, default 0.0 (bypass)
    std::atomic<float> adsrTimeScale_{1.0f};          // 0.25-4.0, default 1.0
    std::atomic<float> adsrAttackCurve_{0.0f};        // -1.0 to +1.0, default 0.0
    std::atomic<float> adsrDecayCurve_{0.0f};         // -1.0 to +1.0, default 0.0
    std::atomic<float> adsrReleaseCurve_{0.0f};       // -1.0 to +1.0, default 0.0

    // =========================================================================
    // DSP Members (T081)
    // =========================================================================
    Krate::DSP::HarmonicOscillatorBank oscillatorBank_;
    Krate::DSP::ResidualSynthesizer residualSynth_;

    // Parameter smoothers (FR-025)
    Krate::DSP::OnePoleSmoother harmonicLevelSmoother_;
    Krate::DSP::OnePoleSmoother residualLevelSmoother_;
    Krate::DSP::OnePoleSmoother brightnessSmoother_;
    Krate::DSP::OnePoleSmoother transientEmphasisSmoother_;

    // M6 Parameter smoothers
    Krate::DSP::OnePoleSmoother timbralBlendSmoother_;      // 5ms (FR-005)
    Krate::DSP::OnePoleSmoother stereoSpreadSmoother_;      // 10ms (FR-011)
    Krate::DSP::OnePoleSmoother evolutionSpeedSmoother_;    // 5ms (FR-023)
    Krate::DSP::OnePoleSmoother evolutionDepthSmoother_;    // 5ms (FR-023)
    Krate::DSP::OnePoleSmoother mod1RateSmoother_;          // 5ms (FR-033)
    Krate::DSP::OnePoleSmoother mod1DepthSmoother_;         // 5ms (FR-033)
    Krate::DSP::OnePoleSmoother mod2RateSmoother_;          // 5ms (FR-033)
    Krate::DSP::OnePoleSmoother mod2DepthSmoother_;         // 5ms (FR-033)
    Krate::DSP::OnePoleSmoother detuneSpreadSmoother_;      // 5ms (FR-033)
    std::array<Krate::DSP::OnePoleSmoother, 9> blendWeightSmootherArray_; // 5ms (FR-041): [0-7]=slots, [8]=live

    // Harmonic Physics smoothers (Spec A: 122-harmonic-physics)
    Krate::DSP::OnePoleSmoother warmthSmoother_;
    Krate::DSP::OnePoleSmoother couplingSmoother_;
    Krate::DSP::OnePoleSmoother stabilitySmoother_;
    Krate::DSP::OnePoleSmoother entropySmoother_;

    // ADSR Envelope (Spec 124: 124-adsr-envelope-detection)
    Krate::DSP::ADSREnvelope adsr_;
    Krate::DSP::OnePoleSmoother adsrAmountSmoother_;

    // ADSR Playback State atomics (Spec 124: T048)
    // Updated per-block in process() for ADSRDisplay playback dot visualization.
    std::atomic<float> adsrEnvelopeOutput_{0.0f};
    std::atomic<int> adsrStage_{0};
    std::atomic<bool> adsrActive_{false};

    /// One-shot flag: send ADSR playback state pointers to controller (T049)
    bool adsrPlaybackPtrsSent_ = false;

    /// Atomic pointer for lock-free analysis transfer (FR-058)
    std::atomic<SampleAnalysis*> currentAnalysis_{nullptr};

    /// Deferred deletion: old analysis pointer flagged for cleanup.
    /// Deletion happens in destructor/terminate, never on audio thread (SC-010).
    SampleAnalysis* pendingDeletion_ = nullptr;

    /// Background analysis engine
    SampleAnalyzer sampleAnalyzer_;

    // =========================================================================
    // Voice State
    // =========================================================================
    bool noteActive_ = false;
    int currentMidiNote_ = -1;
    float velocityGain_ = 1.0f;
    float pitchBendSemitones_ = 0.0f;

    // =========================================================================
    // Frame Advancement (FR-047)
    // =========================================================================
    size_t currentFrameIndex_ = 0;
    size_t frameSampleCounter_ = 0;

    // Sustain loop region (Spec 124): frames loop within [sustainLoopStart_, sustainLoopEnd_)
    // when ADSR is in Sustain stage. Set from DetectedADSR on sample load.
    int sustainLoopStart_ = 0;
    int sustainLoopEnd_ = 0;

    // =========================================================================
    // Release Envelope (FR-049, FR-057)
    // =========================================================================
    float releaseGain_ = 1.0f;
    bool inRelease_ = false;
    bool inAdsrRelease_ = false;  // ADSR handles release (skip old FR-049 release)
    float releaseDecayCoeff_ = 0.0f;

    // =========================================================================
    // Anti-Click Voice Steal Crossfade (FR-054)
    // =========================================================================
    int antiClickSamplesRemaining_ = 0;
    int antiClickLengthSamples_ = 0;
    float antiClickOldLevel_ = 0.0f;

    // =========================================================================
    // Confidence-Gated Freeze (FR-052, FR-053)
    // =========================================================================
    static constexpr float kConfidenceThreshold = 0.3f;
    static constexpr float kConfidenceHysteresis = 0.05f;
    /// During spectral decay, new frame amplitude must exceed this fraction
    /// of the original frozen frame amplitude to allow recovery.
    /// Prevents noise from interrupting the fade-out.
    static constexpr float kDecayRecoveryAmplitudeRatio = 0.1f;

    /// Last known-good frame for freeze
    Krate::DSP::HarmonicFrame lastGoodFrame_{};
    bool isFrozen_ = false;

    /// Crossfade from frozen to live (FR-053)
    int freezeRecoverySamplesRemaining_ = 0;
    int freezeRecoveryLengthSamples_ = 0;
    float freezeRecoveryOldLevel_ = 0.0f;
    static constexpr float kFreezeRecoveryTimeSec = 0.007f; // 7ms default

    /// Per-partial spectral decay for natural fade-out when confidence gate freezes
    SpectralDecayEnvelope spectralDecay_;

    // =========================================================================
    // Pitch Bend (FR-051)
    // =========================================================================
    static constexpr float kPitchBendRangeSemitones = 12.0f; // +/- 12 semitones

    // =========================================================================
    // Anti-Click (FR-057)
    // =========================================================================
    static constexpr float kMinAntiClickMs = 20.0f; // 20ms minimum
    static constexpr float kAntiClickTimeSec = 0.020f; // 20ms crossfade for voice steal

    // =========================================================================
    // Sidechain Routing (FR-001, FR-011, FR-014)
    // =========================================================================
    /// Pre-allocated stereo-to-mono downmix buffer (max 8192 samples)
    std::array<float, 8192> sidechainBuffer_{};

    /// Crossfade state for input source switching (FR-011)
    int sourceCrossfadeSamplesRemaining_ = 0;
    int sourceCrossfadeLengthSamples_ = 0;
    float sourceCrossfadeOldLevel_ = 0.0f;

    /// Tracks previous input source to detect switches
    int previousInputSource_ = 0; // 0 = Sample

    // =========================================================================
    // Analysis Feedback Loop (Spec B: FR-005, FR-006, FR-016)
    // =========================================================================
    /// Pre-allocated mono feedback buffer sized to max block size (same as sidechainBuffer_)
    std::array<float, 8192> feedbackBuffer_{};

    /// Tracks previous freeze state for feedback buffer clear on disengage (FR-016)
    bool previousFreezeForFeedback_ = false;

    // =========================================================================
    // Live Analysis Pipeline (FR-003, FR-005, FR-008, FR-009)
    // =========================================================================
    LiveAnalysisPipeline liveAnalysis_;

    /// Latest harmonic frame from live analysis
    Krate::DSP::HarmonicFrame currentLiveFrame_{};

    /// Latest residual frame from live analysis
    Krate::DSP::ResidualFrame currentLiveResidualFrame_{};

    /// Latest polyphonic frame from live analysis (for multi-voice display)
    Krate::DSP::PolyphonicFrame currentLivePolyFrame_{};

    // =========================================================================
    // Manual Freeze State (M4: FR-002, FR-003, FR-007)
    // =========================================================================
    bool manualFreezeActive_ = false;
    Krate::DSP::HarmonicFrame manualFrozenFrame_{};
    Krate::DSP::ResidualFrame manualFrozenResidualFrame_{};

    /// Crossfade from frozen to live when manual freeze is disengaged (FR-006)
    int manualFreezeRecoverySamplesRemaining_ = 0;
    int manualFreezeRecoveryLengthSamples_ = 0;
    float manualFreezeRecoveryOldLevel_ = 0.0f;
    static constexpr float kManualFreezeRecoveryTimeSec = 0.010f; // 10ms

    /// Tracks the previous freeze parameter value to detect transitions
    bool previousFreezeState_ = false;

    // =========================================================================
    // Morph Interpolation (M4: FR-010 to FR-018)
    // =========================================================================
    Krate::DSP::OnePoleSmoother morphPositionSmoother_{};
    Krate::DSP::HarmonicFrame morphedFrame_{};
    Krate::DSP::ResidualFrame morphedResidualFrame_{};

    // =========================================================================
    // Harmonic Filter (M4: FR-019 to FR-028)
    // =========================================================================
    std::array<float, Krate::DSP::kMaxPartials> filterMask_{};
    int currentFilterType_ = 0;

    // =========================================================================
    // Harmonic Memory Slots (M5: FR-010)
    // =========================================================================
    std::array<Krate::DSP::MemorySlot, 8> memorySlots_{};

    // =========================================================================
    // M6: Pure Harmonic Reference (FR-004, R-004)
    // =========================================================================
    Krate::DSP::HarmonicFrame pureHarmonicFrame_{};

    // =========================================================================
    // M6: Evolution Engine (FR-014 to FR-023)
    // =========================================================================
    EvolutionEngine evolutionEngine_;

    // =========================================================================
    // M6: Harmonic Modulators (FR-024 to FR-029, FR-051)
    // =========================================================================
    HarmonicModulator mod1_;
    HarmonicModulator mod2_;

    // =========================================================================
    // M6: Harmonic Blender (FR-034 to FR-042)
    // =========================================================================
    HarmonicBlender harmonicBlender_;

    // =========================================================================
    // Harmonic Physics (Spec A: 122-harmonic-physics)
    // =========================================================================
    HarmonicPhysics harmonicPhysics_;

    // =========================================================================
    // Display Data (M7: FR-048)
    // =========================================================================
    std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchange_;
    DisplayData displayDataBuffer_{}; // Processor-side buffer, no atomic needed
    uint32_t displayFrameCounter_ = 0; // Monotonic counter across all sends
    int displaySendIntervalSamples_ = 0; // Throttle: samples between sends (~30Hz)
    int displaySendAccumulatorSamples_ = 0;

    // =========================================================================
    // Processing State
    // =========================================================================
    double sampleRate_ = 44100.0;
    double tempoBPM_ = 120.0;   // host tempo for modulator sync
    std::string loadedFilePath_; // for state persistence (FR-056)
};

} // namespace Innexus
