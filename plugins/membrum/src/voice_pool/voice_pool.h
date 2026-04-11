#pragma once

// ==============================================================================
// VoicePool -- Phase 3 polyphonic voice pool for Membrum
// ==============================================================================
// Phase 3 wraps Phase 2's single `Membrum::DrumVoice` in a fixed 16-slot pool
// driven by the existing `Krate::DSP::VoiceAllocator`. Up to `maxPolyphony`
// voices (4..16, default 8) sound concurrently, with click-free voice
// stealing via an exponential fast-release envelope, three user-selectable
// stealing policies (Oldest, Quietest, Priority), and 8 choke groups
// (plus group 0 = none) via a 32-entry `chokeGroupAssignments` table.
//
// Clarification Q5: `DrumVoice` is NOT modified by Phase 3. The fast-release
// gain ramp is applied by `VoicePool::processBlock` to a local scratch buffer,
// never inside `DrumVoice`.
//
// Two-slot crossfade technique (plan.md Architecture): to keep the old voice
// audible during its 5 ms fade, the pool holds a shadow
// `std::array<DrumVoice, kMaxVoices>` + shadow meta array and renders both
// arrays each block. One fade-out is in flight per slot at a time.
//
// ------------------------------------------------------------------
// sizeof(DrumVoice) decision (plan.md Open Question #1)
// ------------------------------------------------------------------
// Measured on MSVC x64 Release (Phase 3.0, test_voice_pool_scaffold.cpp):
//   sizeof(DrumVoice) = 224 096 bytes (~218.8 KiB)
//   32 * sizeof(DrumVoice) = ~6.84 MiB
//
// Decision: per the plan's criteria (`sizeof > 32768` -> fallback), Phase 3
// takes the **per-slot single-fade reservation** path: exactly one fade-out
// in flight per slot at a time. `voices_[16]` + `releasingVoices_[16]` are
// both declared here so Phase 3.2's fast-release ramp can crossfade without
// touching `DrumVoice` (FR-194), but `VoicePool::beginFastRelease` will
// reject re-entry on a slot that is already `FastReleasing` (FR-127) --
// ensuring the shadow footprint stays bounded at `kMaxVoices` entries.
// The ~6.84 MiB peak is accepted as the Phase 3 memory cost; see
// `plan.md §Complexity Tracking` for the full rationale.
//
// Phase 3.0: scaffolding only -- all methods are stubs. Real bodies land in
// Phases 3.1 (allocator integration) and 3.2 (fast-release ramp).
//
// FR references:
//   FR-110, FR-111, FR-112, FR-114, FR-115, FR-116, FR-117, FR-120..125,
//   FR-127, FR-128, FR-130, FR-132..136, FR-138, FR-150..153, FR-163..165,
//   FR-170..172, FR-180, FR-187, FR-193, FR-194
// ==============================================================================

#include "choke_group_table.h"
#include "voice_meta.h"
#include "voice_stealing_policy.h"

#include "../dsp/drum_voice.h"

#include <krate/dsp/systems/voice_allocator.h>

#include <array>
#include <cstdint>
#include <memory>

namespace Membrum {

// ------------------------------------------------------------------
// Compile-time constants
// ------------------------------------------------------------------

constexpr int   kMaxVoices         = 16;      // FR-110
constexpr int   kVoicePoolMaxBlock = 2048;    // FR-117 scratch buffer max block size
constexpr float kFastReleaseSecs   = 0.005f;  // FR-124 exponential 5 ms tau
constexpr float kFastReleaseFloor  = 1e-6f;   // FR-164 denormal guard

class VoicePool
{
public:
    VoicePool() = default;

    // ------------------------------------------------------------------
    // Lifecycle -- setup thread, allocation allowed only here
    // ------------------------------------------------------------------

    /// Allocate scratch buffers and seed all 16 DrumVoice instances. Must be
    /// called from `Processor::setupProcessing` / `setActive(true)`. This is
    /// the **only** method that may allocate memory (FR-116 / FR-117).
    void prepare(double sampleRate, int maxBlockSize) noexcept;

    // ------------------------------------------------------------------
    // Note events -- audio thread, allocation-free, noexcept
    // ------------------------------------------------------------------

    /// Route a MIDI note-on through the allocator, applying the current
    /// stealing policy and choke group. `midiNote` must be in [36, 67] -- the
    /// Processor drops notes outside that range before calling this.
    void noteOn(std::uint8_t midiNote, float velocity) noexcept;

    /// Percussion note-off: bookkeeping only (FR-114). The voice continues
    /// decaying naturally until its amp envelope hits the silence threshold,
    /// at which point `processBlock` frees the slot.
    void noteOff(std::uint8_t midiNote) noexcept;

    // ------------------------------------------------------------------
    // Audio processing -- audio thread
    // ------------------------------------------------------------------

    /// Render every active and fast-releasing voice into `outL` / `outR`.
    void processBlock(float* outL, float* outR, int numSamples) noexcept;

    // ------------------------------------------------------------------
    // Configuration -- audio thread via `processParameterChanges`
    // ------------------------------------------------------------------

    /// FR-111 -- clamped to [4, 16]. Shrinks trigger fast-release on any
    /// slots released by `allocator_.setVoiceCount(n)`.
    void setMaxPolyphony(int n) noexcept;

    /// FR-120 -- stores the policy; takes effect on the next `noteOn`.
    /// Currently-sounding voices are NOT disturbed (FR-152).
    void setVoiceStealingPolicy(VoiceStealingPolicy p) noexcept;

    /// FR-138 -- writes the same `group` value into all 32 entries of the
    /// `ChokeGroupTable` (Phase 3 single-pad template per Clarification Q1).
    void setChokeGroup(std::uint8_t group) noexcept;

    // ------------------------------------------------------------------
    // Shared parameter snapshots -- propagated from Processor atomics
    // ------------------------------------------------------------------

    /// Phase 1 parameter snapshot, read on the next `noteOn`. Does not
    /// disturb currently-sounding voices.
    void setSharedVoiceParams(float material,
                              float size,
                              float decay,
                              float strikePos,
                              float level) noexcept;

    // ------------------------------------------------------------------
    // State (non-audio thread)
    // ------------------------------------------------------------------

    /// FR-141 -- serializes the 32-entry choke-group assignments block for
    /// `getState`.
    [[nodiscard]] std::array<std::uint8_t, ChokeGroupTable::kSize>
    getChokeGroupAssignments() const noexcept;

    /// FR-144 -- loads the 32-entry table during `setState`, clamping each
    /// byte to [0, 8].
    void loadChokeGroupAssignments(
        const std::array<std::uint8_t, ChokeGroupTable::kSize>& in) noexcept;

    // ------------------------------------------------------------------
    // Query helpers (Phase 3.0 stubs)
    // ------------------------------------------------------------------

    [[nodiscard]] int  maxPolyphony() const noexcept { return maxPolyphony_; }
    [[nodiscard]] bool isAnyVoiceActive() const noexcept;

private:
    // ------------------------------------------------------------------
    // Main voice storage (FR-110) -- always sized `kMaxVoices`; the active
    // count is `maxPolyphony_`, controlled by `allocator_.setVoiceCount`.
    // ------------------------------------------------------------------
    std::array<DrumVoice, kMaxVoices> voices_{};
    std::array<VoiceMeta, kMaxVoices> meta_{};

    // ------------------------------------------------------------------
    // Shadow voice storage for the two-array fast-release crossfade. One
    // fade-out in flight per slot at a time. See `voice_pool.h` comment at
    // top for the fallback decision criteria.
    // ------------------------------------------------------------------
    std::array<DrumVoice, kMaxVoices> releasingVoices_{};
    std::array<VoiceMeta, kMaxVoices> releasingMeta_{};

    // ------------------------------------------------------------------
    // Allocator + choke table
    // ------------------------------------------------------------------
    Krate::DSP::VoiceAllocator allocator_{};   // FR-112
    ChokeGroupTable            chokeGroups_{}; // FR-130

    // ------------------------------------------------------------------
    // Scratch buffers -- FR-117 / Clarification Q4. Mono, because DrumVoice
    // is mono. The right-channel scratch is reserved for a future stereo
    // DrumVoice (Phase 4+).
    // ------------------------------------------------------------------
    std::unique_ptr<float[]> scratchL_{};
    std::unique_ptr<float[]> scratchR_{};

    int    maxBlockSize_   = 0;
    double sampleRate_     = 44100.0;
    float  fastReleaseK_   = 0.0f;  // exp(-1 / (tau * sampleRate))

    int                  maxPolyphony_  = 8;                           // FR-111
    VoiceStealingPolicy  stealingPolicy_= VoiceStealingPolicy::Oldest; // FR-120
    std::uint64_t        sampleCounter_ = 0;                           // FR-128
};

} // namespace Membrum
