# Implementation Plan: Digital Delay Mode

**Branch**: `026-digital-delay` | **Date**: 2025-12-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/026-digital-delay/spec.md`

## Summary

**Primary Requirement**: Implement a Layer 4 Digital Delay Mode with three era presets (Pristine, 80s Digital, Lo-Fi), featuring transparent clean delays, vintage digital character, and creative lo-fi degradation. Must include program-dependent limiter, flexible LFO modulation with 6 waveform shapes, and tempo sync.

**Technical Approach**: Compose existing Layer 3 components (DelayEngine, FeedbackNetwork, CharacterProcessor) with the existing LFO primitive. Leverage CharacterProcessor's DigitalVintage mode for 80s/Lo-Fi eras, add a new Pristine bypass path for transparency, and integrate DynamicsProcessor for feedback limiting.

## Technical Context

**Language/Version**: C++20 (per Constitution Principle III)
**Primary Dependencies**:
- DelayEngine (Layer 3) - core delay with tempo sync
- FeedbackNetwork (Layer 3) - feedback path with filtering
- CharacterProcessor (Layer 3) - DigitalVintage mode exists
- DynamicsProcessor (Layer 2) - limiter functionality
- LFO (Layer 1) - already has all 6 waveforms (Sine, Triangle, Sawtooth, Square, SampleHold, SmoothRandom)
- BitCrusher, SampleRateReducer (Layer 1) - via CharacterProcessor
**Storage**: N/A (DSP, no persistence)
**Testing**: Catch2 (per existing test infrastructure)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - VST3 plugin
**Project Type**: Single project - VST3 plugin with DSP library
**Performance Goals**: < 1% CPU at 44.1kHz stereo (per Constitution Principle XI)
**Constraints**: noexcept, no allocations in process(), sample-accurate timing
**Scale/Scope**: Single Layer 4 feature, ~500-700 LOC estimated

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | PASS | Will use noexcept, pre-allocate in prepare() |
| III. Modern C++ | PASS | C++20, RAII, smart pointers not needed (stack objects) |
| IV. SIMD Optimization | PASS | Composing existing optimized primitives |
| VIII. Testing | PASS | Test-first, Catch2 framework |
| IX. Layered Architecture | PASS | Layer 4 composing Layer 0-3 only |
| X. DSP Constraints | PASS | Parameter smoothing, DC blocking via components |
| XI. Performance Budget | PASS | < 1% CPU target, will benchmark |
| XII. Test-First | PASS | Tests before implementation |
| XIII. Living Documentation | PASS | Will update ARCHITECTURE.md |
| XIV. ODR Prevention | PASS | See Codebase Research below |
| XV. Honest Completion | PASS | Will verify all FR/SC at completion |

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: DigitalDelay, DigitalEra (enum), LimiterCharacter (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| DigitalDelay | `grep -r "class DigitalDelay" src/` | No | Create New |
| DigitalEra | `grep -r "DigitalEra" src/` | No | Create New |
| LimiterCharacter | `grep -r "LimiterCharacter" src/` | No | Create New |
| ModulationWaveform | `grep -r "ModulationWaveform" src/` | No | NOT NEEDED - use existing `Waveform` enum from LFO |

**Utility Functions to be created**: None new - all exist

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" src/` | Yes | dsp/core/db_utils.h | Reuse |
| gainToDb | `grep -r "gainToDb" src/` | Yes | dsp/core/db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayEngine | dsp/systems/delay_engine.h | 3 | Core delay with tempo sync |
| FeedbackNetwork | dsp/systems/feedback_network.h | 3 | Feedback path |
| CharacterProcessor | dsp/systems/character_processor.h | 3 | DigitalVintage mode for 80s/Lo-Fi |
| DynamicsProcessor | dsp/processors/dynamics_processor.h | 2 | Program-dependent limiter |
| LFO | dsp/primitives/lfo.h | 1 | Modulation with all 6 waveforms |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing |
| BitCrusher | dsp/primitives/bit_crusher.h | 1 | Via CharacterProcessor |
| SampleRateReducer | dsp/primitives/sample_rate_reducer.h | 1 | Via CharacterProcessor |
| BlockContext | dsp/core/block_context.h | 0 | Tempo sync support |
| NoteValue | dsp/core/note_value.h | 0 | Note value calculations |
| dbToGain, gainToDb | dsp/core/db_utils.h | 0 | Level conversions |
| TimeMode | dsp/systems/delay_engine.h | 3 | Free/Synced enum - already exists |
| Waveform | dsp/primitives/lfo.h | 1 | LFO waveform enum - already exists |
| CharacterMode | dsp/systems/character_processor.h | 3 | Has DigitalVintage - reuse |

### Key Discovery: LFO Waveforms Already Exist

The existing `Waveform` enum in `lfo.h` already provides all 6 required modulation waveforms:
- `Sine` - FR-025
- `Triangle` - FR-026
- `Sawtooth` (can be used for Saw) - FR-027
- `Square` - FR-028
- `SampleHold` - FR-029
- `SmoothRandom` - FR-030

**Decision**: Use the existing `Waveform` enum directly instead of creating `ModulationWaveform`.

### Key Discovery: CharacterProcessor DigitalVintage Mode

CharacterProcessor already has `CharacterMode::DigitalVintage` which includes:
- BitCrusher (bit depth reduction)
- SampleRateReducer (sample rate reduction)

This can be used directly for 80s Digital and Lo-Fi eras with Age parameter controlling intensity.

**Decision**:
- Pristine era: Use CharacterMode::Clean (bypass)
- 80s Digital era: Use CharacterMode::DigitalVintage with moderate settings
- Lo-Fi era: Use CharacterMode::DigitalVintage with aggressive settings

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - Legacy utilities (no conflicts)
- [x] `src/dsp/core/` - Layer 0 core utilities (reuse db_utils)
- [x] `ARCHITECTURE.md` - Component inventory (up to date)
- [x] `src/dsp/features/` - Existing features (TapeDelay, BBDDelay patterns)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new types (DigitalDelay, DigitalEra, LimiterCharacter) are unique and not found in codebase. Key utilities like Waveform and CharacterMode already exist and will be reused rather than duplicated.

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | None identified | — | — |

**Analysis**: Digital Delay is primarily a composition feature. All required utilities already exist in Layer 0-1:
- dB conversions: `db_utils.h`
- Parameter smoothing: `smoother.h`
- LFO waveforms: `lfo.h`

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Era-specific parameter mapping | One-liner mappings specific to DigitalDelay era presets |
| Limiter engagement logic | Specific to this feature's feedback limiting behavior |

**Decision**: No new Layer 0 utilities needed. This is a pure composition feature.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md):
- PingPong Delay: Stereo alternating delays
- Multi-Tap Delay: Up to 16 taps with patterns
- Shimmer Delay: Pitch-shifted feedback
- Reverse Delay: Reversed buffer playback
- Granular Delay: FFT-based grain processing

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| DigitalEra enum | LOW | Only Digital Delay | Keep local |
| LimiterCharacter enum | MEDIUM | PingPong, Multi-Tap might want feedback limiting | Keep local, extract after 2nd use |
| Era-based CharacterProcessor integration | LOW | Other delays use different modes (Tape, BBD) | Keep local |

### Detailed Analysis (for MEDIUM potential items)

**LimiterCharacter (Soft/Medium/Hard knee)** provides:
- Soft: Gentle limiting (6dB knee)
- Medium: Balanced limiting (3dB knee)
- Hard: Aggressive limiting (0dB knee)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| PingPong Delay | MAYBE | Might want feedback limiting |
| Multi-Tap Delay | MAYBE | Might want feedback limiting |
| Shimmer Delay | YES | Likely needs limiting for pitch feedback |

**Recommendation**: Keep LimiterCharacter local to DigitalDelay. If Shimmer or other delays need similar functionality, consider extracting to a shared location.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | TapeDelay/BBDDelay don't share base - each composes differently |
| Keep DigitalEra local | Era presets are feature-specific |
| Keep LimiterCharacter local | Wait for second consumer before extracting |

### Review Trigger

After implementing **027-pingpong-delay** or **028-shimmer-delay**, review this section:
- [ ] Does sibling need LimiterCharacter or similar? -> Extract to shared location
- [ ] Does sibling use same CharacterProcessor integration? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/026-digital-delay/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output (minimal - mostly existing components)
├── checklists/
│   └── requirements.md  # Quality checklist
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/features/
├── tape_delay.h         # Existing Layer 4 feature
├── bbd_delay.h          # Existing Layer 4 feature
└── digital_delay.h      # NEW - Digital Delay implementation

tests/unit/features/
├── tape_delay_test.cpp  # Existing tests
├── bbd_delay_test.cpp   # Existing tests
└── digital_delay_test.cpp # NEW - Digital Delay tests
```

**Structure Decision**: Single header file in `src/dsp/features/` following the established pattern from TapeDelay and BBDDelay. Tests in corresponding `tests/unit/features/` directory.

## Design Decisions

### Era Implementation Strategy

| Era | CharacterMode | Age Mapping | Notes |
|-----|---------------|-------------|-------|
| Pristine | Clean | Ignored | Bypass CharacterProcessor entirely for transparency |
| 80s Digital | DigitalVintage | 0-50% maps to subtle SR reduction | Moderate vintage character |
| Lo-Fi | DigitalVintage | 0-100% maps to aggressive bit/SR reduction | Maximum degradation at 100% |

### Limiter Integration

The DynamicsProcessor will be used in the feedback path with these settings:
- Detection: Peak (transient-responsive)
- Knee: Configurable via LimiterCharacter enum
  - Soft: 6dB knee
  - Medium: 3dB knee
  - Hard: 0dB knee (hard limiting)
- Threshold: -0.5dBFS (prevent clipping)
- Ratio: 100:1 (true limiting)

### Modulation Strategy

Use existing LFO primitive with Waveform enum. Map spec FR-023 waveform names to LFO::Waveform:
- Sine -> Waveform::Sine
- Triangle -> Waveform::Triangle
- Saw -> Waveform::Sawtooth
- Square -> Waveform::Square
- Sample & Hold -> Waveform::SampleHold
- Random (smoothed) -> Waveform::SmoothRandom

## Complexity Tracking

> No Constitution Check violations requiring justification.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

## Next Steps

Run `/speckit.tasks` to generate the implementation task list.
