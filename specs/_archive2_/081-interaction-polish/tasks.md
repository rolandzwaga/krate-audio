# Tasks: Arpeggiator Interaction Polish (081)

**Input**: Design documents from `specs/081-interaction-polish/`
**Prerequisites**: plan.md, spec.md, data-model.md, research.md, quickstart.md, contracts/ (6 documents)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story. User Stories 1, 2, and 3 are P1; User Stories 4, 5, and 6 are P2; User Story 7 is P3.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow:

1. Write failing tests (no implementation yet)
2. Implement to make tests pass
3. Verify all tests pass
4. Run clang-tidy
5. Commit

Skills auto-load when needed (testing-guide, vst-guide).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Extend shared data types and the IArpLane interface -- these are required by ALL user stories and must be done first.

- [X] T001 Extend `IArpLane` interface in `plugins/shared/src/ui/arp_lane.h` with all Phase 11c methods: `setTrailSteps()`, `setSkippedStep()`, `clearOverlays()`, `getActiveLength()`, `getNormalizedStepValue()`, `setNormalizedStepValue()`, `getLaneTypeId()`, `setTransformCallback()`, `setCopyPasteCallbacks()`, `setPasteEnabled()`
- [X] T002 Add `PlayheadTrailState` struct to `plugins/shared/src/ui/arp_lane.h` (or a new `plugins/shared/src/ui/arp_trail_state.h`) per the data-model definition: `int32_t steps[4]`, `bool skipped[32]`, `advance()`, `clear()`, `markSkipped()`, `clearPassedSkips()` methods
- [X] T003 Add `ClipboardLaneType` enum and `LaneClipboard` struct to `plugins/ruinae/src/controller/controller.h` per the data-model definition: `std::array<float, 32> values`, `int length`, `ClipboardLaneType sourceType`, `bool hasData`, `clear()` method
- [X] T004 Add `TransformType` enum to `plugins/shared/src/ui/arp_lane_header.h`: `kInvert = 0`, `kShiftLeft = 1`, `kShiftRight = 2`, `kRandomize = 3`
- [X] T005 Add stub implementations of the new `IArpLane` pure virtual methods to `ArpLaneEditor` in `plugins/shared/src/ui/arp_lane_editor.h` and `.cpp` (empty bodies that compile -- no logic yet)
- [X] T006 [P] Add stub implementations of the new `IArpLane` pure virtual methods to `ArpModifierLane` in `plugins/shared/src/ui/arp_modifier_lane.h` and `.cpp`
- [X] T007 [P] Add stub implementations of the new `IArpLane` pure virtual methods to `ArpConditionLane` in `plugins/shared/src/ui/arp_condition_lane.h` and `.cpp`
- [X] T008 Build target `shared_tests` (full build, zero errors, zero warnings) to confirm the interface stubs compile: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests`

**Checkpoint**: Phase 1 complete. All 3 lane concrete classes compile with new IArpLane methods stubbed out. No user story work begins until this checkpoint is reached.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Trail rendering infrastructure and skip-event IMessage plumbing -- these underpin User Stories 1 and 2 which are both P1. The ArpLaneHeader transform button scaffold also needs to be in place before US3 implementation.

- [X] T009 Add `ArpLaneHeader` transform button drawing scaffold to `plugins/shared/src/ui/arp_lane_header.h`: declare `drawTransformButtons(CDrawContext*, const CRect&)`, `handleTransformClick(const CPoint&, const CRect&)`, `handleRightClick(const CPoint&, const CRect&, CFrame*)`, `setTransformCallback(TransformCallback)`, `setCopyPasteCallbacks(CopyCallback, PasteCallback)`, `setPasteEnabled(bool)` -- stub implementations only (no draw logic yet)
- [X] T010 Add `editorOpen_` atomic flag (`std::atomic<bool>`) to `plugins/ruinae/src/processor/processor.h` for FR-012 skip event gating; add `skipMessages_` member (`std::array<Steinberg::IPtr<Steinberg::Vst::IMessage>, 6>`) for pre-allocated IMessages
- [X] T011 Pre-allocate skip IMessages in `plugins/ruinae/src/processor/processor.cpp` `initialize()`: loop 0-5, call `allocateMessage()`, set ID `"ArpSkipEvent"` on each, store in `skipMessages_[i]` per the skip-event-imessage.md contract
- [X] T012 Add `handleArpSkipEvent(int lane, int step)` method declaration to `plugins/ruinae/src/controller/controller.h`; add `LaneClipboard clipboard_` member; add `CVSTGUITimer* trailTimer_` member pointer
- [X] T013 Build Ruinae plugin to confirm phase 2 additions compile clean: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`

**Checkpoint**: Foundation ready. Trail timer member, skip IMessage pool, clipboard struct, and ArpLaneHeader scaffold are all in place. P1 user stories (1, 2, 3) can now proceed.

---

## Phase 3: User Story 1 - Playhead Trail with Fading History (Priority: P1)

**Goal**: Each of the 6 lane types renders a fading playhead trail (current + 3 previous steps at decreasing alpha) driven by a shared ~30fps CVSTGUITimer in the controller. Trail clears on transport stop. Collapsed lanes show no trail.

**Independent Test**: Start playback with arp enabled. Verify current step shows bright accent-color overlay; previous 3 steps show progressively dimmer overlays. Stop playback; verify all overlays clear.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T014 [P] [US1] Write failing unit tests for `PlayheadTrailState` in `plugins/shared/tests/ui/playhead_trail_test.cpp`: test `advance()` shifts buffer correctly, test wrap-around at lane boundary, test `clear()` resets all positions and skipped flags, test `markSkipped()` and `clearPassedSkips()`, test trail clamping for lanes with fewer steps than trail length
- [X] T015 [P] [US1] Write failing unit tests for `IArpLane::setTrailSteps()` / `clearOverlays()` in `plugins/shared/tests/ui/playhead_trail_test.cpp` using a mock lane object: verify that setTrailSteps stores the 4 steps and 4 alphas, verify clearOverlays resets all positions to -1

### 3.2 Implementation for User Story 1

- [X] T016 [US1] Implement `PlayheadTrailState::advance()`, `clear()`, `markSkipped()`, `clearPassedSkips()` with full logic (not stubs) in `plugins/shared/src/ui/arp_lane.h` (or `arp_trail_state.h`)
- [X] T017 [US1] Implement `setTrailSteps()` and `clearOverlays()` in `ArpLaneEditor` (`plugins/shared/src/ui/arp_lane_editor.cpp`): store trail data in a `PlayheadTrailState` member; in `draw()`, render a semi-transparent accent-color rect for each trail step using `PlayheadTrailState::kTrailAlphas` ({160, 100, 55, 25} out of 255) from `color_utils.h`; skip trail rendering when `isCollapsed()` is true; handle wrap-around correctly -- trail steps store absolute step indices, render using `stepIndex % laneLength` so a trail spanning step N back to step 1 after wrap displays correctly (FR-004)
- [X] T018 [P] [US1] Implement `setTrailSteps()` and `clearOverlays()` in `ArpModifierLane` (`plugins/shared/src/ui/arp_modifier_lane.cpp`): same trail rendering logic adapted for the dot-grid cell layout; skip when `isCollapsed()` is true (FR-006)
- [X] T019 [P] [US1] Implement `setTrailSteps()` and `clearOverlays()` in `ArpConditionLane` (`plugins/shared/src/ui/arp_condition_lane.cpp`): same trail rendering logic adapted for the icon/pill layout; skip when `isCollapsed()` is true (FR-006)
- [X] T020 [US1] Create the trail timer in `plugins/ruinae/src/controller/controller.cpp` `didOpen()`: instantiate `CVSTGUITimer` at ~33ms interval (30fps), assign to `trailTimer_` (owned by the controller, not by ArpLaneContainer); in the timer callback read each lane's playhead parameter, call `setTrailSteps()` on each `IArpLane*` in the container, then call `clearPassedSkips()` on each lane's trail state to automatically remove skip overlays for steps no longer in the trail window (FR-010), then call `invalid()` on dirty lanes; destroy timer in `willClose()`
- [X] T021 [US1] Wire transport-stop trail clear in `plugins/ruinae/src/controller/controller.cpp`: detect playback stopped (transport state parameter change or polling), call `clearOverlays()` on all 6 lanes
- [X] T022 [US1] Verify all User Story 1 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests && build/windows-x64-release/bin/Release/shared_tests.exe "[trail]"`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T023 [US1] Verify IEEE 754 compliance: check `playhead_trail_test.cpp` for any `std::isnan`/`std::isfinite` usage; if present, add file to `-fno-fast-math` list in `plugins/shared/tests/CMakeLists.txt`

### 3.4 Commit

- [X] T024 [US1] Build Ruinae plugin clean (zero warnings): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T025 [US1] **Commit completed User Story 1 work** (trail timer, trail rendering in all 3 lane classes, PlayheadTrailState, tests)

**Checkpoint**: User Story 1 fully functional, tested, and committed. Trail animates live during playback, clears on stop.

---

## Phase 4: User Story 2 - Skipped Step Indicators (Priority: P1)

**Goal**: When the arp engine skips a step (condition, probability, or rest), the processor sends a pre-allocated `"ArpSkipEvent"` IMessage to the controller. The controller calls `setSkippedStep()` on the corresponding lane, which renders a small X overlay on that step. X clears when the trail overtakes the step or on transport stop.

**Independent Test**: Set step 2 to condition "50%", start playback, verify X appears on step 2 roughly half the cycles. Set a rest flag, verify X appears every cycle on that step.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T026 [P] [US2] Write failing unit tests for skip overlay rendering in `plugins/shared/tests/ui/playhead_trail_test.cpp`: test `setSkippedStep()` sets the correct flag in `PlayheadTrailState::skipped[step]`, test `clearPassedSkips()` removes skip flags for steps no longer in trail, test `clearOverlays()` clears all skip flags, test out-of-range step indices (0-31 valid, 32+ no-op)
- [X] T027 [P] [US2] Write failing unit tests for skip event IMessage attributes in `plugins/ruinae/tests/unit/vst/skip_event_test.cpp`: verify pre-allocated messages have ID `"ArpSkipEvent"`, verify `setInt("lane", x)` and `setInt("step", y)` round-trip correctly, verify lane range validation (0-5), verify step range validation (0-31)

### 4.2 Implementation for User Story 2

- [X] T028 [US2] Before modifying `dsp/include/krate/dsp/processors/arpeggiator_core.h`: search for existing skip event types (`grep -r "kSkip\|Skip\|SKIP" dsp/include/krate/dsp/processors/arpeggiator_core.h`). If a skip event type already exists, reuse it and skip the addition. If it does not exist, add `kSkip` to the ArpEvent type enum and update `processBlock()` to emit `kSkip` events when a step is evaluated but skipped due to condition, probability, or rest flag
- [X] T029 [US2] Implement `sendSkipEvent(int lane, int step)` in `plugins/ruinae/src/processor/processor.cpp`: check `editorOpen_`, retrieve `skipMessages_[lane]`, set `"lane"` and `"step"` int attributes, call `sendMessage()` -- no allocations per the contract in `skip-event-imessage.md`
- [X] T030 [US2] Wire `editorOpen_` flag in processor: receive the `"EditorState"` IMessage from controller (or add this message if it does not exist) and update the atomic bool; check existing IMessage patterns in `processor.cpp:383-447` before adding new patterns
- [X] T031 [US2] Implement `handleArpSkipEvent(int lane, int step)` in `plugins/ruinae/src/controller/controller.cpp`: validate lane (0-5) and step (0-31), call `setSkippedStep(step)` on the corresponding lane, call `invalid()` on the lane's view
- [X] T032 [US2] Wire `"ArpSkipEvent"` in `Controller::notify()` in `plugins/ruinae/src/controller/controller.cpp`: extract `"lane"` and `"step"` int64 attributes, validate ranges, call `handleArpSkipEvent()`
- [X] T033 [US2] Implement `setSkippedStep()` in `ArpLaneEditor` (`plugins/shared/src/ui/arp_lane_editor.cpp`): call `trailState_.markSkipped(step)`, schedule `invalid()`; in `draw()` render a small X glyph (CGraphicsPath, using `brightenColor(accentColor_, 1.3)` at ~80% alpha) centered on the step's cell
- [X] T034 [P] [US2] Implement `setSkippedStep()` in `ArpModifierLane` (`plugins/shared/src/ui/arp_modifier_lane.cpp`): same X glyph rendering logic adapted for the dot-grid cell geometry
- [X] T035 [P] [US2] Implement `setSkippedStep()` in `ArpConditionLane` (`plugins/shared/src/ui/arp_condition_lane.cpp`): same X glyph rendering logic adapted for the icon/pill cell geometry
- [X] T036 [US2] Send `"EditorState"` IMessage from controller `didOpen()` (open=1) and `willClose()` (open=0) in `plugins/ruinae/src/controller/controller.cpp` to keep `editorOpen_` in sync
- [X] T037 [US2] Verify all User Story 2 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests && build/windows-x64-release/bin/Release/shared_tests.exe "[skip]"` and `build/windows-x64-release/bin/Release/ruinae_tests.exe "[skip]"`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T038 [US2] Verify IEEE 754 compliance: check `skip_event_test.cpp` for any `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 4.4 Commit

- [X] T039 [US2] Build Ruinae plugin clean: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T040 [US2] **Commit completed User Story 2 work** (skip IMessage plumbing, ArpeggiatorCore kSkip event, X overlay rendering in all 3 lane classes, tests)

**Checkpoint**: User Story 2 fully functional, tested, and committed. X overlays appear during playback and clear on stop.

---

## Phase 5: User Story 3 - Per-Lane Transform Buttons (Priority: P1)

**Goal**: Each lane header shows 4 small transform buttons (Invert, Shift Left, Shift Right, Randomize) drawn via `ArpLaneHeader`. Each transform applies semantically correct logic per lane type, updates all modified step parameters via `beginEdit`/`performEdit`/`endEdit` for host undo support, and completes within 16ms for 32 steps.

**Independent Test**: Set a known velocity pattern [1.0, 0.5, 0.0, 0.75], click Invert, verify result is [0.0, 0.5, 1.0, 0.25]. Set [A, B, C, D], click Shift Left, verify [B, C, D, A].

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T041 [P] [US3] Write failing unit tests for velocity/gate/pitch/ratchet transform operations in `plugins/shared/tests/ui/transform_test.cpp`: test Invert formula (`new = 1.0 - old`) on each lane type; test Shift Left circular rotation `new[i] = old[(i+1) % N]`; test Shift Right circular rotation `new[i] = old[(i-1+N) % N]`; test single-step no-op for Shift Left/Right; test edge case: 32-step lane all transforms
- [X] T042 [P] [US3] Write failing unit tests for modifier/condition transform operations in `plugins/shared/tests/ui/transform_test.cpp`: test Modifier Invert as bitwise `(~old) & 0x0F`; test Condition Invert using the 18-entry lookup table from `transform-operations.md` (index 0 stays 0, 1<->5, 2<->4, 3 stays, 16<->17); test Randomize for each lane type produces values in the documented valid range
- [X] T043 [P] [US3] Write failing unit tests for `ArpLaneHeader` transform button hit detection in `plugins/shared/tests/ui/transform_test.cpp`: test `handleTransformClick()` with a point in each of the 4 button rects returns the correct `TransformType`, test click outside button area returns false

### 5.2 Implementation for User Story 3

- [X] T044 [US3] Implement `ArpLaneHeader::drawTransformButtons()` in `plugins/shared/src/ui/arp_lane_header.cpp`: draw 4 icon glyphs (12x12px each, 2px gap, right-aligned at `headerRight - 4px`) using CGraphicsPath matching `ActionIconStyle::kInvert`, `kShiftLeft`, `kShiftRight`, `kRegen` respectively; use `darkenColor(laneAccentColor, 0.6)` for button tint
- [X] T045 [US3] Implement `ArpLaneHeader::handleTransformClick()` in `plugins/shared/src/ui/arp_lane_header.cpp`: compute the 4 button rects from `headerRect`, test `where` against each rect, fire `transformCallback_(type)` if hit, return true if consumed; return false otherwise
- [X] T046 [US3] Implement transform operations for `ArpLaneEditor` in `plugins/shared/src/ui/arp_lane_editor.cpp`: in the `setTransformCallback()` wiring, handle `kInvert` (new = 1.0 - old, for velocity/gate/pitch using normalized mirror), `kShiftLeft` (circular rotation), `kShiftRight` (circular rotation), `kRandomize` (uniform float for velocity/gate; snap-to-semitone for pitch; discrete 0/1/2/3 for ratchet); each changed step calls `beginEdit`/`performEdit`/`setParamNormalized`/`endEdit` on the controller per the protocol in `transform-operations.md`
- [X] T047 [US3] Implement transform operations for `ArpModifierLane` in `plugins/shared/src/ui/arp_modifier_lane.cpp`: `kInvert` as `(~bitmask) & 0x0F`; `kShiftLeft`/`kShiftRight` as circular rotation of bitmask values; `kRandomize` as `uniform_int(0, 15)`; apply parameter update protocol per step
- [X] T048 [US3] Implement transform operations for `ArpConditionLane` in `plugins/shared/src/ui/arp_condition_lane.cpp`: `kInvert` using the 18-entry inversion table; `kShiftLeft`/`kShiftRight` as circular rotation; `kRandomize` as `uniform_int(0, 17)`; apply parameter update protocol per step
- [X] T049 [US3] Wire `ArpLaneHeader::setTransformCallback()` in the controller for all 6 lanes in `plugins/ruinae/src/controller/controller.cpp`: each lane's callback calls the appropriate lane's transform implementation via `getNormalizedStepValue()`/`setNormalizedStepValue()` + the VST3 parameter edit protocol
- [X] T050 [US3] Wire `handleTransformClick()` in each lane's `onMouseDown()` override: update `ArpLaneEditor::onMouseDown()`, `ArpModifierLane::onMouseDown()`, `ArpConditionLane::onMouseDown()` to delegate to `header_.handleTransformClick()` before own handling
- [X] T051 [US3] Verify all User Story 3 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests && build/windows-x64-release/bin/Release/shared_tests.exe "[transform]"`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T052 [US3] Verify IEEE 754 compliance: check `transform_test.cpp` for any `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list in `plugins/shared/tests/CMakeLists.txt`

### 5.4 Commit

- [X] T053 [US3] Build Ruinae plugin clean: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T054 [US3] **Commit completed User Story 3 work** (ArpLaneHeader transform buttons draw+hit, transform logic in all 3 lane types, controller wiring, tests)

**Checkpoint**: User Story 3 fully functional, tested, and committed. All 4 transform buttons appear in each lane header and apply correct per-type transformations.

---

## Phase 6: User Story 4 - Copy/Paste Lane Patterns (Priority: P2)

**Goal**: Right-clicking any lane header opens a COptionMenu with Copy and Paste. Copy stores all step values and lane type in `LaneClipboard`. Paste overwrites the target lane, adapts length, performs normalized cross-type mapping. Paste is grayed out when clipboard is empty.

**Independent Test**: Set velocity [1.0, 0.5, 0.0, 0.75] length 4, right-click Copy, right-click Paste on gate lane, verify gate values are [1.0, 0.5, 0.0, 0.75] (same normalized shape, different plain value semantics).

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T055 [P] [US4] Write failing unit tests for copy/paste round-trip in `plugins/shared/tests/ui/copy_paste_test.cpp`: test same-type copy then paste produces bit-identical values (SC-004); test cross-type copy velocity -> gate produces identical normalized values; test cross-type copy pitch -> velocity correctly maps (+24 semitones -> 1.0, 0 semitones -> 0.5, -24 semitones -> 0.0); test `LaneClipboard::clear()` resets hasData
- [X] T056 [P] [US4] Write failing unit tests for clipboard state transitions in `plugins/shared/tests/ui/copy_paste_test.cpp`: test clipboard starts empty (hasData=false); test copy sets hasData=true; test paste with empty clipboard is no-op; test paste updates length to match clipboard length; test length change from 16 to 8 and 8 to 32

### 6.2 Implementation for User Story 4

- [X] T057 [US4] Implement `ArpLaneHeader::handleRightClick()` in `plugins/shared/src/ui/arp_lane_header.cpp`: create a `COptionMenu`, add "Copy" entry (always enabled), add "Paste" entry (enabled only if `pasteEnabled_`), call `popup(frame, where)`, call `forget()` after popup returns; fire `copyCallback_()` or `pasteCallback_()` based on user selection
- [X] T058 [US4] Wire right-click in each lane's `onMouseDown()` override: update `ArpLaneEditor`, `ArpModifierLane`, `ArpConditionLane` to call `header_.handleRightClick()` when `buttons & kRButton` is set, passing `frame` pointer from the lane's `getFrame()` call
- [X] T059 [US4] Implement copy logic in `plugins/ruinae/src/controller/controller.cpp`: `onLaneCopy(int laneIndex)` method reads `getNormalizedStepValue(i)` for each step 0..(getActiveLength()-1), stores in `clipboard_.values`; sets `clipboard_.length = getActiveLength()`; sets `clipboard_.sourceType = static_cast<ClipboardLaneType>(lane->getLaneTypeId())` (FR-022 -- must record source type for cross-type paste identification); sets `clipboard_.hasData = true`; then calls `setPasteEnabled(true)` on all 6 lanes
- [X] T060 [US4] Implement paste logic in `plugins/ruinae/src/controller/controller.cpp`: `onLanePaste(int targetLaneIndex)` method checks `clipboard_.hasData` (no-op if false); for both same-type and cross-type paste, copies `clipboard_.values[i]` directly to the target lane as normalized floats via `setNormalizedStepValue()` + `beginEdit`/`performEdit`/`endEdit` per step (all values are already normalized 0-1 at the VST boundary -- no additional range conversion is performed, per FR-024 and copy-paste.md contract); updates target lane length via the length parameter edit protocol to match `clipboard_.length`
- [X] T061 [US4] Wire copy/paste callbacks in controller `didOpen()` for all 6 lanes in `plugins/ruinae/src/controller/controller.cpp`: call `lane->setCopyPasteCallbacks(copyFn, pasteFn)` and `lane->setPasteEnabled(clipboard_.hasData)` on each lane
- [X] T062 [US4] Verify all User Story 4 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests && build/windows-x64-release/bin/Release/shared_tests.exe "[clipboard]"`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T063 [US4] Verify IEEE 754 compliance: check `copy_paste_test.cpp` for any `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list in `plugins/shared/tests/CMakeLists.txt`

### 6.4 Commit

- [X] T064 [US4] Build Ruinae plugin clean: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T065 [US4] **Commit completed User Story 4 work** (ArpLaneHeader right-click menu, LaneClipboard copy/paste logic in controller, tests)

**Checkpoint**: User Story 4 fully functional, tested, and committed. Right-click copy/paste works between all 6 lane types.

---

## Phase 7: User Story 5 - Euclidean Dual Visualization (Priority: P2)

**Goal**: A new `EuclideanDotDisplay` CView (circular ring of dots) appears in the bottom bar. A linear dot overlay (reusing existing `StepPatternEditor::drawEuclideanDots()` approach) appears in the lane editor when Euclidean mode is enabled. Both update live on knob changes. Both hidden when Euclidean mode is off.

**Independent Test**: Enable Euclidean mode, set Hits=3, Steps=8, Rotation=0. Verify circular display shows 3 filled and 5 outline dots in E(3,8) distribution. Verify linear overlay matches. Adjust Rotation; verify both update.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T066 [P] [US5] Write failing unit tests for `EuclideanDotDisplay` in `plugins/shared/tests/ui/euclidean_dot_display_test.cpp`: test constructor creates view with default values (hits=0, steps=8, rotation=0); test `setHits(3)` with steps=8 generates pattern via `EuclideanPattern::generate(3, 8, 0)` with exactly 3 hit positions; test `setSteps(16)` clamps hits to steps; test `setRotation()` shifts hit positions correctly; test property clamping: hits clamped to [0, steps], steps clamped to [2, 32], rotation clamped to [0, steps-1]; test `setHits(3), setSteps(8), setRotation(0)` produces E(3,8) = hits at steps {0,3,5} (or Bjorklund result for these params)
- [X] T067 [P] [US5] Write failing unit tests for Euclidean pattern consistency in `plugins/shared/tests/ui/euclidean_dot_display_test.cpp`: verify circular display and linear overlay use identical `EuclideanPattern::generate()` call with same hits/steps/rotation values (SC-005)

### 7.2 Implementation for User Story 5

- [X] T068 [US5] Create `EuclideanDotDisplay` class in `plugins/shared/src/ui/euclidean_dot_display.h`: CView subclass, `CLASS_METHODS` macro, properties `hits_`, `steps_`, `rotation_`, `dotRadius_`, `accentColor_`, `outlineColor_`; getter/setter methods with clamping per data-model.md; default values (hits=0, steps=8, rotation=0, dotRadius=3.0f, accentColor={208,132,92,255})
- [X] T069 [US5] Implement `EuclideanDotDisplay::draw()` in `plugins/shared/src/ui/euclidean_dot_display.h` or a `.cpp` file: calculate center and ring radius from view size; loop steps 0..steps-1; compute angle = -PI/2 + 2*PI*i/steps; compute position; call `EuclideanPattern::generate(hits_, steps_, rotation_)` and `EuclideanPattern::isHit(pattern, i, steps_)`; draw filled circle (accentColor) for hits, stroked circle (outlineColor) for non-hits; all via CGraphicsPath
- [X] T070 [US5] Register `EuclideanDotDisplay` with VSTGUI ViewCreator system: create a `EuclideanDotDisplayCreator` class in the same file; register attributes "hits", "steps", "rotation", "accent-color", "dot-radius" with their types; call registration in the shared plugin's ViewCreator registration point
- [X] T071 [US5] Wire `EuclideanDotDisplay` in `plugins/ruinae/src/controller/controller.cpp`: in `didOpen()` find the `EuclideanDotDisplay` CView by tag or name in the editor; on parameter changes for `kArpEuclideanHitsId`, `kArpEuclideanStepsId`, `kArpEuclideanRotationId` call `setHits()`/`setSteps()`/`setRotation()` on the view and `invalid()` it
- [X] T072 [US5] Add linear Euclidean overlay to lane editors in `plugins/shared/src/ui/arp_lane_editor.cpp`: when Euclidean mode is enabled, call `EuclideanPattern::generate()` with current hits/steps/rotation (received from controller via a new setter like `setEuclideanOverlay(int hits, int steps, int rotation, bool enabled)`), draw small filled/outline circles above step bars; hide when disabled
- [X] T073 [US5] Add `setEuclideanOverlay()` method to `IArpLane` interface in `plugins/shared/src/ui/arp_lane.h` and implement stubs in `ArpModifierLane` and `ArpConditionLane` (Euclidean overlay only shown on bar lanes / ArpLaneEditor, but all must implement the interface method)
- [X] T074 [US5] Wire Euclidean enable/disable visibility: in controller, detect `kArpEuclideanEnabledId` changes and call `setEuclideanOverlay(hits, steps, rotation, enabled)` on all lanes; show/hide the EuclideanDotDisplay CViewContainer in uidesc
- [X] T075 [US5] Verify all User Story 5 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests && build/windows-x64-release/bin/Release/shared_tests.exe "[euclidean]"`

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T076 [US5] Verify IEEE 754 compliance: check `euclidean_dot_display_test.cpp` for any `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list in `plugins/shared/tests/CMakeLists.txt`

### 7.4 Commit

- [X] T077 [US5] Build Ruinae plugin clean: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T078 [US5] **Commit completed User Story 5 work** (EuclideanDotDisplay CView, ViewCreator registration, linear overlay in lane editors, controller wiring, tests)

**Checkpoint**: User Story 5 fully functional, tested, and committed. Circular and linear Euclidean overlays both update live and match each other.

---

## Phase 8: User Story 6 - Bottom Bar Generative Controls (Priority: P2)

**Goal**: The bottom bar below the lane container in `editor.uidesc` contains all generative controls (Euclidean section, Humanize, Spice, Ratchet Swing, Dice, Fill) wired to existing parameter IDs. Dice button sends 1.0 then 0.0 in one edit block. Fill is a latching toggle. All controls reflect host automation. Euclidean knobs + dot display show/hide with kArpEuclideanEnabledId.

**Independent Test**: Load plugin, drag Humanize knob to 50%, verify kArpHumanizeId = 0.5 in automation. Click Dice, verify kArpDiceTriggerId spikes to 1.0 then 0.0. Toggle Fill, verify kArpFillToggleId = 1.0 latched.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T079 [P] [US6] Write failing unit tests for Dice trigger behavior in `plugins/ruinae/tests/unit/controller/bottom_bar_test.cpp`: test that the Dice button callback issues `beginEdit(kArpDiceTriggerId)`, then `performEdit(1.0)`, then `performEdit(0.0)`, then `endEdit()` in that exact order; verify parameter does not remain at 1.0 after the sequence (SC-006)
- [X] T080 [P] [US6] Write failing unit tests for Fill toggle latch behavior in `plugins/ruinae/tests/unit/controller/bottom_bar_test.cpp`: test toggle on produces kArpFillToggleId = 1.0; test toggle off produces kArpFillToggleId = 0.0; test that the parameter remains latched between clicks

### 8.2 Implementation for User Story 6

- [X] T081 [US6] Add bottom bar layout to `plugins/ruinae/resources/editor.uidesc`: within the SEQ tab arpeggiator section below ArpLaneContainer, add a ~80px CViewContainer; add Euclidean sub-container with `ToggleButton` (kArpEuclideanEnabledId/3230), `ArcKnob` Hits (3231), `ArcKnob` Steps (3232), `ArcKnob` Rotation (3233), `EuclideanDotDisplay` (60x60px); add Generative section with `ArcKnob` Humanize (3292), `ArcKnob` Spice (3290), `ArcKnob` RatchetSwing (3293); add Performance section with `ActionButton` Dice (3291, 24x24, regen icon), `ToggleButton` Fill (3280, 24x24); assign neutral color `#606068` to all knobs/buttons per FR-041
- [X] T082 [US6] Wire bottom bar control pointers in `plugins/ruinae/src/controller/controller.cpp` `didOpen()`: find each control by tag using the confirmed VSTGUI pattern already established in controller.cpp (check existing `getViewByTagRecursive()` or equivalent calls before using an unverified API); store pointers to bottom bar knob and button controls; read `kArpEuclideanEnabledId` initial normalized value and set Euclidean section visibility accordingly at open time (FR-031: Euclidean section must start hidden if parameter is 0)
- [X] T083 [US6] Implement Dice button handler in `plugins/ruinae/src/controller/controller.cpp`: in the `valueChanged()` callback for the Dice ActionButton's tag `kArpDiceTriggerId`, intercept when the incoming value == 1.0 (ActionButton fires at 1.0 on button press, then returns to 0.0); at that point, call `beginEdit(kArpDiceTriggerId)`, `performEdit(kArpDiceTriggerId, 1.0f)`, `performEdit(kArpDiceTriggerId, 0.0f)`, `endEdit(kArpDiceTriggerId)` -- do NOT rely on the ActionButton resetting to 0.0 on its own; this ensures the processor sees the 0->1 edge in one audio block (per R-008)
- [X] T084 [US6] Implement Euclidean enable/disable visibility toggle in controller: when `kArpEuclideanEnabledId` changes (from host automation or user click), show/hide the Euclidean knobs CViewContainer and call `setEuclideanOverlay(enabled)` on all lanes
- [X] T085 [US6] Verify bottom bar controls update on host automation: in `Controller::setParamNormalized()`, find the corresponding bottom bar control and call `setValue(normalized)` + `invalid()` to keep the visual in sync with automation (FR-038)
- [X] T086 [US6] Verify all User Story 6 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[bottombar]"`; confirm SC-006 coverage: T085 must exercise ALL bottom bar controls (Humanize, Spice, RatchetSwing, Euclidean Hits/Steps/Rotation, Fill) for automation round-trip, not only Dice and Fill

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T087 [US6] Verify IEEE 754 compliance: check `bottom_bar_test.cpp` for any `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 8.4 Commit

- [X] T088 [US6] Build Ruinae plugin clean: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T089 [US6] **Commit completed User Story 6 work** (editor.uidesc bottom bar layout, Dice/Fill/Humanize/Spice/RatchetSwing wiring, Euclidean show/hide, tests)

**Checkpoint**: User Story 6 fully functional, tested, and committed. All bottom bar controls visible and interactive.

---

## Phase 9: User Story 7 - Color Scheme Finalization (Priority: P3)

**Goal**: All 6 lane accent colors (#D0845C copper, #C8A464 sand, #6CA8A0 sage, #9880B0 lavender, #C0707C rose, #7C90B0 slate) are consistently applied across all visual states: expanded bars, collapsed preview, playhead highlight, trail fade levels, and skipped-step X overlay. Bottom bar uses neutral #606068. No visual inconsistencies remain.

**Independent Test**: Visual inspection of all 6 lanes in expanded state with playback active; verify trail fades use the correct hue. Inspect collapsed previews; verify each lane's distinct hue. Inspect X overlays; verify they use brightened/desaturated variants of the lane color.

### 9.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T090 [P] [US7] Write failing unit tests for color derivation consistency in `plugins/shared/tests/ui/color_scheme_test.cpp` (new file): test that `darkenColor(accentColor, factor)` and `brightenColor(accentColor, factor)` from `color_utils.h` produce deterministic results for all 6 accent colors; test that trail alpha values (160/255, 100/255, 55/255, 25/255 per `PlayheadTrailState::kTrailAlphas`) applied to each accent color produce CColor values matching expected RGBA; test that the X overlay color (brightenColor at 1.3 factor, alpha ~204) is computed identically for all 6 accent colors

### 9.2 Implementation for User Story 7

- [X] T091 [US7] Add all 6 lane accent colors and their derived variants as named color entries in `plugins/ruinae/resources/editor.uidesc` color definitions: `"arp.velocity.accent"`, `"arp.gate.accent"`, `"arp.pitch.accent"`, `"arp.ratchet.accent"`, `"arp.modifier.accent"`, `"arp.condition.accent"`, plus dim and fill variants (e.g., `"arp.velocity.dim"`, `"arp.velocity.fill"`) per the color palette in `research.md` (R-010)
- [X] T092 [US7] Audit `ArpLaneEditor::draw()` in `plugins/shared/src/ui/arp_lane_editor.cpp`: verify playhead highlight, trail steps 0-3, and X overlay all use `getAccentColor()` (or lane-type color constant) as the base with the correct alpha derivations; fix any hardcoded colors to use the lane's dynamic accent color
- [X] T093 [P] [US7] Audit `ArpModifierLane::draw()` in `plugins/shared/src/ui/arp_modifier_lane.cpp`: verify all visual states (normal, playhead, trail, skip-X) use the rose (#C0707C) accent color base with correct alpha; fix any inconsistencies
- [X] T094 [P] [US7] Audit `ArpConditionLane::draw()` in `plugins/shared/src/ui/arp_condition_lane.cpp`: verify all visual states use the slate (#7C90B0) accent color base with correct alpha; fix any inconsistencies
- [X] T095 [US7] Audit collapsed miniature preview rendering in all 3 lane classes: verify the miniature preview in collapsed state uses the lane's accent color at dim alpha (~40% alpha), not a generic gray
- [X] T096 [US7] Verify bottom bar controls in `editor.uidesc` use neutral color `#606068` for all knobs and buttons (FR-041); update any entries that have a non-neutral accent color
- [X] T097 [US7] Verify all User Story 7 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests && build/windows-x64-release/bin/Release/shared_tests.exe "[color]"`; additionally perform visual inspection with all 6 lanes rendered simultaneously to verify SC-009 (each lane is identifiable by color alone, no two adjacent lanes confusable); document pass/fail in the compliance table

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T098 [US7] Verify IEEE 754 compliance: check `color_scheme_test.cpp` for any `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list in `plugins/shared/tests/CMakeLists.txt`

### 9.4 Commit

- [X] T099 [US7] Build Ruinae plugin clean: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T100 [US7] **Commit completed User Story 7 work** (uidesc color definitions, color audit + fixes in all 3 lane draw() methods, color derivation tests)

**Checkpoint**: User Story 7 complete. All 6 lanes use consistent color scheme across all visual states.

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Pluginval validation, clang-tidy, full test suite confirmation, and any remaining edge-case polish.

- [X] T101 Run all tests (shared + ruinae + dsp) and confirm zero failures: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target shared_tests ruinae_tests && ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [X] T102 Run Pluginval level 5 on Ruinae: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- must pass clean (FR-044, SC-008)
- [X] T103 Run clang-tidy on all modified source files: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` and `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`; fix all errors; document any intentional suppressions with NOLINT comments
- [X] T104 [P] Verify SC-001 (trail fps): add a frame counter to the timer callback in `plugins/ruinae/src/controller/controller.cpp`, log to debug output, confirm 25-35 invalidation calls per second during active playback
- [X] T105 [P] Verify SC-003 (transform latency): time a 32-step transform call in a test using `std::chrono::high_resolution_clock`; confirm < 16ms completion in `plugins/shared/tests/ui/transform_test.cpp`
- [X] T106 [P] Verify SC-004 (copy/paste round-trip bit-identical): already covered by `copy_paste_test.cpp` T055; confirm this test is in the passing suite
- [X] T107 [P] Verify SC-007 (no heap allocs in draw/mouse/timer paths): run tests under ASan build (`cmake -S . -B build-asan -DENABLE_ASAN=ON && cmake --build build-asan --config Debug && ctest --test-dir build-asan -C Debug`); confirm no heap-use-after-free or unexpected allocations in the new code paths

---

## Phase 11: Architecture Documentation (MANDATORY per Principle XIII)

- [X] T108 Update `specs/_architecture_/` with new components added by this spec: add `EuclideanDotDisplay` entry to the shared UI layer section (file location, properties, ViewCreator attribute names, draw algorithm, "when to use"); add `PlayheadTrailState` entry as a helper struct (purpose, usage pattern, where it lives)
- [X] T109 [P] Update `specs/_architecture_/` to document the `ArpLaneHeader` transform button additions (transform types, hit detection delegation pattern, callback protocol)
- [X] T110 [P] Update `specs/_architecture_/` to document the skip event IMessage pattern (pre-allocation in `initialize()`, `editorOpen_` gate, attribute schema, frequency estimate)
- [X] T111 **Commit architecture documentation updates**

---

## Phase 12: Completion Verification (MANDATORY per Principle XV)

- [X] T112 **Review ALL FR-001 through FR-044** from `specs/081-interaction-polish/spec.md` against actual implementation: open each relevant source file, read the code, confirm the requirement is met; cite file and line number for each
- [X] T113 **Review ALL SC-001 through SC-010**: run or inspect each success criterion; record actual measured values (fps count, latency ms, ASan result) -- do NOT mark complete without actual evidence
- [X] T114 **Search for cheating patterns** in all new code: confirm zero `// placeholder`, `// TODO`, `// stub` comments; confirm no test thresholds relaxed; confirm no features quietly removed
- [X] T115 **Update `specs/081-interaction-polish/spec.md`** Implementation Verification section with compliance table: each FR row cites file path + line, each SC row shows actual measured value vs. threshold
- [X] T116 **Final commit**: commit the filled compliance table and any final adjustments; verify all spec work is on branch `081-interaction-polish`
- [X] T117 **Claim completion ONLY if all FR and SC requirements are honestly verified as MET** -- document any gaps explicitly if they exist

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies. Start immediately.
- **Phase 2 (Foundational)**: Depends on Phase 1. BLOCKS all user stories.
- **Phase 3 (US1 - Trail)**: Depends on Phase 2.
- **Phase 4 (US2 - Skip Indicators)**: Depends on Phase 2. Can run in parallel with Phase 3 after Phase 2 completes, but shares `PlayheadTrailState` so sequential is safer.
- **Phase 5 (US3 - Transform Buttons)**: Depends on Phase 2. Independent of Phases 3 and 4. Can start immediately after Phase 2.
- **Phase 6 (US4 - Copy/Paste)**: Depends on Phase 2. Independent of Phases 3-5.
- **Phase 7 (US5 - Euclidean Display)**: Depends on Phase 2. Independent of Phases 3-6.
- **Phase 8 (US6 - Bottom Bar)**: Depends on Phase 7 (EuclideanDotDisplay must exist). Depends on Phase 2.
- **Phase 9 (US7 - Color Scheme)**: Depends on Phases 3-8 being complete (verifies colors across all new visual elements).
- **Phase 10 (Polish)**: Depends on all user stories complete.
- **Phase 11 (Architecture Docs)**: Depends on Phase 10.
- **Phase 12 (Completion Verification)**: Depends on Phase 11.

### User Story Dependencies

```
Phase 1 (IArpLane stubs)
  |
Phase 2 (Foundation: trail timer member, skip IMsg pool, clipboard struct, header scaffold)
  |
  +-- Phase 3 (US1: Trail)        -- independent after Phase 2
  |
  +-- Phase 4 (US2: Skip X)       -- independent after Phase 2; shares PlayheadTrailState with US1
  |
  +-- Phase 5 (US3: Transforms)   -- independent after Phase 2
  |
  +-- Phase 6 (US4: Copy/Paste)   -- independent after Phase 2
  |
  +-- Phase 7 (US5: Euclidean)    -- independent after Phase 2
        |
        +-- Phase 8 (US6: Bottom Bar) -- depends on US5 for EuclideanDotDisplay
  |
  +-- Phase 9 (US7: Colors)       -- depends on Phases 3-8 complete (audits all draw code)
```

### Parallel Opportunities Within Each Story

- US1: T018 (ArpModifierLane trail) and T019 (ArpConditionLane trail) are [P] parallel after T017 (ArpLaneEditor trail)
- US2: T034 (ArpModifierLane X) and T035 (ArpConditionLane X) are [P] parallel after T033
- US2: T026 and T027 (test writing) are [P] parallel
- US3: T041, T042, T043 (test writing) are [P] parallel; T047 and T048 (transform logic) are [P] parallel
- US4: T055 and T056 (test writing) are [P] parallel
- US5: T066 and T067 (test writing) are [P] parallel
- US7: T093 and T094 (modifier/condition color audit) are [P] parallel

---

## Parallel Example: P1 User Stories After Phase 2

```
Phase 2 COMPLETE
  |
  +-- [Developer A] Phase 3: US1 Trail (T014-T025)
  |
  +-- [Developer B] Phase 5: US3 Transforms (T041-T054)
  |
  +-- [Developer C] Phase 6: US4 Copy/Paste (T055-T065)
```

Phase 4 (US2) touches the same `PlayheadTrailState` as US1 -- best done sequentially after Phase 3.
Phase 7 (US5) and Phase 8 (US6) can run in parallel with Phases 3-6.

---

## Implementation Strategy

### MVP First (P1 Stories Only - Phases 1 through 5)

1. Complete Phase 1: Interface extensions (stubs compile)
2. Complete Phase 2: Foundation (timer member, skip IMsg pool, clipboard struct)
3. Complete Phase 3: US1 Trail -- highest impact visual polish feature
4. Complete Phase 4: US2 Skip Indicators -- essential for condition/probability transparency
5. Complete Phase 5: US3 Transform Buttons -- most-requested workflow accelerator
6. **STOP and VALIDATE**: all P1 features functional, tested, pluginval passes

### Full Delivery (All Stories)

7. Phase 6: US4 Copy/Paste
8. Phase 7: US5 Euclidean Visualization
9. Phase 8: US6 Bottom Bar Controls
10. Phase 9: US7 Color Scheme Finalization
11. Phase 10: Pluginval + clang-tidy + ASan
12. Phase 11: Architecture docs
13. Phase 12: Honest completion verification

---

## Notes

- [P] tasks = can run in parallel (different files, no inter-task dependencies)
- [USn] label maps each task to its user story for traceability
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance after each story
- **MANDATORY**: Commit at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before completion (Principle XIII)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment (Principle XV)
- **NEVER** claim completion if any FR or SC requirement is unverified
- ArpLaneEditor covers 4 of the 6 lane types (Velocity, Gate, Pitch, Ratchet share the same bar renderer); ArpModifierLane covers the Modifier lane; ArpConditionLane covers the Condition lane
- The `editorOpen_` atomic + `"EditorState"` IMessage pattern: check `processor.cpp:383-447` for the existing pattern before creating new messaging infrastructure
- COptionMenu popup is synchronous -- call `forget()` after `popup()` returns to avoid resource leak (documented in plan.md gotchas)
- IMessage instances must be wrapped in `Steinberg::owned()` -- do NOT raw-delete them
