# Contract: `Membrum::VoicePool` Public API

**File**: `plugins/membrum/src/voice_pool/voice_pool.h`
**Spec**: `specs/138-membrum-phase3-polyphony/spec.md`
**Plan**: `specs/138-membrum-phase3-polyphony/plan.md`
**Status**: Frozen target for `/speckit.implement`

This document is the in-plugin contract between `Membrum::Processor` and `Membrum::VoicePool`. It is the **only** in-plugin API surface introduced by Phase 3. There is no external VST3 ABI change beyond the 3 new `IEditController` parameters and the v3 state blob tail, both documented in `plan.md`.

---

## Lifecycle

### `void VoicePool::prepare(double sampleRate, int maxBlockSize) noexcept`

Called **once** from `Membrum::Processor::setupProcessing` and whenever the host changes sample rate or block size. This is the **only** method that may allocate memory — it allocates the stereo scratch buffer (`float[2][maxBlockSize]`) via `std::make_unique<float[]>`. All subsequent calls on the audio thread MUST be allocation-free.

**Pre-conditions**:
- `sampleRate ∈ [22050, 192000]` (implementation clamps if outside).
- `maxBlockSize ∈ [1, kVoicePoolMaxBlock]` where `kVoicePoolMaxBlock = 2048`.

**Post-conditions**:
- All 16 `DrumVoice` instances in `voices_` and `releasingVoices_` are `prepare()`-ed with `sampleRate` and a unique `voiceId`.
- `fastReleaseK_ = std::exp(-1.0f / (kFastReleaseSecs * static_cast<float>(sampleRate)))`.
- `allocator_.reset()` is called and `allocator_.setVoiceCount(maxPolyphony_)` seeds the active voice count.
- `chokeGroups_` state is preserved (persisted via setState).
- All `meta_` and `releasingMeta_` entries are reset to `{state = Free}`.

**Thread**: setup thread (UI or audio — must be coordinated by the VST3 host as per `IAudioProcessor::setupProcessing` contract).

---

## Note events (audio thread)

### `void VoicePool::noteOn(std::uint8_t midiNote, float velocity) noexcept`

**Pre-conditions**:
- `midiNote ∈ [36, 67]` — notes outside this range are silently dropped by the Processor before `noteOn` is called.
- `velocity ∈ (0, 1]` (velocity == 0 is routed to `noteOff` by the Processor).

**Behavior** (see plan.md "Note-on flow" for the prose diagram):
1. Snapshots the shared Phase 1/2 parameter atomics from the Processor into the pool's `sharedParams_`.
2. Consults `chokeGroups_.lookup(midiNote)`. If the group is non-zero, iterates `meta_` and `releasingMeta_` for matching `originatingChoke` values and calls `beginFastRelease()` on each matching slot (FR-132, FR-133).
3. If `stealingPolicy_ == Quietest` and the pool is full, pre-selects the quietest active slot via `selectQuietestActiveSlot()` (reads `VoiceMeta::currentLevel`), calls `beginFastRelease(q)`, then `allocator_.noteOff(meta_[q].originatingNote)` and `allocator_.voiceFinished(q)` to yield the slot back to the allocator as Idle (Clarification Q3).
4. Otherwise, sets `allocator_.setAllocationMode(Oldest)` or `HighestNote` matching the Priority mapping (FR-121 / FR-123).
5. Calls `allocator_.noteOn(midiNote, velocityByte)` where `velocityByte = static_cast<uint8_t>(velocity * 127.f + 0.5f)`.
6. For every returned `VoiceEvent`:
   - `Steal`: `beginFastRelease(ev.voiceIndex)` on the main slot.
   - `NoteOn`: configures `voices_[ev.voiceIndex]` via the shared params, calls `voices_[ev.voiceIndex].noteOn(velocity)`, updates `meta_[ev.voiceIndex]` fields, sets state to `Active`.
   - `NoteOff`: ignored (only occurs in unison mode, which Phase 3 does not use).
7. Increments `sampleCounter_` by 0 (the counter advances per `processBlock`, not per `noteOn`).

**Post-conditions**:
- Exactly one `meta_[slot].state == Active` for the new note.
- Any displaced voices are in `releasingMeta_[slot].state == FastReleasing`.
- `allocator_.getActiveVoiceCount() <= maxPolyphony_`.

**Invariants**:
- No heap allocation. No locks. No exceptions. `noexcept`.
- FR-194: `DrumVoice` not modified by Phase 3; `VoicePool::noteOn` only calls existing `DrumVoice` setters and `noteOn`.

---

### `void VoicePool::noteOff(std::uint8_t midiNote) noexcept`

**Behavior** (FR-114): percussion is no-op for note-off at the voice level. The pool calls `allocator_.noteOff(midiNote)` purely for allocator bookkeeping, but **does not** gate the amp envelope off on the matching `DrumVoice`. The voice continues decaying naturally until its amp envelope reaches the silence threshold, at which point `VoicePool::processBlock` observes `!voices_[i].isActive()` and calls `allocator_.voiceFinished(i)` to free the slot (FR-115).

**Invariants**: no allocation, no locks, no exceptions, `noexcept`.

---

## Audio processing

### `void VoicePool::processBlock(float* outL, float* outR, int numSamples) noexcept`

**Pre-conditions**:
- `outL` and `outR` are non-null and point to `numSamples`-length buffers.
- `numSamples <= maxBlockSize_` (the value passed to `prepare()`).

**Behavior** (see plan.md "Block-processing flow"):
1. Zero `outL[0..numSamples)` and `outR[0..numSamples)`.
2. For each `slot ∈ [0, maxPolyphony_)`:
   - If `voices_[slot].isActive()`:
     - `voices_[slot].processBlock(scratchL_, numSamples)`
     - Compute block peak → `meta_[slot].currentLevel` (FR-165).
     - Accumulate `scratchL_[i]` into `outL[i]` and `outR[i]`.
   - Else if `meta_[slot].state == Active`:
     - `meta_[slot].state = Free`; call `allocator_.voiceFinished(slot)` (FR-115).
3. For each `slot ∈ [0, kMaxVoices)` with `releasingMeta_[slot].state == FastReleasing`:
   - `releasingVoices_[slot].processBlock(scratchL_, numSamples)`
   - Apply per-sample exponential decay to `scratchL_` with gain seeded from `releasingMeta_[slot].fastReleaseGain`, multiplied by `fastReleaseK_` each sample, with the mandatory `1e-6f` floor (FR-124, FR-164).
   - When the floor triggers, zero the remainder of `scratchL_` and set `releasingMeta_[slot].state = Free`.
   - Accumulate the faded `scratchL_` into both output channels.
4. `sampleCounter_ += numSamples`.

**Invariants**:
- No allocation, no locks, no exceptions, `noexcept`.
- `allocator_.getActiveVoiceCount()` may decrease (when voices go idle) but never increases from this method.
- FR-165: `currentLevel` is written exactly once per block per slot (not per sample).
- Clarification Q5: the fast-release gain ramp is applied here, not inside `DrumVoice`.

---

## Configuration (audio thread via `processParameterChanges`)

### `void VoicePool::setMaxPolyphony(int n) noexcept`

Clamps `n` to `[4, 16]`. Calls `allocator_.setVoiceCount(n)`. For every `NoteOff` event returned by `setVoiceCount` (voices released because the pool is shrinking), calls `beginFastRelease(slot)` so the shrink is click-free. FR-111 / spec §User Story 6 Acceptance #3.

### `void VoicePool::setVoiceStealingPolicy(VoiceStealingPolicy p) noexcept`

Stores the policy in `stealingPolicy_`. Takes effect on the next `noteOn`. Does not disrupt currently-sounding voices (FR-152).

### `void VoicePool::setChokeGroup(std::uint8_t group) noexcept`

Clamps `group` to `[0, 8]`. Calls `chokeGroups_.setGlobal(group)` which writes the same value into all 32 entries (Phase 3 single-pad-template — FR-138 / Clarification Q1). Takes effect on the next `noteOn` (FR-152).

### `void VoicePool::setSharedVoiceParams(float material, float size, float decay, float strikePos, float level) noexcept`

Stores the Phase 1 parameter snapshot in `sharedParams_`. The snapshot is read on the next `noteOn` and applied to the allocated `DrumVoice` via its setters. Does not disturb currently-sounding voices.

### `void VoicePool::setSharedExciterParams(int exciterType, int bodyModel, ...Phase 2 floats...) noexcept`

Stores the Phase 2 parameter snapshot in `sharedParams_`. Same semantics as `setSharedVoiceParams` — next-note-boundary application.

---

## State (non-audio thread)

### `std::array<std::uint8_t, 32> VoicePool::getChokeGroupAssignments() const noexcept`

Returns the current 32-entry `chokeGroupAssignments` table. Called from `Processor::getState` to serialize the v3 tail.

### `void VoicePool::loadChokeGroupAssignments(const std::array<std::uint8_t, 32>& in) noexcept`

Loads the 32-entry table, clamping each byte to `[0, 8]` per FR-144. Called from `Processor::setState` during v3 load.

---

## Invariants (static)

1. **No dsp/ modification**: `voice_allocator.h` is unchanged. `VoicePool` consumes it read-only via the public API.
2. **No DrumVoice modification**: `DrumVoice`'s public API is unchanged. `VoicePool` only calls existing `prepare`, `noteOn`, `processBlock`, `isActive`, and `set*` setters.
3. **No dynamic containers**: `voices_` / `meta_` / `releasingVoices_` / `releasingMeta_` / `chokeGroups_` are all `std::array` with compile-time sizes.
4. **Single scratch buffer**: `scratchL_` (and the reserved `scratchR_`) is the only dynamically-sized allocation; it lives for the lifetime of the `VoicePool` and is sized in `prepare()`.
5. **Fast-release locality**: the exponential decay loop exists in exactly one place (`VoicePool::processBlock`'s per-voice mixing loop). Clarification Q5 explicitly forbids moving any part of it into `DrumVoice`.
6. **Thread contract**: every `noexcept` method listed above is audio-thread-safe. `prepare` is setup-thread-only. `getChokeGroupAssignments` / `loadChokeGroupAssignments` are UI/setup thread only.

---

## Test contract

Every method above has at least one dedicated test in `plugins/membrum/tests/unit/voice_pool/`. The mapping is:

| Method | Test file(s) |
|--------|--------------|
| `prepare` | `test_voice_pool_scaffold.cpp`, `test_polyphony_allocation_matrix.cpp` |
| `noteOn` (all 3 policies) | `test_voice_pool_allocate.cpp`, `test_voice_pool_stealing_policies.cpp` |
| `noteOff` | `test_voice_pool_allocate.cpp` (verifies drum amp envelope not gated off) |
| `processBlock` | `test_voice_pool_allocate.cpp`, `test_steal_click_free.cpp`, `test_choke_click_free.cpp` |
| `setMaxPolyphony` | `test_poly_change_live.cpp` (shrinks from 8 to 4) |
| `setVoiceStealingPolicy` | `test_voice_pool_stealing_policies.cpp` |
| `setChokeGroup` | `test_choke_group.cpp` |
| `getChokeGroupAssignments` / `loadChokeGroupAssignments` | `test_state_roundtrip_v3.cpp`, `test_state_migration_v2_to_v3.cpp` |
| (all methods) | `test_voice_pool_allocation_free.cpp`, `test_polyphony_allocation_matrix.cpp` (allocation_detector coverage) |
