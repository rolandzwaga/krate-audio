# Research: Mod Source Dropdown Selector

**Date**: 2026-02-14 | **Spec**: [spec.md](spec.md)

## R-001: UIViewSwitchContainer Pattern for Mod Source Switching

**Decision**: Reuse the exact same UIViewSwitchContainer + COptionMenu + StringListParameter pattern already used for Distortion, Filter, Oscillator, and Delay type switching in Ruinae.

**Rationale**: The codebase already has 7+ working instances of this pattern. The pattern is:
1. A `StringListParameter` registered in the controller defines the list entries and parameter ID
2. A `COptionMenu` in the uidesc XML is bound to the parameter via `control-tag`
3. A `UIViewSwitchContainer` in the uidesc XML references the same parameter via `template-switch-control`
4. Named templates are listed in the `template-names` attribute, one per dropdown entry

VSTGUI's `UIDescriptionViewSwitchController` automatically:
- Registers as `IControlListener` on the `COptionMenu` control
- Converts the normalized parameter value to a template index
- Switches to the corresponding template view

No custom controller code is needed for the view switching itself.

**Alternatives considered**:
- Keep manual `setVisible()` approach: Rejected because it does not scale to 10+ views and is inconsistent with the rest of the codebase
- Sub-controller pattern: Not needed here since each mod source template uses unique per-source control tags (not remapped generic tags)

## R-002: COptionMenu Styling to Match Distortion Type Dropdown

**Decision**: Use identical XML attributes as the Distortion Type `COptionMenu` at line 1761 of `editor.uidesc`.

**Rationale**: The spec (FR-001) explicitly requires matching the existing Distortion Type dropdown styling.

**Reference styling** (from `editor.uidesc:1761-1764`):
```xml
<view class="COptionMenu" origin="..." size="..., 18"
      control-tag="..." default-value="0"
      font="~ NormalFontSmaller" font-color="text-menu"
      back-color="bg-dropdown" frame-color="frame-dropdown"/>
```

Width will be adjusted to fit the mod source area (158px available; dropdown will be approximately 140px wide with some margin).

**Alternatives considered**:
- Custom styled dropdown: Rejected, spec explicitly says match existing style

## R-003: StringListParameter Extension from 3 to 10 Entries

**Decision**: Modify the `registerChaosModParams()` function in `chaos_mod_params.h` to add 7 additional `appendString()` calls to the existing `StringListParameter`.

**Rationale**: The parameter is already registered there (lines 65-70). Simply adding more entries is the minimal change.

**New entries** (in order):
1. "LFO 1" (existing, index 0)
2. "LFO 2" (existing, index 1)
3. "Chaos" (existing, index 2)
4. "Macros" (new, index 3)
5. "Rungler" (new, index 4)
6. "Env Follower" (new, index 5)
7. "S&H" (new, index 6)
8. "Random" (new, index 7)
9. "Pitch Follower" (new, index 8)
10. "Transient" (new, index 9)

**Impact on normalized value calculation**: `StringListParameter` automatically handles the `stepCount` (N-1 = 9) and normalizes correctly. The old manual formula `std::round(value * 2.0)` in the controller will be removed entirely since UIViewSwitchContainer handles the index calculation internally.

**Alternatives considered**:
- Create a separate parameter file for the mod source view mode: Rejected, it is a single parameter already co-located with chaos mod params

## R-004: Template Extraction for LFO1, LFO2, and Chaos Views

**Decision**: Extract the inline `CViewContainer` blocks from the main editor template into named templates placed after the Delay templates (around line 1470) and before the editor template (line 1472).

**Rationale**: The UIViewSwitchContainer requires named templates. The content is currently inline. Extraction follows the same pattern used for OscA/OscB, Filter, and Distortion templates.

**Template sizing**: The LFO views are `size="158, 120"`, and the Chaos view is `size="158, 106"`. The UIViewSwitchContainer size will be `158, 120` (the maximum). The Chaos template will be smaller but contained within the switch container.

**Key consideration**: The extracted templates must preserve `custom-view-name` attributes on internal views (e.g., `LFO1RateGroup`, `LFO1NoteValueGroup`, `ChaosRateGroup`, etc.) so the Rate/NoteValue sync-swap visibility logic in the controller continues to work within the extracted templates. The controller's `verifyView()` method resolves `custom-view-name` when the template is instantiated by the UIViewSwitchContainer.

**Alternatives considered**:
- Leave views inline and wrap with UIViewSwitchContainer: Not possible; `UIViewSwitchContainer` requires `template-names` referencing external named templates

## R-005: Controller Code Removal

**Decision**: Remove the following from the controller:

1. **Member variables** (`controller.h:226-229`):
   - `modLFO1View_`
   - `modLFO2View_`
   - `modChaosView_`

2. **valueChanged logic** (`controller.cpp:510-516`):
   - The entire `if (tag == kModSourceViewModeTag)` block that manually toggles visibility

3. **createView/verifyView logic** (`controller.cpp:824-840`):
   - The `"ModLFO1View"`, `"ModLFO2View"`, `"ModChaosView"` custom-view-name handling branches

4. **willClose cleanup**: The `modLFO*View_` pointers are NOT currently nulled in `willClose()` (oversight), but since we are removing them entirely, this is moot.

**Rationale**: The UIViewSwitchContainer handles view switching automatically. Manual visibility management is redundant and creates maintenance burden when adding new sources.

**Risk assessment**: LOW. The Rate/NoteValue sync-swap logic for LFO1, LFO2, and Chaos (which uses `custom-view-name` attributes like `LFO1RateGroup`, `LFO1NoteValueGroup`, etc.) is orthogonal to the view switching. Those `custom-view-name` handlers in `verifyView()` remain unchanged and will continue to work when the templates are instantiated by the UIViewSwitchContainer.

**Alternatives considered**:
- Keep as dead code: Rejected, constitution requires clean removal

## R-006: Placeholder Template Design

**Decision**: Create 7 empty `CViewContainer` templates, one per future source:
- `ModSource_Macros`
- `ModSource_Rungler`
- `ModSource_EnvFollower`
- `ModSource_SampleHold`
- `ModSource_Random`
- `ModSource_PitchFollower`
- `ModSource_Transient`

Each template is a minimal empty container:
```xml
<template name="ModSource_Macros" size="158, 120" class="CViewContainer" transparent="true"/>
```

**Rationale**: Spec (FR-009) requires separate templates for each future source. Empty containers are safe (no crashes, no visual glitches). Future phases replace the empty templates with populated layouts.

**Alternatives considered**:
- Single shared placeholder: Rejected by spec clarification (7 separate templates required)
- Placeholder with "Coming Soon" text: Rejected by spec clarification (completely empty views)

## R-007: Layout Geometry

**Decision**: The dropdown occupies approximately `origin="0, 14" size="140, 18"` within the FieldsetContainer. The UIViewSwitchContainer is placed at approximately `origin="0, 36" size="158, 120"`.

**Rationale**:
- The FieldsetContainer starts at y=14 for content (below the title)
- The IconSegmentButton was at `origin="0, 14" size="158, 18"` (18px height)
- The COptionMenu replacement occupies the same vertical space (18px)
- Content area starts at y=36 (14 + 18 + 4px gap), same as the current inline views
- Total height: 18px (dropdown) + ~4px gap + 120px (view) = ~142px, within the 160px FieldsetContainer

The dropdown width of 140px (vs the 158px of the former segment button) provides slight visual margin within the FieldsetContainer. This matches how other COptionMenu dropdowns in the UI have slightly less width than their containing areas.

**Alternatives considered**:
- Full-width 158px dropdown: Would work but might look cramped against the FieldsetContainer edges

## Post-Implementation Notes (2026-02-14)

### Implementation Outcome

The migration went as planned with no unexpected behavior or workarounds required. Key observations:

1. **UIViewSwitchContainer works correctly with extracted templates**: The `custom-view-name` attributes (`LFO1RateGroup`, `LFO1NoteValueGroup`, etc.) continue to be found by `verifyView()` when the templates are instantiated inside the `UIViewSwitchContainer`. No changes to the Rate/NoteValue sync-swap visibility logic were needed.

2. **Pluginval passes at strictness level 5**: All tests pass with exit code 0. The editor open/close, automation, and state tests all pass cleanly, confirming the parameter extension and view switching are correctly integrated.

3. **No workarounds needed**: Unlike some previous VSTGUI migrations, this one required no framework workarounds. The `COptionMenu` + `UIViewSwitchContainer` + `StringListParameter` pattern worked exactly as documented in the existing reference implementations (Distortion Type, Filter Type, etc.).

4. **Controller code reduction**: Removing the manual visibility management code (`modLFO1View_`, `modLFO2View_`, `modChaosView_` member variables and the associated `valueChanged`/`verifyView` branches) simplified the controller. Future mod source additions will not require any controller code changes for view switching.
