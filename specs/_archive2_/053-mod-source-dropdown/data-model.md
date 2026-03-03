# Data Model: Mod Source Dropdown Selector

**Date**: 2026-02-14 | **Spec**: [spec.md](spec.md)

## Entities

### ModSourceViewMode Parameter

**Type**: `StringListParameter` (VSTGUI `Steinberg::Vst::StringListParameter`)
**Parameter ID**: `kModSourceViewModeTag` = 10019
**Location**: `plugins/ruinae/src/parameters/chaos_mod_params.h`, registered in `registerChaosModParams()`
**Persistence**: Ephemeral (UI-only, never saved to preset state)
**Default**: Index 0 ("LFO 1") on every plugin open and preset load

| Index | Name            | Template Name               | Status       |
|-------|-----------------|-----------------------------|--------------|
| 0     | LFO 1           | ModSource_LFO1              | Implemented  |
| 1     | LFO 2           | ModSource_LFO2              | Implemented  |
| 2     | Chaos           | ModSource_Chaos             | Implemented  |
| 3     | Macros          | ModSource_Macros            | Placeholder  |
| 4     | Rungler         | ModSource_Rungler           | Placeholder  |
| 5     | Env Follower    | ModSource_EnvFollower       | Placeholder  |
| 6     | S&H             | ModSource_SampleHold        | Placeholder  |
| 7     | Random          | ModSource_Random            | Placeholder  |
| 8     | Pitch Follower  | ModSource_PitchFollower     | Placeholder  |
| 9     | Transient       | ModSource_Transient         | Placeholder  |

**Normalized value mapping**: `normalizedValue = index / 9.0` (stepCount = 9)

### View Templates

All templates use `size="158, 120"` to fill the UIViewSwitchContainer content area.

#### ModSource_LFO1 (Extracted from inline view)

Contains the following controls:
- Row 1: Rate/NoteValue (sync-swapped via `custom-view-name`), Shape, Depth, Phase
- Row 2: Sync toggle, Retrigger toggle, Unipolar toggle
- Row 3: Fade In knob, Symmetry knob, Quantize knob

Control tags: `LFO1Rate`, `LFO1NoteValue`, `LFO1Shape`, `LFO1Depth`, `LFO1Phase`, `LFO1Sync`, `LFO1Retrigger`, `LFO1Unipolar`, `LFO1FadeIn`, `LFO1Symmetry`, `LFO1Quantize`

Custom view names preserved: `LFO1RateGroup`, `LFO1NoteValueGroup`

#### ModSource_LFO2 (Extracted from inline view)

Identical layout to LFO1, all control tags prefixed with `LFO2`.

Control tags: `LFO2Rate`, `LFO2NoteValue`, `LFO2Shape`, `LFO2Depth`, `LFO2Phase`, `LFO2Sync`, `LFO2Retrigger`, `LFO2Unipolar`, `LFO2FadeIn`, `LFO2Symmetry`, `LFO2Quantize`

Custom view names preserved: `LFO2RateGroup`, `LFO2NoteValueGroup`

#### ModSource_Chaos (Extracted from inline view)

Contains:
- Row 1: Rate/NoteValue (sync-swapped via `custom-view-name`), Type dropdown, Depth
- Row 2: Sync toggle

Control tags: `ChaosRate`, `ChaosNoteValue`, `ChaosType`, `ChaosDepth`, `ChaosSync`

Custom view names preserved: `ChaosRateGroup`, `ChaosNoteValueGroup`

Note: Original size is `158, 106` but template size is `158, 120` to match the UIViewSwitchContainer. The extra vertical space will be transparent/empty below the controls.

#### Placeholder Templates (7 total)

Empty `CViewContainer` with `size="158, 120"` and `transparent="true"`. No child views.

## Relationships

```
StringListParameter (kModSourceViewModeTag)
    |
    v
COptionMenu (control-tag="ModSourceViewMode")
    |
    v (via template-switch-control)
UIViewSwitchContainer
    |
    +-- index 0 --> ModSource_LFO1 template
    +-- index 1 --> ModSource_LFO2 template
    +-- index 2 --> ModSource_Chaos template
    +-- index 3 --> ModSource_Macros template (empty)
    +-- index 4 --> ModSource_Rungler template (empty)
    +-- index 5 --> ModSource_EnvFollower template (empty)
    +-- index 6 --> ModSource_SampleHold template (empty)
    +-- index 7 --> ModSource_Random template (empty)
    +-- index 8 --> ModSource_PitchFollower template (empty)
    +-- index 9 --> ModSource_Transient template (empty)
```

## State Machine

The `ModSourceViewMode` parameter has no state transitions in the traditional sense. It is a simple index selector:

- **Initial state**: Always index 0 (LFO 1)
- **On preset load**: Reset to index 0 (never persisted)
- **On editor reopen**: Reset to index 0 (parameter default)
- **On user selection**: Immediately switches to selected index

## Validation Rules

- Parameter index must be in range [0, 9]
- `StringListParameter` enforces this automatically via `stepCount = 9`
- No cross-parameter validation needed (this is a UI-only view selector)
