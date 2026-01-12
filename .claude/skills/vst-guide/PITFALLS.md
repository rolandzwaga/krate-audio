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

## Key Takeaways

1. **Use the right parameter type** - `StringListParameter` for dropdowns, `RangeParameter` for ranges
2. **Use SDK functions** - Don't reinvent `toPlain()` with custom formulas
3. **Trust automatic bindings** - `template-switch-control`, menu population work correctly
4. **Thread safety is critical** - Never manipulate UI from non-UI threads
5. **Read the source** - When stuck, read VSTGUI/VST3 SDK source code
6. **Create helpers** - Prevent future mistakes with helper functions that enforce correct usage
