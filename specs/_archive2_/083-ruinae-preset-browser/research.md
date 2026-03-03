# Research: Ruinae Preset Browser Integration

**Date**: 2026-02-27

## Research Topics

### 1. State Serialization Strategy: getComponentState() vs Controller-Side Re-serialization

**Decision**: Use `getComponentState()` from the host component handler.

**Rationale**: The spec explicitly requires (FR-013) that `createComponentStateStream()` delegate to the host via `getComponentState()`, not duplicate the parameter serialization logic in `Processor::getState()`. The Ruinae processor has complex state: 19+ parameter packs, 16 voice route slots, multiple enable flags, and arpeggiator lane data. Re-serializing this from the controller would require 300+ lines of code mirroring the processor's getState(), creating a maintenance burden and dual-source-of-truth risk.

**Alternatives considered**:
- **Controller-side re-serialization** (Disrumpo pattern at controller.cpp:4425-4690): Works but creates ~270 lines of duplicated serialization logic. Disrumpo chose this because it predates the getComponentState() pattern. Not appropriate for Ruinae given the complexity (arpeggiator lanes add significant state).
- **Host getComponentState()**: The VST3 SDK provides `IComponent::getState()` accessible through the host's component handler. The controller can query this via `FUnknownPtr<IComponent>` cast of the component handler. This returns the exact binary stream that `Processor::getState()` produces. Zero duplication.

**Research finding**: After examining the Disrumpo implementation, the controller-side serialization approach at `controller.cpp:4425-4690` is 265 lines of manual parameter serialization. For Ruinae, with its 30+ parameter packs and arpeggiator lanes, this would be 500+ lines. The host delegation approach is strictly better.

**Implementation note**: The host-delegated approach requires querying `IEditController::getComponentHandler()` to get the `IComponentHandler`, then querying for `IComponent` interface to call `getState()`. A `Steinberg::MemoryStream` is created and passed to `getState()`. The stream is then returned for the PresetManager's `StateProvider`.

### 2. loadComponentStateWithNotify Strategy: Reusing Parameter Pack Helpers

**Decision**: Reuse existing `loadXxxParamsToController` template functions with `editParamWithNotify` as the callback.

**Rationale**: Ruinae's `setComponentState()` (controller.cpp:257-352) already uses template helper functions like `loadGlobalParamsToController(streamer, setParam)` where `setParam` is a generic callable taking `(ParamID, double)`. For preset loading, the only difference is that we call `editParamWithNotify(id, value)` instead of `setParamNormalized(id, value)`. This means `loadComponentStateWithNotify` can mirror `setComponentState` exactly, just changing the callback.

**Alternatives considered**:
- **Full manual re-implementation** (Disrumpo pattern): Disrumpo manually reads each parameter and calls editParamWithNotify. This is because Disrumpo does not use template helper functions for param loading. Ruinae does, so this approach would be wasteful.
- **Reuse template helpers**: Pass `[this](auto id, auto val) { editParamWithNotify(id, val); }` as the `SetParamFunc` to all `loadXxxParamsToController` functions. This produces identical behavior with zero code duplication.

**Key finding**: The `loadXxxParamsToController` functions are templates accepting any callable with signature `(Steinberg::Vst::ParamID, double)`. The `editParamWithNotify` function takes `(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue)` where `ParamValue = double`. These are compatible.

### 3. Custom View Creation Pattern for Ruinae

**Decision**: Add `createCustomView` override to the Ruinae controller, following the Disrumpo pattern.

**Rationale**: The Ruinae controller currently uses `verifyView` for all custom view handling (matching views by `custom-view-name` attribute after VSTGUI creates them from uidesc). The PresetBrowserButton and SavePresetButton need to be fully custom views (not just modified standard views), because they store a `Controller*` pointer and override `onMouseDown`. This requires `createCustomView` which creates views from scratch by name, identical to how Disrumpo handles it.

**Alternatives considered**:
- **verifyView with CTextButton in uidesc**: Place standard CTextButton views in uidesc and intercept them in verifyView by custom-view-name. This works but requires casting the already-created CTextButton and adding a control listener, which is messier than creating the correct custom class directly.
- **createCustomView** (chosen): Clean creation of PresetBrowserButton/SavePresetButton objects by name. VSTGUI calls `createCustomView("PresetBrowserButton", ...)` when it encounters `custom-view-name="PresetBrowserButton"` in the uidesc. The controller returns a fully-configured button instance.

**Implementation note**: The Ruinae controller.h needs to add the `createCustomView` declaration. The uidesc uses `<view custom-view-name="PresetBrowserButton" origin="X, Y" size="W, H"/>` which triggers `createCustomView`. The method reads origin/size from UIAttributes, creates the button with the correct rect, and returns it.

### 4. Editor Layout Analysis

**Decision**: Place "Presets" and "Save" buttons in the top bar after the tab selector, matching Disrumpo's pattern.

**Rationale**: The Ruinae editor (1400x800) has a top bar from y=0 to y=40. The "RUINAE" title occupies x=12 to x=112. The tab selector (SOUND|MOD|FX|SEQ) occupies x=120 to x=440 (origin=120, size=320). The space from x=440 to x=1400 is completely empty -- 960px available.

**Placement (matching Disrumpo pattern)**:
- Presets button: origin="460, 8" size="80, 25" -- 20px gap after tab selector
- Save button: origin="550, 8" size="60, 25" -- 10px gap after Presets

**Alternatives considered**:
- Right-aligned buttons (origin ~1300): Too far from tabs, breaks visual flow.
- Left of tab selector: No space (RUINAE title at x=12-112, 8px gap, tabs at x=120).

### 5. Overlay View Lifecycle

**Decision**: Create overlay views in `didOpen()`, null out pointers in `willClose()`.

**Rationale**: This is the proven pattern from both Iterum and Disrumpo. The views are created and added to the editor's frame. The frame owns the views (manages their lifetime). When the editor closes, the frame destroys all its child views. The controller must null its raw pointers to avoid dangling references.

**Key lifecycle**:
1. `didOpen(editor)`: Create `PresetBrowserView` and `SavePresetDialogView` with frame size, add to frame.
2. Button clicks: Call `openPresetBrowser()` / `openSavePresetDialog()` which call `view->open("")`.
3. `willClose(editor)`: Set `presetBrowserView_ = nullptr` and `savePresetDialogView_ = nullptr` BEFORE `activeEditor_ = nullptr`.

### 6. Factory Preset Discovery

**Decision**: No changes needed. The shared `PresetManager` handles discovery automatically.

**Rationale**: The `PresetManager::scanPresets()` method scans the factory preset directory (resolved from the plugin's resource bundle) and the user preset directory. Ruinae already has 14 factory presets in `plugins/ruinae/resources/presets/` organized by subcategory (6 Arp categories, 2-3 presets each). The `makeRuinaePresetConfig()` already provides the correct `processorUID`, `pluginName`, and `subcategoryNames`.

### 7. Test Strategy

**Decision**: Unit tests for controller-level wiring; pluginval for lifecycle verification.

**Tests to write**:
1. `createComponentStateStream` returns non-null stream with valid data
2. `loadComponentStateWithNotify` round-trips parameters correctly
3. `editParamWithNotify` calls beginEdit/setParamNormalized/performEdit/endEdit in sequence
4. `openPresetBrowser`/`closePresetBrowser` toggle browser visibility (requires mock views)
5. State roundtrip test: save preset via stateProvider, load via loadProvider, verify parameters match

**Existing tests to verify after changes**:
- `ruinae_tests` full suite (no regressions)
- `shared_tests` full suite (no regressions)
- pluginval strictness level 5 (lifecycle, 100 open/close cycles)
