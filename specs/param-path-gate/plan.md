# Parameter-path gate — implementation plan

**Date:** 2026-11-22 · **Status:** not started. Implement after the Ruinae audit fix plan (`specs/ruinae-audit/fix-plan.md`, phases 3-6) is committed. · **Branch:** `feat/param-path-gate`, from `main` once `audit/ruinae` is merged (ruling 4 below).

## Goal

One generic quality gate, run for every plugin under `plugins/`, that proves for each parameter a user can change:

1. the edit reaches the processor at all;
2. the processor and the controller read it in the same format (range, curve, step count, default);
3. the edit changes what the plugin outputs.

The gate is generic: it uses only the VST3 interfaces every plugin implements (`IEditController`, `IComponent`,
`IAudioProcessor`, `IComponentHandler`) plus the plugin's `editor.uidesc`. A plugin joins it with one small test file and
one exceptions file.

## Why (the bugs it targets)

The Ruinae audit (2026-11-16) and the 8-plugin load-path check (2026-11-22) found parameters that never reached the
processor, reached it in the wrong format, or reached it and did nothing. The checks below are numbered as in
"Architecture". The classification comes from reading the findings, not from running the gate.

| Finding | Defect | Caught by |
|---|---|---|
| R-185, R-186, R-188 | registered default ≠ processor default | Check 3 |
| Gradus lane speed (found by T2.34) | stepped default passed as normalized (0.25x instead of 1x) | Check 3 |
| Disrumpo Rungler (found by T1.38) | processor default outside the parameter's range | Check 3 |
| R-184, R-097 | controller maps the knob with a different curve than the processor | Check 3 |
| R-201 (Innexus) | controller parser skips a field, so the knob shows the wrong value after a load | Check 3 |
| R-121 | one parameter overwrites the field another parameter owns | Check 2 (if the field is in the state) or Check 4 |
| R-041, R-183 | LFO / Chaos Depth stored but never applied | Check 4 |
| R-140 | trance-gate rate overwritten by a constant | Check 4 |
| R-101 | Gain Compensation control does nothing | Check 4 |
| R-142 | Dice trigger lost (on/off pair in one block) | Check 4 (trigger recipe) |
| R-207 (Gradus) | speed-curve depth never applied after a host load | Check 4 |
| R-018 | velocity never reaches the VCA (not a parameter: the gate does not cover it) | — |
| R-039, R-208, R-043, R-135 | wrong destination, swapped lanes, wrong labels, per-block vs per-sample | out of reach (needs meaning) |

## Architecture

```
tests/test_helpers/param_path_gate.h        shared helper (header-only, namespace Krate::TestSupport)
tests/test_helpers/param_path_gate_test.cpp self-tests of the helper against a tiny fake plugin
plugins/<p>/tests/unit/param_path_gate_test.cpp   per-plugin test, ~20 lines
plugins/<p>/tests/param_path_exceptions.json      per-plugin exceptions, each with a reason
plugins/<p>/tests/param_path_witnesses.json       Stage 2 output: one witness preset (and recipe) per parameter
tools/param-path-witness.js                 Stage 2 driver: runs the nightly witness discovery, writes the JSON
tools/lint-param-path-gate.js               Stage 4 lint: every plugin runs the gate
```

Four checks. Each check is a function in the helper that returns a list of human-readable findings. The test
REQUIREs every list to be empty after removing the exceptions.

| Check | Name | Needs | Runs |
|---|---|---|---|
| 1 | Control → host | the editor, opened headless | per push (Stage 3) |
| 2 | Host → processor | processor only | per push (Stage 1) |
| 3 | Processor ↔ controller | processor + controller | per push (Stage 1) |
| 4 | Effect | processor + factory presets + rendering | nightly discovery, per-push spot check (Stage 2) |

**The parameter list.** The gate enumerates the controller's registered parameters (`getParameterCount` /
`getParameterInfo`). It skips `kIsReadOnly` and `kIsProgramChange` parameters, plus any parameter that is `kIsHidden`
and not `kCanAutomate`: UI-only view switches, as pinned by Ruinae's
`"UI-only view parameters are hidden and not automatable"`. Hidden but automatable parameters, such as MIDI mappings,
stay in (ruling 2). Stage 3 cross-checks this list against the uidesc and the live editor.

**Core idea of Checks 2 and 3.** The processor's `getState` is the plugin's own record of every value it holds. Check 2
asks whether a parameter edit changes that record at all, and whether it changes only that parameter's part of it.
Check 3 hands the record to a fresh controller. If the controller reads the value back as the one that was sent, the
processor (normalized → plain) and the controller (plain → normalized) use the same mapping. That covers range, curve,
steps, clamps and the field position in the stream, all without decoding the plugin-specific state format.

---

## Stage 1 — Checks 2 and 3 (processor + controller, no audio, no editor)

### PG-1 — The helper skeleton and the fake plugin (red first)

**Files:** `tests/test_helpers/param_path_gate.h`, `tests/test_helpers/param_path_gate_test.cpp`,
`tests/test_helpers/CMakeLists.txt` (add the test TU to the existing helper test target; follow how
`spectral_analysis_test.cpp` is built).
**Estimate:** 120 min.

1. **Fake plugin, inside the test TU.** `FakeProcessor : AudioEffect` and `FakeController : EditController` with five
   parameters:
   - P1 is correct.
   - P2 is never handled in `processParameterChanges`.
   - P3 is stored with a different curve than the controller's parameter maps it (processor `x²`, controller linear).
   - P4 writes P1's field as well as its own.
   - P5 has a registered default different from the processor's start value.

   `getState` writes five floats; `setComponentState` reads them back into the parameters.
2. **Failing tests** (`[param-path-gate]`):
   - `"Gate reports a parameter the processor never stores"` → P2 only.
   - `"Gate reports a parameter read back in a different format"` → P3 only.
   - `"Gate reports a parameter that moves another parameter's value"` → P4 only.
   - `"Gate reports a registered default the processor does not start at"` → P5 only.
   - `"Gate passes a correct parameter"` → P1 not reported.

   Each REQUIRE names the expected finding line. Run them on the empty helper and see them fail.
3. **API** (the shape; names may change, the behaviour may not):

```cpp
namespace Krate::TestSupport {

struct ParamPathPlugin {
    // Factories so the gate can make fresh instances; the gate initialises, sets up (48 kHz, 512, kRealtime,
    // kSample32), activates and tears down every instance itself.
    std::function<Steinberg::Vst::IComponent*()> makeProcessor;          // returns an AddRef'd processor
    std::function<Steinberg::Vst::EditController*()> makeController;     // returns an AddRef'd controller
    int numInputs = 0, numOutputs = 2;                                     // audio buses for process()
    std::size_t stateTailBytesToIgnore = 0;   // e.g. Disrumpo's 12-byte instance-ID block
};

struct ParamPathOptions {
    std::vector<double> sweep = {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0}; // continuous params
    double continuousReadbackTolerance = 1e-4;  // normalized; stepped params must read back exactly the step
    std::set<Steinberg::Vst::ParamID> exempt;   // filled from the exceptions file by the caller
};

struct ParamPathReport {
    std::vector<std::string> notStored;      // Check 2a
    std::vector<std::string> collateral;     // Check 2b
    std::vector<std::string> deadTravel;     // Check 2c
    std::vector<std::string> formatMismatch; // Check 3a
    std::vector<std::string> defaultMismatch;// Check 3b
    std::vector<std::string> readbackMoves;  // Check 3c
};

ParamPathReport runParamPathChecks(const ParamPathPlugin&, const ParamPathOptions& = {});
std::set<Steinberg::Vst::ParamID> loadParamPathExceptions(const std::string& jsonPath,
                                                          std::vector<std::string>& staleOrInvalid);
} // namespace Krate::TestSupport
```

4. **Acceptance:** the five cases pass; the helper compiles with zero warnings under MSVC and clang
   (`tools/check-portability.js`), and has no platform-specific code.

### PG-2 — Check 2: host → processor

**Estimate:** 150 min. **Prerequisite:** PG-1.

For every parameter `id` in the list:

1. Make a fresh processor, activate it, run one silent block, and take `base = getState()` (drop
   `stateTailBytesToIgnore`).
2. For each sweep value `v`, on a fresh processor, deliver `id = v` as a one-point `IParamValueQueue` at sample offset
   0 (`tests/test_helpers/vst_param_changes.h`) in one 512-sample `process()`, then take `s(v) = getState()`.
   - Stepped parameters (`stepCount > 0`) sweep every step instead: `k / stepCount` for `k = 0..stepCount`.
     For a step count above 64, sweep 0, 1, 2, the middle step, the last two steps and the last step.
3. **2a not stored:** every `s(v)` equals `base` → `"<id> <title>: no sweep value changes the processor state"`.
4. **2b collateral.** Build an ownership map first, from the byte ranges each parameter's sweep changes. Then, if
   parameter A's sweep changes bytes that only parameter B's sweep changes, report
   `"<A> writes <B>'s state field at byte <n>"`.
   - A state field can be shared on purpose: a stepped mode and a value it derives, or Disrumpo's Folds slot. Those
     go in the exceptions file with a reason.
5. **2c dead travel:** for a continuous parameter, adjacent sweep values that give identical states over more than
   25 % of the sweep → `"<id>: values <a>..<b> store the same state"`. This catches a range wider than the
   processor's clamp, as in R-097's dead 27 %.
6. Fresh processors per value cost little: Ruinae's 890 parameters × 11 values is about 10,000 instances, each built
   once with one 512-sample block. Estimate before you build: time one Ruinae instance, then multiply. If the total
   passes 60 s, split the case into shards by parameter-ID range (`SECTION`s or several `TEST_CASE`s) so
   `tools/run-gate.js` spreads it. Never make it serial; never tag it `[.]`.

**Failing test first:** the PG-1 fake-plugin cases for P2 and P4 go green here.

### PG-3 — Check 3: processor ↔ controller

**Estimate:** 120 min. **Prerequisite:** PG-2.

For every parameter `id` and every sweep value `v` from PG-2:

1. Hand `s(v)` to a fresh controller's `setComponentState`.
2. **3a format mismatch:** `getParamNormalized(id)` must equal `v` (stepped parameters: the same step, exactly;
   continuous: within `continuousReadbackTolerance`). Otherwise
   `"<id> <title>: sent <v>, controller reads <r> (plain <toPlain(v)> vs <toPlain(r)>)"`.
   The plain values in the message show what the mismatch is: a range, a curve or a step count.
3. **3c readback moves:** every other parameter must read back what it reads after `setComponentState(base)`.
   Otherwise `"<id> = <v> moves <other> from <a> to <b> in the controller"`. This is the controller-side twin of 2b, and
   it catches a controller parser that is off by one field.
4. **3b default mismatch:** `setComponentState(base)` on a fresh controller; every parameter must read back its
   `defaultNormalizedValue` (tolerance as in 3a). This generalises Ruinae's
   `"Every registered default denormalises to the processor default"` and the shared
   `tests/test_helpers/registered_defaults_probe.h`, which compares only against the controller's own start value.

**Failing test first:** the PG-1 cases for P3 and P5 go green here.

### PG-4 — Exceptions file format and the stale-entry rule

**Estimate:** 45 min.

`plugins/<p>/tests/param_path_exceptions.json`:

```json
{
  "notStored":   [{ "id": 3401, "reason": "Dice trigger: momentary, never persisted; Check 4 covers it" }],
  "collateral":  [{ "id": 812, "other": 813, "reason": "Folds is derived from the shape slot (D-050)" }],
  "deadTravel":  [],
  "formatMismatch": [],
  "defaultMismatch": [],
  "readbackMoves": []
}
```

Rules:
- Every entry needs a non-empty reason.
- **Stale entries fail.** An entry whose finding no longer occurs is reported as a defect ("stale exception"), as with
  `tools/uidesc-inventory.js --allow`. The file can only shrink unless someone adds a reasoned entry.
- No wildcard and no ID range.

### PG-5 — Roll Checks 2-3 out to every plugin

**Estimate:** 30-90 min per plugin, plus triage. **Prerequisite:** PG-1 to PG-4.

Per plugin, `plugins/<p>/tests/unit/param_path_gate_test.cpp`:

```cpp
TEST_CASE("Every parameter reaches the processor and reads back in the same format", "[<p>][param-path]") {
    Krate::TestSupport::ParamPathPlugin plugin{ /* factories for <P>::Processor and <P>::Controller */ };
    std::vector<std::string> stale;
    Krate::TestSupport::ParamPathOptions opts;
    opts.exempt = Krate::TestSupport::loadParamPathExceptions(<P>_TESTS_DIR "/param_path_exceptions.json", stale);
    const auto report = Krate::TestSupport::runParamPathChecks(plugin, opts);
    // one CHECK per list, each printing its lines, plus REQUIRE(stale.empty())
}
```

Order, with what each plugin needs:

1. **Ruinae** (most parameters; already fixed by the audit, so mostly a confirmation). Expect findings in the arp
   block (IDs 3000-3372, shared with Gradus).
2. **Gradus.** It shares the arp IDs. Its lane-speed default bug must be fixed by then (queued for the phase 3
   commit); if it is not, the gate must report it, which is the red evidence.
3. **Innexus.** The state holds a sample path; set none.
4. **Disrumpo.** `stateTailBytesToIgnore = 12` (the instance-ID block, T1.38). Shape-shadow slots will show
   collateral by design: exceptions with reasons.
5. **Iterum.** Also run `approval_tests` in the gate (root CLAUDE.md).
6. **Membrum.** Per-pad parameters number in the thousands. Sweep only 0, 0.5 and 1 for continuous per-pad
   parameters, through `ParamPathOptions`, and keep every pad in the list. The selected-pad parameter
   (FR-052) is UI state; if it is not hidden, add a reasoned exception.
7. **Seraphis, Vorago, Profundum.**

**Each finding is triaged, never silenced.** For each one, write a fix or a reasoned exception. A real defect gets a
row in a findings table in `specs/param-path-gate/findings.md` (`PG-F-nnn`, plugin, parameter, check, evidence, fix
commit). A fix in a plugin follows the repo loop (red test, fix, build, sharded gate, pluginval for that plugin,
clang-tidy for that plugin). Fixes in shared code are in scope (user ruling 2026-11-17); gate every consumer.

**Historic replay, the acceptance evidence for Stage 1.** In a worktree at the pre-audit commit `f8ed81e1`, add the
helper and the Ruinae test and run it once. The report must contain R-184, R-185, R-186, R-188 and R-097. Write the
lines to `specs/param-path-gate/evidence/ruinae-replay-f8ed81e1.txt`. Do the same for Gradus at the commit before the
lane-speed fix. If the replay misses one of these, the helper is wrong: fix it before rolling out further.

---

## Stage 2 — Check 4: effect (does the parameter change the output?)

### PG-6 — Witness discovery (nightly)

**Estimate:** 240 min. **Prerequisite:** Stage 1 green on all plugins.

A **witness** for parameter `id` is a factory preset (plus, optionally, a recipe) in which moving `id` from its preset
value to a far value changes the plugin's output.

1. **Render rig, in the helper:** load a factory `.vstpreset` into a fresh processor with
   `Steinberg::Vst::PresetFile::loadPreset` (`public.sdk/source/vst/vstpresetfile.h`), then render a fixed scenario:
   - Instruments: a held chord (60/64/67, velocity 100) for 1.0 s, released, 0.5 s tail, transport playing at
     120 BPM, `kRealtime`.
   - Effects (Iterum, Disrumpo): a fixed 1 s noise burst plus 1 s silence on the input.
   - MIDI-out plugins (Gradus): compare the output event list instead of audio.
2. **Metric:** render A (preset as is) and B (`id` set to the far value: 1 - v rounded to its step, or v ± 0.5 for
   v near the middle).
   - Audio: the parameter has an effect if `rms(B - A) / max(rms(A), 1e-6) > 1e-3` (-60 dB).
   - Events: the parameter has an effect if the event lists differ (count, order, pitch, velocity, or timing by more
     than 1 sample).
   - Determinism precondition: render A twice and require the two to be identical (Ruinae has this after T2.31 and
     T3.1). If a plugin is not deterministic, the threshold becomes 10 × the A-vs-A difference. Record that in its
     witness file.
3. **Search:** for each parameter, try the factory presets in a fixed order (sorted path) and stop at the first
   witness. No witness among the factory presets → try the recipes (PG-7). Still none → report
   `"<id> <title>: no witness"`.
4. **Driver:** `tools/param-path-witness.js <plugin>` runs a hidden `[.][param-path][witness]` case per shard through
   `tools/run-gate.js` (cpu-count pool, `--share` when other lanes run) and merges the shard outputs into
   `plugins/<p>/tests/param_path_witnesses.json` (`{id: {preset, recipe?, far, delta}}`).
5. **Cost:** measure one render first. A Ruinae render is about 1.5 s of audio; parameters × presets in the worst case
   is 890 × 168. The search usually stops at the first preset, so measure the real total on one shard before running
   them all, and record it in this plan.

### PG-7 — Recipes for parameters no preset exercises

**Estimate:** 120 min, plus triage.

A recipe is a short list of parameter settings applied before the A/B render: put the plugin into the mode where `id`
acts, for example an arp mode, a filter type, or a trigger parameter sent as an on/off pair in one block (R-142's
case). Recipes live in the witness JSON next to the parameter:

```json
"3401": { "preset": "Arp Classic/Up_Down.vstpreset",
          "recipe": [{"id": 3000, "value": 1.0}], "trigger": true, "far": 1.0, "delta": 0.31 }
```

A parameter with neither a witness nor a recipe goes into `param_path_exceptions.json` as `"noEffect"` with a reason,
or is a defect (dead control, as with R-101) and gets a fix.

### PG-8 — Per-push effect check and the nightly lane

**Estimate:** 90 min.

1. **Per-push:** `"Every parameter changes the output in its witness preset", "[<p>][param-path][effect]"` renders each
   parameter once, in its stored witness preset and recipe only, and requires `delta > threshold`. Split it into
   shards so each case stays under 15 s (per-push `[wallclock]` rule, root CLAUDE.md).
2. **Nightly:** add the witness discovery to `.github/workflows/long-tests-nightly.yml` as `[long]` cases (Linux, sharded,
   timings in `tools/ci-test-times.json`). It fails when a parameter that had a witness loses it, and it uploads a
   refreshed witness JSON as an artifact when a preset changes. Never commit from CI.
3. **Acceptance:**
   - Historic replay at `f8ed81e1` for Ruinae lists R-041 (LFO 1/2 Depth), R-183 (Chaos Depth), R-140
     (trance-gate rate) and R-101 (Gain Compensation) as "no witness".
   - R-142 (Dice) fails its trigger recipe.
   - Write the lines to `specs/param-path-gate/evidence/ruinae-effect-replay-f8ed81e1.txt`.

---

## Stage 3 — Check 1: control → host (the editor walk)

### PG-9 — Tag-bound controls

**Estimate:** 180 min. **Prerequisite:** Stage 1.

1. Open the real editor headless as `tests/test_helpers/editor_lifecycle_harness.h` does (`ensureVstguiInitialized`,
   `VST3Editor(&controller, "editor", <P>_RESOURCES_DIR "/editor.uidesc")`, `attached(nullptr, nativePlatformType())`).
   Install a recording `IComponentHandler` (the plugins' tests already define several; move one into the helper).
2. **Visit every template, including inactive ones.** A `UIViewSwitchContainer` only realises its active template, so
   for each switch control in the uidesc (from `tools/uidesc-inventory.js`'s inventory), step the controlling
   parameter through its values with `setParamNormalized` and walk the view tree after each step.
3. For each `CControl` with a tag, call `beginEdit()`, `setValueNormalized(x)`, `valueChanged()`, then `endEdit()`.
   Use `x = 0.25` and then `0.75`; for menus and segment buttons use each entry. Record the edits.
4. **Findings:**
   - **1a** a control whose tag is not a registered parameter.
   - **1b** a control edit that produces no `performEdit`.
   - **1c** `performEdit` on a different ID than the control's tag.
   - **1d** an unbalanced `beginEdit` / `endEdit`.
   - **1e** the value that reaches the host differs from the control's value. Stepped controls must deliver the step.
5. Cross-check with the uidesc: every control-tag in the uidesc must have been visited at least once. Report the ones
   never realised, with their template name.

### PG-10 — Custom views

**Estimate:** 240 min, highest flake risk; do last.

1. For every view whose class is not a standard VSTGUI control, typically a `custom-view-name` view, send synthetic
   mouse gestures through the view's own handlers (`onMouseDown` / `onMouseMoved` / `onMouseUp`, with `CButtonState`
   kLButton) at a grid of points: 8 × 4 across the view's size. Use a click, then a short vertical drag. Record every
   parameter the gestures edit.
2. Any parameter that is in the Stage 1 list but never edited by a tag-bound control (PG-9) or a custom view → finding
   `"<id> <title>: no control edits it"`, the controller-side twin of 2a. This replaces the allowlists in
   `tests/test_helpers/uidesc_reachability.h` for the plugins that use it; once the gate covers them, remove those
   tests in the same change.
3. A view that crashes, asserts or hangs under synthetic input is a finding in its own right (run under ASan in the
   sanitizer nightly).
4. If a custom view cannot be driven reliably, record it with a reason in the exceptions file under `"undrivenViews"`
   and list the parameters only that view edits. Those parameters then need an explicit `"editedBy"` entry naming the
   view, so the list stays checked.

---

## Stage 4 — Enforcement

### PG-11 — CI lint and docs

**Estimate:** 60 min.

1. `tools/lint-param-path-gate.js` (Node; `--self-test`): every directory under `plugins/` except `shared` has
   `tests/unit/param_path_gate_test.cpp` calling `runParamPathChecks`, and a `param_path_exceptions.json` that parses
   and has a reason on every entry. Model it on `tools/lint-plugin-editor-baseline.js` and add it to the same commit
   gate and CI job.
2. Root `CLAUDE.md`: add a short "Every parameter passes the parameter-path gate" rule next to "Every Plugin Editor
   Carries the Update Check". Say what the gate checks, where the exceptions live, and that the lint enforces it.
   Add a line to each plugin's leaf `CLAUDE.md` test section.
3. The scaffold for a new plugin, wherever plugins are scaffolded from (find it with Grep for the update-config copy
   step), includes the test file and an empty exceptions file.

---

## Workflow every task follows

The repo rules apply (root `CLAUDE.md` and memory). The ones that matter most here:

1. Red test first. For the helper: the fake-plugin cases. For a plugin: the gate's first run, or the historic replay.
2. Build with the cap: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <targets> -- -m:2 /nr:false`. Zero warnings.
3. Every test run goes through `node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]' <exes>`.
   - Gate scope: the plugins whose files changed, plus every executable `node tools/header-consumers.js` names for a
     changed shared header.
   - `param_path_gate.h` is a new test helper, so it has no consumers outside the tests that include it.
4. clang-tidy per touched target (`./tools/run-clang-tidy.ps1 -Target <t> -BuildDir build/windows-ninja -Jobs 4`).
   Reconfigure the ninja preset first when test CMake files changed.
5. pluginval only for a plugin whose sources changed, i.e. when a finding is fixed. Never for test-only changes.
6. One heavy job at a time; never stop or restart WSL; never push; commit with `git commit -F`.

## Tasks and estimates

| Task | Stage | What | Estimate |
|---|---|---|---|
| PG-1 | 1 | Helper skeleton, fake plugin, red self-tests | 120 min |
| PG-2 | 1 | Check 2 (not stored, collateral, dead travel) | 150 min |
| PG-3 | 1 | Check 3 (format, defaults, readback moves) | 120 min |
| PG-4 | 1 | Exceptions file + stale rule | 45 min |
| PG-5 | 1 | Roll out to 9 plugins, historic replay, triage | 6-10 h incl. fixes |
| PG-6 | 2 | Witness discovery + render rig + driver | 240 min |
| PG-7 | 2 | Recipes + triage of "no witness" | 120 min + triage |
| PG-8 | 2 | Per-push effect check, nightly lane, replay | 90 min |
| PG-9 | 3 | Editor walk, tag-bound controls | 180 min |
| PG-10 | 3 | Custom views | 240 min |
| PG-11 | 4 | Lint, CLAUDE.md, scaffold | 60 min |

**Order:** Stage 1 → Stage 4's lint (so no plugin drops out) → Stage 2 → Stage 3. Commit once per task group:
PG-1 to PG-4, PG-5 per plugin or in pairs, PG-6 to PG-8, PG-9 to PG-10, PG-11.

## Decisions (user, 2026-11-22; final, do not re-ask)

| # | Question | Ruling |
|---|---|---|
| 1 | Tolerances | **As proposed.** Continuous readback 1e-4 normalized (stepped: the exact step); effect threshold -60 dB relative RMS (`rms(B - A) / max(rms(A), 1e-6) > 1e-3`); dead travel when more than 25 % of the sweep stores the same state. Never loosen these to get green; a finding gets a fix or a reasoned exception. |
| 2 | Hidden but automatable parameters (MIDI CC mappings such as Ruinae's pitch bend / aftertouch / mod wheel) | **Included.** They are in the parameter list for every check. Momentary or non-persisted ones get a reasoned `notStored` exception for Checks 2-3 and must have a witness or recipe in Check 4. |
| 3 | Stage 3 custom views (PG-10) | **Build it, last.** PG-10 runs after every other task. It replaces the allowlists in `tests/test_helpers/uidesc_reachability.h` and Membrum's bespoke scanner once it covers them; a view that cannot be driven reliably gets an `undrivenViews` exception with reason and explicit `editedBy` entries. |
| 4 | Branch | **`feat/param-path-gate`**, created from `main` after `audit/ruinae` is merged. |

## Out of scope

- Whether a parameter does the **right** thing (a cutoff knob that changes resonance passes). Behaviour tests per
  plugin cover that.
- Non-parameter state (voice routes, speed-curve tables, shape shadows, samples). The phase 1b test
  "Browser load leaves the processor in the same state as a host load" covers it in every plugin.
- MIDI input paths that are not parameters (note velocity, MPE). Per-plugin tests cover them.
