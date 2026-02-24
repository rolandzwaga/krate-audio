# Plugin Architecture

[← Back to Architecture Index](README.md)

---

## VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/iterum/src/processor/` | Audio processing (real-time) |
| Controller | `plugins/iterum/src/controller/` | UI state management |
| Entry | `plugins/iterum/src/entry.cpp` | Factory registration |
| IDs | `plugins/iterum/src/plugin_ids.h` | Parameter and component IDs |

---

## Parameter Flow

```
Host → Processor.processParameterChanges() → atomics → process()
                                                    ↓
Host ← Controller.setParamNormalized() ← IMessage ←┘
```

---

## State Flow

```
Save: Processor.getState() → IBStream (parameters + version)
Load: IBStream → Processor.setState() → Controller.setComponentState()
```

---

## UI Components

| Component | Path | Purpose |
|-----------|------|---------|
| TapPatternEditor | `plugins/iterum/src/ui/tap_pattern_editor.h` | Custom tap pattern visual editor |
| CopyPatternButton | `plugins/iterum/src/controller/controller.cpp` | Copy current pattern to custom |
| ResetPatternButton | `plugins/iterum/src/controller/controller.cpp` | Reset pattern to linear spread |

---

## TapPatternEditor (Spec 046)
**Path:** [tap_pattern_editor.h](../../plugins/iterum/src/ui/tap_pattern_editor.h) | **Since:** 0.9.6

Visual editor for creating custom delay tap patterns. Extends `VSTGUI::CControl`.

```cpp
class TapPatternEditor : public VSTGUI::CControl {
    void setTapTimeRatio(size_t tapIndex, float ratio);  // [0, 1]
    void setTapLevel(size_t tapIndex, float level);      // [0, 1]
    void setActiveTapCount(size_t count);                // 1-16
    void setSnapDivision(SnapDivision division);         // Grid snapping
    void resetToDefault();                                // Linear spread, full levels
    void onPatternChanged(int patternIndex);             // Cancel drag if not Custom
    void setParameterCallback(ParameterCallback cb);     // Notify on user drag
};
```

**Features:**
- Horizontal drag adjusts tap timing (X axis = time ratio 0-1)
- Vertical drag adjusts tap level (Y axis = level 0-1)
- Grid snapping: Off, 1/4, 1/8, 1/16, 1/32, Triplet
- Only editable when pattern == Custom (index 19)
- Copy from any pattern to use as starting point

**Parameter IDs (Custom Pattern):**
- Time ratios: `kMultiTapCustomTime0Id` - `kMultiTapCustomTime15Id` (950-965)
- Levels: `kMultiTapCustomLevel0Id` - `kMultiTapCustomLevel15Id` (966-981)
- UI tags: `kMultiTapPatternEditorTagId` (920), `kMultiTapCopyPatternButtonTagId` (921), `kMultiTapResetPatternButtonTagId` (923)

**Visibility:** Controlled by `patternEditorVisibilityController_` using IDependent pattern. Visible only when `kMultiTapTimingPatternId` == 19 (Custom).

---

## Shared Plugin Infrastructure (KratePluginsShared)

**Path:** `plugins/shared/` | **Namespace:** `Krate::Plugins` | **Since:** Spec 010

Shared static library providing cross-plugin preset management, UI components, and platform abstraction.

### Architecture

```
plugins/shared/
├── src/
│   ├── preset/                    # Core preset management
│   │   ├── preset_manager_config.h  # Plugin-specific configuration struct
│   │   ├── preset_manager.h/.cpp    # File scanning, load/save, search
│   │   └── preset_info.h            # Preset metadata (name, path, subcategory)
│   ├── platform/
│   │   └── preset_paths.h/.cpp      # Cross-platform preset directory resolution
│   └── ui/                         # VSTGUI shared UI components
│       ├── preset_browser_view.h/.cpp  # Modal preset browser overlay
│       ├── category_tab_bar.h/.cpp     # Subcategory filter tabs
│       ├── preset_data_source.h/.cpp   # CDataBrowser data source
│       ├── save_preset_dialog_view.h/.cpp  # Save dialog overlay
│       ├── search_debouncer.h          # Search input debounce
│       ├── step_pattern_editor.h       # Visual step pattern editor (Spec 046)
│       ├── xy_morph_pad.h             # 2D XY morph pad control (Spec 047)
│       ├── arc_knob.h                  # Minimal arc-style knob control
│       ├── fieldset_container.h        # Labeled container with border
│       ├── color_utils.h              # Shared color manipulation utilities
│       ├── mod_source_colors.h        # Modulation source/dest enums, colors, route structs (Spec 049)
│       ├── mod_matrix_grid.h          # Slot-based modulation route list with tabs (Spec 049)
│       ├── mod_ring_indicator.h       # Colored arc overlays on destination knobs (Spec 049)
│       ├── mod_heatmap.h             # Source-by-destination routing heatmap (Spec 049)
│       ├── bipolar_slider.h          # Bipolar (-1 to +1) slider control (Spec 049)
│       └── oscillator_type_selector.h  # Dropdown tile grid oscillator type chooser (Spec 050)
└── tests/                         # Shared library tests
```

### PresetManagerConfig Pattern

Each plugin provides its own configuration to the shared library:

```cpp
// In plugin's preset config header (e.g., disrumpo_preset_config.h)
Krate::Plugins::PresetManagerConfig makeDisrumpoPresetConfig() {
    return {
        .processorUID = kProcessorUID,
        .pluginName = "Disrumpo",
        .pluginCategoryDesc = "Distortion",
        .subcategoryNames = {"Init", "Sweep", "Morph", "Bass", ...}
    };
}

// Tab labels for the browser (includes "All" as first entry)
std::vector<std::string> getDisrumpoTabLabels() {
    return {"All", "Init", "Sweep", "Morph", "Bass", ...};
}
```

### Integration Quickstart

To add preset support to a new plugin:

1. **Create config**: `plugins/myplugin/src/preset/myplugin_preset_config.h`
2. **Link library**: Add `KratePluginsShared` to CMakeLists.txt `target_link_libraries`
3. **Controller setup**: Initialize PresetManager in `Controller::initialize()`
4. **Set callbacks**: Wire `setStateProvider()` and `setLoadProvider()` for state persistence
5. **Create UI**: Instantiate `PresetBrowserView` with tab labels in editor creation

### Key Classes

| Class | Purpose |
|-------|---------|
| `PresetManager` | Scan, load, save, search, delete, import presets |
| `PresetManagerConfig` | Plugin-specific FUID, name, category, subcategories |
| `PresetInfo` | Single preset metadata: name, path, subcategory, isFactory |
| `PresetBrowserView` | Full modal preset browser with tabs, list, search |
| `CategoryTabBar` | Horizontal tab bar for subcategory filtering |
| `PresetDataSource` | CDataBrowser adapter for preset list display |
| `SavePresetDialogView` | Inline save dialog with name field and category selector |
| `SearchDebouncer` | Debounced text input for search field |

### StepPatternEditor (Spec 046)
**Path:** [step_pattern_editor.h](../../plugins/shared/src/ui/step_pattern_editor.h) | **Since:** 0.1.0

Visual step pattern editor for creating and editing step sequences. Extends `VSTGUI::CControl`. Plugin-agnostic -- communicates exclusively via `ParameterCallback` and configurable base parameter IDs.

```cpp
class StepPatternEditor : public VSTGUI::CControl {
    // Configuration
    void setStepLevelBaseParamId(int32_t baseId);     // First step level parameter ID
    void setNumSteps(int numSteps);                    // 2-32 visible steps
    void setParameterCallback(ParameterCallback cb);   // Notify host of value changes
    void setBeginEditCallback(BeginEditCallback cb);   // Gesture begin
    void setEndEditCallback(EndEditCallback cb);       // Gesture end

    // Step data
    void setStepLevel(int index, float level);         // Set step level [0, 1]
    float getStepLevel(int index) const;               // Get step level

    // Euclidean rhythm
    void setEuclideanEnabled(bool enabled);
    void setEuclideanHits(int hits);
    void setEuclideanRotation(int rotation);
    void regenerateEuclidean();                         // Apply Bjorklund algorithm

    // Playback
    void setPlaybackStep(int step);                    // Highlight active step
    void setPhaseOffset(float offset);                 // Visual phase offset line

    // Presets and transforms
    void applyPresetAll();                             // All steps to 1.0
    void applyPresetOff();                             // All steps to 0.0
    void applyPresetAlternate();                       // Alternating 1.0/0.0
    void applyPresetRampUp();                          // Linear ramp 0->1
    void applyPresetRampDown();                        // Linear ramp 1->0
    void applyPresetRandom();                          // Random values
    void applyTransformInvert();                       // 1.0 - level
    void applyTransformShiftRight();                   // Rotate pattern right
    void applyTransformShiftLeft();                    // Rotate pattern left
};
```

**Features:**
- Click-and-drag vertical editing with paint mode across steps
- Shift+drag: fine mode (0.1x sensitivity, >= 1/1024th precision)
- Double-click: reset step to 1.0; Alt+click: toggle 0/1
- Escape key: revert to pre-drag levels
- Euclidean dot indicators with ghost note preservation
- Playback position highlight updated via atomic pointer (30fps poll)
- Phase offset vertical line indicator
- Scroll/zoom for high step counts (>16 steps)
- Color-coded bars: accent (>0.85), normal (0.15-0.85), ghost (<0.15), silent (0.0)
- Registered as "StepPatternEditor" via VSTGUI ViewCreator system

**Consumers:** Ruinae TranceGate (Spec 046). Any future plugin needing step pattern editing.

### ColorUtils
**Path:** [color_utils.h](../../plugins/shared/src/ui/color_utils.h) | **Since:** 0.1.0

Header-only color manipulation utilities for VSTGUI custom controls.

```cpp
namespace Krate::Plugins::ColorUtils {
    CColor withAlpha(const CColor& color, uint8_t alpha);   // Set alpha channel
    CColor blendColors(const CColor& a, const CColor& b, float t);  // Linear blend
}

// Free functions in Krate::Plugins namespace
CColor lerpColor(const CColor& a, const CColor& b, float t);  // Component-wise lerp
CColor bilinearColor(bottomLeft, bottomRight, topLeft, topRight, tx, ty);  // 4-corner interpolation
```

**Consumers:** ArcKnob, FieldsetContainer, StepPatternEditor, XYMorphPad.

### ArcKnob
**Path:** [arc_knob.h](../../plugins/shared/src/ui/arc_knob.h) | **Since:** 0.1.0

Minimal arc-style knob control rendered as a filled arc with value indicator. Extends `VSTGUI::CControl`. Registered as "ArcKnob" via VSTGUI ViewCreator.

### FieldsetContainer
**Path:** [fieldset_container.h](../../plugins/shared/src/ui/fieldset_container.h) | **Since:** 0.1.0

Labeled container with rounded border and title, similar to HTML fieldset. Extends `VSTGUI::CViewContainer`. Registered as "FieldsetContainer" via VSTGUI ViewCreator.

### XYMorphPad (Spec 047)
**Path:** [xy_morph_pad.h](../../plugins/shared/src/ui/xy_morph_pad.h) | **Since:** 0.1.0

2D XY pad for simultaneous control of two parameters with bilinear gradient background. Extends `VSTGUI::CControl`. Registered as "XYMorphPad" via VSTGUI ViewCreator.

```cpp
class XYMorphPad : public VSTGUI::CControl {
    // Configuration
    void setController(EditControllerEx1* controller);
    void setSecondaryParamId(ParamID paramId);
    void setMorphPosition(float x, float y);        // [0, 1] for both axes
    void setModulationRange(float rangeX, float rangeY); // Bipolar mod ranges

    // Colors
    void setColorBottomLeft/BottomRight/TopLeft/TopRight(CColor);
    void setCursorColor(CColor);
    void setLabelColor(CColor);

    // Display options
    void setGridSize(int size);                      // Gradient resolution (4-64)
    void setCrosshairOpacity(float opacity);          // [0, 1]
};
```

**Features:**
- Bilinear gradient background: 4-corner color interpolation on configurable grid (default 24x24)
- Cursor rendering: 16px open circle + 4px center dot
- Crosshair lines at cursor position (configurable opacity, default 12%)
- Corner labels: "A"/"B" (bottom-left/right), "Dark"/"Bright" (bottom/top center)
- Position readout: "Mix: 0.XX  Tilt: +Y.YdB" (hidden below 100px)
- Dual-parameter pattern: X axis via CControl tag/setValue, Y axis via controller->performEdit()
- Shift+drag: fine mode (0.1x sensitivity) with no-jump re-anchoring on Shift toggle
- Double-click: reset to center (0.5, 0.5)
- Scroll wheel: vertical adjusts Y (tilt), horizontal adjusts X (morph), Shift for fine
- Escape: cancel drag and revert to pre-drag values
- Modulation visualization: translucent stripes/rectangle showing LFO sweep extent
- Minimum size: 80x80px

**ViewCreator Attributes:**
- `bottom-left-color`, `bottom-right-color`, `top-left-color`, `top-right-color` (kColorType)
- `cursor-color`, `label-color` (kColorType)
- `crosshair-opacity` (kFloatType)
- `grid-size` (kIntegerType)
- `secondary-tag` (kTagType)

**Consumers:** Ruinae Oscillator Mixer (Spec 047). Any future plugin needing 2D parameter control.

### ADSRDisplay (Spec 048)
**Path:** [adsr_display.h](../../plugins/shared/src/ui/adsr_display.h) | **Since:** 0.18.0

Interactive ADSR envelope editor and display with per-segment curve shaping. Extends `VSTGUI::CControl`. Registered as "ADSRDisplay" via VSTGUI ViewCreator. Plugin-agnostic -- communicates exclusively via `ParameterCallback`, configurable base parameter IDs, and atomic pointers for playback visualization.

```cpp
class ADSRDisplay : public VSTGUI::CControl {
    // ADSR time/level setters (called from controller on parameter changes)
    void setAttackMs(float ms);              // 0.1 - 10000
    void setDecayMs(float ms);               // 0.1 - 10000
    void setSustainLevel(float level);       // 0.0 - 1.0
    void setReleaseMs(float ms);             // 0.1 - 10000

    // Simple curve shaping
    void setAttackCurve(float curveAmount);  // [-1, +1]
    void setDecayCurve(float curveAmount);
    void setReleaseCurve(float curveAmount);

    // Bezier mode
    void setBezierEnabled(bool enabled);
    void setBezierHandleValue(int segment, int handle, int axis, float value);

    // Parameter ID configuration
    void setAdsrBaseParamId(uint32_t baseId);       // Attack, Decay, Sustain, Release
    void setCurveBaseParamId(uint32_t baseId);       // AttackCurve, DecayCurve, ReleaseCurve
    void setBezierEnabledParamId(uint32_t paramId);
    void setBezierBaseParamId(uint32_t baseId);      // 12 Bezier CPs (3 segments x 2 handles x 2 axes)

    // Callbacks (decoupled from controller)
    void setParameterCallback(ParameterCallback cb);
    void setBeginEditCallback(EditCallback cb);
    void setEndEditCallback(EditCallback cb);

    // Playback visualization (atomic pointer wiring)
    void setPlaybackStatePointers(std::atomic<float>* outputPtr,
                                   std::atomic<int>* stagePtr,
                                   std::atomic<bool>* activePtr);
};
```

**Features:**
- Logarithmic time axis with 15% minimum segment width (extreme ratios always visible)
- Draggable control points: Peak (attack time), Sustain (decay time + sustain level), End (release time)
- Draggable curve segments: vertical drag adjusts curve amount [-1, +1] continuously
- Bezier mode: toggle [S]/[B] button enables 2 handles per segment for S-curves and overshoots
- Shift+drag: fine mode (0.1x sensitivity) with no-jump mid-drag toggle
- Double-click: reset control point or curve to default
- Escape: cancel drag and revert to pre-drag values
- Real-time playback dot: bright dot traces envelope position at ~30fps via atomic pointer polling
- Visual elements: filled gradient curve, grid lines, time labels, sustain hold dashed line, gate marker
- Color-per-envelope identity (Amp=blue, Filter=amber, Mod=purple configured in editor.uidesc)

**ViewCreator Attributes:**
- `fill-color`, `stroke-color`, `background-color`, `grid-color`, `control-point-color`, `text-color` (kColorType)

**Parameter IDs (Ruinae):**
- Amp envelope base: 700 (ADSR), 704 (curves), 707 (Bezier enabled), 708 (Bezier CPs)
- Filter envelope base: 800 (ADSR), 804 (curves), 807 (Bezier enabled), 808 (Bezier CPs)
- Mod envelope base: 900 (ADSR), 904 (curves), 907 (Bezier enabled), 908 (Bezier CPs)

**Consumers:** Ruinae Amp/Filter/Mod envelope displays (Spec 048). Any future synthesizer plugin needing ADSR editing.

### ModSourceColors / Data Model (Spec 049)
**Path:** [mod_source_colors.h](../../plugins/shared/src/ui/mod_source_colors.h) | **Since:** 0.19.0

Header-only modulation data model providing enums, color registry, route structs, and abbreviation tables for modulation routing UI components.

```cpp
namespace Krate::Plugins {
    enum class ModSource : int { ENV1=0, ENV2, ENV3, LFO1, LFO2, Velocity, KeyTrack, Aftertouch, ModWheel, GateOutput };
    enum class ModDestination : int { FilterCutoff=0, FilterResonance, MorphPosition, DistortionDrive, TranceGateDepth, OscAPitch, OscBPitch, GlobalFilterCutoff, GlobalFilterResonance, MasterVolume, DelayMix };

    struct ModSourceInfo { CColor color; const char* fullName; const char* abbreviation; };
    struct ModDestInfo { const char* fullName; const char* voiceAbbr; const char* globalAbbr; };
    struct ModRoute { ModSource source; ModDestination destination; float amount; int curve; float smoothMs; int scale; bool bypass; bool active; };
    struct VoiceModRoute { /* 14-byte fixed-size serializable route for IMessage protocol */ };

    constexpr int kMaxGlobalRoutes = 8;
    constexpr int kMaxVoiceRoutes = 16;
    constexpr int kNumModSources = 10;
    constexpr int kNumModDestinations = 11;
    constexpr int kNumVoiceModSources = 7;
    constexpr int kNumVoiceModDestinations = 7;
}
```

**Source Colors (FR-011, FR-048):**
- ENV 1: rgb(80,140,200) blue -- matches ADSRDisplay Amp envelope
- ENV 2: rgb(220,170,60) amber -- matches ADSRDisplay Filter envelope
- ENV 3: rgb(160,90,200) purple -- matches ADSRDisplay Mod envelope
- LFO 1: rgb(78,205,196) teal
- LFO 2: rgb(255,107,107) coral
- Velocity: rgb(149,232,107) green
- KeyTrack: rgb(137,221,255) light blue
- Aftertouch: rgb(199,146,234) lavender
- ModWheel: rgb(255,203,107) yellow
- GateOutput: rgb(220,130,60) orange

**Consumers:** ModMatrixGrid, ModRingIndicator, ModHeatmap, Controller integration code.

### ModMatrixGrid (Spec 049)
**Path:** [mod_matrix_grid.h](../../plugins/shared/src/ui/mod_matrix_grid.h) | **Since:** 0.19.0

Slot-based modulation route list with Global/Voice tabs, expandable detail rows, and scrollable route area. Extends `VSTGUI::CViewContainer`. Registered as "ModMatrixGrid" via VSTGUI ViewCreator.

```cpp
class ModMatrixGrid : public VSTGUI::CViewContainer {
    // Route management
    void addRoute();                                    // Add to first empty slot
    void removeRoute(int slotIndex);                    // Remove and shift up
    void setGlobalRoute(int index, const ModRoute& route);  // Programmatic sync
    void setVoiceRoute(int index, const VoiceModRoute& route);

    // Tab control
    void setActiveTab(int tab);                         // 0=Global, 1=Voice
    int getActiveTab() const;

    // Route selection
    void selectRoute(int sourceIndex, int destIndex);   // Cross-component mediation

    // Callbacks (decoupled from controller)
    void setRouteChangedCallback(RouteChangedCallback cb);
    void setRouteRemovedCallback(RouteRemovedCallback cb);
    void setBeginEditCallback(BeginEditCallback cb);
    void setEndEditCallback(EndEditCallback cb);

    // Heatmap sync
    void syncHeatmap();
};
```

**Features:**
- 8 global slots (VST parameters) + 16 voice slots (IMessage protocol)
- Tab bar with route count labels ("Global (3)" / "Voice (1)")
- Scrollable route list via CScrollView (FR-061)
- Per-row controls: source color dot, source/dest dropdowns, BipolarSlider, numeric label, remove button
- Expandable detail section: Curve, Smooth, Scale, Bypass controls per route
- Disclosure triangle toggle for expand/collapse
- Bypassed routes rendered dimmed
- Source/destination dropdowns filtered by tab (Global: 10/11, Voice: 7/7)

**Parameter IDs (Ruinae, Global Routes):**
- Source: 1300 + slot*3 (IDs 1300, 1303, 1306, ...)
- Destination: 1301 + slot*3 (IDs 1301, 1304, 1307, ...)
- Amount: 1302 + slot*3 (IDs 1302, 1305, 1308, ...)
- Curve: 1324 + slot*4 (IDs 1324, 1328, 1332, ...)
- Smooth: 1325 + slot*4 (IDs 1325, 1329, 1333, ...)
- Scale: 1326 + slot*4 (IDs 1326, 1330, 1334, ...)
- Bypass: 1327 + slot*4 (IDs 1327, 1331, 1335, ...)

**Consumers:** Ruinae modulation section (Spec 049). Any future plugin needing modulation routing UI.

### ModRingIndicator (Spec 049)
**Path:** [mod_ring_indicator.h](../../plugins/shared/src/ui/mod_ring_indicator.h) | **Since:** 0.19.0

Colored arc overlay rendered on top of destination knobs showing modulation ranges. Extends `VSTGUI::CView`. Registered as "ModRingIndicator" via VSTGUI ViewCreator.

```cpp
class ModRingIndicator : public VSTGUI::CView {
    // Arc data
    void setArcs(const std::vector<ArcInfo>& arcs);    // Up to 4 individual + composite
    void setBaseValue(float value);                     // Destination knob current value

    // Configuration
    void setStrokeWidth(float width);                   // Arc line width (default 3.0)
    void setDestinationIndex(int index);                // ModDestination enum value
    void setController(void* controller);               // For cross-component mediation
    void setSelectCallback(SelectCallback cb);          // Click-to-select handler

    // Queries
    [[nodiscard]] const std::vector<ArcInfo>& getArcs() const;
    [[nodiscard]] int getDestinationIndex() const;
};
```

**Features:**
- Arc rendering using CGraphicsPath (135-405 degree convention matching ArcKnob)
- Up to 4 individual colored arcs per destination (most recent on top, FR-025)
- 5+ sources: composite gray arc with "+" label (FR-026)
- Arc clamping at 0.0 and 1.0 boundaries (no wraparound, FR-022)
- Bypassed routes excluded from rendering (FR-019)
- Click-to-select: arc hit testing selects route in ModMatrixGrid (FR-027)
- Dynamic tooltip: "{SourceFullName} -> {DestFullName}: {signedAmount}" (FR-028)

**ViewCreator Attributes:**
- `stroke-width` (kFloatType, default 3.0)
- `dest-index` (kIntegerType, ModDestination enum value)

**Consumers:** Ruinae destination knob overlays (Spec 049).

### ModHeatmap (Spec 049)
**Path:** [mod_heatmap.h](../../plugins/shared/src/ui/mod_heatmap.h) | **Since:** 0.19.0

Read-only source-by-destination grid visualization showing modulation routing intensity. Extends `VSTGUI::CView`. Registered as "ModHeatmap" via VSTGUI ViewCreator.

```cpp
class ModHeatmap : public VSTGUI::CView {
    // Cell data
    void setCell(int sourceRow, int destCol, float amount, bool active);
    void setMode(int mode);                             // 0=Global (10x11), 1=Voice (7x7)
    void setHeatmap(const std::vector<HeatmapCell>& cells);  // Bulk update

    // Interaction
    void setCellClickCallback(CellClickCallback cb);    // Click on active cell

    // Queries
    [[nodiscard]] float getCellAmount(int row, int col) const;
    [[nodiscard]] bool isCellActive(int row, int col) const;
};
```

**Features:**
- Grid rendering: source color * |amount| intensity per cell (FR-033)
- Row headers: abbreviated source names (FR-036)
- Column headers: abbreviated destination names (FR-035)
- Empty cells: dark background rgb(30,30,33) (FR-034)
- Click-to-select on active cells: fires CellClickCallback (FR-037)
- Dynamic tooltip: "{SourceFullName} -> {DestFullName}: {signedAmount}" (FR-038)
- Mode switching: Global (10 sources x 11 dests) or Voice (7x7)

**Consumers:** Ruinae modulation section (Spec 049).

### BipolarSlider (Spec 049)
**Path:** [bipolar_slider.h](../../plugins/shared/src/ui/bipolar_slider.h) | **Since:** 0.19.0

Bipolar slider control for -1.0 to +1.0 range with centered fill visualization. Extends `VSTGUI::CControl`. Registered as "BipolarSlider" via VSTGUI ViewCreator.

```cpp
class BipolarSlider : public VSTGUI::CControl {
    // Colors
    void setFillColor(const CColor& color);
    void setTrackColor(const CColor& color);
    void setCenterTickColor(const CColor& color);
};
```

**Features:**
- Centered fill: left of center for negative values, right for positive (FR-007)
- Center tick always visible (FR-008)
- Fine adjustment: Shift+drag at 0.1x scale (FR-009)
- Escape key: cancel drag and revert (FR-010)
- beginEdit/endEdit wrapping for gesture tracking

**ViewCreator Attributes:**
- `fill-color` (kColorType)
- `track-color` (kColorType)
- `center-tick-color` (kColorType)

**Consumers:** ModMatrixGrid route rows (Spec 049). Any future control needing bipolar range input.

### OscillatorTypeSelector (Spec 050)
**Path:** [oscillator_type_selector.h](../../plugins/shared/src/ui/oscillator_type_selector.h) | **Since:** 0.19.0

Dropdown-style oscillator type selector with popup 5x2 tile grid. Extends `VSTGUI::CControl`, implements `IMouseObserver` and `IKeyboardHook`. Registered as "OscillatorTypeSelector" via VSTGUI ViewCreator.

```cpp
class OscillatorTypeSelector : public VSTGUI::CControl,
                               public VSTGUI::IMouseObserver,
                               public VSTGUI::IKeyboardHook {
    // Identity
    void setIdentity(const std::string& identity);  // "a" = blue, "b" = orange
    const std::string& getIdentity() const;
    CColor getIdentityColor() const;

    // State
    int getCurrentIndex() const;             // 0-9
    OscType getCurrentType() const;          // OscType enum
    bool isPopupOpen() const;
};
```

**Features:**
- Collapsed state: waveform icon (20x14) + display name (11px) + dropdown arrow (8x5)
- Popup overlay: 260x94px, 5x2 grid of 48x40px cells with waveform icons and labels
- 10 programmatic waveform icons (no bitmaps) via CGraphicsPath
- Identity color support: OSC A = blue rgb(100,180,255), OSC B = orange rgb(255,140,100)
- Smart popup positioning with 4-corner fallback (below-left, below-right, above-left, above-right)
- Click cell to select, click outside or press Escape to dismiss
- Scroll wheel auditioning: cycle through types without opening popup, wraps 0-9
- Keyboard navigation: Enter/Space to open, arrow keys to navigate grid, Enter/Space to confirm
- Host automation: valueChanged() updates display without opening popup
- Multi-instance exclusivity: static sOpenInstance_ ensures only one popup open at a time
- NaN/inf defense: corrupt values treated as 0.5, clamped to valid range (FR-042)

**ViewCreator Attributes:**
- `osc-identity` (kStringType, "a" or "b")

**Free Functions (testable without VSTGUI):**
- `oscTypeIndexFromNormalized(float)` - normalized [0,1] to index [0,9] with NaN defense
- `normalizedFromOscTypeIndex(int)` - index [0,9] to normalized [0,1]
- `oscTypeDisplayName(int)` - full display name for collapsed state
- `oscTypePopupLabel(int)` - abbreviated label for popup cell
- `hitTestPopupCell(double x, double y)` - grid hit testing, returns cell index or -1
- `OscWaveformIcons::getIconPath(OscType)` - normalized point data for icon drawing

**When to use:** When you need a type selector with visual waveform icons and rapid auditioning (scroll wheel) support. Reusable for any plugin that needs oscillator type selection with visual feedback.

**Consumers:** Ruinae OSC A/B type selectors (Spec 050). Control testbench demo instances.

### Factory Preset Generator

Tools for generating factory presets at build time:

| Tool | Path | Purpose |
|------|------|---------|
| `preset_generator` | `tools/preset_generator.cpp` | Iterum factory presets |
| `disrumpo_preset_generator` | `tools/disrumpo_preset_generator.cpp` | Disrumpo factory presets (120 across 11 categories) |

Generate presets: `cmake --build build --target generate_disrumpo_presets`

### Disrumpo Preset Categories (11 + All)

| Category | Count | Focus |
|----------|-------|-------|
| Init | 5 | Clean starting points (1-5 bands) |
| Sweep | 15 | Sweep system features (LFO, envelope, link modes) |
| Morph | 15 | Morph engine (1D, 2D, node transitions) |
| Bass | 10 | Low-end processing (tube, tape, fuzz) |
| Leads | 10 | Aggressive distortion (hard clip, feedback) |
| Pads | 10 | Subtle processing (soft clip, allpass) |
| Drums | 10 | Transient-friendly (bitcrush, rectify) |
| Experimental | 15 | Exotic types (spectral, granular, fractal) |
| Chaos | 10 | Chaos models (Lorenz, Rossler, Henon, Chua) |
| Dynamic | 10 | Modulation-driven (envelope follower, transient) |
| Lo-Fi | 10 | Degradation (bitcrush, sample reduce, aliasing) |

---

## Disrumpo Plugin Components

### VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/disrumpo/src/processor/` | Audio processing (real-time) |
| Controller | `plugins/disrumpo/src/controller/` | UI state management |
| Entry | `plugins/disrumpo/src/entry.cpp` | Factory registration |
| IDs | `plugins/disrumpo/src/plugin_ids.h` | Parameter and component IDs |

### DSP Components (Spec 002-band-management, 009-intelligent-oversampling)

| Component | Path | Purpose |
|-----------|------|---------|
| CrossoverNetwork | `plugins/disrumpo/src/dsp/crossover_network.h` | N-band phase-coherent crossover (1-8 bands) |
| BandProcessor | `plugins/disrumpo/src/dsp/band_processor.h` | Per-band distortion, oversampling, gain/pan/mute |
| BandState | `plugins/disrumpo/src/dsp/band_state.h` | Per-band configuration structure |
| OversamplingUtils | `plugins/disrumpo/src/dsp/oversampling_utils.h` | Morph-weighted oversampling factor computation |

### CrossoverNetwork
**Path:** [crossover_network.h](../../plugins/disrumpo/src/dsp/crossover_network.h) | **Since:** 0.1.0

Phase-coherent multiband crossover using cascaded LR4 filters with D'Appolito allpass compensation.

```cpp
class CrossoverNetwork {
    void prepare(double sampleRate, int numBands);   // Initialize (1-8 bands)
    void reset();                                     // Clear filter states
    void setBandCount(int numBands);                  // Dynamic band change
    void setCrossoverFrequency(int index, float hz); // Set crossover point
    float getCrossoverFrequency(int index) const;    // Get crossover point
    void process(float input, std::array<float, 8>& bands); // Split to bands
    int getBandCount() const;
    bool isPrepared() const;
};
```

**Features:**
- SC-001 compliant: +/-0.1dB flat frequency response when bands summed
- D'Appolito allpass compensation for phase coherence
- Logarithmic default frequency distribution (FR-009)
- Band count change preserves existing crossovers (FR-011a/b)
- Real-time safe: fixed-size arrays, no allocations

### BandProcessor
**Path:** [band_processor.h](../../plugins/disrumpo/src/dsp/band_processor.h) | **Since:** 0.1.0 | **Updated:** 0.3.0 (Spec 009)

Per-band distortion processing with intelligent oversampling, morph engine, gain/pan/mute, and click-free transitions.

```cpp
class BandProcessor {
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize);  // Initialize all state
    void reset();                                           // Clear filter/smoother states

    // Distortion type (triggers oversampling recalculation)
    void setDistortionType(DistortionType type);
    void setDistortionCommonParams(const DistortionCommonParams& params);

    // Morph (triggers oversampling recalculation on position/node changes)
    void setMorphMode(MorphMode mode);
    void setMorphPosition(float x, float y);
    void setMorphNodes(const std::array<MorphNode, kMaxMorphNodes>& nodes, int count);
    void setMorphEnabled(bool enabled);

    // Oversampling control (Spec 009)
    void setMaxOversampleFactor(int maxFactor);    // Global limit: 1, 2, 4, or 8
    int getOversampleFactor() const;               // Current active factor
    int getLatency() const;                        // Always 0 (IIR zero-latency mode)
    bool isOversampleTransitioning() const;        // True during 8ms crossfade

    // Bypass (bit-transparent pass-through when true)
    void setBypassed(bool bypassed);
    bool isBypassed() const;

    // Processing
    void processBlock(float* left, float* right, size_t numSamples);

    // Gain/Pan/Mute
    void setGainDb(float db);
    void setPan(float pan);
    void setMute(bool muted);
};
```

**Oversampling Features (Spec 009):**
- Automatic factor selection based on distortion type profile (26 types: 1x/2x/4x)
- Morph-aware factor selection using inverse-distance-weighted average of node factors
- Global limit parameter clamps all bands to user-specified maximum (1x, 2x, 4x, or 8x)
- 8ms equal-power crossfade between old and new oversampling paths on factor change
- Hysteresis: crossfade only triggers when computed factor actually differs from current
- Abort-and-restart: mid-transition factor changes restart crossfade from current blend state
- IIR zero-latency mode: `getLatency()` always returns 0
- Bypass optimization: bypassed bands skip all processing (bit-transparent pass-through)

**When to use:**
- Processing audio for a single frequency band in the Disrumpo multiband distortion plugin
- Factor selection is automatic -- just set distortion type, morph state, and global limit

### OversamplingUtils (Spec 009)
**Path:** [oversampling_utils.h](../../plugins/disrumpo/src/dsp/oversampling_utils.h) | **Since:** 0.3.0

Pure utility functions for oversampling factor computation. Header-only, no state. Disrumpo-specific (not shared DSP).

```cpp
namespace Disrumpo {
    // Round up to nearest power-of-2 factor (1, 2, 4, or 8), clamped to maxFactor
    constexpr int roundUpToPowerOf2Factor(float value, int maxFactor = 8) noexcept;

    // Get oversampling factor for a single distortion type, clamped to limit
    constexpr int getSingleTypeOversampleFactor(DistortionType type, int maxFactor = 8) noexcept;

    // Compute morph-weighted oversampling factor from node types and blend weights
    constexpr int calculateMorphOversampleFactor(
        const std::array<MorphNode, kMaxMorphNodes>& nodes,
        const std::array<float, kMaxMorphNodes>& weights,
        int activeNodeCount,
        int maxFactor = 8) noexcept;
}
```

**Algorithm:**
1. `calculateMorphOversampleFactor()` computes weighted average of per-node recommended factors
2. Result is rounded up to nearest power-of-2 via `roundUpToPowerOf2Factor()`
3. Final factor is clamped to `maxFactor` (global limit)

**When to use:**
- Called internally by `BandProcessor::recalculateOversampleFactor()` -- not typically called directly
- Useful in tests to verify factor computation independently of BandProcessor

### UI Components (Spec 006-morph-ui)

| Component | Path | Purpose |
|-----------|------|---------|
| MorphPad | `plugins/disrumpo/src/controller/views/morph_pad.h` | 2D XY pad for morph position control |
| SpectrumDisplay | `plugins/disrumpo/src/controller/views/spectrum_display.h` | Crossover frequency visualization |
| VisibilityController | `plugins/disrumpo/src/controller/controller.cpp` | IDependent-based visibility toggle |
| ContainerVisibilityController | `plugins/disrumpo/src/controller/controller.cpp` | Show/hide containers by parameter |
| MorphSweepLinkController | `plugins/disrumpo/src/controller/controller.cpp` | Update morph from sweep frequency |

### MorphPad (Spec 006)
**Path:** [morph_pad.h](../../plugins/disrumpo/src/controller/views/morph_pad.h) | **Since:** 0.2.0

Custom VSTGUI control for 2D morph position control with node visualization. Extends `VSTGUI::CControl`.

```cpp
class MorphPad : public VSTGUI::CControl {
    void setActiveNodeCount(int count);         // 2, 3, or 4 nodes
    void setMorphMode(MorphMode mode);          // Linear1D, Planar2D, Radial2D
    void setMorphPosition(float x, float y);    // [0, 1] cursor position
    void setNodePosition(int idx, float x, float y);  // Custom node positions
    void setNodeType(int idx, DistortionType type);   // For color rendering
    void setNodeWeight(int idx, float weight);  // For connection line opacity
    void setSelectedNode(int idx);              // Node selection (-1 = none)
    void setMorphPadListener(MorphPadListener* listener);
};
```

**Features:**
- Node rendering: 12px filled circles with category-specific colors
- Cursor rendering: 16px open circle, 2px white stroke
- Connection lines: Cursor to nodes, opacity proportional to weight
- Interaction: Click, drag, Shift+drag (10x fine), Alt+drag (node move), double-click (reset)
- Scroll wheel: Vertical scroll adjusts X, horizontal scroll adjusts Y
- Mode visualization: 1D Linear (horizontal constraint), 2D Planar (default), 2D Radial (grid overlay)
- Position label: "X: 0.00 Y: 0.00" at bottom-left

**Category Colors:**
- Saturation: Orange (#FF6B35)
- Wavefold: Teal (#4ECDC4)
- Digital: Green (#95E86B)
- Rectify: Purple (#C792EA)
- Dynamic: Yellow (#FFCB6B)
- Hybrid: Red (#FF5370)
- Experimental: Light Blue (#89DDFF)

### MorphSweepLinkController
**Path:** Controller implementation in `controller.cpp` | **Since:** 0.2.0

IDependent-based controller that updates morph position when sweep frequency changes.

```cpp
class MorphSweepLinkController : public Steinberg::FObject {
    // Listens to sweep frequency parameter changes
    // For each band, applies link mode to compute morph position
    // Updates Band*MorphX/Y parameters
};
```

**Link Modes (MorphLinkMode enum):**
- None: Manual control only
- SweepFreq: Linear (low freq = 0, high freq = 1)
- InverseSweep: Inverted (high freq = 0, low freq = 1)
- EaseIn: sqrt(x) - more range in bass
- EaseOut: x^2 - more range in highs
- HoldRise: Hold at 0 until midpoint, then rise to 1
- Stepped: Quantized to 5 steps (0, 0.25, 0.5, 0.75, 1.0)

### IDependent Pattern for Visibility Controllers
Thread-safe UI visibility updates using VST3 parameter observation.

```cpp
class VisibilityController : public Steinberg::FObject {
    // In constructor:
    watchedParam_->addDependent(this);
    watchedParam_->deferUpdate();

    // In update():
    if (message == IDependent::kChanged) {
        // Update UI visibility (on UI thread)
    }

    // In deactivate() - CRITICAL before editor closes:
    watchedParam_->removeDependent(this);
};
```

**Usage:**
1. Create in `Controller::didOpen()`
2. Call `deactivate()` in `Controller::willClose()` BEFORE releasing
3. Store in `Steinberg::IPtr<>` for automatic reference counting

---

### Parameter ID Encoding (Disrumpo)

Band-level parameters use bit-encoded IDs:

```cpp
// Encoding: (0xF << 12) | (band << 8) | param
// Band index: 0-7, Param type: BandParamType enum

// Examples:
// Band 0 Gain  = 0xF000
// Band 0 Pan   = 0xF001
// Band 1 Mute  = 0xF104
// Band 7 Solo  = 0xF702

constexpr ParamID makeBandParamId(uint8_t band, BandParamType param);
constexpr ParamID makeCrossoverParamId(uint8_t index);  // 0x0F10 + index
```

---

## Ruinae Plugin Components (Spec 045)

### VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/ruinae/src/processor/` | Audio processing (real-time) |
| Controller | `plugins/ruinae/src/controller/` | UI state management |
| Entry | `plugins/ruinae/src/entry.cpp` | Factory registration |
| IDs | `plugins/ruinae/src/plugin_ids.h` | Parameter and component IDs |

### RuinaeEngine Integration

The Processor owns a `Krate::DSP::RuinaeEngine` instance that handles all DSP: voice pool (16 max), modulation, effects chain, and master output. The Processor serves as the bridge between VST3 host APIs and the engine.

```
Host MIDI events -> Processor::processEvents() -> engine_.noteOn/noteOff()
Host param changes -> processParameterChanges() -> atomic storage -> applyParamsToEngine() -> engine setters
Host tempo/transport -> ProcessContext -> BlockContext -> engine_.setBlockContext()
Engine audio output -> process() output buffers -> Host
```

### Parameter Pack Pattern (20 Sections)

Each synthesizer section has a self-contained header in `plugins/ruinae/src/parameters/`:

```cpp
// Example: global_params.h
struct GlobalParams {                    // Atomic storage for thread-safe access
    std::atomic<float> masterGain{1.0f};
    std::atomic<int> voiceMode{0};
    // ...
};
void handleGlobalParamChange(...);       // Denormalize 0-1 to real-world values
void registerGlobalParams(...);          // Register in Controller with names/units
tresult formatGlobalParam(...);          // Display formatting ("0.0 dB", "440 Hz")
void saveGlobalParams(...);              // Serialize to IBStreamer
bool loadGlobalParams(...);              // Deserialize from IBStreamer
void loadGlobalParamsToController(...);  // Sync Controller display from state
```

### Parameter ID Allocation (Flat Ranges)

| Range | Section | Count |
|-------|---------|-------|
| 0-99 | Global (Gain, Voice Mode, Polyphony, Soft Limit, Width, Spread) | 6 |
| 100-199 | OSC A (Type, Tune, Fine, Level, Phase) | 5 |
| 200-299 | OSC B (same as OSC A) | 5 |
| 300-399 | Mixer (Mode, Position, Tilt) | 3 |
| 400-499 | Filter (Type, Cutoff, Resonance, EnvAmount, KeyTrack) | 5 |
| 500-599 | Distortion (Type, Drive, Character, Mix) | 4 |
| 600-699 | Trance Gate (Enabled, NumSteps, Rate, Depth, Attack, Release, Sync, NoteValue, EuclideanEnabled, EuclideanHits, EuclideanRotation, PhaseOffset, StepLevels x32) | 44 |
| 700-799 | Amp Envelope (ADSR) | 4 |
| 800-899 | Filter Envelope (ADSR) | 4 |
| 900-999 | Mod Envelope (ADSR) | 4 |
| 1000-1099 | LFO 1 (Rate, Shape, Depth, Sync) | 4 |
| 1100-1199 | LFO 2 (same as LFO 1) | 4 |
| 1200-1299 | Chaos Mod (Rate, Type, Depth) | 3 |
| 1300-1355 | Mod Matrix (8 slots: Source/Dest/Amount 1300-1323, Curve/Smooth/Scale/Bypass 1324-1355) | 56 |
| 1400-1499 | Global Filter (Enabled, Type, Cutoff, Resonance) | 4 |
| 1500-1599 | FX Enable (1500 kDelayEnabledId, 1501 kReverbEnabledId, 1502 kPhaserEnabledId, 1503 kHarmonizerEnabledId) + Freeze (Enabled, Toggle) | 6 |
| 1600-1699 | Delay (Type, Time, Feedback, Mix, Sync, NoteValue) | 6 |
| 1700-1799 | Reverb (Size, Damping, Width, Mix, PreDelay, Diffusion, Freeze, ModRate, ModDepth) | 9 |
| 1800-1899 | Mono Mode (Priority, Legato, Portamento, PortaMode) | 4 |
| 1900-1999 | Phaser | varies |
| 2000-2099 | Macros (Macro 1-4 Value) | 4 |
| 2100-2199 | Rungler (Osc1Freq, Osc2Freq, Depth, Filter, Bits, LoopMode) | 6 |
| 2200-2299 | Settings (PitchBendRange, VelocityCurve, TuningRef, AllocMode, StealMode, GainComp) | 6 |
| 2800-2899 | Harmonizer (HarmonyMode, Key, Scale, PitchShiftMode, FormantPreserve, NumVoices, DryLevel, WetLevel, Voice1-4 Interval/Level/Pan/Delay/Detune) | 28 |
| 3000-3099 | Arpeggiator (Enabled, Mode, OctaveRange, OctaveMode, TempoSync, NoteValue, FreeRate, GateLength, Swing, LatchMode, Retrigger) | 11 |

### Global Parameters (IDs 0-5)

| ID | Name | Atomic Type | Range (Engine) | Normalized | Default (Norm) | Engine Method | Spec |
|----|------|-------------|----------------|------------|----------------|---------------|------|
| 0 | MasterGain | `std::atomic<float>` | 0.0-2.0 (linear gain) | norm * 2.0 | 0.5 | `engine_.setMasterGain()` | 045 |
| 1 | VoiceMode | `std::atomic<int>` | 0=Polyphonic, 1=Mono | StringListParameter | 0 (Poly) | `engine_.setMode()` | 045, 054 |
| 2 | Polyphony | `std::atomic<int>` | 1-16 voices | StringListParameter | 7 (8 voices) | `engine_.setPolyphony()` | 045 |
| 3 | SoftLimit | `std::atomic<bool>` | on/off | 0=off, 1=on | 1 (on) | `engine_.setSoftLimitEnabled()` | 045 |
| 4 | Width | `std::atomic<float>` | 0.0-2.0 (stereo width) | norm * 2.0 | 0.5 (100%) | `engine_.setStereoWidth()` | 054 |
| 5 | Spread | `std::atomic<float>` | 0.0-1.0 (voice spread) | 1:1 mapping | 0.0 (0%) | `engine_.setStereoSpread()` | 054 |

**Width Parameter (kWidthId = 4)**:
Controls stereo width of the final output via Mid/Side processing. At 0% (engine 0.0) the output collapses to mono. At 100% (engine 1.0, norm 0.5) the stereo image is unchanged (natural). At 200% (engine 2.0, norm 1.0) the stereo image is exaggerated beyond natural width. Displayed as percentage: `int(norm * 200 + 0.5)` followed by "%".

**Spread Parameter (kSpreadId = 5)**:
Controls distribution of polyphonic voices across the stereo field. At 0% all voices are panned to center. At 100% voices are distributed evenly across the stereo field from left to right. Has no audible effect in Mono mode (only one voice exists). Displayed as percentage: `int(norm * 100 + 0.5)` followed by "%".

**When to use Width vs Spread:**
- **Width** affects the overall stereo image using Mid/Side processing. It works on any signal (mono or stereo, single voice or polyphonic). Use Width to narrow or widen the entire output mix.
- **Spread** affects per-voice panning distribution. It only has an audible effect with multiple simultaneous voices in Polyphonic mode. Use Spread to create a wider sound by panning individual voices across the stereo field.
- Both can be used together: Width controls the global stereo balance while Spread controls how voices are distributed within that stereo field.

**VoiceMode UI Control Binding (Spec 054)**:
The VoiceMode parameter (ID 1) was registered as a `StringListParameter` with entries "Polyphonic" and "Mono" in Spec 045 but had no UI control. Spec 054 added a `control-tag` named "VoiceMode" (tag 1) and a `COptionMenu` dropdown in the Voice & Output panel. The COptionMenu auto-populates its items from the StringListParameter -- no manual menu item addition is needed. A "Mode" `CTextLabel` is placed to the left of the dropdown.

### Denormalization Mappings

| Mapping | Parameters | Formula |
|---------|------------|---------|
| Linear | Gain, Levels, Mix, Depth, Sustain | `value * range` |
| Linear (x2) | Width (0-200%) | `normalized * 2.0` |
| Exponential | Filter Cutoff (20-20kHz) | `20 * pow(1000, normalized)` |
| Exponential | LFO Rate (0.01-50Hz) | `0.01 * pow(5000, normalized)` |
| Cubic | Envelope Times (0-10000ms) | `normalized^3 * 10000` |
| Cubic | Portamento Time (0-5000ms) | `normalized^3 * 5000` |
| Bipolar | Tune (-24/+24 st), Mod Amount (-1/+1) | `normalized * range - offset` |
| Identity | Spread (0-100%) | `normalized` (1:1) |

### Versioned State Persistence

```
Stream Format (v1):
  int32: stateVersion (1)
  [GlobalParams] [OscAParams] [OscBParams] [MixerParams] [FilterParams]
  [DistortionParams] [TranceGateParams] [AmpEnvParams] [FilterEnvParams]
  [ModEnvParams] [LFO1Params] [LFO2Params] [ChaosModParams] [ModMatrixParams(v1: 24 params)]
  [GlobalFilterParams] [FreezeParams] [DelayParams] [ReverbParams] [MonoModeParams]

Stream Format (v2): Same as v1 but ModMatrixParams extended to 56 params (adds Curve/Smooth/Scale/Bypass)

Stream Format (v3): Same as v2 + voice routes (16 x VoiceModRoute, 14 bytes each = 224 bytes)
  Voice routes also sent via VoiceModRouteState IMessage from Processor to Controller

Stream Format (v13): Same as v12 + MacroParams (4 floats) + RunglerParams (4 floats + 2 int32s)
  ModSource enum migration for v < 13: source values >= 10 incremented by 1 (Rungler inserted at 10)

Stream Format (v14): Same as v13 + SettingsParams (2 floats + 4 int32s = 24 bytes)
  Settings: pitchBendRangeSemitones, velocityCurve, tuningReferenceHz, voiceAllocMode, voiceStealMode, gainCompensation
  Backward compat for v < 14: settings default to pre-spec values (gain comp OFF)
  See [Plugin State Persistence](plugin-state-persistence.md) for full details.
```

- Unknown future versions: fail closed with safe defaults
- Truncated streams: load what is available, keep defaults for rest
- Version migration: stepwise N -> N+1
- TranceGateParams uses v2 format (adds 32 step levels, Euclidean params, phase offset; v1 migrates with full-volume defaults)
- v3 voice routes: serialized as 224-byte binary blob (16 routes x 14 bytes), sent bidirectionally via IMessage protocol (VoiceModRouteUpdate, VoiceModRouteRemove, VoiceModRouteState)
- GlobalParams Width/Spread use EOF-safe loading (Spec 054): see pattern below

#### EOF-Safe State Loading Pattern (Spec 054)

When adding new fields to the end of an existing parameter pack's serialized state, use this pattern to maintain backward compatibility with old presets that lack the new fields:

```cpp
// In loadGlobalParams() -- AFTER reading all existing (required) fields:

// Width (added in Spec 054 -- EOF-safe for old presets)
if (streamer.readFloat(floatVal))
    params.width.store(floatVal, std::memory_order_relaxed);
// else: keep default 1.0f (natural stereo width)

// Spread (added in Spec 054 -- EOF-safe for old presets)
if (streamer.readFloat(floatVal))
    params.spread.store(floatVal, std::memory_order_relaxed);
// else: keep default 0.0f (all voices centered)
```

**Key difference from existing fields:** The original fields (masterGain, voiceMode, polyphony, softLimit) use `if (!read()) return false` -- hard failure that aborts the entire load. The new fields use `if (read()) store()` -- soft failure that keeps defaults. This asymmetry is intentional: old presets legitimately lack the new fields, so a missing read is not an error.

**When to use this pattern:** Any future spec that appends new global parameters to `GlobalParams` should follow this same pattern. Write the new fields at the end in `saveGlobalParams()`, and read them with soft EOF handling in both `loadGlobalParams()` and `loadGlobalParamsToController()`.

### Dropdown Mappings

`plugins/ruinae/src/parameters/dropdown_mappings.h` provides enum-to-string mappings for all discrete parameters (OscType, FilterType, DistortionType, Waveform, MonoMode, PortaMode, SVFMode, ModSource, RuinaeModDest, ChaosType, NumSteps).

### Arp Modulation Destination Pattern (Spec 078)

The arpeggiator modulation integration (Spec 078) established a pattern for exposing plugin-layer parameters as modulation destinations in the existing ModulationEngine. This pattern requires no new DSP components, no new parameter IDs, and no state serialization changes. It is the recommended approach for adding future modulation destinations.

#### Architecture Overview

The modulation destination system spans three layers:

```
DSP Layer (enum)           UI/Shared Layer (registry)      Plugin Layer (wiring)
RuinaeModDest enum         kGlobalDestNames array           kGlobalDestParamIds array
  in ruinae_engine.h         in mod_matrix_types.h            in controller.cpp
  Values: 64-78              Indices: 0-14                    Indices: 0-14
        |                          |                               |
        v                          v                               v
ModulationEngine           Mod matrix UI dropdown           Controller indicator
  getModulationOffset()      (source, dest, amount)           routing display
        |
        v
Processor::applyParamsToEngine()
  reads offset, applies formula, calls ArpeggiatorCore setter
```

#### RuinaeModDest Enum Extension

**File**: `plugins/ruinae/src/engine/ruinae_engine.h` (lines 66-83)

The `RuinaeModDest` enum defines DSP-side modulation destination IDs. Values start at 64 to avoid collision with per-voice modulation destination IDs. The enum is organized in groups:

| Group | Enum Values | UI Indices | Purpose |
|-------|-------------|------------|---------|
| Global engine destinations | 64-73 | 0-9 | Filter, Volume, Effects, Voice parameters |
| Arpeggiator destinations | 74-78 | 10-14 | Rate, Gate, Octave, Swing, Spice |

```cpp
enum class RuinaeModDest : uint32_t {
    GlobalFilterCutoff     = 64,  // ... existing 10 destinations (64-73)
    AllVoiceFilterEnvAmt   = 73,
    // Arpeggiator modulation destinations (078-modulation-integration)
    ArpRate                = 74,  ///< Arp rate/speed modulation
    ArpGateLength          = 75,  ///< Arp gate length modulation
    ArpOctaveRange         = 76,  ///< Arp octave range modulation
    ArpSwing               = 77,  ///< Arp swing modulation
    ArpSpice               = 78   ///< Arp spice amount modulation
};
```

All values (64-78) are well below `kMaxModDestinations = 128` in `ModulationEngine`.

#### modDestFromIndex() Linear Mapping Invariant

**File**: `plugins/ruinae/src/parameters/dropdown_mappings.h` (line 205)

The function `modDestFromIndex()` maps a UI dropdown index to a `RuinaeModDest` enum value using:

```cpp
inline RuinaeModDest modDestFromIndex(int index) {
    return static_cast<RuinaeModDest>(
        static_cast<uint32_t>(RuinaeModDest::GlobalFilterCutoff) + index);
}
```

This means UI index `i` always maps to enum value `64 + i`. The invariant is protected by a compile-time assertion:

```cpp
// File: plugins/ruinae/src/engine/ruinae_engine.h (lines 85-89)
static_assert(static_cast<uint32_t>(RuinaeModDest::ArpRate) ==
              static_cast<uint32_t>(RuinaeModDest::GlobalFilterCutoff) + 10,
              "ArpRate enum value must equal GlobalFilterCutoff + 10 "
              "for modDestFromIndex() to work correctly");
```

**CRITICAL**: When adding new destinations, enum values MUST be contiguous from `GlobalFilterCutoff` (64) upward. Any gap or re-ordering breaks the linear mapping and causes silent DSP routing errors.

#### UI Destination Registry

**File**: `plugins/shared/src/ui/mod_matrix_types.h`

Two constants must be updated together:

| Constant | Current Value | Purpose |
|----------|---------------|---------|
| `kNumGlobalDestinations` | 15 | Total destination count (line 64) |
| `kGlobalDestNames` | 15 entries | Display names for dropdown (lines 161-184) |

An existing `static_assert` at line 223 enforces `kGlobalDestNames.size() == kNumGlobalDestinations`.

#### Controller Param ID Mapping

**File**: `plugins/ruinae/src/controller/controller.cpp`

The `kGlobalDestParamIds` array maps each destination UI index to a VST parameter ID for indicator routing. Each new destination must have a corresponding arp parameter ID entry:

| UI Index | Destination | Param ID |
|----------|-------------|----------|
| 10 | Arp Rate | `kArpFreeRateId` (3006) |
| 11 | Arp Gate Length | `kArpGateLengthId` (3007) |
| 12 | Arp Octave Range | `kArpOctaveRangeId` (3002) |
| 13 | Arp Swing | `kArpSwingId` (3008) |
| 14 | Arp Spice | `kArpSpiceId` (3290) |

**Accepted limitation**: Index 10 always maps to `kArpFreeRateId` regardless of tempo-sync mode. The mod matrix indicator always highlights the free-rate knob, even when tempo-synced.

An existing `static_assert` enforces `kGlobalDestParamIds.size() == kGlobalDestNames.size()`.

#### Processor-Side Mod Offset Application Pattern

**File**: `plugins/ruinae/src/processor/processor.cpp`, `applyParamsToEngine()` (lines 1239-1337)

The mod offset application follows a strict 5-step pattern for each destination:

```
1. Read base value from atomic param:     baseRate = arpParams_.freeRate.load(relaxed)
2. Read mod offset from engine:           rateOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpRate)
3. Compute effective value per formula:    effectiveRate = baseRate * (1.0f + 0.5f * rateOffset)
4. Clamp to valid range:                  std::clamp(effectiveRate, 0.5f, 50.0f)
5. Pass to ArpeggiatorCore setter:        arpCore_.setFreeRate(effectiveRate)
```

**Enabled guard (FR-015)**: All 5 offset reads are wrapped in `if (arpParams_.enabled.load(relaxed))`. When the arp is disabled, the `else` branch uses raw parameter values without reading mod offsets. On re-enable, the first block reads whatever offset the ModulationEngine most recently computed (maximum 1-block staleness, identical to normal operation latency).

**Per-destination formulas:**

| Destination | Formula | Range | Unit |
|-------------|---------|-------|------|
| Rate (free mode) | `baseRate * (1.0 + 0.5 * offset)` | [0.5, 50.0] | Hz |
| Rate (tempo-sync) | `baseDuration / (1.0 + 0.5 * offset)` converted to Hz | [0.5, 50.0] | Hz |
| Gate Length | `baseGate + 100.0 * offset` | [1.0, 200.0] | % |
| Octave Range | `baseOctave + round(3.0 * offset)` | [1, 4] | int |
| Swing | `baseSwing + 50.0 * offset` | [0.0, 75.0] | % |
| Spice | `baseSpice + offset` | [0.0, 1.0] | normalized |

**Tempo-sync rate override**: When the arp is tempo-synced and `rateOffset != 0`, the processor computes an equivalent free rate from the modulated step duration, then temporarily sets `arpCore_.setTempoSync(false)` and `arpCore_.setFreeRate(effectiveHz)`. When `rateOffset == 0`, normal tempo-sync behavior is preserved. This approach is necessary because ArpeggiatorCore has no API to override step duration while keeping tempo-sync active.

**Octave range change detection**: `prevArpOctaveRange_` tracks the EFFECTIVE (modulated) value, not the raw base. `setOctaveRange()` is only called when the effective value changes, preventing unnecessary selector resets that would break the arp pattern.

**Real-time safety**: All operations are array lookups (getGlobalModOffset is a single array index) plus inline arithmetic (multiply, add, clamp, round). Zero allocations, zero locks, zero exceptions.

#### How to Add Future Mod Destinations

To add a new modulation destination (e.g., effects chain parameters, trance gate parameters), follow these steps:

1. **Extend `RuinaeModDest` enum** in `ruinae_engine.h`:
   - Add new entries after `ArpSpice = 78` with the next sequential values (79, 80, ...)
   - Add a `static_assert` confirming the linear mapping invariant (new entry == `GlobalFilterCutoff + newIndex`)

2. **Update `kNumGlobalDestinations`** in `mod_matrix_types.h` to the new total count

3. **Extend `kGlobalDestNames`** in `mod_matrix_types.h`:
   - Append new `ModDestInfo` entries with full name, medium name, and short abbreviation
   - Update the array template parameter to match the new count

4. **Extend `kGlobalDestParamIds`** in `controller.cpp`:
   - Append the VST parameter ID that corresponds to each new destination
   - The existing `static_assert` will catch any size mismatch

5. **Add mod offset application** in `processor.cpp` `applyParamsToEngine()`:
   - Follow the 5-step pattern: read base, read offset, compute effective, clamp, call setter
   - Choose the appropriate formula (multiplicative for rate-like params, additive for range-like params)
   - Guard with an enabled check if the feature can be disabled

6. **Write integration tests** using the Macro source for deterministic offset control:
   - Route Macro to the new destination with known amount
   - Set Macro value
   - Process blocks to let mod engine compute offsets
   - Verify effective parameter value matches expected formula

**No changes needed to**: `ModulationEngine`, `ArpeggiatorCore` (or target DSP component), state serialization, `plugin_ids.h`, or the mod matrix UI components (they iterate over `kNumGlobalDestinations` automatically).

### Shared Utilities

| File | Purpose | Origin |
|------|---------|--------|
| `controller/parameter_helpers.h` | Dropdown parameter creation helpers | Copied from Iterum |
| `parameters/note_value_ui.h` | Note value dropdown strings | Copied from Iterum |

### Ruinae UI Layout Patterns (Spec 051)

#### 4-Row Layout Structure

The Ruinae editor uses a fixed 900x600px window divided into four horizontal rows, each containing FieldsetContainer sections with accent colors for visual grouping:

| Row | Y Offset | Height | Sections |
|-----|----------|--------|----------|
| Header | 0 | 30px | Logo, PresetBrowserView |
| Row 1: Sound Source | 32 | 160px | OSC A (blue #64B4FF), Spectral Morph (gold #DCA850), OSC B (orange #FF8C64) |
| Row 2: Timbre & Dynamics | 194 | 138px | Filter (cyan #4ECDC4), Distortion (red #E8644C), Envelopes (gray) |
| Row 3: Movement & Modulation | 334 | 130px | Trance Gate (gold #DCA83C), Modulation (green #5AC882) |
| Row 4: Effects & Output | 466 | 80px | Effects (gray #6E7078), Master (silver #C8C8CC) |

The top-to-bottom flow mirrors the signal chain: source generation, timbre shaping, modulation, and output processing.

#### COptionMenu + UIViewSwitchContainer + StringListParameter Pattern

Ruinae uses a consistent pattern throughout the UI for switching between different control layouts based on a discrete parameter selection. The pattern has three components:

1. **StringListParameter** -- A discrete parameter with named entries (registered in a parameter pack header). Drives both the dropdown and the view switcher.
2. **COptionMenu** -- A dropdown selector in `editor.uidesc` bound to the parameter via `control-tag`. Auto-populates its entries from the StringListParameter.
3. **UIViewSwitchContainer** -- A container in `editor.uidesc` bound to the same parameter via `template-switch-control`. Displays one named template at a time, selected by the parameter's discrete index.

```xml
<!-- Dropdown selector -->
<view class="COptionMenu" control-tag="MyViewMode" ... />

<!-- Automatic view switching -->
<view class="UIViewSwitchContainer"
      template-names="MyType_Option1,MyType_Option2,..."
      template-switch-control="MyViewMode" />
```

**Key constraints:**
- `template-switch-control` binds by parameter **name** (not ID). The parameter's discrete index (0, 1, 2, ...) selects which template is displayed.
- Templates are destroyed and recreated when the selection changes. Never cache pointers to controls inside the switch container.
- No controller code is needed for view switching -- the framework handles it automatically. This eliminates manual `setVisible()` calls and member variable tracking.

**Areas using this pattern in Ruinae:**

| Area | Parameter | Templates | Count |
|------|-----------|-----------|-------|
| OSC A type | `OscAType` | `OscA_PolyBLEP`, `OscA_Wavetable`, ... | 10 |
| OSC B type | `OscBType` | `OscB_PolyBLEP`, `OscB_Wavetable`, ... | 10 |
| Filter view | `FilterViewMode` | `Filter_General`, `Filter_Envelope`, ... | varies |
| Distortion view | `DistortionViewMode` | `Dist_Clean`, `Dist_Tube`, ... | varies |
| Delay type | `DelayViewMode` | `Delay_Default`, ... | varies |
| Mod source | `ModSourceViewMode` | `ModSource_LFO1`, `ModSource_LFO2`, `ModSource_Chaos`, + 7 placeholders | 10 |

**Adding a new entry to an existing switcher:**
1. Add the new entry string to the `StringListParameter` in the parameter pack header
2. Create a new named template in `editor.uidesc`
3. Append the template name to the `template-names` attribute of the `UIViewSwitchContainer`

No controller code changes are needed. The dropdown auto-populates and the view switcher auto-selects.

**Mod source area (Spec 053):** The mod source dropdown replaced a manual `IconSegmentButton` + `setVisible()` pattern with this standard COptionMenu + UIViewSwitchContainer approach. Seven placeholder templates (`ModSource_Macros`, `ModSource_Rungler`, `ModSource_EnvFollower`, `ModSource_SampleHold`, `ModSource_Random`, `ModSource_PitchFollower`, `ModSource_Transient`) are empty and ready for future phases to populate with controls -- no dropdown or controller changes needed.

#### FX Chevron Expand/Collapse Pattern

The Effects row uses controller-managed UI state (not VST parameters) to show/hide detail panels. Four `CViewContainer` detail panels are identified by `custom-view-name` attribute in `verifyView()`:

| Panel | custom-view-name | Action Tag |
|-------|-----------------|------------|
| Freeze | `FreezeDetail` | `kActionFxExpandFreezeTag` (10010) |
| Delay | `DelayDetail` | `kActionFxExpandDelayTag` (10011) |
| Reverb | `ReverbDetail` | `kActionFxExpandReverbTag` (10012) |
| Harmonizer | `HarmonizerDetail` | `kActionFxExpandHarmonizerTag` (10022) |

Chevron buttons are `COnOffButton` controls using action tags (not `control-tag` bindings). When clicked, `valueChanged()` in the controller calls `toggleFxDetail(panelIndex)`, which shows the selected panel and hides the others. Only one panel is visible at a time. All panel pointers are nulled in `willClose()` to prevent dangling references.

This pattern is appropriate when expand/collapse state is transient UI state that does not need to be persisted in the plugin state or exposed to host automation.

#### Settings Drawer Slide-Out Pattern (Spec 058)

The Settings drawer is a `CViewContainer` that slides in from the right edge of the 925x880 window when the gear icon in the Master section is clicked. It partially overlaps the main UI content.

**Geometry:**

| State | Drawer X Position | Visible Area |
|-------|-------------------|--------------|
| Closed | x=925 (off-screen) | None |
| Open | x=705 | 220px wide, overlaps rightmost ~195px of main content |

**Components:**

| Element | custom-view-name / control-tag | Purpose |
|---------|-------------------------------|---------|
| Drawer container | `SettingsDrawer` (CViewContainer) | 220x880px panel with bg-drawer background |
| Click-outside overlay | `SettingsOverlay` (ToggleButton) | 925x880px transparent overlay for dismiss gesture |
| Gear icon | `ActionSettingsToggle` (tag 10020) | Existing gear ToggleButton in Master section, now wired |

**Action Tags:**

| Tag | ID | Purpose |
|-----|-----|---------|
| `kActionSettingsToggleTag` | 10020 | Gear icon click toggles drawer open/close |
| `kActionSettingsOverlayTag` | 10021 | Transparent overlay click dismisses open drawer |

**Animation:**
- Duration: 160ms (~10 frames at 60fps)
- Timer: `CVSTGUITimer` with 16ms interval
- Curve: Quadratic ease-out (`1 - (1-t)^2` for opening, `t * (2-t)` for closing)
- Position interpolation: Linear progress 0-1, mapped through ease curve to x position [925, 705]
- Interruption: Changing direction mid-animation naturally reverses from current position (timer continues with flipped target, no restart needed)

**Controller Fields:**
```cpp
CViewContainer* settingsDrawer_ = nullptr;
CView* settingsOverlay_ = nullptr;
CControl* gearButton_ = nullptr;
SharedPointer<CVSTGUITimer> settingsAnimTimer_;
bool settingsDrawerOpen_ = false;
float settingsDrawerProgress_ = 0.0f;   // 0.0 = closed, 1.0 = open
bool settingsDrawerTargetOpen_ = false;
```

**Lifecycle:**
- `verifyView()`: Captures `settingsDrawer_`, `settingsOverlay_`, `gearButton_` pointers
- `valueChanged()`: Handles `kActionSettingsToggleTag` (toggle) and `kActionSettingsOverlayTag` (dismiss)
- `willClose()`: Nulls all drawer pointers and timer, resets state flags

**Z-Order (critical):** In `editor.uidesc`, the overlay and drawer must be the last children of the root template:
1. All main content containers
2. `SettingsOverlay` (transparent click catcher, z-order below drawer)
3. `SettingsDrawer` (topmost, draws on top of overlay and content)

**Drawer state is NOT persisted:** The drawer always starts closed when the plugin loads. Open/closed state is transient UI state, not saved in the preset stream.

**Controls inside drawer:**
- Pitch Bend Range: ArcKnob (tag 2200), default 0.0833
- Velocity Curve: COptionMenu (tag 2201), auto-populated from StringListParameter
- Tuning Reference: ArcKnob (tag 2202), default 0.5
- Voice Allocation: COptionMenu (tag 2203), auto-populated from StringListParameter
- Voice Steal: COptionMenu (tag 2204), auto-populated from StringListParameter
- Gain Compensation: ToggleButton (tag 2205), default ON
