# Feature Specification: Vorago Phase 12 — Full Parameter Surface & State

**Spec slug:** `vorago-phase12-parameters`
**Roadmap:** `specs/Vorago-roadmap.md` → Part B, Phase 12 (lines 550–556); Open Questions 6 and 7 (lines 638–642); the Phase 10 status block's SC-008 deferral (lines 449–455); Cross-Cutting Constraints (lines 598–622)
**Depends on:** Phase 11 (`plugins/vorago/`, ✅ COMPLETE, `75a71e10`), Phase 10 (`VoragoEngine` / `VoragoVoice` / `VoragoMacroMatrix`) ✅, Phase 10a (ghost extension, ships inert) ✅, Phase 9 (`CavernVerb`) ✅
**Template:** `specs/seraphis-phase9-parameters/spec.md` — the shipped Seraphis parameter phase (roadmap line 521: *"Follows the Seraphis Part B template nearly verbatim"*)
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-09-24 (branch `feat/vorago-phase1-events-modulation` at `f149cced`)

---

## Overview

Phase 11 shipped a plugin with **fourteen** parameters: master gain, polyphony, and twelve concept macros
that are registered but **inert** — `plugins/vorago/src/parameters/macro_params.h:6-7` states *"INERT in Phase 11: no code reads MacroParams into VoragoMacroMatrix; Phase 12
wires it"*, and the processor owns a `VoragoMacroMatrix macros_{}` that is *"never written (FR-042)"*
(`plugins/vorago/src/processor/processor.h:100`). Phase 12 inverts that sentence and completes the
surface. The roadmap's whole text for this phase (lines 554–556) is:

> *"All engine parameters registered/denormalized/persisted with `kCurrentStateVersion`; concept-macro
> system wired; per-section parameter packs (`cloud`, `noise`, `resonance`, `ecology`, `sub`, `smear`,
> `events`, `ecosystem`, `body`, `space`, `macros`); pluginval + full round-trip tests."*

Three decisions carried into this phase by earlier phases are also closed here: the **seed parameter**
(Phase 11 spec Conventions, `specs/vorago-phase11-plugin-scaffold/spec.md:144`), **sustain / CC64 as a
wrapper-side note-off latch together with the single `IMidiMapping` addition** (Phase 11 leaf decision 5,
`plugins/vorago/CLAUDE.md` §"Decisions that outlive Phase 11" items 1 and 5; roadmap OQ-7 decided, lines
641–642), and the **output-saturation control through the matrix's base-override surface** (Phase 11
FR-041; Phase 10 spec ruling 10, `specs/vorago-phase10-voice-engine/spec.md:2240`).

Three facts read from the headers this session shape the phase exactly as they shaped Seraphis Phase 9:

1. **There is no public mutable route from `VoragoEngine` to its voices except the four envelope
   fan-outs.** `dsp/include/krate/dsp/systems/vorago_engine.h:703-716` says so verbatim (*"Envelope fan-out
   (FR-014) - THE ONLY MUTABLE ROUTE FROM OUTSIDE TO THE VOICES … THERE IS NO NON-CONST getVoice(i)"*);
   `getVoice()` is `const` (`:1068`); the only non-const route is `friend class VoragoMacroMatrix` (`:1090`).
   A plugin cannot today set Body Material A, Stereo Spread, or any noise/resonance/ecology knob on a voice.
2. **`VoragoMacroMatrix::apply()` overwrites 32 engine/voice targets on every call** from a per-target
   `base` that is a compile-time literal in `kRows` (`vorago_macro_matrix.h:296`, `apply()` at `:954-1016`,
   `evaluateAll()` at `:1074-1086` seeds each target from `row.base` and adds the contributions), and the
   processor calls it once per `process()` (`plugins/vorago/src/processor/processor.cpp:208`). A seven-field
   `computeCavernTargets()` result is pushed into the cavern at `:209` the same way. Writing a
   "Cloud Richness" parameter straight into a voice would therefore be erased by the next block.
3. **`setTargetBase` / `resetTargetBases` / `getTargetBase` were deliberately withheld for this phase**
   (`vorago_macro_matrix.h:58-62`: *"NOT PROVIDED, DELIBERATELY … Those are Phase 12's surface (FR-069)"*),
   and Seraphis Phase 9 shipped the exact precedent (`seraphis_macro_matrix.h:872-894`,
   `SeraphisVoiceParams` at `seraphis_engine.h:118`, `SeraphisEngine::applyVoiceParams` at `:723`).

Phase 12 therefore resolves (1) and (2) with **small, additive `dsp/` changes confined to Vorago-only
headers** (`vorago_engine.h`, `vorago_voice.h`, `vorago_macro_matrix.h`): per-target base overrides on the
matrix, a voice-parameter broadcast POD on the engine, and a handful of forwarders onto already-shipped
component setters. No DSP algorithm is written and no rendered sample changes at the registered defaults,
which is asserted as a same-binary negative control (SC-002).

---

## Clarifications

### Session 2026-09-24

- **Q1 (OQ-2 — Phase 10 SC-008 Gravity/Pressure/Mass deferral):** Ruling (b) — retune / add `kRows` rows
  (Gravity needs a new row; its single `ResonanceGravity` row is structurally capped) so Gravity, Pressure
  and Mass pass Phase 10 SC-008's **unchanged** thresholds. Bases stay frozen so SC-002 holds. Re-run
  `VoragoMacro_SweepAxes` and the Phase 10 CPU gate. Thresholds are never moved. [FR-007, FR-060, SC-021]
- **Q2 (OQ-1 — channel-pressure mapping):** Ruling (a) — Pressure macro only:
  `effective = clamp(knob + channelPressure, 0, 1)`. No smoother added up front; SC-011's continuity sweep
  (channel pressure is already a continuous, non-stepped C-6 ID) decides whether one is needed. Per-note
  MPE and `INoteExpressionController` stay out of scope. [FR-021, FR-032, SC-011, SC-013]
- **Q3 (OQ-3 — scope of "all engine parameters"):** Ruling (i) — ratify C-1 as written: 108 registered
  IDs, 106 persisted. Excluded: prepare-time config, `setEcosystemDepthFor` (overwritten by `fill()`),
  internal voicing with no forwarder, `BloomEngine::Relation`. C-5/C-6 are final. [FR-061]
- **Q4 (normalized-to-plain taper):** Ruling (b) — log taper (`logMapFromNormalized`, Seraphis precedent)
  for every continuous time, frequency and rate ID; linear for levels, amounts and bipolar controls. C-6
  gains a Taper column. Zero-floored ranges (envelope times, bloom spawn rate) use a stated epsilon or
  offset-log form, which the plan must cite per ID. [FR-012, FR-013, SC-018]
- **Q5 (Body Material A/B list):** Ruling (a) — StringList(11) in `BodyMaterial` enum declaration order;
  index == enum value, no mapping table. Defaults are the shipped chain's materials (`StoneChamber` /
  `SteelTank`) expressed as their enum indices. Phase 14 presets may avoid the bright materials. [FR-012,
  C-6]
- **Q6 (seed change while a note sounds):** Ruling (a) — immediate live reseed (ecosystem `reset()` plus
  agent re-deal); audible re-organisation accepted; alloc-free and bounded. Renders stay a pure function
  of state and events. C-8/FR-023/SC-014 stand as written. [FR-023, SC-014]
- **Q7 (per-slot / per-loop granularity):** Ruling (a) — per slot and per loop as written: 26 IDs
  (4 × noise model/type/comb fundamental/comb spread/comb feedback, plus 6 × ecology loop filter mode). VP
  POD keeps 31 fields; SC-003/SC-008/SC-018 keep the 108-ID totals. [FR-003, SC-003, SC-008, SC-018]
- **Q8 (sustain latch on `setState()`):** Ruling (a) — `setState()` raises a latch-release request
  consumed at the top of the next `process()`, alongside `pushAllSurfaces`; latched notes receive
  `noteOff` there. `setState()` itself makes no DSP call — it only raises the request. [FR-022, FR-030,
  SC-012]

### Session 2026-09-24 (plan stage)

- **R-1 (FR-060 Gravity / Pressure, contingent remedy — plan §2.6 / §8 OQ-1):** the plan predicts that no
  `kRows` row on the 39 existing targets reaches Phase 10's thresholds for Gravity (≤ ~21 % vs 30 %) or
  Pressure (~0.4 dB vs 3 dB). Ruling **(iii), taken before the probe runs**: if the T004 probe confirms
  that for an axis, that axis gains **one new `VoragoMacroTarget`** (a spec amendment to `Count == 39`;
  e.g. an `OutputDriveDb` engine target for Pressure, a peak wander-depth voice target for Gravity),
  additive, default-inert (its base equals the shipped chain's value, so SC-002 holds unchanged), and
  driven only by a new `kRows` row that contributes zero at macro-neutral. The new target is a macro
  target only — it adds **no registered parameter ID** (C-1 / FR-061 counts stay 108 / 106; the MB route
  keeps 39 IDs, and no static_assert may tie the MB count to `VoragoMacroTarget::Count`). If the probe
  finds an admissible row set for an axis, that set lands instead and this ruling is moot for it. The
  voicing change (ii), re-recording FAILED (i) and Phase 14 deferral (iv) are rejected. Thresholds are
  never moved. [FR-060, SC-021, SC-002]
- **R-2 (plan D-P2):** confirmed — `pushAllSurfaces()` leaves the seed and polyphony trackers alone; C-3's
  "every tracker" reads "every tracker except seed and polyphony". Forcing them would live-reseed on every
  preset load (breaking SC-014 (2) / SC-023 (2)) and disturb Phase 11's polyphony edge-trigger counter.
  [FR-030, SC-014, SC-023]
- **R-3 (plan D-P3):** confirmed — Sub Level Offset (ID 600) registers plain range **[−24, +24] dB**;
  `setSubToneLevelOffsetDb` has no clamp to transcribe. [C-6, SC-018]
- **R-4 (plan D-P8 / D-P9):** confirmed — SC-011 continuity remedies are block-rate smoothing once per
  `process()` inside the pre-slice push step (MB via a smoothed base before the single `apply()`), never
  per slice; SC-011's scope includes the 12 macros and every discrete ID whose destination does its own
  smoothing (measured with a discrete step sweep). No bound is loosened. [C-3, SC-007, SC-011]
- **R-5 (tasks deviation):** CMake registration of every new file is the first task (T002, placeholder
  files that compile with zero warnings), not the last group, because the enumerated test lists would
  otherwise stop a failing-test-first task from running its test. The last group keeps an audit.
- **R-6 (tasks deviation):** 15 per-pack contract test TUs under `plugins/vorago/tests/unit/params/`
  (14 packs plus the global extension) are added; coverage only, no requirement changes.
- **R-7 (tasks deviation):** parameter titles are ASCII only (e.g. "Sub Div2 Level", never "÷"); the
  title/unit strings tasks chose for IDs the plan's table left untitled are ratified. [FR-041, SC-018]

### Session 2026-09-24 (build stage, gate P-0 read-out and group 4 stop-and-surface)

- **Q9 (T005 read-out of `artifacts/fr060_probe.log`, 672 assertions, exit 0):** every baseline row
  reproduces Phase 10 (Gravity rho −0.9333 / endpoint 0.1801; Pressure rho −0.5667 / 0.0736 dB; Mass
  rho −1.0000 / −0.4464 dB). **Mass:** `Mass → SubToneLevelOffsetDb` +3.0 dB PASSES (rho 1.0000,
  endpoint 2.4343 dB ≥ 0.25; +4.5 → 3.8398, +6.0 → 5.2107) — T006 lands **+3.0 dB**, the smallest
  passing amount. **Gravity:** G-a 0.1950, G-b 0.1775, G-a+G-b 0.2024, all < 0.30 → no admissible set on
  the 39 targets, path B. **Pressure:** all 15 subsets of {P-a, P-b, P-c, P-d} fail; best P-a+P-b
  1.5030 dB < 3 dB (P-a 0.3541, P-b 1.2478, P-c 0.0737 = baseline, P-d −0.1579) → path B. [FR-060,
  SC-021]
- **B-1 (Gravity new target, R-1 path B):** the wander-depth target named in T007 is rejected on the
  probe data (wander spreads peaks *around* anchors it cannot move; the metric's stone-end floor 0.209 is
  set by the shipped keyed ratios, which are harmonic, not octave-aligned, while the metric measures
  distance to the nearest octave). Ruling: **a Voice-owned `ResonanceOctaveLock` target** — in
  `ResonanceDriftNetwork`, the keyed anchor becomes `noteLog2 + lerp(ratioLog2, round(ratioLog2), lock)`
  with `lock ∈ [0, 1]`, so 0 is bit-equal to the shipped ratios and 1 puts every keyed anchor on an
  octave of the note; forwarded through `VoragoVoice`. Base 0 (default-inert, SC-002 unchanged);
  Gravity's new row pulls it to 1 at the stone end (g = +1) and contributes 0 at neutral and at the air
  end (clamped at 0). Predicted stone-end metric ≈ wander spread only (≈ 0.06), endpoint ≈ 70 % ≥ 30 %.
  FR-007's file list widens by `resonance_drift_network.h` (consumed only by Vorago Layer 3 systems:
  bloom, ecosystem, feedback ecology, subharmonic, voice — never by Seraphis) and `vorago_voice.h`.
  [FR-007, FR-060, SC-021, SC-002]
- **B-2 (Pressure new target, R-1 path B):** ruling: **an Engine-owned `OutputDriveDb` target with
  makeup compensation** — `VoragoEngine::setOutputDriveDb(d)` applies `+d` dB into both
  `TapeSaturator`s (`setDrive`, range ±24 dB) and `−d` dB linear gain after them, before the limiter,
  so loudness stays level while the tanh curvature lowers the crest factor. The makeup gain is **ramped
  per sample** over the saturators' own 5 ms drive smoothing time (a per-chunk scalar failed SC-011 for
  channel pressure and the Pressure macro at ratio 2.19 on 2026-09-25; every other ID passed). Base 0 dB (= the retired
  `kOutputDriveDb` constant; default-inert). Landed **together with the P-a retune** (Pressure →
  OutputSaturation amount 0.35 → 0.88). The drive amount is **probed after the setter lands** over
  {6, 12, 18, 24} dB with P-a applied, using the same SC-008 fixture; the smallest passing amount lands.
  The uncompensated form is rejected. [FR-007, FR-060, SC-021, SC-002]
- **B-3 (SC-023 (1) positive control, group 5 stop-and-surface, 2026-09-25):** the slot-2 noise-model
  toggle moves the render by 2.1e-6 RMS against the `> 1e-3` floor because the noise bed is inaudible
  at the fixture (see SC-023). Measured alternatives: cloud spectral gravity toggle 2.5e-3, sub tone 0
  level −12/−60 dB 3.6e-3, body material A toggle 7.4e-3. Ruling: **body material A toggle** (same
  `applyVoiceParams` path as the original control). The `1e-3` floor and the `1e-5` inertness bound
  are unchanged. [SC-023]
- **B-4 (SC-014 (4) live reseed, group 11 red tree, 2026-09-25):** a seed change on a prepared instance
  differed from a fresh instance at that seed by 5.6e-3 (bound 1e-5): `AetherReverb` builds its
  Dimensionality matrix endpoint only in `prepare()` (5.565e-3, the dominant term, documented FR-021 /
  FR-073 / Edge case 23) and the engine's components re-seed their streams without rewinding prepare-time
  state (6.7e-5). Ruling: **full live reseed, FR-007 widened** — three opt-in additive methods
  (`AetherReverb::rebuildMatrixFromSeed()`, `CavernVerb::rebuildMatrixFromSeed()`,
  `VoragoEngine::rewindIdleVoices()`) that only the plugin's seed route calls; `setSeed()` semantics and
  every existing golden (Phase 10a SC-010 fingerprint) stay untouched. Rejected: re-scoping (4) to the
  next prepare (a preset's seed would not reproduce until the host re-prepares) and dropping the cavern
  reseed. [FR-007, FR-023, SC-014]
- **B-5 (SC-004 cavern non-vacuity, group 11 red tree, 2026-09-25):** on a single held C2 the guard
  (`> 1e-3` RMS vs the default cavern) is unreachable for Darkness 1.1e-4, Damper Depth 8e-5, Early
  Absorption 1.6e-4 and Damper Rate 3e-6 even at 30 s (reference RMS 4.6e-3), while route equality
  against the reference chain is exact for all 16 IDs. Ruling: **component-level noise stimulus** — the
  non-vacuity arm feeds two `CavernVerb`s 4 s of identical seeded white noise, same `1e-3` bound; the
  plugin-render equality arm is unchanged. Rejected: a relative threshold (Darkness reaches 2.4 %) and
  exempting the four IDs. [SC-004] Probe values stay the test's 0.8 / 0.2 rule except Damper Rate (1114), probed at its
  range end 1 Hz: measured 1.8e-4 at 0.39 Hz and 3.75e-3 at 1 Hz under the noise stimulus.
- **B-6 (SC-011 window geometry, group 13 stop-and-surface, 2026-09-25):** the spec centred every test
  window on `step + 3072`, but only engine-path IDs land there; a master-gain or saturation step lands at
  the output immediately and cavern-side steps within 1024, so for those IDs the step fell inside the
  *reference* window and positive control (b) could not fail. Ruling: **three windows per step at
  +0 / +1024 / +3072**, the test statistic the max over them, the reference the same three offsets at the
  midpoint (symmetric draws), step spacing 27 blocks (288 ms) so every reference window is ≥ 50 ms clear.
  Bound 1.5×, 20 ms windows and 64 steps unchanged. Rejected: a per-ID path-latency table (guessable
  wrong for cavern controls) and two windows (misses the diffusion path). [SC-011]
- **Q10 (T007 part 2 drive probe read-out, `artifacts/fr060_drive_probe.log`, 2026-09-25):** with P-a
  (saturation amount 0.88) the compensated drive alone reads, at 6 / 12 / 18 / 24 dB, endpoints 1.1654 /
  2.0188 / 2.4876 / 2.6003 dB (rho −1.0 throughout) — **no drive amount reaches 3 dB**; the crest floor at
  C1 is the sub-tone beating, which the drive cannot touch. Combined with a Pressure →
  `SubToneLevelOffsetDb` row (P-b family, base 0.0 dB shared with Weight and Mass): +18 dB with −12 /
  −18 / −24 dB → 3.5217 / 3.7010 / 3.7902; +24 dB with −12 / −18 / −24 dB → 3.8821 / 4.0550 / 4.1278.
  [FR-060, SC-021]
- **B-7 (Pressure row set, 2026-09-25):** ruling: **drive +18 dB + Pressure → `SubToneLevelOffsetDb`
  −12 dB** (the smallest passing set, 3.5217 dB ≥ 3 dB, rho −1.0), on top of the P-a retune. The new row
  is on an existing target with its shared base, admissible under ruling (b); `kNumRows` 49 → 50. At full
  Pressure the sub tones sit 12 dB lower. Rejected: +24 dB drive (more margin, saturator at its ceiling),
  −18 dB sub, and recording Pressure FAILED at 2.60 dB. [FR-060, SC-021]
- **B-8 (SC-021 CPU clause, 2026-09-25):** the SC-001b gate `VoragoEngine_CpuBudget` fails clause (i) on
  this machine today for the **untouched pre-phase binary** as well: same-conditions A/B (16 min idle
  each, P-core pinned, alone) reads f149cced 4.23711e6 / 4.36161e6 ns/block (136.3 % of the 3.2e6
  reference, `artifacts/sc021_cpu_base_f149cced.log`) against the Phase 12 tree 3.89004e6 / 4.01454e6
  (125.5 %, `artifacts/sc021_cpu.log`) — Phase 12 is **8 % faster** than the baseline; clause (ii) passes
  on both. Ruling: **record machine-limited with the A/B** (the Phase 11 B-5 precedent): the compliance
  row cites both logs, states no regression, and keeps the cold-machine re-measure as the recorded
  follow-up. The reference is not moved and the phase is not blocked on it. [SC-021]

---

## Scope

**In scope**

1. **Every engine parameter registered, denormalized and persisted**, under the inclusion rule stated in
   Conventions C-1, in per-section packs: the roadmap's eleven (`cloud`, `noise`, `resonance`, `ecology`,
   `sub`, `smear`, `events`, `ecosystem`, `body`, `space`, `macros`), plus the `global` pack extended, plus
   **four new bands claimed in the unassigned 1200+ range** for the engine sections roadmap lines 555–556 do
   not name — `envelope`, `bloom`, `ghost`, `life` — exactly as the Phase 11 reserved-map comment
   anticipated (`plugins/vorago/src/plugin_ids.h:51`: *"1200+ UNASSIGNED - Phase 12 claims a whole band for
   any unnamed section"*).
2. **The concept-macro system wired**: `MacroParams` → `VoragoMacroMatrix::setMacros()` once per
   `process()`, never per slice (Phase 11 P-10 rule, `plugins/vorago/CLAUDE.md` §"The live-macro push rule").
3. **`dsp/` additions (Layer 3, additive only, Vorago-only headers)** — see *New components*.
4. **State format version 2** (`kCurrentStateVersion` 1 → 2), with the Phase 11 v1 stream a strict,
   loadable prefix of v2.
5. **Seed parameter** (`kSeedId`) with a curated seed table.
6. **Output-saturation parameter** routed through the matrix base override (not around it).
7. **`IMidiMapping`** on the controller (the single interface addition Phase 11 deferred), mapping CC64 →
   a sustain parameter and channel pressure → a pressure parameter; the **wrapper-side sustain latch**.
8. **Tests**: default negative control, per-route read-back, macro wiring, full state round-trip
   (including v1 load and truncation), setState-after-prepare re-push, sustain, seed, RT safety,
   continuity, latency invariance, controller metadata, and the carried-forward Phase 11 wrapper-overhead
   gate. **pluginval strictness 5** and the editor-lifecycle harness stay green at the enlarged surface.

**Non-goals, owned by later phases**

| Deferred to | What |
|---|---|
| Phase 13 (`vorago-phase13-ui`) | Every `editor.uidesc` layout change, control-tags, custom views, the ecosystem view, DataExchange. Phase 12 leaves the placeholder uidesc untouched. |
| Phase 14 (`vorago-phase14-presets-release`) | Factory presets; the final preset category set; engaging the Phase 10a ghost features in presets; the Phase 10a Q1 density-vs-trigger preset choice (`specs/vorago-phase10a-ghost-extension/spec.md:1470-1472`); minutes-scale long-render sweeps (roadmap lines 572–574, the all-presets sweep; SC-020 here is deliberately seconds-scale); `getTailSamples()` revisit (Phase 11 Q6). |

The Phase 10 SC-008 Gravity / Pressure / Mass deferral (roadmap lines 449–455, *"the fix deferred to
Phase 12 as a product decision"*) is **not** a non-goal: it is owned by this phase, was put to the user as
OQ-2, ruled (b) retune `kRows` (*Clarifications* Q1), and is enacted by FR-060 / SC-021.

**Non-goals within Phase 12**

- **No new DSP algorithm and no change to any component shared with Seraphis.** `ContinuousBody`,
  `AtmosphereEngine`, `HarmonicCloud`, `AetherReverb` and the life modulators are not edited. The only
  `dsp/` files touched are `systems/vorago_engine.h`, `systems/vorago_voice.h` and
  `systems/vorago_macro_matrix.h`, plus new Layer 3 unit tests.
- **No `kRows` retune beyond the OQ-2 (b) ruling's scope.** OQ-2 is ruled (b): `kRows` rows for the
  Gravity, Pressure and Mass axes ARE retuned / added, per FR-060 / SC-021 (see *Clarifications*,
  Q1). Every other claim row (`CavernMix`, `CavernWidth`, `BodyMix`, `SubTrackingAmount` and
  `Fog → CloudSpectralTiltDb`, all `amount = 0`) stays as shipped and is not reopened; the Phase 10 spec's
  remark that moving `CavernMix` is a *"Phase 12 product decision"*
  (`specs/vorago-phase10-voice-engine/spec.md:1261-1263`) is not a roadmap deferral and is not addressed by
  this ruling.
- **No prepare-time configuration exposed as a parameter** (C-1 clause 3): `VoragoVoiceConfig` capacities
  (`vorago_voice.h:187-218`), `VoragoEngineConfig` FFT sizes / capture length / enable flags
  (`vorago_engine.h:105-156`) and `CavernVerb::PrepareConfig` stay fixed. None of them may change latency or
  allocate after `setupProcessing()`.
- **No `INoteExpressionController` and no per-note expression** — `VoragoEngine::noteOn(note, velocity)`
  carries no per-note expression (`vorago_engine.h:564`), so per-note MPE would need new DSP (Phase 11
  OQ-7 reasoning, `specs/vorago-phase11-plugin-scaffold/spec.md:1127-1133`). The OQ-7 remainder is
  *Open Questions* OQ-1.
- **No macro-roster change.** Roadmap OQ-6 (line 638) was resolved in Phase 10 (*"confirmed at twelve"*,
  `specs/vorago-phase10-voice-engine/spec.md:2272-2278`, which states *"this spec takes the **roster**,
  Phase 12 takes the **parameter surface**"*) and the twelve IDs are registered with frozen types
  (`plugin_ids.h:52-53`). Phase 12 wires them; it does not trim them.
- **No sleep events** (negative-polarity scheduler output) — excluded by Phase 10 Q7
  (`specs/vorago-phase10-voice-engine/spec.md:397-399`), not deferred to this phase.

---

## Existing components (verified this session)

Every signature below was read from the file this session at `f149cced`.

### DSP the parameters reach

| Component | Header | What Phase 12 reuses (verified signature) |
|---|---|---|
| `VoragoEngine` | `dsp/include/krate/dsp/systems/vorago_engine.h:163` | `void prepare(double sampleRate, const VoragoEngineConfig& cfg) noexcept` (:277, *"MAY BE CALLED REPEATEDLY … polyphony, seed and every engine-owned setter value survive"*, :270-276); `void setPolyphony(std::size_t n) noexcept` (:507); `void setSeed(std::uint32_t seed) noexcept` (:544, forwards to every slot and the atmosphere); the four envelope fan-outs over **all `kMaxVoices`**: `setEnvelopeMode(VoragoVoice::EnvelopeMode)` (:719), `setEnvelopeStageTimeMs(int stage, float ms)` (:725), `setEnvelopeReleaseMs(float)` (:731), `setGrowthDurationSeconds(float)` (:737) with slot-0 getters (:745-756); engine-owned targets `setSubToneLevelOffsetDb` (:764, early-out on unchanged value because it re-arms per-tone ramps, :766-775), `setSubTrackingAmount` (:783), `setSmearAmount` (:804, writes `smearBase_`), `setSmearDecoherence` (:813), `setSmearTilt` (:822, clamp [-1, 1]), `setGhostPeakLevel` (:833), `setAtmosBlur` (:841), `setOutputSaturation` (:850); each with a getter reading the engine field. Private `applySubToneLevels()` (:1154-1158) writes `sub_.setToneLevelDb(t, SubharmonicEngine::kDefaultToneLevelDb[t] + subToneOffsetDb_)`. `friend class VoragoMacroMatrix` (:1090). Const accessors `getVoice(i)` (:1068), `atmosphere()` / `subharmonic()` / `smear()` (:1085-1087). `kMaxVoices = 6` (:175), `kControlChunkSamples = 64` (:181) |
| `VoragoEngineConfig` | same header :105-156 | `atmosGhostReverseProbability = 0.0f`, `atmosGhostEventTriggers = false` (both inert by default, Phase 10a); forwarded **only in `prepare()`**: `ghostEventTriggers_ = cfg.atmosGhostEventTriggers` (:290), `atmos_.setGrainReverseProbability(cfg.atmosGhostReverseProbability)` (:323); `ghostEventTriggers_` is read by the control step at :1316 |
| `VoragoVoice` | `dsp/include/krate/dsp/systems/vorago_voice.h:225` | The macro-writable forwarders: `setRichness` (:1114), `setSpectralTiltDb` (:1122), `setMutation` (:1130), `setInharmonicity` (:1136), `setDriftDepthCents` (:1139), `setNoiseLevelDb` (:1148), `setNoiseWakeBase` (:1176), `setNoiseWanderRate` (:1184), `setResonanceGravity` (:1195), `setResonanceMix` (:1204), `setResonanceWanderRate` (:1207), `setEcologyMix` (:1216), `setEcologyLoopGain` (:1225), `setBodyBlend` (:1235), `setBodyDamping` (:1252), `setBodyResonance` (:1263), `setBodyMix` (:1274), `setEcosystemDepth` (:1309), `setEventRateScale` (:1353), `setBloomDepth` (:1363), `setBloomSpawnRateHz` (:1380), `setBreathingDepth` (:1383), `setBreathingIrregularity` (:1393), `setTidalDepth` (:1403). **Non-target** public setters: `void setStereoSpread(float s) noexcept` (:1144), `void setBodyMaterialA(ContinuousBody::BodyMaterial m) noexcept` (:1284), `void setBodyMaterialB(...)` (:1285), `void setEcosystemDepthFor(EcosystemEngine::Kind dest, float d) noexcept` (:1336). `void setSeed(std::uint32_t) noexcept` (:961-967) — on a prepared voice it runs `applySeeds()` and `assignAgentSlots()` **immediately** (a live reseed, not a no-op). Const accessors `noise()`, `resonance()`, `ecology()`, `bloom()`, `ecosystem()`, `bodyA()`, `bodyB()` (:1415-1433) |
| `VoragoVoice` defaults (prepare step 5) | same header :541-693 | Richness 0.70 (:545), tilt −4.0 (:546), mutation 0.15 (:547), inharmonicity 0.015 (:548), `cloud_.setSpectralGravity(0.10f)` *"NOT a Gravity-macro target (Q4)"* (:549), drift 8.0 (:550), **stereo spread 0.45** (:551); noise slot models `FilteredWind, GranularDust, Direct, MetallicHiss` (:557-560), level −18 dB (:561); **anchor mode `Hybrid`**, *"the only mode that consumes setGravity"* (:566-567); ecology per-loop filter mode is the component default; **body materials `StoneChamber` / `SteelTank`** (:625-626); envelope `kDefaultStageTimesMs{20000, 30000, 45000, 60000, 0, 0}` (:318-321), release `45000` (:324), `kEnvelopeMaxStageTimeMs = 120000` (:328), growth default and max `120 s` (:332-333), `EnvelopeMode { Standard = 0, Growth = 1 }` (:338) |
| `VoragoMacroMatrix` | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h:245` | `enum class VoragoMacroTarget` — 24 Voice-owned, 8 Engine-owned, 7 Cavern-owned, declared in owner blocks (:125-172); `kNumTargets` (:254), `kFirstEngineTarget` / `kFirstCavernTarget` (:262-265); `kRows` 46 rows (:296, `kNumRows = 46` at :258); `void setMacros(const VoragoMacroValues&) noexcept` (:915), `setMacro` rejects non-finite and clamps [0,1] (:832-...); `void apply(VoragoEngine&) const noexcept` (:954; writes nothing to an unprepared engine; voice loop bound **`i < engine.getPolyphony()`** at :971); `VoragoCavernTargets computeCavernTargets() const noexcept` (:1026); `evaluateAll()` (:1074-1086); `values_` is the only state (:1089). `static_assert(everyRowSharesOneBasePerTarget(kRows))` (:1107) — one base per target, so a per-target override is well-defined |
| `VoragoMacroValues` / `VoragoCavernTargets` | same header | Neutrals all `0.0f` except `gravity = 0.5f` (bipolar); cavern target defaults `size 0.50, darkness 0.80, decaySeconds 20.0, fog 0.30, damperDepth 0.35, mix 1.00, width 1.00` (both as quoted by Phase 11 and re-read this session) |
| `kRows` bases (the Voice/Engine prepare-time values) | same header, read this session | `CloudSpectralTiltDb −4.0`, `CloudRichness 0.70`, `CloudMutation 0.15`, `CloudInharmonicity 0.015`, `CloudDriftDepthCents 8.0`, `NoiseLevelDb −18.0`, `NoiseWakeBase 0.35`, `NoiseWanderRate 0.03`, `ResonanceGravity 0.0`, `ResonanceMix 0.45`, `ResonanceWanderRate 0.03`, `EcologyMix 0.15`, `EcologyLoopGain 0.72`, `BodyBlend 0.35`, `BodyDamping 0.25`, `BodyResonance 0.70`, `BodyMix 1.00`, `EcosystemDepth 0.85`, `EventRateScale 1.0`, `BloomDepth 0.60`, `BloomSpawnRateHz = BloomEngine::kDefaultSpawnRateHz = 1/240` (`bloom_engine.h:281`), `BreathingDepth 0.30`, `BreathingIrregularity 0.30`, `TidalDepth 0.40`; engine `SubToneLevelOffsetDb 0.0`, `SubTrackingAmount 1.0`, `SmearAmount 0.20`, `SmearDecoherence 0.20`, `SmearTilt 0.0`, `GhostPeakLevel 0.60`, `AtmosBlur 0.85`, `OutputSaturation 0.12`. The **Gravity** macro has exactly one row (`ResonanceGravity`, amount +1.0) |
| `CavernVerb` | `dsp/include/krate/dsp/effects/cavern_verb.h:193` | Setters, each clamping and substituting its default on non-finite input: `setSize` (:613), `setDarkness` (:619), `setDecaySeconds` (:626, clamp `[kCavernDecayMinSeconds 0.5, kCavernDecayMaxSeconds 60]`, :208-210), `setDensity` (:632), `setDimensionality` (:637), `setBreath` (:645, two engine targets), `setFog` (:652), `setEarlySizeMs` (:665, clamp `[kEarlySizeMinMs 80, min(600, maxEarlySeconds·1000)]`, :223-224), `setEarlyLevel` (:679), `setEarlyAbsorption` (:692), `setEarlySend` (:702), `setDamperDepth` (:717), `setDamperRate` (:726), `void setFreeze(bool on) noexcept` (:735, idempotent), `setWidth` (:741), `setMix` (:748), `setSeed` (:758, *"Store and re-seed every stochastic stream"*). Defaults `kDefault*` at :247-265. **No control getters** except `isFrozen()` (:770) |
| `NoiseOrganism` | `dsp/include/krate/dsp/systems/noise_organism.h:136` | `enum class NoiseOrganismModel : std::uint8_t { Direct = 0, FilteredWind = 1, GranularDust = 2, MetallicHiss = 3 }` (:126-131); `kMaxSources = 4` (:142); `void setSourceModel(std::size_t slot, NoiseOrganismModel model) noexcept` (:463, ducked, applied-state getter); `void setSourceNoiseType(std::size_t slot, NoiseType type) noexcept` (:478, *"a Direct slot's noise type"*, `ModulationNoise` snapped to `TapeHiss`); `void setCombTuning(std::size_t slot, float fundamentalHz, float spread) noexcept` (:568, fallbacks 60 Hz / 0.35, :573-575); `void setCombFeedback(std::size_t slot, float feedback) noexcept` (:588, clamp `[0, kCombFeedbackCap = 0.9]` :165, **latches the slot's feedback — the per-model default no longer applies**, :580-586; defaults `kDefaultCombFeedback 0.55` :167, `kMetallicCombFeedback 0.75` :170 via `defaultCombFeedback(model)` :1246-1251). Getters `getSourceModel` (:862), `getSourceNoiseType` (:868), `getCombFundamental` (:892), `getCombSpread` (:896), `getCombFeedback` (:904). Phase 2 spec names Phase 12 as the driver of these setters (`specs/vorago-phase2-noise-organism/spec.md:333`, `:674`) |
| `NoiseType` | `dsp/include/krate/dsp/processors/noise_generator.h:44-58` | 13 enumerators `White … RadioStatic`; `ModulationNoise` (index 11) is not selectable on the organism |
| `ResonanceDriftNetwork` | `dsp/include/krate/dsp/systems/resonance_drift_network.h` | `enum class AnchorMode : std::uint8_t { Free = 0, Keyed = 1, Hybrid = 2 }` (:293); `void setAnchorMode(AnchorMode mode) noexcept` (:587); `getAnchorMode()` (:815); `kMaxPeaks = 12` (:128). Phase 3 spec: *"they become a persisted plugin parameter at Phase 12"* (`specs/vorago-phase3-resonance-drift/spec.md:487`) |
| `FeedbackEcology` | `dsp/include/krate/dsp/systems/feedback_ecology.h` | `enum class FilterMode : std::uint8_t { Lowpass = 0, Bandpass = 1, Highpass = 2 }` (:597); `void setLoopFilterMode(std::size_t loop, FilterMode mode) noexcept` (:1035, **STEPPED — "a genuine signal discontinuity", exempted by name from Phase 5 SC-001 (d)**, :1032-1034); `getLoopFilterMode(loop)` (:1375); default `Lowpass` (:1618); `kMaxLoops = 6` (:193). Phase 5 spec: *"it becomes a persisted plugin parameter at Phase 12"* (`specs/vorago-phase5-feedback-ecology/spec.md:234`) |
| `SubharmonicEngine` | `dsp/include/krate/dsp/systems/subharmonic_engine.h` | `enum class Tone : std::uint8_t { Div2 = 0, Div4 = 1, FifthBelow = 2 }` (:320); `kNumTones = 3` (:162); `kDefaultToneLevelDb{-18, -24, -30}` (:218); `kMinToneLevelDb = -60` / `kMaxToneLevelDb = +6` (:210-211); `void setToneLevelDb(std::size_t tone, float db) noexcept` (:638, re-arms a per-tone `LinearRamp`); `getToneLevelDb` (:782). Phase 6 spec: *"it becomes a persisted plugin parameter at Phase 12"* (`specs/vorago-phase6-subharmonic/spec.md:291`) |
| `AtmosphereEngine` | `dsp/include/krate/dsp/systems/atmosphere_engine.h` | `void setGrainReverseProbability(float probability) noexcept` (:955, clamp [0,1], non-finite → 0, **read at grain birth only**, :947-952); `getGrainReverseProbability` (:958); `void triggerGrain() noexcept` (:1083). Phase 10a: *"Macro/parameter exposure is Phase 12"* (`specs/vorago-phase10a-ghost-extension/spec.md:371-373`) |
| `ContinuousBody::BodyMaterial` | `dsp/include/krate/dsp/systems/continuous_body.h:84-97` | 11 materials (`Glass … Ice`, then the six Vorago dark materials); `kNumMaterials = 11` (:100); `setMaterial` early-outs on an unchanged value and crossfades when prepared (:1401-1413) |
| `render_fingerprint.h` | `tests/test_helpers/render_fingerprint.h` | `kSampleTolerance = 5.0e-4f` (:58), `fingerprintRender` (:73), `compareFingerprints` (:122) — secondary, warn-only aggregates; never a checked-in bit-exact reference |

### Plugin code Phase 12 extends

| Item | File | Verified shape |
|---|---|---|
| IDs | `plugins/vorago/src/plugin_ids.h` | `kCurrentStateVersion = 1` (:20); reserved map (:46-53); `kMasterGainId = 0`, `kPolyphonyId = 1`, `kMacroDarknessId = 100 … kMacroMassId = 111` (:54-70); `kGlobalParamRangeEnd = 100`, `kMacroParamRangeEnd = 200` (:73-74) |
| Global pack | `plugins/vorago/src/parameters/global_params.h` | `struct GlobalParams { std::atomic<float> masterGain{1.0f}; std::atomic<int> polyphony{4}; }` (:31-34); six-function contract `handleGlobalParamChange` / `registerGlobalParams` / `formatGlobalParam` / `saveGlobalParams` (8 bytes) / `loadGlobalParams` (EOF-safe) / `loadGlobalParamsToController` |
| Macro pack | `plugins/vorago/src/parameters/macro_params.h` | `struct MacroParams` twelve `std::atomic<float>` with explicit initializers, `gravity{0.5f}` (:33-46); `static_assert` against `VoragoMacroMatrix::kNumMacros` (:48); save = twelve floats, 48 bytes |
| Engine config | `plugins/vorago/src/engine/vorago_engine_config.h` | `kEngineSeed = kCavernSeed = 1u` (:30-31), *"Not parameters in Phase 11"*; `applyCavernTargets(CavernVerb&, const VoragoCavernTargets&)` (:64-73) calls exactly the seven setters |
| Processor | `plugins/vorago/src/processor/processor.cpp` | `setupProcessing` seeds then prepares (:127-130), pushes polyphony (:133-137); `process()` → `processParameterChanges` (:175, takes the **last** point of each queue, :339) → guards → `pushGlobalParams()` (:207) → `macros_.apply(*engine_)` (:208) → `applyCavernTargets(...)` (:209) → event-sliced render loop (:255-274); `setState` reads version, rejects `> kCurrentStateVersion`, loads global then macros (:292-309); `getState` (:311-320); `dispatchEvent` handles note on/off only (:404-...) |
| Controller | `plugins/vorago/src/controller/controller.{h,cpp}` | `class Controller : public EditControllerEx1, public VST3EditorDelegate` (controller.h:25); header comment *"NO INoteExpressionController and NO IMidiMapping (FR-019, OQ-7 ruling)"* (:9); `initialize` registers the two packs; `setComponentState` mirrors `setState` |
| Tests | `plugins/vorago/tests/` | `Vorago_StateRoundTrip` (`unit/state_roundtrip_test.cpp:108`), `Vorago_ParamFlowReachesEngine` (`integration/param_flow_test.cpp:86`, holds the Phase 11 SC-023 *"macros are inert"* negative control), `Vorago_ProcessorCpu` (`integration/processor_cpu_test.cpp:144`, `[.perf][performance]`), `Vorago_EditorLifecycle` (`unit/controller/editor_lifecycle_test.cpp:89`) |
| `IMidiMapping` precedent | `plugins/disrumpo/src/controller/controller.h:62, :177-180` | `public Steinberg::Vst::IMidiMapping`; `tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel, Vst::CtrlNumber midiControllerNumber, Vst::ParamID& id) override`; `DEF_INTERFACE(Steinberg::Vst::IMidiMapping)` (:222) |
| MIDI controller numbers | `extern/vst3sdk/pluginterfaces/vst/ivstmidicontrollers.h:52, :104` | `kCtrlSustainOnOff = 64`, `kAfterTouch = 128` (channel pressure) |

### Seraphis precedent (pattern only; no Seraphis file is modified)

| Item | File | Shape |
|---|---|---|
| Voice broadcast POD | `dsp/include/krate/dsp/systems/seraphis_engine.h:118-176` | `struct SeraphisVoiceParams`, every member initializer = the shipped voice default; *"NO FIELD HERE MAY NAME A SeraphisMacroTarget"* (:110-112); `static constexpr std::size_t kFieldCount` (:175); `static_assert(std::is_trivially_copyable_v<...>)` (:178) |
| Broadcast | same header :723 | `void applyVoiceParams(const SeraphisVoiceParams& p) noexcept`, loops **`v < kMaxVoices`**, not `getPolyphony()` (reason :705-713: orphan tails keep rendering) |
| Base override | `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h:872-894` | `setTargetBase(target, base)` rejects out-of-range and non-finite (bit-pattern check); `resetTargetBases()`; `getTargetBase()` returns override or `kRows` literal; *"NO HEADROOM RESCALING"* (:866-871) |
| Spec conventions | `specs/seraphis-phase9-parameters/spec.md` C-1 … C-4 (:385-530) | base override = the deep parameter; everything else via broadcast; on-change push cadence with a generation counter; `pushAllSurfaces()` after `setState` (its Q2 bug class); same-binary default negative control, no checked-in fingerprint |

---

## New components

**ODR sweep run this session** (`grep -rnE "(class|struct) <Name>\b" dsp/ plugins/`, and `grep -rn "\b<symbol>\b" dsp/ plugins/` for members/IDs):

### DSP additions (Layer 3, `Krate::DSP`, Vorago-only headers)

| New type / symbol | Layer | Header | ODR sweep result |
|---|---|---|---|
| `VoragoVoiceParams` (trivially copyable POD) | 3 | `systems/vorago_engine.h`, beside `VoragoEngineConfig` | `VoragoVoiceParams` → **0 hits**. CLEAR. Distinct from `VoragoVoiceConfig` (`vorago_voice.h:187`, prepare-time; not extended). |
| `VoragoEngine::applyVoiceParams(const VoragoVoiceParams&) noexcept` | 3 | same header, public | `applyVoiceParams` → 24 hits, **all in Seraphis** (`seraphis_engine.h`, `seraphis_param_broadcast_test.cpp`, `plugins/seraphis/...`). A member of a different class — no ODR interaction. CLEAR. |
| `VoragoEngine::setSubToneLevelDb(std::size_t tone, float dB) noexcept` + `getSubToneLevelDb(std::size_t) const noexcept` | 3 | same header | 0 hits each. CLEAR. |
| `VoragoEngine::setGhostReverseProbability(float) noexcept` / `getGhostReverseProbability() const noexcept` | 3 | same header | 0 hits each. CLEAR. |
| `VoragoEngine::setGhostEventTriggers(bool) noexcept` / `getGhostEventTriggers() const noexcept` | 3 | same header | 0 hits each. CLEAR. |
| `VoragoMacroMatrix::setTargetBase(VoragoMacroTarget, float) noexcept`, `resetTargetBases() noexcept`, `[[nodiscard]] float getTargetBase(VoragoMacroTarget) const noexcept` | 3 | `systems/vorago_macro_matrix.h` | 42 / 7 / 43 hits; in `vorago_macro_matrix.h` **only the "NOT PROVIDED" comment** (:57-62); all others are `SeraphisMacroMatrix` members and their tests. CLEAR. |
| `VoragoVoice` forwarders: `setCloudSpectralGravity(float)`, `setNoiseSourceModel(std::size_t, NoiseOrganismModel)`, `setNoiseSourceType(std::size_t, NoiseType)`, `setNoiseCombTuning(std::size_t, float, float)`, `setNoiseCombFeedback(std::size_t, float)`, `setResonanceAnchorMode(ResonanceDriftNetwork::AnchorMode)`, `setEcologyLoopFilterMode(std::size_t, FeedbackEcology::FilterMode)` — all `noexcept` | 3 | `systems/vorago_voice.h` | 0 hits each (`setCloudSpectralGravity` / `getCloudSpectralGravity` re-swept 2026-09-24: 0 hits). CLEAR. Prefixed (`Cloud…`, `Noise…`, `Resonance…`, `Ecology…`) to match the facade's existing prefixed names (`setNoiseLevelDb` :1148, `setResonanceMix` :1204, `setEcologyMix` :1216). The spectral-gravity value is read back through the existing const `cloud().getSpectralGravity()` (`vorago_voice.h:1415`, `harmonic_cloud.h:494`); no voice getter is added. |
| `VoragoEngine::isGhostTriggerLatchHigh() const noexcept` (read-only test observable of `ghostTriggerHigh_`, `vorago_engine.h:1316-1323`) | 3 | `systems/vorago_engine.h` | 0 hits. CLEAR. |

### Plugin-local additions (`namespace Vorago`)

| New type | Header | ODR sweep result |
|---|---|---|
| `Vorago::CloudParams` | `plugins/vorago/src/parameters/cloud_params.h` | 1 hit: `Seraphis::CloudParams` (`plugins/seraphis/src/parameters/cloud_params.h:82`, inside `namespace Seraphis` :35). **Different namespace, different target** (`vorago_tests` never links Seraphis sources) — no ODR violation. Recorded as a near-name hazard: no TU may `using namespace` both plugin namespaces. |
| `Vorago::BodyParams` | `.../body_params.h` | 1 hit: `Seraphis::BodyParams` (`plugins/seraphis/src/parameters/body_params.h:108`, `namespace Seraphis` :35). Same ruling as above. |
| `Vorago::NoiseParams`, `ResonanceParams`, `EcologyParams`, `SubParams`, `SmearParams`, `EventsParams`, `EcosystemParams`, `SpaceParams`, `EnvelopeParams`, `BloomParams`, `GhostParams`, `LifeParams` | `.../<section>_params.h` | **0 hits each.** CLEAR. |
| `Vorago::SustainLatch` (processor-local, fixed 128-bit held/latched sets) | `plugins/vorago/src/processor/sustain_latch.h` | **0 hits.** CLEAR. |
| New IDs `kSeedId`, `kOutputSaturationId`, `kSustainPedalId`, `kChannelPressureId` and every section ID below | `plugin_ids.h` | `kSeedId` → 75 hits, all `Seraphis::kSeedId` (an enumerator in another namespace; no clash). The other three → **0 hits**. Every section ID name is checked by the plan against `grep -rn` before landing. |

---

## Conventions decided in this spec

### C-1. Which parameters exist (the inclusion rule)

*"All engine parameters"* (roadmap line 554) is read as the union of three classes, and **nothing else**:

1. **Every public run-time setter the engine chain already exposes** — the 39 `VoragoMacroTarget`
   enumerators (24 Voice, 8 Engine, 7 Cavern), the non-target `VoragoVoice` setters (`setStereoSpread`,
   `setBodyMaterialA/B`), the engine envelope fan-outs, `setPolyphony`/`setSeed`, and the nine
   `CavernVerb` controls that are not macro targets (`Density, Dimensionality, Breath, EarlySizeMs,
   EarlyLevel, EarlyAbsorption, EarlySend, DamperRate, Freeze`).
2. **Every component control an earlier Vorago spec explicitly names Phase 12 as the consumer of**, verified
   this session: noise slot model / type / comb tuning / comb feedback (Phase 2), resonance `AnchorMode`
   (Phase 3), ecology per-loop `FilterMode` (Phase 5), sub per-tone level indexed by `Tone` (Phase 6), the
   two Phase 10a ghost features (Phase 10a spec :371-373), and **`HarmonicCloud` spectral gravity** — the
   roadmap's own Reuse Inventory row L13 says *"Vorago exposes it"* (`specs/Vorago-roadmap.md:122`), and
   Phase 10 removed it only from the Gravity macro, leaving it *"at the cloud's own shipped default/control
   surface"* (`specs/vorago-phase10-voice-engine/spec.md:137-141` Q4, `:804-808` FR-016). No macro row and
   no life lane writes it (`vorago_voice.h:1893-1894`: *"driven by NEITHER lane"*), so a VP field is never
   overwritten. It is exposed as a VP route via the new `setCloudSpectralGravity` forwarder (ID 206).
   `BloomEngine::Relation` is **excluded**: Phase 7 says only that it *"may become"* one
   (`specs/vorago-phase7-harmonic-bloom/spec.md:205`).
3. **Excluded, by rule:** (a) prepare-time configuration (allocates or moves latency); (b) any control
   **overwritten by another path** — the Phase 11 FR-041 principle — which excludes
   `VoragoVoice::setEcosystemDepthFor` (the `EcosystemDepth` target's `setEcosystemDepth` does
   `ecosystemDepth_.fill(...)`, `vorago_voice.h:1309-1314`, on every `apply()`); (c) internal voicing the
   voice sets on private members with no forwarder and no Phase 12 promise (per-peak levels, ecology
   coupling, bloom fade times, scheduler envelopes, breath/tide rates, `vorago_voice.h:567-693`). Class (c)
   stays reachable only through the macros.

**This rule narrows the roadmap's literal "all engine parameters"** and was therefore put to the user as
**OQ-3**. OQ-3 is ruled **(i)**: C-1 is ratified as written (see *Clarifications*, Q3), recorded as
FR-061. Its exclusion list (class 3 (a)–(c) and `BloomEngine::Relation`) is final, and C-5/C-6 are the
decided surface, not a recommendation.

### C-2. Routes

Every registered ID has exactly one route (asserted by a `constexpr routeOf(id)` switch with a
compile-time completeness check, the Seraphis `processor.cpp:160-200` shape):

| Route | Mechanism | IDs |
|---|---|---|
| **MB** | `macros_.setTargetBase(target, denormalized)` on change; reaches the engine through `apply()` / `computeCavernTargets()` once per `process()` | 39 (all `VoragoMacroTarget`s, including `kOutputSaturationId`) |
| **VP** | fields of one `VoragoVoiceParams` POD, pushed by `engine_->applyVoiceParams()` on change (generation counter), fanned over **all `kMaxVoices`** | 31 (stereo spread, cloud spectral gravity, material A/B, 4 × {model, type, comb fundamental, comb spread, comb feedback}, anchor mode, 6 loop filter modes) |
| **ENG** | a direct `VoragoEngine` setter with its own last-pushed tracker, on change | 14 (polyphony, seed, envelope mode, stage 0–3 times, release, growth duration, 3 sub tone levels, ghost reverse probability, ghost event triggers) |
| **CV** | a direct `CavernVerb` setter, on change | 9 (the non-target cavern controls) |
| **MAC** | `macros_.setMacros(...)` once per `process()` from the twelve atomics, with channel pressure added into the **Pressure** macro only (OQ-1 ruled (a): `Pressure_effective = clamp(Pressure_knob + channelPressure, 0, 1)`; every other macro equals its atomic unchanged) | 12 macros + `kChannelPressureId` |
| **Local** | consumed in the processor | 2 (`kMasterGainId`, `kSustainPedalId`) |

**Base override is the composition rule for MB** (the Seraphis C-1 ruling, adopted unchanged): the
parameter **is** the target's base; `evaluateAll()` yields `base + Σ contributions`, so at the macro
neutral the parameter reaches the engine unmodified, and a macro moves the target *from* the parameter.
**No headroom rescaling**: an override placed at the clamp a macro travels toward consumes that macro's
travel, and that saturation is legal (asserted as monotone non-decreasing, SC-006).

**The voice-loop residue is inherited, not introduced.** `apply()` writes voices `i < getPolyphony()`
(`vorago_macro_matrix.h:971`), so MB-routed voice values do not reach an orphan tail above the polyphony;
VP and ENG fan out over all `kMaxVoices`. Phase 12 records this and does not change `apply()`'s bound.

### C-3. Push cadence

- `setMacros` + `apply()` + `applyCavernTargets()`: **once per `process()`, before the first slice, never
  per slice** (Phase 11 P-10 rule; `apply()` idempotence is a documented property of the forwarders,
  `vorago_macro_matrix.h:45-56`).
- MB bases, VP broadcast, ENG and CV setters: **on change only**, each behind a generation counter or a
  last-pushed tracker compared once per `process()` before the first slice, exactly like
  `pushGlobalParams()` (`processor.cpp:352-367`).
- **`pushAllSurfaces()`**: `setupProcessing()` (directly, audio thread stopped) and `setState()` (via a
  release-store request consumed at the top of the next `process()`) both force every route to re-push by
  resetting every tracker to a sentinel and bumping every generation. This closes the Seraphis Q2 bug class
  (a preset loaded after `setupProcessing()` reaching the DSP only when the user next touches a control).
- **Continuity** is owned by the destination components (every MB forwarder is a scalar store or a
  `setTarget()` on a ramp the component runs, `vorago_macro_matrix.h:45-51`). Where SC-011 measures a step
  a destination does not smooth, the plan adds a processor-side smoother for that ID family; it never
  loosens SC-011. Documented **stepped** IDs (C-6 column "Stepped") are exempt from SC-011's bound and are
  asserted finite and bounded only.

### C-4. Registered defaults equal the shipped chain

Every registered default normalized value denormalizes to exactly the value the Phase 11 chain installs:
the `kRows` base (MB), the voice's prepare-step-5 value read back through its getter (VP; for the four
noise-type IDs the organism's *requested*-type initializer `Brown`, `noise_organism.h:1138`, because the
getter reports the effective type — C-6), the engine / voice constant (ENG), or `CavernVerb::kDefault*` (CV). Two consequences are normative:

- **Comb feedback latches** (`noise_organism.h:580-586`). Its registered default per slot is
  `defaultCombFeedback(model-of-that-slot-at-prepare)` — `0.55` for slots 0–2 and `0.75` for slot 3
  (`MetallicHiss`, `vorago_voice.h:560`) — so the first push latches each slot to the value it already
  runs at, and SC-002 holds. After that, a model change no longer re-derives feedback; with the feedback
  an explicit, visible parameter, that is the intended behaviour.
- **At every default, every macro neutral and seed index 0, the plugin renders what Phase 11 renders**
  (SC-002, same binary, no checked-in fingerprint).

### C-5. Parameter-ID map

Bands follow the Phase 11 reserved map (`plugin_ids.h:46-51`) unchanged; Phase 12 claims four whole
bands above 1200 and updates the map comment and the leaf `CLAUDE.md` table in the same change.

```
0–99      Global     0 master gain, 1 polyphony (shipped) · 2 seed · 3 output saturation · 4 sustain pedal · 5 channel pressure
100–199   Macros     100–111 (shipped; now LIVE)
200–299   Cloud      200 richness · 201 tilt · 202 mutation · 203 inharmonicity · 204 drift depth · 205 stereo spread
                     · 206 spectral gravity
300–399   Noise      300 level · 301 wake · 302 wander rate · 310–313 slot model · 320–323 slot type
                     · 330–333 comb fundamental · 340–343 comb spread · 350–353 comb feedback
400–499   Resonance  400 gravity · 401 mix · 402 wander rate · 403 anchor mode
500–599   Ecology    500 mix · 501 loop gain · 510–515 loop filter mode
600–699   Sub        600 level offset · 601 tracking · 610–612 tone level (Div2, Div4, FifthBelow)
700–799   Smear      700 amount · 701 decoherence · 702 tilt
800–899   Events     800 rate scale
900–999   Ecosystem  900 depth
1000–1099 Body       1000 blend · 1001 damping · 1002 resonance · 1003 mix · 1004 material A · 1005 material B
1100–1199 Space      1100 size · 1101 darkness · 1102 decay · 1103 fog · 1104 damper depth · 1105 mix · 1106 width
                     · 1107 density · 1108 dimensionality · 1109 breath · 1110 early size · 1111 early level
                     · 1112 early absorption · 1113 early send · 1114 damper rate · 1115 freeze
1200–1299 Envelope   1200 mode · 1201–1204 stage 0–3 time · 1205 release · 1206 growth duration   (NEW BAND)
1300–1399 Bloom      1300 depth · 1301 spawn rate                                                  (NEW BAND)
1400–1499 Ghost      1400 peak level · 1401 blur · 1402 reverse probability · 1403 event triggers  (NEW BAND)
1500–1599 Life       1500 breathing depth · 1501 breathing irregularity · 1502 tidal depth        (NEW BAND)
1600+     UNASSIGNED
```

Enum names follow `k{Section}{Parameter}Id` with the project's standard names (`Mix`, not `DryWet`).
Totals: **108 registered IDs** (14 shipped + 94 new); **106 persisted** (sustain and channel pressure are
performance controllers and are not persisted, FR-045).

### C-6. Parameter table (normative columns; the plan transcribes each setter's clamp with file:line)

| Pack | IDs | Route | Type | Plain range | Default | Stepped | Taper |
|---|---|---|---|---|---|---|---|
| Global | 2 seed | ENG | `StringListParameter`, 16 entries | C-8 table | index 0 (= `1u`) | yes | — |
| Global | 3 output saturation | MB `OutputSaturation` | Range | [0, 1] (`vorago_engine.h:850-856`) | 0.12 | no | linear |
| Global | 4 sustain pedal | Local | Range, `kIsHidden`, not persisted | ≥ 0.5 = down | 0 | — | — |
| Global | 5 channel pressure | MAC | Range, `kIsHidden`, not persisted | [0, 1] | 0 | no | linear |
| Cloud | 200–204 | MB | Range | owning setter's clamp | `kRows` bases | no | linear (amounts/level/bipolar; none are time/frequency/rate) |
| Cloud | 205 stereo spread | VP | Range | `HarmonicCloud::setStereoSpread` clamp | 0.45 | no | linear |
| Cloud | 206 spectral gravity | VP | Range | `[-1, 1]`, linear (`harmonic_cloud.h:478-483`, non-finite rejected) | 0.10 (`vorago_voice.h:549`; normalized 0.55) | no (phase-continuous by construction, `harmonic_cloud.h:476-477`; measured by SC-011) | linear (bipolar) |
| Noise | 300–302 | MB | Range | `[-96, +12]` dB for level (`noise_organism.h` `setSourceLevel`), owner clamps otherwise | −18 / 0.35 / 0.03 | no | linear (300 level, 301 wake); **log** (302 wander rate) |
| Noise | 310–313 model | VP | StringList (4) | `NoiseOrganismModel` | `FilteredWind, GranularDust, Direct, MetallicHiss` | ducked (Phase 2 FR-013) | — |
| Noise | 320–323 type | VP | StringList (12: `NoiseType` minus `ModulationNoise`) | — | **`Brown` on all four slots** — the organism's *requested*-type initializer (`noise_organism.h:1138`, re-installed by prepare at `:1857`). The getter `getSourceNoiseType` reports the **effective** type (`:863-867`), which each non-Direct model pins (`effectiveNoiseType`, `:1226-1240`), so the requested type is observable only on a `Direct` slot (SC-003/SC-004 "Direct probe") | ducked | — |
| Noise | 330–333 comb fundamental | VP | Range | **`[20, 19845]` Hz, fixed and sample-rate independent** (`kMinResonatorFrequency = 20`, `resonator_bank.h:42`; ceiling `0.45 × 44100` from `kMaxResonatorFrequencyRatio`, `:45`), **logarithmic**: `Hz = 20 · 992.25^n`. The organism clamps to its own `sr·0.45` at run time (`noise_organism.h:573-574`, `:1214-1215`), which is ≥ 19845 at every rate ≥ 44.1 kHz; below 44.1 kHz the component clamp applies (edge case) and the stored parameter is not rewritten | 60 Hz (`noise_organism.h:1145`); normalized literal **`ln 3 / ln 992.25 = 0.159219`** | no | **log** (frequency; non-zero floor `20` Hz, no epsilon needed) |
| Noise | 340–343 comb spread | VP | Range | `[0, 1]`, linear | 0.35 (`noise_organism.h:1146`) | no | linear |
| Noise | 350–353 comb feedback | VP | Range | `[0, 0.9]` | 0.55, 0.55, 0.55, 0.75 (C-4) | no | linear |
| Resonance | 400–402 | MB | Range | owner clamps | 0.0 / 0.45 / 0.03 | no | linear (400 gravity, 401 mix); **log** (402 wander rate) |
| Resonance | 403 anchor mode | VP | StringList (3) | `AnchorMode` | `Hybrid` | measured (SC-011) | — |
| Ecology | 500–501 | MB | Range | owner clamps | 0.15 / 0.72 | no | linear |
| Ecology | 510–515 loop filter mode | VP | StringList (3) | `FilterMode` | `Lowpass` | **yes** (`feedback_ecology.h:1032-1034`) | — |
| Sub | 600–601 | MB (Engine) | Range | owner clamps | 0.0 dB / 1.0 | no | linear |
| Sub | 610–612 tone level | ENG | Range | `[-60, +6]` dB | `kDefaultToneLevelDb` −18 / −24 / −30 | no | linear (level) |
| Smear | 700–702 | MB (Engine) | Range | [0,1] / [0,1] / [-1,1] | 0.20 / 0.20 / 0.0 | no | linear (amounts/bipolar) |
| Events | 800 rate scale | MB | Range | owner clamp | 1.0 | no | **log** (rate) |
| Ecosystem | 900 depth | MB | Range | [0, 1] | 0.85 | no | linear |
| Body | 1000–1003 | MB | Range | owner clamps | 0.35 / 0.25 / 0.70 / 1.00 | no | linear |
| Body | 1004–1005 material A / B | VP | StringList (11) | `BodyMaterial` | `StoneChamber` / `SteelTank` | crossfaded by `ContinuousBody` | — (StringList index == `BodyMaterial` enum value, declaration order, no mapping table) |
| Space | 1100–1106 | MB (Cavern) | Range | cavern setter clamps (decay `[0.5, 60]` s) | `VoragoCavernTargets` defaults | no | linear (1100 size, 1101 darkness, 1103 fog, 1104 damper depth, 1105 mix, 1106 width); **log** (1102 decay; non-zero floor `0.5` s, no epsilon needed) |
| Space | 1107–1114 | CV | Range | cavern setter clamps (early size `[80, …]` ms) | `CavernVerb::kDefault*` | no | linear (1107 density, 1108 dimensionality, 1109 breath, 1111 early level, 1112 early absorption, 1113 early send); **log** (1110 early size, non-zero floor `80` ms; 1114 damper rate) |
| Space | 1115 freeze | CV | toggle (StringList 2) | off/on | off | latch (`aether_reverb` 50 ms) | — |
| Envelope | 1200 mode | ENG | StringList (2) | `Standard, Growth` | `Standard` | yes | — |
| Envelope | 1201–1205 | ENG | Range | `[0, 120000]` ms | 20000 / 30000 / 45000 / 60000 / 45000 | no (applies to later stages / releases) | **log, zero-floored** — the plan cites the offset-log inverse (`ms = ε + (120000 − ε) · baseᵏ`, or equivalent `log(ms + ε)` form) and its ε per ID |
| Envelope | 1206 growth | ENG | Range | `[.., 120]` s (component min, plan cites) | 120 | no | **log**; if the component min cited by the plan is `0` the same zero-floored offset-log form as 1201–1205 applies |
| Bloom | 1300–1301 | MB | Range | owner clamps | 0.60 / 1/240 Hz | no | linear (1300 depth); **log, zero-floored** (1301 spawn rate `[0, 0.05]` Hz — the plan cites an epsilon or offset-log form) |
| Ghost | 1400–1401 | MB (Engine) | Range | [0, 1] | 0.60 / 0.85 | no | linear (levels/amounts) |
| Ghost | 1402 reverse probability | ENG | Range | [0, 1] | 0 | birth-time only | linear (probability, not a rate) |
| Ghost | 1403 event triggers | ENG | toggle | off/on | off | yes | — |
| Life | 1500–1502 | MB | Range | owner clamps | 0.30 / 0.30 / 0.40 | no | linear |

**Taper** (Q4 ruled (b)): `logMapFromNormalized` (the Seraphis `aether_params.h:113` precedent) for every
continuous time, frequency and rate ID; linear for levels, amounts and bipolar controls; discrete
(StringList / toggle) rows carry no taper (`—`). The two zero-floored log ranges (envelope stage
times/release 1201–1205, growth 1206 if its component min is 0, and bloom spawn rate 1301) use a stated
epsilon or offset-log form, cited per ID by the plan — comb fundamental (330–333) and cavern decay (1102)
and early size (1110) are log but **not** zero-floored (their component minimums are 20 Hz / 0.5 s / 80 ms)
and need no epsilon.

Registered **types are frozen from the moment they ship** (roadmap line 565, "No param-type swaps on registered IDs, ever"; leaf `CLAUDE.md` param-ID
paragraph). The fourteen Phase 11 types are unchanged.

### C-7. State format version 2

`kCurrentStateVersion = 2`. Little-endian `IBStreamer`. **The v1 stream is a strict prefix of v2**:

```
int32 version
[v1 block]  float masterGain, int32 polyphony            (8 B)   — unchanged
            12 × float macro                             (48 B)  — unchanged
[v2 block]  int32 seedIndex, float outputSaturation      (global extension)
            each pack in ascending ID-band order; floats as float, discrete values as int32 index,
            toggles as int32 0/1; sustain and channel pressure are NOT written
```

`setState` accepts versions 1 and 2; a version-1 stream loads the v1 block and leaves every Phase 12
field at its registered default; a version `> 2` loads nothing and returns `kResultFalse`. Loading is
EOF-safe per field (a failed read stops the load and leaves that field and all later ones unchanged),
rejects non-finite floats (field unchanged, `detail::isFinite`), clamps finite floats to the plain range
and clamps discrete indices into range. The controller mirror inverts every mapping.

### C-8. The seed table

`kSeedId` is a `StringListParameter` of **16 curated, checked-in 32-bit seed constants**, labelled
`"Seed 1" … "Seed 16"`, index 0 pinned to `1u` (the Phase 11 `kEngineSeed`, so SC-002 holds). The same
entry seeds the engine and, XORed with a fixed salt, the cavern — except that at index 0 the cavern seed
stays `1u` (the Phase 11 `kCavernSeed`). The Seraphis C-10 shape (`plugins/seraphis/src/plugin_ids.h:83`).

### C-9. Sustain latch

The pedal is the `kSustainPedalId` parameter (fed by `IMidiMapping` CC64). **Every** point of its queue in
a block is processed — not only the last — as an ordered event interleaved with note events by sample
offset in the existing slice loop. Down = value ≥ 0.5. While down, a note-off marks the note *latched*
instead of calling `engine_->noteOff`; a note-on for a latched note clears its latch mark and dispatches
normally; on the down→up edge every latched note that is not currently key-held receives
`engine_->noteOff`. `setActive(false)` releases the latch **immediately** (pedal up, latched set emptied,
latched notes sent `noteOff` if prepared) — permitted because it already runs off the audio-render path
(`silence()`, `vorago_engine.h:447`). `setupProcessing()` clears the latch as part of re-preparing (no
notes can be sounding across that call). `setState()` (Q8 ruled (a)) makes **no DSP call of its own**: it
raises a latch-release request — the same release-store mechanism `pushAllSurfaces` uses (C-3, FR-022) —
consumed at the top of the next `process()`, where the pedal is set up, the latched set is emptied and
every latched note receives `engine_->noteOff`. No `dsp/` change.

---

## Functional Requirements

### A. DSP additions (Layer 3, additive only)

- **FR-001** `VoragoMacroMatrix` MUST gain `void setTargetBase(VoragoMacroTarget target, float base) noexcept`,
  `void resetTargetBases() noexcept` and `[[nodiscard]] float getTargetBase(VoragoMacroTarget target) const noexcept`
  with the Seraphis semantics (`seraphis_macro_matrix.h:872-894`): out-of-range target and non-finite base
  (bit-pattern check, never `std::isnan`) are silent no-ops; `getTargetBase` returns the override if set,
  else the `kRows` literal; `resetTargetBases` restores every literal. `evaluateAll()` MUST seed each target
  from `getTargetBase(target)` instead of `row.base`. No headroom rescaling. The header's *"NOT PROVIDED,
  DELIBERATELY"* banner (`:58-62`) is replaced by the new contract. [roadmap lines 554–555 "concept-macro system
  wired"; Phase 10 FR-069 / ruling 10]
- **FR-002** With no override set, `apply()` and `computeCavernTargets()` MUST be bit-identical to the
  shipped matrix; `everyRowSharesOneBasePerTarget` and the other `static_assert`s (`:1107-...`) MUST remain.
  Phase 10's SC-009 clause 1 (*base == getter-after-prepare*) is stated as holding **with no override set**.
- **FR-003** `VoragoEngine` MUST gain `struct VoragoVoiceParams` (trivially copyable, `static_assert`ed)
  whose every member initializer is the voice's shipped prepare-step-5 value, with **no field naming a
  `VoragoMacroTarget`** (the `seraphis_engine.h:110-112` rule), a `kFieldCount` constant, and
  `void applyVoiceParams(const VoragoVoiceParams&) noexcept` fanning out over **all `kMaxVoices`** slots.
  Fields: `stereoSpread`, `cloudSpectralGravity`, `bodyMaterialA`, `bodyMaterialB`, `noiseModel[4]`,
  `noiseType[4]`, `noiseCombFundamentalHz[4]`, `noiseCombSpread[4]`, `noiseCombFeedback[4]`,
  `resonanceAnchorMode`, `ecologyLoopFilterMode[6]` (31 values). [roadmap line 554 "all engine
  parameters"; roadmap `:122` L13 "Vorago exposes it"; C-1]
- **FR-004** `VoragoVoice` MUST gain the seven forwarders listed under *New components*, each a bare forward
  onto the verified component setter (`harmonic_cloud.h:478`; `noise_organism.h:463, :478, :568, :588`;
  `resonance_drift_network.h:587`; `feedback_ecology.h:1035`), adding a non-finite guard only where the
  owner substitutes a default instead of rejecting (the `vorago_voice.h:1092-1108` rule). Each forwarder
  MUST early-out on an unchanged effective value so a repeated broadcast arms no duck and restarts no ramp
  (Phase 2's coalescing requirement, `specs/vorago-phase2-noise-organism/spec.md:328-336`).
- **FR-005** `VoragoEngine` MUST gain `setSubToneLevelDb(tone, dB)` / `getSubToneLevelDb(tone)`: a per-tone
  base (default `SubharmonicEngine::kDefaultToneLevelDb[t]`) that `applySubToneLevels()` uses in place of
  the constant, so the matrix's `SubToneLevelOffsetDb` still composes on top. Out-of-range tone and
  non-finite dB are no-ops; an unchanged value MUST early-out (the `:766-775` ramp-restart reason).
- **FR-006** `VoragoEngine` MUST gain `setGhostReverseProbability(float)` forwarding to
  `atmos_.setGrainReverseProbability` (birth-time read, so live changes cannot flip a sounding grain) and
  `setGhostEventTriggers(bool)` writing `ghostEventTriggers_` and disarming the trigger latch
  (`ghostTriggerHigh_ = false`) on an on→off edge; both with getters, plus the read-only
  `isGhostTriggerLatchHigh()` observable (SC-022 needs it: with the flag off the control step skips the
  whole block, `vorago_engine.h:1312-1316`, so no audio or counter shows the latch). `prepare()` keeps installing the `VoragoEngineConfig` values, and a
  later setter value survives a re-prepare (FR-077 of Phase 10: engine-owned setter values survive).
- **FR-007** No other `dsp/` file is modified, **except** the matrix's `kRows` for the Gravity, Pressure
  and Mass axes, retuned / added per the OQ-2 (b) ruling (FR-060 (b)), and — under R-1 path B as ruled in
  B-1 / B-2 — the two new macro targets: `ResonanceOctaveLock` (`resonance_drift_network.h` setter,
  `vorago_voice.h` forwarder, both Vorago-only) and `OutputDriveDb` (`vorago_engine.h`), each
  additive and default-inert; and — under B-4 — the three **opt-in, additive** reseed methods
  `AetherReverb::rebuildMatrixFromSeed()` (`aether_reverb.h`, a Seraphis-consumed header: nothing inside
  the header calls it, so Seraphis keeps prepare()-only matrix semantics bit for bit; `dsp_effects_tests`
  and `seraphis_tests` are re-run green as the Phase 11 B-2 precedent requires),
  `CavernVerb::rebuildMatrixFromSeed()` (`cavern_verb.h`) and `VoragoEngine::rewindIdleVoices()`
  (`vorago_engine.h`); no other Seraphis-consumed header is
  touched; every
  `dsp_systems_tests` / `dsp_effects_tests` case passes **unedited**; new Layer 3 cases land in
  `dsp/tests/unit/systems/` registered with `dsp_systems_tests`.

### B. Parameter IDs and packs

- **FR-010** `plugin_ids.h` MUST add every ID in C-5 with the names in C-5/C-6, update the reserved-map
  comment and `kCurrentStateVersion = 2`, and add range-end constants for every band.
- **FR-011** One pack header per section (`cloud, noise, resonance, ecology, sub, smear, events,
  ecosystem, body, space, envelope, bloom, ghost, life`) plus the extended `global` pack, each following
  the six-function contract of `global_params.h` (struct of atomics with explicit initializers,
  `handle…ParamChange`, `register…Params`, `format…Param`, `save…Params`, EOF-safe `load…Params`,
  `load…ParamsToController`). The `macros` pack is unchanged except that it is now read.
- **FR-012** Registered types, titles, units, step counts, defaults **and taper** MUST match C-6; discrete
  parameters use `createDropdownParameterWithDefault` **and** pin `defaultNormalizedValue` (the Phase 11
  P-1 trap, `global_params.h:82-84`). Body Material A / B (IDs 1004–1005) MUST be a `StringList(11)` in
  `ContinuousBody::BodyMaterial` declaration order with the StringList index equal to the enum value and
  no mapping table (Q5 ruled (a)), unlike Seraphis ID 800's 5-of-11 subset (`dropdown_mappings.h:206-213`);
  their registered defaults MUST denormalize to `StoneChamber` and `SteelTank` at those materials' enum
  indices.
- **FR-013** Denormalization happens in `processParameterChanges()` (root `CLAUDE.md`), dispatched by ID
  range; an unregistered in-band ID changes nothing; a non-finite normalized value is ignored; finite
  values are clamped to [0, 1] before mapping. **Taper (Q4 ruled (b)):** an ID marked `log` in C-6's Taper
  column MUST denormalize via `logMapFromNormalized` (and normalize via its inverse); every other
  continuous ID MUST use linear mapping; a zero-floored log ID (C-6: envelope 1201–1206, bloom 1301) MUST
  use the epsilon or offset-log form the plan cites for that ID, never a bare `log(0)`.

### C. Processor wiring

- **FR-020** `process()` MUST: (0) consume a pending `pushAllSurfaces` request; (1) apply parameter
  changes (last point per queue, except sustain, FR-030); (2) once, before the first slice: global push,
  ENG pushes, CV pushes, VP broadcast if its generation moved, MB `setTargetBase` for changed IDs, then
  `setMacros` → `apply(*engine_)` → `applyCavernTargets(...)`; (3) the Phase 11 slice loop unchanged except
  for sustain events. Never per slice.
- **FR-021** The MAC route MUST build the macro vector from the twelve atomics, adding the channel-pressure
  value into the **Pressure** macro only (OQ-1 ruled (a)): `Pressure_effective = clamp(Pressure_knob +
  channelPressure, 0, 1)`; every other macro equals its atomic unmodified. It MUST call `setMacros` once
  per `process()`. With pressure 0 the vector equals the twelve atomics exactly.
- **FR-022** `pushAllSurfaces()` per C-3, reached from `setupProcessing()` and (by a release-store request)
  from `setState()`.
- **FR-023** Seed changes (ENG) are applied on change by `engine_->setSeed(v)` **+
  `engine_->rewindIdleVoices()`** and `cavern_->setSeed(...)` **+ `cavern_->rebuildMatrixFromSeed()`**
  per C-8 and B-4 (the two rewinds are what make a live reseed reproduce a fresh instance, SC-014 (4)). `setupProcessing()` seeds from the parameter **before** `prepare()` (Phase 11 FR-023.2 order).
  A live reseed is **not** assumed a no-op (`vorago_voice.h:961-967` reseeds immediately;
  `ecosystem_engine.h:398-407` `setSeed` = `reset()`); it MUST be allocation-free and bounded (SC-014).
- **FR-024** No parameter may change `getLatencySamples()` (remains `engine + cavern` = 3072 after prepare).
- **FR-025** All paths above are allocation-, lock-, exception- and I/O-free on the audio thread.

### D. MIDI

- **FR-030** Sustain per C-9, implemented in `Vorago::SustainLatch` (fixed-size bitsets, no allocation).
  `setState()` (Q8 ruled (a)) MUST raise a latch-release request — the same release-store mechanism as
  `pushAllSurfaces` (FR-022) — consumed at the top of the next `process()`, where latched notes receive
  `engine_->noteOff`; `setState()` itself MUST make no DSP call.
- **FR-031** The controller MUST implement `IMidiMapping::getMidiControllerAssignment`: bus 0, any channel,
  `kCtrlSustainOnOff` → `kSustainPedalId`, `kAfterTouch` → `kChannelPressureId`; every other controller →
  `kResultFalse`. The controller header comment (:9) is updated. `INoteExpressionController` is not added.
- **FR-032** Channel pressure composes into the **Pressure** macro only, per OQ-1 ruling (a) (FR-021); it
  is never persisted and no processor-side smoother is added to it up front (SC-011 decides whether one is
  needed, per C-3's remedy rule).

### E. State and controller

- **FR-040** `getState` / `setState` per C-7; `setComponentState` mirrors it through every pack's
  `load…ParamsToController`.
- **FR-041** Controller `initialize` registers every pack; `getParamStringByValue` formats every pack
  (dB, %, Hz, ms, s, labels).
- **FR-045** Sustain and channel pressure are not written to state and are reset to 0 by `setState`.

### F. Tests and gates

- **FR-050** Tests in `vorago_tests` and `dsp_systems_tests` for every SC below; the Phase 11 SC-023
  *"macros are inert"* section is **inverted** (it becomes SC-005's clause 3), not deleted.
- **FR-051** pluginval strictness 5, the editor-lifecycle harness, clang-tidy (`dsp` and `vorago`),
  `check-portability.js`, `lint-odr.js`, `lint-layers.js`, `lint-nonfinite-symbols.js`,
  `lint-float-bit-goldens.js`, `lint-plugin-roster.js` all clean.
- **FR-052** The leaf `plugins/vorago/CLAUDE.md` MUST be updated: ID table (new bands, macros live), state
  version 2 layout, route table, decisions 1 and 5 marked **delivered**, the output-saturation route
  replacing decision 3's "must go through the matrix" with the shipped mechanism.

- **FR-053** The processor MUST provide a test-only, plugin-side seam
  `Vorago::detail::VoragoMasterGainSmootherBypassProbe` (ODR sweep 2026-09-24: 0 hits) that, while engaged,
  makes the master-gain push `snapTo` instead of `setTarget` (`processor.cpp:362-365`). It exists only as
  SC-011's criterion-wiring positive control (b); no `dsp/` seam is added (FR-007), and it is compiled out
  of, or inert in, the shipped plugin's audio path.

### G. Rulings carried into this phase

- **FR-060 — The Phase 10 SC-008 Gravity / Pressure / Mass deferral is enacted per the OQ-2 ruling, (b)
  retune `kRows`** (roadmap lines 449–455; *Clarifications* Q1). The retuned / added rows for the Gravity,
  Pressure and Mass axes MUST land in `vorago_macro_matrix.h` (the one additional `dsp/` edit FR-007
  admits); Gravity MUST gain a new row (its single `ResonanceGravity` row is structurally capped at 24 %,
  Phase 10 spec Q-N). Every `kRows` **base** stays frozen (C-4; SC-002 must hold unchanged) — only row
  amounts/curves and new rows with zero contribution at macro-neutral may move. SC-021 MUST re-measure
  every Phase 10 axis at Phase 10's **unchanged** thresholds, and the Phase 12 `compliance.md` MUST carry
  one row per axis (Gravity, Pressure, Mass) citing the ruling, the Phase 10 measured figures
  (`specs/vorago-phase10-voice-engine/compliance.md:20`) and the retuned figures. **Contingent clause
  (R-1):** if the T004 probe shows that no admissible row on the 39 existing targets reaches an axis's
  threshold, that axis gains exactly one new `VoragoMacroTarget` (additive; base equal to the shipped
  chain's value so SC-002 holds unchanged; driven only by a new zero-at-neutral `kRows` row; a macro
  target only, no registered parameter ID). `VoragoMacroTarget::Count` then reads 40 or 41 and the
  edit stays inside `vorago_macro_matrix.h` plus the one owner header that gains the setter.
- **FR-061 — The scope of "all engine parameters" is ratified as OQ-3 (i).** C-1's inclusion rule, its
  exclusion list and the resulting C-5/C-6 surface (108 registered IDs, 106 persisted) are final as
  written (*Clarifications* Q3); no excluded control is admitted and no C-1 exclusion is reopened by this
  spec.

---

## Success Criteria

All render criteria use `render_fingerprint.h` tolerances or measured tolerances stated here; **no
bit-exact float golden is checked in**. "Same binary" comparisons render both arms in one test process.

- **SC-001 — Build and gates.** `vorago_tests`, `dsp_systems_tests`, `dsp_effects_tests` build with zero
  warnings and pass; pluginval `--strictness-level 5` on `Vorago.vst3` exits 0; clang-tidy
  `-Target vorago` and `-Target dsp` report 0 errors / 0 warnings; the six node lints in FR-051 exit 0.
- **SC-002 — Default negative control (same binary).** `Vorago_Phase12DefaultsMatchPhase11Chain`: a fresh
  processor at every registered default renders a fixed script (note-on C2 vel 100, 8 s at 48 kHz,
  512-sample blocks) within **max-abs 1e-5** per sample, both channels, of a reference built in the same
  test from `makeVoragoEngineConfig` / `makeVoragoCavernConfig` driven through the Phase 11 chain with no
  Phase 12 push engaged. Precondition: reference peak over `[3072, end)` ≥ 1e-4 (else lengthen the script,
  never loosen the threshold).
- **SC-003 — Default read-back.** `Vorago_RegisteredDefaultsMatchEngine`: for every getter-backed ID the
  registered default denormalizes to the prepared chain's value within `1e-6` relative (exact for
  discrete). The getter-backed set is **exactly 97 IDs**, and the case prints its row count and asserts
  `== 97`:
  - **39 MB** — via `getTargetBase` **and** the owning engine/voice getter for the 32 Voice/Engine targets;
  - **31 VP** — via `getVoice(i).cloud()/noise()/resonance()/ecology()/bodyA()/bodyB()` getters for
    **every `i < kMaxVoices`**; the four type rows 320–323 via the **Direct probe** of SC-004 (the default
    `Brown` is otherwise unobservable on slots 0, 1, 3, C-6);
  - **14 ENG** — `getPolyphony`, `getSeed`, the four envelope getters (seven IDs: mode, four stage times, release,
    growth),
    `subharmonic().getToneLevelDb(t)` ×3, `atmosphere().getGrainReverseProbability()`,
    `getGhostEventTriggers()`;
  - **1 CV** — freeze via `isFrozen()` (the other eight CV IDs have no getter; `cavern_verb.h` publishes
    none, and their defaults are pinned against `CavernVerb::kDefault*` by SC-018's table);
  - **12 MAC** — `getMacros()` (`vorago_macro_matrix.h:930`) equals `VoragoMacroValues{}` neutrals.
  The 11 IDs outside the set (master gain, sustain, pressure, the eight getter-less CV IDs) sum to 108.
- **SC-004 — Route read-back (every ID).** `Vorago_EveryRouteReachesTheChain`, macros at neutral:
  - **Fixture.** Polyphony = `kMaxVoices` (6), and six notes held so that **every** slot takes the audio
    path (idle slots never reach the duck swap point, `noise_organism.h:1737-1741`,
    `vorago_engine.h:1132-1136`). For each ID: one host parameter change to a non-default in-range value
    (comb fundamental: **440 Hz**, below every rate's ceiling), then **4 800 samples** (100 ms at 48 kHz,
    ≥ 2 × `kGainRampMs` so any duck, `noise_organism.h:178`, `:456-461`, has completed), then read back.
  - **MB-Voice** rows on every `i < getPolyphony()`; **VP** rows on every `i < kMaxVoices`; **Engine**
    MB rows on the engine getter; all within `1e-6` relative of the denormalized value.
  - **ENG** rows read the **chain**, not the engine's stored field: sub tone levels through
    `engine.subharmonic().getToneLevelDb(t)` (`subharmonic_engine.h:782`; with the matrix's
    `SubToneLevelOffsetDb` at its 0 dB base the value equals the parameter), ghost reverse probability
    through `engine.atmosphere().getGrainReverseProbability()` (`atmosphere_engine.h:958`); envelope rows
    through the slot-0 getters **and** `getVoice(i)` for every `i < kMaxVoices`.
  - **Noise model** rows via `getSourceModel(slot)` after the 4 800-sample wait (an applied-state getter).
    **Noise type** rows via the **Direct probe**: in the same step the slot's model ID is also set to
    `Direct`, the wait elapses, then `getSourceNoiseType(slot)` must equal the set type (a non-Direct model
    pins the effective type, `noise_organism.h:1226-1240`, so no other read-back exists without a new
    getter, which FR-007 forbids).
  - **Cavern rows (MB-Cavern and CV) — fixture.** A 4 s render at 48 kHz / 512-sample blocks with one note
    (C2, velocity 100) held from sample 0. The **reference** is the SC-002 reference chain built in the same
    test — a second `VoragoEngine` prepared with `makeVoragoEngineConfig`, a `CavernVerb` prepared with
    `makeVoragoCavernConfig` and the same seed, unity master gain and the same `processOutputStage` —
    rendered with the same 512-sample partition and the same note, with the cavern setter for this ID
    applied by hand before the first block. Plugin output vs reference: max-abs `≤ 1e-6` per channel.
    Non-vacuity (B-5): two `CavernVerb`s prepared with `makeVoragoCavernConfig`, one at the default
    targets and one with this ID's change applied as the processor applies it (matrix base for MB rows,
    setter for CV rows), fed 4 s of identical seeded white noise (±0.25), differ by `> 1e-3` RMS over
    `[3072, end)`. Precondition: the reference RMS over `[3072, end)` is `≥ 1e-4` (else lengthen,
    never loosen). Freeze additionally via `isFrozen()`.
- **SC-005 — Macros are live.** `Vorago_MacrosDriveTheMatrix`: (1) for each of the 12 macros at 1.0
  (Gravity at 0.0 and 1.0), every Voice/Engine target read back from the plugin's engine equals, exactly,
  the same getter on a second engine prepared identically and driven by a directly constructed
  `VoragoMacroMatrix` with that macro set; (2) the rendered output matches the SC-004 cavern-row reference
  chain whose engine is driven by that directly constructed matrix's `apply()` and whose cavern by its
  `applyCavernTargets(computeCavernTargets())`, both once per block, within `1e-6` max-abs per channel, same note, same partition, same RMS `≥ 1e-4`
  precondition over `[3072, end)`; (3) **inversion of Phase 11 SC-023**: all-macros-at-1 vs
  all-at-default, same script, RMS difference `> 1e-3`.
- **SC-006 — Base override composes with macros.** `VoragoMacro_TargetBaseOverride` (`dsp_systems_tests`):
  (1) with no override, `apply()`/`computeCavernTargets()` read-back equals the shipped matrix exactly for
  a set of 8 non-neutral macro vectors; (2) for each of `CloudRichness @ 0.45 + Density`, `NoiseLevelDb @
  −24 + Density`, `CavernSize @ 0.30 + Depth`, `OutputSaturation @ 0.05 + Pressure`, getter ==
  `clamp(override + contribution)` at 11 macro steps, strictly monotone; (3) override at the travel-toward
  clamp: getter monotone non-decreasing and `getTargetBase` == override; (4) non-finite / out-of-range
  target rejected; `resetTargetBases` restores every literal.
- **SC-007 — Per block, never per slice.** `Vorago_MacroPushOncePerProcess`: a test-only counter shows
  exactly one `setMacros`/`apply`/`applyCavernTargets` per `process()` for blocks carrying 0, 1, 64 and
  1024 events. **Block-size invariance — carries Phase 11 SC-008 (1)
  (`specs/vorago-phase11-plugin-scaffold/spec.md:810-822`) in full, extended to the Phase 12 paths:**
  - *Script.* The Phase 11 SC-008 (1) 4 s script — note events at sample offsets that are non-multiples of
    every partition — **plus sustain-pedal down/up events (`kSustainPedalId` points, C-9) at offsets that
    are likewise non-multiples of every partition**, placed so that at least one note-off is latched and
    later released by a pedal-up. **No parameter change after sample 0** other than the pedal (FR-043 of
    Phase 11 applies a parameter change per host block; the pedal is exempt because C-9 processes every
    point sample-accurately). At sample 0 — the one offset every partition shares — all twelve macros are
    set to a **non-neutral constant** (e.g. 0.7, Gravity 0.8) and one MB and one VP ID to non-default
    values, so the once-per-`process()` `setMacros` → `apply()` → `applyCavernTargets()` push and the VP
    broadcast are live in every partition.
  - *Partitions.* Host blocks **{1, 7, 64, 65, 512, 2048, 4096}**, each compared against the 512 reference.
  - *Precondition (P-6).* The 512 reference's peak over `[3072, end)` is `≥ 1e-4`; else the script is
    lengthened (longer render, velocity 127), never the threshold.
  - *Threshold.* Max absolute per-sample difference `≤ 1.0e-5` per channel.
  - *Structural assertions.* A partition boundary falls inside a 64-sample control chunk (block 65
    guarantees it), and the 4096 run goes through Phase 11 FR-026's sub-division, observed through
    `lastSliceCountForTest()` (`plugins/vorago/src/processor/processor.h:66`; an event-free 4096 block
    reports 2 slices). `compareFingerprints` runs `WARN`-only as a secondary check.
- **SC-008 — Full state round-trip.** `Vorago_StateRoundTripV2`: (1) all 106 persisted IDs set to
  seeded non-default values; `getState` → fresh processor `setState` → every atomic bit-identical, every
  controller normalized value within `1e-9`, and the two processors render a held note within `1e-5`
  max-abs; (2) a Phase 11 v1 stream (60 bytes, built by the v1 writer shape) restores the 14 v1 fields and
  leaves all 92 others at registered defaults; (3) truncation at **every** byte offset of a v2 stream:
  no crash, fields before the cut restored, the rest unchanged; (4) version 3 → `kResultFalse`, nothing
  changed; (5) a NaN in any float field leaves that field unchanged; (6) sustain and pressure absent from
  the stream (stream length equals the C-7 sum).
- **SC-009 — setState after prepare re-pushes everything.** `Vorago_SetStateAfterPrepareReachesDsp`: a
  prepared, rendering processor loads a state with every persisted ID non-default, renders one block, and
  every SC-004 read-back holds.
- **SC-010 — Input hygiene.** In-band unregistered IDs (e.g. 207, 399, 1116, 1599) change no atomic;
  normalized NaN / ±Inf / <0 / >1 on every ID leave values finite and in range.
- **SC-011 — Continuity (the Seraphis Phase 9 SC-005 matched-regime construction,
  `specs/seraphis-phase9-parameters/spec.md:2092-2125`).** `Vorago_ParameterStepsAreContinuous`
  (`[long]`). Scope: every continuous, non-stepped ID of C-6 plus master gain. For each ID, one render at
  48 kHz / 512-sample blocks with a note (C2, velocity 100) held from sample 0 and 1 s of warm-up, in which
  the parameter is automated from one extreme to the other in **64 equal steps spaced 288 ms apart**
  (27 blocks of 512; B-6 — Seraphis's 2 s / 64 geometry spaces steps 31.25 ms apart, which cannot seat a
  20 ms window 50 ms clear of every step, so the spacing — not the statistic — is changed):
  1. *Test statistic (B-6).* For each step, the **max** of `maxPerSampleDelta` over three **±10 ms
     windows** centred, **in the output domain**, on `step sample + {0, 1024, 3072}`: a step lands at the
     output immediately for the output-stage IDs (master gain, output saturation), after the cavern's
     1024-sample diffusion for cavern-side IDs and after the full 3072 (smear + cavern, FR-024 / SC-017)
     for engine-path IDs, so the three offsets are measured and no per-ID latency table is needed.
  2. *Reference.* **The same three windows per measured step** from the **same render**, centred on the
     midpoint between consecutive steps + {0, 1024, 3072}, so every reference window lies ≥ 50 ms clear of
     the preceding step's last test window and of the next step — **the same number of draws on both
     sides** (192).
  3. *Bound.* `max(test statistics) ≤ 1.5 × max(reference statistics)`.
  4. *Non-finite clause (every registered ID, no exemptions).* No sample is non-finite (bit-pattern test,
     never `std::isnan`), and peak ≤ the limiter ceiling (`kOutputCeilingDb = −0.3`,
     `vorago_engine.h:203`).
  *Positive controls (mandatory, both).* (a) *Detector wiring:* the same statistic over a reference window
  into which a one-sample step of **2 × that window's own `maxPerSampleDelta`** is injected must
  **exceed** the bound. (b) *Criterion wiring:* with FR-053's plugin-side
  `detail::VoragoMasterGainSmootherBypassProbe` snapping `masterGain_` (`processor.h:105`, normally
  `setTarget` at `processor.cpp:365`) to instant, master gain's 64-step render **must fail** clause 3.
  *Remedy rule:* an ID that fails clauses 1–3 gets a processor-side smoother (C-3); it is never exempted
  and the 1.5 × bound is never loosened. Stepped IDs (C-6 column "Stepped", and seed) carry clause 4 only.
- **SC-012 — Sustain.** `Vorago_SustainLatch`. Every latch/release assertion reads
  `engine_->getVoiceState(i)` (`vorago_engine.h:1065-1067`; `VoiceState` transitions
  `voice_allocator.h:38-42`) for the note's slot, **read after the block that carries the event**:
  (1) pedal down, note-off → the slot reads `Active` after that block and still `Active` after a further
  2 s; pedal up → the slot reads `Releasing` after the block carrying the pedal-up; control: the same
  note-off with the pedal up reads `Releasing` after its block; (2) within one block, note-off at offset
  100 then pedal-down at 200 → `Releasing` after the block; pedal-down at 100 then note-off at 200 →
  `Active` after the block; (3) re-strike of a latched note, then pedal up while the key is held → the
  slot reads `Active` after the pedal-up block; the later note-off → `Releasing` after its block;
  (4) `setActive(false)` and `setState` each release all latched notes, on different schedules (Q8 ruled
  (a)): `setActive(false)` releases **immediately** — on a processor with a latched note, the slot reads
  `Releasing` or `Idle` right after the call returns (never `Active`); `setState` only **raises the
  request** — the slot MUST still read `Active` immediately after `setState()` returns and MUST NOT read
  `Releasing`/`Idle` until **after the next `process()` block runs**, at which point it reads `Releasing`
  or `Idle` (never `Active`); (5) zero allocations across 10 000 random pedal/note events.
- **SC-013 — Channel pressure.** `Vorago_ChannelPressure` (OQ-1 ruled (a): the **Pressure** macro only):
  (1) pressure 0 → macro vector identical to the atomics; (2) pressure 1 with the Pressure knob at its
  default → every Pressure-macro target reaches the engine exactly as knob = 1.0 would; (3)
  **composition:** knob 0.4 + pressure 0.3 gives every Pressure-macro target read-back and `getMacros()`
  equal to knob 0.7 with pressure 0 (within `1e-6`); every other macro's target read-back is unaffected by
  pressure; (4) **clamp:** knob 0.8 + pressure 0.5 equals knob 1.0 with pressure 0; (5) `IMidiMapping`
  returns `kSustainPedalId` for CC64, `kChannelPressureId` for `kAfterTouch`, `kResultFalse` for CC1 and
  for bus 1; (6) **continuity:** channel pressure is in SC-011's scope (a continuous, non-stepped C-6 ID)
  without further test-plan changes — its 64-step sweep and 1.5× bound apply unmodified, and if it fails,
  C-3's remedy rule (a processor-side smoother) applies exactly as for any other ID.
- **SC-014 — Seed.** `Vorago_SeedParameter`: (1) index 0 is SC-002; (2) same index twice → renders within
  `1e-5`; (3) every pair of the 16 seeds differs over a 30 s held-note render by RMS difference `> 1e-3`
  (a small spread is fixed by re-picking constants, never by lowering the gate); (4) a seed change while
  silent, then a note, matches a fresh instance at that seed within `1e-5`; (5) a seed change while a note
  sounds: zero allocations, all samples finite, peak ≤ limiter ceiling.
- **SC-015 — RT safety under full automation.** `Vorago_FullSurfaceAutomationAllocFree`: every registered
  ID changed every block (seeded random) for 2 000 blocks with notes and pedal: zero heap allocations on
  the audio path (the repo's allocation-counter pattern), all samples finite, peak ≤ limiter ceiling,
  `getNonFiniteRecoveryCount()` == 0.
- **SC-016 — Wrapper overhead (carried Phase 11 gate).** `Vorago_ProcessorCpu`, run only via
  `node tools/run-cpu-tests.js vorago_tests`, alone: P/D ≤ **1.05** at registered defaults with every
  route wired (quiescent); a second arm with one MB and one VP ID automated every block is recorded
  (`WARN`), not gated. The absolute composed-chain figure stays recorded and non-gating; a result above
  the Phase 10 30 % ceiling is surfaced, never absorbed. The event-dense arm E is compared with Phase 11's
  recorded `1.79691e+07 ns` (recorded, not gated).
- **SC-017 — Latency invariance.** `Vorago_LatencyIndependentOfParameters`: `getLatencySamples()` == 3072
  at 44.1/48/96 kHz with every discrete ID at each of its values and every continuous ID at min and max.
- **SC-018 — Controller metadata.** `Vorago_ParameterInfoTable`: `getParameterCount()` == 108; every
  ID's title, units, stepCount, flags and default equal a checked-in table; the 14 Phase 11 entries are
  unchanged (type-freeze guard); every `getParamStringByValue` returns a non-empty string for 0, 0.5, 1.
  **Taper (Q4):** for every ID C-6 marks `log`, the denormalized value at normalized 0.5 equals
  `logMapFromNormalized(0.5, ...)` for that ID's range (not the linear midpoint), and for every ID marked
  `linear` it equals the linear midpoint; both checked against the same checked-in table. **Body material
  (Q5):** IDs 1004–1005 denormalize index `n` to `BodyMaterial` enum value `n` for every `n < 11`, with no
  mapping table.
- **SC-019 — Default-inert DSP additions.** `VoragoEngine_ApplyVoiceParamsDefaultIsNoOp` and
  `VoragoEngine_NewSettersDefaultInert` (`dsp_systems_tests`): a default-constructed `VoragoVoiceParams`
  broadcast, `setSubToneLevelDb(t, kDefaultToneLevelDb[t])`, `setGhostReverseProbability(0)`,
  `setGhostEventTriggers(false)` on a prepared engine → 60 s render within `1e-6` max-abs of an untouched
  engine; every pre-existing `dsp_systems_tests` / `dsp_effects_tests` case passes with no test edited
  (git diff evidence).
- **SC-020 — Boundedness (theme FR), seconds-scale.** `Vorago_RandomSurfaceSoak` (`[long]`): 5 seeds ×
  **60 s** at 48 kHz of held notes (C2 + G2, velocity 100) with the randomized set re-drawn every 5 s.
  The randomized set is every registered ID **except** master gain (0), sustain (4), channel pressure (5),
  ecology mix (500), body mix (1003) and cavern mix (1105), which stay at their registered defaults (each
  of them can legitimately drive the output toward silence, so randomizing them would make the liveness
  gate meaningless). Gates: every sample finite; peak ≤ the limiter ceiling; and, for every seed, output
  RMS over the last 10 s ≥ `R_default − 60 dB`, where `R_default` is the RMS over the same window of a
  default-surface render of the same notes measured in the same test (precondition `R_default ≥ 1e-4`).
  The minutes-scale long render belongs to Phase 14 (roadmap lines 572–574); Phase 10's 8 h engine soaks
  (`specs/vorago-phase10-voice-engine/compliance.md`, SC-004b / SC-005) already cover the engine.
- **SC-021 — Phase 10 SC-008 deferral enacted (FR-060), ruling (b) retune `kRows`.** The Phase 12
  `compliance.md` MUST carry one row each for Gravity, Pressure, Mass, quoting both the Phase 10 measured
  figures (Gravity endpoint 0.18014 vs 0.3; Pressure rho −0.566667 / endpoint 0.0735783 vs 0.9 / 3 dB;
  Mass rho −1 / endpoint −0.446365 vs 0.9 / 0.25) and the retuned figures, reading **PASSING after retune
  (ruling b)**. `VoragoMacro_SweepAxes` (`dsp/tests/unit/systems/vorago_macro_test.cpp:1211`) and
  `VoragoComposed_DepthMacroAxis` re-run on the retuned `kRows` with **Phase 10's thresholds unchanged** —
  all twelve axes pass, including Gravity endpoint ≥ 0.3, Pressure rho ≥ 0.9 and endpoint ≥ 3 dB, Mass rho
  ≥ 0.9 and endpoint ≥ 0.25; the SC-001b gate `VoragoEngine_CpuBudget` re-runs alone via
  `node tools/run-cpu-tests.js dsp_systems_tests` and passes; SC-002 passes **unchanged** (the retune may
  change row amounts / curves and add rows whose contribution is zero at every macro neutral, but may not
  move a `kRows` base — C-4 pins every default to the Phase 11 chain); and SC-006 (1) holds against the
  retuned rows. Where R-1's contingent clause applies, the compliance row also cites the probe log
  (`artifacts/fr060_probe.log`) and names the new target; the row still reads **PASSING after retune**
  at Phase 10's unchanged thresholds, or the phase is not complete.
- **SC-022 — New ENG setters survive re-prepare and disarm correctly.** `VoragoEngine_NewSettersSurvivePrepare`
  (`dsp_systems_tests`): (1) set each of `setSubToneLevelDb(t, v_t)` (three non-default values),
  `setGhostReverseProbability(0.4f)`, `setGhostEventTriggers(true)`; call `prepare()` again with the same
  config; each getter **and** its chain read-back (`subharmonic().getToneLevelDb(t)`,
  `atmosphere().getGrainReverseProbability()`) still returns the set value after the first rendered block.
  (2) With triggers on, a ghost peak of 1.0 and a gated value driven ≥ the rise threshold until
  `isGhostTriggerLatchHigh()` reads true, toggle triggers off → the latch reads **false** after the next
  block; toggle back on with the gated value still high → the latch reads true again after one control
  chunk (a fresh rising edge fired, proving the disarm, not a stale latch).
- **SC-023 — A repeated broadcast is inert (FR-004 / FR-005 early-outs).** Two arms. (1)
  `VoragoEngine_RepeatedBroadcastIsInert` (`dsp_systems_tests`): a prepared engine with polyphony 6 and six
  held notes renders 10 s twice — once with `applyVoiceParams(p)` for a non-default `p` and
  `setSubToneLevelDb(t, v_t)` called **once**, once with the identical calls repeated **every 512-sample
  block** — and the two renders match within `1e-5` max-abs per channel. Positive control (B-3): a third
  render that writes `bodyMaterialA` alternately the fixture's material / `Glass` every block through
  the same `applyVoiceParams` call differs from the first by `> 1e-3` RMS, proving the comparison can
  see a per-block write that reaches the audio. (The original slot-2 model toggle is unreachable at this
  fixture: the noise bed at −18 dB with wake 0.35 contributes ≈ 2e-6 RMS of a 0.0078 RMS render, so no
  noise-slot write can move it past the floor — measured 2026-09-25, all four slots 1.9–2.7e-6.) (2) `Vorago_HostResendsEveryParameter` (`vorago_tests`): a processor whose host re-sends **every**
  registered ID at its current value in every block renders a held note for 10 s within `1e-5` max-abs of
  one that sends nothing after block 0.

---

## Edge cases

**RT-safety boundaries**
- A host that re-sends every parameter every block: on-change trackers and FR-004/FR-005 early-outs
  guarantee no duck, ramp restart or reseed occurs for an unchanged value (Phase 2 coalescing
  requirement; `setSubToneLevelOffsetDb`'s ramp-restart reason, `vorago_engine.h:766-775`).
- `setState` runs on a non-audio thread: it writes atomics only and raises the `pushAllSurfaces` request
  **and** the sustain-latch-release request (Q8 ruled (a)); it makes no DSP call itself — both requests are
  consumed at the top of the next `process()`, where the latched notes' `noteOff` is sent.
- `setActive(false)` calls `silence()` (not an audio-thread operation, `vorago_engine.h:447`); the sustain
  latch is cleared **immediately** there too (unlike `setState`, this is not deferred to the next
  `process()`).
- Seed change while sounding reseeds the ecosystem via `reset()` (`ecosystem_engine.h:398-407`) and
  re-deals agent slots (`vorago_voice.h:961-967`): an audible re-organisation is accepted; allocation and
  non-finite output are not (SC-014 (5)). `ContinuousBody::setSeed` on a sounding body takes effect only at
  the next mode-set rebuild (`continuous_body.h:1605-1611`), so a live reseed is not retro-deterministic —
  SC-014 (4) therefore reseeds while silent.

**Parameter extremes and interactions**
- Anchor mode away from `Hybrid` makes `ResonanceGravity` — the **only** Gravity-macro row — inert
  (`vorago_voice.h:566-567`). This is recorded, not prevented; Phase 13 may surface it.
- Noise type is consumed only by `Direct` slots (`noise_organism.h:474-478`); on other models it is stored
  and persisted but inaudible until the model changes.
- Comb feedback latch (C-4): after the first push a model change leaves feedback alone.
- A deep MB value at the clamp a macro travels toward saturates that macro (C-2, SC-006 (3)).
- Loop filter mode is a documented discontinuity (`feedback_ecology.h:1032-1034`); exempt from SC-011's
  bound, asserted bounded.
- Envelope stage times apply to stages not yet entered; a change mid-stage follows `MultiStageEnvelope`'s
  own rule. Growth mode stores but does not apply pre-sustain times (`vorago_voice.h:1020-1037`).
- Polyphony shrink leaves orphan tails; VP/ENG reach them (all `kMaxVoices`), MB-voice rows do not
  (`apply()` bound `:971`, the inherited residue of C-2).
- Output saturation parameter and the Pressure macro compose (base + contribution); the true-peak limiter
  has no bypass (`vorago_engine.h:1006-1016`).
- Ghost event triggers on with reverse probability 0 spawns forward grains only; triggers change nothing
  while the ghost peak is 0.

**Sample-rate changes**
- A second `setupProcessing()` re-prepares and then `pushAllSurfaces()` re-applies every value; engine-owned
  setter values also survive `prepare()` by design (`vorago_engine.h:270-276`). Comb fundamental's plain
  range is fixed at `[20, 19845]` Hz (C-6), the component ceiling at 44.1 kHz; at any rate below 44.1 kHz
  the organism's own `sr·kMaxResonatorFrequencyRatio` clamp (`noise_organism.h:573-574`) lowers the
  effective value, and the stored parameter is not rewritten.

**Seed determinism**
- A render is a pure function of (seed index, every parameter value, note/pedal sequence); per-slot seeds
  are never advanced per note (`vorago_engine.h:526-543`). Two instances with equal state render equally.

**Cross-platform**
- Non-finite checks use `detail::isFinite` (bit pattern), never `std::isnan`. Designated initializers for
  every POD. No platform code; `IMidiMapping` is SDK-only.

---

## Traceability

| Roadmap / carried statement | FR | SC |
|---|---|---|
| "All engine parameters registered/denormalized" (line 554) | FR-003–FR-006, FR-010–FR-013, FR-020 | SC-003, SC-004, SC-010, SC-018, SC-019 |
| "persisted with `kCurrentStateVersion`" (line 554) | FR-040, FR-045, C-7 | SC-008, SC-009 |
| "concept-macro system wired" (lines 554–555) | FR-001, FR-002, FR-021 | SC-005, SC-006, SC-007 |
| "per-section parameter packs" (lines 555–556) | FR-011, C-5 | SC-018 |
| "pluginval + full round-trip tests" (line 556) | FR-050, FR-051 | SC-001, SC-008 |
| OQ-7 decided → Phase 12 (lines 639–642); leaf decisions 1 and 5 | FR-030–FR-032 | SC-012, SC-013 |
| Seed parameter (Phase 11 spec :144, :1189) | FR-023, C-8 | SC-002, SC-014 |
| Output saturation through the matrix (Phase 11 FR-041) | C-2 (MB), C-6 ID 3 | SC-004, SC-006 (2) |
| Boundedness (lines 602–604), RT safety (lines 600–601) | FR-025 | SC-015, SC-020 |
| CPU budgets are FRs (line 606) | — | SC-016 |
| Latency (Phase 11 FR-033) | FR-024 | SC-017 |
| Shared-component rule (lines 620–622) | FR-007 | SC-019 |
| Phase 10 SC-008 Gravity/Pressure/Mass deferral (roadmap lines 449–455) | FR-060, OQ-2 | SC-021 |
| Scope of "all engine parameters" (line 554) — C-1 exclusions ratified | FR-061, OQ-3 | SC-003, SC-018 |
| L13 `HarmonicCloud` spectral gravity, *"Vorago exposes it"* (roadmap line 122) | FR-003, FR-004, C-1, C-5, C-6 ID 206 | SC-003, SC-004, SC-011, SC-018 |
| Coalescing / repeated-broadcast early-outs (Phase 2 spec :328-336) | FR-004, FR-005 | SC-023 |
| New ENG setters survive re-prepare; trigger disarm (Phase 10 FR-077) | FR-006 | SC-022 |
| Phase 11 SC-008 (1) block-size invariance, carried | FR-020, FR-030 | SC-007 |

---

## Open Questions (resolved)

Questions the roadmap explicitly left to this phase (OQ-1, OQ-2), plus OQ-3, a narrowing of roadmap scope
the spec could not self-ratify. All three were put to the user and ruled in the *Clarifications* session
(2026-09-24); this section is kept as the detailed rationale record, but the ruling — not the options
below — is what the spec body (C-1/C-2, FR-021/FR-032/FR-060/FR-061, SC-013/SC-021) implements.

- **OQ-1 — The channel-pressure mapping (roadmap OQ-7, lines 639–642: *"pressure → Weight/Pressure
  macros is a natural fit"*; decided *"Phase 12"*, mechanism left open).** **Ruled (a)** (*Clarifications*
  Q2): pressure adds to the **Pressure** macro only, `effective = clamp(knob + pressure, 0, 1)`; the
  rejected alternatives were (b) adding to **Weight** and (c) adding to both. No depth parameter (C-1
  admits none) and no smoother added up front (SC-011's existing continuity sweep, which already covers
  channel pressure as a continuous, non-stepped C-6 ID, decides whether one is needed). Per-note (MPE)
  expression stays out of scope (no per-note DSP API; after the first release adding
  `INoteExpressionController` becomes the accepted controller-FUID host-cache hazard, leaf `CLAUDE.md`
  decision 1). Enacted by FR-021, FR-032; verified by SC-013.
- **OQ-2 — Phase 10 SC-008's Gravity, Pressure and Mass rows (roadmap Phase 10 status, lines 449–455:
  *"the fix deferred to Phase 12 as a product decision"*).** Measured (`specs/vorago-phase10-voice-engine/
  compliance.md:20`): Gravity endpoint 0.18014 vs bound 0.3 (the keyed ratio table caps it at 24 % in
  theory, spec Q-N); Pressure rho −0.567 / endpoint 0.0736 vs 0.9 / 3 dB; Mass rho −1 / endpoint −0.446 vs
  0.9 / 0.25. Thresholds are never moved. **Ruled (b)** (*Clarifications* Q1): retune/add `kRows` rows so
  each axis passes Phase 10's unchanged thresholds (a sound change at non-neutral macros, re-running
  Phase 10's `VoragoMacro_SweepAxes` and CPU gates; Gravity gains a new row, since its single row is
  structurally capped); the rejected alternatives were (a) keep the three rows FAILED by ruling and (c)
  defer to Phase 14 listening (which would have required a same-change roadmap edit). Enacted by FR-060;
  verified by SC-021.
- **OQ-3 — Scope of "all engine parameters" (roadmap line 554).** C-1 reads the phrase as three classes
  and **excludes**: (a) prepare-time configuration — `VoragoVoiceConfig` capacities, `VoragoEngineConfig`
  FFT sizes / capture length / enable flags, `CavernVerb::PrepareConfig` (they allocate or move latency);
  (b) controls overwritten by another path — `VoragoVoice::setEcosystemDepthFor` (the `EcosystemDepth`
  target fills all four per-destination depths on every `apply()`, `vorago_voice.h:1309-1314`);
  (c) internal voicing with no forwarder and no earlier Phase 12 promise — per-peak levels, ecology coupling,
  bloom fade times, scheduler envelopes, breath/tide rates (`vorago_voice.h:567-693`); and
  `BloomEngine::Relation` (Phase 7 says only *"may become"*, `specs/vorago-phase7-harmonic-bloom/spec.md:205`).
  **Ruled (i)** (*Clarifications* Q3): ratify C-1 as written (108 registered IDs, 106 persisted); the
  rejected alternatives were (ii) admitting named class-(c) controls or `Relation` and (iii) admitting
  `setEcosystemDepthFor` by moving the `EcosystemDepth` target off `fill()` — class (a) cannot be a
  run-time parameter under FR-024/FR-025, class (b) would be silently erased every block, and class (c) is
  voicing the macros already reach. Enacted and verified by FR-061.

---

## Review notes

- Written 2026-09-24 against `f149cced`. Every line citation was read this session. The
  `VoragoMacroTarget` comment line numbers inside `vorago_macro_matrix.h` (e.g. `setRichness (:1052)`) are
  stale against `vorago_voice.h` (`setRichness` is at `:1114`); this spec cites the voice header's actual
  lines and does not edit those comments (surgical-changes rule).
- **Review pass 1 (2026-09-24), all 16 issues applied, none rejected.** Resolutions of note:
  - *Spectral gravity (L13):* resolved by **exposure**, not by an open question — roadmap `:122` promises
    it and Phase 10 Q4 left it on the "control surface". New VP ID 206 and `setCloudSpectralGravity`;
    totals move to 108 registered / 106 persisted; SC-010's unregistered example moves from 206 to 207.
  - *SC-011:* the Seraphis construction is taken verbatim **except for step spacing** (125 ms, not
    31.25 ms), because 64 steps in 2 s cannot seat a 20 ms reference window 50 ms clear of every step;
    the statistic, the output-domain latency shift (3072 here), equal draws, the 1.5 × bound and both
    positive controls are unchanged.
  - *SC-020:* shortened from 5 × 10 min to 5 × 60 s and its floor derived from a measured default render,
    because the spec's own Non-goals defer minutes-scale renders to Phase 14 (roadmap lines 572–574);
    this is a scope correction against the roadmap, not a relaxed threshold.
  - *SC-004 noise-type rows:* no requested-type getter is added (FR-007 limits `dsp/` edits to the three
    Vorago headers and `NoiseOrganism` is not one); the Direct probe observes the requested type instead.
  - *SC-004 cavern rows:* the reference applies the identical output stage (the issue's second option),
    keeping the `1e-6` bound.
