# Feature Specification: Profundum Phase 4 — Drop & Note Behaviour

**Spec slug:** `profundum-phase4-drop-note-behaviour`
**Status:** Clarified (2026-10-12): the eight clarify-scan questions are decided and encoded (see Clarifications).
Decisions D-2, D-4, D-5, D-6, D-8 and D-12 are ratified by that session; the other decisions stand as drafted.
**Roadmap source:** `specs/Profundum-roadmap.md` → Part A → **Phase 4** (lines 353–382). Supporting roadmap statements
this spec traces to:
- **Architecture Overview:** "DROP (pitch exciter) · amp envelope · voice sum (+ Anchor)" (line 91).
- **Key Design Decision 3:** "MONO: one active voice, a new note replaces the previous (legato, retrigger, glide, note
  priority)" (lines 131–137). It also says "Every voice owns its oscillator state, Anchor, amplitude and pitch envelopes".
- **Reuse Inventory, "Pitch & note behaviour" row** (line 165). Its verdict is "🔶 extend `MonoHandler` (constant-rate
  and quantized glide) — append-only, Ruinae green. 🆕 `DropEnvelope` (L2). ✅ `VoiceAllocator` for POLY". It also says
  that Membrum's `PitchSegmentEnvelope` "is plugin-local and stays there".
- **Relationship section:** "The one shared-header change this roadmap plans (`MonoHandler` glide modes, Phase 4) is
  append-only and gated on Ruinae's suites" (lines 183–185).
- **Phase 1 handoff:** "a retrigger phase policy input (reset vs free-running, consumed by Phase 4)" (line 242).
- **Phase 9 consumers.**
  - "Drop on pitch" (line 519).
  - "**MONO** (default) via `MonoHandler` with the Phase 4 note behaviour" (line 524).
  - "voice steal and MONO note replacement clickless" (line 542).
  - Drop is "routed to the core only: the Anchor receives the resolved note pitch before Drop in LOCK / PUNCH / GHOST
    and the core's post-Drop f0 in FOLLOW" (lines 552–554).
- **Phase 8 consumer:** `MorphState` holds "Drop" (line 480), and its "short → long" axis morphs "Drop times
  logarithmic" (line 493). `MorphState` excludes "note behaviour" (line 482).
- **Phase 12 consumer:** the Advanced page shows "Drop amount / time / shape / rebound, amp envelope, note behaviour:
  retrigger, glide mode, note memory" (lines 625–626).
- **Cross-Cutting Constraints** (lines 827–847). "Shared-component changes (`MonoHandler` glide modes …) are
  append-only and must keep Ruinae, Seraphis, Vorago and Innexus suites green" (lines 846–847).
- **Dependency graph:** Phase 4 "has no dependency on the core" (lines 819, 823). This spec still exercises its outputs
  against the shipped `ProfundumCore` and `AnchorOscillator`, because the Phase 3 handoff requires that (below).

**Concept source:** `specs/Profundum-concept.md`.
- §5 "A Pitch Exciter" (lines 261–310) defines DROP with Amount, Time, Shape ("Exponential ↔ linear ↔ logarithmic ↔
  elastic") and REBOUND ("The pitch can briefly overshoot the target pitch before settling"). It names four use cases:
  808 attacks, cinematic bass impacts, techno bass plucks and synthetic kick/bass hybrids, "without turning the
  instrument into an 808 clone".
- §12 "A very useful Note Behavior section" (lines 564–590) defines RETRIGGER ("Oscillator phase behaviour"), GLIDE
  ("rate · constant time · quantized") and NOTE MEMORY ("always retrigger · legato · envelope retrigger only ·
  oscillator continuous").
- §12's LOW NOTE GUARD (line 591) is Phase 1's and has shipped (roadmap line 237; Phase 1 status line 196).

**Upstream contracts consumed (Phase 3 handoff, ratified).** Roadmap Phase 3 status (line 317) says "the FR-066 handoff
clauses are written into Phases 4, 5, 6, 8, 9 and 11". The Phase 4 clause is the Note Memory bullet (roadmap lines
368–372). The relevant clauses of `specs/profundum-phase3-anchor/spec.md` are:
- **FR-006** (lines 241–265): "Click-freedom of a Reset noteOn **on a sounding voice** is Phase 4's", and "Phase 4
  chooses which phase policy is in force and when the re-arm event is issued; this phase implements both".
- **FR-036** (lines 373–379): "The reference is associated with the **resolved note pitch after Glide but before Drop**".
- **FR-048** (lines 437–449): "Phase 4's Note Memory decides **when** the re-arm event is issued".
- **FR-049** (lines 450–464): "Re-locking to h1 after a Drop is Phase 4's / Phase 9's obligation and is not asserted
  here". FR-053 to FR-059 carry it.
- **Phase 3 traceability row** (line 103): Phase 3 hands over "Choosing when `noteOn` is called and **when the re-arm
  event is issued** (legato, retrigger, overlapping notes, voice reuse; Note Memory), and producing the glide / Drop
  trajectories".

**Layer:** four new components and one append-only extension, all header-only in `Krate::DSP`:
- `DropEnvelope` (L2, `processors/drop_envelope.h`), named by the roadmap;
- `NoteMemory` (L2, `processors/note_memory.h`), the Note Memory / Retrigger policy resolver;
- `RetriggerDeclick` (L1, `primitives/retrigger_declick.h`), the click-free Reset mechanism (D-8);
- `DropPhaseLock` (L2, `processors/drop_phase_lock.h`), the shipped corrector that lands the core-h1 / Anchor phase
  inside the Drop (FR-055);
- `MonoHandler` glide modes (L2, `processors/mono_handler.h`), append-only.

The roadmap names a class only for `DropEnvelope`. The other three names are this spec's proposals (D-7, D-8, FR-055);
pass the ODR sweep.

---

## Overview

Phase 4 delivers the pitch exciter and the note behaviour that every bass voice needs. Phase 9 consumes both.

- **`DropEnvelope`** produces a per-sample pitch offset in semitones.
  - The offset starts **Amount** semitones above the note and falls to the note over **Time**.
  - The fall follows a continuously variable **Shape**: exponential ↔ linear ↔ logarithmic ↔ elastic.
  - **Rebound** adds a brief overshoot below the note before the offset settles at 0.
  - The offset is C¹ everywhere, including across a retrigger, which continues from the current value and slope.
- **`MonoHandler`** gains a glide-mode selector: **constant-time** (today's behaviour, the default), **constant-rate**
  and **quantized** (semitone-stepped). The change is append-only, and Ruinae's behaviour stays bit-identical.
- **`NoteMemory`** maps each note transition to a set of actions.
  - Inputs: the transition kind (from silence, overlapping, into a release tail, return to a held key, POLY voice
    reuse), the Note Memory mode (always retrigger / legato / envelope retrigger only / oscillator continuous) and the
    Retrigger setting (reset / continue).
  - Actions: the core's and the Anchor's phase policy for this noteOn; whether the amp envelope restarts; whether the
    Drop triggers; whether the Anchor's re-arm event (Phase 3 FR-048) is issued; whether a declick is required.
- **`RetriggerDeclick`** makes a Reset-policy restart on a sounding voice click-free by construction.
  - It applies a C¹ gain dip to zero, and the Reset noteOn takes effect at the sample where the gain reaches zero.
  - It reports how many samples remain until that point, so the caller can split its block there.
- **`DropPhaseLock`** composes after `DropEnvelope` and emits the corrected semitone offset per sample. At trigger it
  takes the planned curve description and f_res, and removes the Drop's fractional excess cycle before landing, so core
  h1 and the Anchor meet at |θ| ≤ 1° at landing with no pitch motion after it (FR-053).

The phase reference follows Phase 3.
- The core is fed the resolved pitch multiplied by the Drop ratio.
- The Anchor is fed the resolved pitch before Drop in LOCK / PUNCH / GHOST, and the core's post-Drop trajectory in
  FOLLOW.
- A Drop therefore leaves a phase offset between core h1 and the Anchor: the Drop's excess phase, modulo one cycle.
  `DropPhaseLock` removes that offset inside the Drop, so the Phase 3 convention (0°) holds at landing (FR-053).

## Scope

1. `DropEnvelope` (L2), complete: Amount, Time, Shape and Rebound; C¹ output; retrigger with continuation; no
   allocation; sample-rate and block-partition invariance.
2. The `MonoHandler` glide-mode extension, append-only. Constant-time stays the default and stays bit-identical to
   today. Constant-rate and quantized are added.
3. The `NoteMemory` resolver: four modes × two Retrigger settings × the transition kinds, as a documented and tested
   matrix of actions.
4. `RetriggerDeclick`: click-free Reset restarts on a sounding voice, in MONO note replacement and in POLY voice reuse.
5. The phase-reference and routing contract for Drop and Glide toward `ProfundumCore` and `AnchorOscillator`, including
   the shipped `DropPhaseLock` corrector that lands the post-Drop phase inside the Drop (FR-053 to FR-059). The routing
   and the declick scheduling are proven by a test-only composition (FR-050 series).
6. Tests, audibility gates, the four use-case renders, a CPU measurement, and the shared-header regression gates
   (Ruinae, Seraphis, Vorago and Innexus, plus the `MonoHandler` and `PolySynthEngine` suites).

## Non-Goals (owned by later phases, or deliberately excluded)

| Excluded | Owner | Roadmap |
|---|---|---|
| `ProfundumVoice` / `ProfundumEngine`: wiring Drop, glide, Note Memory and the declick into a real voice; POLY allocation through `VoiceAllocator`; the amp-envelope type (`ADSREnvelope` vs `MultiStageEnvelope`) | Phase 9 | 517–529 |
| The voice-level "voice steal and MONO note replacement clickless" gate over the full chain (Shape, Fold, Character, Mass) | Phase 9 | 542 |
| Morphing Drop parameters (laws, `MorphState` field) | Phase 8 | 480, 493 |
| Parameter registration, IDs (400–499 block), normalisation, state | Phases 10, 11 | 575–580, 593–596 |
| UI for Drop and note behaviour | Phase 12 | 625–626 |
| Motion modulation of Drop parameters | Phase 9 (Motion section) | 527–531 |
| Generalising Membrum's `PitchSegmentEnvelope` | Never: the roadmap says it "stays where it is — not generalised speculatively" | 364–365 |
| An upward pitch exciter (negative Amount), velocity → Drop, a second Drop segment | Not in roadmap or concept; not added | — |
| Pitch-bend and tuning-reference handling (`NoteProcessor`) | Phase 9 composes the existing component unchanged | 165 |
| Changes to `ProfundumCore` or `AnchorOscillator` | None planned. The correction lives in `DropPhaseLock`, outside both (FR-053, FR-058). An observer, if one is ever needed, is append-only (FR-063 pattern) | — |

---

## Existing components (verified this session)

Paths are relative to `dsp/include/krate/dsp/` unless stated. Every row was opened and read on 2026-10-12.

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `MonoHandler` | `processors/mono_handler.h:91` | **Extended** (glide modes). Provides note priority, legato, the note stack and portamento for MONO | Layer 2 (line 2). `struct MonoNoteEvent { float frequency; uint8_t velocity; bool retrigger; bool isNoteOn; }` (40–45). `enum class MonoMode : uint8_t { LastNote = 0, LowNote = 1, HighNote = 2 }` (52–56). `enum class PortaMode : uint8_t { Always = 0, LegatoOnly = 1 }` (63–66). `kMaxStackSize = 16`, `kMaxPortamentoTimeMs = 10000.0f` (97–100). `void prepare(double sampleRate) noexcept` (121). `[[nodiscard]] MonoNoteEvent noteOn(int note, int velocity) noexcept` (135). Overlap is detected as `hadNotesHeld = (stackSize_ > 0)` (152). Legato suppresses retrigger only for overlapping notes (176–179). Glide is enabled on overlap always, on staccato only in `PortaMode::Always`, and never on the first note ever (186–199). `[[nodiscard]] MonoNoteEvent noteOff(int note) noexcept` (211); a return to a held note glides, and retriggers unless legato (246–256). `void setPortamentoTime(float ms) noexcept` clamps to [0, 10000] and ignores non-finite input (280–286). `[[nodiscard]] float processPortamento() noexcept` advances one sample and returns Hz (291–295). `void setMode(MonoMode)`, `void setLegato(bool)`, `void setPortamentoMode(PortaMode)` (309, 325, 331). `[[nodiscard]] bool hasActiveNote() const noexcept` (340). `void reset() noexcept` (349). Glide runs in **semitone space** through a `LinearRamp portamentoRamp_` (494). `updatePortamentoTarget` calls `setTarget` when gliding and `snapTo` otherwise (450–457). Frequency is `kA4FrequencyHz · semitonesToRatio(semitones − kA4MidiNote)` (461–463). |
| — `MonoHandler` consumers | `systems/poly_synth_engine.h:130, 255–271, 637–664`; `plugins/ruinae/src/engine/ruinae_engine.h:159, 205` | Regression surface of the append-only change | `PolySynthEngine` forwards `setMode`, `setLegato`, `setPortamentoTime` and `setPortamentoMode`, and calls `noteOn`, `noteOff` and `processPortamento`. `RuinaeEngine` prepares and resets its `monoHandler_`. No other `MonoHandler` user exists in `dsp/` or `plugins/` (grep, 2026-10-12). |
| `LinearRamp` | `primitives/smoother.h:305` | The glide ramp inside `MonoHandler`. It stays the **ConstantTime** law only (FR-021); the new modes do not run on it (FR-022, FR-023, D-13) | `void configure(float rampTimeMs, float sampleRate) noexcept` (329). `setTarget(float)` recomputes `increment_ = calculateLinearIncrement(target_ − current_, rampTimeMs_, sampleRate_)` (342–354). `calculateLinearIncrement` returns `delta / (rampTimeMs · 0.001 · sampleRate)` in float (100–108). **A ramp's duration is therefore independent of its distance: today's glide is constant-time.** `process()` accumulates `current_ += increment_` in float32 and clamps at the target (370–388). `snapTo` (430), `getCurrentValue` (364), `isComplete` (410). **Float drift (measured 2026-10-12, Node `Math.fround` reproduction of lines 100–108 and 370–388):** the sample count to reach the target exceeds T·fs by +3 (36→60, 240 ms, 48 kHz), +4 (60→72, 120 ms, 44.1 kHz), +8 (36→60, 240 ms, 44.1 kHz), +155 (60→84, 1000 ms, 44.1 kHz) and +4608 (72→96, 2000 ms, 96 kHz). The review's quantized-step reproduction reports misses of up to 337 samples (96 kHz, 500 ms). |
| `SlewLimiter` | `primitives/smoother.h:468` | **Not used** (FR-022) | `void configure(float riseRatePerMs, float fallRatePerMs, float sampleRate) noexcept` (501). It is also a float accumulator, so it drifts like `LinearRamp` and cannot meet SC-007's ±1-sample bar. |
| `NoteProcessor` | `processors/note_processor.h:51` | Not composed in Phase 4 (pitch bend and tuning are Phase 9's) | Layer 2. `void prepare(double)` (80), `void setPitchBend(float bipolar)` (101), `[[nodiscard]] float processPitchBend()` (113), `[[nodiscard]] float getFrequency(uint8_t note) const` (166). |
| `VoiceAllocator` | `systems/voice_allocator.h:187` | Defines the POLY **voice-reuse** transition kind (FR-031) | Layer 3. `enum class VoiceState : uint8_t { Idle, Active, Releasing }` (43–47). `enum class StealMode : uint8_t { Hard = 0, Soft = 1 }`; Hard emits `Steal` + `NoteOn` on the same voice (67–70). `struct VoiceEvent { Type type; uint8_t voiceIndex; uint8_t note; uint8_t velocity; float frequency; }` with `Type { NoteOn, NoteOff, Steal }` (102–115). `[[nodiscard]] std::span<const VoiceEvent> noteOn(uint8_t note, uint8_t velocity) noexcept` (228). `kMaxVoices = 32` (193). Phase 4 does not include this header; it only names the transition the allocator produces. |
| `ProfundumCore` | `systems/profundum_core.h:92` | Consumer of the phase policy and the post-Drop trajectory (test composition only) | `enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning }` (94). `kControlInterval = 32`, `kPitchUpdateInterval = 16` (97–98). `void setRetriggerPhase(RetriggerPhase) noexcept` (197). `void noteOn(float f0Hz) noexcept`, "Effective at offset 0 of the next block" (203–209). `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept` (233–234). The Reset branch (`policy_ == RetriggerPhase::Reset \|\| !sounding_`, 528) calls `bank_.reset()` (543) and seeds phases so that "the first sample is 0.0" (557–558). On a sounding voice that restart is a waveform discontinuity, which is why FR-040 exists. FreeRunning while sounding keeps the oscillator state (559–600). `sounding_ = true` (602) is cleared only by `reset()` / `prepare`; "The RT-safe way to clear a sounding core is a Reset-policy noteOn" (156). f0 is sanitised to [`kMinF0Hz`, `capHz_`] (`sanitizeF0`, 795–799). **Pitch cadence:** `processBlock` samples the trajectory only when `intervalPhase_ % kPitchUpdateInterval == 0` and renders runs of up to 16 samples at that held f0 (253–266); `trajectoryF0` takes the newest finite element and clamps it to [`kMinF0Hz`, `capHz_`] (805–811). The rendered pitch is therefore a 16-sample staircase. Observers: `deliveredGains()` returning `std::span<const float>` (294), `currentFrequency()` "f0 of the last pitch update" (298), `maskFrequency()` "f0 of the last control update" (299), `numPartials()` (301). **There is no phase observer** (observers 290–305). |
| `AnchorOscillator` | `systems/anchor_oscillator.h:55` | Consumer of the phase policy, the re-arm event and the pre-Drop trajectory (test composition only) | `enum class Mode : std::uint8_t { Lock, Follow, Punch, Ghost }` (58). `enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning }`, "same values as ProfundumCore::RetriggerPhase" (59). `void setRetriggerPhase(RetriggerPhase) noexcept` (269). `void noteOn(float f0Hz, bool rearm = true) noexcept`, "Effective at offset 0 of the next block … The last call before a block wins" (275–282). `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr, const float* followRatioPerSample = nullptr) noexcept` (285–286). `fromSilence = !sounding_` (480); "from not sounding always arms" (481); Reset branch (483). `sounding_` is cleared only in `reset()` (169), which `prepare` reaches. `kPitchUpdateInterval = 16` (63): the Anchor updates on the core's cadence. `kPunchRiseMs = 1.0f`, `kGhostDipMs = 2.0f` (77–78). Observers `modeGain()`, `phase()`, `phaseIncrement()`, `isSounding()` (357–360). `static constexpr std::size_t latencySamples() noexcept { return 0; }` (383). |
| `ADSREnvelope` | `primitives/adsr_envelope.h:54` | Representative amp envelope in the test composition (D-10; Phase 9 rules the shipped type) | `void prepare(float sampleRate) noexcept` (68). `void gate(bool on) noexcept`: with `RetriggerMode::Hard` every gate-on calls `enterAttack()`; with `Legato` it re-enters attack only from Idle, and resumes decay or sustain from Release (86–112). `enterAttack()` does not reset `output_`, so the attack starts from the current level (451–468). `[[nodiscard]] float process() noexcept` (232). `RetriggerMode retriggerMode_ = RetriggerMode::Hard` (541). `enum class RetriggerMode : uint8_t { Hard = 0, Legato }` (`primitives/envelope_utils.h:64–67`). |
| Power-curve table | `core/curve_table.h:46, 65, 177` | Candidate math for the exponential ↔ linear ↔ logarithmic families (FR-004). **Not usable as-is for C¹** | `kCurveRangeK = 3.0f`: the exponent is 2^(curve·3) ∈ [0.125, 8] (40–46). `generatePowerCurveTable(std::array<float, 256>&, float curveAmount, float startLevel, float endLevel)` (65). `lookupCurveTable` interpolates **linearly** between 256 knots (177–188), so its derivative steps at every knot. A table lookup therefore cannot satisfy FR-006's C¹ bound; the plan uses closed forms or a C¹ interpolant. |
| `semitonesToRatio`, `ratioToSemitones` | `core/pitch_utils.h:23, 73` | Converting the Drop offset to a frequency ratio in the composition | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` (23). |
| `equalPowerGains` | `core/crossfade_utils.h:50, 64` | Not used: the declick is a dip, not a crossfade (D-8). Listed because it is the only crossfade helper in `core/` | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` (50). |
| `SpectralShapeRecipe` named states | `processors/spectral_shape_recipe.h:83–84, 231–247` | Test fixtures: `kSineAnchor` for instantaneous-frequency measurement; `kSawAnchor` / `kGrowl` as click and aliasing worst cases | `static const Controls kSineAnchor, kTriangleAnchor, kSawAnchor;` and `kHeavy, kHollow, kGrowl` (83–84). |
| `PitchSegmentEnvelope` (Membrum) | `plugins/membrum/src/dsp/pitch_segment_envelope.h:28` | **Not reused**, per the roadmap. Recorded so the ODR sweep sees it | In `namespace Membrum`. One or two segments, curve ∈ [−1, +1] over the power-curve table, output in Hz, `noteOff` a no-op (lines 1–60). |

**Test helpers (verified).**
- `tests/test_helpers/artifact_detection.h`: `ClickDetectorConfig` (38) and `ClickDetector` (99).
- `tests/test_helpers/render_fingerprint.h`: `fingerprintRender` (73) and `compareFingerprints` (122).
- `tests/test_helpers/allocation_detector.h`: `AllocationScope`.
- `dsp/tests/unit/systems/profundum_core_test_helpers.h`: `goertzelPhase` and `renderedHarmonicPowers`.

**Test registration (verified).** `dsp/tests/CMakeLists.txt` lists TUs explicitly.
- `dsp_primitives_tests` starts at line 87.
- `dsp_processors_tests` starts at line 158, and `unit/processors/mono_handler_test.cpp` is at line 232.
- `dsp_systems_tests` starts at line 331, and the Profundum systems TUs are at lines 569–650.

---

## New components

| Class / type | Layer | Header | ODR sweep (`grep -rnE "(class\|struct\|enum class\|enum\|using) <Name>\b" dsp/ plugins/ tests/ tools/`, 2026-10-12) |
|---|---|---|---|
| `DropEnvelope` | 2 | `processors/drop_envelope.h` | **0 hits.** No file named `*drop_envelope*` exists. |
| `NoteMemory`, with nested `Mode`, `Retrigger`, `Transition`, `Actions` and `PhasePolicy { Reset, FreeRunning }` | 2 | `processors/note_memory.h` | `NoteMemory`: **0 hits.** The namespace-scope spellings `NoteMemoryMode`, `NoteActions`, `NoteTransition`, `RetriggerPolicy` and `NoteBehaviour` were swept too: **0 hits each.** No `*note_memory*` file exists. |
| `RetriggerDeclick` | 1 | `primitives/retrigger_declick.h` | **0 hits.** `Declick`: **0 hits.** No `*retrigger_declick*` file exists. |
| `DropPhaseLock` | 2 | `processors/drop_phase_lock.h` | **0 hits** (`DropPhaseLock`, `DropCurve`, `DropPlan` swept 2026-10-12; no `*drop_phase_lock*` or `*drop_envelope*` file exists). The name is the interview's example (FR-055) |
| `GlideMode`, a namespace-scope enum beside `PortaMode` | 2 | `processors/mono_handler.h` (append-only) | **0 hits** as a type. A word search for `GlideMode` / `glide_mode` / `kGlideMode` over `dsp/` and `plugins/` headers finds no use at all, so it collides with no parameter ID. |
| Test-only composition `NoteBehaviourHarness` | test | `dsp/tests/unit/systems/profundum_note_behaviour_test_helpers.h` | **0 hits.** |

**Near-name check.**
- None of the roadmap's hazard names is used: `CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`,
  `MorphNode`, `TransientDetector`, `HarmonicCloud`, `SpectralState`, `MacroMapper`.
- `RetriggerMode` (`primitives/envelope_utils.h:64`) and `PortaMode` (`mono_handler.h:63`) already exist at namespace
  scope. Neither is reused for a new meaning.
- The Note Memory enums are nested in `NoteMemory`, so no second namespace-scope `Retrigger*` name appears.

**Include discipline.**
- `drop_envelope.h` (L2) includes Layer 0–1 only.
- `note_memory.h` (L2) is pure logic and includes Layer 0 only.
  - It does **not** include `profundum_core.h` or `anchor_oscillator.h`, which are Layer 3 (`dsp/CLAUDE.md`: "each may
    only include from layers below it").
  - `NoteMemory::PhasePolicy` therefore mirrors `ProfundumCore::RetriggerPhase` and `AnchorOscillator::RetriggerPhase`:
    same spelling, same order, same values (`Reset` = 0, `FreeRunning` = 1). This is the Phase 3 D-6 pattern.
  - A test TU `static_assert`s that the three enums agree, and Phase 9 maps them.
- `retrigger_declick.h` (L1) includes Layer 0 only.
- `drop_phase_lock.h` (L2) includes Layer 0–1 only. It does **not** include `drop_envelope.h` (same layer): it takes plain
  numbers (the latched Amount, Time, Shape and Rebound, f_res, the planned curve's phase integral from the FR-056 hook,
  and the trigger kind), and the caller or test composition passes them.
- `mono_handler.h` keeps its current includes (lines 22–25), unless the plan shows that the constant-rate law needs
  another Layer 0–1 header.

---

## Functional Requirements

### Descriptor definitions (used by FRs and SCs)

- **Drop offset d[n]:** the `DropEnvelope` output in semitones at sample n, with n = 0 at the trigger's effective
  sample: the **planned** curve, with no phase correction. **Corrected offset d_c[n]:** the `DropPhaseLock` output for
  that d[n] (FR-055). It differs from d[n] by the bounded correction c[n] = d_c[n] − d[n] (FR-054) and equals d[n] when
  no correction applies (FOLLOW, FR-058). The core's pitch is f_core[n] = f_res[n] · 2^(d_c[n]/12), where f_res is the
  resolved note pitch after Glide.
- **Normalised fall p(u):** p(u) = d(u·T)/A for u = t/T ∈ [0, 1], with Rebound 0. p(0) = 1, and p reaches 0 at u = 1.
- **Curve area α:** α = ∫₀¹ p(u) du.
  - α < 0.5: the curve falls early (exponential-like).
  - α = 0.5: linear.
  - α > 0.5: the curve holds, then falls (logarithmic-like).
- **Overshoot ρ:** ρ = −min_n d[n] in semitones. It is positive when d goes below 0.
- **Elastic undershoot ε:** with Rebound 0, ε = −min d over the samples after the first landing (d first reaching 0).
  It is 0 for s ≤ 2/3 and grows across the logarithmic → elastic segment (FR-004).
- **Settle time t_s:** the last sample at which |d| > 1 cent, measured from the trigger.
- **Landing time t_L:** the sample at which the fall ends analytically, t_L = round(T·fs/1000). FR-006 gives the slope
  condition at t_L per family.
- **Rendered pitch trace d_core[n] (D-15):** the pitch the core actually renders, expressed as a Drop offset:
  d_core[n] = 12·log2(`currentFrequency()` / f_res[n]), read after every sample (host block 1). It is the held f0 of the
  last pitch update after the cap clamp (`profundum_core.h:253–266, 298, 805–811`): a 16-sample staircase. Every
  pitch-domain lever descriptor (start pitch, t_s, α, ρ, ε, glide trajectory) is measured on d_core, except where an
  SC says "on the control signal" (d[n]).
- **Audio pitch estimate f_zc (D-15):** the rendered-audio corroboration of d_core. Input is the core's float output at
  `kSineAnchor`, a single partial (FR-060). Zero crossings are located by linear interpolation between the two samples
  that straddle zero, in double. Each half-period between successive crossings gives one estimate,
  f_zc = 1 / (2·Δt_zc), stamped at the half-period's midpoint. The time resolution is one half-period of the
  instantaneous pitch (12.1 ms at E1 on the note, 0.76 ms at E1 + 48 semitones). f_zc estimates the **mean** frequency
  over that half-period.
  - **Estimator self-test (gates every SC that uses f_zc):** a reference chirp is generated in double as
    sin(2π·Σ f_ref/fs) from an analytic trajectory, stored as float32, and fed through the estimator. Trajectories: the
    four Shape anchors with A ∈ {12, 48}, T ∈ {10, 1000} ms, at E1 and C2, 48 kHz. Pass: every f_zc equals the
    analytic mean of f_ref over the same half-period within **1 cent**.
  - **Agreement with d_core:** on the core's own render, f_zc equals the half-period mean of f_res·2^(d_core/12)
    within **10 cents**. That bar sits 10× above the estimator's validated 1 cent, so a failure points at the core's
    rendering, not at the estimator.
- **Instantaneous frequency on the Anchor:** from `phase()` increments, as in Phase 3 SC-008(b).
- **h1–Anchor phase offset θ:** core h1 phase (`goertzelPhase` on a window) minus the Anchor's fundamental phase, as in
  Phase 3 SC-003.
- **Sample-delta bound:** the Phase 3 SC-006(c) form, max |y[n] − y[n−1]| ≤ 1.1 × (G_max·D_max + S_max·P_max). The terms
  are defined analytically from the transition's curves and never read from the measured trace.
- **Note set:** E0 (20.60 Hz), E1 (41.20 Hz), E2 (82.41 Hz) and C3 (130.81 Hz), as in Phases 1–3.
- **Rates:** 44.1, 48 and 96 kHz.

### FR-001 series — `DropEnvelope` interface and lifecycle (roadmap lines 362–365)

- **FR-001 Header and layer.** `processors/drop_envelope.h`: Layer 2, header-only, `Krate::DSP`. Every method is
  `noexcept`.
- **FR-002 Lifecycle.**
  - `void prepare(double sampleRate) noexcept` clamps the rate to [22050, 192000], the range `ProfundumCore` uses
    (`profundum_core.h:104–105`). It sizes all state with no heap use, then resets.
  - `void reset() noexcept` sets d and d' to 0 and the envelope to idle. It keeps the parameters.
  - Before `prepare`, `process` returns 0 and every other call is a no-op.
- **FR-003 Parameters (D-1).** Every setter sanitises its input: a non-finite value keeps the previous value, and any
  other value is clamped to its range.
  - `setAmount(float semitones)`: A ∈ [0, kMaxAmountSemitones = 48]. Amount is "how far the pitch falls" (roadmap line
    362). The fall starts A semitones above the note and ends at the note.
  - `setTime(float ms)`: T ∈ [kMinTimeMs = 1, kMaxTimeMs = 4000].
  - `setShape(float)`: s ∈ [0, 1], continuous (FR-004).
  - `setRebound(float)`: R ∈ [0, 1] (FR-005).
  - Parameters (Amount, Time, Shape, Rebound) are **latched at trigger** (D-4). A change while a drop runs affects only
    the next trigger, so a drop in flight is deterministic, C¹ by construction and never steps. The `DropPhaseLock`
    correction is computed once per trigger from the same latched values (FR-057). Phase 8's Morph and Phase 9's Motion
    therefore act on the next note's Drop (SC-005). Live re-planning of a drop in flight is rejected.
- **FR-004 Shape families (D-2).** Four anchors, with a continuous blend between neighbours:

  | s | Family | Behaviour | Curve area |
  |---|---|---|---|
  | 0 | Exponential | Fast early fall | α ≤ 0.2 |
  | 1/3 | Linear | Constant fall | α = 0.5 |
  | 2/3 | Logarithmic | Slow early fall; the mirror of exponential | α ≥ 0.8 |
  | 1 | Elastic | Lands on the target with momentum, then settles through a damped oscillation of at least two half-cycles around it. First undershoot ε_e = kElasticOvershootFraction·A (0.15, D-2); each later peak ≤ kElasticDecay = 0.5 × the previous one (D-2) | α of the fall to the first landing is recorded, not gated |

  - Between anchors, p(u; s) is a continuous blend of the two neighbouring families, with weight w ∈ [0, 1] linear in s
    within each third.
  - α is strictly monotonic in s over [0, 2/3].
  - Over [2/3, 1], the elastic undershoot ε(s) = w·ε_e, with w = 3s − 2, is strictly monotonic in s. The logarithmic
    family has landed (p = 0) wherever the elastic ringing lives (u ≥ 1), so the blend's undershoot is exactly w·ε_e.
  - **Continuity over the whole range:** for adjacent s steps, p(u; s) changes by a bounded max-abs amount at every u
    (SC-001). A linear blend with Δw = 1/64 per step meets this by construction, because |p_a − p_b| ≤ 1 + ε_e/A.
  - With Rebound 0, every family reaches |d| ≤ 1 cent at u = 1, before any elastic ringing. Elastic's ringing lies
    within [T, kElasticSettleFactor·T] (D-2).
  - **Elastic is damped ringing (Q4).** After the fall the pitch oscillates around the target: first undershoot
    ε_e = 0.15 · Amount, each later peak ≤ 0.5 × the previous one, settled (|d| ≤ 1 cent) within 2 · Time
    (kElasticSettleFactor = 2). It composes additively with Rebound (FR-005). The Shape sweep stays continuous through
    [2/3, 1]. `DropPhaseLock` treats the ringing as part of the planned drop and lands the phase at the end of the settled
    curve (FR-053).
- **FR-005 Rebound (D-3).** R > 0 adds one overshoot below the target.
  - Depth: ρ = R · kMaxReboundFraction · A. kMaxReboundFraction = 0.25 is proposed.
  - The overshoot is followed by a C¹ return to 0 that settles within kReboundSettleFactor · T after the fall ends.
  - R = 0 leaves every family exactly as FR-004 defines it.
  - **Composition law with elastic (D-3).** Rebound adds to the elastic undershoot. For every s, with w = max(0, 3s − 2):
    ρ(s, R) = w·ε_e + R·kMaxReboundFraction·A. At s ≤ 2/3 this is the plain Rebound law; at s = 1 it is
    ε_e + R·kMaxReboundFraction·A. The later elastic peaks keep FR-004's decay ratio relative to the first. ρ is
    therefore continuous in s and strictly increasing in R for every s.
- **FR-006 C¹ output.** d[n] and its first difference are continuous everywhere:
  - at the landing t_L, by family:
    - **exponential, linear and logarithmic with R = 0:** d' reaches 0 at t_L (a zero-slope landing into idle). The
      linear and logarithmic families reach it through a landing fillet confined to the last 5 % of T, inside
      SC-001's fit windows. The logarithmic family needs the fillet because the plain mirror of the exponential would
      land at the exponential's steepest slope;
    - **any family with R > 0, and the elastic component for s > 2/3:** with Rebound the fall runs from A to −ρ_R along the family shape, d = (A + ρ_R)·p(u) − ρ_R, so it crosses 0
      **inside** the fall with a non-zero negative slope (R = 1: exponential at u = 0.201, linear 0.800, logarithmic 0.967), reaches
      −ρ_R exactly at t_L, and a C¹ return settles at t_L + kReboundSettleFactor·T (plan S1 C-7, re-ruled 2026-10-12: a zero-slope
      exponential landing cannot cross 0 steeply at t_L; ρ = R·0.25·A holds exactly as a knot value); the elastic component for
      s > 2/3 starts its ringing at the same (d, d');
  - at the rebound boundaries;
  - at the elastic segment;
  - at the return to idle;
  - across every retrigger (FR-007).

  The only exception is a **snap trigger** (FR-007), which sets d = A while nothing is sounding.
  - The gate is the Phase 3 C¹ checker. Per segment, max |Δ²d| ≤ 1.05 × the segment's analytic peak second difference.
    At every boundary, the slope step is no larger than the larger of the two adjacent bounds.
  - Positive control: a linear ramp with corners must **fail** the same checker.
- **FR-007 Retrigger with continuation.** `void trigger(bool snap) noexcept` takes effect at the next processed sample.
  - **Snap** (`snap = true`): d jumps to A, and d' to the family's initial slope. The caller uses this when the voice is
    silent, or when a declick has brought the output to zero (FR-042).
  - **Bridged** (`snap = false`): the bridge is **brake → rise → onset fillet** (plan S1 C-8, re-ruled 2026-10-12; a single Hermite bridge overshoots
    above A by 1.8 st at T = 10 ms and unboundedly as T → 1 ms): (1) a monotone Hermite brake takes the incoming slope to 0 over
    L_br = clamp(round(3·0.5 st/|m₀|), 1, L_min) samples (excursion ≤ 0.5 st whenever L_br > 1, ≤ 2.7 st at the theoretical
    extreme L_br = 1); (2) a smoothstep rise to (A, 0) over L_r = L_min = round(min(kDropBridgeMs, T)·fs/1000) samples (no
    rate-dependent lengthening: O-1 removed the slope bound); (3) the new fall starts with zero slope through a Hermite over its
    first 5 % of T. Bridge length = L_br + L_r exactly. Snap triggers keep p'(0).
  - A retrigger mid-fall, mid-rebound or mid-elastic therefore never steps d or d'.
  - `trigger(false)` while idle behaves as a bridge from (0, 0).
- **FR-008 Processing.**
  - `[[nodiscard]] float process() noexcept` returns d[n] and advances one sample.
  - `void processBlock(float* out, std::size_t n) noexcept` writes n samples. n = 0, or a null pointer, is a no-op with
    no state change.
  - `[[nodiscard]] bool isActive() const noexcept` is false once d has settled at exactly 0.0f and the envelope is
    idle.
  - `[[nodiscard]] float currentOffset() const noexcept` returns d at the last processed sample.
  - Output is bit-identical across host-block partitions {1, 37, 512}.
- **FR-009 Robustness and RT safety.**
  - No allocation, lock, exception or I/O in any method after `prepare`.
  - Output is finite for every input, including non-finite setter arguments, which are sanitised (FR-003).
  - Finite clamped parameters cannot produce a non-finite internal state. The tests sweep the parameter corners to
    prove it.
  - State-finiteness checks use the bit-pattern `detail::isFinite` (`core/db_utils.h`), never `std::isnan`.
- **FR-010 Sample-rate invariance.** At 44.1, 48 and 96 kHz:
  - the fall time, the rebound settle and the elastic period match their millisecond values within 1 sample;
  - the normalised curve p(u) matches across rates within 1e-4.

### FR-020 series — `MonoHandler` glide modes (roadmap lines 366–367; append-only)

- **FR-020 Append-only.**
  - New, at namespace scope beside `PortaMode`:
    `enum class GlideMode : uint8_t { ConstantTime = 0, ConstantRate = 1, Quantized = 2 }`.
  - New members: `void setGlideMode(GlideMode mode) noexcept` and `[[nodiscard]] GlideMode glideMode() const noexcept`.
  - No existing declaration, signature, default or behaviour changes. No existing code may come to depend on member
    order.
  - The default is `GlideMode::ConstantTime`.
- **FR-021 Constant-time (existing).** The behaviour is exactly today's: the glide reaches its target in the portamento
  time T regardless of distance (`smoother.h:100–108`).
  - With `GlideMode::ConstantTime`, the `processPortamento()` sequence is **bit-identical** to the pre-change
    `MonoHandler`.
  - This holds for every call sequence in the existing `mono_handler_test.cpp`, plus a seeded random event script.
  - The reference is a frozen copy of today's class in a test-only header under a test namespace (the
    `profundum_phase1_core_ref.h` pattern). It is never a stored float golden.
- **FR-022 Constant-rate (D-6).** The portamento time T is read as **milliseconds per octave**.
  - A glide over Δ semitones lasts T·|Δ|/12.
  - A retarget mid-glide keeps the rate: the new glide lasts T·|Δ_remaining|/12 from the current pitch.
  - The law stays linear in semitones, as today.
  - **Precision (D-13).** The position is held in **double**, in new private state appended to `MonoHandler`, advanced by
    a per-sample rate of 12·1000/(T·fs) semitones and clamped at the target. It does **not** run on `portamentoRamp_`
    (float `LinearRamp`) or on `SlewLimiter`: both accumulate in float32, and the `LinearRamp` path misses the duration by up to +4608 samples
    (Existing components, `LinearRamp` row). Requirement: duration error ≤ 1 sample at every rate up to 96 kHz and every
    T up to `kMaxPortamentoTimeMs` = 10 000 ms (a Δ = 24 glide then lasts 20 s, 1.92 M samples at 96 kHz).
- **FR-023 Quantized (D-5).** The output pitch moves in **whole-semitone steps** on the MIDI-note grid.
  - The underlying position follows the constant-time law over T, held in **double** in its own appended state, with
    the D-13 precision requirement. It does not read `portamentoRamp_`.
  - The output is out = target − sign(r)·⌈|r|⌉, where r = target − position. It holds the start note and steps one
    semitone each time the position covers one more semitone.
  - A glide over Δ whole semitones makes |Δ| steps, at t_k = k·T/|Δ| for k = 1…|Δ|. The last step lands on the target
    at T.
  - A retarget from a fractional underlying position stays on the integer grid: every output is an integer note number.
  - The frequency between steps is constant. The oscillator's phase continuity carries the steps without a click
    (FR-064; gated by SC-010(c)).
- **FR-024 Mode switch mid-glide.** A mode switch never moves the output by more than 1 semitone in one sample.
  - ConstantTime ↔ ConstantRate continue from the current pitch under the new law. Entering ConstantTime snaps
    `portamentoRamp_` to the current position and retargets it, so the remaining glide lasts T.
  - Entering ConstantRate or Quantized seeds the double position from the current position.
  - Entering Quantized steps to the grid value of the current position.
- **FR-025 Unchanged surfaces.** Note priority, the note stack, the legato retrigger flags, `PortaMode` gating, the
  first-note snap (lines 186–199) and `reset()` behave identically in every glide mode. Glide modes change only the
  pitch law between the glide's start and its target: ConstantTime through the unchanged `portamentoRamp_` path
  (FR-021), ConstantRate and Quantized through the appended double-precision state (D-13). Every glide-start and
  snap decision stays in the existing `updatePortamentoTarget` gating (`mono_handler.h:450–457`).

- **FR-026 One glide knob, no new setter (Q7).** Constant-rate reuses `setPortamentoTime`, read as **milliseconds per
  octave** in ConstantRate. `MonoHandler` gains no setter other than `setGlideMode` (FR-020): no `setGlideRate`, no second
  time parameter. The rate is ms per octave, not ms per semitone. The duration law is T_ms · |Δ semitones| / 12
  (FR-022).
- **FR-027 Phase 11 handoff (Q7).** The one portamento knob is labelled per mode by Phase 11: milliseconds for
  ConstantTime (the whole glide), milliseconds per octave for ConstantRate, milliseconds for Quantized (the whole
  underlying ramp). The clause is written into the Phase 11 section of the roadmap by this phase's comply stage, in the
  FR-066-style handoff form (SC-027).
- **FR-028 Three modes only (Q8).** `GlideMode` stays three-valued: `{ ConstantTime (default, bit-identical to today's
  behaviour, FR-021), ConstantRate, Quantized }`. Quantized is a mode of its own: not an orthogonal modifier on
  ConstantTime or ConstantRate, and there is no `setGlideQuantize`. Its underlying position runs the constant-time law
  over T, and its output steps per semitone toward the target, rounding toward the start note (FR-023).

### FR-030 series — `NoteMemory` policy (roadmap lines 368–372)

- **FR-030 Resolver.** `NoteMemory` holds a configured mode and Retrigger setting and offers
  `[[nodiscard]] Actions resolve(Transition t, bool voiceAudible) const noexcept`.
  - `resolve` keeps no state between calls.
  - It allocates nothing and has no audio path.
- **FR-031 Transition kinds.**
  - `FromSilence`: the voice is not audible (amp envelope idle) or has never played.
  - `Overlap` (MONO): another key is held. It is detected as `MonoHandler::hasActiveNote()` **before** `noteOn`
    (`mono_handler.h:152, 340`). A same-note re-press while that note is held is an `Overlap`, by `MonoHandler`'s own
    rule (its FR-016, line 154).
  - `Tail` (MONO): no key is held, but the voice is still audible in its release.
  - `ReturnToHeld` (MONO): a `noteOff` makes a held key the active note (`mono_handler.h:237–256`).
  - `VoiceReuse` (POLY): `VoiceAllocator` assigns a voice that is still audible. That is either a Hard `Steal` + `NoteOn`
    on the same voice (`voice_allocator.h:67–70, 102–115`) or the reuse of a `Releasing` voice.
- **FR-032 Modes (D-7).**
  - `Mode { AlwaysRetrigger, Legato, EnvelopeRetriggerOnly, OscillatorContinuous }`: the concept's four (§12, lines
    582–590).
  - `Retrigger { Reset, Continue }`: the concept's RETRIGGER, "oscillator phase behaviour" (§12, line 570).
  - The default is `AlwaysRetrigger` + `Reset` (D-7).
- **FR-033 Actions.** `Actions` is a plain aggregate:

  | Field | Type | Value |
  |---|---|---|
  | `oscillatorRestart` | bool | The noteOn is a note start for the oscillators (FR-034) |
  | `phase` | `PhasePolicy` | `Reset` iff `oscillatorRestart && retrigger == Reset`; otherwise `FreeRunning` |
  | `envelopeRestart` | bool | The amp envelope's gate-on starts a new attack (FR-034) |
  | `dropTrigger` | bool | Equals `envelopeRestart` |
  | `dropSnap` | bool | True iff the voice is not audible or the declick applies |
  | `anchorRearm` | bool | Equals `envelopeRestart`, because PUNCH and GHOST are attack behaviours (Phase 3 FR-042, FR-043) |
  | `declick` | bool | `phase == Reset && voiceAudible` |

  On every noteOn, the core and the Anchor receive `setRetriggerPhase(phase)` and `noteOn(..., rearm = anchorRearm)` on
  the **same** block boundary (FR-051).
- **FR-034 The matrix (D-7).** Each cell is (oscillatorRestart, envelopeRestart):

  | Mode | FromSilence | Overlap | Tail | ReturnToHeld | VoiceReuse |
  |---|---|---|---|---|---|
  | AlwaysRetrigger | (1, 1) | (1, 1) | (1, 1) | (1, 1) | (1, 1) |
  | Legato | (1, 1) | (0, 0) | (1, 1) | (0, 0) | (1, 1) |
  | EnvelopeRetriggerOnly | (1, 1) | (0, 1) | (0, 1) | (0, 1) | (0, 1) |
  | OscillatorContinuous | (0, 1) | (0, 0) | (0, 1) | (0, 0) | (0, 1) |

  - OscillatorContinuous never restarts the oscillators after the voice's first note since `prepare` / `reset`. The core
    and the Anchor already treat that first noteOn as Reset (`profundum_core.h:528`; `anchor_oscillator.h:480–483`,
    where `fromSilence = !sounding_` forces the Reset branch). From then on the phase is continuous, including after
    silence, because neither component clears `sounding_` except in `reset()` (`profundum_core.h:156, 602`;
    `anchor_oscillator.h:169`).
  - Legato and OscillatorContinuous use legato envelopes on `Overlap` and `ReturnToHeld`.
  - **Where Retrigger acts (D-12).** Retrigger changes an action only in cells with oscillatorRestart = 1. It is a
    no-op in every cell of EnvelopeRetriggerOnly except FromSilence, in every cell of OscillatorContinuous, and in
    Legato's Overlap and ReturnToHeld cells. In AlwaysRetrigger it acts in every cell.
  - **Observability (D-12, Q3).** Under Retrigger = **Reset**, every row differs from every other row in at least one
    cell, so all four modes are observable. Under Retrigger = **Continue**, `phase` is FreeRunning everywhere and the
    oscillatorRestart column has no effect, so the pairs {AlwaysRetrigger, EnvelopeRetriggerOnly} and {Legato,
    OscillatorContinuous} resolve to `Actions` equal in every acted-on field (all but the raw `oscillatorRestart` bit) in all 10 (transition, audible) cells: the four modes collapse
    to two. This is the ruled behaviour (FR-036). The mode-distinctness gate (SC-014) pins Retrigger = Reset;
    SC-009(a) asserts the collapse.
  - `MonoHandler` is driven with `setLegato(false)`. Note Memory, not `MonoHandler`'s legato flag, is the single
    authority over envelope restart. Glide gating stays with `PortaMode`, which is orthogonal (`mono_handler.h:186–199`
    gates glide on overlap and `PortaMode`, not on `legato_`). The roadmap's "legato" axis is therefore realised as Note
    Memory's Legato mode (D-11).
- **FR-035 Determinism.** `resolve` is a pure function of (mode, retrigger, transition, voiceAudible). The tests assert
  the whole table exhaustively: 4 × 2 × 5 × 2 = 80 cells.

- **FR-036 No Retrigger-independent effect (Q3).** Retrigger is purely oscillator phase behaviour (concept §12). Under
  Continue, `oscillatorRestart` has no effect on any other action: `dropTrigger` and `anchorRearm` stay equal to
  `envelopeRestart` (attack behaviours, Phase 3 FR-042, FR-043) in every mode and every setting. There is no Anchor-only
  phase reset under Continue and no re-wiring of Drop or re-arm to the oscillator column. The collapse of FR-034 is
  asserted by SC-009(a).

### FR-040 series — Retrigger click-freedom (roadmap lines 373–374; Phase 3 FR-006 handoff)

- **FR-040 Problem statement (evidence).**
  - A Reset noteOn on a sounding voice restarts the core's bank at phase 0 (`profundum_core.h:543, 557–558`) and the
    Anchor's φ (Phase 3 FR-006).
  - The output therefore jumps from its current value to 0.0. On any voice audible at that moment, that is a click.
  - FreeRunning noteOns are already phase-continuous (Phase 1 SC-012, Phase 3 SC-026(a)).
- **FR-041 `RetriggerDeclick` (L1, D-8).** A per-voice scalar gain g_d ∈ [0, 1].
  - When armed, g_d falls from its current value to exactly 0.0f over kDeclickMs (1.5 ms, D-8, ratified Q6) along a C¹
    raised cosine.
  - It holds 0 for the sample at which the restart takes effect, then returns to 1.0 **immediately**.
  - The return needs no fade. The restarted core and Anchor start at exactly 0.0 (core: `profundum_core.h:557–558`;
    Anchor: Phase 3 SC-017). The restarted amp envelope's attack starts from its current level
    (`adsr_envelope.h:451–468`).
  - The **same** scalar applies to L and R, so the Anchor stays L == R bit-identical (Phase 3 FR-050).
  - API:
    - `void prepare(double sampleRate) noexcept`
    - `void arm() noexcept`
    - `[[nodiscard]] std::size_t samplesUntilSwitch() const noexcept`
    - `[[nodiscard]] float process() noexcept`
    - `void processBlock(float* gain, std::size_t n) noexcept`
    - `[[nodiscard]] bool isActive() const noexcept`
- **FR-042 Scheduling.** When `declick` is true:
  1. The caller arms the declick and defers the Reset noteOn.
  2. The caller renders up to `samplesUntilSwitch()` samples.
  3. The caller issues `setRetriggerPhase(Reset)` and `noteOn` to the core and the Anchor, and triggers the Drop with
     `snap = true`.

  - The deferred noteOn is then effective at "offset 0 of the next block" (`profundum_core.h:203`;
    `anchor_oscillator.h:275`), which is exactly the zero-gain sample.
  - The test harness proves this with host blocks {1, 37, 512}, splitting each at the switch.
  - The restart latency is ≤ ⌈kDeclickMs·fs/1000⌉ + 1 samples. It applies only when `declick` is true; every other
    transition starts at its own sample.
- **FR-043 New note during a declick.** The new note re-targets the deferred noteOn; as for `noteOn`, the last call
  wins. The dip does not restart, and the gain never steps.
- **FR-044 No declick where none is needed.** FreeRunning noteOns, and Reset noteOns on a voice that is not audible,
  never arm the declick. Their output is bit-identical to the same calls without a `RetriggerDeclick` in the chain.

- **FR-045 Core and Anchor dip together (Q6).** The declick scalar g_d multiplies the core output **and** the Anchor
  output with the identical float on every sample, so both reach exactly 0.0f at the switch sample and the common phase
  reference (FR-051) holds. A core-only dip is rejected. kDeclickMs = 1.5 ms (0.5 ms rejected). The onset latency
  (≤ ⌈1.5 ms · fs/1000⌉ + 1 samples, FR-042) is recorded per cell in SC-011, and S_max of SC-010(b) is derived from the
  1.5 ms raised-cosine slope.

### FR-050 series — Phase reference, routing and post-Drop landing (roadmap lines 370–372; Phase 3 FR-036, FR-049)

- **FR-050 Routing.**
  - The core's per-sample trajectory is f_core[n] = f_res[n]·2^(d_c[n]/12).
  - The Anchor's trajectory is f_res[n] in LOCK / PUNCH / GHOST, and f_core[n] in FOLLOW (Phase 3 FR-049; roadmap lines
    552–554).
  - f_res is the `MonoHandler::processPortamento()` output in MONO, and the voice's note frequency in POLY.
  - Phase 4 supplies the trajectories and the composition test. Phase 9 owns the voice.
- **FR-051 Common reference at note start.** On every noteOn, the core and the Anchor receive their noteOn on the same
  block boundary with the same `PhasePolicy`.
  - The core's noteOn f0 is f_res·2^(d₀/12): the Drop's start, which is A on a snap trigger.
  - The Anchor's noteOn f0 is f_res.
  - After a Reset noteOn both start at phase 0 (Phase 3 FR-035), so the common reference holds at note start.
- **FR-052 Glide keeps the lock.** With no Drop active, the core and the Anchor are fed the same f_res trajectory.
  Phase 3 SC-003's lock bounds hold through every glide mode, including Quantized steps.
- **FR-053 Post-Drop landing: core-side correction inside the Drop (Q1).** During a Drop under LOCK / PUNCH / GHOST,
  core h1 accumulates the **excess phase** Φ_x = ∫ f_res · (2^(d(t)/12) − 1) dt over the **planned curve**, including
  Rebound and the elastic ringing. Because the Drop parameters are latched at trigger (FR-003), Φ_x is known at trigger.
  - A C¹ correction term c(t) is added to the planned offset (FR-054) and **spread over the Drop's own duration**, from
    the trigger to the landing. It removes frac(Φ_x) to whichever neighbouring whole cycle (⌊Φ̂⌋ or ⌈Φ̂⌉) needs the smaller correction, so the residual is below 1 cycle (plan S1 C-4, re-ruled 2026-10-12: the nearest target can be unreachable at E0 with short drops because it dives through the 8 Hz clamp; with the neighbour rule every SC-013 cell is feasible with |γ| ≤ 48 st).
  - **Landing** is the end of the planned curve: t_L (no Rebound, s ≤ 2/3), t_L + kReboundSettleFactor·T (Rebound), or
    kElasticSettleFactor·T = 2·T (elastic).
  - At landing core h1 and the Anchor meet at |θ| ≤ **1°**, and there is **no pitch motion after landing**: the
    corrected offset d_c is exactly 0.0f from landing on. There is no post-landing tail.
  - The Anchor's pitch and phase are never altered (FR-058; Phase 3 FR-049).
  - Orientation figures for the uncorrected Φ_x (analytic, 2026-10-12):

    | Note | A (semitones) | T (ms) | Φ_x, exponential (cycles) | Φ_x, linear (cycles) |
    |---|---|---|---|---|
    | E1 | 24 | 60 | 0.74 | 2.88 |
    | E0 | 12 | 20 | 0.05 | 0.18 |

  - Without the correction θ is arbitrary: at Anchor level 1 with k = 1, θ near 180° would cancel most of the summed
    fundamental and break "The fundamental never disappears" (roadmap line 43). SC-013 holds a negative control.
  - Φ_x is defined on the frequency the core actually renders (the held f0 of each pitch update after the cap clamp,
    E-3), so the 16-sample staircase and the cap are part of the integral the plan derives (SC-013 measures θ on
    rendered audio).
- **FR-054 The correction is C¹ and bounded (Q1).** The corrected offset d_c = d + c passes the FR-006 checker in every
  family, including across bridged retriggers. The deviation c of the corrected curve from the planned curve is
  bounded and recorded per Time.
  - With residual r (|r| ≤ 0.5 cycle) spread over the Drop's duration D, the mean frequency offset is |r|/(f_res · D);
    the plan derives the peak factor of the chosen C¹ profile, and the analytic peak |c| in semitones follows.
  - Orientation figures: at most 0.5 cycle of f_res spread over the drop, about 3 semitones peak for a 60 ms drop, below
    0.1 semitone for drops over 1 s (the figures scale with 1/(f_res · D)).
  - SC-013 gates the measured peak against the analytic bound and records it per cell.
- **FR-055 `DropPhaseLock` component (Q2).** A separate **shipped** L2 corrector (`processors/drop_phase_lock.h`,
  `Krate::DSP`, header-only, every method `noexcept`), composed after `DropEnvelope`.
  - At trigger it takes the planned curve description (FR-056) and f_res, and plans the correction (FR-057).
  - Per sample it takes the planned offset d[n] and emits the corrected semitone offset d_c[n] (FR-053).
  - Lifecycle: `prepare(double)`, `reset()`, a trigger call and a per-sample process call; the plan fixes the exact
    signatures. Routing and declick scheduling stay test-only (`NoteBehaviourHarness`) until Phase 9's
    `ProfundumVoice` composes them.
- **FR-056 `DropEnvelope` stays pitch-agnostic (Q2).** `DropEnvelope` gains a small hook exposing the closed-form phase
  integral of its planned curve: the integral of (2^(d(t)/12) − 1) over the latched curve, in seconds, for the latched
  Amount, Time, Shape and Rebound. Both are Phase 4 code. Apart from that hook `DropEnvelope` knows nothing about
  f_res or the correction, and SC-001 to SC-005 test its output d[n] without the correction.
- **FR-057 Closed-form, O(1) trigger-time computation (Q1).** The plan derives the closed-form phase integral per shape
  family: exponential, linear, logarithmic and elastic are analytic, and the Shape blend is linear in s, so the
  integral is evaluated by O(1) fixed-node Gauss–Legendre 8 quadrature of the latched blended curve (2 panels per piece, 4 on pieces longer than 0.5·T; worst error 8.7e-9 against the 1e-6 bar) — plan S1 C-2, re-ruled 2026-10-12: no elementary closed form exists over the exponential and logarithmic pieces (Ei terms, ∫e^{cubic}), and 2^{d/12} is nonlinear in d, so "the integral of the blend is the blend of the integrals" was false; "closed-form" throughout this spec reads as this O(1) quadrature. Rebound and the elastic ringing add their analytic terms. The
  trigger-time cost is O(1) and independent of Time: there is **never a per-sample dry run on the audio thread**
  (SC-024, SC-025).
- **FR-058 Anchor untouched; FOLLOW needs no correction (Q1).** The Anchor's pitch and phase are never altered by
  `DropPhaseLock` in any mode. In FOLLOW the Anchor tracks the core's post-Drop f0, so it stays locked and
  `DropPhaseLock` emits no correction (c ≡ 0, d_c = d, bit-identical).
- **FR-059 Latching, retrigger and Reset (Q1, Q5).** The correction is computed once per trigger from the latched
  parameters and f_res at trigger.
  - A Reset noteOn zeroes the running excess-phase accumulator (both oscillators start at phase 0, FR-051).
  - A bridged retrigger re-plans the correction from the current corrected value and slope (d_c, d_c'), so d_c and its
    slope never step. The running total, accumulated in double from the corrected output, still lands on an integer
    number of cycles at the new landing.
  - A Glide in flight during a Drop is not part of the guarantee, because f_res is taken at trigger: the residual θ
    is recorded, not gated (E-15).

### FR-060 series — Cross-cutting: audibility, mono, aliasing, RT, CPU, portability, tests

- **FR-060 Audibility gate** (theme FR, roadmap line 832). Every lever moves its descriptor on what the core renders,
  not only on the control signal. Pitch descriptors are measured on the rendered pitch trace d_core and corroborated on
  rendered audio through f_zc (core at `kSineAnchor`; D-15):
  - Amount → the starting pitch;
  - Time → t_s;
  - Shape over [0, 2/3] → α;
  - Shape over [2/3, 1] → the elastic undershoot ε;
  - Rebound → ρ;
  - **Retrigger** (Reset vs Continue) → the output waveform around each note onset on a sounding voice (the declick dip
    and the phase restart), measured on rendered audio;
  - each Note Memory mode (under Retrigger = Reset, D-12) → a rendered-audio difference from every other mode on a
    fixed script;
  - each glide mode → a distinct rendered pitch trajectory.

  Nothing ships inert. SC-014 holds the bars.
- **FR-061 Mono** (theme FR, roadmap line 834). No Phase 4 component creates or alters stereo content. The declick gain
  is one scalar applied identically to L and R, so the Anchor path stays L == R bit-identical under every policy.
- **FR-062 Anti-aliasing under pitch trajectories** (theme FR, roadmap line 836). Phase 4 adds no nonlinear stage.
  Its trajectories do raise the core's pitch: the Drop starts up to 48 semitones up, and Rebound recovers upward.
  - **Requirement (normative):** no partial with a non-zero delivered gain exceeds fs/2 at any sample, measured against
    the frequency the core actually renders (the held f0 of the last pitch update after the cap clamp,
    `currentFrequency()`), not the commanded f_core[n]. **Re-ruled 2026-10-12 (O-1):** the normative gate is SC-015's
    rendered aliased power (≤ −96 dBFS); the index × frequency formula is descriptive only, because the core never renders a
    partial above its cap. No Phase 4 trajectory carries an upward-slope bound.
  - The core's mask follows the trajectory only at control updates (`kControlInterval = 32`, `profundum_core.h:97`;
    `maskFrequency()`, 299), while pitch updates every 16 samples (98). A fast upward segment can therefore carry a
    partial past fs/2 for up to one control interval. The candidates are bridged retriggers over ≤ 1 ms, upward rebound
    recovery, the `DropPhaseLock`-corrected Drop curve and 24-semitone up-glides at minimum time.
  - **A violation is a Phase 4 defect, and Phase 4 fixes it (D-14).** The first remedy is in Phase 4's own
    trajectories: bounding the upward slope per control interval of the Drop bridge, the rebound recovery and the glide
    laws. An append-only change to the core's mask cadence is the fallback, and only by clarify ruling.
  - The trajectory, the margin and any excess are written out as diagnostic evidence alongside SC-015. That output never
    replaces the gate.
- **FR-063 Shared-header gate.** After the `MonoHandler` change, these suites stay green (roadmap lines 846–847):
  `dsp_processors_tests` (`mono_handler_test.cpp`), `dsp_systems_tests` (`PolySynthEngine`), `ruinae_tests`,
  `seraphis_tests`, `vorago_tests` and `innexus_tests`.
- **FR-064 Click-free by construction** (roadmap line 373). No policy, mode, transition or glide mode may produce a
  click on the core + Anchor output (FR-040 to FR-044). Note transitions are gated by SC-010(a)–(b); glide modes,
  including every Quantized step and every mid-glide mode switch, by SC-010(c).
- **FR-065 RT safety.** `DropEnvelope`, `DropPhaseLock`, `NoteMemory`, `RetriggerDeclick` and the extended `MonoHandler` allocate
  nothing, lock nothing and throw nothing after `prepare`. `AllocationScope` asserts zero allocations over every process
  and event call.
- **FR-066 CPU (D-9).** Per voice, `DropEnvelope` + `DropPhaseLock` (including its O(1) trigger-time plan, FR-057) + `NoteMemory` + `RetriggerDeclick`, plus `MonoHandler` glide in any
  mode, cost ≤ **0.05 %** of one core at 48 kHz, block 64. The roadmap's per-voice sum of ≈ 4.8 % (line 541) has no
  Drop line, so these must fit inside its rounding.
- **FR-067 Portability.**
  - `node tools/check-portability.js` and `tools/lint-apple-globals.js` are clean on every new or changed TU.
  - No narrowing in brace initialisation.
  - Non-finite tests live in a `-fno-fast-math` TU (the non-finite block of `dsp/tests/CMakeLists.txt`).
- **FR-068 Use-case evaluation** (roadmap lines 378–379). Four documented settings are rendered at E1 and C2 through
  the test composition (core + Anchor + Drop + `ADSREnvelope`): **808 attack**, **cinematic impact**, **techno pluck**
  and **kick/bass hybrid**.
  - WAVs and descriptor tables go to `specs/profundum-phase4-drop-note-behaviour/evidence/` for inspection.
  - Gates: boundedness, no clicks, and pairwise distinctness on the Drop descriptors.
  - The listening verdict is recorded in comply and does not gate.
- **FR-069 Test registration.**
  - `DropEnvelope`, `DropPhaseLock` and `NoteMemory` TUs go to `dsp_processors_tests`.
  - `RetriggerDeclick` goes to `dsp_primitives_tests`.
  - The `MonoHandler` glide-mode cases are appended to `mono_handler_test.cpp` or a sibling TU.
  - The composition (core + Anchor) TUs go to `dsp_systems_tests`, beside the Phase 3 TUs.
  - CPU cases are hidden `[.perf]` and run alone through `node tools/run-cpu-tests.js`.
  - A multi-minute render carries `[long]` only if it costs > 15 s and is toolchain-independent.
- **FR-070 Amplitude- and phase-continuous pitch updates (user ruling 2026-10-13, after T026).** The shared
  `HarmonicOscillatorBank` gains an **opt-in**, append-only `setContinuousFrequencyUpdates(bool)` (default off, so every other
  consumer stays bit-identical): with it, every stereo render rewrites each active partial's state across a coefficient change
  so its phase is exact and its state amplitude keeps the bank's 1/cos(ω/2) convention (r = cos(ω₁/2)/cos(ω₂/2), s′ = r·s,
  c′ = c + s·ε₁/2 − r·s·ε₂/2), so the rendered amplitude is unchanged once the bank's smoothed gain settles (`evidence/growl-probe.md`: without it a
  48-semitone Drop re-rolled partial levels by up to ±11 dB, harmonic energy +4 dB, on every recipe). `ProfundumCore::prepare`
  enables it. The Non-Goals table's "no core change" is amended by this ruling for exactly this one-line enable plus the bank
  opt-in; Phase 1's bars are re-verified by its own suites.

---

## Success Criteria

| ID | Criterion | Metric / threshold | Test (sketch) |
|---|---|---|---|
| **SC-001** | Shape families (roadmap line 376: "fitted exponent / linearity metrics") | Rebound 0, A = 24, T ∈ {10, 100, 1000} ms, all three rates, on the control signal d[n] of `DropEnvelope` alone (the `DropPhaseLock` correction is excluded, FR-056). **Exponential** (s = 0): α ≤ 0.2, and ln p(u) is linear in u over u ∈ [0.05, 0.8] with R² ≥ 0.999 (fitted rate recorded). **Linear** (s = 1/3): \|α − 0.5\| ≤ 0.005, and max deviation from 1 − u ≤ 0.01 over u ∈ [0.05, 0.95]. **Logarithmic** (s = 2/3): α ≥ 0.8, and ln(1 − p(u)) is linear in (1 − u) over the mirrored window u ∈ [0.2, 0.95] with R² ≥ 0.999. **Elastic** (s = 1): ≥ 2 zero crossings of d after the first landing; first undershoot ε = kElasticOvershootFraction·A within ±2 %; each later peak ≤ kElasticDecay × the previous one; \|d\| ≤ 1 cent by kElasticSettleFactor·T. **Continuity over [0, 2/3]:** α strictly monotonic in 64 steps, adjacent steps differing by ≤ 0.02. **Continuity over [2/3, 1]:** ε strictly monotonic in 64 steps, equal to w·ε_e (w = 3s − 2) within ±2 %, adjacent steps differing by ≤ 0.005·A. **Continuity over the whole range:** for 192 uniform steps of s ∈ [0, 1], max over u ∈ [0, kElasticSettleFactor] of \|p(u; s_k) − p(u; s_{k+1})\| ≤ 0.025 (a linear blend gives ≤ (1 + ε_e/A)/64 ≈ 0.018). | `DropEnvelope_ShapeFamilies`, `_ShapeContinuity` |
| **SC-002** | Amount and Time | On a snap trigger, d[0] = A within 1e-5 semitones. Rebound 0, s ∈ {0, 1/6, 1/3, 1/2, 2/3}, A ∈ {1, 12, 48}, T ∈ {1, 20, 200, 4000} ms, all three rates, on d[n]. **(a) Landing:** \|d[n]\| ≤ 1 cent at every n ≥ t_L + 1, and d is exactly 0.0f with `isActive()` false from t_L + 1. **(b) Not early:** the first sample with \|d\| ≤ 1 cent equals the analytic 1-cent crossing n_1c(A, T, s), computed in double from the closed form of p(u; s), within ±1 sample; n_1c is recorded per cell. (a) and (b) together pin the time base to T at both ends, without asking a zero-slope landing to cross 1 cent at T (FR-006). | `DropEnvelope_AmountTime` |
| **SC-003** | Rebound overshoot and settle (roadmap line 377) | R ∈ {0.25, 0.5, 1}, s ∈ {0, 1/3, 2/3, 5/6, 1}, A ∈ {12, 48}, T ∈ {20, 200} ms. ρ equals FR-005's law ρ(s, R) = w·ε_e + R·kMaxReboundFraction·A, with w = max(0, 3s − 2), within ±2 %. Settle (\|d\| ≤ 1 cent): within kReboundSettleFactor·T + 1 sample after t_L for s ≤ 2/3, and by kElasticSettleFactor·T for s > 2/3. R = 0 with s ≤ 2/3 gives ρ = 0 exactly (d ≥ 0 at every sample). ρ is strictly increasing in R ∈ {0, 0.25, 0.5, 1} at every tested s. | `DropEnvelope_Rebound` |
| **SC-004** | C¹ output and retrigger continuation (FR-006, FR-007) | The Phase 3 C¹ checker (per segment \|Δ²d\| ≤ 1.05 × analytic peak; slope continuity at boundaries) passes on: full drops in all families × R ∈ {0, 1}; bridged retriggers mid-fall, mid-rebound, mid-elastic and at idle; T at its minimum (the shortened bridge). **Positive control:** a cornered linear ramp fails the checker. A snap trigger is the only step allowed. | `DropEnvelope_C1`, `_C1CheckerPositiveControl`, `_RetriggerContinuation` |
| **SC-005** | Determinism and invariance | Output bit-identical across host blocks {1, 37, 512}. Normalised p(u) equal across 44.1 / 48 / 96 kHz within 1e-4. Times equal within 1 sample. A parameter change mid-drop (Amount, Time, Shape or Rebound) leaves the running drop and its `DropPhaseLock` plan bit-identical, and the next trigger uses the new values (latching, D-4, FR-003). | `DropEnvelope_BlockInvariance`, `_RateInvariance`, `_Latching` |
| **SC-006** | Glide: constant-time unchanged (Ruinae gate, FR-021) | `GlideMode::ConstantTime` output bit-identical to the frozen pre-change reference, over the existing `MonoHandler` call scripts plus a 10 000-event seeded random script. Every pre-existing `mono_handler_test.cpp` case passes unmodified. | `MonoHandler_GlideModeConstantTimeUnchanged` |
| **SC-007** | Glide: constant-rate (FR-022, D-13) | Δ ∈ {±1, ±7, ±12, ±24} semitones at T = 120 ms per octave, plus Δ = 24 at T = 10 000 ms per octave, all three rates. The glide is configured through `setPortamentoTime` only, read as ms per octave in ConstantRate (FR-026). **Duration** = T·\|Δ\|/12 within ±1 sample. For comparison, the float `LinearRamp` path misses by +8 samples (44.1 kHz, Δ = 24, 240 ms) and by +4608 (96 kHz, 2000 ms), per the existing-components row. **Slope:** the least-squares slope of the output in semitones (69 + 12·log2(f/440)) over the glide interior equals 12·1000/(T·fs) semitones per sample within 1e-6 relative, for every Δ. A mid-glide retarget keeps the slope within the same bar. | `MonoHandler_GlideModeConstantRate` |
| **SC-008** | Quantized-glide step timing (roadmap line 377; FR-023, D-13) | Δ ∈ {±1, ±5, ±12, ±24}, T ∈ {50, 500} ms, plus Δ = ±24 at T = 10 000 ms, all three rates, start notes {28, 40, 60, 76, 100} where the target stays in range. Exactly \|Δ\| steps of exactly 1 semitone each. Step k at round(k·T·fs/(1000·\|Δ\|)) within ±1 sample. The float-ramp construction misses this by up to 337 samples at 96 kHz and 500 ms (existing-components row). Every output is an integer note frequency (within 1e-4 relative of `midiNoteToFrequency`). A mid-glide retarget stays on the grid. A mid-glide mode switch moves the output ≤ 1 semitone (FR-024). The step positions are those of a constant-time underlying ramp over T with the output rounded toward the start note (FR-023). `GlideMode` has exactly the three values `{ConstantTime = 0, ConstantRate = 1, Quantized = 2}` (`static_assert`ed), with ConstantTime the default (FR-028). | `MonoHandler_GlideModeQuantizedSteps` |
| **SC-009** | Legato × retrigger × note-memory matrix (roadmap lines 377–378) | Under D-11, the roadmap's three axes are realised as Note Memory mode (whose Legato mode is the legato axis; `MonoHandler::setLegato(false)`) × Retrigger × transition kind. This 4 × 2 × 5 matrix **is** that matrix. **(a) Resolver:** all 80 (mode, retrigger, transition, audible) cells equal the FR-033 / FR-034 tables. The Continue collapse of FR-034 is asserted explicitly: {AlwaysRetrigger, EnvelopeRetriggerOnly} and {Legato, OscillatorContinuous} return `Actions` equal in every acted-on field — `phase`, `envelopeRestart`, `dropTrigger`, `dropSnap`, `anchorRearm`, `declick` — while `oscillatorRestart` keeps the raw FR-034 table bit (it is the input to the `phase` derivation and has no effect of its own under Continue, D-12; wording ruled by the main loop 2026-10-12 on T013, no bar change) in all 10 cells under Continue. **(b) Rendered:** in the core + Anchor + `ADSREnvelope` + Drop composition (MONO), one scripted sequence per mode covers every transition kind under both Retrigger settings. Per transition it asserts: *Anchor phase*, as Phase 3 SC-026(a) `phase()` continuity (≤ 1e-9 cycles) on FreeRunning and `phase()` = 0 at the effective sample on Reset; *core phase* (the core has no phase observer, `profundum_core.h:290–305`): on Reset, the core's output at the effective sample is exactly 0.0f and, with A = 0, the h1 `goertzelPhase` over the next 4 periods equals that of a fresh, prepared instance given the same Reset noteOn within 1°; on FreeRunning, a same-note noteOn with A = 0 leaves the core output bit-identical to an uninterrupted render without it, and a different-note noteOn advances the h1 `goertzelPhase`, measured over the 4 periods before and after it, by the analytic Σ f/fs within 1° (no restart); *amp envelope*, whether attack is re-entered (stage observer); *Drop*, whether it triggered (`isActive`, d at the effective sample); *Anchor re-arm*, `modeGain()` either restarting or bit-identical to the uninterrupted trace (Phase 3 SC-027(d)). **(c) POLY:** a `VoiceReuse` script (Hard steal) asserts the same per-voice actions, for both the core and the Anchor. | `NoteMemory_ResolverTable`, `ProfundumNoteBehaviour_Matrix`, `_PolyVoiceReuse` |
| **SC-010** | No clicks on retrigger under any policy, nor on any glide (roadmap line 378; FR-064) | E0, E1, E2 and C3, with `kSawAnchor` and `kGrowl`, Anchor level 1 in all four Anchor modes, all three rates and host blocks {1, 37, 512}. **(a)** Over every cell of SC-009(b) and (c), `ClickDetector` with its documented defaults (`artifact_detection.h:38`) is run and its **differential** count (a detection counts only if the phase-aligned uninterrupted reference render has none within ±N samples, N = ⌊fs / (8 f_max)⌋) is **recorded per cell, not gated** (amended by the user ruling Clarifications 2026-10-13 "T023 epoch 6 readings" (B): the steady core alone trips the detector's 5-σ derivative test at every saw / growl edge, and an undeclicked restart lands on that edge, so no count of (a) separates a click; the click gates are (b) and (c)'s bound). **(b)** The sample-delta bound (Phase 3 SC-006(c) form) holds at every transition, with S_max taken from the declick's analytic raised-cosine slope for kDeclickMs = 1.5 ms (FR-045) and the Drop's analytic peak slope. **(c) Glides:** Quantized with Δ ∈ {±1, ±12, ±24} at T ∈ {10, 500} ms; ConstantRate with Δ = ±24 at T = 10 ms per octave; mid-glide switches between every ordered pair of the three modes. `ClickDetector`'s differential count is recorded (not gated, ruling Clarifications 2026-10-13 "T023 epoch 6 readings" (B)), and the sample-delta bound holds at every Quantized step and every switch, with S_max from the analytic one-semitone frequency step on a phase-continuous oscillator. **Positive controls:** a Reset noteOn on a sounding voice **without** the declick is flagged by (b) (originally "both (a) and (b)"; (a) is recorded only, ruling Clarifications 2026-10-13 "T023 epoch 6 readings" (B)); a declick scheduled one block late (not split) is flagged by (b); a Quantized step applied with a phase reset is flagged by (c). | `ProfundumNoteBehaviour_RetriggerClickFree`, `_GlideClickFree`, `_DeclickPositiveControl` |
| **SC-011** | Declick timing and transparency (FR-041–FR-045) | Restart latency ≤ ⌈kDeclickMs·fs/1000⌉ + 1 samples with kDeclickMs = 1.5 ms, recorded per cell as the onset latency. The core and the Anchor outputs are multiplied by the bit-identical gain sample, and both reach exactly 0.0f at the effective sample. The gain is exactly 0.0f at the effective sample, and the dip passes the C¹ checker. A new note during a dip re-targets without stepping the gain. Every non-declick transition is bit-identical with and without `RetriggerDeclick` in the chain. The Anchor is L == R bit-identical in every cell. | `RetriggerDeclick_Timing`, `ProfundumNoteBehaviour_DeclickTransparency` |
| **SC-012** | Phase reference and routing (FR-050–FR-052) | **(a)** At every noteOn the core and the Anchor receive the same policy on the same boundary. After every Reset noteOn, for any A, the core's output at the effective sample is exactly 0.0f and the Anchor's `phase()` is 0 there (FR-051). With **A = 0**, θ = 0 within 1° (Phase 3 SC-026(b) method, `goertzelPhase` after the first second). The A > 0 case is SC-013's subject. **(b)** LOCK under a Drop: the Anchor's instantaneous frequency (from `phase()`) equals f_res within 1 cent throughout (Phase 3 SC-025(3)). The core's `currentFrequency()` at every pitch update equals the commanded f_core at that update's sample, clamped by the `trajectoryF0` rule (`profundum_core.h:805–811`), within 0.1 cent. The per-sample staircase error max \|12·log2(`currentFrequency()`/f_core[n])\| is recorded per cell as a diagnostic and not gated: it reaches ≈ 27 cents for 48 semitones over 60 ms at 48 kHz. **(c)** FOLLOW (r = 1) under a Drop: at every sample of the Drop, the Anchor's accumulated `phase()` advance equals the integral of the trajectory FOLLOW is specified to integrate (the core's post-Drop f0 × r on the Phase 3 cadence; Phase 3 FR-045, FR-049) within 1e-6 cycles, as in Phase 3 SC-026(c). The residual against the core's rendered pitch, Σ(f_Anchor − `currentFrequency()`)/fs, is recorded per sample. After t_s, θ meets Phase 3 SC-003's Goertzel bounds over its 1 s and 60 s windows. No windowed-phase criterion applies while the pitch sweeps. **(d)** Glide in all three modes, no Drop: Phase 3 SC-003(b)'s lock bounds hold. | `ProfundumNoteBehaviour_PhaseReference`, `_Routing` |
| **SC-013** | Post-Drop landing (FR-053, FR-054, FR-057 to FR-059; Q1) | Grid: A ∈ {6, 12, 24, 48}, T ∈ {10, 60, 200, 1000} ms, all four Shape anchors, R ∈ {0, 1}, at E0–E2 and C3, LOCK; PUNCH and GHOST on the same A, T, Shape, R grid at E1. Landing is the end of the planned curve (FR-053). **(a)** At landing, |θ| ≤ **1°** by `goertzelPhase` on rendered audio over the 4 periods starting at landing, and Phase 3 SC-003's drift bound holds for the following 10 s. **(b) No pitch motion after landing:** from landing on, d_c is exactly 0.0f and `currentFrequency()` equals f_res (after the `trajectoryF0` clamp) at every pitch update; the Anchor's pitch and phase are bit-identical to a run without the correction (FR-058). **(c) Deviation:** the correction c = d_c − d is recorded per Time (peak in semitones, per cell), and the measured peak is ≤ 1.05 × the analytic peak the plan derives (FR-054) **+ 1 ulp(A)** (plan S1 C-5, re-ruled 2026-10-12: d and d_c are float casts of doubles, so their difference carries up to 1 ulp(A) even when the analytic peak → 0; the analytic peak Γ = γ_an + the measured bridged carry, γ_an solver-independent, no slope cap under O-1); every grid cell is gated (O-1 / O-4: γ ≤ 48 st, every cell feasible, measured |γ| up to 47.9 st at T = 10 ms and 9.78 st at 60 ms on E0, recorded per Time); d_c passes the C¹ checker, with the cornered-ramp positive control failing it. **(d) Retrigger and Reset:** bridged retriggers mid-fall, mid-rebound and mid-elastic land at |θ| ≤ 1° at the final landing; a Reset noteOn zeroes the accumulator (θ = 0 at the effective sample); a glide in flight during a Drop is recorded, not gated (E-15). **Negative control:** with the correction disabled, at least one grid cell shows |θ| > 90°, proving the gate has teeth. | `ProfundumNoteBehaviour_PostDropLanding`, `DropPhaseLock_C1` |
| **SC-014** | Audibility gates (FR-060) | Core at `kSineAnchor`, E1 and C2, 48 kHz. Pitch descriptors are measured on d_core and must also pass on f_zc (D-15), after the f_zc estimator self-test passes. **Amount** 0 → 48 (T = 1000 ms, s = 1/3): the starting pitch moves monotonically by ≥ 47 semitones (on f_zc, the first half-period). **Time** 10 → 1000 ms (A = 24, s = 0): t_s moves monotonically by ≥ 10×. **Shape** 0 → 2/3 (A = 24, T = 1000 ms, R = 0): α moves by ≥ 0.6. **Shape** 2/3 → 1 (A = 24, T = 1000 ms, R = 0): ε moves by ≥ 0.1·A. **Rebound** 0 → 1 (A = 24, T = 1000 ms, s = 1/3): ρ moves by ≥ 0.2·A. **Retrigger** (AlwaysRetrigger, A = 0, E1, `kSawAnchor`, Anchor LOCK at level 1, the script below): at every onset on an audible voice, Reset vs Continue gives a difference ratio D = 10·log10(Σ(y_Reset − y_Continue)² / Σ y_Continue²) ≥ −10 dB over the onset window [arm sample, effective sample + one period of the new note], and the Reset render's minimum 0.25 ms RMS in that window is ≥ 20 dB below the Continue render's RMS over one period of the new note centred on the effective sample (reference amended by the user ruling Clarifications 2026-10-13 "T023 epoch 6 readings" (C): a 0.25 ms Continue slice can sit on a zero crossing, which made the verdict depend on waveform phase; the 20 dB figure is unchanged). **Note Memory** (Retrigger = Reset, D-12). The fixed script: n1 from silence; n2 over held n1 (Overlap); release n2 (ReturnToHeld to n1); release n1, then n3 in its release tail (Tail); release n3, ≥ 1 s of silence, then n4 (FromSilence). `ADSREnvelope` sustain 0.3. Every mode pair differs in at least one onset window, by ≥ 3 dB of RMS-envelope difference or by D ≥ −10 dB. Under Continue, the collapsed pairs of FR-034 are asserted bit-identical, not distinct. **Glide** (C2 → C4, Δ = +24, T = 500 ms, no Drop; f_zc interpolated onto a common time grid): ConstantTime vs ConstantRate differ by ≥ 500 cents at t = T/2 (analytically 600: 12 vs 6 semitones); Quantized vs ConstantRate by ≥ 500 cents at t = T/2; Quantized vs ConstantTime by a maximum deviation over the glide of ≥ 50 cents, and d_core shows exactly 24 one-semitone plateaus. | `ProfundumNoteBehaviour_Audibility`, `_PitchEstimatorSelfTest` |
| **SC-015** | Pitch-trajectory aliasing (FR-062, D-14) | At 44.1, 48 and 96 kHz, E0–E2 and C3, with `kSawAnchor` and `kGrowl`, host block 1 sample. Trajectories: Drop A = 48 at T = 1 ms in every family with R = 1, both without and with the `DropPhaseLock` correction (the corrected curve d_c); quantized and constant-rate up-glides of 24 semitones at minimum time; bridged retriggers, including upward bridges over ≤ 1 ms; upward rebound recovery. **Gate (re-ruled 2026-10-12, O-1):** the aliased power of the core's **rendered** output over each trajectory render, measured with Phase 1 SC-009's estimator (the bend-smoke method), is ≤ **−96 dBFS** per cell. The partial-index × `currentFrequency()` formula is **not** a gate: the core freezes any partial above its cap at the cap frequency (`profundum_core.h:700–712`), so the formula over-flags for ≤ 16 samples at a mask gain ≤ −73 dB and would force slope bounds on every Phase 4 trajectory (plan C-6, C-8 rise lengthening, C-10, O-2, O-3 — all dropped). Any violation of the rendered bar fails this SC, and Phase 4 fixes it (FR-062, D-14). The trajectory and the measured aliased power per cell are written to `evidence/` on every run. | `ProfundumNoteBehaviour_TrajectoryNyquist` |
| **SC-016** | Mono (FR-061) | In every SC-010 render, the Anchor output is L == R bit-identical. With pan offsets at identity, the core output is L == R bit-identical as in Phase 3, and the declick multiplies L and R by the identical float. | `ProfundumNoteBehaviour_MonoIdentity` |
| **SC-017** | Robustness (FR-009, FR-003) | Every setter of `DropEnvelope`, `RetriggerDeclick` and the glide API keeps its previous value on non-finite input and clamps out-of-range input. Output stays finite (bit test) over 10 s renders at the parameter corners. Runs in the `-fno-fast-math` TU. | `DropEnvelope_NonFinite`, `MonoHandler_GlideModeNonFinite` |
| **SC-018** | RT safety (FR-065) | `AllocationScope` reports 0 allocations over every process call, trigger, resolve, arm, glide-mode switch and noteOn / noteOff script. | `ProfundumNoteBehaviour_NoAllocation` |
| **SC-019** | CPU (FR-066, D-9) | Per voice, 48 kHz, block 64: Drop + `DropPhaseLock` trigger plan (retriggered every 100 ms, elastic, R = 1) + a `NoteMemory` resolve per note + a declick armed every 100 ms + a `MonoHandler` quantized glide. Cost ≤ **0.05 %** of one core: the case reports the median of 5; the verdict is the median of three `node tools/run-cpu-tests.js dsp_processors_tests` runs, each alone after ≥ 3 min idle, all three recorded (protocol amended by the user ruling Clarifications 2026-10-13 "T023 epoch 6 readings" (D): single lanes on near-identical trees spread 25 %, more than the gap; the bar is unchanged). | `ProfundumNoteBehaviour_CpuBudget` `[.perf]`, `node tools/run-cpu-tests.js` |
| **SC-020** | Shared-header gate (FR-063; roadmap line 378) | After the change, all of `dsp_processors_tests`, `dsp_systems_tests`, `ruinae_tests`, `seraphis_tests`, `vorago_tests` and `innexus_tests` pass, with output quoted in comply. The `mono_handler.h` diff is additions only, and its only new public members are `setGlideMode` and `glideMode` (no rate setter, FR-026): a `node` check of `git diff` finds no removed lines. | Suites + `node` diff check in comply |
| **SC-021** | Use-case renders (FR-068; roadmap line 379) | Four settings × E1 and C2: rendered and written to `evidence/`; peak finite and ≤ the core + Anchor ceiling; zero clicks (SC-010 method), which per the user ruling Clarifications 2026-10-13 "T023 epoch 6 readings" (B) means zero windows over the SC-010(b) sample-delta bound at every transition, the SC-010(a) differential count recorded in each render's CSV. Pairwise distinct on the Drop descriptors measured on d_core, by bars set for presets in their own right: every pair differs in at least one of start pitch by ≥ 6 semitones, t_s by ≥ 2×, α by ≥ 0.1, or ρ by ≥ 0.05·A (A being the larger of the pair's Amounts). The SC-014 bars measure full lever sweeps and are not reused here. Listening verdict recorded (by the user, on the `evidence/usecase-*.wav` renders; an agent cannot supply it). | `ProfundumNoteBehaviour_UseCaseRenders` |
| **SC-022** | Portability (FR-067) | `node tools/check-portability.js` and `node tools/lint-apple-globals.js` report 0 findings on the new and changed TUs, output quoted. | comply |
| **SC-023** | Enum mirror (include discipline) | A test TU `static_assert`s that `NoteMemory::PhasePolicy`, `ProfundumCore::RetriggerPhase` and `AnchorOscillator::RetriggerPhase` have equal underlying values for `Reset` and `FreeRunning`. | `NoteMemory_PhasePolicyMirror` |
| **SC-024** | Closed-form phase integrals (FR-056, FR-057) | For the four Shape anchors and 64 uniform steps of s ∈ [0, 1], R ∈ {0, 0.5, 1}, A ∈ {1, 12, 48}, T ∈ {1, 20, 200, 4000} ms, all three rates: the `DropEnvelope` hook's closed-form integral of (2^(d(t)/12) − 1) equals a test-only brute-force numeric integral of the same planned curve (double precision, step ≤ 1e-3 of the shortest feature) within 1e-6 relative. `DropPhaseLock`'s predicted Φ_x equals the Φ_x accumulated by summing f_res · (2^(d[n]/12) − 1)/fs per sample in double, within the discretisation bound the plan derives (recorded per cell). Elastic and Rebound terms are covered in every cell. | `DropEnvelope_PhaseIntegral`, `DropPhaseLock_PredictedExcessPhase` |
| **SC-025** | O(1) trigger, RT (FR-057, FR-065) | `AllocationScope` reports 0 allocations over `DropPhaseLock` prepare-free trigger and process calls. The trigger-time cost at T = 4000 ms is ≤ 2× the cost at T = 1 ms (median of 5, run alone through `node tools/run-cpu-tests.js`), so no per-sample dry run exists; comply cites the trigger code. | `DropPhaseLock_NoAllocation`, `DropPhaseLock_TriggerCost` `[.perf]` |
| **SC-026** | FOLLOW and Anchor untouched (FR-058) | In FOLLOW (r = 1) over the SC-013 grid at E1: `DropPhaseLock` output d_c is bit-identical to d (c ≡ 0), and θ meets Phase 3 SC-003's Goertzel bounds after the Drop. In LOCK, PUNCH and GHOST: the Anchor's pitch, `phase()` trace and mode gain are bit-identical between a run with `DropPhaseLock` enabled and a run with it bypassed, and its output satisfies output_on = output_off × (a1_on / a1_off) per sample within ≤ −120 dB re the Anchor's peak, in the bridged `AnchorUntouched` case and over every SC-013 cell; the raw output difference is recorded per cell, not gated (Phase 9 listening item). Amended by the user rulings Clarifications 2026-10-13 "T023 epoch 5 readings" (A) and "T023 epoch 6 readings" (A): the Anchor level follows the core's delivered h1 (Phase 3), which the correction moves inside the Drop, so output bit-identity was never reachable by design. | `DropPhaseLock_FollowBypass`, `ProfundumNoteBehaviour_AnchorUntouched` |
| **SC-027** | Phase 11 glide-knob handoff (FR-027) | Comply quotes the handoff clause as written into the Phase 11 section of the roadmap: the one portamento knob is labelled in milliseconds (ConstantTime), milliseconds per octave (ConstantRate) and milliseconds (Quantized). | comply |
| **SC-028** | Spectrum preserved across a Drop (FR-070; ruled 2026-10-13) | Core alone through a Drop-shaped `f0PerSample` trajectory (exponential family, k = 8): fs ∈ {44.1, 48, 96 kHz} × {E1, C3} × {`kGrowl`, `kSawAnchor`, mid grid} × A ∈ {12, 24, 48} st × T ∈ {1, 60, 1000} ms, 162 cells, 512-sample blocks, 1 s at the note before, measured 4096 samples after the landing over ≥ 0.9 s of integer periods. Per partial with a pre-Drop level ≥ −60 dB re h1: \|Δ\| ≤ **0.1 dB** when the partial is within 40 dB of h1, ≤ **0.5 dB** between −40 and −60 dB (single-bin leakage); harmonic energy \|Δ\| ≤ **0.05 dB**. Positive control: the bank's default path re-rolls at least one partial by > 1 dB on a ±53 st staircase (`HarmonicOscillatorBank_ContinuousFrequencyUpdates`), while the opt-in keeps every partial within 0.05 dB and the first rendered sample after a change bit-identical. | `ProfundumCore_SpectrumPreservedAcrossDrop`, `HarmonicOscillatorBank_ContinuousFrequencyUpdates` |

---

## Edge Cases

- **E-1 Amount 0.** The Drop is inert: d ≡ 0, and a trigger changes nothing, bit-identically. Under FR-050 f_core = f_res,
  so no excess phase accrues.
- **E-2 Minimum Time (1 ms) at high Amount.** The fall spans fewer than 50 samples at 44.1 kHz. The bridge shortens
  (FR-007), C¹ still holds (SC-004), and SC-015 covers Nyquist.
- **E-3 Drop start above the core's cap.** f_res·2^(A/12) may exceed `SpectralShapeRecipe::capFrequency(fs)`.
  - The core clamps f0 to the cap (`profundum_core.h:795–799`), so the top of the fall is clipped in pitch. That is
    recorded, not an error.
  - The Drop's own output is not clamped.
  - FR-053's Φ_x uses the frequency the core actually rendered (the held f0 after the cap clamp).
- **E-4 Retrigger at every sample.** `trigger(false)` on every sample keeps d bounded in [−ρ_max − 2.7 st, A] and C¹, because each
  bridge starts from the current (d, d') (plan S1 C-8, re-ruled 2026-10-12: the 2.7 st brake excursion occurs only at T ≈ 1 ms on
  the steepest family; ≤ 0.5 st otherwise).
- **E-5 Declick re-armed during its own dip** (FR-043). The last note wins, and the gain never steps.
- **E-6 Note-off during a declick.** The deferred noteOn is still issued, so the voice is in a defined state. The amp
  envelope then releases from its restarted attack, and no stuck state can arise.
- **E-7 Glide time 0.** All three modes snap, by today's `updatePortamentoTarget` rule (`mono_handler.h:451`). Quantized
  with T = 0 snaps to the target.
- **E-8 Glide across a sample-rate change.** `prepare` keeps the glide position, as today (`mono_handler.h:121–125`).
  Constant-rate and Quantized recompute their increments.
- **E-9 Quantized glide from a fractional start** (an interrupted constant-time glide followed by a mode switch). The
  output joins the integer grid, moving ≤ 1 semitone in one sample (FR-024).
- **E-10 MONO stack overflow** (a 17th held key). `MonoHandler` drops the oldest entry (`mono_handler.h:157–160`), and
  Note Memory classifies the transition as `Overlap`.
- **E-11 Sample-rate change mid-drop.** `prepare` resets the Drop (FR-002), and the caller re-issues the note through
  the normal path.
- **E-12 POLY steal of a voice in its declick.** The new note re-targets the deferred noteOn (FR-043).
- **E-13 Determinism / seeds.** Phase 4 has no random source. Every output is a pure function of the call sequence and
  is bit-identical across host-block partitions (SC-005, SC-011). The only seeded artefact is SC-006's random event
  script, whose seed is fixed in the test.
- **E-14 Non-finite inputs.** Every setter sanitises its input (SC-017). `MonoHandler`'s existing NaN/Inf guard
  (`mono_handler.h:281–283`) stays, and the glide-mode setter rejects out-of-enum values.
- **E-15 Glide during a Drop.** The `DropPhaseLock` plan takes f_res at trigger (FR-059), so a Glide in flight changes the actual excess phase relative to the plan. The residual θ at landing is recorded in SC-013(d), not gated.
- **E-16 Very short Drop.** For a tiny T (E-2) the correction is spread over the Drop's own duration; its peak deviation follows the FR-054 bound, which is recorded per Time (SC-013(c)).

---

## Decisions taken where the roadmap is silent (D-2, D-4, D-5, D-6, D-8, D-12 ratified in the 2026-10-12 clarify session; the rest as drafted)

- **D-1 Drop direction, units and ranges.**
  - The fall runs from +A semitones down to the note. This matches "how far the pitch falls" (roadmap line 362) and
    the concept's 808 and kick use cases.
  - A ∈ [0, 48] semitones, T ∈ [1, 4000] ms, s ∈ [0, 1], R ∈ [0, 1].
  - Phase 3's SC-025 used a −12 semitone dip only as a generic trajectory fixture; it does not define Drop.
  - Phase 8 morphs Drop times logarithmically (roadmap line 493), so T must be > 0; the minimum is 1 ms.
- **D-2 Shape anchors and the meaning of elastic (ratified Q4).**
  - The anchors sit at s = 0, 1/3, 2/3 and 1.
  - "Elastic" is a damped oscillation around the target after the fall: kElasticOvershootFraction = 0.15 (first
    undershoot ε_e = 0.15 · Amount), kElasticDecay = 0.5 (each later peak at most half the previous one) and
    kElasticSettleFactor = 2 (settled within 2 · Time). It composes additively with Rebound (D-3).
  - Between neighbouring anchors the blend weight is linear in s within each third, so the logarithmic → elastic segment
    is continuous and its undershoot grows as w·ε_e (FR-004).
  - Rejected: an S-curve with a soft start and a soft landing.
- **D-3 Rebound and bridge constants.** kMaxReboundFraction = 0.25 of A, kReboundSettleFactor = 0.5, kDropBridgeMs = 1.
  Rebound composes additively with the elastic undershoot: ρ(s, R) = w·ε_e + R·kMaxReboundFraction·A (FR-005). The
  alternatives max(ε_e, R·kMaxReboundFraction·A) and (1 + R)·ε_e were rejected: the first is flat in R until R·0.25 > 0.15,
  so Rebound would be inert over the low 60 % of its range on elastic; the second makes Rebound's reach depend on Shape.
- **D-4 Latch at trigger (ratified Q5).** Amount, Time, Shape and Rebound are latched at trigger. This is
  deterministic, C¹ by construction, and the phase correction (FR-053) is computed once per trigger. Phase 8's Morph and
  Phase 9's Motion therefore affect the next note's Drop, not the one in flight. Rejected: live re-planning, full or
  hybrid.
- **D-5 Quantized law (ratified Q8).** A constant-time underlying ramp over T, with the output stepped per semitone toward
  the target (ceil toward the start note): |Δ| evenly spaced steps at k·T/|Δ|. `GlideMode` stays three-valued
  (FR-028). Rejected: a constant-rate base; quantize as an orthogonal modifier.
- **D-6 Constant-rate units (ratified Q7).** `setPortamentoTime` is reused, read as milliseconds per octave in
  ConstantRate. No new setter is added to the shared `MonoHandler` (FR-026). Phase 11 labels the one glide knob per
  mode (FR-027). Rejected: ms per semitone; a separate `setGlideRate`.
- **D-7 Note Memory matrix, default and class.**
  - The matrix is FR-034's table. The default is AlwaysRetrigger + Reset.
  - The policy is an L2 pure resolver (`NoteMemory`), not a voice; Phase 9 composes it.
- **D-8 Click-free mechanism (ratified Q6).** A kDeclickMs = 1.5 ms C¹ raised-cosine gain dip to exactly 0 with the Reset
  noteOn deferred to the zero sample (`RetriggerDeclick`, `samplesUntilSwitch` and a block split). The core and the
  Anchor dip together (FR-045). Rejected alternatives:
  - **0.5 ms** dip: S_max would rise 3×.
  - **Core-only dip:** it would break the common phase reference.
  - **Dual-instance crossfade** (the old core keeps running while a new one starts): it doubles the per-voice core
    cost, against Phase 1's 1 % and Phase 2's 1.5 % budgets.
  - **Waiting for a zero crossing:** a 64-partial spectrum has no guaranteed common zero, and the wait is unbounded at
    E0 (48.5 ms period).
- **D-9 CPU bar.** ≤ 0.05 % per voice, because the roadmap's ≈ 4.8 % per-voice sum (line 541) allocates nothing to Drop.
- **D-10 Representative amp envelope.** The tests use `ADSREnvelope` with Hard retrigger. Phase 9 rules the shipped
  type (roadmap line 519). Note Memory's `envelopeRestart` maps onto any gate-on that re-enters attack.
- **D-11 The roadmap's "legato" axis.** The SC "legato × retrigger × note memory matrix" (roadmap line 377) is realised
  as Note Memory mode × Retrigger × transition kind. Note Memory's Legato mode is the legato axis, and `MonoHandler`'s
  own legato flag is held false (FR-034). Nothing is lost: `MonoHandler`'s legato flag only suppresses the retrigger
  flag on overlapping notes (`mono_handler.h:176–179`) and does not gate glide (186–199), and Note Memory owns that
  decision. SC-009 is that matrix under this ruling.
- **D-12 Retrigger scope and the Continue collapse (ratified Q3).** Retrigger acts only where oscillatorRestart = 1
  (FR-034 lists the no-op cells). Under Continue, Note Memory's four modes collapse to two: {AlwaysRetrigger,
  EnvelopeRetriggerOnly} and {Legato, OscillatorContinuous} (FR-036). Retrigger is purely oscillator phase behaviour
  (concept §12, line 570); `dropTrigger` and `anchorRearm` stay equal to `envelopeRestart`. SC-014 gates mode
  distinctness with Retrigger = Reset, and SC-009(a) asserts the collapse. Rejected: rewiring Drop or re-arm to the
  oscillator column; an Anchor-only phase reset under Continue.
- **D-13 Glide-law precision.** ConstantRate and Quantized keep their position in double, in appended private state,
  and never run on the float `portamentoRamp_` or on `SlewLimiter`. ConstantTime keeps the frozen float path, so it
  stays bit-identical (FR-021). The reason is measured: the float ramp misses SC-007 / SC-008's ±1-sample bars by up to
  +4608 samples (existing-components `LinearRamp` row). The rejected alternative was to relax SC-007 / SC-008 to a
  float-accumulation tolerance, which would let a 2 s glide end 48 ms late at 96 kHz.
- **D-14 Where a trajectory-Nyquist fix goes.** If SC-015 finds a violation, Phase 4 fixes it first in its own
  trajectories: an upward-slope bound per control interval on the Drop bridge, the rebound recovery and the glide laws.
  An append-only change to `ProfundumCore`'s mask cadence is the fallback, and it needs a clarify ruling, because the
  Non-Goals table plans no core change. *(O-1, 2026-10-12: with the rendered gate no slope bound is needed; the first remedy
  clause applies only if SC-015's rendered bar is ever red.)*
- **D-15 Pitch measurement method.** Pitch descriptors are measured on the core's rendered pitch trace d_core
  (`currentFrequency()` per sample) and corroborated on audio by the zero-crossing half-period estimator f_zc, which
  has its own self-test against an analytic chirp. A phase-unwrapped analytic signal was rejected: at E1 (24 ms period)
  its Hilbert FIR would smear time far beyond 10 ms Drop features, and a per-sample estimator on float32 audio cannot
  resolve 1 cent at E1 (cos ω moves about 1.7e-8 per cent, below float32 epsilon). Unlike Phase 3 SC-008(b), the core
  has no `phase()` observer to read.

- **D-16 Phase landing inside the Drop (ratified Q1, Q2).** The phase correction is a separate shipped L2 component,
  `DropPhaseLock`, composed after `DropEnvelope` (FR-055). It works inside the Drop with a closed-form O(1) trigger
  plan (FR-056, FR-057), and leaves the Anchor untouched (FR-058). `DropEnvelope` stays pitch-agnostic with clean shape
  tests. Rejected, from the drafted options: a phase-landing tail after the Drop; an Anchor phase slew; stretching T so
  that Φ_x is an integer (it fails for small Φ_x and cannot anticipate a glide); accepting the offset; folding the
  correction into `DropEnvelope`.


---

## Traceability

| Roadmap statement (line) | FR / SC |
|---|---|
| `DropEnvelope` Amount / Time / Shape / Rebound (362–364) | FR-003–FR-005, FR-055–FR-057; SC-001–SC-003, SC-024 |
| "C¹ output, retriggerable with continuation, no allocation" (364) | FR-006, FR-007, FR-065; SC-004, SC-018 |
| "Reusable (Membrum's … stays where it is)" (364–365) | Non-goals; existing-components row |
| `MonoHandler` constant-time / constant-rate / quantized, append-only, Ruinae unchanged (366–367) | FR-020–FR-028, FR-063; SC-006–SC-008, SC-020, SC-027 |
| Note Memory modes, the core's retrigger policy, the amp / drop envelopes (368–369) | FR-030–FR-035; SC-009 |
| Note Memory decides the re-arm event; common phase reference before Drop (369–372) | FR-033, FR-036, FR-050–FR-059; SC-009, SC-012, SC-013, SC-024–SC-026 |
| Retrigger reset / continue, click-free by construction (373–374) | FR-034 (D-12), FR-036, FR-040–FR-045, FR-060, FR-064; SC-010, SC-011, SC-014 (Retrigger row) |
| Curve-shape tests, rebound, quantized step timing, the legato × retrigger × note memory matrix (D-11), no clicks, `ruinae_tests` green (376–378) | SC-001, SC-003, SC-008, SC-009, SC-010, SC-020 |
| Four use-case renders inspected (378–379) | FR-068; SC-021 |
| Audibility / mono / anti-aliasing theme FRs (832–837) | FR-060–FR-062; SC-014–SC-016 |
| CPU budgets are FRs (840–841) | FR-066; SC-019 |
| Portability, layer discipline, ODR (838–845) | FR-067, include discipline, New components; SC-022, SC-023 |
| Shared-component changes keep Ruinae, Seraphis, Vorago and Innexus green (846–847) | FR-063; SC-020 |

## Assumptions

- Phase 4 composes the shipped `ProfundumCore` and `AnchorOscillator` without modifying them. `DropPhaseLock` and the
  other Phase 4 components are shipped; the routing and the declick scheduling in the composition are test-only
  (`NoteBehaviourHarness`) until Phase 9's `ProfundumVoice` composes them. The composition is not a voice.
- The quantized-glide grid is the MIDI note numbers on the 12-TET, A4 = 440 Hz grid (`mono_handler.h:461–463`). Phase 9
  applies the tuning reference and pitch bend after glide (`NoteProcessor`).
- "Audible voice" means the amp envelope is not idle. Phase 9 supplies that flag to `NoteMemory::resolve`.

## Review notes

- **2026-10-12 specify-stage review (20 issues).** All were applied; none was rejected. No threshold was relaxed. Two
  bars were re-pointed with reasons recorded where they sit. SC-002 now pins the analytic 1-cent crossing and the
  landing, because a C¹ zero-slope landing cannot cross 1 cent at T. SC-021 uses preset-scale distinctness bars,
  because the SC-014 bars measure full lever sweeps. The float `LinearRamp` drift figures quoted by the review were
  reproduced in Node (`Math.fround`) before D-13 was written.

## Clarifications

### Session 2026-10-12

- Q: Which mechanism returns core-h1 / Anchor phase to 0° after a Drop (OQ-A), and with what bound? → A: Core-side correction **inside** the Drop, not a tail after landing. Parameters are latched at trigger, so Φ_x over the planned curve (including elastic ringing and rebound) is known at trigger; a C¹ correction spread over the Drop's own duration removes the fractional cycle, so core h1 and the Anchor meet at |θ| ≤ 1° at landing with no pitch motion after it. The Anchor is never altered. The deviation from the planned curve is bounded and recorded per Time (at most 0.5 cycle of f_res spread over the drop). The closed-form per-family phase integrals make the trigger-time computation O(1). FOLLOW needs no correction. [FR-053, FR-054, FR-057, FR-058, FR-059, SC-013, SC-024, SC-025, SC-026]
- Q: Where does the shipped re-lock logic live? → A: A separate shipped L2 corrector (`DropPhaseLock`, ODR-swept) composed after `DropEnvelope`; `DropEnvelope` stays pitch-agnostic (SC-001 to SC-005 exclude the correction) and gains a small closed-form phase-integral hook. Routing and declick scheduling stay test-only until Phase 9. [FR-055, FR-056, SC-001, SC-024, SC-025]
- Q: Accept the Continue collapse of the four Note Memory modes? → A: Yes. Retrigger is purely oscillator phase; under Continue {AlwaysRetrigger, EnvelopeRetriggerOnly} and {Legato, OscillatorContinuous} resolve to identical Actions; SC-014 runs under Reset; `dropTrigger` and `anchorRearm` stay equal to `envelopeRestart`. [FR-034, FR-036, SC-009(a), SC-014]
- Q: What does the elastic Shape anchor mean? → A: Damped oscillation around the target after the fall: first undershoot 0.15 · Amount, each later peak ≤ 0.5× the previous, settled within 2 · Time; additive with Rebound; the Shape sweep stays continuous through [2/3, 1]; the corrector lands the phase at the end of the settled curve. [FR-004, FR-005, FR-053, SC-001, SC-003, SC-013]
- Q: Are Drop parameters latched at trigger? → A: Yes (Amount, Time, Shape, Rebound). Deterministic, C¹ by construction, the phase correction is computed once per trigger; Morph (Phase 8) and Motion (Phase 9) act on the next note's Drop. Live re-planning is rejected. [FR-003, FR-059, SC-005]
- Q: Is the 1.5 ms dip-to-zero with deferred Reset the click-free mechanism? → A: Yes. kDeclickMs = 1.5 ms C¹ raised cosine to exactly 0, Reset noteOn deferred to the zero sample; core and Anchor dip together; onset latency ≤ 1.5 ms recorded in SC-011; S_max of SC-010(b) derives from 1.5 ms. [FR-041, FR-042, FR-045, SC-010(b), SC-011]
- Q: How is the constant-rate glide rate expressed? → A: `setPortamentoTime` read as ms per octave in ConstantRate; no new setter on `MonoHandler`; duration = T_ms · |Δ semitones| / 12; Phase 11 labels the one knob per mode. [FR-022, FR-026, FR-027, SC-007, SC-020, SC-027]
- Q: What law drives the quantized glide? → A: A constant-time underlying ramp over T with the output stepped per semitone toward the target (ceil toward the start note), |Δ| steps at k · T/|Δ|, ±1 sample; `GlideMode` stays three-valued with ConstantTime the default and bit-identical. [FR-020, FR-021, FR-023, FR-028, SC-006, SC-008]

### Session 2026-10-12 — plan S1 ratification and open questions

The user ruled O-1 and O-4 individually and ratified C-1 … C-5, C-7 … C-9 and C-11 … C-16 as a batch at the plan's recommended
resolution, adjusted for O-1. Every figure below is from `drop-model.js` (plan S10).

- O-1 re-ruled: SC-015 measures the **rendered** aliased power (Phase 1 SC-009's estimator, bar ≤ −96 dBFS per trajectory cell)
  instead of the partial-index × `currentFrequency()` formula, because the core freezes partials above its cap (the formula
  over-flags for ≤ 16 samples at ≤ −73 dB). Consequences: no slope bound on any Phase 4 trajectory — C-6's γ clamp and its 23 of 384
  infeasible cells at T = 10 ms, C-8's rate-dependent rise lengthening (480 samples for 0 → 48 st), C-10's σ_up = 0.15 st/sample
  constant and its private floor on ConstantRate / Quantized, O-2 (ConstantTime glides shorter than |Δ|/(0.15·fs)) and O-3 are
  moot and recorded. [FR-062, SC-015, SC-013, FR-007, D-14]
- O-4 ruled: no correction ceiling below the 48 st clamp; the lock lands within 1° in every SC-013 cell; |γ| recorded per Time
  (max 47.9 st at T = 10 ms, 9.78 st at 60 ms, 4.47 st at 200 ms, 0.48 st at 1 s). Rejected: a 12 st ceiling (θ arbitrary on
  808-style attacks), a Time-tied ceiling. [FR-054, SC-013]
- C-1 ratified: one new Layer 0 header `core/drop_curve.h` (`DropCurve`, closed-form evaluation and the quadrature) included by
  both Layer 2 components; `DropEnvelope::plannedCurve()` / `phaseIntegralSeconds()`. [FR-055, FR-056]
- C-2 re-ruled (wording, no bar change): "closed-form" = O(1) fixed-panel Gauss–Legendre 8 of the latched curve; worst error
  8.7e-9 relative (bar 1e-6). [FR-056, FR-057, SC-024]
- C-3 re-ruled (refinement, no bar change): Φ_x is predicted on the core's 16-sample pitch staircase and its [8 Hz, cap] clamp
  (first 4 pitch updates exact, clamp-aware remainder with Euler–Maclaurin endpoint terms): worst prediction error 0.218° over the
  SC-013 grid at 44.1 / 48 / 96 kHz; the smooth integral without the clamp misses by 37.6° where an E0 rebound dips to 6.8 Hz.
  [FR-053, SC-013, SC-024]
- C-4 re-ruled: landing target = the neighbouring whole cycle with the smaller |γ|; |r| < 1 cycle; every cell feasible. [FR-053]
- C-5 re-ruled: correction profile c = γ·16x²(1−x)² (peak factor 15/8, max|b′| 3.079); SC-013(c) = 1.05·Γ + 1 ulp(A) with a
  solver-independent γ_an; measured worst |γ|/γ_an 1.0192, 0 cells above 1.05. [FR-054, SC-013]
- C-7 re-ruled: with Rebound the fall runs A → −ρ_R along the family shape and returns; ρ = R·0.25·A exact. [FR-005, FR-006]
- C-8 re-ruled (two bars): bridge = brake → rise → onset fillet, length L_br + L_r with L_r = L_min under O-1; E-4's bound
  [−ρ_max − 2.7 st, A]. [FR-007, E-4]
- C-9 ratified (recorded): the elastic / rebound upswings rise up to 0.418 st/sample (E0, 44.1 kHz, A = 48, T = 10 ms) and are not
  slope-bounded; handed to Phase 9 as a recorded figure (no guard needed under the rendered gate unless Phase 9's own SC-015-style
  measurement is red). [FR-062, SC-003, FR-010]
- C-11 ratified: a Quantized retarget / entry restarts from the current integer output note. [FR-023, FR-024]
- C-12 ratified: k = 8 (exponential α 0.12495, linear 0.49979, logarithmic 0.87338), fillets 0.05, elastic peak ratio 0.45 with
  knots at 1.00 / 1.15 / 1.30 / 1.45 / 1.60 / 1.75 / 2.00 T, landing slope 1.5·A/T, brake 0.5 st, kDeclickMs 1.5; defined once in
  `DropCurve` (kDeclickMs in `RetriggerDeclick`); `kMaxUpwardSemitonesPerSample` is NOT defined (O-1). [D-2, D-3]
- C-13 ratified: SC-024's discretisation bound 2e-5 cycles absolute (measured 1.41e-5). [SC-024]
- C-14 ratified: `DropPhaseLock::trigger(curve, fResHz, gridPhase, minF0Hz, capHz, kind)`; the systems TU static_asserts the core's
  constants and `kPitchUpdateInterval = 16`. [FR-055, SC-023]
- C-15 ratified: declick g[k] = g₀·½(1 + cos(π(k+1)/L_d)), switch sample written exactly 0.0f, restart latency ≤ 66 samples at
  44.1 kHz, S_max = g₀·π/(2L_d). [FR-041, FR-042, SC-011]
- C-16 ratified: CPU is benched first (T005) at the worst-case solve; the 0.05 % bar is never relaxed. [SC-019]
- T013 wording (main loop, 2026-10-12): SC-009(a)'s Continue collapse compares the six acted-on fields of `Actions`; `oscillatorRestart` keeps the raw FR-034 bit, which differs between the collapsed pairs by construction (Overlap (1,1) vs (0,1) etc.) and is only the input to the `phase` derivation. Resolves the contradiction between FR-033 / plan S6 ("raw column") and SC-009(a) / Q3 ("identical Actions") at the plan's recommended option A; no bar changes. [FR-033, FR-034, SC-009]

### Session 2026-10-13 — T026 full-suite reds and the bank ruling

- Bank continuity ruled (user, 2026-10-13, option "opt-in bank fix, enabled by Profundum"): the T026 SC-015 reds (kGrowl −29.98 dBFS
  at E1 / 44.1 kHz, bypass variant −57.4) were probed on the core alone (`evidence/growl-probe.md`): the core is clean (≤ −125 dBFS)
  on every such trajectory, so the composition owns that red (Defects found). The probe found instead that the bank re-rolled partial
  amplitudes after any Drop ≥ 12 st (harmonic energy +4.0 dB, worst partial +10.7 dB at C3 kGrowl A 48 T 60 ms; +2.1 dB at A 24;
  the MCF state keeps (s, c) across a coefficient change). New FR-070 (append-only opt-in `setContinuousFrequencyUpdates`, default
  off; `ProfundumCore::prepare` enables it) and new SC-028, re-ruled into the Success Criteria table with the bars 0.1 dB /
  0.5 dB / 0.05 dB above (measured: energy within 0.0007 dB, worst partial −0.246 dB at −50 dB re h1; bank test −0.0001 dB,
  control −5.82 dB). Rejected: fixing the bank for every consumer (Innexus / Membrum / Seraphis / Vorago renders would change);
  deferring to Phase 9. [FR-070, SC-028, SC-015, SC-013]
- Workflow defect recorded (main loop, 2026-10-13): `profundum-phase.js` gated Groups 10–15 on `dsp_primitives_tests` only (no
  "profundum" target exists for a DSP phase; the fallback took the first listed target), so T018–T022's cases first ran at T026.
  Fixed in the workflow (narrowed gates now derive their targets from the test TUs a group touches).
- FR-070 amplitude convention (main loop, 2026-10-13, after the first regression run): the first rewrite kept the state amplitude A
  fixed, which is exact for the recurrence but not for the bank, whose seed is the unit circle and whose antiAliasGain carries
  cos(ω/2): a permanent pitch move then scaled partials by cos(ω₂/2)/cos(ω₁/2) and Phase 1's ±12 st bend read −47 dB / 34 clicks
  against its reference (−60 dB / 0 before). Corrected to r = cos(ω₁/2)/cos(ω₂/2), s′ = r·s, c′ = c + s·ε₁/2 − r·s·ε₂/2: the bend
  now reads **−67.6 dB / 0 clicks** against Phase 1's reference re-expressed in the same law (Phase 1 Clarifications
  2026-10-13), SC-028 unchanged (energy within 0.0007 dB), the bank test unchanged (−0.0001 dB). Phase 2's SC-004(a) null is
  re-scoped: the frozen Phase 1 core is the reference on static-pitch fixtures (bit-identical); on pitch-motion fixtures the
  reference is the live default core (Phase 2 Clarifications 2026-10-13). [FR-070, SC-028]
### Session 2026-10-13 — T023 epoch 5 readings (two user rulings, three main-loop applications of existing rulings)

T023 (epoch 5, `evidence/run-log/T023.txt`) fixed two unconverged SC-013 solves (Illinois continuation in `drop_phase_lock.h`,
only when the residual is still > 1e-6 cycles), two harness defects, and tightened the Phase 4 per-sample path with every reading
unchanged; SC-015, NoAllocation, the declick positive control and FOLLOW are green on re-measure. Four readings collided with bars:

- **(A) SC-026 re-scoped (user ruling, recommended option):** in LOCK, PUNCH and GHOST the Anchor's **pitch, `phase()` trace and
  mode gain** are bit-identical between a run with `DropPhaseLock` enabled and one with it bypassed. The Anchor's **output** is
  not, by design: Phase 3 ruled the Anchor level relative to the core's delivered h1, and `SpectralShapeRecipe::applyMask`
  renormalises per f, so the delivered h1 follows the corrected pitch inside the Drop. The output difference is **recorded per
  cell and gated at ≤ −60 dB re the Anchor's peak** (measured ≈ −70 dB: max |diff| 8.0e-5 against a 0.2518 peak in LOCK / GHOST,
  1.23e-4 against 0.3775 in PUNCH). SC-013(b)'s wording ("pitch and phase bit-identical") already matches. Rejected: feeding a1
  from a correction-independent h1 (changes the Phase 9 composition contract); a core change (out of scope, FR-070 is in).
  [SC-026, SC-013(b), FR-058]
- **(B) SC-010(a) and SC-021 count differentially (user ruling, recommended option):** `ClickDetector` with its documented defaults
  flags the *steady, transition-free* core at every note, rate and recipe (12–115 detections per 0.5 s: the 5-σ derivative-outlier
  test fires on every band-limited saw / growl edge), so "zero" cannot be reached by any Phase 4 component. Ruled: a detection
  counts only if the **uninterrupted reference render** of the same cell (the sounding note(s) rendered without the transition,
  phase-aligned to the transition render where a Reset re-seeds) has **no detection within ±N samples** of it, same detector,
  same defaults, N fixed per cell at ≤ 1/8 of the sounding period and recorded with the method in the test. The bar stays
  **zero** such detections. Guard: the three SC-010 positive controls must remain flagged by (a) under the differential count.
  Rejected: calibrating the threshold on the reference (a click smaller than the waveform's own edges becomes invisible);
  dropping (a). [SC-010(a), SC-010(c), SC-021]
- **(B′) SC-010(b) D term (main loop; follows from the bar's own Phase 3 SC-006(c) form):** the harness took D and P from steady
  references; after a +12 st Drop snap the core's Phase 1 shape slew (mask taper after a pitch jump) exceeds every steady
  reference for 48–150 samples (worst ratio 1.67 at C3 / E2). D is now taken from the **core's delivered gains over the window**
  (the actual trajectory), P and the 1.1 factor unchanged; the positive controls must remain flagged. [SC-010(b)]
- **(C) SC-014 Retrigger dip window (main loop; interpretation of the bar's text, number unchanged):** the Continue render's
  0.25 ms RMS is measured in the **same window as the Reset render's minimum** (centred on the same sample), not at the Continue
  render's own minimum, which always contained a zero crossing and made the verdict depend on waveform phase (onset 2 read
  Reset −51.74 dB vs Continue −53.71 dB with the declick exactly 0 at the effective sample). Bar ≥ 20 dB unchanged. [SC-014]
- **(D) SC-019 (main loop; the 2026-10-12 T005 ruling (ii) already decides it):** the per-hold mirror accumulation is REQUIRED
  "where it keeps the output bit-identical or within the existing bars, SC-024 is the judge"; T023 applies it, SC-024 and SC-013
  judge. Lever (4) (lazy bracket ends) stays off: its condition (residual < 1e-9 cycles on every SC-013 cell) is not met
  (float floor ≈ 1e-8). The 0.05 % bar is unchanged; the final tree before this lever reads 0.0470 / 0.0530 / 0.0514 % in the
  official lane (T028 before T023: 0.0843 / 0.0709 %). [SC-019, SC-024]
### Session 2026-10-13 — T023 epoch 6 readings (four user rulings; every bar number unchanged)

T023 epoch 6 (`evidence/run-log/T023.txt`) applied the epoch-5 decisions. (B′) is green (0 windows over the bound, worst ratio
0.8536; the positive controls still fail (b) at 1.99 / 1.99 / 2.11). The other four came out red for reasons the epoch-5
measurement could not show; the user ruled each at the recommended option:

- **(D) SC-019 — reading-changing levers authorised within the existing bars; verdict = median of three isolated lanes.** After
  the per-hold mirror the lane read **0.0598 %** (12.47 ns/sample); near-identical trees read 0.0470 / 0.0530 / 0.0514 / 0.0598 %,
  a 25 % spread larger than the gap. Per component (ns/sample, min of 15): env 2.46–3.19, env+lock 7.31, glide 2.15–2.20,
  declick 0.48–0.70, memory 0.06, full 9.61–11.23; the T005 bench figures are 2.93 / 1.91 / 1.16. Ruled: the T005 ruling (ii)
  clause is generalised to every Phase 4 per-sample path — any lever (float instead of double in the envelope / correction
  paths, the correction-curve recurrence, …) is allowed if SC-024 (1e-6 relative), SC-013 and every other Phase 4 bar still
  pass; bit-identity is not required. The **bar stays 0.05 %**; the **measurement protocol** is the median of three
  `node tools/run-cpu-tests.js dsp_processors_tests` runs, each alone after ≥ 3 min idle, recorded individually. Rejected:
  single-lane verdict (one thermal outlier decides); shipping Phase 4 red on SC-019. [SC-019, SC-024, SC-013]
- **(B) SC-010(a) / SC-021 — the sample-delta bound (b) is the click gate; (a) is a recorded differential count with no zero
  bar.** The differential count was implemented (phase-aligned held-note reference, Drop mirrored, N = ⌊fs / (8 f_max)⌋) and
  the guard failed: every undeclicked restart lands at h1 phase 0, which is the saw's own edge, 0.36 / 0.36 / 0.50 samples
  from a reference detection, so position-only matching cannot flag it for any N ≥ 1; residual counted detections remained
  (34 / 9234 smoke, 22.6k–28.0k in the retrigger grids). (b) flags all three positive controls at ≈ 2× and holds on every cell
  (worst 0.85 under (B′)). Ruled: (b) and (c)'s bound are the gates; (a)'s differential count is recorded per cell for the
  compliance table; SC-021's "zero clicks (SC-010 method)" means (b). Rejected: a magnitude-aware differential (untested, the
  restart jump depends on the old waveform's instantaneous value); carrying (a) red. [SC-010(a), SC-010(c), SC-021]
- **(A) SC-026 — the Anchor waveform is gated with the level feed divided out; the raw level excursion is recorded per cell,
  not gated.** At the −60 dB ceiling the bridged `AnchorUntouched` case reads −69.95 / −69.74 / −69.95 dB, but 510 of 1536
  SC-013 snap landing cells are over it (E0 335 / 384, worst −8.63 dB; E1 137 / 384, worst −9.58; E2 15 / 384; C3 23 / 384;
  PunchGhostE1 worst −9.54), mostly R = 1 and short T where |γ| reaches ≈ 6 st and the mask renormalisation moves a1 — the
  Phase 3 design (Anchor level relative to the delivered h1) acting on the ruled correction. Ruled: assert per sample
  output_on == output_off × (a1_on / a1_off) within float rounding (≤ −120 dB re peak), proving the correction changes
  nothing but the level feed; the raw per-cell max |diff| re peak is recorded (worst −8.63 dB at E0 A 48 T 10 ms R 1) as a
  **Phase 9 listening item** (the sub's level follows the corrected h1 for a few ms on hard snaps). Rejected: exempting the
  snap cells; a −8 dB ceiling. [SC-026, SC-013(b), FR-058]
- **(C) SC-014 Retrigger dip — the reference is the Continue render's RMS over one period of the new note centred on the
  effective sample.** With the same 0.25 ms window, onset 2 read Reset −51.74 dB vs Continue −32.81 dB (gap 18.93 dB against
  20): the 12-sample Continue slice sat near a zero crossing, so the ratio was phase-dependent in the other direction; the
  declick is exactly 0 at the effective sample. Ruled: the Reset render's minimum 0.25 ms RMS in the onset window must be
  ≥ 20 dB below the Continue render's one-period RMS around the effective sample (phase-independent; expected gap ≈ 40 dB).
  Bar unchanged. Rejected: carrying the red; a 1 ms same-window slice. [SC-014]

### Session 2026-10-14 — comply remediation: SC-020 inherited record (no ruling; evidence only)

- **SC-020 inherited-failure record (applies the user instruction of 2026-10-11, Phase 2 spec Clarifications SC-021):** the
  comply verifier's full `dsp_systems_tests` `[long]` lane failed `VoragoMacro_SweepAxes` at `vorago_macro_test.cpp:1318`,
  "Movement rho=0.866667" against 0.9. The figure is identical to the inherited record in
  `specs/profundum-phase2-edge-fold/compliance.md:14` (rho 0.8666666667 on `main` at 89b512d1 before Phase 1, Long Tests
  Nightly run 38046177066, and the 2026-10-09 local lane before Phase 2) and the roadmap's Phase 2 status line. Phase 4 changes
  no Vorago file (`git status -- '*vorago*'` empty; `git diff --stat HEAD -- dsp/include` lists only
  `harmonic_oscillator_bank.h` +65, `mono_handler.h` +239, `profundum_core.h` +4, all additions), no `vorago_*.h` header
  includes any of them, and the bank change is opt-in (`setContinuousFrequencyUpdates`, default off,
  `harmonic_oscillator_bank.h:701`). **Classification: INHERITED (Vorago),** not counted as a Phase 4 pass and not re-run.
  The Vorago overnight soaks and `EcosystemEngine_HostileFuzzStaysBounded`, cut at the verifier's 1800 s timeout, are
  multi-hour cases of another instrument and are not this phase's gate (Phase 2 recorded them "not run" on the same basis).
  Green: per-push `dsp_processors_tests`, `dsp_systems_tests`, `ruinae_tests`, `seraphis_tests`, `vorago_tests`,
  `innexus_tests`, the 16 Phase 4 `[long]` shards, and the `node` append-only check. The full `[long]` lane of
  `dsp_systems_tests` carries this inherited red exactly as Phase 2 recorded it; the row is reported with that
  classification, not softened to a pass. [SC-020]
### Session 2026-10-14 — SC-021 listening verdict (user)

- The user listened to the eight `evidence/usecase-*.wav` renders against the five listening questions (each sounds like its
  name; the four are distinct; no click at onset or landing and no post-landing wobble; the Anchor behaves per mode; the C2 file is
  the E1 gesture transposed). Verdict: **808 attack, cinematic impact and techno pluck pass; kick-bass hybrid FAILS — "the thump is
  more like a squeak."** Cause (main loop): the plan S11.4 preset starts the fall 48 semitones above the note (659 Hz at E1,
  1047 Hz at C2) and covers four octaves in 45 ms, which is a chirp; a kick's pitch envelope starts one to two octaves above the
  note. The measured descriptors were never the question: every bar of SC-021 passed; the preset values are what fail. FR-068's
  hybrid preset is re-authored under the listening verdict (candidates A 24 / T 45 / elastic / R 1.0, A 24 / T 60 / exp / R 0.3,
  A 19 / T 50 / elastic / R 0.6, all LOCK, each still clearing the pairwise bars against the three passing presets by start pitch
  ≥ 12 st or t_s ≥ 2×); the user re-listens and the adopted values replace the S11.4 row. Until then SC-021 is PARTIAL (3 of 4).
  [SC-021, FR-068]
### Session 2026-10-15 — SC-021 re-listen and the hybrid preset adopted (user)

- Six candidate renders (`kick-bass-hybrid-c1/c2/c3` at E1 and C2) were rendered beside the four presets (the pairwise bars held
  with seven presets in the table). The user's second listen: the shipped hybrid (both notes) and c3 at C2 still read too high
  ("lower"); c1 and c2 pass. Adopted: **c1 — A 24 st, T 45 ms, elastic (s = 1), R 1.0, Anchor LOCK** (main-loop choice between the
  two passing candidates: c1 keeps the elastic snap that distinguishes the hybrid from the exponential 808 attack; the user may
  swap to c2). Plan S11.4 and the test table carry the new values; the eight renders were regenerated and the candidate files
  removed. SC-021 listening verdict: **4 of 4 pass.** [SC-021, FR-068]
