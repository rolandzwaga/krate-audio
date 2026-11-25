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
// audible during its 5 ms fade, each slot owns a second (shadow) DrumVoice +
// shadow meta entry, and the pool renders both roles each block. One
// fade-out is in flight per slot at a time. A steal or choke hands the
// sounding voice to the shadow role by exchanging two indices, never by
// moving a DrumVoice (audit MB-005).
//
// ------------------------------------------------------------------
// sizeof(DrumVoice) decision (plan.md Open Question #1)
// ------------------------------------------------------------------
// Measured on MSVC x64 Release (test_voice_pool_scaffold.cpp, re-measured
// 2026-11-24 by audit T2.5):
//   sizeof(DrumVoice) = 276 576 bytes (~270.1 KiB)
//   32 * sizeof(DrumVoice) = ~8.44 MiB
//
// Decision: per the plan's criteria (`sizeof > 32768` -> fallback), Phase 3
// takes the **per-slot single-fade reservation** path: exactly one fade-out
// in flight per slot at a time. 16 main + 16 shadow voices are held here so
// Phase 3.2's fast-release ramp can crossfade without touching `DrumVoice`
// (FR-194), and a slot holds one fade at a time: `VoicePool::beginFastRelease`
// either leaves an in-flight fade alone (FR-127) or, when forced (T2.6),
// replaces it with the newer voice and truncates the older tail -- ensuring
// the shadow footprint stays bounded at `kMaxVoices` entries. The ~8.4 MiB heap peak is accepted
// as the memory cost; see `plan.md §Complexity Tracking` for the rationale.
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
#include "strike_norm_table.h"
#include "voice_meta.h"
#include "voice_stealing_policy.h"

#include "../dsp/body_model_type.h"
#include "../dsp/drum_voice.h"
#include "../dsp/exciter_type.h"
#include "../dsp/pad_config.h"

#include <krate/dsp/systems/sympathetic_resonance.h>
#include <krate/dsp/systems/voice_allocator.h>

#include <array>
#include <cstdint>
#include <memory>

namespace Membrum {

// ------------------------------------------------------------------
// Compile-time constants
// ------------------------------------------------------------------

constexpr int   kMaxVoices         = 16;      // FR-110
// FR-117: minimum scratch-buffer reservation. prepare() reserves at least this
// many samples but GROWS to the host's actual maxSamplesPerBlock when larger,
// so a host that uses very large blocks (offline/bounce/freeze renders) cannot
// overflow the audio-thread scratch. This is a floor, NOT a hard cap.
constexpr int   kVoicePoolMaxBlock = 8192;
// FR-124 (Option B): kFastReleaseSecs is the TOTAL wall-clock decay time to
// the 1e-6 denormal floor, NOT the time constant τ. The per-sample decay
// coefficient is `k = exp(-kFastReleaseLnFloor / (kFastReleaseSecs * sr))`
// which reaches 1e-6 after exactly `kFastReleaseSecs` seconds. The resulting
// time constant is τ ≈ kFastReleaseSecs / ln(1e6) ≈ 362 μs, guaranteeing
// click-free voice steal within 5 ms wall-clock regardless of sample rate.
constexpr float kFastReleaseSecs   = 0.005f;  // FR-124 total wall-clock decay to 1e-6 floor
constexpr float kFastReleaseLnFloor = 13.8155106f;  // ln(1e6), used in k computation
constexpr float kFastReleaseFloor  = 1e-6f;   // FR-164 denormal guard

class VoicePool
{
public:
    VoicePool();
    ~VoicePool() = default;

    VoicePool(const VoicePool&)            = delete;
    VoicePool& operator=(const VoicePool&) = delete;
    VoicePool(VoicePool&&)                 = delete;
    VoicePool& operator=(VoicePool&&)      = delete;

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

    /// Percussion note-off (FR-114): allocator bookkeeping, plus the release
    /// of a sustaining exciter (Friction's bow). The voice continues decaying
    /// naturally until its own auto-release retires it, at which point
    /// `processBlock` frees the slot.
    void noteOff(std::uint8_t midiNote) noexcept;

    // ------------------------------------------------------------------
    // Audio processing -- audio thread
    // ------------------------------------------------------------------

    /// Render every active and fast-releasing voice into `outL` / `outR`.
    /// Legacy overload (Phase 3 compat) -- routes all audio to main only.
    /// Forwards to the multi-bus overload with one bus (one renderer).
    void processBlock(float* outL, float* outR, int numSamples) noexcept;

    /// FR-044: Extended processBlock with multi-bus output support.
    /// auxL/auxR: arrays of [numOutputBuses] buffer pointers.
    /// busActive: boolean array indicating which buses are active.
    /// After rendering each voice, audio is accumulated to main (always) and
    /// to auxL[pad.outputBus]/auxR[pad.outputBus] if the bus is active and > 0.
    void processBlock(float* outL, float* outR,
                      float** auxL, float** auxR,
                      const bool* busActive,
                      int numOutputBuses,
                      int numSamples) noexcept;

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
    // Per-pad configuration (Phase 4 -- replaces SharedParams)
    // ------------------------------------------------------------------

    /// Update a continuous float field in a specific pad's config.
    /// Called from processParameterChanges. `offset` is a PadParamOffset.
    void setPadConfigField(int padIndex, int offset, float normalizedValue) noexcept;

    /// Set a discrete selector for a pad (ExciterType/BodyModel/etc.).
    /// `offset` must be kPadExciterType or kPadBodyModel (or discrete TS params).
    void setPadConfigSelector(int padIndex, int offset, int discreteValue) noexcept;

    /// Read-only access to a pad's config (for state serialization).
    [[nodiscard]] const PadConfig& padConfig(int padIndex) const noexcept;

    /// Mutable access to a pad's config (for state deserialization / preset load).
    [[nodiscard]] PadConfig& padConfigMut(int padIndex) noexcept;

    /// Set the choke group for a specific pad (replaces setChokeGroup global).
    void setPadChokeGroup(int padIndex, std::uint8_t group) noexcept;

    /// Mutable accessor for the full padConfigs array (for DefaultKit::apply).
    [[nodiscard]] std::array<PadConfig, kNumPads>& padConfigsArray() noexcept
    {
        return padConfigs_;
    }

    // ------------------------------------------------------------------
    // Direct-forward helpers for Phase 2 param changes that live inside the
    // DrumVoice sub-components (ToneShaper / UnnaturalZone / MaterialMorph).
    // Phase 3.1 keeps parameter behaviour bit-identical to Phase 2 by
    // broadcasting parameter changes to every main slot -- tests at
    // maxPolyphony=1 (FR-187) require this so only slot 0 is touched, and
    // at higher polyphony every sounding voice sees the same edit. Since
    // DrumVoice state is already written on the audio thread from the
    // Processor's atomics, the broadcast is allocation-free.
    // ------------------------------------------------------------------
    template <typename Fn>
    // Called once per voice, so fn is not forwarded: forwarding could move from it after the first call.
    void forEachMainVoice(Fn&& fn) noexcept  // NOLINT(cppcoreguidelines-missing-std-forward)
    {
        for (int i = 0; i < kMaxVoices; ++i)
            fn(mainVoiceRef(i));
    }

    // ------------------------------------------------------------------
    // Query helpers
    // ------------------------------------------------------------------

    /// Number of slots currently rendering an Active voice (excludes slots
    /// that are only in FastReleasing state via the shadow array).
    [[nodiscard]] int getActiveVoiceCount() const noexcept;

    /// Read access to main slot metadata (used by tests to verify
    /// stealing-policy victim selection / tiebreakers).
    [[nodiscard]] const VoiceMeta& voiceMeta(int slot) const noexcept;

    /// Read access to shadow-slot metadata (tests that inspect fast-release
    /// state transitions).
    [[nodiscard]] const VoiceMeta& releasingMeta(int slot) const noexcept;

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

    /// Kit load (T2.3, MB-054): every sounding main voice moves to its shadow
    /// role and fades out over the 5 ms fast release (forced), instead of
    /// being cut. The voice left in each main role is wiped
    /// (`DrumVoice::resetForKitSwitch`) so no envelope / bank / filter state
    /// of the prior kit reaches the new kit's first hits; shadows already
    /// fading keep fading. Clears the main metadata, resets the allocator and
    /// re-applies the voice count. Audio thread, allocation-free.
    void fadeOutAllVoicesForKitSwitch() noexcept;

    // ------------------------------------------------------------------
    // Phase 5: Coupling engine integration
    // ------------------------------------------------------------------

    /// Set the coupling engine pointer (called from Processor::setupProcessing).
    /// When non-null, noteOn/noteOff hooks forward partial info to the engine.
    void setCouplingEngine(Krate::DSP::SympatheticResonance* engine) noexcept
    {
        couplingEngine_ = engine;
    }

    /// T2.3: registers the partials of every Active main voice with the
    /// coupling engine, as noteOn does. The Processor calls it after the
    /// engine was flushed at the end of a kit-switch coupling fade, so a note
    /// struck during the fade keeps its sympathetic resonators.
    void registerActiveVoicesWithCoupling() noexcept;

    // ------------------------------------------------------------------
    // Direct read access for the forEachMainVoice helper (unique_ptr
    // storage requires a non-inline helper method; kept public so the
    // header-defined template below can reach it).
    // ------------------------------------------------------------------
    [[nodiscard]] DrumVoice& mainVoiceRef(int slot) noexcept;

    /// Read access to the voice in a slot's shadow (fast-release) role.
    [[nodiscard]] const DrumVoice& releasingVoiceRef(int slot) const noexcept;

    /// T1.6: per-pad strike-normalisation gains, measured off the note-on
    /// path (prepare / worker thread). The Processor starts and stops its
    /// worker; tests use its counters and drainOnce().
    [[nodiscard]] StrikeNormTable& strikeNorm() noexcept { return strikeNorm_; }

private:
    // ------------------------------------------------------------------
    // Private helpers (Phase 3.1)
    // ------------------------------------------------------------------

    /// Processor-layer-only Quietest victim selection (FR-122 / Clarification
    /// Q3). Returns the index of the Active slot with the lowest
    /// `currentLevel`, tiebreaking by oldest `noteOnSampleCount` (FR-128).
    /// Slots triggered in the current block (not yet rendered, no measured
    /// level) are skipped. Returns -1 when no candidate slot exists.
    [[nodiscard]] int selectQuietestActiveSlot() const noexcept;

    /// Phase 3.2 hook — hand a slot's main voice to its shadow role (an
    /// index exchange, no DrumVoice moves: audit MB-005) and start the
    /// fast-release ramp on it.
    ///
    /// `force` (audit M-8, T2.6): the default (false) keeps the FR-127
    /// idempotent short-circuit — a re-entrant call on a slot already
    /// FastReleasing leaves the in-flight fade alone; only the
    /// choke-completeness re-scan uses it. When true, a release whose shadow
    /// is still busy from a PRIOR release re-snapshots the CURRENT
    /// (full-amplitude) main voice into the shadow, so it fades smoothly and
    /// only the older, already-attenuated tail is truncated. Every caller
    /// that releases a sounding main voice passes true: steal, choke,
    /// Quietest pre-steal, polyphony shrink and kit load all mark the slot
    /// Free afterwards, and an early return would leave the voice ringing
    /// unramped in that Free slot or hard-overwritten by the next NoteOn.
    ///
    /// The voice left in the main role is always inactive: an idle voice is
    /// left as it is, and the forced case's old shadow (still sounding) is
    /// wiped with `DrumVoice::resetForKitSwitch`. A finished fade wipes its
    /// shadow too (applyFastRelease), so a Free slot never holds a rendering
    /// voice (T2.6, MB-053).
    ///
    /// `linear` (T2.3, kit load): the shadow fades with a linear gain ramp
    /// from 1 to 0 over kFastReleaseSecs and its body is NOT fast-damped.
    /// The exponential ramp plus fastReleaseDamp drops the voice by ~16 % in
    /// its first sample, a step of -16 dB at a crest; a kit load cuts every
    /// sounding voice at once, at any phase, so its fade must stay under the
    /// -30 dB step bound relative to the voices' level.
    void beginFastRelease(int slot, bool force = false, bool linear = false) noexcept;

    /// Phase 3.2 hook — per-sample exponential decay applied to `scratch`
    /// with `releasingMeta_[slot].fastReleaseGain`. Returns the number of
    /// "live" samples written (i.e. samples where the gain was >= the
    /// denormal floor before multiplication). Samples past the termination
    /// point are zeroed in-place and MUST NOT be accumulated into the
    /// caller's output per FR-124's explicit clause. When the slot is
    /// still fast-releasing after processing, the return value is always
    /// `numSamples`; when the floor triggered partway through, the return
    /// value is the pre-floor sample count and the slot transitions to
    /// `Free`.
    [[nodiscard]] int applyFastRelease(int slot,
                                       float* scratch,
                                       int numSamples) noexcept;

    /// Phase 3.3 hook — iterates active + releasing slots on note-on and
    /// fast-releases any voice sharing the choke group of `newNote`. Phase
    /// 3.1 stub is a no-op; Phase 3.3 fills in the body.
    void processChokeGroups(std::uint8_t newNote) noexcept;

    /// Apply pad N's configuration to a voice slot at noteOn time.
    /// Called internally by noteOn() using the midiNote-to-pad mapping.
    void applyPadConfigToSlot(int slot, int padIndex) noexcept;

    /// Phase 5: registers the main voice's partials in `slot` with the
    /// coupling engine when pad `padIndex` couples (couplingAmount > 0).
    void registerCouplingPartials(int slot, int padIndex) noexcept;

    /// M-9: equal-power per-pad pan gains for a voice, looked up from the
    /// voice's originating note. `gainL`/`gainR` default to unity (center) for
    /// notes outside the pad range. The law is referenced to unity-at-center so
    /// a default (pan 0.5) pad reproduces the legacy mono-duplication mix
    /// exactly (gainL == gainR == 1.0); hard-panned pads reach +3 dB on one
    /// side (constant power: gainL^2 + gainR^2 == 2).
    void panGainsForNote(std::uint8_t originatingNote,
                         float& gainL, float& gainR) const noexcept;


    /// The voice in a slot's main (sounding) role.
    [[nodiscard]] DrumVoice& mainVoice(int slot) noexcept;

    /// The voice in a slot's shadow (fast-release) role.
    [[nodiscard]] DrumVoice& shadowVoice(int slot) noexcept;

    // ------------------------------------------------------------------
    // Voice storage (FR-110): 2 * kMaxVoices DrumVoices, 16 main + 16
    // shadow. Heap-allocated via unique_ptr in the constructor so the
    // 32 * ~270 KiB DrumVoice footprint does not blow up the stack when
    // VoicePool is an owning member of Processor. All voices are fully
    // constructed by the time `prepare()` runs; no audio-thread allocation
    // occurs after construction (FR-116).
    //
    // A slot's two roles are reached through `mainIndex_` / `shadowIndex_`.
    // `beginFastRelease` exchanges the two indices of a slot (audit MB-005)
    // instead of moving two whole DrumVoice objects, so a voice never
    // changes address. Each slot's pair stays {slot, slot + kMaxVoices}.
    // The active count is `maxPolyphony_`, controlled by
    // `allocator_.setVoiceCount`.
    // ------------------------------------------------------------------
    static constexpr std::size_t kVoiceStorageCount =
        2 * static_cast<std::size_t>(kMaxVoices);
    std::unique_ptr<std::array<DrumVoice, kVoiceStorageCount>> voiceStoragePtr_;
    std::array<std::uint8_t, kMaxVoices>                   mainIndex_{};
    std::array<std::uint8_t, kMaxVoices>                   shadowIndex_{};
    std::array<VoiceMeta, kMaxVoices>                      meta_{};

    // Shadow metadata for the fast-release crossfade. One fade-out in
    // flight per slot at a time.
    std::array<VoiceMeta, kMaxVoices>                      releasingMeta_{};

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
    float  linearFadeStep_ = 0.0f;  // T2.3: 1 / (kFastReleaseSecs * sampleRate)

    int                  maxPolyphony_  = 8;                           // FR-111
    VoiceStealingPolicy  stealingPolicy_= VoiceStealingPolicy::Oldest; // FR-120
    std::uint64_t        sampleCounter_ = 0;                           // FR-128

    // ------------------------------------------------------------------
    // Per-pad configuration storage (Phase 4). 32 pre-allocated PadConfig
    // structs, one per GM drum map pad. Replaces the Phase 3 SharedParams.
    // ------------------------------------------------------------------
    std::array<PadConfig, kNumPads> padConfigs_{};

    // T1.6: per-pad strike-norm gains (probe voices live on the heap).
    StrikeNormTable strikeNorm_;

    // ------------------------------------------------------------------
    // Phase 5: Coupling engine (owned by Processor, non-owning pointer).
    // ------------------------------------------------------------------
    Krate::DSP::SympatheticResonance* couplingEngine_ = nullptr;
};

// ------------------------------------------------------------------
// SC-027a -- compile-time safety net on VoicePool struct size. The
// definitive evidence for SC-027a is the allocation-detector fuzz test
// `test_polyphony_allocation_matrix.cpp`; this static_assert exists to
// catch any accidental addition of a heap-owning member (std::vector,
// std::string, etc.) that would enlarge the sizeof at compile time.
//
// Budget: 32 * (sizeof(DrumVoice) + sizeof(VoiceMeta))
//       + sizeof(Krate::DSP::VoiceAllocator)
//       + sizeof(ChokeGroupTable)
//       + 1 KiB slack for bookkeeping scalars and unique_ptr header fields.
// Note that the `unique_ptr<std::array<DrumVoice, 32>>` heap block is
// intentionally included in the 32-slot budget (16 main + 16 shadow = 32).
// ------------------------------------------------------------------
constexpr std::size_t kVoicePoolSizeLimit =
    32 * sizeof(DrumVoice) + 32 * sizeof(VoiceMeta)
    + sizeof(Krate::DSP::VoiceAllocator) + sizeof(ChokeGroupTable)
    + kNumPads * sizeof(PadConfig) + 2048;

static_assert(sizeof(VoicePool) <= kVoicePoolSizeLimit,
              "VoicePool struct size exceeds budget");

} // namespace Membrum
