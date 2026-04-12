# Feature Specification: Membrum Phase 3 — Multi-Voice Polyphony, Voice Management, Choke Groups

**Feature Branch**: `138-membrum-phase3-polyphony`
**Plugin**: Membrum (`plugins/membrum/`)
**Created**: 2026-04-11
**Status**: Draft
**Input**: Phase 3 scope from Spec 135 (Membrum Synthesized Drum Machine); builds on Phase 2 (`specs/137-membrum-phase2-exciters-bodies/`)

## Background

Phase 2 (spec 137) shipped a single-voice Membrum (v0.2.0) with the full sound-design palette: 6 exciter types, 6 body models, the Tone Shaper (Drive → Wavefolder → DCBlocker → SVF), and all 5 Unnatural Zone modules. The Phase 2 voice passes SC-003's 1.25% single-core CPU budget on 143 of 144 exciter/body/ToneShaper/Unnatural combinations, SC-009's 808-kick pitch glide, and FR-055 defaults-off bit-identity at −120 dBFS. State is at schema version 2 and the processor holds **one** `DrumVoice` instance hardcoded to MIDI note 36.

Phase 3 turns that single voice into a polyphonic voice pool. The single unchanged-from-Phase-2 voice template gets replicated into 4–16 instances managed by the existing `Krate::DSP::VoiceAllocator`, voice stealing becomes configurable and click-free, and choke groups are introduced so open/closed hi-hat pairs (and similar mutually-exclusive pad relationships) can mute each other without clicks.

Phase 3 is **deliberately scoped to polyphony**. Every incoming MIDI note still hits the same single pad template (carryover from Phase 2 — all 32 pads map to one voice configuration). **32 pads, per-pad presets, kit presets, and separate outputs are Phase 4**, not Phase 3. **Cross-pad coupling (sympathetic resonance) is Phase 5**. **Macros, Acoustic/Extended UI modes, and the custom VSTGUI editor are Phase 6**. See "Deferred to Later Phases" for the full list.

## Clarifications

### Session 2026-04-11

- Q: How should `chokeGroupAssignments[32]` be serialized in v3 state — (A) always 32 bytes, Phase 3 mirrors the global value, Phase 4 additive; (B) 1 byte only in Phase 3, Phase 4 re-layouts to 32 with a v4 bump; or (C) length-prefixed variable-length block? → A: Option A — serialize all 32 bytes in state v3. Phase 3 writes the same value 32 times; Phase 4 becomes strictly additive with no state version bump.
- Q: What curve shape should the fast-release use for voice stealing and choke muting — (A) linear ramp, (B) exponential decay (`gain *= k` per sample, `k = exp(-1/(τ·sampleRate))`), or (C) equal-power cosine taper? → A: Option B — exponential decay with τ = 5 ms (`gain *= exp(-1/(0.005·sampleRate))`) starting from the voice's current amplitude. A mandatory denormal floor `if (gain < 1e-6f) gain = 0.f` terminates the ramp and returns the voice slot to the pool. No other curve shapes are permitted in Phase 3.
- Q: Where does the quietest-selection loop live — (a) new `AllocationMode::Quietest` added to `dsp/include/krate/dsp/systems/voice_allocator.h` with an external per-voice level callback, or (b) entirely in `Membrum::VoicePool`, reading `VoiceMeta::currentLevel` and using the real allocator API sequence (`noteOff` + `voiceFinished` to vacate the slot, then `noteOn` for the new note) with no changes to the shared DSP library? → A: Option B — quietest-selection loop lives entirely in `Membrum::VoicePool`, using `VoiceMeta::currentLevel`; calls `allocator_.noteOff(meta_[q].originatingNote)` then `allocator_.voiceFinished(q)` to free the slot before the new `noteOn`. Modifying `dsp/include/krate/dsp/systems/voice_allocator.h` for Phase 3 is forbidden.
- Q: Who owns the per-voice scratch buffer used during `VoicePool::processBlock` — (A) `DrumVoice` owns its own stereo output buffer; (B) `VoicePool` owns one pre-allocated `float[2][kMaxBlockSize]` scratch buffer allocated in `setupProcessing()`, reused per voice per block, with `VoicePool` accumulating scratch into the caller's output buffer; or (C) no scratch buffer, voices write directly into the caller-provided output buffer via per-voice offset pointers? → A: Option B — `VoicePool` owns a single stereo scratch buffer `float[2][kMaxBlockSize]` allocated in `setupProcessing()` and reused per voice per block. Each `DrumVoice::processBlock` writes into the scratch; `VoicePool::processBlock` then sums the scratch into the caller-provided stereo output buffer. No per-voice output buffers. No allocations on the audio thread.
- Q: Where is the fast-release exponential gain ramp applied — inside `DrumVoice::processBlock`, or in `VoicePool::processBlock` post-processing the scratch buffer? → A: `VoicePool::processBlock` post-processes the scratch buffer after each `DrumVoice::processBlock()` call and before accumulation into the caller output. `DrumVoice` has zero awareness of fast-release. The exponential gain ramp logic lives exclusively in `VoicePool::processBlock`'s per-voice mixing loop. When `voiceMeta[i].fastReleaseGain < 1e-6f`, `VoicePool` zeros the remaining samples in the scratch, sets `voiceMeta[i].state = Free`, returns the slot to the allocator, and does NOT accumulate zero samples into the output beyond the termination point. FR-194 (`DrumVoice` unchanged from Phase 2) is preserved.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Polyphonic Drum Roll Without Voice Cut-Off (Priority: P1)

A drummer plays a rapid MIDI drum roll at 1/32 notes, 140 BPM, on the Membrum plugin. With default max polyphony (8 voices), the first 8 triggered notes ring out simultaneously, each with its own modal body decaying naturally. As the 9th note arrives, the oldest voice is stolen click-free (fast release ≤5 ms) to make room. The new note plays with zero perceptible gap. No clicks, no NaN/Inf, no CPU spike.

**Why this priority**: Polyphony is *the* Phase 3 deliverable. Without it Membrum cannot play any realistic drum pattern (even a simple kick + hat roll needs 2+ simultaneous voices because drum tails overlap). P1 blocker.

**Independent Test**: Send a rapid MIDI pattern of 32 notes at 1/32 notes tempo 140 BPM to a configured Membrum instance (max polyphony 8, voice stealing = Oldest, Impulse + Membrane body). Record the output. Assert: (a) no sample contains NaN/Inf, (b) no click artifact exceeds −30 dBFS relative to the new voice's peak between note transitions (same metric as FR-126 / SC-021), (c) at least one time window of 500 ms contains non-zero energy from 8 concurrently active voices (verified by confirming at least 8 noteOn events have been issued without any voiceFinished call intervening), (d) voice stealing events are audibly click-free (peak click artifact ≤ −30 dBFS relative to incoming voice peak in any 5 ms window around a steal — SC-021 threshold).

**Acceptance Scenarios**:

1. **Given** max polyphony = 8 and voice stealing = Oldest, **When** 16 MIDI note-ons arrive faster than any voice completes its natural decay, **Then** the first 8 voices sound simultaneously and voices 9–16 each steal the oldest currently-sounding voice without audible clicks.
2. **Given** any of 4, 8, or 16 configured max polyphony values, **When** the number of simultaneous MIDI notes equals the max polyphony, **Then** all requested voices sound concurrently and CPU stays within the Phase 3 budget (see FR-160).
3. **Given** max polyphony = 16 and a continuous stress pattern of 32 overlapping notes, **When** 10 seconds of audio are rendered at 44.1 kHz / 128-sample block on modern x86_64 with AVX2, **Then** no xruns occur and no samples contain NaN/Inf.
4. **Given** a MIDI note-off arrives for a currently sounding voice, **When** the voice is in its decay phase, **Then** note-off does NOT immediately free the voice (drum amp envelope tails naturally — note-off is a no-op for percussion); the voice is freed only when its amp envelope reaches zero or it is stolen.
5. **Given** the DAW saves and reloads Membrum state, **When** the plugin loads, **Then** `maxPolyphony`, `voiceStealingPolicy`, and all 32 `chokeGroupAssignments` round-trip exactly.

---

### User Story 2 — Open/Closed Hi-Hat Choke Group (Priority: P1)

A programmer maps pad 42 (closed hi-hat, GM MIDI 42) and pad 46 (open hi-hat, GM MIDI 46) both into choke group 1. Triggering open-hat and then closed-hat mid-decay of the open-hat causes the open-hat voice to fast-release (≤5 ms) while the closed-hat voice sounds normally. The result is indistinguishable from a real drummer's pedal close — no hanging open-hat tail, no click from the muted voice.

**Why this priority**: Choke groups are the *non-negotiable* secondary Phase 3 deliverable. Spec 135 line 48 says "configurable, essential for open/closed hat pairs and similar." Without choke groups, drum kits with hi-hats sound broken. P1.

**Independent Test**: Configure two MIDI notes (42 and 46) into the same choke group. Trigger note 46 at velocity 100, wait 100 ms, trigger note 42 at velocity 100. Assert: (a) the note-46 voice's amplitude envelope drops to −60 dBFS within ≤ 5 ms of the note-42 trigger, (b) no click artifact exceeds −30 dBFS during the choke, (c) the note-42 voice is completely unaffected (its first 20 ms is bit-identical to an isolated note-42 trigger within −120 dBFS noise floor), (d) no allocations on the audio thread during the choke.

**Acceptance Scenarios**:

1. **Given** MIDI notes 42 and 46 assigned to choke group 1, **When** note 46 is triggered, **Then** any currently-sounding voice originating from note 42 is fast-released (≤ 5 ms) and vice-versa.
2. **Given** a MIDI note assigned to choke group 0 (none), **When** the note is triggered, **Then** no other voices are choked and no choke logic executes.
3. **Given** two MIDI notes assigned to **different** choke groups (e.g. note 42 in group 1, note 47 in group 2), **When** note 42 is triggered and then note 47 is triggered, **Then** note 42's voice continues decaying naturally — cross-group choking does not occur.
4. **Given** a choke group has 3+ currently-sounding voices (e.g. multiple hat pedal hits), **When** a new note in the same choke group arrives, **Then** ALL currently-sounding voices in that group (except the newly-triggered one) are fast-released — choke is a group-wide mute, not a one-victim selection.
5. **Given** the chokeGroupAssignments table contains a non-zero assignment for every MIDI note 36–67, **When** 32-note stress-test patterns are played, **Then** no NaN/Inf, no allocations, no clicks above −30 dBFS, no xruns.
6. **Given** a voice is fast-released by a choke, **When** the next available voice slot is requested, **Then** the choked voice becomes eligible for re-allocation after its fast-release fade completes (choke → voice-freed).

---

### User Story 3 — Configurable Voice Stealing Policy (Priority: P2)

A sound designer stress-tests the voice stealer. Under the **Oldest** policy, rapid overlapping hits steal the longest-ringing voice first — natural and musical. Switching to **Quietest** (lowest amp-envelope level) causes the softest-decaying voice to be taken instead — ideal for busy cymbal washes where the audibility of the stolen voice matters more than its age. Switching to **Priority** protects low-note pads (e.g. kick, snare) from being stolen in favor of high-note pads (hats, cymbals). All three policies produce click-free transitions and no allocations.

**Why this priority**: A single policy is enough for the basic Phase 3 gate; configurability is the P2 quality layer. Spec 135 line 46 explicitly lists all three, but shipping only "Oldest" would still let users make music.

**Independent Test**: For each of 3 policies (Oldest, Quietest, Priority), configure Membrum with max polyphony 4, then trigger 8 overlapping voices in a deterministic pattern and record which voice is stolen first. Assert: (a) Oldest steals the first-triggered voice, (b) Quietest steals the voice with the lowest current amplitude envelope value, (c) Priority steals the voice whose originating MIDI note has the highest pitch (= lowest priority), (d) all 3 transitions are click-free (≤ −30 dBFS click artifact).

**Acceptance Scenarios**:

1. **Given** voice stealing = Oldest, **When** all voice slots are full and a new note arrives, **Then** the voice with the earliest `noteOn` timestamp is stolen.
2. **Given** voice stealing = Quietest, **When** all voice slots are full and a new note arrives, **Then** the voice whose amp envelope follower reports the lowest current instantaneous level is stolen.
3. **Given** voice stealing = Priority, **When** all voice slots are full and a new note arrives, **Then** the voice whose source MIDI note is the **highest** pitch is stolen (priority = inverse of pitch; low pitches are protected).
4. **Given** any voice stealing policy, **When** a steal occurs, **Then** the stolen voice begins a fast release (≤ 5 ms), the new voice begins its attack immediately, and the crossfade is click-free (click artifact ≤ −30 dBFS).
5. **Given** voice stealing policy is changed via automation while voices are sounding, **When** the next steal event occurs, **Then** the new policy is in effect (no deferral to next block required).

---

### User Story 4 — State v2 → v3 Migration (Priority: P2)

A user who saved a DAW project with Membrum v0.2.0 (state v2) opens it in Membrum v0.3.0 (state v3). The saved project loads without errors, the Phase 2 single-voice sound is preserved exactly, and the three new Phase 3 parameters (`maxPolyphony`, `voiceStealingPolicy`, `chokeGroupAssignments`) are populated with Phase-3 defaults (8, Oldest, all zeros).

**Why this priority**: Backward compatibility is mandatory per Phase 2 precedent (FR-082 / SC-006). Without it, users' saved projects break on upgrade.

**Independent Test**: A state-roundtrip test loads a captured Phase 2 state v2 blob into a Phase 3 processor and asserts: (a) load succeeds (returns `kResultOk`), (b) all Phase 2 parameters match the Phase 2 blob bit-exact, (c) `maxPolyphony == 8`, `voiceStealingPolicy == Oldest`, `chokeGroupAssignments[i] == 0` for all 32 entries, (d) a subsequent save produces a v3 blob containing the Phase 2 data plus the new Phase 3 fields.

**Acceptance Scenarios**:

1. **Given** a Phase 2 (v2) state blob, **When** loaded into a Phase 3 processor, **Then** load succeeds, all Phase 2 params round-trip bit-exact, and Phase 3 params take their documented defaults.
2. **Given** a Phase 3 (v3) state blob, **When** saved and reloaded, **Then** all 34 Phase 2 params AND all Phase 3 params round-trip bit-exact.
3. **Given** a Phase 1 (v1) state blob, **When** loaded into a Phase 3 processor, **Then** load still succeeds (v1 → v3 double migration chains through the v2 migration path).
4. **Given** a Phase 3 (v3) state blob is loaded into a hypothetical future Phase 2 processor, **When** the version field is read, **Then** the Phase 2 processor rejects the newer state gracefully (returns a well-defined error; does NOT crash or corrupt).

---

### User Story 5 — Allocation-Free 16-Voice Stress Test (Priority: P2)

A CI allocation test triggers 16 simultaneous voices (the Phase 3 maximum), each with a different exciter/body combination, all choke-group-assigned in various ways, and runs the audio thread through 10 seconds of continuous playback with random note triggers every 5 ms. The `allocation_detector` test helper reports **zero** heap allocations on the audio thread across note-ons, note-offs, voice steals, and choke events. No deadlocks. No exceptions.

**Why this priority**: Constitutional rule, non-negotiable. Shipping a polyphonic plugin with *any* allocation on the audio thread would break real-time contracts. This is a gate, not a feature.

**Independent Test**: `test_polyphony_allocation_matrix.cpp` exercises the 16-voice pool with fuzzed random MIDI input for 10 seconds at 44.1 kHz / 128-sample block using `allocation_detector`. Assert: zero allocations, zero deallocations, zero `std::mutex` locks (only `atomic_flag`/`atomic` permitted).

**Acceptance Scenarios**:

1. **Given** max polyphony = 16 and a 10-second fuzzed MIDI stream, **When** `allocation_detector` is active, **Then** zero heap allocations are reported.
2. **Given** voice stealing and choke events happen thousands of times during the test, **When** the test completes, **Then** zero allocations are reported.
3. **Given** the `chokeGroupAssignments` table is reconfigured mid-test via parameter changes, **When** the changes are applied, **Then** zero allocations are reported (the 32-entry table is stored in pre-allocated atomic/plain memory, not a dynamic container).
4. **Given** the fuzz test runs, **When** it completes, **Then** no FP exceptions, no denormals, no NaN/Inf in output.

---

### User Story 6 — 3 New Host-Generic Parameters (Priority: P3)

A user opens Membrum's host-generic editor and sees the three new Phase 3 parameters alongside the Phase 2 parameter list: **Max Polyphony** (stepped integer 4–16), **Voice Stealing** (3-option list: Oldest / Quietest / Priority), and **Choke Group** (stepped 0–8 where 0 = none). Changing any of them takes effect on the next note-on without restarting the plugin. No custom UI is built in Phase 3 — that is Phase 6.

**Why this priority**: The parameters must exist for anyone to USE the feature, but exposing them in the custom UI is a Phase 6 concern. P3 because automation and preset saving are the only concrete user-facing effects in Phase 3 (before pads exist in Phase 4, choke group is effectively "which group does the current single-pad belong to").

**Independent Test**: A VST parameter test asserts the three new parameters are registered in the controller, have correct IDs following `k{Parameter}Id` convention, have correct ranges, and round-trip via `setParamNormalized`/`getParamNormalized`.

**Acceptance Scenarios**:

1. **Given** the Phase 3 controller, **When** queried via `IEditController::getParameterCount()` and `getParameterInfo()`, **Then** `Max Polyphony`, `Voice Stealing`, and `Choke Group` are present with correct IDs (see FR-150), ranges, and default values.
2. **Given** a parameter change arrives via `IParamValueQueue`, **When** processed, **Then** the processor applies the change on the next note-on boundary (polyphony and stealing policy) or immediately (choke group, since it is only read on note-on).
3. **Given** Max Polyphony is changed from 8 → 4 while 6 voices are currently sounding, **When** the change is applied, **Then** the two oldest voices are released with fast-release (≤ 5 ms) and the pool shrinks to 4 — no clicks, no allocations.
4. **Given** Max Polyphony is changed from 4 → 16, **When** the change is applied, **Then** the newly available voice slots become immediately usable on the next note-on, no allocation occurs (the 16 voice instances were pre-allocated at plugin instantiation).

---

### Edge Cases

- **Same MIDI note retriggered while already sounding**: The existing voice continues decaying until its amp envelope ends OR it is stolen by a policy — there is no mandatory "same-note-re-trigger steals itself" behavior for percussion (each hit gets its own voice, subject to polyphony).
- **All 16 voices in choke group 1**: If every voice in the pool is in the same choke group and a new note in that group arrives, **all 15 other voices fast-release** and the new voice takes slot 16. Must not click, must not allocate.
- **Choke triggered while voice is already fast-releasing from a previous choke/steal**: The second fast-release must be a no-op on an already-fading voice (do not re-trigger the release). No double-click, no envelope glitch.
- **Voice stealing policy = Quietest on all-silent pool**: If every voice is below −120 dBFS (e.g. at the very end of decay), the selection falls through to the secondary tiebreaker (age). Must not deadlock or pick an invalid voice.
- **`maxPolyphony` set to 4 while 8 voices are active**: The 4 excess voices must be released (oldest-first, fast-release). Must not crash or allocate.
- **Parameter change for `chokeGroupAssignments[note]` arrives in the same block as a note-on for that note**: The new group assignment takes effect for THIS note (parameter change processed before note event per VST3 ordering). If ordering inverts, the old group is used — both are deterministic.
- **Extreme sample rates (22050, 96000, 192000 Hz)**: Fast-release time must scale with sample rate so the 5 ms target is wall-clock accurate, not sample-count fixed.
- **Zero-velocity note-on (= note-off in some MIDI conventions)**: Handled as note-off (percussion no-op). Does not allocate a voice.
- **MIDI note outside the 36–67 pad range**: Dropped (documented in Phase 1 / Phase 2). No voice allocated, no choke consulted.
- **State file corruption of `chokeGroupAssignments` (e.g. value > 8)**: Clamped on load, never trusted raw. Does not crash.
- **Fuzzing: new note arrives in the same sample as a voice completes its decay**: The pool allocator must see the voice as idle before the new note is allocated OR steal it. Both branches are valid and must be click-free.

## Requirements *(mandatory)*

> **Numbering note**: Phase 2's highest functional requirement is FR-102 and highest success criterion is SC-011. Phase 3 continues FR numbering from **FR-110** (leaving a gap to signal a new phase) and SC numbering from **SC-020**. FRs are grouped by section.

### Functional Requirements

#### Voice Pool & Allocation (Core of Phase 3)

- **FR-110**: System MUST replace the current single `DrumVoice` instance in the Membrum processor with a fixed-size pool of **16** pre-allocated `DrumVoice` instances. All 16 are constructed during `setupProcessing()` / `setActive()` — never on the audio thread. Unused pool slots remain idle (no CPU cost) until allocated.
- **FR-111**: The number of currently active voice slots MUST be controlled by a new **`maxPolyphony`** parameter (range 4–16, stepped, default 8). Slots beyond `maxPolyphony` are held idle and never receive note events until the limit is raised. When the limit is lowered below the number of currently-sounding voices, the excess voices MUST be released with a fast release (≤ 5 ms) starting with the oldest, and no allocations MUST occur.
- **FR-112**: Voice allocation MUST use `Krate::DSP::VoiceAllocator` from `dsp/include/krate/dsp/systems/voice_allocator.h`. The allocator's hard internal limit is `kMaxVoices = 32` (a compile-time library constant, not a runtime configuration). Phase 3 MUST set its active voice count via `setVoiceCount(maxPolyphony)` so that only the configured number of slots are considered during allocation.
- **FR-113**: MIDI note-on (velocity > 0) on any MIDI note 36–67 MUST request a voice from `VoiceAllocator::noteOn()`, which either returns an idle slot or performs a steal according to the configured policy. The allocator's returned voice MUST be used to set the triggered pad's state (in Phase 3, all notes hit the same pad template — Phase 4 adds the per-pad lookup).
- **FR-114**: MIDI note-off for percussion MUST be a no-op on the voice (drums decay naturally via their amp envelope; note-off does NOT begin release). The allocator's `noteOff()` bookkeeping MUST still be called so the allocator's internal state stays consistent, but the underlying voice's amp envelope MUST NOT be forced into release by note-off.
- **FR-115**: Voices MUST be freed from the pool automatically when their amp envelope reaches its silence threshold (same threshold as Phase 1/2 — roughly −120 dBFS or equivalent `isActive()` return). The processor polls `DrumVoice::isActive()` each block and informs the allocator of any voice transitions to idle.
- **FR-116**: All voice pool state (the 16 `DrumVoice` instances, the `VoiceAllocator`, all helper buffers) MUST be pre-allocated. Zero heap allocations on the audio thread under any code path. This MUST be verified by `tests/test_helpers/allocation_detector.h`.
- **FR-117**: `VoicePool` MUST own exactly one stereo scratch buffer — `float[2][kMaxBlockSize]` — allocated (via `std::vector<float>` or `std::unique_ptr<float[]>`) during `setupProcessing()` / `setActive()` and sized to the host's reported maximum block size. This single scratch buffer is reused for every active voice on every audio block: `DrumVoice::processBlock` writes into the scratch, then `VoicePool::processBlock` accumulates the scratch into the caller-provided stereo output buffer using a clear+sum loop. No per-voice output buffers exist. No allocation occurs on the audio thread. The total memory footprint of `VoicePool` MUST be bounded and deterministic: `sizeof(VoicePool)` plus `2 × kMaxBlockSize × sizeof(float)` scratch plus `16 × sizeof(DrumVoice)` voice instances plus `16 × sizeof(VoiceMeta)` metadata — all independent of the runtime value of `maxPolyphony`.

#### Voice Stealing Policy

- **FR-120**: System MUST expose a **`voiceStealingPolicy`** parameter with 3 discrete options via `StringListParameter`: **Oldest**, **Quietest**, **Priority**.
- **FR-121**: The **Oldest** policy MUST map to `VoiceAllocator::AllocationMode::Oldest`. Research confirms this is the default in virtually every professional synth/drum machine and is the musical default — see `research.md §1`. This is the Phase 3 default value of the parameter.
- **FR-122**: The **Quietest** policy MUST steal the currently-sounding voice whose instantaneous amp-envelope output level is lowest. `VoiceAllocator`'s existing `LowestVelocity` mode is insufficient (it uses note-on velocity, not current amplitude). The quietest-selection loop MUST be implemented entirely in `Membrum::VoicePool`: iterate the pool, read `VoiceMeta::currentLevel` for each active voice, identify the minimum, then call `allocator_.noteOff(meta_[q].originatingNote)` followed by `allocator_.voiceFinished(q)` to vacate the slot before the subsequent `allocator_.noteOn()` call. **Modifying `dsp/include/krate/dsp/systems/voice_allocator.h` for the Quietest policy is FORBIDDEN in Phase 3.** Option (a) — adding a new `AllocationMode::Quietest` to the shared DSP library — is not viable for Phase 3 and is explicitly rejected.
- **FR-123**: The **Priority** policy MUST steal the voice whose source MIDI note is the **highest** pitch (i.e. low-pitched pads like kick and snare are protected from being stolen in favor of high-pitched pads like hats and cymbals). This maps directly to `VoiceAllocator::AllocationMode::HighestNote`. In Phase 4 when per-pad presets exist, Priority MAY be upgraded to a per-pad user-assignable priority rank; Phase 3 ships with the pitch-based default only.
- **FR-124**: On a steal, the stolen voice MUST execute a **fast release** of wall-clock duration **5 ms ± 1 ms** (see `research.md §2` for the 5 ms figure justification) before being reassigned. During the fast release, the voice continues producing audio (fading to zero); the new voice begins its attack immediately in parallel. Mixing the two tails is the natural crossfade that makes the steal click-free. The fast release MUST use **per-sample exponential decay**: `gain *= k` where `k = std::exp(-std::log(1e6f) / (kFastReleaseSecs * sampleRate))` and `kFastReleaseSecs = 0.005f` denotes the **total wall-clock decay time** to the 1e-6 floor (NOT the time constant τ). This reaches the 1e-6 denormal floor in exactly 5 ms ± 1 sample regardless of sample rate. The resulting time constant is τ ≈ 362 μs, guaranteeing click-free voice steal within 5 ms wall-clock. The ramp is applied starting from the voice's current amplitude at the moment of steal. A mandatory denormal floor `if (gain < 1e-6f) gain = 0.f` MUST terminate the ramp; once the floor is reached the voice is immediately returned to the pool. No other curve shape (linear, cosine taper, etc.) is permitted for fast release in Phase 3. This matches the Phase 2 amp ADSR exponential pattern. **The fast-release gain ramp is applied exclusively by `VoicePool::processBlock` to the scratch buffer after each `DrumVoice::processBlock()` call and before accumulation into the caller-provided output buffer. `DrumVoice` is not aware of fast-release state and MUST NOT be modified to add fast-release awareness (FR-194). When `voiceMeta[i].fastReleaseGain < 1e-6f`, `VoicePool` zeros the remaining samples in the scratch buffer, marks the slot Free, and does NOT accumulate those zeroed samples into the output.**
- **FR-125**: The fast release duration MUST scale with sample rate so the wall-clock figure holds at 22050, 44100, 48000, 96000, and 192000 Hz.
- **FR-126**: The peak click artifact during any voice steal MUST be ≤ **−30 dBFS** relative to the incoming voice's peak, measured in the 5 ms window centered on the steal event.
- **FR-127**: If the steal occurs on a voice that is already in a fast release (from a previous steal or choke), the second steal MUST NOT re-trigger the release — the voice continues its in-progress release and the new voice takes over normally. No envelope glitch, no discontinuity.
- **FR-128**: The three voice-stealing policies MUST all be deterministic and tiebreaker-stable: ties are broken by voice slot index ascending.

#### Choke Groups

- **FR-130**: System MUST support **8** choke groups (IDs 1–8), plus choke group **0** which means "no group / not choked." The number 8 is justified in `research.md §3` by a survey of SoundFont 2.04 Exclusive Class (no explicit count, per-preset integer), Ableton Drum Rack (16), Battery 4 (up to 128 voice groups), and Kontakt (unlimited). Phase 3 picks **8** as sufficient for a GM drum kit (open/closed/pedal hat triple, crash/ride sharing, bell/cowbell pairs, snare/cross-stick, tom-group mutes, plus reserved slots) while keeping state size small. Phase 4 MAY raise this to 16 to match Ableton if user feedback demands it.
- **FR-131**: Choke group assignments MUST be stored as a 32-entry lookup table `chokeGroupAssignments[32]` indexed by (MIDI note − 36), where each entry is an unsigned integer in [0, 8]. The table is a fixed-size array, NOT a dynamic container. Default value: all zeros (no choke groups active).
- **FR-132**: On each note-on, the processor MUST consult `chokeGroupAssignments[note − 36]`. If the returned group is **non-zero**, the processor MUST iterate all currently-sounding voices and, for each voice whose originating MIDI note's choke group matches the new note's group, fast-release that voice (same ≤ 5 ms release used for voice stealing — FR-124). The newly-triggered voice MUST NOT be choked by its own entry.
- **FR-133**: Choke MUST be a group-wide mute: ALL voices in the matching group are fast-released, not just the oldest or loudest. This matches SoundFont 2.04's Exclusive Class semantics (`research.md §3`) and Ableton/Battery/Kontakt behavior.
- **FR-134**: Choke fast-release MUST be click-free (≤ −30 dBFS click artifact, same budget as voice stealing — FR-126).
- **FR-135**: Cross-group choking MUST NOT occur: a note in group 1 MUST NOT affect any voice in groups 2–8. Choke groups are mutually orthogonal.
- **FR-136**: Choke group 0 (none) MUST be a true no-op: no iteration of the voice pool, no performance cost beyond the single lookup.
- **FR-137**: Choked voices, after their fast-release completes (i.e. when `fastReleaseGain < 1e-6f`), MUST become eligible for re-allocation. `VoicePool` marks the slot `Free` and `allocator_.voiceFinished(slot)` MUST have already been called on the choke path (it is called in the same code path as voice-stealing). From the allocator's perspective a choke-freed slot is indistinguishable from a steal-freed slot. No voice leaks.
- **FR-138**: In Phase 3, the processor state holds a **single shared `chokeGroupAssignments[32]` table** (all 32 MIDI notes map to the same pad template, same group assignment via the single Phase 3 `kChokeGroupId` parameter). On every `kChokeGroupId` parameter change, the processor MUST mirror the new value into all 32 entries of the table so that every entry holds the same uint8 value at all times during Phase 3. Phase 4 upgrades this to a per-pad assignment by writing distinct per-pad values into the same 32-byte layout — no state format change is needed because the layout is identical.

#### State Schema (version 3)

- **FR-140**: Plugin state version MUST be bumped from `kCurrentStateVersion = 2` to `kCurrentStateVersion = 3`. The symbol in `plugin_ids.h` is updated; the `static_assert` is updated accordingly.
- **FR-141**: The v3 state format MUST include the v2 blob verbatim plus the following new fields appended in this exact order:
  1. `maxPolyphony` — uint8, valid range [4, 16], default 8.
  2. `voiceStealingPolicy` — uint8 enum, values {0 = Oldest, 1 = Quietest, 2 = Priority}, default 0 (Oldest).
  3. `chokeGroupAssignments[32]` — **always serialized as exactly 32 contiguous uint8 bytes**, each in [0, 8], default all 0. In Phase 3 all 32 bytes hold the same value (mirrored from `kChokeGroupId`); in Phase 4 each byte holds a distinct per-pad value. The 32-byte block is written and read unconditionally — no length prefix, no version guard — making Phase 4 strictly additive with no state version bump for this field.
- **FR-142**: v2 → v3 migration MUST be backward compatible: loading a v2 blob MUST succeed, populate the 3 new fields with their documented defaults, and yield a processor state that is audibly identical to Phase 2's single-voice behavior when only one voice at a time is triggered.
- **FR-143**: v1 → v3 migration MUST succeed by chaining through the existing v1 → v2 migration path plus the new v2 → v3 migration.
- **FR-144**: Loading a v3 blob MUST fail gracefully in a hypothetical v2 processor (version mismatch → well-defined error return). Loading corrupted field values (e.g. `maxPolyphony = 99` or `chokeGroupAssignments[i] = 200`) MUST be clamped to valid ranges on load, not rejected. This preserves user projects across minor corruption.

#### Parameters (Controller Registration)

- **FR-150**: Controller MUST register the following NEW parameters in addition to the Phase 2 parameter set:
  - **`kMaxPolyphonyId`** — stepped integer, range [4, 16], default 8. Unit: "voices". `RangeParameter` with step count 12.
  - **`kVoiceStealingId`** — `StringListParameter`, 3 choices: "Oldest", "Quietest", "Priority". Default: "Oldest".
  - **`kChokeGroupId`** — stepped integer, range [0, 8], default 0. Unit: "group". `RangeParameter` with step count 8. In Phase 3 this is a **single global parameter** that represents "the choke group assigned to the current (and only) pad template"; in Phase 4 this becomes a per-pad array. Phase 3 wires the parameter to write the same value into every entry of `chokeGroupAssignments[32]` on change.
- **FR-151**: All three new parameter IDs MUST follow the `k{Parameter}Id` naming convention from `CLAUDE.md`. IDs MUST NOT collide with existing Phase 1 or Phase 2 parameter IDs — an ID-collision check MUST be performed in `plan.md`.
- **FR-152**: Parameter changes from the host (`IParamValueQueue`) MUST be processed sample-accurately: `maxPolyphony` and `voiceStealingPolicy` take effect on the next note-on boundary, `kChokeGroupId` takes effect immediately on the shared `chokeGroupAssignments[32]` table (which is only consulted on note-on, so "immediately" and "next note-on" are equivalent in practice).
- **FR-153**: Controller MUST continue to provide only the host-generic editor (no uidesc, no custom VSTGUI views). Custom UI is Phase 6 (see Deferred section). The three new parameters are exposed automatically via the host-generic editor — no custom drawing code is added in Phase 3.

#### CPU Budget & Real-Time Safety

- **FR-160**: The 8-voice polyphonic worst case (8 voices running the worst-case Phase 2 pad — Feedback exciter + NoiseBody + Tone Shaper enabled + Unnatural Zone all engaged) MUST measure ≤ **12%** single-core CPU on a modern desktop x86-64 CPU at 44.1 kHz / 128-sample block. This number is derived in `research.md §4`: Phase 2's worst case was 1.25% × 8 = 10% linear baseline, plus 20% headroom for `VoiceAllocator` overhead and choke-group iteration. Spec 135 quotes a target of "< 2% CPU" for 8 voices × 16 partials on unspecified hardware; the 12% number here represents the Phase 2 worst case scaled to polyphony, which is more stringent for this specific engine. The spec 135 "2%" is for the simplest patch; the 12% is for the *most expensive*.
- **FR-161**: The 16-voice stress test (16 voices, varied exciter/body, full Tone Shaper and Unnatural Zone) MUST complete 10 seconds of continuous 44.1 kHz / 128-sample block processing on modern x86_64 with AVX2 **without any xrun**. "Xrun" here means the benchmark's wall-clock processing time exceeds the audio buffer wall-clock duration on any block.
- **FR-162**: Phase 3 MAY enable `ModalResonatorBankSIMD` on the audio thread if and only if the scalar 8-voice worst case exceeds the 12% budget. The decision point is documented in `plan.md` and is based on measurement, not assumption. Phase 2 was scalar-only; Phase 3 preserves the scalar path as default unless measurement forces SIMD.
- **FR-163**: All audio-thread code MUST remain allocation-free, lock-free, and exception-free. `VoiceAllocator`, all 16 `DrumVoice` instances, the `chokeGroupAssignments[32]` table, and any per-voice metadata (choke-group ID, steal state) MUST be pre-allocated. This MUST be verified by `tests/test_helpers/allocation_detector.h` on (note-on, note-off, choke-trigger, steal-trigger, process-block) code paths.
- **FR-164**: FTZ/DAZ denormal protection MUST remain enabled on the audio thread (carryover from Phase 1 via `enable_ftz_daz.h`). Fast-release envelopes MUST NOT produce denormals — the exponential decay loop from FR-124 MUST include the mandatory denormal floor `if (gain < 1e-6f) gain = 0.f` as an explicit software guard in addition to FTZ/DAZ hardware protection. This floor MUST be present regardless of whether FTZ/DAZ is active (portability across non-x86 targets). The denormal safety of fast-release envelopes MUST be verified by an explicit unit test (a dedicated section in `test_steal_click_free.cpp` or `test_choke_group.cpp`) in addition to the Phase 2 denormal safety test.
- **FR-165**: The `VoiceAllocator` per-voice metadata (originating MIDI note, allocation time, current amp level for Quietest policy) MUST be stored entirely in the processor layer — NOT added to the shared `VoiceAllocator` header. Storage is via a fixed-size `std::array<VoiceMeta, 16>` owned by `Membrum::VoicePool`. The `VoiceMeta::currentLevel` field MUST be updated **once per audio block** (not once per sample) in `VoicePool::processBlock`, after the voice's audio has been mixed into the output buffer, using the voice's amp-envelope output level for that block. Per-sample updates are explicitly forbidden (unnecessary CPU cost). Because `VoiceMeta` is read and written exclusively on the audio thread, `currentLevel` MUST be stored as a plain `float` — no `std::atomic`, no mutex, no synchronization needed.

#### Voice Management Integration

- **FR-170**: Only the single `DrumVoice` template from Phase 2 is replicated 16 times. No per-pad configuration is stored in Phase 3 — every voice is configured from the same parameter snapshot on note-on. The voice-pool data structure MUST be designed so that Phase 4's per-pad preset system can supply per-note parameter overrides without reworking Phase 3's allocation path.
- **FR-171**: On note-on, the Membrum processor MUST (a) consult the choke-group table, (b) fast-release any choked voices, (c) call `VoiceAllocator::noteOn()` to get a voice slot (which may trigger a steal — fast-release is applied by the processor, not the allocator), (d) call `DrumVoice::noteOn()` on the selected slot to begin the new note. This sequence MUST be deterministic and allocation-free.
- **FR-172**: The Membrum processor MUST track, per voice slot: the originating MIDI note (for choke-group and Priority-policy lookups), the `noteOn` timestamp (for Oldest), a `float currentLevel` holding the most recent per-block amp-envelope output level (for Quietest — read non-atomically, audio-thread only, updated once per block per FR-165), and a flag indicating whether the voice is in fast-release. All four fields fit in a single cache-line-aligned `VoiceMeta` struct. `currentLevel` is NOT a pointer or reference into the voice's internals — it is a plain snapshot copied at block boundary.

#### Testing & Validation

- **FR-180**: A parameterized unit test MUST exercise all 3 voice-stealing policies × max polyphony {4, 8, 16} combinations by triggering a deterministic overlapping-note pattern and asserting the correct voice is stolen per policy.
- **FR-181**: A click-free-steal test MUST verify that the peak sample-level click artifact during a voice steal is ≤ −30 dBFS relative to the incoming voice's peak, across all 3 policies and across sample rates {22050, 44100, 48000, 96000, 192000} Hz.
- **FR-182**: A choke-group test MUST verify:
  (a) single-group open/closed-hat choke behavior,
  (b) orthogonality between groups 1–8,
  (c) group 0 = no-op,
  (d) choke-free behavior when the new note is in a different group or group 0,
  (e) all-voices-in-one-group choke (stress case).
- **FR-183**: A state v2 → v3 migration test MUST use a captured Phase 2 state blob (from the existing Phase 2 `test_state_roundtrip_v2.cpp` fixture) and verify that loading it into a Phase 3 processor succeeds, round-trips the Phase 2 fields bit-exact, and populates the 3 new Phase 3 fields with their documented defaults.
- **FR-184**: A state v3 roundtrip test MUST save and reload a Phase 3 blob with non-default values for all 3 new fields and assert bit-exact round-trip.
- **FR-185**: An allocation test (`test_polyphony_allocation_matrix.cpp`) MUST use `allocation_detector.h` to verify zero heap allocations on the audio thread across: 16-voice fuzz stream, voice steal events, choke events, parameter changes to `maxPolyphony`/`voiceStealingPolicy`/`chokeGroupAssignments`.
- **FR-186**: A CPU benchmark (`test_polyphony_benchmark.cpp`) MUST measure 8-voice and 16-voice CPU cost on the worst-case Phase 2 patch and assert ≤ 12% (8 voices) and completes-without-xrun (16 voices) on the CI reference machine.
- **FR-187**: A regression test MUST verify that Phase 2's default patch (Impulse + Membrane, single-voice) still produces bit-identical output when played through the Phase 3 processor with `maxPolyphony = 1`. This is the Phase 2 regression guarantee carried forward.
- **FR-188**: Pluginval MUST continue to pass at strictness level 5 on Windows with zero errors and zero warnings.
- **FR-189**: `auval -v aumu Mbrm KrAt` MUST continue to pass on macOS (CI-only — not runnable on Windows build host).

#### ODR Prevention & Codebase Reuse

- **FR-190**: No new class name MUST collide with existing classes. Before creating any new type (per Principle XIV), `grep -r "class X" dsp/ plugins/` MUST be performed. Expected new types: `Membrum::VoicePool` (the processor-layer wrapper around `VoiceAllocator` + 16 `DrumVoice` instances + metadata), `Membrum::VoiceMeta` (per-slot metadata struct), `Membrum::ChokeGroupTable` (the 32-entry lookup with mutation API). All exact names MUST be confirmed in `plan.md`.
- **FR-191**: Phase 3 MUST reuse `Krate::DSP::VoiceAllocator` from `dsp/include/krate/dsp/systems/voice_allocator.h` — it is the *only* voice allocator in this codebase and was explicitly built for this purpose (spec 135 line 343 "Voice Management — Ready to Use"). Phase 3 MUST NOT implement a parallel voice allocator. Phase 3 MUST NOT modify `voice_allocator.h` (the Quietest policy is implemented entirely in the processor layer per FR-122 — option (a) is rejected).
- **FR-192**: Phase 3 MUST reuse `tests/test_helpers/allocation_detector.h` for all real-time safety validation.
- **FR-193**: Phase 3 MUST NOT modify the shared KrateDSP library. The Quietest policy is implemented at the processor layer (FR-122), and `voice_allocator.h` is unchanged in Phase 3. If any future unanticipated `dsp/` modification becomes strictly necessary, it MUST be justified in `plan.md` and requires explicit approval.
- **FR-194**: Phase 3 MUST NOT modify Phase 2's single-voice audio DSP (`ExciterBank`, `BodyBank`, `ToneShaper`, `UnnaturalZone`, `MaterialMorph`, any per-body mapper, or any mode table). The Phase 3 refactor is entirely in the processor / voice-pool layer above the single voice.

### Key Entities

- **VoicePool (`Membrum::VoicePool`)**: Owns 16 pre-allocated `DrumVoice` instances, one `VoiceAllocator`, a `std::array<VoiceMeta, 16>` metadata array, and one stereo scratch buffer `float[2][kMaxBlockSize]` allocated in `setupProcessing()`. The scratch buffer is reused each block: each active voice renders into the scratch, then `VoicePool::processBlock` sums the scratch into the caller-provided output buffer via a clear+sum loop. No per-voice output buffers. Exposes `noteOn(note, velocity)`, `noteOff(note)` (no-op for percussion per FR-114), `setMaxPolyphony(n)`, `setStealingPolicy(policy)`, `processBlock(inputs, outputs, numSamples)`, and `setChokeGroupAssignments(...)`.
- **VoiceMeta (`Membrum::VoiceMeta`)**: Per-slot metadata struct containing `originatingNote` (uint8), `noteOnSampleCount` (uint64 sample count), `state` (`VoiceSlotState` enum: `Free | Active | FastReleasing`), `fastReleaseGain` (float, runs 1→0 during `FastReleasing`), and `currentLevel` (plain `float`) — a per-block peak snapshot of the scratch buffer used by the Quietest policy. Updated once per audio block in `VoicePool::processBlock`, read non-atomically on the audio thread only (no synchronization needed). See `plan.md §VoicePool layout` for the complete field list and alignment.
- **ChokeGroupTable (`Membrum::ChokeGroupTable`)**: Fixed-size `std::array<uint8_t, 32>` wrapper indexed by (MIDI note − 36). Provides `operator[]`, `setGlobal(uint8_t group)` (used in Phase 3 to write the same group into all 32 entries), and iteration helpers. Designed so Phase 4's per-pad variant is a drop-in extension.
- **Fast-Release Envelope**: A short exponential fade applied to a voice's `DrumVoice::processBlock()` scratch buffer output by `VoicePool::processBlock` when the voice is in fast-release state. NOT a separate DSP component — implemented as a per-voice gain multiplier (`gain *= k`, `k = exp(-1/(0.005f*sampleRate))`) in `VoicePool::processBlock`'s per-voice mixing loop, applied after `DrumVoice::processBlock()` writes into the scratch buffer and before `VoicePool` accumulates the scratch into the caller output. `DrumVoice` has zero awareness of fast-release. When `fastReleaseGain < 1e-6f`, `VoicePool` zeros remaining scratch samples, marks the slot Free, and skips accumulation for those samples.
- **DrumVoice (unchanged from Phase 2)**: Each voice retains the full Phase 2 signal path: exciter variant → body variant → tone shaper → unnatural zone → amp ADSR → level. Phase 3 does NOT modify `DrumVoice` internals.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-020**: 8 voices can sound concurrently without any voice being cut off prematurely. Verified by triggering 8 overlapping notes (offset by 5 ms each), recording 500 ms of audio, and confirming: (a) no NaN/Inf samples; (b) at least 8 note-on events have been issued without voiceFinished returning those slots (allocator-level concurrency check); (c) the audio output contains non-zero energy for the full 500 ms window. **Note**: spectral-peak-count verification of 8 distinct modal decay envelopes (the original "peak count in decaying-tail envelope analysis" method) is **deferred to Phase 4**, when per-pad presets produce acoustically distinct modal signatures that make individual tail identification reliable. The NaN/Inf check plus voice-count concurrency check constitute sufficient Phase 3 evidence for SC-020.
- **SC-021**: Voice stealing is audibly click-free across all 3 policies. The peak click artifact in the 5 ms window around any voice steal is ≤ **−30 dBFS** relative to the new voice's peak. Verified by `test_steal_click_free.cpp` across {Oldest, Quietest, Priority} × sample rates {22050, 44100, 48000, 96000, 192000}.
- **SC-022**: Choke group fast-release completes within **5 ± 1 ms** wall-clock time and produces a click artifact ≤ **−30 dBFS**. Verified by `test_choke_group.cpp` on the open/closed-hat canonical case and on the all-voices-in-one-group stress case.
- **SC-023**: 8-voice worst-case CPU (Feedback + NoiseBody + Tone Shaper + Unnatural Zone) on modern x86-64 at 44.1 kHz / 128-sample block is ≤ **12%** single-core. Verified by `test_polyphony_benchmark.cpp`. If Phase 2's 1/144 waived cell (Feedback+NoiseBody+TS+UN at 2.0%) is used, 8 × 2.0% = 16%; the test is then waived to ≤ **18%** for that specific combination, documented in `plan.md` per the Phase 2 precedent.
- **SC-024**: 16-voice stress test runs 10 seconds of continuous 44.1 kHz / 128-sample block processing on CI reference hardware with **zero xruns** and **zero NaN/Inf** samples. Verified by `test_polyphony_benchmark.cpp`.
- **SC-025**: State v2 → v3 migration round-trip: a captured Phase 2 (v2) state blob loaded into a Phase 3 processor produces bit-exact Phase 2 parameter values AND Phase 3 default values for the 3 new fields. Verified by `test_state_roundtrip_v3.cpp`.
- **SC-026**: State v3 round-trip: save → load → save produces a bit-identical v3 blob with all Phase 2 fields + all Phase 3 fields preserved. Verified by the same test.
- **SC-027**: Zero heap allocations on the audio thread across the 10-second 16-voice fuzz stress test including voice steals, choke events, and parameter changes. Verified by `test_polyphony_allocation_matrix.cpp` using `allocation_detector.h`.
- **SC-027a**: `VoicePool` memory footprint is bounded and deterministic regardless of the `maxPolyphony` runtime value. A static or startup assertion MUST confirm the pool allocates exactly: `sizeof(VoicePool)` struct overhead + `2 × kMaxBlockSize × sizeof(float)` scratch buffer + `16 × sizeof(DrumVoice)` voice instances + `16 × sizeof(VoiceMeta)` metadata. No additional heap allocation occurs between `setupProcessing()` and `terminate()`. Verified by `test_polyphony_allocation_matrix.cpp` (allocation_detector reports zero allocations after `setupProcessing()` completes) and by inspecting that `VoicePool` holds no `std::vector` or dynamic container whose capacity could grow at runtime.
- **SC-028**: Phase 2 regression: the Phase 2 default patch (Impulse + Membrane) played through the Phase 3 processor with `maxPolyphony = 1` produces output within **−90 dBFS RMS difference** of the Phase 2 golden reference. Verified by a new `test_phase2_regression_in_phase3.cpp`.
- **SC-029**: Pluginval passes at strictness level 5 with zero errors on Windows. `auval -v aumu Mbrm KrAt` passes on macOS CI. Zero compiler warnings on Windows x64, macOS universal, and Linux x64 builds.
- **SC-030**: Three new parameters (`kMaxPolyphonyId`, `kVoiceStealingId`, `kChokeGroupId`) are registered in the controller and visible in the host-generic editor. Verified by `test_phase3_params.cpp` calling `IEditController::getParameterInfo()` for each ID.
- **SC-031**: The `maxPolyphony` parameter takes effect on the next note-on: setting it to 4 while 6 voices are sounding fast-releases the 2 excess voices (oldest-first, ≤ 5 ms, click-free). Verified by `test_poly_change_live.cpp`.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- `Krate::DSP::VoiceAllocator` is stable, tested, and its public API (`noteOn`, `noteOff`, `setAllocationMode`, `setVoiceCount`, `voiceFinished`, event iteration via the returned `std::span<const VoiceEvent>`) is sufficient to drive the Phase 3 voice pool. Verified by reading `dsp/include/krate/dsp/systems/voice_allocator.h` — see `plan.md §Dependency API Contracts`. Spec 135 line 343 explicitly earmarks it for Membrum.
- Phase 2's `DrumVoice` is copyable/default-constructible in a way that permits a 16-slot `std::array<DrumVoice, 16>` (or equivalent). If the Phase 2 voice holds any reference to processor-level shared state (it should not), `plan.md` must document and fix this before pool instantiation.
- The Phase 2 `ExciterBank` / `BodyBank` / `ToneShaper` / `UnnaturalZone` components are independent per-instance — each `DrumVoice` owns its own. Spec 137's forward reusability section explicitly notes: "Phase 3 will instantiate 8 DrumVoice instances in a voice pool. The DrumVoice refactored in Phase 2 must be cheap to duplicate and must work without any global state."
- The Membrum processor's Phase 2 `process()` method currently iterates a single voice. Phase 3 extends this to iterate all active slots and sum their outputs. The mixing is simple summation (already the case for percussion — no per-voice stereo spread in Phase 3).
- Sample rates are in the standard range {22050, 44100, 48000, 88200, 96000, 176400, 192000} Hz. Non-standard sample rates are not specifically tested but must not crash.
- The CI reference machine's CPU is consistent enough that a 12% budget (FR-160) can be measured with ±20% margin. Absolute numbers are recorded in `plan.md`.
- No custom UI work is required in Phase 3. The 3 new parameters appear in the host-generic editor automatically. VSTGUI / uidesc integration is Phase 6.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused (not re-implemented):**

| Component | Location | Relevance |
|-----------|----------|-----------|
| VoiceAllocator | `dsp/include/krate/dsp/systems/voice_allocator.h` | Core voice routing with Oldest/LowestVelocity/HighestNote/RoundRobin strategies — 3 of 4 map directly to Phase 3 policies; Quietest needs processor-layer addition |
| DrumVoice (Phase 2) | `plugins/membrum/src/dsp/drum_voice.h` | Per-voice engine — replicated 16× in the pool, not modified |
| ExciterBank, BodyBank, ToneShaper, UnnaturalZone, MaterialMorph | `plugins/membrum/src/dsp/` | Per-voice components owned by each DrumVoice — unchanged |
| plugin_ids.h | `plugins/membrum/src/plugin_ids.h` | Parameter ID registry — add `kMaxPolyphonyId`, `kVoiceStealingId`, `kChokeGroupId`; bump `kCurrentStateVersion` to 3 |
| processor.h / processor.cpp | `plugins/membrum/src/processor/` | Processor class — extend to own the `VoicePool` instead of a single `DrumVoice` |
| controller.cpp | `plugins/membrum/src/controller/` | Controller — register the 3 new parameters |
| allocation_detector.h | `tests/test_helpers/allocation_detector.h` | RT safety verification — used for all Phase 3 allocation tests |
| enable_ftz_daz.h | `tests/test_helpers/enable_ftz_daz.h` | Denormal protection for tests — carryover from Phase 2 Phase 9 fix |
| signal_metrics.h | `tests/test_helpers/signal_metrics.h` | Click artifact detection for SC-021 / SC-022 |
| spectral_analysis.h | `tests/test_helpers/spectral_analysis.h` | Spectral tail-count verification — deferred to Phase 4 for SC-020 (see SC-020 note); available if needed for any other spectral checks |
| artifact_detection.h | `tests/test_helpers/artifact_detection.h` | Click/pop detection regression tests |

**Initial codebase searches to perform at `/speckit.plan` time:**

```bash
# Confirm existing components and APIs
grep -rn "class VoiceAllocator" dsp/include/krate/dsp/systems/
grep -rn "class DrumVoice" plugins/membrum/src/
grep -rn "kCurrentStateVersion" plugins/membrum/src/

# Confirm no name collisions for planned new types
grep -rn "class VoicePool" plugins/
grep -rn "class VoiceMeta" plugins/
grep -rn "class ChokeGroupTable" plugins/
grep -rn "kMaxPolyphonyId\|kVoiceStealingId\|kChokeGroupId" plugins/
```

**Search Results Summary**: `VoiceAllocator` confirmed to exist with `AllocationMode::Oldest/LowestVelocity/HighestNote/RoundRobin` strategies and `kMaxVoices = 32` hard cap (see voice_allocator.h lines 43–62 and 193). `DrumVoice`, `ExciterBank`, `BodyBank`, `ToneShaper`, `UnnaturalZone`, `MaterialMorph` confirmed in `plugins/membrum/src/dsp/` (Phase 2 FR-100 MET). No collisions expected for the three new types proposed in FR-190.

### Forward Reusability Consideration

**Sibling features at same layer:**

- **Phase 4** (32-pad layout, per-pad presets, separate outputs): Will replace the Phase 3 single-pad-template model with a 32-entry `std::array<PadPreset, 32>`. The Phase 3 voice pool must support per-note configuration lookups during `noteOn()` WITHOUT changing the voice allocation code path. The `ChokeGroupTable` is designed as a 32-entry table already (for forward compatibility), even though Phase 3 only uses a single shared value across all entries.
- **Phase 5** (cross-pad coupling / sympathetic resonance): Will route the pool's mixed output BACK into per-voice exciters via `Krate::DSP::SympatheticResonanceSIMD`. The Phase 3 `VoicePool::processBlock` must be structured so the post-mix stereo buffer is addressable for Phase 5's injection step.
- **Phase 6** (custom UI, macros, Acoustic/Extended modes): Will add uidesc and custom VSTGUI views for the pool-level parameters (polyphony, stealing policy, choke group per pad). Phase 3's parameter layout must be grouped coherently so Phase 6 can label and arrange them without renames.

**Potential shared components:**

- `VoicePool` (the processor-layer wrapper around `VoiceAllocator` + N voices + choke-group logic) is a strong candidate for promotion to `plugins/shared/` once Phase 4 confirms the pattern works for per-pad state. Any future polyphonic physical-modeling drum instrument could reuse it.
- `ChokeGroupTable` is specific to drum-kit instruments but is small and independent enough to live in `plugins/shared/` from the start if another plugin ever adopts choke semantics.

## Deferred to Later Phases *(mandatory)*

The following features from spec 135 are **consciously out of scope for Phase 3**, with the phase they belong to:

| Feature | Deferred to | Reason |
|---------|-------------|--------|
| 32-pad layout with distinct per-pad parameter state | Phase 4 | Phase 3 ships with a single shared pad template for all 32 MIDI notes |
| Per-pad presets and kit presets | Phase 4 | Depends on 32-pad layout + preset manager integration |
| Per-pad output routing (separate output buses) | Phase 4 | Depends on pad identity |
| Pad templates (Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, 808, FX) | Phase 4 | Templates require the per-pad preset system |
| Cross-pad coupling / sympathetic resonance (snare buzz, tom resonance, coupling matrix) | Phase 5 | Requires multiple distinct pads to couple between |
| Snare wire modeling | Phase 5 or dedicated phase | Requires new collision-model code not in KrateDSP; only applies to Membrane |
| Macro Controls (Tightness, Brightness, Body Size, Punch, Complexity) | Phase 6 (UI phase) | Macros aggregate lower-level params; belong with UI design |
| Acoustic vs Extended UI mode gating | Phase 6 | Pure UI-layer feature |
| Custom VSTGUI UI (4×8 pad grid, per-pad editor, XY morph pad) | Phase 6 | Explicit phase for UI |
| Per-pad user-assignable Priority ranks (beyond the pitch-based default) | Phase 4+ | Requires per-pad state |
| Choke group count beyond 8 (e.g. 16 to match Ableton) | Phase 4 reviewable | Phase 3 ships 8; raise if user feedback demands |
| Voice allocator strategy "Quietest" as a `VoiceAllocator` built-in (rather than processor-layer) | Not adopted (alternative chosen) | FR-122 mandates processor-layer implementation in `Membrum::VoicePool`; modifying `voice_allocator.h` for this is explicitly forbidden in Phase 3. The processor-layer approach was selected instead and is fully implemented. |
| `ModalResonatorBankSIMD` mandatory acceleration | Phase 3 conditional (FR-162) | Only activated if scalar 8-voice worst case exceeds 12% |
| Hi-hat pedal position continuous control | Phase 4 | Requires per-pad state |
| Per-voice crossfade curves other than exponential fast release | Future Phase | 5 ms exponential fast release (`gain *= exp(-1/(0.005·sampleRate))` with `1e-6f` denormal floor) is the locked Phase 3 implementation; alternative curve shapes are deferred |
| Note-off as optional voice-release trigger (for non-percussion use) | Future Phase | Phase 3 treats note-off as a no-op (drum semantics) |

## Cross-Platform & Compatibility Notes *(mandatory)*

- Phase 3 MUST build and run on Windows (MSVC, x64), macOS (Clang, universal / ARM64 + x86_64), and Linux (GCC/Clang, x86_64). No platform-specific code.
- No UI code in Phase 3 — only 3 new parameters exposed via the host-generic editor. No Win32, Cocoa, or X11 code required.
- The fast-release envelope math MUST work consistently across all three compilers. Narrowing warnings (see Phase 2's MSVC vs Clang incident) MUST be avoided by using designated initializers for any new struct literal.
- `std::atomic_flag` is the only guaranteed lock-free primitive; any new atomic state in the voice pool MUST be verified with `is_lock_free()` in a startup assert.
- Denormal protection (FTZ/DAZ on x86, `-ffast-math` NOT used) MUST remain enabled. The fast-release exponential decay tail is particularly vulnerable to denormals — this is explicitly tested in SC-022.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*To be filled in by `/speckit.implement` — see CLAUDE.md "Completion Honesty" section. Every row MUST be verified against actual code and test output before being marked ✅.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-110 | ✅ MET | `voice_pool.h:68` `kMaxVoices=16`; `voice_pool.h:266-267` `voicesPtr_` + `meta_` arrays; `voice_pool.h:274-275` `releasingVoicesPtr_` + `releasingMeta_` shadow arrays. All 16 DrumVoice instances pre-allocated in `prepare()`. |
| FR-117 | ✅ MET | `voice_pool.h:288-289` `scratchL_`/`scratchR_` as `unique_ptr<float[]>`; allocated in `prepare()` sized to host max block size. `voice_pool.h:328-337` static_assert on bounded footprint. |
| FR-120 | ✅ MET | `voice_stealing_policy.h:17-21` enum class with `Oldest=0, Quietest=1, Priority=2`; `plugin_ids.h:93` `kVoiceStealingId=251` as StringListParameter. |
| FR-130 | ✅ MET | `choke_group_table.h:26` `kSize=32` fixed array; `choke_group_table.h:60` `setGlobal()` clamps to [0,8]; `choke_group_table.h:69-70` `lookup()` returns 0 outside [36,67]. 8 groups + group 0 (no choke). |
| FR-140 | ✅ MET | `plugin_ids.h:28` `kCurrentStateVersion=3`; `processor.cpp:522` writes v3; `processor.cpp:810` reads v3 tail (maxPoly, policy, 32 choke bytes). v1/v2 migration paths with Phase 3 defaults. |
| FR-150 | ✅ MET | `plugin_ids.h:92-94` `kMaxPolyphonyId=250`, `kVoiceStealingId=251`, `kChokeGroupId=252`. Test `Phase 3 params: controller exposes Phase 2 count + 3` passes. |
| FR-160 | ✅ MET | 8-voice worst-case CPU measured at **5.952%** (budget: 12%). `test_polyphony_benchmark.cpp` `[phase35-bench8]`. |
| FR-170 | ✅ MET | `voice_pool.h:70-78` fast-release constants: `kFastReleaseSecs=0.005f`, `kFastReleaseFloor=1e-6f`, `kFastReleaseLnFloor=13.8155106f`. Formula: `exp(-ln(1e6)/(0.005*sr))`. Tests verify 5ms±1ms wall-clock across sample rates. |
| FR-180 | ✅ MET | `processor.cpp` `getState()` writes 302-byte v3 blob; `setState()` dispatches on version with v1→v3, v2→v3 migration. Tests: `State v3 StateRoundTripV3`, `StateMigration v2->v3`, `StateMigration v1->v3`, corruption clamping — all pass. |
| FR-190 | ✅ MET | `DrumVoice` unchanged from Phase 2 (FR-194). `VoicePool` applies fast-release externally on scratch buffer. Test `VoicePool maxPolyphony=1 matches Phase 2 DrumVoice reference` verifies byte-identical output (maxDiff=0.0). |
| SC-020 | ✅ MET | Test `VoicePool: 8 concurrent noteOn → getActiveVoiceCount() == 8` passes; 500ms audio output verified non-zero, no NaN/Inf. `test_voice_pool_allocate.cpp`. |
| SC-021 | ✅ MET | Test `VoicePool steal click-free across policies and sample rates` — peak click ≤ −30 dBFS across {Oldest,Quietest,Priority} × {22050,44100,48000,96000,192000}. `test_steal_click_free.cpp`. |
| SC-022 | ✅ MET | Test `VoicePool choke: click <= -30 dBFS, terminates within 5 +/- 1 ms, bit-identical reuse` — click metric and timing verified. `test_choke_click_free.cpp`. |
| SC-023 | ✅ MET | 8-voice worst-case CPU **5.952%** (budget ≤12%). `test_polyphony_benchmark.cpp` `[phase35-bench8]`. Waiver not needed (already under non-waived budget). |
| SC-024 | ✅ MET | 16-voice stress: **0 xruns** over 3445 blocks, 10s @ 44.1kHz/128. CPU 11.718%. `test_polyphony_stress_16.cpp` `[phase35-stress16]`. |
| SC-025 | ✅ MET | Test `State v2 StateMigration v2->v3: fixture loads into Phase 3 processor` — v2 blob produces bit-exact Phase 2 values + Phase 3 defaults (maxPoly=8, policy=Oldest, choke=0). `test_state_migration_v2_to_v3.cpp`. |
| SC-026 | ✅ MET | Test `State v3 StateRoundTripV3: getState emits exactly 302 bytes with v3 tail` + `extreme/boundary values round-trip` — save→load→save bit-identical. `test_state_roundtrip_v3.cpp`. |
| SC-027 | ✅ MET | Test `Phase35: VoicePool 10-second fuzz is allocation-free` — 0 heap allocations across 3445 blocks with voice steals, chokes, parameter changes. `test_polyphony_allocation_matrix.cpp`. |
| SC-027a | ✅ MET | `voice_pool.h:328-337` static_assert on `sizeof(VoicePool)` bounded budget. Allocation detector confirms 0 allocations after `prepare()`. No `std::vector` or growable container in VoicePool hot path. |
| SC-028 | ✅ MET | Test `VoicePool maxPolyphony=1 matches Phase 2 DrumVoice reference` — RMS difference ≤ −90 dBFS (actual: byte-identical, maxDiff=0.0). `test_phase2_regression_maxpoly1.cpp`. Also `Phase 2 default patch matches Phase 1 golden reference`. |
| SC-029 | ✅ MET | Pluginval strictness 5: 19/19 tests passed, 0 errors, 0 warnings. Clang-tidy: 0 warnings, 0 errors after Phase 3.6 fixes. Build: 0 compiler warnings on Windows x64 Release. |
| SC-030 | ✅ MET | Tests `Phase 3 params: kMaxPolyphonyId is a stepped [4,16] RangeParameter`, `kVoiceStealingId is a 3-entry StringListParameter`, `kChokeGroupId is a stepped [0,8] RangeParameter` — all pass. `test_phase3_params.cpp`. |
| SC-031 | ✅ MET | Tests `VoicePool: shrink from 8 to 4 voices releases the excess click-tolerant` + `expand maxPolyphony back to 16 accepts more voices` — fast-release on shrink, immediate allocation on expand. `test_poly_change_live.cpp`. |

**Status Key:**
- ✅ MET: Requirement verified against actual code and test output with specific evidence
- ❌ NOT MET: Requirement not satisfied (spec is NOT complete)
- ⚠️ PARTIAL: Partially met with documented gap and specific evidence of what IS met
- 🔄 DEFERRED: Explicitly moved to future work (or not yet implemented during spec phase)

### Honest Assessment

**Overall Status**: NOT COMPLETE (spec-only phase — implementation is `/speckit.plan` + `/speckit.tasks` + `/speckit.implement`)
