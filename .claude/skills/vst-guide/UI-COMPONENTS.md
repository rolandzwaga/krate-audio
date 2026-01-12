# VSTGUI Components

## UIViewSwitchContainer and template-switch-control

### Automatic View Switching

UIViewSwitchContainer can automatically switch views based on a control's value using the `template-switch-control` attribute:

```xml
<view class="UIViewSwitchContainer"
      template-names="Panel1,Panel2,Panel3"
      template-switch-control="Mode"/>
```

This creates a `UIDescriptionViewSwitchController` that:
1. Listens to the control's valueChanged events
2. Converts normalized value to template index
3. Calls `setCurrentViewIndex()` automatically

### Index Calculation

The controller calculates the view index from the control's normalized value:

```cpp
// Simplified from UIDescriptionViewSwitchController
int32_t index = static_cast<int32_t>(normalizedValue * templateCount);
index = std::min(index, templateCount - 1);
```

### Requirements for Automatic Binding

1. The control must have a `control-tag` matching the parameter name
2. The parameter must be properly registered in the controller
3. For discrete parameters, use `StringListParameter` for correct value scaling

---

## COptionMenu

### Menu Population

VSTGUI's VST3Editor automatically populates COptionMenu items from the parameter's `stepCount` and calls `getParamStringByValue()` for each index.

### Value Range

COptionMenu stores values as integers (0, 1, 2, ..., nbEntries-1). When bound to a parameter:
- `getValue()` returns the integer index
- `getValueNormalized()` returns index / (nbEntries - 1)

### XML Example

```xml
<view class="COptionMenu"
      control-tag="FilterType"
      menu-popup-style="true"
      menu-check-style="true" />
```

Items are added programmatically via the controller using `StringListParameter`.

---

## CViewContainer Visibility (setVisible Works)

### The Misconception

Some older forum posts (VSTGUI 3.6 era) mention problems with `setVisible()` on CViewContainer. **This is outdated for VSTGUI 4.x.**

### The Reality

In modern VSTGUI (4.x), `setVisible(false)` on CViewContainer correctly hides the container and all children.

### Source Code Evidence

**Drawing is skipped for invisible children** (`cviewcontainer.cpp:849-851`):
```cpp
for (const auto& pV : pImpl->children)
{
    if (pV->isVisible ())  // Parent checks visibility before drawing
    {
        // draw the child...
```

**Invalidation respects visibility** (`cviewcontainer.cpp:737-740`):
```cpp
void CViewContainer::invalid ()
{
    if (!isVisible ())
        return;  // Won't invalidate if not visible
```

**Mouse events are blocked for invisible views** (`cviewcontainer.cpp:962`):
```cpp
if (pV && pV->isVisible () && pV->getMouseEnabled () && pV->hitTest(...))
```

**setVisible() invalidates before hiding** (`cview.cpp:1041-1056`):
```cpp
void CView::setVisible (bool state)
{
    if (hasViewFlag (kVisible) != state)
    {
        if (state)
        {
            setViewFlag (kVisible, true);
            invalid ();
        }
        else
        {
            invalid ();  // Invalidates BEFORE setting invisible
            setViewFlag (kVisible, false);
        }
    }
}
```

### Using setVisible() with CViewContainer

You can use the `IDependent` / `VisibilityController` pattern with CViewContainer:

```cpp
// In didOpen(), find the container and create a visibility controller
if (auto* containerView = findViewByName(frame, "MyContainer")) {
    if (auto* param = getParameterObject(kControllingParamId)) {
        myContainerVisibilityController_ = new VisibilityController(
            this, param, containerView);
    }
}
```

The container and all its children will be hidden/shown together.

### Key Points

1. **setVisible(false) on CViewContainer** hides the entire container and all children
2. **Mouse events** are blocked for invisible views and their children
3. **Invalidation** is handled correctly (triggers redraw of parent area)
4. **Old workarounds** (moving off-screen, add/remove) are unnecessary in VSTGUI 4.x

---

## CDataBrowser

`CDataBrowser` is fully cross-platform but has some considerations:

```cpp
// Row height may render differently due to font metrics
CCoord dbGetRowHeight(CDataBrowser* browser) override {
    return 24;  // Use fixed heights, not font-derived
}

// Scrollbar width varies by platform
CDataBrowser(size, delegate, style,
    16,  // Explicit scrollbar width for consistency
    nullptr);

// Selection colors should be explicit, not system-derived
if (flags & kRowSelected) {
    context->setFillColor(CColor(100, 150, 255, 150));  // Explicit color
}
```

---

## CNewFileSelector

`CNewFileSelector` wraps native dialogs but behavior differs slightly:

| Behavior | Windows | macOS | Linux |
|----------|---------|-------|-------|
| Dialog appearance | Native Windows | Native Cocoa | GTK (if available) |
| Multiple extensions | All shown | All shown | May show separately |
| Initial directory | Respected | Respected | May be ignored |
| Default filename | Respected | Respected | Respected |

**Recommendation:** Always set `setInitialDirectory()` and `setDefaultSaveName()` but don't rely on exact behavior. Test on all platforms.

### Example Usage

```cpp
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
```

---

## Sources

- [CViewContainer source](https://github.com/steinbergmedia/vstgui/blob/master/vstgui/lib/cviewcontainer.cpp)
- [CView::setVisible source](https://github.com/steinbergmedia/vstgui/blob/master/vstgui/lib/cview.cpp)
- [VSTGUI View System Overview](https://github.com/steinbergmedia/vstgui/wiki/View-system-overview)
