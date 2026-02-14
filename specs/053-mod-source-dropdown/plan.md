# Implementation Plan: Mod Source Dropdown Selector

**Branch**: `053-mod-source-dropdown` | **Date**: 2026-02-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/053-mod-source-dropdown/spec.md`

## Summary

Replace the 3-segment `IconSegmentButton` (LFO 1 | LFO 2 | Chaos) in the Modulation section with a 10-entry `COptionMenu` dropdown driving a `UIViewSwitchContainer`. This migrates the mod source area to the same automatic view-switching pattern used throughout the Ruinae UI (oscillator types, filter types, distortion types, delay types), removes manual visibility management code from the controller, and adds 7 placeholder entries for future modulation sources.

This is a **UI-only change** -- no DSP code is modified, no audio processing is affected, and no new C++ classes are created.

## Technical Context

**Language/Version**: C++20, CMake 3.20+
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+ (UIViewSwitchContainer, COptionMenu, StringListParameter)
**Storage**: N/A (parameter is ephemeral, never persisted)
**Testing**: Pluginval (strictness level 5), manual visual verification *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: VST3 plugin monorepo
**Performance Goals**: N/A (no DSP changes)
**Constraints**: Must fit within existing 158px-wide mod source area; dropdown max 20px height
**Scale/Scope**: 4 files modified, 10 named templates added to uidesc, controller code reduced

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-design check**: PASSED -- all gates clear.

**Required Check - Principle V (VSTGUI Development):**
- [x] Using UIDescription XML for layout (UIViewSwitchContainer, COptionMenu)
- [x] UI thread only -- no audio processing data accessed
- [x] All parameter values normalized (0.0-1.0) at VST boundary
- [x] Using cross-platform VSTGUI abstractions only (COptionMenu, not native menus)

**Required Check - Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific APIs used
- [x] COptionMenu is cross-platform VSTGUI control
- [x] UIViewSwitchContainer is cross-platform VSTGUI component

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Pluginval will validate plugin integrity after changes
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No new classes/structs/functions are created -- only modifying existing code and XML

**Required Check - Principle XVI (Honest Completion):**
- [x] All FR-xxx and SC-xxx will be verified against actual implementation
- [x] Evidence will include specific file paths, line numbers, and test output

**Post-design re-check**: PASSED -- no constitution violations in the design.

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: NONE. This feature creates no new C++ types.

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| (none) | N/A | N/A | N/A |

**Utility Functions to be created**: NONE. This feature creates no new functions.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none) | N/A | N/A | N/A | N/A |

**Named templates to be created** (uidesc XML, not C++ -- no ODR risk):

| Template Name | Search (`grep -r "ModSource_" plugins/ruinae/`) | Existing? | Action |
|---------------|--------------------------------------------------|-----------|--------|
| ModSource_LFO1 | No matches | No | Create (extracted from inline view) |
| ModSource_LFO2 | No matches | No | Create (extracted from inline view) |
| ModSource_Chaos | No matches | No | Create (extracted from inline view) |
| ModSource_Macros | No matches | No | Create (empty placeholder) |
| ModSource_Rungler | No matches | No | Create (empty placeholder) |
| ModSource_EnvFollower | No matches | No | Create (empty placeholder) |
| ModSource_SampleHold | No matches | No | Create (empty placeholder) |
| ModSource_Random | No matches | No | Create (empty placeholder) |
| ModSource_PitchFollower | No matches | No | Create (empty placeholder) |
| ModSource_Transient | No matches | No | Create (empty placeholder) |

### Existing Components to Reuse

| Component | Location | How It Will Be Used |
|-----------|----------|---------------------|
| `UIViewSwitchContainer` | VSTGUI framework | Automatic view switching based on parameter value |
| `COptionMenu` | VSTGUI framework | Dropdown selector, auto-populated from StringListParameter |
| `StringListParameter` | VST3 SDK | Parameter with named entries, drives COptionMenu + UIViewSwitchContainer |
| `kModSourceViewModeTag` (10019) | `plugins/ruinae/src/plugin_ids.h:587` | Existing parameter ID, reused unchanged |
| `registerChaosModParams()` | `plugins/ruinae/src/parameters/chaos_mod_params.h:46` | Extend the existing StringListParameter from 3 to 10 entries |
| Inline LFO1 view | `plugins/ruinae/resources/editor.uidesc:2019-2107` | Extract into ModSource_LFO1 template |
| Inline LFO2 view | `plugins/ruinae/resources/editor.uidesc:2112-2201` | Extract into ModSource_LFO2 template |
| Inline Chaos view | `plugins/ruinae/resources/editor.uidesc:2206-2259` | Extract into ModSource_Chaos template |

### Files Checked for Conflicts

- [x] `plugins/ruinae/resources/editor.uidesc` - No existing `ModSource_*` templates
- [x] `plugins/ruinae/src/parameters/chaos_mod_params.h` - StringListParameter registration found
- [x] `plugins/ruinae/src/plugin_ids.h` - `kModSourceViewModeTag = 10019` confirmed
- [x] `plugins/ruinae/src/controller/controller.h` - Member variables to remove identified
- [x] `plugins/ruinae/src/controller/controller.cpp` - Code blocks to remove identified

### ODR Risk Assessment

**Risk Level**: None

**Justification**: No new C++ types or functions are created. This feature only modifies existing parameter registration, removes existing controller code, and adds XML templates. There is zero risk of ODR violations.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from source) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `StringListParameter` | constructor | `StringListParameter(const TChar* title, ParamID tag, ...)` | Yes |
| `StringListParameter` | appendString | `void appendString(const String128 string)` | Yes |
| `ParameterContainer` | addParameter | `Parameter* addParameter(Parameter* p)` | Yes |

### Header Files Read

- [x] `plugins/ruinae/src/parameters/chaos_mod_params.h` - StringListParameter usage confirmed (lines 65-70)
- [x] `plugins/ruinae/src/parameters/distortion_params.h` - Reference pattern for StringListParameter (lines 196-200)
- [x] `plugins/ruinae/src/controller/controller.h` - Member variables at lines 226-229
- [x] `plugins/ruinae/src/controller/controller.cpp` - valueChanged at lines 510-516, verifyView at lines 824-840
- [x] `plugins/ruinae/resources/editor.uidesc` - Complete mod source area (lines 2005-2259)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `StringListParameter` | Normalized value = index / (count - 1) | With 10 entries, stepCount = 9, index 2 = 2/9 = 0.222... |
| Old controller code | Used `std::round(value * 2.0)` for 3-entry mapping | UIViewSwitchContainer handles this automatically for any count |
| `UIViewSwitchContainer` | Requires named templates in `template-names`, not inline views | Must extract inline views to named templates first |
| `custom-view-name` | Works within templates instantiated by UIViewSwitchContainer | `LFO1RateGroup`, etc. will still be found by `verifyView()` |
| Template size mismatch | Chaos view was 158x106, UIViewSwitchContainer is 158x120 | Template size set to 158x120, extra space is transparent |

## Layer 0 Candidate Analysis

N/A -- this is a UI-only feature with no DSP code. No utility functions to extract.

**Decision**: No Layer 0 extraction needed.

## SIMD Optimization Analysis

N/A -- this is a UI-only feature with no DSP processing. SIMD analysis is not applicable.

**Verdict**: NOT APPLICABLE

**Reasoning**: This feature modifies only UI code (uidesc XML, controller view management, parameter registration). No audio processing loops are involved. SIMD analysis is irrelevant.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: UI infrastructure (VSTGUI XML + controller)

**Related features at same layer** (from roadmap):
- Phase 4.2: Macro Knobs view
- Phase 4.3: Rungler Configuration view
- Phase 6.1-6.5: Env Follower, S&H, Random, Pitch Follower, Transient views

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| UIViewSwitchContainer infrastructure | HIGH | All 7 future mod source phases | Already built by this spec |
| Placeholder template pattern | HIGH | Each future source replaces its placeholder | Template naming convention established |

### Detailed Analysis

**UIViewSwitchContainer infrastructure** provides:
- Automatic view switching driven by ModSourceViewMode parameter
- 10-slot template-names list with room for all current + future sources

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Phase 4.2 (Macros) | YES | Replace `ModSource_Macros` empty template with populated one |
| Phase 4.3 (Rungler) | YES | Replace `ModSource_Rungler` empty template with populated one |
| Phase 6.1-6.5 (5 new sources) | YES | Replace each empty template with populated one |

**Recommendation**: Infrastructure is built now. Future phases only need to populate templates -- no dropdown or switching code changes required.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| 10-entry dropdown from the start | Avoids touching dropdown infrastructure in 7 future phases |
| Separate empty templates per source | Each future phase is independently implementable |
| No shared base template for mod sources | Sources have very different control layouts (LFO: 11 controls, Chaos: 5 controls) |

### Review Trigger

After implementing **Phase 4.2 (Macro Knobs view)**, review this section:
- [ ] Does Macros need additional dropdown changes? Expectation: NO
- [ ] Does Macros need sub-controller for parameter remapping? Possibly, if macros share generic controls
- [ ] Any duplicated template structure? Check if LFO layout can be templated

## Project Structure

### Documentation (this feature)

```text
specs/053-mod-source-dropdown/
+-- plan.md              # This file
+-- research.md          # Phase 0 research findings
+-- data-model.md        # Entity/template definitions
+-- quickstart.md        # Implementation guide
+-- contracts/
|   +-- uidesc-contract.md  # XML structure contract
+-- checklists/          # Pre-existing
+-- spec.md              # Feature specification
```

### Source Code (files modified)

```text
plugins/ruinae/
+-- resources/
|   +-- editor.uidesc           # Add 10 templates, replace IconSegmentButton + inline views
+-- src/
    +-- parameters/
    |   +-- chaos_mod_params.h  # Extend StringListParameter (3 -> 10 entries)
    +-- controller/
        +-- controller.h        # Remove 3 member variables
        +-- controller.cpp      # Remove valueChanged block + verifyView branches
```

## Complexity Tracking

No constitution violations. No complexity deviations required.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none) | N/A | N/A |
