# Implementation Tasks: Progressive Disclosure & Accessibility

**Feature Branch**: `012-progressive-disclosure`
**Spec**: [spec.md](spec.md) | [Plan](plan.md) | [Data Model](data-model.md) | [Quickstart](quickstart.md)
**Status**: Ready for Implementation

---

## Overview

This feature adds progressive disclosure UI patterns and accessibility support to the Disrumpo multiband distortion plugin. It encompasses 8 user stories organized by priority, with a total of 40 functional requirements (FR-001 to FR-040) and 11 success criteria (SC-001 to SC-011).

**Key capabilities**:
- Smooth panel expand/collapse with animation (US1, US6)
- Resizable plugin window with 5:3 aspect ratio constraints (US2)
- Modulation panel visibility toggle (US3)
- Keyboard shortcuts for band navigation and parameter adjustment (US4)
- MIDI CC mapping with MIDI Learn for all parameters (US5)
- 14-bit MIDI CC support for high-resolution control (US8)
- High contrast mode and reduced motion detection (US7)

---

## Implementation Strategy

**MVP Scope**: User Story 1 (Expand/Collapse with Animation) + User Story 2 (Window Resize)
- Delivers immediate value: reduces visual clutter and scales across screen sizes
- Foundation for remaining stories (modulation panel toggle reuses expand pattern)
- Independently testable without external dependencies

**Incremental Delivery**:
- Phase 1 (Setup): Project initialization, parameter additions
- Phase 2 (Foundational): Shared infrastructure (AccessibilityHelper, MidiCCManager)
- Phases 3-10: One user story per phase, independently deliverable
- Final Phase: Polish, cross-platform validation, clang-tidy

**Parallel Execution Opportunities**: Each user story phase has parallelizable tasks marked with [P].

---

## Dependencies

### User Story Completion Order

```
Phase 1 (Setup)
    ↓
Phase 2 (Foundational - AccessibilityHelper, MidiCCManager)
    ↓
    ├→ US1 (Expand/Collapse) - Independent
    ├→ US2 (Window Resize) - Independent
    ├→ US3 (Modulation Panel Visibility) - Independent
    ├→ US4 (Keyboard Shortcuts) - Independent
    ├→ US5 (MIDI CC Mapping) - Depends on Foundational (MidiCCManager)
    ├→ US6 (Panel Animation) - Depends on US1 (extends expand controller)
    ├→ US7 (Accessibility) - Depends on Foundational (AccessibilityHelper), US6 (reduced motion)
    └→ US8 (14-bit MIDI) - Depends on US5 (extends MidiCCManager)
    ↓
Phase 11 (Polish)
```

**Critical Path**: Setup → Foundational → US6 → US7 (longest chain: 4 phases)

**Parallel Opportunities**:
- US1, US2, US3, US4 can be implemented simultaneously after Foundational
- US5 and US8 can run in parallel with US1/US2/US3/US4 (different files)
- US6 and US7 are sequential dependencies on the critical path

---

## Phase 1: Setup

**Goal**: Initialize project structure, add new parameter IDs, update state versioning.

### Tasks

- [x] T001 Add new global parameter types to plugin_ids.h: kGlobalModPanelVisible (0x06), kGlobalMidiLearnActive (0x07), kGlobalMidiLearnTarget (0x08)
- [x] T002 Bump kPresetVersion from 6 to 7 in plugin_ids.h for new state fields
- [x] T003 [P] Add contract headers to project: accessibility_helper.h, midi_cc_manager.h, keyboard_shortcut_handler.h, animation_controller.h from specs/012-progressive-disclosure/contracts/
- [x] T004 Update editor.uidesc minSize from "800, 700" to "834, 500" (exact 5:3 ratio) and maxSize to "1400, 840"
- [x] T005 Create CMakeLists.txt entry for shared/src/platform/accessibility_helper.cpp (platform-specific with MSVC/Clang/GCC detection)
- [x] T006 Create CMakeLists.txt entry for shared/src/midi/midi_cc_manager.cpp

---

## Phase 2: Foundational Infrastructure

**Goal**: Implement shared utilities needed by multiple user stories.

**Blocking for**: US5 (MIDI CC), US7 (Accessibility), US8 (14-bit MIDI)

### Tasks

- [x] T007 [P] Implement AccessibilityHelper in plugins/shared/src/platform/accessibility_helper.cpp with Windows SystemParametersInfo(SPI_GETHIGHCONTRAST) behind #ifdef _WIN32 (covers FR-025b)
- [x] T008 [P] Add macOS accessibility detection to accessibility_helper.cpp using NSWorkspace.shared.accessibilityDisplayShouldIncreaseContrast behind #ifdef __APPLE__ (covers FR-025c)
- [x] T009 [P] Add Linux best-effort accessibility detection to accessibility_helper.cpp (GTK_THEME check, GSettings fallback) behind #else (covers FR-025d)
- [x] T010 [P] Implement MidiCCManager in plugins/shared/src/midi/midi_cc_manager.cpp: addGlobalMapping, addPresetMapping, removeMapping, clearAll
- [x] T011 [P] Implement MIDI Learn in MidiCCManager: startLearn, cancelLearn, isLearning, getLearnTargetParamId
- [x] T012 [P] Implement MIDI CC processing in MidiCCManager: processCCMessage with callback support
- [x] T013 [P] Implement serialization in MidiCCManager: serializeGlobalMappings, deserializeGlobalMappings, serializePresetMappings, deserializePresetMappings
- [x] T014 Write unit tests for MidiCCManager in plugins/disrumpo/tests/unit/midi_cc_manager_test.cpp: mapping CRUD, MIDI Learn workflow, 14-bit CC pairing
- [x] T015 Write unit tests for AccessibilityHelper in plugins/disrumpo/tests/unit/accessibility_test.cpp: mock OS queries, color palette parsing
- [x] T016 Build and verify all tests pass: cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure

---

## Phase 3: User Story 1 - Expand/Collapse Band Detail Panels

**Goal**: Enable per-band expand/collapse of detail panels with smooth animation.

**Independent Test**: Click expand/collapse buttons on band strips, verify panel visibility changes.

**Acceptance Criteria**: FR-001 to FR-006

### Tasks

- [x] T017 [US1] Implement AnimatedExpandController in plugins/disrumpo/src/controller/animated_expand_controller.cpp based on contract (extends ContainerVisibilityController pattern)
- [x] T018 [US1] Add VSTGUI animation integration to AnimatedExpandController: use CFrame::getAnimator(), ViewSizeAnimation, CubicBezierTimingFunction::easyInOut(250)
- [x] T019 [US1] Implement mid-animation state change handling in AnimatedExpandController (FR-006): cancel existing animation, start new one from current position
- [x] T020 [US1] Add setAnimationsEnabled(bool) method to AnimatedExpandController for reduced motion support
- [x] T021 [US1] Update Controller::didOpen() in controller.cpp: replace ContainerVisibilityController for expandedVisibilityControllers_ with AnimatedExpandController instances (8 controllers for 8 bands)
- [x] T022 [US1] Update Controller::willClose() in controller.cpp: deactivate all AnimatedExpandControllers before cleanup
- [x] T023 [US1] Write integration test in plugins/disrumpo/tests/integration/expand_collapse_test.cpp: verify animation timing <= 300ms, mid-animation reversal, reduced motion bypass
- [x] T024 [US1] Build and verify all tests pass

---

## Phase 4: User Story 2 - Resizable Plugin Window

**Goal**: Allow users to resize the plugin window from 834x500 to 1400x840, maintaining 5:3 aspect ratio.

**Independent Test**: Drag resize handle, verify window scales correctly at min/default/max sizes.

**Acceptance Criteria**: FR-017 to FR-023

### Tasks

- [x] T025 [US2] Implement window resize constraint in Controller::createView(): call setEditorSizeConstrains(CPoint(834, 500), CPoint(1400, 840))
- [x] T026 [US2] Add window size state members to controller.h: double lastWindowWidth_, double lastWindowHeight_
- [x] T027 [US2] Implement window size persistence in Controller::getState(): serialize lastWindowWidth_ and lastWindowHeight_ to IBStream after version number
- [x] T028 [US2] Implement window size restoration in Controller::setState(): deserialize lastWindowWidth_ and lastWindowHeight_, call requestResize() if editor is open
- [x] T029 [US2] Update Controller::didOpen() to restore last window size: read lastWindowWidth_/lastWindowHeight_, call requestResize(CPoint(w, h))
- [x] T030 [US2] Add aspect ratio enforcement in didOpen: compute 5:3 constrained size from requested size before calling requestResize (covers FR-021a: auto-snap to 5:3 diagonal)
- [x] T031 [US2] Write integration test in plugins/disrumpo/tests/integration/resize_test.cpp: verify min/max bounds, aspect ratio constraint, size persistence across editor close/open
- [x] T032 [US2] Build and verify all tests pass

---

## Phase 5: User Story 3 - Modulation Panel Visibility

**Goal**: Add a toggle control to show/hide the modulation panel.

**Independent Test**: Click modulation panel toggle, verify panel appears/disappears and active routings persist.

**Acceptance Criteria**: FR-007 to FR-009

### Tasks

- [x] T033 [US3] Register kGlobalModPanelVisible parameter in Controller::registerGlobalParams(): Boolean parameter, stepCount=1, default=0.0 (hidden)
- [x] T034 [US3] Add UI tag 9300 to modulation panel container in editor.uidesc
- [x] T035 [US3] Create ContainerVisibilityController in Controller::didOpen() for modulation panel: watch kGlobalModPanelVisible, control tag 9300
- [x] T036 [US3] Deactivate modulation panel visibility controller in Controller::willClose()
- [x] T037 [US3] Write integration test in plugins/disrumpo/tests/integration/modulation_panel_test.cpp: toggle visibility, verify active modulation routings persist when hidden
- [x] T038 [US3] Build and verify all tests pass

---

## Phase 6: User Story 4 - Keyboard Shortcuts for Efficient Workflow

**Goal**: Enable keyboard-only navigation and control of the plugin.

**Independent Test**: Press Tab/Space/Arrow keys, verify focus changes and parameter adjustments.

**Acceptance Criteria**: FR-010 to FR-016

### Tasks

- [x] T039 [US4] Implement KeyboardShortcutHandler in plugins/disrumpo/src/controller/keyboard_shortcut_handler.cpp based on contract (implements IKeyboardHook)
- [x] T040 [US4] Implement Tab key handling in KeyboardShortcutHandler::onKeyboardEvent(): cycle focus through band strips using CFrame::advanceNextFocusView()
- [x] T041 [US4] Implement Shift+Tab key handling: reverse focus cycle
- [x] T042 [US4] Implement Space key handling: toggle kBandBypass parameter for focused band
- [x] T043 [US4] Implement Arrow key handling: Up/Right increments by 1/100th range, Down/Left decrements by 1/100th range
- [x] T044 [US4] Implement Shift+Arrow key handling: coarse adjustment (1/10th range)
- [x] T045 [US4] Register KeyboardShortcutHandler in Controller::didOpen(): call frame->registerKeyboardHook(keyboardHandler_.get())
- [x] T046 [US4] Unregister KeyboardShortcutHandler in Controller::willClose(): call frame->unregisterKeyboardHook(keyboardHandler_.get())
- [x] T047 [US4] Enable focus drawing in Controller::didOpen(): call setFocusDrawingEnabled(true), setFocusColor(CColor(0x3A, 0x96, 0xDD)), setFocusWidth(2.0) (covers FR-010a: 2px colored focus outline, FR-010b: visible against all backgrounds)
- [x] T048 [US4] Write unit tests in plugins/disrumpo/tests/unit/keyboard_shortcut_test.cpp: Tab cycling logic, Space toggle logic, Arrow key step calculation
- [x] T049 [US4] Build and verify all tests pass

---

## Phase 7: User Story 5 - MIDI CC Mapping with MIDI Learn

**Goal**: Enable users to map any plugin parameter to MIDI CC via right-click MIDI Learn.

**Independent Test**: Right-click a control, select MIDI Learn, send a CC message, verify mapping is created and parameter responds.

**Acceptance Criteria**: FR-030 to FR-037

### Tasks

- [x] T050 [US5] Add MidiCCManager member to controller.h: std::unique_ptr<Krate::Plugins::MidiCCManager> midiCCManager_
- [x] T051 [US5] Initialize MidiCCManager in Controller::initialize(): midiCCManager_ = std::make_unique<MidiCCManager>()
- [x] T052 [US5] Override createContextMenu() in controller.cpp: detect parameter under mouse click, add "MIDI Learn" menu item
- [x] T053 [US5] Add "Clear MIDI Learn" menu item to createContextMenu() when a mapping already exists for the parameter
- [x] T054 [US5] Add "Save Mapping with Preset" checkbox menu item to createContextMenu(): calls addPresetMapping() instead of addGlobalMapping() (covers FR-032a: opt-in per-preset checkbox)
- [x] T055 [US5] Implement MIDI Learn activation in createContextMenu(): call midiCCManager_->startLearn(paramId), set kGlobalMidiLearnActive parameter to 1.0
- [x] T055a [US5] Implement Escape key handling for MIDI Learn cancellation: extend KeyboardShortcutHandler to detect Escape when midiCCManager_->isLearning(), call cancelLearn() and reset kGlobalMidiLearnActive to 0.0 (covers FR-037: cancel via Escape)
- [x] T056 [US5] Extend Controller::getMidiControllerAssignment() to query midiCCManager_->getMidiControllerAssignment() for all mapped CCs
- [x] T057 [US5] Extend Processor::process() to call midiCCManager_->processCCMessage() for incoming MIDI events, invoke callback to update parameter
- [x] T058 [US5] Serialize global mappings in Controller::getState(): call midiCCManager_->serializeGlobalMappings(), write to IBStream
- [x] T059 [US5] Deserialize global mappings in Controller::setState(): read byte buffer from IBStream, call midiCCManager_->deserializeGlobalMappings()
- [x] T060 [US5] Serialize per-preset mappings in Processor::getState(): call midiCCManager_->serializePresetMappings() (passed from controller) (covers FR-032b: per-preset mapping storage)
- [x] T061 [US5] Deserialize per-preset mappings in Processor::setState(): call midiCCManager_->deserializePresetMappings() (covers FR-032b: per-preset mapping restoration)
- [x] T062 [US5] Write integration test in plugins/disrumpo/tests/integration/midi_learn_integration_test.cpp: MIDI Learn workflow, mapping persistence, CC conflict resolution
- [x] T063 [US5] Build and verify all tests pass

---

## Phase 8: User Story 6 - Smooth Panel Animations

**Goal**: Add smooth animation to panel expand/collapse transitions.

**Independent Test**: Expand/collapse panels, verify animation completes in <= 300ms.

**Acceptance Criteria**: FR-005, FR-006

**Note**: This is an enhancement of US1. US1 already implements animation; this phase adds polish and ensures all animation timing requirements are met.

### Tasks

- [x] T064 [US6] Verify AnimatedExpandController animation duration is exactly 250ms (within FR-005's 300ms limit)
- [x] T065 [US6] Add animation smoothness test in plugins/disrumpo/tests/integration/expand_collapse_test.cpp: measure frame timing, verify no jitter
- [x] T066 [US6] Test mid-animation state change with rapid click sequence: verify smooth transition without visual jump
- [x] T067 [US6] Build and verify all tests pass

---

## Phase 9: User Story 7 - High Contrast and Reduced Motion Accessibility

**Goal**: Detect OS accessibility preferences and adapt the UI accordingly.

**Independent Test**: Enable OS high contrast / reduced motion, verify plugin responds with appropriate visual changes.

**Acceptance Criteria**: FR-024 to FR-029

### Tasks

- [x] T068 [US7] Query accessibility preferences in Controller::didOpen(): call queryAccessibilityPreferences(), store result
- [x] T069 [US7] Disable animations when reduced motion is active: call animatedExpandController->setAnimationsEnabled(false) for all band expand controllers
- [x] T070 [US7] Apply high contrast colors to CFrame in Controller::didOpen(): set background color, border colors from AccessibilityPreferences.colors
- [x] T071 [US7] Apply high contrast colors to SpectrumDisplay: increase border width to 2px, use solid fills instead of gradients (covers FR-025a: border widths, solid fills, 4.5:1 contrast)
- [x] T072 [US7] Apply high contrast colors to MorphPad: increase node border width, use high contrast accent color
- [x] T073 [US7] Apply high contrast colors to SweepIndicator: use high contrast accent color for sweep line
- [x] T074 [US7] Apply high contrast colors to all custom views (DynamicNodeSelector, CustomCurveEditor, NodeEditorBorder): increase borders, solid fills
- [x] T075 [US7] Write integration test in plugins/disrumpo/tests/integration/accessibility_test.cpp: verify reduced motion disables animations, high contrast colors applied
- [x] T076 [US7] Measure text contrast ratios in high contrast mode: verify >= 4.5:1 (SC-007)
- [x] T077 [US7] Build and verify all tests pass

---

## Phase 10: User Story 8 - 14-bit MIDI CC for High-Resolution Control

**Goal**: Support 14-bit MIDI CC pairs (MSB+LSB) for 16,384-step parameter resolution.

**Independent Test**: Map a parameter to CC 1 (MSB) and CC 33 (LSB), verify 14-bit resolution (16,384 steps).

**Acceptance Criteria**: FR-038 to FR-040

### Tasks

- [x] T078 [US8] Extend MidiCCManager::processCCMessage() to detect 14-bit CC pairs: when CC 0-31 is received, check for CC+32 LSB
- [x] T079 [US8] Implement 14-bit value combining in MidiCCManager: value = (MSB << 7) | LSB, normalized = value / 16383.0
- [x] T080 [US8] Implement backwards compatibility in MidiCCManager: if only MSB is received (no LSB), use 7-bit value = MSB / 127.0
- [x] T081 [US8] Update MidiCCMapping struct: auto-detect is14Bit when ccNumber is 0-31 and user maps both MSB and LSB
- [x] T082 [US8] Write unit test in plugins/disrumpo/tests/unit/midi_cc_manager_test.cpp: verify 14-bit resolution (16,384 steps), backwards compat with 7-bit
- [x] T083 [US8] Build and verify all tests pass

---

## Phase 11: Polish & Cross-Cutting Concerns

**Goal**: Final integration, cross-platform validation, code quality checks.

### Tasks

- [x] T084 Run full build on Windows: cmake --preset windows-x64-release && cmake --build build/windows-x64-release --config Release
- [x] T085 Run full test suite: ctest --test-dir build/windows-x64-release -C Release --output-on-failure
- [x] T086 [P] Run pluginval on Disrumpo: tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"
- [ ] T087 [P] Run clang-tidy on all new code: ./tools/run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja
- [ ] T088 Fix all clang-tidy warnings and compiler warnings
- [x] T089 Test preset round-trip: save preset with MIDI mappings and window size, load preset, verify all state restored
- [ ] T090 Test cross-platform build on macOS (if available): cmake --preset macos-release && cmake --build build/macos-release --config Release
- [ ] T091 Test cross-platform build on Linux (if available): cmake --preset linux-release && cmake --build build/linux-release --config Release
- [ ] T092 Update specs/_architecture_/controller.md with new visibility controllers and keyboard hook pattern
- [ ] T093 Update specs/_architecture_/shared-infrastructure.md with AccessibilityHelper and MidiCCManager documentation
- [ ] T094 Commit all changes with message: "Progressive disclosure & accessibility (Spec 012)"

---

## Parallel Execution Examples

### Example 1: Foundational Phase Parallelization

```bash
# Terminal 1: Accessibility implementation + tests
# T007-T009, T015

# Terminal 2: MIDI CC Manager implementation + tests
# T010-T014

# Terminal 3: CMake integration
# T005-T006
```

All three tracks are independent and can run simultaneously.

### Example 2: User Story Phase Parallelization

After Foundational phase completes:

```bash
# Terminal 1: US1 (Expand/Collapse)
# T017-T024

# Terminal 2: US2 (Window Resize)
# T025-T032

# Terminal 3: US3 (Modulation Panel Visibility)
# T033-T038

# Terminal 4: US4 (Keyboard Shortcuts)
# T039-T049
```

All four user stories are independent and can be implemented in parallel.

---

## Success Metrics

**Total Tasks**: 95
**User Story Breakdown**:
- Setup: 6 tasks
- Foundational: 10 tasks
- US1 (Expand/Collapse): 8 tasks
- US2 (Window Resize): 8 tasks
- US3 (Modulation Panel Visibility): 6 tasks
- US4 (Keyboard Shortcuts): 11 tasks
- US5 (MIDI CC Mapping): 15 tasks
- US6 (Smooth Animation): 4 tasks
- US7 (Accessibility): 10 tasks
- US8 (14-bit MIDI CC): 6 tasks
- Polish: 11 tasks

**Parallel Opportunities Identified**: 38 tasks marked [P] can run in parallel within their phase

**Independent Test Criteria**:
- US1: Click expand/collapse, verify panel visibility within 300ms
- US2: Drag resize handle, verify 5:3 aspect ratio maintained
- US3: Toggle modulation panel, verify active routings persist
- US4: Press keyboard shortcuts, verify focus/bypass/adjustment
- US5: MIDI Learn workflow, verify mapping created and parameter responds
- US6: Animation timing measurement, verify <= 300ms
- US7: OS accessibility detection, verify UI adapts
- US8: 14-bit CC message, verify 16,384 steps resolution

**Suggested MVP Scope**: Setup + Foundational + US1 + US2 (28 tasks)
- Delivers: animated expand/collapse + resizable window
- Foundation for remaining features
- Independently testable and releasable

---

## Format Validation

All tasks follow the strict checklist format:
- [x] Checkbox prefix: `- [ ]`
- [x] Task ID: Sequential T001-T094
- [x] [P] marker: 38 parallelizable tasks identified
- [x] [Story] label: US1-US8 labels applied to user story tasks
- [x] Description: Clear action with file paths
- [x] Setup/Foundational: No story label (correct)
- [x] Polish phase: No story label (correct)

---

## Constitution Compliance

- **Principle V (VSTGUI)**: All UI via UIDescription and VSTGUI APIs. Animation uses built-in VSTGUI::Animation framework.
- **Principle VI (Cross-Platform)**: Platform-specific code ONLY in accessibility_helper.cpp behind #ifdef guards with graceful fallbacks.
- **Principle VIII (Testing)**: Test-first discipline maintained. Unit tests before implementation, integration tests per user story.
- **Principle XIII (Test-First)**: Canonical todo items followed: Write test → Implement → Verify → Commit.
- **Principle XIV (ODR Prevention)**: All planned classes verified unique via grep searches in research.md.
- **Principle XV (Honest Completion)**: Compliance table in spec.md must be filled before claiming completion.

---

**END OF TASKS**
