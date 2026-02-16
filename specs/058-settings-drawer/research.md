# Research: Settings Drawer

**Spec**: 058-settings-drawer | **Date**: 2026-02-16

## Research Summary

This spec involves minimal unknowns since it follows well-established patterns in the codebase. All 6 DSP engine methods already exist and are tested. The work is parameter plumbing + UI drawer.

## Decision 1: Drawer Animation Approach

**Decision**: Timer-driven animation using `CVSTGUITimer` with `setViewSize()` updates at ~60fps (16ms interval).

**Rationale**: The project already uses `CVSTGUITimer` for the trance gate playback poll timer (controller.cpp:419). This is the established VSTGUI timer pattern. The animation moves the drawer container's x-position from 925 (off-screen) to 705 (open) over 160ms with ease-out interpolation. `setViewSize()` is the same approach used by `toggleEnvExpand()` for resizing envelope groups.

**Alternatives considered**:
- VSTGUI's built-in `CAnimation` framework: More complex, not used elsewhere in the project. Would introduce a new pattern without benefit.
- Immediate show/hide (no animation): Functional but spec requires smooth slide-out with ease-out.
- CSS-style transitions: Not available in VSTGUI.

## Decision 2: Click-Outside-to-Close Mechanism

**Decision**: Use a transparent full-window `ToggleButton` overlay placed between the main content and the drawer in z-order. When clicked, it triggers `toggleSettingsDrawer()` to close the drawer.

**Rationale**: VSTGUI's z-ordering means later children draw (and receive mouse events) on top of earlier children. A transparent ToggleButton with no icon style and no title is visually invisible but captures mouse clicks. The project's ToggleButton class only draws recognized icon styles (power, chevron, gear, funnel) -- an empty icon-style draws nothing. This overlay becomes visible when the drawer opens and hidden when it closes.

**Alternatives considered**:
- `IMouseObserver` on CFrame: Not a standard VSTGUI interface; would require framework extension.
- Subclassed CViewContainer as click catcher: Requires a new custom view class just for mouse catching.
- Mouse event tracking in verifyView: Fragile and hard to maintain.

## Decision 3: Actual Window Dimensions

**Decision**: Use 925x880 (actual uidesc values) instead of 900x866 (spec approximation).

**Rationale**: The uidesc `editor` template specifies `minSize="925, 880"`, `maxSize="925, 880"`, `size="925, 880"`. The spec's "900x866" was an approximation. All drawer geometry calculations use 925x880:
- Drawer width: 220px
- Closed x: 925 (off-screen right)
- Open x: 705 (925 - 220)
- Drawer height: 880 (full window height)

## Decision 4: Gain Compensation Default Strategy

**Decision**: New param default = ON (1.0). Old presets (version < 14) explicitly set to OFF in setState().

**Rationale**: The spec requires:
- New presets: gain comp ON (musically expected -- prevents volume jumps)
- Old presets: gain comp OFF (preserves existing behavior -- the hardcoded `false`)

The `SettingsParams` struct defaults `gainCompensation{true}` for new instances. In `setState()`, when `version < 14`, we explicitly store `false`. In `setComponentState()`, we call `setParam(kSettingsGainCompensationId, 0.0)` for `version < 14`.

## Decision 5: Parameter Registration Types

**Decision**: Use VST3 SDK parameter types matching the control requirements:

| Parameter | VST3 Type | Mapping | stepCount |
|-----------|-----------|---------|-----------|
| Pitch Bend Range | `Parameter` (basic) | Linear 0-24 | 24 (integer steps) |
| Velocity Curve | `StringListParameter` via `createDropdownParameter` | 4 items | 3 (auto) |
| Tuning Reference | `Parameter` (basic) | Linear 400-480 Hz | 0 (continuous) |
| Voice Allocation | `StringListParameter` via `createDropdownParameterWithDefault` | 4 items, default=1 | 3 (auto) |
| Voice Steal Mode | `StringListParameter` via `createDropdownParameter` | 2 items | 1 (auto) |
| Gain Compensation | `Parameter` (basic) | Boolean toggle | 1 (on/off) |

**Rationale**: This matches the exact patterns used by existing parameters. StringListParameter handles its own display formatting (dropdown items). Basic Parameter with stepCount provides integer stepping for pitch bend range and boolean for gain comp. The tuning reference is continuous (stepCount=0) for fine control.

**Why NOT RangeParameter for Pitch Bend Range and Tuning Reference**: Looking at the existing codebase, `RangeParameter` is not used in the Ruinae parameter files. All parameters use basic `Parameter` with manual normalization in the handle/format functions. This is the established pattern (see `global_filter_params.h` line 54 for cutoff using basic Parameter with log mapping). Using `RangeParameter` would introduce a new pattern. Staying consistent with existing code.

## Decision 6: Ease-Out Interpolation Formula

**Decision**: Quadratic ease-out: `eased = 1 - (1-t)^2` where t is linear progress [0,1].

**Rationale**: This is a standard ease-out curve that starts fast and decelerates. It provides a polished feel without being overly complex. The 160ms duration at ~60fps means approximately 10 animation frames, which is sufficient for perceived smoothness with quadratic easing.

For reverse animation (closing): the linear progress decreases from current value toward 0. The same ease-out formula is applied to the current progress value, providing symmetric deceleration.

## No Further Research Needed

All unknowns from the Technical Context section have been resolved:
- DSP methods: Already exist, signatures verified
- Parameter types: Determined from spec requirements and existing patterns
- Animation approach: Timer-based, matches project patterns
- Click-outside mechanism: Transparent overlay ToggleButton
- Window geometry: Measured from actual uidesc (925x880)
- Backward compatibility: Explicit defaults in setState for old presets
