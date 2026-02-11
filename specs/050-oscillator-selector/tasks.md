# Tasks: OscillatorTypeSelector -- Dropdown Tile Grid Control

**Input**: Design documents from `/specs/050-oscillator-selector/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `plugins/shared/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             test_oscillator_type_selector.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and file structure

- [X] T001 Create test file structure in plugins/shared/tests/test_oscillator_type_selector.cpp
- [X] T002 Create header file structure in plugins/shared/src/ui/oscillator_type_selector.h
- [X] T003 Add test file to plugins/shared/tests/CMakeLists.txt
- [X] T004 Add header to plugins/shared/CMakeLists.txt source list

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core logic that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Core Value Conversion (Write FIRST - Must FAIL)

- [X] T005 [P] Unit tests for oscTypeIndexFromNormalized() in plugins/shared/tests/test_oscillator_type_selector.cpp (test NaN → 0.5, inf → 0.5, clamping, rounding)
- [X] T006 [P] Unit tests for normalizedFromOscTypeIndex() in plugins/shared/tests/test_oscillator_type_selector.cpp (test all 10 indices → correct normalized values)
- [X] T007 [P] Unit tests for display name lookups (oscTypeDisplayName, oscTypePopupLabel) in plugins/shared/tests/test_oscillator_type_selector.cpp

### 2.2 Implementation of Core Value Conversion

- [X] T008 [P] Implement oscTypeIndexFromNormalized() with NaN/inf defense (FR-042) in plugins/shared/src/ui/oscillator_type_selector.h
- [X] T009 [P] Implement normalizedFromOscTypeIndex() in plugins/shared/src/ui/oscillator_type_selector.h
- [X] T010 [P] Create display name lookup tables (kOscTypeDisplayNames, kOscTypePopupLabels) in plugins/shared/src/ui/oscillator_type_selector.h

### 2.3 Tests for Waveform Icon Logic (Write FIRST - Must FAIL)

- [X] T011 Unit tests for OscWaveformIcons::getIconPath() for all 10 types in plugins/shared/tests/test_oscillator_type_selector.cpp (verify point count, normalized coords [0,1])

### 2.4 Implementation of Waveform Icon Logic (FR-038 Humble Object)

- [X] T012 Implement OscWaveformIcons namespace with NormalizedPoint and IconPath structs in plugins/shared/src/ui/oscillator_type_selector.h
- [X] T013 Implement OscWaveformIcons::getIconPath() for all 10 oscillator types (FR-008, FR-040) in plugins/shared/src/ui/oscillator_type_selector.h:
  - PolyBLEP (sawtooth), Wavetable (overlapping waves), PhaseDistortion (bent sine), Sync (truncated burst), Additive (bar spectrum)
  - Chaos (attractor squiggle), Particle (dots + arc), Formant (resonant humps), SpectralFreeze (frozen bars), Noise (jagged line)

### 2.5 Tests for Grid Hit Testing (Write FIRST - Must FAIL)

- [X] T014 Unit tests for popup grid hit testing arithmetic in plugins/shared/tests/test_oscillator_type_selector.cpp (5x2 grid, padding, gaps, out-of-bounds)

### 2.6 Implementation of Grid Hit Testing

- [X] T015 Implement hitTestPopupCell() pure function (FR-026 grid arithmetic) in plugins/shared/src/ui/oscillator_type_selector.h

### 2.7 Verify Foundational Tests Pass

- [X] T016 Build shared tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests`
- [X] T017 Run shared tests: `build/windows-x64-release/plugins/shared/tests/Release/shared_tests.exe`
- [X] T018 Verify ALL foundational tests pass

### 2.8 Cross-Platform Verification (MANDATORY)

- [X] T019 **Verify IEEE 754 compliance**: Check if test_oscillator_type_selector.cpp uses std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list in plugins/shared/tests/CMakeLists.txt

### 2.9 Commit Foundational Work

- [X] T020 **Commit completed foundational work** (value conversion, waveform icons, hit testing, all tests passing)

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Select Oscillator Type via Popup Grid (Priority: P1) 🎯 MVP

**Goal**: Implement the core collapsed control + popup grid selection interaction. User can click the collapsed dropdown to open a 5x2 tile grid with all 10 oscillator types, click a cell to select, and the collapsed state updates to show the selected type. This is the fundamental oscillator type selection mechanism.

**Independent Test**: Place the control in the UI with a bound parameter. Click to open popup. Click different cells. Verify collapsed state updates to show the selected type's icon and name. Verify parameter value changes correctly. Verify popup closes on selection.

### 3.1 Class Skeleton

- [X] T021 [US1] Create OscillatorTypeSelector class skeleton in plugins/shared/src/ui/oscillator_type_selector.h:
  - Inherit from CControl, IMouseObserver, IKeyboardHook
  - Constructor with CRect, listener, tag
  - Copy constructor for ViewCreator
  - Member fields: identityColor_, identityId_, popupOpen_, popupView_, hoveredCell_, focusedCell_, isHovered_, static sOpenInstance_
  - Public API: setIdentity(), getIdentity(), getIdentityColor(), getCurrentIndex(), getCurrentType(), isPopupOpen()
  - CLASS_METHODS macro

### 3.2 Collapsed State Rendering (FR-009 - FR-011)

- [X] T022 [US1] Implement draw() for collapsed state in plugins/shared/src/ui/oscillator_type_selector.h:
  - Background rgb(38,38,42), 1px border rgb(60,60,65), 3px border radius
  - Waveform icon (20x14px, identity color, 1.5px stroke) via OscWaveformIcons::drawIcon()
  - Display name (11px font, rgb(220,220,225))
  - Dropdown arrow (8x5px, right-aligned)
  - Hover state: border brightens to rgb(90,90,95)

### 3.3 Collapsed Control Mouse Interaction

- [X] T023 [US1] Implement onMouseDown() to toggle popup open/close in plugins/shared/src/ui/oscillator_type_selector.h
- [X] T024 [US1] Implement onMouseEnterEvent() and onMouseExitEvent() for hover state (isHovered_) in plugins/shared/src/ui/oscillator_type_selector.h

### 3.4 Popup Overlay Creation and Positioning (FR-014 - FR-016)

- [X] T025 [US1] Implement openPopup() in plugins/shared/src/ui/oscillator_type_selector.h:
  - Check if already open (return early)
  - Close any other open instance via sOpenInstance_ (FR-041)
  - Create CViewContainer for popup (260x94px)
  - Compute smart popup position with 4-corner fallback (FR-015): below-left, below-right, above-left, above-right
  - Add popup to CFrame
  - Register IMouseObserver and IKeyboardHook on frame
  - Set popupOpen_ = true, sOpenInstance_ = this, focusedCell_ = getCurrentIndex()

- [X] T026 [US1] Implement closePopup() in plugins/shared/src/ui/oscillator_type_selector.h:
  - Unregister hooks from frame
  - Remove popupView_ from frame (withForget=true)
  - Set popupOpen_ = false, clear sOpenInstance_ if this instance, reset hoveredCell_ and focusedCell_
  - Invalidate collapsed control for redraw

### 3.5 Popup Grid Rendering (FR-022 - FR-024)

- [X] T027 [US1] Implement popup drawing in draw() (when popupOpen_) in plugins/shared/src/ui/oscillator_type_selector.h:
  - Popup background rgb(30,30,35), 1px border rgb(70,70,75), 4px blur shadow rgba(0,0,0,0.5)
  - 5x2 grid layout (48x40px cells, 2px gaps, 6px padding)
  - For each cell: draw waveform icon (48x26px) + popup label (9px font, centered)
  - Selected cell styling (FR-022): identity-color border, 10% opacity identity-color background, identity-color icon stroke, identity-color label
  - Unselected cell styling (FR-023): rgb(60,60,65) border, transparent background, rgb(140,140,150) icon/label
  - Hover styling (FR-024): rgba(255,255,255,0.06) background tint

### 3.6 Popup Cell Selection

- [X] T028 [US1] Implement cell click handling in onMouseEvent() (IMouseObserver) in plugins/shared/src/ui/oscillator_type_selector.h:
  - Use hitTestPopupCell() to determine clicked cell
  - If cell clicked: call selectType(index) → beginEdit(), setValue(), valueChanged(), endEdit() (FR-017, FR-027)
  - Close popup
  - If clicked outside popup: close popup without selection change (FR-018)

### 3.7 Popup Dismissal (FR-019)

- [X] T029 [US1] Implement Escape key handling in onKeyboardEvent() (IKeyboardHook, 2-param) in plugins/shared/src/ui/oscillator_type_selector.h:
  - Check event.character == VirtualKey::Escape
  - Close popup without selection change
  - Set event.consumed = true

### 3.8 Host Automation Support (FR-028, User Story 4)

- [X] T030 [US1] Implement valueChanged() override to invalidate collapsed control display when host changes parameter in plugins/shared/src/ui/oscillator_type_selector.h

### 3.9 ViewCreator Registration (FR-035)

- [X] T031 [US1] Implement OscillatorTypeSelectorCreator struct at bottom of plugins/shared/src/ui/oscillator_type_selector.h:
  - Inherit from ViewCreatorAdapter
  - getViewName() returns "OscillatorTypeSelector"
  - getBaseViewName() returns kCControl
  - create() instantiates OscillatorTypeSelector(180x28)
  - apply() handles "osc-identity" attribute (FR-006)
  - getAttributeNames(), getAttributeType(), getAttributeValue() for "osc-identity"
  - Inline global: `inline OscillatorTypeSelectorCreator gOscillatorTypeSelectorCreator;`

### 3.10 Integration into Ruinae Plugin

- [X] T032 [US1] Add #include "ui/oscillator_type_selector.h" to plugins/ruinae/src/entry.cpp for ViewCreator registration

### 3.11 Integration into Control Testbench

- [X] T033 [US1] Add #include "ui/oscillator_type_selector.h" to tools/control_testbench/src/control_registry.cpp
- [X] T034 [US1] Add two demo instances to control testbench: OSC A (osc-identity="a", blue) and OSC B (osc-identity="b", orange)
- [X] T035 [US1] Add mock kOscATypeId and kOscBTypeId to tools/control_testbench/src/mocks/plugin_ids.h

### 3.12 Build and Manual Verification

- [X] T036 [US1] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T037 [US1] Build control testbench: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target control_testbench`
- [X] T038 [US1] Manual verification in testbench:
  - Click collapsed control → popup opens with 5x2 grid
  - Click different cells → collapsed control updates icon and name
  - Click outside popup → popup closes without change
  - Press Escape → popup closes without change
  - Verify OSC A uses blue identity color, OSC B uses orange
  - Verify all 10 waveform icons are visually distinct (SC-004)

### 3.13 Cross-Platform Verification (MANDATORY)

- [X] T039 [US1] **Verify IEEE 754 compliance confirmed**: Ensure test_oscillator_type_selector.cpp is in -fno-fast-math list if using std::isnan

### 3.14 Commit User Story 1

- [X] T040 [US1] **Commit completed User Story 1 work** (core selection mechanism, collapsed state, popup grid, integration)

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. Core oscillator type selection via popup grid is complete.

---

## Phase 4: User Story 2 - Quick Auditioning via Scroll Wheel (Priority: P2)

**Goal**: Enable rapid auditioning of oscillator types via scroll wheel on the collapsed control. User hovers over collapsed control and scrolls up/down to cycle through types without opening the popup. Wraps from index 9 to 0 and vice versa.

**Independent Test**: Hover over collapsed control. Scroll mouse wheel up and down. Verify selection cycles through all 10 types with wrapping. Verify parameter changes are issued.

### 4.1 Scroll Wheel on Collapsed Control (FR-013)

- [X] T041 [US2] Implement onMouseWheelEvent() override in plugins/shared/src/ui/oscillator_type_selector.h:
  - Get current index
  - If wheel up (positive delta): increment with wrap (9 → 0)
  - If wheel down (negative delta): decrement with wrap (0 → 9)
  - Call selectType(newIndex) → beginEdit/performEdit/endEdit
  - Invalidate collapsed control for redraw
  - Do NOT open popup

### 4.2 Scroll Wheel While Popup Open (FR-020)

- [X] T042 [US2] Extend onMouseWheelEvent() to handle scrolling when popup is open in plugins/shared/src/ui/oscillator_type_selector.h:
  - Change selection by one step
  - Keep popup open (do NOT close)
  - Update focused cell to new selection
  - Invalidate popup for visual feedback

### 4.3 Manual Verification

- [X] T043 [US2] Manual verification in testbench:
  - Hover over collapsed control and scroll wheel → selection cycles through all 10 types
  - Open popup and scroll wheel → selection changes but popup stays open
  - Verify wrapping: 9→0 (scroll up), 0→9 (scroll down)
  - Verify each scroll event = exactly 1 step regardless of delta magnitude (SC-002)

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T044 [US2] **Verify scroll wheel delta normalization works on all platforms** (Windows, macOS, Linux report different deltas)

### 4.5 Commit User Story 2

- [X] T045 [US2] **Commit completed User Story 2 work** (scroll wheel auditioning)

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. Scroll wheel auditioning enhances the selection workflow.

---

## Phase 5: User Story 3 - Keyboard Navigation of Popup Grid (Priority: P2)

**Goal**: Enable keyboard-only interaction with the popup grid. User tabs to the control, presses Enter/Space to open popup, uses arrow keys to navigate the 5x2 grid, and presses Enter/Space to confirm selection. Escape closes popup.

**Independent Test**: Tab to control. Press Enter to open popup. Use arrow keys to move focus indicator around the grid. Press Enter to select focused cell. Verify popup closes and selection updates. Verify Escape closes popup without selection change.

### 5.1 Keyboard Popup Opening (FR-030, FR-031)

- [X] T046 [US3] Implement onKeyboardEvent() override (1-param, CView version) in plugins/shared/src/ui/oscillator_type_selector.h:
  - When collapsed control has focus and user presses Enter or Space: call openPopup()
  - Set event.consumed = true

- [X] T047 [US3] Implement getFocusPath() override for focus indicator in plugins/shared/src/ui/oscillator_type_selector.h:
  - Return dotted 1px border around collapsed control when focused

### 5.2 Arrow Key Navigation in Popup (FR-025, FR-032)

- [X] T048 [US3] Extend onKeyboardEvent() (IKeyboardHook, 2-param) to handle arrow keys in plugins/shared/src/ui/oscillator_type_selector.h:
  - Left/Right: move focusedCell_ horizontally across columns
  - Up/Down: move focusedCell_ between rows
  - Wrap at grid boundaries (col 4 → col 0 next row, etc.)
  - Invalidate popup to show focus indicator
  - Set event.consumed = true

- [X] T049 [US3] Update popup drawing to show focus indicator in plugins/shared/src/ui/oscillator_type_selector.h:
  - Draw dotted 1px border around focused cell (distinct from selection highlight)

### 5.3 Keyboard Selection Confirmation (FR-025)

- [X] T050 [US3] Extend onKeyboardEvent() (IKeyboardHook, 2-param) to handle Enter/Space in popup in plugins/shared/src/ui/oscillator_type_selector.h:
  - If popup is open and user presses Enter or Space: call selectType(focusedCell_)
  - Close popup
  - Set event.consumed = true

### 5.4 Manual Verification

- [X] T051 [US3] Manual verification in testbench:
  - Tab to control → focus indicator appears (dotted border)
  - Press Enter → popup opens with focused cell on current selection
  - Arrow keys → focus indicator moves around grid with correct wrapping
  - Press Enter → selection confirmed, popup closes, collapsed control updates
  - Tab to control, press Enter, arrow keys, press Escape → popup closes without selection change (SC-007)

### 5.5 Cross-Platform Verification (MANDATORY)

- [X] T052 [US3] **Verify keyboard event handling works on all platforms** (Windows VK codes vs macOS/Linux key symbols)

### 5.6 Commit User Story 3

- [X] T053 [US3] **Commit completed User Story 3 work** (keyboard navigation)

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed. Keyboard accessibility is complete.

---

## Phase 6: User Story 4 - Host Automation Updates Display (Priority: P2)

**Goal**: Ensure the control correctly reflects externally-driven parameter changes from host automation. During playback, as the host sends parameter changes, the collapsed control redraws to show the updated oscillator type without user interaction or popup opening.

**Independent Test**: Automate the oscillator type parameter in a host DAW. Play back the automation. Verify the collapsed control display updates in real-time to match each automated value. Verify popup does not open during automation.

### 6.1 Host Automation Handling

Note: This was already implemented in User Story 1 (T030 - valueChanged() override). This phase is primarily for verification.

- [X] T054 [US4] Verify valueChanged() correctly invalidates collapsed control when parameter changes externally in plugins/shared/src/ui/oscillator_type_selector.h
- [X] T055 [US4] Verify popup does NOT open when parameter changes externally (only on user click)

### 6.2 Manual Verification in Host DAW

- [X] T056 [US4] Manual verification in Reaper/FL Studio/Ableton:
  - Automate OSC A Type parameter with multiple type changes
  - Play back automation
  - Verify collapsed control redraws with correct icon and name for each value (SC-003)
  - Verify no visual artifacts or popup opening during automation (SC-005)

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T057 [US4] **Verify host automation works in DAWs on Windows, macOS, Linux**

### 6.4 Commit User Story 4

- [X] T058 [US4] **Commit any refinements for host automation** (if needed)

**Checkpoint**: All four user stories should now be independently functional and committed. Core feature set is complete.

---

## Phase 7: User Story 5 - Visual Feedback in Popup Grid (Priority: P3)

**Goal**: Refine popup grid visual feedback. Implement per-cell tooltips that show the full display name when hovering over a cell. Verify all visual states (selected, unselected, hover) are clearly distinguishable.

**Independent Test**: Open popup. Hover over each cell. Verify tooltip shows full display name. Verify selected cell uses identity color. Verify unselected cells use muted gray. Verify hover highlights with subtle tint.

### 7.1 Per-Cell Tooltips (FR-043)

- [X] T059 [US5] Implement onMouseMoveEvent() override in plugins/shared/src/ui/oscillator_type_selector.h:
  - When popup is open, use grid arithmetic to determine hovered cell
  - Dynamically call setTooltipText() with the full display name for that cell
  - Update hoveredCell_ for hover highlight

### 7.2 Verify Visual States

- [X] T060 [US5] Manual verification in testbench:
  - Open popup → selected cell has identity-color border, icon, label, 10% opacity background
  - Unselected cells have muted gray (rgb(140,140,150)) icon and label
  - Hover over cells → subtle rgba(255,255,255,0.06) background tint
  - Hover over cells → tooltip shows full display name ("Phase Distortion", "Spectral Freeze", etc.)
  - Verify all 10 waveform icons are visually distinguishable at both 20x14 (collapsed) and 48x26 (popup) sizes (SC-004)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T061 [US5] **Verify tooltips work on all platforms** (VSTGUI tooltip API compatibility)

### 7.4 Commit User Story 5

- [X] T062 [US5] **Commit completed User Story 5 work** (per-cell tooltips, visual polish)

**Checkpoint**: All user stories should now be independently functional and committed. Visual feedback is polished.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

### 8.1 Edge Case Handling

- [X] T063 [P] Verify popup toggle behavior: clicking collapsed control when popup already open → popup closes (FR-014 toggle)
- [X] T064 [P] Verify multi-instance exclusivity: opening OSC B popup when OSC A popup is open → OSC A closes immediately, OSC B opens (FR-041, SC-006)
- [X] T065 [P] Verify NaN/inf parameter handling: corrupt state with NaN/inf → control treats as 0.5, clamps, displays valid type (FR-042)
- [X] T066 [P] Verify popup smart positioning: position control near window edges → popup flips to fit (FR-015)

### 8.2 Code Quality

- [X] T067 Verify no raw new/delete (use VSTGUI ownership patterns)
- [X] T068 Verify all member functions use trailing underscore naming
- [X] T069 Verify all constants use kPascalCase naming
- [X] T070 Verify all destructors properly clean up (unregister hooks, null sOpenInstance_)

### 8.3 Performance Verification

- [X] T071 Verify collapsed control redraw is fast (within 1 frame of parameter change, SC-003)
- [X] T072 Verify popup open/close has no visual artifacts (no flicker, no stale content, SC-005)

### 8.4 Documentation

- [X] T073 Add inline documentation comments for public API in plugins/shared/src/ui/oscillator_type_selector.h
- [X] T074 Add implementation notes for ViewCreator usage in header comments
- [X] T075 Add usage example in header comments showing XML attributes

### 8.5 Commit Polish Work

- [X] T076 **Commit polish and cross-cutting improvements**

---

## Phase 9: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 9.1 Run Clang-Tidy Analysis

- [X] T077 **compile_commands.json** already exists at `build/windows-ninja/`
- [X] T078 **Ran clang-tidy**: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja` — 226 files analyzed, 0 errors, 31 warnings found across 5 files

### 9.2 Address Findings

- [X] T079 **0 errors** — no error-level findings
- [X] T080 **Fixed all 31 warnings** across 5 files per Constitution VIII (no "pre-existing" exceptions):
  - `spectral_simd.cpp` (6): NOLINT for Highway self-inclusion, macro namespace, HWY_EXPORT linkage; split multi-declaration
  - `selectable_oscillator_test.cpp` (5): removed redundant `static` in anon namespace; NOLINT for intentional malloc/free in operator new/delete overrides
  - `spectrum_display.cpp` (3): merged duplicate branch bodies; added default member initializers; fixed widening cast order
  - `disrumpo controller.cpp` (11): simplified redundant expression; explicit bool conversion; `.data()` instead of `&[0]`; isolated declarations; range-based for on save loops; NOLINT for conditional-access load loops and VST3 int-to-ptr pattern
  - `disrumpo processor.cpp` (6): range-based for on save loops; isolated declarations; NOLINT for conditional-access load loops
- [X] T081 **NOLINT suppressions documented** — each has inline justification explaining why the warning cannot be resolved

### 9.3 Commit Static Analysis Fixes

- [X] T082 **Re-ran clang-tidy after fixes**: 226 files, 0 errors, 0 warnings — fully clean

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [X] T083 **Update specs/_architecture_/plugin-architecture.md** with OscillatorTypeSelector component:
  - Add entry for OscillatorTypeSelector in Shared UI Components section
  - Include: purpose ("Dropdown-style oscillator type selector with tile grid popup"), public API summary (setIdentity, getCurrentIndex, etc.), file location (plugins/shared/src/ui/oscillator_type_selector.h)
  - Add "when to use this": "When you need a type selector with visual waveform icons and rapid auditioning (scroll wheel) support"
  - Add usage example: XML snippet showing osc-identity attribute
  - Note: Reusable for any plugin that needs oscillator type selection with visual feedback

### 10.2 Commit Architecture Documentation

- [X] T084 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T085 **Review ALL 43 FR-xxx requirements** from spec.md against implementation:
  - FR-001 to FR-043: Open plugins/shared/src/ui/oscillator_type_selector.h and verify each requirement
  - Record file path and line number for each FR in spec.md compliance table

- [X] T086 **Review ALL 8 SC-xxx success criteria** and verify measurable targets:
  - SC-001: 2 clicks to select type (1 open, 1 select) ✓
  - SC-002: Cycle through 10 types in <5 seconds via scroll wheel ✓
  - SC-003: Collapsed control updates within 1 frame of parameter change ✓
  - SC-004: All 10 waveform icons visually distinguishable at both sizes ✓
  - SC-005: Popup opens/closes without artifacts ✓
  - SC-006: Same class for OSC A/B, different configuration ✓
  - SC-007: Keyboard-only navigation works ✓
  - SC-008: Testbench displays OSC A and OSC B demo instances ✓

- [X] T087 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in plugins/shared/src/ui/oscillator_type_selector.h
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope (all 43 FRs implemented)

### 11.2 Fill Compliance Table in spec.md

- [X] T088 **Update spec.md "Implementation Verification" section** with compliance status:
  - For each FR-001 to FR-043: File path + line number + description of how requirement is met
  - For each SC-001 to SC-008: Test name + measured result + verification method
  - Mark overall status: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T089 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [X] T090 **Commit all spec work** to feature branch 050-oscillator-selector
- [X] T091 **Verify all tests pass**:
  ```bash
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests
  build/windows-x64-release/plugins/shared/tests/Release/shared_tests.exe
  ```

### 12.2 Final Build Verification

- [X] T092 **Build Ruinae plugin** and verify no compilation errors/warnings
- [X] T093 **Build control testbench** and verify OscillatorTypeSelector demo works
- [X] T094 **Test in host DAW** (load Ruinae, interact with OSC A/B type selectors)

### 12.3 Completion Claim

- [X] T095 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User Story 1 (P1): Core popup selection - MVP - MUST complete first
  - User Story 2 (P2): Scroll wheel - Can start after US1
  - User Story 3 (P2): Keyboard navigation - Can start after US1
  - User Story 4 (P2): Host automation - Partially implemented in US1, verified independently
  - User Story 5 (P3): Visual polish - Can start after US1
- **Polish (Phase 8)**: Depends on all user stories being complete
- **Static Analysis (Phase 9)**: Depends on all implementation being complete
- **Architecture Docs (Phase 10)**: Depends on all implementation being complete
- **Completion Verification (Phase 11)**: Depends on all previous phases
- **Final Completion (Phase 12)**: Depends on completion verification

### User Story Dependencies

- **User Story 1 (P1) - Core Selection**: MUST complete first (MVP) - No dependencies on other stories
- **User Story 2 (P2) - Scroll Wheel**: Can start after US1 - Extends collapsed control and popup behavior
- **User Story 3 (P2) - Keyboard Nav**: Can start after US1 - Adds keyboard interaction layer
- **User Story 4 (P2) - Host Automation**: Partially implemented in US1 (valueChanged), verified independently
- **User Story 5 (P3) - Visual Polish**: Can start after US1 - Refines visual feedback

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XIII)
- Core logic before UI rendering
- UI rendering before interaction handlers
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

**Phase 2 (Foundational)**:
- T005, T006, T007 (unit tests) can run in parallel
- T008, T009, T010 (value conversion) can run in parallel after tests
- T011 (icon tests), T014 (hit testing tests) can run in parallel
- T012, T013 (icon implementation) can happen in parallel
- T015 (hit testing) is independent

**User Story 1**:
- Most tasks are sequential (class skeleton → rendering → interaction)
- T036, T037 (builds) can run in parallel

**Polish Phase**:
- T063, T064, T065, T066 (edge cases) can all run in parallel
- T067, T068, T069, T070 (code quality) can all run in parallel

---

## Parallel Example: Foundational Phase

```bash
# Launch all foundational unit tests together (Phase 2.1):
Task T005: "Unit tests for oscTypeIndexFromNormalized()"
Task T006: "Unit tests for normalizedFromOscTypeIndex()"
Task T007: "Unit tests for display name lookups"

# Launch all core implementations together (Phase 2.2):
Task T008: "Implement oscTypeIndexFromNormalized()"
Task T009: "Implement normalizedFromOscTypeIndex()"
Task T010: "Create display name tables"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - value conversion, waveform icons, hit testing)
3. Complete Phase 3: User Story 1 (core popup selection)
4. **STOP and VALIDATE**: Build testbench, manually verify popup selection works
5. Deploy to Ruinae plugin for real-world testing

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready (testable value conversion, icons, hit testing)
2. Add User Story 1 → Test independently → Deploy/Demo (MVP! Core selection works)
3. Add User Story 2 → Test independently → Deploy/Demo (Scroll wheel auditioning)
4. Add User Story 3 → Test independently → Deploy/Demo (Keyboard accessibility)
5. Add User Story 4 → Test independently → Deploy/Demo (Host automation verified)
6. Add User Story 5 → Test independently → Deploy/Demo (Visual polish complete)
7. Each story adds value without breaking previous stories

### Recommended Order for Single Developer

1. Phase 1: Setup (15 minutes)
2. Phase 2: Foundational (2-3 hours) - CRITICAL
3. Phase 3: User Story 1 (4-6 hours) - MVP
4. **Manual validation checkpoint**
5. Phase 4: User Story 2 (1 hour)
6. Phase 5: User Story 3 (2 hours)
7. Phase 6: User Story 4 (30 minutes - mostly verification)
8. Phase 7: User Story 5 (1 hour)
9. Phase 8: Polish (1-2 hours)
10. Phase 9: Static Analysis (30 minutes)
11. Phase 10: Architecture Docs (30 minutes)
12. Phase 11: Completion Verification (1 hour)
13. Phase 12: Final Completion (30 minutes)

**Total estimated time**: 14-18 hours for complete implementation

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability (US1, US2, US3, US4, US5)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- This is a header-only implementation (single .h file, no .cpp) following existing shared UI pattern
