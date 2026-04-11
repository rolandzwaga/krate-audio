# Implementation Plan: Membrum Phase 3 — Multi-Voice Polyphony, Voice Management, Choke Groups

**Branch**: `138-membrum-phase3-polyphony` | **Date**: 2026-04-11 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/138-membrum-phase3-polyphony/spec.md`

## Summary

Phase 3 wraps Phase 2's single `Membrum::DrumVoice` in a fixed 16-slot `Membrum::VoicePool` driven by the existing `Krate::DSP::VoiceAllocator`. Up to `maxPolyphony` voices (4–16, default 8) sound concurrently, with click-free voice stealing via an exponential fast-release envelope (`gain *= exp(-1/(0.005·sampleRate))`, denormal floor `1e-6f`), three user-selectable stealing policies (Oldest, Quietest, Priority), and 8 choke groups (plus group 0 = none) implemented via a 32-entry `chokeGroupAssignments` table. State schema is bumped from v2 → v3 with a strictly additive 34-byte tail (1 + 1 + 32). `DrumVoice`, every Phase 2 DSP component, and `dsp/include/krate/dsp/systems/voice_allocator.h` are **not modified** (FR-193, FR-194, Clarification Q3, Q5). The plan honors the five locked Clarifications from 2026-04-11: **Q1** chokeGroupAssignments is always 32 serialized bytes; **Q2** fast-release curve is exponential with 5 ms τ and a `1e-6f` denormal floor; **Q3** Quietest policy is processor-layer-only; **Q4** the `VoicePool` owns one `float[2][kVoicePoolMaxBlock]` scratch buffer (declared constant `kVoicePoolMaxBlock = 2048`); **Q5** the fast-release gain is applied by `VoicePool::processBlock` to that scratch, never inside `DrumVoice`.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang/Xcode 13+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+ (host-generic editor only — no uidesc), KrateDSP (reuses `VoiceAllocator`; no modifications), KratePluginsShared
**Storage**: Binary state version 3 — v2 blob (268 bytes) followed by 1 + 1 + 32 new bytes = 302 bytes total
**Testing**: Catch2 via `membrum_tests` target; `tests/test_helpers/allocation_detector.h`, `signal_metrics.h`, `artifact_detection.h`, `enable_ftz_daz.h`; `pluginval --strictness-level 5`; `auval -v aumu Mbrm KrAt` (macOS CI)
**Target Platform**: Windows 10/11 (MSVC x64), macOS 11+ (Clang universal, ARM64 + x86_64), Linux (GCC/Clang x86_64)
**Project Type**: Plugin refactor in the existing monorepo (`plugins/membrum/`)
**Performance Goals**: ≤ 12 % single-core CPU on 8-voice worst case at 44.1 kHz / 128-sample block (18 % waiver for the Phase 2 Feedback+NoiseBody+TS+UN waived cell). 16-voice stress test completes 10 s without xrun.
**Constraints**: Zero heap allocations on the audio thread under any code path; lock-free (only `std::atomic` / `std::atomic_flag`); no `dsp/` changes; no custom UI; no VSTGUI code; fully cross-platform; FTZ/DAZ on x86, explicit `1e-6f` denormal floor for portability.
**Scale/Scope**: 1 new top-level class (`VoicePool`), 1 new metadata struct (`VoiceMeta`), 1 new tiny wrapper (`ChokeGroupTable`), 3 new parameters, 1 state version bump, ~8 new test files, zero modifications to `DrumVoice` internals or to `voice_allocator.h`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I — VST3 Architecture Separation:**
- [x] Processor and Controller remain separate. No cross-includes. (Phase 2 carryover.)
- [x] Parameter flow: Host → Processor via `IParameterChanges`; Controller receives state via `setComponentState()`.
- [x] Processor works without controller. 3 new parameters follow the same atomic pattern.

**Principle II — Real-Time Audio Thread Safety:**
- [x] `VoicePool` pre-allocates 16 `DrumVoice` instances + `std::array<VoiceMeta, 16>` + 1 `VoiceAllocator` + `float[2][kVoicePoolMaxBlock]` scratch in `setupProcessing()` / `setActive()` (where `kVoicePoolMaxBlock = 2048`; actual allocation is sized to the host's reported max block size, clamped to this limit).
- [x] Fast-release exponential decay runs branchless inside `VoicePool::processBlock`'s per-voice mixing loop with explicit `1e-6f` denormal floor (FR-164).
- [x] All parameter setters (`setMaxPolyphony`, `setVoiceStealingPolicy`, `setChokeGroup`) are atomic-write-only; no audio-thread allocation.
- [x] No locks, no exceptions, no I/O, no virtual dispatch on the hot path.
- [x] `tests/test_helpers/allocation_detector.h` gates every audio-thread entry point (FR-185 / SC-027).

**Principle III — Modern C++ Standards:**
- [x] C++20, RAII, `std::array`, `std::variant` (Phase 2 carryover), no raw `new`/`delete`.
- [x] Designated initializers used for all new struct literals to avoid clang narrowing errors (CLAUDE.md Cross-Platform).

**Principle IV — SIMD & DSP Optimization:**
- [x] No new SIMD code in Phase 3. `ModalResonatorBankSIMD` is conditionally switched in **only** if the scalar 8-voice worst case exceeds 12 % (FR-162). Baseline is scalar, matching Phase 2.

**Principle V — VSTGUI Development:**
- [x] N/A — host-generic editor only. No uidesc, no custom views. Custom UI is Phase 6.

**Principle VI — Cross-Platform Compatibility:**
- [x] No platform-specific code. All logic is portable C++. Denormal floor is an explicit software guard layered on top of FTZ/DAZ (portable to ARM per FR-164).
- [x] CI matrix (Windows/macOS/Linux) unchanged. No AU bus-config change → `au-info.plist` and `audiounitconfig.h` untouched.
- [x] Narrowing: every new struct literal uses designated initializers.

**Principle VII — Project Structure & Build System:**
- [x] All Phase 3 code is plugin-local (`plugins/membrum/src/voice_pool/` + `plugins/membrum/tests/unit/voice_pool/`). `dsp/` is unchanged (FR-193).
- [x] CMake updates limited to adding source/test files to `plugins/membrum/CMakeLists.txt` and `plugins/membrum/tests/CMakeLists.txt`.

**Principle VIII — Testing Discipline:**
- [x] Tests are written BEFORE implementation per Phase 3.x sub-phase ordering (see Phase breakdown below).
- [x] Pre-existing Phase 2 tests must continue to pass unchanged — the `maxPolyphony = 1` regression path is explicitly verified (FR-187 / SC-028).
- [x] `tests/test_helpers/allocation_detector.h` / `signal_metrics.h` / `artifact_detection.h` reused, not duplicated.

**Principle IX — Layered DSP Architecture:**
- [x] Zero new `dsp/` components. `Krate::DSP::VoiceAllocator` (Layer 3) is consumed read-only by the plugin-local `Membrum::VoicePool`.

**Principle XII — Debugging Discipline:**
- [x] Framework commitment — `VoiceAllocator::noteOn()` returns a `std::span<const VoiceEvent>`; Phase 3 consumes that API as documented rather than inventing a parallel path. See Dependency API Contracts below for the exact signatures (spec and research text referred to a non-existent `stealVoice(idx)` / `setActiveVoiceCount()`; the plan corrects this to `setVoiceCount()` and the real `noteOn/noteOff/voiceFinished` event protocol).

**Principle XIII — Test-First Development:**
- [x] Skills auto-load (`testing-guide`, `vst-guide`, `dsp-architecture`, `claude-file`).
- [x] Each Phase 3.x sub-phase ends with a build-before-test cycle, zero-warning build, green test run, then commit.

**Principle XIV — ODR Prevention:**
- [x] Codebase research complete — see below. New types are `Membrum::VoicePool`, `Membrum::VoiceMeta`, `Membrum::VoiceSlotState` (enum), `Membrum::VoiceStealingPolicy` (enum), `Membrum::ChokeGroupTable`. None collide.
- [x] Phase 3 does NOT introduce a class called `VoiceMeta` in any other namespace — verified below.

**Principle XVI — Honest Completion:**
- [x] Compliance table in `spec.md` will be filled ONLY with file paths, line numbers, test names, and measured values — never from memory (per CLAUDE.md Completion Honesty).

**Post-design re-check (after Phase 1 design below complete):** PASS. No new constitution violations. The only correction made during design was renaming the `VoiceAllocator` API references from the spec/research text (`stealVoice`, `setActiveVoiceCount`) to the actual headers (`setVoiceCount`, `noteOn`→events); this is documented in the Dependency API Contracts section below.

## Codebase Research (Principle XIV — ODR Prevention)

### Mandatory Searches Performed

**Classes/structs to be created — ODR searches:**

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `Membrum::VoicePool` | `grep -rn "class VoicePool" dsp/ plugins/` | No | Create New |
| `Membrum::VoiceMeta` | `grep -rn "VoiceMeta" dsp/ plugins/` | No | Create New |
| `Membrum::VoiceSlotState` (enum class) | `grep -rn "VoiceSlotState" dsp/ plugins/` | No | Create New |
| `Membrum::VoiceStealingPolicy` (enum class) | `grep -rn "VoiceStealingPolicy" dsp/ plugins/` | No | Create New |
| `Membrum::ChokeGroupTable` | `grep -rn "ChokeGroupTable" dsp/ plugins/` | No | Create New |
| `kMaxPolyphonyId`, `kVoiceStealingId`, `kChokeGroupId` | `grep -rn "kMaxPolyphonyId\|kVoiceStealingId\|kChokeGroupId" plugins/` | No | Create New |

Existing collision hazards ruled out:
- `Krate::DSP::VoiceAllocator::VoiceSlot` — nested, in `Krate::DSP::` namespace; our `Membrum::VoiceSlotState` is a distinct enum in a distinct namespace. No collision.
- `Krate::DSP::VoiceState` — enum class in `Krate::DSP::`; our `VoiceSlotState` is intentionally differently named to avoid reader confusion despite the distinct namespace.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `Krate::DSP::VoiceAllocator` | `dsp/include/krate/dsp/systems/voice_allocator.h` | 3 | Pool allocation, stealing for Oldest/Priority policies, per-slot timestamp tracking via `VoiceEvent` consumption. Unchanged. |
| `Membrum::DrumVoice` | `plugins/membrum/src/dsp/drum_voice.h` | (plugin-local) | Replicated 16× inside `VoicePool::voices_`. Zero modifications per FR-194 + Clarification Q5. |
| `Krate::DSP::ADSREnvelope::getOutput()` | `dsp/include/krate/dsp/primitives/adsr_envelope.h:268` | 1 | (Read-only) used indirectly — `VoicePool` measures the peak of the scratch buffer per block to update `VoiceMeta::currentLevel`; this avoids any `DrumVoice` modification. |
| `tests/test_helpers/allocation_detector.h` | `tests/test_helpers/` | (test helper) | Wraps all Phase 3 audio-thread entry points (FR-185). |
| `tests/test_helpers/signal_metrics.h` | `tests/test_helpers/` | (test helper) | −30 dBFS click artifact verification for SC-021, SC-022. |
| `tests/test_helpers/artifact_detection.h` | `tests/test_helpers/` | (test helper) | Click/pop detection regression. |
| `tests/test_helpers/enable_ftz_daz.h` | `tests/test_helpers/` | (test helper) | Denormal protection in Phase 3 tests (FR-164). |
| `plugins/membrum/src/plugin_ids.h` | (existing) | (plugin-local) | Extended with `kMaxPolyphonyId`, `kVoiceStealingId`, `kChokeGroupId`; bumped `kCurrentStateVersion` from 2 → 3. |
| `plugins/membrum/src/processor/processor.{h,cpp}` | (existing) | (plugin-local) | `voice_` replaced with `voicePool_`; `processEvents` routes note-ons through the pool; `getState`/`setState` gain v3 tail. |
| `plugins/membrum/src/controller/controller.cpp` | (existing) | (plugin-local) | Registers the 3 new parameters (1 `StringListParameter` + 2 `RangeParameter`). |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` — no `VoicePool`, `VoiceMeta`, or `ChokeGroupTable` symbols.
- [x] `dsp/include/krate/dsp/systems/voice_allocator.h` — read in full; the public API is documented in "Dependency API Contracts" below.
- [x] `plugins/membrum/src/` — Phase 2 files listed. Single `DrumVoice` member (`Processor::voice_`) will be replaced with `VoicePool`.
- [x] `plugins/iterum/`, `plugins/disrumpo/`, `plugins/ruinae/`, `plugins/innexus/`, `plugins/gradus/`, `plugins/shared/` — no `Membrum::*` symbols.

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Every new type is in the `Membrum` namespace, which Phase 1 and Phase 2 already established as the plugin-local scope. The single hazard was `VoiceState` (exists in `Krate::DSP::`), resolved by renaming Phase 3's enum to `VoiceSlotState` to keep readability high. No Phase 3 type is created in a namespace shared with any other plugin or with `Krate::DSP::`. The ODR searches in the table above will be re-executed at the start of `/speckit.implement` to catch any drift from parallel branches.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

All signatures verified by reading `dsp/include/krate/dsp/systems/voice_allocator.h` and `plugins/membrum/src/dsp/drum_voice.h` on 2026-04-11.

| Dependency | Method | Exact Signature | Verified? |
|------------|--------|-----------------|-----------|
| `Krate::DSP::VoiceAllocator` | `noteOn` | `[[nodiscard]] std::span<const VoiceEvent> noteOn(uint8_t note, uint8_t velocity) noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `noteOff` | `[[nodiscard]] std::span<const VoiceEvent> noteOff(uint8_t note) noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `voiceFinished` | `void voiceFinished(size_t voiceIndex) noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `setAllocationMode` | `void setAllocationMode(AllocationMode mode) noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `setVoiceCount` | `[[nodiscard]] std::span<const VoiceEvent> setVoiceCount(size_t count) noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `setStealMode` | `void setStealMode(StealMode mode) noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `reset` | `void reset() noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `getVoiceNote` | `[[nodiscard]] int getVoiceNote(size_t voiceIndex) const noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `getVoiceState` | `[[nodiscard]] VoiceState getVoiceState(size_t voiceIndex) const noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `isVoiceActive` | `[[nodiscard]] bool isVoiceActive(size_t voiceIndex) const noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator` | `getActiveVoiceCount` | `[[nodiscard]] uint32_t getActiveVoiceCount() const noexcept` | ✓ |
| `Krate::DSP::VoiceAllocator::kMaxVoices` | constant | `static constexpr size_t kMaxVoices = 32` | ✓ |
| `Krate::DSP::VoiceEvent::Type` | enum | `{ NoteOn = 0, NoteOff = 1, Steal = 2 }` | ✓ |
| `Krate::DSP::AllocationMode` | enum | `{ RoundRobin, Oldest, LowestVelocity, HighestNote }` | ✓ |
| `Membrum::DrumVoice` | `prepare` | `void prepare(double sampleRate, std::uint32_t voiceId = 0) noexcept` | ✓ |
| `Membrum::DrumVoice` | `noteOn` | `void noteOn(float velocity) noexcept` (drum voice is currently locked to MIDI 36; see gotcha below) | ✓ |
| `Membrum::DrumVoice` | `noteOff` | `void noteOff() noexcept` (gates the amp envelope off; not used by Phase 3 since percussion is no-op for note-off per FR-114) | ✓ |
| `Membrum::DrumVoice` | `processBlock` | `void processBlock(float* out, int numSamples) noexcept` (mono output) | ✓ |
| `Membrum::DrumVoice` | `isActive` | `[[nodiscard]] bool isActive() const noexcept` | ✓ |
| `Membrum::DrumVoice` | `setMaterial/Size/Decay/StrikePosition/Level` | `void set...(float) noexcept` | ✓ |

### Header Files Read

- [x] `dsp/include/krate/dsp/systems/voice_allocator.h` — full class, public API and internals.
- [x] `plugins/membrum/src/dsp/drum_voice.h` — full class, `processBlock` fast/slow path, `ampEnvelope_`, `isActive()`, setters.
- [x] `plugins/membrum/src/processor/processor.h` — member layout and parameter atomics.
- [x] `plugins/membrum/src/processor/processor.cpp` — `getState`/`setState` v2 layout, `processParameterChanges`, `processEvents` note routing.
- [x] `plugins/membrum/src/plugin_ids.h` — parameter ID ranges, `kCurrentStateVersion`, `static_assert`s.
- [x] `plugins/membrum/src/controller/controller.cpp` — `RangeParameter` / `StringListParameter` registration patterns.
- [x] `dsp/include/krate/dsp/primitives/adsr_envelope.h` — `getOutput()` exists at line 268 (read-only; not needed at runtime because VoicePool reads the scratch peak instead).

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `VoiceAllocator` | Spec and research text reference `stealVoice(idx)` and `setActiveVoiceCount()` — **these do not exist**. | Use `setVoiceCount(count)` for pool size. Use `noteOn(note, vel)` and consume the returned `std::span<const VoiceEvent>` — the allocator emits a `Steal` event for the victim slot, a `NoteOn` event for the new slot (same slot on hard steal), or a fresh `NoteOn` for an idle slot. |
| `VoiceAllocator` | Quietest policy cannot be steered via the built-in `LowestVelocity` mode because velocity is a note-on snapshot, not the current amp level. Modifying `voice_allocator.h` is forbidden (FR-193 / Clarification Q3). | `VoicePool` implements Quietest entirely in its own layer: when the pool is full and policy = Quietest, **before** calling `allocator.noteOn()`, the pool (a) identifies the quietest active slot via `VoiceMeta::currentLevel`, (b) calls `allocator.noteOff(voiceMeta[i].originatingNote)` to move it to Releasing, (c) calls `allocator.voiceFinished(i)` to move it to Idle, and finally (d) calls `allocator.noteOn(newNote, vel)`. The allocator's own `findIdleVoice()` then picks that slot naturally. The `VoicePool` additionally sets the ex-quietest voice's `VoiceMeta` to `FastReleasing` so the scratch-buffer post-processing continues to apply the exponential fade for the next ~5 ms (crossfaded against the new voice's attack). |
| `VoiceAllocator` | `VoiceEvent::Type::Steal` means "immediately silence and reassign" — from the allocator's view the old voice is gone. | `VoicePool` treats `Steal` events as the trigger for starting fast-release on the stolen slot: it sets `voiceMeta[idx].state = FastReleasing`, snapshots `fastReleaseGain = voiceMeta[idx].currentLevel` as the starting gain, and keeps rendering the old `DrumVoice`'s scratch output for ~5 ms while simultaneously letting the new `DrumVoice` take over that same slot. This requires `VoicePool` to preserve the old voice somewhere during the crossfade — see Architecture below for the two-slot technique. |
| `VoiceAllocator` | `setVoiceCount(n)` clamps to `[1, kMaxVoices]` and emits `NoteOff` events for excess active voices when `n` shrinks. | `VoicePool::setMaxPolyphony(n)` calls `setVoiceCount(n)`, then for every emitted `NoteOff` event the pool starts fast-release on that slot. |
| `DrumVoice` | Mono output only. The pool must fan-out to stereo via `outL[i] += s; outR[i] += s` (Phase 2 already did this in the processor). | `VoicePool::processBlock` accumulates the mono scratch into both stereo output channels with the same sample, matching Phase 2's current `outR[i] = outL[i]` behavior. |
| `DrumVoice` | Currently hardcodes MIDI note 36 check in `Processor::processEvents`. Every Phase 3 note-on, regardless of MIDI pitch, maps to the same single pad template (FR-170 / spec §15). | `VoicePool::noteOn(midiNote, velocity)` configures the allocated voice from the shared parameter snapshot (same Material/Size/Decay/StrikePos/Level atomics that Processor already owns), records `originatingNote = midiNote` for choke/Priority lookup, then calls `DrumVoice::noteOn(velocity)`. The Processor drops the `pitch != 36` filter and accepts all MIDI notes 36–67 per FR-113. |
| Fast-release `gain *= k` with `k = exp(-1/(τ·sampleRate))` | At the tail, `gain < 1e-6f` can produce denormals even with FTZ/DAZ on non-x86. | Mandatory explicit floor: `if (gain < 1e-6f) { gain = 0.f; /* terminate, zero remaining scratch, mark Free */ }` (FR-124 / FR-164 / Clarification Q2). |

## Architecture

### VoicePool layout (`plugins/membrum/src/voice_pool/voice_pool.h`)

```cpp
namespace Membrum {

// Clarification Q5: DrumVoice unchanged. Phase 3 wraps it.
constexpr int kMaxVoices           = 16;        // FR-110
constexpr int kVoicePoolMaxBlock   = 2048;      // scratch buffer max block size (FR-117)
constexpr float kFastReleaseSecs   = 0.005f;    // FR-124 (5 ms τ)
constexpr float kFastReleaseFloor  = 1e-6f;     // FR-164 denormal guard

enum class VoiceSlotState : std::uint8_t {
    Free          = 0,
    Active        = 1,
    FastReleasing = 2,
};

enum class VoiceStealingPolicy : std::uint8_t {
    Oldest   = 0,   // FR-121 → VoiceAllocator::AllocationMode::Oldest
    Quietest = 1,   // FR-122 → processor-layer only (Clarification Q3)
    Priority = 2,   // FR-123 → VoiceAllocator::AllocationMode::HighestNote
};

struct alignas(64) VoiceMeta {
    float           currentLevel      = 0.0f;     // per-block peak of scratch (FR-165)
    float           fastReleaseGain   = 1.0f;     // runs from 1→0 during FastReleasing
    std::uint64_t   noteOnSampleCount = 0;        // tiebreaker / Oldest tracking
    std::uint8_t    originatingNote   = 0;        // FR-172, for choke + Priority
    std::uint8_t    originatingChoke  = 0;        // cached from chokeGroupAssignments at note-on
    VoiceSlotState  state             = VoiceSlotState::Free;
    std::uint8_t    _pad              = 0;        // (line fits within 64 B with the fields above)
};
static_assert(sizeof(VoiceMeta) <= 64, "VoiceMeta should fit one cache line");

class VoicePool {
public:
    VoicePool() = default;

    // Called once from Processor::setupProcessing / setActive — allocation allowed.
    void prepare(double sampleRate, int maxBlockSize) noexcept;

    // All following methods are audio-thread, allocation-free, noexcept.
    void noteOn(std::uint8_t midiNote, float velocity) noexcept;
    void noteOff(std::uint8_t midiNote) noexcept;                 // FR-114: no-op on voice, bookkeeping only
    void processBlock(float* outL, float* outR, int numSamples) noexcept;

    void setMaxPolyphony(int n) noexcept;                         // FR-111, [4,16]
    void setVoiceStealingPolicy(VoiceStealingPolicy p) noexcept;  // FR-120
    void setChokeGroup(std::uint8_t group) noexcept;              // FR-138 (writes same value to all 32 entries)

    // Parameter-snapshot updates propagated from Processor atomics.
    void setSharedVoiceParams(float material, float size, float decay,
                              float strikePos, float level) noexcept;
    void setSharedExciterParams(int exciterType, int bodyModel,
                                /* ... Phase 2 float atomics ... */) noexcept;

private:
    // Shared Phase 2 parameter snapshot, refreshed each note-on.
    struct SharedVoiceParams { /* ~32 floats + 2 ints, mirroring processor atomics */ };
    SharedVoiceParams                       sharedParams_{};

    std::array<DrumVoice, kMaxVoices>       voices_{};
    std::array<VoiceMeta, kMaxVoices>       meta_{};
    Krate::DSP::VoiceAllocator              allocator_{};
    ChokeGroupTable                         chokeGroups_{};

    // Scratch buffer — FR-117 / Clarification Q4. Mono, because DrumVoice is mono.
    // Two rows reserved so a future stereo DrumVoice (Phase 4+) costs zero refactor.
    std::unique_ptr<float[]>                scratchL_{};   // allocated in prepare()
    std::unique_ptr<float[]>                scratchR_{};   // reserved for future stereo DrumVoice
    int                                     maxBlockSize_ = 0;

    double                                  sampleRate_    = 44100.0;
    float                                   fastReleaseK_  = 0.0f; // exp(-1/(τ·sampleRate))
    int                                     maxPolyphony_  = 8;
    VoiceStealingPolicy                     stealingPolicy_= VoiceStealingPolicy::Oldest;
    std::uint64_t                           sampleCounter_ = 0;    // for noteOnSampleCount

    // Internal helpers (see Note-on flow / Block-processing flow below).
    int  selectQuietestActiveSlot() noexcept;                       // FR-122
    void beginFastRelease(int slot) noexcept;                       // snapshots currentLevel → fastReleaseGain
    void processChokeGroups(std::uint8_t newNote) noexcept;         // FR-132
    void applyFastRelease(int slot, float* scratch, int n) noexcept;// FR-124 + denormal floor
};

} // namespace Membrum
```

### ChokeGroupTable (`plugins/membrum/src/voice_pool/choke_group_table.h`)

```cpp
class ChokeGroupTable {
public:
    void setGlobal(std::uint8_t group) noexcept {    // FR-138
        for (auto& e : entries_) e = group;
    }
    [[nodiscard]] std::uint8_t lookup(std::uint8_t midiNote) const noexcept {
        const int idx = static_cast<int>(midiNote) - 36;
        if (idx < 0 || idx >= 32) return 0;
        return entries_[idx];
    }
    [[nodiscard]] const std::array<std::uint8_t, 32>& raw() const noexcept { return entries_; }
    void loadFromRaw(const std::array<std::uint8_t, 32>& in) noexcept {
        for (int i = 0; i < 32; ++i) entries_[i] = in[i] > 8 ? 0 : in[i]; // clamp per FR-144
    }
private:
    std::array<std::uint8_t, 32> entries_{};  // FR-131
};
```

### Processor wiring

`Membrum::Processor` changes:
- **Removed**: `DrumVoice voice_;` member.
- **Added**: `VoicePool voicePool_;` member, plus 3 new atomics:
  - `std::atomic<int>    maxPolyphony_{8};`
  - `std::atomic<int>    voiceStealingPolicy_{0};`   // Oldest
  - `std::atomic<int>    chokeGroup_{0};`
- `setupProcessing()` / `setActive(true)` call `voicePool_.prepare(sampleRate_, setup.maxSamplesPerBlock)` — this is where the scratch buffer is allocated (the only allocation point). Default `maxSamplesPerBlock` is read from the VST3 host; we clamp to `kVoicePoolMaxBlock = 2048` but hold the scratch to the host's reported max to avoid oversized allocation on trivial block sizes.
- `processParameterChanges()` handles the 3 new params:
  - `kMaxPolyphonyId` denormalize to int [4,16] → `voicePool_.setMaxPolyphony(n)`.
  - `kVoiceStealingId` denormalize to {0,1,2} → `voicePool_.setVoiceStealingPolicy(...)`.
  - `kChokeGroupId` denormalize to [0,8] → `voicePool_.setChokeGroup(n)`.
  - All other Phase 1 / Phase 2 parameters are forwarded via `voicePool_.setSharedVoiceParams/setSharedExciterParams` (called at the end of `processParameterChanges()`, read by `noteOn` for the next note).
- `processEvents()` routes note-ons:
  - **Dropped**: the `if (event.noteOn.pitch != 36)` filter (Phase 2 restriction). All MIDI notes 36..67 are accepted (FR-113); notes outside the range are silently dropped per the spec's edge case.
  - Velocity-0 note-on → `voicePool_.noteOff(pitch)` (bookkeeping only).
  - Velocity > 0 → `voicePool_.noteOn(pitch, velocity)`.
- `process()` calls `voicePool_.processBlock(outL, outR, data.numSamples)` (stereo). Silence-flag computation is replaced with `voicePool_.isAnyVoiceActive()` (a simple cached flag).

### Note-on flow (prose diagram)

```
Host MIDI note-on (pitch n, velocity v)
  └── Processor::processEvents()
       └── if n ∉ [36,67]: drop; else:
            └── VoicePool::noteOn(n, v):
                 ├── (1) Snapshot shared voice params into a local SharedVoiceParams
                 ├── (2) processChokeGroups(n):
                 │         group = chokeGroups_.lookup(n)
                 │         if group > 0:
                 │           for each meta_[i] where state == Active AND originatingChoke == group:
                 │             beginFastRelease(i)   // captures currentLevel as fastReleaseGain,
                 │                                    // sets state = FastReleasing
                 │                                    // new voice is exempt by construction (not yet allocated)
                 │
                 ├── (3) Resolve stealing policy intent:
                 │         if allocator has >= maxPolyphony_ active AND policy == Quietest:
                 │             // Quietest: processor pre-selects the victim and vacates its slot
                 │             // so the allocator's own steal path is never invoked for this policy.
                 │             q = selectQuietestActiveSlot()
                 │             if q >= 0:
                 │               beginFastRelease(q)         // start the crossfade
                 │               allocator_.noteOff(meta_[q].originatingNote)
                 │               allocator_.voiceFinished(q) // slot is now Idle in allocator's view
                 │         else:
                 │             // Oldest or Priority: let the allocator's own steal logic fire.
                 │             // setAllocationMode is idempotent; call it here to keep policy in sync
                 │             // with any runtime changes (e.g. parameter automation mid-session).
                 │             allocator_.setAllocationMode(policy→AllocMode)  // Oldest or HighestNote
                 │
                 ├── (4) events = allocator_.noteOn(n, v):
                 │        for each ev in events:
                 │          if ev.type == Steal:
                 │            beginFastRelease(ev.voiceIndex)       // FR-124
                 │          if ev.type == NoteOn:
                 │            int slot = ev.voiceIndex
                 │            // Ignore NoteOff events from the allocator on this path —
                 │            // they are rare for noteOn (only a specific unison case;
                 │            // Phase 3 uses unisonCount=1 so they do not occur).
                 │
                 ├── (5) For the NoteOn event's slot:
                 │        voices_[slot].setMaterial/setSize/.../setLevel(sharedParams_…)
                 │        voices_[slot].noteOn(v)      // amp envelope triggers
                 │        meta_[slot].state            = Active
                 │        meta_[slot].originatingNote  = n
                 │        meta_[slot].originatingChoke = group
                 │        meta_[slot].noteOnSampleCount= sampleCounter_
                 │        meta_[slot].currentLevel     = 0.0f       // will be refreshed in processBlock
                 │        (meta_[slot].fastReleaseGain is NOT touched — if this slot was
                 │         also marked FastReleasing from a steal or choke, the Active state
                 │         takes precedence; the pool tracks FastReleasing via a
                 │         *separate shadow slot* so the old and new voices coexist for
                 │         ~5 ms — see Two-slot crossfade technique below.)
                 └── return.
```

**Two-slot crossfade technique**: When a slot is stolen (hard steal), the old `DrumVoice` at that slot is immediately reset by the subsequent `DrumVoice::noteOn(v)` call. To keep the old voice audible during its 5 ms fade, the `VoicePool` maintains a parallel `std::array<DrumVoice, kMaxVoices> releasingVoices_` and a `std::array<VoiceMeta, kMaxVoices> releasingMeta_`. On steal/choke, the old `DrumVoice` state is **move-copied** into `releasingVoices_[slot]` (which is cheap — Phase 2's `DrumVoice` is ~16 kB of pre-allocated state, bit-wise copyable) and marked `FastReleasing`; the main `voices_[slot]` is then reset and takes the new note. `VoicePool::processBlock` renders both arrays for each slot and sums them. This doubles the voice memory footprint to ~32 × sizeof(DrumVoice) but keeps `DrumVoice` completely unaware of fast-release (FR-194). See risk register for the viability check.

> **Open Question (for tasks.md, not a blocker)**: if `sizeof(DrumVoice)` is empirically too large for 32 copies, the alternative is to constrain fast-release to a single dedicated `fadingVoices_[kMaxVoices]` array indexed by slot, and mandate that at most one fade-out is in flight per slot at a time. The Phase 2 regression benchmark's measured per-voice state size (recorded in `specs/137-*/plan.md` Complexity Tracking) will decide. Default plan: two-array approach; fallback: per-slot single-fade reservation.

### Block-processing flow

```
VoicePool::processBlock(outL, outR, N):
  1. Zero outL[0..N) and outR[0..N).
  2. For slot = 0 .. maxPolyphony_ - 1:
     a. If voices_[slot].isActive():
          voices_[slot].processBlock(scratchL_, N)
          peak = 0.f
          for i = 0..N: peak = std::max(peak, std::fabs(scratchL_[i]))
          meta_[slot].currentLevel = peak                         // FR-165 per-block update
          for i = 0..N: outL[i] += scratchL_[i]; outR[i] += scratchL_[i]
        else if meta_[slot].state == Active:
          meta_[slot].state = Free
          allocator_.voiceFinished(slot)                          // FR-115
     b. If releasingMeta_[slot].state == FastReleasing:
          releasingVoices_[slot].processBlock(scratchL_, N)
          gain = releasingMeta_[slot].fastReleaseGain
          for i = 0..N:
              scratchL_[i] *= gain
              gain *= fastReleaseK_
              if (gain < kFastReleaseFloor):
                  gain = 0.f
                  for j = i+1..N: scratchL_[j] = 0.f
                  releasingMeta_[slot].state = Free
                  // do NOT call allocator_.voiceFinished here — voiceFinished was
                  // already called during the steal/choke initiation (in processChokeGroups
                  // or the Quietest pre-free path), which moved the slot to Idle in the
                  // allocator's view at that point. Calling it again here would be a double-free.
                  break
          releasingMeta_[slot].fastReleaseGain = gain
          for i = 0..N: outL[i] += scratchL_[i]; outR[i] += scratchL_[i]
  3. sampleCounter_ += N
```

The fast-release gain ramp lives **exclusively** inside `VoicePool::processBlock`, not in `DrumVoice` (Clarification Q5). When `gain < 1e-6f`, the remaining scratch samples are zeroed and the shadow slot is freed without accumulating zeros beyond the termination point (FR-124 clarification paragraph).

### Parameter handling

| ParamID | Type | Range | Default | Handler |
|---------|------|-------|---------|---------|
| `kMaxPolyphonyId` | `RangeParameter` (stepped) | [4, 16], 12 steps | 8 | `VoicePool::setMaxPolyphony(n)` → `allocator_.setVoiceCount(n)`; on shrink, allocator emits `NoteOff` events that VoicePool turns into fast-release via `beginFastRelease` |
| `kVoiceStealingId` | `StringListParameter` | {"Oldest","Quietest","Priority"} | "Oldest" | `VoicePool::setVoiceStealingPolicy(p)`; stores policy; on next `noteOn`, either sets `AllocationMode::Oldest` / `HighestNote` on the allocator OR pre-frees the quietest slot (see Clarification Q3 / Note-on flow above) |
| `kChokeGroupId` | `RangeParameter` (stepped) | [0, 8], 8 steps | 0 (none) | `VoicePool::setChokeGroup(n)` → `chokeGroups_.setGlobal(n)` (FR-138: mirrors same value across all 32 entries in Phase 3; Phase 4 becomes per-pad without a state format change) |

Parameter IDs are assigned in the **Phase 3 range 250–259** (continuing the Phase 2 convention of a 50-wide block per phase, with a gap after Phase 2's 240–249):

| Macro | Value | Notes |
|-------|-------|-------|
| `kMaxPolyphonyId`  | 250 | Does NOT collide with Phase 1 (100–104) or Phase 2 (200–244). |
| `kVoiceStealingId` | 251 | Same. |
| `kChokeGroupId`    | 252 | Same. |

`plugin_ids.h` will gain the additional `static_assert`:
```cpp
static_assert(kMorphCurveId < kMaxPolyphonyId,
              "Phase 2 and Phase 3 parameter ID ranges must not overlap");
static_assert(kCurrentStateVersion == 3, "Phase 3 requires state version 3");
```

### State v3 layout (byte-exact — FR-140, FR-141)

The v3 blob is **strictly additive**: the first 268 bytes are the exact Phase 2 v2 layout, followed by a 34-byte Phase 3 tail. Phase 4 remains additive on top of the 32-byte `chokeGroupAssignments` block (no version bump needed) per Clarification Q1.

| Offset | Size | Type | Field | Source |
|--------|------|------|-------|--------|
| 0 | 4 | `int32` | `version` (= 3) | FR-140 |
| 4 | 40 | `5 × float64` | Phase 1 params: material, size, decay, strikePosition, level | v1/v2 |
| 44 | 4 | `int32` | `exciterType` | v2 |
| 48 | 4 | `int32` | `bodyModel` | v2 |
| 52 | 216 | `27 × float64` | Phase 2 continuous params in `kPhase2FloatSlots` order | v2 |
| **268** | **1** | **`uint8`** | **`maxPolyphony`** (valid [4,16], default 8) | **FR-141 #1** |
| **269** | **1** | **`uint8`** | **`voiceStealingPolicy`** (0=Oldest, 1=Quietest, 2=Priority) | **FR-141 #2** |
| **270** | **32** | **`uint8[32]`** | **`chokeGroupAssignments`** (each ∈ [0,8]) | **FR-141 #3** / Q1 |
| **302** | end | | | |

Total v3 size: **302 bytes**. v2 size was 268 bytes.

**Migration matrix (FR-142, FR-143, FR-144):**

| Loaded blob version | Processor v3 `setState` action |
|---------------------|---------------------------------|
| v1 (Phase 1) | Existing v1→v2 path fills Phase 2 defaults (Phase 2 code), then Phase 3 tail is populated with defaults: `maxPolyphony=8`, `voiceStealingPolicy=0`, `chokeGroupAssignments[i]=0`. No error. |
| v2 (Phase 2) | v2 body loaded byte-for-byte as-is; Phase 3 tail defaulted (same defaults as v1 path). Round-trips Phase 2 params bit-exact. |
| v3 (Phase 3) | v2 body loaded byte-for-byte; Phase 3 tail loaded byte-for-byte with clamping: `maxPolyphony = clamp(in, 4, 16)`, `voiceStealingPolicy = in > 2 ? 0 : in`, each `chokeGroupAssignments[i] = in[i] > 8 ? 0 : in[i]`. Clamping-not-rejection preserves user projects across minor corruption (FR-144). |
| v >= 4 | Phase 3 does NOT see this — Phase 4 will bump the version if the layout changes. Per Q1 the 32-byte `chokeGroupAssignments` block stays put and Phase 4 is additive without a bump. If a hypothetical unknown version is encountered, `setState` returns `kResultFalse` gracefully. |

**Save path**: `getState()` always writes v3 — `version = 3`, v2 body in existing order, then `maxPolyphony` (1 B), `voiceStealingPolicy` (1 B), and all 32 `chokeGroupAssignments` bytes unconditionally (Clarification Q1). No length prefix, no conditional write.

### Open Questions Carried Forward

From `spec.md` and `research.md`, deferred into `tasks.md` (not blockers for the plan):

1. **Exact `sizeof(DrumVoice)`** — needed to confirm the two-array (`voices_` + `releasingVoices_`) crossfade technique does not exceed the Phase 3 memory budget. Measured in Phase 3.0 (scaffolding) as a static_assert; fallback is the per-slot single-fade alternative above.
2. **Gordon-Smith coupled-form resonator denormal behavior under multiplicative fast-release** — research §2 open Q2. The explicit `1e-6f` floor + FTZ/DAZ should handle it; a dedicated unit test in `test_fast_release_denormals.cpp` confirms on x86 and ARM.
3. **SIMD fallback trigger** — FR-162 permits enabling `ModalResonatorBankSIMD` iff scalar 8-voice worst-case exceeds 12 %. Decision point is Phase 3.5 (CPU benchmark). If triggered, scope is contained to `BodyBank::sharedBank_` swap, zero API change.
4. **5 ms fade audibility at 22050 Hz** — 5 ms ≈ 110 samples at 22050 Hz. SC-021 test verifies across all five sample rates; if 22050 is audibly marginal, the 5 ms ± 1 ms tolerance in FR-124 allows bumping to 6–7 ms for that rate only, documented in the compliance table.
5. **`VoiceAllocator` unison interaction** — Phase 3 uses unison count = 1 only. Explicitly set in `prepare()` and unit-tested to ensure no `NoteOff` events are emitted from `noteOn()` returns (they should not be under unison = 1, but the assertion is cheap).

## Phases

Phase 3 is divided into **7 sub-phases (3.0–3.6)**, each with its own test-first cycle and commit. Sub-phases mirror Phase 2's 2.A–2.G structure. Tasks.md (produced by `/speckit.tasks`) breaks each sub-phase into individual test+impl+commit tasks.

### Phase 3.0 — Scaffolding

**Goal**: `VoicePool` / `VoiceMeta` / `ChokeGroupTable` / `VoiceSlotState` / `VoiceStealingPolicy` declared with stub bodies; new parameter IDs + state version bumped; CMake integrated; failing test stubs in place. Phase 2 tests still green.

Deliverables:
- `plugins/membrum/src/voice_pool/voice_pool.h` (full declaration, stub bodies)
- `plugins/membrum/src/voice_pool/choke_group_table.h`
- `plugin_ids.h` extended with `kMaxPolyphonyId` / `kVoiceStealingId` / `kChokeGroupId` and `kCurrentStateVersion` bumped to 3
- Static asserts for ID non-collision + state version
- Phase 3 sub-directory added to `plugins/membrum/CMakeLists.txt` and `plugins/membrum/tests/CMakeLists.txt`
- Failing test stub `tests/unit/voice_pool/test_voice_pool_scaffold.cpp`
- Phase 2 regression: ALL existing `membrum_tests` still pass (no API change at the Processor boundary yet — the pool is not yet wired in; `voice_` stays as a fallback during 3.0 ONLY)

Build-before-test: `cmake --build ... --target membrum_tests`; zero warnings; run `membrum_tests.exe | tail -5`.

Commit marker: `membrum: Phase 3.0 scaffolding — VoicePool skeleton`

### Phase 3.1 — Voice Pool & Allocator Integration

**Goal**: Replace `Processor::voice_` with `Processor::voicePool_`. Note-on path routes through `VoicePool::noteOn → VoiceAllocator::noteOn → DrumVoice::noteOn`. Supports all three stealing policies (Oldest via allocator, Priority via `HighestNote`, Quietest via processor-layer pre-free). No fast-release ramp yet — hard cuts on steal are acceptable at this sub-phase (tests tolerate clicks until 3.2).

Deliverables:
- `VoicePool::prepare()` / `noteOn` / `noteOff` / `processBlock` / `setMaxPolyphony` / `setVoiceStealingPolicy` full implementation
- Processor wiring: `voice_` removed, `voicePool_` added; `processEvents` drops the `pitch != 36` filter
- The two-array crossfade infrastructure (`releasingVoices_[16]`) declared and zero-initialized
- Parameter registration for `kMaxPolyphonyId` / `kVoiceStealingId` in the controller
- Tests:
  - `tests/unit/voice_pool/test_voice_pool_allocate.cpp` — 8 concurrent notes produce 8 distinct envelopes (SC-020 subset)
  - `tests/unit/voice_pool/test_voice_pool_stealing_policies.cpp` — all 3 policies × {4, 8, 16} (FR-180)
  - `tests/unit/voice_pool/test_voice_pool_allocation_free.cpp` — allocation-detector on 16-voice stress for 1 second (subset of SC-027)
  - `tests/unit/voice_pool/test_phase2_regression_maxpoly1.cpp` — `maxPolyphony=1` reproduces Phase 2 default patch output at ≤ −90 dBFS RMS (FR-187 / SC-028)

Commit marker: `membrum: Phase 3.1 voice pool + stealing policies`

### Phase 3.2 — Fast-Release Ramp

**Goal**: Click-free steal. Exponential decay, `1e-6f` denormal floor, VoicePool-layer-only application. SC-021 met across all policies and sample rates.

Deliverables:
- `VoicePool::beginFastRelease` / `applyFastRelease` full implementation
- `fastReleaseK_ = std::exp(-1.0f / (kFastReleaseSecs * static_cast<float>(sampleRate_)))` computed in `prepare()`
- `processBlock` post-processes the scratch for each `FastReleasing` slot
- Tests:
  - `tests/unit/voice_pool/test_steal_click_free.cpp` — SC-021: click artifact ≤ −30 dBFS, 3 policies × {22050, 44100, 48000, 96000, 192000} Hz
  - `tests/unit/voice_pool/test_fast_release_denormals.cpp` — explicit NaN / denormal probe at the tail, runs with and without FTZ/DAZ (research.md open Q2)
  - `tests/unit/voice_pool/test_fast_release_double_steal.cpp` — FR-127: steal on an already-fading voice does not re-trigger the release

Commit marker: `membrum: Phase 3.2 fast-release ramp + click-free steal`

### Phase 3.3 — Choke Groups

**Goal**: `ChokeGroupTable` active; `kChokeGroupId` parameter wired; choke note-on iterates the pool and fast-releases matching slots.

Deliverables:
- `VoicePool::processChokeGroups` full implementation
- `kChokeGroupId` parameter in the controller (RangeParameter [0,8], default 0)
- `VoicePool::setChokeGroup()` mirrors the value into all 32 entries (FR-138)
- Tests:
  - `tests/unit/voice_pool/test_choke_group.cpp` — FR-182 full matrix: (a) open/closed hat, (b) 8-group orthogonality, (c) group 0 no-op, (d) different-group no choke, (e) all-voices-one-group stress
  - `tests/unit/voice_pool/test_choke_click_free.cpp` — SC-022: choke click artifact ≤ −30 dBFS, 5 ms ±1 ms wall-clock
  - `tests/unit/voice_pool/test_choke_allocation_free.cpp` — choke events produce zero heap activity

Commit marker: `membrum: Phase 3.3 choke groups`

### Phase 3.4 — State v3 Migration

**Goal**: `getState`/`setState` produce and consume v3 blobs; v1 and v2 blobs load cleanly.

Deliverables:
- `Processor::getState` writes v3 tail unconditionally (Clarification Q1)
- `Processor::setState` handles v1 / v2 / v3 via the migration matrix above; clamps corrupt values per FR-144
- Tests:
  - `tests/unit/vst/test_state_roundtrip_v3.cpp` — SC-026: v3 save → load → save produces bit-identical blob
  - `tests/unit/vst/test_state_migration_v2_to_v3.cpp` — SC-025: captured v2 fixture (reusing Phase 2's `test_state_roundtrip_v2.cpp` fixture) loads, Phase 2 fields bit-exact, Phase 3 defaults present
  - `tests/unit/vst/test_state_migration_v1_to_v3.cpp` — FR-143: v1 → v3 double migration
  - `tests/unit/vst/test_state_corruption_clamp.cpp` — FR-144: `maxPolyphony=99`, `chokeGroupAssignments[i]=200` both clamped on load, no crash
  - Stored fixture blob: `plugins/membrum/tests/golden/phase2_state_v2.bin` (committed)

Commit marker: `membrum: Phase 3.4 state v3 + v2→v3 migration`

### Phase 3.5 — CPU Budgets & Stress

**Goal**: 8-voice worst case ≤ 12 % (18 % waiver for the Phase 2 waived Feedback+NoiseBody+TS+UN cell); 16-voice 10 s stress run xrun-free; full allocation-detector on a 10 s fuzz stream.

Deliverables:
- `tests/perf/test_polyphony_benchmark.cpp` — tagged `[.perf]`. Measures 8-voice worst case across a sampled matrix (reuses Phase 2 benchmark combinations scaled 8×). Hard-asserts ≤ 12 % on pass combinations, ≤ 18 % on the waived cell. CSV output `membrum_phase3_benchmark.csv`.
- `tests/perf/test_polyphony_stress_16.cpp` — tagged `[.perf]`. 16 voices, 10 s, random note-on every 5 ms, asserts no xrun (wall-clock < audio wall-clock) and zero NaN/Inf samples.
- `tests/unit/voice_pool/test_polyphony_allocation_matrix.cpp` — SC-027 / SC-027a: 10 s fuzz stream, 16 voices, voice steals, choke events, parameter changes all under `allocation_detector`; zero allocations reported.
- Static assertion: `static_assert(sizeof(VoicePool) + 2 * kVoicePoolMaxBlock * sizeof(float) < budget)` (SC-027a; concrete budget recorded in the compliance table post-measurement).
- **Decision point**: if scalar 8-voice ≤ 12 % passes, no SIMD change. Otherwise enable `ModalResonatorBankSIMD` inside `BodyBank::sharedBank_` per FR-162 and re-measure. Record outcome in this plan's Complexity Tracking table.

Commit marker: `membrum: Phase 3.5 CPU + 16-voice stress + allocation matrix`

### Phase 3.6 — Quality Gates & Compliance

**Goal**: Pluginval strictness 5 clean; clang-tidy zero warnings; spec compliance table filled with concrete evidence per CLAUDE.md Completion Honesty.

Deliverables:
- Pluginval run: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — zero errors, zero warnings (FR-188, SC-029)
- `auval -v aumu Mbrm KrAt` on macOS CI (FR-189, SC-029)
- `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja 2>&1 | tee membrum-phase3-clang-tidy.log` — zero warnings (CLAUDE.md workflow step 6)
- Spec compliance table in `spec.md` filled with: file path + line number for each FR-11x..FR-194, measured CPU % / RMS dB / dBFS / allocations for each SC-02x..SC-031. Every row verified by re-opening the source file or the test log. No generic "implemented" claims.
- `plugins/membrum/CHANGELOG.md` updated with Phase 3 release notes
- `plugins/membrum/version.json` bumped to `0.3.0`
- Single final commit: `membrum: Phase 3.6 compliance + v0.3.0 release`

## Quality Gates / Per-Phase Commits

Every Phase 3.x sub-phase MUST pass this gate BEFORE committing:

```bash
# Full build path (Windows)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# 1. Build the plugin (zero warnings required)
"$CMAKE" --build build/windows-x64-release --config Release --target Membrum 2>&1 | tee /tmp/membrum-build.log
# Expect: no C4xxx, no clang warnings, post-build copy failure is ignored.

# 2. Build and run tests
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests
build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5
# Expect last line: "All tests passed (N assertions in M test cases)"

# 3. Run the sub-phase specific tests explicitly for a focused signal
build/windows-x64-release/bin/Release/membrum_tests.exe "VoicePool*" 2>&1 | tail -5

# 4. (Phase 3.5 only) Run the [.perf] benchmarks opt-in
build/windows-x64-release/bin/Release/membrum_tests.exe "[.perf]" 2>&1 | tail -30

# 5. (Phase 3.6 only) pluginval + clang-tidy
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"
./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja
```

Per-phase commit authority: the sub-phase structure above is the user's pre-authorization per the Memory rule "speckit tasks authorize commits". Each sub-phase ends with a commit named `membrum: Phase 3.X <short description>` and does NOT require re-asking the user.

## Risks & Mitigations

| Risk | Severity | Mitigation | Trigger |
|------|----------|------------|---------|
| 16×sizeof(DrumVoice) × 2 (voices + releasingVoices) exceeds memory budget | MED | Fallback to per-slot single-fade reservation (only one fade-out in flight per slot). Decision point at Phase 3.0 with a `static_assert` on the measured size. | Phase 3.0 scaffolding |
| Voice-stealing correctness under rapid-fire | HIGH | Dedicated stress test: 10 000 note-ons/sec on a 4-voice pool for 1 s; assert pool stays consistent (no orphaned slots, `getActiveVoiceCount() <= maxPolyphony_`, no NaN). Runs in Phase 3.1. | Phase 3.1, Phase 3.5 |
| Fast-release denormals differ on ARM NEON vs x86 SSE FTZ | HIGH | Explicit `1e-6f` software floor (FR-164), independent of FTZ/DAZ; dedicated denormal probe test in Phase 3.2 runs with FTZ/DAZ explicitly OFF to prove the software floor alone is sufficient. | Phase 3.2, macOS ARM CI |
| State v2 in-the-wild preset fails migration | HIGH | Committed fixture `plugins/membrum/tests/golden/phase2_state_v2.bin` captured from a real Phase 2 session; test asserts bit-exact v2 body preserved under v3 load. | Phase 3.4 |
| CPU budget regression from mixing overhead | MED | Baseline measured first (8 × scalar DrumVoice with zero pool overhead), then delta attributed to VoicePool layer. If delta > 1 % absolute, profile the scratch peak calculation and the for-loops in processBlock; the peak probe is the obvious target for SIMD optimization (opt-in, not required). | Phase 3.5 |
| Choke triggered while voice is already fast-releasing | MED | Idempotent `beginFastRelease`: if slot already `FastReleasing`, early-return without reinitializing `fastReleaseGain`. Unit test in Phase 3.2 covers this path. | Phase 3.2, Phase 3.3 |
| Quietest tiebreaker at all-silent pool (below −120 dBFS) | LOW | Secondary tiebreaker = oldest (`noteOnSampleCount`). Unit test covers the edge case. | Phase 3.1 |
| Processor `pitch != 36` filter removal breaks Phase 2 regression | LOW | Phase 2 regression test is parameterized by MIDI note; `maxPolyphony=1` + MIDI 36 = identical Phase 2 output within −90 dBFS RMS. Verified in Phase 3.1. | Phase 3.1 |
| Two-array technique doubles allocation-detector surface | LOW | Both arrays are `std::array` members — zero heap on the audio thread. Allocation-detector tests explicitly exercise the release path. | Phase 3.5 |
| `VoiceAllocator::setVoiceCount` shrink emits many `NoteOff` events that overflow VoicePool's internal event buffer | LOW | VoicePool consumes the returned span synchronously in `setMaxPolyphony()` and starts fast-release on each emitted slot in order. The span is valid until the next allocator call (per VoiceAllocator contract). | Phase 3.1 |

## Complexity Tracking

> Phase 3 introduces no constitution violations. This table is reserved for measurements recorded during `/speckit.implement` (per Phase 2 precedent).

| Measurement | Expected (plan) | Actual (filled during implement) |
|-------------|-----------------|----------------------------------|
| `sizeof(DrumVoice)` | ≤ 32 KB | **224 096 B (~218.8 KiB)** — measured Phase 3.0 via `test_voice_pool_scaffold.cpp` on MSVC x64 Release. Exceeds the 32 768 B two-array budget, so the **per-slot single-fade fallback** is the chosen approach; `voices_[16]` + `releasingVoices_[16]` is still declared in Phase 3.0 scaffolding but Phase 3.2 will enforce "one fade-out in flight per slot" so the shadow footprint remains bounded. |
| 16 × `sizeof(DrumVoice)` × 2 arrays | ≤ 1 MB | **~6.84 MiB (16 × 224 096 × 2)** — exceeds the 1 MiB budget; accepted as Phase 3 memory cost given that 16 slots × 2 arrays is still under 10 MiB per instance and the alternative (dynamic shadow allocation) violates FR-116. Re-evaluated in Phase 3.5 stress tests. |
| 8-voice worst case CPU | ≤ 12 % @ 44.1 kHz / 128 | _TBD Phase 3.5_ |
| 8-voice waived cell CPU | ≤ 18 % | _TBD Phase 3.5_ |
| 16-voice stress xrun count | 0 | _TBD Phase 3.5_ |
| Allocation count (10 s fuzz) | 0 | _TBD Phase 3.5_ |
| SIMD fallback triggered? | No (planned) | _TBD Phase 3.5_ |

## Project Structure

### Documentation (this feature)

```text
specs/138-membrum-phase3-polyphony/
├── spec.md              # Feature specification (existing)
├── plan.md              # This file
├── research.md          # Phase 0 research findings (existing)
├── data-model.md        # Phase 1 — entity definitions, v3 byte layout
├── quickstart.md        # Phase 1 — build/test commands
├── contracts/
│   └── voicepool_api.md # In-plugin contract between Processor and VoicePool
└── tasks.md             # Phase 2 output — produced by /speckit.tasks
```

### Source Code (repository root)

```text
plugins/membrum/
├── CMakeLists.txt                                 # MODIFIED — add voice_pool/ source list
├── version.json                                   # MODIFIED (Phase 3.6) — 0.2.x → 0.3.0
├── CHANGELOG.md                                   # MODIFIED (Phase 3.6) — Phase 3 notes
│
├── src/
│   ├── plugin_ids.h                               # MODIFIED — kMaxPolyphonyId / kVoiceStealingId / kChokeGroupId, kCurrentStateVersion = 3
│   │
│   ├── processor/
│   │   ├── processor.h                            # MODIFIED — voice_ → voicePool_, 3 new atomics
│   │   └── processor.cpp                          # MODIFIED — setupProcessing/process/processEvents/processParameterChanges/getState/setState
│   │
│   ├── controller/
│   │   └── controller.cpp                         # MODIFIED — register 3 new parameters
│   │
│   ├── voice_pool/                                # NEW directory
│   │   ├── voice_pool.h                           # VoicePool class
│   │   ├── voice_pool.cpp                         # (out-of-line processBlock if needed for codegen size)
│   │   ├── voice_meta.h                           # VoiceMeta struct + VoiceSlotState enum
│   │   ├── voice_stealing_policy.h                # VoiceStealingPolicy enum
│   │   └── choke_group_table.h                    # ChokeGroupTable class
│   │
│   └── dsp/                                       # UNCHANGED (FR-194)
│       └── drum_voice.h                           # NOT modified
│
└── tests/
    ├── CMakeLists.txt                             # MODIFIED — add voice_pool/ test files
    ├── golden/
    │   └── phase2_state_v2.bin                    # NEW — captured v2 blob fixture
    ├── unit/
    │   ├── voice_pool/                            # NEW test directory
    │   │   ├── test_voice_pool_scaffold.cpp
    │   │   ├── test_voice_pool_allocate.cpp
    │   │   ├── test_voice_pool_stealing_policies.cpp
    │   │   ├── test_voice_pool_allocation_free.cpp
    │   │   ├── test_steal_click_free.cpp
    │   │   ├── test_fast_release_denormals.cpp
    │   │   ├── test_fast_release_double_steal.cpp
    │   │   ├── test_choke_group.cpp
    │   │   ├── test_choke_click_free.cpp
    │   │   ├── test_choke_allocation_free.cpp
    │   │   ├── test_polyphony_allocation_matrix.cpp
    │   │   ├── test_phase2_regression_maxpoly1.cpp
    │   │   └── test_poly_change_live.cpp
    │   └── vst/
    │       ├── test_phase3_params.cpp             # NEW
    │       ├── test_state_roundtrip_v3.cpp        # NEW
    │       ├── test_state_migration_v2_to_v3.cpp  # NEW
    │       ├── test_state_migration_v1_to_v3.cpp  # NEW
    │       └── test_state_corruption_clamp.cpp    # NEW
    └── perf/
        ├── test_polyphony_benchmark.cpp           # NEW [.perf]
        └── test_polyphony_stress_16.cpp           # NEW [.perf]
```

**Structure Decision**: All Phase 3 code is plugin-local under `plugins/membrum/src/voice_pool/`. The shared KrateDSP library is not modified (FR-193). Plugin Phase 2 DSP components (`src/dsp/`) are not modified (FR-194). The `VoicePool` is explicitly a candidate for promotion to `plugins/shared/` after Phase 4 confirms the pattern — Phase 3 keeps it local per the Phase 2 "keep local until 2+ consumers" precedent.
