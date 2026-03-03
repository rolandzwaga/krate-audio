# Feature Specification: Macros & Rungler UI Exposure

**Feature Branch**: `057-macros-rungler`
**Created**: 2026-02-15
**Status**: Draft
**Plugin**: Ruinae (synthesizer plugin, not Iterum)
**Input**: User description: "Macros and Rungler UI exposure - Phase 4 from Ruinae UI roadmap. Define parameter IDs for macros 1-4 values and rungler configuration, create macro knobs view and rungler configuration view in the modulation source dropdown."
**Roadmap Reference**: [ruinae-ui-roadmap.md](../ruinae-ui-roadmap.md) - Phase 4 (Macros & Rungler)

**Note**: This spec is for the Ruinae synthesizer plugin located at `plugins/ruinae/`. The monorepo contains multiple plugins (Iterum delay, Ruinae synth, etc.). All file paths reference `plugins/ruinae/` correctly.

## Clarifications

### Session 2026-02-15

- Q: What frequency range should the Rungler Osc1 Freq and Osc2 Freq UI parameters use? → A: 0.1-100 Hz (extended for faster chaos, still modulation-focused)
- Q: What should Rungler Depth=0 produce? → A: Depth=0 produces simpler periodic patterns (no cross-modulation, still generates output)
- Q: What display formatting should Rungler parameters use? → A: Freq: "X.XX Hz" (2 decimals), Depth/Filter: "XX%" (0 decimals, 0-100%)
- Q: What should the macro source names be in the mod matrix dropdown? → A: "Macro 1", "Macro 2", "Macro 3", "Macro 4" (full descriptive names)
- Q: Where should `ModSource::Rungler` be inserted in the enum? → A: After Chaos, before SampleHold (Rungler=10, renumber SampleHold=11, PitchFollower=12, Transient=13)

## Context

This spec implements **Phase 4** from the Ruinae UI roadmap: exposing Macros 1-4 and the Rungler as configurable modulation sources.

**Phase 0B** (Spec 053, completed) replaced the mod source tabs with a dropdown selector (`COptionMenu` + `UIViewSwitchContainer`) that already lists all 10 modulation sources: LFO 1, LFO 2, Chaos, **Macros**, **Rungler**, Env Follower, S&H, Random, Pitch Follower, Transient. The dropdown view templates `ModSource_Macros` and `ModSource_Rungler` already exist in the uidesc as empty 158x120px containers. Selecting "Macros" or "Rungler" from the dropdown already shows these empty views. This spec fills them with controls.

**Macros** are user-controlled knobs that feed directly into the modulation matrix as sources. The DSP layer already has:
- `MacroConfig` struct (`modulation_types.h`) with `value`, `minOutput`, `maxOutput`, `curve` fields
- `ModulationEngine::setMacroValue()` to set a macro's knob position
- `ModSource::Macro1` through `ModSource::Macro4` in the enum (values 5-8)
- `getMacroOutput()` processes value through min/max mapping and response curve

What is missing: No VST parameter IDs exist for macro values. The macros produce zero signal because `setMacroValue()` is never called from the processor. Users cannot control macros.

**Rungler** is a Benjolin-inspired chaotic stepped-voltage generator (Spec 029). The DSP class exists at Layer 2 (`dsp/include/krate/dsp/processors/rungler.h`) with full functionality: two cross-modulating triangle oscillators driving an N-bit shift register with XOR feedback, producing evolving stepped sequences via 3-bit DAC. The `Rungler` class implements the `ModulationSource` interface (`getCurrentValue()` returns the filtered CV output [0, 1]).

What is missing: The Rungler is NOT integrated into the `ModulationEngine` or the `RuinaeEngine`. No parameter IDs exist for its configuration. Users cannot configure or use it as a modulation source.

### Scope Clarification: Rungler Parameters

The roadmap lists 8 Rungler parameters including "Clock Div" and "Slew". However, the existing `Rungler` DSP class has 6 configurable parameters:

| Parameter | DSP Method | Range | UI Display |
|-----------|-----------|-------|------------|
| Osc1 Frequency | `setOsc1Frequency(float hz)` | 0.1-100 Hz (UI range, DSP supports 0.1-20000, UI default 2.0 Hz) | "X.XX Hz" (2 decimals) |
| Osc2 Frequency | `setOsc2Frequency(float hz)` | 0.1-100 Hz (UI range, DSP supports 0.1-20000, UI default 3.0 Hz) | "X.XX Hz" (2 decimals) |
| Depth | `setRunglerDepth(float depth)` | 0-1 (default 0, controls cross-modulation intensity) | "XX%" (0-100%, no decimals) |
| Filter | `setFilterAmount(float amount)` | 0-1 (default 0, CV smoothing) | "XX%" (0-100%, no decimals) |
| Bits | `setRunglerBits(size_t bits)` | 4-16 (default 8) | Integer display "X" |
| Loop Mode | `setLoopMode(bool loop)` | false=chaos, true=loop (default false) | Toggle (on/off) |

"Clock Div" and "Slew" do not exist in the current DSP class. This spec covers the 6 parameters that the DSP already supports. Adding Clock Divider and Slew Rate requires DSP-layer changes and is deferred to a future spec -- the Rungler is fully functional and musically useful without them. This keeps the spec focused on the UI-exposure goal.

**Clarification on Depth Parameter Behavior**: At Depth=0 (default), the Rungler still produces output. Depth controls the amount of cross-modulation between the two oscillators. At Depth=0, the oscillators run at their base frequencies without cross-modulation, producing a stepped output from the 3-bit DAC that follows a simple, predictable pattern based on the shift register state (determined by the frequency ratio of the two oscillators). The output is still a stepped CV voltage in the [0, 1] range, but the pattern is more periodic and less chaotic. As Depth increases, the oscillators increasingly modulate each other's frequencies, creating more chaotic and unpredictable patterns. This makes Depth a musical control from "simple periodic stepped CV" (0%) to "maximum chaos" (100%).

### ID Range Allocation

The current `ParameterIDs` enum uses ranges up to 1999 (Phaser). This spec adds two new ranges:

- **2000-2099**: Macro Parameters (4 macro values)
- **2100-2199**: Rungler Parameters (6 configuration params)

The `kNumParameters` sentinel must increase from 2000 to 2200 to accommodate these ranges.

### Current Mod Source View Area

The mod source dropdown view area is 158x120px. Existing templates (LFO, Chaos) use a pattern of:
- Row 1 (y=0): 28x28 knobs with 10px labels below = 38px
- Row 2 (y=42): Toggles or smaller controls = 12-18px
- Row 3 (y=60): Additional 28x28 knobs with labels = 38px

The Macros view (4 knobs) fits comfortably in one row. The Rungler view (6 params) requires two rows but fits within 120px.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Macro Knobs as Modulation Sources (Priority: P1)

A sound designer wants to create a performance macro that simultaneously controls filter cutoff, reverb mix, and LFO depth. They select "Macros" from the modulation source dropdown and see four knobs labeled M1, M2, M3, M4. They turn M1 to about 75%. In the mod matrix, they add three routes: Macro 1 -> Filter Cutoff (amount +0.8), Macro 1 -> Reverb Mix (amount +0.5), Macro 1 -> LFO1 Depth (amount -0.3). Now when they sweep the M1 knob, the filter opens, reverb increases, and LFO depth decreases -- all from a single control. They assign macros M2-M4 to other parameter groups for a total of 4 independent macro controls.

**Why this priority**: Macros are the most requested modulation feature. The entire modulation infrastructure (ModSource enum, ModulationEngine, mod matrix routing) already supports Macro1-4 -- only the parameter IDs and UI controls are missing. This is the highest-value item because it immediately unlocks creative modulation workflows that were previously impossible.

**Independent Test**: Can be fully tested by selecting "Macros" from the mod source dropdown, turning M1 to a non-zero value, adding a mod matrix route from Macro 1 to any destination, and verifying that the destination parameter moves when M1 is adjusted.

**Acceptance Scenarios**:

1. **Given** the user selects "Macros" from the mod source dropdown, **When** they look at the view area, **Then** they see four knobs labeled M1, M2, M3, M4 arranged horizontally
2. **Given** M1 is at 0% (default), **When** the user turns M1 to 100%, **Then** any mod matrix route with Macro 1 as source applies its full amount to the destination
3. **Given** M1 is at 50%, **When** a mod matrix route maps Macro 1 -> Filter Cutoff with amount +0.5, **Then** the filter cutoff is offset by 25% of its range (50% * 0.5)
4. **Given** all four macros have mod matrix routes, **When** the user adjusts each macro independently, **Then** each macro only affects its own routed destinations -- macros are independent
5. **Given** M1 is at 75%, **When** the user saves the preset and reloads it, **Then** M1 restores to 75% and the macro's modulation effect is immediately active
6. **Given** the user automates M1 in their DAW, **When** automation plays back, **Then** the M1 knob moves in the UI and the modulation updates in real-time

---

### User Story 2 - Rungler as Chaotic Modulation Source (Priority: P1)

A producer wants evolving, unpredictable modulation. They select "Rungler" from the modulation source dropdown and see a configuration panel with knobs for Osc1 Freq, Osc2 Freq, Depth, Filter, and Bits, plus a Loop Mode toggle. They set Osc1 to 5 Hz, Osc2 to 7 Hz, Depth to 50%, and Filter to 30%. In the mod matrix, they route the Rungler to filter cutoff. The filter cutoff now changes in chaotic, stepped patterns -- not periodic like an LFO, but with an evolving character determined by the two cross-modulating oscillators and the shift register.

**Why this priority**: The Rungler is a unique modulation source that differentiates Ruinae from other synthesizers. The DSP is fully implemented (Spec 029) but entirely inaccessible. Exposing it requires integration into the engine, parameter IDs, and UI controls. This is equal priority with macros because both are needed to make the modulation system complete.

**Independent Test**: Can be tested by selecting "Rungler" from the dropdown, adjusting Osc1 Freq and Depth, adding a mod matrix route from Rungler to any destination, and verifying that the destination changes in a chaotic stepped pattern.

**Acceptance Scenarios**:

1. **Given** the user selects "Rungler" from the mod source dropdown, **When** they look at the view area, **Then** they see controls for Osc1 Freq, Osc2 Freq, Depth, Filter, Bits, and Loop Mode
2. **Given** the Rungler is configured with non-zero Depth, **When** a mod matrix route maps Rungler to a destination, **Then** the destination parameter changes in a chaotic stepped pattern
3. **Given** the user increases Osc1 Freq and Osc2 Freq, **When** the Rungler is routed to a destination, **Then** the chaotic pattern speed increases
4. **Given** the user increases the Filter amount, **When** the Rungler output changes, **Then** the stepped transitions become smoother (CV smoothing filter)
5. **Given** the user increases Bits from 4 to 16, **When** the Rungler output changes, **Then** the stepped pattern has finer resolution (more distinct voltage levels)
6. **Given** the user enables Loop Mode, **When** the Rungler output is observed over time, **Then** the pattern repeats (deterministic loop) rather than evolving chaotically
7. **Given** Rungler parameters are configured, **When** the user saves and reloads the preset, **Then** all Rungler configuration values are restored
8. **Given** Rungler Depth is at 0 (default), **When** the Rungler is routed to a destination, **Then** the output is simpler and more periodic (oscillators run at base frequencies without cross-modulation, but the shift register still produces stepped CV output)

---

### User Story 3 - Preset Persistence and Automation (Priority: P2)

A user creates a complex patch using Macro 2 to control a filter sweep and the Rungler routed to stereo width for evolving spatial movement. They save the preset. Later they reload it, and all macro values and Rungler configuration restore exactly. In a live performance, they automate Macro 1 and Rungler Osc1 Freq from their DAW for real-time modulation control.

**Why this priority**: Persistence and automation are essential for production use but are standard behaviors that follow established patterns. All ~10 new parameters will use the same `kCanAutomate` flag and state persistence pattern as existing parameters.

**Independent Test**: Can be tested by configuring macros and rungler, saving a preset, loading a different preset, then reloading the saved one and verifying all values restore.

**Acceptance Scenarios**:

1. **Given** M1=0.5, M2=0.3, M3=0.8, M4=0.0 and Rungler with Osc1=5Hz, Osc2=7Hz, Depth=0.5, Filter=0.3, Bits=12, Loop=on, **When** the user saves and reloads the preset, **Then** all 10 parameter values restore to their saved values
2. **Given** a preset saved before this spec existed (no macro/rungler params), **When** the user loads it, **Then** macros default to 0 and rungler defaults to Osc1=200Hz, Osc2=300Hz, Depth=0, Filter=0, Bits=8, Loop=off
3. **Given** a preset saved before this spec with a mod route using SampleHold (old enum value 10), **When** the user loads it, **Then** the route correctly maps to the new SampleHold enum value (11) and the modulation still works
4. **Given** all 10 parameters, **When** the user opens the DAW automation lane list, **Then** all 10 are visible and automatable

---

### Edge Cases

- What happens when all four macros are at 0? The modulation engine returns 0.0 for Macro1-4 sources. Any mod matrix routes with macro sources produce zero offset. This is the default state and expected behavior.
- What happens when the Rungler Depth is at 0? The oscillators do not cross-modulate (they run at their base frequencies independently). The shift register still produces stepped CV output [0, 1], but without chaotic interaction between the oscillators, the pattern is simpler and more periodic. This is musically useful as a starting point — Depth acts as a "chaos amount" control from "simple periodic" (0%) to "maximum chaos" (100%).
- What happens when Rungler Bits is at minimum (4)? The 3-bit DAC output has only 8 distinct voltage levels, producing coarse stepped patterns with limited resolution.
- What happens when Rungler Bits is at maximum (16)? The shift register is 16 bits long, creating very long sequences before repeating (in loop mode) and more varied patterns (in chaos mode).
- What happens when the Rungler Loop Mode is toggled during playback? The shift register continues from its current state. In chaos mode, XOR feedback creates evolving patterns. In loop mode, the register output bit is fed back directly, creating a repeating sequence from whatever the current register state is.
- What happens when Macro values are automated at audio rate? The parameter system smooths automation values. The macro output updates at the modulation engine's block rate, not sample-by-sample, preventing zipper artifacts.
- What happens when Osc1 Freq and Osc2 Freq are set to the same value? The oscillators synchronize, producing a periodic (non-chaotic) pattern. This is valid but musically less interesting than detuned frequencies.
- What happens when a preset with macros at non-zero values is loaded but no mod matrix routes use macros? The macro values are stored and displayed in the UI, but produce no audible effect because no routing references them.

## Requirements *(mandatory)*

### Functional Requirements

**Parameter ID Definitions (Phase 4.1)**

- **FR-001**: A new parameter range `2000-2099` for Macros MUST be added to `plugin_ids.h` with the following IDs:
  - `kMacro1ValueId = 2000` -- Macro 1 knob value [0, 1] (default 0)
  - `kMacro2ValueId = 2001` -- Macro 2 knob value [0, 1] (default 0)
  - `kMacro3ValueId = 2002` -- Macro 3 knob value [0, 1] (default 0)
  - `kMacro4ValueId = 2003` -- Macro 4 knob value [0, 1] (default 0)
  - `kMacroBaseId = 2000`, `kMacroEndId = 2099`
- **FR-002**: A new parameter range `2100-2199` for Rungler MUST be added to `plugin_ids.h` with the following IDs:
  - `kRunglerOsc1FreqId = 2100` -- Oscillator 1 frequency [0.1, 100] Hz UI range (DSP supports up to 20000 Hz, UI default 2.0 Hz, normalized default 0.4337), display format "X.XX Hz"
  - `kRunglerOsc2FreqId = 2101` -- Oscillator 2 frequency [0.1, 100] Hz UI range (DSP supports up to 20000 Hz, UI default 3.0 Hz, normalized default 0.4924), display format "X.XX Hz"
  - `kRunglerDepthId = 2102` -- Cross-modulation depth [0, 1] (default 0), display format "XX%" (0-100%, no decimals)
  - `kRunglerFilterId = 2103` -- CV smoothing filter amount [0, 1] (default 0), display format "XX%" (0-100%, no decimals)
  - `kRunglerBitsId = 2104` -- Shift register bit count [4, 16] integer (default 8), display format "X"
  - `kRunglerLoopModeId = 2105` -- Loop mode on/off (default off = chaos mode), toggle display
  - `kRunglerBaseId = 2100`, `kRunglerEndId = 2199`
- **FR-003**: The `kNumParameters` sentinel MUST be increased from `2000` to `2200` to accommodate the new parameter ranges. The ID range allocation comment at the top of the enum MUST be updated to document the new ranges.

**Parameter Registration**

- **FR-004**: A new `macro_params.h` file MUST be created in `plugins/ruinae/src/parameters/` containing registration, handling, formatting, and persistence functions for the 4 macro parameters, following the established pattern used by `mono_mode_params.h` and `global_filter_params.h`. Each macro parameter MUST be registered with `kCanAutomate`, unit `"%"`, stepCount `0` (continuous), and default value `0.0`. The parameter titles MUST be `"Macro 1"`, `"Macro 2"`, `"Macro 3"`, `"Macro 4"` (full descriptive names for the mod matrix dropdown).
- **FR-005**: A new `rungler_params.h` file MUST be created in `plugins/ruinae/src/parameters/` containing registration, handling, formatting, and persistence functions for the 6 rungler parameters, following the established pattern. Specific parameter types:
  - Osc1 Freq and Osc2 Freq: `RangeParameter` with logarithmic mapping for 0.1-100 Hz range, unit `"Hz"`, display format with 2 decimal places ("X.XX Hz"), UI defaults 2.0 Hz (normalized 0.4337) and 3.0 Hz (normalized 0.4924) respectively
  - Depth: Continuous [0, 1] with `"%"` unit, display format "XX%" (0-100%, no decimals), default 0.0
  - Filter: Continuous [0, 1] with `"%"` unit, display format "XX%" (0-100%, no decimals), default 0.0
  - Bits: `RangeParameter` with `stepCount=12` (13 discrete values from 4 to 16 inclusive, mapped as 4 + round(normalized * 12)), integer display format, default 8 (normalized 0.3333)
  - Loop Mode: Boolean toggle (`stepCount=1`, default 0.0 = chaos mode), on/off display

**Processor Wiring**

- **FR-006**: The processor MUST handle macro parameter changes by calling `engine_.setMacroValue(index, value)` for each macro (index 0-3, value 0-1). The `processParameterChanges()` method MUST recognize IDs 2000-2003 and forward the normalized values to the engine.
- **FR-007**: The processor MUST handle rungler parameter changes by calling the appropriate engine methods for each rungler configuration parameter. This requires adding rungler setter methods to `RuinaeEngine` that forward to a `Rungler` instance in the `ModulationEngine`. The `processParameterChanges()` method MUST recognize IDs 2100-2105 and forward denormalized values to the engine.

**Rungler Integration into ModulationEngine**

- **FR-008**: The `ModulationEngine` MUST be extended to include a `Rungler` instance as a modulation source. The rungler MUST be prepared in `prepare()`, reset in `reset()`, and processed during `processBlock()` when any routing references it as a source (following the existing pattern for ChaosModSource, SampleHoldSource, etc.).
- **FR-009**: A new `ModSource::Rungler = 10` entry MUST be added to the `ModSource` enum in `modulation_types.h` **after Chaos (=9) and before SampleHold**. Existing values SampleHold, PitchFollower, and Transient MUST be renumbered to 11, 12, 13 respectively. `kModSourceCount` MUST be updated from 13 to 14. The `getRawSourceValue()` method in `ModulationEngine` MUST return the Rungler's `getCurrentValue()` for this source. The mod matrix source dropdown (`kModSourceStrings` and `kModSourceCount` in `dropdown_mappings.h`) MUST be updated to include "Rungler" at index 10. **CRITICAL**: This enum renumbering requires a preset migration strategy (see FR-009a).
- **FR-009a** (Preset Migration): To maintain backward compatibility with presets saved before this spec, the state loading code MUST translate old ModSource enum values to new values when loading mod matrix routing data. Specifically:
  - Old SampleHold (10) → New SampleHold (11)
  - Old PitchFollower (11) → New PitchFollower (12)
  - Old Transient (12) → New Transient (13)
  - Old values 0-9 (None through Chaos) remain unchanged
  - New Rungler (10) only appears in presets saved after this spec
  - The migration MUST occur in the mod matrix state loading function (`loadModMatrixState()` or equivalent) by detecting the preset version or presence of new parameters and applying the +1 offset to source values ≥10 (old numbering) when loading old presets.

- **FR-010**: The `RuinaeEngine` MUST expose setter methods for all 6 rungler configuration parameters that forward to the `ModulationEngine`'s Rungler instance (following the same pattern as `setMacroValue()` forwarding to `globalModEngine_.setMacroValue()`).

**State Persistence**

- **FR-011**: All 10 new parameters (4 macro values + 6 rungler config) MUST be saved and loaded using the established state persistence pattern. The save/load functions MUST be called from the processor's `setState()`/`getState()` methods. Backward compatibility MUST be maintained: loading presets saved before this spec MUST use default values for all new parameters (macros=0, rungler defaults per DSP class constants).

**Control-Tag Registration**

- **FR-012**: Control-tags for all 10 new parameters MUST be added to the uidesc control-tags section:
  - `"Macro1Value"` tag `"2000"`, `"Macro2Value"` tag `"2001"`, `"Macro3Value"` tag `"2002"`, `"Macro4Value"` tag `"2003"`
  - `"RunglerOsc1Freq"` tag `"2100"`, `"RunglerOsc2Freq"` tag `"2101"`, `"RunglerDepth"` tag `"2102"`, `"RunglerFilter"` tag `"2103"`, `"RunglerBits"` tag `"2104"`, `"RunglerLoopMode"` tag `"2105"`

**Macros View Template (Phase 4.2)**

- **FR-013**: The existing empty `ModSource_Macros` template (158x120px) MUST be populated with four `ArcKnob` controls (28x28) in a single horizontal row, each bound to its respective macro parameter control-tag. Layout:
  ```
  +----------------------------------+
  | (M1)   (M2)   (M3)   (M4)      |  y=0: 28x28 ArcKnobs
  |  M1     M2     M3     M4        |  y=28: Labels (10px)
  |                                  |
  |                                  |
  +----------------------------------+
           158 x 120
  ```
  - M1 knob at origin (4, 0), M2 at (42, 0), M3 at (80, 0), M4 at (118, 0) -- evenly spaced within 158px
  - Each knob uses `arc-color="modulation"` and `guide-color="knob-guide"` for visual consistency with the modulation section
  - Each has a `CTextLabel` below (at y=28) showing "M1", "M2", "M3", "M4" (font and color styling should match existing mod source view labels from LFO/Chaos templates)
  - Default value for all four knobs: `"0.0"`

**Rungler View Template (Phase 4.3)**

- **FR-014**: The existing empty `ModSource_Rungler` template (158x120px) MUST be populated with controls for all 6 rungler configuration parameters, arranged in two rows. Layout:
  ```
  +----------------------------------+
  | (Osc1)  (Osc2)  (Depth) (Filter)|  y=0: 28x28 ArcKnobs
  |  Osc1    Osc2   Depth   Filter  |  y=28: Labels (10px)
  |                                  |
  | (Bits)   [Loop Mode]            |  y=50: Bits knob + Loop toggle
  |  Bits     Loop                  |  y=78: Labels (10px)
  +----------------------------------+
           158 x 120
  ```
  - Row 1 (y=0): Four 28x28 ArcKnobs -- Osc1 Freq at (4, 0), Osc2 Freq at (42, 0), Depth at (80, 0), Filter at (118, 0)
  - Row 1 labels (y=28): CTextLabels for "Osc1", "Osc2", "Depth", "Filter" (font and color styling should match existing mod source view labels from LFO/Chaos templates)
  - Row 2 (y=50): Bits ArcKnob (28x28) at (4, 50), Loop Mode ToggleButton at (50, 54)
  - Row 2 labels (y=78): CTextLabel "Bits" below knob (match existing label style)
  - All knobs use `arc-color="modulation"` and `guide-color="knob-guide"`
  - Loop Mode toggle uses `on-color="modulation"` and `off-color="toggle-off"` with title "Loop"
  - Default values: Osc1 Freq normalized default 0.4337 (2.0 Hz), Osc2 Freq normalized default 0.4924 (3.0 Hz), Depth=0, Filter=0, Bits normalized default 0.3333 (8 bits), Loop Mode=0

**ModSource Dropdown String Update**

- **FR-015**: The `kModSourceStrings` array in `dropdown_mappings.h` MUST include "Rungler" at index 10 (between "Chaos" and "Sample & Hold"). The `kModSourceCount` MUST be updated from 13 to 14. The `kNumGlobalSources` constant in `mod_matrix_types.h` MUST be updated from 12 to 13 to include the Rungler source.

**No Layout Changes**

- **FR-016**: No window size changes, row repositioning, or FieldsetContainer modifications are required. The macro and rungler views fill existing empty templates within the already-allocated 158x120px mod source view area.

### Key Entities

- **Macro Value Parameter**: A continuous [0, 1] knob that sets the output level of a macro modulation source. Four independent macros (M1-M4) can each be routed to any destination via the mod matrix. The DSP processes macro values through a min/max output range and response curve (from `MacroConfig`) though this spec only exposes the value knob -- min/max/curve are advanced features for a future spec.
- **Rungler Configuration**: Six parameters that control the behavior of the Benjolin-inspired chaotic oscillator. Osc1 Freq and Osc2 Freq set the base frequencies of the two cross-modulating triangle oscillators. Depth controls how much the Rungler CV feeds back into the oscillator frequencies (creating chaos). Filter controls CV smoothing. Bits sets the shift register length. Loop Mode switches between chaotic (XOR feedback) and looping (direct feedback) behavior.
- **ModSource::Rungler**: A new entry in the ModSource enum that connects the Rungler processor to the modulation routing system, allowing it to be selected as a source in any mod matrix slot.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can select "Macros" from the mod source dropdown and see four functional knobs (M1-M4) that control macro modulation values, verified by adjusting M1 and observing a mod matrix route from Macro 1 affecting its destination parameter.
- **SC-002**: Users can select "Rungler" from the mod source dropdown and see six functional controls (Osc1 Freq, Osc2 Freq, Depth, Filter, Bits, Loop Mode) that configure the Rungler behavior, verified by adjusting parameters and observing changes in the Rungler's modulation output.
- **SC-003**: The Rungler produces non-zero modulation output when routed in the mod matrix, verified by adding a route from Rungler to any visible parameter and observing the parameter value change in a chaotic stepped pattern.
- **SC-004**: All 10 new parameters (4 macros + 6 rungler) persist correctly across preset save/load cycles, verified by setting non-default values, saving, loading a different preset, then reloading and confirming all values match.
- **SC-005**: The plugin passes pluginval at strictness level 5, confirming all new parameters are accessible, automatable, and the state save/load cycle works correctly.
- **SC-006**: All existing parameters and controls continue to function identically after the changes, verified by existing tests passing with zero regressions.
- **SC-007**: The plugin builds with zero compiler warnings related to the changes in this spec.
- **SC-008**: Presets saved before this spec load correctly with macros defaulting to 0 and rungler defaulting to UI defaults (Osc1=2.0Hz, Osc2=3.0Hz, Depth=0, Filter=0, Bits=8, Loop=off).
- **SC-009**: Presets saved before this spec that use SampleHold, PitchFollower, or Transient as mod sources load correctly with those sources still functioning (enum values migrated from old 10/11/12 to new 11/12/13), verified by creating a test preset with SampleHold routed before the spec changes, then loading it after the spec and confirming the route still works.
- **SC-010**: All 10 new parameters are visible in DAW automation lanes and respond to automation playback when the editor is open.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The `ModulationEngine` processes modulation sources during `processBlock()`. Sources are only processed when `sourceActive_[source]` is true, which is set by `updateActiveSourceFlags()` based on whether any routing references that source. The Rungler will follow this same pattern -- it only runs when at least one mod matrix slot uses it as a source.
- The `Rungler` DSP class is fully implemented and tested (Spec 029). It implements the `ModulationSource` interface, so it can be stored and called via the same pattern as `ChaosModSource`, `SampleHoldSource`, etc. No DSP algorithm changes are needed.
- The `MacroConfig` struct supports `minOutput`, `maxOutput`, and `curve` fields, but this spec only exposes the `value` knob. The min/max range and curve are advanced features that can be added in a future spec (e.g., right-click macro knob for range settings, or a dedicated macro configuration panel). For now, macros use the full [0, 1] output range with linear curve (the `MacroConfig` defaults).
- The mod source dropdown view switch (`UIViewSwitchContainer`) is already functional. Templates are switched based on the `ModSourceViewMode` parameter (tag 10019). No changes to the switching mechanism are needed.
- The `kModSourceStrings` array and `kModSourceCount` in `dropdown_mappings.h` currently do NOT include Rungler. The ModSource enum has 13 entries (None + 12 sources), and `kModSourceCount = 13`. After adding `ModSource::Rungler`, the count will increase to 14 and the strings array must include "Rungler" at the correct position.
- The mod source view dropdown (`kModSourceViewModeTag` = 10019) lists sources in a different order (LFO1, LFO2, Chaos, Macros, Rungler, ...) than the `ModSource` enum. The view dropdown is a UI-only parameter for switching visible templates and does not map 1:1 to the `ModSource` enum. These are independent.
- The Rungler frequency parameters use a logarithmic mapping for 0.1-100 Hz range (modulation-focused, not full audio range). UI defaults are 2.0 Hz and 3.0 Hz, maintaining the DSP class's 2:3 frequency ratio within the modulation range. This ratio (2.0-3.0 Hz = 0.5 sec and 0.33 sec periods) produces perceptible slow modulation with interesting non-repeating chaotic patterns due to the incommensurate frequencies. The DSP class defaults (200/300 Hz) are audio-rate defaults suitable for standalone use but fall outside the modulation-focused UI range. Display format is "X.XX Hz" with 2 decimal places for precise slow modulation control. Depth and Filter parameters use linear 0-100% mapping with integer display (no decimals).
- Backward compatibility for state loading has TWO concerns: (1) EOF-safe defaults for new parameters (macros=0, rungler defaults), and (2) **ModSource enum renumbering migration** — old presets that use SampleHold/PitchFollower/Transient as mod sources must have their source enum values incremented by 1 during load (old SampleHold=10 → new SampleHold=11, etc.). See FR-009a for migration strategy.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `MacroConfig` struct | `dsp/include/krate/dsp/core/modulation_types.h:118-123` | Already implemented -- value, minOutput, maxOutput, curve. Used as-is. |
| `kMaxMacros = 4` | `modulation_types.h:126` | Already defines the number of macros. |
| `ModulationEngine::setMacroValue()` | `modulation_engine.h:436-438` | Already implemented -- forwards to `macros_[index].value`. Processor will call this. |
| `ModulationEngine::getMacroOutput()` | `modulation_engine.h:642-650` | Already processes value through min/max and curve. Used by `getRawSourceValue()`. |
| `ModSource::Macro1-4` | `modulation_types.h:40-43` | Already in enum (values 5-8). Already handled in `getRawSourceValue()`. |
| `Rungler` class | `dsp/include/krate/dsp/processors/rungler.h:62-468` | Fully implemented DSP processor. Implements `ModulationSource` interface. Needs to be instantiated in `ModulationEngine`. |
| `Rungler::setOsc1Frequency()` | `rungler.h:166-171` | Setter for Osc1 -- will be called from engine. |
| `Rungler::setOsc2Frequency()` | `rungler.h:175-180` | Setter for Osc2 -- will be called from engine. |
| `Rungler::setRunglerDepth()` | `rungler.h:196-200` | Sets both osc depths -- will be called from engine. |
| `Rungler::setFilterAmount()` | `rungler.h:204-207` | CV smoothing -- will be called from engine. |
| `Rungler::setRunglerBits()` | `rungler.h:211-217` | Shift register length -- will be called from engine. |
| `Rungler::setLoopMode()` | `rungler.h:221-223` | Chaos vs loop -- will be called from engine. |
| `RuinaeEngine::setMacroValue()` | `ruinae_engine.h:417-419` | Already forwards to `globalModEngine_`. Pattern to follow for rungler setters. |
| `kModSourceStrings` | `dropdown_mappings.h:172-186` | Mod source dropdown strings. Must insert "Rungler" at index 10 (after "Chaos"). |
| `kModSourceCount = 13` | `dropdown_mappings.h:170` | Must increase to 14 when Rungler is added to ModSource enum. |
| `kNumGlobalSources = 12` | `mod_matrix_types.h:31` | Must increase to 13 when Rungler source is added. |
| `ModSource` enum | `modulation_types.h:36-50` | Rungler=10 must be inserted after Chaos=9. SampleHold/PitchFollower/Transient renumber to 11/12/13. |
| `ModSource_Macros` template | `editor.uidesc:1724` | Empty 158x120px template -- will be populated with 4 knobs. |
| `ModSource_Rungler` template | `editor.uidesc:1725` | Empty 158x120px template -- will be populated with 6 controls. |
| `registerChaosModParams()` | `chaos_mod_params.h:45-77` | Reference pattern for parameter registration + mod view dropdown. |
| `global_filter_params.h` | `plugins/ruinae/src/parameters/global_filter_params.h` | Reference pattern for new parameter file structure (register, handle, format, save/load). |
| `mono_mode_params.h` | `plugins/ruinae/src/parameters/mono_mode_params.h` | Reference pattern for new parameter file structure. |
| Control-tags section | `editor.uidesc:65-217` | Add new Macro and Rungler control-tags here. |

**Initial codebase search for key terms:**

```bash
grep -r "kMacro1ValueId\|kMacro2ValueId\|kRunglerOsc1FreqId" plugins/ruinae/src/plugin_ids.h
# Result: No matches -- no macro or rungler param IDs exist

grep -r "macro_params\|rungler_params" plugins/ruinae/src/parameters/
# Result: No matches -- no macro or rungler param files exist

grep -r "ModSource::Rungler" dsp/
# Result: No matches -- no Rungler entry in ModSource enum

grep -r "Macro1Value\|RunglerOsc1Freq" plugins/ruinae/resources/editor.uidesc
# Result: No matches -- no control-tags for macros or rungler
```

**Search Results Summary**: No macro or rungler parameter IDs, parameter files, ModSource::Rungler enum entry, or uidesc control-tags exist. The DSP classes (MacroConfig, Rungler) and engine forwarding (setMacroValue) are implemented but not wired to the parameter system. The mod source view templates exist but are empty. All gaps are confirmed and addressed by this spec.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Phase 6 (Additional Modulation Sources) adds 5 more mod sources (Env Follower, S&H, Random, Pitch Follower, Transient). These follow the exact same pattern: define param IDs, create a params.h file, register parameters, wire processor, populate the empty uidesc template. The pattern established by this spec's `macro_params.h` and `rungler_params.h` will serve as the template for all Phase 6 parameter files.
- Phase 5.1 (Settings Drawer) needs its own param IDs in a new range but does not interact with macro/rungler parameters.

**Potential shared components** (preliminary, refined in plan.md):
- The `ModulationEngine` integration pattern for the Rungler (adding a new source instance, processing it in `processBlock()`, returning its value in `getRawSourceValue()`) is the exact same pattern needed for Phase 6 sources. The code structure established here will be directly reusable.
- The parameter registration pattern (`macro_params.h` / `rungler_params.h`) with register, handle, format, save, load functions is the standard pattern that Phase 6 will replicate for each new source.

## Dependencies

This spec depends on:
- **053-mod-source-dropdown** (completed, merged): Mod source dropdown selector and `UIViewSwitchContainer` with empty templates for Macros and Rungler
- **029-rungler-oscillator** (completed, merged): Rungler DSP class implementation

This spec enables:
- **Phase 6 (Additional Mod Sources)**: Establishes the integration pattern for adding new modulation sources (param IDs, params.h file, engine integration, uidesc template population)
- **Advanced Macro Configuration**: Future spec could add macro min/max range and curve controls (the `MacroConfig` struct already supports these fields)
- **Rungler Clock Divider and Slew**: Future spec could add these DSP-layer features to the Rungler class and expose them as additional parameters

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it -- record the file path and line number*
3. *Run or read the test that proves it -- record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark with a checkmark without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `plugin_ids.h:562-569` -- kMacroBaseId=2000, kMacro1ValueId-kMacro4ValueId (2000-2003), kMacroEndId=2099 |
| FR-002 | MET | `plugin_ids.h:574-581` -- kRunglerBaseId=2100, kRunglerOsc1FreqId-kRunglerLoopModeId (2100-2105), kRunglerEndId=2199 |
| FR-003 | MET | `plugin_ids.h:584` -- kNumParameters=2200. ID range allocation comment at lines 32-55 documents new ranges |
| FR-004 | MET | `macro_params.h:13-14` -- MacroParams struct, 4 atomic floats. register/handle/format/save/load functions. Tests pass (macro_params_test.cpp) |
| FR-005 | MET | `rungler_params.h:18-25` -- RunglerParams struct with 6 fields, log freq mapping, bits mapping. Tests pass (rungler_params_test.cpp) |
| FR-006 | MET | `processor.cpp:641-642` -- handleMacroParamChange for IDs 2000-2003. `processor.cpp:971-974` -- applyParamsToEngine loops setMacroValue |
| FR-007 | MET | `processor.cpp:643-644` -- handleRunglerParamChange for IDs 2100-2105. `processor.cpp:978-983` -- 6 engine rungler setters called |
| FR-008 | MET | `modulation_engine.h:28` -- includes rungler.h. Line 778: Rungler field. Prepare/reset/process/getRawSourceValue integrated. Test: "Rungler source processes and returns value" passes |
| FR-009 | MET | `modulation_types.h:47` -- Rungler=10 inserted. SampleHold=11, PitchFollower=12, Transient=13 renumbered. kModSourceCount=14 |
| FR-009a | MET | `processor.cpp:542-553` -- Migration: version<13 increments source values>=10 by +1. `controller.cpp:278` -- same. Tests: state_migration_test.cpp passes |
| FR-010 | MET | `ruinae_engine.h:422-427` -- 6 setter methods forwarding to globalModEngine_. Test: "RuinaeEngine Rungler setters forward" passes |
| FR-011 | MET | `processor.h:69` -- kCurrentStateVersion=13. Save in getState, load in setState with version>=13 guard. Backward compat: defaults for version<13 |
| FR-012 | MET | `editor.uidesc:84-95` -- 10 control-tags: Macro1-4Value (2000-2003), RunglerOsc1Freq-LoopMode (2100-2105) |
| FR-013 | MET | `editor.uidesc:1738-1767` -- ModSource_Macros template: 4 ArcKnobs (M1-M4) at x=4,42,80,118, 28x28, with labels |
| FR-014 | MET | `editor.uidesc:1768-1806` -- ModSource_Rungler template: Row 1 = 4 ArcKnobs (Osc1/Osc2/Depth/Filter), Row 2 = Bits ArcKnob + Loop ToggleButton |
| FR-015 | MET | `dropdown_mappings.h:183` -- "Rungler" at index 10. kModSourceCount=14. `mod_matrix_types.h:31` -- kNumGlobalSources=13 |
| FR-016 | MET | No window size changes. Templates fill existing 158x120 containers |
| SC-001 | MET | 4 functional ArcKnob controls (M1-M4) bound to kMacro1-4ValueId. Registered with kCanAutomate. Pluginval and unit tests pass |
| SC-002 | MET | 6 functional controls in Rungler template. Parameters registered, handled, forwarded. Pluginval and unit tests pass |
| SC-003 | MET | "Rungler source processes and returns value" test verifies non-zero chaotic modulation output. Passes |
| SC-004 | MET | Save/load round-trip tests for both macros and rungler pass. Pluginval "Plugin state" test passes |
| SC-005 | MET | Pluginval strictness 5: all 19 test sections pass with zero errors |
| SC-006 | MET | All 4 test suites: dsp_tests (5,473), ruinae_tests (302), plugin_tests (239), shared_tests (175) -- zero failures |
| SC-007 | MET | Build with zero warnings (verified by filtering build output excluding external deps) |
| SC-008 | MET | version<13 guard keeps macro defaults=0, rungler defaults=2.0/3.0Hz/0/0/8/false per constructors |
| SC-009 | MET | Migration: source values>=10 shift +1 for version<13. Tests in state_migration_test.cpp pass |
| SC-010 | MET | All 10 params registered with kCanAutomate. Pluginval "Automatable Parameters" and "Automation" tests pass |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [X] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [X] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [X] Evidence column contains specific file paths, line numbers, test names, and measured values
- [X] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [X] No test thresholds relaxed from spec requirements
- [X] No placeholder values or TODO comments in new code
- [X] No features quietly removed from scope
- [X] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

All 17 functional requirements (FR-001 through FR-016, including FR-009a) and all 10 success criteria (SC-001 through SC-010) are MET. The compliance agent independently verified each requirement against the implementation code and test results. No gaps, no deferred items, no relaxed thresholds.
