# Research: Macros & Rungler UI Exposure

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md) | **Date**: 2026-02-15

## Research Questions & Findings

### RQ-1: Rungler DSP API Surface

**Context**: The Rungler DSP class exists at Layer 2 but is not integrated into the ModulationEngine. Need to verify the exact API signatures, constants, and integration requirements.

**Decision**: Use the existing `Rungler` class as-is. No DSP modifications needed.

**Rationale**: The Rungler class implements the `ModulationSource` interface (providing `getCurrentValue()`), has `prepare(double sampleRate)`, `reset()`, and `processBlock(size_t numSamples)` methods that match the pattern used by all other modulation sources in `ModulationEngine`. All setter methods (`setOsc1Frequency`, `setOsc2Frequency`, `setRunglerDepth`, `setFilterAmount`, `setRunglerBits`, `setLoopMode`) are public, `noexcept`, and take simple types.

**Alternatives considered**:
- Creating a wrapper class around Rungler for ModulationEngine: Unnecessary, Rungler already implements ModulationSource.
- Modifying Rungler to add Clock Div and Slew: Deferred to future spec. Current 6 parameters are sufficient for Phase 4.

**Key constants verified**:
- `kMinFrequency = 0.1f`, `kMaxFrequency = 20000.0f`
- `kDefaultOsc1Freq = 200.0f`, `kDefaultOsc2Freq = 300.0f`
- `kMinBits = 4`, `kMaxBits = 16`, `kDefaultBits = 8`
- Location: `dsp/include/krate/dsp/processors/rungler.h:81-88`

---

### RQ-2: Frequency Range and Default Values

**Context**: The spec specifies UI range 0.1-100 Hz but the DSP defaults are 200/300 Hz (outside UI range). Need to reconcile.

**Decision**: Use 2.0 Hz and 3.0 Hz as UI defaults, maintaining the DSP's 2:3 frequency ratio within the modulation-focused range.

**Rationale**: The Rungler in the context of a modulation source should default to low-frequency operation (0.1-100 Hz) for LFO-like chaotic modulation. The DSP class defaults (200/300 Hz) are audio-rate defaults suitable for the standalone Rungler use case. For modulation, 2.0/3.0 Hz produces musically useful results -- the 2:3 ratio between oscillators creates interesting non-repeating patterns at a speed that's perceptible as modulation.

**Alternatives considered**:
- Clamp to UI max (100 Hz): Would lose the frequency ratio relationship.
- Widen UI range to 20000 Hz: Would make the knob impractical for modulation (too wide a range).
- Use DSP defaults (200/300 Hz) and widen range: Conflicts with the spec's 0.1-100 Hz requirement.

**Logarithmic mapping**:
- Formula: `hz = 0.1 * pow(1000.0, normalized)` maps [0,1] to [0.1, 100] Hz
- Inverse: `normalized = log(hz / 0.1) / log(1000.0)`
- Osc1 default (2.0 Hz): `normalized = log(20) / log(1000) = 1.301 / 3.0 = 0.4337`
- Osc2 default (3.0 Hz): `normalized = log(30) / log(1000) = 1.477 / 3.0 = 0.4924`

---

### RQ-3: ModSource Enum Renumbering and Preset Migration

**Context**: Inserting `ModSource::Rungler = 10` shifts SampleHold/PitchFollower/Transient. Presets store mod source values as integers. Need a backward-compatible migration strategy.

**Decision**: Apply migration during state loading in `Processor::setState()` when `version < 13`. For mod matrix source values >= 10, increment by 1.

**Rationale**: The state version (bumped from 12 to 13) provides a clear boundary. All presets saved with version < 13 use the old enum numbering. The migration is simple (single-pass increment for values >= 10), safe (values 0-9 are unchanged), and follows the principle of doing migration at load time rather than polluting the runtime.

**Alternatives considered**:
- Append Rungler at the end of the enum (value 13): Would break the logical grouping (chaos sources should be together). The mod source dropdown strings would be in a confusing order.
- Use a mapping table instead of enum renumbering: Would require maintaining two numbering schemes, adding complexity without benefit.
- Migrate in ModulationEngine instead of processor: Wrong layer -- ModulationEngine should not know about state versioning.

**Migration scope**: Two locations need migration:
1. **Mod matrix params** (global slots): `modMatrixParams_.slots[i].source` -- each slot stores a ModSource value as int.
2. **Voice routes** (per-voice): `voiceRouteParams_` -- store source as int8_t. Same migration: if source >= 10 and version < 13, increment by 1.

---

### RQ-4: Macro Parameter Plumbing

**Context**: MacroConfig and setMacroValue() exist in DSP, but no VST parameter IDs exist. Need to verify the full data flow path.

**Decision**: Create simple parameter IDs (2000-2003) that directly set macro values via `engine_.setMacroValue(index, value)`.

**Rationale**: The entire DSP path already exists:
1. `RuinaeEngine::setMacroValue(index, value)` -> forwards to `globalModEngine_`
2. `ModulationEngine::setMacroValue(index, value)` -> stores to `macros_[index].value`
3. `ModulationEngine::getRawSourceValue(ModSource::Macro1..4)` -> calls `getMacroOutput(index)`
4. `getMacroOutput()` -> processes through MacroConfig (value, minOutput, maxOutput, curve)
5. With default MacroConfig (min=0, max=1, curve=Linear), the output equals the input value.

The only missing piece is the VST parameter -> processor -> engine wiring, which is pure plumbing.

**Alternatives considered**:
- Expose MacroConfig.minOutput/maxOutput/curve as parameters: Deferred. The spec explicitly calls this out as a future feature. Exposing only the value knob is consistent with typical macro implementations.
- Use a single parameter with index encoding: Would make automation confusing. Four independent parameters is cleaner.

---

### RQ-5: Parameter File Pattern

**Context**: Need to create two new parameter files. Which existing file is the best template?

**Decision**: Use `mono_mode_params.h` as the primary template, with `chaos_mod_params.h` as reference for the mod source view mode pattern.

**Rationale**: `mono_mode_params.h` is the most recently created parameter file (spec 056) and demonstrates the cleanest pattern:
- `struct MonoModeParams` with `std::atomic<T>` fields
- `handleMonoModeParamChange()` with switch/case on IDs
- `registerMonoModeParams()` using `parameters.addParameter()`
- `formatMonoModeParam()` returning `kResultOk` or `kResultFalse`
- `saveMonoModeParams()` using `streamer.writeXxx()`
- `loadMonoModeParams()` using `streamer.readXxx()` with early return
- `loadMonoModeParamsToController()` using template SetParamFunc

This pattern has been refined over 19 iterations and is the established standard.

**Alternatives considered**:
- `global_filter_params.h`: Also a good reference but older, with a more complex logarithmic mapping that we do not need to replicate (our log mapping is different).
- Creating a base class or template for param files: Over-engineering -- each param file is ~100-120 lines and the inline functions have no runtime cost.

---

### RQ-6: kNumGlobalSources vs kModSourceCount

**Context**: Both constants exist and need updating but serve different purposes.

**Decision**: Update both: `kModSourceCount` from 13 to 14 (in `modulation_types.h`), `kNumGlobalSources` from 12 to 13 (in `mod_matrix_types.h`).

**Rationale**:
- `kModSourceCount` (DSP layer) = total ModSource enum entries including None = 14 after adding Rungler
- `kNumGlobalSources` (UI layer) = total sources excluding None = 13 after adding Rungler
- The relationship `kNumGlobalSources == kModSourceCount - 1` is maintained

**Impact**:
- `kModSourceStrings[]` in `dropdown_mappings.h` must have 14 entries (indexed by ModSource value)
- `mod_matrix_grid_test.cpp` has an assertion `REQUIRE(kNumGlobalSources == 12)` that must change to 13
- The mod source view dropdown (10 entries) in `chaos_mod_params.h` is INDEPENDENT -- it already lists Rungler and does not need changes

---

### RQ-7: State Persistence Order

**Context**: New params must be appended to the state stream. Need to verify the exact save/load order.

**Decision**: Append macro params, then rungler params after the v12 extended LFO params at the end of the stream.

**Rationale**: The current save order (verified from `processor.cpp:353-405`):
1. State version (int32)
2. Global params
3. Osc A/B params
4. Filter A/B params
5. Mix params
6. Amp envelope params
7. Filter A/B envelope params
8. Mod matrix params
9. Delay params (by type)
10. Mod matrix routes (v3+)
11. Voice routes source fields (v3+)
12. Extended delay params (v5+, v7+, v9+)
13. Mono mode params (v10+)
14. FX enabled flags (v10+)
15. Phaser params (v11+)
16. Extended LFO params (v12+)
17. **Macro params (v13+)** -- NEW
18. **Rungler params (v13+)** -- NEW

This append-at-end pattern maintains backward compatibility: older versions simply stop reading before reaching the new data, and the new fields default to their initial values.

---

### RQ-8: UIDESC Template Layout

**Context**: The `ModSource_Macros` and `ModSource_Rungler` templates already exist as empty 158x120px containers. Need to verify the exact template names and how they are referenced.

**Decision**: Populate the existing templates in-place. No new template creation or view switching changes needed.

**Rationale**: The view switch mechanism (spec 053) already maps dropdown index 3 ("Macros") to `ModSource_Macros` and index 4 ("Rungler") to `ModSource_Rungler`. These templates are empty `<view>` elements with `class="CViewContainer"` and the correct dimensions. We just need to add child controls inside them.

**Verified template names**: `ModSource_Macros` (dropdown index 3), `ModSource_Rungler` (dropdown index 4). These match the `UIViewSwitchContainer` configuration from spec 053.

---

### RQ-9: Existing Test Files Requiring Updates

**Context**: Need to identify all test files that will break due to enum renumbering or constant changes.

**Decision**: The primary test requiring update is `mod_matrix_grid_test.cpp` which asserts `kNumGlobalSources == 12`.

**Rationale**: Search results show:
- `mod_matrix_grid_test.cpp` directly asserts the source count
- DSP tests for the Rungler class (`dsp/tests/processors/rungler_test.cpp`) test the standalone class and do not reference ModSource enum values
- DSP tests for ModulationEngine (`dsp/tests/systems/modulation_engine_test.cpp`) may reference ModSource enum values but these use the enum names (e.g., `ModSource::SampleHold`) not integer literals, so they will automatically pick up the new values
- Any test that hardcodes ModSource integer values (e.g., `source = 10` meaning SampleHold) would break, but this pattern is unlikely -- tests should use enum names

**Action items**:
- Update `REQUIRE(kNumGlobalSources == 12)` to `REQUIRE(kNumGlobalSources == 13)` in `mod_matrix_grid_test.cpp`
- Run full test suite after enum changes to catch any other hardcoded values
