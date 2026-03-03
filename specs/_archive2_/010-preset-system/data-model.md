# Data Model: 010-preset-system

**Date**: 2026-01-30
**Spec**: `specs/010-preset-system/spec.md`

## 1. Shared Library Entities (plugins/shared/)

### 1.1 PresetManagerConfig

Configuration structure provided by each plugin to customize shared preset operations.

```cpp
// plugins/shared/src/preset/preset_manager_config.h
namespace Krate::Plugins {

struct PresetManagerConfig {
    Steinberg::FUID processorUID;                  // For .vstpreset headers
    std::string pluginName;                        // "Iterum" or "Disrumpo"
    std::string pluginCategoryDesc;                // "Delay" or "Distortion"
    std::vector<std::string> subcategoryNames;     // Subfolder names for scanning/saving
};

} // namespace Krate::Plugins
```

**Validation Rules**:
- `processorUID` must not be all zeros
- `pluginName` must not be empty
- `subcategoryNames` must have at least 1 entry

**Relationships**: Consumed by PresetManager, used to derive preset paths and metadata XML

### 1.2 PresetInfo

Metadata for a single preset file (generalized from Iterum's version).

```cpp
// plugins/shared/src/preset/preset_info.h
namespace Krate::Plugins {

struct PresetInfo {
    std::string name;                           // Display name (from filename or metadata)
    std::string category;                       // Category label (e.g., "Ambient", "Rhythmic")
    std::string subcategory;                    // Directory-derived (e.g., "Granular", "Bass")
    std::filesystem::path path;                 // Full path to .vstpreset file
    bool isFactory = false;                     // True if factory preset (read-only)
    std::string description;                    // Optional description text
    std::string author;                         // Optional author name

    [[nodiscard]] bool isValid() const {
        return !name.empty() && !path.empty();
    }

    bool operator<(const PresetInfo& other) const {
        return name < other.name;
    }
};

} // namespace Krate::Plugins
```

**Change from Iterum**: `DelayMode mode` field replaced with `std::string subcategory`.

### 1.3 PresetManager

Core preset operations class (generalized from Iterum's ~550-line implementation).

```cpp
// plugins/shared/src/preset/preset_manager.h
namespace Krate::Plugins {

class PresetManager {
public:
    using PresetList = std::vector<PresetInfo>;
    using StateProvider = std::function<Steinberg::IBStream*()>;
    using LoadProvider = std::function<bool(Steinberg::IBStream*)>;

    explicit PresetManager(
        const PresetManagerConfig& config,
        Steinberg::Vst::IComponent* processor,
        Steinberg::Vst::IEditController* controller,
        std::filesystem::path userDirOverride = {},
        std::filesystem::path factoryDirOverride = {}
    );

    // Scanning
    PresetList scanPresets();
    // Empty subcategory = return ALL presets ("All" filter). Non-empty = filter to match.
    PresetList getPresetsForSubcategory(const std::string& subcategory) const;
    PresetList searchPresets(std::string_view query) const;

    // Load/Save
    bool loadPreset(const PresetInfo& preset);
    bool savePreset(const std::string& name, const std::string& subcategory,
                    const std::string& description = "");
    bool overwritePreset(const PresetInfo& preset);
    bool deletePreset(const PresetInfo& preset);
    bool importPreset(const std::filesystem::path& sourcePath);

    // Directory Access
    std::filesystem::path getUserPresetDirectory() const;
    std::filesystem::path getFactoryPresetDirectory() const;

    // Validation & State
    static bool isValidPresetName(const std::string& name);
    std::string getLastError() const;
    void setStateProvider(StateProvider provider);
    void setLoadProvider(LoadProvider provider);

    // Configuration access
    const PresetManagerConfig& getConfig() const;

private:
    PresetManagerConfig config_;
    // ... (same private members as current Iterum implementation)
};

} // namespace Krate::Plugins
```

**Key Changes from Iterum**:
- Constructor takes `PresetManagerConfig` instead of relying on hardcoded values
- `savePreset()` takes `subcategory` string instead of `DelayMode`
- `getPresetsForMode()` becomes `getPresetsForSubcategory()`
- XML metadata uses `config_.pluginName` and `config_.pluginCategoryDesc`
- Preset loading uses `config_.processorUID`

### 1.4 Platform Preset Paths

Cross-platform path resolution (parameterized).

```cpp
// plugins/shared/src/platform/preset_paths.h
namespace Krate::Plugins::Platform {

std::filesystem::path getUserPresetDirectory(const std::string& pluginName);
std::filesystem::path getFactoryPresetDirectory(const std::string& pluginName);
bool ensureDirectoryExists(const std::filesystem::path& path);

} // namespace Krate::Plugins::Platform
```

### 1.5 CategoryTabBar (UI)

Generalized tab bar (renamed from ModeTabBar).

```cpp
// plugins/shared/src/ui/category_tab_bar.h
namespace Krate::Plugins {

class CategoryTabBar : public VSTGUI::CView {
public:
    CategoryTabBar(const VSTGUI::CRect& size, std::vector<std::string> labels);

    int getSelectedTab() const;
    void setSelectedTab(int tab);

    using SelectionCallback = std::function<void(int)>;
    void setSelectionCallback(SelectionCallback cb);

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override;

private:
    std::vector<std::string> labels_;
    int selectedTab_ = 0;
    SelectionCallback selectionCallback_;

    VSTGUI::CRect getTabRect(int index) const;
};

} // namespace Krate::Plugins
```

### 1.6 PresetDataSource (UI)

CDataBrowser delegate (generalized to use string subcategory).

```cpp
// plugins/shared/src/ui/preset_data_source.h
namespace Krate::Plugins {

class PresetDataSource : public VSTGUI::DataBrowserDelegateAdapter {
public:
    void setPresets(const std::vector<PresetInfo>& presets);
    void setSubcategoryFilter(const std::string& subcategory);  // Was setModeFilter(int)
    void setSearchFilter(const std::string& query);
    const PresetInfo* getPresetAtRow(int row) const;

    // IDataBrowserDelegate overrides (unchanged)
    // ...

    // Selection callbacks (unchanged)
    // ...

private:
    std::vector<PresetInfo> allPresets_;
    std::vector<PresetInfo> filteredPresets_;
    std::string subcategoryFilter_;  // Was int modeFilter_
    std::string searchFilter_;
    // ...
};

} // namespace Krate::Plugins
```

### 1.7 PresetBrowserView (UI)

Modal overlay for preset management (generalized).

```cpp
// plugins/shared/src/ui/preset_browser_view.h
namespace Krate::Plugins {

class PresetBrowserView : public VSTGUI::CViewContainer,
                          public VSTGUI::IControlListener,
                          public VSTGUI::IKeyboardHook,
                          public VSTGUI::ITextEditListener {
public:
    PresetBrowserView(const VSTGUI::CRect& size,
                      PresetManager* presetManager,
                      std::vector<std::string> tabLabels);

    void open(const std::string& currentSubcategory);  // Was open(int currentMode)
    void openWithSaveDialog(const std::string& currentSubcategory);
    void close();
    bool isOpen() const;

    // Callbacks
    void onCategoryTabChanged(int tabIndex);  // Was onModeTabChanged
    // ... (all other callbacks unchanged)

private:
    PresetManager* presetManager_ = nullptr;
    std::vector<std::string> tabLabels_;       // For CategoryTabBar
    CategoryTabBar* categoryTabBar_ = nullptr; // Was ModeTabBar*
    // ... (same structure as Iterum version)
};

} // namespace Krate::Plugins
```

### 1.8 SearchDebouncer and PresetBrowserLogic

These are header-only, pure C++ components with no plugin-specific types. They move as-is with only a namespace change from `Iterum` to `Krate::Plugins`.

---

## 2. Disrumpo Preset Serialization Format (Already Implemented)

The serialization format is already implemented in `plugins/disrumpo/src/processor/processor.cpp`. This section documents the binary layout for reference.

### Version History

| Version | Content | Byte Count (approx) |
|---------|---------|---------------------|
| v1 | Global params (inputGain, outputGain, globalMix) | 16 bytes |
| v2 | + Band management (bandCount, 8x bandState, 7x crossover) | +136 bytes |
| v3 | + VSTGUI params (implied, same stream) | 0 additional |
| v4 | + Sweep (core 6, LFO 6, envelope 4, custom curve N) | +variable |
| v5 | + Modulation (sources, macros, 32 routings) | +variable |
| v6 | + Morph nodes (8 bands x (5 + 4 nodes x 7 values)) | +variable |

### Stream Layout (v6 - Current)

```
[int32]  version = 6
[float]  inputGain (normalized)
[float]  outputGain (normalized)
[float]  globalMix (normalized)
[int32]  bandCount
For each of 8 bands:
    [float]  gainDb
    [float]  pan
    [int8]   solo
    [int8]   bypass
    [int8]   mute
For each of 7 crossovers:
    [float]  frequency
--- v4: Sweep ---
[int8]   sweepEnabled
[float]  sweepFrequency (normalized)
[float]  sweepWidth (normalized)
[float]  sweepIntensity (normalized)
[int8]   sweepFalloffMode
[int8]   sweepMorphLinkMode
[int8]   sweepLFOEnabled
[float]  sweepLFORate (normalized)
[int8]   sweepLFOWaveform
[float]  sweepLFODepth
[int8]   sweepLFOTempoSync
[int8]   sweepLFONoteValue (encoded)
[int8]   sweepEnvEnabled
[float]  sweepEnvAttack (normalized)
[float]  sweepEnvRelease (normalized)
[float]  sweepEnvSensitivity
[int32]  customCurvePointCount
For each breakpoint:
    [float]  x
    [float]  y
--- v5: Modulation ---
(LFO1: 7 values, LFO2: 7 values, EnvFollower: 4, Random: 3,
 Chaos: 3, S&H: 3, PitchFollower: 4, Transient: 3,
 4 Macros x 4 values, 32 Routings x 4 values)
--- v6: Morph Nodes ---
For each of 8 bands:
    [float]  morphX
    [float]  morphY
    [int8]   morphMode
    [int8]   activeNodeCount
    [float]  morphSmoothing
    For each of 4 nodes:
        [int8]   type
        [float]  drive
        [float]  mix
        [float]  toneHz
        [float]  bias
        [float]  folds
        [float]  bitDepth
```

---

## 3. Iterum Adapter Layer

After the shared library refactoring, Iterum needs thin wrappers to bridge between its `DelayMode` enum and the shared library's string-based subcategory.

```cpp
// plugins/iterum/src/preset/iterum_preset_config.h
namespace Iterum {

inline Krate::Plugins::PresetManagerConfig makeIterumPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        .processorUID = kProcessorUID,
        .pluginName = "Iterum",
        .pluginCategoryDesc = "Delay",
        .subcategoryNames = {
            "Granular", "Spectral", "Shimmer", "Tape", "BBD",
            "Digital", "PingPong", "Reverse", "MultiTap", "Freeze", "Ducking"
        }
    };
}

// Convert DelayMode to subcategory string for save operations
inline std::string delayModeToSubcategory(DelayMode mode) {
    static const char* names[] = {
        "Granular", "Spectral", "Shimmer", "Tape", "BBD",
        "Digital", "PingPong", "Reverse", "MultiTap", "Freeze", "Ducking"
    };
    int idx = static_cast<int>(mode);
    if (idx >= 0 && idx < static_cast<int>(DelayMode::NumModes))
        return names[idx];
    return "Digital";
}

// Convert subcategory string back to DelayMode for Iterum-specific logic
inline DelayMode subcategoryToDelayMode(const std::string& subcategory) {
    // Same mapping as existing modeMapping in preset_manager.cpp
    static const std::pair<std::string, DelayMode> mapping[] = {
        {"Granular", DelayMode::Granular}, {"Spectral", DelayMode::Spectral},
        {"Shimmer", DelayMode::Shimmer},   {"Tape", DelayMode::Tape},
        {"BBD", DelayMode::BBD},           {"Digital", DelayMode::Digital},
        {"PingPong", DelayMode::PingPong}, {"Reverse", DelayMode::Reverse},
        {"MultiTap", DelayMode::MultiTap}, {"Freeze", DelayMode::Freeze},
        {"Ducking", DelayMode::Ducking}
    };
    for (const auto& [name, mode] : mapping) {
        if (subcategory == name) return mode;
    }
    return DelayMode::Digital;
}

} // namespace Iterum
```

---

## 4. Disrumpo Preset Configuration

```cpp
// plugins/disrumpo/src/preset/disrumpo_preset_config.h
namespace Disrumpo {

inline Krate::Plugins::PresetManagerConfig makeDisrumpoPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        .processorUID = kProcessorUID,
        .pluginName = "Disrumpo",
        .pluginCategoryDesc = "Distortion",
        .subcategoryNames = {
            "Init", "Sweep", "Morph", "Bass", "Leads",
            "Pads", "Drums", "Experimental", "Chaos", "Dynamic", "Lo-Fi"
        }
    };
}

} // namespace Disrumpo
```

---

## 5. Entity Relationship Diagram

```
PresetManagerConfig
    |
    v
PresetManager ---------> PresetInfo (vector)
    |                        ^
    |                        |
    +-- Platform::getUserPresetDirectory(config.pluginName)
    +-- Platform::getFactoryPresetDirectory(config.pluginName)
    |
    v
PresetBrowserView
    |
    +-- CategoryTabBar(tabLabels from config)
    +-- PresetDataSource(uses PresetInfo with string subcategory)
    +-- SearchDebouncer
    +-- PresetBrowserLogic (keyboard/selection)
    +-- SavePresetDialogView
```
