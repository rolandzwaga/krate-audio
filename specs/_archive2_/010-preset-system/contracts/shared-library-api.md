# API Contract: Shared Preset Library

**Library Target**: `KratePluginsShared`
**Namespace**: `Krate::Plugins`
**Location**: `plugins/shared/`

## 1. PresetManagerConfig

```cpp
// plugins/shared/src/preset/preset_manager_config.h
#pragma once
#include "pluginterfaces/base/funknown.h"
#include <string>
#include <vector>

namespace Krate::Plugins {

// IMPORTANT: Field order matters for C++20 designated initializers.
// All designated initializer usage must match this declaration order.
// If fields are reordered, update ALL initializer sites (makeIterumPresetConfig, makeDisrumpoPresetConfig, tests).
struct PresetManagerConfig {
    Steinberg::FUID processorUID;                  // .vstpreset file header identification
    std::string pluginName;                        // Directory path segment ("Iterum", "Disrumpo")
    std::string pluginCategoryDesc;                // Metadata field ("Delay", "Distortion")
    std::vector<std::string> subcategoryNames;     // Subfolder names for scanning/saving
};

} // namespace Krate::Plugins
```

## 2. PresetInfo

```cpp
// plugins/shared/src/preset/preset_info.h
#pragma once
#include <string>
#include <filesystem>

namespace Krate::Plugins {

struct PresetInfo {
    std::string name;
    std::string category;
    std::string subcategory;              // Was: DelayMode mode
    std::filesystem::path path;
    bool isFactory = false;
    std::string description;
    std::string author;

    [[nodiscard]] bool isValid() const;
    bool operator<(const PresetInfo& other) const;
};

} // namespace Krate::Plugins
```

## 3. Platform Preset Paths

```cpp
// plugins/shared/src/platform/preset_paths.h
#pragma once
#include <filesystem>
#include <string>

namespace Krate::Plugins::Platform {

/// Get user preset directory (writable)
/// Windows:  %USERPROFILE%\Documents\Krate Audio\{pluginName}
/// macOS:    ~/Documents/Krate Audio/{pluginName}
/// Linux:    ~/Documents/Krate Audio/{pluginName}
std::filesystem::path getUserPresetDirectory(const std::string& pluginName);

/// Get factory preset directory (read-only)
/// Windows:  %PROGRAMDATA%\Krate Audio\{pluginName}
/// macOS:    /Library/Application Support/Krate Audio/{pluginName}
/// Linux:    /usr/share/krate-audio/{pluginName}
std::filesystem::path getFactoryPresetDirectory(const std::string& pluginName);

/// Ensure directory exists, creating it if necessary
bool ensureDirectoryExists(const std::filesystem::path& path);

} // namespace Krate::Plugins::Platform
```

## 4. PresetManager

```cpp
// plugins/shared/src/preset/preset_manager.h
#pragma once
#include "preset_info.h"
#include "preset_manager_config.h"
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <functional>

namespace Steinberg { class IBStream; }
namespace Steinberg::Vst { class IComponent; class IEditController; }

namespace Krate::Plugins {

class PresetManager {
public:
    using PresetList = std::vector<PresetInfo>;
    using StateProvider = std::function<Steinberg::IBStream*()>;
    using LoadProvider = std::function<bool(Steinberg::IBStream*)>;

    explicit PresetManager(
        PresetManagerConfig config,
        Steinberg::Vst::IComponent* processor,
        Steinberg::Vst::IEditController* controller,
        std::filesystem::path userDirOverride = {},
        std::filesystem::path factoryDirOverride = {}
    );
    ~PresetManager();

    // Scanning
    PresetList scanPresets();
    /// Filter cached presets by subcategory.
    /// Empty string returns ALL presets (equivalent to "All" UI filter).
    /// Non-empty string returns only presets matching that subcategory.
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

    // Validation
    static bool isValidPresetName(const std::string& name);
    std::string getLastError() const;

    // Provider callbacks
    void setStateProvider(StateProvider provider);
    void setLoadProvider(LoadProvider provider);

    // Configuration access
    const PresetManagerConfig& getConfig() const;
};

} // namespace Krate::Plugins
```

## 5. CategoryTabBar

```cpp
// plugins/shared/src/ui/category_tab_bar.h
#pragma once
#include "vstgui/lib/cview.h"
#include <functional>
#include <string>
#include <vector>

namespace Krate::Plugins {

class CategoryTabBar : public VSTGUI::CView {
public:
    CategoryTabBar(const VSTGUI::CRect& size, std::vector<std::string> labels);
    ~CategoryTabBar() override = default;

    int getSelectedTab() const;
    void setSelectedTab(int tab);

    using SelectionCallback = std::function<void(int)>;
    void setSelectionCallback(SelectionCallback cb);

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;

private:
    std::vector<std::string> labels_;
    int selectedTab_ = 0;
    SelectionCallback selectionCallback_;
    VSTGUI::CRect getTabRect(int index) const;
};

} // namespace Krate::Plugins
```

## 6. PresetDataSource

```cpp
// plugins/shared/src/ui/preset_data_source.h
#pragma once
#include "vstgui/lib/idatabrowserdelegate.h"
#include "vstgui/lib/cdatabrowser.h"
#include "../preset/preset_info.h"
#include "preset_browser_logic.h"
#include <vector>
#include <string>
#include <functional>

namespace Krate::Plugins {

class PresetDataSource : public VSTGUI::DataBrowserDelegateAdapter {
public:
    PresetDataSource() = default;
    ~PresetDataSource() override = default;

    void setPresets(const std::vector<PresetInfo>& presets);
    void setSubcategoryFilter(const std::string& subcategory);  // empty = All
    void setSearchFilter(const std::string& query);
    const PresetInfo* getPresetAtRow(int row) const;

    // IDataBrowserDelegate overrides (same signatures as Iterum version)
    int32_t dbGetNumRows(VSTGUI::CDataBrowser* browser) override;
    int32_t dbGetNumColumns(VSTGUI::CDataBrowser* browser) override;
    VSTGUI::CCoord dbGetRowHeight(VSTGUI::CDataBrowser* browser) override;
    VSTGUI::CCoord dbGetCurrentColumnWidth(int32_t index, VSTGUI::CDataBrowser* browser) override;
    void dbDrawCell(VSTGUI::CDrawContext* context, const VSTGUI::CRect& size,
                    int32_t row, int32_t column, int32_t flags,
                    VSTGUI::CDataBrowser* browser) override;
    void dbSelectionChanged(VSTGUI::CDataBrowser* browser) override;
    VSTGUI::CMouseEventResult dbOnMouseDown(const VSTGUI::CPoint& where,
                                             const VSTGUI::CButtonState& buttons,
                                             int32_t row, int32_t column,
                                             VSTGUI::CDataBrowser* browser) override;

    // Selection callbacks (unchanged)
    using SelectionCallback = std::function<void(int)>;
    using DoubleClickCallback = std::function<void(int)>;
    void setSelectionCallback(SelectionCallback cb);
    void setDoubleClickCallback(DoubleClickCallback cb);

    void capturePreClickSelection(VSTGUI::CDataBrowser* browser);
    void clearSelectionState();

private:
    std::vector<PresetInfo> allPresets_;
    std::vector<PresetInfo> filteredPresets_;
    std::string subcategoryFilter_;
    std::string searchFilter_;
    SelectionCallback selectionCallback_;
    DoubleClickCallback doubleClickCallback_;
    int32_t preClickSelectedRow_ = -1;
    int32_t previousSelectedRow_ = -1;
    void applyFilters();
};

} // namespace Krate::Plugins
```

## 7. PresetBrowserView

```cpp
// plugins/shared/src/ui/preset_browser_view.h
#pragma once
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdatabrowser.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include "vstgui/lib/cvstguitimer.h"
#include "search_debouncer.h"
#include <string>
#include <vector>

namespace Krate::Plugins {

class PresetManager;
class PresetDataSource;
class CategoryTabBar;

enum PresetBrowserButtonTags { /* same values as Iterum */ };

class PresetBrowserView : public VSTGUI::CViewContainer,
                          public VSTGUI::IControlListener,
                          public VSTGUI::IKeyboardHook,
                          public VSTGUI::ITextEditListener {
public:
    PresetBrowserView(const VSTGUI::CRect& size,
                      PresetManager* presetManager,
                      std::vector<std::string> tabLabels);
    ~PresetBrowserView() override;

    // Lifecycle
    void open(const std::string& currentSubcategory);
    void openWithSaveDialog(const std::string& currentSubcategory);
    void close();
    bool isOpen() const;

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    void drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& rect) override;
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override;

    // IControlListener
    void valueChanged(VSTGUI::CControl* control) override;

    // IKeyboardHook
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* frame) override;

    // ITextEditListener
    void onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit* textEdit) override;
    void onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit* textEdit) override;

    // Callbacks (same as Iterum but with string subcategory)
    void onCategoryTabChanged(int newTabIndex);
    void onSearchTextChanged(const std::string& text);
    void onPresetSelected(int rowIndex);
    void onPresetDoubleClicked(int rowIndex);
    void onSaveClicked();
    void onImportClicked();
    void onDeleteClicked();
    void onCloseClicked();

private:
    PresetManager* presetManager_ = nullptr;
    std::vector<std::string> tabLabels_;

    // Child views (same structure as Iterum)
    CategoryTabBar* categoryTabBar_ = nullptr;
    VSTGUI::CDataBrowser* presetList_ = nullptr;
    VSTGUI::CTextEdit* searchField_ = nullptr;
    VSTGUI::CTextButton* saveButton_ = nullptr;
    VSTGUI::CTextButton* importButton_ = nullptr;
    VSTGUI::CTextButton* deleteButton_ = nullptr;
    VSTGUI::CTextButton* closeButton_ = nullptr;

    PresetDataSource* dataSource_ = nullptr;

    std::string currentSubcategoryFilter_;
    int selectedPresetIndex_ = -1;
    bool isOpen_ = false;

    // Dialogs (same structure as Iterum)
    // Save, Delete, Overwrite dialog components...

    SearchDebouncer searchDebouncer_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> searchPollTimer_;
    bool isSearchFieldFocused_ = false;

    // Private methods (same as Iterum)
    void createChildViews();
    void createDialogViews();
    void refreshPresetList();
    void updateButtonStates();
    void showSaveDialog();
    void hideSaveDialog();
    // ... etc
};

} // namespace Krate::Plugins
```

## 8. SearchDebouncer and PresetBrowserLogic

Moved as-is with namespace change only. No API changes.

```cpp
// plugins/shared/src/ui/search_debouncer.h
namespace Krate::Plugins { /* exact same class */ }

// plugins/shared/src/ui/preset_browser_logic.h
namespace Krate::Plugins { /* exact same classes and functions */ }
```
