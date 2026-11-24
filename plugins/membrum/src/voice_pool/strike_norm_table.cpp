// ==============================================================================
// StrikeNormTable -- per-pad strike-normalisation gains (T1.6)
// ==============================================================================
// See strike_norm_table.h for the design and the thread contract.
// ==============================================================================

#include "strike_norm_table.h"

#include "../dsp/pad_config_apply.h"

#include <krate/dsp/core/scoped_denormal_mode.h>

#include <chrono>

namespace Membrum {

namespace {

// Ticket 0 means "never measured": a fresh result slot also reads 0.
std::uint32_t nextTicket(std::uint32_t t) noexcept
{
    ++t;
    return t == 0u ? 1u : t;
}

} // namespace

StrikeNormTable::StrikeNormTable()
    : inlineProbe_(std::make_unique<DrumVoice>())
    , workerProbe_(std::make_unique<DrumVoice>())
    , ring_(std::make_unique<std::array<Request, kRingSize>>())
{
}

StrikeNormTable::~StrikeNormTable()
{
    stopWorker();
}

StrikeNormGains StrikeNormTable::measure(DrumVoice& probe, const PadConfig& cfg,
                                         float velocity) noexcept
{
    // Same FTZ / DAZ state on every thread that measures (the audio thread
    // runs under process()'s guard, the setup thread and the worker do not),
    // so a config measures one value wherever it is measured.
    const Krate::DSP::ScopedDenormalMode denormalGuard;
    probe.resetForStrikeProbe();
    applyPadConfigToVoice(probe, cfg);
    probe.invalidateStrikeNormCache();
    // Velocity 1.0 is the canonical hard strike kBodyHeadroom is defined
    // against; only an FMImpulse gain depends on the velocity passed (T1.18).
    probe.noteOn(velocity);
    const StrikeNormGains g = probe.lastStrikeNormGains();
    probe.resetForKitSwitch();
    return g;
}

template <typename StillCurrent>
bool StrikeNormTable::measureAll(DrumVoice& probe, const PadConfig& cfg,
                                 StrikeNormGains& gains, VelocityTable& velTable,
                                 const StillCurrent& stillCurrent) noexcept
{
    if (!DrumVoice::strikeNormFollowsVelocity(cfg.exciterType))
    {
        gains = measure(probe, cfg);
        return true;
    }
    // T1.18 (MB-260): one body gain per MIDI velocity step. Step 127 is
    // velocity 1.0, which also gives the pad's velocity-1.0 entry.
    for (int k = 1; k < kVelocitySteps; ++k)
    {
        if (!stillCurrent())
            return false;
        gains = measure(probe, cfg, static_cast<float>(k) / 127.0f);
        velTable[static_cast<std::size_t>(k)] = gains.body;
    }
    return true;
}

void StrikeNormTable::prepare(double sampleRate,
                              const std::array<PadConfig, kNumPads>& pads)
{
    stopWorker();

    inlineProbe_->prepare(sampleRate, kProbeVoiceId);
    workerProbe_->prepare(sampleRate, kProbeVoiceId);

    // Requests queued before this point describe a superseded state.
    head_.store(tail_.load(std::memory_order_relaxed), std::memory_order_relaxed);

    const bool rateChanged = (sampleRate != keyRate_);
    for (int p = 0; p < kNumPads; ++p)
    {
        const auto i = static_cast<std::size_t>(p);
        if (rateChanged || !valid_[i] || !(pads[i] == keyCfg_[i]))
        {
            keyCfg_[i] = pads[i];
            ticket_[i] = nextTicket(ticket_[i]);
            (void)measureAll(*inlineProbe_, pads[i], gains_[i], velBody_[i],
                             []() noexcept { return true; });
            velValid_[i].fill(true);
            valid_[i]  = true;
            latestTicket_[i].store(ticket_[i], std::memory_order_release);
        }
        retry_[i] = false;
    }
    keyRate_ = sampleRate;
}

void StrikeNormTable::syncAll(const std::array<PadConfig, kNumPads>& pads) noexcept
{
    if (!isPrepared())
        return;
    for (int p = 0; p < kNumPads; ++p)
        syncPad(p, pads[static_cast<std::size_t>(p)]);
}

bool StrikeNormTable::pushRequest(int p) noexcept
{
    const std::uint32_t t = tail_.load(std::memory_order_relaxed);
    const std::uint32_t h = head_.load(std::memory_order_acquire);
    if (t - h >= kRingSize)
        return false;
    auto& slot  = (*ring_)[t % kRingSize];
    const auto i = static_cast<std::size_t>(p);
    slot.pad    = p;
    slot.ticket = ticket_[i];
    slot.cfg    = keyCfg_[i];
    tail_.store(t + 1u, std::memory_order_release);
    return true;
}

void StrikeNormTable::syncPad(int p, const PadConfig& cfg) noexcept
{
    const auto i = static_cast<std::size_t>(p);
    if (!(cfg == keyCfg_[i]))
    {
        keyCfg_[i] = cfg;
        ticket_[i] = nextTicket(ticket_[i]);
        latestTicket_[i].store(ticket_[i], std::memory_order_release);
        valid_[i] = false;
        velValid_[i].fill(false);
        retry_[i] = consumerAttached_.load(std::memory_order_acquire) && !pushRequest(p);
    }
    else if (retry_[i])
    {
        retry_[i] = consumerAttached_.load(std::memory_order_acquire) && !pushRequest(p);
    }

    if (!valid_[i] &&
        resultTicket_[i].load(std::memory_order_acquire) == ticket_[i])
    {
        gains_[i].body  = resultBody_[i].load(std::memory_order_relaxed);
        gains_[i].shell = resultShell_[i].load(std::memory_order_relaxed);
        if (DrumVoice::strikeNormFollowsVelocity(keyCfg_[i].exciterType))
        {
            for (int k = 1; k < kVelocitySteps; ++k)
            {
                const auto ki = static_cast<std::size_t>(k);
                velBody_[i][ki] = resultVelBody_[i][ki].load(std::memory_order_relaxed);
            }
        }
        velValid_[i].fill(true);
        valid_[i]       = true;
    }
}

StrikeNormGains StrikeNormTable::gainsForNoteOn(int pad, const PadConfig& cfg,
                                                float velocity) noexcept
{
    const auto i = static_cast<std::size_t>(pad);
    syncPad(pad, cfg);
    if (DrumVoice::strikeNormFollowsVelocity(keyCfg_[i].exciterType))
    {
        // T1.18: an FMImpulse pad's body gain is per velocity step. Before the
        // worker delivers the table, measure only the step this note needs.
        const int k   = DrumVoice::strikeNormVelocityStep(velocity);
        const auto ki = static_cast<std::size_t>(k);
        if (!velValid_[i][ki])
        {
            const StrikeNormGains g = measure(*inlineProbe_, cfg, static_cast<float>(k) / 127.0f);
            velBody_[i][ki]  = g.body;
            gains_[i].shell  = g.shell;
            velValid_[i][ki] = true;
            ++inlineMeasureCount_;
        }
        return {.body = velBody_[i][ki], .shell = gains_[i].shell};
    }
    if (!valid_[i])
    {
        // The worker has not delivered this config yet: measure it here, once.
        gains_[i] = measure(*inlineProbe_, cfg);
        valid_[i] = true;
        ++inlineMeasureCount_;
    }
    return gains_[i];
}

bool StrikeNormTable::drainOnce() noexcept
{
    bool popped = false;
    std::uint32_t h = head_.load(std::memory_order_relaxed);
    for (;;)
    {
        const std::uint32_t t = tail_.load(std::memory_order_acquire);
        if (h == t)
            break;
        const Request req = (*ring_)[h % kRingSize];
        ++h;
        head_.store(h, std::memory_order_release);
        popped = true;

        const auto i = static_cast<std::size_t>(req.pad);
        // A newer edit has superseded this request: skip it.
        if (latestTicket_[i].load(std::memory_order_acquire) != req.ticket)
            continue;
        // An FMImpulse table is 127 measurements: stop as soon as a newer
        // edit supersedes it.
        StrikeNormGains g;
        VelocityTable velTable{};
        if (!measureAll(*workerProbe_, req.cfg, g, velTable, [this, i, &req]() noexcept {
                return latestTicket_[i].load(std::memory_order_acquire) == req.ticket;
            }))
            continue;
        if (DrumVoice::strikeNormFollowsVelocity(req.cfg.exciterType))
        {
            for (int k = 1; k < kVelocitySteps; ++k)
            {
                const auto ki = static_cast<std::size_t>(k);
                resultVelBody_[i][ki].store(velTable[ki], std::memory_order_relaxed);
            }
        }
        resultBody_[i].store(g.body, std::memory_order_relaxed);
        resultShell_[i].store(g.shell, std::memory_order_relaxed);
        resultTicket_[i].store(req.ticket, std::memory_order_release);
        workerMeasureCount_.fetch_add(1u, std::memory_order_relaxed);
    }
    return popped;
}

void StrikeNormTable::startWorker()
{
    if (running_.load(std::memory_order_acquire))
        return;
    running_.store(true, std::memory_order_release);
    consumerAttached_.store(true, std::memory_order_release);
    worker_ = std::thread([this]() noexcept {
        // Same FTZ / DAZ state as process() (measure() also sets it).
        const Krate::DSP::ScopedDenormalMode guard;
        while (running_.load(std::memory_order_acquire))
        {
            if (!drainOnce())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

void StrikeNormTable::stopWorker() noexcept
{
    running_.store(false, std::memory_order_release);
    if (worker_.joinable())
        worker_.join();
    consumerAttached_.store(false, std::memory_order_release);
}

void StrikeNormTable::setManualConsumer(bool on) noexcept
{
    if (running_.load(std::memory_order_acquire))
        return;
    consumerAttached_.store(on, std::memory_order_release);
}

} // namespace Membrum
