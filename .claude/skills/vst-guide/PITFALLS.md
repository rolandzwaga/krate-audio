# Common Pitfalls and Incident Log

## Common Pitfalls

### Pitfall 1: Using Basic Parameter for Discrete Values

**Wrong:**
```cpp
parameters.addParameter(STR16("Mode"), nullptr, 10, 0.0, kCanAutomate, kModeId);
```
This creates a basic `Parameter` where `toPlain()` returns the normalized value unchanged.

**Right:**
```cpp
auto* modeParam = new StringListParameter(STR16("Mode"), kModeId, ...);
```

Or use the helper:
```cpp
parameters.addParameter(createDropdownParameter(
    STR16("Mode"), kModeId,
    STR16("Option1"), STR16("Option2"), STR16("Option3")
));
```

### Pitfall 2: Custom Normalization Formulas

**Wrong:**
```cpp
int modeIndex = static_cast<int>(valueNormalized * 10.0 + 0.5);
```
This reinvents the wheel and may have edge case bugs.

**Right:**
```cpp
auto* param = getParameterObject(kModeId);
int modeIndex = static_cast<int>(param->toPlain(valueNormalized));
```

### Pitfall 3: Manual View Switching

**Wrong:**
```cpp
// In setParamNormalized override
viewSwitchContainer_->setCurrentViewIndex(modeIndex);
```
Duplicates VSTGUI's built-in functionality and can cause race conditions.

**Right:**
Use `template-switch-control` attribute in editor.uidesc and let VSTGUI handle it.

### Pitfall 4: Overriding getParamStringByValue for StringListParameter

**Wrong:**
```cpp
case kModeId: {
    const char* modeNames[] = {"Granular", "Spectral", ...};
    // Manual string lookup
}
```
Duplicates what StringListParameter already does.

**Right:**
Don't override - let the base class delegate to StringListParameter's toString().

### Pitfall 5: UI Manipulation from setParamNormalized

**Wrong:**
```cpp
tresult Controller::setParamNormalized(ParamID id, ParamValue value) {
    if (id == kTimeModeId) {
        delayTimeControl_->setVisible(value < 0.5f);  // Thread unsafe!
    }
}
```

**Right:**
Use `IDependent` with `VisibilityController`. See [THREAD-SAFETY.md](THREAD-SAFETY.md).

### Pitfall 6: Unregistering from Child Views in Destructor

**Wrong:**
```cpp
~MyContainer() {
    textField_->unregisterTextEditListener(this);  // USE-AFTER-FREE!
}
```
Child views are destroyed by `CViewContainer::removeAll()` before destructor runs.

**Right:**
Don't access child views in destructor - they're already gone.

### Pitfall 7: Parameter Unit Mismatch Between Processor and DSP

**Wrong:**
```cpp
// Processor stores normalized 0-1 value
shimmerParams_.shimmerMix.store(normalizedValue, std::memory_order_relaxed);

// Later in process(), pass directly to DSP component
shimmerDelay_.setShimmerMix(shimmerParams_.shimmerMix.load(std::memory_order_relaxed));
// BUG: DSP expects 0-100 percent, but we're passing 0-1!
```

**Symptom:** Parameter appears to have no effect because the value is ~100x smaller than expected.

**Right:**
```cpp
// Convert to the units the DSP API expects
shimmerDelay_.setShimmerMix(shimmerParams_.shimmerMix.load(std::memory_order_relaxed) * 100.0f);
```

**Prevention checklist:**
1. Document expected units in DSP setter comments (e.g., "percent 0-100", "dB", "Hz")
2. Check if similar parameters in the same mode use conversion - follow the pattern
3. When a parameter "doesn't work", first check if it's a unit mismatch

---

## Debugging Checklist

When VSTGUI/VST3 features don't work:

1. [ ] Add logging to trace actual values at each step
2. [ ] Check which parameter type you're using (`Parameter` vs `StringListParameter` vs `RangeParameter`)
3. [ ] Verify `toPlain()` returns expected values
4. [ ] Read the VSTGUI source in `extern/vst3sdk/vstgui4/vstgui/`
5. [ ] Read the VST3 SDK source in `extern/vst3sdk/public.sdk/source/vst/`
6. [ ] Check if automatic bindings are configured correctly in editor.uidesc
7. [ ] Verify control-tag names match parameter registration

---

## Incident Log

### 2025-12-28: Mode Switching Bug

**Symptom:** Mode dropdown (COptionMenu) modes 5-10 all switched to Spectral (index 1).

**Root Cause:** Mode parameter was created with `parameters.addParameter()` which creates a basic `Parameter`. The `toPlain()` method of basic `Parameter` just returns the normalized value unchanged, so `toPlain(0.5)` returned `0.5` instead of `5.0`.

**Solution:** Changed to `StringListParameter` which properly implements `toPlain()` using `FromNormalized()` to convert 0.0-1.0 to integer indices 0-10.

**Time Wasted:** Multiple hours due to pivoting between approaches without deeply investigating the actual value flow.

**Lesson:** Always check which parameter type you're using and verify `toPlain()` returns the expected value.

---

### 2025-12-28: Universal Dropdown Fix

**Symptom:** After fixing Mode dropdown, discovered 17+ other dropdown parameters across 9 files had the same issue.

**Files Affected:**
- `bbd_params.h` - Era (4 options)
- `spectral_params.h` - FFT Size (4), Spread Direction (3)
- `digital_params.h` - Time Mode (3), Note Value (11), Limiter Character (4), Era (4), Mod Waveform (4)
- `granular_params.h` - Envelope Type (4)
- `pingpong_params.h` - Time Mode (3), Note Value (11), L/R Ratio (5)
- `reverse_params.h` - Playback Mode (3), Filter Type (3)
- `freeze_params.h` - Filter Type (3)
- `multitap_params.h` - Timing Pattern (20), Spatial Pattern (7)
- `ducking_params.h` - Duck Target (3)

**Solution:** Created `src/controller/parameter_helpers.h` with `createDropdownParameter()` and `createDropdownParameterWithDefault()` helper functions. Updated all 9 parameter files to use these helpers.

**Prevention:** The helper pattern makes it impossible to accidentally create dropdown parameters with the wrong type.

---

### 2025-12-29: Conditional Visibility Thread Safety Crash

**Symptom:** Plugin hangs and crashes when switching between Digital and PingPong modes while changing time mode between "Free" and "Synced". Delay time controls would initially show/hide correctly, then stop responding, then crash the host.

**Root Cause:** Called `setVisible()` on VSTGUI controls directly from `setParamNormalized()`, which can be called from ANY thread. VSTGUI controls MUST only be manipulated on the UI thread.

**Solution:** Implemented `VisibilityController` class using VST3's `IDependent` mechanism with `UpdateHandler`:
1. Register as dependent on the controlling parameter via `addDependent()`
2. When parameter changes (any thread), it calls `deferUpdate()`
3. `UpdateHandler` schedules `update()` callback to run on UI thread at 30Hz
4. `update()` method safely calls `setVisible()` on UI thread

**Time Wasted:** Multiple hours initially trying to add mode filtering and other workarounds without understanding the fundamental threading violation.

**Lesson:** NEVER manipulate VSTGUI controls from `setParamNormalized()` or any method that can be called from non-UI threads. ALWAYS use `IDependent` with deferred updates.

**Sources:**
- [VSTGUI VST3Editor sets kDirtyCallAlwaysOnMainThread](https://github.com/steinbergmedia/vstgui/blob/develop/vstgui/plugin-bindings/vst3editor.cpp)
- [VST3 setParamNormalized must be called on UI thread discussion](https://forums.steinberg.net/t/vst3-hosting-when-to-use-ieditcontroller-setparamnormalized/787800)
- [JUCE added thread safety for setParamNormalized](https://github.com/juce-framework/JUCE/commit/9f03bbc358d67a3e0d0e3d7082259a4155aebd85)

---

### 2026-01-15: Shimmer Pitch Shift Has No Effect

**Symptom:** In Shimmer delay mode, changing pitch shift parameters (semitones, cents, shimmer mix) appeared to have no audible effect on the output.

**Root Cause:** Parameter unit mismatch in `processor.cpp:534`. The shimmer mix parameter was stored as normalized 0-1 in `shimmerParams_.shimmerMix`, but `ShimmerDelay::setShimmerMix()` expects 0-100 percent. The value was passed directly without conversion, so when user set shimmer mix to 50%, it actually became 0.5%, making the pitched feedback almost completely blended out.

**The Bug:**
```cpp
// Line 534 - WRONG
shimmerDelay_.setShimmerMix(shimmerParams_.shimmerMix.load(std::memory_order_relaxed));

// Line 540 - CORRECT (same file, different parameter)
shimmerDelay_.setDryWetMix(shimmerParams_.dryWet.load(std::memory_order_relaxed) * 100.0f);
```

**Solution:** Add `* 100.0f` conversion to match the pattern used for `setDryWetMix()` on line 540.

**Lesson:** When a parameter appears to have no effect, check if there's a unit mismatch between how the processor stores it (often normalized 0-1) and what the DSP API expects (often percent 0-100, Hz, dB, etc.). Look at similar parameters in the same code block for the correct pattern.

---

### 2026-01-29: UIViewSwitchContainer Not Responding to Proxy Parameter

**Symptom:** UIViewSwitchContainer with `template-switch-control="Band1DisplayedType"` would not switch templates when the proxy parameter was updated via `performEdit()`. Direct binding to the actual parameter (e.g., `B1N1Type`) worked fine.

**Investigation:**
1. First attempt: Changed from sub-view syntax to `template-names` attribute - didn't fix it
2. Diagnostic test: Changed `template-switch-control` to bind directly to `B1N1Type` - THIS WORKED
3. Research: Read UIViewSwitchContainer source code in `extern/vst3sdk/vstgui4/vstgui/uidescription/uiviewswitchcontainer.cpp`

**Root Cause:** UIViewSwitchContainer uses **`IControlListener`** to monitor parameter changes, NOT **`IDependent`**.

The flow for direct binding:
- Host updates parameter → `ParameterChangeListener.update()` → `CControl.setValueNormalized()` → `IControlListener.valueChanged()` → template switches

The flow for proxy parameter (BROKEN):
- Controller calls `performEdit(proxyParamId)` → Parameter updates → BUT there's no CControl bound to this parameter → no `valueChanged()` → no switch

**Solution:** Add a hidden 1x1 pixel CControl bound to the proxy parameter:

```xml
<!-- Hidden proxy control creates the ParameterChangeListener bridge -->
<view class="CSlider" origin="0, 0" size="1, 1" transparent="true"
      control-tag="Band1DisplayedType" min-value="0" max-value="1"/>
```

**Additional Fix:** For bidirectional sync (dropdown changes selected node's type), modified `NodeSelectionController` to:
1. Listen to `DisplayedType` parameter as well as node type parameters
2. When `DisplayedType` changes → copy to selected node's type
3. Added re-entrancy guard (`isUpdating_` flag) to prevent feedback loops

**Time Wasted:** Multiple hours of trial-and-error before researching the actual VSTGUI source code.

**Key Files:**
- `extern/vst3sdk/vstgui4/vstgui/uidescription/uiviewswitchcontainer.cpp` (line 255: `registerControlListener`)
- `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.cpp` (line 100: `ParameterChangeListener`)

**Lesson:** UIViewSwitchContainer and similar VSTGUI components that respond to parameter changes do so via `IControlListener` on CControls, NOT via `IDependent` on Parameters. If there's no CControl bound to a parameter, VSTGUI won't see changes to it. Always read the source when automatic features don't work as expected.

---

### Pitfall 8: COptionMenu Bound to RangeParameter Instead of StringListParameter

**Symptom:** Dropdown defaults to first entry and snaps back to it on any selection. The displayed value is wrong (e.g., shows "-23 steps" instead of "0 steps"), and selecting any item immediately reverts to the first item ("-24 steps").

**Root Cause:** The parameter was registered with `parameters.addParameter(title, units, stepCount, default, flags, id)` which creates a `RangeParameter`. `COptionMenu` requires a `StringListParameter` (with `kIsList` flag) to populate its dropdown entries. Without string entries, the menu has 0 items and cannot function.

**This is NOT the same as Pitfall 1** (which is about `toPlain()` behavior). Even with correct `stepCount`, a `RangeParameter` has no string list for `COptionMenu` to display.

**Wrong:**
```cpp
// Creates a RangeParameter — COptionMenu can't populate entries from this
parameters.addParameter(title, STR16("steps"), 48,
    0.5, ParameterInfo::kCanAutomate, voiceIntervalIds[v]);
```

**Right:**
```cpp
// Creates a StringListParameter — COptionMenu auto-populates entries
auto* param = new StringListParameter(title, id, nullptr,
    ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
for (int step = -24; step <= 24; ++step) {
    param->appendString(STR16("...")); // one entry per discrete value
}
auto defaultNorm = param->toNormalized(24.0); // index 24 = "0 steps"
param->setNormalized(defaultNorm);
param->getInfo().defaultNormalizedValue = defaultNorm;
parameters.addParameter(param);
```

**Key rule:** ANY parameter bound to a `COptionMenu` in the uidesc MUST be a `StringListParameter`. If you see `class="COptionMenu"` in the uidesc referencing a control-tag, the parameter registered for that tag MUST use `createDropdownParameter()`, `createDropdownParameterWithDefault()`, or manually construct a `StringListParameter`. A `RangeParameter` with `stepCount > 0` is NOT sufficient — it has the right number of steps but no string entries for the menu.

**Note on defaultNormalizedValue:** `StringListParameter` constructor sets `defaultNormalizedValue = 0.0`. Calling `setNormalized()` only changes the current value, NOT the default in `ParameterInfo`. You must also set `param->getInfo().defaultNormalizedValue` explicitly if the default is not index 0.

---

### Pitfall 9: Cached View Pointers + UIViewSwitchContainer = Dangling Pointers

**Problem:** UIViewSwitchContainer destroys and recreates template view hierarchies on every switch. If the controller caches raw pointers to views inside those templates, the pointers become dangling when the template is torn down.

**Wrong:**
```cpp
// In verifyView() — stores pointer to a view inside a UIViewSwitchContainer template
auto* indicator = dynamic_cast<ModRingIndicator*>(view);
if (indicator) {
    ringIndicators_[destIdx] = indicator;  // Dangling after template switch!
}
```

After switching templates, `ringIndicators_[destIdx]` points to freed memory. Any access (parameter sync, rebuild, etc.) crashes.

**Right:** Wire a removal callback so the pointer is nulled when the view is destroyed:

```cpp
// In the custom view class:
bool removed(CView* parent) override {
    if (removedCallback_) removedCallback_();
    return CView::removed(parent);
}

// In verifyView() / wireMyView():
indicator->setRemovedCallback([this, destIdx]() {
    ringIndicators_[destIdx] = nullptr;
});
ringIndicators_[destIdx] = indicator;
```

When `UIViewSwitchContainer` tears down the old template, `CView::removed()` fires, which nulls the pointer. When the template is recreated on switch-back, `verifyView()` re-wires it.

**General rule:** Any raw pointer cached in the controller to a view that lives inside a `UIViewSwitchContainer` template MUST have a removal callback to null it out. This applies to all custom views (ModRingIndicator, ModMatrixGrid, etc.), not just to views with complex lifecycle. The `verifyView()` → cache pattern is safe ONLY for views that live for the entire editor lifetime.

---

### 2026-02-20: Settings Drawer Toggle Does Nothing

**Symptom:** Clicking the gear icon in the Ruinae "Voices & Output" section did nothing — the settings drawer didn't open.

**Root Cause:** When the Harmonizer feature was added (commit 161cfa4), the listener registration range in `verifyView()` was extended from `kActionFxExpandPhaserTag` (10018) to `kActionFxExpandHarmonizerTag` (10022). This inadvertently included `kActionSettingsToggleTag` (10020) and `kActionSettingsOverlayTag` (10021) in the range.

These tags were ALSO registered explicitly later in `verifyView()`:
```cpp
if (tag == kActionSettingsToggleTag) {
    gearButton_ = ctrl;
    ctrl->registerControlListener(this);  // Second registration!
}
```

VSTGUI's `DispatchList::add()` does NOT check for duplicates — it simply appends to a vector. When clicked, `valueChanged()` was called twice:
1. First call: `settingsDrawerTargetOpen_` toggles false→true, animation timer starts
2. Second call: `settingsDrawerTargetOpen_` toggles true→false, timer already running so returns early

The drawer animation immediately reversed because the target was toggled twice.

**Solution:** Exclude the settings tags from the range check:
```cpp
if (tag >= kActionTransformInvertTag && tag <= kActionFxExpandHarmonizerTag &&
    tag != kActionSettingsToggleTag &&
    tag != kActionSettingsOverlayTag) {
    control->registerControlListener(this);
}
```

**Lesson:** When extending tag ranges for listener registration, audit ALL tags in the new range to ensure none are already registered explicitly. Adding comments documenting which tag values a range covers helps prevent this.

---

### Pitfall 10: Duplicate IControlListener Registration via Overlapping Tag Ranges

**Problem:** When using a range-based registration pattern for action buttons, extending the range to include new tags can inadvertently capture tags that are also registered explicitly elsewhere. VSTGUI's `DispatchList::add()` does NOT check for duplicates — calling `registerControlListener()` twice with the same listener adds it twice, causing `valueChanged()` to be called twice per click.

**Symptom:** Toggle buttons appear to do nothing. The action fires twice, toggling state back to the original value (e.g., open→close drawer instantly).

**Wrong:**
```cpp
// Range-based registration for action buttons
if (tag >= kActionTransformInvertTag && tag <= kActionFxExpandHarmonizerTag) {
    control->registerControlListener(this);  // Registers 10006-10022
}

// Later: explicit registration for settings drawer
if (tag == kActionSettingsToggleTag) {  // tag 10020 — ALREADY IN RANGE ABOVE!
    gearButton_ = ctrl;
    ctrl->registerControlListener(this);  // DUPLICATE! valueChanged called twice
}
```

When a new tag (`kActionFxExpandHarmonizerTag = 10022`) was added and the range extended, it inadvertently included `kActionSettingsToggleTag = 10020` which was also registered explicitly below.

**Right:**
```cpp
// Explicitly exclude tags that have their own registration
if (tag >= kActionTransformInvertTag && tag <= kActionFxExpandHarmonizerTag &&
    tag != kActionSettingsToggleTag &&
    tag != kActionSettingsOverlayTag) {
    control->registerControlListener(this);
}
```

**Prevention checklist:**
1. When extending tag ranges, audit ALL tags in the new range
2. Document which tags each range-based registration covers
3. Consider using explicit tag lists instead of ranges for clarity:
   ```cpp
   static constexpr std::array kActionButtonTags = {
       kActionTransformInvertTag, kActionFxExpandDelayTag, ...
   };
   if (std::find(kActionButtonTags.begin(), kActionButtonTags.end(), tag) != kActionButtonTags.end()) {
       control->registerControlListener(this);
   }
   ```
4. Add logging to `valueChanged()` during development to catch unexpected double-calls

**Key insight:** VSTGUI's listener pattern DOES NOT deduplicate. Calling `registerControlListener(listener)` twice adds the listener twice. This is intentional (some use cases need it), but it's a footgun when tag ranges overlap.

---

## Key Takeaways

1. **Use the right parameter type** - `StringListParameter` for dropdowns, `RangeParameter` for ranges
2. **Use SDK functions** - Don't reinvent `toPlain()` with custom formulas
3. **Trust automatic bindings** - `template-switch-control`, menu population work correctly
4. **Thread safety is critical** - Never manipulate UI from non-UI threads
5. **Read the source** - When stuck, read VSTGUI/VST3 SDK source code
6. **Create helpers** - Prevent future mistakes with helper functions that enforce correct usage
7. **Check parameter units** - Verify normalized values are converted to the units DSP APIs expect
8. **COptionMenu needs StringListParameter** - A `RangeParameter` with correct `stepCount` is NOT enough; `COptionMenu` requires `StringListParameter` with `kIsList` to populate entries
9. **Proxy parameters need hidden controls** - UIViewSwitchContainer uses IControlListener on CControls, not IDependent on Parameters
10. **Null cached view pointers on removal** - Views inside UIViewSwitchContainer templates get destroyed on every switch; wire `removed()` callbacks to null cached pointers
11. **Tag ranges can overlap** - When extending range-based listener registration, verify the new range doesn't include tags already registered explicitly elsewhere; `registerControlListener()` does NOT deduplicate
