// ==============================================================================
// VoicePool -- Phase 3.1 voice pool + allocator integration
// ==============================================================================
// Phase 3.1 wires the VoicePool into Membrum::Processor: note-on routes through
// VoiceAllocator, the three stealing policies (Oldest / Quietest / Priority)
// are implemented, and MIDI notes 36..67 are accepted. Phase 3.1 does NOT yet
// provide a click-free fast-release ramp (Phase 3.2) -- the scaffolding hooks
// below will be filled in later.
// ==============================================================================

#include "voice_pool.h"

#include "../dsp/macro_offsets.h"
#include "../dsp/pad_config_apply.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <utility>

namespace Membrum {

// ------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------

VoicePool::VoicePool()
    : voiceStoragePtr_(std::make_unique<std::array<DrumVoice, kVoiceStorageCount>>())
{
    // The 32 DrumVoices are default-constructed on the heap via make_unique.
    // Per FR-116 this is the only allocation point outside of prepare() --
    // it runs on the host thread during Processor construction, not on the
    // audio thread.
    for (int i = 0; i < kMaxVoices; ++i)
    {
        mainIndex_[static_cast<std::size_t>(i)]   = static_cast<std::uint8_t>(i);
        shadowIndex_[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i + kMaxVoices);
    }
}

DrumVoice& VoicePool::mainVoice(int slot) noexcept
{
    return (*voiceStoragePtr_)[mainIndex_[static_cast<std::size_t>(slot)]];
}

DrumVoice& VoicePool::shadowVoice(int slot) noexcept
{
    return (*voiceStoragePtr_)[shadowIndex_[static_cast<std::size_t>(slot)]];
}

DrumVoice& VoicePool::mainVoiceRef(int slot) noexcept
{
    return mainVoice(slot);
}

const DrumVoice& VoicePool::releasingVoiceRef(int slot) const noexcept
{
    return (*voiceStoragePtr_)[shadowIndex_[static_cast<std::size_t>(slot)]];
}

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------

void VoicePool::prepare(double sampleRate, int maxBlockSize) noexcept
{
    // Clamp sample rate to the supported range. The allocator does not
    // depend on sample rate; the DrumVoice instances and the fast-release
    // coefficient do.
    sampleRate  = std::clamp(sampleRate, 22050.0, 192000.0);
    sampleRate_ = sampleRate;

    // Size the scratch buffers to the host's ACTUAL maximum block size. VST3
    // permits maxSamplesPerBlock to exceed any fixed constant -- offline /
    // bounce / freeze renders (and pluginval stress modes) routinely use very
    // large blocks -- and VST3 only guarantees numSamples <= maxSamplesPerBlock,
    // never numSamples <= kVoicePoolMaxBlock. Previously the allocation was
    // clamped to kVoicePoolMaxBlock while processBlock still wrote `numSamples`
    // worth of audio into the scratch, producing an out-of-bounds heap write on
    // the audio thread for any block larger than 8192. kVoicePoolMaxBlock is now
    // the *minimum* reservation (small-block hosts still get a sensible floor);
    // we grow to fit anything larger. This remains the ONLY allocation point
    // (FR-116 / FR-117) and runs on the setup thread.
    maxBlockSize_ = std::max(maxBlockSize, kVoicePoolMaxBlock);

    // FR-116/FR-117/Q4: this is the ONLY allocation point in VoicePool. All
    // subsequent audio-thread calls are allocation-free.
    scratchL_ = std::make_unique<float[]>(static_cast<std::size_t>(maxBlockSize_));
    scratchR_ = std::make_unique<float[]>(static_cast<std::size_t>(maxBlockSize_));

    // FR-124 (Option B) / FR-125: per-sample decay coefficient. `kFastReleaseSecs`
    // is the total wall-clock decay time to the 1e-6 denormal floor (NOT τ). For
    // `gain(t) = exp(-t/τ)` to hit 1e-6 at t = kFastReleaseSecs, we need
    // τ = kFastReleaseSecs / ln(1e6), and the per-sample coefficient becomes
    //     k = exp(-1 / (τ * sr)) = exp(-ln(1e6) / (kFastReleaseSecs * sr)).
    // This gives τ ≈ 362 μs and reaches the floor in exactly 5 ms ± 1 sample at
    // any supported sample rate.
    fastReleaseK_ =
        std::exp(-kFastReleaseLnFloor / (kFastReleaseSecs * static_cast<float>(sampleRate_)));
    // T2.3: the kit-load fade reaches 0 linearly in the same kFastReleaseSecs.
    linearFadeStep_ = 1.0f / (kFastReleaseSecs * static_cast<float>(sampleRate_));

    // Prepare every main + shadow voice with voiceId = slot index. Main and
    // shadow slots at the same index share the same voiceId because the
    // fast-release crossfade (beginFastRelease) exchanges their roles, and
    // any voiceId-derived state (PRNG seeds, per-voice
    // decorrelation) must be IDENTICAL across the two arrays so that a
    // freshly-swapped voice behaves BIT-IDENTICALLY to a pristine voice at
    // that slot (FR-124 bit-identity clause / T3.3.2(c)). If the two arrays
    // held distinct voiceIds, a reused voice after steal/choke would
    // produce sample-level differences from an isolated fresh-pool voice,
    // violating the -120 dBFS noise-floor bit-identity requirement.
    //
    // Shadow and main slots for the SAME index never render concurrently
    // with the same state: at any moment either (a) the main slot is
    // rendering and the shadow is idle-prepared, or (b) the shadow is
    // fast-releasing the previous voice while the main slot renders the
    // new note. Even in case (b) they are rendering DIFFERENT notes
    // triggered at DIFFERENT times, so per-voice PRNG sequences diverge
    // naturally; sharing the seed does not collapse them into lockstep.
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const auto vid = static_cast<std::uint32_t>(i);
        mainVoice(i).prepare(sampleRate_, vid);
        shadowVoice(i).prepare(sampleRate_, vid);
        // T1.19: DrumVoice::prepare keeps active_, so a voice still ringing at
        // a deactivate or re-prepare would render on as an uncounted ghost
        // (meta_ is cleared below: no noteOff, no auto-release reaches it).
        mainVoice(i).resetForKitSwitch();
        shadowVoice(i).resetForKitSwitch();

        meta_[i]          = VoiceMeta{};
        releasingMeta_[i] = VoiceMeta{};
    }

    // T1.6: measure every pad's strike-norm gains here, on the setup thread,
    // so a note-on never renders the probe for an unchanged pad.
    strikeNorm_.prepare(sampleRate_, padConfigs_);

    // Reset the allocator and seed the active voice count so it matches
    // `maxPolyphony_`.
    allocator_.reset();
    (void)allocator_.setVoiceCount(static_cast<std::size_t>(maxPolyphony_));

    sampleCounter_ = 0;
}

// ------------------------------------------------------------------
// Note events
// ------------------------------------------------------------------

void VoicePool::noteOn(std::uint8_t midiNote, float velocity) noexcept
{
    if (velocity <= 0.0f)
    {
        noteOff(midiNote);
        return;
    }

    // Phase 4: validate MIDI note is in the GM drum map range [36, 67].
    if (midiNote < kFirstDrumNote || midiNote > kLastDrumNote)
        return;

    const int padIndex = static_cast<int>(midiNote) - static_cast<int>(kFirstDrumNote);

    // Phase 8F: disabled pads short-circuit the entire trigger pipeline.
    // No allocator events, no choke iteration, no audition: a disabled pad
    // is silently absent from the kit until the user re-enables it.
    if (padConfigs_[static_cast<std::size_t>(padIndex)].enabled < 0.5f)
        return;

    // FR-171: note-on sequence = (choke) -> (steal) -> allocator -> DrumVoice.
    // Step 1: choke group iteration. Phase 3.3 fills this in; Phase 3.1 is a
    // no-op stub so the code path compiles.
    processChokeGroups(midiNote);

    // Step 2: Quietest pre-steal (Clarification Q3 / FR-122). Only when the
    // allocator would really have to steal -- the pool is full AND the note
    // is not already playing (a same-note retrigger is served on the note's
    // own slot, so stealing another voice for it would kill it for nothing)
    // -- pre-select the quietest rendered voice and release it so the
    // allocator sees an Idle slot. We must yield the slot to the allocator
    // via noteOff + voiceFinished so the next allocator.noteOn() call can
    // pick it up without issuing its own Steal event. When no slot is freed
    // here (pool not full, retrigger, or no voice with a measured level) the
    // allocator's own Oldest steal applies.
    const bool poolFull =
        std::cmp_greater_equal(allocator_.getActiveVoiceCount(), maxPolyphony_);

    bool alreadyPlaying = false;
    for (int i = 0; i < maxPolyphony_; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        if (allocator_.getVoiceState(idx) != Krate::DSP::VoiceState::Idle &&
            allocator_.getVoiceNote(idx) == static_cast<int>(midiNote))
        {
            alreadyPlaying = true;
            break;
        }
    }

    if (stealingPolicy_ == VoiceStealingPolicy::Quietest && poolFull && !alreadyPlaying)
    {
        const int q = selectQuietestActiveSlot();
        if (q >= 0)
        {
            // Phase 5: Release coupling resonators for the stolen voice.
            if (couplingEngine_ != nullptr)
                couplingEngine_->noteOff(q);

            const uint8_t victimNote = meta_[q].originatingNote;
            // T2.6 (MB-148): forced, so a shadow still fading from an earlier
            // release cannot leave the victim sounding in a Free slot.
            beginFastRelease(q, /*force*/ true);
            (void)allocator_.noteOff(victimNote);
            allocator_.voiceFinished(static_cast<std::size_t>(q));
            // Mark the main slot Free so our own bookkeeping agrees with the
            // allocator's Idle view (the audible tail continues from the
            // shadow slot).
            meta_[q].state = VoiceSlotState::Free;
        }
    }

    // FR-121 / FR-123: Oldest -> AllocationMode::Oldest;
    //                  Priority -> AllocationMode::HighestNote;
    //                  Quietest -> AllocationMode::Oldest, so the allocator
    //                  never steals with a stale mode when the pre-steal
    //                  above freed nothing.
    const auto mode =
        (stealingPolicy_ == VoiceStealingPolicy::Priority)
            ? Krate::DSP::AllocationMode::HighestNote
            : Krate::DSP::AllocationMode::Oldest;
    allocator_.setAllocationMode(mode);

    // Step 3: the allocator picks / steals a slot.
    const auto clampedVel = std::clamp(velocity, 0.0f, 1.0f);
    const auto velocityByte =
        static_cast<std::uint8_t>(clampedVel * 127.0f + 0.5f);

    const auto events = allocator_.noteOn(midiNote, velocityByte);

    // Step 4: walk the event list and translate it into voice actions.
    for (const auto& ev : events)
    {
        switch (ev.type)
        {
        case Krate::DSP::VoiceEvent::Type::Steal:
        {
            // Phase 5: Release coupling resonators for the stolen voice.
            // noteOff allows resonators to ring out naturally.
            if (couplingEngine_ != nullptr)
                couplingEngine_->noteOff(static_cast<int>(ev.voiceIndex));

            // FR-126/FR-127: fade out the stolen voice through the shadow
            // slot. The voice left in the main role is inactive (T2.6); the
            // NoteOn below re-configures it for the new note.
            //
            // Audit M-8: force=true so a same-slot re-steal whose shadow is
            // still fading from a prior release (e.g. fast same-note
            // retrigger) re-snapshots the ringing main voice instead of letting
            // the incoming attack hard-overwrite it (a click); only the older,
            // already-attenuated shadow tail is truncated. Choke, Quietest
            // pre-steal and polyphony shrink force the same way (T2.6).
            beginFastRelease(static_cast<int>(ev.voiceIndex), /*force*/ true);
            // After stealing we mark the main slot Free; the NoteOn event
            // that follows will transition it back to Active.
            meta_[ev.voiceIndex].state = VoiceSlotState::Free;
            break;
        }
        case Krate::DSP::VoiceEvent::Type::NoteOn:
        {
            const int slot = static_cast<int>(ev.voiceIndex);

            // T1.6: the voice is re-configured from scratch below, so its
            // guarded body setters must not re-map modes for a still-active
            // (retriggered / re-acquired) voice; noteOn re-maps them once.
            mainVoice(slot).markInactiveForNoteOn();

            // Configure the voice from the pad's config (Phase 4 per-pad dispatch).
            applyPadConfigToSlot(slot, padIndex);
            // T1.6: the pad's strike-norm gains come from the per-pad table
            // (measured in prepare / on the worker), not from a probe render
            // inside this note-on. An FMImpulse pad's gain is looked up at
            // this note's velocity (T1.18, MB-260).
            if (strikeNorm_.isPrepared())
            {
                const auto g = strikeNorm_.gainsForNoteOn(
                    padIndex, padConfigs_[static_cast<std::size_t>(padIndex)], clampedVel);
                mainVoice(slot).setSuppliedStrikeNormGains(g.body, g.shell);
            }
            mainVoice(slot).noteOn(clampedVel);

            // Phase 5: Register this voice's partials with the coupling engine.
            // The engine will create sympathetic resonators at the voice's
            // modal frequencies (FR-041: velocity scales coupling excitation).
            //
            // Phase 6 (US4 / FR-023 CPU optimization): if the struck pad has
            // couplingAmount == 0.0, it is fully excluded from coupling --
            // skip resonator registration entirely. No CPU cost for silenced
            // pads.
            registerCouplingPartials(slot, padIndex);

            // Bookkeeping: populate the per-slot metadata for later stealing
            // decisions + choke lookups.
            meta_[slot].state             = VoiceSlotState::Active;
            meta_[slot].originatingNote   = midiNote;
            meta_[slot].originatingChoke  = chokeGroups_.lookup(midiNote);
            meta_[slot].noteOnSampleCount = sampleCounter_;
            meta_[slot].currentLevel      = 0.0f;
            meta_[slot].fastReleaseGain   = 1.0f;
            break;
        }
        case Krate::DSP::VoiceEvent::Type::NoteOff:
            // Phase 3 does not use unison, so allocator never emits NoteOff
            // here. Ignore.
            break;
        }
    }
}

void VoicePool::fadeOutAllVoicesForKitSwitch() noexcept
{
    for (int slot = 0; slot < kMaxVoices; ++slot)
    {
        if (couplingEngine_ != nullptr && meta_[slot].state != VoiceSlotState::Free)
            couplingEngine_->noteOff(slot);

        // T2.3 (MB-054): a sounding voice moves to the shadow role and fades
        // out over the fast-release time instead of being cut, with the
        // linear kit-load ramp (see beginFastRelease). The forced path
        // re-snapshots even when the shadow is still fading.
        if (meta_[slot].state == VoiceSlotState::Active && mainVoice(slot).isActive())
            beginFastRelease(slot, /*force*/ true, /*linear*/ true);

        // The voice now in the main role (an idle voice, or the old shadow
        // contents) is wiped, so no residue of the old kit reaches the new
        // kit's first hit on this slot. Shadow voices fading from an earlier
        // steal or choke keep fading.
        mainVoice(slot).resetForKitSwitch();
        meta_[slot] = VoiceMeta{};
    }
    allocator_.reset();
    (void)allocator_.setVoiceCount(static_cast<std::size_t>(maxPolyphony_));
}

void VoicePool::registerCouplingPartials(int slot, int padIndex) noexcept
{
    if (couplingEngine_ != nullptr &&
        applyMacroOffsets(padConfigs_[static_cast<std::size_t>(padIndex)])
                .couplingAmount > 0.0f)
    {
        auto partials = mainVoice(slot).getPartialInfo();
        couplingEngine_->noteOn(slot, partials);
    }
}

void VoicePool::registerActiveVoicesWithCoupling() noexcept
{
    for (int slot = 0; slot < kMaxVoices; ++slot)
    {
        if (meta_[slot].state != VoiceSlotState::Active)
            continue;
        const int padIndex = static_cast<int>(meta_[slot].originatingNote)
                           - static_cast<int>(kFirstDrumNote);
        if (padIndex >= 0 && padIndex < kNumPads)
            registerCouplingPartials(slot, padIndex);
    }
}

void VoicePool::noteOff(std::uint8_t midiNote) noexcept
{
    // FR-114 (ruling 2026-11-25, fix plan T2.9): a MIDI note-off on a drum
    // voice releases only a sustaining exciter (Friction's bow). The amp
    // envelope, tone shaper and body are not gated or damped, so a short
    // note plays the full hit; the voice retires through its own
    // auto-release (DrumVoice::noteOff from autoReleaseAtGridPoint).
    for (int slot = 0; slot < maxPolyphony_; ++slot)
    {
        if (meta_[slot].state == VoiceSlotState::Active
            && meta_[slot].originatingNote == midiNote)
        {
            mainVoice(slot).releaseExciter();
        }
    }
    (void)allocator_.noteOff(midiNote);
}

// ------------------------------------------------------------------
// Audio processing
// ------------------------------------------------------------------

void VoicePool::processBlock(float* outL, float* outR, int numSamples) noexcept
{
    // T1.9 (MB-118): one renderer. With a single bus the aux routing of the
    // multi-bus overload never runs.
    processBlock(outL, outR, nullptr, nullptr, nullptr, 1, numSamples);
}

// ------------------------------------------------------------------
// Multi-bus processBlock (FR-044)
// ------------------------------------------------------------------

void VoicePool::processBlock(float* outL, float* outR,
                             float** auxL, float** auxR,
                             const bool* busActive,
                             int numOutputBuses,
                             int numSamples) noexcept
{
    if (numSamples <= 0 || outL == nullptr || outR == nullptr)
        return;

    // T1.6: queue edited pads for the strike-norm worker, adopt its results.
    strikeNorm_.syncAll(padConfigs_);

    // Memory-safety guard: never render more samples than the scratch buffers
    // were sized for in prepare(). A spec-compliant host always passes
    // numSamples <= maxBlockSize_ (the size we reserved), so this clamp is
    // defense-in-depth against a host that violates the ProcessSetup contract
    // by passing an oversized block -- it turns a heap overflow into (at worst)
    // a truncated block.
    numSamples = std::min(numSamples, maxBlockSize_);

    // Zero the main output buffers (FR-165 per-block accumulate model).
    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] = 0.0f;
        outR[i] = 0.0f;
    }

    float* scratch = scratchL_.get();

    // Render every active main voice, compute its per-block peak, then
    // accumulate into both output channels. DrumVoice is mono — the pool
    // mirrors mono-to-stereo here.
    //
    // T1.13 (MB-052): the voice owns its auto-release
    // (DrumVoice::kAutoReleaseThreshold, -60 dBFS held for 20 ms after a
    // 10 ms onset grace, acting on a fixed grid of voice time). The pool
    // used to release a voice after any host block whose peak was below
    // -60 dBFS, so the voice length depended on the host block size and a
    // 1-16 sample host cut hits at their onset. The per-block peak below
    // only feeds voice stealing (currentLevel).
    for (int slot = 0; slot < maxPolyphony_; ++slot)
    {
        if (mainVoice(slot).isActive())
        {
            mainVoice(slot).processBlock(scratch, numSamples);

            // M-9: equal-power per-pad pan (applied to main and aux alike).
            float gainL = 1.0f;
            float gainR = 1.0f;
            panGainsForNote(meta_[slot].originatingNote, gainL, gainR);

            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float a = std::fabs(scratch[i]);
                peak = std::max(peak, a);
                outL[i] += scratch[i] * gainL;
                outR[i] += scratch[i] * gainR;
            }
            meta_[slot].currentLevel = peak;

            // FR-044: accumulate to auxiliary bus if assigned and active
            const int padIndex = static_cast<int>(meta_[slot].originatingNote)
                                 - static_cast<int>(kFirstDrumNote);
            if (padIndex >= 0 && padIndex < kNumPads)
            {
                const int bus = static_cast<int>(
                    padConfigs_[static_cast<std::size_t>(padIndex)].outputBus);
                if (bus > 0 && bus < numOutputBuses &&
                    busActive != nullptr && busActive[bus] &&
                    auxL != nullptr && auxR != nullptr &&
                    auxL[bus] != nullptr && auxR[bus] != nullptr)
                {
                    for (int i = 0; i < numSamples; ++i)
                    {
                        auxL[bus][i] += scratch[i] * gainL;
                        auxR[bus][i] += scratch[i] * gainR;
                    }
                }
            }
        }
        else if (meta_[slot].state == VoiceSlotState::Active)
        {
            // Voice naturally finished -- FR-115.
            // Phase 5 / T1.9 (MB-069): release its coupling resonators.
            if (couplingEngine_ != nullptr)
                couplingEngine_->noteOff(slot);

            // T1.9 (MB-050 / MB-105): a voice that finishes while its MIDI
            // note is still held leaves its allocator slot Active, and
            // voiceFinished() ignores a slot that is not Releasing. Release
            // the note first so the slot returns to Idle; the later MIDI
            // note-off then finds no Active slot for it. A same-note
            // retrigger reuses the note's own slot, so at most one slot
            // holds this note.
            const auto allocSlot = static_cast<std::size_t>(slot);
            if (allocator_.getVoiceState(allocSlot) == Krate::DSP::VoiceState::Active)
            {
                [[maybe_unused]] const auto released =
                    allocator_.noteOff(meta_[slot].originatingNote);
                assert(released.size() <= 1);
            }

            meta_[slot].state        = VoiceSlotState::Free;
            meta_[slot].currentLevel = 0.0f;
            allocator_.voiceFinished(allocSlot);
        }
    }

    // Fast-releasing shadow voices
    for (int slot = 0; slot < kMaxVoices; ++slot)
    {
        if (releasingMeta_[slot].state != VoiceSlotState::FastReleasing)
            continue;

        shadowVoice(slot).processBlock(scratch, numSamples);
        const int liveCount = applyFastRelease(slot, scratch, numSamples);

        // M-9: the fading shadow voice keeps its pad's pan position, as it
        // was when the fade began (T2.3: a kit load may change the pad).
        const float gainL = releasingMeta_[slot].panGainL;
        const float gainR = releasingMeta_[slot].panGainR;

        for (int i = 0; i < liveCount; ++i)
        {
            outL[i] += scratch[i] * gainL;
            outR[i] += scratch[i] * gainR;
        }

        // FR-044: also route fast-releasing voices to their aux bus (the bus
        // captured when the fade began, T2.3).
        {
            const int bus = static_cast<int>(releasingMeta_[slot].outputBus);
            if (bus > 0 && bus < numOutputBuses &&
                busActive != nullptr && busActive[bus] &&
                auxL != nullptr && auxR != nullptr &&
                auxL[bus] != nullptr && auxR[bus] != nullptr)
            {
                for (int i = 0; i < liveCount; ++i)
                {
                    auxL[bus][i] += scratch[i] * gainL;
                    auxR[bus][i] += scratch[i] * gainR;
                }
            }
        }
    }

    sampleCounter_ += static_cast<std::uint64_t>(numSamples);
}

// ------------------------------------------------------------------
// Configuration
// ------------------------------------------------------------------

void VoicePool::setMaxPolyphony(int n) noexcept
{
    n = std::clamp(n, 4, kMaxVoices);

    // allocator_.setVoiceCount returns a span of NoteOff events for any
    // voices that were released because the pool is shrinking. We fast-
    // release each one so the shrink is click-free (FR-111).
    const auto releasedEvents =
        allocator_.setVoiceCount(static_cast<std::size_t>(n));
    for (const auto& ev : releasedEvents)
    {
        if (ev.type == Krate::DSP::VoiceEvent::Type::NoteOff)
        {
            // T2.6 (MB-053 / MB-148): forced, so a released slot whose shadow
            // is still fading cannot keep its voice above the new polyphony,
            // where it would resume when the polyphony grows back.
            beginFastRelease(static_cast<int>(ev.voiceIndex), /*force*/ true);
            meta_[ev.voiceIndex].state = VoiceSlotState::Free;
        }
    }
    maxPolyphony_ = n;
}

void VoicePool::setVoiceStealingPolicy(VoiceStealingPolicy p) noexcept
{
    stealingPolicy_ = p;
}

void VoicePool::setChokeGroup(std::uint8_t group) noexcept
{
    if (group > 8) group = 0;
    chokeGroups_.setGlobal(group);
}

// ------------------------------------------------------------------
// Per-pad configuration (Phase 4)
// ------------------------------------------------------------------

void VoicePool::setPadConfigField(int padIndex, int offset, float normalizedValue) noexcept
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return;

    auto& cfg = padConfigs_[static_cast<std::size_t>(padIndex)];
    switch (offset)
    {
    case kPadMaterial:            cfg.material = normalizedValue; break;
    case kPadSize:                cfg.size = normalizedValue; break;
    case kPadDecay:               cfg.decay = normalizedValue; break;
    case kPadStrikePosition:      cfg.strikePosition = normalizedValue; break;
    case kPadLevel:               cfg.level = normalizedValue; break;
    case kPadTSFilterType:        cfg.tsFilterType = normalizedValue; break;
    case kPadTSFilterCutoff:      cfg.tsFilterCutoff = normalizedValue; break;
    case kPadTSFilterResonance:   cfg.tsFilterResonance = normalizedValue; break;
    case kPadTSFilterEnvAmount:   cfg.tsFilterEnvAmount = normalizedValue; break;
    case kPadTSDriveAmount:       cfg.tsDriveAmount = normalizedValue; break;
    case kPadTSFoldAmount:        cfg.tsFoldAmount = normalizedValue; break;
    case kPadTSPitchEnvStart:     cfg.tsPitchEnvStart = normalizedValue; break;
    case kPadTSPitchEnvEnd:       cfg.tsPitchEnvEnd = normalizedValue; break;
    case kPadTSPitchEnvTime:      cfg.tsPitchEnvTime = normalizedValue; break;
    case kPadTSPitchEnvCurve:     cfg.tsPitchEnvCurve = normalizedValue; break;
    case kPadTSFilterEnvAttack:   cfg.tsFilterEnvAttack = normalizedValue; break;
    case kPadTSFilterEnvDecay:    cfg.tsFilterEnvDecay = normalizedValue; break;
    case kPadTSFilterEnvSustain:  cfg.tsFilterEnvSustain = normalizedValue; break;
    case kPadTSFilterEnvRelease:  cfg.tsFilterEnvRelease = normalizedValue; break;
    case kPadModeStretch:         cfg.modeStretch = normalizedValue; break;
    case kPadDecaySkew:           cfg.decaySkew = normalizedValue; break;
    case kPadModeInjectAmount:    cfg.modeInjectAmount = normalizedValue; break;
    case kPadNonlinearCoupling:   cfg.nonlinearCoupling = normalizedValue; break;
    case kPadMorphEnabled:        cfg.morphEnabled = normalizedValue; break;
    case kPadMorphStart:          cfg.morphStart = normalizedValue; break;
    case kPadMorphEnd:            cfg.morphEnd = normalizedValue; break;
    case kPadMorphDuration:       cfg.morphDuration = normalizedValue; break;
    case kPadMorphCurve:          cfg.morphCurve = normalizedValue; break;
    case kPadChokeGroup:
    {
        const auto g = static_cast<std::uint8_t>(
            std::clamp(static_cast<int>(normalizedValue * 8.0f + 0.5f), 0, 8));
        cfg.chokeGroup = g;
        chokeGroups_.setEntry(static_cast<std::size_t>(padIndex), g);
        break;
    }
    case kPadPan:                 cfg.pan = normalizedValue; break;
    case kPadWireCoupling:        cfg.wireCoupling = normalizedValue; break;
    case kPadOutputBus:
    {
        cfg.outputBus = static_cast<std::uint8_t>(
            std::clamp(static_cast<int>(normalizedValue * 15.0f + 0.5f), 0, 15));
        break;
    }
    case kPadFMRatio:             cfg.fmRatio = normalizedValue; break;
    case kPadFeedbackAmount:      cfg.feedbackAmount = normalizedValue; break;
    case kPadNoiseBurstDuration:  cfg.noiseBurstDuration = normalizedValue; break;
    case kPadFrictionPressure:    cfg.frictionPressure = normalizedValue; break;
    case kPadCouplingAmount:      cfg.couplingAmount = normalizedValue; break;
    // Phase 6: per-pad macros
    case kPadMacroTightness:      cfg.macroTightness = normalizedValue; break;
    case kPadMacroBrightness:     cfg.macroBrightness = normalizedValue; break;
    case kPadMacroBodySize:       cfg.macroBodySize = normalizedValue; break;
    case kPadMacroPunch:          cfg.macroPunch = normalizedValue; break;
    case kPadMacroComplexity:     cfg.macroComplexity = normalizedValue; break;
    // Phase 7: parallel noise layer + always-on click transient
    case kPadNoiseLayerMix:        cfg.noiseLayerMix = normalizedValue; break;
    case kPadNoiseLayerCutoff:     cfg.noiseLayerCutoff = normalizedValue; break;
    case kPadNoiseLayerResonance:  cfg.noiseLayerResonance = normalizedValue; break;
    case kPadNoiseLayerDecay:      cfg.noiseLayerDecay = normalizedValue; break;
    case kPadNoiseLayerColor:      cfg.noiseLayerColor = normalizedValue; break;
    case kPadClickLayerMix:        cfg.clickLayerMix = normalizedValue; break;
    case kPadClickLayerContactMs:  cfg.clickLayerContactMs = normalizedValue; break;
    case kPadClickLayerBrightness: cfg.clickLayerBrightness = normalizedValue; break;
    // Phase 8A: per-mode damping law overrides.
    case kPadBodyDampingB1:        cfg.bodyDampingB1 = normalizedValue; break;
    case kPadBodyDampingB3:        cfg.bodyDampingB3 = normalizedValue; break;
    // Phase 8C: air-loading + per-mode scatter.
    case kPadAirLoading:           cfg.airLoading  = normalizedValue; break;
    case kPadModeScatter:          cfg.modeScatter = normalizedValue; break;
    // Phase 8D: head <-> shell coupling.
    case kPadCouplingStrength:     cfg.couplingStrength  = normalizedValue; break;
    case kPadSecondaryEnabled:     cfg.secondaryEnabled  = normalizedValue; break;
    case kPadSecondarySize:        cfg.secondarySize     = normalizedValue; break;
    case kPadSecondaryMaterial:    cfg.secondaryMaterial = normalizedValue; break;
    // Phase 8E: nonlinear tension modulation.
    case kPadTensionModAmt:        cfg.tensionModAmt     = normalizedValue; break;
    // Phase 8F: per-pad enable toggle (float-as-bool).
    case kPadEnabled:              cfg.enabled           = normalizedValue; break;
    // Phase 10: three-point pitch envelope extension.
    case kPadTSPitchEnvKneeEnabled: cfg.tsPitchEnvKneeEnabled = normalizedValue; break;
    case kPadTSPitchEnvMidPitch:    cfg.tsPitchEnvMidPitch    = normalizedValue; break;
    case kPadTSPitchEnvMidFraction: cfg.tsPitchEnvMidFraction = normalizedValue; break;
    case kPadTSPitchEnvCurve2:      cfg.tsPitchEnvCurve2      = normalizedValue; break;
    default: break;
    }
}

void VoicePool::setPadConfigSelector(int padIndex, int offset, int discreteValue) noexcept
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return;

    auto& cfg = padConfigs_[static_cast<std::size_t>(padIndex)];
    switch (offset)
    {
    case kPadExciterType:
        cfg.exciterType = static_cast<ExciterType>(
            std::clamp(discreteValue, 0, static_cast<int>(ExciterType::kCount) - 1));
        break;
    case kPadBodyModel:
        cfg.bodyModel = static_cast<BodyModelType>(
            std::clamp(discreteValue, 0, static_cast<int>(BodyModelType::kCount) - 1));
        break;
    default: break;
    }
}

const PadConfig& VoicePool::padConfig(int padIndex) const noexcept
{
    static const PadConfig kDefault{};
    if (padIndex < 0 || padIndex >= kNumPads)
        return kDefault;
    return padConfigs_[static_cast<std::size_t>(padIndex)];
}

PadConfig& VoicePool::padConfigMut(int padIndex) noexcept
{
    // Caller is responsible for valid index (state loading path).
    return padConfigs_[static_cast<std::size_t>(padIndex)];
}

void VoicePool::setPadChokeGroup(int padIndex, std::uint8_t group) noexcept
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return;
    if (group > 8) group = 0;
    padConfigs_[static_cast<std::size_t>(padIndex)].chokeGroup = group;
    chokeGroups_.setEntry(static_cast<std::size_t>(padIndex), group);
}

void VoicePool::panGainsForNote(std::uint8_t originatingNote,
                                float& gainL, float& gainR) const noexcept
{
    // Default: center / unity-to-both (legacy mono-dup behaviour).
    gainL = 1.0f;
    gainR = 1.0f;

    const int pad = static_cast<int>(originatingNote)
                  - static_cast<int>(kFirstDrumNote);
    if (pad < 0 || pad >= kNumPads)
        return;

    const float panNorm =
        std::clamp(padConfigs_[static_cast<std::size_t>(pad)].pan, 0.0f, 1.0f);

    // Equal-power pan referenced to unity-at-center. theta sweeps [0, pi/2] as
    // pan sweeps [0, 1]; the sqrt(2) factor lifts the -3 dB center of a textbook
    // equal-power law back to 0 dB so the default (pan 0.5) stays bit-identical
    // to the old `outL += s; outR += s` mix. gainL^2 + gainR^2 == 2 for all pan.
    constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;
    const float theta = panNorm * kHalfPi;
    gainL = std::numbers::sqrt2_v<float> * std::cos(theta);
    gainR = std::numbers::sqrt2_v<float> * std::sin(theta);
}

void VoicePool::applyPadConfigToSlot(int slot, int padIndex) noexcept
{
    if (slot < 0 || slot >= kMaxVoices)
        return;
    if (padIndex < 0 || padIndex >= kNumPads)
        return;

    // The whole PadConfig -> DrumVoice mapping lives in the shared helper
    // (dsp/pad_config_apply.h) so the offline fitter/render harness applies
    // the IDENTICAL surface (06-orchestralKit-fix-plan.md D1). Historical
    // note kept from the pre-extraction body: the tone-shaper block was
    // missing for years (a divergent dead-code helper in processor.cpp was
    // removed in audit L-12), so kit presets never propagated tone-shaper /
    // pitch-envelope / unnatural-zone / morph fields -- the 808 tom row
    // sounded identical across pads until it was restored.
    // T1.22: the voice plays the stored base fields with the pad's macro
    // offsets applied (a stack copy; the stored config is never modified).
    applyPadConfigToVoice(mainVoice(slot),
                          applyMacroOffsets(padConfigs_[static_cast<std::size_t>(padIndex)]));
}

// ------------------------------------------------------------------
// Query helpers
// ------------------------------------------------------------------

int VoicePool::getActiveVoiceCount() const noexcept
{
    int n = 0;
    for (int slot = 0; slot < maxPolyphony_; ++slot)
    {
        if (meta_[slot].state == VoiceSlotState::Active)
            ++n;
    }
    return n;
}

const VoiceMeta& VoicePool::voiceMeta(int slot) const noexcept
{
    return meta_[static_cast<std::size_t>(slot)];
}

const VoiceMeta& VoicePool::releasingMeta(int slot) const noexcept
{
    return releasingMeta_[static_cast<std::size_t>(slot)];
}

bool VoicePool::isAnyVoiceActive() const noexcept
{
    for (int slot = 0; slot < maxPolyphony_; ++slot)
    {
        if (meta_[slot].state == VoiceSlotState::Active)
            return true;
    }
    for (int slot = 0; slot < kMaxVoices; ++slot)
    {
        if (releasingMeta_[slot].state == VoiceSlotState::FastReleasing)
            return true;
    }
    return false;
}

// ------------------------------------------------------------------
// State (non-audio thread)
// ------------------------------------------------------------------

std::array<std::uint8_t, ChokeGroupTable::kSize>
VoicePool::getChokeGroupAssignments() const noexcept
{
    return chokeGroups_.raw();
}

void VoicePool::loadChokeGroupAssignments(
    const std::array<std::uint8_t, ChokeGroupTable::kSize>& in) noexcept
{
    chokeGroups_.loadFromRaw(in);
}

// ------------------------------------------------------------------
// Private helpers
// ------------------------------------------------------------------

int VoicePool::selectQuietestActiveSlot() const noexcept
{
    // FR-122 / FR-128: lowest currentLevel; tiebreak oldest noteOnSampleCount,
    // then ascending slot index. T1.9 (MB-049): a voice triggered in the
    // current block (noteOnSampleCount == sampleCounter_) has not rendered
    // yet, so it has no measured level -- its currentLevel 0 is not "quiet",
    // it is the most recent hit. Such voices are never candidates; if every
    // active voice was triggered in this block the function returns -1 and
    // the allocator's Oldest steal applies.
    int   bestSlot  = -1;
    float bestLevel = 0.0f;
    std::uint64_t bestAge = 0;
    for (int slot = 0; slot < maxPolyphony_; ++slot)
    {
        if (meta_[slot].state != VoiceSlotState::Active)
            continue;
        if (meta_[slot].noteOnSampleCount == sampleCounter_)
            continue;

        const float level = meta_[slot].currentLevel;
        const std::uint64_t age = meta_[slot].noteOnSampleCount;
        if (bestSlot < 0 ||
            level < bestLevel ||
            (level == bestLevel && age < bestAge))
        {
            bestSlot  = slot;
            bestLevel = level;
            bestAge   = age;
        }
    }
    return bestSlot;
}

void VoicePool::beginFastRelease(int slot, bool force, bool linear) noexcept
{
    if (slot < 0 || slot >= kMaxVoices)
        return;

    // FR-127: idempotent. If the shadow slot is already fast-releasing,
    // leave the in-flight fade alone so a re-entrant call (the choke-
    // completeness re-scan, the only caller that does not force) does NOT
    // re-snapshot the gain.
    //
    // Audit M-8 / T2.6 (MB-148): every caller that releases a sounding main
    // voice passes `force == true` -- a steal, a choke, a Quietest pre-steal,
    // a polyphony shrink and a kit load all mark the slot Free afterwards, so
    // returning early would leave the voice ringing unramped in a Free slot
    // (or let the next NoteOn on that slot hard-overwrite it). When this
    // slot's shadow is still fading from a PRIOR release, the current main
    // voice is re-snapshotted into the shadow so it fades smoothly, and only
    // the older, already-attenuated tail is truncated -- a far smaller
    // discontinuity. That old shadow voice lands in the main role and is
    // wiped below.
    if (releasingMeta_[slot].state == VoiceSlotState::FastReleasing && !force)
        return;

    // FR-124 / Q5 / T3.2.4: hand the currently-sounding DrumVoice to the
    // shadow role by exchanging the slot's two storage indices (audit
    // MB-005). No DrumVoice is moved or copied: the voices stay where they
    // are, so there is no ~270 KB stack temporary on the audio thread and
    // no self-referencing member (FMOperator's wavetable pointer) ever
    // points into another voice's storage. After the exchange:
    //   - shadowVoice(slot) is the currently-sounding voice (the "live"
    //     voice that must fade out).
    //   - mainVoice(slot) is the slot's previous shadow voice -- a
    //     freshly-prepared idle DrumVoice (first release on that slot), a
    //     finished fade (wiped by applyFastRelease, so inactive), or, in the
    //     forced case, the old shadow still mid-fade.
    std::swap(mainIndex_[static_cast<std::size_t>(slot)],
              shadowIndex_[static_cast<std::size_t>(slot)]);
    // T2.6 (MB-053): the voice left in the main role is always inactive,
    // whatever the caller does next. Only the forced case can bring a
    // sounding voice here (the old shadow, mid-fade): wipe it, so a choke,
    // pre-steal or shrink -- which mark the slot Free with no NoteOn after
    // them -- cannot leave it rendering as a ghost, and a steal's NoteOn
    // starts from a clean voice. An idle voice is left untouched (no
    // noteOff, no reset), so it stays bit-identical to a freshly-prepared
    // DrumVoice.
    if (mainVoice(slot).isActive())
        mainVoice(slot).resetForKitSwitch();

    // Phase 8A.5: the shadow voice (shadowVoice(slot)) is now the live
    // voice that must fade out. Under Option-D envelope (sustain = 1.0)
    // its body is still ringing at full amplitude, so the multiplicative
    // 5 ms fastRelease ramp alone is not enough to keep the residual
    // below the -30 dBFS click bound. Fire the aggressive fast-release
    // damp on the shadow's modal bank so the body decays within ~1 ms
    // (well inside the fast-release window) through its own damping law.
    // T2.3: a kit-load fade is linear and reaches exactly 0 in
    // kFastReleaseSecs, so it needs no damping (the damp alone drops the body
    // by ~11 % per sample at 48 kHz, a step at a crest).
    if (!linear)
        shadowVoice(slot).fastReleaseDamp();

    // Starting gain is UNITY. The shadow slot's DrumVoice already produces
    // the voice's absolute amplitude via its own envelopes and filters;
    // the fast-release ramp is a multiplicative decay applied on top of
    // that, starting from 1.0 so the transition is perfectly continuous
    // at the steal sample (FR-124: "applied starting from the voice's
    // current amplitude at the moment of steal" -- the voice's amplitude
    // IS the shadow's native output, which starts at unity gain).
    releasingMeta_[slot].fastReleaseGain   = 1.0f;
    releasingMeta_[slot].originatingNote   = meta_[slot].originatingNote;
    releasingMeta_[slot].originatingChoke  = meta_[slot].originatingChoke;
    releasingMeta_[slot].noteOnSampleCount = meta_[slot].noteOnSampleCount;
    releasingMeta_[slot].currentLevel      = 0.0f;
    releasingMeta_[slot].state             = VoiceSlotState::FastReleasing;
    releasingMeta_[slot].linearFadeStep    = linear ? linearFadeStep_ : 0.0f;
    // T2.3 (MB-054): the fade keeps the pan and bus the voice sounded with,
    // even when a kit load changes the pad's config under it.
    panGainsForNote(meta_[slot].originatingNote,
                    releasingMeta_[slot].panGainL, releasingMeta_[slot].panGainR);
    {
        const int pad = static_cast<int>(meta_[slot].originatingNote)
                      - static_cast<int>(kFirstDrumNote);
        releasingMeta_[slot].outputBus =
            (pad >= 0 && pad < kNumPads)
                ? padConfigs_[static_cast<std::size_t>(pad)].outputBus
                : std::uint8_t{0};
    }

    // NOTE: We deliberately do NOT call mainVoice(slot).noteOff() here. The
    // voice in the main role is inactive (see above); a steal re-configures
    // it with `applyPadConfigToSlot` + `noteOn(velocity)`, and every other
    // caller leaves the slot Free. The shadow voice keeps its own independent
    // state because it is a separate DrumVoice object.
}

int VoicePool::applyFastRelease(int slot,
                                float* scratch,
                                int numSamples) noexcept
{
    if (slot < 0 || slot >= kMaxVoices || scratch == nullptr || numSamples <= 0)
        return 0;

    // FR-124 / FR-125 / FR-164 / Q2 / Q5 / T3.2.5: per-sample exponential
    // decay. The 5 ms time constant is encoded in `fastReleaseK_`, computed
    // once during `prepare()` as exp(-1 / (0.005 * sampleRate)).
    //
    // The mandatory 1e-6 denormal floor is UNCONDITIONAL regardless of
    // FTZ/DAZ: when the running gain drops below `kFastReleaseFloor`, we
    // zero the remainder of the scratch buffer in-place, mark the slot
    // Free, return the count of pre-floor ("live") samples, and the
    // caller (`VoicePool::processBlock`) accumulates only those samples
    // into the output per FR-124's "does NOT accumulate those zeroed
    // samples into the output" clause.
    float gain = releasingMeta_[slot].fastReleaseGain;
    const float k = fastReleaseK_;

    int liveCount = numSamples;

    // T2.3: a kit-load fade lowers the gain linearly to 0 over
    // kFastReleaseSecs (the samples after the end are zeroed and not
    // accumulated, as for the exponential ramp below).
    const float step = releasingMeta_[slot].linearFadeStep;
    if (step > 0.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            scratch[i] *= gain;
            gain -= step;
            if (gain <= 0.5f * step)
            {
                gain = 0.0f;
                for (int j = i + 1; j < numSamples; ++j)
                    scratch[j] = 0.0f;
                releasingMeta_[slot].state = VoiceSlotState::Free;
                // T2.6 (MB-053): see the exponential branch below.
                shadowVoice(slot).resetForKitSwitch();
                liveCount = i + 1;
                break;
            }
        }
        releasingMeta_[slot].fastReleaseGain = gain;
        return liveCount;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        scratch[i] *= gain;
        gain *= k;
        if (gain < kFastReleaseFloor)
        {
            gain = 0.0f;
            // Zero the remainder of the scratch buffer.
            for (int j = i + 1; j < numSamples; ++j)
                scratch[j] = 0.0f;
            releasingMeta_[slot].state = VoiceSlotState::Free;
            // T2.6 (MB-053): the fade only damps the shadow's bank; the voice
            // itself would stay active with frozen state, and the next
            // hand-over on this slot would bring it back into the main role
            // as a ghost. A finished shadow is wiped and inactive.
            shadowVoice(slot).resetForKitSwitch();
            liveCount = i + 1;
            break;
        }
    }

    releasingMeta_[slot].fastReleaseGain = gain;
    return liveCount;
}

void VoicePool::processChokeGroups(std::uint8_t newNote) noexcept
{
    // FR-132 / FR-133 / FR-134 / FR-135 / FR-136 / FR-137 / FR-138:
    // On every note-on, before the allocator selects a slot, we iterate the
    // pool and fast-release every voice that shares the new note's choke
    // group. Group 0 means "no choke" and we early-out without iterating
    // (FR-136 -- zero pool iteration overhead for the default case).
    //
    // Both arrays must be scanned:
    //   - `meta_`          : Active main voices -- these are the normal
    //                        targets; beginFastRelease swaps them into the
    //                        shadow array and starts the 5 ms ramp.
    //   - `releasingMeta_` : Voices already fast-releasing from a prior
    //                        steal or choke. If their choke group matches
    //                        we leave them alone -- beginFastRelease is
    //                        idempotent (FR-127) and the shadow slot is
    //                        already terminating. Iterating them is the
    //                        spec's "active + releasing" language for
    //                        FR-133 completeness but in practice
    //                        `beginFastRelease` short-circuits on
    //                        FastReleasing. We still test the condition so
    //                        the intent is explicit.
    //
    // The newly-triggered voice is NOT yet in `meta_[]` at this point
    // (allocator.noteOn runs AFTER this call in VoicePool::noteOn), so it
    // cannot accidentally self-choke.
    const std::uint8_t group = chokeGroups_.lookup(newNote);
    if (group == 0U)
        return;  // FR-136 early-out

    for (int slot = 0; slot < kMaxVoices; ++slot)
    {
        // FR-133: check active main voices first.
        if (meta_[slot].state == VoiceSlotState::Active &&
            meta_[slot].originatingChoke == group)
        {
            // Phase 5: Release coupling resonators for choked voice.
            if (couplingEngine_ != nullptr)
                couplingEngine_->noteOff(slot);

            // FR-134: reuse the Phase 3.2 fast-release path for click-free
            // group-wide mute. beginFastRelease swaps the main voice into
            // the shadow slot and starts the 5 ms ramp. T2.6 (MB-148):
            // forced, so a shadow still fading from a steal on this slot
            // cannot leave the choked voice ringing in a Free slot.
            beginFastRelease(slot, /*force*/ true);
            // FR-133: mark the main slot Free so subsequent allocator
            // bookkeeping sees it as available. We also release the
            // allocator's view by issuing a noteOff + voiceFinished pair
            // on the originating note -- otherwise the allocator would
            // still believe the slot is Active and would issue a Steal
            // event on a later note-on, causing a double-fade.
            const std::uint8_t victimNote = meta_[slot].originatingNote;
            meta_[slot].state = VoiceSlotState::Free;
            (void)allocator_.noteOff(victimNote);
            allocator_.voiceFinished(static_cast<std::size_t>(slot));
            continue;
        }

        // FR-133 completeness: match voices that are already fast-releasing
        // but share the choke group. beginFastRelease is idempotent
        // (FR-127) so this is a no-op for slots already terminating, but
        // we preserve the pattern per the spec.
        if (releasingMeta_[slot].state == VoiceSlotState::FastReleasing &&
            releasingMeta_[slot].originatingChoke == group)
        {
            beginFastRelease(slot);
        }
    }
}

} // namespace Membrum
