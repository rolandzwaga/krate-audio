# VST3 & VSTGUI Code Review Checklist

Detailed review criteria for VST3 plugin infrastructure: controllers, parameter handling, UI components, editor lifecycle, and uidesc files.

---

## 1. Thread Safety (CRITICAL)

The single most common source of crashes in VST3 plugins.

### setParamNormalized() Safety

`setParamNormalized()` can be called from ANY thread (UI, audio, host background). Review every override or callback that touches UI:

| Pattern | Verdict | Why |
|---------|---------|-----|
| `setParamNormalized()` calls `setVisible()` | CRITICAL | Thread safety violation |
| `setParamNormalized()` calls `setValue()` on a control | CRITICAL | Thread safety violation |
| `setParamNormalized()` stores value in atomic, defers UI update | OK | Safe pattern |
| `IDependent::update()` calls `setVisible()` | OK | Runs on UI thread |

### IDependent Lifecycle

When reviewing `IDependent`-based controllers (VisibilityController, etc.):

1. **Constructor**: `addDependent(this)` called? `addRef()` on watched parameter?
2. **deactivate()**: Does `removeDependent(this)` happen HERE (not in destructor)?
3. **update()**: Checks `isActive_` before accessing any views?
4. **willClose()**: Deactivates ALL controllers BEFORE nulling editor pointer BEFORE destroying them?
5. **Race condition**: If constructor calls `deferUpdate()`, can editor close before it fires?

**willClose() Order of Operations (MUST be this order):**
```
Phase 1: deactivate() all controllers     → stops receiving updates
Phase 2: activeEditor_ = nullptr          → in-flight updates return early
Phase 3: destroy controllers (= nullptr)  → safe destruction
```

### CViewContainer Child Destruction

Children are destroyed by `removeAll()` BEFORE the parent destructor runs:
- Accessing child view pointers in destructor = use-after-free (CRITICAL)
- Unregistering listeners from child views in destructor = use-after-free (CRITICAL)

---

## 2. Parameter Handling

### Parameter Type Selection

| UI Control | Required Parameter Type | Common Mistake |
|------------|------------------------|----------------|
| `COptionMenu` (dropdown) | `StringListParameter` with `kIsList` | `RangeParameter` (no string entries) |
| `CSlider` / `CKnob` (continuous) | `RangeParameter` or `Parameter` | Wrong range bounds |
| `COnOffButton` (toggle) | `Parameter` with `stepCount=1` | Using `RangeParameter` |
| `CTextEdit` (value display) | Any with correct `toString` | Missing unit string |

**Critical rule:** ANY parameter bound to a `COptionMenu` MUST be a `StringListParameter`. A `RangeParameter` with correct `stepCount` is NOT sufficient — it has no string entries for the menu.

### Parameter Unit Conversions

The VST3 boundary operates in normalized 0.0-1.0. DSP components expect real units. Review every place where parameter values flow from processor to DSP:

```
Processor receives normalized → stores in atomic → passes to DSP
                                                    ↑
                                          CHECK: Is conversion correct?
```

Common mismatches:
| Parameter | Normalized Range | DSP Expects | Conversion Needed |
|-----------|-----------------|-------------|-------------------|
| Mix/Dry-Wet | 0.0 - 1.0 | 0 - 100 (percent) | `* 100.0f` |
| Delay time | 0.0 - 1.0 | milliseconds | `toPlain()` or custom range |
| Frequency | 0.0 - 1.0 | Hz | Usually logarithmic mapping |
| Gain/Drive | 0.0 - 1.0 | dB or linear | `toPlain()` + dBToLinear |

**Review action:** For each parameter, trace the value from `processParameterChanges()` to the DSP setter call. Verify units match.

### Parameter ID Naming

IDs in `plugin_ids.h` must follow `k{Mode}{Parameter}Id`:

| Check | Example Bad | Example Good |
|-------|-------------|-------------|
| Standard names | `kShimmerDryWetId` | `kShimmerMixId` |
| Abbreviations | `kDigitalModulationDepthId` | `kDigitalModDepthId` |
| No redundant prefix | `kShimmerShimmerMixId` | `kShimmerMixId` |
| ID value collisions | Two IDs with same numeric value | Each ID is unique |

### defaultNormalizedValue

`StringListParameter` constructor sets `defaultNormalizedValue = 0.0`. If the default is NOT index 0:
```cpp
param->setNormalized(defaultNorm);                          // Current value
param->getInfo().defaultNormalizedValue = defaultNorm;      // ALSO set this!
```

Missing the `getInfo().defaultNormalizedValue` assignment means "Reset to Default" in the host will reset to index 0 instead of the intended default.

---

## 3. Editor Lifecycle

### didOpen() / willClose() Symmetry

Everything created in `didOpen()` must be cleaned up in `willClose()`:
- Visibility controllers created → deactivated and destroyed
- View pointers cached → nulled
- Listeners registered → unregistered (if the view outlives the editor)

### View Pointer Safety Inside UIViewSwitchContainer

Views inside `UIViewSwitchContainer` templates are destroyed and recreated on every template switch. Cached pointers to these views WILL dangle.

**Required pattern:**
```cpp
// Wire a removal callback to null the pointer
indicator->setRemovedCallback([this, idx]() { indicators_[idx] = nullptr; });
indicators_[idx] = indicator;
```

**Review action:** For every cached view pointer (`someView_` member in controller), determine if the view lives inside a `UIViewSwitchContainer`. If yes, verify there's a removal callback.

---

## 4. Control Listener Registration

### Duplicate Registration

VSTGUI's `DispatchList::add()` does NOT deduplicate. Calling `registerControlListener(listener)` twice adds the listener twice, causing `valueChanged()` to fire twice per interaction.

**Watch for:**
- Range-based registration (`if (tag >= X && tag <= Y)`) that overlaps with explicit registration
- Registration in both `verifyView()` and `didOpen()`
- Registration in a loop that may run multiple times

**Review action:** For each `registerControlListener()` call, verify the same listener isn't registered elsewhere for the same control.

### Feedback Loop Prevention

VST3Editor's `valueChanged()` already checks `isEditing()` to prevent feedback loops. Custom `valueChanged()` overrides should NOT add additional feedback prevention unless there's a specific reason. Unnecessary guards mask real bugs.

---

## 5. UIDesc Review

### template-switch-control Binding

```xml
<view class="UIViewSwitchContainer" template-switch-control="ControlTagName" ...>
```

- The control tag MUST correspond to a registered parameter
- If using a proxy parameter (not directly bound to a CControl), there MUST be a hidden 1x1 CControl bound to the proxy parameter — UIViewSwitchContainer uses `IControlListener`, not `IDependent`

### COptionMenu Binding

```xml
<view class="COptionMenu" control-tag="SomeTag" .../>
```

- The parameter registered for `SomeTag` MUST be a `StringListParameter`
- If it's a `RangeParameter`, the menu will have 0 entries and snap to first item

### Control Tag Consistency

Every `control-tag` referenced in the uidesc must:
1. Be defined in the `<control-tags>` section
2. Have a corresponding parameter registered in the controller
3. Have matching numeric values between uidesc tag and `plugin_ids.h`

---

## 6. Cross-Platform Compliance

### Forbidden APIs

| API | Platform | Use Instead |
|-----|----------|-------------|
| `MessageBox`, `CreateWindow`, Win32 APIs | Windows | VSTGUI equivalents |
| `NSAlert`, `NSOpenPanel`, Cocoa/AppKit | macOS | VSTGUI `CFileSelector`, `COptionMenu` |
| `gtk_*`, X11 calls | Linux | VSTGUI equivalents |
| Platform-specific fonts | Any | Use VSTGUI font abstraction or web-safe fonts |

**Exception:** Platform-specific code is ONLY acceptable for debug logging, guarded by `#ifdef _WIN32` / `#ifdef __APPLE__` / etc.

### Narrowing Conversions

Clang errors on narrowing in brace initialization. Watch for:
```cpp
// FAILS on Clang:
BlockContext ctx{44100.0, 120.0};  // double to float narrowing

// CORRECT:
BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0};
```

### Floating-Point Consistency

- MSVC and Clang produce different results at 7th-8th decimal place
- Tests comparing floating-point values MUST use `Approx().margin()`, not exact equality
- Approval tests must limit precision to `std::setprecision(6)` or coarser

---

## 7. State Save/Load

When a new parameter is added, verify it's included in both:
1. `Processor::getState()` — serialization
2. `Processor::setState()` — deserialization
3. `Controller::setComponentState()` — syncing controller to processor state

### Version Compatibility

- New parameters should have sensible defaults when loading old presets (where they're absent)
- State loading should handle missing parameters gracefully (not crash)
- Parameter order in state serialization matters for binary formats

---

## 8. Processor/Controller Separation

VST3's processor and controller are separate components that may run in different processes.

### Forbidden Cross-References

| From | To | Allowed? |
|------|----|----------|
| Processor headers | Controller headers | NO (CRITICAL) |
| Controller headers | Processor headers | NO (CRITICAL) |
| Both | `plugin_ids.h`, `version.h` | YES (shared) |
| Processor → Controller | Via `IMessage` | YES (message passing) |

### Processor Independence

The processor MUST work correctly without a controller:
- No UI state should affect audio processing
- Parameters should have sensible defaults
- Processor must handle `process()` calls even if no editor was ever opened

---

## 9. Test Coverage (for VST/UI code changes)

### Required Tests for New Parameters

| Test | What It Verifies |
|------|-----------------|
| Parameter registration | Parameter exists with correct type, range, default |
| State round-trip | Save state → load state → parameter values match |
| Parameter application | Setting parameter actually affects audio output |
| Integration wiring | Parameter flows from host → processor → DSP component |

### Required Tests for New UI Components

| Test | What It Verifies |
|------|-----------------|
| Controller creation | Plugin controller instantiates without crash |
| State sync | Controller receives processor state correctly |
| Parameter binding | UI controls are connected to correct parameters |

### pluginval Validation

After any plugin source changes, `pluginval --strictness-level 5` must pass. This catches:
- Initialization bugs
- State save/load errors
- Audio processing violations
- Parameter range issues
