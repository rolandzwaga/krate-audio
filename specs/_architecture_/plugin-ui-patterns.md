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

## IArpLane Interface (Spec 080)

### Overview

**Location:** [`plugins/shared/src/ui/arp_lane.h`](../../plugins/shared/src/ui/arp_lane.h)

**Purpose:** Pure virtual interface for polymorphic arpeggiator lane management. All concrete lane classes (ArpLaneEditor, ArpModifierLane, ArpConditionLane) implement this interface. ArpLaneContainer holds `std::vector<IArpLane*>` for heterogeneous lane management without `dynamic_cast`.

**When to use:** Any lane type that will be added to ArpLaneContainer. Implement this interface in new lane classes to participate in the container's vertical stacking, collapse/expand, and playhead system.

**Namespace:** `Krate::Plugins`

### Key API

```cpp
class IArpLane {
public:
    virtual ~IArpLane() = default;

    virtual VSTGUI::CView* getView() = 0;                          // For addView/removeView
    [[nodiscard]] virtual float getExpandedHeight() const = 0;      // Header + body height
    [[nodiscard]] virtual float getCollapsedHeight() const = 0;     // Header-only height (16px)
    [[nodiscard]] virtual bool isCollapsed() const = 0;
    virtual void setCollapsed(bool collapsed) = 0;                  // Fires collapseCallback on change
    virtual void setPlayheadStep(int32_t step) = 0;                 // -1 = no playhead
    virtual void setLength(int32_t length) = 0;                     // Active step count (2-32)
    virtual void setCollapseCallback(std::function<void()> cb) = 0; // Container relayout trigger
};
```

### Implementors

| Class | Base Class | Lane Types |
|-------|------------|------------|
| ArpLaneEditor | StepPatternEditor + IArpLane | Velocity, Gate, Pitch (bipolar), Ratchet (discrete) |
| ArpModifierLane | CControl + IArpLane | Modifier (4-row toggle dot grid) |
| ArpConditionLane | CControl + IArpLane | Condition (18-value enum popup) |

### Design Notes

- Pure interface with virtual destructor -- no diamond problem with multiple inheritance
- `getView()` bridges between the interface and VSTGUI's CView hierarchy (needed for `addView()`/`removeView()`)
- All implementors return `this` from `getView()` since they inherit from CView-derived classes

**Consumers:** ArpLaneContainer, Ruinae Controller (lane wiring).

---

## ArpLaneHeader (Spec 080)

### Overview

**Location:** [`plugins/shared/src/ui/arp_lane_header.h`](../../plugins/shared/src/ui/arp_lane_header.h)

**Purpose:** Non-CView helper class owned by composition in each lane class. Encapsulates the 16px collapsible header with collapse toggle triangle, accent-colored lane name label, and step count length dropdown. Extracted from ArpLaneEditor's private methods (Spec 079) into a reusable helper shared by all lane types.

**When to use:** All lane classes that need a collapsible header with lane name and length dropdown. Owned as a member variable, not as a CView child. Phase 11c will add transform buttons to this header exclusively.

**Namespace:** `Krate::Plugins`

### Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `kHeight` | 16.0f | Header height in pixels |
| `kCollapseTriangleSize` | 8.0f | Collapse triangle size |
| `kLengthDropdownX` | 80.0f | X offset of length dropdown from header left |
| `kLengthDropdownWidth` | 36.0f | Width of length dropdown area |
| `kMinSteps` | 2 | Minimum step count |
| `kMaxSteps` | 32 | Maximum step count |

### Key API

```cpp
class ArpLaneHeader {
    // Configuration
    void setLaneName(const std::string& name);
    void setAccentColor(const CColor& color);
    void setNumSteps(int steps);
    void setLengthParamId(uint32_t paramId);

    // State
    void setCollapsed(bool collapsed);
    bool isCollapsed() const;
    float getHeight() const;                       // Returns kHeight (16.0f)

    // Callbacks
    void setCollapseCallback(std::function<void()> cb);
    void setLengthParamCallback(std::function<void(uint32_t, float)> cb);

    // Rendering and interaction (called by owning lane)
    void draw(CDrawContext* context, const CRect& headerRect);
    bool handleMouseDown(const CPoint& where, const CRect& headerRect, CFrame* frame);
};
```

### Header Layout

```
|<--24px-->|<---56px--->|<----36px---->|
| triangle |  lane name | step count v |
```

- **Collapse zone** (left 24px): Click toggles collapsed/expanded, fires collapseCallback
- **Name label** (24-80px): Drawn in accent color
- **Length dropdown** (80-116px): Shows current step count, click opens COptionMenu popup

### Drawing

1. Fill header background (`#1E1E21`)
2. Draw collapse triangle (right-pointing if collapsed, down-pointing if expanded)
3. Draw lane name in accent color
4. Draw step count label + small dropdown indicator triangle

### Interaction

`handleMouseDown()` returns `true` if click was handled:
1. If click in collapse zone (localX < 24px): toggle collapse state, fire callback
2. If click in length dropdown zone (localX in 80-116px): open COptionMenu with values 2-32
3. Otherwise: return false (let owning lane handle the click)

**Consumers:** ArpLaneEditor (header_ member), ArpModifierLane (header_ member), ArpConditionLane (header_ member).

---

## ArpLaneEditor (Specs 079 + 080)

### Overview

**Location:** [`plugins/shared/src/ui/arp_lane_editor.h`](../../plugins/shared/src/ui/arp_lane_editor.h)

**Purpose:** StepPatternEditor subclass implementing IArpLane for arpeggiator lane editing with a collapsible header (delegated to ArpLaneHeader) and accent color. Supports 4 lane type modes: standard bar charts (velocity, gate), bipolar bar charts (pitch), and stacked discrete blocks (ratchet). Each lane represents a single parameter dimension with configurable value range, display labels, and color scheme. When collapsed, a miniature preview renders all steps at reduced height.

**When to use:** Any arpeggiator lane needing bar-chart or block-based step editing with per-lane identity (name, color, value range). Use kVelocity/kGate for standard 0-1 bar charts, kPitch for bipolar bars with center line and integer snapping, kRatchet for discrete 1-4 stacked blocks.

### Class Hierarchy

```
StepPatternEditor (plugins/shared/src/ui/step_pattern_editor.h)
    |                                IArpLane (plugins/shared/src/ui/arp_lane.h)
    |                                    |
    +-------- ArpLaneEditor -------------+  (multiple inheritance)
              (plugins/shared/src/ui/arp_lane_editor.h)
              owns ArpLaneHeader header_
```

### Key API

```cpp
class ArpLaneEditor : public StepPatternEditor, public IArpLane {
    // Lane identity
    void setLaneType(ArpLaneType type);           // kVelocity, kGate, kPitch, kRatchet
    void setLaneName(const std::string& name);    // Header label (e.g., "VEL", "PITCH")
    void setAccentColor(CColor color);            // Derives normalColor_ (0.6x) and ghostColor_ (0.35x)

    // Display range (used by bipolar mode for grid labels)
    void setDisplayRange(float min, float max,
                         const std::string& topLabel,
                         const std::string& bottomLabel);

    // Parameter binding
    void setLengthParamId(uint32_t paramId);      // Per-lane step count parameter
    void setPlayheadParamId(uint32_t paramId);     // Per-lane playhead parameter

    // IArpLane interface
    CView* getView() override;                     // Returns this
    float getExpandedHeight() const override;
    float getCollapsedHeight() const override;      // Returns ArpLaneHeader::kHeight (16.0f)
    bool isCollapsed() const override;              // Delegates to header_
    void setCollapsed(bool collapsed) override;     // Delegates to header_, resizes
    void setPlayheadStep(int32_t step) override;    // Delegates to setPlaybackStep()
    void setLength(int32_t length) override;        // Delegates to setNumSteps()
    void setCollapseCallback(std::function<void()> cb) override;

    // Callbacks
    void setLengthParamCallback(std::function<void(uint32_t, float)> cb);
};
```

### ArpLaneType Enum

```cpp
enum class ArpLaneType {
    kVelocity = 0,   // Standard bar chart, 0.0-1.0, copper accent
    kGate = 1,       // Standard bar chart, 0%-200%, sand accent
    kPitch = 2,      // Bipolar bar chart, -24..+24 semitones, sage accent
    kRatchet = 3     // Discrete stacked blocks, 1-4 subdivisions, lavender accent
};
```

### Lane Type Rendering Modes

| Mode | Rendering | Interaction | Value Range |
|------|-----------|-------------|-------------|
| kVelocity | Standard bars from bottom | Click/drag sets level | 0.0-1.0 normalized |
| kGate | Standard bars from bottom | Click/drag sets level | 0.0-1.0 normalized (0%-200%) |
| kPitch | Bipolar bars from center line | Click/drag with integer semitone snapping, right-click resets to 0.5 | 0.0-1.0 (0.5 = 0 semitones, maps to -24..+24) |
| kRatchet | Stacked blocks (1-4) | Click cycles 1->2->3->4->1, drag adjusts, right-click resets to 1 | 0.0-1.0 (0.0=1 subdivision, 1.0=4 subdivisions) |

### Bipolar Mode Details (kPitch)

- Center line drawn at Y corresponding to normalized 0.5 (0 semitones)
- Positive values: bars extend upward from center
- Negative values: bars extend downward from center
- **Snapping**: Values snap to integer semitones: `snapped = round(signedValue * 24.0f) / 24.0f`
- Grid labels: "+24" at top, "0" at center, "-24" at bottom

### Discrete Mode Details (kRatchet)

- Stacked rectangular blocks from bottom (1-4 blocks with 2px gaps)
- **Click**: Cycles value 1 -> 2 -> 3 -> 4 -> 1 (only if vertical movement < 4px)
- **Drag**: Vertical drag adjusts value (8px per increment)
- **Right-click**: Resets to 1 (normalized 0.0)
- Block count derived from normalized: `count = 1 + round(normalized * 3)`

### Color Derivation

When `setAccentColor()` is called, the editor automatically derives two additional colors using `darkenColor()`:

| Color | Factor | Example (Copper) | Example (Sand) | Example (Sage) | Example (Lavender) |
|-------|--------|-------------------|-----------------|----------------|---------------------|
| Accent | 1.0x | `{208, 132, 92}` | `{200, 164, 100}` | `{108, 168, 160}` | `{152, 128, 176}` |
| Normal | 0.6x | `{125, 79, 55}` | `{120, 98, 60}` | `{65, 101, 96}` | `{91, 77, 106}` |
| Ghost | 0.35x | `{73, 46, 32}` | `{70, 57, 35}` | `{38, 59, 56}` | `{53, 45, 62}` |

### Named Colors (editor.uidesc)

| Color Name | Hex | Usage |
|------------|-----|-------|
| `arp-lane-velocity` | `#D0845Cff` | Velocity lane accent |
| `arp-lane-velocity-normal` | `#7D4F37ff` | Velocity lane normal bars |
| `arp-lane-velocity-ghost` | `#492E20ff` | Velocity lane ghost bars |
| `arp-lane-gate` | `#C8A464ff` | Gate lane accent |
| `arp-lane-gate-normal` | `#78623Cff` | Gate lane normal bars |
| `arp-lane-gate-ghost` | `#463923ff` | Gate lane ghost bars |
| `arp.pitch` | `#6CA8A0FF` | Pitch lane accent (sage) |
| `arp.ratchet` | `#9880B0FF` | Ratchet lane accent (lavender) |
| `arp.modifier` | `#C0707CFF` | Modifier lane accent (rose) |
| `arp.condition` | `#7C90B0FF` | Condition lane accent (slate) |

### Step Content Alignment (FR-049)

All lane types share a common left margin (`kStepContentLeftMargin = 40.0f`) so step columns align vertically across lanes. This constant must match `ArpModifierLane::kLeftMargin` and `ArpConditionLane::kLeftMargin`.

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
ArpLaneEditor* pitchLane_ = nullptr;
ArpLaneEditor* ratchetLane_ = nullptr;
ArpModifierLane* modifierLane_ = nullptr;
ArpConditionLane* conditionLane_ = nullptr;
ArpLaneContainer* arpLaneContainer_ = nullptr;

// In verifyView(): construct lanes with configuration
velocityLane_ = new ArpLaneEditor(CRect(0, 0, 500, 86), nullptr, -1);
velocityLane_->setLaneName("VEL");
velocityLane_->setLaneType(ArpLaneType::kVelocity);
velocityLane_->setAccentColor(CColor{208, 132, 92, 255});
velocityLane_->setDisplayRange(0.0f, 1.0f, "1.0", "0.0");
velocityLane_->setStepLevelBaseParamId(kArpVelocityLaneStep0Id);
velocityLane_->setLengthParamId(kArpVelocityLaneLengthId);
velocityLane_->setPlayheadParamId(kArpVelocityPlayheadId);

pitchLane_ = new ArpLaneEditor(CRect(0, 0, 500, 86), nullptr, -1);
pitchLane_->setLaneName("PITCH");
pitchLane_->setLaneType(ArpLaneType::kPitch);
pitchLane_->setAccentColor(CColor{108, 168, 160, 255});  // Sage
pitchLane_->setDisplayRange(-24.0f, 24.0f, "+24", "-24");

// Add lanes to container (accepts IArpLane*)
arpLaneContainer_->addLane(velocityLane_);
arpLaneContainer_->addLane(pitchLane_);
arpLaneContainer_->addLane(modifierLane_);
arpLaneContainer_->addLane(conditionLane_);

// In setParamNormalized(): route step values
if (paramId >= kArpVelocityLaneStep0Id && paramId <= kArpVelocityLaneStep31Id) {
    velocityLane_->setStepLevel(paramId - kArpVelocityLaneStep0Id, value);
    velocityLane_->setDirty(true);
}

// In willClose(): null all pointers
arpLaneContainer_ = nullptr;
velocityLane_ = nullptr;
gateLane_ = nullptr;
pitchLane_ = nullptr;
ratchetLane_ = nullptr;
modifierLane_ = nullptr;
conditionLane_ = nullptr;
```

**Consumers:** Ruinae Arpeggiator velocity, gate, pitch, and ratchet lanes (Specs 079 + 080).

---

## ArpModifierLane (Spec 080)

### Overview

**Location:** [`plugins/shared/src/ui/arp_modifier_lane.h`](../../plugins/shared/src/ui/arp_modifier_lane.h)

**Purpose:** Custom CControl implementing IArpLane that renders a 4-row toggle dot grid for step modifiers (Rest, Tie, Slide, Accent). Each step stores a bitmask matching ArpStepFlags from `arpeggiator_core.h`. Collapsible header via ArpLaneHeader composition, registered as ViewCreator for uidesc configurability.

**When to use:** 4-row toggle dot grid for per-step Rest/Tie/Slide/Accent bitmask editing. The Rest row is inverted: an active dot means the step is rested (kStepActive bit OFF).

**Namespace:** `Krate::Plugins`

### Class Hierarchy

```
CControl (VSTGUI)
    |                   IArpLane (plugins/shared/src/ui/arp_lane.h)
    |                       |
    +-- ArpModifierLane ----+  (multiple inheritance)
        (plugins/shared/src/ui/arp_modifier_lane.h)
        owns ArpLaneHeader header_
```

### Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `kMaxSteps` | 32 | Maximum step count |
| `kRowCount` | 4 | Number of modifier rows |
| `kLeftMargin` | 40.0f | Left margin for row labels (matches ArpLaneEditor::kStepContentLeftMargin) |
| `kDotRadius` | 4.0f | Radius of toggle dots |
| `kBodyHeight` | 44.0f | Body height (4 rows at 11px each) |
| `kRowHeight` | 11.0f | Height per row |

### Row Definitions

| Row | Label | Bit | Behavior |
|-----|-------|-----|----------|
| 0 | "Rest" | `0x01` (kStepActive) | **INVERTED**: active dot = bit OFF (step is rested) |
| 1 | "Tie" | `0x02` | Normal: active dot = bit ON |
| 2 | "Slide" | `0x04` | Normal: active dot = bit ON |
| 3 | "Accent" | `0x08` | Normal: active dot = bit ON |

### Key API

```cpp
class ArpModifierLane : public CControl, public IArpLane {
    // Step flag management
    void setStepFlags(int index, uint8_t flags);    // Masked to 4 bits (& 0x0F)
    uint8_t getStepFlags(int index) const;
    void setNumSteps(int count);

    // Configuration
    void setAccentColor(const CColor& color);
    void setLaneName(const std::string& name);
    void setStepFlagBaseParamId(uint32_t baseId);
    void setLengthParamId(uint32_t paramId);
    void setPlayheadParamId(uint32_t paramId);

    // Callbacks
    void setParameterCallback(ParameterCallback cb);
    void setBeginEditCallback(EditCallback cb);
    void setEndEditCallback(EditCallback cb);
    void setLengthParamCallback(std::function<void(uint32_t, float)> cb);

    // IArpLane interface
    CView* getView() override;                     // Returns this
    float getExpandedHeight() const override;       // 60.0f (16px header + 44px body)
    float getCollapsedHeight() const override;      // 16.0f (header only)
    // ... (other IArpLane methods)
};
```

### Parameter Encoding

- **Bitmask normalization**: `normalized = (flags & 0x0F) / 15.0f` (denominator is 15, NOT 255)
- **Default step flags**: `0x01` (kStepActive ON, all modifiers OFF)
- **High bits masked**: Input values are always masked with `& 0x0F`

### Drawing

**Expanded**: Header + body with row labels in left margin, filled/outline dots per step per row. Active dots are filled in accent color, inactive dots are outlined in dimmed accent color. Playhead overlay is drawn as a semi-transparent column.

**Collapsed**: Header + mini preview with small dots indicating non-default steps.

### Interaction

- **Click on body**: Toggles the flag bit for the clicked step/row combination (XOR)
- **Click on header**: Delegated to ArpLaneHeader (collapse toggle or length dropdown)
- No drag painting (single-click toggle per dot)

### ViewCreator Attributes

Registered as `"ArpModifierLane"` via VSTGUI ViewCreator.

| Attribute | Type | Description |
|-----------|------|-------------|
| `accent-color` | kColorType | Lane accent color |
| `lane-name` | kStringType | Header label text |
| `step-flag-base-param-id` | kStringType | First step flag parameter ID |
| `length-param-id` | kStringType | Step count parameter ID |
| `playhead-param-id` | kStringType | Playhead position parameter ID |

**Consumers:** Ruinae Arpeggiator modifier lane (Spec 080).

---

## ArpConditionLane (Spec 080)

### Overview

**Location:** [`plugins/shared/src/ui/arp_condition_lane.h`](../../plugins/shared/src/ui/arp_condition_lane.h)

**Purpose:** Custom CControl implementing IArpLane that renders per-step condition abbreviation cells. Left-click opens a COptionMenu popup with 18 TrigCondition values, right-click resets to Always (index 0), hover updates tooltip with full description. Collapsible header via ArpLaneHeader composition, registered as ViewCreator for uidesc configurability.

**When to use:** Per-step enum popup with 18 TrigCondition values for conditional step firing (probability, periodic, fill-based).

**Namespace:** `Krate::Plugins`

### Class Hierarchy

```
CControl (VSTGUI)
    |                    IArpLane (plugins/shared/src/ui/arp_lane.h)
    |                        |
    +-- ArpConditionLane ----+  (multiple inheritance)
        (plugins/shared/src/ui/arp_condition_lane.h)
        owns ArpLaneHeader header_
```

### Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `kMaxSteps` | 32 | Maximum step count |
| `kConditionCount` | 18 | Number of condition types |
| `kBodyHeight` | 28.0f | Body height (single row of cells) |
| `kLeftMargin` | 40.0f | Left margin for step alignment (matches other lane types) |

### Condition Values (18 entries)

| Index | Abbreviation | Full Name | Category |
|-------|-------------|-----------|----------|
| 0 | "Alw" | "Always" | Default |
| 1-5 | "10%"-"90%" | Probability | Probability |
| 6-14 | "Ev2"-"4:4" | Periodic | Periodic (every N, Nth of N) |
| 15 | "1st" | "First" | Special |
| 16 | "Fill" | "Fill" | Fill-based |
| 17 | "!F" | "Not Fill" | Fill-based |

### Key API

```cpp
class ArpConditionLane : public CControl, public IArpLane {
    // Step condition management
    void setStepCondition(int index, uint8_t conditionIndex);  // Clamped to 0-17
    uint8_t getStepCondition(int index) const;
    void setNumSteps(int count);

    // Configuration
    void setAccentColor(const CColor& color);
    void setLaneName(const std::string& name);
    void setStepConditionBaseParamId(uint32_t baseId);
    void setLengthParamId(uint32_t paramId);
    void setPlayheadParamId(uint32_t paramId);

    // Callbacks
    void setParameterCallback(ParameterCallback cb);
    void setBeginEditCallback(EditCallback cb);
    void setEndEditCallback(EditCallback cb);
    void setLengthParamCallback(std::function<void(uint32_t, float)> cb);

    // IArpLane interface
    CView* getView() override;                     // Returns this
    float getExpandedHeight() const override;       // 44.0f (16px header + 28px body)
    float getCollapsedHeight() const override;      // 16.0f (header only)
    // ... (other IArpLane methods)
};
```

### Parameter Encoding

- **Condition normalization**: `normalized = index / 17.0f` (denominator is 17, range 0-17)
- **Decode**: `index = clamp(round(normalized * 17.0f), 0, 17)`
- **Default step condition**: 0 (Always)
- **Out-of-range clamped**: Input index >= 18 is clamped to 0

### Drawing

**Expanded**: Header + body with abbreviation cells. Non-Always cells have a slightly lighter background (`#232328`). Labels are drawn in accent color for non-Always, dimmed accent for Always. Playhead overlay is a semi-transparent column.

**Collapsed**: Header + mini preview with small filled/outline cells indicating non-default conditions.

### Interaction

- **Left-click**: Opens COptionMenu popup with 18 full-name entries; selection updates the step's condition
- **Right-click**: Resets step to Always (index 0)
- **Hover**: Updates tooltip text via `setTooltipText()` with descriptive string from `kConditionTooltips[]`
- **Click on header**: Delegated to ArpLaneHeader (collapse toggle or length dropdown)

### ViewCreator Attributes

Registered as `"ArpConditionLane"` via VSTGUI ViewCreator.

| Attribute | Type | Description |
|-----------|------|-------------|
| `accent-color` | kColorType | Lane accent color |
| `lane-name` | kStringType | Header label text |
| `step-condition-base-param-id` | kStringType | First step condition parameter ID |
| `length-param-id` | kStringType | Step count parameter ID |
| `playhead-param-id` | kStringType | Playhead position parameter ID |

**Consumers:** Ruinae Arpeggiator condition lane (Spec 080).

---

## ArpLaneContainer (Specs 079 + 080)

### Overview

**Location:** [`plugins/shared/src/ui/arp_lane_container.h`](../../plugins/shared/src/ui/arp_lane_container.h)

**Purpose:** CViewContainer subclass with manual vertical scroll for stacked arp lanes. Manages a vector of `IArpLane*` children (heterogeneous -- ArpLaneEditor, ArpModifierLane, ArpConditionLane), calculates total content height based on collapsed/expanded states, and provides scroll offset when content exceeds the viewport. Children are added programmatically (not from XML). No `dynamic_cast` is used in the container -- all lane interaction is through the `IArpLane` interface.

**When to use:** Multi-lane arpeggiator display where lanes need vertical stacking, independent collapse/expand, and scrolling when content exceeds the viewport. Supports mixed lane types via IArpLane polymorphism.

### Key API

```cpp
class ArpLaneContainer : public CViewContainer {
    // Viewport
    void setViewportHeight(float height);

    // Lane management (accepts any IArpLane implementor)
    void addLane(IArpLane* lane);                  // Appends, sets collapse callback, recalculates
    void removeLane(IArpLane* lane);               // Removes, recalculates layout
    size_t getLaneCount() const;
    IArpLane* getLane(size_t index) const;          // Returns IArpLane* (for layout only)

    // Layout
    void recalculateLayout();                      // Stacks lanes vertically, clamps scroll
    float getTotalContentHeight() const;

    // Scroll
    float getScrollOffset() const;
    void setScrollOffset(float offset);            // Clamped to [0, maxScrollOffset]
    float getMaxScrollOffset() const;              // max(0, totalContentHeight - viewportHeight)
};
```

### Internal Storage

```cpp
std::vector<IArpLane*> lanes_;  // Changed from std::vector<ArpLaneEditor*> in Spec 080
```

### Layout Algorithm

`recalculateLayout()` iterates all lanes and stacks them vertically:

1. For each lane: height = `isCollapsed()` ? `getCollapsedHeight()` (16px) : `getExpandedHeight()`
2. Lane y-position = cumulative height of all preceding lanes, minus `scrollOffset_`
3. `totalContentHeight_` = sum of all lane heights
4. `scrollOffset_` clamped to `[0, getMaxScrollOffset()]`
5. Each lane resized via `lane->getView()->setViewSize()` to its computed rect

### Scroll Behavior

- Mouse wheel: `scrollOffset_ += -distance * 20.0f` (20px per wheel unit)
- Scroll clamped to `[0, max(0, totalContentHeight - viewportHeight)]`
- When content fits in viewport, scroll is disabled

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
- `addLane()` calls `lane->getView()` to obtain the CView* for `addView()` -- no downcasting needed
- `getLane()` returns `IArpLane*`, not a concrete type -- callers should use the interface only

**Consumers:** Ruinae SEQ tab arpeggiator section (Specs 079 + 080). Holds all 6 lanes (velocity, gate, pitch, ratchet, modifier, condition).
