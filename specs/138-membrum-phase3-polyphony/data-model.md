# Data Model — Membrum Phase 3

**Spec**: `specs/138-membrum-phase3-polyphony/spec.md`
**Plan**: `specs/138-membrum-phase3-polyphony/plan.md`
**Date**: 2026-04-11

This document defines every new type introduced in Phase 3 and the exact binary layout of the v3 state blob. Every field listed here maps 1:1 to a Functional Requirement (FR) in `spec.md`.

---

## §1 — New enums

### `Membrum::VoiceSlotState` (`plugins/membrum/src/voice_pool/voice_meta.h`)

Tracks the internal lifecycle of a single slot in `VoicePool`. Deliberately *not* named `VoiceState` to avoid reader confusion with `Krate::DSP::VoiceState` from the allocator, even though they are in distinct namespaces.

```cpp
enum class VoiceSlotState : std::uint8_t {
    Free          = 0,  // slot is idle; allocator's view must also be Idle
    Active        = 1,  // voice is rendering normally; amp envelope is active
    FastReleasing = 2,  // voice is in its 5 ms fast-release tail (steal or choke)
};
```

### `Membrum::VoiceStealingPolicy` (`plugins/membrum/src/voice_pool/voice_stealing_policy.h`)

Exposed to the user as the `kVoiceStealingId` StringListParameter. Persisted as a single `uint8` byte in the v3 state blob.

```cpp
enum class VoiceStealingPolicy : std::uint8_t {
    Oldest   = 0,  // FR-121 — default; maps to Krate::DSP::AllocationMode::Oldest
    Quietest = 1,  // FR-122 — processor-layer-only per Clarification Q3
    Priority = 2,  // FR-123 — maps to Krate::DSP::AllocationMode::HighestNote
};
```

---

## §2 — New struct: `Membrum::VoiceMeta`

**File**: `plugins/membrum/src/voice_pool/voice_meta.h`

Per-slot metadata. One instance per slot in `VoicePool::meta_` (main) and `VoicePool::releasingMeta_` (shadow for the two-array crossfade technique — see plan.md Architecture).

```cpp
struct alignas(64) VoiceMeta {
    float            currentLevel      = 0.0f;   // per-block peak of the voice's scratch (FR-165)
    float            fastReleaseGain   = 1.0f;   // multiplicative gain, runs 1→0 during FastReleasing (FR-124)
    std::uint64_t    noteOnSampleCount = 0;      // monotonic sample timestamp at note-on (Oldest tiebreaker)
    std::uint8_t     originatingNote   = 0;      // MIDI note 36..67 — Priority + Choke lookup key (FR-172)
    std::uint8_t     originatingChoke  = 0;      // cached chokeGroupAssignments[note-36] at note-on (FR-132)
    VoiceSlotState   state             = VoiceSlotState::Free;
    std::uint8_t     _pad              = 0;      // reserved for future per-slot flags (Phase 4+)
};
static_assert(sizeof(VoiceMeta) <= 64, "VoiceMeta must fit a single cache line");
```

| Field | Purpose | FR | Who writes it |
|-------|---------|----|---------------|
| `currentLevel` | Per-block peak of the voice's scratch buffer, snapshot taken after `DrumVoice::processBlock` | FR-165 | `VoicePool::processBlock` (audio thread, once per block per active voice) |
| `fastReleaseGain` | Multiplicative gain applied by `VoicePool::applyFastRelease`; decays per-sample with `k = exp(-1/(τ·sampleRate))` | FR-124 | `VoicePool::beginFastRelease` (init) and `VoicePool::processBlock` (per-sample update) |
| `noteOnSampleCount` | Tiebreaker for Oldest and Quietest policies; incremented by the VoicePool's `sampleCounter_` | FR-128 | `VoicePool::noteOn` at allocation time |
| `originatingNote` | Source MIDI note used for choke-group and Priority lookups | FR-172 | `VoicePool::noteOn` |
| `originatingChoke` | Cached choke group (0–8) from `chokeGroupAssignments[note-36]` at note-on | FR-132 | `VoicePool::noteOn` |
| `state` | Slot lifecycle — drives whether the slot is rendered, mixed with fast-release, or skipped | FR-172 | All `VoicePool` mutating methods |
| `_pad` | Reserved — Phase 4 may use for a per-pad dirty flag | — | — |

**Lifetime and thread contract**:
- All fields are read and written exclusively on the audio thread.
- No `std::atomic` wrapping is needed (FR-165) — the VoicePool is the sole owner.
- `alignas(64)` avoids false sharing if future work parallelizes across voices.

---

## §3 — New class: `Membrum::ChokeGroupTable`

**File**: `plugins/membrum/src/voice_pool/choke_group_table.h`

A fixed 32-entry `uint8_t` table indexed by `(midiNote − 36)`. Phase 3 mirrors the same value across all 32 entries on every `kChokeGroupId` parameter change; Phase 4 becomes per-pad without a state format change (FR-138 / Clarification Q1).

```cpp
class ChokeGroupTable {
public:
    static constexpr int kSize = 32;   // GM drum pads 36..67

    void setGlobal(std::uint8_t group) noexcept;      // FR-138 — writes `group` into all 32 entries
    [[nodiscard]] std::uint8_t lookup(std::uint8_t midiNote) const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, kSize>& raw() const noexcept { return entries_; }
    void loadFromRaw(const std::array<std::uint8_t, kSize>& in) noexcept;  // clamps per FR-144

private:
    std::array<std::uint8_t, kSize> entries_{};   // FR-131
};
```

| Method | Purpose | FR |
|--------|---------|----|
| `setGlobal(g)` | Writes `g` into all 32 entries in one pass. Called from the parameter-change path. | FR-138 |
| `lookup(note)` | Returns the choke group for a MIDI note; returns 0 for notes outside [36, 67]. | FR-132 |
| `raw()` | Used by `getState` to serialize all 32 bytes (Q1). | FR-141 |
| `loadFromRaw(in)` | Used by `setState`; clamps each entry to [0, 8] per FR-144. | FR-144 |

---

## §4 — New class: `Membrum::VoicePool`

**File**: `plugins/membrum/src/voice_pool/voice_pool.h`

Owns everything Phase 3 introduces. See `contracts/voicepool_api.md` for the exact public interface.

Key invariants:
- `voices_.size() == meta_.size() == releasingVoices_.size() == releasingMeta_.size() == 16`.
- `allocator_.setVoiceCount(maxPolyphony_)` is the single source of truth for the active voice count.
- `scratchL_` is sized once in `prepare()` to `min(hostMaxBlock, kVoicePoolMaxBlock)` and never reallocated.
- No `std::vector`, no dynamic containers, no mutexes on the hot path.
- `fastReleaseK_` is recomputed only in `prepare()` when `sampleRate_` changes.

Memory layout (approximate, not ABI-stable):

| Field | Size (16-voice) | Purpose |
|-------|-----------------|---------|
| `voices_` (`std::array<DrumVoice,16>`) | 16 × `sizeof(DrumVoice)` | Main active voices |
| `meta_` (`std::array<VoiceMeta,16>`) | 16 × 64 = 1024 B | Per-slot metadata |
| `releasingVoices_` (`std::array<DrumVoice,16>`) | 16 × `sizeof(DrumVoice)` | Shadow slots for the fast-release crossfade |
| `releasingMeta_` (`std::array<VoiceMeta,16>`) | 1024 B | Shadow metadata |
| `allocator_` | `sizeof(Krate::DSP::VoiceAllocator)` < 4096 B (per its SC-009) | Allocator state |
| `chokeGroups_` | 32 B | Choke table |
| `scratchL_` / `scratchR_` | ≤ 2 × `kVoicePoolMaxBlock` × 4 B | Audio scratch |
| `sharedParams_` | ~160 B | Phase 2 parameter snapshot |
| Scalar members | ~64 B | sampleRate, K, policy, counters |

`sizeof(DrumVoice)` will be measured at Phase 3.0 implementation time; a `static_assert` bounds the total to a value recorded in the compliance table. If the two-array approach is infeasible the fallback (per-slot single-fade reservation) reduces the shadow array to zero.

---

## §5 — New atomics on `Membrum::Processor`

**File**: `plugins/membrum/src/processor/processor.h`

Added alongside the existing Phase 1 / Phase 2 atomics:

```cpp
std::atomic<int> maxPolyphony_{8};           // FR-111 — valid [4, 16]
std::atomic<int> voiceStealingPolicy_{0};    // FR-120 — VoiceStealingPolicy enum int cast
std::atomic<int> chokeGroup_{0};             // FR-138 — [0, 8]; 0 = none
```

No removals. `voice_` is replaced by `voicePool_` (see `processor.h` diff in plan.md).

---

## §6 — New parameter IDs

**File**: `plugins/membrum/src/plugin_ids.h`

```cpp
kMaxPolyphonyId   = 250,   // FR-150 — RangeParameter stepped [4,16]
kVoiceStealingId  = 251,   // FR-150 — StringListParameter {Oldest,Quietest,Priority}
kChokeGroupId     = 252,   // FR-150 — RangeParameter stepped [0,8]
```

Placed in the **Phase 3 range 250–259**, leaving 253–259 open for any future Phase 3 follow-ups (e.g. a debug-only "fast release ms" tweak if listening tests reveal the 5 ms value needs exposing).

Updated `static_assert`s:
```cpp
static_assert(kMorphCurveId < kMaxPolyphonyId,
              "Phase 2 and Phase 3 parameter ID ranges must not overlap");
static_assert(kCurrentStateVersion == 3, "Phase 3 requires state version 3");
```

And the bumped version constant:
```cpp
constexpr Steinberg::int32 kCurrentStateVersion = 3;  // Phase 1 = 1, Phase 2 = 2, Phase 3 = 3.
```

---

## §7 — State v3 binary layout (EXACT)

The v3 blob is **strictly additive** over v2 per Clarification Q1.

**Total size**: 302 bytes (v2 was 268).

```
+--------+--------+----------+-----------------------------------------------------+
| Offset | Bytes  | Type     | Field                                                |
+--------+--------+----------+-----------------------------------------------------+
|   0    |   4    | int32    | version           (= 3)                              |
|   4    |  40    | f64 × 5  | Phase 1: material, size, decay, strikePos, level     |
|  44    |   4    | int32    | exciterType       (0..5)                             |
|  48    |   4    | int32    | bodyModel         (0..5)                             |
|  52    | 216    | f64 × 27 | Phase 2: 27 continuous params in kPhase2FloatSlots order |
|        |        |          | (exciterFM, exciterFeedback, exciterNoiseBurstDur,   |
|        |        |          |  exciterFrictionPressure, toneShaperFilterType,      |
|        |        |          |  toneShaperFilterCutoff, toneShaperFilterResonance,  |
|        |        |          |  toneShaperFilterEnvAmount, toneShaperDriveAmount,   |
|        |        |          |  toneShaperFoldAmount, toneShaperPitchEnvStart,      |
|        |        |          |  toneShaperPitchEnvEnd, toneShaperPitchEnvTime,      |
|        |        |          |  toneShaperPitchEnvCurve, toneShaperFilterEnvAttack, |
|        |        |          |  toneShaperFilterEnvDecay, toneShaperFilterEnvSustain,|
|        |        |          |  toneShaperFilterEnvRelease, unnaturalModeStretch,   |
|        |        |          |  unnaturalDecaySkew, unnaturalModeInjectAmount,      |
|        |        |          |  unnaturalNonlinearCoupling, morphEnabled, morphStart,|
|        |        |          |  morphEnd, morphDurationMs, morphCurve)              |
| ====== | ====== | ======== | ====== PHASE 3 TAIL (NEW) ========================== |
| 268    |   1    | uint8    | maxPolyphony           (valid 4..16; default 8)      |
| 269    |   1    | uint8    | voiceStealingPolicy    (0=Oldest,1=Quietest,2=Priority) |
| 270    |  32    | uint8×32 | chokeGroupAssignments  (each 0..8; default all 0)    |
| 302    |        |          | END                                                  |
+--------+--------+----------+-----------------------------------------------------+
```

**Endian / ABI note**: both v2 and v3 use host byte order via `IBStream::write(&value, sizeof(value), nullptr)` (matching Phase 1 / Phase 2 precedent). VST3 hosts are all little-endian on the three target platforms (x86_64 Win/macOS/Linux + ARM64 macOS); byte-swap handling is not needed.

**Default values written when loading v1 or v2 blobs (migration)**:

| Field | v1 / v2 load default |
|-------|----------------------|
| `maxPolyphony` | 8 |
| `voiceStealingPolicy` | 0 (Oldest) |
| `chokeGroupAssignments[0..31]` | 0 (no choke) |

**Clamp-on-load (FR-144)**: `setState` never rejects — corrupt values are clamped to valid ranges:
- `maxPolyphony = std::clamp(in, 4, 16)` (if in < 4 or > 16, clamp; do NOT treat as error)
- `voiceStealingPolicy = in > 2 ? 0 : in`
- Each `chokeGroupAssignments[i] = in[i] > 8 ? 0 : in[i]`

**`getState` save path**: writes `version = 3`, then the v2 body, then the 3 new tail fields **unconditionally** (no version branch, no length prefix — Clarification Q1). Phase 4 reuses the same 32-byte `chokeGroupAssignments` block with distinct per-pad values and MUST NOT bump the version.

---

## §8 — Summary table: FR → file/type mapping

| FR | Type / field | File |
|----|--------------|------|
| FR-110 | `std::array<DrumVoice, 16> VoicePool::voices_` | `plugins/membrum/src/voice_pool/voice_pool.h` |
| FR-111 | `int VoicePool::maxPolyphony_` + `setMaxPolyphony()` | same |
| FR-112 | `Krate::DSP::VoiceAllocator VoicePool::allocator_` + `allocator_.setVoiceCount(n)` | same |
| FR-113 | `Processor::processEvents` removes `pitch != 36` filter | `plugins/membrum/src/processor/processor.cpp` |
| FR-114 | `VoicePool::noteOff` bookkeeping only | `voice_pool.h/.cpp` |
| FR-115 | `VoicePool::processBlock` detects `!voices_[i].isActive()` and calls `allocator_.voiceFinished(i)` | same |
| FR-116 | `prepare()` is the only allocation point; all members `std::array` | same |
| FR-117 | `scratchL_/scratchR_` allocated via `std::make_unique<float[]>(maxBlockSize)` in `prepare()` | same |
| FR-120 | `VoiceStealingPolicy` enum + `setVoiceStealingPolicy()` | `voice_stealing_policy.h` + `voice_pool.h` |
| FR-121 | Oldest maps to `AllocationMode::Oldest` | `VoicePool::setVoiceStealingPolicy` |
| FR-122 | `VoicePool::selectQuietestActiveSlot()` processor-layer implementation | `voice_pool.cpp` |
| FR-123 | Priority maps to `AllocationMode::HighestNote` | same |
| FR-124 | `fastReleaseK_`, `fastReleaseGain`, `kFastReleaseFloor=1e-6f`, `applyFastRelease()` | same |
| FR-125 | `fastReleaseK_` recomputed in `prepare()` from `sampleRate_` | same |
| FR-126 | (tested) `tests/unit/voice_pool/test_steal_click_free.cpp` | test |
| FR-127 | `beginFastRelease` early-return if already `FastReleasing` | `voice_pool.cpp` |
| FR-128 | Tiebreaker = slot index ascending | selection helpers |
| FR-130 | `ChokeGroupTable` | `choke_group_table.h` |
| FR-131 | `std::array<uint8_t, 32> entries_` | same |
| FR-132 | `processChokeGroups(note)` on note-on | `voice_pool.cpp` |
| FR-133 | Iterates all active + releasing slots | same |
| FR-134 | Same fast-release path as steal | same |
| FR-135 | `if (meta.originatingChoke != newGroup) continue;` | same |
| FR-136 | `if (group == 0) return;` early-out | same |
| FR-138 | `setGlobal(n)` mirrors value across all 32 entries | `choke_group_table.h` |
| FR-140 | `kCurrentStateVersion = 3` | `plugin_ids.h` |
| FR-141 | state v3 layout table above | `processor.cpp` getState/setState |
| FR-142 | v2 → v3 migration defaults | same |
| FR-143 | v1 → v3 chained migration | same |
| FR-144 | Clamp-on-load | same |
| FR-150 | 3 new parameters registered | `controller.cpp` |
| FR-151 | ID naming convention + non-collision `static_assert`s | `plugin_ids.h` |
| FR-152 | `Processor::processParameterChanges` handles the 3 new IDs | `processor.cpp` |
| FR-153 | No uidesc; host-generic editor only | (no file) |
| FR-160 / FR-161 / FR-162 | (tested) CPU benchmark in Phase 3.5 | `tests/perf/test_polyphony_benchmark.cpp` |
| FR-163 | allocation_detector tests across all code paths | Phase 3.5 |
| FR-164 | `1e-6f` software floor + FTZ/DAZ carryover | `voice_pool.cpp` |
| FR-165 | `VoiceMeta::currentLevel` plain float, updated once per block | `voice_pool.cpp` |
| FR-170 | All notes hit the same pad template (sharedParams_) | `voice_pool.cpp noteOn` |
| FR-171 | Note-on sequence: choke → steal → allocator → DrumVoice::noteOn | same |
| FR-172 | `VoiceMeta` fields map 1:1 | `voice_meta.h` |
| FR-180..FR-187 | Test files listed in plan.md Phase 3.x deliverables | (tests) |
| FR-188 / FR-189 | Phase 3.6 quality gates | (CI) |
| FR-190 | ODR check in plan.md Codebase Research | `plan.md` |
| FR-191 / FR-193 | `voice_allocator.h` NOT modified | (invariant) |
| FR-194 | `DrumVoice` NOT modified | (invariant) |
