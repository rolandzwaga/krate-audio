# Tasks: Polyphonic Synth Engine

**Input**: Design documents from `specs/038-polyphonic-synth-engine/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/poly_synth_engine_api.h, quickstart.md

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

**Purpose**: Prerequisite modification to SynthVoice for legato support

- [x] T001 Create test file structure at `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp`
- [x] T002 Add poly_synth_engine_test.cpp to `F:\projects\iterum\dsp\tests\CMakeLists.txt` (in Layer 3: Systems section)
- [x] T003 Add poly_synth_engine.h to `F:\projects\iterum\dsp\lint_all_headers.cpp` include list

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: SynthVoice modification that ALL user stories depend on for mono mode legato

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 SynthVoice setFrequency() Method Addition

- [x] T004 Write failing test for SynthVoice::setFrequency() in `F:\projects\iterum\dsp\tests\unit\systems\synth_voice_test.cpp`
  - Test setFrequency(880.0) updates oscillator frequencies without retriggering envelopes
  - Test pitch change during sustain maintains envelope level
  - Test NaN/Inf inputs are ignored (frequency unchanged)
- [x] T005 Implement setFrequency(float hz) in `F:\projects\iterum\dsp\include\krate\dsp\systems\synth_voice.h`
  - Add method after noteOff()
  - Guard against NaN/Inf using detail::isNaN/isInf
  - Update noteFrequency_, osc1_.setFrequency(), call updateOsc2Frequency()
  - DO NOT gate envelopes or update velocity
- [x] T006 Verify synth_voice tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice]"`
- [x] T007 Commit SynthVoice modification

**Checkpoint**: setFrequency() available - user story implementation can now begin

---

## Phase 3: User Story 1 - Polyphonic Playback with Voice Pool (Priority: P1) 🎯 MVP

**Goal**: A musician plays chords on their MIDI keyboard and hears multiple notes sounding simultaneously. The engine manages a pool of SynthVoice instances, dispatches note events via VoiceAllocator, and sums all active voice outputs.

**Independent Test**: Create engine, call prepare(), send noteOn for chord (60, 64, 67), process block, verify non-zero output. Send noteOff for all notes, verify voices release to silence and isActive() count becomes 0.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T008 [P] [US1] Write failing construction and constants tests in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` (FR-001, FR-002, FR-003, FR-004) [poly-engine][lifecycle]
  - Test kMaxPolyphony == 16
  - Test kMinMasterGain == 0.0, kMaxMasterGain == 2.0
  - Test default mode is VoiceMode::Poly
  - Test default polyphony is 8
- [x] T009 [P] [US1] Write failing lifecycle tests (FR-005, FR-006) [poly-engine][lifecycle]
  - Test prepare(44100.0, 512) initializes engine
  - Test reset() clears all voices (getActiveVoiceCount() == 0)
  - Test processBlock before prepare produces silence
- [x] T010 [P] [US1] Write failing poly mode note dispatch tests (FR-007, FR-008) [poly-engine][poly-mode]
  - Test noteOn(60, 100) triggers voice 0
  - Test chord (60, 64, 67) triggers 3 voices
  - Test noteOff(60) releases voice 0
  - Test getActiveVoiceCount() returns 3 after 3 noteOn calls
- [x] T011 [P] [US1] Write failing voice stealing test (FR-007, edge case) [poly-engine][poly-mode]
  - Set polyphony to 4, trigger 5 notes, verify 5th note produces sound (steal occurs)
- [x] T012 [P] [US1] Write failing processBlock tests (FR-026, FR-027) [poly-engine][processing]
  - Test processBlock with 3 active voices produces non-zero output
  - Test inactive voices are not processed (mock test or CPU measurement)
  - Test output contains summed audio from all active voices

### 3.2 Implementation for User Story 1

- [x] T013 [US1] Create PolySynthEngine header at `F:\projects\iterum\dsp\include\krate\dsp\systems\poly_synth_engine.h`
  - Includes: Layer 0 (sigmoid, db_utils), Layer 1 (svf), Layer 2 (mono_handler, note_processor), Layer 3 (voice_allocator, synth_voice)
  - Forward declare enums from dependencies: OscWaveform, SVFMode, EnvCurve, AllocationMode, StealMode, MonoMode, PortaMode, VelocityCurve
  - Define VoiceMode enum: Poly = 0, Mono = 1
  - Declare class PolySynthEngine with constants, members, methods per API contract
- [x] T014 [US1] Implement PolySynthEngine constructor
  - Initialize all members with defaults (mode_ = VoiceMode::Poly, polyphonyCount_ = 8, masterGain_ = 1.0f, gainCompensation_ = 1.0f / std::sqrt(8.0f), softLimitEnabled_ = true, globalFilterEnabled_ = false, sampleRate_ = 0.0, prepared_ = false)
  - Initialize timestampCounter_ = 0, noteOnTimestamps_ = {0}, monoVoiceNote_ = -1
- [x] T015 [US1] Implement prepare(double sampleRate, size_t maxBlockSize) (FR-005)
  - Call prepare() on all 16 voices
  - Call prepare() on allocator_, monoHandler_, noteProcessor_, globalFilter_
  - Resize scratchBuffer_ to maxBlockSize
  - Set sampleRate_, prepared_ = true
- [x] T016 [US1] Implement reset() (FR-006)
  - Call reset() on all 16 voices
  - Call reset() on allocator_, monoHandler_, noteProcessor_, globalFilter_
  - Clear scratchBuffer_
  - Reset timestampCounter_, noteOnTimestamps_, monoVoiceNote_
- [x] T017 [US1] Implement noteOn(uint8_t note, uint8_t velocity) for Poly mode (FR-007)
  - Guard: return early if !prepared_ or mode_ != VoiceMode::Poly
  - Forward to allocator_.noteOn(note, velocity), get VoiceEvent span
  - For each VoiceEvent:
    - If NoteOn: get frequency from `noteProcessor_.getFrequency(event.note)` (source of truth for audio frequency, applies tuning + pitch bend), get mapped velocity from `noteProcessor_.mapVelocity(static_cast<int>(velocity)).amplitude`, call `voices_[voiceIndex].noteOn(freq, vel)`, store `timestampCounter_++` in `noteOnTimestamps_[voiceIndex]`
    - If Steal: call `voices_[voiceIndex].noteOff()`, then `noteOn()` with new note data, update timestamp
    - If NoteOff (soft steal): call `voices_[voiceIndex].noteOff()`
- [x] T018 [US1] Implement noteOff(uint8_t note) for Poly mode (FR-008)
  - Guard: return early if !prepared_ or mode_ != VoiceMode::Poly
  - Forward to allocator_.noteOff(note), get VoiceEvent span
  - For each VoiceEvent of type NoteOff: call voices_[voiceIndex].noteOff()
- [x] T019 [US1] Implement processBlock(float* output, size_t numSamples) - Poly mode only (FR-026, FR-027, FR-028, FR-029)
  - Return early if !prepared_, fill output with zeros
  - In Poly mode:
    - Call noteProcessor_.processPitchBend() once per block
    - Track which voices were active (bool wasActive[kMaxPolyphony])
    - For each active voice: update frequency from NoteProcessor::getFrequency(allocator_.getVoiceNote(i)) via setFrequency() — ensures pitch bend affects already-playing voices in real time
    - Zero the output buffer
    - For each voice 0..polyphonyCount_-1: if isActive(), processBlock() into scratchBuffer_, sum into output
    - After processing, check each voice: if wasActive && !isActive(), call allocator_.voiceFinished(voiceIndex)
  - (Global filter, master gain, soft limit added in later phases)
- [x] T020 [US1] Implement getActiveVoiceCount() (FR-030)
  - Return allocator_.getActiveVoiceCount()
- [x] T021 [US1] Implement getMode() (FR-031)
  - Return mode_
- [x] T022 [US1] Add poly_synth_engine.h to `F:\projects\iterum\dsp\CMakeLists.txt` KRATE_DSP_SYSTEMS_HEADERS list

### 3.3 Verification for User Story 1

- [x] T023 [US1] Build dsp_tests target: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [x] T024 [US1] Run lifecycle tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][lifecycle]"`
- [x] T025 [US1] Run poly mode tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][poly-mode]"`
- [x] T026 [US1] Run processing tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][processing]"`
- [x] T027 [US1] Verify all User Story 1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [x] T028 [US1] Verify IEEE 754 compliance: Check if poly_synth_engine_test.cpp uses std::isnan/isfinite/isinf, add to -fno-fast-math list in `F:\projects\iterum\dsp\tests\CMakeLists.txt` if needed

### 3.5 Commit (MANDATORY)

- [x] T029 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 fully functional - polyphonic playback with voice pool and basic note dispatch

---

## Phase 4: User Story 2 - Configurable Polyphony Count (Priority: P1)

**Goal**: Configure the maximum number of simultaneous voices (1-16) to balance sound richness against CPU load.

**Independent Test**: Set polyphony to 4, play 4 notes (all sound), then reduce to 2, verify excess voices release and subsequent notes only allocate from reduced pool.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T030 [P] [US2] Write failing polyphony configuration tests in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` (FR-012) [poly-engine][polyphony]
  - Test setPolyphony(4), play 4 notes, all produce sound
  - Test setPolyphony(4), play 5 notes, verify voice stealing occurs (5th note sounds)
  - Test setPolyphony(8), 8 active voices, setPolyphony(4), verify voices 4-7 released
  - Test setPolyphony(1), play 2 notes, verify monophonic-via-allocator behavior (steal)
  - Test setPolyphony(0) clamps to 1, setPolyphony(20) clamps to 16

### 4.2 Implementation for User Story 2

- [x] T031 [US2] Implement setPolyphony(size_t count) in `F:\projects\iterum\dsp\include\krate\dsp\systems\poly_synth_engine.h` (FR-012)
  - Clamp count to [1, kMaxPolyphony]
  - Store in polyphonyCount_
  - Call allocator_.setVoiceCount(count), get returned NoteOff events
  - For each NoteOff event: call voices_[voiceIndex].noteOff()
  - Recalculate gainCompensation_ = 1.0f / std::sqrt(static_cast<float>(polyphonyCount_))

### 4.3 Verification for User Story 2

- [x] T032 [US2] Build dsp_tests target
- [x] T033 [US2] Run polyphony tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][polyphony]"`
- [x] T034 [US2] Verify all User Story 2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [x] T035 [US2] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 4.5 Commit (MANDATORY)

- [x] T036 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 work - configurable polyphony with dynamic voice count adjustment

---

## Phase 5: User Story 6 - Unified Parameter Forwarding (Priority: P2)

**Goal**: Set patch parameters once on the engine and have them applied to all voices uniformly.

**Independent Test**: Set filter cutoff to 500 Hz on engine, trigger note, verify voice uses 500 Hz cutoff.

### 5.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T037 [P] [US6] Write failing parameter forwarding tests in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` (FR-018) [poly-engine][parameters]
  - Test setOsc1Waveform(Square), trigger note, verify output contains square wave harmonics
  - Test setFilterCutoff(500.0), trigger note, verify high frequencies attenuated
  - Test setAmpRelease(200.0), noteOff, verify release time ~200ms
  - Test set parameter with 4 active voices, verify all voices updated (check output character)
  - Test set parameter before any noteOn, verify newly triggered voice inherits setting

### 5.2 Implementation for User Story 6

- [x] T038 [P] [US6] Implement oscillator parameter forwarding methods in `F:\projects\iterum\dsp\include\krate\dsp\systems\poly_synth_engine.h` (FR-018)
  - setOsc1Waveform(OscWaveform): iterate all 16 voices, call setOsc1Waveform()
  - setOsc2Waveform(OscWaveform): iterate all 16 voices, call setOsc2Waveform()
  - setOscMix(float): guard NaN/Inf, iterate all 16 voices, call setOscMix()
  - setOsc2Detune(float): guard NaN/Inf, iterate all 16 voices, call setOsc2Detune()
  - setOsc2Octave(int): iterate all 16 voices, call setOsc2Octave()
- [x] T039 [P] [US6] Implement filter parameter forwarding methods (FR-018)
  - setFilterType(SVFMode): iterate all 16 voices, call setFilterType()
  - setFilterCutoff(float): guard NaN/Inf, iterate all 16 voices, call setFilterCutoff()
  - setFilterResonance(float): guard NaN/Inf, iterate all 16 voices, call setFilterResonance()
  - setFilterEnvAmount(float): guard NaN/Inf, iterate all 16 voices, call setFilterEnvAmount()
  - setFilterKeyTrack(float): guard NaN/Inf, iterate all 16 voices, call setFilterKeyTrack()
- [x] T040 [P] [US6] Implement amplitude envelope parameter forwarding methods (FR-018)
  - setAmpAttack(float): guard NaN/Inf, iterate all 16 voices, call setAmpAttack()
  - setAmpDecay(float): guard NaN/Inf, iterate all 16 voices, call setAmpDecay()
  - setAmpSustain(float): guard NaN/Inf, iterate all 16 voices, call setAmpSustain()
  - setAmpRelease(float): guard NaN/Inf, iterate all 16 voices, call setAmpRelease()
  - setAmpAttackCurve(EnvCurve): iterate all 16 voices, call setAmpAttackCurve()
  - setAmpDecayCurve(EnvCurve): iterate all 16 voices, call setAmpDecayCurve()
  - setAmpReleaseCurve(EnvCurve): iterate all 16 voices, call setAmpReleaseCurve()
- [x] T041 [P] [US6] Implement filter envelope parameter forwarding methods (FR-018)
  - setFilterAttack(float): guard NaN/Inf, iterate all 16 voices, call setFilterAttack()
  - setFilterDecay(float): guard NaN/Inf, iterate all 16 voices, call setFilterDecay()
  - setFilterSustain(float): guard NaN/Inf, iterate all 16 voices, call setFilterSustain()
  - setFilterRelease(float): guard NaN/Inf, iterate all 16 voices, call setFilterRelease()
  - setFilterAttackCurve(EnvCurve): iterate all 16 voices, call setFilterAttackCurve()
  - setFilterDecayCurve(EnvCurve): iterate all 16 voices, call setFilterDecayCurve()
  - setFilterReleaseCurve(EnvCurve): iterate all 16 voices, call setFilterReleaseCurve()
- [x] T042 [P] [US6] Implement velocity routing forwarding method (FR-018)
  - setVelocityToFilterEnv(float): guard NaN/Inf, iterate all 16 voices, call setVelocityToFilterEnv()

### 5.3 Verification for User Story 6

- [x] T043 [US6] Build dsp_tests target
- [x] T044 [US6] Run parameter forwarding tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][parameters]"`
- [x] T045 [US6] Verify all User Story 6 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [x] T046 [US6] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 5.5 Commit (MANDATORY)

- [x] T047 [US6] Commit completed User Story 6 work

**Checkpoint**: User Stories 1, 2, AND 6 work - unified parameter control of all voices

---

## Phase 6: User Story 3 - Mono/Poly Mode Switching (Priority: P2)

**Goal**: Switch between monophonic and polyphonic playing modes during performance. Mono mode provides legato and portamento.

**Independent Test**: Set mono mode, play overlapping notes, verify single-voice behavior with legato. Switch to poly mode, verify multiple voices play simultaneously.

### 6.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T048 [P] [US3] Write failing mono mode note dispatch tests in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` (FR-009, FR-010) [poly-engine][mono-mode]
  - Test setMode(Mono), noteOn(60, 100), verify voice 0 plays note 60
  - Test mono mode with legato enabled, overlapping notes (noteOn 60, noteOn 64 before noteOff 60), verify envelope does not retrigger (no attack restart)
  - Test mono mode retrigger, noteOn 60, noteOff 60, noteOn 64, verify envelope retriggers (attack restart)
  - Test mono mode noteOff releases voice 0 when all notes released
  - Test mono mode returning to held note (noteOn 60, noteOn 64, noteOff 64), verify voice 0 plays note 60 without retrigger
- [x] T049 [P] [US3] Write failing portamento test (FR-011, SC-006) [poly-engine][mono-mode]
  - Test setPortamentoTime(100.0), setPortamentoMode(Always), noteOn 60, noteOn 72, measure frequency at 50ms midpoint, verify ~note 66 (370 Hz) within 10 cents
- [x] T050 [P] [US3] Write failing mode switching tests (FR-013, SC-007) [poly-engine][mode-switching]
  - Test poly with 3 active voices (60, 64, 67 in that order), switch to mono, verify voice playing note 67 continues (most recent), others released
  - Test poly->mono switch produces no discontinuities > -40 dBFS (peak abs sample-to-sample diff < 0.01)
  - Test mono mode with active note, switch to poly, verify voice continues, subsequent notes allocate via VoiceAllocator
  - Test setMode(Poly) when already Poly -> no-op, no disruption
  - Test setMode(Mono) when already Mono -> no-op

### 6.2 Implementation for User Story 3

- [x] T051 [US3] Implement mono mode configuration forwarding methods in `F:\projects\iterum\dsp\include\krate\dsp\systems\poly_synth_engine.h` (FR-014)
  - setMonoPriority(MonoMode mode): call monoHandler_.setMode(mode)
  - setLegato(bool enabled): call monoHandler_.setLegato(enabled)
  - setPortamentoTime(float ms): guard NaN/Inf, call monoHandler_.setPortamentoTime(ms)
  - setPortamentoMode(PortaMode mode): call monoHandler_.setPortamentoMode(mode)
- [x] T052 [US3] Implement noteOn for Mono mode in noteOn(uint8_t note, uint8_t velocity) (FR-009)
  - If mode_ == VoiceMode::Mono:
    - Forward to monoHandler_.noteOn(static_cast<int>(note), static_cast<int>(velocity)), get MonoNoteEvent
    - If isNoteOn == true:
      - Get frequency from monoHandler_.processPortamento() (current portamento position)
      - Get mapped velocity from noteProcessor_.mapVelocity(velocity).amplitude
      - If retrigger == true: call voices_[0].noteOn(freq, vel)
      - If retrigger == false (legato): call voices_[0].setFrequency(freq)
      - Store note in monoVoiceNote_
- [x] T053 [US3] Implement noteOff for Mono mode in noteOff(uint8_t note) (FR-010)
  - If mode_ == VoiceMode::Mono:
    - Forward to monoHandler_.noteOff(static_cast<int>(note)), get MonoNoteEvent
    - If isNoteOn == false: call voices_[0].noteOff()
    - If isNoteOn == true (returning to held note): get frequency from monoHandler_.processPortamento(), call voices_[0].setFrequency(freq)
- [x] T054 [US3] Update processBlock to handle Mono mode portamento (FR-011, FR-026)
  - In Mono mode, per sample:
    - Call monoHandler_.processPortamento() to get gliding frequency
    - Call voices_[0].setFrequency(glidingFreq)
    - Process voices_[0] if active
  - (Refactor processBlock to have separate poly and mono paths)
- [x] T055 [US3] Implement setMode(VoiceMode mode) (FR-013)
  - If mode == mode_, return (no-op)
  - If switching Poly -> Mono:
    - Find most recently triggered voice: scan noteOnTimestamps_, find index with max value
    - Get note from allocator_.getVoiceNote(mostRecentVoiceIndex)
    - If mostRecentVoiceIndex == 0: call noteOff on voices 1..polyphonyCount_-1 (voice 0 continues seamlessly)
    - Else: call noteOff on ALL voices (including mostRecentVoiceIndex), then immediately call noteOn on voices_[0] with the note/frequency/velocity (brief envelope restart, per FR-013)
    - **In BOTH branches**: initialize monoHandler_ with the surviving note: `monoHandler_.noteOn(static_cast<int>(note), static_cast<int>(velocity))` — this sets up the MonoHandler's note stack and portamento target
    - Set monoVoiceNote_ to note
  - If switching Mono -> Poly:
    - Call monoHandler_.reset()
    - Set monoVoiceNote_ = -1
  - Store mode_ = mode

### 6.3 Verification for User Story 3

- [x] T056 [US3] Build dsp_tests target
- [x] T057 [US3] Run mono mode tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][mono-mode]"`
- [x] T058 [US3] Run mode switching tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][mode-switching]"`
- [x] T059 [US3] Verify all User Story 3 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [x] T060 [US3] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 6.5 Commit (MANDATORY)

- [x] T061 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, 3, AND 6 work - mono/poly mode switching with legato and portamento

---

## Phase 7: User Story 4 - Global Filter (Priority: P3)

**Goal**: Enable a global post-mix filter that processes the summed output of all voices.

**Independent Test**: Enable global filter, set lowpass at 500 Hz, play bright chord, verify output has reduced high-frequency content vs filter disabled.

### 7.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T062 [P] [US4] Write failing global filter tests in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` (FR-019, FR-020, FR-021, SC-011) [poly-engine][global-filter]
  - Test globalFilterEnabled_ defaults to false
  - Test setGlobalFilterEnabled(false), process audio, verify filter not applied (no CPU overhead)
  - Test setGlobalFilterEnabled(true), setGlobalFilterCutoff(500.0), setGlobalFilterType(Lowpass), play sawtooth chord, verify energy above 2000 Hz at least 20 dB lower vs filter disabled (SC-011)
  - Test global filter with per-voice filters both active, verify processing order (per-voice -> sum -> global)
  - Test setGlobalFilterCutoff/Resonance/Type with NaN/Inf, verify values ignored (parameter unchanged)

### 7.2 Implementation for User Story 4

- [x] T063 [P] [US4] Implement global filter configuration methods in `F:\projects\iterum\dsp\include\krate\dsp\systems\poly_synth_engine.h` (FR-020, FR-021)
  - setGlobalFilterEnabled(bool enabled): store in globalFilterEnabled_
  - setGlobalFilterCutoff(float hz): guard NaN/Inf, clamp to [20.0, 20000.0], call globalFilter_.setCutoff(hz)
  - setGlobalFilterResonance(float q): guard NaN/Inf, clamp to [0.1, 30.0], call globalFilter_.setResonance(q)
  - setGlobalFilterType(SVFMode mode): call globalFilter_.setMode(mode)
- [x] T064 [US4] Update processBlock to apply global filter (FR-019, FR-026)
  - After voice summing, before master output:
    - If globalFilterEnabled_ == true: call globalFilter_.processBlock(output, numSamples)
    - Else: skip filter processing (bypass)

### 7.3 Verification for User Story 4

- [x] T065 [US4] Build dsp_tests target
- [x] T066 [US4] Run global filter tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][global-filter]"`
- [x] T067 [US4] Verify all User Story 4 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [x] T068 [US4] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 7.5 Commit (MANDATORY)

- [x] T069 [US4] Commit completed User Story 4 work

**Checkpoint**: User Stories 1-4 and 6 work - global post-mix filter for collective timbral shaping

---

## Phase 8: User Story 5 - Master Output with Soft Limiting (Priority: P3)

**Goal**: Prevent digital clipping when many voices are active simultaneously via gain compensation and soft limiting.

**Independent Test**: Set 8 voices at full velocity sawtooth, play dense chord, verify output never exceeds [-1.0, +1.0] with soft limiter enabled.

### 8.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T070 [P] [US5] Write failing gain compensation tests in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` (FR-022, FR-023, SC-005) [poly-engine][master-output]
  - Test setMasterGain(0.8), polyphony=8, verify effectiveGain = 0.8 / sqrt(8) ≈ 0.283
  - Test RMS output level with N voices (1, 2, 4, 8) playing same note at full velocity scales as sqrt(N) within 20% tolerance (SC-005)
  - Test setMasterGain with NaN/Inf, verify value ignored
  - Test setMasterGain(-1.0) clamps to 0.0, setMasterGain(3.0) clamps to 2.0
- [x] T071 [P] [US5] Write failing soft limiting tests (FR-024, FR-025, SC-003, SC-004) [poly-engine][master-output]
  - Test setSoftLimitEnabled(true), 16 voices full velocity sawtooth, verify no output sample > 1.0 or < -1.0 (SC-003)
  - Test setSoftLimitEnabled(false), allow clipping (output may exceed [-1, +1])
  - Test single voice at moderate velocity (0.5), soft limiter enabled, verify peak difference vs non-limited < 0.01 (SC-004 - limiter transparent at low levels)

### 8.2 Implementation for User Story 5

- [x] T072 [P] [US5] Implement master output configuration methods in `F:\projects\iterum\dsp\include\krate\dsp\systems\poly_synth_engine.h` (FR-022, FR-024)
  - setMasterGain(float gain): guard NaN/Inf, clamp to [kMinMasterGain, kMaxMasterGain], store in masterGain_
  - setSoftLimitEnabled(bool enabled): store in softLimitEnabled_
- [x] T073 [US5] Update setPolyphony to recalculate gain compensation (FR-023)
  - After setting polyphonyCount_, compute gainCompensation_ = 1.0f / std::sqrt(static_cast<float>(polyphonyCount_))
- [x] T074 [US5] Update processBlock to apply master gain and soft limiting (FR-025, FR-026)
  - After global filter (or after voice summing if no global filter):
    - Compute effectiveGain = masterGain_ * gainCompensation_
    - For each sample: output[i] *= effectiveGain
    - If softLimitEnabled_ == true: for each sample, output[i] = Sigmoid::tanh(output[i])

### 8.3 Verification for User Story 5

- [x] T075 [US5] Build dsp_tests target
- [x] T076 [US5] Run master output tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][master-output]"`
- [x] T077 [US5] Verify all User Story 5 tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [x] T078 [US5] Verify cross-platform compatibility (IEEE 754 compliance already checked in US1)

### 8.5 Commit (MANDATORY)

- [x] T079 [US5] Commit completed User Story 5 work

**Checkpoint**: All 6 user stories work - complete polyphonic synth engine with all features

---

## Phase 9: NoteProcessor & VoiceAllocator Config (Cross-Cutting)

**Purpose**: Configuration forwarding methods for shared components

### 9.1 Tests for Config Forwarding (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T080 [P] Write failing pitch bend tests in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` (FR-016, FR-017) [poly-engine][config]
  - Test setPitchBend(0.5) with range 2 semitones, noteOn 60, verify frequency ~277.18 Hz (60 + 1 semitone)
  - Test setPitchBendRange, setTuningReference, setVelocityCurve, verify forwarding to NoteProcessor
- [x] T081 [P] Write failing allocator config tests (FR-015) [poly-engine][config]
  - Test setAllocationMode, setStealMode, verify forwarding to VoiceAllocator (check behavior with test notes)

### 9.2 Implementation for Config Forwarding

- [x] T082 [P] Implement NoteProcessor config forwarding in `F:\projects\iterum\dsp\include\krate\dsp\systems\poly_synth_engine.h` (FR-016)
  - setPitchBendRange(float semitones): guard NaN/Inf, call noteProcessor_.setPitchBendRange(semitones)
  - setTuningReference(float a4Hz): guard NaN/Inf, call noteProcessor_.setTuningReference(a4Hz)
  - setVelocityCurve(VelocityCurve curve): call noteProcessor_.setVelocityCurve(curve)
- [x] T083 [P] Implement setPitchBend(float bipolar) (FR-017)
  - Guard NaN/Inf, clamp to [-1.0, 1.0]
  - Forward to noteProcessor_.setPitchBend(bipolar)
  - If mode_ == Poly: also forward to VoiceAllocator (compute semitones from pitchBendRange, call allocator_.setPitchBend(semitones))
- [x] T084 [P] Implement VoiceAllocator config forwarding (FR-015)
  - setAllocationMode(AllocationMode mode): call allocator_.setAllocationMode(mode)
  - setStealMode(StealMode mode): call allocator_.setStealMode(mode)

### 9.3 Verification for Config Forwarding

- [x] T085 Build dsp_tests target
- [x] T086 Run config tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][config]"`
- [x] T087 Verify all config tests pass

### 9.4 Commit (MANDATORY)

- [x] T088 Commit completed config forwarding work

**Checkpoint**: All configuration methods forwarded correctly

---

## Phase 10: Edge Cases & Safety (Cross-Cutting)

**Purpose**: Parameter safety and edge case handling across all user stories

### 10.1 Tests for Edge Cases (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T089 [P] Write failing edge case tests in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` (FR-032, FR-033, FR-034) [poly-engine][safety]
  - Test velocity 0 treated as noteOff (MIDI convention)
  - Test voice stealing with more notes than polyphony limit
  - Test poly->mono switch with all voices idle (no active note)
  - Test mono->poly switch with no active note
  - Test setMode same mode no-op (already in US3 tests)
  - Test prepare() called while voices playing -> all voices reset, output silence
  - Test processBlock before prepare -> output silence
  - Test all sample rates: 44100, 48000, 88200, 96000, 176400, 192000 Hz (SC-008)
  - Test NaN/Inf handling for all parameter setters (spot check key methods)

### 10.2 Implementation for Edge Cases

- [x] T090 [P] Verify edge case handling in noteOn/noteOff
  - velocity 0 -> noteOff (already handled by VoiceAllocator, verify)
- [x] T091 [P] Add guards to remaining methods
  - Verify all public setters have NaN/Inf guards where applicable

### 10.3 Verification for Edge Cases

- [x] T092 Build dsp_tests target
- [x] T093 Run safety tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][safety]"`
- [x] T094 Verify all edge case tests pass

### 10.4 Commit (MANDATORY)

- [x] T095 Commit completed edge case and safety work

**Checkpoint**: All edge cases handled safely

---

## Phase 11: Performance & Success Criteria

**Purpose**: Verify all success criteria are met

### 11.1 Tests for Success Criteria (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T096 [P] Write performance benchmark test in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` (SC-001) [poly-engine][performance]
  - Process 1 second of audio (44100 samples) at 44.1 kHz with 8 active voices
  - Voices: sawtooth, filter engaged (Q=5), both envelopes active in sustain
  - Measure average processing time per sample
  - Verify < 5% CPU (processing time < 5% of real-time duration)
- [x] T097 [P] Write memory footprint test (SC-010) [poly-engine][performance]
  - Verify sizeof(PolySynthEngine) excluding scratchBuffer_ heap allocation < 32768 bytes
- [x] T098 [P] Write dedicated acceptance tests for SC-002 and SC-012 in `F:\projects\iterum\dsp\tests\unit\systems\poly_synth_engine_test.cpp` [poly-engine][performance]
  - SC-002: Voice allocation latency — trigger noteOn, call processBlock in the same block, verify non-zero output (the note MUST produce audio within the same processBlock call, not deferred to a future block). Explicit test: `noteOn(60, 100)` then immediately `processBlock()` → output contains signal.
  - SC-012: Voice stealing correctness — fill all voices, trigger one more note, verify in the same processBlock call that (a) the new note produces sound, (b) the stolen voice's previous note stops, and (c) getActiveVoiceCount() equals the polyphony count. Explicit test: set polyphony=4, trigger 5 notes, processBlock → verify output contains frequency content from note 5, and the stolen voice no longer plays its old note.

### 11.2 Verification for Performance

- [x] T099 Build dsp_tests in Release mode: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [x] T100 Run performance benchmark: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine][performance]"`
- [x] T101 Verify SC-001 < 5% CPU is met
- [x] T102 Verify SC-010 < 32 KB memory footprint is met
- [x] T103 Run all acceptance tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine]"`
- [x] T104 Verify all success criteria are met

### 11.3 Commit (MANDATORY)

- [x] T105 Commit completed performance verification work

**Checkpoint**: All success criteria verified and met

---

## Phase 12: Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task

### 12.1 Architecture Documentation Update

- [x] T106 Update `F:\projects\iterum\specs\_architecture_\layer-3-systems.md` with PolySynthEngine class
  - Add new section for PolySynthEngine
  - Include: purpose, public API summary, file location
  - Add "when to use this" guidance (complete polyphonic synthesis engine for plugin integration)
  - Document VoiceMode enum
  - Note composition of VoiceAllocator, SynthVoice, MonoHandler, NoteProcessor
  - Note sibling systems (FMVoice for future multi-engine synth)

### 12.2 Final Commit

- [x] T107 Commit architecture documentation updates
- [x] T108 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 13: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 13.1 Run Clang-Tidy Analysis

- [x] T109 Run clang-tidy on DSP target: `powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 13.2 Address Findings

- [x] T110 Fix all errors reported by clang-tidy (blocking issues)
- [x] T111 Review warnings and fix where appropriate (use judgment for DSP code)
- [x] T112 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 14: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 14.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [x] T113 Review ALL 36 FR-xxx requirements from `F:\projects\iterum\specs\038-polyphonic-synth-engine\spec.md` against implementation in `F:\projects\iterum\dsp\include\krate\dsp\systems\poly_synth_engine.h`
  - Open poly_synth_engine.h and locate code for each FR-001 through FR-036
  - Record file path and line numbers for each requirement
- [x] T114 Review ALL 12 SC-xxx success criteria and verify measurable targets are achieved
  - Run tests and record actual measured values vs spec targets
  - SC-001: Measure CPU usage with 8 voices, verify < 5%
  - SC-003 through SC-012: Verify test results with actual measurements
- [x] T115 Search for cheating patterns in implementation:
  - Search for `// placeholder`, `// TODO`, `// stub` comments in poly_synth_engine.h
  - Verify no test thresholds were relaxed from spec requirements
  - Verify no features were quietly removed from scope

### 14.2 Fill Compliance Table in spec.md

- [x] T116 Update `F:\projects\iterum\specs\038-polyphonic-synth-engine\spec.md` "Implementation Verification" section
  - Fill Evidence column for each FR-001 through FR-036 with file path and line numbers
  - Fill Evidence column for each SC-001 through SC-012 with test names and actual measured values
  - Mark each requirement status: MET / NOT MET / PARTIAL / DEFERRED
- [x] T117 Mark overall status in spec.md: COMPLETE / NOT COMPLETE / PARTIAL
- [x] T118 Fill Completion Checklist in spec.md
- [x] T119 Write Honest Assessment section in spec.md

### 14.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [x] T120 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 15: Final Completion

**Purpose**: Final commit and completion claim

### 15.1 Final Commit

- [ ] T121 Commit all spec work to feature branch `038-polyphonic-synth-engine`
- [ ] T122 Verify all tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine]"`

### 15.2 Completion Claim

- [ ] T123 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - User Story 1 (Phase 3): MVP - complete first
  - User Story 2 (Phase 4): Extends US1 polyphony config
  - User Story 6 (Phase 5): Extends US1 with parameter forwarding (independent of other stories)
  - User Story 3 (Phase 6): Depends on US1, US6 (needs parameter forwarding for mono config)
  - User Story 4 (Phase 7): Extends US1 with global filter (independent)
  - User Story 5 (Phase 8): Extends US1 with master output (independent)
- **Config Forwarding (Phase 9)**: Depends on all user stories
- **Edge Cases (Phase 10)**: Depends on all user stories
- **Performance (Phase 11)**: Depends on all user stories
- **Documentation (Phase 12)**: Depends on all implementation complete
- **Static Analysis (Phase 13)**: Depends on all implementation complete
- **Completion Verification (Phase 14)**: Depends on all previous phases
- **Final Completion (Phase 15)**: Depends on Phase 14

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories (MVP!)
- **User Story 2 (P1)**: Extends US1 polyphony config - Should complete after US1
- **User Story 6 (P2)**: Extends US1 parameter forwarding - Can start after US1 (independent of US2)
- **User Story 3 (P2)**: Mono/poly mode - Depends on US1 and US6 (needs parameter forwarding for mono config)
- **User Story 4 (P3)**: Global filter - Can start after US1 (independent of US2, US3, US6)
- **User Story 5 (P3)**: Master output - Can start after US1 (independent of all other stories)

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
- Within each user story: All implementation tasks marked [P] can run in parallel (different setters, separate methods)
- After US1 completes: US2, US4, US5, US6 can start in parallel (independent)
- Phase 9, 10, 11: Many test writing and implementation tasks marked [P] can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T008: Write construction and constants tests (FR-001, FR-002, FR-003, FR-004)
Task T009: Write lifecycle tests (FR-005, FR-006)
Task T010: Write poly mode note dispatch tests (FR-007, FR-008)
Task T011: Write voice stealing test
Task T012: Write processBlock tests (FR-026, FR-027)

# After tests fail, implementation tasks in sequence:
Task T013: Create header structure
Task T014: Implement constructor
Task T015: Implement prepare()
Task T016: Implement reset()
Task T017: Implement noteOn (poly mode)
Task T018: Implement noteOff (poly mode)
Task T019: Implement processBlock (poly mode only)
Task T020: Implement getActiveVoiceCount()
Task T021: Implement getMode()
Task T022: Add to CMakeLists.txt
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (SynthVoice setFrequency modification)
3. Complete Phase 3: User Story 1 (basic poly playback)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Verify chords play, voices release correctly, voice stealing works

### Incremental Delivery

1. Complete Setup + Foundational → setFrequency() available
2. Add User Story 1 → Test independently → Basic poly playback works (MVP!)
3. Add User Story 2 → Test independently → Configurable polyphony works
4. Add User Story 6 → Test independently → Parameter forwarding works
5. Add User Story 3 → Test independently → Mono/poly mode switching works
6. Add User Story 4 → Test independently → Global filter works
7. Add User Story 5 → Test independently → Master output with soft limiting works
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Complete User Story 1 (MVP, blocking)
3. After US1:
   - Developer A: User Story 2 (polyphony config)
   - Developer B: User Story 6 (parameter forwarding)
   - Developer C: User Story 4 (global filter)
   - Developer D: User Story 5 (master output)
4. After US1 and US6:
   - Developer X: User Story 3 (mono mode - depends on US6)
5. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files/methods, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIV)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Build command: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- Test command: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[poly-engine]"`
