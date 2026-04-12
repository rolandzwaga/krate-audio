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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Membrum {

// Aliases to make indexing readable without the `(*voicesPtr_)[i]` noise.
#define VP_VOICES   (*voicesPtr_)
#define VP_RVOICES  (*releasingVoicesPtr_)

// ------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------

VoicePool::VoicePool()
    : voicesPtr_(std::make_unique<std::array<DrumVoice, kMaxVoices>>())
    , releasingVoicesPtr_(std::make_unique<std::array<DrumVoice, kMaxVoices>>())
{
    // Both DrumVoice arrays are default-constructed on the heap via
    // make_unique. Per FR-116 this is the only allocation point outside of
    // prepare() -- it runs on the host thread during Processor construction,
    // not on the audio thread.
}

DrumVoice& VoicePool::mainVoiceRef(int slot) noexcept
{
    return (*voicesPtr_)[static_cast<std::size_t>(slot)];
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

    maxBlockSize = std::clamp(maxBlockSize, 1, kVoicePoolMaxBlock);
    maxBlockSize_ = maxBlockSize;

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

    // Prepare every main + shadow voice with voiceId = slot index. Main and
    // shadow slots at the same index share the same voiceId because the
    // two-array fast-release crossfade (beginFastRelease) swaps them via
    // std::swap, and any voiceId-derived state (PRNG seeds, per-voice
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
        VP_VOICES[i].prepare(sampleRate_, vid);
        VP_RVOICES[i].prepare(sampleRate_, vid);

        meta_[i]          = VoiceMeta{};
        releasingMeta_[i] = VoiceMeta{};
    }

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

    // FR-171: note-on sequence = (choke) -> (steal) -> allocator -> DrumVoice.
    // Step 1: choke group iteration. Phase 3.3 fills this in; Phase 3.1 is a
    // no-op stub so the code path compiles.
    processChokeGroups(midiNote);

    // Step 2: if Quietest policy AND the pool is already full, pre-select the
    // quietest active slot and release it so the allocator sees an Idle slot
    // (Clarification Q3 / FR-122). We must yield the slot to the allocator
    // via noteOff + voiceFinished so the next allocator.noteOn() call can
    // pick it up without issuing its own Steal event.
    const bool poolFull =
        std::cmp_greater_equal(allocator_.getActiveVoiceCount(), maxPolyphony_);

    if (stealingPolicy_ == VoiceStealingPolicy::Quietest && poolFull)
    {
        const int q = selectQuietestActiveSlot();
        if (q >= 0)
        {
            // Phase 5: Release coupling resonators for the stolen voice.
            if (couplingEngine_ != nullptr)
                couplingEngine_->noteOff(q);

            const uint8_t victimNote = meta_[q].originatingNote;
            beginFastRelease(q);
            (void)allocator_.noteOff(victimNote);
            allocator_.voiceFinished(static_cast<std::size_t>(q));
            // Mark the main slot Free so our own bookkeeping agrees with the
            // allocator's Idle view (the audible tail continues from the
            // shadow slot).
            meta_[q].state = VoiceSlotState::Free;
        }
    }
    else
    {
        // FR-121 / FR-123: Oldest -> AllocationMode::Oldest;
        //                  Priority -> AllocationMode::HighestNote.
        const auto mode =
            (stealingPolicy_ == VoiceStealingPolicy::Priority)
                ? Krate::DSP::AllocationMode::HighestNote
                : Krate::DSP::AllocationMode::Oldest;
        allocator_.setAllocationMode(mode);
    }

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
            // slot. The hot path must not touch `voices_[ev.voiceIndex]` after
            // this — the new note will overwrite it below.
            beginFastRelease(static_cast<int>(ev.voiceIndex));
            // After stealing we mark the main slot Free; the NoteOn event
            // that follows will transition it back to Active.
            meta_[ev.voiceIndex].state = VoiceSlotState::Free;
            break;
        }
        case Krate::DSP::VoiceEvent::Type::NoteOn:
        {
            const int slot = static_cast<int>(ev.voiceIndex);

            // Configure the voice from the pad's config (Phase 4 per-pad dispatch).
            applyPadConfigToSlot(slot, padIndex);
            VP_VOICES[slot].noteOn(clampedVel);

            // Phase 5: Register this voice's partials with the coupling engine.
            // The engine will create sympathetic resonators at the voice's
            // modal frequencies (FR-041: velocity scales coupling excitation).
            if (couplingEngine_ != nullptr)
            {
                auto partials = VP_VOICES[slot].getPartialInfo();
                couplingEngine_->noteOn(slot, partials);
            }

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

void VoicePool::noteOff(std::uint8_t midiNote) noexcept
{
    // FR-114: percussion is a no-op at the voice level. We still call
    // `allocator_.noteOff` so the allocator's Active/Releasing bookkeeping
    // stays consistent. We do NOT gate the amp envelope off; the voice
    // continues decaying naturally until processBlock observes
    // `!isActive()` and calls voiceFinished.
    (void)allocator_.noteOff(midiNote);
}

// ------------------------------------------------------------------
// Audio processing
// ------------------------------------------------------------------

void VoicePool::processBlock(float* outL, float* outR, int numSamples) noexcept
{
    if (numSamples <= 0 || outL == nullptr || outR == nullptr)
        return;

    // Zero the output buffers first (FR-165 per-block accumulate model).
    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] = 0.0f;
        outR[i] = 0.0f;
    }

    float* scratch = scratchL_.get();

    // Render every active main voice, compute its per-block peak, then
    // accumulate into both output channels. DrumVoice is mono — the pool
    // mirrors mono-to-stereo here.
    // Silence threshold below which a sustaining voice is considered
    // "naturally finished" (FR-115). For percussion with sustain=0 the amp
    // envelope never transitions to Idle on its own; the pool promotes the
    // slot to Release once the block peak stays below this level, then the
    // envelope's Release-stage idle check (`output_ < kEnvelopeIdleThreshold`)
    // frees the slot on a subsequent block.
    constexpr float kSilenceThreshold = 1.0e-5f;   // ~ -100 dBFS

    for (int slot = 0; slot < maxPolyphony_; ++slot)
    {
        if (VP_VOICES[slot].isActive())
        {
            VP_VOICES[slot].processBlock(scratch, numSamples);

            // FR-165: per-block peak into VoiceMeta.currentLevel. One pass
            // through the scratch, then one write. Not per-sample.
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float a = std::fabs(scratch[i]);
                peak           = std::max(peak, a);
                outL[i] += scratch[i];
                outR[i] += scratch[i];
            }
            meta_[slot].currentLevel = peak;

            // Auto-release once the voice has decayed below the silence
            // threshold. Gate the envelope off from this side so the
            // per-slot `isActive()` poll below eventually flips false.
            if (peak < kSilenceThreshold &&
                meta_[slot].state == VoiceSlotState::Active)
            {
                VP_VOICES[slot].noteOff();
            }
        }
        else if (meta_[slot].state == VoiceSlotState::Active)
        {
            // Phase 5: Release coupling resonators for naturally finished voice.
            if (couplingEngine_ != nullptr)
                couplingEngine_->noteOff(slot);

            // Voice naturally finished -- FR-115.
            meta_[slot].state        = VoiceSlotState::Free;
            meta_[slot].currentLevel = 0.0f;
            allocator_.voiceFinished(static_cast<std::size_t>(slot));
        }
    }

    // T3.2.6 / FR-124 / Q5: render every fast-releasing shadow voice, apply
    // the per-sample exponential decay to the scratch buffer, then
    // accumulate only the live (pre-floor) samples into the output per
    // FR-124's "does NOT accumulate those zeroed samples into the output"
    // clause.
    for (int slot = 0; slot < kMaxVoices; ++slot)
    {
        if (releasingMeta_[slot].state != VoiceSlotState::FastReleasing)
            continue;

        // Render the shadow copy of the drum voice into scratchL_.
        VP_RVOICES[slot].processBlock(scratch, numSamples);

        // Apply the exponential fast-release ramp. Returns the count of
        // samples before the 1e-6 floor triggered (numSamples if the fade
        // continues into the next block).
        const int liveCount = applyFastRelease(slot, scratch, numSamples);

        for (int i = 0; i < liveCount; ++i)
        {
            outL[i] += scratch[i];
            outR[i] += scratch[i];
        }

        // When the slot has transitioned back to Free (floor triggered),
        // the main-slot allocator bookkeeping was already cleared by
        // `noteOn`'s Steal event handler. The shadow slot does not need
        // its own voiceFinished() call because the main slot was
        // already released from the allocator's perspective at the
        // moment of the steal.
    }

    sampleCounter_ += static_cast<std::uint64_t>(numSamples);
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

    // Zero the main output buffers (FR-165 per-block accumulate model).
    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] = 0.0f;
        outR[i] = 0.0f;
    }

    float* scratch = scratchL_.get();
    constexpr float kSilenceThreshold = 1.0e-5f;

    for (int slot = 0; slot < maxPolyphony_; ++slot)
    {
        if (VP_VOICES[slot].isActive())
        {
            VP_VOICES[slot].processBlock(scratch, numSamples);

            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float a = std::fabs(scratch[i]);
                peak = std::max(peak, a);
                outL[i] += scratch[i];
                outR[i] += scratch[i];
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
                        auxL[bus][i] += scratch[i];
                        auxR[bus][i] += scratch[i];
                    }
                }
            }

            if (peak < kSilenceThreshold &&
                meta_[slot].state == VoiceSlotState::Active)
            {
                VP_VOICES[slot].noteOff();
            }
        }
        else if (meta_[slot].state == VoiceSlotState::Active)
        {
            meta_[slot].state        = VoiceSlotState::Free;
            meta_[slot].currentLevel = 0.0f;
            allocator_.voiceFinished(static_cast<std::size_t>(slot));
        }
    }

    // Fast-releasing shadow voices
    for (int slot = 0; slot < kMaxVoices; ++slot)
    {
        if (releasingMeta_[slot].state != VoiceSlotState::FastReleasing)
            continue;

        VP_RVOICES[slot].processBlock(scratch, numSamples);
        const int liveCount = applyFastRelease(slot, scratch, numSamples);

        for (int i = 0; i < liveCount; ++i)
        {
            outL[i] += scratch[i];
            outR[i] += scratch[i];
        }

        // FR-044: also route fast-releasing voices to their aux bus
        const int padIndex = static_cast<int>(releasingMeta_[slot].originatingNote)
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
                for (int i = 0; i < liveCount; ++i)
                {
                    auxL[bus][i] += scratch[i];
                    auxR[bus][i] += scratch[i];
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
            beginFastRelease(static_cast<int>(ev.voiceIndex));
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

void VoicePool::applyPadConfigToSlot(int slot, int padIndex) noexcept
{
    if (slot < 0 || slot >= kMaxVoices)
        return;
    if (padIndex < 0 || padIndex >= kNumPads)
        return;

    const auto& cfg = padConfigs_[static_cast<std::size_t>(padIndex)];
    auto& v = VP_VOICES[slot];
    v.setExciterType(cfg.exciterType);
    v.setBodyModel(cfg.bodyModel);
    v.setMaterial(cfg.material);
    v.setSize(cfg.size);
    v.setDecay(cfg.decay);
    v.setStrikePosition(cfg.strikePosition);
    v.setLevel(cfg.level);
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
    // then ascending slot index.
    int   bestSlot  = -1;
    float bestLevel = 0.0f;
    std::uint64_t bestAge = 0;
    for (int slot = 0; slot < maxPolyphony_; ++slot)
    {
        if (meta_[slot].state != VoiceSlotState::Active)
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

void VoicePool::beginFastRelease(int slot) noexcept
{
    if (slot < 0 || slot >= kMaxVoices)
        return;

    // FR-127: idempotent. If the shadow slot is already fast-releasing,
    // leave the in-flight fade alone so a double-steal does NOT re-snapshot
    // the gain.
    if (releasingMeta_[slot].state == VoiceSlotState::FastReleasing)
        return;

    // FR-124 / Q5 / T3.2.4: snapshot the currently-sounding DrumVoice into
    // the shadow slot. DrumVoice's implicit COPY assignment is DELETED by
    // the compiler (one of the exciter variant alternatives,
    // FrictionExciter -> Krate::DSP::BowExciter, explicitly deletes its
    // copy operators). DrumVoice's MOVE assignment is however defaulted
    // and functional -- BowExciter declares `= default` move ops and
    // std::variant of move-only alternatives is itself move-assignable.
    // We therefore snapshot via `std::swap`, which is implemented in
    // terms of move-assignment. After the swap:
    //   - releasingVoices_[slot] now holds the currently-sounding voice
    //     state (the "live" voice that must fade out).
    //   - voices_[slot] holds the previous contents of
    //     releasingVoices_[slot] -- either a freshly-prepared idle
    //     DrumVoice (first steal on that slot) or a naturally-ended
    //     fast-releasing voice from a prior fade (both of which are
    //     valid DSP states ready to receive a new noteOn).
    // The subsequent allocator NoteOn path calls
    // `applySharedParamsToSlot(slot)` + `voices_[slot].noteOn(v)` which
    // fully re-initializes the envelope and re-triggers the exciter, so
    // any residual state in voices_[slot] after the swap is overwritten
    // cleanly. We must call noteOff() on the re-acquired voice first to
    // gate the envelope off and release the exciter, otherwise a lingering
    // amp-envelope from a prior fade could add a tiny transient to the new
    // note's attack.
    static_assert(std::is_move_assignable_v<DrumVoice>,
                  "DrumVoice must be move-assignable for the shadow swap "
                  "technique (Phase 3.2 fast-release).");
    std::swap(VP_RVOICES[slot], VP_VOICES[slot]);
    // The re-acquired main voice receives a fresh noteOn from the
    // subsequent allocator NoteOn event, which fully re-initializes
    // the envelope and re-triggers the exciter. We do NOT call
    // noteOff() on it here: that would add unnecessary DSP state
    // churn (small but non-zero) and could introduce bit-level
    // differences versus a freshly-prepared DrumVoice.

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

    // NOTE: We deliberately do NOT call VP_VOICES[slot].noteOff() here.
    // The main slot is about to be overwritten by the new note's
    // `applySharedParamsToSlot` + `noteOn(velocity)` call, which will reset
    // the amp envelope and exciter cleanly. Calling noteOff first would
    // waste a few instructions and could affect envelope timing on the
    // new note's attack. The shadow copy retains its own independent state
    // because we duplicated the DrumVoice above.
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
            // the shadow slot and starts the 5 ms ramp.
            beginFastRelease(slot);
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
