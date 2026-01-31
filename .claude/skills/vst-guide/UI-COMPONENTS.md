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

### CRITICAL: IControlListener vs IDependent

**UIViewSwitchContainer uses `IControlListener`, NOT `IDependent`.**

This is a crucial distinction:
- **IDependent** (VST3 parameter system): Notified when `Parameter.changed()` is called
- **IControlListener** (VSTGUI): Notified when a `CControl.setValue()` is called

UIViewSwitchContainer's `UIDescriptionViewSwitchController` registers as an `IControlListener` on the CControl specified by `template-switch-control`:

```cpp
// From uiviewswitchcontainer.cpp line 255
switchControl->registerControlListener(this);
```

When the control's value changes (via UI interaction OR via `ParameterChangeListener`), `valueChanged()` is called, which triggers the template switch.

### Proxy Parameters Require Hidden Controls

**Problem:** If you use a "proxy" parameter (like `Band1DisplayedType`) that has no visible UI control, UIViewSwitchContainer cannot listen to it because there's no CControl to register on.

**Solution:** Add a hidden 1x1 pixel CControl bound to the proxy parameter:

```xml
<!-- Hidden proxy control - required for UIViewSwitchContainer to detect changes -->
<view class="CSlider" origin="0, 0" size="1, 1" transparent="true"
      control-tag="Band1DisplayedType" min-value="0" max-value="1"/>

<!-- Now UIViewSwitchContainer can find and listen to this control -->
<view class="UIViewSwitchContainer" origin="0, 38" size="280, 185"
      animation-time="0" template-switch-control="Band1DisplayedType"
      template-names="Template1,Template2,Template3"/>
```

### Why This Works

The notification chain with a hidden control:
1. Controller calls `performEdit(proxyParamId, value)`
2. `ParameterChangeListener` receives update via `IDependent`
3. `ParameterChangeListener` updates the hidden `CSlider` via `setValueNormalized()`
4. `CSlider` notifies its `IControlListeners` via `valueChanged()`
5. `UIDescriptionViewSwitchController` receives `valueChanged()` and switches templates

Without the hidden control, step 3 has nothing to update, breaking the chain.

### Bidirectional Proxy Sync

When using a proxy parameter for both reading AND writing (e.g., a type dropdown that should edit the currently selected node), implement bidirectional sync in your controller:

```cpp
class NodeSelectionController : public Steinberg::FObject {
    // Watch both the proxy and the actual parameters
    displayedTypeParam_->addDependent(this);
    for (int n = 0; n < 4; ++n) {
        nodeTypeParams_[n]->addDependent(this);
    }

    void update(FUnknown* changedUnknown, int32 message) override {
        if (isUpdating_) return;  // Prevent feedback loop
        isUpdating_ = true;

        auto* changedParam = FCast<Parameter>(changedUnknown);
        if (changedParam == displayedTypeParam_) {
            // User changed dropdown → copy to selected node
            copyDisplayedTypeToSelectedNode();
        } else {
            // Node changed → copy to DisplayedType
            copySelectedNodeToDisplayedType();
        }

        isUpdating_ = false;
    }
};
```

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

## Reusable View Templates & Sub-Controllers

VSTGUI has first-class support for defining a view once and instantiating it multiple times with different parameter bindings. This is the standard pattern for repeated UI sections (e.g., identical band editors, channel strips, multi-tap controls).

### Defining a Reusable Template

Define the template once in `.uidesc` using generic control-tag names:

```xml
<template name="band_panel" class="CViewContainer" sub-controller="BandController"
          origin="0, 0" size="200, 400">
    <view class="CTextLabel"   title="Band"              origin="10, 5"  size="180, 20" />
    <view class="CSlider"      control-tag="Band::Gain"  origin="10, 30" size="180, 20" />
    <view class="CSlider"      control-tag="Band::Pan"   origin="10, 60" size="180, 20" />
    <view class="COnOffButton" control-tag="Band::Mute"  origin="10, 90" size="40, 20"  />
    <!-- All controls use generic "Band::XYZ" tags -->
</template>
```

### Instantiating Multiple Copies

Reference the template multiple times in the main editor. Each `<view template="..."/>` creates a fresh independent copy:

```xml
<template name="editor" class="CViewContainer" size="840, 400">
    <view template="band_panel" origin="0, 0"   />   <!-- Band 1 -->
    <view template="band_panel" origin="210, 0"  />   <!-- Band 2 -->
    <view template="band_panel" origin="420, 0"  />   <!-- Band 3 -->
    <view template="band_panel" origin="630, 0"  />   <!-- Band 4 -->
</template>
```

You can override attributes (like `origin`) at the instantiation site. From `uidescription.cpp`:

```cpp
// After creating the view from the template, attributes from the
// instantiation site are applied on top:
impl->viewFactory->applyAttributeValues(view, *node->getAttributes(), this);
```

### Sub-Controllers for Parameter Remapping

The `sub-controller` attribute on the template container is the key mechanism. When VSTGUI encounters it, it calls `IController::createSubController()` on the active controller to get a per-instance controller. That sub-controller's `getTagForName()` remaps generic tag names to band-specific parameter IDs.

**Sub-controller implementation:**

```cpp
class BandSubController : public VSTGUI::DelegationController
{
    int bandIndex_;

public:
    BandSubController(int bandIndex, IController* parent)
        : DelegationController(parent), bandIndex_(bandIndex) {}

    // Called for every control-tag in the template during view creation.
    // Return the actual parameter tag ID for this specific band instance.
    int32_t getTagForName(UTF8StringPtr name, int32_t registeredTag) const override
    {
        auto view = UTF8StringView(name);
        if (view == "Band::Gain")  return kBand1GainId  + bandIndex_;
        if (view == "Band::Pan")   return kBand1PanId   + bandIndex_;
        if (view == "Band::Mute")  return kBand1MuteId  + bandIndex_;
        // Fall back to parent for unrecognized tags
        return DelegationController::getTagForName(name, registeredTag);
    }

    // Called after each child view is created. Use for per-instance customization.
    CView* verifyView(CView* view, const UIAttributes& attrs,
                      const IUIDescription* desc) override
    {
        if (auto* label = dynamic_cast<CTextLabel*>(view))
        {
            if (label->getText() == "Band")
                label->setText(("Band " + std::to_string(bandIndex_ + 1)).c_str());
        }
        return DelegationController::verifyView(view, attrs, desc);
    }
};
```

**Factory in the main controller:**

```cpp
IController* Controller::createSubController(
    UTF8StringPtr name,
    const IUIDescription* description,
    VST3Editor* editor) override
{
    if (UTF8StringView(name) == "BandController")
    {
        return new BandSubController(bandInstanceCounter_++, this);
    }
    return nullptr;
}
```

**IMPORTANT:** `createSubController()` is called once per template instantiation, in document order. The counter-based approach works because VSTGUI creates views top-to-bottom as they appear in the XML. Reset the counter in `willClose()` if the editor can be reopened.

### How Tag Remapping Works

When VSTGUI creates a view from a template and encounters `control-tag="Band::Gain"`:

1. VSTGUI calls `getTagForName("Band::Gain", registeredTag)` on the **active sub-controller**
2. The sub-controller returns the band-specific tag (e.g., `kBand2GainId` for band index 1)
3. The view is bound to that specific parameter

| Template Tag | Band 0 | Band 1 | Band 2 | Band 3 |
|---|---|---|---|---|
| `Band::Gain` | `kBand1GainId` | `kBand2GainId` | `kBand3GainId` | `kBand4GainId` |
| `Band::Pan` | `kBand1PanId` | `kBand2PanId` | `kBand3PanId` | `kBand4PanId` |
| `Band::Mute` | `kBand1MuteId` | `kBand2MuteId` | `kBand3MuteId` | `kBand4MuteId` |

### Nested Templates

Templates can reference other templates for further composition:

```xml
<template name="gain_section" class="CViewContainer" size="180, 50">
    <view class="CSlider" control-tag="Band::Gain" ... />
    <view class="CParamDisplay" control-tag="Band::Gain" ... />
</template>

<template name="band_panel" class="CViewContainer" sub-controller="BandController" ...>
    <view template="gain_section" origin="0, 0" />
    <view class="CSlider" control-tag="Band::Pan" origin="0, 60" ... />
</template>
```

The sub-controller's `getTagForName()` applies to all nested templates within its scope.

### Programmatic Template Instantiation

You can also create template instances from C++ code rather than XML:

```cpp
// UIDescription provides these template APIs:
CView* createView(UTF8StringPtr name, IController* controller);
void collectTemplateViewNames(std::list<const std::string*>& names) const;
bool duplicateTemplate(UTF8StringPtr name, UTF8StringPtr duplicateName);
bool addNewTemplate(UTF8StringPtr name, const SharedPointer<UIAttributes>& attr);
```

This enables dynamic instantiation (e.g., creating N band panels based on a runtime configuration):

```cpp
auto* description = editor->getUIDescription();
for (int i = 0; i < numBands; ++i)
{
    CView* bandView = description->createView("band_panel", controller);
    if (bandView)
    {
        CRect r = bandView->getViewSize();
        r.offset(i * bandWidth, 0);
        bandView->setViewSize(r);
        mainContainer->addView(bandView);
    }
}
```

### DelegationController Base Class

VSTGUI provides `DelegationController` which forwards all `IController` methods to a parent. Use it as the base class for sub-controllers so you only override what you need:

```cpp
class DelegationController : public IController
{
public:
    explicit DelegationController(IController* controller);

    // All methods delegate to the wrapped controller by default:
    void valueChanged(CControl*) override;
    int32_t getTagForName(UTF8StringPtr, int32_t) const override;
    IControlListener* getControlListener(UTF8StringPtr) override;
    CView* createView(const UIAttributes&, const IUIDescription*) override;
    CView* verifyView(CView*, const UIAttributes&, const IUIDescription*) override;
    IController* createSubController(UTF8StringPtr, const IUIDescription*) override;
};
```

### Key Source Files

| File | Purpose |
|------|---------|
| `vstgui4/vstgui/uidescription/uidescription.h` | `UIDescription` class with template creation API |
| `vstgui4/vstgui/uidescription/icontroller.h` | `IController` interface (`getTagForName`, `createSubController`) |
| `vstgui4/vstgui/uidescription/delegationcontroller.h` | `DelegationController` base class |
| `vstgui4/vstgui/plugin-bindings/vst3editor.h` | `VST3Editor` with `createSubController` override point |

### Combinatorial Explosion Prevention

For a plugin like Disrumpo with 4 identical bands × 22 distortion types, templates + sub-controllers reduce the XML from O(bands × types) to O(1):

- **Without templates:** 4 × 22 = 88 near-identical XML blocks
- **With templates:** 1 band template + 1 sub-controller class

The sub-controller maps generic tags to band-specific parameters. The distortion type switching is handled separately via `UIViewSwitchContainer` (see above) within the template itself — also defined once and remapped per band.

---

## Sources

- [CViewContainer source](https://github.com/steinbergmedia/vstgui/blob/master/vstgui/lib/cviewcontainer.cpp)
- [CView::setVisible source](https://github.com/steinbergmedia/vstgui/blob/master/vstgui/lib/cview.cpp)
- [VSTGUI View System Overview](https://github.com/steinbergmedia/vstgui/wiki/View-system-overview)
- [UIDescription source](https://github.com/steinbergmedia/vstgui/blob/master/vstgui/uidescription/uidescription.h)
- [IController interface](https://github.com/steinbergmedia/vstgui/blob/master/vstgui/uidescription/icontroller.h)
- [DelegationController](https://github.com/steinbergmedia/vstgui/blob/master/vstgui/uidescription/delegationcontroller.h)
