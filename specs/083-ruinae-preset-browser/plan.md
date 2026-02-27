# Implementation Plan: Ruinae Preset Browser Integration

**Branch**: `083-ruinae-preset-browser` | **Date**: 2026-02-27 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/083-ruinae-preset-browser/spec.md`

## Summary

Integrate a full preset browser into the Ruinae synthesizer plugin UI by wiring the existing shared preset infrastructure (`PresetBrowserView`, `SavePresetDialogView`, `PresetManager`) already proven in Iterum and Disrumpo. The feature requires: (1) adding `stateProvider` and `loadProvider` callbacks to the already-instantiated `presetManager_`, (2) implementing `createComponentStateStream()` using the host's `getComponentState()` API, (3) implementing `loadComponentStateWithNotify()` by reusing existing `loadXxxParamsToController` template helpers with `editParamWithNotify` as the callback, (4) creating preset browser/save dialog overlay views in `didOpen()` and cleaning them up in `willClose()`, (5) adding `PresetBrowserButton` and `SavePresetButton` custom view classes, and (6) placing "Presets" and "Save" buttons in the editor.uidesc top bar.

This is a UI-thread integration task with zero DSP changes. All shared components are reused as-is.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, shared preset infrastructure (`plugins/shared/`)
**Storage**: .vstpreset files (factory in resources/presets/, user in platform-specific directory)
**Testing**: Catch2 via `ruinae_tests` target *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform, VSTGUI only
**Project Type**: Monorepo plugin
**Performance Goals**: Preset browser opens/closes without memory leaks across 100 cycles (pluginval level 5); search filtering within 200ms
**Constraints**: No audio thread impact; UI thread only; no new shared components
**Scale/Scope**: 14 existing factory presets across 6 Arp subcategories; 13 category tabs; all 6 synth subcategory tabs currently empty

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate. No cross-includes added.
- [x] State flows from host to processor via `getComponentState()`, not from controller.
- [x] `createComponentStateStream()` delegates to host, not re-implements serialization.

**Principle V (VSTGUI Development):**
- [x] UI uses VSTGUI overlays (`PresetBrowserView`, `SavePresetDialogView`) -- cross-platform.
- [x] Custom views (`PresetBrowserButton`, `SavePresetButton`) extend `CTextButton`.
- [x] `editor.uidesc` used for layout placement.

**Principle VI (Cross-Platform Compatibility):**
- [x] No Win32, Cocoa, or platform-specific UI APIs used.
- [x] All views use VSTGUI abstractions.

**Principle VIII (Testing Discipline):**
- [x] Tests written BEFORE implementation code.
- [x] Tests cover state roundtrip, preset discovery, and button wiring.

**Principle XII (Debugging Discipline):**
- [x] Framework patterns copied from Disrumpo/Iterum -- proven working.

**Principle XV (Pre-Implementation Research / ODR Prevention):**
- [x] Codebase Research section below is complete.
- [x] No duplicate classes/functions will be created.
- [x] `PresetBrowserButton` and `SavePresetButton` are in anonymous namespace in controller.cpp (same pattern as Disrumpo).

**Principle XVI (Honest Completion):**
- [x] Compliance table must be verified against actual code and test output.

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (Pre-Implementation Research / ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: PresetBrowserButton, SavePresetButton (both in anonymous namespace within controller.cpp)

| Planned Type | Search Result | Existing? | Action |
|--------------|---------------|-----------|--------|
| PresetBrowserButton | Found in `plugins/disrumpo/src/controller/controller.cpp:1159` (anonymous namespace) | Yes, in Disrumpo | Create New in Ruinae controller.cpp (anonymous namespace -- no ODR risk) |
| SavePresetButton | Found in `plugins/disrumpo/src/controller/controller.cpp:1187` (anonymous namespace) | Yes, in Disrumpo | Create New in Ruinae controller.cpp (anonymous namespace -- no ODR risk) |

**Utility Functions to be created**: editParamWithNotify, loadComponentStateWithNotify, createComponentStateStream, openPresetBrowser, closePresetBrowser, openSavePresetDialog

| Planned Function | Existing? | Location | Action |
|------------------|-----------|----------|--------|
| editParamWithNotify | Yes, in Disrumpo | `plugins/disrumpo/src/controller/controller.cpp:4696` | Create New in Ruinae controller (private method, no ODR risk) |
| loadComponentStateWithNotify | Yes, in Disrumpo | `plugins/disrumpo/src/controller/controller.cpp:4707` | Create New in Ruinae controller (private method) |
| createComponentStateStream | Yes, in Disrumpo | `plugins/disrumpo/src/controller/controller.cpp:4425` | Create New in Ruinae controller -- BUT use different strategy: delegate to host `getComponentState()` instead of re-serializing all params. The Disrumpo version re-serializes from controller params, which violates FR-013 for this spec. |
| openPresetBrowser | Yes, in Disrumpo | `plugins/disrumpo/src/controller/controller.cpp:4401` | Create New in Ruinae controller |
| closePresetBrowser | Yes, in Disrumpo | `plugins/disrumpo/src/controller/controller.cpp:4415` | Create New in Ruinae controller |
| openSavePresetDialog | Yes, in Disrumpo | `plugins/disrumpo/src/controller/controller.cpp:4409` | Create New in Ruinae controller |

### Existing Components to Reuse

| Component | Location | How It Will Be Used |
|-----------|----------|---------------------|
| PresetManager | plugins/shared/src/preset/preset_manager.h | Already instantiated in Ruinae controller. Add stateProvider + loadProvider callbacks. |
| PresetBrowserView | plugins/shared/src/ui/preset_browser_view.h | Create in didOpen(), add to frame as overlay |
| SavePresetDialogView | plugins/shared/src/ui/save_preset_dialog_view.h | Create in didOpen(), add to frame as overlay |
| makeRuinaePresetConfig() | plugins/ruinae/src/preset/ruinae_preset_config.h | Already used in Controller::initialize() |
| getRuinaeTabLabels() | plugins/ruinae/src/preset/ruinae_preset_config.h | Pass to PresetBrowserView constructor (13 tabs) |
| loadXxxParamsToController | plugins/ruinae/src/parameters/*.h | Reuse in loadComponentStateWithNotify with editParamWithNotify as callback |
| PresetManagerConfig | plugins/shared/src/preset/preset_manager_config.h | Already used via makeRuinaePresetConfig() |

### Files Checked for Conflicts

- [x] `plugins/ruinae/src/controller/controller.h` -- Already has presetManager_ field, forward decls for PresetBrowserView/SavePresetDialogView
- [x] `plugins/ruinae/src/controller/controller.cpp` -- No existing preset browser methods
- [x] `plugins/ruinae/src/preset/ruinae_preset_config.h` -- Already has makeRuinaePresetConfig() and getRuinaeTabLabels()
- [x] `plugins/ruinae/resources/editor.uidesc` -- Top bar has space after x=440 for buttons

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All new classes (`PresetBrowserButton`, `SavePresetButton`) are in anonymous namespaces within a single .cpp file. All new methods are private members of the existing `Ruinae::Controller` class. No new shared components are created. Pattern is identical to proven Disrumpo implementation.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PresetManager | constructor | `PresetManager(const PresetManagerConfig& config, Steinberg::Vst::AudioEffect* processor, Steinberg::Vst::EditControllerEx1* controller)` | Yes |
| PresetManager | setStateProvider | `void setStateProvider(StateProvider provider)` | Yes |
| PresetManager | setLoadProvider | `void setLoadProvider(LoadProvider provider)` | Yes |
| PresetManager | StateProvider typedef | `std::function<Steinberg::IBStream*()>` | Yes |
| PresetManager | LoadProvider typedef | `std::function<bool(Steinberg::IBStream*)>` | Yes |
| PresetBrowserView | constructor | `PresetBrowserView(const CRect& size, PresetManager* presetManager, const std::vector<std::string>& tabLabels)` | Yes |
| PresetBrowserView | open | `void open(const std::string& subcategory)` | Yes |
| PresetBrowserView | close | `void close()` | Yes |
| PresetBrowserView | isOpen | `bool isOpen() const` | Yes |
| SavePresetDialogView | constructor | `SavePresetDialogView(const CRect& size, PresetManager* presetManager)` | Yes |
| SavePresetDialogView | open | `void open(const std::string& subcategory)` | Yes |
| SavePresetDialogView | close | `void close()` | Yes |
| SavePresetDialogView | isOpen | `bool isOpen() const` | Yes |
| getRuinaeTabLabels | function | `inline std::vector<std::string> getRuinaeTabLabels()` | Yes |

### Header Files Read

- [x] `plugins/shared/src/preset/preset_manager.h` -- PresetManager class, StateProvider/LoadProvider typedefs
- [x] `plugins/shared/src/ui/preset_browser_view.h` -- PresetBrowserView constructor, open/close/isOpen
- [x] `plugins/shared/src/ui/save_preset_dialog_view.h` -- SavePresetDialogView constructor, open/close/isOpen
- [x] `plugins/ruinae/src/preset/ruinae_preset_config.h` -- makeRuinaePresetConfig, getRuinaeTabLabels
- [x] `plugins/ruinae/src/controller/controller.h` -- Controller class fields and methods
- [x] `plugins/disrumpo/src/controller/controller.h` -- Reference for preset browser fields
- [x] `plugins/disrumpo/src/controller/controller.cpp` -- Reference for all preset browser methods

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PresetBrowserView | Constructor takes `PresetManager*` (raw ptr), view is owned by frame (not by controller) | Store as raw pointer `presetBrowserView_`, null out in willClose() |
| SavePresetDialogView | Same ownership model as PresetBrowserView | Store as raw pointer `savePresetDialogView_`, null out in willClose() |
| PresetBrowserView::open | Takes subcategory string -- pass empty "" to show "All" tab | `presetBrowserView_->open("")` |
| loadXxxParamsToController | Template functions taking `SetParamFunc` -- any callable with (ParamID, double) | Can pass lambda wrapping editParamWithNotify |
| Ruinae state version | Currently version 1 only (no versioned migration needed) | `if (version != 1) return false;` in loadComponentStateWithNotify |
| voice routes in state | 16 voice route slots are processor-internal data, skipped in setComponentState | Must also skip in loadComponentStateWithNotify (read and discard) |
| FX enable flags | Stored as int8 between voice routes and phaser params | Must read as int8, convert to 0.0/1.0 for editParamWithNotify |
| Ruinae uses verifyView, not createCustomView | Custom views matched by `custom-view-name` attribute in uidesc | Must add createCustomView override OR use verifyView with custom-view-name on standard button views in uidesc |

## Layer 0 Candidate Analysis

**N/A** -- This is a UI integration task. No DSP code is created or modified. No Layer 0 utilities needed.

## SIMD Optimization Analysis

**N/A** -- This is a UI integration task. No DSP algorithms involved.

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This feature involves only UI-thread controller code (preset browser, save dialog, state serialization helpers). No audio processing or DSP algorithms are introduced.

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin integration (controller/UI)

### Sibling Features Analysis

**Related features at same layer**: None pending -- the preset browser is the last plugin to receive this feature. Iterum and Disrumpo already have it.

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| editParamWithNotify | HIGH | All three plugins have this pattern | Already exists in Disrumpo; could extract to shared, but not blocking |
| loadComponentStateWithNotify | LOW | Plugin-specific (each has different param layout) | Keep in Ruinae controller |
| createComponentStateStream | LOW | Plugin-specific (each has different state format) | Keep in Ruinae controller |
| PresetBrowserButton/SavePresetButton | MEDIUM | Same pattern in Disrumpo | Could extract to shared, but anonymous-namespace classes work fine |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep editParamWithNotify as private method | Third plugin to implement it. Could extract to shared base/utility, but all three implementations are identical one-liners. Refactoring existing plugins is out of scope. |
| Use Disrumpo pattern for custom view buttons | Proven approach. Ruinae does not currently override createCustomView, so we add the override. |
| Delegate to host getComponentState() for state stream | FR-013 requirement. Avoids duplicating the massive serialization code from Processor::getState(). The host already has the serialized state. |

## Project Structure

### Documentation (this feature)

```text
specs/083-ruinae-preset-browser/
+-- plan.md              # This file
+-- research.md          # Phase 0 research findings
+-- data-model.md        # Phase 1 data model
+-- quickstart.md        # Phase 1 quickstart guide
+-- contracts/           # Phase 1 API contracts
    +-- controller-api.md
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
plugins/ruinae/
+-- src/
|   +-- controller/
|   |   +-- controller.h         # ADD: presetBrowserView_, savePresetDialogView_ fields;
|   |   |                        #      openPresetBrowser, closePresetBrowser, openSavePresetDialog,
|   |   |                        #      createComponentStateStream, loadComponentStateWithNotify,
|   |   |                        #      editParamWithNotify, createCustomView declarations
|   |   +-- controller.cpp       # ADD: PresetBrowserButton/SavePresetButton classes (anon namespace);
|   |                            #      stateProvider/loadProvider wiring in initialize();
|   |                            #      overlay creation in didOpen(); cleanup in willClose();
|   |                            #      createCustomView override;
|   |                            #      createComponentStateStream, loadComponentStateWithNotify,
|   |                            #      editParamWithNotify, open/close/save methods
|   +-- preset/
|       +-- ruinae_preset_config.h  # NO CHANGES (already complete)
+-- resources/
|   +-- editor.uidesc            # ADD: PresetBrowserButton and SavePresetButton views in top bar
+-- tests/
    +-- unit/
        +-- controller/
            +-- preset_browser_test.cpp  # NEW: Unit tests for preset browser wiring
```

**Structure Decision**: All changes are within the existing Ruinae plugin directory. No new shared components. No DSP changes. Controller.h/cpp are the primary modification targets, with editor.uidesc for UI placement and a new test file for verification.

## Complexity Tracking

No constitution violations. All design decisions align with established patterns.
