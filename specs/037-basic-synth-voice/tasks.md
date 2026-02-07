# Tasks: Basic Synth Voice

**Input**: Design documents from `specs/037-basic-synth-voice/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

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
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Layer 0 utility setup and test infrastructure

- [X] T001 Create test file structure at `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp`
- [X] T002 Add synth_voice_test.cpp to `F:\projects\iterum\dsp\tests\CMakeLists.txt` (in Layer 3: Systems section)
- [X] T003 Add synth_voice.h to `F:\projects\iterum\dsp\lint_all_headers.cpp` include list

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Layer 0 utility that ALL user stories depend on for key tracking

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Layer 0 Utility - frequencyToMidiNote

- [X] T004 Write failing tests for frequencyToMidiNote() in `F:\projects\iterum\dsp\tests\unit\core\pitch_utils_test.cpp`
  - Test frequency 440.0 Hz returns 69.0 (A4)
  - Test frequency 261.63 Hz returns 60.0 (C4)
  - Test frequency <= 0 returns 0.0
  - Test continuous values (e.g., 466.16 Hz returns ~70.0)
- [X] T005 Implement frequencyToMidiNote() in `F:\projects\iterum\dsp\include\krate\dsp\core\pitch_utils.h`
  - Formula: `12.0f * std::log2(hz / 440.0f) + 69.0f`
  - Return 0.0 for hz <= 0
- [X] T006 Verify pitch_utils tests pass for frequencyToMidiNote()
- [ ] T007 Commit Layer 0 utility addition

**Checkpoint**: frequencyToMidiNote() available - user story implementation can now begin

---

## Phase 3: User Story 1 - Single Voice Plays a Note (Priority: P1) 🎯 MVP

**Goal**: A single voice produces shaped audio on note-on, sustains while held, and fades out on note-off.

**Independent Test**: Prepare voice, call noteOn(440, 1.0), process samples, verify non-zero output during attack/decay/sustain, call noteOff(), verify release to silence and isActive() becomes false.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T008 [P] [US1] Write failing lifecycle tests in `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp` (FR-001, FR-002, FR-003) [synth-voice][lifecycle]
  - Test prepare() initializes all components
  - Test reset() clears state and sets isActive() to false
  - Test process() returns 0.0 before prepare()
  - Test processBlock() fills zeros before prepare()
- [X] T009 [P] [US1] Write failing note control tests (FR-004, FR-005, FR-006) [synth-voice][note-control]
  - Test noteOn() produces non-zero output within first 512 samples (SC-002)
  - Test noteOff() triggers release and eventually isActive() becomes false
  - Test isActive() returns false before noteOn, true after, false after release
- [X] T010 [P] [US1] Write failing envelope tests (FR-022, FR-023, FR-024, FR-025) [synth-voice][envelope]
  - Test both envelopes configured with defaults
  - Test amplitude envelope shapes output
  - Test voice becomes inactive when amp envelope reaches idle
- [X] T011 [P] [US1] Write failing signal flow tests (FR-028, FR-029, FR-030) [synth-voice][signal-flow]
  - Test process() returns single sample
  - Test processBlock() is bit-identical to process() loop (SC-004)
  - Test output is 0.0 when voice is idle
  - Test output transitions through attack/decay/sustain/release stages

### 3.2 Implementation for User Story 1

- [X] T012 [US1] Create SynthVoice class header at `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h`
  - Includes: Layer 0 (pitch_utils, db_utils), Layer 1 (polyblep_oscillator, adsr_envelope, svf, envelope_utils)
  - Member components: 2 PolyBlepOscillators, 1 SVF, 2 ADSREnvelopes
  - Member parameters: oscMix_, osc2DetuneCents_, osc2Octave_, filterCutoffHz_, filterEnvAmount_, filterKeyTrack_, velToFilterEnv_
  - Member state: noteFrequency_, velocity_, sampleRate_, prepared_
- [X] T013 [US1] Implement prepare() method (FR-001)
  - Initialize both oscillators (sampleRate, Sawtooth waveform)
  - Initialize filter (sampleRate, Lowpass mode, 1000 Hz cutoff, 0.707 Q)
  - Initialize amp envelope (sampleRate, A=10ms D=50ms S=1.0 R=100ms, velocity scaling enabled)
  - Initialize filter envelope (sampleRate, A=10ms D=200ms S=0.0 R=100ms)
  - Set prepared_ = true
- [X] T014 [US1] Implement reset() method (FR-002)
  - Call reset() on all sub-components
  - Clear state variables (noteFrequency_, velocity_)
- [X] T015 [US1] Implement noteOn() method (FR-004)
  - Guard against NaN/Inf inputs (FR-032)
  - Clamp and store frequency and velocity
  - Update oscillator frequencies (osc1 at noteFrequency, osc2 with detune/octave applied)
  - Call setVelocity() on amp envelope
  - Call gate(true) on both envelopes (RetriggerMode::Hard — enterAttack() does not reset output_, naturally attacks from current level)
- [X] T016 [US1] Implement noteOff() method (FR-005)
  - Call gate(false) on both envelopes
- [X] T017 [US1] Implement isActive() method (FR-006)
  - Return ampEnv_.isActive()
- [X] T018 [US1] Implement process() method (FR-028, FR-030)
  - Return 0.0 if not prepared or not active
  - Generate oscillator samples (osc1, osc2)
  - Mix oscillators: (1 - oscMix_) * osc1 + oscMix_ * osc2
  - Process filter envelope
  - Compute effective cutoff (base cutoff only for US1, envelope modulation added in US3)
  - Clamp cutoff to [20.0, sampleRate * 0.495]
  - Update and process filter
  - Process amplitude envelope
  - Return filtered * ampLevel
- [X] T019 [US1] Implement processBlock() method (FR-030)
  - Loop calling process() for numSamples
- [X] T020 [US1] Add synth_voice.h to `F:\projects\iterum\dsp\CMakeLists.txt` KRATE_DSP_SYSTEMS_HEADERS list

### 3.3 Verification for User Story 1

- [X] T021 [US1] Build dsp_tests target: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T022 [US1] Run SynthVoice lifecycle tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][lifecycle]"`
- [X] T023 [US1] Run SynthVoice note control tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][note-control]"`
- [X] T024 [US1] Run SynthVoice envelope tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][envelope]"`
- [X] T025 [US1] Run SynthVoice signal flow tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][signal-flow]"`
- [X] T026 [US1] Verify all User Story 1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T027 [US1] Verify IEEE 754 compliance: Check if synth_voice_test.cpp uses std::isnan/isfinite/isinf, add to -fno-fast-math list in `F:\projects\iterum\dsp\tests\CMakeLists.txt` if needed

### 3.5 Commit (MANDATORY)

- [ ] T028 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 should be fully functional - a voice that plays and stops with basic oscillator-filter-envelope signal flow

---

## Phase 4: User Story 2 - Dual Oscillator Sound Shaping (Priority: P1)

**Goal**: Configure two oscillators with different waveforms, detune, octave offset, and blend them using mix control.

**Independent Test**: Set osc1 to sawtooth, osc2 to square with +7 cents detune and -1 octave, mix to 0.5, verify output contains spectral content from both waveforms at expected frequencies.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T029 [P] [US2] Write failing oscillator parameter tests in `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp` (FR-008, FR-009) [synth-voice][oscillator]
  - Test setOsc1Waveform() and setOsc2Waveform() for each waveform (Sine, Saw, Square, Pulse, Triangle)
  - Test each waveform produces non-zero, distinct output
- [X] T030 [P] [US2] Write failing mix tests (FR-010, SC-007) [synth-voice][oscillator]
  - Test mix=0.0 silences osc2 (exact 0.0 contribution)
  - Test mix=1.0 silences osc1 (exact 0.0 contribution)
  - Test mix=0.5 blends both oscillators
- [X] T031 [P] [US2] Write failing detune tests (FR-011) [synth-voice][oscillator]
  - Test +10 cents produces audible beating
  - Test detune range [-100, +100] cents
- [X] T032 [P] [US2] Write failing octave tests (FR-012) [synth-voice][oscillator]
  - Test osc2 octave +1 produces 880 Hz when note is 440 Hz
  - Test octave range [-2, +2]
  - Test octave compounds with detune
  - Test out-of-range values clamped to [-2, +2] per FR-032

### 4.2 Implementation for User Story 2

- [X] T033 [P] [US2] Implement setOsc1Waveform() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h`
  - Call osc1_.setWaveform(waveform)
  - NaN/Inf guard not needed (enum type)
- [X] T034 [P] [US2] Implement setOsc2Waveform() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h`
  - Call osc2_.setWaveform(waveform)
- [X] T035 [P] [US2] Implement setOscMix() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h` (FR-010, FR-031, FR-032)
  - Guard against NaN/Inf
  - Clamp to [0.0, 1.0]
  - Store in oscMix_
- [X] T036 [P] [US2] Implement setOsc2Detune() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h` (FR-011, FR-031, FR-032)
  - Guard against NaN/Inf
  - Clamp to [-100.0, +100.0]
  - Store in osc2DetuneCents_
  - If voice is active, update osc2 frequency immediately
- [X] T037 [P] [US2] Implement setOsc2Octave() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h` (FR-012, FR-031)
  - Clamp to [-2, +2]
  - Store in osc2Octave_
  - If voice is active, update osc2 frequency immediately
- [X] T038 [US2] Update noteOn() to apply detune and octave to osc2 frequency
  - Formula: `noteFrequency * 2^octave * 2^(cents/1200)`
  - Use semitonesToRatio() for both octave and detune

### 4.3 Verification for User Story 2

- [X] T039 [US2] Build dsp_tests target
- [X] T040 [US2] Run SynthVoice oscillator tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][oscillator]"`
- [X] T041 [US2] Verify all User Story 2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T042 [US2] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 4.5 Commit (MANDATORY)

- [ ] T043 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 should both work - single note with dual oscillator shaping

---

## Phase 5: User Story 3 - Filter with Envelope Modulation (Priority: P1)

**Goal**: Filter cutoff modulates via envelope for classic "pluck" or "bass sweep" sound.

**Independent Test**: Set filter cutoff to 200 Hz, envelope amount to +48 semitones, verify effective cutoff rises during attack then settles during decay.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T044 [P] [US3] Write failing filter parameter tests in `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp` (FR-013, FR-014, FR-015, FR-016) [synth-voice][filter]
  - Test setFilterType() for LP/HP/BP/Notch modes produce distinct frequency responses
  - Test setFilterCutoff() affects output frequency content
  - Test setFilterResonance() produces resonant peak at high Q
  - Test high Q (Q=30) allows self-oscillation
- [X] T045 [P] [US3] Write failing filter envelope tests (FR-017, FR-018, FR-019) [synth-voice][filter-env]
  - Test 500 Hz cutoff + 48 semitone env amount at peak = 8000 Hz effective cutoff
  - Test 2000 Hz cutoff + -24 semitone env amount at peak = 500 Hz effective cutoff
  - Test env amount = 0 keeps cutoff at base value
  - Test per-sample modulation produces smooth sweeps (no stepping artifacts)
- [X] T046 [P] [US3] Write failing cutoff clamping tests (SC-006) [synth-voice][filter-env]
  - Test extreme parameters: max env amount + max key tracking + highest note stays in [20 Hz, sr*0.495]

### 5.2 Implementation for User Story 3

- [X] T047 [P] [US3] Implement setFilterType() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h` (FR-014, FR-031)
  - Call filter_.setMode(type)
- [X] T048 [P] [US3] Implement setFilterCutoff() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h` (FR-015, FR-031, FR-032)
  - Guard against NaN/Inf
  - Clamp to [20.0, 20000.0]
  - Store in filterCutoffHz_
- [X] T049 [P] [US3] Implement setFilterResonance() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h` (FR-016, FR-031, FR-032)
  - Guard against NaN/Inf
  - Clamp to [0.1, 30.0]
  - Call filter_.setResonance(q)
- [X] T050 [P] [US3] Implement setFilterEnvAmount() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h` (FR-017, FR-031, FR-032)
  - Guard against NaN/Inf
  - Clamp to [-96.0, +96.0]
  - Store in filterEnvAmount_
- [X] T051 [P] [US3] Implement setFilterAttack/Decay/Sustain/Release() methods
  - Forward to filterEnv_ methods
  - Add NaN/Inf guards
- [X] T052 [P] [US3] Implement setFilterAttackCurve/DecayCurve/ReleaseCurve() methods
  - Forward to filterEnv_ methods
- [X] T053 [US3] Update process() to apply filter envelope modulation (FR-018, FR-019)
  - Process filter envelope to get current level
  - Compute effective cutoff: baseCutoff * 2^(filterEnvAmount_ * filterEnvLevel / 12.0)
  - Clamp to [20.0, sampleRate * 0.495]
  - Call filter_.setCutoff(effectiveCutoff) per sample
  - Note: velocity scaling of envelope amount is added later in US5 (T070)

### 5.3 Verification for User Story 3

- [X] T054 [US3] Build dsp_tests target
- [X] T055 [US3] Run SynthVoice filter tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][filter]"`
- [X] T056 [US3] Run SynthVoice filter envelope tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][filter-env]"`
- [X] T057 [US3] Verify all User Story 3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T058 [US3] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 5.5 Commit (MANDATORY)

- [ ] T059 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, AND 3 should work - single note with dual oscillators and animated filter envelope

---

## Phase 6: User Story 4 - Filter Key Tracking (Priority: P2)

**Goal**: Filter cutoff tracks keyboard pitch for consistent harmonic content across the keyboard.

**Independent Test**: Compare effective filter cutoff for notes an octave apart (MIDI 60 vs 72) at 100% key tracking, verify cutoff doubles.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US4] Write failing key tracking tests in `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp` (FR-020, FR-021) [synth-voice][key-tracking]
  - Test 100% key tracking, cutoff 1000 Hz, note C5 (12 semitones above C4) -> effective cutoff shifts up by 12 semitones (factor 2.0)
  - Test 0% key tracking -> cutoff does not change with pitch
  - Test 50% key tracking, note 12 semitones above reference -> cutoff shifts up by 6 semitones (factor ~1.414)

### 6.2 Implementation for User Story 4

- [X] T061 [P] [US4] Implement setFilterKeyTrack() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h` (FR-020, FR-031, FR-032)
  - Guard against NaN/Inf
  - Clamp to [0.0, 1.0]
  - Store in filterKeyTrack_
- [X] T062 [US4] Update process() to apply key tracking (FR-021)
  - Compute keyTrackSemitones: filterKeyTrack_ * (frequencyToMidiNote(noteFrequency_) - 60.0)
  - Add to totalSemitones in effective cutoff formula
  - Formula: effectiveCutoff = baseCutoff * 2^((effectiveEnvAmount * filterEnvLevel + keyTrackSemitones) / 12.0)

### 6.3 Verification for User Story 4

- [X] T063 [US4] Build dsp_tests target
- [X] T064 [US4] Run SynthVoice key tracking tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][key-tracking]"`
- [X] T065 [US4] Verify all User Story 4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T066 [US4] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 6.5 Commit (MANDATORY)

- [ ] T067 [US4] Commit completed User Story 4 work

**Checkpoint**: User Stories 1-4 work - key tracking provides consistent timbre across keyboard

---

## Phase 7: User Story 5 - Velocity-Sensitive Dynamics (Priority: P2)

**Goal**: Softer notes are quieter and duller, harder notes are louder and brighter.

**Independent Test**: Trigger notes at velocity 0.25 and 1.0, compare peak amplitude and filter envelope depth.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T068 [P] [US5] Write failing velocity tests in `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp` (FR-026, FR-027) [synth-voice][velocity]
  - Test velocity 0.5 produces ~50% peak amplitude vs velocity 1.0
  - Test velToFilterEnv=1.0, velocity 0.25 -> filter envelope depth is 25% of full amount
  - Test velToFilterEnv=0.0 -> filter envelope depth is always full amount regardless of velocity

### 7.2 Implementation for User Story 5

- [X] T069 [P] [US5] Implement setVelocityToFilterEnv() in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h` (FR-027, FR-031, FR-032)
  - Guard against NaN/Inf
  - Clamp to [0.0, 1.0]
  - Store in velToFilterEnv_
- [X] T070 [US5] Update process() to apply velocity to filter envelope depth (FR-027)
  - Compute effectiveEnvAmount: filterEnvAmount_ * (1.0 - velToFilterEnv_ + velToFilterEnv_ * velocity_)
  - Use effectiveEnvAmount in cutoff formula

### 7.3 Verification for User Story 5

- [X] T071 [US5] Build dsp_tests target
- [X] T072 [US5] Run SynthVoice velocity tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][velocity]"`
- [X] T073 [US5] Verify all User Story 5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T074 [US5] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 7.5 Commit (MANDATORY)

- [ ] T075 [US5] Commit completed User Story 5 work

**Checkpoint**: User Stories 1-5 work - velocity controls both amplitude and filter brightness

---

## Phase 8: User Story 6 - Block Processing for Polyphonic Use (Priority: P2)

**Goal**: Efficient block processing for polyphonic synth engine.

**Independent Test**: Compare output from processBlock() against loop of process() calls, verify bit-identical results.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T076 [P] [US6] Write failing block processing tests in `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp` (FR-030, SC-004) [synth-voice][signal-flow]
  - Test processBlock(512) produces bit-identical output to 512 process() calls
  - Test voice that completes release mid-block -> isActive() false after call, remaining samples are zero

### 8.2 Implementation for User Story 6

- [X] T077 [US6] Verify processBlock() implementation (already implemented in US1, just verify tests)
  - processBlock() is already a simple loop calling process()
  - No changes needed

### 8.3 Verification for User Story 6

- [X] T078 [US6] Build dsp_tests target
- [X] T079 [US6] Run SynthVoice block processing tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][signal-flow]"`
- [X] T080 [US6] Verify all User Story 6 tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [X] T081 [US6] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 8.5 Commit (MANDATORY)

- [ ] T082 [US6] Commit completed User Story 6 work

**Checkpoint**: All 6 user stories work - complete voice with all features

---

## Phase 9: Edge Cases & Safety (Cross-Cutting)

**Purpose**: Parameter safety and edge case handling across all user stories

### 9.1 Tests for Edge Cases (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T083 [P] Write failing retrigger tests in `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp` (FR-007, SC-009) [synth-voice][retrigger]
  - Test noteOn() while active -> envelopes attack from current level (RetriggerMode::Hard)
  - Test oscillator phase preservation on retrigger: save osc phase before retrigger, verify phase is identical after retrigger (only frequency changes)
  - Test retrigger produces no clicks: compute peak absolute sample-to-sample difference around retrigger point, verify <= 0.01 (-40 dBFS)
- [X] T084 [P] Write failing safety tests (FR-031, FR-032) [synth-voice][safety]
  - Test all setters work before prepare, while playing, while idle
  - Test all setters ignore NaN inputs: set parameter to known value, call setter with NaN, verify parameter retains original value (not reset to default or 0)
  - Test all setters ignore Inf inputs: set parameter to known value, call setter with Inf, verify parameter retains original value (not reset to default or 0)
  - Test frequency=0 produces silence
  - Test velocity=0 produces silence but voice becomes inactive after release
  - Test noteOff() while idle (before any noteOn) -> verify isActive() remains false, no crash
  - Test prepare() called while note is active -> verify isActive() becomes false, process() returns 0.0
- [X] T085 [P] Write failing sample rate tests (SC-005) [synth-voice][acceptance]
  - Test voice works at 44100, 48000, 88200, 96000, 176400, 192000 Hz
- [X] T086 [P] Write failing output range tests (SC-008) [synth-voice][acceptance]
  - Test output in [-1, +1] under normal conditions (single osc, no resonance)

### 9.2 Implementation for Edge Cases

- [X] T087 [P] Add NaN/Inf guards to all remaining setters in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h`
  - setAmpAttack/Decay/Sustain/Release
  - setAmpAttackCurve/DecayCurve/ReleaseCurve (enum, no guard needed)
- [X] T088 [P] Verify retrigger behavior in noteOn() (already implemented, just verify)
  - setVelocity() then gate(true) on both envelopes
  - Oscillator frequencies updated without phase reset
- [X] T089 [P] Add edge case handling in process()
  - Return 0.0 if noteFrequency_ <= 0 (already handled by osc clamp)

### 9.3 Verification for Edge Cases

- [X] T090 Build dsp_tests target
- [X] T091 Run SynthVoice safety tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][safety]"`
- [X] T092 Run SynthVoice acceptance tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][acceptance]"`
- [X] T093 Verify all edge case tests pass

### 9.4 Commit (MANDATORY)

- [ ] T094 Commit completed edge case and safety work

**Checkpoint**: All edge cases handled safely

---

## Phase 10: Performance & Success Criteria

**Purpose**: Verify all success criteria are met

### 10.1 Tests for Success Criteria (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T095 [P] Write performance benchmark test in `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp` (SC-001) [synth-voice][performance]
  - Process 1 second of audio (44100 samples) at 44.1 kHz
  - Measure average processing time per sample
  - Verify < 1% CPU (processing time < 1% of real-time duration)
  - Typical settings: both oscillators generating sawtooth, Q=5, both envelopes in sustain stage, filter envelope modulation enabled at +48 semitones, mix=0.5
- [X] T096 [P] Write acceptance tests for remaining success criteria (SC-002, SC-003, SC-006, SC-007, SC-008, SC-009, SC-010)
  - SC-002: Non-zero output within first 512 samples (already tested in US1)
  - SC-003: Exactly 0.0 output after release completes (already tested in US1)
  - SC-006: Cutoff clamping with extreme parameters (already tested in US3)
  - SC-007: Mix extremes produce exact 0.0 contribution (already tested in US2)
  - SC-008: Output range [-1, +1] under normal conditions (already tested in edge cases)
  - SC-009: Retrigger click-free (already tested in edge cases)
  - SC-010: All 32 FRs have tests (verify by test count/tags)

- [X] T096b [P] Verify SC-010 (all 32 FRs have test coverage): grep test file for FR-001 through FR-032 tags/comments and verify each has at least one corresponding test case

### 10.2 Implementation for Performance

- [X] T097 Verify performance optimizations in process() if needed
  - Skip filter modulation when filterEnvAmount_ == 0.0 (optional optimization)
  - Skip osc2 when oscMix_ == 0.0 (optional optimization)

### 10.3 Verification for Performance

- [X] T098 Build dsp_tests in Release mode: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T099 Run performance benchmark: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][performance]"`
- [X] T100 Verify SC-001 < 1% CPU is met
- [X] T101 Run all acceptance tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice][acceptance]"`
- [X] T102 Verify all success criteria are met

### 10.4 Commit (MANDATORY)

- [ ] T103 Commit completed performance verification work

**Checkpoint**: All success criteria verified and met

---

## Phase 11: Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [X] T104 Update `F:\projects\iterum\specs\_architecture_\layer-0-core.md` with frequencyToMidiNote() function
  - Add to pitch utilities section
  - Include purpose, signature, usage example
  - Document when to use (key tracking in voice systems)
- [X] T105 Update `F:\projects\iterum\specs\_architecture_\layer-3-systems.md` with SynthVoice class
  - Add new section for SynthVoice
  - Include: purpose, public API summary, file location
  - Add "when to use this" guidance (single-voice subtractive synthesis for polyphonic engine)
  - Note sibling systems (FMVoice, future WavetableVoice)

### 11.2 Final Commit

- [ ] T106 Commit architecture documentation updates
- [ ] T107 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 12.1 Run Clang-Tidy Analysis

- [X] T108 Run clang-tidy on DSP target: `powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 12.2 Address Findings

- [X] T109 Fix all errors reported by clang-tidy (blocking issues)
- [X] T110 Review warnings and fix where appropriate (use judgment for DSP code)
- [X] T111 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T112 Review ALL 32 FR-xxx requirements from `F:\projects\iterum\specs\037-basic-synth-voice\spec.md` against implementation in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h`
  - Open synth_voice.h and locate code for each FR-001 through FR-032
  - Record file path and line numbers for each requirement
- [X] T113 Review ALL 10 SC-xxx success criteria and verify measurable targets are achieved
  - Run tests and record actual measured values vs spec targets
  - SC-001: Measure CPU usage, verify < 1%
  - SC-002 through SC-010: Verify test results
- [X] T114 Search for cheating patterns in implementation:
  - Search for `// placeholder`, `// TODO`, `// stub` comments in synth_voice.h
  - Verify no test thresholds were relaxed from spec requirements
  - Verify no features were quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [X] T115 Update `F:\projects\iterum\specs\037-basic-synth-voice\spec.md` "Implementation Verification" section
  - Fill Evidence column for each FR-001 through FR-032 with file path and line numbers
  - Fill Evidence column for each SC-001 through SC-010 with test names and actual measured values
  - Mark each requirement status: MET / NOT MET / PARTIAL / DEFERRED
- [X] T116 Mark overall status in spec.md: COMPLETE / NOT COMPLETE / PARTIAL
- [X] T117 Fill Completion Checklist in spec.md
- [X] T118 Write Honest Assessment section in spec.md

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T119 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [ ] T120 Commit all spec work to feature branch `037-basic-synth-voice`
- [X] T121 Verify all tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice]"`

### 14.2 Completion Claim

- [X] T122 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - User Story 1 (Phase 3): MVP - complete first
  - User Story 2 (Phase 4): Depends on US1 completion (extends oscillator functionality)
  - User Story 3 (Phase 5): Depends on US1 completion (adds filter envelope)
  - User Story 4 (Phase 6): Depends on US1, US3 completion (adds key tracking to filter)
  - User Story 5 (Phase 7): Depends on US1, US3 completion (adds velocity to filter)
  - User Story 6 (Phase 8): Depends on US1 completion (validates block processing)
- **Edge Cases (Phase 9)**: Depends on all user stories
- **Performance (Phase 10)**: Depends on all user stories
- **Documentation (Phase 11)**: Depends on all implementation complete
- **Static Analysis (Phase 12)**: Depends on all implementation complete
- **Completion Verification (Phase 13)**: Depends on all previous phases
- **Final Completion (Phase 14)**: Depends on Phase 13

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories (MVP!)
- **User Story 2 (P1)**: Extends US1 oscillator functionality - Should complete after US1
- **User Story 3 (P1)**: Extends US1 filter functionality - Should complete after US1
- **User Story 4 (P2)**: Extends US3 filter functionality - Should complete after US1 and US3
- **User Story 5 (P2)**: Extends US3 filter functionality - Should complete after US1 and US3
- **User Story 6 (P2)**: Validates US1 block processing - Should complete after US1

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation tasks can often run in parallel (marked with [P])
- Verification tasks run after implementation
- Cross-platform check after verification
- Commit is LAST task in each user story

### Parallel Opportunities

- Within Phase 1 (Setup): All tasks can run in parallel
- Within Phase 2 (Foundational): Test writing (T004) can run while reading existing code
- Within each user story: All test writing tasks marked [P] can run in parallel
- Within each user story: All implementation tasks marked [P] can run in parallel (different setters, etc.)
- Phase 9 (Edge Cases): All test writing and implementation tasks marked [P] can run in parallel
- Phase 10 (Performance): Test writing tasks can run in parallel

---

## Parallel Example: User Story 3

```bash
# Launch all test writing for User Story 3 together:
Task T044: Write filter parameter tests (FR-013, FR-014, FR-015, FR-016)
Task T045: Write filter envelope tests (FR-017, FR-018, FR-019)
Task T046: Write cutoff clamping tests (SC-006)

# After tests fail, launch all implementation tasks together:
Task T047: Implement setFilterType()
Task T048: Implement setFilterCutoff()
Task T049: Implement setFilterResonance()
Task T050: Implement setFilterEnvAmount()
Task T051: Implement filter envelope setters
Task T052: Implement filter envelope curve setters
# Then sequentially:
Task T053: Update process() to apply filter envelope modulation
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (frequencyToMidiNote utility)
3. Complete Phase 3: User Story 1 (basic voice lifecycle with single oscillator)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Verify voice plays, sustains, and releases correctly

### Incremental Delivery

1. Complete Setup + Foundational → frequencyToMidiNote() available
2. Add User Story 1 → Test independently → Basic voice works (MVP!)
3. Add User Story 2 → Test independently → Dual oscillators work
4. Add User Story 3 → Test independently → Filter envelope modulation works
5. Add User Story 4 → Test independently → Key tracking works
6. Add User Story 5 → Test independently → Velocity sensitivity works
7. Add User Story 6 → Test independently → Block processing validated
8. Each story adds value without breaking previous stories

---

## Notes

- [P] tasks = different files/functions, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Build command: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- Test command: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice]"`
