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

## Key Takeaways

1. **Use the right parameter type** - `StringListParameter` for dropdowns, `RangeParameter` for ranges
2. **Use SDK functions** - Don't reinvent `toPlain()` with custom formulas
3. **Trust automatic bindings** - `template-switch-control`, menu population work correctly
4. **Thread safety is critical** - Never manipulate UI from non-UI threads
5. **Read the source** - When stuck, read VSTGUI/VST3 SDK source code
6. **Create helpers** - Prevent future mistakes with helper functions that enforce correct usage
7. **Check parameter units** - Verify normalized values are converted to the units DSP APIs expect
8. **Proxy parameters need hidden controls** - UIViewSwitchContainer uses IControlListener on CControls, not IDependent on Parameters
