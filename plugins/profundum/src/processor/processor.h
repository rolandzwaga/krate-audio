#pragma once

// ==============================================================================
// Profundum - Audio Processor (audio thread)   Plan section 2.5
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation. This header includes
// no controller header; processor and controller share only plugin_ids.h and
// the parameters/*.h packs.
//
// A thin wrapper around Krate::DSP::ProfundumEngine (heap-owned, FR-022): one
// event input, one stereo output, no audio input (FR-021). The only operation
// after the engine's TruePeakLimiter is a smoothed output gain in [0, 1]
// (FR-028). Latency = the limiter look-ahead, 64 samples (FR-032).
//
// The implementation is processor.cpp (plan section 2.5).
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/timer.h"

#include "parameters/endpoint_names.h"  // Phase 12 T019 (FR-047)
#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/performance_params.h"
#include "parameters/sound_param_row.h"

#include "processor/spectrum_frame.h"     // Phase 12 T020 (FR-071 - FR-075)
#include "processor/spectrum_producer.h"

#include "midi/sustain_latch.h"  // plugins/shared/src/midi/sustain_latch.h (FR-061)

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

// Phase 12 T020 (plan 4.4): forward-declared as Vorago processor.h:53 does, so this header stays cheap;
// dataexchange.h is included by processor.cpp alone. The unique_ptr member over this incomplete type is legal
// because ~Processor() is defined out of line in processor.cpp.
namespace Steinberg::Vst {
class DataExchangeHandler;
}  // namespace Steinberg::Vst

namespace Profundum {

// Phase 11 T020 (plan 4.5.4 - 4.5.6; FR-025, FR-030 - FR-033): the processor is authoritative for both endpoint
// slots and the compare mode. The audio thread only sets the syncPending / compareSyncPending_ flags;
// flushControllerSync() sends the changed endpoints and the compare mode on a NON-audio thread: the 50 ms SDK
// timer (onTimer; Timer::create returns nullptr on Linux, base/source/timer.cpp:346-351) and the ends of notify,
// setState, getState and setActive. Copy and Compare arrive through notify (kMsgCopyEndpoint / kMsgCompare);
// kMsgRequestSync is the controller's Edit Target pull.
class Processor : public Steinberg::Vst::AudioEffect, public Steinberg::ITimerCallback {
public:
    static constexpr std::size_t kMaxEventsPerBlock = 1024;  // FR-025; 1024 x 8 B = 8 KiB
    static constexpr Steinberg::uint32 kSyncTimerIntervalMs = 50;  // FR-025 periodic send (Windows / macOS)
    /// FR-041 (plan 4.5.2): the 20-byte morph runtime blob, carried as five 32-bit seqlock words.
    using RuntimeBlob = std::array<std::byte, Krate::DSP::BassMorphEngine::kMorphRuntimeBytes>;
    static constexpr std::size_t kRuntimeWords = Krate::DSP::BassMorphEngine::kMorphRuntimeBytes / 4u;
    static_assert(kRuntimeWords * 4u == Krate::DSP::BassMorphEngine::kMorphRuntimeBytes,
                  "the runtime blob is a whole number of 32-bit words");
    static constexpr int kRuntimeReadTries = 1024;  // getState's bounded seqlock read (plan 4.5.2)

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
    /// FR-060 (plan 4.5.7): ceil((R_eff + 1250) * fs / 1000) + 64. Reads atomics only; 0 before setupProcessing.
    Steinberg::uint32 PLUGIN_API getTailSamples() override;

    /// FR-030 / FR-031 / FR-025 (plan 4.5.5): copy, compare and sync-pull requests; anything else -> base.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;
    /// FR-025: the 50 ms SDK timer; does nothing but flushControllerSync().
    void onTimer(Steinberg::Timer* timer) override;
    /// Phase 12 FR-071 / FR-076 (plan 4.4): the spectrum-frame DataExchange lifecycle (Vorago processor.cpp:262-303).
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;
    /// FR-025 sender (plan 4.5.4). NON-AUDIO threads only (timer, notify, setState, setActive, getState, tests).
    /// Allocates IMessages; never reached from process(). Sends the PERSISTED slots only (FR-056).
    void flushControllerSync() noexcept;

    // ---- FR-033 test seams: const, allocation-free, never called by process() ----
    [[nodiscard]] const Krate::DSP::ProfundumEngine* engineForTest() const noexcept { return engine_.get(); }
    [[nodiscard]] const GlobalParams& globalParamsForTest() const noexcept { return globalParams_; }
    [[nodiscard]] const MacroParams& macroParamsForTest() const noexcept { return macroParams_; }
    [[nodiscard]] float outputGainValueForTest() const noexcept {
        return outputGain_.getCurrentValue();  // smoother.h:191
    }
    [[nodiscard]] std::uint32_t lastSliceCountForTest() const noexcept { return lastSliceCount_; }

    // ---- Phase 11 FR-067 seams (never called by process(), allocation-free) ----
    void setEngineObserverForTest(Krate::DSP::ProfundumEngineObserver* o) noexcept { engine_->setObserver(o); }
    /// SC-020: the CPU case drives this processor's own engine directly, so P / D share one instance.
    [[nodiscard]] Krate::DSP::ProfundumEngine* mutableEngineForTest() noexcept { return engine_.get(); }
    [[nodiscard]] std::uint32_t endpointRejectionsForTest() const noexcept {
        return endpointRejections_.load(std::memory_order_relaxed);
    }
    /// FR-052: the sustain latch's pedal state. Audio-thread state: read between process() calls on one thread.
    [[nodiscard]] bool sustainDownForTest() const noexcept { return latch_.isDown(); }
    /// The stored slot word of @p leaf in endpoint @p e (A or B); 0 for a leaf past the table.
    [[nodiscard]] std::uint32_t endpointSlotForTest(Krate::DSP::BassMorphEngine::Endpoint e,
                                                    std::size_t leaf) const noexcept {
        if (leaf >= kNumLeaves) {
            return 0u;
        }
        const std::size_t idx = (e == Krate::DSP::BassMorphEngine::Endpoint::B) ? 1u : 0u;
        return endpoints_[idx].leaf[leaf].load(std::memory_order_relaxed);
    }
    /// FR-042 (plan 4.5.2): the last loaded blob restoreMorphRuntime accepted. Written by the audio thread;
    /// read it only after that thread has joined (or between process() calls on one thread).
    [[nodiscard]] const RuntimeBlob& lastRestoreBlobForTest() const noexcept { return lastRestoreBlob_; }
    /// FR-042: the number of restoreMorphRuntime calls that returned true. Same threading rule as above.
    [[nodiscard]] std::uint32_t restoreApplyCountForTest() const noexcept { return restoreApplyCount_; }
    /// FR-031: the compare mode the processor holds.
    [[nodiscard]] CompareMode compareModeForTest() const noexcept {
        return static_cast<CompareMode>(compareMode_.load(std::memory_order_relaxed));
    }
    /// FR-025: true while the 50 ms sync timer exists (initialize .. terminate; never on Linux).
    [[nodiscard]] bool hasSyncTimerForTest() const noexcept { return syncTimer_.get() != nullptr; }
    /// Phase 12 FR-047: the name the processor holds for endpoint @p e, copied under namesMutex_. Non-audio threads.
    [[nodiscard]] EndpointName endpointNameForTest(Krate::DSP::BassMorphEngine::Endpoint e) const;

    // ---- Phase 12 T020 test seams (plan 4.4; shipping logic branches only on the gate) ----
    /// Seam arm (SC-010 - SC-013, SC-025, SC-026, SC-030): run the producer with no open queue.
    /// Set it between process() calls.
    void setSpectrumProducerEnabledForTest(bool on) noexcept { spectrumForced_ = on; }
    /// The last frame publishSpectrumFrame() took from the producer (audio-written; read between process() calls).
    [[nodiscard]] const ProfundumSpectrumFrame& lastSpectrumFrameForTest() const noexcept { return lastFrame_; }
    /// Frames taken from the producer since the last setActive(true), sent or not.
    [[nodiscard]] std::uint32_t spectrumFramesPublishedForTest() const noexcept { return framesPublished_; }
    /// The most frames one process() call published since the last setActive(true) (FR-071: at most 1).
    [[nodiscard]] std::uint32_t spectrumMaxFramesPerProcessForTest() const noexcept { return maxFramesPerCall_; }
    [[nodiscard]] const ProfundumSpectrumProducer& spectrumProducerForTest() const noexcept { return spectrum_; }
    /// SC-026 steps 1 and 4: the processor's own producer instance, for feedInterval injection between process()
    /// calls (the engine-seam precedent is mutableEngineForTest above).
    [[nodiscard]] ProfundumSpectrumProducer& mutableSpectrumProducerForTest() noexcept { return spectrum_; }
    /// Publishes that found no usable DataExchange block (never retried).
    [[nodiscard]] std::uint64_t spectrumSkippedBlocksForTest() const noexcept { return skippedBlocks_; }
    [[nodiscard]] bool spectrumQueueOpenForTest() const noexcept {
        return queueState_.load(std::memory_order_acquire) == SpectrumQueueState::Open;
    }

private:
    /// Phase 12 (plan 4.4): Closed until activation; ProbePending (a handler exists) until the first process()
    /// observes whether the queue opened; then Open or Closed for the rest of the activation.
    enum class SpectrumQueueState : std::uint8_t { Closed, ProbePending, Open };
    struct EventSlot {
        std::int32_t offset;     // clamped to [0, numSamples - 1]
        std::int32_t listIndex;  // index into data.inputEvents
    };
    /// FR-052: one Sustain Pedal point of the current block. offset is the RAW host offset (clamped by process()).
    struct PedalPoint {
        std::int32_t offset;
        bool down;  // value >= 0.5
    };
    static constexpr std::size_t kMaxPedalPoints = 128;  // as Vorago processor.h:206; surplus points coalesce

    void applyPendingCopy() noexcept;  // FR-030 (plan 4.5.1 step 2), audio thread
    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    // FR-052 (Vorago processor.cpp:803-846 semantics): every Sustain Pedal point of the block, applied in order.
    void collectPedalPoints(Steinberg::Vst::IParamValueQueue* queue) noexcept;
    void applyPedalPoint(const PedalPoint& p) noexcept;
    void applyPendingPedalPointsImmediately() noexcept;
    void routeParamChange(EndpointParams& slot, Steinberg::Vst::ParamID id, double value) noexcept;  // pass 2
    void applyEndpoints() noexcept;                     // FR-013, FR-055
    void pushParamsToEngine(CompareMode cmp) noexcept;  // FR-023.5, FR-024.4, FR-014
    std::size_t buildEventOrder(Steinberg::Vst::IEventList* events, std::size_t total) noexcept;
    void dispatchEvent(const Steinberg::Vst::Event& e) noexcept;
    void applyOutputGain(float* outL, float* outR, std::size_t n) noexcept;
    void publishRuntime() noexcept;                                         // FR-041 writer (audio thread)
    [[nodiscard]] bool readPublishedRuntime(RuntimeBlob& out) const noexcept;  // FR-041 reader (getState)
    [[nodiscard]] RuntimeBlob synthesizedRuntimeBlob() const noexcept;      // FR-041 pre-render blob
    void publishRestore(const RuntimeBlob& blob) noexcept;                  // FR-042 writer (setState only)
    void applyPendingRestore() noexcept;                                    // FR-042 reader (audio, one try)
    [[nodiscard]] bool readPendingRestore(RuntimeBlob& out) const noexcept;  // FR-041 precedence 1 (getState)
    void resetNamesToDefaults();                                            // FR-047: v1 / v2 load (setState only)
    void publishSpectrumFrame() noexcept;                                   // Phase 12 FR-071 (audio thread)

    std::unique_ptr<Krate::DSP::ProfundumEngine> engine_;  // FR-022: heap only
    GlobalParams globalParams_{};
    MacroParams macroParams_{};
    Krate::DSP::OnePoleSmoother outputGain_{1.0f};  // smoother.h:148-155 (explicit initial value)
    bool snapGainPending_ = true;
    bool prepared_ = false;
    std::uint32_t lastSliceCount_ = 0;
    std::array<EventSlot, kMaxEventsPerBlock> eventOrder_{};

    // ---- Phase 11 (plan 4.5) ----
    std::array<EndpointParams, 2> endpoints_{};  // [A, B]: the authoritative slots (FR-025, FR-040)
    NoteParams noteParams_{};
    MorphControlParams morphParams_{};
    PerformanceParams perfParams_{};
    std::atomic<std::uint8_t> compareMode_{static_cast<std::uint8_t>(CompareMode::AB)};  // FR-031
    std::atomic<std::uint32_t> endpointRejections_{0};  // FR-013 / FR-067
    std::atomic<std::uint32_t> seed_{Krate::DSP::ProfundumEngine::kDefaultSeed};  // FR-043: persisted, not registered
    std::atomic<bool> compareSyncPending_{false};       // FR-025 / FR-033: report the compare mode
    std::atomic<bool> clearLatchPending_{false};        // FR-052: setState asks the audio thread to clear the latch
    // FR-041 runtime blob: a Boehm seqlock with one writer, the audio thread (plan 4.5.2).
    std::atomic<std::uint32_t> rtSeq_{0};               // even = stable, odd = a write in progress
    std::array<std::atomic<std::uint32_t>, kRuntimeWords> rtWords_{};
    std::atomic<bool> rtPublished_{false};
    // FR-042 restore hand-off, setState -> audio thread: a second seqlock (plan 4.5.2 "Restore hand-off").
    std::atomic<std::uint32_t> restoreSeq_{0};          // even = stable, odd = a write in progress
    std::array<std::atomic<std::uint32_t>, kRuntimeWords> restoreWords_{};
    std::atomic<std::uint32_t> restoreAppliedSeq_{0};   // stored by the audio thread only, at the end of process()
    std::uint32_t restoreSeenSeq_ = 0;                  // audio only: the sequence consumed so far
    std::atomic_flag restoreWriterBusy_ = ATOMIC_FLAG_INIT;  // serialises setState writers (non-audio side only)
    RuntimeBlob lastRestoreBlob_{};                     // audio-written test seam; read after the thread joins
    std::uint32_t restoreApplyCount_ = 0;               // idem: restoreMorphRuntime calls that returned true
    float lastPerfOffsetC_ = 0.0f;                      // audio only (FR-055 change detection)
    // FR-052: the sustain latch (audio thread; setupProcessing / setActive(false) with processing stopped) and this
    // block's Sustain Pedal points. Fixed-size: no allocation.
    Krate::Plugins::SustainLatch latch_{};
    std::array<PedalPoint, kMaxPedalPoints> pedalPoints_{};
    std::size_t numPedalPoints_ = 0;
    // FR-025 / FR-030 (plan 4.5, T020)
    std::atomic<std::int32_t> copyRequest_{-1};         // -1 none, 0 A -> B, 1 B -> A (FR-030)
    Steinberg::IPtr<Steinberg::Timer> syncTimer_;       // nullptr on Linux (timer.cpp:346-351)
    std::atomic_flag flushBusy_ = ATOMIC_FLAG_INIT;     // flushControllerSync re-entrancy / cross-thread guard
    // FR-060 (plan 4.5.7, T022): stored by a successful setupProcessing, cleared by terminate; 0 = no tail.
    std::atomic<double> tailSampleRate_{0.0};
    // Phase 12 FR-047 (plan 4.4, 4.5): the endpoint names [A, B]. Touched only by notify (kMsgEndpointName),
    // getState and setState, under namesMutex_; process() never touches them, so the mutex is never taken on the
    // audio thread.
    std::array<EndpointName, 2> names_{defaultEndpointName(Krate::DSP::BassMorphEngine::Endpoint::A),
                                       defaultEndpointName(Krate::DSP::BassMorphEngine::Endpoint::B)};
    mutable std::mutex namesMutex_;
    // Phase 12 FR-071 - FR-075 (plan 4.4): the spectrum producer and its DataExchange queue. The handler is built in
    // connect and released in disconnect (host thread); setActive opens / closes the queue; the audio thread probes
    // it once per activation and publishes at most one frame per process() call.
    std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_;
    ProfundumSpectrumProducer spectrum_{};
    ProfundumSpectrumFrame lastFrame_{};
    bool spectrumForced_ = false;
    std::atomic<SpectrumQueueState> queueState_{SpectrumQueueState::Closed};  // setActive / disconnect and the probe
    std::uint32_t framesPublished_ = 0;
    std::uint32_t maxFramesPerCall_ = 0;
    std::uint64_t skippedBlocks_ = 0;
};

static_assert(sizeof(Processor) < 64u * 1024u,
              "the ~800 KB engine must live on the heap (FR-022, FR-064)");

}  // namespace Profundum
