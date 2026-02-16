# Plugin UI Patterns

[<- Back to Architecture Index](README.md)

**Location**: `plugins/ruinae/src/controller/controller.cpp` + `plugins/ruinae/resources/editor.uidesc`

---

## Sync Visibility Switching Pattern

### Overview

Several modulation sources and effects support tempo sync, where a Rate knob is replaced by a NoteValue dropdown when Sync is enabled. This is implemented using the **custom-view-name visibility switching** pattern: two overlapping `CViewContainer` groups share the same position, and the controller toggles their visibility based on the Sync parameter value.

### Sources Using This Pattern

| Source | Sync Param ID | Rate Group Name | NoteValue Group Name |
|--------|--------------|-----------------|---------------------|
| LFO 1 | `kLFO1SyncId` | `LFO1RateGroup` | `LFO1NoteValueGroup` |
| LFO 2 | `kLFO2SyncId` | `LFO2RateGroup` | `LFO2NoteValueGroup` |
| Chaos | `kChaosModSyncId` | `ChaosRateGroup` | `ChaosNoteValueGroup` |
| S&H | `kSampleHoldSyncId` | `SHRateGroup` | `SHNoteValueGroup` |
| Random | `kRandomSyncId` | `RandomRateGroup` | `RandomNoteValueGroup` |
| Delay | `kDelaySyncId` | `DelayTimeGroup` | `DelayNoteValueGroup` |
| Phaser | `kPhaserSyncId` | `PhaserRateGroup` | `PhaserNoteValueGroup` |
| Trance Gate | `kTranceGateSyncId` | `TranceGateRateGroup` | `TranceGateNoteValueGroup` |

### UIDescription (editor.uidesc)

Two `CViewContainer` elements occupy the same origin and size. The Rate group is visible by default (Sync=off), and the NoteValue group is hidden:

```xml
<!-- Rate (visible when sync off) -->
<view class="CViewContainer" origin="0, 0" size="36, 38"
      custom-view-name="SHRateGroup" transparent="true">
    <view class="ArcKnob" origin="4, 0" size="28, 28"
          control-tag="SampleHoldRate" default-value="0.702"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="0, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Rate"/>
</view>

<!-- Note Value (visible when sync on) -->
<view class="CViewContainer" origin="0, 0" size="36, 38"
      custom-view-name="SHNoteValueGroup" transparent="true"
      visible="false">
    <view class="COptionMenu" origin="2, 6" size="32, 16"
          control-tag="SampleHoldNoteValue"
          font="~ NormalFontSmaller" font-color="text-primary"
          back-color="bg-dropdown" frame-color="frame-dropdown"
          round-rect-radius="2" frame-width="1"
          min-value="0" max-value="1"
          default-value="0.5"/>
    <view class="CTextLabel" origin="0, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Note"/>
</view>
```

### Controller Implementation

The visibility switching is implemented in two places in `controller.cpp`:

#### 1. View Creation (`verifyView` / custom-view-name handler)

When views are created, the controller stores pointers to each group and sets initial visibility based on the current Sync parameter value:

```cpp
// In the custom-view-name handling section of verifyView():
if (*name == "SHRateGroup") {
    shRateGroup_ = container;
    auto* syncParam = getParameterObject(kSampleHoldSyncId);
    bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
    container->setVisible(!syncOn);
} else if (*name == "SHNoteValueGroup") {
    shNoteValueGroup_ = container;
    auto* syncParam = getParameterObject(kSampleHoldSyncId);
    bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
    container->setVisible(syncOn);
}
```

This ensures correct visibility on editor open (e.g., when loading a preset with Sync already enabled).

#### 2. Parameter Change (`setParamNormalized`)

When the Sync parameter changes (from automation, UI toggle, or state loading), the controller toggles the visibility of both groups:

```cpp
// In setParamNormalized():
if (tag == kSampleHoldSyncId) {
    if (shRateGroup_) shRateGroup_->setVisible(value < 0.5);
    if (shNoteValueGroup_) shNoteValueGroup_->setVisible(value >= 0.5);
}
```

### Controller Member Variables

Each pair of groups requires two `CViewContainer*` member fields in the Controller class:

```cpp
// In controller.h or controller.cpp private section:
VSTGUI::CViewContainer* shRateGroup_ = nullptr;
VSTGUI::CViewContainer* shNoteValueGroup_ = nullptr;
VSTGUI::CViewContainer* randomRateGroup_ = nullptr;
VSTGUI::CViewContainer* randomNoteValueGroup_ = nullptr;
// ... (same pattern for LFO, Chaos, Delay, Phaser, TranceGate)
```

### Adding a New Sync-Switched Source

To add a new source with sync visibility switching:

1. **Define `custom-view-name` groups** in `editor.uidesc`: Two `CViewContainer` elements at the same origin with unique `custom-view-name` values (e.g., `"MySourceRateGroup"` and `"MySourceNoteValueGroup"`)
2. **Set initial visibility**: Rate group `visible="true"` (default), NoteValue group `visible="false"`
3. **Add member variables** in the controller: Two `CViewContainer*` fields
4. **Handle view creation**: In the `custom-view-name` handler, store pointers and set initial visibility from current parameter value
5. **Handle parameter changes**: In `setParamNormalized()`, toggle visibility when the Sync parameter ID changes

### Design Notes

- The `custom-view-name` attribute is a VSTGUI feature that allows the controller to identify specific views by name during `verifyView()` callbacks
- Null-checking the group pointers (`if (shRateGroup_)`) is essential because `setParamNormalized()` can be called before the editor is open (automation playback)
- Both groups must occupy the same position and size to create a seamless visual swap
- The NoteValue dropdown is populated via `createNoteValueDropdown()` in the parameter registration, which uses `StringListParameter` with note value strings

---

## Mod Source Dropdown View Switching

### Overview

The modulation section uses a `UIViewSwitchContainer` driven by the `ModSourceViewMode` `StringListParameter` to display different control panels for each modulation source. Each source has its own template in `editor.uidesc`.

### Template Naming Convention

Templates follow the pattern `ModSource_{SourceName}`:

| ModSource Enum | Template Name | Parameter Count |
|----------------|---------------|-----------------|
| LFO1 | `ModSource_LFO1` | 4+ |
| LFO2 | `ModSource_LFO2` | 4+ |
| AmpEnv | `ModSource_AmpEnv` | 0 (info only) |
| FilterEnv | `ModSource_FilterEnv` | 0 (info only) |
| ModEnv | `ModSource_ModEnv` | 0 (info only) |
| Chaos | `ModSource_Chaos` | 3+ |
| Macros | `ModSource_Macros` | 4 |
| Rungler | `ModSource_Rungler` | 6 |
| EnvFollower | `ModSource_EnvFollower` | 3 |
| SampleHold | `ModSource_SampleHold` | 4 |
| Random | `ModSource_Random` | 4 |
| PitchFollower | `ModSource_PitchFollower` | 4 |
| Transient | `ModSource_Transient` | 3 |

### Template Size

All mod source templates use a fixed container size of `158 x 120` pixels.

### Visual Consistency

All ArcKnobs in mod source templates use:
- `arc-color="modulation"` -- modulation section accent color
- `guide-color="knob-guide"` -- standard guide ring color

All labels use:
- `font="~ NormalFontSmaller"` -- compact label font
- `font-color="text-secondary"` -- subdued label color
- `text-alignment="center"` -- centered below knob
