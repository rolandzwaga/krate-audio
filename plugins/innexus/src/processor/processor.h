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
#include "dsp/sample_analysis.h"
#include "dsp/sample_analyzer.h"
#include "dsp/live_analysis_pipeline.h"

#include <krate/dsp/processors/harmonic_oscillator_bank.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_synthesizer.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>

#include "public.sdk/source/vst/vstaudioeffect.h"

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

private:
    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void processEvents(Steinberg::Vst::IEventList* events);
    void handleNoteOn(int noteNumber, float velocity);
    void handleNoteOff();
    void handlePitchBend(float bendSemitones);
    void updateReleaseDecayCoeff();
    void checkForNewAnalysis();
    void cleanupPendingDeletion();

    // =========================================================================
    // Parameter Atomics
    // =========================================================================
    std::atomic<float> bypass_{0.0f};
    std::atomic<float> masterGain_{1.0f};
    std::atomic<float> releaseTimeMs_{100.0f};
    std::atomic<float> inharmonicityAmount_{1.0f};

    // M2 Residual parameters (FR-021, FR-022, FR-023)
    std::atomic<float> harmonicLevel_{0.5f};       // normalized, default = 1.0 plain
    std::atomic<float> residualLevel_{0.5f};       // normalized, default = 1.0 plain
    std::atomic<float> residualBrightness_{0.5f};  // normalized, default = 0.0 plain = neutral
    std::atomic<float> transientEmphasis_{0.0f};   // normalized, default = 0.0 plain

    // M3 Sidechain parameters (FR-002, FR-004)
    std::atomic<float> inputSource_{0.0f};         // 0.0 = Sample, 1.0 = Sidechain
    std::atomic<float> latencyMode_{0.0f};         // 0.0 = LowLatency, 1.0 = HighPrecision

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

    // =========================================================================
    // Release Envelope (FR-049, FR-057)
    // =========================================================================
    float releaseGain_ = 1.0f;
    bool inRelease_ = false;
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

    /// Last known-good frame for freeze
    Krate::DSP::HarmonicFrame lastGoodFrame_{};
    bool isFrozen_ = false;

    /// Crossfade from frozen to live (FR-053)
    int freezeRecoverySamplesRemaining_ = 0;
    int freezeRecoveryLengthSamples_ = 0;
    float freezeRecoveryOldLevel_ = 0.0f;
    static constexpr float kFreezeRecoveryTimeSec = 0.007f; // 7ms default

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
    // Live Analysis Pipeline (FR-003, FR-005, FR-008, FR-009)
    // =========================================================================
    LiveAnalysisPipeline liveAnalysis_;

    /// Latest harmonic frame from live analysis
    Krate::DSP::HarmonicFrame currentLiveFrame_{};

    /// Latest residual frame from live analysis
    Krate::DSP::ResidualFrame currentLiveResidualFrame_{};

    // =========================================================================
    // Processing State
    // =========================================================================
    double sampleRate_ = 44100.0;
    std::string loadedFilePath_; // for state persistence (FR-056)
};

} // namespace Innexus
