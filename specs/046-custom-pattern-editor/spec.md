# Feature Specification: Custom Tap Pattern Editor

**Feature Branch**: `046-custom-pattern-editor`
**Created**: 2026-01-04
**Status**: Draft
**Input**: User description: "Custom tap pattern editor for MultiTap delay mode"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Visual Pattern Editing (Priority: P1)

As a music producer using MultiTap delay, I want to visually create custom delay tap patterns by dragging tap positions and levels on a timeline, so I can design unique rhythmic echo patterns that match my creative vision without relying on preset mathematical patterns.

**Why this priority**: This is the core value proposition of the feature. Without visual editing, users have no way to create custom patterns beyond the preset mathematical options.

**Independent Test**: Can be fully tested by opening the MultiTap panel, selecting "Custom" pattern, and dragging taps to create a pattern that produces audible delays at the specified times and levels.

**Acceptance Scenarios**:

1. **Given** the MultiTap delay is active with Custom pattern selected, **When** I drag a tap handle horizontally, **Then** the tap's delay time changes proportionally to its position on the timeline
2. **Given** the MultiTap delay is active with Custom pattern selected, **When** I drag a tap handle vertically, **Then** the tap's level (volume) changes proportionally to its height
3. **Given** I have modified tap positions, **When** I play audio through the delay, **Then** I hear the delay taps at the exact timing and volume I configured

---

### User Story 2 - Pattern Persistence (Priority: P2)

As a music producer, I want my custom tap patterns to be saved with my presets and recalled correctly when I reload a project, so I don't lose my creative work.

**Why this priority**: Without persistence, custom patterns would be useless in real-world production workflows where sessions are saved and reopened.

**Independent Test**: Can be tested by creating a custom pattern, saving a preset, reloading the plugin, loading the preset, and verifying the pattern is restored.

**Acceptance Scenarios**:

1. **Given** I have created a custom tap pattern, **When** I save the plugin preset, **Then** all tap positions and levels are stored
2. **Given** a saved preset with a custom pattern, **When** I load that preset in a new session, **Then** the custom pattern is restored exactly as I saved it

---

### User Story 3 - Grid Snapping (Priority: P2)

As a music producer, I want optional grid snapping when editing tap positions, so I can create rhythmically precise patterns aligned to beat subdivisions.

**Why this priority**: Grid snapping is essential for creating musically coherent patterns, but the editor is still usable without it.

**Independent Test**: Can be tested by enabling snap, dragging a tap, and verifying it snaps to the nearest grid line.

**Acceptance Scenarios**:

1. **Given** grid snapping is enabled at 1/8 note divisions, **When** I drag a tap, **Then** it snaps to the nearest 1/8 note position
2. **Given** grid snapping is disabled, **When** I drag a tap, **Then** it moves freely to any position

---

### User Story 4 - Copy from Mathematical Pattern (Priority: P3)

As a music producer, I want to copy the current mathematical pattern (Golden Ratio, Fibonacci, etc.) to the custom editor as a starting point, so I can make small modifications to an algorithmically-generated pattern.

**Why this priority**: This is a convenience feature that speeds up workflow but isn't essential for core functionality.

**Independent Test**: Can be tested by selecting a mathematical pattern, choosing "Copy to Custom", and verifying the tap positions match.

**Acceptance Scenarios**:

1. **Given** a mathematical pattern is selected (e.g., Golden Ratio), **When** I click "Copy to Custom", **Then** the custom editor is populated with tap positions matching the mathematical pattern
2. **Given** I have copied a pattern to custom, **When** I modify taps, **Then** the changes do not affect the original mathematical pattern

---

### Edge Cases

#### State & Layout
- What happens when tap count is changed while editing? Editor updates to show only active taps
- What happens when two taps are dragged to the same position? They overlap visually; DSP processes both at same time
- What happens when a tap level is set to 0%? The tap is silent but still visible in the editor
- How does the editor handle very narrow window widths? Minimum width of 200px enforced; taps compressed proportionally below that threshold

#### Mouse Interaction - Boundary Handling
- What happens when user drags a tap outside the editor bounds? Values are clamped to 0-100% range; tracking continues until mouse release
- What happens when mouse is released outside the editor? Edit ends normally; clamped values are committed
- What happens when dragging beyond time < 0% or > 100%? Time ratio clamped to valid 0.0-1.0 range
- What happens when dragging beyond level < 0% or > 100%? Level clamped to valid 0.0-1.0 range

#### Mouse Interaction - Modifiers & Gestures
- What happens on right-click on a tap? Ignored (no context menu in v1)
- What happens on double-click on a tap? Resets that tap to default position (evenly spaced) and full level (100%)
- What happens when Escape is pressed during a drag? Current drag is cancelled; tap returns to pre-drag value
- What happens when Shift is held during drag? Constrains movement to single axis (horizontal OR vertical, whichever has larger delta)

#### Context Changes During Editing
- What happens if pattern selection changes during drag? Current drag is cancelled; editor updates to reflect new pattern (may hide if not Custom)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST display a visual timeline showing all active tap positions and levels
- **FR-002**: System MUST allow users to drag tap handles horizontally to adjust timing (time ratio 0-100%)
- **FR-003**: System MUST allow users to drag tap handles vertically to adjust level (amplitude 0-100%)
- **FR-004**: System MUST provide optional grid snapping with selectable divisions (1/4, 1/8, 1/16, 1/32, triplets)
- **FR-005**: System MUST persist custom pattern data (all tap times and levels) with preset save/load
- **FR-006**: System MUST update the DSP engine in real-time as taps are dragged
- **FR-007**: System MUST display the current tap count based on the existing Tap Count parameter
- **FR-008**: System MUST show/hide the editor based on pattern selection (visible only when Custom pattern is selected)
- **FR-009**: System MUST provide a way to copy the current mathematical pattern to the custom editor
- **FR-010**: System MUST provide a reset function to restore default evenly-spaced tap pattern

### Key Entities

- **Tap**: A single delay echo point with properties: time ratio (0-1), level (0-1)
- **Custom Pattern**: A collection of up to 16 taps with their time and level values
- **Grid Division**: The quantization setting for snap-to-grid (e.g., 1/8 note = 8 divisions per bar)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can create a custom 8-tap pattern within 30 seconds of first opening the editor
- **SC-002**: Custom patterns persist correctly across 100% of preset save/load cycles
- **SC-003**: Tap drag operations complete with visual feedback in under 16ms (60fps responsiveness)
- **SC-004**: All 16 tap positions and levels are accurately recalled after project reload (no data loss)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- User has MultiTap delay mode active
- User understands basic delay concepts (time, feedback, taps)
- Pattern == Custom (index 19) enables the custom editor visibility
- Tap count is controlled by existing parameter (2-16 taps range)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| ModeTabBar | plugins/iterum/src/ui/mode_tab_bar.h | Reference for CView custom drawing |
| PresetBrowserView | plugins/iterum/src/ui/preset_browser_view.h | Reference for complex custom views |
| MultiTapDelay | dsp/include/krate/dsp/effects/multi_tap_delay.h | DSP to receive custom pattern data |
| kMultiTapTapCountId | plugins/iterum/src/plugin_ids.h | Existing tap count parameter to reuse |
| kMultiTapPatternId | plugins/iterum/src/plugin_ids.h | Pattern selector (Custom = index 19) |
| multitap_params.h | plugins/iterum/src/parameters/multitap_params.h | Add custom pattern parameters here |

**Initial codebase search for key terms:**

```bash
grep -r "TapPattern" dsp/ plugins/
grep -r "CustomPattern" dsp/ plugins/
grep -r "kMultiTapCustom" plugins/
```

**Search Results Summary**: No existing custom pattern editor implementation. MultiTapDelay has `setCustomPattern()` method accepting time ratios. Need to extend for levels.

### Forward Reusability Consideration

**Sibling features at same layer**:
- Potential step sequencer views for other modes
- Pattern visualization for rhythmic effects

**Potential shared components**:
- Grid drawing utilities could be shared across pattern editors
- Drag handle hit-testing logic could be reused

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

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
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |

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
