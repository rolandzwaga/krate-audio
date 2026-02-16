# Tasks: Settings Drawer

**Input**: Design documents from `/specs/058-settings-drawer/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/parameter-ids.md, quickstart.md, research.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation for parameter handling and state persistence. Note: Drawer UI animation tests (gear toggle, click-outside dismiss, animation interruption) are performed as manual verification tasks (T059-T063) because the project lacks automated UI testing infrastructure.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task Group

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Build**: Compile with zero warnings
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Parameter IDs and UI Action Tags)

**Purpose**: Define parameter ID ranges (2200-2205), UI action tags (10020-10021), bump kNumParameters sentinel

**Goal**: Parameter IDs allocated, action tags defined, ready for parameter file creation

### 1.1 Implement Parameter ID Definitions

- [X] T001 Add settings parameter IDs (2200-2205) and range sentinel IDs to F:\projects\iterum\plugins\ruinae\src\plugin_ids.h after kRunglerEndId (line 2199): kSettingsBaseId=2200, kSettingsPitchBendRangeId=2200, kSettingsVelocityCurveId=2201, kSettingsTuningReferenceId=2202, kSettingsVoiceAllocModeId=2203, kSettingsVoiceStealModeId=2204, kSettingsGainCompensationId=2205, kSettingsEndId=2299
- [X] T002 Add UI action tags for drawer after existing UI action tags: kActionSettingsToggleTag=10020 (gear icon toggle), kActionSettingsOverlayTag=10021 (click-outside overlay)
- [X] T003 Update kNumParameters sentinel from 2200 to 2300 in plugin_ids.h
- [X] T004 Update ID range allocation comment block at top of ParameterIDs enum to document 2200-2299 (Settings: Pitch Bend Range, Velocity Curve, Tuning Ref, Alloc Mode, Steal Mode, Gain Comp)

### 1.2 Build & Verify

- [X] T005 Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T006 Verify zero compiler warnings for plugin_ids.h changes

### 1.3 Commit

- [X] T007 Commit Phase 1 work: Parameter IDs and UI action tags defined

**Checkpoint**: Parameter IDs 2200-2205 defined, action tags 10020-10021 defined, kNumParameters=2300, ready for parameter file creation

---

## Phase 2: Parameter Infrastructure (No UI Yet)

**Purpose**: Create settings_params.h with SettingsParams struct and 6 inline functions (handle/register/format/save/load), wire to processor and controller

**Goal**: Settings parameters registered, handled, saved/loaded, but no UI controls yet (not accessible to user)

### 2.1 Write Tests for Settings Parameters

- [X] T008 [P] Write test for settings param handling in F:\projects\iterum\plugins\ruinae\tests\unit\processor\processor_test.cpp: create test case "Settings parameter changes update engine", send kSettingsPitchBendRangeId param change (value 0.5 = 12 semitones), call applyParamsToEngine, verify engine pitch bend range is 12.0f (will FAIL - settings_params.h not created yet)
- [X] T009 [P] Write test for settings state persistence in F:\projects\iterum\plugins\ruinae\tests\unit\processor\state_persistence_test.cpp: create test case "Settings params save and load", set non-default values (pitchBendRange=7, velocityCurve=2/Hard, tuning=432Hz, allocMode=0/RoundRobin, stealMode=1/Soft, gainComp=true), save state, reset values to defaults, load state, verify values restored (will FAIL - no save/load functions yet)

### 2.2 Create settings_params.h Parameter File

- [X] T010 [US2] [US3] Create new file F:\projects\iterum\plugins\ruinae\src\parameters\settings_params.h with header guards, includes (Steinberg headers, atomic, clamp, cmath for round)
- [X] T011 [US2] [US3] Define SettingsParams struct in settings_params.h with 6 atomic fields: pitchBendRangeSemitones (default 2.0f), velocityCurve (default 0), tuningReferenceHz (default 440.0f), voiceAllocMode (default 1=Oldest), voiceStealMode (default 0=Hard), gainCompensation (default true)
- [X] T012 [US2] [US3] Implement handleSettingsParamChange() function in settings_params.h: switch on paramId, denormalize using appropriate mappings (pitch bend: round(norm*24), velocity curve: round(norm*3), tuning: 400+norm*80, alloc mode: round(norm*3), steal mode: round(norm*1), gain comp: norm>=0.5), store clamped values
- [X] T013 [US2] [US3] Implement registerSettingsParams() function in settings_params.h: register Pitch Bend Range (stepCount=24, default 2/24=0.0833, unit "st"), Velocity Curve (StringListParameter via createDropdownParameter with 4 items: Linear/Soft/Hard/Fixed), Tuning Reference (stepCount=0, default 0.5, unit "Hz"), Voice Allocation (StringListParameter via createDropdownParameterWithDefault with 4 items: RoundRobin/Oldest/LowestVelocity/HighestNote, defaultIndex=1), Voice Steal (StringListParameter via createDropdownParameter with 2 items: Hard/Soft), Gain Compensation (stepCount=1, default 1.0 = ON)
- [X] T014 [US2] Implement formatSettingsParam() function in settings_params.h: format pitch bend range as "X st" (integer), tuning reference as "XXX.X Hz" (1 decimal), return kResultFalse for dropdown IDs (framework handles them), return kResultFalse for non-settings IDs
- [X] T015 [US2] [US3] [US4] Implement saveSettingsParams() function in settings_params.h: write pitchBendRangeSemitones, velocityCurve, tuningReferenceHz, voiceAllocMode, voiceStealMode, gainCompensation (4 floats, 2 int32s for enum indices, 1 int32 for bool) using streamer.writeFloat/Int32
- [X] T016 [US2] [US3] [US4] Implement loadSettingsParams() function in settings_params.h: read 4 floats and 3 int32s in same order, store to settingsParams fields, return false on read failure (EOF-safe)
- [X] T017 [US2] [US3] [US4] Implement loadSettingsParamsToController() function in settings_params.h: read 6 values, apply inverse mappings (pitch bend: semitones/24, velocity curve: index/3, tuning: (hz-400)/80, alloc mode: index/3, steal mode: index/1, gain comp: bool to 0.0 or 1.0), call setParam for each of 6 IDs

### 2.3 Wire Settings Parameters to Processor

- [X] T018 [US2] [US3] Add `#include "parameters/settings_params.h"` in F:\projects\iterum\plugins\ruinae\src\processor\processor.h after other param includes
- [X] T019 [US2] [US3] Add `SettingsParams settingsParams_;` field to Processor class in processor.h after runglerParams_ field
- [X] T020 [US2] Bump kCurrentStateVersion from 13 to 14 in processor.h (line 69)
- [X] T021 [US2] Add settings param handling case to processParameterChanges() in F:\projects\iterum\plugins\ruinae\src\processor\processor.cpp after rungler block: `} else if (paramId >= kSettingsBaseId && paramId <= kSettingsEndId) { handleSettingsParamChange(settingsParams_, paramId, value); }`
- [X] T022 [US2] [US3] Add settings forwarding to applyParamsToEngine() in processor.cpp after Rungler section: call engine_.setPitchBendRange(pitchBendRangeSemitones.load), engine_.setVelocityCurve(static_cast<VelocityCurve>(velocityCurve.load)), engine_.setTuningReference(tuningReferenceHz.load), engine_.setAllocationMode(static_cast<AllocationMode>(voiceAllocMode.load)), engine_.setStealMode(static_cast<StealMode>(voiceStealMode.load)), engine_.setGainCompensationEnabled(gainCompensation.load)
- [X] T023 [US2] [US3] Remove hardcoded `engine_.setGainCompensationEnabled(false);` from processor.cpp line 117 (gain comp now parameter-driven)
- [X] T024 [US2] [US3] [US4] Add saveSettingsParams(settingsParams_, streamer) to getState() in processor.cpp after v13 macro/rungler params, preceded by comment "// v14: Settings params"
- [X] T025 [US2] [US3] [US4] Add settings loading to setState() in processor.cpp: inside `if (version >= 14)` block after v13 loading, call loadSettingsParams(settingsParams_, streamer); in `else` block for version < 14, explicitly set backward-compatible defaults (pitchBendRange=2, velocityCurve=0, tuning=440, allocMode=1, stealMode=0, gainCompensation=false to match pre-spec behavior)

### 2.4 Wire Settings Parameters to Controller

- [X] T026 [US2] [US3] Add `#include "parameters/settings_params.h"` in F:\projects\iterum\plugins\ruinae\src\controller\controller.cpp after other param includes
- [X] T027 [US2] [US3] Add registerSettingsParams(parameters) call to Controller::initialize() in controller.cpp after registerRunglerParams
- [X] T028 [US2] [US3] [US4] Add settings loading to setComponentState() in controller.cpp: inside `if (version >= 14)` block after v13 loading, call loadSettingsParamsToController(streamer, setParam); in version < 14 path, explicitly set gain compensation to OFF (setParam(kSettingsGainCompensationId, 0.0)) to preserve pre-spec behavior for old presets
- [X] T029 [US2] Add settings formatting case to getParamStringByValue() in controller.cpp after rungler block: `} else if (id >= kSettingsBaseId && id <= kSettingsEndId) { result = formatSettingsParam(id, valueNormalized, string); }`

### 2.5 Build & Verify Tests Pass

- [X] T030 Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T031 Run Ruinae tests for settings param handling: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[processor]"`
- [X] T032 Run Ruinae tests for settings state persistence: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[state_persistence]"`
- [X] T033 Verify zero compiler warnings for settings_params.h, processor changes, controller changes

### 2.6 Commit

- [X] T034 Commit Phase 2 work: Settings parameter infrastructure complete (not yet accessible via UI)

**Checkpoint**: Settings parameters registered, handled, saved/loaded, backward-compatible, but no UI controls yet (drawer infrastructure needed)

---

## Phase 3: User Story 1 - Open and Close Settings Drawer (Priority: P1) 🎯 MVP

**Goal**: Activate gear icon in Master section to toggle settings drawer visibility. Drawer slides in from right edge (x=925 to x=705) with ease-out animation, partially overlapping main UI. User can close by clicking gear again or clicking outside drawer.

**Independent Test**: Click gear icon in Master section, verify drawer slides in from right with visible controls. Click gear again, verify drawer slides out. Open drawer again, click on main UI area, verify drawer closes.

### 3.1 Add Settings Control-Tags to UIDESC

- [ ] T035 [US1] Add 6 settings control-tag entries in F:\projects\iterum\plugins\ruinae\resources\editor.uidesc control-tags section after Rungler tags: SettingsPitchBendRange (2200), SettingsVelocityCurve (2201), SettingsTuningReference (2202), SettingsVoiceAllocMode (2203), SettingsVoiceStealMode (2204), SettingsGainCompensation (2205)
- [ ] T036 [US1] Add action control-tags after existing action tags: ActionSettingsToggle (10020), ActionSettingsOverlay (10021)

### 3.2 Activate Gear Icon and Add Drawer Color

- [ ] T037 [US1] Add bg-drawer color to colors section in editor.uidesc: `<color name="bg-drawer" rgba="#131316ff"/>` (approximately 12-15% darker than bg-main for visual distinction)
- [ ] T038 [US1] Modify existing gear icon ToggleButton in Master section (line ~2814) to add control-tag="ActionSettingsToggle" (currently has no tag, making it inert)

### 3.3 Add Click-Outside Overlay to UIDESC

- [ ] T039 [US1] Add transparent overlay ToggleButton at root level (after all main content, before drawer) in editor.uidesc: origin (0, 0), size (925, 880), control-tag="ActionSettingsOverlay", icon-style="" (no icon), on-color="~ TransparentCColor", off-color="~ TransparentCColor", transparent="true", visible="false", custom-view-name="SettingsOverlay" (acts as click-outside dismiss gesture)

### 3.4 Add Settings Drawer Container to UIDESC

- [ ] T040 [US1] Add settings drawer CViewContainer as LAST child of root template in editor.uidesc (so it draws on top due to z-order): origin (925, 0) (off-screen when closed), size (220, 880), background-color="bg-drawer", custom-view-name="SettingsDrawer", transparent="false"
- [ ] T041 [P] [US1] Add SETTINGS title CTextLabel inside drawer container: origin (16, 16), size (188, 20), title="SETTINGS", font="~ NormalFontBig", font-color="text-light", text-alignment="left"

### 3.5 Add Placeholder Controls to Drawer (Initially)

- [ ] T042 [P] [US1] Add Pitch Bend Range label + placeholder control: CTextLabel at (16, 56) size (120, 14), title="Pitch Bend Range", font-color="text-secondary"; placeholder CView below it (will be replaced with ArcKnob in User Story 2)
- [ ] T043 [P] [US1] Add Velocity Curve label + placeholder control: CTextLabel at (16, 126) size (120, 14), title="Velocity Curve"; placeholder CView below it
- [ ] T044 [P] [US1] Add Tuning Reference label + placeholder control: CTextLabel at (16, 182) size (120, 14), title="Tuning Reference"; placeholder CView below it
- [ ] T045 [P] [US1] Add Voice Allocation label + placeholder control: CTextLabel at (16, 252) size (120, 14), title="Voice Allocation"; placeholder CView below it
- [ ] T046 [P] [US1] Add Voice Steal label + placeholder control: CTextLabel at (16, 308) size (120, 14), title="Voice Steal"; placeholder CView below it
- [ ] T047 [P] [US1] Add Gain Compensation label + placeholder control: CTextLabel at (16, 364) size (120, 14), title="Gain Compensation"; placeholder CView below it

### 3.6 Add Controller Drawer Fields and Methods

- [ ] T048 [US1] Add drawer fields to F:\projects\iterum\plugins\ruinae\src\controller\controller.h after existing view pointer fields: settingsDrawer_ (CViewContainer* = nullptr), settingsOverlay_ (CView* = nullptr), gearButton_ (CControl* = nullptr), settingsAnimTimer_ (SharedPointer<CVSTGUITimer>), settingsDrawerOpen_ (bool = false), settingsDrawerProgress_ (float = 0.0f), settingsDrawerTargetOpen_ (bool = false)
- [ ] T049 [US1] Add toggleSettingsDrawer() method declaration to controller.h private methods section

### 3.7 Implement Drawer Animation Logic

- [ ] T050 [US1] Implement toggleSettingsDrawer() method in F:\projects\iterum\plugins\ruinae\src\controller\controller.cpp: flip settingsDrawerTargetOpen_ flag, if timer already running return (animation will naturally reverse due to changed target), otherwise create CVSTGUITimer with 16ms interval (~60fps), animation logic moves progress toward target (linear step kTimerInterval/kAnimDuration where kAnimDuration=0.16f = 160ms), apply quadratic ease-out curve (opening: 1-(1-t)^2, closing: t*(2-t)), map progress to x position (closed=925, open=705), call setViewSize on drawer, check if animation complete (progress reached 0.0 or 1.0), on completion set settingsDrawerOpen_ flag, stop timer, update overlay visibility, update gear button value

### 3.8 Wire Drawer in Controller verifyView

- [ ] T051 [US1] Add SettingsDrawer capture case to verifyView() in controller.cpp custom-view-name section: capture settingsDrawer_ pointer from container
- [ ] T052 [US1] Add SettingsOverlay capture case to verifyView(): capture settingsOverlay_ pointer, set initial visible=false
- [ ] T053 [US1] Add gear button capture case to verifyView() in control-tag registration section: if tag == kActionSettingsToggleTag, capture gearButton_ pointer, register controller as control listener

### 3.9 Handle Gear Icon and Overlay Clicks

- [ ] T054 [US1] Add kActionSettingsToggleTag case to valueChanged() in controller.cpp toggle buttons section: call toggleSettingsDrawer(), return
- [ ] T055 [US1] Add kActionSettingsOverlayTag case to valueChanged(): if settingsDrawerOpen_ is true, call toggleSettingsDrawer() to close drawer, return

### 3.10 Clean Up in willClose

- [ ] T056 [US1] Add drawer pointer cleanup to willClose() in controller.cpp: set settingsDrawer_ = nullptr, settingsOverlay_ = nullptr, gearButton_ = nullptr, settingsAnimTimer_ = nullptr, settingsDrawerOpen_ = false, settingsDrawerProgress_ = 0.0f, settingsDrawerTargetOpen_ = false

### 3.11 Build & Manual Verification

- [ ] T057 [US1] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T058 [US1] Verify zero compiler warnings for controller changes
- [ ] T059 [US1] Manual verification: Open plugin, click gear icon in Master section, verify drawer slides in from right edge with smooth ease-out animation (160ms duration), verify placeholder controls visible
- [ ] T060 [US1] Manual verification: Click gear icon again, verify drawer slides out smoothly and disappears off-screen
- [ ] T061 [US1] Manual verification: Open drawer, click anywhere on main UI area outside drawer bounds, verify drawer closes (overlay click-dismiss works)
- [ ] T062 [US1] Manual verification: Click gear icon twice quickly during animation, verify drawer immediately reverses direction from current position (no visual glitches)
- [ ] T063 [US1] Manual verification: With drawer open, click on controls inside drawer, verify drawer remains open (does not dismiss on internal interaction)

### 3.12 Commit

- [ ] T064 [US1] Commit completed User Story 1 work: Settings drawer infrastructure with animation and dismiss gestures

**Checkpoint**: User Story 1 complete - Drawer opens/closes smoothly, gear icon works, click-outside dismisses, animation handles interruptions correctly

---

## Phase 4: User Story 2 - Configure Pitch Bend and Tuning (Priority: P1)

**Goal**: Replace pitch bend range and tuning reference placeholder controls with functional ArcKnobs. Wire to VST parameters so user can adjust pitch bend range (0-24 semitones, integer steps) and tuning reference (400-480 Hz, continuous). Changes affect pitch bend wheel response and global tuning.

**Independent Test**: Open drawer, set Pitch Bend Range to 12 semitones, play A4, bend pitch wheel fully up, verify note reaches A5 (one octave). Set Tuning Reference to 432 Hz, play MIDI note 69, verify output frequency is 432 Hz.

### 4.1 Replace Placeholder Controls with Functional Knobs

- [ ] T065 [US2] Replace Pitch Bend Range placeholder control in editor.uidesc with ArcKnob: origin (16, 74), size (36, 36), control-tag="SettingsPitchBendRange", default-value="0.0833" (= 2/24), arc-color="master", guide-color="knob-guide"
- [ ] T066 [US2] Replace Tuning Reference placeholder control in editor.uidesc with ArcKnob: origin (16, 200), size (36, 36), control-tag="SettingsTuningReference", default-value="0.5" (= 440 Hz), arc-color="master", guide-color="knob-guide"

### 4.2 Build & Manual Verification

- [ ] T067 [US2] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T068 [US2] Verify zero compiler warnings
- [ ] T069 [US2] Manual verification: Open drawer, verify Pitch Bend Range knob defaults to 2 (normalized 0.0833), turn knob to 12 semitones (normalized 0.5), verify knob value displays "12 st"
- [ ] T070 [US2] Manual verification: Play A4 (MIDI note 69), bend pitch wheel fully up, verify pitch reaches A5 (one octave = 12 semitones)
- [ ] T071 [US2] Manual verification: Set Pitch Bend Range to 0, move pitch wheel, verify pitch does not change
- [ ] T072 [US2] Manual verification: Set Pitch Bend Range to 24, bend fully up from A4, verify pitch reaches A6 (two octaves)
- [ ] T073 [US2] Manual verification: Open drawer, verify Tuning Reference knob defaults to 440.0 Hz (normalized 0.5), turn knob to 432 Hz, verify knob value displays "432.0 Hz"
- [ ] T074 [US2] Manual verification: Play MIDI note 69 (A4), verify output frequency is 432 Hz (slightly flat compared to standard 440 Hz tuning)
- [ ] T075 [US2] Manual verification: Set Tuning Reference to 442 Hz, save preset, reload preset, verify tuning restores to 442 Hz

### 4.3 Commit

- [ ] T076 [US2] Commit completed User Story 2 work: Pitch Bend and Tuning knobs functional

**Checkpoint**: User Story 2 complete - Pitch Bend Range and Tuning Reference knobs functional, preset persistence works, knobs display correct values with units

---

## Phase 5: User Story 3 - Configure Voice Engine Behavior (Priority: P2)

**Goal**: Replace velocity curve, voice allocation, voice steal, and gain compensation placeholder controls with functional controls (dropdowns for discrete params, toggle for gain comp). Wire to VST parameters so user can customize polyphonic behavior.

**Independent Test**: Open drawer, set Voice Allocation to Round Robin, play 4 notes in sequence, verify voices used in order (0,1,2,3). Set Voice Steal to Soft, fill all voices, play one more note, verify stolen voice fades out. Set Velocity Curve to Soft, play at velocity 64, verify gain is louder than linear response. Enable Gain Compensation, change polyphony from 4 to 16, verify volume remains consistent.

### 5.1 Replace Placeholder Controls with Functional Controls

- [ ] T077 [P] [US3] Replace Velocity Curve placeholder in editor.uidesc with COptionMenu: origin (16, 144), size (140, 20), control-tag="SettingsVelocityCurve", default-value="0" (Linear), font="~ NormalFontSmaller", font-color="master", back-color="bg-dropdown", frame-color="frame-dropdown-dim"
- [ ] T078 [P] [US3] Replace Voice Allocation placeholder in editor.uidesc with COptionMenu: origin (16, 270), size (140, 20), control-tag="SettingsVoiceAllocMode", font="~ NormalFontSmaller", font-color="master", back-color="bg-dropdown", frame-color="frame-dropdown-dim" (default-value NOT set here because dropdown uses createDropdownParameterWithDefault which sets default=1/Oldest)
- [ ] T079 [P] [US3] Replace Voice Steal placeholder in editor.uidesc with COptionMenu: origin (16, 326), size (140, 20), control-tag="SettingsVoiceStealMode", default-value="0" (Hard), font="~ NormalFontSmaller", font-color="master", back-color="bg-dropdown", frame-color="frame-dropdown-dim"
- [ ] T080 [P] [US3] Replace Gain Compensation placeholder in editor.uidesc with ToggleButton: origin (16, 382), size (50, 20), control-tag="SettingsGainCompensation", default-value="1" (ON), title="On", on-color="master", off-color="toggle-off"

### 5.2 Build & Manual Verification

- [ ] T081 [US3] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T082 [US3] Verify zero compiler warnings
- [ ] T083 [US3] Manual verification: Open drawer, verify Velocity Curve dropdown defaults to "Linear", change to "Soft", play at velocity 64, verify resulting gain is approximately 0.71 (louder than linear which would be 0.5)
- [ ] T084 [US3] Manual verification: Set Voice Allocation to "Round Robin", play 4 notes in sequence, verify each note uses next voice slot in order (0,1,2,3,0,1,2,3...)
- [ ] T085 [US3] Manual verification: Set Voice Allocation to "Oldest", fill all voices, play one more note, verify oldest voice is stolen (default behavior)
- [ ] T086 [US3] Manual verification: Set Voice Steal to "Soft", fill all voices, play one more note, verify stolen voice receives note-off and fades out before new note starts
- [ ] T087 [US3] Manual verification: Set Voice Steal to "Hard", fill all voices, play one more note, verify stolen voice is immediately reassigned (hard cut, no fade)
- [ ] T088 [US3] Manual verification: Enable Gain Compensation (default), change polyphony from 4 to 16 voices, play full chords, verify overall volume remains approximately the same
- [ ] T089 [US3] Manual verification: Disable Gain Compensation, change polyphony from 4 to 16 voices, play full chords, verify volume is louder with more voices
- [ ] T090 [US3] Manual verification: Set all 6 settings to non-default values (pitch bend 7, velocity Hard, tuning 443, allocation RoundRobin, steal Soft, gain comp on), save preset, load different preset, reload saved preset, verify all 6 values restore exactly

### 5.3 Commit

- [ ] T091 [US3] Commit completed User Story 3 work: Voice engine behavior controls functional

**Checkpoint**: User Story 3 complete - All 6 settings controls functional, preset persistence works, voice engine behavior customizable

---

## Phase 6: User Story 4 - Preset Persistence and Automation (Priority: P2)

**Goal**: Verify all 6 settings parameters persist correctly across preset save/load cycles and are visible/automatable in DAW. Confirm backward compatibility for presets saved before this spec (version < 14).

**Independent Test**: Configure all 6 settings to non-default values, save preset, load different preset, reload saved preset, verify all values restore. Open DAW automation lane list, verify all 6 settings parameters are visible. Automate Pitch Bend Range parameter, play back, verify parameter changes in real-time. Load preset saved before this spec, verify settings default to pre-spec behavior (pitch bend 2, velocity Linear, tuning 440, allocation Oldest, steal Hard, gain comp OFF).

### 6.1 Preset Persistence Verification

- [ ] T092 [US4] Manual verification: Set all 6 settings to non-default values (pitch bend 7, velocity Hard, tuning 443 Hz, allocation Round Robin, steal Soft, gain comp enabled), save preset as "Settings Test"
- [ ] T093 [US4] Manual verification: Load a different preset (or initialize plugin), then reload "Settings Test" preset, verify all 6 values restore exactly: pitch bend 7, velocity Hard, tuning 443, allocation Round Robin, steal Soft, gain comp ON
- [ ] T094 [US4] Manual verification: Load a preset saved before this spec existed (state version < 14), verify settings default to backward-compatible values: pitch bend 2, velocity Linear, tuning 440, allocation Oldest, steal Hard, gain compensation OFF (preserving pre-spec behavior)
- [ ] T095 [US4] Manual verification: Save preset with settings at defaults, reload, verify drawer state (open/closed) is NOT persisted (drawer always starts closed on plugin load)

### 6.2 Automation Verification

- [ ] T096 [US4] Manual verification: Open plugin in DAW, open DAW automation lane list, verify all 6 settings parameters are visible: Pitch Bend Range, Velocity Curve, Tuning Reference, Voice Allocation, Voice Steal, Gain Compensation
- [ ] T097 [US4] Manual verification: Write automation for Pitch Bend Range parameter (e.g., ramp from 2 to 12 over 4 bars), play back, verify knob position updates in real-time and pitch bend wheel response changes audibly during playback
- [ ] T098 [US4] Manual verification: Write automation for Tuning Reference parameter (e.g., sweep from 440 to 432 Hz), play back, verify pitch shifts downward smoothly during playback
- [ ] T099 [US4] Manual verification: Automate Velocity Curve parameter (e.g., switch from Linear to Hard mid-playback), play notes, verify velocity response changes match automation
- [ ] T100 [US4] Manual verification: With drawer closed, automate any settings parameter (e.g., Pitch Bend Range), play back, verify parameter value changes internally even though drawer is closed. Open drawer mid-playback, verify control updates to current automated value

### 6.3 Commit

- [ ] T101 [US4] Commit completed User Story 4 work: Preset persistence and automation verified (if any fixes were needed)

**Checkpoint**: User Story 4 complete - All settings persist correctly, automation works, backward compatibility confirmed

---

## Phase 7: Polish & Validation

**Purpose**: Final testing and validation across all user stories

### 7.1 Pluginval Validation

- [ ] T102 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- [ ] T103 Verify all existing Ruinae tests still pass (no regressions): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe` (Note: Per Constitution Principle VIII, regression testing should ideally occur after each major phase, but is consolidated here due to task organization constraints)

### 7.2 Cross-Story Integration Verification

- [ ] T104 Manual verification: Open plugin, verify all 4 user stories work together: drawer opens/closes correctly, all 6 controls functional, preset persistence works, automation works, backward compatibility preserved
- [ ] T105 Manual verification: Test edge cases from spec: set pitch bend range to 0 (verify wheel has no effect), set tuning to boundary values 400/480 Hz (verify clamping works), click gear twice quickly (verify animation reverses smoothly), save preset with drawer open (verify drawer state NOT saved, always starts closed)

### 7.3 Commit

- [ ] T106 Commit any fixes from validation (if needed)

**Checkpoint**: All user stories validated, pluginval passes, no regressions

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

### 8.1 Run Clang-Tidy Analysis

- [ ] T107 Run clang-tidy on all modified source files: `./tools/run-clang-tidy.ps1 -Target ruinae`

### 8.2 Address Findings

- [ ] T108 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T109 Review warnings and fix where appropriate (document suppressions with NOLINT if intentionally ignored)
- [ ] T110 Commit clang-tidy fixes (if any)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [ ] T111 Update F:\projects\iterum\specs\_architecture_\ with new components added by this spec:
  - Document settings parameters (IDs 2200-2205) and their mapping to engine methods in appropriate layer section
  - Document settings drawer UI (animation pattern, gear icon toggle, click-outside dismiss) in UI patterns section
  - Document state version 14 format (appended settings param pack: 4 floats, 3 int32s, 24 bytes total)
  - Document backward compatibility behavior for version < 14 presets
  - Verify no duplicate functionality was introduced

### 9.2 Final Commit

- [ ] T112 Commit architecture documentation updates
- [ ] T113 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T114 Review ALL FR-001 through FR-013 requirements from spec.md against implementation (param IDs, settings_params.h, processor wiring, controller registration, drawer UI, animation, click-outside dismiss)
- [ ] T115 Review ALL SC-001 through SC-010 success criteria and verify measurable targets are achieved (manual verification results, pluginval pass, automation visibility)
- [ ] T116 Search for cheating patterns in implementation: No placeholder comments, no test thresholds relaxed, no features quietly removed

### 10.2 Fill Compliance Table in spec.md

- [ ] T117 Update F:\projects\iterum\specs\058-settings-drawer\spec.md "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx requirement
- [ ] T118 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T119 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T120 Commit all spec work to feature branch `058-settings-drawer`
- [ ] T121 Verify all tests pass and pluginval passes

### 11.2 Completion Claim

- [ ] T122 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 completion (param IDs must exist before param file creation) - BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Phase 2 completion (drawer infrastructure needs parameter registration for control-tags to be valid)
- **User Story 2 (Phase 4)**: Depends on Phase 3 completion (replaces placeholder controls in drawer with functional knobs)
- **User Story 3 (Phase 5)**: Depends on Phase 3 completion (replaces placeholder controls in drawer with functional dropdowns/toggle) - Can proceed in parallel with US2
- **User Story 4 (Phase 6)**: Depends on Phase 2, 3, 4, 5 completion (tests persistence and automation of all implemented settings)
- **Polish (Phase 7)**: Depends on Phase 2-6 completion
- **Static Analysis (Phase 8)**: Depends on Phase 7 completion
- **Documentation (Phase 9)**: Depends on Phase 8 completion
- **Completion Verification (Phase 10)**: Depends on Phase 9 completion
- **Final Completion (Phase 11)**: Depends on Phase 10 completion

### User Story Dependencies

- **User Story 1 (P1)**: Core drawer infrastructure - MUST complete first
- **User Story 2 (P1)**: Adds pitch bend and tuning knobs - Depends on US1 (drawer must exist)
- **User Story 3 (P2)**: Adds voice engine controls - Depends on US1 (drawer must exist) - Can proceed in parallel with US2
- **User Story 4 (P2)**: Tests persistence and automation - Depends on US1, US2, US3 (all controls must exist)

### Within Each User Story

- Tests FIRST (where applicable): Tests MUST be written and FAIL before implementation (Phase 2 only has parameter tests)
- UIDESC changes (control-tags, containers, controls) before controller.h changes
- controller.h changes before controller.cpp changes
- Build before manual verification
- Manual verification before commit
- Commit at end of each user story phase

### Parallel Opportunities

- **Phase 1 (Setup)**: T001-T004 can run in parallel (different sections of plugin_ids.h, but coordinate edits)
- **Phase 2 (Foundational)**: T008-T009 (tests) can run in parallel
- **Phase 3 (User Story 1)**: T035-T036 (control-tags) can run in parallel, T042-T047 (placeholder controls) can run in parallel after T040-T041 (drawer container) complete
- **Phase 4 (User Story 2)**: T065-T066 (knobs) can run in parallel
- **Phase 5 (User Story 3)**: T077-T080 (dropdowns and toggle) can run in parallel
- **User Stories 2 and 3**: Can be worked on in parallel after User Story 1 completes (both replace different placeholder controls in the same drawer)

---

## Parallel Example: User Story 1

```bash
# After drawer container is created (T040-T041):
# Launch all placeholder control additions in parallel:
Task: "Add Pitch Bend Range label + placeholder control" (T042)
Task: "Add Velocity Curve label + placeholder control" (T043)
Task: "Add Tuning Reference label + placeholder control" (T044)
Task: "Add Voice Allocation label + placeholder control" (T045)
Task: "Add Voice Steal label + placeholder control" (T046)
Task: "Add Gain Compensation label + placeholder control" (T047)
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2 Only)

1. Complete Phase 1: Setup (param IDs and action tags)
2. Complete Phase 2: Foundational (parameter infrastructure - settings_params.h, processor/controller wiring, state persistence)
3. Complete Phase 3: User Story 1 (drawer infrastructure with animation)
4. Complete Phase 4: User Story 2 (pitch bend and tuning knobs functional)
5. **STOP and VALIDATE**: Test drawer animation, pitch bend/tuning functionality, preset persistence
6. Deploy/demo if ready (basic drawer with 2 functional controls)

### Incremental Delivery

1. Complete Setup + Foundational → Parameter infrastructure ready
2. Add User Story 1 → Drawer opens/closes, animation works
3. Add User Story 2 → Pitch bend and tuning knobs functional (MVP!)
4. Add User Story 3 → Voice engine controls functional
5. Add User Story 4 → Persistence and automation validated
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (drawer infrastructure)
3. Once User Story 1 is done:
   - Developer A: User Story 2 (pitch bend + tuning)
   - Developer B: User Story 3 (voice engine controls) - in parallel with US2
4. Once User Stories 2 & 3 are done:
   - Developer A or B: User Story 4 (validation)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Phase 2 parameter tests)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- **Window is 925x880** (not 900x866 as spec approximated) - use correct dimensions for drawer calculations
- **Gain comp default**: ON for new presets (param default 1.0), explicitly OFF for old presets (version < 14 backward compat)
- **Voice Allocation default**: Oldest (index 1), NOT first item - use `createDropdownParameterWithDefault()`
- **Drawer z-order**: Overlay THEN drawer must be last children of root template (overlay below drawer in z-order)
- **Timer cleanup**: `settingsAnimTimer_ = nullptr` in willClose to prevent dangling lambda
