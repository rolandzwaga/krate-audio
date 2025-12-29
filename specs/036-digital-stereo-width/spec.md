# Feature Specification: Digital Delay Stereo Width Control

**Feature Branch**: `036-digital-stereo-width`
**Created**: 2025-12-29
**Status**: Draft
**Input**: User description: "can we add stereo width control to the digital delay, similar to what the ping pong delay also has"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Stereo Width Adjustment (Priority: P1)

A music producer working on a mix wants to adjust the stereo width of their digital delay to create a wider or narrower delay image without changing the delay time or feedback.

**Why this priority**: Core functionality that delivers immediate, tangible value. Stereo width is a fundamental mixing parameter that directly affects the spatial characteristics of the delay effect.

**Independent Test**: Can be fully tested by loading the digital delay in a DAW, playing stereo audio, adjusting the width parameter from 0% to 200%, and verifying that the stereo image narrows (approaching mono at 0%) and widens (maximum separation at 200%).

**Acceptance Scenarios**:

1. **Given** the digital delay is loaded with a stereo audio source, **When** the width is set to 0%, **Then** the delay output becomes mono (identical left and right channels)
2. **Given** the digital delay is loaded with a stereo audio source, **When** the width is set to 100%, **Then** the delay output preserves the original stereo image width
3. **Given** the digital delay is loaded with a stereo audio source, **When** the width is set to 200%, **Then** the delay output has maximum stereo separation (doubled width)
4. **Given** the digital delay is processing audio, **When** the width parameter is automated from 50% to 150%, **Then** the width changes smoothly without clicks or artifacts

---

### User Story 2 - Narrow Width for Focused Delays (Priority: P2)

A mixing engineer wants to narrow the stereo width of the delay to keep it centered and prevent it from overwhelming the stereo field, especially on vocals or lead instruments.

**Why this priority**: Common mixing technique for professional workflows. Provides creative control over stereo positioning without requiring external plugins.

**Independent Test**: Can be tested by applying digital delay to a vocal track, setting width to 30-50%, and verifying that the delay stays centered while still providing depth.

**Acceptance Scenarios**:

1. **Given** the digital delay is applied to a centered vocal, **When** the width is set to 50%, **Then** the delay remains centered but provides subtle stereo interest
2. **Given** the digital delay is processing a mono source, **When** the width is reduced from 100% to 30%, **Then** the delay output narrows appropriately

---

### User Story 3 - Wide Width for Ambient Effects (Priority: P2)

A sound designer wants to create an expansive, ambient delay effect by increasing the stereo width beyond the original source material.

**Why this priority**: Creative sound design capability that expands the plugin's usefulness for ambient and cinematic applications.

**Independent Test**: Can be tested by applying digital delay to a pad or ambient sound, setting width to 150-200%, and verifying the expanded stereo field.

**Acceptance Scenarios**:

1. **Given** the digital delay is applied to an ambient pad, **When** the width is set to 150%, **Then** the delay output is wider than the original source
2. **Given** the digital delay is applied to a stereo source, **When** the width is set to 200%, **Then** the stereo separation is maximized without distortion or phase issues

---

### User Story 4 - State Persistence (Priority: P1)

A user saves a project with a specific width setting on the digital delay, closes the DAW, and reopens the project.

**Why this priority**: Essential for professional use. Projects must recall settings exactly as saved.

**Independent Test**: Can be tested by setting width to a specific value (e.g., 75%), saving the DAW project, closing and reopening it, and verifying the width parameter recalls correctly.

**Acceptance Scenarios**:

1. **Given** the digital delay width is set to 75%, **When** the project is saved and reloaded, **Then** the width parameter restores to exactly 75%
2. **Given** the digital delay width is set to 0% (mono), **When** the project is saved and reloaded, **Then** the width parameter restores to 0%

---

### Edge Cases

- What happens when the width is adjusted during a delay tail (feedback decay)? System must smooth the parameter change to avoid clicks.
- How does width interact with mono input sources? Width should still affect the delay output's stereo field (e.g., modulation creates stereo from mono).
- What happens if the user sets width to 200% on already very wide stereo material? System must not clip or distort; proper gain compensation should prevent overload.
- How does width behave with extreme feedback settings (>100%)? Width should apply consistently regardless of feedback amount.

## Requirements *(mandatory)*

### Functional Requirements

#### VST3 Parameter System

- **FR-001**: System MUST add parameter ID `kDigitalWidthId = 612` to the Digital Delay parameter range (600-699) in plugin_ids.h
- **FR-002**: System MUST register the width parameter in Controller::initialize() with display range 0-200%, default 100% (normalized 0.5), unit "%"
- **FR-003**: System MUST handle width parameter changes in Processor::processParameterChanges() by denormalizing to 0-200% and storing in DigitalParams.width atomic
- **FR-004**: Width parameter MUST be automatable in the host DAW (ParameterInfo::kCanAutomate flag)
- **FR-005**: System MUST persist the width parameter value in plugin state (both Processor::getState and Controller::getState)
- **FR-006**: System MUST restore the width parameter value when loading saved state (both Processor::setState and Controller::setComponentState)

#### UI Control

- **FR-007**: System MUST add a horizontal slider control tagged "DigitalWidth" (tag="612") to the DigitalPanel template in editor.uidesc
- **FR-008**: UI control MUST be positioned in the second row of the Digital panel after the OutputLevel control
- **FR-009**: UI control MUST display the current width value as a percentage (0-200%)
- **FR-010**: UI control MUST update in real-time when the parameter is automated from the host DAW
- **FR-011**: User MUST be able to adjust width by clicking and dragging the UI control

#### DSP Processing

- **FR-012**: System MUST add `std::atomic<float> width{100.0f}` field to DigitalParams struct in digital_params.h
- **FR-013**: System MUST apply width control using Mid/Side processing where Mid = (L+R)/2, Side = (L-R)/2, and output is L = Mid + Side × width_factor, R = Mid - Side × width_factor (where width_factor = width / 100.0)
- **FR-014**: System MUST smooth width parameter changes over 20ms using OnePoleSmoother to prevent clicks and artifacts
- **FR-015**: Width control MUST work independently of other digital delay parameters (time, feedback, modulation, etc.)
- **FR-016**: Width parameter MUST apply to the delay output (wet signal), not to the input (dry signal)

### Key Entities *(include if feature involves data)*

- **Width Parameter**: Stereo width control ranging from 0% (mono) to 200% (maximum width), default 100%, stored as normalized value 0.0-1.0 at the VST interface boundary, converted to 0.0-200.0 for internal processing
- **Digital Delay Parameter Set**: Existing collection of digital delay parameters (delay time, feedback, modulation, etc.) to which width will be added

## Success Criteria *(mandatory)*

### Measurable Outcomes

#### UI Interaction
- **SC-001**: User can click and drag the width slider in the UI to adjust width from 0% to 200%
- **SC-002**: UI slider displays the current width value as a percentage in real-time
- **SC-003**: UI slider updates correctly when width parameter is automated from the host DAW

#### DSP Processing
- **SC-004**: Width parameter changes smoothly without audible clicks (no discontinuities when automated)
- **SC-005**: Width at 0% produces mono output (left and right channels are identical within floating-point precision)
- **SC-006**: Width at 100% preserves the original stereo image (no audible change in stereo width from input to delay output)
- **SC-007**: Width at 200% doubles the stereo separation (measured as increased difference between L and R channels)
- **SC-008**: Width parameter smooths over 20ms using OnePoleSmoother (verified through test measurement)

#### State Persistence
- **SC-009**: Width parameter saves correctly in plugin state (both Processor and Controller states match)
- **SC-010**: Width parameter restores correctly when loading saved projects (100% restoration accuracy)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Users understand stereo width as a standard mixing parameter (0-200% range is industry standard)
- The digital delay is used in stereo contexts (mono sources can still benefit from width control on delayed signal if modulation creates stereo content)
- Width control should affect only the wet (delayed) signal, not the dry signal
- The existing parameter ID range for digital delay (600-699) has available IDs for the width parameter

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
| --- | --- | --- |
| PingPongParams.width | src/parameters/pingpong_params.h (line 35) | Reference implementation - same range (0-200%), default 100% |
| StereoField.setWidth() | src/dsp/systems/stereo_field.h (line 142) | Reference implementation of M/S width processing |
| MidSideProcessor | src/dsp/processors/midside_processor.h | May provide M/S encoding/decoding utilities |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Used for parameter smoothing (20ms) |
| DigitalParams struct | src/parameters/digital_params.h (line 28) | Needs width field added |

**Initial codebase search for key terms:**

```bash
# Search for width parameter implementation
grep -r "width" src/parameters/
grep -r "setWidth" src/dsp/
grep -r "Mid.*Side" src/dsp/
```

**Search Results Summary**:
- PingPong delay already has width parameter implementation as reference
- StereoField class provides M/S width processing logic
- MidSideProcessor likely provides encoding/decoding utilities
- Pattern is well-established: width range 0-200%, default 100%, M/S processing

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- Other delay modes (Tape, BBD, Reverse, etc.) may benefit from width control in the future
- Multi-tap delay might want independent width control per tap

**Potential shared components** (preliminary, refined in plan.md):
- Width parameter handling pattern could be extracted as a helper if multiple delays need it
- M/S processing utilities from MidSideProcessor likely already reusable

## Implementation Verification *(mandatory at completion)*

<!--
  CRITICAL: This section MUST be completed when claiming spec completion.
  Constitution Principle XV: Honest Completion requires explicit verification
  of ALL requirements before claiming "done".

  This section is EMPTY during specification phase and filled during
  implementation phase when /speckit.implement completes.
-->

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is ❌ NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 (Parameter ID) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-002 (Controller registration) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-003 (Processor handling) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-004 (Automatable) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-005 (Processor state save) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-006 (State restore) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-007 (UI control in uidesc) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-008 (UI positioning) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-009 (UI display format) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-010 (UI automation update) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-011 (UI interaction) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-012 (DigitalParams field) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-013 (M/S processing) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-014 (Parameter smoothing) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-015 (Independence) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| FR-016 (Wet signal only) | [✅/❌/⚠️/🔄] | [Test name or reason for failure] |
| SC-001 (UI interaction) | [✅/❌/⚠️/🔄] | [Measured value vs target] |
| SC-002 (UI display) | [✅/❌/⚠️/🔄] | [Measured value vs target] |
| SC-003 (UI automation) | [✅/❌/⚠️/🔄] | [Measured value vs target] |
| SC-004 (Smooth changes) | [✅/❌/⚠️/🔄] | [Measured value vs target] |
| SC-005 (0% = mono) | [✅/❌/⚠️/🔄] | [Measured value vs target] |
| SC-006 (100% = original) | [✅/❌/⚠️/🔄] | [Measured value vs target] |
| SC-007 (200% = doubled) | [✅/❌/⚠️/🔄] | [Measured value vs target] |
| SC-008 (20ms smoothing) | [✅/❌/⚠️/🔄] | [Measured value vs target] |
| SC-009 (State save) | [✅/❌/⚠️/🔄] | [Measured value vs target] |
| SC-010 (State restore) | [✅/❌/⚠️/🔄] | [Measured value vs target] |

**Status Key:**
- ✅ MET: Requirement fully satisfied with test evidence
- ❌ NOT MET: Requirement not satisfied (spec is NOT complete)
- ⚠️ PARTIAL: Partially met with documented gap
- 🔄 DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-001 through FR-016 requirements verified against implementation
- [ ] All SC-001 through SC-010 success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim
- [ ] Manual UI test: Width slider exists in Digital panel and functions correctly
- [ ] Manual UI test: Width slider updates when parameter is automated

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
