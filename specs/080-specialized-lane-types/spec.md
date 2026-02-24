# Feature Specification: Specialized Arpeggiator Lane Types

**Feature Branch**: `080-specialized-lane-types`
**Plugin**: Shared (UI components) + Ruinae (layout wiring)
**Created**: 2026-02-24
**Status**: Draft
**Input**: User description: "Arpeggiator Phase 11b: Specialized Lane Types. Implement pitch, ratchet, modifier, and condition lane types with custom rendering and interaction. Extend ArpLaneEditor with bipolar and discrete modes, create ArpModifierLane and ArpConditionLane custom views, integrate all 6 lanes into ArpLaneContainer."
**Depends on**: Phase 11a (079-layout-framework) -- Layout Restructure & Lane Framework (COMPLETE)

## Design Principles (from Arpeggiator Roadmap Phase 11)

- **Shared UI components**: All new arpeggiator UI views and controls MUST be implemented in `plugins/shared/src/ui/`, NOT in `plugins/ruinae/`. These are reusable components (lane editors, lane containers, modifier/condition views) that belong in shared infrastructure. Only Ruinae-specific layout and wiring (e.g., `editor.uidesc`, sub-controller registration) lives in `plugins/ruinae/`.
- **Reuse over rebuild**: Extend `ArpLaneEditor` (which subclasses `StepPatternEditor`) rather than creating from scratch. New custom views only for lane types that fundamentally differ from bar charts (modifier bitmask, condition enum).
- **Stacked multi-lane view**: All lanes visible simultaneously in a vertically scrollable container, sized to content. No tab-switching between lanes.
- **Per-lane playheads**: Each lane tracks its own position independently (polymetric support). Phase 11b adds basic playhead highlighting for the 4 new lanes. Trail and skipped-step X overlay are Phase 11c scope.
- **Left-aligned steps**: Shorter lanes display wider bars/cells. Step 1 always aligns across lanes, making polymetric relationships visually obvious.
- **Collapsible lanes**: Each lane header is clickable to collapse/expand. Collapsed state shows a miniature preview in the lane's accent color.
- **Progressive disclosure**: Warm/primary lanes (velocity, gate) at top; cooler/specialized lanes (modifier, condition) at bottom.

## Clarifications

### Session 2026-02-24

- Q: What interface/pattern should ArpLaneContainer use to hold heterogeneous lane types (ArpLaneEditor, ArpModifierLane, ArpConditionLane)? → A: Option A — introduce a lightweight `IArpLane` pure virtual interface with methods `getView()`, `getExpandedHeight()`, `setPlayheadStep()`, and `setLength()`; all three lane classes implement it; container holds `std::vector<IArpLane*>`.
- Q: What are the concrete playhead parameter IDs for the 4 new lane types? → A: kArpPitchPlayheadId = 3296, kArpRatchetPlayheadId = 3297, kArpModifierPlayheadId = 3298, kArpConditionPlayheadId = 3299 (contiguous block with velocity=3294, gate=3295).
- Q: How many pixels of vertical drag equal one ratchet level change in discrete drag mode? → A: 8px per level (matching approximate visual block height in the ~36px lane; clamping at 1 and 4 still applies).
- Q: Should the collapsible header logic (collapse toggle, name label, length dropdown) be extracted as a shared component or duplicated per lane class? → A: Option A — extract a shared `ArpLaneHeader` helper (non-CView, owned by composition in each lane class) that encapsulates collapse toggle, name label, and length dropdown rendering/interaction; all three lane classes own an `ArpLaneHeader` member; Phase 11c transform buttons added to `ArpLaneHeader` once.
- Q: Must ArpModifierLane and ArpConditionLane have ViewCreator registrations for uidesc configurability? → A: Yes — both MUST have ViewCreator registrations following the same pattern as `ArpLaneEditorCreator`; uidesc-driven configuration is required for consistency with the shared UI component philosophy.

## Lane Color Palette

Cohesive earth-tone family under the arpeggiator copper (#C87850):

| Lane       | Color    | Hex       | Rationale                                          |
|------------|----------|-----------|----------------------------------------------------|
| Velocity   | Copper   | `#D0845C` | Warmest -- primary lane, closest to parent (11a)   |
| Gate       | Sand     | `#C8A464` | Pairs naturally with velocity (11a)                |
| Pitch      | Sage     | `#6CA8A0` | Cool contrast for the "musical" lane               |
| Ratchet    | Lavender | `#9880B0` | Distinctive for the "exotic" rhythmic feature      |
| Modifier   | Rose     | `#C0707C` | Alert-adjacent tone for flags/toggles              |
| Condition  | Slate    | `#7C90B0` | Cool neutral for logic/probability                 |

All colors at similar saturation (~40-50%) and lightness (~55-60%) to avoid visual noise against the #1A1A1E background. Phase 11a registered Velocity and Gate colors; this phase registers the remaining four.

## SEQ Tab Layout (Updated with All 6 Lanes)

```
SEQ Tab (1400 x 620)
+================================================================+ y=0
|  TRANCE GATE  [ON] Steps:[16] [Presets v] [invert][<-][->]     |
|  Sync:[knob] Rate:[1/16 v] Depth:[knob] Atk:[knob] Rel:[knob] |
|  Phase:[knob] [Eucl] Hits:[knob] Rot:[knob]      ~26px toolbar |
|  +----------------------------------------------------------+   |
|  |  Thin StepPatternEditor bars (~70px)                      |   |
|  +----------------------------------------------------------+   |
+================================================================+ y~=104
|  --- divider ---                                                |
+================================================================+ y~=108
|  ARPEGGIATOR  [ON] Mode:[Up v] Oct:[2] [Seq v]                 |
|  Sync:[knob] Rate:[1/16 v] Gate:[knob] Swing:[knob]            |
|  Latch:[Hold v] Retrig:[Note v]                  ~40px toolbar  |
+----------------------------------------------------------------+ y~=148
|                                                                 |
|  SCROLLABLE LANE EDITOR (ArpLaneContainer, ~390px viewport)     |
|  +----------------------------------------------------------+   |
|  | v VEL  [16 v] [copper bars, 0-1 normalized]       ~70px   |   |
|  |                                                            |   |
|  | v GATE [16 v] [sand bars, 0-200% gate length]     ~70px   |   |
|  |                                                            |   |
|  | v PITCH [8 v] [sage bipolar bars, -24..+24 semi]  ~70px   |   |
|  |               center line at 0, bars up/down               |   |
|  |                                                            |   |
|  | v RATCH [8 v] [lavender stacked blocks, 1-4]      ~36px   |   |
|  |               click cycles 1->2->3->4->1                  |   |
|  |                                                            |   |
|  | v MOD  [16 v] [rose 4-row dot grid]               ~44px   |   |
|  |  Rest    .  o  .  .  .  o  .  .  .  .  .  .  o  .        |   |
|  |  Tie     .  .  o--o  .  .  .  .  .  .  o--o  .  .        |   |
|  |  Slide   .  .  .  .  o  .  .  .  .  .  .  .  .  o        |   |
|  |  Accent  o  .  .  .  .  .  o  .  .  o  .  .  .  .        |   |
|  |                                                            |   |
|  | v COND  [8 v] [slate icons + popup menu]          ~28px   |   |
|  |               Alw 50% Alw 25% Ev2 Alw Fill Alw            |   |
|  +----------------------------------------------------------+   |
|  Per-lane playhead highlight (basic, no trail -- trail is 11c)  |
|                                                                 |
+================================================================+ y~=540
|  (Bottom bar -- Euclidean/generative controls, Phase 11c)       |
+================================================================+ y~=620
```

**Lane header legend**: `v` = collapse toggle (down-pointing when expanded, right-pointing `>` when collapsed), `[16 v]` = length dropdown, transform buttons deferred to Phase 11c.

## Lane Height Summary

| Lane       | Expanded Height | Content                                              |
|------------|----------------|------------------------------------------------------|
| Velocity   | ~70px          | Bar chart (0.0-1.0) -- from Phase 11a               |
| Gate       | ~70px          | Bar chart (0-200%) -- from Phase 11a                 |
| Pitch      | ~70px          | Bipolar bars (-24..+24), center line at 0            |
| Ratchet    | ~36px          | Discrete blocks (1-4), click to cycle                |
| Modifier   | ~44px          | 4-row dot toggle grid (Rest/Tie/Slide/Accent)        |
| Condition  | ~28px          | Icon per step + popup menu (18 conditions)           |
| **Headers** | ~16px x 6     | Label, length control                                |
| **Total**  | ~414px         | Fits in 390px viewport with 1 lane collapsed or small scroll |

## Mouse Interaction Summary

| Lane Type            | Click                              | Drag                                  | Right-Click         |
|----------------------|------------------------------------|---------------------------------------|---------------------|
| Bar (Vel/Gate)       | Set step level                     | Paint across steps                    | Set to 0            |
| Bipolar (Pitch)      | Set semitone (snap to integer)     | Paint, up=positive down=negative      | Set to 0            |
| Discrete (Ratchet)   | Cycle 1->2->3->4->1               | Drag up/down; 8px per level, clamped  | Reset to 1          |
| Toggle (Modifier)    | Toggle individual flag dot         | --                                    | --                  |
| Enum (Condition)     | Open COptionMenu popup             | --                                    | Reset to Always     |
| Lane header          | Collapse/expand lane               | --                                    | (Phase 11c: copy/paste) |

## Condition Icons Reference

| Index | Enum Value   | Name     | Abbrev | Icon Concept |
|-------|--------------|----------|--------|--------------|
| 0     | Always       | Always   | Alw    | (bolt)       |
| 1     | Prob10       | 10%      | 10%    | dice         |
| 2     | Prob25       | 25%      | 25%    | dice         |
| 3     | Prob50       | 50%      | 50%    | half         |
| 4     | Prob75       | 75%      | 75%    | three-quarter|
| 5     | Prob90       | 90%      | 90%    | dice         |
| 6     | Ratio_1_2    | Every 2  | Ev2    | 2x           |
| 7     | Ratio_2_2    | 2nd of 2 | 2:2    | ratio        |
| 8     | Ratio_1_3    | Every 3  | Ev3    | 3x           |
| 9     | Ratio_2_3    | 2nd of 3 | 2:3    | ratio        |
| 10    | Ratio_3_3    | 3rd of 3 | 3:3    | ratio        |
| 11    | Ratio_1_4    | Every 4  | Ev4    | 4x           |
| 12    | Ratio_2_4    | 2nd of 4 | 2:4    | ratio        |
| 13    | Ratio_3_4    | 3rd of 4 | 3:4    | ratio        |
| 14    | Ratio_4_4    | 4th of 4 | 4:4    | ratio        |
| 15    | First        | First    | 1st    | (play arrow) |
| 16    | Fill         | Fill     | Fill   | F            |
| 17    | NotFill      | Not Fill | !F     | F strikethrough |

Note: The TrigCondition enum in the engine has 18 values (indices 0-17) matching this table exactly. The condition lane stores indices 0-17 as discrete values per step.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Editing Pitch Lane with Bipolar Values (Priority: P1)

A sound designer opens the SEQ tab and sees a pitch lane displayed as a sage-colored (#6CA8A0) bipolar bar chart. A horizontal center line at y=0 semitones divides the lane. Clicking above the center line creates positive pitch offsets (bars extend upward); clicking below creates negative offsets (bars extend downward). Values snap to integer semitone values in the range -24 to +24. The designer drags across steps to paint a melodic pattern, and the arpeggiator applies these pitch offsets to the notes selected by the NoteSelector.

**Why this priority**: The pitch lane is the most musically expressive of the four new lane types. It validates that ArpLaneEditor can handle bipolar value rendering with a center line, integer snapping, and signed value ranges -- capabilities that do not exist in the Phase 11a bar-based modes. The pitch lane extends ArpLaneEditor's existing architecture rather than requiring a completely new custom view.

**Independent Test**: Can be fully tested by opening the plugin, navigating to the SEQ tab, clicking/dragging on pitch lane bars above and below the center line, and verifying that (a) bars render correctly in both directions, (b) values snap to integer semitones, (c) the corresponding parameter values map correctly to the -24..+24 range, and (d) the arpeggiator output notes are offset by the correct number of semitones.

**Acceptance Scenarios**:

1. **Given** the pitch lane is visible with 8 steps, **When** the user clicks above the center line at approximately the +12 semitone position, **Then** a sage-colored bar extends upward from the center line to the +12 position, and the corresponding parameter (`kArpPitchLaneStep<N>Id`) updates to the normalized value representing +12.
2. **Given** the pitch lane is visible, **When** the user clicks below the center line at approximately the -8 semitone position, **Then** a bar extends downward from the center line to the -8 position.
3. **Given** the pitch lane has steps set to various values, **When** the user right-clicks on any step, **Then** the step resets to 0 semitones (center line, no offset).
4. **Given** the pitch lane has a value of +12.7 (from a drag between grid lines), **When** the value is applied, **Then** it snaps to the nearest integer semitone (+13), not a fractional value.
5. **Given** the pitch lane displays 8 steps and the velocity lane displays 16 steps, **When** both are rendered, **Then** step 1 of each lane is left-aligned and the pitch lane bars are approximately twice as wide as the velocity lane bars.

---

### User Story 2 - Editing Ratchet Lane with Discrete Block Values (Priority: P1)

A sound designer wants to add rhythmic subdivisions to their arpeggiator pattern. They see a ratchet lane displayed in lavender (#9880B0) showing stacked blocks per step. Each step displays 1 to 4 blocks, representing the ratchet count (number of rapid re-triggers per step). Clicking a step cycles the value: 1->2->3->4->1. Dragging up/down on a step changes the block count. Right-clicking resets a step to 1 (no subdivision).

**Why this priority**: The ratchet lane validates that ArpLaneEditor can handle discrete integer values with a non-bar visual representation (stacked blocks). This is the simplest of the non-bar lane types and extends ArpLaneEditor's existing architecture with a discrete mode, establishing the pattern for value cycling and discrete rendering.

**Independent Test**: Can be tested by clicking on ratchet lane steps and verifying (a) the block count cycles correctly, (b) the stacked blocks visualization renders the correct number of blocks per step, (c) the parameter value updates to the correct discrete value (1-4), and (d) the arpeggiator produces the correct number of rapid re-triggers per step during playback.

**Acceptance Scenarios**:

1. **Given** the ratchet lane shows step 3 with value 1 (one block), **When** the user clicks step 3, **Then** the value changes to 2 and two stacked blocks are displayed.
2. **Given** the ratchet lane shows step 3 with value 4 (four blocks), **When** the user clicks step 3, **Then** the value wraps to 1 and one block is displayed.
3. **Given** the ratchet lane shows step 5 with value 2, **When** the user drags upward on step 5, **Then** the value increases to 3 (then 4 if dragging continues).
4. **Given** the ratchet lane shows step 5 with value 3, **When** the user right-clicks step 5, **Then** the value resets to 1 (one block).
5. **Given** the ratchet lane has 8 steps with various values, **When** the lane is rendered, **Then** each step shows the correct number of lavender stacked blocks (1-4), with blocks visually distinct and evenly sized within each step's cell.

---

### User Story 3 - Editing Modifier Lane with Toggle Grid (Priority: P1)

A sound designer wants to set per-step modifier flags for their arpeggiator pattern. They see a modifier lane displayed in rose (#C0707C) as a 4-row dot grid. Each row represents one modifier type: Rest, Tie, Slide, and Accent. Row labels appear on the left margin. Each step has 4 toggle dots (one per row), and clicking a dot toggles it between active (filled circle) and inactive (empty circle). The modifier flags are stored as a bitmask per step, with the arpeggiator engine already supporting these exact flags (kStepActive for Rest inversion, kStepTie, kStepSlide, kStepAccent).

**Why this priority**: The modifier lane is a fundamentally different visual paradigm from bar charts -- it is a 2D grid of toggles rather than a 1D bar chart. This requires a new custom CView (ArpModifierLane) that cannot be implemented as an ArpLaneEditor mode. It is the most complex new view and validates the architecture for non-bar lane types.

**Independent Test**: Can be tested by clicking on individual flag dots in the modifier lane grid and verifying (a) each dot toggles independently, (b) the visual state (filled vs outline) matches the flag state, (c) the bitmask parameter value correctly encodes all 4 flags per step, and (d) the arpeggiator engine applies the correct modifier behavior (rest, tie, slide, accent) during playback.

**Acceptance Scenarios**:

1. **Given** the modifier lane shows 16 steps with all flags inactive, **When** the user clicks the Rest dot on step 3, **Then** the dot becomes filled (active) and the bitmask parameter for step 3 updates to include the Rest flag (bit 0 cleared, since Rest means kStepActive is OFF).
2. **Given** step 5 has Tie active, **When** the user clicks the Slide dot on step 5, **Then** both Tie and Slide dots are filled, and the bitmask reflects both flags (kStepTie | kStepSlide | kStepActive).
3. **Given** step 7 has Accent active, **When** the user clicks the Accent dot again on step 7, **Then** the Accent dot becomes inactive (outline), and the bitmask no longer includes kStepAccent.
4. **Given** the modifier lane has row labels on the left margin, **When** the lane is rendered, **Then** four labels ("Rest", "Tie", "Slide", "Accent") are visible on the left, each aligned with its corresponding row of dots.
5. **Given** the modifier lane has 16 steps, **When** step 1 is toggled and the lane's steps are left-aligned with the velocity lane above (also 16 steps), **Then** the toggle dots align horizontally with the velocity bars above.

---

### User Story 4 - Editing Condition Lane with Enum Popup (Priority: P1)

A sound designer wants to assign conditional triggers to arpeggiator steps. They see a condition lane displayed in slate (#7C90B0) with one icon cell per step. Each cell shows an abbreviated label indicating the active condition (e.g., "Alw" for Always, "50%" for 50% probability, "Ev2" for Every 2nd loop). Clicking a step opens a COptionMenu popup listing all 18 conditions. Right-clicking a step resets it to Always. Hovering shows a tooltip with the full condition name.

**Why this priority**: The condition lane is the second fundamentally different visual paradigm, requiring a new custom CView (ArpConditionLane) with enum popup selection. It validates that the lane framework supports non-editable-via-drag interaction (popup menus only), the correct mapping between the 18 TrigCondition enum values and their visual representation, and tooltip support.

**Independent Test**: Can be tested by clicking on condition lane steps, selecting conditions from the popup, and verifying (a) the popup displays all 18 conditions, (b) the selected condition icon/label updates immediately, (c) the parameter value maps correctly to the TrigCondition enum index (0-17), and (d) the arpeggiator engine evaluates the selected condition during playback.

**Acceptance Scenarios**:

1. **Given** the condition lane shows step 2 set to "Alw" (Always), **When** the user clicks step 2, **Then** a COptionMenu popup appears listing all 18 conditions: Always, 10%, 25%, 50%, 75%, 90%, Every 2, 2nd of 2, Every 3, 2nd of 3, 3rd of 3, Every 4, 2nd of 4, 3rd of 4, 4th of 4, First, Fill, Not Fill.
2. **Given** the condition popup is open for step 2, **When** the user selects "50%", **Then** the popup closes, step 2 displays "50%", and the parameter (`kArpConditionLaneStep1Id`) updates to the normalized value representing TrigCondition::Prob50 (index 3).
3. **Given** step 4 is set to "Ev2" (Every 2), **When** the user right-clicks step 4, **Then** it resets to "Alw" (Always, index 0).
4. **Given** the condition lane shows step 6 set to "Fill", **When** the user hovers over step 6, **Then** a tooltip displays the full name "Fill -- Fires only when fill mode is active".
5. **Given** the condition lane has 8 steps and the velocity lane has 16 steps, **When** both are rendered, **Then** step 1 of each lane is left-aligned and the condition lane cells are approximately twice as wide as the velocity lane bars.

---

### User Story 5 - All 6 Lanes Visible in Stacked Container (Priority: P2)

A sound designer views the full arpeggiator lane editor with all 6 lanes stacked vertically: Velocity, Gate, Pitch, Ratchet, Modifier, Condition (in that order, top to bottom). The total content height (~414px with all lanes expanded) slightly exceeds the ~390px viewport, so the user scrolls with the mouse wheel to see the bottom lanes. Each lane retains its independent length, playhead, and accent color.

**Why this priority**: This validates the integration of all 4 new lane types into the existing ArpLaneContainer from Phase 11a. The stacking order, scroll behavior, and visual coherence across 6 heterogeneous lane types are critical for the complete user experience.

**Independent Test**: Can be tested by opening the SEQ tab and verifying (a) all 6 lanes are visible (some may require scrolling), (b) the display order matches the specification, (c) mouse wheel scrolling works to access lanes below the fold, (d) each lane type renders correctly with its distinct visual style, and (e) mouse events route correctly to each lane regardless of scroll position.

**Acceptance Scenarios**:

1. **Given** all 6 lanes are expanded, **When** the SEQ tab is rendered, **Then** the lanes appear in order: Velocity (copper), Gate (sand), Pitch (sage), Ratchet (lavender), Modifier (rose), Condition (slate).
2. **Given** all 6 lanes are expanded with total content ~414px in a ~390px viewport, **When** the user scrolls down, **Then** the condition lane at the bottom becomes fully visible.
3. **Given** the user has scrolled down to see the condition lane, **When** the user clicks on a condition step, **Then** the click is correctly routed to the condition lane (not intercepted by the container) and the popup opens.
4. **Given** the ratchet lane is partially obscured by scroll, **When** the user collapses the velocity lane (saving ~70px), **Then** the ratchet lane becomes fully visible without scrolling.

---

### User Story 6 - Collapsible Lanes with Miniature Previews (Priority: P2)

A user wants to focus on the pitch and condition lanes. They collapse the velocity, gate, ratchet, and modifier lanes. Each collapsed lane shows a ~16px header with a miniature preview in its accent color. For the pitch lane, the miniature preview shows tiny bipolar bars. For the ratchet lane, the preview shows tiny stacked blocks. For the modifier lane, the preview shows tiny dots indicating which steps have active flags. For the condition lane, the preview shows tiny colored cells. All collapsed previews give the user a quick at-a-glance summary of the pattern without taking up vertical space.

**Why this priority**: Collapsible lanes with previews are essential for managing vertical real estate when all 6 lanes are present. Without collapse, users cannot see all lanes simultaneously. The miniature preview ensures collapsed lanes remain informative.

**Independent Test**: Can be tested by collapsing each lane type individually and verifying (a) the lane collapses to ~16px, (b) the miniature preview renders the correct lane-type-specific visualization, (c) the preview uses the correct accent color, and (d) expanding the lane restores the full editor.

**Acceptance Scenarios**:

1. **Given** the pitch lane is expanded, **When** the user clicks the collapse toggle, **Then** the lane collapses to ~16px showing a miniature bipolar bar preview in sage color.
2. **Given** the ratchet lane is expanded with step values 1,3,2,4,1,2,3,1, **When** the user collapses it, **Then** the miniature preview shows tiny block indicators at the correct relative heights in lavender.
3. **Given** the modifier lane is expanded with some flags active, **When** the user collapses it, **Then** the miniature preview shows tiny dot indicators in rose color where flags are active.
4. **Given** the condition lane is collapsed and then re-expanded, **When** the user clicks the expand toggle, **Then** the condition lane restores to full size (~28px + ~16px header) with all condition icons visible and editable.

---

### User Story 7 - Per-Lane Playhead for New Lane Types (Priority: P2)

A performer starts playback with the arpeggiator active and all 6 lanes visible. Each lane shows a basic step highlight (bright bar/cell overlay) indicating the currently playing step. Because lanes have independent lengths, the playheads advance at the same rate but wrap at different points. For example, with pitch length 8 and velocity length 16, the pitch playhead wraps twice for every full velocity cycle. This is the basic playhead; the fading trail and skipped-step X overlay are deferred to Phase 11c.

**Why this priority**: Playhead visualization provides essential feedback showing which step is currently active in each lane. This is critical for understanding polymetric interactions during live performance.

**Independent Test**: Can be tested by starting playback with the arpeggiator enabled and lanes at different lengths, and verifying that each of the 4 new lane types shows a playhead highlight that advances and wraps independently.

**Acceptance Scenarios**:

1. **Given** playback is active and arp is enabled with pitch lane length 8, **When** the arp advances to step 5, **Then** step 5 in the pitch lane is highlighted with a sage-tinted overlay.
2. **Given** playback is active with ratchet lane length 4, **When** the arp advances to step 5, **Then** the ratchet lane playhead has wrapped to step 1 (5 mod 4 = 1).
3. **Given** playback is active with modifier lane length 16, **When** the arp advances to step 10, **Then** step 10 in the modifier lane is highlighted with a rose-tinted overlay across all 4 modifier rows.
4. **Given** playback is active, **When** playback stops, **Then** all playhead highlights across all 6 lanes are cleared.

---

### Edge Cases

- What happens when a lane's step count is set to the minimum (2 steps)? Each lane type should display 2 very wide bars/cells/dots that fill the entire lane width, maintaining consistent interaction behavior.
- What happens when a pitch lane value is at the extremes (-24 or +24)? The bar should extend to the full height of the lane in the appropriate direction, touching the top or bottom edge.
- What happens when a ratchet lane step is at value 4 and the user drags upward? The value should clamp at 4 (no wrap on drag, only on click).
- What happens when the user clicks between modifier dots (on the grid gap)? The click should be ignored (no toggle).
- What happens when a condition popup is open and the user clicks outside it? The popup should close without changing the condition value.
- What happens when all 6 lanes are collapsed? The total height is ~96px (6 x 16px headers), well within the viewport, with no scrollbar needed.
- What happens when the host automates a modifier lane step parameter with an invalid bitmask value (e.g., >15)? The display should clamp to valid flag combinations (mask with 0x0F).
- What happens when the host automates a condition lane step parameter with a value >= 18 (TrigCondition::kCount)? The display should default to Always (index 0).
- What happens when all 4 modifier flags are active on a single step? The precedence chain (Rest > Tie > Slide > Accent) is handled by the engine; the UI simply displays all 4 dots as filled.
- What happens when the pitch lane is collapsed? The miniature preview should show bipolar bars relative to the center (center = zero semitones), not from the bottom.

## Requirements *(mandatory)*

### Functional Requirements

**Pitch Lane -- ArpLaneEditor Bipolar Mode**

- **FR-001**: ArpLaneEditor MUST support a bipolar rendering mode where a horizontal center line is drawn at the vertical midpoint, representing 0 semitones. Bars extend upward for positive values and downward for negative values from this center line.
- **FR-002**: The pitch lane MUST display values in the range -24 to +24 semitones. The normalized parameter range (0.0 to 1.0) MUST map to -24..+24 with 0.5 representing 0 semitones. The canonical conversion formulas are: `semitones = round((normalized - 0.5f) * 48.0f)` (decode) and `normalized = 0.5f + semitones / 48.0f` (encode). These formulas MUST be used consistently across rendering, interaction, and tests.
- **FR-003**: Pitch lane values MUST snap to the nearest integer semitone. Fractional values from drag positions MUST be rounded to the nearest integer before being applied and stored.
- **FR-004**: Click interaction in the pitch lane MUST set the step value based on the vertical position relative to the center line. Clicking above the center line sets a positive semitone offset; clicking below sets a negative offset.
- **FR-005**: Drag interaction in the pitch lane MUST support paint-across-steps behavior (inherited from StepPatternEditor), with values snapping to integer semitones at each step.
- **FR-006**: Right-click in the pitch lane MUST reset the step value to 0 semitones (center line).
- **FR-007**: The pitch lane MUST display grid labels showing "+24" at the top and "-24" at the bottom, with "0" at the center line.
- **FR-008**: The pitch lane MUST use the Sage accent color (#6CA8A0) with derived normal and ghost variants (using the same darkenColor derivation as Phase 11a velocity/gate lanes).
- **FR-009**: The pitch lane MUST wire to parameter IDs `kArpPitchLaneLengthId` (3100) for step count and `kArpPitchLaneStep0Id` (3101) through `kArpPitchLaneStep31Id` (3132) for step values.
- **FR-010**: When the pitch lane is collapsed, the miniature preview MUST render bipolar bars relative to the center (not from the bottom), showing positive bars above center and negative bars below center, in the sage accent color.

**Ratchet Lane -- ArpLaneEditor Discrete Mode**

- **FR-011**: ArpLaneEditor MUST support a discrete rendering mode that displays stacked blocks (1-4 per step) instead of continuous bars.
- **FR-012**: Each ratchet step MUST display N stacked rectangular blocks where N is the ratchet count (1, 2, 3, or 4). Blocks MUST be visually distinct with a gap between them.
- **FR-013**: Click interaction in the ratchet lane MUST cycle the value: 1->2->3->4->1 (wrapping). A "click" is defined as a mouse-down + mouse-up with less than 4px of total vertical movement, using the same drag-threshold convention as StepPatternEditor. If vertical movement exceeds 4px before mouse-up, the event is treated as a drag (see FR-014) and the click cycle does NOT fire.
- **FR-014**: Drag interaction in the ratchet lane MUST change the value based on vertical drag direction: dragging up increases (clamped at 4), dragging down decreases (clamped at 1). Drag does NOT wrap. The drag sensitivity MUST be 8px of vertical movement per level change (e.g., dragging 16px up increases the value by 2 levels). This threshold matches the approximate visual block height in the ~36px lane.
- **FR-015**: Right-click in the ratchet lane MUST reset the step value to 1 (no subdivision).
- **FR-016**: The ratchet lane MUST use the Lavender accent color (#9880B0) with derived normal and ghost variants.
- **FR-017**: The ratchet lane MUST wire to parameter IDs `kArpRatchetLaneLengthId` (3190) for step count and `kArpRatchetLaneStep0Id` (3191) through `kArpRatchetLaneStep31Id` (3222) for step values.
- **FR-018**: The ratchet lane expanded height MUST be approximately 36px (plus the 16px header), reflecting the smaller vertical space needed for 4 discrete levels compared to a continuous bar chart.
- **FR-019**: When the ratchet lane is collapsed, the miniature preview MUST show tiny block indicators at relative heights (1=25%, 2=50%, 3=75%, 4=100%) in the lavender accent color.

**Modifier Lane -- ArpModifierLane Custom View**

- **FR-020**: ArpModifierLane MUST be a new custom CView (not a subclass of ArpLaneEditor or StepPatternEditor) that renders a 4-row dot toggle grid with one column per step.
- **FR-021**: The 4 rows MUST correspond to the modifier flags in this order (top to bottom): Rest, Tie, Slide, Accent. Each row MUST have a text label on the left margin ("Rest", "Tie", "Slide", "Accent").
- **FR-022**: Each dot MUST be a clickable toggle that alternates between active (filled circle in the rose accent color) and inactive (outline circle in a dimmed color).
- **FR-023**: The modifier lane MUST support a collapsible header (~16px) by owning an `ArpLaneHeader` member (see FR-051). The header displays: collapse toggle triangle, "MOD" lane name in the rose accent color (#C0707C), and a length dropdown. ArpModifierLane MUST delegate all header rendering and hit-testing to its `ArpLaneHeader` member.
- **FR-024**: The modifier lane expanded height MUST be approximately 44px (plus the 16px header).
- **FR-025**: Step values MUST be stored as a bitmask per step using the existing ArpStepFlags encoding: bit 0 = kStepActive (0x01, default ON -- toggling Rest turns this OFF), bit 1 = kStepTie (0x02), bit 2 = kStepSlide (0x04), bit 3 = kStepAccent (0x08). Only bits 0-3 are valid; bits 4-7 MUST be masked off on read (`flags & 0x0F`) before use or storage. The VST3 normalized encoding is `normalizedValue = (flags & 0x0F) / 15.0f` (mapping 0x00..0x0F to 0.0..1.0). The default value kStepActive (0x01) encodes as `1/15.0f`.
- **FR-026**: The modifier lane MUST wire to parameter IDs `kArpModifierLaneLengthId` (3140) for step count and `kArpModifierLaneStep0Id` (3141) through `kArpModifierLaneStep31Id` (3172) for step bitmask values.
- **FR-027**: Steps MUST be left-aligned and column widths MUST match the width of steps in other lanes with the same length, ensuring visual alignment across the stacked lane view.
- **FR-028**: When the modifier lane is collapsed, the miniature preview MUST show a small filled dot in rose color for each step where the step is "non-default" -- defined as: (a) kStepActive is cleared (Rest is active, `flags & 0x01 == 0`), OR (b) any of kStepTie, kStepSlide, or kStepAccent is set (`flags & 0x0E != 0`). Equivalently: `(flags & 0x0F) != 0x01`. Steps at the default value (kStepActive only, no modifiers) show an unfilled/dimmed dot.
- **FR-029**: The modifier lane MUST support a per-lane playhead position with a rose-tinted overlay across all 4 rows of the highlighted step.
- **FR-030**: ArpModifierLane MUST be implemented in `plugins/shared/src/ui/arp_modifier_lane.h` as a shared, plugin-agnostic component. It MUST include a `ArpModifierLaneCreator` ViewCreator registration (following the same `VSTGUI::ViewCreatorAdapter` pattern as `ArpLaneEditorCreator`) so that it can be placed and configured from `editor.uidesc` without C++ sub-controller construction.

**Condition Lane -- ArpConditionLane Custom View**

- **FR-031**: ArpConditionLane MUST be a new custom CView (not a subclass of ArpLaneEditor or StepPatternEditor) that renders one icon/label cell per step.
- **FR-032**: Each step cell MUST display an abbreviated label for the active condition (using the abbreviations from the Condition Icons Reference table: Alw, 10%, 25%, 50%, 75%, 90%, Ev2, 2:2, Ev3, 2:3, 3:3, Ev4, 2:4, 3:4, 4:4, 1st, Fill, !F).
- **FR-033**: Clicking a step MUST open a COptionMenu popup listing all 18 conditions with full names. Selecting a condition updates the step's value and closes the popup.
- **FR-034**: Right-clicking a step MUST reset the condition to Always (index 0).
- **FR-035**: Hovering over a step MUST display a tooltip with the full condition name and description (e.g., "Always -- Step fires unconditionally", "50% -- ~50% probability of firing", "Every 2 -- Fires on 1st of every 2 loops").
- **FR-036**: The condition lane MUST support a collapsible header (~16px) by owning an `ArpLaneHeader` member (see FR-051). The header displays: collapse toggle triangle, "COND" lane name in the slate accent color (#7C90B0), and a length dropdown. ArpConditionLane MUST delegate all header rendering and hit-testing to its `ArpLaneHeader` member.
- **FR-037**: The condition lane expanded height MUST be approximately 28px (plus the 16px header).
- **FR-038**: The condition lane MUST wire to parameter IDs `kArpConditionLaneLengthId` (3240) for step count and `kArpConditionLaneStep0Id` (3241) through `kArpConditionLaneStep31Id` (3272) for step condition values (0-17). The VST3 normalized encoding is `normalizedValue = index / 17.0f` (mapping indices 0..17 to 0.0..1.0). Index 0 (Always) encodes as 0.0f. Index 17 (NotFill) encodes as 1.0f. On decode: `index = clamp(round(normalized * 17.0f), 0, 17)`.
- **FR-039**: Steps MUST be left-aligned and cell widths MUST match the width of steps in other lanes with the same length.
- **FR-040**: When the condition lane is collapsed, the miniature preview MUST show small colored indicators in slate color per step. Non-Always conditions (index != 0) MUST be rendered as filled slate cells. Always conditions (index 0) MUST be rendered as unfilled/dimmed slate cells (outline only or reduced opacity). This filled-vs-unfilled distinction ensures the preview communicates at a glance which steps have active conditions.
- **FR-041**: The condition lane MUST support a per-lane playhead position with a slate-tinted overlay on the highlighted step cell.
- **FR-042**: ArpConditionLane MUST be implemented in `plugins/shared/src/ui/arp_condition_lane.h` as a shared, plugin-agnostic component. It MUST include a `ArpConditionLaneCreator` ViewCreator registration (following the same `VSTGUI::ViewCreatorAdapter` pattern as `ArpLaneEditorCreator`) so that it can be placed and configured from `editor.uidesc` without C++ sub-controller construction.

**Lane Integration**

- **FR-043**: All 6 lanes MUST be integrated into the ArpLaneContainer in the display order: Velocity, Gate, Pitch, Ratchet, Modifier, Condition (top to bottom).
- **FR-044**: The ArpLaneContainer MUST accept ArpModifierLane and ArpConditionLane as child views alongside ArpLaneEditor instances via a lightweight `IArpLane` pure virtual interface (declared in `plugins/shared/src/ui/arp_lane.h`). The interface MUST expose exactly the following methods (see `specs/080-specialized-lane-types/contracts/arp_lane_interface.h` for the authoritative definition): `CView* getView()`, `float getExpandedHeight() const`, `float getCollapsedHeight() const`, `bool isCollapsed() const`, `void setCollapsed(bool)`, `void setPlayheadStep(int32_t step)`, `void setLength(int32_t length)`, and `void setCollapseCallback(std::function<void()>)`. All three lane classes (`ArpLaneEditor`, `ArpModifierLane`, `ArpConditionLane`) MUST implement `IArpLane`. The container MUST hold lanes as `std::vector<IArpLane*>` and dispatch all polymorphic operations through this interface (no dynamic_cast at call sites). Note: ArpLaneContainer::getLane() is for layout purposes only -- plugin-specific lane wiring in controller.cpp uses direct typed pointers, not container->getLane().
- **FR-045**: The 4 new lane colors (Sage #6CA8A0, Lavender #9880B0, Rose #C0707C, Slate #7C90B0) MUST be registered in the Ruinae `editor.uidesc` as named colors for consistent theming.
- **FR-046**: Playhead parameter IDs MUST be added for the 4 new lane types, following the same hidden non-persisted pattern as the Phase 11a velocity and gate playhead parameters. The assigned IDs are: `kArpPitchPlayheadId = 3296`, `kArpRatchetPlayheadId = 3297`, `kArpModifierPlayheadId = 3298`, `kArpConditionPlayheadId = 3299`. Together with kArpVelocityPlayheadId (3294) and kArpGatePlayheadId (3295), these form a contiguous block of 6 playhead parameter IDs (3294–3299). The step encoding for all playhead parameters is: `normalizedValue = (step + 1) / 32.0f` when playhead is active (step 0 = 1/32, step 31 = 1.0), and `0.0f` when there is no active playhead. On decode: `step = round(normalized * 32.0f) - 1`; a result of -1 means no playhead.
- **FR-047**: All parameter wiring for the 4 new lane types MUST support host automation and state save/load. Automation changes MUST update the lane visuals in real-time. State round-trip correctness MUST be verified via a `getState()`/`setState()` test for each new lane type in `ruinae_tests` (see Phase N-1 verification task).
- **FR-048**: The ArpLaneContainer scrolling and collapse behavior from Phase 11a MUST continue to work correctly with all 6 lanes.
- **FR-051**: A shared `ArpLaneHeader` helper MUST be extracted into `plugins/shared/src/ui/arp_lane_header.h` as a non-CView struct owned by composition in each lane class (`ArpLaneEditor`, `ArpModifierLane`, `ArpConditionLane`). It MUST encapsulate: collapse toggle triangle rendering and hit-testing, accent-colored lane name label rendering, and length dropdown (COptionMenu) management. Phase 11c transform buttons MUST be added to `ArpLaneHeader` exclusively — no lane class should duplicate this logic. `ArpLaneEditor` MUST be refactored to delegate its existing header rendering to `ArpLaneHeader` as part of this phase.

**Cross-Lane Alignment**

- **FR-049**: All lanes MUST share the same left edge and right edge for their step content area, regardless of whether the lane has a left margin (e.g., modifier lane row labels). The modifier lane's row labels MUST be rendered within a fixed left margin (kLeftMargin = 40.0f), with the toggle dots starting at the same horizontal position as bars/cells in other lanes at the same step count. This alignment MUST be verified by a unit test (see T088) that measures the step-content x-origin of ArpLaneEditor (kPitch, 8 steps) and ArpModifierLane (8 steps) and confirms they are equal.
- **FR-050**: For lanes with the same step count, the step boundaries MUST align vertically (step N in one lane is directly above/below step N in another lane).

### Key Entities

- **IArpLane (new)**: Lightweight pure virtual interface declaring the polymorphic contract for all lane types. The authoritative method list is in `specs/080-specialized-lane-types/contracts/arp_lane_interface.h` (8 pure virtual methods: getView, getExpandedHeight, getCollapsedHeight, isCollapsed, setCollapsed, setPlayheadStep, setLength, setCollapseCallback). Located at `plugins/shared/src/ui/arp_lane.h`. All three concrete lane classes implement this interface. ArpLaneContainer holds `std::vector<IArpLane*>` and never dynamic_casts at call sites.
- **ArpLaneHeader (new)**: Non-CView helper struct owned by composition in each lane class. Encapsulates collapse toggle triangle, accent-colored name label, and length dropdown rendering/interaction. Eliminates header logic duplication across `ArpLaneEditor`, `ArpModifierLane`, and `ArpConditionLane`. Phase 11c transform buttons will be added here exclusively. Located in `plugins/shared/src/ui/arp_lane_header.h`.
- **ArpLaneEditor (extended)**: Existing Phase 11a shared UI component, now extended with bipolar mode (pitch) and discrete mode (ratchet) in addition to existing bar mode (velocity/gate). Implements `IArpLane`. Refactored to delegate header rendering to `ArpLaneHeader`. Located in `plugins/shared/src/ui/arp_lane_editor.h`.
- **ArpModifierLane (new)**: Custom CView for the 4-row toggle dot grid representing per-step modifier flags (Rest/Tie/Slide/Accent as bitmask). Implements `IArpLane`. Located in `plugins/shared/src/ui/arp_modifier_lane.h`.
- **ArpConditionLane (new)**: Custom CView for the per-step condition enum selection with icon display and popup menu (18 TrigCondition values). Implements `IArpLane`. Located in `plugins/shared/src/ui/arp_condition_lane.h`.
- **ArpLaneContainer (extended)**: Existing Phase 11a scrollable stacking container, now holding lanes via `std::vector<IArpLane*>` to accommodate heterogeneous lane types (ArpLaneEditor, ArpModifierLane, ArpConditionLane).
- **TrigCondition enum**: Existing DSP engine enum (18 values, 0-17) that maps 1:1 with condition lane values. Located in `dsp/include/krate/dsp/processors/arpeggiator_core.h`.
- **ArpStepFlags**: Existing DSP engine bitmask (kStepActive=0x01, kStepTie=0x02, kStepSlide=0x04, kStepAccent=0x08) that maps 1:1 with modifier lane bitmask values. Located in `dsp/include/krate/dsp/processors/arpeggiator_core.h`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 6 lanes are visible, editable, and independently functional in the stacked lane editor. Users can edit values in each lane type and hear the corresponding changes during playback.
- **SC-002**: Pitch lane bipolar rendering is correct: center line visible at y=0, bars extend up for positive and down for negative semitone values, values snap to integer semitones within the -24..+24 range.
- **SC-003**: Ratchet lane discrete rendering is correct: click cycles 1->2->3->4->1, stacked blocks visualization shows the correct number of blocks (1-4) per step, drag up/down changes value with clamping.
- **SC-004**: Modifier lane toggle grid is correct: 4-row dot layout with Rest/Tie/Slide/Accent labels, each flag independently toggleable per step, bitmask encoding matches ArpStepFlags exactly.
- **SC-005**: Condition lane enum popup is correct: click opens COptionMenu with all 18 conditions, selection updates the step immediately, abbreviation labels display correctly, right-click resets to Always.
- **SC-006**: All lanes collapse/expand with lane-type-specific miniature previews: bipolar bars for pitch, block indicators for ratchet, dot indicators for modifier, colored cells for condition.
- **SC-007**: Left-alignment is correct: step 1 lines up vertically across all 6 lanes regardless of different lane lengths. For lanes with the same length, step boundaries align exactly.
- **SC-008**: All parameter wiring is functional: host automation reads/writes update all 4 new lane types in real-time, and state save/load round-trips all lane values correctly.
- **SC-009**: No memory allocations occur in draw, mouse interaction, or playhead update paths. Verified by code inspection or ASan testing.
- **SC-010**: Pluginval level 5 passes with all 6 lanes active and populated with non-default values.
- **SC-011**: Per-lane playhead highlights appear for all 4 new lane types during playback, with each lane's playhead wrapping independently at its own length.
- **SC-012**: Zero compiler warnings across all modified files.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Phase 11a (079-layout-framework) is complete: ArpLaneEditor, ArpLaneContainer, velocity lane, gate lane, and basic playhead infrastructure are all functional and tested.
- All arp engine features (Phases 1-10) are stable: held note buffer, note selector, timing, lanes, modifiers, ratcheting, Euclidean, conditions, spice/dice/humanize, and modulation integration are all working.
- Parameter IDs for all 4 new lane types already exist in `plugin_ids.h` and are registered in `arpeggiator_params.h` (verified: pitch 3100-3132, modifier 3140-3172, ratchet 3190-3222, condition 3240-3272).
- The TrigCondition enum (18 values) and ArpStepFlags bitmask (4 flags) in the DSP engine are stable and will not change.
- Playhead parameter IDs for the 4 new lanes MUST be added to `plugin_ids.h`: `kArpPitchPlayheadId = 3296`, `kArpRatchetPlayheadId = 3297`, `kArpModifierPlayheadId = 3298`, `kArpConditionPlayheadId = 3299`. These extend the existing contiguous block (kArpVelocityPlayheadId = 3294, kArpGatePlayheadId = 3295).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| IArpLane | `plugins/shared/src/ui/arp_lane.h` | CREATE: Lightweight pure virtual interface; all lane classes implement it; container holds `std::vector<IArpLane*>` |
| ArpLaneHeader | `plugins/shared/src/ui/arp_lane_header.h` | CREATE: Non-CView helper struct owned by composition in each lane class; encapsulates collapse toggle, name label, length dropdown; Phase 11c transform buttons added here |
| ArpLaneEditor | `plugins/shared/src/ui/arp_lane_editor.h` | EXTEND: Add bipolar and discrete rendering modes; implement IArpLane; refactor header rendering to delegate to ArpLaneHeader |
| ArpLaneContainer | `plugins/shared/src/ui/arp_lane_container.h` | EXTEND: Accept heterogeneous lane types (modifier, condition) |
| StepPatternEditor | `plugins/shared/src/ui/step_pattern_editor.h` | REFERENCE: Base class for ArpLaneEditor, provides bar chart rendering, mouse interaction, zoom/scroll, parameter callbacks |
| color_utils.h | `plugins/shared/src/ui/color_utils.h` | REUSE: darkenColor() for deriving normal/ghost color variants from accent colors |
| ArpLaneEditorCreator | `plugins/shared/src/ui/arp_lane_editor.h` | EXTEND: Add new lane-type options (pitch, ratchet) to the ViewCreator |
| ArpModifierLaneCreator | `plugins/shared/src/ui/arp_modifier_lane.h` | CREATE: ViewCreator registration for ArpModifierLane (same ViewCreatorAdapter pattern as ArpLaneEditorCreator) |
| ArpConditionLaneCreator | `plugins/shared/src/ui/arp_condition_lane.h` | CREATE: ViewCreator registration for ArpConditionLane (same ViewCreatorAdapter pattern as ArpLaneEditorCreator) |
| ArpStepFlags | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | REFERENCE: Bitmask values for modifier lane encoding |
| TrigCondition | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | REFERENCE: Enum values for condition lane encoding |
| Parameter IDs (pitch lane) | `plugins/ruinae/src/plugin_ids.h` (3100-3132) | WIRE: Already registered, needs UI binding |
| Parameter IDs (modifier lane) | `plugins/ruinae/src/plugin_ids.h` (3140-3172) | WIRE: Already registered, needs UI binding |
| Parameter IDs (ratchet lane) | `plugins/ruinae/src/plugin_ids.h` (3190-3222) | WIRE: Already registered, needs UI binding |
| Parameter IDs (condition lane) | `plugins/ruinae/src/plugin_ids.h` (3240-3272) | WIRE: Already registered, needs UI binding |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | REFERENCE: Parameter handling, state serialization for all lane types |
| COptionMenu | `extern/vst3sdk/vstgui4/vstgui/lib/controls/coptionmenu.cpp` | REUSE: Popup menu for condition lane and length dropdowns |

**Search Results Summary**: All lane parameter IDs (pitch 3100-3132, modifier 3140-3172, ratchet 3190-3222, condition 3240-3272) are already defined in `plugin_ids.h` and registered in `arpeggiator_params.h` with correct discrete ranges. No existing ArpModifierLane or ArpConditionLane implementations found -- these are new. The existing ArpLaneEditor has placeholders for kPitch and kRatchet lane types but no bipolar/discrete rendering logic yet.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Phase 11c (Interaction Polish): Will add per-lane transform buttons (invert, shift, randomize), copy/paste, playhead trail, and skipped-step indicators. All 6 lane types must support these operations.
- Future plugins with lane-based sequencers could reuse ArpModifierLane and ArpConditionLane.

**Potential shared components** (preliminary, refined in plan.md):
- ArpModifierLane and ArpConditionLane MUST have ViewCreator registrations (`ArpModifierLaneCreator`, `ArpConditionLaneCreator`) following the same `VSTGUI::ViewCreatorAdapter` pattern as `ArpLaneEditorCreator` (see FR-030, FR-042). This is required for uidesc configurability and consistency with the shared UI component philosophy.
- The collapsible header design (triangle toggle, name label, length dropdown) is shared across all lane types. A shared `ArpLaneHeader` helper (non-CView, owned by composition) MUST be extracted in this phase. `ArpLaneEditor` MUST be refactored to use it. Phase 11c transform buttons will be added to `ArpLaneHeader` exclusively (see FR-051).
- The lane playhead highlight pattern (per-lane parameter, step overlay) should be consistent across all lane types for Phase 11c trail support.

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
| FR-026 | | |
| FR-027 | | |
| FR-028 | | |
| FR-029 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-034 | | |
| FR-035 | | |
| FR-036 | | |
| FR-037 | | |
| FR-038 | | |
| FR-039 | | |
| FR-040 | | |
| FR-041 | | |
| FR-042 | | |
| FR-043 | | |
| FR-044 | | |
| FR-045 | | |
| FR-046 | | |
| FR-047 | | |
| FR-048 | | |
| FR-049 | | |
| FR-050 | | |
| FR-051 | | |
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
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
