# plugins/vorago/ — Vorago Dark-Ambient Drone Instrument

Auto-loads when working under `plugins/vorago/`. Root `CLAUDE.md` still applies.

- **Type:** dark-ambient drone instrument (AU `aumu`, subtype `Vrgo`, manufacturer `KrAt`,
  bundle base `com.krateaudio.vorago`). **Version:** see `version.json` (unreleased `0.1.0`).
- **Roadmap:** `specs/Vorago-roadmap.md` — the plugin wrapper starts at Phase 11
  (`specs/vorago-phase11-plugin-scaffold/`); the DSP it drives is Phases 1–10a in
  `dsp/include/krate/dsp/` (`VoragoEngine`, `VoragoVoice`, `VoragoMacroMatrix`, `CavernVerb`).
  **No DSP lives in this plugin** — `src/engine/` holds prepare-time *config* only.
- **src skeleton:** `controller/ engine/ parameters/ preset/ processor/ ui/ update/`
  — `engine/` = `vorago_engine_config.h` (thin prepare-time config factories, no DSP);
  `parameters/` = `global_params.h`, `macro_params.h`; `ui/` is **empty** (`.gitkeep`) until Phase 13
  registers custom views; `preset/` and `update/` are the shared-config adapters
  (`makeVoragoPresetConfig()` / `makeVoragoUpdateConfig()`).
  Generated, never hand-edited, never committed (see `.gitignore`): `src/version.h`,
  `resources/win32resource.rc`, `resources/auv3/audiounitconfig.h` — only `audiounitconfig.h.in` is authored.
- **Buses:** 1 event input, 1 stereo audio output, **no `addAudioInput()`**. `kSupportedNumChannels` is
  `02` and `au-info.plist` declares exactly `Inputs 0 / Outputs 2`; a mismatch between the bus layout and
  those two files is the documented AU `-10875` init failure.
- **Param IDs:** flat base 0 with 100-ID section gaps. The **reserved map is load-bearing** — a later phase
  must claim its own band, never squat in another (code of record: the comment above
  `enum ParameterIDs` in `src/plugin_ids.h`):

  | Range | Section | Phase |
  |---|---|---|
  | 0–99 | Global (master gain, polyphony) | 11 — shipped |
  | 100–199 | Macros (Darkness, Age, Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass) | 11 — shipped **inert**; wired in 12 |
  | 200–299 | Cloud | 12 |
  | 300–399 | Noise | 12 |
  | 400–499 | Resonance | 12 |
  | 500–599 | Ecology | 12 |
  | 600–699 | Sub | 12 |
  | 700–799 | Smear | 12 |
  | 800–899 | Events | 12 |
  | 900–999 | Ecosystem | 12 |
  | 1000–1099 | Body | 12 |
  | 1100–1199 | Space | 12 |
  | 1200+ | **Unassigned** — Phase 12 claims a whole band for any unnamed section | — |

  Macro IDs obey `id - 100 == static_cast<int>(VoragoMacro::X)`. Registered types are **frozen**:
  `kMasterGainId` and the 12 macros are plain `Steinberg::Vst::Parameter`, `kPolyphonyId` is a
  `StringListParameter` — never swap a type at the same ID.
  Pipeline to add one: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
  State version lives in `plugin_ids.h` (`kCurrentStateVersion = 1`), shared by processor and controller
  with no cross-include.
- **Tests:** `vorago_tests` (the timed CPU case is hidden behind `[.perf]`; run it only via
  `node tools/run-cpu-tests.js vorago_tests`, alone).
  ```bash
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target vorago_tests
  build/windows-x64-release/bin/Release/vorago_tests.exe 2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"`

## The live-macro push rule (P-10) — once per `process()`, never per slice

`Processor::process()` slices the block at every event offset. The macro push
(`macros_.apply(*engine_)`) and the cavern-target push
(`applyCavernTargets(*cavern_, macros_.computeCavernTargets())`) run **once per `process()` call**, after
`pushGlobalParams()` and **before the first slice** — never inside the slice loop. Both read only
`macros_`, which does not change inside a call, and both write stored bases that survive a mid-block
note-on / steal / retrigger, so a per-slice repeat buys nothing and costs a full matrix fan-out per event.
Phase 12, when it wires the live macros, keeps this: **per block, never per slice.**

## IMMUTABLE identity — the FUIDs

Both GUIDs were generated once in Phase 11 (T001, recorded in
`specs/vorago-phase11-plugin-scaffold/compliance.md`) and are **IMMUTABLE**. Changing either one after
release makes every host treat the plugin as a different class: saved projects lose their instance, and
preset/state bindings referencing the processor UID are orphaned. Never regenerate, never "tidy", never
reuse elsewhere.

| Class | Constant | Value (verbatim, as in `src/plugin_ids.h`) |
|---|---|---|
| Processor | `Vorago::kProcessorUID` | `0xE25977E5, 0xFF444D29, 0x98D56C21, 0x3F178458` |
| Controller | `Vorago::kControllerUID` | `0xBDDF94B8, 0xEABE4000, 0x8EE4CD39, 0xA8107DBC` |

The Windows installer `AppId` (`installers/windows/setup.iss`) is a third, separate GUID,
`{5716199F-E321-435B-A60E-4F7AFAA1ADBD}` — equally immutable once an installer ships.

`src/plugin_ids.h` is the code-of-record; this table is the **durable prose record**. If the two ever
disagree, `plugin_ids.h` as shipped in the released binary wins — and the disagreement is a bug to be
investigated, not silently "fixed" by editing the header.

## Decisions that outlive Phase 11

### 1. OQ-7 (MPE / channel pressure) — **DECIDED: deferred to Phase 12**

Roadmap Open Question 7 was resolved in Phase 11 (Clarification Q3, 2026-09-24): Phase 11 ships **no**
note-expression surface — no `INoteExpressionController`, no `IMidiMapping`, no declared note-expression
types (FR-019). The engine's note API carries no per-note expression (`VoragoEngine::noteOn(note,
velocity)` / `noteOff(note)`), so an interface today would have nothing to drive. The event-input bus
is the whole note surface. Phase 12 takes it, together with sustain (decision 5).

**The controller-FUID host-cache caveat.** Adding an interface (`INoteExpressionController` or
`IMidiMapping`) to an already-released controller FUID can invalidate host-cached class metadata: hosts
cache the interface set they discovered for a class UID, and users do not clear plugin caches. Vorago is
**unreleased at `0.1.0`**, so a Phase 12 addition lands before any release and has **no host-cache cost**.
That window closes at the first release — after it, adding an interface is the accepted-hazard situation
Seraphis documented, and `kControllerUID` is still never regenerated over it.

### 2. Preset categories only grow — `Drones` is permanent

Phase 11 seeds exactly one category, **`Drones`** (`resources/presets/Drones/`, and the
`subcategoryNames` list in `src/preset/vorago_preset_config.h`). It is a **seed, not a placeholder**.
Phase 14 extends the list, but categories **only grow**: a shipped category is **never renamed**,
reordered out of existence, or removed, because renaming a category orphans every preset saved in it.
This is the Membrum/Seraphis lesson (Membrum's kit categories are fixed for exactly this reason).

The category name is carried in **two** places and they must **always agree**: the filesystem
subdirectory (`resources/presets/{Category}/`, at runtime `C:\ProgramData\Krate Audio\Vorago\{Category}\`)
and the preset XML metadata / `subcategoryNames`. Adding a category means adding it to both, in the same
change.

### 3. The soft-limit omission (FR-041) — deliberate, do not add it back naively

Seraphis ships `kSoftLimitId` → `setOutputSaturation`. **Vorago ships no soft-limit parameter.**
`VoragoMacroMatrix::apply()` rewrites `engine.setOutputSaturation(...)` on every call
(`dsp/include/krate/dsp/systems/vorago_macro_matrix.h:968`, driven by the Pressure row at `:476–481`,
base `0.12f`, amount `0.35f`), so a soft-limit parameter pushed to the same setter would be silently
overwritten at the next `apply()` — a control that does nothing, which is a defect. If Phase 12 wants a
user output-saturation control, it must go **through** the macro matrix (e.g. as a base/offset the
matrix composes), not around it. The rationale is narrowed (Clarification Q1) to controls that are
**overwritten by another path**; it does not cover the twelve macros, which are registered and inert as
the explicit unreleased-`0.1.0` exception.

### 4. Polyphony "1".."6" — the 30 % ceiling gates only the default 4

`kPolyphonyId` is a `StringListParameter` exposing "1".."6" (up to `VoragoEngine::kMaxVoices`), default
index 3 = **4 voices** (Clarification Q2, 2026-09-24). The 30 %-of-one-core CPU ceiling gates only the
shipped **default of 4**, not the maximum. Polyphony 5 and 6 measured **37.99–43.41 %** of one core
(engine alone) in Phase 10 and are offered **deliberately**, as an opt-in cost. Do not "fix" this by
clamping the list to 4 or by relaxing the ceiling to cover 6.

### 5. CC64 (sustain) — deferred to Phase 12 as a wrapper-side note-off latch

In Phase 11, CC64 and every other non-note event are **ignored** (FR-031; Clarification Q4,
2026-09-24). `VoragoEngine` has no sustain API, so sustain arrives in Phase 12 — together with the
`IMidiMapping` addition from decision 1 — implemented **in the wrapper** as a note-off latch: while the
pedal is down, note-offs are held and released on pedal-up. No `dsp/` change is implied.
