# Feature Specification: Innexus ADSR Envelope Detection & Application

**Feature Branch**: `124-adsr-envelope-detection`
**Plugin**: Innexus
**Created**: 2026-03-08
**Status**: Complete
**Input**: User description: "Add automatic ADSR envelope detection to Innexus's sample analysis pipeline with editable parameters and global envelope application"

## Clarifications

### Session 2026-03-08

- Q: Is the ADSR envelope monophonic (one shared instance) or polyphonic (per-voice instance)? → A: Monophonic only — one shared ADSR instance that retriggers (hard retrigger) on every new note-on.
- Q: Which amplitude signal should the envelope detector use per HarmonicFrame? → A: Use `HarmonicFrame.globalAmplitude` directly (pre-computed smoothed RMS, includes full signal energy).
- Q: Should ADSR time parameters (Attack, Decay, Release) be interpolated linearly in milliseconds or logarithmically (geometric mean) during morph/evolution? → A: Logarithmic (geometric mean) — midpoint between 10ms and 500ms is ~71ms. Sustain level and curve amounts interpolate linearly (they are ratios, not times).
- Q: What algorithm defines "steady-state" onset for Sustain detection in FR-002? → A: Least-squares slope over a sliding window (valid size range: 8–20 frames; a fixed value within this range such as 12 satisfies the requirement) with a variance guard: steady-state when BOTH |slope| < 0.0005/frame AND variance < 0.002. An O(1) rolling least-squares algorithm (no per-frame recomputation) is the recommended implementation technique. Exact thresholds are tuned to frame rate, envelope smoothing, and signal normalization.
- Q: Should the ADSR curve shape (exponential, linear, etc.) be fixed internally or user-exposed? → A: User-exposed per segment — three additional parameters: Attack Curve Amount, Decay Curve Amount, Release Curve Amount (each −1.0 to +1.0, where −1=logarithmic, 0=linear, +1=exponential). The ADSRDisplay component already supports curve dragging natively. Total new parameter count is 9.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Auto-Detect Envelope from Analyzed Sample (Priority: P1)

A sound designer loads a piano sample into Innexus. The system analyzes the sample and automatically extracts ADSR parameters from the amplitude contour. The detected Attack, Decay, Sustain, and Release values populate the corresponding knobs. When the user plays a MIDI note, the resynthesized output follows the natural dynamics of the original piano note rather than sounding like a sustained organ tone.

**Why this priority**: This is the core value proposition. Without envelope detection, the feature has no auto-populate capability and users must manually dial in ADSR values by ear, defeating the purpose.

**Independent Test**: Load a sample with a clear percussive envelope (fast attack, moderate decay, low sustain). Verify detected ADSR values match the expected contour within tolerance. Play a MIDI note and confirm the output amplitude follows the ADSR shape.

**Acceptance Scenarios**:

1. **Given** a piano sample is loaded, **When** analysis completes, **Then** Attack time is detected in the range 1-50ms, Decay time is detected as a positive value, Sustain level is below 0.5 (piano has significant decay), and Release time is a positive value.
2. **Given** a pad/drone sample is loaded, **When** analysis completes, **Then** Attack time is longer (>50ms), Sustain level is high (>0.7), reflecting the sustained nature of the sound.
3. **Given** analysis completes, **When** the user plays a MIDI note, **Then** the output amplitude follows the detected ADSR envelope shape, with note-on triggering the attack phase and note-off triggering the release phase.

---

### User Story 2 - Edit ADSR Parameters After Detection (Priority: P1)

After the system auto-detects ADSR values, the user wants to reshape the dynamics. They increase the attack time to create a swelling effect, lower the sustain level for a more percussive character, and extend the release for a lingering tail. The Envelope Amount knob lets them blend between the flat organ-like sound (0.0) and the full ADSR-shaped output (1.0). The Time Scale knob stretches the entire envelope proportionally. The Attack Curve, Decay Curve, and Release Curve knobs — or dragging directly on the ADSR display — shape the curvature of each segment independently.

**Why this priority**: Equally critical to detection -- the detected values are a starting point; user editing is the creative tool.

**Independent Test**: Set known ADSR values manually (ignoring detection), play a MIDI note, and confirm the output follows the manually-set envelope. Adjust Envelope Amount and verify blending behavior. Drag a curve segment on the ADSRDisplay and verify the corresponding Curve Amount parameter updates.

**Acceptance Scenarios**:

1. **Given** ADSR values are populated, **When** the user adjusts Attack time from 10ms to 500ms, **Then** the attack phase duration changes audibly and the display updates.
2. **Given** Envelope Amount is set to 0.0, **When** a note is played, **Then** the output is identical to the current behavior (no envelope shaping, bit-exact bypass).
3. **Given** Envelope Amount is set to 1.0, **When** a note is played, **Then** the output is fully shaped by the ADSR envelope.
4. **Given** Time Scale is set to 2.0x, **When** a note is played, **Then** Attack, Decay, and Release durations are all doubled proportionally.
5. **Given** Attack Curve Amount is set to +1.0 (exponential), **When** a note is played, **Then** the attack segment follows an exponential rise; set to −1.0 (logarithmic) the attack follows a logarithmic rise.

---

### User Story 3 - Per-Slot ADSR Storage & Recall (Priority: P2)

A user captures a harmonic memory snapshot to Slot 3 while using a slow-attack pad envelope. They then switch to Slot 1 which has a sharp, percussive envelope. When morphing between slots, the ADSR parameters interpolate smoothly alongside the harmonic content, creating a gradual transition from percussive to pad dynamics.

**Why this priority**: Memory slots are a core Innexus feature. ADSR values must participate in the snapshot/recall/morph system for consistency, but the feature is usable without this (users can manually adjust ADSR per recall).

**Independent Test**: Capture ADSR values to two different memory slots with contrasting envelopes. Recall each slot and verify ADSR knobs update. Set morph position to 0.5 and verify interpolated values.

**Acceptance Scenarios**:

1. **Given** ADSR values are {A=10, D=100, S=0.3, R=200}, **When** the user captures to Slot 1, **Then** those ADSR values are stored alongside the harmonic snapshot.
2. **Given** Slot 1 has {A=10ms, D=100ms, S=0.3, R=200ms} and Slot 2 has {A=500ms, D=50ms, S=0.9, R=1000ms}, **When** morphing at position 0.5, **Then** the effective ADSR values are approximately {A=71ms, D=71ms, S=0.6, R=447ms} — Attack, Decay, and Release use geometric mean interpolation (sqrt(a*b)); Sustain uses linear mean ((0.3+0.9)/2=0.6).
3. **Given** the evolution engine is cycling through occupied slots, **When** it transitions between slots, **Then** ADSR parameters evolve alongside harmonic content using geometric mean interpolation for time parameters and linear interpolation for Sustain and curve amounts.

---

### User Story 4 - ADSR Visualization (Priority: P2)

The user sees a visual ADSR display showing the current envelope shape including segment curvature. They can drag control points to adjust Attack time, Decay time, Sustain level, and Release time directly on the display, and drag the curve segments themselves to adjust curve amounts. A playback dot shows the current position in the envelope during note playback.

**Why this priority**: Visual feedback significantly improves usability and creative workflow, but the feature is functional without it (knobs alone suffice).

**Independent Test**: Display the ADSRDisplay view, drag control points and curve segments, and verify the corresponding parameter values update. Play a note and verify the playback dot tracks the envelope position.

**Acceptance Scenarios**:

1. **Given** the ADSR display is visible, **When** the user drags the peak control point horizontally, **Then** the Attack time parameter updates in real time.
2. **Given** a note is active, **When** the ADSR display is visible, **Then** a playback dot animates along the envelope curve showing the current stage.
3. **Given** ADSR parameters change (via detection or knob), **When** the display is visible, **Then** the envelope curve updates immediately.
4. **Given** the user drags a curve segment on the ADSRDisplay, **When** the drag completes, **Then** the corresponding Curve Amount parameter (Attack, Decay, or Release) updates to reflect the new curvature.

---

### User Story 5 - State Persistence & Backward Compatibility (Priority: P3)

A user saves a project with ADSR parameters configured. When reopening the project, all ADSR values and per-slot data are restored. When opening an older project (v1-v8), the plugin loads without errors and defaults to Envelope Amount 0.0, preserving the original organ-like behavior.

**Why this priority**: Essential for production use but lower priority for initial development since it can be added after core functionality works.

**Independent Test**: Save state, reload, verify all ADSR parameters match. Load a v8 state and verify Envelope Amount defaults to 0.0 with no errors.

**Acceptance Scenarios**:

1. **Given** ADSR parameters are configured, **When** the host saves and reloads the project, **Then** all ADSR values (including per-slot data and curve amounts) are restored exactly.
2. **Given** a v8 (or earlier) saved state, **When** loading the state in the updated plugin, **Then** Envelope Amount defaults to 0.0, all ADSR times default to reasonable values, curve amounts default to 0.0 (linear), and playback is unaffected.

---

### Edge Cases

- What happens when a sample has no detectable attack transient (e.g., pure sine wave)? The envelope detector produces sensible defaults: Attack ~5ms, Sustain ~1.0, Decay ~50ms, Release ~100ms.
- What happens when a sample is extremely short (< 50ms)? Detection still produces valid ADSR values, with times clamped to minimum values (1ms).
- What happens when Envelope Amount transitions from 0.0 to 1.0 during an active note? The envelope gain blends smoothly via parameter smoothing, without clicks or discontinuities.
- What happens when the input source is switched to Sidechain? Envelope detection does NOT run; existing ADSR parameters remain unchanged and editable.
- What happens when all 8 memory slots have different ADSR values and the evolution engine is active? ADSR interpolation is smooth and continuous across all slot transitions, using geometric mean for time parameters and linear for Sustain and curve amounts.
- What happens when Time Scale is at its extremes (0.25x or 4.0x)? The effective times are computed as `time * scale` and clamped to the valid 1-5000ms range.
- What happens when a sample with no amplitude variation is loaded (constant tone)? Detection yields Attack ~5ms, Sustain ~1.0, providing a flat envelope that preserves the original behavior.
- What happens when a new note-on arrives while a note is held? The single shared ADSR instance hard-retriggers — output resets to the attack stage immediately regardless of current stage.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST extract an amplitude envelope from the harmonic frame sequence during sample analysis (not sidechain mode) by reading `HarmonicFrame.globalAmplitude` for each frame — no recomputation of RMS from partial amplitudes is required
- **FR-002**: System MUST fit the extracted amplitude contour to ADSR segments using the following algorithm: (1) Attack = time from first frame to peak frame; (2) Decay = time from peak frame until steady-state is detected; (3) steady-state is defined as the first frame where BOTH conditions hold over a sliding window: |least-squares slope| < 0.0005/frame AND variance < 0.002 (an O(1) rolling least-squares algorithm is used — no per-frame recomputation); the sliding window size MUST be in the range 8–20 frames; a fixed value within this range (e.g., 12) satisfies the requirement — the range is the valid tuning space, not a runtime-adaptive constraint; (4) Sustain = mean `globalAmplitude` over the steady-state region, expressed as a ratio relative to the peak (0.0–1.0); (5) Release = time from the last steady-state frame (inclusive) to the end of the contour, or default 100ms if no second inflection is detected; exact slope/variance thresholds are tuned to the analysis frame rate and signal normalization
- **FR-003**: System MUST auto-populate ADSR parameter values upon every new sample load
- **FR-004**: System MUST provide user-editable Attack time parameter (1-5000ms range)
- **FR-005**: System MUST provide user-editable Decay time parameter (1-5000ms range)
- **FR-006**: System MUST provide user-editable Sustain level parameter (0.0-1.0 range)
- **FR-007**: System MUST provide user-editable Release time parameter (1-5000ms range)
- **FR-008**: System MUST provide an Envelope Amount parameter (0.0-1.0) that blends between flat output and full ADSR shaping. The blend formula is `lerp(1.0, envelope, amount)`, meaning the effective gain is `(1 - amount) + amount * envelopeValue`. At Amount=0.0 the gain is always 1.0 (flat, bit-exact bypass per FR-009). At Amount=1.0 the gain equals the raw envelope output (0→1 during attack, sustain level during hold, 0 during silent release). At intermediate Amount values the output is never fully silent even when the envelope reaches zero — this is intentional and preserves signal continuity.
- **FR-009**: System MUST produce bit-exact bypass when Envelope Amount is 0.0 (matching current organ-like behavior)
- **FR-010**: System MUST provide an Envelope Time Scale parameter (0.25-4.0x) that proportionally stretches or compresses Attack, Decay, and Release without changing Sustain level. Effective time = `param_time_ms * scale`, clamped independently to [1, 5000]ms per segment. When clamping occurs at extreme Time Scale values, the relative shape ratios among time segments may not be exactly preserved — this is acceptable behavior.
- **FR-011**: System MUST apply the ADSR envelope as a global amplitude multiplier to the combined oscillator bank and residual output; the plugin is monophonic — a single shared ADSR instance is used, not one per voice
- **FR-012**: System MUST trigger the attack phase (hard retrigger) on every MIDI note-on and the release phase on MIDI note-off; a new note-on while a note is held resets the envelope to the attack stage immediately
- **FR-013**: Each of the 8 memory slots MUST store its own set of ADSR parameters alongside the harmonic snapshot, including all 9 user parameters: Attack, Decay, Sustain, Release, Amount, Time Scale, Attack Curve Amount, Decay Curve Amount, Release Curve Amount
- **FR-014**: Capturing a memory slot MUST include all 9 current ADSR parameter values
- **FR-015**: Recalling a memory slot MUST restore all 9 ADSR parameter values
- **FR-016**: The morph engine MUST interpolate ADSR parameters between occupied slots using logarithmic (geometric mean) interpolation for Attack, Decay, and Release times, and linear interpolation for Sustain level, Envelope Amount, and the three Curve Amount parameters
- **FR-017**: The evolution engine MUST interpolate ADSR parameters between slots during evolution traversal using the same interpolation domain as FR-016: geometric mean for time parameters, linear for Sustain, Amount, and curve amounts
- **FR-018**: System MUST visualize the ADSR envelope using the existing shared ADSRDisplay component with draggable control points, draggable curve segments (wired to Curve Amount parameters), and a playback dot
- **FR-019**: State serialization MUST use a new version (v9) that stores all 9 ADSR parameters and per-slot ADSR data
- **FR-020**: State deserialization MUST be backward compatible with v1-v8 states, defaulting Envelope Amount to 0.0 and all curve amounts to 0.0 (linear)
- **FR-021**: All envelope computation on the audio thread MUST be real-time safe (no allocations, no locks, no I/O)
- **FR-022**: Envelope detection MUST NOT trigger when the input source is set to Sidechain
- **FR-023**: The ADSR envelope gain MUST transition smoothly when Envelope Amount changes during an active note (no clicks or discontinuities)
- **FR-024**: System MUST provide user-editable Attack Curve Amount parameter (−1.0 to +1.0; −1=logarithmic, 0=linear, +1=exponential), wired to the ADSRDisplay attack curve segment
- **FR-025**: System MUST provide user-editable Decay Curve Amount parameter (−1.0 to +1.0), wired to the ADSRDisplay decay curve segment
- **FR-026**: System MUST provide user-editable Release Curve Amount parameter (−1.0 to +1.0), wired to the ADSRDisplay release curve segment

### Key Entities

- **ADSR Parameters**: A set of 9 user-editable values — Attack time (ms), Decay time (ms), Sustain level (0–1), Release time (ms), Envelope Amount (0–1), Time Scale (0.25–4x), Attack Curve Amount (−1 to +1), Decay Curve Amount (−1 to +1), Release Curve Amount (−1 to +1) — representing the complete global amplitude envelope configuration
- **Envelope Detector**: An analysis-time algorithm that reads `HarmonicFrame.globalAmplitude` per frame and fits the resulting contour to ADSR segments using peak-finding and O(1) rolling least-squares steady-state detection
- **Global Envelope**: A single shared amplitude multiplier driven by the ADSR parameters, applied after oscillator bank + residual synthesis (monophonic; hard-retriggers on every note-on)
- **Memory Slot ADSR Data**: Per-slot storage of all 9 ADSR parameters extending the existing MemorySlot structure

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Envelope detection for a typical sample (< 5 seconds, 44.1kHz) completes within the existing sample analysis time without adding more than 10% overhead
- **SC-002**: Detected ADSR parameters for a percussive sample (e.g., piano) yield Attack < 50ms and Sustain < 0.5; for a sustained sample (e.g., pad) yield Sustain > 0.7
- **SC-003**: With Envelope Amount at 0.0, the output is bit-exact identical to the output without the ADSR feature (zero regression)
- **SC-004**: ADSR parameter changes (including Time Scale, Amount, and Curve Amounts) produce smooth, click-free transitions during active notes
- **SC-005**: The ADSR envelope processing adds less than 0.1% CPU overhead per voice at 44.1kHz
- **SC-006**: Older saved states (v1-v8) load without errors, default Envelope Amount to 0.0, and default all curve amounts to 0.0
- **SC-007**: All 9 new parameters (Attack, Decay, Sustain, Release, Amount, Time Scale, Attack Curve Amount, Decay Curve Amount, Release Curve Amount) are automatable and respond correctly to host automation

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The amplitude envelope is extracted from `HarmonicFrame.globalAmplitude` (the pre-computed smoothed RMS already present in each frame), not recomputed from partial amplitudes; this captures the full signal energy including residual and is consistent with how the plugin perceives loudness during playback
- A peak-finding and O(1) rolling least-squares steady-state detection algorithm is sufficient for ADSR fitting; complex machine-learning approaches are not needed
- The existing 1-5000ms time range for ADSR parameters covers practical musical use cases
- Memory slots currently store only `HarmonicSnapshot` and an `occupied` flag; extending this struct is safe for all consumers
- The existing `kReleaseTimeId` (ID 200) serves a different purpose (oscillator release fade) and the new ADSR Release is a distinct, additional parameter
- Envelope Amount default of 0.0 for new instances ensures the plugin behaves identically to the current version until the user explicitly enables envelope shaping
- Curve Amount defaults of 0.0 (linear) for new instances and v1-v8 loaded states provide neutral behavior consistent with the prior organ-like output

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `ADSREnvelope` (Layer 1 DSP) | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | MUST reuse for audio-thread envelope generation. Supports setAttack/Decay/Sustain/Release, gate(), process(), getOutput(), getStage(). Curve shaping via setAttackCurve(float)/setDecayCurve(float)/setReleaseCurve(float) overloads accepting continuous −1 to +1 curve amounts — maps directly to FR-024/025/026. |
| `ADSRDisplay` (Shared UI) | `plugins/shared/src/ui/adsr_display.h` | MUST reuse for envelope visualization. Supports draggable control points, curve segment dragging (wired to curve amount parameters), bezier mode, playback dot via setPlaybackStatePointers(). Has ParameterCallback support for wiring to VST parameter IDs. |
| `ADSRDisplayCreator` | `plugins/shared/src/ui/adsr_display.h` | MUST reuse for VSTGUI view factory integration. Already registered globally. |
| `SampleAnalysis` struct | `plugins/innexus/src/dsp/sample_analysis.h` | Contains `frames` (HarmonicFrame sequence) and `hopTimeSec` from which amplitude envelope will be extracted via `globalAmplitude` per frame. |
| `SampleAnalyzer` | `plugins/innexus/src/dsp/sample_analyzer.h` | Background analysis thread where envelope detection logic will be added as a post-analysis step. |
| `MemorySlot` struct | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | MUST extend to include all 9 ADSR parameter fields alongside `HarmonicSnapshot`. |
| `EvolutionEngine` | `plugins/innexus/src/dsp/evolution_engine.h` | Already interpolates between occupied MemorySlots. MUST be extended to include ADSR parameter interpolation using geometric mean for time parameters and linear for Sustain and curve amounts. |
| `HarmonicBlender` | `plugins/innexus/src/dsp/harmonic_blender.h` | Multi-source blending engine. May need ADSR interpolation support for blend mode. |
| `SpectralDecayEnvelope` | `plugins/innexus/src/dsp/spectral_decay_envelope.h` | Existing per-partial decay mechanism. The new global ADSR is complementary, not a replacement. |
| State serialization (v8) | `plugins/innexus/src/processor/processor_state.cpp` | Current version is v8. MUST increment to v9 for ADSR parameter serialization with backward compatibility. |

**Search Results Summary**: The codebase has a complete `ADSREnvelope` DSP class (Layer 1, ~560 lines) and `ADSRDisplay` UI component (shared, ~1800 lines) ready for reuse. The `ADSREnvelope` float curve overloads (`setAttackCurve(float)` etc.) accept −1 to +1 amounts and map directly to FR-024/025/026 with no API changes needed. The `MemorySlot` struct is minimal (`HarmonicSnapshot` + `occupied` flag) and needs extension for all 9 ADSR fields. The `EvolutionEngine` already handles MemorySlot interpolation and can be extended for ADSR fields. No existing envelope detection/fitting code was found -- this is new functionality.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Any future per-partial envelope feature could reuse the envelope detection algorithm adapted per-partial rather than globally
- Other Krate plugins could benefit from the envelope detection algorithm if extracted to a shared DSP component

**Potential shared components** (preliminary, refined in plan.md):
- The ADSR fitting algorithm (peak-find + O(1) rolling least-squares steady-state detection) could be a standalone utility in `dsp/include/krate/dsp/core/` if other plugins need amplitude contour analysis
- The MemorySlot ADSR extension pattern could inform how other per-slot data is stored in future features

## Implementation Verification *(mandatory at completion)*

### Compliance Status

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `envelope_detector.h:46-47` reads `HarmonicFrame.globalAmplitude`; `sample_analyzer.cpp:355` calls `EnvelopeDetector::detect(analysis->frames, ...)` |
| FR-002 | MET | `envelope_detector.h:68-239` implements peak-finding + O(1) rolling least-squares; `kWindowSize=12` (line 248), `kSlopeThreshold=0.0005f` (line 252), `kVarianceThreshold=0.002f` (line 256); Welford variance + circular buffer sliding. Test `EnvelopeDetector: rolling least-squares detects steady-state region` passes |
| FR-003 | MET | `processor.cpp:1760-1788` (`checkForNewAnalysis`) reads `detectedADSR` and stores to atomics + sends IMessage to controller |
| FR-004 | MET | `plugin_ids.h:128` `kAdsrAttackId=720`; `controller.cpp:517-521` `RangeParameter 1-5000ms`; `processor_params.cpp:248-256` log mapping; test `ADSR VST: all 9 parameter IDs (720-728) are registered` passes |
| FR-005 | MET | `plugin_ids.h:129` `kAdsrDecayId=721`; `controller.cpp:523-527` `RangeParameter 1-5000ms`; `processor_params.cpp:257-264` log mapping |
| FR-006 | MET | `plugin_ids.h:130` `kAdsrSustainId=722`; `controller.cpp:529-533` `RangeParameter 0.0-1.0`; `processor_params.cpp:265-268` |
| FR-007 | MET | `plugin_ids.h:131` `kAdsrReleaseId=723`; `controller.cpp:535-539` `RangeParameter 1-5000ms`; `processor_params.cpp:269-276` log mapping |
| FR-008 | MET | `processor.cpp:1597-1602` implements `adsrGain = 1.0f - smoothedAmount + smoothedAmount * envVal` (lerp(1.0, envVal, amount)); test `ADSR Integration: Amount=1.0 shapes output with ADSR` passes |
| FR-009 | MET | `processor.cpp:1593-1596` skips ALL ADSR processing when `!adsrActive`; `adsrActive` is false when `adsrAmountTarget==0.0 && smoother==0`; test `ADSR Integration: Amount=0.0 produces bit-exact bypass` passes with bit-exact comparison |
| FR-010 | MET | `processor.cpp:504-509` computes `effAttack = clamp(adsrAttackMs * timeScale, 1, 5000)`; test `ADSR Integration: Time Scale=2.0 doubles effective times` and `Time Scale extremes clamp to [1, 5000]ms` both pass |
| FR-011 | MET | `processor.cpp:1593-1603` applies ADSR gain as global multiplier after oscillator bank + residual output; monophonic single `adsr_` instance in `processor.h:476` |
| FR-012 | MET | `processor.cpp:64-65` sets `RetriggerMode::Hard`; `processor_midi.cpp:88` calls `adsr_.gate(true)` on note-on; `processor_midi.cpp:197` calls `adsr_.gate(false)` on note-off; test `ADSR Integration: hard retrigger resets envelope on new note-on` passes |
| FR-013 | MET | `harmonic_snapshot.h:55-64` `MemorySlot` has all 9 ADSR fields; test `Memory Slot ADSR: default slot has adsrAmount=0.0` passes |
| FR-014 | MET | `processor.cpp:737-749` captures all 9 ADSR atomics into slot; test `Memory Slot ADSR: capture stores all 9 ADSR values` passes |
| FR-015 | MET | `processor.cpp:617-658` recalls all 9 ADSR values from slot to atomics + sends IMessage; test `Memory Slot ADSR: recall restores all 9 ADSR values` passes |
| FR-016 | MET | `harmonic_blender.h:200-276` `blendADSR()` uses `std::log/std::exp` for geometric mean on times, linear for sustain/amount/curves; `processor.cpp:1409-1422` wires blended ADSR back to atomics; test `Memory Slot ADSR: morph at t=0.5 uses geometric mean` passes |
| FR-017 | MET | `evolution_engine.h:224-256` `interpolateSlotADSR()` uses geometric mean for time params, linear for sustain/curves; `processor.cpp:1328-1337` wires evolution ADSR to atomics; test `Memory Slot ADSR: evolution engine smooth ADSR interpolation` passes |
| FR-018 | MET | `controller.cpp:1227-1238` creates `ADSRDisplay`, sets `setAdsrBaseParamId(720)`, `setCurveBaseParamId(726)`, wires callbacks and playback pointers; test `Controller::createCustomView returns non-null ADSRDisplay` and `ADSRDisplay receives correct base param IDs after wiring` both pass |
| FR-019 | MET | `processor_state.cpp:27` writes version 9; `processor_state.cpp:183-208` writes 9 global ADSR floats + 72 per-slot ADSR floats |
| FR-020 | MET | `processor_state.cpp:667-691` defaults all ADSR values for pre-v9 states: Amount=0.0, curves=0.0, times=10/100/100ms, sustain=1.0, timeScale=1.0; controller.cpp:1054-1074 also defaults v8 and older |
| FR-021 | MET | `ADSREnvelope adsr_` is pre-allocated member of `Processor` (`processor.h:476`); `OnePoleSmoother adsrAmountSmoother_` likewise (`processor.h:477`); no allocations in `process()` path |
| FR-022 | MET | `sample_analyzer.cpp:352-357` only calls `detect()` for file-based analysis; sidechain goes through `LiveAnalysisPipeline`; test `sidechain mode suppresses envelope detection (FR-022)` passes |
| FR-023 | MET | `adsrAmountSmoother_` smooths Amount transitions (15ms, `processor.cpp:185`); test `Amount 0->1 transition has no large discontinuities` requires `maxGainDiscontinuity < 0.01f` and passes |
| FR-024 | MET | `plugin_ids.h:134` `kAdsrAttackCurveId=726`; `controller.cpp:553-557` `RangeParameter -1.0 to +1.0`; `processor.cpp:517` calls `adsr_.setAttackCurve()` |
| FR-025 | MET | `plugin_ids.h:135` `kAdsrDecayCurveId=727`; `controller.cpp:559-563` `RangeParameter -1.0 to +1.0`; `processor.cpp:518` calls `adsr_.setDecayCurve()` |
| FR-026 | MET | `plugin_ids.h:136` `kAdsrReleaseCurveId=728`; `controller.cpp:565-569` `RangeParameter -1.0 to +1.0`; `processor.cpp:519` calls `adsr_.setReleaseCurve()` |
| SC-001 | MET | Test `Phase 8 T063b: EnvelopeDetector::detect() adds <10% analysis overhead` passes; detect() < 1ms vs 100ms+ analysis baseline |
| SC-002 | MET | Test `percussive contour yields short Attack and low Sustain` verifies Attack < 50ms, Sustain < 0.5; test `pad contour yields long Attack and high Sustain` verifies Attack > 50ms, Sustain > 0.7 |
| SC-003 | MET | Test `Amount=0.0 produces bit-exact bypass` uses exact comparison (`outA[i] != outB[i]`) over 20 blocks; passes |
| SC-004 | MET | Test `Amount 0->1 during active note has no gain jump > 0.01/sample` extracts envelope gain via test/ref ratio, requires `maxGainJump < 0.01f`; passes |
| SC-005 | MET | Test `ADSR CPU overhead < 0.1% of single core` measures over 5000 blocks x 5 rounds, requires `overheadPercent < 0.1`; passes |
| SC-006 | MET | `processor_state.cpp:667-691` defaults Amount=0.0 and curves=0.0 for v1-v8; test `v8 backward compatibility` passes |
| SC-007 | MET | All 9 params registered with `kCanAutomate`; test `All 9 ADSR parameters respond to automation` passes; pluginval automation test passed |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [x] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [x] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [x] Evidence column contains specific file paths, line numbers, test names, and measured values
- [x] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [x] No test thresholds relaxed from spec requirements
- [x] No placeholder values or TODO comments in new code
- [x] No features quietly removed from scope
- [x] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

Build result: 0 warnings in new code
Test result: 473 test cases, 1,067,546 assertions, all passed
Pluginval result: PASS (strictness 5)

All 26 functional requirements and 7 success criteria are met with concrete evidence.
