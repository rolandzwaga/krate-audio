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
  `processor/sustain_latch.h` is the wrapper-side CC64 latch; `processor/ecosystem_frame.h` is the POD
  `EcosystemFrame` payload (shared by processor and controller) and `processor/ecosystem_frame_builder.h` its
  **processor-only** pure fill helpers; `ui/` holds `ecosystem_view.{h,cpp}` (`Vorago::UI::EcosystemView`, the
  live habitat display) and `panel_sub_controller.{h,cpp}` (`VoragoPanelSubController`, all session-UI
  routing) — see "Phase 13 interface" below; `preset/` and `update/` are the shared-config adapters
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
  | 900–999 | Ecosystem: depth 900, **Ecosystem Sync 901, Self Affinity 902** (the Phase 14 rule knobs, FR-072) | 12; 901–902 in 14 |
  | 1000–1099 | Body | 12 |
  | 1100–1199 | Space | 12 |
  | 1200–1299 | Envelope | 12 (new band) |
  | 1300–1399 | Bloom | 12 (new band) |
  | 1400–1499 | Ghost | 12 (new band) |
  | 1500–1599 | Life | 12 (new band) |
  | 1600+ | **Unassigned** — a later phase claims a whole band | — |

  **110 registered IDs, 108 persisted** (Phase 14 added 901, 902). Macro IDs obey `id - 100 == static_cast<int>(VoragoMacro::X)`.
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
  | VP | 33 | a per-voice `VoragoVoiceParams` field, broadcast by `applyVoiceParams` on a generation bump (901, 902 are VP) |
  | ENG | 14 | a direct `VoragoEngine` setter (polyphony, seed, sub tone levels, envelope, ghost reverse/triggers) |
  | CV | 9 | a direct `CavernVerb` setter outside the matrix (density … damper rate, freeze) |
  | MAC | 13 | a macro value: the 12 macros + channel pressure (summed into Pressure, clamped to [0, 1]) |
  | Local | 2 | consumed by the processor itself: master gain, sustain pedal |

  The totals are `static_assert`ed in `param_routes.h`; change the table and the asserts together.
- **State:** `kCurrentStateVersion = 3`, `kStateV2Bytes = 428` and `kStateV3Bytes = 436` live in `plugin_ids.h`
  (shared by processor and controller with no cross-include; both byte counts are `static_assert`ed sums).
  **The 60-byte v1 stream is a strict prefix of v2, and v2 is a strict prefix of v3** — never reorder or
  remove a field of an earlier version:

  | Block | Fields | Bytes |
  |---|---|---|
  | header | int32 version (= 3) | 4 |
  | v1 global | float masterGain, int32 polyphony | 8 |
  | v1 macros | 12 × float | 48 |
  | v2 global | int32 seedIndex, float outputSaturation | 8 |
  | cloud · noise · resonance · ecology | 7F · 3F+4I+4I+4F+4F+4F · 3F+1I · 2F+6I | 28 · 92 · 16 · 32 |
  | sub · smear · events · ecosystem | 5F · 3F · 1F · 1F | 20 · 12 · 4 · 4 |
  | body · space · envelope | 4F+2I · 15F+1I · 1I+6F | 24 · 64 · 28 |
  | bloom · ghost · life | 2F · 3F+1I · 3F | 8 · 16 · 12 |
  | **v2 total** | | **428** |
  | v3 ecosystem rule knobs (Phase 14, FR-072) | float syncRate, float selfAffinity | 8 |
  | **v3 total** | | **436** |

  Packs are written in ascending ID band, fields in ID order. Load: version `> 3` → `kResultFalse`,
  nothing changed; `≤ 1` → v1 block only, every Phase 12 field keeps its current value; `== 2` → the
  pack chain, short-circuiting on EOF so a truncated stream restores its prefix and leaves the rest
  unchanged; `== 3` → the same chain plus the 8-byte ecosystem rule-knob extension. Sustain pedal and
  channel pressure are never written. The next format change appends after byte 436 — it never rewrites v3.
- **Tests:** `vorago_tests` (the timed CPU cases — `Vorago_ProcessorCpu`, `Vorago_PresetCpu` — are hidden
  behind `[.perf]`; run them only via `node tools/run-cpu-tests.js vorago_tests`, alone). The factory-preset
  **sweep lane** is tagged `[long][vorago-sweep]` (`tests/integration/preset_sweep_test.cpp`): hours of
  rendering, run as concurrent shards `VORAGO_SWEEP_SHARD=i/N VORAGO_SWEEP_OUT=<dir>` followed by
  `VORAGO_SWEEP_IN=<dir> "[vorago-aggregate]"` (`tools/run-close-lanes.js` does this); CI excludes it from
  the generic nightly (`[long]~[vorago-sweep]`) and runs it in its own nightly job. The CPU runner's filter
  excludes it too, so the sweep never shares a timing run.
- **Factory presets:** `resources/presets/{Category}/*.vstpreset` are **generated**, never hand-edited:
  `tools/vorago_preset_defs.h` (the 42 defs, each a primary capability cell plus secondaries) → the
  `vorago_preset_generator` tool → `cmake --build … --target generate_vorago_presets` rewrites the tree.
  `Vorago_FactoryPresets_TreeMatchesGenerator` fails when the committed tree drifts from the defs;
  `node tools/check-preset-generator-determinism.js --plugin vorago` is the release-gate determinism check.
  Ghost density and Ghost Event Triggers are **additive** (Phase 14 FR-061, Clarification Q8 2026-09-29):
  the 0.30 grains/s scheduler keeps running when triggers are On; triggered grains add on top of it.
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

## Phase 13 interface — frame data path, binding, pages, session tags

Spec: `specs/vorago-phase13-ui/` (plan §3–§7 are normative).

**Ecosystem frame data path — one-way, change-triggered.** Processor → controller only, over VST3
DataExchange with `userContextID = kEcosystemFrameUserContextId` (`0x5645434F`, `'VECO'`), block size
`sizeof(EcosystemFrame)` = **1072 bytes** (pinned by `static_assert`s in `processor/ecosystem_frame.h`; the
struct is trivially copyable and memcpy'd, so never reorder or pad it). `Processor::publishEcosystemFrame()`
runs once per `process()`, after the slice loop; it is gated on a connected `DataExchangeHandler` and fills
and sends a frame **only when the habitat changed** (control-step count, focus voice or agent count differs
from the last fill, or a resync is pending after `connect()`). The focus is the newest sounding voice. Nothing
flows back: the view never writes to the processor.
- `ecosystem_frame.h` includes `<cstddef> <cstdint> <type_traits>` only — the controller and `ui/` may include it.
- `ecosystem_frame_builder.h` pulls in the Layer 3 `ecosystem_engine.h` and is **processor-only**: never
  include it from `controller/` or `ui/`. `EcosystemView` keeps its own local torus delta for the same reason.
- **Two test seams:** `setEcosystemFrameForcedForTest(true)` forces the **gate and the trigger** (every call
  fills — used for the CPU case); `setEcosystemFrameEnabledForTest(true)` opens the **gate only** and leaves
  the natural change trigger in place (used for cadence). Shipping logic never branches on either except at
  the gate. A seam-free `ConnectedFixture` in `tests/integration/ecosystem_frame_test.cpp` covers the real handler.

**Binding — 108 IDs, `{4, 5}` is the complete reachability allowlist.** `resources/editor.uidesc` binds
every registered ID exactly once except `kSustainPedalId` (4) and `kChannelPressureId` (5), which are hidden
and reached only through `IMidiMapping`. Adding a parameter means adding its control-tag and a bound view;
never widen the allowlist.

**Page table** (seven sibling `CViewContainer`s `page-0`..`page-6` under `page-area`, switched by the
`CSegmentButton` page strip — never a `UIViewSwitchContainer`). Grid: 68 × 74 cells; continuous IDs are
44 × 44 `ArcKnob`s, list IDs 96 × 18 `COptionMenu`s, Freeze (1115) and Ghost Event Triggers (1403) are
`CCheckBox`es. The twelve macros sit as 96 × 96 `ArcKnob`s either side of the `EcosystemView`.

| Page | Rows (one row = one functional group) |
|---|---|
| 0 Cloud | r0: 200–206 · r1: 1300, 1301 (Bloom) |
| 1 Noise | r0: 300, 301, 302 · then one column group per slot s (0–3): menus 310+s, 320+s, knobs 330+s, 340+s, 350+s |
| 2 Resonance | r0: 400, 401, 402, menu 403 · r1: 500, 501 · r2: menus 510–515 |
| 3 Body | r0: 1000–1003, menus 1004, 1005 · r1: menu 1200, knobs 1201–1206 |
| 4 Sub / Smear | r0: 600, 601, 610, 611, 612 · r1: 700, 701, 702 |
| 5 Space | r0: 1100–1107 · r1: 1108–1114, checkbox 1115 |
| 6 Life | r0: 800, 900, 901, 902 (four knobs; Eco Sync and Self Affinity since Phase 14) · r1: 1400, 1401, 1402, checkbox 1403 · r2: 1500, 1501, 1502 |

**Session tags are `>= 9000` and never a ParamID** (`Vorago::UI::kSessionTagBase = 9000` in
`ui/panel_sub_controller.h`; preset button 9000, page strip 9100; the highest registered ID is 1502).
Controls with no parameter carry a `session-tag` attribute; `VoragoPanelSubController` assigns the tag and
swallows their `valueChanged` / `controlBeginEdit` / `controlEndEdit`, so none reaches `performEdit`. Keep
new session controls in this range and never let the parameter bands grow into it.

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

**Closed at 1.0.0 (Phase 14, FR-064).** The controller interface set is **frozen** from version 1.0.0: it
is exactly the set shipped at 1.0.0. Any further interface would carry the host-cache
cost above; do not add one without the user ruling on it.

### 2. Preset categories only grow — `Drones` is permanent; Phase 14 fixed the seven

**Phase 14 (1.0.0):** the shipped list is **Drones, Abyss, Caverns, Organisms, Machines, Textures, Ghosts**
(`subcategoryNames` in `src/preset/vorago_preset_config.h`; 42 presets, at least three per category, FR-004). These
seven are now shipped and therefore permanent under the rule below.

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
