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
│   └── ui/                         # VSTGUI preset browser components
│       ├── preset_browser_view.h/.cpp  # Modal preset browser overlay
│       ├── category_tab_bar.h/.cpp     # Subcategory filter tabs
│       ├── preset_data_source.h/.cpp   # CDataBrowser data source
│       ├── save_preset_dialog_view.h/.cpp  # Save dialog overlay
│       └── search_debouncer.h          # Search input debounce
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
