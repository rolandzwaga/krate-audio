# Research: Additional Modulation Sources

**Spec**: 059-additional-mod-sources | **Date**: 2026-02-16

## Research Summary

All unknowns from the Technical Context have been resolved through codebase investigation. No external library research was needed -- this spec is pure plugin-layer wiring of existing DSP implementations.

---

## Research Task 1: Current State Version

**Decision**: Bump `kCurrentStateVersion` from 14 to 15.

**Rationale**: Verified in `plugins/ruinae/src/processor/processor.h:71` that the current state version is 14 (added by Spec 058 - Settings Drawer). New mod source parameters will be appended as v15 data.

**Alternatives considered**: None -- sequential versioning is the established pattern.

---

## Research Task 2: S&H Tempo Sync Architecture

**Decision**: Handle S&H tempo sync entirely at the processor level by converting NoteValue + BPM to Hz.

**Rationale**: The `SampleHoldSource` DSP class (`dsp/include/krate/dsp/processors/sample_hold_source.h`) has NO `setTempoSync()` or `setTempo()` methods -- only `setRate(float hz)`, `setSlewTime(float ms)`, and `setInputType()`. Unlike `RandomSource` which has built-in tempo sync, S&H requires the plugin layer to compute the rate. The conversion is: `rateHz = 1000.0f / dropdownToDelayMs(noteIdx, tempoBPM)`. The `dropdownToDelayMs()` function from `dsp/core/note_value.h:240` handles the NoteValue-to-ms conversion. This is the same approach used for delay sync.

**Alternatives considered**:
1. Add tempo sync to SampleHoldSource DSP class -- rejected because it requires DSP changes outside this spec's scope and the plugin-level approach works identically.
2. Use a separate rate variable for synced mode -- rejected because the DSP class only has one rate input.

---

## Research Task 3: Random Tempo Sync Architecture

**Decision**: Use plugin-level NoteValue-to-Hz conversion for Random sync (same as S&H), ignoring the RandomSource built-in sync.

**Rationale**: The `RandomSource` class has `setTempoSync(bool)` and `setTempo(float bpm)`, but these internally just compute a rate from BPM using a fixed formula. Since we want NoteValue-based sync (1/4, 1/8, etc.) matching the UX of Chaos and S&H, we bypass the built-in sync and compute the Hz directly at the plugin level. We set `setRandomTempoSync(false)` always and convert NoteValue + BPM to Hz when Sync is on, then call `setRandomRate(hz)`.

**Alternatives considered**:
1. Use RandomSource built-in sync -- rejected because it doesn't support NoteValue selection, only a fixed BPM-to-rate formula. Users expect to pick note values from a dropdown, not get a fixed subdivision.
2. Add NoteValue support to RandomSource DSP class -- rejected as unnecessary DSP changes.

---

## Research Task 4: Parameter Mapping Functions

**Decision**: Reuse `lfoRateFromNormalized()` / `lfoRateToNormalized()` for S&H and Random Rate. Create new log mappings for Env Follower Attack/Release and Pitch Follower MinHz/MaxHz. Use linear mappings for Transient Attack/Decay, Pitch Follower Speed, S&H Slew.

**Rationale**: The LFO rate mapping (`0.01 * pow(5000, normalized)` -> [0.01, 50] Hz) is already shared between LFO1, LFO2, and Chaos. The spec calls for S&H and Random rates in [0.1, 50] Hz, which fits within this range. The lower bound mismatch (0.01 vs 0.1) is handled by clamping in the handle function.

For Env Follower:
- Attack [0.1, 500] ms: `0.1 * pow(5000, norm)` maps [0,1] to [0.1, 500]
- Release [1, 5000] ms: `1.0 * pow(5000, norm)` maps [0,1] to [1, 5000]

For Pitch Follower:
- MinHz [20, 500]: `20 * pow(25, norm)` maps [0,1] to [20, 500]
- MaxHz [200, 5000]: `200 * pow(25, norm)` maps [0,1] to [200, 5000]

Linear mappings (simpler, for narrow or perceptually uniform ranges):
- Transient Attack [0.5, 10] ms: `0.5 + norm * 9.5`
- Transient Decay [20, 200] ms: `20 + norm * 180`
- Pitch Follower Speed [10, 300] ms: `10 + norm * 290`
- S&H Slew [0, 500] ms: `norm * 500`

**Alternatives considered**: Using RangeParameter for all ranges -- rejected because the project convention uses inline mapping functions in param files, not RangeParameter instances.

---

## Research Task 5: Visibility Switching Pattern for Sync Controls

**Decision**: Reuse the exact `custom-view-name` group pattern from the Chaos template (`ChaosRateGroup`/`ChaosNoteValueGroup`) for S&H and Random.

**Rationale**: The Chaos template at `editor.uidesc:1698-1749` demonstrates the pattern:
1. A `CViewContainer` with `custom-view-name="ChaosRateGroup"` wraps the Rate knob (visible by default)
2. A `CViewContainer` with `custom-view-name="ChaosNoteValueGroup"` wraps the NoteValue dropdown (hidden by default: `visible="false"`)
3. The controller's sub-controller/delegate toggles visibility when `ChaosSync` changes

S&H uses `SHRateGroup`/`SHNoteValueGroup`, Random uses `RandomRateGroup`/`RandomNoteValueGroup`.

**Alternatives considered**: Using UIViewSwitchContainer for rate/notevalue switching -- rejected as overkill for a simple two-state toggle. The custom-view-name approach is simpler and already proven.

---

## Research Task 6: RuinaeEngine Forwarding Pattern

**Decision**: Add 18 forwarding methods to RuinaeEngine, following the exact pattern of the existing Rungler forwarding methods.

**Rationale**: The processor interacts with `RuinaeEngine`, not directly with `ModulationEngine`. The forwarding pattern is established at `ruinae_engine.h:421-426` (Rungler) and `ruinae_engine.h:416-418` (Macros). Each forwarding method is a one-liner that calls the corresponding `globalModEngine_` method.

For S&H and Random, only the "raw" setter methods are forwarded (rate, slew, smoothness). The Sync/NoteValue logic is handled entirely in `applyParamsToEngine()` at the processor level, which computes the final Hz and calls the rate setter.

**Alternatives considered**: Putting sync logic in RuinaeEngine -- rejected because the processor owns `tempoBPM_` and the param structs, making it the natural place for tempo-to-rate conversion.

---

## Research Task 7: Default Normalized Values for UI

Computed default normalized values for uidesc `default-value` attributes:

| Parameter | Default Plain | Formula | Normalized |
|-----------|---------------|---------|------------|
| EnvFollower Sensitivity | 0.5 | direct | 0.5 |
| EnvFollower Attack | 10 ms | `log(10/0.1)/log(5000)` | 0.5406 |
| EnvFollower Release | 100 ms | `log(100/1)/log(5000)` | 0.5406 |
| S&H Rate | 4 Hz | `log(4/0.01)/log(5000)` | 0.702 |
| S&H Slew | 0 ms | `0/500` | 0.0 |
| Random Rate | 4 Hz | `log(4/0.01)/log(5000)` | 0.702 |
| Random Smoothness | 0 | direct | 0.0 |
| PitchFollower MinHz | 80 Hz | `log(80/20)/log(25)` | 0.4307 |
| PitchFollower MaxHz | 2000 Hz | `log(2000/200)/log(25)` | 0.7153 |
| PitchFollower Confidence | 0.5 | direct | 0.5 |
| PitchFollower Speed | 50 ms | `(50-10)/290` | 0.1379 |
| Transient Sensitivity | 0.5 | direct | 0.5 |
| Transient Attack | 2 ms | `(2-0.5)/9.5` | 0.1579 |
| Transient Decay | 50 ms | `(50-20)/180` | 0.1667 |

---

## Research Task 8: No ModSource Enum Changes Needed

**Decision**: No changes to the `ModSource` enum, `kModSourceCount`, `kModSourceStrings`, or `kNumGlobalSources`.

**Rationale**: All 5 sources are already present in the ModSource enum (`EnvFollower=3`, `Random=4`, `SampleHold=11`, `PitchFollower=12`, `Transient=13`). The `kModSourceStrings` dropdown already includes all 5 display names. The `kModSourceCount` is already 14. No enum renumbering is needed (unlike Spec 057 which had to insert Rungler). This simplifies backward compatibility -- no preset migration for source indices is required.

---

## Research Task 9: No Preset Migration Needed

**Decision**: No mod source index migration needed for old presets.

**Rationale**: Unlike Spec 057 which inserted `Rungler` at position 10 and had to renumber `SampleHold`, `PitchFollower`, and `Transient`, this spec adds no new enum values. All ModSource enum values are already in their final positions. Old presets (version < 15) simply get default parameter values for the 5 new sources. The mod matrix source indices are unchanged.
