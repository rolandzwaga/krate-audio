# Feature Specification: Additional Modulation Sources

**Feature Branch**: `059-additional-mod-sources`
**Created**: 2026-02-16
**Status**: Draft
**Plugin**: Ruinae (synthesizer plugin, not Iterum)
**Input**: User description: "Implement all 5 remaining modulation sources: Env Follower, Sample & Hold, Random, Pitch Follower, and Transient Detector - Phase 6 from Ruinae UI roadmap"
**Roadmap Reference**: [ruinae-ui-roadmap.md](../ruinae-ui-roadmap.md) - Phase 6 (Additional Modulation Sources)

**Note**: This spec is for the Ruinae synthesizer plugin located at `plugins/ruinae/`. The monorepo contains multiple plugins (Iterum delay, Ruinae synth, etc.). All file paths reference `plugins/ruinae/` correctly.

## Clarifications

### Session 2026-02-16

- Q: What is the exact behavior of the S&H Rate and NoteValue parameters with the Sync toggle? → A: Separate Rate param and NoteValue param with visibility switching based on Sync toggle — same pattern as LFO and Chaos (Rate knob visible when Sync=off, NoteValue dropdown visible when Sync=on, controlled via custom-view-name groups in uidesc)
- Q: Should Random source have a NoteValue parameter like LFO/Chaos/S&H, or only Rate with Sync scaling the Hz? → A: Random has 4 params total (Rate, NoteValue, Smoothness, Sync) matching the LFO/Chaos/S&H pattern with visibility switching for consistent UX across all tempo-syncable sources
- Q: What is the correct state version increment (spec assumes current=14, increment to 15)? → A: Verify current state version in processor.h during planning, then increment by 1 from actual current value (prevents conflicts if intermediate specs already incremented)
- Q: When S&H/Random Sync is on but host provides no tempo, what fallback behavior? → A: Use 120 BPM as fallback tempo (same as LFO) — NoteValue=1/4 → 0.5 sec, NoteValue=1/8 → 0.25 sec, etc. (Rate parameter value is only used when Sync=off)
- Q: What is the total parameter count across all 5 sources? → A: 18 parameters total — Env Follower (3), S&H (4), Random (4), Pitch Follower (4), Transient (3)

## Context

This spec implements **Phase 6** from the Ruinae UI roadmap: wiring up all 5 remaining modulation sources that exist in the architecture but have no parameter exposure. All five are equal priority and will be implemented together.

**Phase 0B** (Spec 053, completed) replaced the mod source tabs with a dropdown selector (`COptionMenu` + `UIViewSwitchContainer`) that already lists all 10 modulation sources. The dropdown view templates for all 5 sources (`ModSource_EnvFollower`, `ModSource_SampleHold`, `ModSource_Random`, `ModSource_PitchFollower`, `ModSource_Transient`) already exist in the uidesc as empty 158x120px containers. Selecting any of these from the dropdown already shows the empty view. This spec fills them with controls.

**All 5 DSP processors are fully implemented and integrated into the `ModulationEngine`:**

| Source | DSP Class | Location | ModulationEngine Integration |
|--------|-----------|----------|------------------------------|
| Env Follower | `EnvelopeFollower` | `dsp/include/krate/dsp/processors/envelope_follower.h` | Prepared, reset, processed per-sample via `processEnvFollowerSample()`, value returned via `getRawSourceValue()` |
| Sample & Hold | `SampleHoldSource` | `dsp/include/krate/dsp/processors/sample_hold_source.h` | Prepared, reset, processed per-block, value returned via `getRawSourceValue()` |
| Random | `RandomSource` | `dsp/include/krate/dsp/processors/random_source.h` | Prepared, reset, processed per-block, value returned via `getRawSourceValue()` |
| Pitch Follower | `PitchFollowerSource` | `dsp/include/krate/dsp/processors/pitch_follower_source.h` | Prepared, reset, processed per-block with audio input, value returned via `getRawSourceValue()` |
| Transient | `TransientDetector` | `dsp/include/krate/dsp/processors/transient_detector.h` | Prepared, reset, processed per-sample, value returned via `getRawSourceValue()` |

The `ModulationEngine` already has setter methods for all parameters of all 5 sources (e.g., `setEnvFollowerAttack()`, `setRandomRate()`, `setSampleHoldRate()`, `setPitchFollowerMinHz()`, `setTransientSensitivity()`, etc.). The `ModSource` enum already includes entries for all 5 sources. The `kModSourceStrings` dropdown already displays all 5 source names.

**What is missing (and what this spec covers):**

1. **No VST parameter IDs** in `plugin_ids.h` for any of the 5 sources
2. **No parameter registration files** in `plugins/ruinae/src/parameters/` for any of the 5 sources
3. **No processor wiring** -- parameter changes from the host/UI never reach the engine setter methods
4. **No RuinaeEngine forwarding methods** -- the engine has no passthrough methods for the 5 sources (unlike LFO, Chaos, Macros, Rungler which already have them)
5. **No control-tags** in the uidesc for any source parameters
6. **Empty mod source dropdown view templates** -- the 5 templates exist but contain no controls
7. **No state persistence** -- the parameters are not saved/loaded with presets

This is the same integration pattern established by Spec 057 (Macros & Rungler), extended to 5 more sources.

### Parameter Inventory

Based on the existing DSP class interfaces, here are the exact parameters for each source:

**Env Follower** (3 params from roadmap: Sensitivity, Attack, Release):
- Sensitivity: [0, 1] linear, default 0.5 -- scales the envelope follower output before routing (already in `ModulationEngine` as `envFollowerSensitivity_`)
- Attack: [0.1, 500] ms, default 10 ms -- how fast the envelope responds to rising amplitude
- Release: [1, 5000] ms, default 100 ms -- how fast the envelope decays when amplitude drops

**Sample & Hold** (4 params from roadmap: Rate, Sync, NoteValue, Slew):
- Rate: [0.1, 50] Hz, default 4 Hz -- how often a new value is sampled (reuses LFO rate mapping [0.01, 50] Hz with clamping to [0.1, 50] Hz)
- Sync: on/off, default off -- tempo sync toggle
- Note Value: dropdown (same note values as LFO), default 1/8 (index 10) -- note division when synced
- Slew: [0, 500] ms, default 0 -- transition smoothing between held values

Note: The roadmap says "Quantize" but the DSP class implements "Slew" (transition smoothing). Slew is the more musically useful parameter already implemented. The `SampleHoldSource` also has `setInputType()` (Random/LFO1/LFO2/External) but the roadmap lists 4 params, so InputType is omitted from this spec to match the 4-param scope. Slew replaces the roadmap's "Quantize" since the DSP has slew, not quantization.

**Random** (4 params: Rate, NoteValue, Smooth, Sync):
- Rate: [0.1, 50] Hz, default 4 Hz -- how often a new random target is generated (visible when Sync=off) (reuses LFO rate mapping [0.01, 50] Hz with clamping to [0.1, 50] Hz)
- Sync: on/off, default off -- tempo sync toggle (controls visibility + processor-level Hz computation; RandomSource built-in sync is not used)
- Note Value: dropdown (same note values as LFO), default 1/8 (index 10) -- note division when synced (visible when Sync=on)
- Smoothness: [0, 1] linear, default 0 -- how smoothly values transition (0 = instant, 1 = very smooth)

Note: The roadmap lists "Range" as a 3rd param, but the DSP class does not have a range parameter -- it always outputs [-1, +1]. Range limiting can be achieved via the mod matrix amount scaling. This spec adds NoteValue (4th param) to match the established tempo-sync pattern from LFO/Chaos/S&H for UX consistency across all tempo-syncable sources. RandomSource has built-in tempo sync methods (setTempoSync, setTempo) but they do not support note value selection; the plugin layer bypasses them and computes Hz directly from NoteValue + BPM when Sync is on.

**Pitch Follower** (2 params from roadmap: Range, Response):
- Min Hz: [20, 500] Hz, default 80 Hz -- bottom of the pitch detection frequency range
- Max Hz: [200, 5000] Hz, default 2000 Hz -- top of the pitch detection frequency range
- Confidence: [0, 1] linear, default 0.5 -- minimum detection confidence to update output
- Tracking Speed: [10, 300] ms, default 50 ms -- smoothing speed for pitch changes

Note: The roadmap lists 2 params (Range, Response), but the DSP class has 4 configurable parameters. "Range" maps to Min Hz + Max Hz (two controls defining the range endpoints). "Response speed" maps to Tracking Speed. Confidence is included as a third knob because without it, noisy input produces erratic output -- users need this control for reliable pitch following. This gives 4 parameters total (4 knobs = fits in the 158x120 view area in one row, same layout as Macros).

**Transient Detector** (3 params from roadmap: Sensitivity, Attack, Release):
- Sensitivity: [0, 1] linear, default 0.5 -- how easily transients are detected (higher = more sensitive)
- Attack: [0.5, 10] ms, default 2 ms -- how fast the output envelope ramps up on detection
- Decay: [20, 200] ms, default 50 ms -- how fast the output envelope fades after a transient (roadmap calls this "Release", DSP implementation calls it "Decay")

Note: The roadmap says "Release" but the DSP class calls it "Decay". Both mean the same thing (envelope falloff time). This spec uses "Decay" to match the DSP implementation.

### ID Range Allocation

The current `ParameterIDs` enum uses ranges up to 2299 (`kSettingsEndId`), with `kNumParameters = 2300`. This spec adds five new ranges:

- **2300-2399**: Env Follower Parameters (3 params)
- **2400-2499**: Sample & Hold Parameters (4 params)
- **2500-2599**: Random Parameters (4 params: Rate, NoteValue, Smoothness, Sync)
- **2600-2699**: Pitch Follower Parameters (4 params)
- **2700-2799**: Transient Detector Parameters (3 params)

The `kNumParameters` sentinel must increase from 2300 to 2800 to accommodate these ranges.

### Current Mod Source View Area

The mod source dropdown view area is 158x120px. Existing templates (LFO, Chaos, Macros, Rungler) use a pattern of:
- Row 1 (y=0): 28x28 knobs with 10px labels below = 38px
- Row 2 (y=42): Toggles, smaller controls, or second row of knobs

All 5 source views fit within this footprint:
- Env Follower: 3 knobs in one row
- Sample & Hold: Rate/NoteValue + Slew knob + Sync toggle (2 rows, Rate/NoteValue visibility switching)
- Random: Rate/NoteValue + Smoothness knob + Sync toggle (2 rows, Rate/NoteValue visibility switching)
- Pitch Follower: 4 knobs in one row
- Transient: 3 knobs in one row

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Envelope Follower as Reactive Modulation Source (Priority: P1)

A sound designer wants the filter cutoff to respond to the amplitude of what they play. They select "Env Follower" from the modulation source dropdown and see three knobs: Sensitivity, Attack, and Release. They set Sensitivity to 70%, Attack to 5 ms (fast response), and Release to 200 ms (slow decay). In the mod matrix, they route Env Follower to Filter Cutoff with amount +0.6. Now when they play loud notes, the filter opens up; when they play softly, the filter stays closed. The attack/release times shape how quickly the filter responds to dynamics.

**Why this priority**: Envelope following is one of the most fundamental modulation techniques. It creates direct, intuitive connections between playing dynamics and sound parameters. The DSP is fully implemented -- only the parameter wiring and UI controls are missing.

**Independent Test**: Can be fully tested by selecting "Env Follower" from the mod source dropdown, adjusting Sensitivity, adding a mod matrix route from Env Follower to any destination, playing audio, and verifying the destination parameter moves in response to amplitude.

**Acceptance Scenarios**:

1. **Given** the user selects "Env Follower" from the mod source dropdown, **When** they look at the view area, **Then** they see three knobs labeled Sens, Atk, and Rel
2. **Given** Sensitivity is at 0%, **When** audio is playing, **Then** the Env Follower output is near zero regardless of amplitude (sensitivity scales the output)
3. **Given** Sensitivity is at 100% and audio is loud, **When** a mod matrix route maps Env Follower to a destination, **Then** the destination is modulated strongly
4. **Given** Attack is set to minimum (0.1 ms), **When** a loud transient occurs, **Then** the envelope responds near-instantly
5. **Given** Release is set to 5000 ms, **When** audio stops, **Then** the envelope takes several seconds to decay to zero
6. **Given** all three parameters are configured, **When** the user saves and reloads the preset, **Then** all Env Follower values restore exactly

---

### User Story 2 - Sample & Hold for Rhythmic Random Modulation (Priority: P1)

A producer wants rhythmic, stepped modulation for a glitchy effect. They select "S&H" from the mod source dropdown and see controls for Rate, Sync toggle, Note Value dropdown, and Slew. They enable Sync and set Note Value to 1/16 notes. The S&H now generates a new random value every 16th note, creating a rhythmic stepped pattern. They add a small Slew (50 ms) to soften the transitions. They route S&H to oscillator pitch for a classic random arpeggiator-like effect.

**Why this priority**: Sample & Hold is a classic synthesis modulation source essential for rhythmic and generative patches. Tempo sync makes it immediately useful in musical contexts.

**Independent Test**: Can be tested by selecting "S&H", setting a rate, adding a mod matrix route to any parameter, and verifying the destination changes in stepped patterns at the configured rate.

**Acceptance Scenarios**:

1. **Given** the user selects "S&H" from the mod source dropdown, **When** they look at the view area, **Then** they see Rate knob, Sync toggle, Note Value dropdown, and Slew knob
2. **Given** Rate is set to 4 Hz with Sync off, **When** routed to a destination, **Then** the destination changes to a new random value approximately 4 times per second
3. **Given** Sync is enabled and Note Value is 1/4, **When** the host plays at 120 BPM, **Then** the S&H triggers every beat (every 0.5 seconds)
4. **Given** Slew is at 0 ms, **When** a new value is sampled, **Then** the transition is instant (hard step)
5. **Given** Slew is at 200 ms, **When** a new value is sampled, **Then** the transition smoothly glides to the new value over approximately 200 ms
6. **Given** all four parameters are configured, **When** the user saves and reloads the preset, **Then** all S&H values restore exactly

---

### User Story 3 - Random Source for Evolving Textures (Priority: P1)

A sound designer wants slow, organic modulation for evolving ambient textures. They select "Random" from the mod source dropdown and see Rate/NoteValue and Smooth knobs plus a Sync toggle. They enable Sync, set NoteValue to 1 bar, and Smooth to 80%. The Random source now generates slowly-changing, smoothly-interpolated values that align with the song structure. They route it to stereo width for gentle spatial movement and to filter cutoff for subtle timbral evolution.

**Why this priority**: Random modulation is essential for organic, evolving patches. Unlike S&H (which steps), Random with smoothing creates continuous, flowing modulation.

**Independent Test**: Can be tested by selecting "Random", setting Rate to a slow value with high Smooth, routing to a visible parameter, and verifying the parameter drifts smoothly over time.

**Acceptance Scenarios**:

1. **Given** the user selects "Random" from the mod source dropdown, **When** they look at the view area, **Then** they see Rate/NoteValue knob, Smooth knob, and Sync toggle
2. **Given** Rate is 1 Hz and Smooth is 0, **When** Sync is off and routed to a destination, **Then** the destination changes abruptly once per second to random values
3. **Given** Rate is 1 Hz and Smooth is 1.0, **When** routed to a destination, **Then** the destination drifts smoothly between random target values
4. **Given** Sync is enabled and NoteValue is 1/4, **When** the host plays at 120 BPM, **Then** the random source generates new targets every beat (Rate knob hides, NoteValue dropdown shows)
5. **Given** all four parameters are configured, **When** the user saves and reloads the preset, **Then** all Random values restore exactly

---

### User Story 4 - Pitch Follower for Pitch-Responsive Effects (Priority: P1)

A vocalist using a synth with sidechain input wants the filter to track their vocal pitch. They select "Pitch Follower" from the mod source dropdown and see four knobs: Min Hz, Max Hz, Confidence, and Speed. They set Min Hz to 80 Hz and Max Hz to 1000 Hz to match the vocal range. They set Confidence to 0.6 (only respond to clear pitches) and Speed to 30 ms (fast tracking). They route Pitch Follower to filter cutoff. Now the filter follows the melody of their voice.

**Why this priority**: Pitch following enables unique cross-modal modulation where the pitch of the input signal controls other parameters. The DSP (autocorrelation-based pitch detection) is fully implemented.

**Independent Test**: Can be tested by selecting "Pitch Follower", feeding a clear pitched signal (e.g., sine wave), routing to a visible parameter, and verifying the parameter value changes with pitch.

**Acceptance Scenarios**:

1. **Given** the user selects "Pitch Follower" from the mod source dropdown, **When** they look at the view area, **Then** they see four knobs labeled Min, Max, Conf, and Speed
2. **Given** Min Hz is 80 and Max Hz is 2000, **When** a 440 Hz signal is present, **Then** the output is approximately 0.5 (log-mapped midpoint of the range)
3. **Given** Confidence is set high (0.8), **When** the input is noisy or unpitched, **Then** the output holds its last valid value (does not jitter)
4. **Given** Speed is set to 10 ms, **When** the pitch changes rapidly, **Then** the output responds quickly with minimal lag
5. **Given** all four parameters are configured, **When** the user saves and reloads the preset, **Then** all Pitch Follower values restore exactly

---

### User Story 5 - Transient Detector for Rhythmic Triggering (Priority: P1)

A producer wants to create pumping effects triggered by drum hits. They select "Transient" from the mod source dropdown and see three knobs: Sensitivity, Attack, and Decay. They set Sensitivity to 60%, Attack to 1 ms, and Decay to 100 ms. They route the Transient detector to the reverb mix. Now every drum hit triggers a burst of reverb that quickly fades, creating rhythmic reverb swells.

**Why this priority**: Transient detection enables dynamic, performance-reactive effects. Combined with the mod matrix, it can create ducking, pumping, and rhythmic effects without external sidechaining.

**Independent Test**: Can be tested by selecting "Transient", setting Sensitivity to a mid value, routing to a visible parameter, playing percussive audio, and verifying the parameter spikes on transients.

**Acceptance Scenarios**:

1. **Given** the user selects "Transient" from the mod source dropdown, **When** they look at the view area, **Then** they see three knobs labeled Sens, Atk, and Decay
2. **Given** Sensitivity is at 50%, **When** a drum hit occurs, **Then** the output spikes to near 1.0 and decays back to 0
3. **Given** Sensitivity is at 0%, **When** a drum hit occurs, **Then** no transient is detected (threshold is very high)
4. **Given** Decay is set to 200 ms, **When** a transient is detected, **Then** the envelope takes approximately 200 ms to decay to near-zero
5. **Given** Attack is set to 0.5 ms, **When** a transient is detected, **Then** the envelope reaches peak almost instantly
6. **Given** all three parameters are configured, **When** the user saves and reloads the preset, **Then** all Transient values restore exactly

---

### User Story 6 - Preset Persistence and DAW Automation (Priority: P2)

A user creates a complex patch using Env Follower for filter dynamics, S&H for rhythmic pitch modulation, and Transient for reverb pumping. They save the preset. Later they reload it, and all parameter values for all 5 sources restore exactly. In their DAW, they automate the Random Rate parameter to speed up during a build section.

**Why this priority**: Persistence and automation are essential for production use but follow established patterns. All 18 new parameters use the same `kCanAutomate` flag and state persistence pattern as existing parameters.

**Independent Test**: Can be tested by configuring parameters across all 5 sources, saving a preset, loading a different preset, reloading, and verifying all values restore.

**Acceptance Scenarios**:

1. **Given** non-default values for all 18 parameters across all 5 sources, **When** the user saves and reloads the preset, **Then** all parameter values restore to their saved values
2. **Given** a preset saved before this spec (no mod source params), **When** the user loads it, **Then** all mod source parameters default to their DSP class defaults (Env Follower: Sens=0.5, Attack=10ms, Release=100ms; S&H: Rate=4Hz, Sync=off, NoteValue=1/8, Slew=0ms; Random: Rate=4Hz, Sync=off, NoteValue=1/8, Smooth=0; Pitch Follower: Min=80Hz, Max=2000Hz, Conf=0.5, Speed=50ms; Transient: Sens=0.5, Attack=2ms, Decay=50ms)
3. **Given** all 18 parameters, **When** the user opens the DAW automation lane list, **Then** all 18 are visible and automatable

---

### Edge Cases

- What happens when the Env Follower has no audio input (silence)? The envelope stays at 0. Any mod matrix routes from Env Follower produce zero offset. This is expected behavior.
- What happens when Pitch Follower receives unpitched audio (noise, drums)? The confidence drops below the threshold and the output holds its last valid pitch value. With Confidence set to 0, even low-quality detections update the output (potentially jittery). This is by design -- Confidence acts as a quality gate.
- What happens when Pitch Follower Min Hz is greater than Max Hz? The DSP class has separate clamp ranges (Min: [20, 500], Max: [200, 5000]), so overlap is possible (e.g., Min=400, Max=300). The `hzToModValue()` method handles this degenerate case by returning 0.5. Musically useless but not a crash.
- What happens when S&H Rate is at maximum (50 Hz) with Slew at 0? The output changes 50 times per second with instant transitions, creating a very fast stepped random signal.
- What happens when Transient Sensitivity is at maximum (1.0)? Very low thresholds mean even quiet signals trigger detection. In a noisy signal, this may cause near-continuous triggering (envelope stays high).
- What happens when Random Smooth is at 0 and Rate is very high? Instant transitions at high rate produce noise-like modulation. This is valid but may produce zipper artifacts on some destinations -- the mod matrix applies at block rate which limits the effective speed.
- What happens when all 5 sources are actively routed simultaneously? The `ModulationEngine` processes sources only when `sourceActive_[source]` is true, so all 5 process during `processBlock()`. CPU impact is minimal since all DSP classes are lightweight (envelope followers, random generators, simple detectors).
- What happens when S&H or Random Sync is on but no host tempo is available (0 BPM) or dropdownToDelayMs returns <=0? The processor uses a 4 Hz fallback rate (same as the default Rate value). The NoteValue setting determines the trigger interval when valid tempo is available. If tempo is unavailable, the fallback ensures continuous modulation rather than silence.
- What happens when tempo is valid but dropdownToDelayMs returns a very large value (slow tempo + long note value)? The computed rate may be extremely low (<0.1 Hz), which is valid musically (one sample every 10+ seconds). No upper limit on delay time is enforced here -- the DSP classes handle any positive rate.

## Requirements *(mandatory)*

### Functional Requirements

**Parameter ID Definitions**

- **FR-001**: A new parameter range `2300-2399` for Env Follower MUST be added to `plugin_ids.h` with the following IDs:
  - `kEnvFollowerSensitivityId = 2300` -- Sensitivity [0, 1] (default 0.5), display format "XX%" (0-100%, no decimals)
  - `kEnvFollowerAttackId = 2301` -- Attack time [0.1, 500] ms (default 10 ms), display format "X.X ms" or "XXX ms"
  - `kEnvFollowerReleaseId = 2302` -- Release time [1, 5000] ms (default 100 ms), display format "X ms" or "XXXX ms"
  - `kEnvFollowerBaseId = 2300`, `kEnvFollowerEndId = 2399`

- **FR-002**: A new parameter range `2400-2499` for Sample & Hold MUST be added to `plugin_ids.h` with the following IDs:
  - `kSampleHoldRateId = 2400` -- Rate [0.1, 50] Hz (default 4 Hz, same mapping as LFO rate), display format "X.XX Hz"
  - `kSampleHoldSyncId = 2401` -- Tempo sync on/off (default off), toggle display
  - `kSampleHoldNoteValueId = 2402` -- Note value dropdown (same entries as LFO note value), default 1/8
  - `kSampleHoldSlewId = 2403` -- Slew time [0, 500] ms (default 0 ms), display format "X ms"
  - `kSampleHoldBaseId = 2400`, `kSampleHoldEndId = 2499`

- **FR-003**: A new parameter range `2500-2599` for Random MUST be added to `plugin_ids.h` with the following IDs:
  - `kRandomRateId = 2500` -- Rate [0.1, 50] Hz (default 4 Hz, same mapping as LFO rate), display format "X.XX Hz"
  - `kRandomSyncId = 2501` -- Tempo sync on/off (default off), toggle display
  - `kRandomNoteValueId = 2502` -- Note value dropdown (same entries as LFO note value), default 1/8
  - `kRandomSmoothnessId = 2503` -- Smoothness [0, 1] (default 0), display format "XX%" (0-100%, no decimals)
  - `kRandomBaseId = 2500`, `kRandomEndId = 2599`

- **FR-004**: A new parameter range `2600-2699` for Pitch Follower MUST be added to `plugin_ids.h` with the following IDs:
  - `kPitchFollowerMinHzId = 2600` -- Min frequency [20, 500] Hz (default 80 Hz), display format "XXX Hz"
  - `kPitchFollowerMaxHzId = 2601` -- Max frequency [200, 5000] Hz (default 2000 Hz), display format "XXXX Hz"
  - `kPitchFollowerConfidenceId = 2602` -- Confidence threshold [0, 1] (default 0.5), display format "XX%" (0-100%, no decimals)
  - `kPitchFollowerSpeedId = 2603` -- Tracking speed [10, 300] ms (default 50 ms), display format "XX ms"
  - `kPitchFollowerBaseId = 2600`, `kPitchFollowerEndId = 2699`

- **FR-005**: A new parameter range `2700-2799` for Transient Detector MUST be added to `plugin_ids.h` with the following IDs:
  - `kTransientSensitivityId = 2700` -- Sensitivity [0, 1] (default 0.5), display format "XX%" (0-100%, no decimals)
  - `kTransientAttackId = 2701` -- Attack time [0.5, 10] ms (default 2 ms), display format "X.X ms"
  - `kTransientDecayId = 2702` -- Decay time [20, 200] ms (default 50 ms), display format "XXX ms"
  - `kTransientBaseId = 2700`, `kTransientEndId = 2799`

- **FR-006**: The `kNumParameters` sentinel MUST be increased from `2300` to `2800`. The ID range allocation comments at the top of the enum (both the block comment and the inline comment) MUST be updated to document all five new ranges.

**Parameter Registration**

- **FR-007**: A new `env_follower_params.h` file MUST be created in `plugins/ruinae/src/parameters/` containing an `EnvFollowerParams` struct and inline functions for registration, handling, formatting, and persistence (register, handle, format, save, load, loadToController), following the established pattern from `chaos_mod_params.h`. Parameters:
  - Sensitivity: Continuous [0, 1] with unit "%", default 0.5 (normalized 0.5)
  - Attack: `RangeParameter` with logarithmic mapping for 0.1-500 ms, unit "ms", default 10 ms
  - Release: `RangeParameter` with logarithmic mapping for 1-5000 ms, unit "ms", default 100 ms
  - All parameters MUST use `kCanAutomate`

- **FR-008**: A new `sample_hold_params.h` file MUST be created in `plugins/ruinae/src/parameters/` containing a `SampleHoldParams` struct and inline functions for registration, handling, formatting, and persistence, following the established pattern. Parameters:
  - Rate: Same logarithmic mapping as LFO rate (reuse `lfoRateFromNormalized`/`lfoRateToNormalized` from `lfo1_params.h`) with clamping to [0.1, 50] Hz in handleParamChange, unit "Hz", default 4 Hz
  - Sync: Boolean toggle (stepCount=1, default 0.0), on/off display
  - Note Value: Note value dropdown (reuse `createNoteValueDropdown()` and `kNoteValueDropdownStrings`), default 1/8 (index 10)
  - Slew: `RangeParameter` with linear mapping for 0-500 ms, unit "ms", default 0 ms
  - All parameters MUST use `kCanAutomate`

- **FR-009**: A new `random_params.h` file MUST be created in `plugins/ruinae/src/parameters/` containing a `RandomParams` struct and inline functions for registration, handling, formatting, and persistence, following the established pattern. Parameters:
  - Rate: Same logarithmic mapping as LFO rate (reuse `lfoRateFromNormalized`/`lfoRateToNormalized`) with clamping to [0.1, 50] Hz in handleParamChange, unit "Hz", default 4 Hz
  - Sync: Boolean toggle (stepCount=1, default 0.0), on/off display (controls visibility + processor-level Hz computation only; RandomSource built-in sync is not used)
  - Note Value: Note value dropdown (reuse `createNoteValueDropdown()` and `kNoteValueDropdownStrings`), default 1/8 (index 10)
  - Smoothness: Continuous [0, 1] with unit "%", default 0.0 (normalized 0.0)
  - All parameters MUST use `kCanAutomate`

- **FR-010**: A new `pitch_follower_params.h` file MUST be created in `plugins/ruinae/src/parameters/` containing a `PitchFollowerParams` struct and inline functions for registration, handling, formatting, and persistence, following the established pattern. Parameters:
  - Min Hz: `RangeParameter` with logarithmic mapping for 20-500 Hz, unit "Hz", default 80 Hz
  - Max Hz: `RangeParameter` with logarithmic mapping for 200-5000 Hz, unit "Hz", default 2000 Hz
  - Confidence: Continuous [0, 1] with unit "%", default 0.5 (normalized 0.5)
  - Tracking Speed: `RangeParameter` with linear mapping for 10-300 ms, unit "ms", default 50 ms
  - All parameters MUST use `kCanAutomate`

- **FR-011**: A new `transient_params.h` file MUST be created in `plugins/ruinae/src/parameters/` containing a `TransientParams` struct and inline functions for registration, handling, formatting, and persistence, following the established pattern. Parameters:
  - Sensitivity: Continuous [0, 1] with unit "%", default 0.5 (normalized 0.5)
  - Attack: `RangeParameter` with linear mapping for 0.5-10 ms, unit "ms", default 2 ms
  - Decay: `RangeParameter` with linear mapping for 20-200 ms, unit "ms", default 50 ms
  - All parameters MUST use `kCanAutomate`

**RuinaeEngine Forwarding Methods**

- **FR-012**: The `RuinaeEngine` MUST be extended with forwarding methods for all 5 modulation sources, following the established pattern from LFO/Chaos/Macros/Rungler. Each forwarding method calls the corresponding `ModulationEngine` setter via `globalModEngine_`. Total: 13 forwarding methods (3+2+2+4+3=14, but RandomSource built-in tempo sync methods setRandomTempoSync/setRandomTempo are NOT forwarded since sync is handled at processor level):
  - Env Follower (3): `setEnvFollowerSensitivity(float)`, `setEnvFollowerAttack(float ms)`, `setEnvFollowerRelease(float ms)`
  - Sample & Hold (2): `setSampleHoldRate(float hz)`, `setSampleHoldSlew(float ms)`
  - Random (2): `setRandomRate(float hz)`, `setRandomSmoothness(float)` — NOTE: setRandomTempoSync/setRandomTempo are NOT forwarded; Sync is handled at processor level via NoteValue-to-Hz conversion
  - Pitch Follower (4): `setPitchFollowerMinHz(float hz)`, `setPitchFollowerMaxHz(float hz)`, `setPitchFollowerConfidence(float)`, `setPitchFollowerTrackingSpeed(float ms)`
  - Transient (3): `setTransientSensitivity(float)`, `setTransientAttack(float ms)`, `setTransientDecay(float ms)`

**Processor Wiring**

- **FR-013**: The processor MUST handle Env Follower parameter changes by calling the appropriate engine forwarding methods:
  - `kEnvFollowerSensitivityId` -> `engine_.setEnvFollowerSensitivity(normalized_value)` (0-1 direct)
  - `kEnvFollowerAttackId` -> `engine_.setEnvFollowerAttack(denormalized_ms)` (log mapping, 0.1-500 ms)
  - `kEnvFollowerReleaseId` -> `engine_.setEnvFollowerRelease(denormalized_ms)` (log mapping, 1-5000 ms)

- **FR-014**: The processor MUST handle Sample & Hold parameter changes by calling the appropriate engine forwarding methods. In `applyParamsToEngine()`, when Sync is on, convert NoteValue + BPM to Hz: `rateHz = 1000.0f / dropdownToDelayMs(noteIdx, tempoBPM_)`. If tempo is invalid (0 BPM) or dropdownToDelayMs returns <=0, use 4 Hz fallback. When Sync is off, use Rate knob value clamped to [0.1, 50] Hz:
  - `kSampleHoldRateId` -> `engine_.setSampleHoldRate(std::clamp(lfoRateFromNormalized(value), 0.1f, 50.0f))` (reuse LFO rate mapping with clamping)
  - `kSampleHoldSyncId` -> stores boolean flag; no engine call (visibility switching handled by controller sub-controller)
  - `kSampleHoldNoteValueId` -> stores dropdown index; no engine call (used in applyParamsToEngine sync logic)
  - `kSampleHoldSlewId` -> `engine_.setSampleHoldSlew(denormalized_ms)` (linear 0-500 ms)

- **FR-015**: The processor MUST handle Random parameter changes by calling the appropriate engine forwarding methods. RandomSource built-in tempo sync (setRandomTempoSync/setRandomTempo) is NOT used. In `applyParamsToEngine()`, when Sync is on, convert NoteValue + BPM to Hz: `rateHz = 1000.0f / dropdownToDelayMs(noteIdx, tempoBPM_)`. If tempo is invalid (0 BPM) or dropdownToDelayMs returns <=0, use 4 Hz fallback. When Sync is off, use Rate knob value clamped to [0.1, 50] Hz:
  - `kRandomRateId` -> `engine_.setRandomRate(std::clamp(lfoRateFromNormalized(value), 0.1f, 50.0f))` (reuse LFO rate mapping with clamping)
  - `kRandomSyncId` -> stores boolean flag; no engine call (visibility switching handled by controller sub-controller)
  - `kRandomNoteValueId` -> stores dropdown index; no engine call (used in applyParamsToEngine sync logic)
  - `kRandomSmoothnessId` -> `engine_.setRandomSmoothness(normalized_value)` (0-1 direct)

- **FR-016**: The processor MUST handle Pitch Follower parameter changes by calling the appropriate engine forwarding methods:
  - `kPitchFollowerMinHzId` -> `engine_.setPitchFollowerMinHz(denormalized_hz)` (log mapping, 20-500 Hz)
  - `kPitchFollowerMaxHzId` -> `engine_.setPitchFollowerMaxHz(denormalized_hz)` (log mapping, 200-5000 Hz)
  - `kPitchFollowerConfidenceId` -> `engine_.setPitchFollowerConfidence(normalized_value)` (0-1 direct)
  - `kPitchFollowerSpeedId` -> `engine_.setPitchFollowerTrackingSpeed(denormalized_ms)` (linear 10-300 ms)

- **FR-017**: The processor MUST handle Transient parameter changes by calling the appropriate engine forwarding methods:
  - `kTransientSensitivityId` -> `engine_.setTransientSensitivity(normalized_value)` (0-1 direct)
  - `kTransientAttackId` -> `engine_.setTransientAttack(denormalized_ms)` (linear 0.5-10 ms)
  - `kTransientDecayId` -> `engine_.setTransientDecay(denormalized_ms)` (linear 20-200 ms)

**State Persistence**

- **FR-018**: All 18 new parameters MUST be saved and loaded using the established state persistence pattern. The state version constant `kCurrentStateVersion` in `plugins/ruinae/src/processor/processor.h` MUST be incremented by 1 from its current value (verify actual current version during planning — spec draft assumed 14→15 but this may have changed). The save/load functions MUST be called from the processor's `setState()`/`getState()` methods. Backward compatibility MUST be maintained: loading presets saved before this spec (state version < new version) MUST use default values for all new parameters:
  - Env Follower: Sensitivity=0.5, Attack=10ms, Release=100ms
  - Sample & Hold: Rate=4Hz, Sync=off, NoteValue=1/8, Slew=0ms
  - Random: Rate=4Hz, Sync=off, NoteValue=1/8, Smoothness=0
  - Pitch Follower: MinHz=80, MaxHz=2000, Confidence=0.5, Speed=50ms
  - Transient: Sensitivity=0.5, Attack=2ms, Decay=50ms

**Control-Tag Registration**

- **FR-019**: Control-tags for all 18 new parameters MUST be added to the uidesc control-tags section:
  - `"EnvFollowerSensitivity"` tag `"2300"`, `"EnvFollowerAttack"` tag `"2301"`, `"EnvFollowerRelease"` tag `"2302"`
  - `"SampleHoldRate"` tag `"2400"`, `"SampleHoldSync"` tag `"2401"`, `"SampleHoldNoteValue"` tag `"2402"`, `"SampleHoldSlew"` tag `"2403"`
  - `"RandomRate"` tag `"2500"`, `"RandomSync"` tag `"2501"`, `"RandomNoteValue"` tag `"2502"`, `"RandomSmoothness"` tag `"2503"`
  - `"PitchFollowerMinHz"` tag `"2600"`, `"PitchFollowerMaxHz"` tag `"2601"`, `"PitchFollowerConfidence"` tag `"2602"`, `"PitchFollowerSpeed"` tag `"2603"`
  - `"TransientSensitivity"` tag `"2700"`, `"TransientAttack"` tag `"2701"`, `"TransientDecay"` tag `"2702"`

**Mod Source View Templates**

- **FR-020**: The existing empty `ModSource_EnvFollower` template (158x120px) MUST be populated with three `ArcKnob` controls (28x28) in a single horizontal row, each bound to its respective control-tag. Layout:
  ```
  +----------------------------------+
  | (Sens)  (Atk)   (Rel)           |  y=0: 28x28 ArcKnobs
  |  Sens    Atk     Rel             |  y=28: Labels (10px)
  |                                  |
  +----------------------------------+
           158 x 120
  ```
  - Sensitivity at (4, 0), Attack at (48, 0), Release at (92, 0)
  - Each knob uses `arc-color="modulation"` and `guide-color="knob-guide"`
  - Labels: "Sens", "Atk", "Rel" (short names to fit, matching the concise label style of other mod source views)
  - Defaults: Sensitivity=0.5, Attack=normalized default for 10ms, Release=normalized default for 100ms

- **FR-021**: The existing empty `ModSource_SampleHold` template (158x120px) MUST be populated with controls in two rows. Layout:
  ```
  +----------------------------------+
  | (Rate/NoteVal)    (Slew)         |  y=0: Rate knob + Slew knob
  |  Rate              Slew          |  y=28: Labels
  |                                  |
  | [Sync]                           |  y=42: Sync toggle
  +----------------------------------+
           158 x 120
  ```
  - Row 1: Rate ArcKnob at (4, 0), Slew ArcKnob at (80, 0), both 28x28
  - Row 1 also has a NoteValue dropdown (hidden when Sync is off, visible when Sync is on) in the same position as Rate (following the Chaos template pattern with custom-view-name groups for visibility switching)
  - Row 2 (y=42): Sync ToggleButton at (2, 42)
  - Labels: "Rate" (or "Note" when synced), "Slew"
  - Rate/NoteValue visibility switching follows the same `custom-view-name` group pattern as the Chaos template (`SHRateGroup`/`SHNoteValueGroup`)

- **FR-022**: The existing empty `ModSource_Random` template (158x120px) MUST be populated with controls in two rows. Layout:
  ```
  +----------------------------------+
  | (Rate/NoteVal)  (Smooth)         |  y=0: Rate knob + Smooth knob
  |  Rate            Smooth          |  y=28: Labels
  |                                  |
  | [Sync]                           |  y=42: Sync toggle
  +----------------------------------+
           158 x 120
  ```
  - Row 1: Rate ArcKnob at (4, 0), Smoothness ArcKnob at (80, 0), both 28x28
  - Row 1 also has a NoteValue dropdown (hidden when Sync is off, visible when Sync is on) in the same position as Rate (following the Chaos/S&H template pattern with custom-view-name groups for visibility switching)
  - Row 2 (y=42): Sync ToggleButton at (2, 42)
  - Labels: "Rate" (or "Note" when synced), "Smooth"
  - Rate/NoteValue visibility switching follows the same `custom-view-name` group pattern as the Chaos/S&H templates (`RandomRateGroup`/`RandomNoteValueGroup`)
  - Defaults: Rate=normalized default for 4Hz, NoteValue=1/8, Smoothness=0.0

- **FR-023**: The existing empty `ModSource_PitchFollower` template (158x120px) MUST be populated with four `ArcKnob` controls (28x28) in a single horizontal row. Layout:
  ```
  +----------------------------------+
  | (Min)  (Max)  (Conf)  (Speed)   |  y=0: 28x28 ArcKnobs
  |  Min    Max    Conf    Speed     |  y=28: Labels
  |                                  |
  +----------------------------------+
           158 x 120
  ```
  - Min Hz at (4, 0), Max Hz at (42, 0), Confidence at (80, 0), Speed at (118, 0)
  - Labels: "Min", "Max", "Conf", "Speed"
  - Defaults: MinHz=normalized for 80Hz, MaxHz=normalized for 2000Hz, Confidence=0.5, Speed=normalized for 50ms

- **FR-024**: The existing empty `ModSource_Transient` template (158x120px) MUST be populated with three `ArcKnob` controls (28x28) in a single horizontal row. Layout:
  ```
  +----------------------------------+
  | (Sens)  (Atk)   (Decay)         |  y=0: 28x28 ArcKnobs
  |  Sens    Atk     Decay           |  y=28: Labels
  |                                  |
  +----------------------------------+
           158 x 120
  ```
  - Sensitivity at (4, 0), Attack at (48, 0), Decay at (92, 0)
  - Labels: "Sens", "Atk", "Decay"
  - Defaults: Sensitivity=0.5, Attack=normalized for 2ms, Decay=normalized for 50ms

**No Layout Changes**

- **FR-025**: No window size changes, row repositioning, or FieldsetContainer modifications are required. All 5 source views fill existing empty templates within the already-allocated 158x120px mod source view area. No changes to the mod source dropdown list or `kModSourceStrings` are required (all 5 sources are already listed).

### Key Entities

- **Env Follower Parameters**: Three controls (Sensitivity, Attack, Release) that configure how the amplitude of the audio input is tracked for modulation. Sensitivity scales the output level. Attack and Release shape the envelope timing.
- **Sample & Hold Parameters**: Four controls (Rate, Sync, NoteValue, Slew) that configure periodic sampling of a random value. Rate/NoteValue set the trigger frequency (free or tempo-synced) with visibility switching. Slew smooths transitions between held values.
- **Random Parameters**: Four controls (Rate, Sync, NoteValue, Smoothness) that configure continuous random modulation generation. Rate/NoteValue set how often new random targets are generated (free or tempo-synced) with visibility switching. Smoothness controls interpolation between targets.
- **Pitch Follower Parameters**: Four controls (Min Hz, Max Hz, Confidence, Speed) that configure pitch detection and mapping. Min/Max Hz define the frequency range mapped to [0, 1]. Confidence gates unreliable detections. Speed controls response smoothing.
- **Transient Parameters**: Three controls (Sensitivity, Attack, Decay) that configure onset detection and envelope generation. Sensitivity sets the detection threshold. Attack and Decay shape the triggered envelope.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can select "Env Follower" from the mod source dropdown and see three functional knobs (Sens, Atk, Rel) that configure envelope following, verified by adjusting parameters and observing a mod matrix route from Env Follower responding to audio amplitude.
- **SC-002**: Users can select "S&H" from the mod source dropdown and see functional Rate, Sync, NoteValue, and Slew controls, verified by adjusting Rate and observing a mod matrix route from S&H producing stepped random values at the configured rate.
- **SC-003**: Users can select "Random" from the mod source dropdown and see functional Rate/NoteValue, Smooth, and Sync controls with visibility switching, verified by adjusting parameters and observing a mod matrix route from Random producing smoothly-varying random values.
- **SC-004**: Users can select "Pitch Follower" from the mod source dropdown and see four functional knobs (Min, Max, Conf, Speed), verified by feeding pitched audio and observing a mod matrix route from Pitch Follower tracking the pitch.
- **SC-005**: Users can select "Transient" from the mod source dropdown and see three functional knobs (Sens, Atk, Decay), verified by feeding percussive audio and observing a mod matrix route from Transient producing spike envelopes on transients.
- **SC-006**: All 18 new parameters persist correctly across preset save/load cycles, verified by setting non-default values for all 18 parameters, saving, loading a different preset, then reloading and confirming all values match.
- **SC-007**: The plugin passes pluginval at strictness level 5, confirming all new parameters are accessible, automatable, and the state save/load cycle works correctly.
- **SC-008**: All existing parameters and controls continue to function identically after the changes, verified by existing tests passing with zero regressions.
- **SC-009**: The plugin builds with zero compiler warnings related to the changes in this spec.
- **SC-010**: Presets saved before this spec load correctly with all mod source parameters defaulting to their DSP class defaults (per FR-018 backward compatibility defaults).
- **SC-011**: All 18 new parameters are visible in DAW automation lanes and respond to automation playback.
- **SC-012**: When all 5 modulation sources are actively routed simultaneously to different destinations, the plugin CPU usage remains below 5% single core at 44.1kHz stereo, and audio playback is glitch-free.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- All 5 DSP processors are fully implemented and tested in the KrateDSP library. No DSP algorithm changes are needed for this spec. Only parameter wiring, engine forwarding, and UI controls are being added.
- The `ModulationEngine` already has setter methods for all parameters of all 5 sources. These have been wired to `getRawSourceValue()` since the initial modulation system spec (008). The sources process during `processBlock()` when active.
- The `RuinaeEngine` needs new forwarding methods (similar to how it already forwards for LFO, Chaos, Macros, Rungler) because the processor interacts with `RuinaeEngine`, not directly with `ModulationEngine`.
- The mod source dropdown view switch (`UIViewSwitchContainer`) is already functional with 10 template entries. The 5 empty templates (`ModSource_EnvFollower`, etc.) are already wired to the view switch at positions 5-9. No changes to the switching mechanism are needed.
- The S&H Rate and Random Rate parameters use the same logarithmic mapping as LFO Rate. The `lfoRateFromNormalized()` / `lfoRateToNormalized()` functions from `lfo1_params.h` can be directly reused.
- The S&H Note Value parameter uses the same dropdown entries as LFO Note Value. The `createNoteValueDropdown()` helper and `kNoteValueDropdownStrings` from `note_value_ui.h` can be directly reused.
- The S&H Sync and Random Sync toggle patterns follow the same pattern as Chaos Sync and LFO Sync (a toggle button that switches Rate knob visibility to NoteValue dropdown visibility). The `custom-view-name` group pattern from the Chaos template (`ChaosRateGroup`/`ChaosNoteValueGroup`) should be replicated for S&H.
- The Env Follower in the `ModulationEngine` has a source type parameter (`EnvFollowerSourceType`: InputL, InputR, InputSum, Mid, Side). This spec does not expose the source type as a UI parameter -- the default InputSum is used. Adding source type selection is deferred to a future spec to keep the view at 3 knobs as per the roadmap.
- The S&H `SampleHoldInputType` (Random, LFO1, LFO2, External) is not exposed in this spec. The default (Random) is used. Adding source type selection is deferred to a future spec to keep the view at 4 controls as per the roadmap.
- Backward compatibility for state loading only requires EOF-safe defaults for new parameters. No enum renumbering or migration is needed (unlike Spec 057 which had to renumber the ModSource enum). All `ModSource` entries are already in their final positions.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `EnvelopeFollower` class | `dsp/include/krate/dsp/processors/envelope_follower.h` | Fully implemented DSP. Used by `ModulationEngine`. No changes needed. |
| `SampleHoldSource` class | `dsp/include/krate/dsp/processors/sample_hold_source.h` | Fully implemented DSP. Uses `ModulationSource` interface. No changes needed. |
| `RandomSource` class | `dsp/include/krate/dsp/processors/random_source.h` | Fully implemented DSP. Uses `ModulationSource` interface. No changes needed. |
| `PitchFollowerSource` class | `dsp/include/krate/dsp/processors/pitch_follower_source.h` | Fully implemented DSP. Uses `ModulationSource` interface. No changes needed. |
| `TransientDetector` class | `dsp/include/krate/dsp/processors/transient_detector.h` | Fully implemented DSP. Uses `ModulationSource` interface. No changes needed. |
| `ModulationEngine` setters | `modulation_engine.h:431-513` | All setter methods already exist (setEnvFollowerAttack, setRandomRate, setSampleHoldRate, setPitchFollowerMinHz, setTransientSensitivity, etc.). |
| `ModulationEngine::getRawSourceValue()` | `modulation_engine.h:627-660` | Already returns values for all 5 sources via their `getCurrentValue()` methods. |
| `ModSource` enum | `modulation_types.h:35-50` | Already includes EnvFollower=3, Random=4, SampleHold=11, PitchFollower=12, Transient=13. No changes needed. |
| `kModSourceStrings` | `dropdown_mappings.h:172-187` | Already includes all 5 source display names. No changes needed. |
| `RuinaeEngine` | `plugins/ruinae/src/engine/ruinae_engine.h` | Missing forwarding methods for the 5 sources. Must add ~15 new forwarding methods. |
| `lfoRateFromNormalized()` / `lfoRateToNormalized()` | `lfo1_params.h` | Reuse for S&H Rate and Random Rate parameter mapping. |
| `createNoteValueDropdown()` | `note_value_ui.h` | Reuse for S&H Note Value dropdown. |
| `kNoteValueDropdownStrings` / `kNoteValueDropdownCount` | `note_value_ui.h` | Reuse for S&H Note Value dropdown entries. |
| `chaos_mod_params.h` Sync/NoteValue pattern | `chaos_mod_params.h:35-41` | Reference for S&H Sync + NoteValue handling pattern. |
| `ModSource_Chaos` template | `editor.uidesc:1698-1749` | Reference for Rate/NoteValue visibility switching pattern (`custom-view-name` groups). |
| `macro_params.h` / `rungler_params.h` | `plugins/ruinae/src/parameters/` | Reference pattern for new param file structure (register, handle, format, save, load, loadToController). |
| Empty mod source templates | `editor.uidesc:1820-1824` | 5 empty 158x120 CViewContainers to be populated. |
| Control-tags section | `editor.uidesc:65-217` | Add 17 new control-tags here. |
| `kCurrentStateVersion = 14` | `processor.h:71` | Must increment to 15 for new state fields. |
| `EnvFollowerSourceType` enum | `modulation_types.h:137-143` | Not exposed as UI param (default InputSum). Documented for future spec. |
| `SampleHoldInputType` enum | `modulation_types.h:153-158` | Not exposed as UI param (default Random). Documented for future spec. |

**Initial codebase search for key terms:**

```bash
grep -r "kEnvFollowerSensitivityId\|kSampleHoldRateId\|kRandomRateId\|kPitchFollowerMinHzId\|kTransientSensitivityId" plugins/ruinae/src/plugin_ids.h
# Result: No matches -- no mod source param IDs exist

grep -r "env_follower_params\|sample_hold_params\|random_params\|pitch_follower_params\|transient_params" plugins/ruinae/src/parameters/
# Result: No matches -- no mod source param files exist

grep -r "EnvFollowerSensitivity\|SampleHoldRate\|RandomRate\|PitchFollowerMinHz\|TransientSensitivity" plugins/ruinae/resources/editor.uidesc
# Result: No matches -- no mod source control-tags exist

grep -r "setEnvFollowerSensitivity\|setRandomRate\|setSampleHoldRate\|setPitchFollowerMinHz\|setTransientSensitivity" plugins/ruinae/src/engine/ruinae_engine.h
# Result: No matches -- no RuinaeEngine forwarding methods exist for these 5 sources
```

**Search Results Summary**: No parameter IDs, parameter files, control-tags, or RuinaeEngine forwarding methods exist for any of the 5 modulation sources. The DSP classes are fully implemented and integrated into `ModulationEngine` with setter methods, but the plugin layer has no way to expose them to users. All gaps are confirmed and addressed by this spec.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- **Advanced Env Follower Source Selection**: Future spec could add an `EnvFollowerSourceType` dropdown (InputL/InputR/InputSum/Mid/Side) as a 4th parameter in the Env Follower view
- **Advanced S&H Input Type**: Future spec could add a `SampleHoldInputType` dropdown (Random/LFO1/LFO2/External) as a 5th parameter in the S&H view
- **Additional Rungler/Chaos Features**: Spec 057 noted Clock Divider and Slew for Rungler as future work. No interaction with this spec.

**Potential shared components** (preliminary, refined in plan.md):
- The parameter registration pattern (struct + register + handle + format + save + load + loadToController) is identical across all 5 param files. A template or macro could reduce boilerplate, but the existing project style uses explicit inline functions per file, so this spec follows the same convention.
- The `lfoRateFromNormalized()` / `lfoRateToNormalized()` functions are already shared between LFO1, LFO2, Chaos, and will now also be used by S&H and Random. No new shared component needed.
- The S&H Rate/NoteValue visibility switching pattern is identical to the Chaos Rate/NoteValue pattern. The sub-controller logic for toggling visibility groups follows the same mechanism. If a third source needs this pattern in the future, extracting a shared "SyncableRateGroup" widget might be warranted, but for now the duplication is minimal.

## Dependencies

This spec depends on:
- **053-mod-source-dropdown** (completed, merged): Mod source dropdown selector and empty view templates for all 10 sources
- **008-modulation-system** (completed, merged): All 5 DSP processor classes and `ModulationEngine` integration
- **057-macros-rungler** (completed, merged): Established the parameter wiring pattern for mod sources

This spec enables:
- **Advanced Env Follower Configuration**: Future spec could add Source Type dropdown
- **Advanced S&H Configuration**: Future spec could add Input Type dropdown
- **Complete Ruinae UI**: This is the final Phase 6 spec, completing the Ruinae UI roadmap

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
| FR-024 | | |
| FR-025 | | |
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

**Recommendation**: [What needs to happen to achieve completion]
