# Quickstart: Mod Source Dropdown Selector

**Branch**: `053-mod-source-dropdown` | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## What This Feature Does

Replaces the 3-segment tab bar (LFO 1 | LFO 2 | Chaos) in the Modulation section with a dropdown menu (`COptionMenu`) that lists 10 modulation sources. The dropdown drives a `UIViewSwitchContainer` that automatically shows the correct view for the selected source. This aligns the mod source area with the existing UI pattern used throughout Ruinae (oscillator types, filter types, distortion types, delay types) and prepares the infrastructure for 7 future modulation sources.

## Files to Modify

| File | Change | Why |
|------|--------|-----|
| `plugins/ruinae/resources/editor.uidesc` | Replace IconSegmentButton with COptionMenu; replace 3 inline views with UIViewSwitchContainer; add 10 named templates | Core UI migration |
| `plugins/ruinae/src/parameters/chaos_mod_params.h` | Extend StringListParameter from 3 to 10 entries | Dropdown needs 10 menu items |
| `plugins/ruinae/src/controller/controller.h` | Remove 3 member variables + comment | Dead code after UIViewSwitchContainer handles switching |
| `plugins/ruinae/src/controller/controller.cpp` | Remove valueChanged block + verifyView branches for mod source views | Dead code after UIViewSwitchContainer handles switching |

## Implementation Order

1. **Parameter first**: Extend `StringListParameter` in `chaos_mod_params.h` (10 entries)
2. **Templates**: Add 10 named templates to `editor.uidesc` (extract 3 existing + 7 empty placeholders)
3. **Dropdown + switch container**: Replace `IconSegmentButton` with `COptionMenu` and 3 inline views with `UIViewSwitchContainer` in `editor.uidesc`
4. **Controller cleanup**: Remove manual visibility code from controller `.h` and `.cpp`
5. **Build and verify**: Build, run pluginval, visually verify dropdown works

## Key Patterns to Follow

### Existing COptionMenu + UIViewSwitchContainer (reference: Distortion Type)

```xml
<!-- Dropdown bound to parameter -->
<view class="COptionMenu" origin="8, 16" size="90, 18"
      control-tag="DistortionType" default-value="0"
      font="~ NormalFontSmaller" font-color="text-menu"
      back-color="bg-dropdown" frame-color="frame-dropdown"/>

<!-- Automatic view switching via template-switch-control -->
<view class="UIViewSwitchContainer"
      origin="0, 38" size="146, 96"
      template-names="Template1,Template2,..."
      template-switch-control="DistortionType"
      transparent="true"/>
```

### StringListParameter Registration (reference: Distortion View Mode)

```cpp
auto* viewModeParam = new StringListParameter(
    STR16("Distortion View"), kDistortionViewModeTag);
viewModeParam->appendString(STR16("General"));
viewModeParam->appendString(STR16("Type"));
parameters.addParameter(viewModeParam);
```

## Gotchas

1. **custom-view-name on inner views**: The `LFO1RateGroup`, `LFO1NoteValueGroup`, etc. custom-view-names must be preserved in the extracted templates. These are used by the Rate/NoteValue sync-swap visibility logic in the controller, which is orthogonal to the mod source view switching.

2. **custom-view-name on outer containers**: The `ModLFO1View`, `ModLFO2View`, `ModChaosView` custom-view-names on the outer containers must be REMOVED (they were used for manual visibility toggling and are no longer needed).

3. **Template size**: The Chaos view was originally `158, 106`. The template must be `158, 120` to match the UIViewSwitchContainer size. The extra 14px is transparent empty space.

4. **Normalized value formula**: The old code used `std::round(value * 2.0)` which only works for 3 entries. The UIViewSwitchContainer handles the index calculation internally and correctly for any number of entries.

5. **Ephemeral parameter**: `kModSourceViewModeTag` is never saved/loaded. It always defaults to 0 (LFO 1). Do not add it to any save/load functions.

## Verification

### Manual Checks
- Open plugin UI, click mod source dropdown, select each of LFO 1 / LFO 2 / Chaos -- verify all controls appear correctly
- Select each placeholder source (Macros through Transient) -- verify empty view, no crash
- Switch back to LFO 1 after selecting a placeholder -- verify controls reappear
- Close and reopen plugin window -- verify defaults to LFO 1
- Rapidly switch between all 10 sources -- verify no flicker or visual artifacts
- Click dropdown to open, click outside without selecting -- verify it closes without changing selection

### Pluginval
```bash
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```
Expected: All tests pass with exit code 0.
