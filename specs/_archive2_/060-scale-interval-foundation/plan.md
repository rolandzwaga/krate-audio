# Implementation Plan: Scale & Interval Foundation (ScaleHarmonizer)

**Branch**: `060-scale-interval-foundation` | **Date**: 2026-02-17 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/060-scale-interval-foundation/spec.md`

## Summary

Add a `ScaleHarmonizer` class to Layer 0 (Core) of the KrateDSP shared library. This header-only component computes diatonic intervals for harmonizer effects: given an input MIDI note, a root key (0-11), a scale type (one of 8 diatonic scales plus Chromatic), and a diatonic step count, it returns the musically correct semitone shift. The algorithm uses precomputed constexpr lookup tables for O(1) performance. This is Phase 1 of the harmonizer roadmap and blocks Phase 4 (HarmonizerEngine).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Standard library only (`<array>`, `<cstdint>`, `<algorithm>`, `<cmath>`); plus Layer 0 utilities `pitch_utils.h` (for `frequencyToMidiNote()`) and `midi_utils.h` (for `kMinMidiNote`/`kMaxMidiNote`)
**Storage**: N/A (pure computation, no state persistence)
**Testing**: Catch2 (via `dsp_tests` target) *(Constitution Principle VIII: Testing Discipline)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform, no platform-specific code
**Project Type**: Shared library component (KrateDSP)
**Performance Goals**: < 0.1% CPU per Layer 0 primitive; O(1) per call with integer-only arithmetic on hot path
**Constraints**: Zero heap allocations, all methods noexcept, safe for real-time audio thread
**Scale/Scope**: Single header file (~200-300 lines), single test file (~500-800 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-design check: PASSED**

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | DSP library component, no plugin code |
| II. Real-Time Audio Thread Safety | PASS | All methods noexcept, zero allocations, no locks, no I/O, no exceptions |
| III. Modern C++ Standards | PASS | Header-only, constexpr, std::array, enum class, [[nodiscard]] |
| IV. SIMD & DSP Optimization | PASS | Integer-only arithmetic, no SIMD needed (see SIMD section below) |
| V. VSTGUI Development | N/A | No UI component |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code, standard C++20 only |
| VII. Project Structure & Build System | PASS | Layer 0 location correct, CMakeLists.txt integration planned |
| VIII. Testing Discipline | PASS | Comprehensive test suite planned, test-first development |
| IX. Layered DSP Architecture | PASS | Layer 0 with stdlib-only deps (plus other Layer 0 utilities) |
| X. DSP Processing Constraints | N/A | No audio processing (pure computation) |
| XI. Performance Budgets | PASS | Well under Layer 0 budget (< 0.1% CPU) |
| XII. Debugging Discipline | N/A | New component, no debugging needed |
| XIII. Test-First Development | PASS | Tests written before implementation per workflow |
| XIV. Living Architecture Documentation | PASS | layer-0-core.md update planned |
| XV. Pre-Implementation Research (ODR) | PASS | All searches performed, no conflicts (see below) |
| XVI. Honest Completion | PASS | Compliance table will be filled with real evidence |
| XVII. Framework Knowledge | N/A | No framework interaction |
| XVIII. Spec Numbering | PASS | 060 is next available number |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ScaleType, DiatonicInterval, ScaleHarmonizer

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ScaleType | `grep -r "class ScaleType\|enum.*ScaleType" dsp/ plugins/` | No | Create New |
| DiatonicInterval | `grep -r "struct DiatonicInterval" dsp/ plugins/` | No | Create New |
| ScaleHarmonizer | `grep -r "class ScaleHarmonizer" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all methods are class members)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none -- all methods on ScaleHarmonizer class) | -- | -- | -- | -- |

**Note**: The spec's "Existing Codebase Components" section confirmed no existing implementations. Search was performed across all code files in `dsp/` and `plugins/`. All matches were in specification/research documents only.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `frequencyToMidiNote()` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | Convert Hz to continuous MIDI note in `getSemitoneShift()` |
| `kMinMidiNote` | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | Lower bound for MIDI note clamping (value: 0) |
| `kMaxMidiNote` | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | Upper bound for MIDI note clamping (value: 127) |
| `quantizePitch(PitchQuantMode::Scale)` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | **NOT reused** -- serves different purpose (float-based pitch quantization for Shimmer). ScaleHarmonizer supersedes for harmonizer use cases. |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - All 33 files listed, no ScaleHarmonizer/ScaleType/DiatonicInterval found
- [x] `dsp/include/krate/dsp/primitives/` - No overlap
- [x] `specs/_architecture_/` - Component inventory checked, no harmonizer components exist yet
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - Contains `PitchQuantMode::Scale` which has partial overlap but is semantically distinct (see spec section "Assumptions & Existing Components")

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All three planned types (`ScaleType`, `DiatonicInterval`, `ScaleHarmonizer`) are unique and not found anywhere in the codebase. The `ScaleType` name is generic but safely scoped within `Krate::DSP` namespace. No other `ScaleType` enum exists in any DSP or plugin code.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| pitch_utils.h | `frequencyToMidiNote` | `[[nodiscard]] inline float frequencyToMidiNote(float hz) noexcept` | Yes -- read at line 126-129 |
| midi_utils.h | `kMinMidiNote` | `inline constexpr int kMinMidiNote = 0` | Yes -- read at line 38 |
| midi_utils.h | `kMaxMidiNote` | `inline constexpr int kMaxMidiNote = 127` | Yes -- read at line 42 |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - Full file read, all function signatures verified
- [x] `dsp/include/krate/dsp/core/midi_utils.h` - Constants verified

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `frequencyToMidiNote()` | Returns `float` (continuous MIDI note), not `int` | Must `std::round()` and cast to `int` before passing to `calculate()` |
| `frequencyToMidiNote()` | Returns `0.0f` for invalid input (hz <= 0), not `-1` | Check for `hz <= 0` before calling, or accept MIDI note 0 as result |
| C++ modulo `%` | Negative values: `-1 % 12 = -1` (not 11) in C++ | Use `((x % 12) + 12) % 12` pattern for positive modulo |

## Layer 0 Candidate Analysis

*This IS a Layer 0 feature, so this section documents what is being created at Layer 0.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `ScaleHarmonizer` (entire class) | Musical intelligence for harmonization | `scale_harmonizer.h` | HarmonizerEngine (L3 Phase 4), future scale-aware effects |
| `ScaleType` enum | Reusable scale type selector | `scale_harmonizer.h` (co-located) | HarmonizerEngine, potentially future "Scale Quantize" processor, UI scale selectors |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `calculate()` | Core method, needs class state (rootNote_, scale_) |
| `getScaleDegree()` | Query that depends on class state |
| `quantizeToScale()` | Query that depends on class state |
| `getSemitoneShift()` | Convenience wrapper around `calculate()` |

**Decision**: Everything lives in `scale_harmonizer.h`. The `ScaleType` enum and `DiatonicInterval` struct are defined at namespace scope (not nested in the class) for easy reuse by consumers.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV requires evaluating SIMD viability.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Pure computation, no state feedback between calls |
| **Data parallelism width** | 1 | Single input note -> single output interval per call |
| **Branch density in inner loop** | LOW | One branch for Chromatic vs diatonic mode; rest is array lookups and arithmetic |
| **Dominant operations** | Integer arithmetic + array lookup | Modulo, addition, subtraction, array indexing. No transcendentals. |
| **Current CPU budget vs expected usage** | < 0.1% budget vs near-zero expected | Integer modulo + array lookup is ~10-20 cycles per call. Even at 44.1kHz sample rate this is negligible. |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The ScaleHarmonizer performs integer arithmetic and constexpr array lookups. There is no data parallelism (one input note produces one output per call). The operation is so inexpensive (~10-20 CPU cycles) that SIMD overhead (register setup, shuffles) would exceed the computation itself. When used in a multi-voice harmonizer (Phase 4), SIMD should be applied at the voice-parallel pitch-shifting level, not at the interval calculation level.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Constexpr lookup tables | Eliminates any runtime initialization | LOW | YES (already planned) |
| Precomputed reverse lookup (pitch class -> degree) | O(1) nearest-degree lookup instead of O(7) search | LOW | YES (already planned) |
| No optimization needed | Algorithm is already O(1) integer arithmetic | -- | N/A |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 0 - Core Utilities

**Related features at same layer** (from harmonizer roadmap and existing code):
- `pitch_utils.h`: Existing pitch conversion utilities (semitonesToRatio, quantizePitch, frequencyToMidiNote)
- `midi_utils.h`: Existing MIDI constants and note-to-frequency conversion

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `ScaleType` enum | HIGH | HarmonizerEngine (L3, Phase 4), UI scale selector, future "Scale Quantize" processor | Keep in scale_harmonizer.h for now; extract if needed by 3+ consumers |
| `ScaleHarmonizer::getScaleIntervals()` | HIGH | Any component needing scale data (chord recognition, scale display) | Already static constexpr, easy to call from anywhere |
| `DiatonicInterval` struct | MEDIUM | HarmonizerEngine (primary consumer) | Keep in scale_harmonizer.h |
| `ScaleHarmonizer::quantizeToScale()` | MEDIUM | Could replace `quantizePitch(PitchQuantMode::Scale)` for configurable key/scale contexts | Keep as member function; don't replace existing utility |

### Detailed Analysis (for HIGH potential items)

**ScaleType enum** provides:
- Named constants for 8 diatonic scales + Chromatic passthrough
- `uint8_t` underlying type for efficient storage/parameter serialization
- Can be used as parameter value for VST3 `StringListParameter`

| Future Feature | Would Reuse? | Notes |
|----------------|--------------|-------|
| HarmonizerEngine (Phase 4) | YES | Primary consumer -- uses ScaleType for key/scale configuration |
| UI Scale Selector (Phase 5) | YES | Maps ScaleType values to dropdown entries |
| Chord Recognition (hypothetical) | MAYBE | Chord detection might use scale data but has different needs |

**Recommendation**: Keep in `scale_harmonizer.h` for now. If it becomes needed by UI parameter code independently, extract to a separate `scale_types.h`.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep all types in `scale_harmonizer.h` | Only one consumer now (future HarmonizerEngine). Extract when 2+ independent consumers need the types. |
| Do not modify existing `quantizePitch()` | It serves the Shimmer effect's pitch quantization and is semantically different (float-based, C-major only). ScaleHarmonizer supersedes for harmonizer use cases only. |
| Do not add `ScaleType` to `PitchQuantMode` enum | They are semantically distinct: PitchQuantMode controls pitch quantization strength, ScaleType selects a musical scale. Mixing them would conflate two different concerns. |

### Review Trigger

After implementing **Phase 4 (HarmonizerEngine)**, review this section:
- [ ] Does HarmonizerEngine need `ScaleType` independently from `ScaleHarmonizer`? If so, extract to `scale_types.h`
- [ ] Does the UI parameter system need `ScaleType` for `StringListParameter`? If so, may need to move enum to a shared location
- [ ] Any duplicated scale data between ScaleHarmonizer and other components? Consolidate.

## Project Structure

### Documentation (this feature)

```text
specs/060-scale-interval-foundation/
├── plan.md              # This file
├── research.md          # Phase 0: Algorithm research and decisions
├── data-model.md        # Phase 1: Entity definitions and relationships
├── quickstart.md        # Phase 1: Usage guide and build instructions
├── contracts/
│   └── scale_harmonizer_api.h  # Phase 1: Public API contract
└── tasks.md             # Phase 2: Implementation tasks (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── core/
│       └── scale_harmonizer.h     # NEW: Header-only implementation
└── tests/
    └── unit/
        └── core/
            └── scale_harmonizer_test.cpp  # NEW: Comprehensive test suite
```

### Files Modified

```text
dsp/tests/CMakeLists.txt                  # MODIFIED: Add test source + -fno-fast-math
specs/_architecture_/layer-0-core.md       # MODIFIED: Add ScaleHarmonizer documentation
```

**Structure Decision**: This follows the established monorepo pattern for Layer 0 components. Header-only implementation in `dsp/include/krate/dsp/core/`, tests in `dsp/tests/unit/core/`. Include path: `<krate/dsp/core/scale_harmonizer.h>`.

## Complexity Tracking

No constitution violations. No complexity tracking entries needed.

## Post-Design Constitution Re-Check

*Re-checked after Phase 1 design completion.*

| Check | Result |
|-------|--------|
| All methods noexcept? | Yes -- every method in the API contract is noexcept |
| Zero heap allocations? | Yes -- all data is constexpr or stack-local |
| Layer 0 deps only? | Yes -- depends on `<array>`, `<cstdint>`, `<algorithm>`, `<cmath>`, plus Layer 0 `pitch_utils.h` and `midi_utils.h` |
| No ODR conflicts? | Yes -- all three types are unique in the codebase |
| Test-first planned? | Yes -- test file is in the plan, tests written before implementation |
| Architecture docs updated? | Yes -- layer-0-core.md update is in the plan |
| Cross-platform? | Yes -- standard C++20 only, no platform-specific code |

**Post-design gate: PASSED**
