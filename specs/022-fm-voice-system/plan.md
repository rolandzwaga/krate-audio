# Implementation Plan: FM Voice System

**Branch**: `022-fm-voice-system` | **Date**: 2026-02-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/022-fm-voice-system/spec.md`

## Summary

The FM Voice System is a Layer 3 component that composes 4 FMOperator instances (Layer 2) into a complete FM synthesis voice with 8 selectable algorithm topologies. It uses enum-indexed static constexpr adjacency lists for algorithm routing, preserves phases on algorithm switch for click-free transitions, and includes a DC blocker on the summed output.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- FMOperator (Layer 2) - existing at `dsp/include/krate/dsp/processors/fm_operator.h`
- DCBlocker (Layer 1) - existing at `dsp/include/krate/dsp/primitives/dc_blocker.h`
- FastMath::fastTanh (Layer 0) - existing at `dsp/include/krate/dsp/core/fast_math.h`
- db_utils (Layer 0) - existing for isNaN/isInf/sanitization patterns

**Storage**: N/A (in-memory DSP processing)
**Testing**: Catch2 (existing test framework)
**Target Platform**: Cross-platform (Windows, macOS, Linux)
**Project Type**: DSP library (monorepo: `dsp/` shared library)
**Performance Goals**:
- Single-sample process() < 1 microsecond at 48kHz
- Full voice < 0.5% single CPU core at 44.1kHz (per spec SC-007)
**Constraints**:
- Real-time safe (no allocation in process())
- Reference hardware: Intel i7-12700K @ 3.6 GHz, g++ 13.1 -O3 -march=native, Linux 6.x

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Pre-Design | Post-Design | Notes |
|-----------|------------|-------------|-------|
| I. VST3 Architecture Separation | N/A | N/A | DSP library component only |
| II. Real-Time Audio Thread Safety | PASS | PASS | process() no-alloc, uses RT-safe FMOperator |
| III. Modern C++ Standards | PASS | PASS | C++20, constexpr, [[nodiscard]], std::array |
| IV. SIMD & DSP Optimization | PASS | PASS | Sequential processing, precomputed order |
| V. VSTGUI Development | N/A | N/A | DSP library component only |
| VI. Cross-Platform Compatibility | PASS | PASS | No platform-specific code |
| VII. Project Structure & Build System | PASS | PASS | Header in systems/, test in unit/systems/ |
| VIII. Testing Discipline | PASS | PASS | Catch2, test-first approach |
| IX. Layered DSP Architecture | PASS | PASS | Layer 3 uses Layer 0-2 only |
| X. DSP Processing Constraints | PASS | PASS | DC blocking at output (20Hz) |
| XI. Performance Budgets | PASS | PASS | < 1% CPU target for voice |
| XII. Test-First Development | PASS | PASS | Tests before implementation |
| XIII. Living Architecture Documentation | PASS | PASS | Will update layer-3-systems.md |
| XIV. ODR Prevention | PASS | PASS | No conflicts found |
| XV. Honest Completion | PASS | PASS | Verification process defined |

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: FMVoice, Algorithm (enum class), AlgorithmTopology (struct)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FMVoice | `grep -r "class FMVoice\|struct FMVoice" dsp/ plugins/` | No | Create New |
| Algorithm | `grep -r "enum.*Algorithm\|class Algorithm" dsp/ plugins/` | No (only unrelated AliasingAlgorithm enum) | Create New |
| AlgorithmTopology | `grep -r "struct AlgorithmTopology" dsp/ plugins/` | No | Create New |
| OperatorMode | `grep -r "enum.*OperatorMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (will reuse existing sanitization patterns)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| sanitize | `grep -r "sanitize" dsp/include/` | Yes (local to each class) | FMOperator, UnisonEngine | Use same pattern |
| isNaN/isInf | `grep -r "isNaN\|isInf" dsp/include/` | Yes | db_utils.h detail namespace | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FMOperator | dsp/include/krate/dsp/processors/fm_operator.h | 2 | Compose 4 instances as voice operators |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | Output DC blocking (~20Hz cutoff) |
| FastMath::fastTanh | dsp/include/krate/dsp/core/fast_math.h | 0 | Already used by FMOperator for feedback |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN sanitization in parameter setters |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity sanitization in parameter setters |
| sanitize pattern | FMOperator, UnisonEngine | - | Branchless NaN detection and clamping pattern |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 DSP processors
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 DSP systems
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing FMVoice, Algorithm (in this context), AlgorithmTopology, or OperatorMode classes/structs found. The implementation will follow established Layer 3 patterns from UnisonEngine.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FMOperator | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| FMOperator | reset | `void reset() noexcept` | Yes |
| FMOperator | process | `[[nodiscard]] float process(float phaseModInput = 0.0f) noexcept` | Yes |
| FMOperator | lastRawOutput | `[[nodiscard]] float lastRawOutput() const noexcept` | Yes |
| FMOperator | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| FMOperator | setRatio | `void setRatio(float ratio) noexcept` | Yes |
| FMOperator | setFeedback | `void setFeedback(float amount) noexcept` | Yes |
| FMOperator | setLevel | `void setLevel(float level) noexcept` | Yes |
| FMOperator | getFrequency | `[[nodiscard]] float getFrequency() const noexcept` | Yes |
| FMOperator | getRatio | `[[nodiscard]] float getRatio() const noexcept` | Yes |
| FMOperator | getFeedback | `[[nodiscard]] float getFeedback() const noexcept` | Yes |
| FMOperator | getLevel | `[[nodiscard]] float getLevel() const noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| detail::isNaN | - | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | - | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/fm_operator.h` - FMOperator class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN, isInf utilities
- [x] `dsp/include/krate/dsp/core/fast_math.h` - fastTanh (used by FMOperator)
- [x] `dsp/include/krate/dsp/systems/unison_engine.h` - Layer 3 pattern reference

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| DCBlocker | Default cutoff is 10.0f Hz, spec requires ~20Hz | `dcBlocker_.prepare(sampleRate, 20.0f)` |
| FMOperator | Ratio is clamped to [0, 16.0] | Use setRatio with values in valid range |
| FMOperator | Feedback is clamped to [0, 1.0] | Use setFeedback with values in valid range |
| FMOperator | process() returns 0.0f before prepare() | Always call prepare() first |
| sanitize pattern | Uses std::bit_cast<uint32_t> | Include `<bit>` header |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | None identified | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| sanitize (branchless NaN/clamp) | Already duplicated in FMOperator, UnisonEngine - established pattern per class |
| computeOperatorOrder | Algorithm-specific, not general utility |

**Decision**: No new Layer 0 utilities needed. Reuse existing sanitize pattern inline as done in FMOperator and UnisonEngine.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from ROADMAP.md or known plans):
- Vector Mixer (OSC-ROADMAP Phase 17) - may share voice output mixing patterns
- Future FM-based effects or instruments
- Potential additive synthesis voice system

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| AlgorithmTopology struct | HIGH | Other modular synthesis systems | Keep local for now, extract if 2nd use case emerges |
| Static constexpr algorithm tables | MEDIUM | Other routing-based systems | Keep local, pattern can be copied |
| Carrier normalization pattern | LOW | Specific to FM synthesis | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep AlgorithmTopology local | First feature using this pattern - wait for concrete 2nd use case |
| Inline sanitize pattern | Established codebase practice per UnisonEngine/FMOperator |
| No shared base class | No clear shared interface with existing systems |

## Project Structure

### Documentation (this feature)

```text
specs/022-fm-voice-system/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output
    └── fm_voice.h       # API contract
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── systems/
│       └── fm_voice.h   # FMVoice implementation (header-only per convention)
└── tests/
    └── unit/
        └── systems/
            └── fm_voice_test.cpp  # Unit tests
```

**Structure Decision**: Header-only implementation in `dsp/include/krate/dsp/systems/fm_voice.h` following established conventions (UnisonEngine, etc.). Tests in `dsp/tests/unit/systems/fm_voice_test.cpp`.

## Complexity Tracking

> No Constitution Check violations requiring justification.

## Algorithm Routing Design

### Data Structure: Static Constexpr Adjacency List

The spec requires enum-indexed adjacency lists with compile-time validation. Based on the clarification, each algorithm is represented as:

```cpp
enum class Algorithm : uint8_t {
    Stacked2Op = 0,      // Simple 2->1 stack
    Stacked4Op = 1,      // Full 4->3->2->1 chain
    Parallel2Plus2 = 2,  // Two parallel 2-op stacks
    Branched = 3,        // Multiple mods to single carrier (3,2->1)
    Stacked3PlusCarrier = 4,  // 3-op stack + independent carrier
    Parallel4 = 5,       // All 4 as carriers (additive)
    YBranch = 6,         // Mod feeding two parallel stacks
    DeepStack = 7        // Deep modulation chain with mid-chain feedback (op 2)
};

struct ModulationEdge {
    uint8_t source;  // Modulator operator index (0-3)
    uint8_t target;  // Target operator index (0-3)
};

struct AlgorithmTopology {
    uint8_t carrierMask;           // Bitmask: bit i set = operator i is carrier
    uint8_t feedbackOperator;      // Which operator has feedback (0-3)
    uint8_t numEdges;              // Number of modulation edges
    ModulationEdge edges[6];       // Max 6 edges for 4 operators
};
```

### Algorithm Definitions (from spec)

| Algorithm | Index | Carriers | Edges | Feedback Op | Description |
|-----------|-------|----------|-------|-------------|-------------|
| Stacked2Op | 0 | 0b0001 (1) | 1->0 | 1 | 2->1 stack |
| Stacked4Op | 1 | 0b0001 (1) | 3->2, 2->1, 1->0 | 3 | 4-op chain |
| Parallel2Plus2 | 2 | 0b0101 (0,2) | 1->0, 3->2 | 1 | Two 2-op stacks |
| Branched | 3 | 0b0001 (1) | 1->0, 2->0 | 2 | Y into carrier |
| Stacked3PlusCarrier | 4 | 0b0011 (0,1) | 3->2, 2->1 | 3 | 3-stack + carrier |
| Parallel4 | 5 | 0b1111 (all) | none | 0 | Pure additive |
| YBranch | 6 | 0b0101 (0,2) | 3->1, 3->2, 1->0, 2->0... | 3 | Mod feeds two paths |
| DeepStack | 7 | 0b0001 (1) | 3->2, 2->1, 1->0 | 2 | Same topology, mid-chain feedback (op 2) |

### Processing Order

Operators must be processed in dependency order (modulators before carriers). For each algorithm, we precompute the processing order at compile time.

### Phase Preservation

Per clarification, algorithm switching MUST preserve operator phases. This is inherently satisfied because:
1. FMOperator phases are internal state (not reset on configuration change)
2. setAlgorithm() only changes routing, not operator state
3. reset() is separate and explicit

## Implementation Phases

### Phase 0: Research (Completed in this plan)

See research.md for detailed findings.

### Phase 1: Design & Contracts

1. **data-model.md**: Entity definitions (FMVoice, Algorithm, AlgorithmTopology, OperatorMode)
2. **contracts/fm_voice.h**: Full API contract header
3. **quickstart.md**: Usage examples

### Phase 2: Implementation Tasks (via /speckit.tasks)

1. Algorithm topology data structures and tables
2. FMVoice class skeleton with lifecycle methods
3. Algorithm routing and operator processing
4. Fixed frequency mode support
5. DC blocking integration
6. Success criteria verification

---

## Generated Artifacts (Phase 1 Complete)

| Artifact | Path | Status |
|----------|------|--------|
| research.md | `specs/022-fm-voice-system/research.md` | Complete |
| data-model.md | `specs/022-fm-voice-system/data-model.md` | Complete |
| fm_voice.h | `specs/022-fm-voice-system/contracts/fm_voice.h` | Complete |
| quickstart.md | `specs/022-fm-voice-system/quickstart.md` | Complete |

## Next Steps

Phase 2 (Implementation Tasks) should be initiated via `/speckit.tasks` command, which will generate:
- `specs/022-fm-voice-system/tasks.md` - Detailed implementation task breakdown

Implementation files to be created:
- `dsp/include/krate/dsp/systems/fm_voice.h` - Header-only implementation
- `dsp/tests/unit/systems/fm_voice_test.cpp` - Unit tests

Architecture documentation to be updated after implementation:
- `specs/_architecture_/layer-3-systems.md` - Add FMVoice entry
