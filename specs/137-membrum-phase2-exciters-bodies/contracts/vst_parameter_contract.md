# VST3 Parameter Contract (Phase 2)

**Applies to:** `Membrum::Processor` (audio thread state) and `Membrum::Controller` (UI/host parameter registration).

## New parameter IDs (plugin_ids.h)

Phase 2 extends the Phase 1 parameter set (100..104) with IDs in range 200..299. All new parameters follow `CLAUDE.md` naming: `k{Section}{Parameter}Id`.

See `data-model.md` §9 for the full enum. Summary:

| Range | Section | Count |
|-------|---------|-------|
| 200–209 | Exciter + Body selectors + secondary exciter params | ~6 |
| 210–229 | Tone Shaper (filter, drive, fold, pitch env) | ~14 |
| 230–239 | Unnatural Zone scalar params | ~4 |
| 240–249 | Material Morph | ~5 |

Total new host-visible parameters: **~29** (including filter envelope ADSR).

## Parameter type selection (per VST3 guidance)

| Parameter | Type | Rationale |
|-----------|------|-----------|
| Exciter Type | `StringListParameter` (6 choices) | Discrete integer list with names visible in host |
| Body Model | `StringListParameter` (6 choices) | Same |
| Filter Type | `StringListParameter` (LP/HP/BP) | Discrete |
| Filter Cutoff | `RangeParameter` (20–20000 Hz, exp taper) | Continuous log |
| Filter Env Amount | `RangeParameter` (-1..+1) | Continuous, bidirectional |
| Drive/Fold Amount | `RangeParameter` (0..1) | Continuous linear |
| Pitch Env Start/End | `RangeParameter` (20–2000 Hz, exp taper) | Continuous log |
| Pitch Env Time | `RangeParameter` (0..500 ms, linear) | Continuous |
| Mode Stretch | `RangeParameter` (0.5..2.0) | Continuous |
| Decay Skew | `RangeParameter` (-1..+1) | Continuous bidirectional |
| Mode Inject / Nonlinear Coupling | `RangeParameter` (0..1) | Continuous |
| Morph Enabled | `Parameter` (0/1 toggle) | Discrete |
| Morph Start/End | `RangeParameter` (0..1) | Continuous |
| Morph Duration | `RangeParameter` (10..2000 ms) | Continuous linear |

See `vst-guide` skill for `StringListParameter` vs `Parameter` vs `RangeParameter` — picking `RangeParameter` for all continuous non-0-1 values ensures `toPlain()` returns the correct scaled value.

## Value flow contract (Host → Processor → Controller)

### Host → Processor
1. Host sets a parameter via `IParameterChanges`.
2. `Processor::processParameterChanges()` dequeues the change, denormalizes via the parameter's known range, and writes the denormalized value to a `std::atomic<float>` (or `std::atomic<int>` for discrete selectors).
3. The audio thread reads the atomic at the start of each process call (or each sample for smoothed parameters) and applies it to the DrumVoice.

### Host → Controller
1. Host sets a parameter via the controller's `setParamNormalized()`.
2. The controller stores the value internally and notifies any VSTGUI bindings.
3. **Controller MUST NOT directly modify audio DSP state** — all audio changes flow through the Processor's parameter change queue.

### State save/load (FR-082, FR-094, SC-006)

**Save flow (`getState(IBStream*)`):**
1. Write `kCurrentStateVersion = 2` (int32).
2. Write the 5 Phase 1 parameters (float64 each, in the order they were stored in Phase 1).
3. Write the 2 integer selectors (Exciter Type, Body Model) as int32 each.
4. Write the remaining Phase 2 parameters (float64 each, in parameter-ID order).

**Load flow (`setState(IBStream*)`):**
1. Read version (int32).
2. Read 5 Phase 1 parameters.
3. If `version >= 2`: read 2 int selectors + Phase 2 parameters.
4. If `version == 1`: fill Phase 2 parameters with defaults (Exciter Type = Impulse, Body Model = Membrane, Tone Shaper bypassed, Unnatural at defaults).
5. Return `kResultOk` on success; `kResultFalse` on corruption/truncation.

**Backward compatibility guarantee (FR-082):**
- A Phase-1 state file loaded into Phase 2 MUST produce a voice that sounds the same as the Phase 1 default patch (Impulse + Membrane with Phase 1 parameter values, all Phase 2 parameters at defaults = bypass).

### Round-trip test (SC-006)
- For every combination of parameter values tested in US1..US7, save and reload state; assert bit-identical normalized values across all ~34 parameters (5 Phase 1 + 29 Phase 2).

## Real-time safety invariants (FR-072, SC-011)

- `Processor::processParameterChanges()` runs on the audio thread. It reads from the `IParameterChanges` queue (host-owned) and writes to plugin-owned atomics. No allocation, no exception, no lock.
- Parameter smoothing happens in the audio thread via `OnePoleSmoother` for any parameter where zipper noise is audible.
- Discrete parameters (Exciter Type, Body Model, Filter Type, Morph Enabled, Curve selectors) are **stored as `std::atomic<int>`** and read as a snapshot at the start of each process call.
- Continuous parameters are `std::atomic<float>` (lock-free on all target platforms — verified via `std::atomic<float>::is_lock_free()` in a static assert at startup).

## Host-generic editor (FR-083)

Phase 2 does NOT add a custom `editor.uidesc`. All parameters are exposed to the host via the default host-generic editor. Custom UI is Phase 5.

The controller's `createView()` method returns `nullptr` for `ViewType::kEditor` (same as Phase 1).

## Pluginval / auval compatibility (FR-096, FR-097, SC-010)

- `pluginval --strictness-level 5 --validate Membrum.vst3` MUST pass on Windows with zero errors and zero warnings.
- `auval -v aumu Mbrm KrAt` MUST pass on macOS.
- AU config files (`au-info.plist`, `auv3/audiounitconfig.h`) remain unchanged from Phase 1 (no bus configuration changes — still 0 in / 2 out instrument).

## Test coverage requirements

1. **Parameter count** — assert `controller.getParameterCount() == 5 + 29 = 34`.
2. **Parameter ID uniqueness** — assert no duplicate IDs.
3. **Parameter range round-trip** — for each parameter, set 5 random values via `setParamNormalized`, read via `getParamNormalized`, assert bit-identical.
4. **State save/load round-trip** — full save-load cycle with non-default values; assert all parameters restored.
5. **Phase 1 state backward compatibility** — write a Phase 1-format state (version=1, 5 parameters), load in Phase 2, assert voice sounds like Phase 1 default.
6. **StringListParameter ranges** — for Exciter Type and Body Model, assert `toPlain(0.0)` through `toPlain(1.0)` span the 6 discrete integer values correctly.
7. **Real-time safety on parameter changes** — allocation detector covering `processParameterChanges()` and all parameter setters.
8. **Thread safety** — ThreadSanitizer run in debug build while host automates parameters across all threads. Note: ThreadSanitizer coverage for parameter setters is deferred to Phase 3 CI hardening — single-voice Phase 2 has no concurrent parameter writers (all writes come through `processParameterChanges()` on the audio thread).
9. **Pluginval strictness 5** — explicit CI step verifies pluginval passes.
