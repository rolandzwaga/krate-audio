# Feature Specification: Membrum Phase 4 — 32-Pad Layout, Per-Pad Presets, Kit Presets, Separate Outputs

**Feature Branch**: `139-membrum-phase4-pads`
**Plugin**: Membrum (`plugins/membrum/`)
**Created**: 2026-04-12
**Status**: Draft
**Input**: Phase 4 scope from Spec 135 (Membrum Synthesized Drum Machine); builds on Phase 3 (`specs/138-membrum-phase3-polyphony/`)

## Clarifications

### Session 2026-04-12

- Q: Should kit presets save and restore `selectedPadIndex`? → A: No. Kit presets exclude `selectedPadIndex`. Loading a kit preset leaves the current pad selection unchanged.
- Q: Should `kPadParamStride` be 32 (tight) or 64 (padded for future per-pad parameters)? → A: 64. Offsets 0-35 are active in Phase 4 (including secondary exciter params at 32-35); offsets 36-63 are reserved for future phases (e.g., per-pad coupling amount in Phase 5). Pad 0 owns IDs 1000-1063, pad 1 owns 1064-1127, ..., pad 31 owns 2984-3047.
- Q: On v3-to-v4 migration, should the single shared config be applied to pad 0 only, or cloned to all 32 pads? → A: Moot — there is no released version of Membrum, so no real users have v3 state to migrate. The state format retains a version header for correctness, but elaborate v3 migration logic is unnecessary. FR-071 is simplified: on loading a v3 blob, the Phase 3 config is applied to pad 0 only; all other pads receive GM-inspired defaults per FR-030. No clone-to-all-32 requirement.
- Q: When `kSelectedPadId` changes and ~36 global proxy parameter values must update, is a batch/suppress mechanism needed? → A: No. One-at-a-time updates are acceptable. Hosts handle rapid sequential parameter-value notifications correctly; a batch suppression mechanism would be over-engineering for Phase 4.
- Q: When a pad is assigned to an auxiliary bus that is inactive, should the output bus parameter offer inactive buses as choices, or fall back silently? → A: Prevent assignment to inactive buses. The Output Bus parameter for each pad MUST only offer currently-active buses as valid choices (plus main, which is always active). If a host deactivates a bus after a pad has been assigned to it, the pad falls back to main output. The processor and controller each track bus active state independently via their own `activateBus()` overrides.

## Background

Phase 3 (spec 138) shipped Membrum v0.3.0 with a multi-voice polyphony engine: 4-16 configurable voices managed by `VoicePool` with three stealing policies (Oldest/Quietest/Priority), 8 choke groups, click-free 5 ms exponential fast-release, and state schema v3. However, every MIDI note still hits the **same single pad template** -- all 32 pads share one exciter type, one body model, and one parameter set. There is no way to build a drum kit with distinct sounds per pad.

Phase 4 is the fundamental transformation from "single-voice demo" to "drum machine." Each of the 32 pads (MIDI 36-67, following the General MIDI Level 1 drum map) gets its own independent exciter type, body model, and full parameter set. Users can save and load individual pad configurations ("pad presets") and complete 32-pad kits ("kit presets"). The plugin exposes separate stereo output buses so producers can mix individual drums in their DAW.

Phase 4 is **deliberately scoped to pad architecture, presets, and output routing.** Cross-pad coupling (sympathetic resonance) is Phase 5. Macro controls, Acoustic/Extended UI modes, and the custom VSTGUI editor are Phase 6. See "Deferred to Later Phases" at the end.

## GM Drum Map Reference

Phase 4 maps 32 pads to the General MIDI Level 1 percussion standard (MIDI channel 10). The assignments follow the GM specification exactly:

| Pad | MIDI Note | GM Name | Default Template |
|-----|-----------|---------|------------------|
| 1   | 36 (C1)   | Bass Drum 1 | Kick |
| 2   | 37 (C#1)  | Side Stick | Perc |
| 3   | 38 (D1)   | Acoustic Snare | Snare |
| 4   | 39 (Eb1)  | Hand Clap | Perc |
| 5   | 40 (E1)   | Electric Snare | Snare |
| 6   | 41 (F1)   | Low Floor Tom | Tom |
| 7   | 42 (F#1)  | Closed Hi-Hat | Hat |
| 8   | 43 (G1)   | High Floor Tom | Tom |
| 9   | 44 (G#1)  | Pedal Hi-Hat | Hat |
| 10  | 45 (A1)   | Low Tom | Tom |
| 11  | 46 (A#1)  | Open Hi-Hat | Hat |
| 12  | 47 (B1)   | Low-Mid Tom | Tom |
| 13  | 48 (C2)   | Hi-Mid Tom | Tom |
| 14  | 49 (C#2)  | Crash Cymbal 1 | Cymbal |
| 15  | 50 (D2)   | High Tom | Tom |
| 16  | 51 (D#2)  | Ride Cymbal 1 | Cymbal |
| 17  | 52 (E2)   | Chinese Cymbal | Cymbal |
| 18  | 53 (F2)   | Ride Bell | Cymbal |
| 19  | 54 (F#2)  | Tambourine | Perc |
| 20  | 55 (G2)   | Splash Cymbal | Cymbal |
| 21  | 56 (G#2)  | Cowbell | Perc |
| 22  | 57 (A2)   | Crash Cymbal 2 | Cymbal |
| 23  | 58 (A#2)  | Vibraslap | Perc |
| 24  | 59 (B2)   | Ride Cymbal 2 | Cymbal |
| 25  | 60 (C3)   | Hi Bongo | Perc |
| 26  | 61 (C#3)  | Low Bongo | Perc |
| 27  | 62 (D3)   | Mute Hi Conga | Perc |
| 28  | 63 (Eb3)  | Open Hi Conga | Perc |
| 29  | 64 (E3)   | Low Conga | Perc |
| 30  | 65 (F3)   | High Timbale | Perc |
| 31  | 66 (F#3)  | Low Timbale | Perc |
| 32  | 67 (G#3)  | High Agogo | Perc |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Each Pad Has Its Own Sound (Priority: P1)

A producer opens Membrum and wants to build a drum kit. They select pad 1 (MIDI 36, kick) and configure it with Impulse exciter + Membrane body + specific Material/Size/Decay settings for a deep punchy kick. They select pad 3 (MIDI 38, snare) and configure it with Noise Burst exciter + Membrane body + snare-like settings. They select pad 7 (MIDI 42, closed hat) and configure it with Noise Burst + Noise Body for a tight metallic hat. Playing MIDI notes 36, 38, and 42 produces three distinct drum sounds simultaneously, each with its own character. Changing a parameter on one pad does not affect any other pad.

**Why this priority**: Per-pad independence is THE Phase 4 deliverable. Without it, Membrum is not a drum machine -- it is a single drum synth with polyphony. This story transforms the plugin's identity.

**Independent Test**: Configure 3 different pads with distinct exciter/body combinations. Trigger all 3 simultaneously. Verify each produces a spectrally distinct sound, and modifying one pad's parameters does not alter the other pads' outputs.

**Acceptance Scenarios**:

1. **Given** pad 1 (MIDI 36) is configured as Impulse+Membrane and pad 3 (MIDI 38) is configured as Noise Burst+Membrane, **When** both notes are triggered simultaneously at velocity 100, **Then** the mixed output contains two spectrally distinct drum sounds -- the pad-36 sound has a sharp impulse transient while the pad-38 sound has a noise-dominated attack.
2. **Given** pad 7 (MIDI 42) has Size=0.2 and pad 10 (MIDI 45) has Size=0.8, **When** both are triggered, **Then** pad 7 produces a higher-pitched sound and pad 10 produces a lower-pitched sound, consistent with their independent Size settings.
3. **Given** any pad's exciter type is changed via its per-pad parameter, **When** a different pad is triggered, **Then** that different pad's sound is unchanged (parameter isolation).
4. **Given** all 32 pads are configured with distinct exciter/body combinations, **When** the DAW saves and reloads plugin state, **Then** all 32 pad configurations round-trip exactly.
5. **Given** a MIDI note outside the 36-67 range is received, **When** processed, **Then** it is silently dropped (no crash, no output, consistent with Phase 1-3 behavior).

---

### User Story 2 - Default Kit Uses GM-Inspired Templates (Priority: P1)

When Membrum is first loaded (or reset to default), the 32 pads are pre-configured with sound templates that match the GM drum map expectations. Pad 1 (Bass Drum 1) sounds like a kick. Pad 3 (Acoustic Snare) sounds like a snare. Pad 7 (Closed Hi-Hat) sounds like a hi-hat. The producer can immediately play a recognizable drum pattern without any manual configuration.

**Why this priority**: A blank slate with 32 identical pads is unusable. Default templates make the plugin immediately musical and demonstrate the range of sounds. Equal P1 with US1.

**Independent Test**: Load a fresh Membrum instance. Trigger pads 36 (kick), 38 (snare), 42 (closed hat), 46 (open hat), 49 (crash). Verify each produces a sound consistent with its GM name -- kick is low-frequency dominant, snare has noise component, hat is high-frequency metallic, crash has long decay.

**Acceptance Scenarios**:

1. **Given** a freshly loaded Membrum, **When** MIDI note 36 is triggered, **Then** the output has a dominant low-frequency component (fundamental below 100 Hz) and short decay, consistent with a kick drum.
2. **Given** a freshly loaded Membrum, **When** MIDI note 38 is triggered, **Then** the output has a noise component in the 2-8 kHz range, consistent with a snare drum.
3. **Given** a freshly loaded Membrum, **When** MIDI note 42 is triggered, **Then** the output is high-frequency dominant (spectral centroid above 4 kHz) with very short decay (< 100 ms), consistent with a closed hi-hat.
4. **Given** a freshly loaded Membrum, **When** MIDI note 49 is triggered, **Then** the output has a long decay (> 500 ms) with broadband frequency content, consistent with a crash cymbal.
5. **Given** the default kit, **When** all 32 pads are triggered sequentially, **Then** each produces non-silent output with no NaN/Inf/denormal values.

---

### User Story 3 - Kit Presets Save and Load All 32 Pads (Priority: P1)

A sound designer builds a custom kit: specific exciter/body/parameter configuration for each of the 32 pads, specific choke group assignments, specific output routing. They save this as a "Kit Preset" called "Deep House Kit" in the "Electronic" category. Later, they load the preset and every pad, choke group, and output assignment is restored exactly. They share the preset file with a collaborator, who loads it in their copy of Membrum and hears the identical kit.

**Why this priority**: Kit presets are the primary preset format for Membrum. Without them, users must reconfigure 32 pads every session. This is the main user-facing deliverable alongside per-pad independence.

**Independent Test**: Configure all 32 pads with distinct settings, save as kit preset, reset the plugin to defaults, load the kit preset. Verify all 32 pads match the original configuration.

**Acceptance Scenarios**:

1. **Given** a fully configured 32-pad kit, **When** the user saves a kit preset, **Then** a preset file is created containing all per-pad parameters (exciter type, body model, Material, Size, Decay, Strike Position, Level, all Tone Shaper and Unnatural Zone settings, all secondary exciter parameters), all choke group assignments, all output routing assignments, and global settings (max polyphony, voice stealing policy).
2. **Given** a saved kit preset, **When** it is loaded into a default Membrum instance, **Then** every pad's configuration matches the saved state bit-exactly (normalized parameter values).
3. **Given** a kit preset saved by one user, **When** loaded by a different user on a different machine, **Then** the kit sounds identical (preset files are self-contained, platform-independent).
4. **Given** a kit preset from Membrum v0.4.0, **When** loaded in a hypothetical future v0.5.0, **Then** the preset loads successfully with forward-compatible defaults for any new parameters.
5. **Given** a kit preset file is corrupted or truncated, **When** a load is attempted, **Then** the load fails gracefully with an error message and the current kit state is preserved (no partial load).

---

### User Story 4 - Per-Pad Presets Save and Load Individual Pad Sounds (Priority: P2)

A sound designer creates a perfect kick drum on pad 1. They want to save just that kick sound for reuse in other kits. They save a "Pad Preset" for pad 1, which captures only that pad's configuration. Later, in a different kit, they load the pad preset onto pad 6 (normally a floor tom), replacing its configuration with the saved kick sound. The other 31 pads are unaffected.

**Why this priority**: Per-pad presets enable sound library building and reuse across kits. Important for workflow but secondary to kit presets -- users can function without per-pad presets (just reconfigure manually) but not without kit presets.

**Independent Test**: Configure pad 1 with a specific sound, save as pad preset. Load the pad preset onto a different pad (e.g., pad 10). Verify pad 10 now sounds identical to the original pad 1, and all other pads are unchanged.

**Acceptance Scenarios**:

1. **Given** a configured pad, **When** the user saves a per-pad preset, **Then** a preset file is created containing that pad's exciter type, body model, all parameter values (Material, Size, Decay, Strike Position, Level, Tone Shaper, Unnatural Zone, Material Morph, secondary exciter params FM Ratio/Feedback Amount/NoiseBurst Duration/Friction Pressure), but NOT choke group or output routing (those are kit-level settings).
2. **Given** a saved pad preset, **When** loaded onto any of the 32 pads, **Then** that pad's sound configuration is replaced with the preset's values. All other pads are unchanged.
3. **Given** a pad preset saved from pad 1, **When** loaded onto pad 15, **Then** the sound is identical to the original (pad presets are pad-position-independent).
4. **Given** a per-pad preset, **When** loaded, **Then** the pad's choke group assignment and output routing are NOT changed (those are kit-level concerns, not sound-level).

---

### User Story 5 - Separate Output Buses for Mixing (Priority: P2)

A producer wants to process the kick, snare, and hi-hats separately in their DAW's mixer. They activate Membrum's auxiliary output buses in the DAW. The main stereo output carries the full mixed kit. Auxiliary output 1 carries only the kick (pad 1). Auxiliary output 2 carries only the snare (pad 3). Each pad can be assigned to one of 16 stereo output buses (1 main + 15 auxiliary). Pads not assigned to an auxiliary output go to the main mix only.

**Why this priority**: Separate outputs are essential for professional production workflows. Without them, the entire drum kit must be mixed as a single stereo signal, which is unacceptable for serious use. P2 because the plugin is functional with just the main stereo output (Phase 1-3 behavior), but not professionally usable.

**Independent Test**: Assign pad 1 to auxiliary output 1, pad 3 to auxiliary output 2, leave pad 7 on main only. Trigger all three. Verify auxiliary output 1 contains only the kick, auxiliary output 2 contains only the snare, and the main output contains all three sounds.

**Acceptance Scenarios**:

1. **Given** pad 1 is assigned to output bus 2 (Aux 1), **When** MIDI note 36 is triggered, **Then** the audio appears on both the main mix (bus 0) AND the assigned auxiliary bus (bus 2). Pads always appear on the main mix regardless of auxiliary assignment.
2. **Given** pad 3 is assigned to output bus 3 (Aux 2), **When** only MIDI note 38 is triggered, **Then** output bus 3 contains the snare sound and output bus 2 (Aux 1) contains silence (no cross-routing).
3. **Given** a pad has no auxiliary output assignment (output bus = 0, main only), **When** the pad is triggered, **Then** audio appears only on the main stereo bus.
4. **Given** the host has activated 4 auxiliary output buses, **When** the plugin processes audio, **Then** only the 4 activated auxiliary buses receive audio. Deactivated buses are not processed (no CPU waste).
5. **Given** the output routing assignments for all 32 pads, **When** the DAW saves and reloads state, **Then** all assignments round-trip exactly.
6. **Given** a host that does not support auxiliary buses (or has none activated), **When** the plugin processes audio, **Then** all pads output to the main stereo bus and the plugin functions identically to Phase 3 behavior.
7. **Given** the host has only 2 auxiliary buses activated, **When** the Output Bus parameter for a pad is presented to the user, **Then** only the main bus and those 2 active auxiliary buses are offered as choices — inactive buses are not selectable. If the host subsequently deactivates one of those buses while a pad is assigned to it, that pad falls back to main output automatically.

---

### User Story 6 - State Version Migration (Priority: P2)

Note: Membrum has no released version. There are no real users with v3 state in production. This story covers the structural requirement for version-tagged state and minimal forward/backward compatibility, not an elaborate migration path for real-world upgrade scenarios.

A developer or QA engineer reloads a v3 state blob (e.g., from a test fixture or a pre-release build) in a Phase 4 processor. The load succeeds without errors. The Phase 3 shared voice configuration is applied to pad 0 only. All other 31 pads receive GM-inspired defaults per FR-030. Output routing defaults to main-only. No data is lost or corrupted.

**Why this priority**: State versioning and graceful load of older-format blobs is a correctness requirement, not a user-facing migration story. The "clone to all 32 pads" requirement was removed because it was premised on real upgrading users, which do not exist.

**Independent Test**: Load a captured v3 state blob into a Phase 4 processor. Assert Phase 3 parameters land on pad 0, all other pads receive defaults, and output routing defaults to main-only.

**Acceptance Scenarios**:

1. **Given** a Phase 3 (v3) state blob, **When** loaded into a Phase 4 processor, **Then** load succeeds, the Phase 3 shared voice configuration (exciter type, body model, all sound parameters) is applied to pad 0 only, all other pads (1-31) receive GM-inspired defaults per FR-030, choke groups take their defaults per FR-032, output routing defaults to main-only, and all other new Phase 4 parameters take their documented defaults.
2. **Given** a Phase 4 (v4) state blob, **When** saved and reloaded, **Then** all Phase 4 parameters (per-pad configurations, output routing, kit-level settings) round-trip bit-exactly.
3. **Given** a Phase 1 (v1) or Phase 2 (v2) state blob, **When** loaded into a Phase 4 processor, **Then** the migration chain (v1->v2->v3->v4) succeeds without error.
4. **Given** a Phase 4 (v4) state blob loaded into a Phase 3 processor, **When** the version field is read, **Then** the Phase 3 processor rejects the newer state gracefully (no crash, no corruption).

---

### Edge Cases

- **All 32 pads triggered simultaneously**: Must not crash or exceed reasonable CPU limits. Not all 32 will sound if max polyphony is lower (voice stealing applies).
- **Same MIDI note on multiple rapid triggers**: Each trigger allocates a voice from the pool. The pad's per-pad configuration is used for each voice, regardless of how many voices that pad currently has active.
- **Pad preset loaded while pad's voice is sounding**: The parameter change applies to the next note-on for that pad. Currently-sounding voices for that pad continue with their original configuration until natural decay or voice stealing.
- **Kit preset loaded while voices are sounding**: All currently-sounding voices should fast-release (5 ms), then the new kit configuration takes effect.
- **Output bus assignment changed while audio is playing**: Takes effect on the next process block. No click, no gap.
- **Host deactivates an auxiliary bus while pads are routed to it**: The processor detects the deactivation via `activateBus()` and immediately routes those pads to the main output only. There is no silent audio loss — the pads remain audible on the main bus. The deactivated bus receives no audio. No crash. The controller also updates the Output Bus parameter's valid choices to exclude the now-inactive bus.
- **Kit preset from a future version with more parameters**: Unknown parameter IDs are silently ignored. Known parameters load normally.
- **Per-pad parameter with value outside valid range in state blob**: Clamped on load, never trusted raw.
- **Choke groups in kit presets**: Choke group assignments are per-pad and saved in kit presets. Loading a kit preset updates all choke group assignments.
- **Output routing saved in kit preset**: Output bus assignments are per-pad and saved in kit presets.

## Requirements *(mandatory)*

### Functional Requirements

#### Per-Pad Architecture

- **FR-001**: Each of the 32 pads (MIDI notes 36-67) MUST have its own independent set of per-pad parameters: Exciter Type, Body Model, Material, Size, Decay, Strike Position, Level, all Tone Shaper parameters (14 params: offsets 7-20), all Unnatural Zone parameters (4 params), all Material Morph parameters (5 params), Choke Group, Output Bus, FM Ratio, Feedback Amount, NoiseBurst Duration, and Friction Pressure. Total: 36 parameters per pad (34 sound + choke group + output bus; 2 selectors + 5 core + 14 Tone Shaper + 4 Unnatural Zone + 5 Material Morph + choke + output bus = 36).
- **FR-002**: Per-pad parameters MUST be stored in a `PadConfig` structure (one per pad, 32 instances) that is pre-allocated at initialization. No dynamic allocation when switching pads or loading presets.
- **FR-003**: When a MIDI note-on arrives for note N (36 <= N <= 67), the voice pool MUST use pad config `[N - 36]` to configure the allocated voice -- applying that pad's exciter type, body model, and all per-pad parameters.
- **FR-004**: Changing a parameter on one pad MUST NOT affect any other pad's configuration or any currently-sounding voices from other pads.
- **FR-005**: The Phase 3 `VoicePool::setSharedVoiceParams()` API MUST be replaced with a per-pad parameter dispatch: when a voice is allocated for pad N, the voice receives pad N's parameters, not a global shared set.

#### Per-Pad Parameter IDs

- **FR-010**: Per-pad parameters MUST use a computed ID scheme: `kPadBaseId + padIndex * kPadParamStride + paramOffset`, where `kPadBaseId = 1000`, `kPadParamStride = 64`, and `paramOffset` identifies the parameter within a pad. This gives pad 0 IDs 1000-1063, pad 1 IDs 1064-1127, and so on through pad 31 IDs 2984-3047. Offsets 36-63 within each stride are RESERVED for future phases (e.g., per-pad coupling parameters in Phase 5) and MUST NOT be assigned in Phase 4.
- **FR-011**: The per-pad parameter offsets within each pad's stride MUST be:

| Offset | Parameter | Type | Range | Default |
|--------|-----------|------|-------|---------|
| 0  | Exciter Type | StringList | 0-5 (6 types) | per-template |
| 1  | Body Model | StringList | 0-5 (6 models) | per-template |
| 2  | Material | Range | 0.0-1.0 | per-template |
| 3  | Size | Range | 0.0-1.0 | per-template |
| 4  | Decay | Range | 0.0-1.0 | per-template |
| 5  | Strike Position | Range | 0.0-1.0 | per-template |
| 6  | Level | Range | 0.0-1.0 | 0.8 |
| 7  | Tone Shaper Filter Type | StringList | 0-2 (LP/HP/BP) | 0 (LP) |
| 8  | Tone Shaper Filter Cutoff | Range | 20-20000 Hz | 20000 |
| 9  | Tone Shaper Filter Resonance | Range | 0.0-1.0 | 0.0 |
| 10 | Tone Shaper Filter Env Amount | Range | -1.0-1.0 | 0.0 |
| 11 | Tone Shaper Drive Amount | Range | 0.0-1.0 | 0.0 |
| 12 | Tone Shaper Fold Amount | Range | 0.0-1.0 | 0.0 |
| 13 | Tone Shaper Pitch Env Start | Range | 20-2000 Hz | 160 |
| 14 | Tone Shaper Pitch Env End | Range | 20-2000 Hz | 50 |
| 15 | Tone Shaper Pitch Env Time | Range | 0-500 ms | 0 |
| 16 | Tone Shaper Pitch Env Curve | StringList | 0-1 (Exp/Lin) | 0 (Exp) |
| 17 | Tone Shaper Filter Env Attack | Range | 0-1000 ms | 0 |
| 18 | Tone Shaper Filter Env Decay | Range | 0-2000 ms | 200 |
| 19 | Tone Shaper Filter Env Sustain | Range | 0.0-1.0 | 0.0 |
| 20 | Tone Shaper Filter Env Release | Range | 0-2000 ms | 300 |
| 21 | Unnatural Mode Stretch | Range | 0.5-2.0 | 1.0 |
| 22 | Unnatural Decay Skew | Range | -1.0-1.0 | 0.0 |
| 23 | Unnatural Mode Inject Amount | Range | 0.0-1.0 | 0.0 |
| 24 | Unnatural Nonlinear Coupling | Range | 0.0-1.0 | 0.0 |
| 25 | Material Morph Enabled | StringList | 0-1 (Off/On) | 0 (Off) |
| 26 | Material Morph Start | Range | 0.0-1.0 | 1.0 |
| 27 | Material Morph End | Range | 0.0-1.0 | 0.0 |
| 28 | Material Morph Duration | Range | 10-2000 ms | 200 |
| 29 | Material Morph Curve | StringList | 0-1 (Lin/Exp) | 0 (Lin) |
| 30 | Choke Group | Range (stepped) | 0-8 | per-template |
| 31 | Output Bus | Range (stepped) | 0-15 | 0 (main only) |
| 32 | FM Ratio | Range | 0.0-1.0 | 0.5 |
| 33 | Feedback Amount | Range | 0.0-1.0 | 0.0 |
| 34 | NoiseBurst Duration | Range | 0.0-1.0 | 0.5 |
| 35 | Friction Pressure | Range | 0.0-1.0 | 0.0 |

  `kPadActiveParamCount = 36`. Offsets 36-63 within each stride are RESERVED for future phases.

- **FR-012**: The Phase 1-3 "global" parameter IDs (100-252) MUST be preserved in the controller for backward compatibility, but they MUST function as "selected pad" parameters -- changing `kMaterialId` (100) changes the material for whichever pad is currently selected for editing. This allows the host-generic editor to work without 32 x 32 visible parameters. Output Bus (PadConfig offset 31) is NOT proxied through a global parameter ID. It can only be set via the per-pad parameter. This is intentional: output routing is a kit-level concern, not a per-pad sound parameter.
- **FR-013**: A new parameter `kSelectedPadId` (260) MUST be added as a stepped RangeParameter (0-31, default 0) that controls which pad the global parameter IDs (100-252) target. When the selected pad changes, the global parameters MUST update their displayed values to reflect the newly selected pad's configuration.

#### Default Kit Templates

- **FR-030**: On first load (no state) or plugin reset, all 32 pads MUST be initialized with GM-inspired default templates as defined in the "GM Drum Map Reference" table's "Default Template" column.
- **FR-031**: Each template (Kick, Snare, Tom, Hat, Cymbal, Perc) MUST map to a specific exciter type, body model, and parameter set as defined in spec 135's "Pad Templates" table. The following mappings MUST be used:

| Template | Exciter | Body | Key Settings |
|----------|---------|------|-------------|
| Kick | Impulse | Membrane | Size=0.8, Decay=0.3, Material=0.3, Pitch Env: 160->50Hz/20ms |
| Snare | Noise Burst | Membrane | Size=0.5, Decay=0.4, Material=0.5, NoiseBurstDuration=8ms (offset 34 normalized) |
| Tom | Mallet | Membrane | Size varies by pitch (0.4-0.8), Decay=0.5, Material=0.4 |
| Hat | Noise Burst | Noise Body | Size=0.15, Decay=0.1, Material=0.9, NoiseBurstDuration=3ms (offset 34 normalized) |
| Cymbal | Noise Burst | Noise Body | Size=0.3, Decay=0.8, Material=0.95, NoiseBurstDuration=10ms (offset 34 normalized) |
| Perc | Mallet | Plate | Size=0.3, Decay=0.3, Material=0.7 |

- **FR-032**: The default choke group assignments MUST follow standard drum machine conventions: closed hi-hat (MIDI 42), pedal hi-hat (MIDI 44), and open hi-hat (MIDI 46) all in choke group 1. All other pads default to choke group 0 (no choke).
- **FR-033**: Tom pads (MIDI 41, 43, 45, 47, 48, 50) MUST have progressively larger Size values to produce a natural pitch gradient from high to low, matching GM pitch expectations: High Tom (50) Size=0.4, Hi-Mid Tom (48) Size=0.45, Low-Mid Tom (47) Size=0.5, Low Tom (45) Size=0.6, High Floor Tom (43) Size=0.7, Low Floor Tom (41) Size=0.8.

#### Separate Output Buses

- **FR-040**: The processor MUST declare 1 main stereo output bus (`kMain`, default active) and 15 auxiliary stereo output buses (`kAux`, default inactive) in `initialize()`. Total: 16 stereo output buses.
- **FR-041**: The main output bus (index 0) MUST always carry the full mixed kit -- every pad's audio is summed into the main bus regardless of auxiliary assignments.
- **FR-042**: When a pad is assigned to auxiliary bus N (1-15) AND the host has activated that bus, the pad's audio MUST also be written to that auxiliary bus. This is a "send" model, not an "exclusive routing" model -- the main mix always receives all pads.
- **FR-043**: The Output Bus parameter for each pad MUST only present currently-active buses as valid choices (plus main output, which is always active and always available). The controller MUST update the Output Bus parameter's valid range when `activateBus()` notifications arrive — the controller tracks bus active state via its own `activateBus()` override, not by calling processor methods. If the host deactivates an auxiliary bus after a pad has already been assigned to it, the processor MUST detect the deactivation via its own `activateBus()` override and fall back to routing that pad to the main output. There MUST be no silent audio loss — the pad remains audible on the main bus. No crash, no CPU waste on deactivated buses.
- **FR-044**: The `VoicePool::processBlock()` method MUST be extended to accept a multi-bus output buffer structure. After each voice renders into the scratch buffer, the scratch is accumulated into (a) the main output, and (b) the auxiliary bus assigned to that voice's originating pad (if the bus is active).
- **FR-045**: Output bus activation/deactivation MUST be handled via `activateBus()`. The processor MUST track which auxiliary buses are active and skip writing to inactive ones.
- **FR-046**: The AU configuration files MUST be updated to reflect the multi-output bus layout. The `au-info.plist` and `audiounitconfig.h` MUST declare the supported channel configurations including the auxiliary outputs.

#### Kit Presets

- **FR-050**: Kit presets MUST use the existing `Krate::Plugins::PresetManager` infrastructure with a `PresetManagerConfig` configured for Membrum. The preset category structure MUST support subcategories (e.g., "Electronic", "Acoustic", "Experimental", "Cinematic").
- **FR-051**: Kit preset files MUST contain: a version header, all 32 pad configurations (exciter type, body model, all 34 per-pad sound parameters at offsets 2-35, choke group, output bus), and global settings (max polyphony, voice stealing policy). Kit preset files MUST NOT contain `selectedPadIndex`; loading a kit preset leaves the currently selected pad unchanged.
- **FR-052**: Kit preset save/load MUST be implemented as `StateProvider` and `LoadProvider` callbacks for the `PresetManager`. The state format MUST be the same binary format used by `getState`/`setState` (the kit preset IS the plugin state).
- **FR-053**: The plugin MUST ship with at least 3 factory kit presets: one electronic/808-style kit, one acoustic-inspired kit, and one experimental/FX kit.

#### Per-Pad Presets

- **FR-060**: Per-pad presets MUST be a separate preset type from kit presets, stored in a different subdirectory (`Pad Presets/` alongside the kit preset directory `Kit Presets/`).
- **FR-061**: Per-pad preset files MUST contain: a version header and one pad's sound configuration (exciter type, body model, all 34 per-pad sound parameters from FR-011 offsets 2-35). Per-pad presets MUST NOT contain choke group assignment, output routing, or any kit-level settings.
- **FR-062**: Loading a per-pad preset MUST apply only to the currently selected pad (identified by `kSelectedPadId`). All other pads MUST be unchanged.
- **FR-063**: Per-pad preset save/load MUST use a dedicated `PadPresetManager` (or a second `PresetManager` instance with a separate config) with subcategories matching the template types: "Kick", "Snare", "Tom", "Hat", "Cymbal", "Perc", "Tonal", "808", "FX".

#### State Serialization (v4)

- **FR-070**: State schema version MUST be bumped to 4. The v4 state format MUST contain:
  - Version header (int32 = 4)
  - Global settings: maxPolyphony (int32), voiceStealingPolicy (int32)
  - Per-pad data (32 entries, sequential): exciter type (int32), body model (int32), 34 per-pad parameter values (as float64 each, offsets 2-35), choke group (uint8), output bus (uint8)
  - Selected pad index (int32)

  Per-pad byte layout: 4 + 4 + 34×8 + 1 + 1 = 282 bytes per pad. Total state size: 12 (header + globals) + 32×282 + 4 (selectedPadIndex) = 9040 bytes. Kit preset size (without selectedPadIndex): 9036 bytes.

  The selected pad index is part of plugin DAW state (so the DAW can restore which pad the user was editing) but MUST NOT be included in kit preset files. Kit preset load leaves `selectedPadIndex` at its current value.

- **FR-071**: Loading a v3 state MUST succeed via migration. Because Membrum has no released version and no real users have v3 state in production, this migration path is minimal: the Phase 3 shared voice configuration (exciter type, body model, all sound parameters) is applied to pad 0 only. Pads 1-31 receive GM-inspired defaults per FR-030. The Phase 3 choke group value is applied to pad 0 only; pads 1-31 receive default choke groups per FR-032. All output bus assignments default to 0 (main only). The state version header MUST be present and checked; migration MUST NOT crash or produce corrupted state.

  **Migration note**: Phase 3 stored choke group assignments as a ChokeGroupTable (32 per-pad entries). During v3-to-v4 migration, the existing table entry [0] is read and applied to pad 0; entries 1-31 are replaced by FR-032 defaults. The secondary exciter parameters (FM Ratio, Feedback Amount, NoiseBurst Duration, Friction Pressure) did not exist in v3 state; they receive their documented per-offset defaults for all pads.
- **FR-072**: Loading a v1 or v2 state MUST chain through the existing v2->v3 migration path, then apply the v3->v4 migration from FR-071.
- **FR-073**: State save MUST always write v4 format. All 32 pads are always serialized, even if most have default values.

#### Processor and Voice Pool Changes

- **FR-080**: The `VoicePool` MUST be refactored to support per-pad voice configuration. The `SharedParams` structure MUST be replaced with a `PadConfig[32]` array. When `noteOn(midiNote, velocity)` is called, the voice pool looks up `padConfigs_[midiNote - 36]` and applies those parameters to the allocated voice.
- **FR-081**: The `VoicePool::processBlock()` signature MUST be extended to accept a multi-bus output structure (array of stereo buffer pointer pairs, plus a boolean array indicating which buses are active) in addition to the main stereo output pointers.
- **FR-082**: The per-pad `PadConfig` updates MUST be real-time safe. Parameter changes from the host are written to the `PadConfig` array via atomic loads (for discrete params) or plain float stores guarded by the VST3 parameter change ordering guarantee (parameter changes are processed before audio in each block). PadConfig float fields are written from `processParameterChanges()` on the audio thread. No cross-thread synchronization is needed because the host guarantees that parameter changes and `process()` occur sequentially in the same thread context.
- **FR-083**: The maximum number of output buses MUST be defined as `constexpr int kMaxOutputBuses = 16` (1 main + 15 aux).

#### Controller Changes

- **FR-090**: The controller MUST register all per-pad parameters (32 pads x 36 active parameters = 1152 parameters, using stride 64 with offsets 36-63 reserved) in addition to the existing global/proxy parameters. Parameter names MUST include the pad number for disambiguation (e.g., "Pad 01 Material", "Pad 01 Size", etc.).
- **FR-091**: The controller MUST implement "selected pad proxy" logic: when `kSelectedPadId` changes, the old global parameter IDs (100-252) MUST update their normalized values to reflect the newly selected pad's configuration. These updates are issued one-at-a-time via `performEdit`/`endEdit` — no batch suppression mechanism is required. When a global parameter is changed, the change MUST be forwarded to the corresponding per-pad parameter for the selected pad.
- **FR-092**: The controller MUST NOT register a custom UI editor in Phase 4 (host-generic editor only, same as Phase 1-3). Custom UI is Phase 6.

### Key Entities

- **PadConfig**: A pre-allocated structure holding one pad's complete configuration: exciter type, body model, 34 sound parameter values (offsets 2-35), choke group, and output bus assignment. 32 instances are stored in the voice pool.
- **Kit Preset**: A preset file containing all 32 PadConfigs plus global settings (polyphony, stealing policy). This is the primary preset format and is identical to the plugin's full state (9036 bytes, excludes selectedPadIndex).
- **Pad Preset**: A lightweight preset file containing a single pad's sound configuration (exciter type, body model, 34 sound parameters, offsets 2-35). Used for building sound libraries. Does not include choke group or output routing. Per-pad preset size: 4 (version) + 4 (exciterType) + 4 (bodyModel) + 34×8 (float64 sound params) = 284 bytes.
- **Output Bus Map**: A per-pad lookup (32 entries) mapping each pad to one of 16 output buses (0 = main only, 1-15 = main + aux). Derived from each PadConfig's output bus field.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 32 pads are independently configurable and produce spectrally distinct sounds when given different exciter/body/parameter combinations. Verified by triggering each pad and measuring that the output spectrum differs from at least 3 other pads in the default kit.
- **SC-002**: Kit preset save/load round-trips all 32 pad configurations, choke groups, and output routing bit-exactly (normalized float64 values).
- **SC-003**: Per-pad preset save/load round-trips a single pad's configuration bit-exactly and does not modify any other pad.
- **SC-004**: State v3 to v4 migration succeeds without error. Phase 3 parameters land on pad 0; pads 1-31 receive GM-inspired defaults per FR-030; output routing defaults to main-only.
- **SC-005**: Pluginval passes at strictness level 5 with 16 output buses declared (1 main + 15 aux).
- **SC-006**: 8-voice worst-case CPU usage with per-pad dispatch remains under 12% single core at 44.1 kHz (same budget as Phase 3 -- per-pad lookup adds negligible overhead over the shared-param path).
- **SC-007**: Zero audio-thread allocations across a 10-second stress test with all 32 pads triggered, voice stealing active, and choke groups engaged.
- **SC-008**: Output bus routing correctly separates pad audio: a pad assigned to aux bus N produces audio on bus N and the main bus, and zero audio on all other aux buses (verified by per-bus RMS measurement).
- **SC-009**: The default kit produces recognizable drum sounds: kick (MIDI 36) has fundamental < 100 Hz, snare (MIDI 38) has noise component > 2 kHz, closed hi-hat (MIDI 42) has spectral centroid > 4 kHz, crash (MIDI 49) has decay > 500 ms.
- **SC-010**: AU validation passes on macOS with the multi-output bus configuration (`auval -v aumu Mbrm KrAt`).
- **SC-011**: The plugin functions correctly when all auxiliary buses are deactivated (falls back to Phase 3 behavior: all pads to main stereo output).
- **SC-012**: At least 3 factory kit presets ship with the plugin and load without error.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The Phase 3 `VoicePool` with 16-voice pre-allocated pool, voice stealing, and choke groups is stable and tested. Phase 4 extends it with per-pad dispatch, not a rewrite.
- The existing `DrumVoice` class from Phase 2/3 is unchanged. Each voice instance is configured at note-on time with a specific pad's parameters.
- No custom UI is built in Phase 4. The host-generic editor exposes all parameters (which will be many -- 1152 per-pad + ~40 global). The custom VSTGUI editor in Phase 6 will provide a usable interface.
- The `Krate::Plugins::PresetManager` infrastructure is sufficient for kit presets. Per-pad presets may need a second `PresetManager` instance or a separate save/load path.
- 16 stereo output buses (1 main + 15 aux) is sufficient for professional use. Most DAWs support this configuration. The number 16 matches common drum machine conventions (e.g., Native Instruments Battery, Kontakt).
- The current state serialization approach (binary `IBStream`) extends naturally to per-pad data. Each pad's data is written sequentially.
- Factory kit presets are created by hand (parameter tuning) and stored in the plugin's resource directory for installation to `C:\ProgramData\Krate Audio\Membrum\Kit Presets\`.

### Existing Codebase Components (Principle XIV)

**Relevant existing components that MUST be reused (not re-implemented):**

| Component | Location | Relevance |
|-----------|----------|-----------|
| VoicePool | `plugins/membrum/src/voice_pool/voice_pool.h` | Core voice management. Extend with per-pad dispatch, not replace. |
| ChokeGroupTable | `plugins/membrum/src/voice_pool/choke_group_table.h` | Per-pad choke group data. Already supports 32 entries from Phase 3 state serialization. |
| VoiceMeta | `plugins/membrum/src/voice_pool/voice_meta.h` | Per-voice metadata. Needs `originatingPad` field (or derive from `originatingNote - 36`). |
| DrumVoice | `plugins/membrum/src/dsp/drum_voice.h` | The voice itself. Unchanged -- receives pad config at note-on. |
| ExciterBank | `plugins/membrum/src/dsp/exciter_bank.h` | Per-voice exciter variant dispatch. Used per-voice as before. |
| BodyBank | `plugins/membrum/src/dsp/body_bank.h` | Per-voice body model dispatch. Used per-voice as before. |
| ToneShaper | `plugins/membrum/src/dsp/tone_shaper.h` | Per-voice tone shaping. Configured per-pad at note-on. |
| VoiceCommonParams | `plugins/membrum/src/dsp/voice_common_params.h` | Existing param struct -- will evolve into PadConfig. |
| PresetManager | `plugins/shared/src/preset/preset_manager.h` | Kit preset save/load infrastructure. Configure for Membrum. |
| PresetManagerConfig | `plugins/shared/src/preset/preset_manager_config.h` | Config struct for PresetManager (processorUID, pluginName, subcategories). |
| MidiEventDispatcher | `plugins/shared/src/midi/midi_event_dispatcher.h` | MIDI dispatch. Already routes note events by pitch. |

**Initial codebase search for key terms:**

```bash
grep -r "class PadConfig" plugins/membrum/    # Should not exist yet
grep -r "kPadBaseId" plugins/membrum/         # Should not exist yet
grep -r "addAudioOutput" plugins/membrum/     # Phase 3 has 1 stereo output
grep -r "PresetManager" plugins/membrum/      # Should not exist yet
```

**Search Results Summary**: No per-pad structures, no multi-output buses, and no preset manager integration exist yet in Membrum. All of these are new in Phase 4. No ODR risk -- new types will be in the `Membrum` namespace.

### Forward Reusability Consideration

**Sibling features at same layer:**
- Phase 5 (cross-pad coupling) will need to read per-pad modal frequencies from `PadConfig` to compute sympathetic resonance between pads. The `PadConfig` structure should expose enough information for coupling calculations.
- Phase 6 (custom UI) will need to display per-pad parameters and provide pad selection UI. The `kSelectedPadId` parameter and global-to-per-pad proxy pattern from FR-012/FR-013 will be the UI's interface to pad editing.

**Potential shared components:**
- The `PadConfig` structure may be useful as a reusable pattern for other multi-voice instruments (if another plugin needs per-note configurations).
- The multi-bus output routing pattern (main + N aux buses with per-voice bus assignment) could be extracted to shared infrastructure if other plugins need it.
- The "selected item proxy" pattern (global parameter IDs that map to a selected item's parameters) could be generalized for any plugin with selectable sub-components.

## Deferred to Later Phases

- **Phase 5**: Cross-pad coupling (sympathetic resonance). PadConfig from Phase 4 provides the per-pad modal frequency data needed.
- **Phase 6**: Macro controls (Tightness, Brightness, Body Size, Punch, Complexity), Acoustic/Extended UI modes, and the custom VSTGUI editor (4x8 pad grid + pad editor + kit controls).
- **Deferred (no phase assigned)**: Snare wire modeling (FR-047 from Phase 2), sample layer, nonlinear pitch envelope on non-Membrane bodies.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it -- record the file path and line number*
3. *Run or read the test that proves it -- record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark as MET without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-040 | | |
| FR-041 | | |
| FR-042 | | |
| FR-043 | | |
| FR-044 | | |
| FR-045 | | |
| FR-046 | | |
| FR-050 | | |
| FR-051 | | |
| FR-052 | | |
| FR-053 | | |
| FR-060 | | |
| FR-061 | | |
| FR-062 | | |
| FR-063 | | |
| FR-070 | | |
| FR-071 | | |
| FR-072 | | |
| FR-073 | | |
| FR-080 | | |
| FR-081 | | |
| FR-082 | | |
| FR-083 | | |
| FR-090 | | |
| FR-091 | | |
| FR-092 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |
| SC-010 | | |
| SC-011 | | |
| SC-012 | | |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

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

**Recommendation**: [What needs to happen to achieve completion]
