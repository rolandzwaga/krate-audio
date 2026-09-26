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
  `parameters/` = one pack header per band (`global_params.h`, `macro_params.h`, `cloud_params.h` …
  `life_params.h`) plus `param_mapping.h` (tapers) and `param_routes.h` (the route table);
  `processor/sustain_latch.h` is the wrapper-side CC64 latch; `ui/` is **empty** (`.gitkeep`) until Phase 13
  registers custom views; `preset/` and `update/` are the shared-config adapters
  (`makeVoragoPresetConfig()` / `makeVoragoUpdateConfig()`).
  Generated, never hand-edited, never committed (see `.gitignore`): `src/version.h`,
  `resources/win32resource.rc`, `resources/auv3/audiounitconfig.h` — only `audiounitconfig.h.in` is authored.
- **Buses:** 1 event input, 1 stereo audio output, **no `addAudioInput()`**. `kSupportedNumChannels` is
  `02` and `au-info.plist` declares exactly `Inputs 0 / Outputs 2`; a mismatch between the bus layout and
  those two files is the documented AU `-10875` init failure.
- **Param IDs:** flat base 0 with 100-ID section gaps. The **reserved map is load-bearing** — a later phase
  must claim its own band, never squat in another (code of record: the comment above
  `enum ParameterIDs` in `src/plugin_ids.h`, and the `k{Section}ParamRangeEnd` constants below it, which
  drive the per-pack dispatch):

  | Range | Section | Phase |
  |---|---|---|
  | 0–99 | Global: master gain 0, polyphony 1, seed 2, output saturation 3, sustain pedal 4, channel pressure 5 | 0–1 in 11; 2–5 in 12 |
  | 100–199 | Macros (Darkness, Age, Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass) | registered in 11; **LIVE** from 12 |
  | 200–299 | Cloud | 12 |
  | 300–399 | Noise (per-slot model 310–313, type 320–323, comb 330–353) | 12 |
  | 400–499 | Resonance | 12 |
  | 500–599 | Ecology (loop filter modes 510–515) | 12 |
  | 600–699 | Sub | 12 |
  | 700–799 | Smear | 12 |
  | 800–899 | Events | 12 |
  | 900–999 | Ecosystem | 12 |
  | 1000–1099 | Body | 12 |
  | 1100–1199 | Space | 12 |
  | 1200–1299 | Envelope | 12 (new band) |
  | 1300–1399 | Bloom | 12 (new band) |
  | 1400–1499 | Ghost | 12 (new band) |
  | 1500–1599 | Life | 12 (new band) |
  | 1600+ | **Unassigned** — a later phase claims a whole band | — |

  108 registered IDs, 106 persisted. Macro IDs obey `id - 100 == static_cast<int>(VoragoMacro::X)`.
  Registered types are **frozen**: `kMasterGainId` and the 12 macros are plain `Steinberg::Vst::Parameter`,
  `kPolyphonyId` is a `StringListParameter`, and every Phase 12 ID keeps the type it shipped with — never
  swap a type at the same ID. `kSustainPedalId` and `kChannelPressureId` are hidden, automatable
  performance controllers and are **never persisted** (FR-045).
  Pipeline to add one: `plugin_ids.h → parameters/ (pack + param_routes.h) → processor → controller →
  resources/editor.uidesc`. A new ID with no `kParamRoutes` row, or a drifted route total, fails the
  `static_assert`s in `param_routes.h` at compile time.
- **Route table** (`src/parameters/param_routes.h`, `kParamRoutes`, ascending ID, exactly one route per ID):

  | Route | Count | Meaning |
  |---|---|---|
  | MB | 39 | a `VoragoMacroMatrix` target base via `setTargetBase` (ID → target in `kMbRoutes`) |
  | VP | 31 | a per-voice `VoragoVoiceParams` field, broadcast by `applyVoiceParams` on a generation bump |
  | ENG | 14 | a direct `VoragoEngine` setter (polyphony, seed, sub tone levels, envelope, ghost reverse/triggers) |
  | CV | 9 | a direct `CavernVerb` setter outside the matrix (density … damper rate, freeze) |
  | MAC | 13 | a macro value: the 12 macros + channel pressure (summed into Pressure, clamped to [0, 1]) |
  | Local | 2 | consumed by the processor itself: master gain, sustain pedal |

  The totals are `static_assert`ed in `param_routes.h`; change the table and the asserts together.
- **State:** `kCurrentStateVersion = 2` and `kStateV2Bytes = 428` live in `plugin_ids.h` (shared by
  processor and controller with no cross-include; the byte count is itself a `static_assert`ed sum).
  **The 60-byte v1 stream is a strict prefix of v2** — never reorder or remove a v1 field:

  | Block | Fields | Bytes |
  |---|---|---|
  | header | int32 version (= 2) | 4 |
  | v1 global | float masterGain, int32 polyphony | 8 |
  | v1 macros | 12 × float | 48 |
  | v2 global | int32 seedIndex, float outputSaturation | 8 |
  | cloud · noise · resonance · ecology | 7F · 3F+4I+4I+4F+4F+4F · 3F+1I · 2F+6I | 28 · 92 · 16 · 32 |
  | sub · smear · events · ecosystem | 5F · 3F · 1F · 1F | 20 · 12 · 4 · 4 |
  | body · space · envelope | 4F+2I · 15F+1I · 1I+6F | 24 · 64 · 28 |
  | bloom · ghost · life | 2F · 3F+1I · 3F | 8 · 16 · 12 |
  | **total** | | **428** |

  Packs are written in ascending ID band, fields in ID order. Load: version `> 2` → `kResultFalse`,
  nothing changed; `≤ 1` → v1 block only, every Phase 12 field keeps its current value; `== 2` → the
  pack chain, short-circuiting on EOF so a truncated stream restores its prefix and leaves the rest
  unchanged. Sustain pedal and channel pressure are never written. The next format change appends after
  byte 428 — it never rewrites v2.
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
Phase 12 wired the live macros and kept this: the pre-slice order in `process()` is
`pushGlobalParams → ENG → CV → VP → MB bases → setMacros → apply → applyCavernTargets`, all **per block,
never per slice.** A smoothing remedy steps its smoother once per `process()` in that same pre-slice
step — never inside the slice loop.

## `pushAllSurfaces()` — what it re-sends, and the two deliberate exclusions (D-P2)

`pushAllSurfaces(Scope)` invalidates the route trackers so the next push re-sends every value. It is
reached **directly** from `setupProcessing()` (`Scope::Reprepared` — `VoragoVoice::prepare()` resets every
VP field and envelope time, so without it a re-prepare silently reverts the user's values) and, by a
release-store request, from `setState()` (`Scope::PresetLoad`, consumed at the top of the next `process()`
after the not-ready guard). It invalidates MB, VP (generation bump + sentinel), ENG and CV.

**Two trackers are deliberately NOT invalidated** (plan decision D-P2, a documented narrowing of spec
C-3's "every tracker"):
- **Seed** — prepare consumes the seed, and `setState` delivers a *changed* seed through the ordinary
  value compare. Forcing a re-push of an unchanged seed is a live reseed — an audible re-organisation of
  the drone on every preset load.
- **Polyphony** — its edge detector compares against `engine_->getPolyphony()`, which survives prepare;
  forcing it would falsify Phase 11's polyphony edge-trigger counter.

Do not "fix" either exclusion. Every other re-push of an unchanged value is inert by construction.

## Near-name hazard — `Vorago::` vs `Seraphis::`

Many Vorago names are identical to Seraphis names in another namespace: parameter enumerators
(`kSeedId`, `kCloudRichnessId`, …) and pack structs (`Vorago::CloudParams` / `Seraphis::CloudParams`,
`Vorago::BodyParams` / `Seraphis::BodyParams`). This is not an ODR violation — different namespaces,
different targets, and `vorago_tests` never links Seraphis sources. **No TU may `using namespace` both
`Vorago` and `Seraphis`**: the unqualified names become ambiguous at best and silently bind to the wrong
plugin's ID at worst. Qualify explicitly in any code that sees both.

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

### 1. OQ-7 (MPE / channel pressure) — **DELIVERED in Phase 12 as `IMidiMapping`**

**Delivered (Phase 12, FR-031):** the controller implements `IMidiMapping`. `getMidiControllerAssignment`
maps, on bus 0 and any channel, CC64 (`kCtrlSustainOnOff`) → `kSustainPedalId` and channel aftertouch
(`kAfterTouch`) → `kChannelPressureId`; every other controller returns `kResultFalse`. Channel pressure is
a MAC route: it is added to the Pressure macro and clamped to [0, 1]; at pressure 0 the macro vector
equals the knobs bit-for-bit. There is still **no** `INoteExpressionController` and no per-note
expression. The interface landed while unreleased at `0.1.0`, so it carries no host-cache cost; the
caveat below applies to any interface added after the first release.

**Phase 11 record:** roadmap Open Question 7 was resolved in Phase 11 (Clarification Q3, 2026-09-24): Phase 11 ships **no**
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

### 3. Output saturation — **shipped through the matrix** (Phase 12); still no soft-limit parameter

Seraphis ships `kSoftLimitId` → `setOutputSaturation`. **Vorago ships no soft-limit parameter.**
`VoragoMacroMatrix::apply()` rewrites `engine.setOutputSaturation(...)` on every call
(`dsp/include/krate/dsp/systems/vorago_macro_matrix.h:1067`, driven by the Pressure row at `:506–511`,
base `0.12f`, amount `0.88f` after the Phase 12 retune), so a soft-limit parameter pushed to the same setter would be silently
overwritten at the next `apply()` — a control that does nothing, which is a defect.

**Shipped mechanism (Phase 12):** `kOutputSaturationId` (ID **3**, [0, 1], default 0.12) is an **MB**
route: it writes `setTargetBase(VoragoMacroTarget::OutputSaturation, …)` on the matrix, and the Pressure
row composes on top of that base at the next `apply()`. A user saturation control goes **through** the
matrix, never to `engine.setOutputSaturation` directly, and no separate soft-limit parameter is added.
The rationale is narrowed (Clarification Q1) to controls that are **overwritten by another path**. (In
Phase 11 the twelve macros were registered and inert as the explicit unreleased-`0.1.0` exception; they
are live from Phase 12.)

### 4. Polyphony "1".."6" — the 30 % ceiling gates only the default 4

`kPolyphonyId` is a `StringListParameter` exposing "1".."6" (up to `VoragoEngine::kMaxVoices`), default
index 3 = **4 voices** (Clarification Q2, 2026-09-24). The 30 %-of-one-core CPU ceiling gates only the
shipped **default of 4**, not the maximum. Polyphony 5 and 6 measured **37.99–43.41 %** of one core
(engine alone) in Phase 10 and are offered **deliberately**, as an opt-in cost. Do not "fix" this by
clamping the list to 4 or by relaxing the ceiling to cover 6.

### 5. CC64 (sustain) — **DELIVERED in Phase 12** as a wrapper-side note-off latch

**Delivered (Phase 12, FR-030):** `Vorago::SustainLatch` (`src/processor/sustain_latch.h`; fixed-size
bitsets, allocation-free, audio thread only). CC64 arrives through `IMidiMapping` as `kSustainPedalId`
(down = value ≥ 0.5). Pedal points are merged into the slice loop at their sample offsets, **notes first,
then pedal at equal offsets** (D-P6). While down, note-offs are latched; pedal-up releases latched notes
whose keys are no longer held; a re-strike clears a note's latch mark; a velocity-0 note-on goes through
the latch as a note-off. `setActive(false)` releases every latched note before silencing; `setState()`
zeroes sustain and channel pressure and raises a latch-release request consumed at the next `process()`
(it makes no engine call). No `dsp/` change was needed.

**Phase 11 record:** in Phase 11, CC64 and every other non-note event are **ignored** (FR-031; Clarification Q4,
2026-09-24). `VoragoEngine` has no sustain API, so sustain arrives in Phase 12 — together with the
`IMidiMapping` addition from decision 1 — implemented **in the wrapper** as a note-off latch: while the
pedal is down, note-offs are held and released on pedal-up. No `dsp/` change is implied.
