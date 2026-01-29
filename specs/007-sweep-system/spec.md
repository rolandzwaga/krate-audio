# Feature Specification: Sweep System

**Feature Branch**: `007-sweep-system`
**Created**: 2026-01-29
**Status**: Draft
**Input**: User description: "Sweep System for Disrumpo plugin (Week 8 per roadmap - Tasks T8.1-T8.18)"

**Related Documents**:
- [Disrumpo/roadmap.md](../Disrumpo/roadmap.md) - Task breakdown T8.1-T8.18
- [Disrumpo/specs-overview.md](../Disrumpo/specs-overview.md) - FR-SWEEP-001 to FR-SWEEP-004
- [Disrumpo/dsp-details.md](../Disrumpo/dsp-details.md) - Parameter IDs, Sweep-Morph Linking Curves (Section 8)
- [Disrumpo/custom-controls.md](../Disrumpo/custom-controls.md) - SweepIndicator specification (Section 1.3.4)
- [Disrumpo/ui-mockups.md](../Disrumpo/ui-mockups.md) - Sweep panel layout
- [Disrumpo/vstgui-implementation.md](../Disrumpo/vstgui-implementation.md) - Sweep parameter IDs (100-199)
- [006-morph-ui/spec.md](../006-morph-ui/spec.md) - Morph-sweep linking (prerequisite)

**Prerequisites**:
- 006-morph-ui MUST be complete (MorphPad, morph-sweep linking modes already defined)
- 004-vstgui-infrastructure MUST be complete (sweep parameter registration)
- 005-morph-system MUST be complete (MorphEngine for linked operation)

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Enable Frequency Sweep Distortion Focus (Priority: P1)

A sound designer wants to create a focused distortion effect that emphasizes a specific frequency region, with the intensity smoothly fading toward adjacent frequencies using a Gaussian distribution.

**Why this priority**: The sweep focus is the core feature that differentiates Disrumpo from standard multiband distortion - without it, users cannot achieve the signature "sweeping distortion" effect.

**Independent Test**: Can be fully tested by enabling sweep, adjusting center frequency, and verifying that bands within the sweep region receive increased distortion intensity while outer bands are attenuated.

**Acceptance Scenarios**:

1. **Given** sweep is disabled, **When** the user enables sweep, **Then** a Gaussian intensity distribution is applied across frequency bands centered at the sweep frequency
2. **Given** sweep is enabled with center at 1kHz and width of 2 octaves, **When** processing audio, **Then** bands near 1kHz receive higher distortion intensity than bands far from 1kHz
3. **Given** sweep intensity is set to 100%, **When** the center frequency is at 2kHz, **Then** the band containing 2kHz receives full distortion while outer bands receive reduced intensity per Gaussian falloff

---

### User Story 2 - Adjust Sweep Parameters (Priority: P1)

A user wants to control the sweep characteristics including center frequency, width, intensity, and falloff shape to shape the focused distortion region.

**Why this priority**: Parameter control is essential for sound design - users must be able to tailor the sweep to their creative needs.

**Independent Test**: Can be fully tested by adjusting each sweep parameter and verifying the corresponding change in audio processing behavior.

**Acceptance Scenarios**:

1. **Given** sweep is enabled, **When** the user adjusts center frequency from 200Hz to 8kHz, **Then** the distortion focus moves across the frequency spectrum accordingly
2. **Given** sweep center is at 1kHz, **When** the user increases width from 0.5 to 4.0 octaves, **Then** the affected frequency region becomes broader
3. **Given** sweep is active, **When** the user increases intensity from 50% to 200%, **Then** the distortion effect becomes more pronounced at the center frequency
4. **Given** sweep falloff is set to "Sharp", **When** processing audio, **Then** intensity drops off rapidly outside the sweep region (linear falloff)
5. **Given** sweep falloff is set to "Smooth", **When** processing audio, **Then** intensity drops off gradually using Gaussian distribution

---

### User Story 3 - Visualize Sweep Position on Spectrum Display (Priority: P1)

A user wants to see where the sweep is positioned on the spectrum display so they can visually understand which frequency region is being affected.

**Why this priority**: Visual feedback is critical for intuitive sound design - users cannot effectively use the sweep without seeing where it is.

**Independent Test**: Can be fully tested by enabling sweep and verifying that the SweepIndicator overlay appears on the SpectrumDisplay at the correct position.

**Acceptance Scenarios**:

1. **Given** sweep is enabled, **When** the SpectrumDisplay is visible, **Then** a Gaussian bell curve overlay shows the sweep position and width
2. **Given** sweep center is at 500Hz with width 1.5 octaves, **When** viewing the spectrum, **Then** the indicator is centered at 500Hz with visual falloff matching the width parameter
3. **Given** sweep intensity is 150%, **When** viewing the spectrum, **Then** the indicator height is proportionally taller than at 100% intensity

---

### User Story 4 - Link Sweep to Morph Position (Priority: P1)

A sound designer wants the sweep position to automatically drive the morph position, creating evolving timbres where the distortion character changes as the sweep moves through the spectrum.

**Why this priority**: Sweep-morph linking is a core differentiator of Disrumpo - it enables the signature "sweeping distortion character" effect that is central to the plugin's value proposition.

**Independent Test**: Can be fully tested by setting a sweep-morph link mode and verifying that morph position changes as sweep frequency changes.

**Acceptance Scenarios**:

1. **Given** sweep-morph link is set to "Linear", **When** sweep frequency moves from 20Hz to 20kHz, **Then** morph position moves from 0 to 1
2. **Given** sweep-morph link is set to "Inverse", **When** sweep frequency increases, **Then** morph position decreases
3. **Given** sweep-morph link is set to "EaseIn", **When** sweep moves through the low frequency range, **Then** morph position changes slowly at first, then accelerates

---

### User Story 5 - Configure Sweep-Morph Link Curves (Priority: P2)

A user wants to choose different curves for how sweep position maps to morph position, enabling different creative expressions.

**Why this priority**: Different link curves enable different musical effects, but the core Linear mode covers basic use cases.

**Independent Test**: Can be fully tested by selecting each link mode and verifying the morph position follows the expected curve.

**Acceptance Scenarios**:

1. **Given** sweep-morph link is "HoldRise", **When** sweep moves from low to high frequency, **Then** morph stays at 0 until 60% of the sweep range, then rises to 1
2. **Given** sweep-morph link is "Stepped", **When** sweep moves continuously, **Then** morph position jumps between discrete levels (0, 0.33, 0.67, 1.0)
3. **Given** sweep-morph link is "EaseOut", **When** sweep moves through the high frequency range, **Then** morph position changes quickly at first, then slows

---

### User Story 6 - Synchronize Sweep Indicator with Audio (Priority: P2)

A user expects the sweep indicator on the spectrum display to be precisely synchronized with what they hear, so the visual matches the audio.

**Why this priority**: Audio-visual sync is important for professional use, but the feature works without perfect sync.

**Independent Test**: Can be fully tested by automating sweep frequency and verifying the visual indicator moves in sync with the audio effect.

**Acceptance Scenarios**:

1. **Given** sweep frequency is being automated, **When** viewing the spectrum display, **Then** the sweep indicator position matches what is heard within one audio buffer latency
2. **Given** output latency compensation is enabled, **When** sweep moves, **Then** the visual indicator is delayed to match the audio output timing
3. **Given** sweep is modulated by LFO at 2Hz, **When** viewing the spectrum, **Then** the indicator smoothly oscillates at 2Hz in sync with the audio

---

### User Story 7 - Per-Band Intensity Calculation (Priority: P1)

The DSP system needs to calculate a per-band intensity multiplier based on the Gaussian sweep distribution, so each band receives the appropriate amount of distortion.

**Why this priority**: This is the core DSP implementation that makes the sweep feature work - without it, there is no sweep effect.

**Independent Test**: Can be fully tested via unit tests that verify Gaussian intensity values at various frequency distances from sweep center.

**Acceptance Scenarios**:

1. **Given** sweep center at 1kHz, width 2 octaves, intensity 100%, **When** calculating intensity for a band at 1kHz, **Then** intensity multiplier is 1.0 (full)
2. **Given** sweep center at 1kHz, width 2 octaves (sigma = 1 octave), intensity 100%, **When** calculating intensity for a band 1 octave away (500Hz or 2kHz), **Then** intensity multiplier is approximately 0.606 (e^-0.5)
3. **Given** sweep center at 1kHz, width 2 octaves, intensity 50%, **When** calculating intensity for a band at 1kHz, **Then** intensity multiplier is 0.5 (half of peak due to multiplicative scaling)

---

### User Story 8 - Sweep UI Controls (Priority: P1)

A user wants accessible UI controls for all sweep parameters in the sweep panel section of the interface.

**Why this priority**: Users cannot use the sweep feature without UI controls.

**Independent Test**: Can be fully tested by locating all sweep controls in the UI and verifying they adjust the corresponding parameters.

**Acceptance Scenarios**:

1. **Given** the sweep panel is visible, **When** the user views the panel, **Then** controls for Enable, Center Frequency, Width, Intensity, Falloff, and Morph Link are displayed
2. **Given** the sweep enable toggle is off, **When** the user clicks it, **Then** sweep is enabled and the toggle indicates the active state
3. **Given** the sweep frequency knob is displayed, **When** the user adjusts it, **Then** the SweepFrequency parameter updates and the sweep indicator moves

---

### User Story 9 - Automate Sweep with Internal Sources (Priority: P1)

A sound designer wants to animate the sweep frequency using internal modulation sources (LFO, envelope follower) or external control (MIDI CC, host automation) to create evolving, dynamic effects.

**Why this priority**: Sweep automation is essential for the signature "sweeping distortion" effect - static sweeps are far less useful than animated ones.

**Independent Test**: Can be fully tested by enabling each automation source and verifying sweep frequency responds accordingly.

**Acceptance Scenarios**:

1. **Given** sweep internal LFO is enabled at 0.5Hz, **When** processing audio, **Then** sweep frequency oscillates between min and max at 0.5Hz rate
2. **Given** sweep envelope follower is enabled, **When** input audio gets louder, **Then** sweep frequency rises proportionally to input level
3. **Given** MIDI CC is mapped to sweep frequency, **When** CC value changes, **Then** sweep frequency responds immediately
4. **Given** host automation is recorded for sweep frequency, **When** playing back, **Then** sweep frequency follows the automation curve exactly

---

### User Story 10 - Custom Sweep-Morph Link Curve (Priority: P2)

A user wants to define a custom curve for how sweep position maps to morph position, beyond the preset curves, for unique creative expressions.

**Why this priority**: Custom curves enable advanced users to create unique effects, but preset curves cover most use cases.

**Independent Test**: Can be fully tested by defining a custom curve and verifying morph position follows the user-defined mapping.

**Acceptance Scenarios**:

1. **Given** sweep-morph link is set to "Custom", **When** the user views the UI, **Then** a dedicated curve editor section becomes visible with editable breakpoints
2. **Given** a custom curve with breakpoints at (0, 0.5), (0.5, 1.0), (1.0, 0.0), **When** sweep moves from low to high, **Then** morph follows the custom path: starts at 0.5, rises to 1.0 at mid-sweep, drops to 0.0 at high frequencies
3. **Given** sweep-morph link is changed from "Custom" to any other mode, **When** the user views the UI, **Then** the dedicated curve editor section is hidden

---

### Edge Cases

- What happens when sweep center frequency is at the extreme low (20Hz) or high (20kHz) end? - Gaussian distribution extends beyond audible range; bands outside receive intensity based on their distance
- What happens when sweep width is at minimum (0.5 octaves)? - Very narrow focus; only bands very close to center receive significant intensity
- What happens when sweep width is at maximum (4.0 octaves)? - Very broad focus; most bands receive significant intensity
- What happens when sweep intensity is 0%? - No additional distortion applied beyond base band settings
- What happens when sweep intensity is 200%? - Doubled intensity at center; can cause strong saturation
- What happens when sweep is enabled but all bands are bypassed? - Sweep calculations occur but have no audible effect
- What happens when sweep-morph link is enabled but sweep is disabled? - Link has no effect; morph position stays at manual value
- What happens when band count changes while sweep is active? - Sweep intensity is recalculated for new band configuration
- What happens when sweep frequency equals a crossover frequency exactly? - Bands on both sides of crossover receive equal intensity based on their center frequencies

---

## Clarifications

### Session 2026-01-29

- Q: When sweep intensity is set to values other than 100%, how should the per-band intensity multipliers be scaled? → A: Scale multiplicatively (baseline = 100%, so 50% = half the Gaussian curve values, 200% = double). This preserves shape invariance and provides predictable behavior.
- Q: When both internal LFO and envelope follower are enabled simultaneously, how should their modulation signals combine to control sweep frequency? → A: Additive (sum both modulation amounts, clamp to parameter range)
- Q: When sweep-morph link mode is set to "Custom", where should the breakpoint curve editor be displayed? → A: Dedicated curve editor section that becomes visible when Custom is selected
- Q: Should sweep center frequency changes be smoothed to prevent audio artifacts (zipper noise, clicks) when the parameter is automated or modulated rapidly? → A: Smooth with 10-50ms time constant (OnePoleSmoother)
- Q: For Sharp falloff mode, when a band is exactly at the edge of the sweep width (distance = width/2), should its intensity be 0.0 or slightly above 0.0? → A: Exactly 0.0 (linear drops to zero at edge)

---

## Requirements *(mandatory)*

### Functional Requirements

#### SweepProcessor DSP (T8.1-T8.6)

- **FR-001**: System MUST provide a SweepProcessor class that calculates per-band intensity multipliers based on sweep parameters
- **FR-002**: SweepProcessor MUST support a center frequency parameter ranging from 20Hz to 20kHz
- **FR-003**: SweepProcessor MUST support a width parameter ranging from 0.5 to 4.0 octaves
- **FR-004**: SweepProcessor MUST support an intensity parameter ranging from 0% to 200%
- **FR-005**: SweepProcessor MUST support a falloff parameter with two modes: Sharp and Smooth
- **FR-006**: When falloff is "Smooth", SweepProcessor MUST calculate intensity using a Gaussian distribution with sigma = width / 2 octaves
- **FR-006a**: When falloff is "Sharp", SweepProcessor MUST calculate intensity using linear falloff: `intensity = intensityParam * max(0, 1 - abs(distanceOctaves) / (width / 2))`, which produces exactly 0.0 at the edge (distance = width/2) and beyond
- **FR-007**: SweepProcessor MUST provide a method to calculate intensity multiplier for a given band center frequency
- **FR-007a**: Sweep center frequency parameter changes MUST be smoothed using a one-pole smoother with a time constant between 10-50ms to prevent zipper noise and audio artifacts during automation or modulation
- **FR-007b**: Default smoothing time MUST be 20ms

#### Gaussian Intensity Distribution (T8.5)

- **FR-008**: Smooth falloff intensity calculation MUST use the formula: `intensity = intensityParam * exp(-0.5 * (distanceOctaves / sigma)^2)` where intensityParam is the sweep intensity parameter (0.0 to 2.0 normalized)
- **FR-009**: Distance MUST be calculated in logarithmic (octave) space: `distanceOctaves = abs(log2(bandFreq) - log2(sweepCenterFreq))`
- **FR-010**: The intensity multiplier MUST be calculated by multiplying the normalized falloff curve (peak = 1.0) by the intensity parameter value, preserving shape: at 100% intensity center = 1.0, at 50% intensity center = 0.5, at 200% intensity center = 2.0

#### Sweep Enable/Disable (T8.7)

- **FR-011**: System MUST provide a sweep enable parameter that toggles the sweep effect on/off
- **FR-012**: When sweep is disabled, all bands MUST receive uniform base intensity (no sweep modulation)
- **FR-013**: Sweep enable/disable MUST be automatable

#### Sweep-Morph Linking (T8.8-T8.13)

- **FR-014**: System MUST support sweep-morph linking with mode options: None, Linear, Inverse, EaseIn, EaseOut, HoldRise, Stepped, Custom
- **FR-015**: When link mode is "None", morph position MUST be independent of sweep position
- **FR-016**: When link mode is "Linear", morph position MUST equal normalized sweep frequency position (low freq = 0, high freq = 1)
- **FR-017**: When link mode is "Inverse", morph position MUST equal 1 minus normalized sweep frequency position
- **FR-018**: When link mode is "EaseIn", morph position MUST follow quadratic curve: `y = x^2`
- **FR-019**: When link mode is "EaseOut", morph position MUST follow inverse quadratic curve: `y = 1 - (1-x)^2`
- **FR-020**: When link mode is "HoldRise", morph position MUST stay at 0 until sweep reaches 60% of range, then rise linearly to 1
- **FR-021**: When link mode is "Stepped", morph position MUST quantize to discrete levels: 0, 0.33, 0.67, 1.0
- **FR-022**: When link mode is "Custom", morph position MUST follow user-defined curve with configurable breakpoints (minimum 2, maximum 8 points)

#### Sweep Automation (FR-SWEEP-004)

- **FR-023**: All sweep parameters (Frequency, Width, Intensity, Enable) MUST support host automation
- **FR-024**: System MUST provide an internal LFO dedicated to sweep frequency modulation with rate 0.01Hz-20Hz (free) or tempo-synced (8 bars to 1/64T)
- **FR-025**: Internal sweep LFO MUST support shapes: Sine, Triangle, Saw, Square, Sample & Hold, Smooth Random
- **FR-026**: System MUST provide envelope follower input to modulate sweep frequency based on input signal level
- **FR-027**: Sweep envelope follower MUST have configurable attack (1-100ms), release (10-500ms), and sensitivity (0-100%)
- **FR-028**: System MUST support MIDI CC mapping for sweep frequency parameter
- **FR-029**: MIDI CC mapping MUST support 7-bit (0-127) and 14-bit high-resolution modes
- **FR-029a**: When both internal LFO and envelope follower are enabled simultaneously, their modulation amounts MUST be summed additively and the result clamped to the sweep frequency parameter range (20Hz-20kHz)

#### Sweep UI Controls (T8.14)

- **FR-030**: System MUST provide sweep UI controls in the sweep panel section
- **FR-031**: Sweep panel MUST include an enable toggle (COnOffButton) bound to SweepEnable parameter
- **FR-032**: Sweep panel MUST include a center frequency knob (CKnob) bound to SweepFrequency parameter with logarithmic mapping
- **FR-033**: Sweep panel MUST include a width knob (CKnob) bound to SweepWidth parameter
- **FR-034**: Sweep panel MUST include an intensity knob (CKnob) bound to SweepIntensity parameter
- **FR-035**: Sweep panel MUST include a falloff toggle (CSegmentButton) bound to SweepFalloff parameter with options "Sharp" and "Smooth"
- **FR-036**: Sweep panel MUST include a morph link dropdown (COptionMenu) bound to SweepMorphLink parameter
- **FR-037**: Sweep panel MUST include internal LFO controls: enable toggle, rate knob, shape selector
- **FR-038**: Sweep panel MUST include envelope follower controls: enable toggle, attack/release knobs, sensitivity knob
- **FR-039**: Sweep panel MUST display MIDI Learn button for CC mapping
- **FR-039a**: When sweep-morph link mode is set to "Custom", a dedicated curve editor section MUST become visible in the UI
- **FR-039b**: The curve editor section MUST be hidden when sweep-morph link mode is changed to any mode other than "Custom"
- **FR-039c**: The curve editor MUST allow users to add, remove, and drag breakpoints (2-8 points) within the valid range (x ∈ [0, 1], y ∈ [0, 1])

#### SweepIndicator on SpectrumDisplay (T8.15)

- **FR-040**: SpectrumDisplay MUST render a SweepIndicator overlay when sweep is enabled
- **FR-041**: SweepIndicator MUST display as a curve (Gaussian for Smooth, triangular for Sharp) centered at sweep center frequency
- **FR-042**: SweepIndicator width MUST visually represent the sweep width parameter
- **FR-043**: SweepIndicator height MUST be proportional to sweep intensity parameter
- **FR-044**: SweepIndicator MUST use semi-transparent rendering (white at 40% opacity for curve, accent color at 20% for edge glow per custom-controls.md)
- **FR-045**: SweepIndicator MUST include a 2px solid vertical center line at sweep center frequency

#### Audio-Synchronized Sweep Visualization (T8.16)

- **FR-046**: Sweep position data MUST be communicated from audio thread to UI thread via lock-free buffer
- **FR-047**: UI thread MUST interpolate sweep position between updates for smooth 60fps display
- **FR-048**: UI thread SHOULD compensate for output latency to align visual with audio
- **FR-049**: SweepIndicator MUST update at minimum 30fps when sweep is active

#### Band Intensity Visualization

- **FR-050**: SpectrumDisplay band regions SHOULD show visual indication of sweep intensity (e.g., brightness proportional to sweep intensity multiplier)

#### Unit Tests (T8.17-T8.18)

- **FR-051**: Unit tests MUST verify Gaussian (Smooth) intensity calculation accuracy within 0.01 tolerance
- **FR-052**: Unit tests MUST verify Sharp (linear) intensity calculation accuracy within 0.01 tolerance
- **FR-053**: Unit tests MUST verify all sweep-morph link curves (including Custom with test breakpoints) produce correct output at key positions (0, 0.25, 0.5, 0.75, 1.0)
- **FR-054**: Unit tests MUST verify edge cases: minimum/maximum width, minimum/maximum frequency, intensity boundaries
- **FR-055**: Unit tests MUST verify internal LFO produces correct waveforms at specified rates
- **FR-056**: Unit tests MUST verify envelope follower responds correctly to input level changes

### Key Entities

- **SweepProcessor**: DSP class that calculates intensity distribution (Gaussian or linear) and per-band multipliers; holds sweep parameters (center, width, intensity, falloff, enabled)
- **SweepMorphLink**: Enum/parameter defining the curve type for sweep-to-morph position mapping (None, Linear, Inverse, EaseIn, EaseOut, HoldRise, Stepped, Custom)
- **SweepFalloff**: Enum/parameter defining the intensity falloff shape (Sharp = linear, Smooth = Gaussian)
- **SweepLFO**: Internal LFO dedicated to sweep frequency modulation; parameters: enable, rate, shape, sync mode
- **SweepEnvelopeFollower**: Envelope follower for input-driven sweep modulation; parameters: enable, attack, release, sensitivity
- **SweepIndicator**: Visual overlay on SpectrumDisplay showing sweep position and width as a curve (shape matches falloff mode)
- **SweepPositionBuffer**: Lock-free SPSC queue for communicating sweep position from audio thread to UI thread
- **CustomCurve**: User-defined breakpoint curve for Custom morph link mode (2-8 breakpoints)

### Parameter Defaults (from specs-overview.md Appendix A)

**Intensity Parameter Semantics**: The intensity parameter uses a 0-200% scale where 100% is the baseline (1.0 multiplier at center). At 50% intensity, the center multiplier is 0.5; at 200%, it's 2.0. The normalized range is [0.0, 2.0] where normalized = percentage / 100.

| Parameter | Min | Max | Default | Normalized Default |
|-----------|-----|-----|---------|-------------------|
| Sweep Frequency | 20 Hz | 20 kHz | **1 kHz** | ~0.566 (log scale) |
| Sweep Width | 0.5 oct | 4.0 oct | **1.5 oct** | 0.286 |
| Sweep Intensity | 0% | 200% | **50%** | 0.5 |
| Sweep Falloff | Sharp | Smooth | **Smooth** | 1 (enum) |
| Sweep Enable | Off | On | **Off** | 0 (bool) |
| Sweep Morph Link | None | Custom | **None** | 0 (enum) |
| Internal LFO Rate | 0.01 Hz | 20 Hz | **1 Hz** | ~0.606 (log scale) |
| Internal LFO Enable | Off | On | **Off** | 0 (bool) |
| Envelope Follower Enable | Off | On | **Off** | 0 (bool) |
| Envelope Attack | 1 ms | 100 ms | **10 ms** | 0.091 |
| Envelope Release | 10 ms | 500 ms | **100 ms** | 0.184 |
| Envelope Sensitivity | 0% | 100% | **50%** | 0.5 |
| Sweep Smoothing Time | 10 ms | 50 ms | **20 ms** | 0.25 |

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Gaussian (Smooth) intensity calculation for band at sweep center returns intensity parameter value within 0.01 tolerance
- **SC-002**: Gaussian intensity calculation for band 1 sigma from center returns approximately 0.606 * intensity within 0.02 tolerance
- **SC-003**: Gaussian intensity calculation for band 2 sigma from center returns approximately 0.135 * intensity within 0.02 tolerance
- **SC-004**: Sharp (linear) intensity calculation for band at sweep center returns intensity parameter value within 0.01 tolerance
- **SC-005**: Sharp intensity calculation for band at edge of width (distance = width/2) returns exactly 0.0 within 0.01 tolerance, and bands beyond edge also return 0.0
- **SC-006**: Sweep enable/disable toggle takes effect within 1 audio buffer (< 12ms at 44.1kHz/512 samples)
- **SC-007**: All 8 sweep-morph link curves (including Custom) produce mathematically correct output verified by unit tests
- **SC-008**: SweepIndicator renders at correct position within 1 pixel of expected location
- **SC-009**: SweepIndicator width visually matches width parameter (±10% visual accuracy)
- **SC-010**: Sweep UI controls respond to user interaction within 1 frame (< 16ms)
- **SC-011**: Sweep visualization updates at minimum 30fps when sweep frequency is being automated
- **SC-012**: All sweep parameters persist correctly across preset save/load cycles
- **SC-013**: Sweep processing adds less than 0.1% CPU overhead per active band
- **SC-014**: User can enable sweep, adjust parameters, and hear the sweep effect within 30 seconds of first interaction
- **SC-015**: Internal LFO produces frequency-accurate waveforms within 0.1% of target rate
- **SC-016**: Envelope follower responds to input level changes within specified attack/release times (±10%)
- **SC-017**: Host automation playback matches recorded curve within 1 sample accuracy
- **SC-018**: MIDI CC changes take effect within 1ms of receipt

---

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- 006-morph-ui is complete and provides MorphPad, morph-sweep linking UI (X/Y Link dropdowns)
- 005-morph-system is complete and MorphEngine can receive position updates from sweep link
- 004-vstgui-infrastructure has registered all sweep parameters (kSweepEnableId, kSweepFrequencyId, kSweepWidthId, kSweepIntensityId, kSweepMorphLinkId) as defined in vstgui-implementation.md
- SpectrumDisplay custom control exists and can receive sweep position updates for overlay rendering
- BandProcessor can accept an intensity multiplier from SweepProcessor to scale distortion intensity

### Existing Codebase Components (Principle XIV)

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Morph-Sweep Link Curves | dsp-details.md Section 8 | Curve formulas already specified; implementation should match |
| SweepPositionBuffer pattern | custom-controls.md Section 1.3.4 | Lock-free buffer pattern for audio-UI sync |
| SpectrumDisplay | custom-controls.md Section 1 | Must be extended for SweepIndicator overlay |
| Parameter IDs | vstgui-implementation.md Section 1.2 | Sweep IDs 100-105 already defined |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | MUST use for sweep center frequency parameter smoothing (10-50ms time constant) |
| EnvelopeFollower | dsp/include/krate/dsp/processors/envelope_follower.h | Reference for similar signal-following patterns |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "SweepProcessor" plugins/
grep -r "Gaussian" dsp/ plugins/
grep -r "SweepMorphLink" dsp/ plugins/
```

**Search Results Summary**: SweepProcessor and SweepMorphLink are specified in Disrumpo reference documents but not yet implemented. Gaussian calculation pattern exists in mathematical utilities. Morph link curve formulas documented in dsp-details.md Section 8.

### Forward Reusability Consideration

**Sibling features at same layer**:
- 008-modulation-system (Week 9-10) will need similar parameter smoothing and audio-UI sync patterns
- Future spectral analyzer features may reuse SweepIndicator rendering patterns

**Potential shared components**:
- Lock-free position buffer pattern can be extracted for other audio-UI sync needs
- Gaussian intensity calculation could be useful for future focus/emphasis effects

---

## Sweep-Morph Link Curve Reference

The following curves are defined in dsp-details.md Section 8 and MUST be implemented exactly:

| Curve | Formula | Musical Intent |
|-------|---------|----------------|
| None | Return 0.5 (center) | Independent control |
| Linear | `y = x` | Baseline, predictable |
| Inverse | `y = 1 - x` | Opposite relationship |
| EaseIn | `y = x^2` | Slow start, fast end (emphasize destruction at highs) |
| EaseOut | `y = 1 - (1-x)^2` | Fast start, gentle landing (transient emphasis) |
| HoldRise | `y = 0 if x < 0.6, else (x - 0.6) / 0.4` | Flat until 60%, then rise (performance sweeps) |
| Stepped | `y = floor(x * 4) / 3` | Discrete steps for glitch/digital character |
| Custom | User-defined breakpoint interpolation | Full creative control |

Where `x` is the normalized sweep frequency position: `x = (log2(sweepFreq) - log2(20)) / (log2(20000) - log2(20))`

### Custom Curve Specification

When link mode is "Custom":
- User defines 2-8 breakpoints as (x, y) pairs where x ∈ [0, 1] and y ∈ [0, 1]
- First breakpoint MUST have x = 0, last breakpoint MUST have x = 1
- Intermediate breakpoints are interpolated linearly
- Default custom curve: two points at (0, 0) and (1, 1) = Linear

---

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | [TBD] | [Test name or reason for failure] |
| FR-002 | [TBD] | [Test name or reason for failure] |
| FR-003 | [TBD] | [Test name or reason for failure] |
| FR-004 | [TBD] | [Test name or reason for failure] |
| FR-005 | [TBD] | [Test name or reason for failure] |
| FR-006 | [TBD] | [Test name or reason for failure] |
| FR-006a | [TBD] | [Test name or reason for failure] |
| FR-007 | [TBD] | [Test name or reason for failure] |
| FR-008 | [TBD] | [Test name or reason for failure] |
| FR-009 | [TBD] | [Test name or reason for failure] |
| FR-010 | [TBD] | [Test name or reason for failure] |
| FR-011 | [TBD] | [Test name or reason for failure] |
| FR-012 | [TBD] | [Test name or reason for failure] |
| FR-013 | [TBD] | [Test name or reason for failure] |
| FR-014 | [TBD] | [Test name or reason for failure] |
| FR-015 | [TBD] | [Test name or reason for failure] |
| FR-016 | [TBD] | [Test name or reason for failure] |
| FR-017 | [TBD] | [Test name or reason for failure] |
| FR-018 | [TBD] | [Test name or reason for failure] |
| FR-019 | [TBD] | [Test name or reason for failure] |
| FR-020 | [TBD] | [Test name or reason for failure] |
| FR-021 | [TBD] | [Test name or reason for failure] |
| FR-022 | [TBD] | [Test name or reason for failure] |
| FR-023 | [TBD] | [Test name or reason for failure] |
| FR-024 | [TBD] | [Test name or reason for failure] |
| FR-025 | [TBD] | [Test name or reason for failure] |
| FR-026 | [TBD] | [Test name or reason for failure] |
| FR-027 | [TBD] | [Test name or reason for failure] |
| FR-028 | [TBD] | [Test name or reason for failure] |
| FR-029 | [TBD] | [Test name or reason for failure] |
| FR-030 | [TBD] | [Test name or reason for failure] |
| FR-031 | [TBD] | [Test name or reason for failure] |
| FR-032 | [TBD] | [Test name or reason for failure] |
| FR-033 | [TBD] | [Test name or reason for failure] |
| FR-034 | [TBD] | [Test name or reason for failure] |
| FR-035 | [TBD] | [Test name or reason for failure] |
| FR-036 | [TBD] | [Test name or reason for failure] |
| FR-037 | [TBD] | [Test name or reason for failure] |
| FR-038 | [TBD] | [Test name or reason for failure] |
| FR-039 | [TBD] | [Test name or reason for failure] |
| FR-040 | [TBD] | [Test name or reason for failure] |
| FR-041 | [TBD] | [Test name or reason for failure] |
| FR-042 | [TBD] | [Test name or reason for failure] |
| FR-043 | [TBD] | [Test name or reason for failure] |
| FR-044 | [TBD] | [Test name or reason for failure] |
| FR-045 | [TBD] | [Test name or reason for failure] |
| FR-046 | [TBD] | [Test name or reason for failure] |
| FR-047 | [TBD] | [Test name or reason for failure] |
| FR-048 | [TBD] | [Test name or reason for failure] |
| FR-049 | [TBD] | [Test name or reason for failure] |
| FR-050 | [TBD] | [Test name or reason for failure] |
| FR-051 | [TBD] | [Test name or reason for failure] |
| FR-052 | [TBD] | [Test name or reason for failure] |
| FR-053 | [TBD] | [Test name or reason for failure] |
| FR-054 | [TBD] | [Test name or reason for failure] |
| FR-055 | [TBD] | [Test name or reason for failure] |
| FR-056 | [TBD] | [Test name or reason for failure] |
| SC-001 | [TBD] | [Measured value vs target] |
| SC-002 | [TBD] | [Measured value vs target] |
| SC-003 | [TBD] | [Measured value vs target] |
| SC-004 | [TBD] | [Measured value vs target] |
| SC-005 | [TBD] | [Measured value vs target] |
| SC-006 | [TBD] | [Measured value vs target] |
| SC-007 | [TBD] | [Measured value vs target] |
| SC-008 | [TBD] | [Measured value vs target] |
| SC-009 | [TBD] | [Measured value vs target] |
| SC-010 | [TBD] | [Measured value vs target] |
| SC-011 | [TBD] | [Measured value vs target] |
| SC-012 | [TBD] | [Measured value vs target] |
| SC-013 | [TBD] | [Measured value vs target] |
| SC-014 | [TBD] | [Measured value vs target] |
| SC-015 | [TBD] | [Measured value vs target] |
| SC-016 | [TBD] | [Measured value vs target] |
| SC-017 | [TBD] | [Measured value vs target] |
| SC-018 | [TBD] | [Measured value vs target] |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
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
