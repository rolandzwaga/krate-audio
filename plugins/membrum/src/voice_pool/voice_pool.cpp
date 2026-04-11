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
    if (sampleRate < 22050.0) sampleRate = 22050.0;
    if (sampleRate > 192000.0) sampleRate = 192000.0;
    sampleRate_ = sampleRate;

    if (maxBlockSize < 1) maxBlockSize = 1;
    if (maxBlockSize > kVoicePoolMaxBlock) maxBlockSize = kVoicePoolMaxBlock;
    maxBlockSize_ = maxBlockSize;

    // FR-116/FR-117/Q4: this is the ONLY allocation point in VoicePool. All
    // subsequent audio-thread calls are allocation-free.
    scratchL_ = std::make_unique<float[]>(static_cast<std::size_t>(maxBlockSize_));
    scratchR_ = std::make_unique<float[]>(static_cast<std::size_t>(maxBlockSize_));

    // FR-124/FR-125: 5 ms exponential decay coefficient.
    fastReleaseK_ =
        std::exp(-1.0f / (kFastReleaseSecs * static_cast<float>(sampleRate_)));

    // Prepare every main + shadow voice with a unique voiceId so per-voice
    // PRNGs (friction, noise burst, etc.) are decorrelated across the pool.
    for (int i = 0; i < kMaxVoices; ++i)
    {
        VP_VOICES[i].prepare(sampleRate_,
                             static_cast<std::uint32_t>(i));
        VP_RVOICES[i].prepare(
            sampleRate_,
            static_cast<std::uint32_t>(i + kMaxVoices));

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
        (static_cast<int>(allocator_.getActiveVoiceCount()) >= maxPolyphony_);

    if (stealingPolicy_ == VoiceStealingPolicy::Quietest && poolFull)
    {
        const int q = selectQuietestActiveSlot();
        if (q >= 0)
        {
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
    const std::uint8_t velocityByte =
        static_cast<std::uint8_t>(clampedVel * 127.0f + 0.5f);

    const auto events = allocator_.noteOn(midiNote, velocityByte);

    // Step 4: walk the event list and translate it into voice actions.
    for (const auto& ev : events)
    {
        switch (ev.type)
        {
        case Krate::DSP::VoiceEvent::Type::Steal:
        {
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

            // Configure the voice from the shared template (FR-170).
            applySharedParamsToSlot(slot);
            VP_VOICES[slot].noteOn(clampedVel);

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
                if (a > peak) peak = a;
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
            // Voice naturally finished -- FR-115.
            meta_[slot].state        = VoiceSlotState::Free;
            meta_[slot].currentLevel = 0.0f;
            allocator_.voiceFinished(static_cast<std::size_t>(slot));
        }
    }

    // Phase 3.1: shadow slots are NOT rendered. beginFastRelease set the
    // FastReleasing state for bookkeeping only; here we terminate each
    // releasing slot immediately (accepting the click on steal -- Phase 3.2
    // replaces this with the 5 ms exponential tail).
    for (int slot = 0; slot < kMaxVoices; ++slot)
    {
        if (releasingMeta_[slot].state == VoiceSlotState::FastReleasing)
        {
            releasingMeta_[slot].state = VoiceSlotState::Free;
        }
    }

    sampleCounter_ += static_cast<std::uint64_t>(numSamples);
}

// ------------------------------------------------------------------
// Configuration
// ------------------------------------------------------------------

void VoicePool::setMaxPolyphony(int n) noexcept
{
    if (n < 4) n = 4;
    if (n > kMaxVoices) n = kMaxVoices;

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
// Shared parameter setters
// ------------------------------------------------------------------

void VoicePool::setSharedVoiceParams(float material,
                                     float size,
                                     float decay,
                                     float strikePos,
                                     float level) noexcept
{
    sharedParams_.material  = material;
    sharedParams_.size      = size;
    sharedParams_.decay     = decay;
    sharedParams_.strikePos = strikePos;
    sharedParams_.level     = level;
}

void VoicePool::setSharedExciterType(ExciterType type) noexcept
{
    sharedParams_.exciterType = type;
}

void VoicePool::setSharedBodyModel(BodyModelType model) noexcept
{
    sharedParams_.bodyModel = model;
}

void VoicePool::applySharedParamsToSlot(int slot) noexcept
{
    if (slot < 0 || slot >= kMaxVoices)
        return;

    auto& v = VP_VOICES[slot];
    v.setExciterType(sharedParams_.exciterType);
    v.setBodyModel(sharedParams_.bodyModel);
    v.setMaterial(sharedParams_.material);
    v.setSize(sharedParams_.size);
    v.setDecay(sharedParams_.decay);
    v.setStrikePosition(sharedParams_.strikePos);
    v.setLevel(sharedParams_.level);
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
    // leave the in-flight fade alone.
    if (releasingMeta_[slot].state == VoiceSlotState::FastReleasing)
        return;

    // Phase 3.1 stub: mark the shadow-slot bookkeeping so Phase 3.1 tests
    // can observe the FastReleasing transition (FR-111 / SC-031). We do NOT
    // copy voices_[slot] into releasingVoices_[slot] here because DrumVoice
    // is not std::is_trivially_copyable -- a bitwise copy would be UB.
    // Phase 3.2 replaces this with a real crossfade snapshot and enables
    // the audible fast-release tail.
    releasingMeta_[slot].fastReleaseGain   = kFastReleaseFloor;
    releasingMeta_[slot].originatingNote   = meta_[slot].originatingNote;
    releasingMeta_[slot].originatingChoke  = meta_[slot].originatingChoke;
    releasingMeta_[slot].noteOnSampleCount = meta_[slot].noteOnSampleCount;
    releasingMeta_[slot].currentLevel      = 0.0f;
    releasingMeta_[slot].state             = VoiceSlotState::FastReleasing;

    // Hard-stop the main voice. Phase 3.1 still accepts audible clicks at
    // steal time, but the envelope must release so a later retrigger on
    // this slot starts from a clean envelope state. DrumVoice::noteOff()
    // gates the amp envelope off and releases the exciter; it does NOT
    // allocate.
    VP_VOICES[slot].noteOff();
}

void VoicePool::applyFastRelease(int slot,
                                 float* scratch,
                                 int numSamples) noexcept
{
    if (slot < 0 || slot >= kMaxVoices || scratch == nullptr)
        return;

    // Phase 3.1 stub: apply a constant gain equal to the snapshotted
    // fastReleaseGain for one block, then immediately transition the slot
    // back to Free. Phase 3.2 replaces this with the full 5 ms exponential
    // decay + 1e-6 denormal floor (FR-124, FR-164).
    const float gain = releasingMeta_[slot].fastReleaseGain;
    for (int i = 0; i < numSamples; ++i)
        scratch[i] *= gain;

    // Immediate termination — Phase 3.1 accepts clicks on steal (the tests
    // that validate click-free steals live in Phase 3.2).
    releasingMeta_[slot].state = VoiceSlotState::Free;
}

void VoicePool::processChokeGroups(std::uint8_t /*newNote*/) noexcept
{
    // Phase 3.1 scaffolding stub -- Phase 3.3 fills in the choke-iteration
    // body. Leaving this empty is safe in Phase 3.1: tests exercising choke
    // behaviour land in Phase 3.3.
}

} // namespace Membrum
