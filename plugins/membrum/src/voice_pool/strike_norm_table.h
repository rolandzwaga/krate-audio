#pragma once

// ==============================================================================
// StrikeNormTable -- per-pad strike-normalisation gains (T1.6)
// ==============================================================================
// DrumVoice normalises a modal body (and the optional shell bank) to its
// -6 dBFS strike budget by rendering a canonical strike through the configured
// bank and measuring the peak (drum_voice.h, measuredStrikeOutputGain). That
// render is 30-100 ms of audio; doing it inside DrumVoice::noteOn on the audio
// thread cost up to 1.4-3.5 ms per hit, and the per-voice cache missed almost
// every time because slots rotate across pads (MB-003 / MB-036).
//
// This table holds the gains per PAD instead, keyed on the pad's whole
// PadConfig plus the sample rate, measured on a dedicated probe DrumVoice with
// a fixed voice id. A gain is therefore a pure function of (PadConfig, rate):
// it does not depend on the slot or on what played before.
//
// An FMImpulse pad's body gain depends on the note's velocity (T1.18, MB-260:
// its velocity moves the excitation spectrum), so such a pad holds one body
// gain per MIDI velocity step 1..127, all measured wherever the pad is
// measured; every other pad holds one gain measured at velocity 1.0. Inline,
// an FMImpulse pad measures only the velocity step the note-on needs.
//
// When a gain is measured (at most once per pad per config change):
//   - prepare() (setup thread): every pad whose config or rate changed;
//   - the worker thread (startWorker(), started by Processor::setActive(true)):
//     every pad edited while processing runs;
//   - inline on the audio thread, only for a note-on that reaches an edited
//     pad before the worker has delivered (gainsForNoteOn).
//
// Threads: the audio thread owns the per-pad plain members and is the single
// producer of the request ring; the worker (or a test calling drainOnce()) is
// the single consumer and writes the result atomics. Each atomic has exactly
// one writer thread. No locks, no allocation after construction.
// ==============================================================================

#include "../dsp/drum_voice.h"
#include "../dsp/pad_config.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

namespace Membrum {

class StrikeNormTable
{
public:
    static constexpr std::uint32_t kProbeVoiceId = 0u;
    static constexpr std::uint32_t kRingSize     = 64u;
    /// Velocity-table size of a velocity-following (FMImpulse) pad: index =
    /// DrumVoice::strikeNormVelocityStep (1..127); index 0 is unused.
    static constexpr int kVelocitySteps = 128;
    using VelocityTable = std::array<float, kVelocitySteps>;

    StrikeNormTable();
    ~StrikeNormTable();

    StrikeNormTable(const StrikeNormTable&)            = delete;
    StrikeNormTable& operator=(const StrikeNormTable&) = delete;
    StrikeNormTable(StrikeNormTable&&)                 = delete;
    StrikeNormTable& operator=(StrikeNormTable&&)      = delete;

    /// Measure one config on `probe` (prepared at the target rate): a clean
    /// probe, the config, one note at `velocity`, the gains that note applied.
    /// Only an FMImpulse config's gain depends on the velocity (T1.18).
    /// Leaves the probe silent. Allocation-free; one compiled copy serves
    /// every thread.
    static StrikeNormGains measure(DrumVoice& probe, const PadConfig& cfg,
                                   float velocity = 1.0f) noexcept;

    /// Setup thread (processing stopped): stops the worker, prepares the probe
    /// voices and measures every pad that is new, edited, or all of them when
    /// the rate changed.
    void prepare(double sampleRate, const std::array<PadConfig, kNumPads>& pads);

    /// Audio thread, once per block: notice edited pads, queue them for the
    /// worker, adopt the worker's results.
    void syncAll(const std::array<PadConfig, kNumPads>& pads) noexcept;

    /// Audio thread, at note-on: the pad's gains for `cfg` at `velocity`,
    /// measured inline only when no current measurement exists yet (for an
    /// FMImpulse pad: for this velocity step).
    [[nodiscard]] StrikeNormGains gainsForNoteOn(int pad, const PadConfig& cfg,
                                                 float velocity = 1.0f) noexcept;

    /// Consumer (worker thread, or a test with setManualConsumer(true)):
    /// measure every queued request that is still current. Returns whether a
    /// request was popped.
    bool drainOnce() noexcept;

    /// Setup / host thread; both idempotent. The destructor stops the worker.
    void startWorker();
    void stopWorker() noexcept;

    /// Test hook: queue requests without a worker thread (call drainOnce()
    /// yourself). Only while the worker is stopped.
    void setManualConsumer(bool on) noexcept;

    /// True once prepare() has measured the table.
    [[nodiscard]] bool isPrepared() const noexcept { return keyRate_ > 0.0; }

    [[nodiscard]] std::uint32_t inlineMeasureCount() const noexcept { return inlineMeasureCount_; }
    [[nodiscard]] std::uint32_t workerMeasureCount() const noexcept
    {
        return workerMeasureCount_.load(std::memory_order_relaxed);
    }

private:
    struct Request
    {
        int           pad    = 0;
        std::uint32_t ticket = 0;
        PadConfig     cfg{};
    };

    void syncPad(int p, const PadConfig& cfg) noexcept;
    [[nodiscard]] bool pushRequest(int p) noexcept;
    // Every gain of one config: `gains` at velocity 1.0 and, for an FMImpulse
    // config, `velTable` at every velocity step. Stops early and returns
    // false once `stillCurrent()` reports the request superseded.
    template <typename StillCurrent>
    static bool measureAll(DrumVoice& probe, const PadConfig& cfg, StrikeNormGains& gains,
                           VelocityTable& velTable, const StillCurrent& stillCurrent) noexcept;

    // Allocated in the constructor (host thread).
    std::unique_ptr<DrumVoice>                    inlineProbe_;
    std::unique_ptr<DrumVoice>                    workerProbe_;
    std::unique_ptr<std::array<Request, kRingSize>> ring_;
    std::atomic<std::uint32_t>                    head_{0};  // consumer position
    std::atomic<std::uint32_t>                    tail_{0};  // producer position

    // Audio thread only.
    std::array<PadConfig, kNumPads>       keyCfg_{};
    std::array<std::uint32_t, kNumPads>   ticket_{};
    std::array<StrikeNormGains, kNumPads> gains_{};
    std::array<bool, kNumPads>            valid_{};
    std::array<bool, kNumPads>            retry_{};
    // FMImpulse pads (T1.18): body gain per velocity step, and which steps
    // hold a current measurement (all of them once valid_ is set).
    std::array<VelocityTable, kNumPads>                     velBody_{};
    std::array<std::array<bool, kVelocitySteps>, kNumPads>  velValid_{};
    double                                keyRate_            = 0.0;
    std::uint32_t                         inlineMeasureCount_ = 0;

    // Audio thread writes, consumer reads.
    std::array<std::atomic<std::uint32_t>, kNumPads> latestTicket_{};
    // Consumer writes, audio thread reads.
    std::array<std::atomic<std::uint32_t>, kNumPads> resultTicket_{};
    std::array<std::atomic<float>, kNumPads>         resultBody_{};
    std::array<std::atomic<float>, kNumPads>         resultShell_{};
    std::array<std::array<std::atomic<float>, kVelocitySteps>, kNumPads> resultVelBody_{};
    std::atomic<std::uint32_t>                       workerMeasureCount_{0};

    // Worker lifecycle (setup / host thread).
    std::atomic<bool> consumerAttached_{false};
    std::atomic<bool> running_{false};
    std::thread       worker_;
};

} // namespace Membrum
