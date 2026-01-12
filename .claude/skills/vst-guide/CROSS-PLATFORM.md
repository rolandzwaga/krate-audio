# Cross-Platform Custom Views

When creating custom VSTGUI views (preset browsers, visualizers, custom controls), follow these rules to ensure cross-platform compatibility.

## Core Principle

**VSTGUI is inherently cross-platform.** All custom views MUST use only VSTGUI abstractions. Platform-specific code (Win32, Cocoa, GTK) is FORBIDDEN in UI code.

---

## Cross-Platform Component Reference

| Component | Purpose | Cross-Platform |
|-----------|---------|----------------|
| `CView` / `CControl` | Base classes for custom views | Yes |
| `CDataBrowser` | List/table views | Yes |
| `CNewFileSelector` | File open/save dialogs | Yes |
| `COptionMenu` | Dropdown menus | Yes |
| `CScrollView` | Scrollable containers | Yes |
| `CDrawContext` | All drawing operations | Yes |

---

## Rule 1: Drawing - VSTGUI Primitives Only

```cpp
// CORRECT - Use CDrawContext methods
void MyView::draw(CDrawContext* context) {
    context->setFillColor(CColor(40, 40, 40, 255));
    context->drawRect(getViewSize(), kDrawFilled);
    context->setFont(kNormalFont);
    context->drawString("Preset Name", textRect, kLeftText);
}

// FORBIDDEN - Platform-specific drawing
#ifdef _WIN32
    HDC hdc = GetDC(hwnd);  // NO!
#endif
#ifdef __APPLE__
    [[NSGraphicsContext currentContext] ...];  // NO!
#endif
```

---

## Rule 2: File Paths - Use std::filesystem

```cpp
#include <filesystem>
namespace fs = std::filesystem;

// CORRECT - Platform-agnostic path handling
fs::path getPresetDirectory() {
    #if defined(_WIN32)
        const char* docs = std::getenv("USERPROFILE");
        return fs::path(docs) / "Documents" / "VST3 Presets" / "Iterum";
    #elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        return fs::path(home) / "Library" / "Audio" / "Presets" / "Iterum";
    #else  // Linux
        const char* home = std::getenv("HOME");
        return fs::path(home) / ".vst3" / "presets" / "Iterum";
    #endif
}

// Path operations are cross-platform
fs::path presetPath = getPresetDirectory() / "Digital" / "My Preset.vstpreset";
bool exists = fs::exists(presetPath);
fs::create_directories(presetPath.parent_path());

// FORBIDDEN - Platform-specific path APIs in UI code
#ifdef _WIN32
    SHGetKnownFolderPath(FOLDERID_Documents, ...);  // Isolate in platform/ folder
#endif
```

---

## Rule 3: File Dialogs - CNewFileSelector Only

```cpp
// CORRECT - Cross-platform file dialog
void PresetBrowserView::onImportClicked() {
    auto selector = CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectFile);
    selector->setTitle("Import Preset");
    selector->addFileExtension(CFileExtension("VST3 Preset", "vstpreset"));

    selector->run([this](CNewFileSelector* sel) {
        if (sel->getNumSelectedFiles() > 0) {
            UTF8StringPtr path = sel->getSelectedFile(0);
            presetManager_->importPreset(path);
        }
    });
    selector->forget();
}

// FORBIDDEN - Platform-specific dialogs
#ifdef _WIN32
    GetOpenFileName(&ofn);  // NO!
#endif
#ifdef __APPLE__
    NSSavePanel* panel = ...;  // NO!
#endif
```

---

## Rule 4: Fonts - Use VSTGUI Font System

```cpp
// CORRECT - VSTGUI font constants
context->setFont(kNormalFont);
context->setFont(kNormalFontBold);
context->setFont(kSystemFont);

// CORRECT - Generic family names (fallback gracefully)
auto font = makeOwned<CFontDesc>("Arial", 12);

// CAUTION - Platform-specific fonts need fallbacks
#if defined(_WIN32)
    auto font = makeOwned<CFontDesc>("Segoe UI", 12);
#elif defined(__APPLE__)
    auto font = makeOwned<CFontDesc>("SF Pro", 12);
#else
    auto font = makeOwned<CFontDesc>("DejaVu Sans", 12);
#endif
// Better: Just use kNormalFont or generic "Arial"
```

---

## Rule 5: Mouse/Keyboard - Use VSTGUI Events

```cpp
// CORRECT - VSTGUI event handling
CMouseEventResult MyView::onMouseDown(CPoint& where, const CButtonState& buttons) {
    if (buttons.isLeftButton() && buttons.isDoubleClick()) {
        loadSelectedPreset();
    }
    return kMouseEventHandled;
}

void MyView::onKeyboardEvent(KeyboardEvent& event) {
    if (event.character == 'f' && event.modifiers.has(ModifierKey::Control)) {
        openSearchField();
        event.consumed = true;
    }
}

// FORBIDDEN - Platform-specific input
#ifdef _WIN32
    if (GetAsyncKeyState(VK_LBUTTON)) { }  // NO!
#endif
```

---

## Rule 6: String Handling - UTF-8 Throughout

```cpp
// CORRECT - UTF-8 strings
std::string presetName = "Cafe Delay";  // UTF-8 encoded
context->drawString(presetName.c_str(), rect, kLeftText);

// For VST3 API calls requiring UTF-16:
Steinberg::String str;
str.fromUTF8(presetName.c_str());

// AVOID - Platform-specific string types
#ifdef _WIN32
    std::wstring wstr = L"...";  // Windows-specific wide strings
#endif
```

---

## Isolating Platform-Specific Code

When platform differences are unavoidable (e.g., preset path discovery), isolate them:

```
src/
├── platform/
│   ├── preset_paths.h           // Interface
│   ├── preset_paths_win.cpp     // Windows implementation
│   ├── preset_paths_mac.cpp     // macOS implementation
│   └── preset_paths_linux.cpp   // Linux implementation
└── ui/
    └── preset_browser.cpp       // Uses Platform:: interface only
```

```cpp
// src/platform/preset_paths.h
namespace Platform {
    std::filesystem::path getUserPresetDirectory();
    std::filesystem::path getFactoryPresetDirectory();
}

// UI code uses only the interface
#include "platform/preset_paths.h"
auto presets = scanDirectory(Platform::getUserPresetDirectory());
```

---

## Cross-Platform Testing Checklist

Before merging any custom view code:

- [ ] Zero platform-specific includes in UI files (`windows.h`, `Cocoa/Cocoa.h`, etc.)
- [ ] All drawing uses `CDrawContext` methods only
- [ ] All file dialogs use `CNewFileSelector`
- [ ] All paths use `std::filesystem::path`
- [ ] Fonts use VSTGUI constants or generic family names
- [ ] Builds successfully on Windows, macOS, and Linux
- [ ] Tested visually on at least 2 platforms
- [ ] pluginval passes on all platforms

---

## Common Cross-Platform Pitfalls

| Issue | Platform | Solution |
|-------|----------|----------|
| Path separators | Windows uses `\` | Use `std::filesystem::path` (handles automatically) |
| Case sensitivity | Linux is case-sensitive | Consistent lowercase for preset folders |
| Hidden files | Linux/macOS `.` prefix | Don't start names with `.` |
| Line endings | Windows CRLF | Use binary mode for preset files |
| Font metrics | All differ slightly | Test text layout on all platforms |
| HiDPI scaling | All handle differently | Use VSTGUI scaling, test at 100%/200% |
| File permissions | Linux strict | Check `fs::permissions()` before write |
| Symbolic links | macOS/Linux common | Use `fs::canonical()` to resolve |
