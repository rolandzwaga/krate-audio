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

---

## ArpLaneEditor (Spec 079)

### Overview

**Location:** [`plugins/shared/src/ui/arp_lane_editor.h`](../../plugins/shared/src/ui/arp_lane_editor.h)

**Purpose:** StepPatternEditor subclass for arpeggiator lane editing with a collapsible header and accent color. Each lane represents a single parameter dimension (velocity, gate, pitch, ratchet) with configurable value range, display labels, and color scheme. The 16px header contains a collapse triangle, lane name, and step count dropdown. When collapsed, a miniature bar preview renders all steps at reduced height.

**When to use:** Any arpeggiator lane needing bar-chart step editing with per-lane identity (name, color, value range). Phase 11b extends this for specialized lane types (pitch bipolar, ratchet discrete).

### Class Hierarchy

```
StepPatternEditor (plugins/shared/src/ui/step_pattern_editor.h)
    |
    +-- ArpLaneEditor (plugins/shared/src/ui/arp_lane_editor.h)
```

### Key API

```cpp
class ArpLaneEditor : public StepPatternEditor {
    // Lane identity
    void setLaneType(ArpLaneType type);           // kVelocity, kGate, kPitch, kRatchet
    void setLaneName(const std::string& name);    // Header label (e.g., "VEL", "GATE")
    void setAccentColor(CColor color);            // Derives normalColor_ (0.6x) and ghostColor_ (0.35x)

    // Display range
    void setDisplayRange(float min, float max,
                         const std::string& topLabel,
                         const std::string& bottomLabel);

    // Parameter binding
    void setLengthParamId(uint32_t paramId);      // Per-lane step count parameter
    void setPlayheadParamId(uint32_t paramId);     // Per-lane playhead parameter

    // Collapse/expand
    void setCollapsed(bool collapsed);
    bool isCollapsed() const;
    float getCollapsedHeight() const;              // Returns kHeaderHeight (16.0f)
    float getExpandedHeight() const;               // Returns full view height

    // Callbacks
    void setCollapseCallback(std::function<void()> cb);
    void setLengthParamCallback(std::function<void(int)> cb);
};
```

### ArpLaneType Enum

```cpp
enum class ArpLaneType {
    kVelocity = 0,   // 0.0-1.0, copper accent
    kGate = 1,        // 0%-200%, sand accent
    kPitch = 2,       // Phase 11b placeholder
    kRatchet = 3      // Phase 11b placeholder
};
```

### Color Derivation

When `setAccentColor()` is called, the editor automatically derives two additional colors using `ColorUtils::darkenColor()`:

| Color | Factor | Example (Copper) | Example (Sand) |
|-------|--------|-------------------|-----------------|
| Accent | 1.0x | `{208, 132, 92, 255}` | `{200, 164, 100, 255}` |
| Normal | 0.6x | `{125, 79, 55, 255}` | `{120, 98, 60, 255}` |
| Ghost | 0.35x | `{73, 46, 32, 255}` | `{70, 57, 35, 255}` |

### Named Colors (editor.uidesc)

| Color Name | Hex | Usage |
|------------|-----|-------|
| `arp-lane-velocity` | `#D0845Cff` | Velocity lane accent |
| `arp-lane-velocity-normal` | `#7D4F37ff` | Velocity lane normal bars |
| `arp-lane-velocity-ghost` | `#492E20ff` | Velocity lane ghost bars |
| `arp-lane-gate` | `#C8A464ff` | Gate lane accent |
| `arp-lane-gate-normal` | `#78623Cff` | Gate lane normal bars |
| `arp-lane-gate-ghost` | `#463923ff` | Gate lane ghost bars |

### ViewCreator Attributes

Registered as `"ArpLaneEditor"` via VSTGUI ViewCreator.

| Attribute | Type | Description |
|-----------|------|-------------|
| `lane-type` | kIntegerType | ArpLaneType enum value |
| `accent-color` | kColorType | Lane accent color |
| `lane-name` | kStringType | Header label text |
| `step-level-base-param-id` | kIntegerType | First step level parameter ID |
| `length-param-id` | kIntegerType | Step count parameter ID |
| `playhead-param-id` | kIntegerType | Playhead position parameter ID |

### Controller Wiring Pattern

```cpp
// In controller.h:
ArpLaneEditor* velocityLane_ = nullptr;
ArpLaneEditor* gateLane_ = nullptr;
ArpLaneContainer* arpLaneContainer_ = nullptr;

// In initialize(): construct lanes with configuration
velocityLane_ = new ArpLaneEditor(CRect(0, 0, 1384, 86));
velocityLane_->setLaneName("VEL");
velocityLane_->setLaneType(ArpLaneType::kVelocity);
velocityLane_->setAccentColor(CColor{208, 132, 92, 255});
velocityLane_->setDisplayRange(0.0f, 1.0f, "1.0", "0.0");
velocityLane_->setStepLevelBaseParamId(kArpVelocityLaneStep0Id);
velocityLane_->setLengthParamId(kArpVelocityLaneLengthId);
velocityLane_->setPlayheadParamId(kArpVelocityPlayheadId);

// In verifyView(): add lanes to container
arpLaneContainer_->addLane(velocityLane_);

// In setParamNormalized(): route step values
if (paramId >= kArpVelocityLaneStep0Id && paramId <= kArpVelocityLaneStep31Id) {
    velocityLane_->setStepLevel(paramId - kArpVelocityLaneStep0Id, value);
    velocityLane_->setDirty(true);
}

// In willClose(): null all pointers
arpLaneContainer_ = nullptr;
velocityLane_ = nullptr;
gateLane_ = nullptr;
```

**Consumers:** Ruinae Arpeggiator velocity and gate lanes (Spec 079). Phase 11b adds pitch and ratchet lanes.

---

## ArpLaneContainer (Spec 079)

### Overview

**Location:** [`plugins/shared/src/ui/arp_lane_container.h`](../../plugins/shared/src/ui/arp_lane_container.h)

**Purpose:** CViewContainer subclass with manual vertical scroll for stacked arp lanes. Manages a vector of ArpLaneEditor children, calculates total content height based on collapsed/expanded states, and provides scroll offset when content exceeds the viewport. Children are added programmatically (not from XML).

**When to use:** Multi-lane arpeggiator display where lanes need vertical stacking, independent collapse/expand, and scrolling when content exceeds the viewport.

### Key API

```cpp
class ArpLaneContainer : public CViewContainer {
    // Viewport
    void setViewportHeight(float height);

    // Lane management
    void addLane(ArpLaneEditor* lane);            // Appends, sets collapse callback, recalculates
    void removeLane(ArpLaneEditor* lane);          // Removes, recalculates layout
    int getLaneCount() const;

    // Layout
    void recalculateLayout();                      // Stacks lanes vertically, clamps scroll
    float getTotalContentHeight() const;

    // Scroll
    float getScrollOffset() const;
    void setScrollOffset(float offset);            // Clamped to [0, maxScrollOffset]
    float getMaxScrollOffset() const;              // max(0, totalContentHeight - viewportHeight)
};
```

### Layout Algorithm

`recalculateLayout()` iterates all lanes and stacks them vertically:

1. For each lane: height = `isCollapsed()` ? `getCollapsedHeight()` (16px) : `getExpandedHeight()`
2. Lane y-position = cumulative height of all preceding lanes, minus `scrollOffset_`
3. `totalContentHeight_` = sum of all lane heights
4. `scrollOffset_` clamped to `[0, getMaxScrollOffset()]`
5. Each lane resized via `setViewSize()` to its computed rect

### Scroll Behavior

- Mouse wheel: `scrollOffset_ += -distance * 20.0f` (20px per wheel unit)
- Scroll clamped to `[0, max(0, totalContentHeight - viewportHeight)]`
- When content fits in viewport (e.g., 2 lanes at 172px < 390px viewport), scroll is disabled

### ViewCreator Attributes

Registered as `"ArpLaneContainer"` via VSTGUI ViewCreator.

| Attribute | Type | Description |
|-----------|------|-------------|
| `viewport-height` | kFloatType | Visible viewport height in pixels |

### Design Notes

- Uses manual scroll offset (NOT CScrollView) for precise control over lane positioning and mouse event routing
- Lane collapse triggers `recalculateLayout()` via the collapse callback set in `addLane()`
- Mouse events are routed through CViewContainer's standard child hit-testing with scroll offset translation
- The container draws a background fill using the plugin background color

**Consumers:** Ruinae SEQ tab arpeggiator section (Spec 079). Phase 11b adds more lanes without container changes.
