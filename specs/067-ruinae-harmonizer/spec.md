# Feature Specification: Ruinae Harmonizer Integration

**Feature Branch**: `067-ruinae-harmonizer`
**Plugin**: Ruinae
**Created**: 2026-02-19
**Status**: Draft
**Input**: User description: "Can we put the harmonizer in the effect chain of the ruinae plugin? It should be placed after the delay engine and before the reverb engine in the signal path. The parameters for the harmonizer should be placed in its own section in the effects section of the UI, similar to the other effects."

## Clarifications

### Session 2026-02-19

- Q: How should the harmonizer be inserted in the stereo signal path? → A: Harmonizer stereo output replaces delay stereo output. The engine's built-in dry/wet handles the blend; incoming stereo is summed to mono for the engine's dry path.
- Q: When Number of Voices is set to fewer than 4, how are the inactive voice controls presented in the UI? → A: All 4 voice rows always visible; inactive voices are dimmed/disabled (greyed out) based on the Number of Voices value.
- Q: How should the harmonizer's latency be incorporated into the effects chain's total latency reporting? → A: Add the harmonizer's worst-case (PhaseVocoder) latency to the existing constant during prepare(), reporting their sum always regardless of active pitch shift mode.
- Q: What are the default values for the Dry Level and Wet Level parameters? → A: 0 dB dry, -6 dB wet (unity dry, slightly reduced wet — audible on first enable without level jump).
- Q: What is the initial state of the harmonizer panel's expand/collapse chevron? → A: Collapsed by default (panel shows header only; user expands to configure).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Enable Harmonizer in Effects Chain (Priority: P1)

A sound designer working in Ruinae wants to add pitch-shifted harmony voices to their synthesizer patches. They open the effects section of the UI, find the Harmonizer panel alongside the existing Phaser, Delay, and Reverb panels, and toggle the Harmonizer on. Audio from the delay output now passes through the harmonizer before reaching the reverb, adding harmonized voices to their sound. They can adjust the harmony mode, key, scale, number of voices, and dry/wet mix directly from the UI.

**Why this priority**: This is the core feature -- without the harmonizer being present in the signal path and controllable from the UI, no other functionality matters.

**Independent Test**: Can be fully tested by enabling the harmonizer in the effects chain, playing a note, and verifying that harmonized voices appear in the output signal between delay and reverb processing. Delivers the primary value of pitch-shifted harmony within the synthesizer.

**Acceptance Scenarios**:

1. **Given** the Ruinae plugin is loaded with default settings, **When** the user enables the Harmonizer effect, **Then** the harmonizer processes audio after the delay engine and before the reverb engine in the signal path.
2. **Given** the Harmonizer is enabled with 2 voices set to a 3rd and 5th above in C Major (Scalic mode), **When** a note is played, **Then** the output contains the original pitch plus two diatonic harmony voices at the correct intervals.
3. **Given** the Harmonizer is enabled, **When** the user adjusts the dry/wet mix, **Then** the balance between the unprocessed and harmonized signal changes smoothly without clicks or artifacts.

---

### User Story 2 - Configure Per-Voice Harmony Parameters (Priority: P2)

A producer wants to create a wide, lush harmonized pad. They configure 4 harmony voices with different diatonic intervals (+3rd, +5th, -3rd, -5th), spread them across the stereo field using per-voice pan controls, add slight per-voice detuning for ensemble width, and adjust individual voice levels for balance. The result is a rich, spatially distributed harmonic texture.

**Why this priority**: Per-voice configuration is what distinguishes a harmonizer from a simple pitch shifter, enabling musically expressive results. Without this, the feature is limited to basic shifting.

**Independent Test**: Can be tested by configuring each of the 4 voices with distinct intervals, pan positions, and levels, then verifying that each voice appears at the correct pitch with the correct stereo position and relative level in the output.

**Acceptance Scenarios**:

1. **Given** the Harmonizer is enabled with 4 voices, **When** the user sets each voice to a different diatonic interval, **Then** each voice produces the correct pitch-shifted output relative to the detected input pitch.
2. **Given** Voice 1 is panned hard left and Voice 2 is panned hard right, **When** audio is processed, **Then** Voice 1 appears predominantly in the left channel and Voice 2 in the right channel.
3. **Given** Voice 1 has +3 cents detune and Voice 2 has -3 cents detune, **When** both voices share the same interval, **Then** a natural beating/ensemble effect is audible.

---

### User Story 3 - Harmonizer UI Section (Priority: P2)

A user navigates to the effects section in the Ruinae UI and sees the Harmonizer panel presented consistently with the existing Phaser, Delay, and Reverb panels. The panel has an enable/disable toggle, global controls (harmony mode, key, scale, pitch shift mode, formant preserve, dry level, wet level), and per-voice controls (interval, level, pan, delay, detune). The panel can be expanded/collapsed like other effect panels.

**Why this priority**: The UI is essential for usability; without it, the harmonizer is inaccessible to users.

**Independent Test**: Can be tested by opening the plugin UI, verifying the Harmonizer panel is visible in the effects section, and confirming all controls are present and functional.

**Acceptance Scenarios**:

1. **Given** the Ruinae plugin UI is open, **When** the user navigates to the effects section, **Then** a Harmonizer panel is visible alongside the Phaser, Delay, and Reverb panels.
2. **Given** the Harmonizer panel is visible, **When** the user clicks the enable toggle, **Then** the harmonizer is enabled/disabled and the UI reflects the current state.
3. **Given** the Harmonizer panel is expanded, **When** the user inspects the controls, **Then** all global and per-voice parameters are accessible with appropriate control types (knobs, dropdowns, toggles).
4. **Given** Number of Voices is set to 2, **When** the user views the per-voice rows, **Then** all 4 rows are visible, rows 1-2 are enabled, and rows 3-4 are visually dimmed; increasing Number of Voices to 4 un-dims rows 3-4 immediately.
5. **Given** the plugin is freshly loaded, **When** the user views the effects section, **Then** the Harmonizer panel is collapsed (header row only visible); clicking the chevron expands the full panel with all controls.

---

### User Story 4 - Harmonizer State Persistence (Priority: P3)

A user creates a patch with specific harmonizer settings (mode, key, scale, 3 voices with intervals, levels, pans, detune, and delay offsets). They save the preset and reload it later. All harmonizer parameters are restored exactly as configured. The harmonizer enabled/disabled state and all per-voice settings survive save/load cycles.

**Why this priority**: State persistence is essential for practical use but is a standard pattern already established for other effects in Ruinae.

**Independent Test**: Can be tested by configuring harmonizer parameters, saving and loading state, then verifying all parameters match the saved values.

**Acceptance Scenarios**:

1. **Given** the Harmonizer is configured with specific settings, **When** the user saves and reloads the plugin state, **Then** all harmonizer parameters are restored exactly.
2. **Given** the Harmonizer is disabled, **When** state is saved and reloaded, **Then** the Harmonizer remains disabled.

---

### Edge Cases

- What happens when the harmonizer is enabled but all voices are set to 0 (no active voices)? The dry signal passes through unchanged.
- What happens when the input pitch cannot be detected (noise, silence) in Scalic mode? The harmonizer holds the last detected note; if no note was ever detected, voices produce no output (wet signal is silent).
- What happens when the user switches harmony mode from Scalic to Chromatic while audio is playing? The pitch shift transitions smoothly without clicks (parameter smoothing handles the transition).
- What happens when the harmonizer introduces latency? The effects chain latency reporting accounts for the harmonizer's contribution.
- What happens when the plugin is loaded at sample rates above 96kHz? The harmonizer engine supports all standard sample rates via its prepare() method.
- What happens when formant preservation is enabled with a pitch shift mode that does not support it (e.g., Simple or Granular)? Formant preservation is silently ignored for modes that do not use the phase vocoder pipeline.

## Requirements *(mandatory)*

### Functional Requirements

**Effects Chain Integration**

- **FR-001**: The RuinaeEffectsChain MUST include a HarmonizerEngine instance as a processing slot between the delay engine and the reverb engine.
- **FR-002**: The signal path order MUST be: Phaser -> Delay -> **Harmonizer** -> Reverb.
- **FR-003**: The RuinaeEffectsChain MUST prepare, reset, and process the HarmonizerEngine alongside existing effects, following the same lifecycle pattern (prepare in prepare(), reset in reset(), process in processChunk()).
- **FR-004**: The HarmonizerEngine MUST be independently enableable/disableable via a dedicated enable parameter, following the same pattern as the existing delay, reverb, and phaser enable flags.
- **FR-005**: When the harmonizer is disabled, the signal MUST pass through unmodified (zero processing cost beyond the enable check).

**Parameter Registration**

- **FR-006**: A new parameter ID range (2800-2899) MUST be allocated for harmonizer parameters in plugin_ids.h.
- **FR-007**: A new FX enable parameter (kHarmonizerEnabledId) MUST be added in the FX Enable Parameters section (1503).
- **FR-008**: The following global harmonizer parameters MUST be registered: Harmony Mode (Chromatic/Scalic), Key (C through B), Scale (Major/NaturalMinor/HarmonicMinor/MelodicMinor/Dorian/Mixolydian/Phrygian/Lydian/Chromatic), Pitch Shift Mode (Simple/Granular/PhaseVocoder/PitchSync), Formant Preserve (on/off), Number of Voices (0-4), Dry Level (dB, default 0 dB), Wet Level (dB, default -6 dB).
- **FR-009**: The following per-voice parameters MUST be registered for each of the 4 voices: Interval (diatonic steps, -24 to +24), Level (-60 to +6 dB), Pan (-1 to +1), Delay (0 to 50 ms), Detune (-50 to +50 cents).
- **FR-010**: All harmonizer parameters MUST follow the existing naming conventions (kHarmonizer{Parameter}Id pattern for globals, kHarmonizerVoice{N}{Parameter}Id for per-voice).

**Parameter Handling**

- **FR-011**: A RuinaeHarmonizerParams struct MUST be created in a new harmonizer_params.h file, using atomic fields for thread-safe parameter storage (matching the pattern of RuinaePhaserParams).
- **FR-012**: The processor MUST handle harmonizer parameter changes in processParameterChanges(), forwarding values to the effects chain.
- **FR-013**: The effects chain MUST expose setter methods for all harmonizer parameters, forwarding to the HarmonizerEngine instance.

**State Persistence**

- **FR-014**: All harmonizer parameters MUST be saved and loaded as part of the plugin state, following the existing save/load pattern used by other effects.
- **FR-015**: The harmonizer enabled state MUST be saved and loaded alongside other FX enable states.

**UI Integration**

- **FR-016**: The harmonizer MUST have its own section in the effects area of the editor.uidesc, consistent with the layout pattern of the Phaser, Delay, and Reverb panels.
- **FR-017**: The harmonizer panel MUST include an expand/collapse chevron toggle, following the same pattern as other effect panels.
- **FR-018**: A UI action tag (kActionFxExpandHarmonizerTag) MUST be added for the expand/collapse chevron.
- **FR-022**: The harmonizer panel MUST display all 4 voice rows at all times. Voice rows whose index is >= the current Number of Voices value MUST appear visually dimmed (disabled state). The dimming state MUST update live as the Number of Voices parameter changes. This avoids dynamic layout changes and allows users to pre-configure voices before activating them.
- **FR-023**: The harmonizer panel MUST default to the collapsed state on first load and on preset load when no explicit expand/collapse state has been saved. Only the header row (enable toggle, label, chevron) is visible when collapsed. This is consistent with the approach for other effect panels and avoids an overwhelming UI on initial load.

**Latency Reporting**

- **FR-019**: During `prepare()`, the RuinaeEffectsChain MUST query the HarmonizerEngine's worst-case (PhaseVocoder) latency via `getLatencySamples()` after setting the pitch shift mode to PhaseVocoder, then add this value to the existing spectral delay latency constant to form a new combined constant (`targetLatencySamples_`). This combined value is returned by `getLatencySamples()` for the lifetime of that prepare session.
- **FR-020**: The total reported latency MUST remain constant at the combined worst-case value regardless of which pitch shift mode is active at runtime. The host is never notified of a mid-session latency change due to pitch shift mode switching.

**Input Signal Handling**

- **FR-021**: The HarmonizerEngine takes mono input and produces stereo output that already incorporates the engine's built-in dry/wet blend. The effects chain MUST sum the stereo delay output to mono (L+R * 0.5) before passing it as the harmonizer's input, then use the harmonizer's stereo output directly as the signal entering the reverb slot, replacing the delay output entirely. No additional external dry/wet mixing is performed by the effects chain; all blend control is handled by the engine's Dry Level and Wet Level parameters.

### Key Entities

- **HarmonizerEngine** (Krate::DSP, Layer 3): The existing DSP component from the shared library that provides multi-voice pitch-shifted harmonization with pitch tracking, scale awareness, and per-voice control. Supports 4 voices, Chromatic/Scalic harmony modes, 4 pitch shift modes, formant preservation, and shared-analysis FFT optimization.
- **RuinaeEffectsChain** (Krate::DSP): The existing effects chain class that composes Phaser -> Delay -> Reverb. Must be extended to include the Harmonizer slot between Delay and Reverb.
- **RuinaeHarmonizerParams** (Ruinae): New parameter struct holding atomic values for all harmonizer parameters, matching the pattern of RuinaePhaserParams.
- **ParameterIDs** (Ruinae): The existing enum in plugin_ids.h that defines all parameter IDs. Must be extended with harmonizer parameter IDs (range 2800-2899 and enable at 1503).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can enable the harmonizer and hear pitch-shifted harmony voices within the synthesizer signal chain, with the harmonizer audibly processing audio between the delay and reverb effects.
- **SC-002**: All 4 harmony voices produce correct diatonic intervals when Scalic mode is selected with any key and scale combination, verified by spectral analysis of the output.
- **SC-003**: The harmonizer adds less than 2% CPU overhead when disabled (enable check only). When enabled with 4 voices in PitchSync mode, total plugin CPU usage remains under 15% at 44.1kHz.
- **SC-004**: All harmonizer parameters survive save/load cycles with exact value preservation (round-trip accuracy within floating-point precision).
- **SC-005**: The harmonizer UI panel is visually consistent with existing effect panels (Phaser, Delay, Reverb) in layout, spacing, and interaction patterns.
- **SC-006**: Switching the harmonizer on/off produces no clicks or audio artifacts (smooth enable/disable transition).
- **SC-007**: All existing tests continue to pass with zero regressions after the harmonizer integration.
- **SC-008**: The harmonizer section is accessible via expand/collapse like other effect panels, and all controls (dropdowns, knobs, toggles) respond correctly to user interaction.
- **SC-009**: On fresh plugin load, the harmonizer panel is collapsed by default. Voice rows 3 and 4 are visually dimmed when Number of Voices is set to 2, and un-dim immediately when Number of Voices is increased to 4.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The HarmonizerEngine (Krate::DSP) is fully implemented and tested in the shared DSP library at `dsp/include/krate/dsp/systems/harmonizer_engine.h`, supporting 4 voices with Chromatic and Scalic harmony modes, all 4 pitch shift modes, formant preservation, and shared-analysis FFT optimization.
- The default harmony mode is Chromatic (fixed semitone shifts, no pitch tracking required), as established in spec 064.
- The default number of active voices is 0 (harmonizer enabled but silent until the user adds voices), consistent with a non-destructive default.
- The default Dry Level is 0 dB (unity gain) and the default Wet Level is -6 dB. This ensures the first enable of the harmonizer is audible (harmony voices present at slightly reduced level) without causing a perceived volume jump or overloading the signal path.
- Parameter ID range 2800-2899 is currently unallocated and available for harmonizer parameters.
- FX enable ID 1503 is currently unallocated (existing: 1500=Delay, 1501=Reverb, 1502=Phaser).
- The HarmonizerEngine introduces latency that varies by pitch shift mode; PhaseVocoder mode has the highest latency. During `prepare()`, the effects chain queries the harmonizer's worst-case (PhaseVocoder) latency and adds it to the existing spectral delay latency constant to form a new combined constant, which is held fixed for the session and reported as-is regardless of the active pitch shift mode at runtime (consistent with the existing constant worst-case model).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| HarmonizerEngine | `dsp/include/krate/dsp/systems/harmonizer_engine.h` | **Direct reuse**: The DSP engine to be integrated. No modification needed -- only instantiation and parameter forwarding. |
| RuinaeEffectsChain | `plugins/ruinae/src/engine/ruinae_effects_chain.h` | **Extend**: Add harmonizer_ member, setter methods, and process call between delay and reverb slots. |
| RuinaePhaserParams | `plugins/ruinae/src/parameters/phaser_params.h` | **Reference pattern**: Follow identical pattern for RuinaeHarmonizerParams (struct with atomics, register/handle/format/save/load functions). |
| ParameterIDs enum | `plugins/ruinae/src/plugin_ids.h` | **Extend**: Add harmonizer parameter IDs in range 2800-2899 and kHarmonizerEnabledId at 1503. |
| Processor | `plugins/ruinae/src/processor/processor.h` | **Extend**: Add harmonizer param struct member, handle param changes, forward to engine. |
| editor.uidesc | `plugins/ruinae/resources/editor.uidesc` | **Extend**: Add harmonizer UI panel in effects section. |
| handlePhaserParamChange | `plugins/ruinae/src/parameters/phaser_params.h` | **Reference pattern**: Follow identical pattern for handleHarmonizerParamChange. |
| registerPhaserParams | `plugins/ruinae/src/parameters/phaser_params.h` | **Reference pattern**: Follow identical pattern for registerHarmonizerParams. |
| savePhaserParams / loadPhaserParams | `plugins/ruinae/src/parameters/phaser_params.h` | **Reference pattern**: Follow identical pattern for saveHarmonizerParams / loadHarmonizerParams. |

**Search Results Summary**: No existing harmonizer-related code was found in the Ruinae plugin (`plugins/ruinae/`). The HarmonizerEngine exists only in the shared DSP library. No ODR risk identified.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- The Iterum delay plugin could also benefit from a harmonizer integration in its effects chain. The parameter registration pattern (harmonizer_params.h) established here could serve as a reference.
- Other future Krate plugins that want harmonizer functionality would follow the same integration pattern.

**Potential shared components** (preliminary, refined in plan.md):
- The RuinaeHarmonizerParams pattern could potentially be extracted to a shared harmonizer params template if multiple plugins need identical parameter layouts. However, for now, keeping it plugin-local is simpler and follows the existing pattern established by phaser, delay, and reverb params.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it -- record the file path and line number*
3. *Run or read the test that proves it -- record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark with a check without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [ ] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [ ] Evidence column contains specific file paths, line numbers, test names, and measured values
- [ ] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
