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
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "dsp/crossover_network.h"
#include "dsp/band_processor.h"
#include "dsp/band_state.h"
#include "dsp/sweep_processor.h"
#include "dsp/custom_curve.h"
#include "dsp/sweep_lfo.h"
#include "dsp/sweep_envelope.h"
#include "controller/spectrum_block.h"
#include "shared_display.h"

#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/one_pole_allpass.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/spectrum_fifo.h>
#include <krate/dsp/primitives/sweep_position_buffer.h>
#include <krate/dsp/systems/modulation_engine.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Steinberg {
class IBStreamer;
}  // namespace Steinberg

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

    /// Latency the processor delays its output by (D-026, T2.10): the worst case the
    /// current discrete settings allow (see the latency section below). Any thread.
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;

    /// kInfiniteTail while a ringing type is active, else kNoTail (D-108). Any thread.
    Steinberg::uint32 PLUGIN_API getTailSamples() override;

    // ===========================================================================
    // IConnectionPoint (DataExchange lifecycle)
    // ===========================================================================

    Steinberg::tresult PLUGIN_API connect(
        Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API disconnect(
        Steinberg::Vst::IConnectionPoint* other) override;

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

    /// Maps a node's normalized shape slots onto the type-specific DistortionParams
    /// fields of `type` (processor_params.cpp). Used by processParameterChanges and,
    /// so a host restore yields the same params as replaying the parameters, by setState.
    static void mapShapeSlotsToParams(DistortionType type, const float* slots,
                                      DistortionParams& p) noexcept;

    // Test accessors
    [[nodiscard]] const std::array<float, kMaxMorphNodes>& getMorphWeightsForBand(int band) const;
    [[nodiscard]] float getMorphCacheX(int band) const;
    [[nodiscard]] float getMorphCacheY(int band) const;
    [[nodiscard]] int getMorphCacheActiveNodes(int band) const;
    [[nodiscard]] const BandProcessor& getBandProcessor(int band) const { return bandProcessors_[band]; }
    [[nodiscard]] const SweepProcessor& getSweepProcessor() const { return sweepProcessor_; }
    /// Morph X the band was last handed (sweep link and mod offsets applied)
    [[nodiscard]] float debugMorphPositionX(int band) const { return bandProcessors_[band].getMorphTargetX(); }

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

    /// @brief Per-sample smoothing of the block-rate global targets (D-025): linear
    /// input gain, linear output gain, wet mix. Configured in setupProcessing; the
    /// first audio block after activation snaps them to their targets.
    static constexpr float kGlobalSmoothingMs = 15.0f;
    Krate::DSP::OnePoleSmoother inputGainSmoother_;
    Krate::DSP::OnePoleSmoother outputGainSmoother_;
    Krate::DSP::OnePoleSmoother wetMixSmoother_;

    // ==========================================================================
    // Band Management (spec 002-band-management)
    // FR-001b: Independent L/R channel processing
    // ==========================================================================

    /// @brief Current band count (1-4)
    std::atomic<int> bandCount_{kDefaultBands};

    /// @brief Crossover networks for L/R channels (FR-001b)
    CrossoverNetwork crossoverL_;
    CrossoverNetwork crossoverR_;

    /// @brief Per-band state (gain, pan, solo, bypass, mute)
    std::array<BandState, kMaxBands> bandStates_{};

    /// @brief Per-band morph node cache for incremental parameter updates.
    /// When a single node parameter changes, the Processor updates the
    /// cached node and pushes the entire array to BandProcessor.
    struct BandMorphCache {
        std::array<MorphNode, kMaxMorphNodes> nodes;
        int activeNodeCount = kDefaultActiveNodes;
        float morphX = 0.5f;
        float morphY = 0.5f;
        MorphMode morphMode = MorphMode::Linear1D;
        float morphSmoothingMs = 0.0f;
        /// Per-node per-type shadow storage for shape slot isolation
        std::array<ShapeShadowStorage, kMaxMorphNodes> shapeShadow;
    };
    std::array<BandMorphCache, kMaxBands> bandMorphCache_{};

    /// @brief Per-band processors for gain/pan/mute
    std::array<BandProcessor, kMaxBands> bandProcessors_{};

    /// @brief Per-band block buffers: the crossover writes each band here, then each
    /// BandProcessor processes its buffer as a block (oversampling, bypass). Host
    /// blocks longer than BandProcessor::kMaxBlockSize are processed in chunks.
    using BandBlock = std::array<float, BandProcessor::kMaxBlockSize>;
    std::array<BandBlock, kMaxBands> bandBufL_{};
    std::array<BandBlock, kMaxBands> bandBufR_{};

    // ==========================================================================
    // Band-count change dip (D-023): bandCount_ is the requested count, the
    // crossovers run the current one. A change ramps the output (dry and wet) to
    // 0 over kBandCountFadeMs at the running topology, switches crossovers and
    // bands at the silent point, then ramps back to 1. Before any audio since
    // activation the switch happens at once. A latency change (T2.10) takes the
    // same dip: the compensation delays change at the silent point. Audio thread only.
    // ==========================================================================

    static constexpr float kBandCountFadeMs = 5.0f;
    enum class BandCountFade : uint8_t { Idle, FadeOut, FadeIn };
    BandCountFade bandCountFade_ = BandCountFade::Idle;
    size_t bandCountFadePos_ = 0;      ///< samples into the current ramp
    size_t bandCountFadeSamples_ = 1;  ///< ramp length at the host rate
    bool processedSinceActivate_ = false;

    /// @brief Switch crossovers and bands to `bands`; bands that join start reset.
    void switchBandCount(int bands) noexcept;

    /// @brief A ramp completed: at the bottom switch, at the top finish or dip again.
    void advanceBandCountFade() noexcept;

    /// @brief A band-count or latency change is owed (the dip's reason to run).
    [[nodiscard]] bool topologyChangePending() const noexcept;

    // ==========================================================================
    // Latency and tail (D-026, D-081, D-098, D-108, follow-up 5; fix plan T2.10)
    // The reported latency is the largest BandProcessor::getWorstCaseLatency() of
    // the requested bands: it depends on node types, Spectral FFT sizes, Oversample
    // Max and the band count, never on morph, sweep, modulation or drive. Every band
    // and the dry path are delayed to the applied latency. A change of the target
    // is applied at the silent bottom of the band-count dip (or at once before any
    // audio since activation), and process() writes it to kLatencyOutputId; the
    // controller answers with restartComponent(kLatencyChanged) on its thread.
    // ==========================================================================

    int targetLatency_ = 0;           ///< worst case of the current settings (audio thread)
    int appliedLatency_ = 0;          ///< what bands and dry path are delayed to (audio thread)
    int lastSignalledLatency_ = -1;   ///< last value written to kLatencyOutputId (audio thread)
    std::atomic<int> reportedLatency_{0};  ///< getLatencySamples()
    std::atomic<Steinberg::uint32> tailSamples_{Steinberg::Vst::kNoTail};  ///< getTailSamples()
    Krate::DSP::DelayLine dryDelayL_;  ///< dry path, read at appliedLatency_
    Krate::DSP::DelayLine dryDelayR_;

    // Minimax alignment context (follow-up 81, user rulings 2026-11-09): whether any requested
    // band can run at 4x (it selects the 2x bands' offset), and the reference shift every path
    // takes when a band's minimax target would need a negative wait (ADAA1 at 44.1 / 48 kHz).
    // Applied with the latency, at the silent bottom of the dip.
    bool targetFourX_ = false;
    bool appliedFourX_ = false;
    double targetShift_ = 0.0;
    double appliedShift_ = 0.0;
    Krate::DSP::OnePoleAllpass dryAllpassL_;  ///< dry path's fraction when appliedShift_ > 0
    Krate::DSP::OnePoleAllpass dryAllpassR_;

    /// @brief Recompute targetLatency_, reportedLatency_ and tailSamples_ from the settings.
    void updateLatencyAndTail() noexcept;

    /// @brief Delay every band and the dry path to @p samples (and the alignment target) from now on.
    void applyLatency(int samples) noexcept;

    /// @brief The latency or the alignment context differs from what is applied.
    [[nodiscard]] bool latencyChangePending() const noexcept {
        return targetLatency_ != appliedLatency_ || targetFourX_ != appliedFourX_ || targetShift_ != appliedShift_;
    }

    // ==========================================================================
    // Solo State Tracking
    // ==========================================================================

    /// @brief Track if any band has solo enabled
    /// @return true if any band's solo flag is set
    [[nodiscard]] bool isAnySoloed() const noexcept;

    /// @brief Check if a band should contribute to output
    /// FR-025: Solo silences non-soloed bands
    /// FR-025a: Mute overrides solo
    /// @param bandIndex Band to check (0-3)
    /// @return true if band should contribute to output
    [[nodiscard]] bool shouldBandContribute(int bandIndex) const noexcept;

    // ==========================================================================
    // Sweep System (spec 007-sweep-system)
    // FR-001 to FR-022: Frequency-focused distortion intensity
    // ==========================================================================

    /// @brief Sweep processor for per-band intensity calculation
    SweepProcessor sweepProcessor_;

    /// @brief Custom curve for Custom morph link mode, evaluated by the sweep -> morph
    /// link in process(). Restored from state; the custom-curve editor view is still
    /// unregistered (D-069, T2.22), so it keeps its saved or default breakpoints.
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
    // Modulation System (spec 008-modulation-system)
    // ==========================================================================

    /// @brief Modulation engine for all modulation sources and routing
    Krate::DSP::ModulationEngine modulationEngine_;

    // ==========================================================================
    // Spectrum Analyzer DataExchange (audio -> UI data transfer)
    // ==========================================================================

    /// @brief DataExchange handler for sending audio samples to controller
    std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchange_;

    /// @brief Pre-allocated buffer for building SpectrumBlock on audio thread
    SpectrumBlock spectrumBlockBuffer_{};

    /// @brief Throttle spectrum sends to ~30Hz to avoid queue overflow
    int spectrumSendIntervalSamples_ = 0;
    int spectrumSendAccumulatorSamples_ = 0;

    /// @brief Latest kSpectrumBlockMaxSamples mono samples, written every block, so
    /// a throttled send carries contiguous audio (D-133)
    static_assert((kSpectrumBlockMaxSamples & (kSpectrumBlockMaxSamples - 1u)) == 0u);
    static constexpr uint32_t kSpectrumRingMask = kSpectrumBlockMaxSamples - 1u;
    std::array<float, kSpectrumBlockMaxSamples> spectrumInputRing_{};
    std::array<float, kSpectrumBlockMaxSamples> spectrumOutputRing_{};
    uint32_t spectrumRingWritePos_ = 0;
    uint32_t spectrumRingFilled_ = 0;

    /// @brief Level meters accumulated since the last send (meter_window.h)
    MeterAccumulator meterAccum_{};
    static_assert(kMeterBands == kMaxBands);

    // ==========================================================================
    // SharedDisplayBridge (Tier 3 fallback for hosts without DataExchange)
    // ==========================================================================

    uint64_t instanceId_ = 0;
    Krate::DSP::SpectrumFIFO<8192> sharedInputFIFO_;
    Krate::DSP::SpectrumFIFO<8192> sharedOutputFIFO_;
    std::atomic<float> sharedSampleRate_{0.0f};
    /// @brief Level-meter windows for Tier 3 (published at each send point)
    SharedMeters sharedMeters_;
    /// @brief Shared display data exposed to controller via SharedDisplayBridge
    SharedDisplay sharedDisplay_{};

    /// @brief Magic marker for instanceId in state stream
    static constexpr Steinberg::int32 kInstanceIdMarker = 0x4B524154; // "KRAT"

    /// @brief Send current audio block to controller via DataExchange
    void sendSpectrumBlock(const float* inputL, const float* inputR,
                           const float* outputL, const float* outputR,
                           Steinberg::int32 numSamples);

    // ==========================================================================
    // State hand-off (D-003): setState never writes live DSP objects while
    // processing is active. It parses the stream into pendingState_ and the next
    // process() applies it. Threading contract: processor_state.cpp, top.
    // ==========================================================================

    /// @brief One modulation-engine LFO, in its setters' units.
    struct ModLfoState {
        float rateHz = 1.0f;
        Krate::DSP::Waveform waveform = Krate::DSP::Waveform::Sine;
        float phaseDeg = 0.0f;
        bool tempoSync = false;
        Krate::DSP::NoteValue noteValue = Krate::DSP::NoteValue::Eighth;
        Krate::DSP::NoteModifier noteModifier = Krate::DSP::NoteModifier::None;
        bool unipolar = false;
        bool retrigger = true;
    };

    /// @brief Everything getState saves and setState restores, in the setters'
    /// units. setState seeds it from the current state, so fields an older or
    /// truncated stream lacks keep their values, then overwrites what it reads.
    struct PendingState {
        // Stream sections the load carried; applyPendingState() runs only these
        // sections' setters, as setState always did. A load that overwrites one
        // not yet applied keeps the earlier one's flags (they are still owed).
        bool hasBands = false;  ///< v2+: band count, band states, crossovers
        std::array<bool, kMaxBands - 1> hasCrossover{};
        bool hasSweep = false;       ///< v4+: sweep, sweep LFO/envelope, custom curve
        bool hasModulation = false;  ///< v5+: mod sources, macros, routings
        bool hasRungler = false;     ///< v11+
        bool hasMorph = false;       ///< v6+: morph caches, link modes

        float inputGain = 0.5f;
        float outputGain = 0.5f;
        float globalMix = 1.0f;
        int bandCount = kDefaultBands;
        std::array<BandState, kMaxBands> bandStates{};
        std::array<float, kMaxBands - 1> crossoverHz{};

        bool sweepEnabled = false;
        float sweepFrequencyHz = 1000.0f;
        float sweepWidthNorm = 0.286f;
        float sweepIntensityNorm = 0.25f;
        SweepFalloff sweepFalloff = SweepFalloff::Smooth;
        MorphLinkMode sweepMorphLink = MorphLinkMode::None;
        bool sweepLfoEnabled = false;
        float sweepLfoRateHz = 1.0f;
        Krate::DSP::Waveform sweepLfoWaveform = Krate::DSP::Waveform::Sine;
        float sweepLfoDepth = 0.0f;
        bool sweepLfoSync = false;
        Krate::DSP::NoteValue sweepLfoNoteValue = Krate::DSP::NoteValue::Eighth;
        Krate::DSP::NoteModifier sweepLfoNoteModifier = Krate::DSP::NoteModifier::None;
        bool sweepEnvEnabled = false;
        float sweepEnvAttackMs = 10.0f;
        float sweepEnvReleaseMs = 100.0f;
        float sweepEnvSensitivity = 0.5f;
        CustomCurve customCurve;

        ModLfoState lfo1;
        ModLfoState lfo2;
        float envFollowerAttackMs = 10.0f;
        float envFollowerReleaseMs = 100.0f;
        float envFollowerSensitivity = 0.5f;
        Krate::DSP::EnvFollowerSourceType envFollowerSource{};
        float randomRateHz = 1.0f;
        float randomSmoothness = 0.0f;
        bool randomTempoSync = false;
        Krate::DSP::ChaosModel chaosModel{};
        float chaosSpeed = 1.0f;
        float chaosCoupling = 0.0f;
        Krate::DSP::SampleHoldInputType sampleHoldSource{};
        float sampleHoldRateHz = 1.0f;
        float sampleHoldSlewMs = 0.0f;
        float pitchFollowerMinHz = 80.0f;
        float pitchFollowerMaxHz = 2000.0f;
        float pitchFollowerConfidence = 0.5f;
        float pitchFollowerTrackingMs = 50.0f;
        float transientSensitivity = 0.5f;
        float transientAttackMs = 2.0f;
        float transientDecayMs = 50.0f;
        float runglerRateHz = 2.0f;
        float runglerDepth = 0.5f;
        std::size_t runglerBits = 8;
        bool runglerLoop = false;
        std::array<Krate::DSP::MacroConfig, Krate::DSP::kMaxMacros> macros{};
        std::array<Krate::DSP::ModRouting, Krate::DSP::kMaxModRoutings> routings{};

        std::array<BandMorphCache, kMaxBands> morph{};
    };

    /// @brief Slot states of pendingState_ (values of pendingSlot_)
    static constexpr int kSlotIdle = 0;      ///< nothing pending
    static constexpr int kSlotWriting = 1;   ///< a host-thread call owns pendingState_
    static constexpr int kSlotReady = 2;     ///< published, not yet applied
    static constexpr int kSlotApplying = 3;  ///< being applied (audio thread, or processing stopped)

    PendingState pendingState_{};
    std::atomic<int> pendingSlot_{kSlotIdle};

    /// @brief True between setActive(true) and setActive(false): process() may run,
    /// so setState defers. Written and read on the host's UI thread.
    std::atomic<bool> active_{false};

    /// @brief Live state -> snapshot (host thread; the reads getState always made)
    void captureState(PendingState& s) const noexcept;

    /// @brief Snapshot -> stream, in the layout getState has always written
    Steinberg::tresult writeState(const PendingState& s, Steinberg::IBStreamer& streamer) const;

    /// @brief Stream sections after the globals -> snapshot. False when the stream
    /// ended before the band count (setState then stops, as it always did).
    static bool parseStateSections(Steinberg::IBStreamer& streamer, int32_t version, PendingState& s) noexcept;

    /// @brief Take pendingSlot_ for a host-thread call (yields while it is being
    /// applied). @return the state it held: kSlotIdle or kSlotReady.
    int acquireStateSlot() noexcept;

    /// @brief Apply a published pendingState_ if there is one. Audio thread at the
    /// top of process(); setActive / setupProcessing while processing is stopped.
    void consumePendingState() noexcept;

    /// @brief pendingState_ -> live DSP objects. Allocation-free and lock-free.
    void applyPendingState() noexcept;
};

} // namespace Disrumpo
