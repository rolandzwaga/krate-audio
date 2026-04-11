---
description: "Task list for Membrum Phase 3 — Multi-Voice Polyphony, Voice Stealing, Choke Groups"
---

# Tasks: Membrum Phase 3 — Multi-Voice Polyphony, Voice Stealing, Choke Groups

**Feature**: Membrum Phase 3 — Multi-Voice Polyphony, Voice Stealing, Choke Groups
**Branch**: `138-membrum-phase3-polyphony`
**Spec**: `specs/138-membrum-phase3-polyphony/spec.md`
**Plan**: `specs/138-membrum-phase3-polyphony/plan.md`
**Date**: 2026-04-11
**Total tasks**: 70
**Pre-authorized commits**: Per the Memory rule "speckit tasks authorize commits", each phase-end commit task listed below is pre-authorized by the presence of this tasks.md. No re-asking is needed between phases.

## Summary

| Phase | Tasks | Complexity | Description |
|-------|-------|------------|-------------|
| 3.0 | 10 | Moderate | Scaffolding — new types, param IDs, CMake, sizeof decision |
| 3.1 | 18 | Complex | Voice pool + allocator integration, all 3 stealing policies |
| 3.2 | 9 | Complex | Fast-release ramp — exponential decay, denormal floor, click-free |
| 3.3 | 8 | Moderate | Choke groups — ChokeGroupTable wired, kChokeGroupId parameter |
| 3.4 | 11 | Moderate | State v3 migration — v1/v2/v3 round-trips, corruption clamping |
| 3.5 | 7 | Complex | CPU budget, 16-voice stress, full allocation matrix |
| 3.6 | 7 | Trivial | Quality gates, pluginval, clang-tidy, compliance, v0.3.0 release |

**Total: 70 tasks**

---

## MANDATORY: Build-Before-Test Workflow (Constitution Principle XIII)

Every implementation task MUST follow this cycle:

1. Write failing tests (must FAIL before implementation starts).
2. Implement to make tests pass.
3. Build: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests` — fix all C4xxx / Clang warnings before continuing.
4. Verify: `build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5` — last line must be `All tests passed`.
5. Run pluginval after any plugin source change: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" 2>&1 | tee membrum-phaseXX-pluginval.log` — capture to a log file on the FIRST run, inspect the log. Do NOT re-run to grep.
6. Run clang-tidy: `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja 2>&1 | tee membrum-phase3-clang-tidy.log` — zero warnings required.
7. Commit.

### NaN Detection Pattern (Required — -ffast-math safe)

```cpp
auto isFiniteSample = [](float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
};
```

Add any test file using `std::isnan`/`std::isfinite` to the `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt`.

---

## Phase 3.0 — Scaffolding

**Goal**: `VoicePool`, `VoiceMeta`, `ChokeGroupTable`, `VoiceSlotState`, `VoiceStealingPolicy` declared with stub bodies; new parameter IDs added; `kCurrentStateVersion` bumped to 3; CMake integrated; failing test stubs in place; `sizeof(DrumVoice)` decision made. Phase 2 tests still green.

**Satisfies**: FR-110 (skeleton), FR-112 (skeleton), FR-120 (enum), FR-130 (skeleton), FR-140, FR-150 (IDs), FR-151, FR-190, FR-191, FR-193, FR-194 (invariants declared)

- [X] T3.0.1 Create directory `plugins/membrum/src/voice_pool/` and add `.gitkeep`. Create directory `plugins/membrum/tests/unit/voice_pool/` and add `.gitkeep`. Create directory `plugins/membrum/tests/perf/` and add `.gitkeep` (this directory is needed by the Phase 3.5 benchmark stubs added in T3.0.7). Acceptance evidence: all three directories exist and appear in `git status`. Satisfies plan.md §Project Structure.

- [X] T3.0.2 Create `plugins/membrum/src/voice_pool/voice_meta.h`: define `Membrum::VoiceSlotState` enum class (`Free=0, Active=1, FastReleasing=2` with `uint8_t` underlying type) and `Membrum::VoiceMeta` struct exactly per `data-model.md §1–§2`. Fields in declared order: `currentLevel (float=0.0f)`, `fastReleaseGain (float=1.0f)`, `noteOnSampleCount (uint64_t=0)`, `originatingNote (uint8_t=0)`, `originatingChoke (uint8_t=0)`, `state (VoiceSlotState=Free)`, `_pad (uint8_t=0)`. `alignas(64)`. Include `static_assert(sizeof(VoiceMeta) <= 64, "VoiceMeta must fit one cache line")`. Acceptance evidence: file compiles; static_assert passes; fields match data-model.md §2 exactly. Dependency: T3.0.1. Satisfies FR-172.

- [X] T3.0.3 [P] Create `plugins/membrum/src/voice_pool/voice_stealing_policy.h`: define `Membrum::VoiceStealingPolicy` enum class with `uint8_t` underlying type: `Oldest=0, Quietest=1, Priority=2` exactly per `data-model.md §1`. Acceptance evidence: file compiles; values match FR-121/FR-122/FR-123. Dependency: T3.0.1. Satisfies FR-120.

- [X] T3.0.4 [P] Create `plugins/membrum/src/voice_pool/choke_group_table.h`: declare `Membrum::ChokeGroupTable` class exactly per `data-model.md §3` with `static constexpr int kSize = 32`. Declare stub methods (empty bodies): `setGlobal(uint8_t group) noexcept`, `lookup(uint8_t midiNote) const noexcept -> uint8_t`, `raw() const noexcept -> const std::array<uint8_t, kSize>&`, `loadFromRaw(const std::array<uint8_t, kSize>& in) noexcept`. Private member: `std::array<uint8_t, kSize> entries_{}`. Acceptance evidence: file compiles; class is in `Membrum` namespace; no dynamic container used. Dependency: T3.0.1. Satisfies FR-130, FR-131.

- [X] T3.0.5 Create `plugins/membrum/src/voice_pool/voice_pool.h`: declare `Membrum::VoicePool` class with the full public interface from `contracts/voicepool_api.md`. Include `DrumVoice`, `VoiceMeta`, `ChokeGroupTable`, `VoiceStealingPolicy`. Declare all public methods as stubs (`noexcept`, empty or minimal return bodies). Declare all private members: `voices_` and `releasingVoices_` (`std::array<DrumVoice, kMaxVoices>`), `meta_` and `releasingMeta_` (`std::array<VoiceMeta, kMaxVoices>`), `allocator_` (`Krate::DSP::VoiceAllocator`), `chokeGroups_` (`ChokeGroupTable`), `scratchL_` and `scratchR_` (`std::unique_ptr<float[]>`), `maxBlockSize_` (int), `sampleRate_` (double), `fastReleaseK_` (float), `maxPolyphony_` (int), `stealingPolicy_` (VoiceStealingPolicy), `sampleCounter_` (uint64_t). Define constants: `kMaxVoices=16`, `kVoicePoolMaxBlock=2048`, `kFastReleaseSecs=0.005f`, `kFastReleaseFloor=1e-6f`. Create empty `plugins/membrum/src/voice_pool/voice_pool.cpp` with the `#include "voice_pool.h"` line. Acceptance evidence: both files compile; class is in `Membrum` namespace; no ODR collision (grep for `class VoicePool` confirms no prior definition). Dependency: T3.0.2, T3.0.3, T3.0.4. Satisfies FR-110, FR-112, FR-116, FR-117.

- [X] T3.0.6 Extend `plugins/membrum/src/plugin_ids.h`: add `kMaxPolyphonyId = 250`, `kVoiceStealingId = 251`, `kChokeGroupId = 252` to the parameter enum; bump `kCurrentStateVersion` from 2 to 3; add `static_assert(kMorphCurveId < kMaxPolyphonyId, "Phase 2 and Phase 3 parameter ID ranges must not overlap")` and `static_assert(kCurrentStateVersion == 3, "Phase 3 requires state version 3")` exactly per `data-model.md §6`. Acceptance evidence: file compiles; both static_asserts pass; grep confirms no other parameter uses IDs 250–252. Satisfies FR-140, FR-150, FR-151.

- [X] T3.0.7 Update `plugins/membrum/CMakeLists.txt`: add `src/voice_pool/voice_pool.cpp`, `src/voice_pool/voice_pool.h`, `src/voice_pool/voice_meta.h`, `src/voice_pool/voice_stealing_policy.h`, `src/voice_pool/choke_group_table.h` to the Membrum source list. Update `plugins/membrum/tests/CMakeLists.txt`: add the 13 new test files under `tests/unit/voice_pool/` (`test_voice_pool_scaffold.cpp`, `test_voice_pool_allocate.cpp`, `test_voice_pool_stealing_policies.cpp`, `test_voice_pool_allocation_free.cpp`, `test_steal_click_free.cpp`, `test_fast_release_denormals.cpp`, `test_fast_release_double_steal.cpp`, `test_choke_group.cpp`, `test_choke_click_free.cpp`, `test_choke_allocation_free.cpp`, `test_polyphony_allocation_matrix.cpp`, `test_phase2_regression_maxpoly1.cpp`, `test_poly_change_live.cpp`), add `tests/unit/vst/test_phase3_params.cpp`, `tests/unit/vst/test_state_roundtrip_v3.cpp`, `tests/unit/vst/test_state_migration_v2_to_v3.cpp`, `tests/unit/vst/test_state_migration_v1_to_v3.cpp`, `tests/unit/vst/test_state_corruption_clamp.cpp`, and the 2 perf files `tests/perf/test_polyphony_benchmark.cpp`, `tests/perf/test_polyphony_stress_16.cpp` — all as empty stub source files that compile to zero tests. Note: the `tests/perf/` directory was created in T3.0.1; verify it exists before creating the stub files. Command to verify: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release && "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests`. Acceptance evidence: CMake configures without error; `membrum_tests` compiles to zero errors and zero warnings; `ls plugins/membrum/tests/perf/` shows the two stub `.cpp` files. Dependency: T3.0.5, T3.0.1 (perf/ directory). Satisfies FR-193 (dsp/ unchanged).

- [X] T3.0.8 Measure `sizeof(DrumVoice)` and commit the two-array decision. Create `plugins/membrum/tests/unit/voice_pool/test_voice_pool_scaffold.cpp` with a `TEST_CASE("DrumVoice sizeof decision", "[scaffolding]")` that captures `CAPTURE(sizeof(DrumVoice))` and asserts it is non-zero. Decision criteria (document as a comment at the top of `voice_pool.h`): if `32 * sizeof(DrumVoice) > 1 MB` (i.e., `sizeof(DrumVoice) > 32768`), use per-slot single-fade reservation (one `releasingVoice` slot per main slot, one fade-out in-flight maximum per slot); otherwise, keep the two-array approach with `voices_[16]` + `releasingVoices_[16]`. Build and run: `membrum_tests.exe "[scaffolding]" 2>&1 | tail -5`. Record the measured size in `plan.md §Complexity Tracking`. Acceptance evidence: test runs and prints `sizeof(DrumVoice)`; decision comment present in `voice_pool.h`. Satisfies plan.md Open Question #1 / Complexity Tracking `sizeof(DrumVoice)` row.

- [X] T3.0.9 Verify Phase 2 regression: build and run full `membrum_tests` suite and confirm ALL existing Phase 2 tests still pass. Command: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5`. Acceptance evidence: last line is `All tests passed (N assertions in M test cases)` with no failures. Dependency: T3.0.7. Satisfies FR-187 (regression gate entry).

- [X] T3.0.10 Commit: `"membrum: Phase 3.0 scaffolding — VoicePool skeleton, VoiceMeta, ChokeGroupTable, param IDs 250-252, state version 3"`. Dependency: T3.0.9 green.

---

## Phase 3.1 — Voice Pool & Allocator Integration

**Goal**: Replace `Processor::voice_` with `Processor::voicePool_`. Note-on routes through `VoicePool::noteOn → VoiceAllocator::noteOn → DrumVoice::noteOn`. All 3 stealing policies work (Oldest via allocator, Priority via `HighestNote`, Quietest via processor-layer pre-free). No fast-release ramp yet; clicks on steal are acceptable at this phase.

**Satisfies**: FR-110–116, FR-120–123, FR-125 (sampleRate wiring), FR-128, FR-152, FR-153, FR-163 (subset), FR-165, FR-170–172, FR-180, FR-187, SC-020 (subset), SC-027 (subset), SC-028, SC-030, SC-031

**Dependencies**: Phase 3.0 complete and green.

### 3.1.1 Tests (Write FIRST — Must FAIL)

- [X] T3.1.1 Write failing test `plugins/membrum/tests/unit/voice_pool/test_voice_pool_allocate.cpp`. Test cases: (a) `prepare()` then 8 concurrent `noteOn()` calls → `getActiveVoiceCount() == 8`; (b) `noteOff()` does NOT decrement the voice count immediately (percussion no-op per FR-114); (c) after 5 seconds of `processBlock()` the voice naturally becomes inactive (FR-115); (d) SC-020 subset: trigger 8 notes, process 500 ms, assert no NaN/Inf in output (bit-manipulation check); (e) `maxPolyphony=1` + MIDI note 36 produces non-zero, non-NaN output. Verify tests FAIL. Satisfies FR-113, FR-114, FR-115, SC-020.

- [X] T3.1.2 [P] Write failing test `plugins/membrum/tests/unit/voice_pool/test_voice_pool_stealing_policies.cpp`. Parameterized over {4, 8, 16} max polyphony × {Oldest, Quietest, Priority} (9 combinations). For each: fill pool to capacity with a deterministic note sequence (notes assigned known `noteOnSampleCount` and `currentLevel` values via `processBlock` timing), send one more note, assert: (a) Oldest steals the voice with earliest `noteOnSampleCount`; (b) Quietest steals the voice with lowest `meta_[slot].currentLevel` (read from the pool's metadata after running one block); (c) Priority steals the voice with highest `originatingNote` pitch; (d) tiebreaker at all-silent pool falls back to oldest slot-index ascending (FR-128). Verify tests FAIL. Satisfies FR-180.

- [X] T3.1.3 [P] Write failing test `plugins/membrum/tests/unit/voice_pool/test_voice_pool_allocation_free.cpp`. Using `tests/test_helpers/allocation_detector.h`: wrap `noteOn`, `noteOff`, `processBlock`, `setMaxPolyphony`, `setVoiceStealingPolicy`, `setChokeGroup` — 1 second at 44100 Hz / 128-sample blocks, 16 concurrent voices, random note-ons every 5 ms — assert zero heap allocations on every call. Verify tests FAIL. Satisfies FR-116, FR-163.

- [X] T3.1.4 [P] Write failing test `plugins/membrum/tests/unit/voice_pool/test_phase2_regression_maxpoly1.cpp`. Configure `VoicePool` with `maxPolyphony=1`. Trigger MIDI note 36, velocity 0.7. Process 500 ms at 44100 Hz. Compare output against Phase 2 golden reference (or reuse `plugins/membrum/tests/golden/phase2_state_v2.bin`-derived single-voice render). Assert RMS difference ≤ −90 dBFS. Assert no NaN/Inf. Verify tests FAIL. Satisfies FR-187, SC-028.

- [X] T3.1.5 [P] Write failing test `plugins/membrum/tests/unit/vst/test_phase3_params.cpp`. Assert: (a) `getParameterCount()` returns Phase 2 count + 3; (b) `getParameterInfo(kMaxPolyphonyId)` → stepped RangeParameter, min=4, max=16, default=8, unit="voices"; (c) `getParameterInfo(kVoiceStealingId)` → StringListParameter, 3 choices "Oldest"/"Quietest"/"Priority", default=0; (d) `getParameterInfo(kChokeGroupId)` → stepped RangeParameter, min=0, max=8, default=0, unit="group"; (e) `setParamNormalized(kMaxPolyphonyId, 0.0)` round-trips to 4; `setParamNormalized(kMaxPolyphonyId, 1.0)` round-trips to 16. Verify tests FAIL. Satisfies FR-150, FR-151, SC-030.

- [X] T3.1.6 [P] Write failing test `plugins/membrum/tests/unit/voice_pool/test_poly_change_live.cpp`. With 6 voices active at `maxPolyphony=8`: call `setMaxPolyphony(4)`. Assert: (a) two voices enter `FastReleasing` state in `releasingMeta_`; (b) `getActiveVoiceCount() <= 4` after the next `processBlock()` call; (c) no NaN/Inf during the shrink. Then call `setMaxPolyphony(16)`: assert no crash, no allocation, next `noteOn` succeeds up to 16 voices. Verify tests FAIL. Satisfies FR-111, SC-031.

### 3.1.2 Implementation

- [X] T3.1.7 Implement `ChokeGroupTable` method bodies in `plugins/membrum/src/voice_pool/choke_group_table.h`: `setGlobal` writes the same value to all 32 entries in a simple for-loop; `lookup` returns 0 for MIDI notes outside [36, 67], otherwise returns `entries_[note - 36]`; `loadFromRaw` clamps each byte: `entries_[i] = in[i] > 8 ? 0 : in[i]` (FR-144). Acceptance evidence: T3.4.2 choke round-trip will pass; `lookup(35)` returns 0; `lookup(67)` returns `entries_[31]`. Satisfies FR-130, FR-131, FR-136, FR-138, FR-144.

- [X] T3.1.8 Implement `VoicePool::prepare(double sampleRate, int maxBlockSize)` in `plugins/membrum/src/voice_pool/voice_pool.cpp`. Allocate `scratchL_` and `scratchR_` via `std::make_unique<float[]>(maxBlockSize)` (ONLY allocation point per FR-116/Q4); call `voices_[i].prepare(sampleRate, i)` and `releasingVoices_[i].prepare(sampleRate, i + kMaxVoices)` for all 16 slots; compute `fastReleaseK_ = std::exp(-1.0f / (kFastReleaseSecs * static_cast<float>(sampleRate)))` (FR-124, FR-125); call `allocator_.reset()` then `allocator_.setVoiceCount(maxPolyphony_)`; reset all `meta_` and `releasingMeta_` entries to `{state = VoiceSlotState::Free}`. `noexcept`. Acceptance evidence: `allocation_detector` reports zero allocations after `prepare()` returns; `fastReleaseK_` is in (0, 1) for all 5 supported sample rates. Satisfies FR-116, FR-117, Q4.

- [X] T3.1.9 Implement `VoicePool::noteOn(uint8_t midiNote, float velocity)` in `voice_pool.cpp`, following the plan.md "Note-on flow" prose diagram exactly: (1) snapshot `sharedParams_`; (2) call `processChokeGroups(midiNote)` (stub returns immediately — Phase 3.3 fills it); (3) if `stealingPolicy_ == Quietest` and pool is full: call `selectQuietestActiveSlot()`, then `beginFastRelease(q)`, then `allocator_.noteOff(meta_[q].originatingNote)`, then `allocator_.voiceFinished(q)` (Clarification Q3); else: call `allocator_.setAllocationMode(Oldest or HighestNote)`; (4) call `allocator_.noteOn(midiNote, velocityByte)` where `velocityByte = static_cast<uint8_t>(velocity * 127.f + 0.5f)`; (5) iterate the returned `VoiceEvent` span: on `Steal` → `beginFastRelease(ev.voiceIndex)`, on `NoteOn` → configure and start the new voice: call `voices_[slot].setMaterial/setSize/.../setLevel(sharedParams_...)`, call `voices_[slot].noteOn(velocity)`, set `meta_[slot]` fields (`state=Active`, `originatingNote=midiNote`, `originatingChoke` from choke table lookup, `noteOnSampleCount=sampleCounter_`, `currentLevel=0.0f`). `noexcept`. Acceptance evidence: T3.1.1(a–e) pass. Satisfies FR-113, FR-121, FR-122, FR-123, FR-170, FR-171.

- [X] T3.1.10 Implement `VoicePool::noteOff(uint8_t midiNote)` in `voice_pool.cpp`: call `allocator_.noteOff(midiNote)` for bookkeeping ONLY. Do NOT call `voices_[i].noteOff()` or gate any amp envelope. `noexcept`. Acceptance evidence: T3.1.1(b) passes — voice count unchanged after `noteOff`. Satisfies FR-114.

- [X] T3.1.11 Implement `VoicePool::processBlock(float* outL, float* outR, int numSamples)` in `voice_pool.cpp`, following plan.md "Block-processing flow": zero `outL[0..numSamples)` and `outR[0..numSamples)`; for each `slot ∈ [0, maxPolyphony_)`: if `voices_[slot].isActive()` → call `voices_[slot].processBlock(scratchL_.get(), numSamples)`, compute block peak → `meta_[slot].currentLevel` (FR-165, one pass, not per-sample), accumulate `scratchL_[i]` into `outL[i]` and `outR[i]`; else if `meta_[slot].state == Active` → set `state = Free`, call `allocator_.voiceFinished(slot)` (FR-115); for each `slot ∈ [0, kMaxVoices)` with `releasingMeta_[slot].state == FastReleasing` → call `releasingVoices_[slot].processBlock(scratchL_.get(), numSamples)` then apply stub constant gain `releasingMeta_[slot].fastReleaseGain` as a scalar multiply to compile (Phase 3.2 replaces with the exponential decay), accumulate. Increment `sampleCounter_ += numSamples`. `noexcept`. Acceptance evidence: T3.1.1(a, d, e) pass; no NaN/Inf in 500 ms output. Satisfies FR-115, FR-165, Q4, Q5.

- [X] T3.1.12 Implement `VoicePool::setMaxPolyphony(int n)` in `voice_pool.cpp`: clamp `n` to [4, 16]; call `allocator_.setVoiceCount(n)`; iterate the returned `NoteOff` events: for each, call `beginFastRelease(slot)` (stub in Phase 3.1 — Phase 3.2 makes it click-free); store `maxPolyphony_ = n`. `noexcept`. Acceptance evidence: T3.1.6 passes. Satisfies FR-111, SC-031.

- [X] T3.1.13 Implement `VoicePool::setVoiceStealingPolicy(VoiceStealingPolicy p)` in `voice_pool.cpp`: store `stealingPolicy_ = p`. `noexcept`. Acceptance evidence: T3.1.2 policy selection subtests pass. Satisfies FR-120.

- [X] T3.1.14 Implement `VoicePool::selectQuietestActiveSlot()` in `voice_pool.cpp`: iterate `[0, maxPolyphony_)`, find slot with `meta_[i].state == Active` and minimum `meta_[i].currentLevel`; tiebreak by lowest `noteOnSampleCount` ascending (oldest — FR-128); return -1 if no active slot. Acceptance evidence: T3.1.2 Quietest subtests pass; all-silent tiebreaker falls back to oldest (FR-128 edge case). Satisfies FR-122, FR-128, Q3.

- [X] T3.1.15 Wire `Membrum::Processor` to use `VoicePool` in `plugins/membrum/src/processor/processor.h` and `processor.cpp`: in `processor.h` remove `DrumVoice voice_` member, add `VoicePool voicePool_` member and 3 new atomics (`std::atomic<int> maxPolyphony_{8}`, `std::atomic<int> voiceStealingPolicy_{0}`, `std::atomic<int> chokeGroup_{0}`); in `processor.cpp`: `setupProcessing()` calls `voicePool_.prepare(sampleRate_, setup.maxSamplesPerBlock)`; `process()` calls `voicePool_.processBlock(outL, outR, data.numSamples)`; `processEvents()` drops the `pitch != 36` filter, accepts MIDI notes 36–67, routes velocity-0 to `voicePool_.noteOff(pitch)` and velocity > 0 to `voicePool_.noteOn(pitch, velocity)`; `processParameterChanges()` handles the 3 new IDs (`kMaxPolyphonyId` → denormalize to int [4,16] → `voicePool_.setMaxPolyphony(n)`; `kVoiceStealingId` → denormalize to {0,1,2} → `voicePool_.setVoiceStealingPolicy(...)`; `kChokeGroupId` → denormalize to [0,8] → `voicePool_.setChokeGroup(n)`) and forwards all Phase 2 param changes to `voicePool_.setSharedVoiceParams/setSharedExciterParams`. Acceptance evidence: plugin compiles; T3.1.1(a, e) pass through the full Processor path. Satisfies FR-113, FR-152.

- [X] T3.1.16 Register the 3 new parameters in `plugins/membrum/src/controller/controller.cpp`: `kMaxPolyphonyId` as `RangeParameter` (min=4, max=16, default=8, stepCount=12, unit="voices"); `kVoiceStealingId` as `StringListParameter` (3 items: "Oldest", "Quietest", "Priority", default=0); `kChokeGroupId` as `RangeParameter` (min=0, max=8, default=0, stepCount=8, unit="group"). Follow the Phase 2 registration pattern. Acceptance evidence: T3.1.5 all assertions pass. Satisfies FR-150, FR-153, SC-030.

### 3.1.3 Verification

- [X] T3.1.17 Build, run all tests, run pluginval. Commands: (1) `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5` — all Phase 2 tests + T3.1.1–T3.1.6 tests must pass; (2) `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" 2>&1 | tee membrum-phase31-pluginval.log` — inspect log for zero errors. Zero compiler warnings required. Acceptance evidence: `All tests passed`; pluginval log zero errors. Dependency: T3.1.7 through T3.1.16.

- [X] T3.1.18 Commit: `"membrum: Phase 3.1 voice pool + stealing policies — Oldest/Quietest/Priority, MIDI 36-67 routing, 3 new params registered"`. Dependency: T3.1.17 green.

---

## Phase 3.2 — Fast-Release Ramp

**Goal**: Click-free voice steal. Exponential decay `gain *= k` where `k = exp(-1/(0.005·sampleRate))`, with mandatory `1e-6f` denormal floor termination, applied by `VoicePool::processBlock` AFTER `DrumVoice::processBlock` writes to scratch. SC-021 met across all 3 policies and 5 sample rates. Idempotent double-steal (FR-127). `DrumVoice` not modified.

**Satisfies**: FR-124, FR-125, FR-126, FR-127, FR-128, FR-164, SC-021

**Dependencies**: Phase 3.1 complete and green.

### 3.2.1 Tests (Write FIRST — Must FAIL)

- [ ] T3.2.1 Write failing test `plugins/membrum/tests/unit/voice_pool/test_steal_click_free.cpp`. Parameterized over {Oldest, Quietest, Priority} × {22050, 44100, 48000, 96000, 192000} Hz (15 combinations). For each: fill pool to capacity, trigger a steal, record the 5 ms window centered on the steal event. Using `tests/test_helpers/signal_metrics.h`: assert peak click artifact ≤ −30 dBFS relative to the incoming voice's peak (FR-126, SC-021). Also assert: no NaN/Inf (bit-manipulation); `fastReleaseGain < 1e-6f` terminates within `ceil(0.006 × sampleRate)` samples (6 ms bound, FR-124's ±1 ms tolerance). Verify tests FAIL. Satisfies FR-126, FR-181, SC-021.

- [ ] T3.2.2 [P] Write failing test `plugins/membrum/tests/unit/voice_pool/test_fast_release_denormals.cpp`. Two cases: (a) FTZ/DAZ explicitly ON (via `tests/test_helpers/enable_ftz_daz.h`): drive a voice into fast-release, run `processBlock()` to completion, assert no non-finite or denormal samples after the `1e-6f` floor triggers; (b) FTZ/DAZ explicitly OFF: repeat and assert the software `1e-6f` floor ALONE prevents denormals — the test must pass even on non-x86 hardware where FTZ/DAZ may be absent (FR-164). Assert voice slot transitions to `Free` within 10 ms at all 5 sample rates. Add this file to `-fno-fast-math` in `tests/CMakeLists.txt`. Verify tests FAIL. Satisfies FR-164.

- [ ] T3.2.3 [P] Write failing test `plugins/membrum/tests/unit/voice_pool/test_fast_release_double_steal.cpp`. Trigger voice A, let it play 50 ms, steal it (fast-release begins in `releasingMeta_`), then while it is still `FastReleasing`, attempt to steal that same slot again. Assert: (a) `beginFastRelease()` is idempotent — `fastReleaseGain` is unchanged by the second call; (b) no double-click artifact in the output; (c) no envelope discontinuity (output monotonically decreasing after the first steal). Verify tests FAIL. Satisfies FR-127.

### 3.2.2 Implementation

- [ ] T3.2.4 Implement `VoicePool::beginFastRelease(int slot)` in `voice_pool.cpp`: if `releasingMeta_[slot].state == FastReleasing`, early-return (idempotent per FR-127); otherwise: copy `voices_[slot]` to `releasingVoices_[slot]` (bitwise copy); set `releasingMeta_[slot].fastReleaseGain = std::max(meta_[slot].currentLevel, kFastReleaseFloor)` (snapshot current amplitude as starting gain); copy relevant `meta_[slot]` fields (`originatingNote`, `originatingChoke`) to `releasingMeta_[slot]`; set `releasingMeta_[slot].state = FastReleasing`. `noexcept`. Acceptance evidence: T3.2.3 idempotency subtests pass; no double-click. Satisfies FR-124, FR-127.

- [ ] T3.2.5 Implement `VoicePool::applyFastRelease(int slot, float* scratch, int numSamples)` in `voice_pool.cpp`: per-sample loop: `scratch[i] *= gain; gain *= fastReleaseK_; if (gain < kFastReleaseFloor) { gain = 0.f; zero scratch[i+1..numSamples); releasingMeta_[slot].state = Free; break; }` then `releasingMeta_[slot].fastReleaseGain = gain`. The `kFastReleaseFloor = 1e-6f` floor is mandatory and UNCONDITIONAL regardless of FTZ/DAZ (Q2, FR-164). `noexcept`. Acceptance evidence: T3.2.1 click ≤ −30 dBFS at all 15 combinations; T3.2.2 denormal floor triggers correctly with FTZ/DAZ off. Satisfies FR-124, FR-125, FR-164, Q2, Q5.

- [ ] T3.2.6 Update `VoicePool::processBlock` in `voice_pool.cpp` to replace the Phase 3.1 stub gain multiplier with the full fast-release post-processing: in the `releasingMeta_[slot].state == FastReleasing` branch, call `releasingVoices_[slot].processBlock(scratchL_.get(), numSamples)`, then call `applyFastRelease(slot, scratchL_.get(), numSamples)`, then accumulate the resulting scratch into both output channels — accumulate ONLY up to the sample where the `1e-6f` floor triggered (when `state` becomes `Free`, do NOT accumulate the zeroed tail per FR-124 clarification). `noexcept`. Acceptance evidence: T3.2.1 all 15 combinations pass. Satisfies Q5, FR-124.

### 3.2.3 Verification

- [ ] T3.2.7 Build and run Phase 3.2 tests, then full suite. Commands: `membrum_tests.exe "fast release*" 2>&1 | tail -5`; `membrum_tests.exe "steal*" 2>&1 | tail -5`; `membrum_tests.exe 2>&1 | tail -5`. All 3 new test files plus all Phase 3.0–3.1 tests pass. Zero compiler warnings.

- [ ] T3.2.8 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" 2>&1 | tee membrum-phase32-pluginval.log` — inspect log, zero errors.

- [ ] T3.2.9 Commit: `"membrum: Phase 3.2 fast-release ramp — exponential decay tau=5ms, 1e-6f denormal floor, click-free steal SC-021 verified"`. Dependency: T3.2.7 and T3.2.8 green.

---

## Phase 3.3 — Choke Groups

**Goal**: `ChokeGroupTable` active in the note-on path; `kChokeGroupId` wired; choke events fast-release ALL matching active voices (group-wide mute, FR-133); SC-022 met; allocation-free on the choke path.

**Satisfies**: FR-130–138, FR-182, SC-022

**Dependencies**: Phase 3.2 complete and green.

### 3.3.1 Tests (Write FIRST — Must FAIL)

- [ ] T3.3.1 Write failing test `plugins/membrum/tests/unit/voice_pool/test_choke_group.cpp`. FR-182 full matrix — (a) canonical open/closed-hat case: configure notes 42 and 46 both in group 1; trigger note 46; wait 100 ms; trigger note 42; assert note-46 voice amplitude drops to ≤ −60 dBFS within 5 ms of the note-42 trigger; (b) 8-group orthogonality: 8 notes in groups 1–8; trigger a second note in group 1; assert only group-1 voice is `FastReleasing`, others remain `Active`; (c) group-0 no-op: note in group 0 triggers; assert zero `releasingMeta_[i].state` changes for otherwise-active voices (no choke iteration executed, FR-136); (d) cross-group isolation: note in group 1, then note in group 2; assert group-1 voice continues decaying naturally (FR-135); (e) all-voices-one-group stress: 16 voices in group 1, new note in group 1; assert all 15 other voices enter `FastReleasing` state (FR-133). Verify tests FAIL. Satisfies FR-182.

- [ ] T3.3.2 [P] Write failing test `plugins/membrum/tests/unit/voice_pool/test_choke_click_free.cpp`. Trigger open-hat (note 46), wait 100 ms, trigger closed-hat (note 42) in the same choke group. Using `tests/test_helpers/signal_metrics.h` and `tests/test_helpers/artifact_detection.h`: (a) peak click artifact during choke ≤ −30 dBFS (SC-022, FR-134); (b) choke fast-release completes within 5 ± 1 ms wall-clock (SC-022); (c) note-42 voice first 20 ms is bit-identical to an isolated note-42 trigger within −120 dBFS noise floor. Verify tests FAIL. Satisfies FR-134, SC-022.

- [ ] T3.3.3 [P] Write failing test `plugins/membrum/tests/unit/voice_pool/test_choke_allocation_free.cpp`. Using `allocation_detector.h`: 1000 rapid open/closed-hat alternations (notes 42 and 46 alternating, both in group 1) over 10 seconds. Assert zero heap allocations across all `noteOn` calls including choke processing, all `processBlock` calls including the fast-release tail. Assert no NaN/Inf samples. Verify tests FAIL. Satisfies FR-116, FR-163.

### 3.3.2 Implementation

- [ ] T3.3.4 Implement `VoicePool::processChokeGroups(uint8_t newNote)` in `voice_pool.cpp`: look up `group = chokeGroups_.lookup(newNote)`; if `group == 0`, return immediately (FR-136 early-out, zero pool iteration); iterate `[0, kMaxVoices)`: for each slot where (`meta_[slot].state == Active` AND `meta_[slot].originatingChoke == group`) OR (`releasingMeta_[slot].state == FastReleasing` AND `releasingMeta_[slot].originatingChoke == group`): call `beginFastRelease(slot)` (reuses Phase 3.2 mechanism). The newly-triggered voice is not yet allocated at this point so cannot accidentally choke itself. `noexcept`. Acceptance evidence: T3.3.1 all 5 subtests pass. Satisfies FR-132, FR-133, FR-135, FR-136, FR-137, FR-138.

- [ ] T3.3.5 Activate `processChokeGroups` call in `VoicePool::noteOn`: remove the Phase 3.1 stub return-guard and let the now-implemented `processChokeGroups` execute fully. Verify that the `originatingChoke` field in `meta_[slot]` is correctly populated from `chokeGroups_.lookup(midiNote)` at note-on time. Acceptance evidence: T3.3.1 (a) canonical hat test passes end-to-end. Satisfies FR-171.

### 3.3.3 Verification

- [ ] T3.3.6 Build and run Phase 3.3 tests, then full suite. Commands: `membrum_tests.exe "choke*" 2>&1 | tail -5`; `membrum_tests.exe 2>&1 | tail -5`. All 3 choke test files plus all Phase 3.0–3.2 tests pass. Zero compiler warnings.

- [ ] T3.3.7 Run pluginval: capture to log, zero errors. Command: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" 2>&1 | tee membrum-phase33-pluginval.log`.

- [ ] T3.3.8 Commit: `"membrum: Phase 3.3 choke groups — ChokeGroupTable wired, kChokeGroupId param, group-wide fast-release, SC-022 click-free verified"`. Dependency: T3.3.6 and T3.3.7 green.

---

## Phase 3.4 — State v3 Migration

**Goal**: `getState` writes a 302-byte v3 blob unconditionally; `setState` dispatches v1/v2/v3 with clamp-on-load for corruption; committed fixture; all state round-trip tests green.

**Satisfies**: FR-140, FR-141, FR-142, FR-143, FR-144, FR-183, FR-184, SC-025, SC-026

**Dependencies**: Phase 3.3 complete and green.

### 3.4.1 Tests (Write FIRST — Must FAIL)

- [ ] T3.4.1 Write failing test `plugins/membrum/tests/unit/vst/test_state_roundtrip_v3.cpp`. SC-026: (a) create Phase 3 processor; set `maxPolyphony=12`, `voiceStealingPolicy=1 (Quietest)`, `chokeGroupAssignments` all=5; call `getState()` — assert blob exactly 302 bytes; call `setState()` on a fresh processor; assert all fields round-trip bit-exactly; (b) extreme values: `maxPolyphony=4`, `voiceStealingPolicy=2`, `chokeGroupAssignments` alternating 0/8. Verify tests FAIL. Satisfies FR-184, SC-026.

- [ ] T3.4.2 [P] Write failing test `plugins/membrum/tests/unit/vst/test_state_migration_v2_to_v3.cpp`. SC-025: load `plugins/membrum/tests/golden/phase2_state_v2.bin` into a Phase 3 processor. Assert: (a) `setState` returns `kResultOk`; (b) all Phase 2 params match the v2 blob bit-exactly; (c) `maxPolyphony==8`, `voiceStealingPolicy==0 (Oldest)`, all 32 `chokeGroupAssignments[i]==0`; (d) a subsequent `getState()` produces exactly 302 bytes with the v2 data plus Phase 3 defaults. Verify tests FAIL. Satisfies FR-142, FR-183, SC-025.

- [ ] T3.4.3 [P] Write failing test `plugins/membrum/tests/unit/vst/test_state_migration_v1_to_v3.cpp`. Construct a synthetic v1 blob (version=1 int32, 5 Phase-1 float64 values, total ~44 bytes). Load into Phase 3 processor. Assert: `kResultOk`; Phase 1 params preserved; Phase 2 params take Phase 2 defaults; Phase 3 params: `maxPolyphony=8`, `voiceStealingPolicy=0`, all `chokeGroupAssignments[i]=0`. Verify tests FAIL. Satisfies FR-143.

- [ ] T3.4.4 [P] Write failing test `plugins/membrum/tests/unit/vst/test_state_corruption_clamp.cpp`. Construct a v3 blob with corrupt values: `maxPolyphony=99`, `voiceStealingPolicy=200`, `chokeGroupAssignments[3]=250`. Load into Phase 3 processor. Assert: (a) `kResultOk` returned (no rejection); (b) `maxPolyphony` clamped to 16; (c) `voiceStealingPolicy` clamped to 0; (d) `chokeGroupAssignments[3]` clamped to 0; (e) no crash; (f) subsequent `getState()` produces a valid 302-byte blob with the clamped values. Verify tests FAIL. Satisfies FR-144.

### 3.4.2 Fixture

- [ ] T3.4.5 Ensure `plugins/membrum/tests/golden/phase2_state_v2.bin` exists as a committed binary. If it does not exist from Phase 2, generate it by running the Phase 2 `getState()` with all-default parameters via `test_state_roundtrip_v2.cpp`'s fixture generation path, or by writing a one-shot generator test. Verify the file is exactly 268 bytes. Commit if not already committed. Acceptance evidence: `ls -la plugins/membrum/tests/golden/phase2_state_v2.bin` shows 268 bytes. Satisfies FR-183.

### 3.4.3 Implementation

- [ ] T3.4.6 Implement `Processor::getState` v3 writer in `plugins/membrum/src/processor/processor.cpp`: write `version = 3` (int32), then the complete v2 body in the exact order from `data-model.md §7` (4 + 40 + 4 + 4 + 216 = 268 bytes), then the v3 tail: `maxPolyphony` (1 byte uint8), `voiceStealingPolicy` (1 byte uint8), all 32 `chokeGroupAssignments` bytes via `voicePool_.getChokeGroupAssignments()` (32 bytes), written unconditionally with no length prefix or version branch (Q1). Total: 302 bytes. Use `IBStream::write(&value, sizeof(value), nullptr)` matching Phase 2 pattern. Acceptance evidence: T3.4.1 blob size == 302 and round-trip passes. Satisfies FR-141, Q1.

- [ ] T3.4.7 Implement `Processor::setState` migration dispatcher in `processor.cpp`: read version field; dispatch: v1 → apply existing v1→v2 path, then apply v3 defaults from T3.4.8; v2 → load v2 body byte-for-byte (268 bytes), then apply v3 defaults; v3 → load v2 body, then load v3 tail with clamping: `maxPolyphony = std::clamp(in, 4, 16)`, `voiceStealingPolicy = in > 2 ? 0 : in`, each `chokeGroupAssignments[i] = in[i] > 8 ? 0 : in[i]` (FR-144); version > 3 → return `kResultFalse`. Propagate loaded values to `maxPolyphony_`, `voiceStealingPolicy_`, `chokeGroup_` atomics; call `voicePool_.loadChokeGroupAssignments(raw)`. Acceptance evidence: T3.4.1, T3.4.2, T3.4.3, T3.4.4 all pass. Satisfies FR-140, FR-141, FR-142, FR-143, FR-144.

- [ ] T3.4.8 Implement the v2→v3 migration inline in `setState`: after loading the v2 body, write Phase 3 defaults: `maxPolyphony = 8`, `voiceStealingPolicy = 0`, all 32 `chokeGroupAssignments = 0`. No additional parsing needed — v2 blobs have no v3 tail. Acceptance evidence: T3.4.2 Phase 3 default fields assert correctly. Satisfies FR-142.

### 3.4.4 Verification

- [ ] T3.4.9 Build and run Phase 3.4 tests, then full suite. Commands: `membrum_tests.exe "state*" 2>&1 | tail -5`; `membrum_tests.exe "migration*" 2>&1 | tail -5`; `membrum_tests.exe 2>&1 | tail -5`. All state and migration tests pass; all Phase 3.0–3.3 tests still pass. Zero compiler warnings. Acceptance evidence: `All tests passed`.

- [ ] T3.4.10 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" 2>&1 | tee membrum-phase34-pluginval.log` — inspect log, zero errors.

- [ ] T3.4.11 Commit: `"membrum: Phase 3.4 state v3 + v1/v2 migration — 302-byte blob, clamping on corrupt load"`. Dependency: T3.4.9 and T3.4.10 green.

---

## Phase 3.5 — CPU Budgets & Stress

**Goal**: 8-voice worst-case ≤ 12% CPU (18% waiver for Phase 2 waived Feedback+NoiseBody+TS+UN cell); 16-voice 10 s stress run zero xruns; full allocation-detector on 10 s fuzz stream; SIMD fallback decision documented.

**Satisfies**: FR-160, FR-161, FR-162, FR-163, FR-185, FR-186, SC-023, SC-024, SC-027, SC-027a

**Dependencies**: Phase 3.4 complete and green.

### 3.5.1 Tests (Write FIRST — Must FAIL)

- [ ] T3.5.1 Write failing test `plugins/membrum/tests/unit/voice_pool/test_polyphony_allocation_matrix.cpp`. SC-027 / SC-027a: using `allocation_detector.h`, run 10 seconds at 44100 Hz / 128-sample blocks with 16 voices, fuzzed random MIDI note-ons every 5 ms (notes 36–67, random velocity, random choke groups), voice steals, choke events, mid-test parameter changes to `maxPolyphony`, `voiceStealingPolicy`, `chokeGroupAssignments`. Assert: (a) zero heap allocations throughout; (b) zero from `processBlock`, `noteOn`, `noteOff`, `setMaxPolyphony`, `setVoiceStealingPolicy`, `setChokeGroup`; (c) no NaN/Inf; (d) `getActiveVoiceCount() <= maxPolyphony` at all times. For SC-027a, add in `voice_pool.h` (outside any function) a `static_assert(sizeof(VoicePool) <= kVoicePoolSizeLimit, "VoicePool struct size exceeds budget")` where `kVoicePoolSizeLimit` is a constant set to `32 * sizeof(DrumVoice) + 32 * sizeof(VoiceMeta) + sizeof(Krate::DSP::VoiceAllocator) + sizeof(ChokeGroupTable) + 1024` (1 KB slack for bookkeeping fields). This catches any accidental addition of a `std::vector` or heap-owning member (which would be detected at runtime by allocation_detector but the static_assert provides a compile-time safety net). The definitive SC-027a evidence is the allocation_detector fuzz test showing zero allocations after `prepare()` returns. Verify tests FAIL. Satisfies FR-163, FR-185, SC-027, SC-027a.

- [ ] T3.5.2 Write failing test `plugins/membrum/tests/perf/test_polyphony_benchmark.cpp` (tagged `[.perf]`). SC-023: measure 8-voice worst-case CPU (Feedback + NoiseBody + Tone Shaper + all Unnatural Zone modules) at 44100 Hz / 128-sample block over 10 s. Hard-assert ≤ 12% for non-waived combinations; ≤ 18% for the Phase 2 waived Feedback+NoiseBody+TS+UN cell (documenting the waiver per Phase 2 precedent). Output to `membrum_phase3_benchmark.csv`. SC-024: 16-voice stress run — 16 voices, 10 s, random note-on every 5 ms, assert no xrun (wall-clock processing time < audio wall-clock per block), assert zero NaN/Inf. Verify tests FAIL (tagged `[.perf]` so they do not run in normal `membrum_tests.exe` invocations). Satisfies FR-160, FR-161, FR-186, SC-023, SC-024.

### 3.5.2 Decision and Measurement

- [ ] T3.5.3 Run the Phase 3.5 benchmarks and record results. Command: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe "[.perf]" 2>&1 | tee membrum-phase35-perf.log`. Record 8-voice CPU % and 16-voice xrun count in `plan.md §Complexity Tracking`. Acceptance evidence: log shows 8-voice CPU ≤ 12% (or ≤ 18% for waived cell); zero xruns; numbers filled into the Complexity Tracking table. Satisfies SC-023, SC-024.

- [ ] T3.5.4 SIMD fallback decision (FR-162): if T3.5.3 shows 8-voice scalar worst-case > 12%: enable `ModalResonatorBankSIMD` inside `BodyBank::sharedBank_` (scope: `plugins/membrum/src/dsp/body_bank.h`, zero API change); re-run T3.5.3 to confirm it passes. If scalar path already passes: document "SIMD fallback: NOT triggered" in `plan.md §Complexity Tracking`. Acceptance evidence: either (a) scalar benchmark passes ≤ 12% OR (b) SIMD is enabled and benchmark passes, both paths documented. Satisfies FR-162.

### 3.5.3 Verification

- [ ] T3.5.5 Run full test suite: `membrum_tests.exe 2>&1 | tail -5` — all Phase 3.0–3.4 tests plus T3.5.1 pass. Run benchmark: `membrum_tests.exe "[.perf]" 2>&1 | tail -30` — T3.5.2 assertions pass. Zero compiler warnings. Acceptance evidence: `All tests passed` in standard run; benchmark asserts green in perf run.

- [ ] T3.5.6 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" 2>&1 | tee membrum-phase35-pluginval.log` — inspect log, zero errors.

- [ ] T3.5.7 Commit: `"membrum: Phase 3.5 CPU budget + 16-voice stress + allocation matrix — SC-023/SC-024/SC-027 verified"`. Dependency: T3.5.5 and T3.5.6 green.

---

## Phase 3.6 — Quality Gates & Compliance

**Goal**: Pluginval strictness 5 zero errors final pass; clang-tidy zero warnings on ALL Phase 3 files; compliance table filled with concrete measured evidence per CLAUDE.md Completion Honesty; Phase 2 regression final confirmation; version bumped to 0.3.0; parent roadmap updated.

**Satisfies**: FR-188, FR-189, SC-028 (final), SC-029, all FRs/SCs requiring concrete evidence

**Dependencies**: Phase 3.5 complete and green.

- [ ] T3.6.1 Run clang-tidy on all Phase 3 source files and fix ALL warnings to zero. Command: `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja 2>&1 | tee membrum-phase3-clang-tidy.log`. Inspect the log and fix every warning in `plugins/membrum/src/voice_pool/`, `plugins/membrum/src/processor/processor.h`, `plugins/membrum/src/processor/processor.cpp`, `plugins/membrum/src/controller/controller.cpp`. No "pre-existing" exemptions (per CLAUDE.md memory "Own ALL failures"). Acceptance evidence: re-run after fixes shows zero warnings in the log. Satisfies CLAUDE.md workflow step 6, SC-029.

- [ ] T3.6.2 Run pluginval final pass: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" 2>&1 | tee membrum-phase3-final-pluginval.log`. Inspect log. Acceptance evidence: log contains zero errors and zero warnings. Satisfies FR-188, SC-029.

- [ ] T3.6.3 Final Phase 2 regression confirmation: `membrum_tests.exe "Phase2Regression*" 2>&1 | tail -5` (or equivalent tag/name filter targeting T3.1.4's test case). Assert output within −90 dBFS RMS of Phase 2 golden reference. Acceptance evidence: `All tests passed` for the regression subset; the measured RMS difference is recorded in the compliance table. Satisfies FR-187, SC-028.

- [ ] T3.6.4 Fill the compliance table in `specs/138-membrum-phase3-polyphony/spec.md`. For each FR-110 through FR-194 and SC-020 through SC-031: open the actual source file or test log and record the concrete evidence — file path + line number for FRs, measured value + test name for SCs. No generic "implemented" claims per CLAUDE.md Completion Honesty. SC row examples: "SC-021: click artifact = X.X dBFS measured in `test_steal_click_free.cpp` across 15 combinations"; "SC-023: 8-voice CPU = X.X% per `membrum_phase3_benchmark.csv`". Acceptance evidence: compliance table has zero blank cells; every SC row has a real number from test output or benchmark CSV; every FR row has a file:line citation from a source read during this task. Satisfies CLAUDE.md Completion Honesty rule.

- [ ] T3.6.5 Bump `plugins/membrum/version.json` to `{"major":0,"minor":3,"patch":0}`. This is the ONLY file to edit for the version bump per CLAUDE.md policy ("Version bumps: only edit version.json, never generated files"). Acceptance evidence: `cat plugins/membrum/version.json` shows `"minor":3,"patch":0`. Satisfies plan.md §Phase 3.6 Deliverables.

- [ ] T3.6.6 Update `plugins/membrum/CHANGELOG.md` with Phase 3 release notes: add a `## v0.3.0` section listing all Phase 3 features (16-voice pool, 3 stealing policies, 8 choke groups, state v3 migration, 3 new parameters). Update `specs/135-membrum-drum-synth.md`: change Phase 3 row status from "Specced" to "Complete (v0.3.0)"; add a version history entry for v0.3.0 dated 2026-04-11. Acceptance evidence: CHANGELOG.md contains `## v0.3.0` heading; roadmap Phase 3 row shows "Complete (v0.3.0)".

- [ ] T3.6.7 Final commit: `"membrum: Phase 3.6 compliance + v0.3.0 release — pluginval clean, zero clang-tidy warnings, all FR/SC verified with concrete evidence"`. Dependency: T3.6.1 through T3.6.6 all complete.

---

## FR/SC Coverage Matrix

Every FR and SC from `spec.md` maps to at least one task that verifies it.

| FR / SC | Verified by Task(s) |
|---------|---------------------|
| FR-110 | T3.0.5, T3.1.1 |
| FR-111 | T3.0.5, T3.1.6, T3.1.12 |
| FR-112 | T3.0.5, T3.1.9 |
| FR-113 | T3.1.9, T3.1.15 |
| FR-114 | T3.1.1(b), T3.1.10 |
| FR-115 | T3.1.1(c), T3.1.11 |
| FR-116 | T3.1.3, T3.1.8, T3.5.1 |
| FR-117 | T3.0.5, T3.1.8 |
| FR-120 | T3.0.3, T3.1.5, T3.1.13 |
| FR-121 | T3.1.2, T3.1.9 |
| FR-122 | T3.1.2, T3.1.9, T3.1.14 |
| FR-123 | T3.1.2, T3.1.9 |
| FR-124 | T3.2.1, T3.2.4, T3.2.5, T3.2.6 |
| FR-125 | T3.2.1, T3.2.5 |
| FR-126 | T3.2.1 |
| FR-127 | T3.2.3, T3.2.4 |
| FR-128 | T3.1.2, T3.1.14 |
| FR-130 | T3.0.4, T3.3.1 |
| FR-131 | T3.0.4, T3.3.4 |
| FR-132 | T3.3.1, T3.3.4 |
| FR-133 | T3.3.1(e), T3.3.4 |
| FR-134 | T3.3.2 |
| FR-135 | T3.3.1(b,d), T3.3.4 |
| FR-136 | T3.3.1(c), T3.3.4 |
| FR-137 | T3.3.1(e), T3.5.1 |
| FR-138 | T3.1.7, T3.3.4, T3.3.5 |
| FR-140 | T3.0.6 |
| FR-141 | T3.4.6 |
| FR-142 | T3.4.2, T3.4.8 |
| FR-143 | T3.4.3, T3.4.7 |
| FR-144 | T3.1.7, T3.4.4, T3.4.7 |
| FR-150 | T3.0.6, T3.1.5, T3.1.16 |
| FR-151 | T3.0.6 |
| FR-152 | T3.1.5, T3.1.15 |
| FR-153 | T3.1.16 |
| FR-160 | T3.5.2, T3.5.3 |
| FR-161 | T3.5.2, T3.5.3 |
| FR-162 | T3.5.4 |
| FR-163 | T3.1.3, T3.3.3, T3.5.1 |
| FR-164 | T3.2.2, T3.2.5 |
| FR-165 | T3.1.11 |
| FR-170 | T3.1.9, T3.1.15 |
| FR-171 | T3.1.9, T3.3.5 |
| FR-172 | T3.0.2, T3.1.9 |
| FR-180 | T3.1.2 |
| FR-181 | T3.2.1 |
| FR-182 | T3.3.1 |
| FR-183 | T3.4.2, T3.4.5 |
| FR-184 | T3.4.1 |
| FR-185 | T3.1.3, T3.5.1 |
| FR-186 | T3.5.2 |
| FR-187 | T3.1.4, T3.6.3 |
| FR-188 | T3.6.2 |
| FR-189 | T3.6.2 (macOS CI) |
| FR-190 | T3.0.5 (ODR check via grep at scaffolding time) |
| FR-191 | T3.0.5, T3.1.8 (voice_allocator.h unchanged invariant) |
| FR-193 | T3.0.7 (dsp/ unchanged — only membrum CMakeLists updated) |
| FR-194 | T3.1.9, T3.2.4, T3.2.6 (DrumVoice not modified) |
| SC-020 | T3.1.1(d) |
| SC-021 | T3.2.1 |
| SC-022 | T3.3.2 |
| SC-023 | T3.5.2, T3.5.3 |
| SC-024 | T3.5.2, T3.5.3 |
| SC-025 | T3.4.2 |
| SC-026 | T3.4.1 |
| SC-027 | T3.5.1 |
| SC-027a | T3.5.1 |
| SC-028 | T3.1.4, T3.6.3 |
| SC-029 | T3.6.1, T3.6.2 |
| SC-030 | T3.1.5 |
| SC-031 | T3.1.6, T3.1.12 |
