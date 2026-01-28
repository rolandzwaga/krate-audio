# Implementation Plan: MorphEngine DSP System

**Branch**: `005-morph-system` | **Date**: 2026-01-28 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/005-morph-system/spec.md`

**Related Documents**:
- [specs/Disrumpo/plans-overview.md](../Disrumpo/plans-overview.md) - System architecture
- [specs/Disrumpo/dsp-details.md](../Disrumpo/dsp-details.md) - Morph algorithm specification
- [specs/Disrumpo/roadmap.md](../Disrumpo/roadmap.md) - Tasks T5.1-T5.17

---

## Summary

Implement the MorphEngine DSP system for Disrumpo that enables smooth blending between 2-4 distortion types within each frequency band. The system uses inverse distance weighting for weight computation, supports three morph modes (1D Linear, 2D Planar, 2D Radial), handles same-family parameter interpolation and cross-family parallel processing with equal-power crossfade, and integrates seamlessly with the existing BandProcessor.

---

## Technical Context

**Language/Version**: C++20 (per constitution)
**Primary Dependencies**:
- KrateDSP (OnePoleSmoother, equalPowerGains, crossfadeIncrement)
- Existing Disrumpo DSP (BandProcessor, DistortionAdapter, DistortionType, DistortionCategory, BandState)
**Storage**: N/A (real-time DSP, no persistence)
**Testing**: Catch2 (unit tests), ApprovalTests (artifact-free verification)
**Target Platform**: Windows, macOS, Linux (cross-platform per constitution)
**Project Type**: VST3 plugin DSP layer
**Performance Goals**:
- Weight computation < 100ns for 4 nodes (SC-001)
- No CPU spikes during rapid automation (SC-007)
- < 5% CPU for full plugin (constitution Principle XI)
**Constraints**:
- Real-time safe (no allocations in process)
- Max 16 active distortion instances globally (FR-019)
- Artifact-free transitions at all speeds

---

## Constitution Check

*GATE: Passed - No violations identified*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in audio callbacks - all buffers pre-allocated in prepare()
- [x] No locks/mutexes in audio thread - weight computation is pure math
- [x] No exceptions in audio thread - noexcept on all process methods

**Required Check - Principle IX (Layered Architecture):**
- [x] MorphEngine is Layer 3 (system) - depends on Layer 1 (primitives) only
- [x] Plugin-specific DSP stays in `plugins/disrumpo/src/dsp/`

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle VI (Cross-Platform):**
- [x] No platform-specific code - pure C++ DSP math
- [x] Uses VSTGUI cross-platform abstractions for any UI (separate spec)

---

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Complete - All planned types verified unique*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| MorphEngine | `grep -r "class MorphEngine" dsp/ plugins/` | No | Create New |
| MorphNode | `grep -r "struct MorphNode" dsp/ plugins/` | No (only in specs) | Create New |
| MorphWeight | N/A | N/A | Inline float array, not separate type |
| DistortionFamily | `grep -r "DistortionFamily" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| calculateMorphWeights | `grep -r "calculateMorphWeights" dsp/ plugins/` | No | - | Create in MorphEngine |
| equalPowerGains | `grep -r "equalPowerGains" dsp/ plugins/` | Yes | `dsp/core/crossfade_utils.h` | REUSE |
| crossfadeIncrement | `grep -r "crossfadeIncrement" dsp/ plugins/` | Yes | `dsp/core/crossfade_utils.h` | REUSE |
| OnePoleSmoother | `grep -r "OnePoleSmoother" dsp/ plugins/` | Yes | `dsp/primitives/smoother.h` | REUSE |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Morph position (X/Y) smoothing for manual control |
| equalPowerGains | `dsp/include/krate/dsp/core/crossfade_utils.h` | 0 | Cross-family output crossfade |
| crossfadeIncrement | `dsp/include/krate/dsp/core/crossfade_utils.h` | 0 | Calculate per-sample increment for transition zone fade |
| BandProcessor | `plugins/disrumpo/src/dsp/band_processor.h` | - | Existing per-band processor to integrate MorphEngine into |
| DistortionAdapter | `plugins/disrumpo/src/dsp/distortion_adapter.h` | - | Unified distortion interface for all 26 types |
| DistortionType | `plugins/disrumpo/src/dsp/distortion_types.h` | - | Enum for 26 distortion types |
| DistortionCategory | `plugins/disrumpo/src/dsp/distortion_types.h` | - | Category grouping (Saturation, Wavefold, etc.) |
| getCategory() | `plugins/disrumpo/src/dsp/distortion_types.h` | - | Map DistortionType to DistortionCategory |
| BandState | `plugins/disrumpo/src/dsp/band_state.h` | - | Existing per-band state (has morph fields, needs MorphNode array) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (crossfade_utils.h found)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (smoother.h found)
- [x] `plugins/disrumpo/src/dsp/` - Existing Disrumpo DSP (BandProcessor, DistortionAdapter found)
- [x] `specs/_architecture_/` - Component inventory checked

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (MorphEngine, MorphNode, DistortionFamily) are unique and not found in codebase. Key utility functions (equalPowerGains, OnePoleSmoother) already exist in Layer 0/1 and will be reused rather than duplicated.

---

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| equalPowerGains | (both) | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` | Yes |
| crossfadeIncrement | (single) | `[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept` | Yes |
| DistortionAdapter | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| DistortionAdapter | setType | `void setType(DistortionType type) noexcept` | Yes |
| DistortionAdapter | setParams | `void setParams(const DistortionParams& params) noexcept` | Yes |
| getCategory | (function) | `constexpr DistortionCategory getCategory(DistortionType type) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class (lines 134-273)
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - equalPowerGains, crossfadeIncrement (complete file)
- [x] `plugins/disrumpo/src/dsp/distortion_adapter.h` - DistortionAdapter class
- [x] `plugins/disrumpo/src/dsp/distortion_types.h` - DistortionType, DistortionCategory, getCategory()
- [x] `plugins/disrumpo/src/dsp/band_state.h` - BandState struct
- [x] `plugins/disrumpo/src/dsp/band_processor.h` - BandProcessor class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| equalPowerGains | Two overloads - one with out params, one returning pair | Use `equalPowerGains(pos, fadeOut, fadeIn)` for efficiency |
| crossfadeIncrement | Returns 1.0f for zero/negative duration | Check duration > 0 before calling |
| BandState | morphMode is int, needs cast to MorphMode enum | `static_cast<MorphMode>(bandState.morphMode)` |

---

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| N/A | - | - | - |

**Decision**: No new Layer 0 utilities needed. All required math (inverse distance weighting, equal-power crossfade) either exists in Layer 0 or is specific to morph algorithm.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateMorphWeights | Morph-specific algorithm, only used by MorphEngine |
| calculateTransitionZoneGains | Morph-specific 40-60% zone logic |

---

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 - System Components (MorphEngine composes multiple processors)

### Sibling Features Analysis

**Related features at same layer** (from ROADMAP.md):
- 006-sweep-system: Sweep-morph linking uses MorphEngine weights
- 008-modulation-system: Modulation targets morph X/Y parameters

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| MorphEngine | HIGH | Sweep-morph linking (006), UI (007) | Keep in plugin DSP, expose weights via getter |
| calculateMorphWeights | MEDIUM | Potentially 006 for sweep-morph curves | Keep as method, may extract if 006 needs it |
| DistortionFamily enum | HIGH | UI display, modulation routing | Create now, will be used by UI |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep MorphEngine in plugin/disrumpo/src/dsp/ | Plugin-specific composition, not generic DSP |
| Create DistortionFamily enum now | Will be needed by morph UI and is fundamental to interpolation strategy |
| Expose weights via getter | 006-sweep-system and 007-morph-ui will read weights for display/linking |

---

## Project Structure

### Documentation (this feature)

```text
specs/005-morph-system/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── morph_engine_api.md
│   └── morph_node.h
└── tasks.md             # Phase 2 output (separate command)
```

### Source Code

```text
plugins/disrumpo/
├── src/
│   └── dsp/
│       ├── distortion_types.h     # EXTEND: Add DistortionFamily enum, getFamily()
│       ├── band_state.h           # EXTEND: Add MorphNode array to BandState
│       ├── morph_node.h           # NEW: MorphNode struct
│       ├── morph_engine.h         # NEW: MorphEngine class
│       ├── morph_engine.cpp       # NEW: MorphEngine implementation
│       └── band_processor.h       # MODIFY: Integrate MorphEngine
└── tests/
    └── unit/
        ├── morph_weight_computation_test.cpp   # NEW: FR-001, SC-001, SC-005
        ├── morph_mode_test.cpp                 # NEW: FR-003, FR-004, FR-005
        ├── morph_interpolation_test.cpp        # NEW: FR-006, FR-007, SC-004
        └── morph_transition_test.cpp           # NEW: FR-008, FR-009, FR-011, SC-002, SC-003, SC-006
```

**Structure Decision**: Plugin-specific DSP in `plugins/disrumpo/src/dsp/`. Tests mirror DSP structure. No new KrateDSP components needed - MorphEngine is a composition layer.

---

## Data Model Summary

### New Types

**MorphNode** (per spec FR-002, dsp-details.md):
```cpp
struct MorphNode {
    int id = 0;                     // Unique identifier (0-3)
    DistortionType type = DistortionType::SoftClip;
    DistortionParams params;
    float posX = 0.0f;              // Position in morph space [0, 1]
    float posY = 0.0f;              // Position in morph space [0, 1]
};
```

**DistortionFamily** (per spec FR-016):
```cpp
enum class DistortionFamily : uint8_t {
    Saturation = 0,     // D01-D06: Transfer function interpolation
    Wavefold,           // D07-D09: Parameter interpolation
    Digital,            // D12-D14, D18-D19: Parameter interpolation
    Rectify,            // D10-D11: Parameter interpolation
    Dynamic,            // D15, D17: Parameter interpolation + envelope coupling
    Hybrid,             // D16, D26: Parallel blend with output crossfade
    Experimental        // D20-D25: Parallel blend with output crossfade
};
```

**MorphMode** (already referenced in BandState, formalize):
```cpp
enum class MorphMode : uint8_t {
    Linear1D = 0,       // Single axis A-B-C-D
    Planar2D,           // XY position in node space
    Radial2D            // Angle + distance from center
};
```

### Extended Types

**BandState** - Add MorphNode array:
```cpp
struct BandState {
    // ... existing fields ...
    std::array<MorphNode, 4> nodes;  // Up to 4 morph nodes (fixed-size for RT safety)
    int activeNodeCount = 2;         // How many nodes are active (2-4)
    MorphMode morphMode = MorphMode::Linear1D;
    float morphX = 0.5f;
    float morphY = 0.5f;
};
```

---

## Algorithm Summary

### Weight Computation (FR-001, dsp-details.md Section 7.1)

Inverse distance weighting with exponent p=2:
```
weight_i = 1 / distance_i^2
weights normalized to sum to 1.0
```

Special cases:
- Cursor exactly on node -> 100% weight to that node
- Weights below 0.001 threshold -> skip node, renormalize (FR-015)

### Morph Modes (FR-003, FR-004, FR-005)

1. **Linear1D**: Single-axis interpolation A-B-C-D using morphX only
2. **Planar2D**: 2D inverse distance weighting using morphX and morphY
3. **Radial2D**: Angle selects nodes, distance controls blend intensity

### Interpolation Strategy (FR-006, FR-007, FR-016, FR-018)

| Family | Interpolation Method |
|--------|---------------------|
| Saturation (D01-D06) | Sample-level transfer function blend: `w_A * f_A(x) + w_B * f_B(x)` |
| Wavefold (D07-D09) | Parameter interpolation |
| Digital (D12-D14, D18-D19) | Parameter interpolation |
| Rectify (D10-D11) | Parameter interpolation |
| Dynamic (D15, D17) | Parameter interpolation + envelope coupling |
| Hybrid (D16, D26) | Parallel blend with output crossfade |
| Experimental (D20-D25) | Parallel blend with output crossfade |

### Cross-Family Processing (FR-007, FR-008, dsp-details.md Section 7.2)

Transition zone model:
- 0-40%: Single algorithm (dominant)
- 40-60%: Both algorithms, equal-power crossfade
- 60-100%: Single algorithm (new dominant)

Fade-in on zone entry: 5-10ms to prevent clicks from cold filter states.

### Smoothing (FR-009)

- Manual control (user dragging): Smooth position (X/Y) before weight computation
- Automated drivers (Chaos, Envelope, etc.): Smooth weights directly

---

## Performance Considerations

### CPU Budget (Constitution Principle XI)

| Component | CPU Target | Notes |
|-----------|------------|-------|
| Weight computation (4 nodes) | < 100ns | Pure math, no branches in hot path |
| Same-family morph | < 0.5% CPU | Single distortion instance |
| Cross-family morph | < 1% CPU | 2 distortion instances in parallel |

### Global Processor Cap (FR-019)

Maximum 16 active distortion instances across all bands. With 5 bands morphing cross-family with 4 nodes each = 20 potential processors. Strategy:
1. Count active instances globally
2. When cap exceeded, raise weight threshold to reduce active nodes
3. Priority: highest-weight nodes stay active

### Memory

All buffers pre-allocated in prepare():
- MorphNode array: 4 nodes per band (fixed)
- DistortionAdapter instances: Pool for parallel processing
- Smoother state: 2 smoothers per band (X/Y)

---

## Test Strategy

### Unit Tests

| Test File | Coverage | Requirements |
|-----------|----------|--------------|
| morph_weight_computation_test.cpp | Weight math | FR-001, FR-014, FR-015, SC-001, SC-005 |
| morph_mode_test.cpp | Mode behaviors | FR-003, FR-004, FR-005 |
| morph_interpolation_test.cpp | Parameter/output blend | FR-006, FR-007, FR-016, FR-018, SC-004 |
| morph_transition_test.cpp | Crossfade, smoothing | FR-008, FR-009, FR-011, SC-002, SC-003, SC-006 |

### Integration Tests (from plans-overview.md)

| Test ID | Description | Requirements Covered |
|---------|-------------|---------------------|
| IT-005 | Morph automation | FR-012, SC-007 - Verify morph position responds correctly to host automation |
| IT-006 | Sweep + morph link | FR-010 - Verify sweep intensity multiplication happens before morph processing |

### Approval Tests

| Test ID | Description | Input | Verification |
|---------|-------------|-------|--------------|
| AP-003 | Morph artifact detection | Sine sweep during morph automation | No clicks/pops in output |

### Performance Tests

| Test | Target | Method |
|------|--------|--------|
| Weight computation benchmark | < 100ns for 4 nodes | 1M iterations, measure average |
| Rapid automation stress | No CPU spikes | 20Hz morph modulation, measure peak CPU |

---

## Signal Flow Architecture (from plans-overview.md)

### Per-Band Processing Pipeline

MorphEngine sits inside the oversampled section of each band's processing chain:

```
Band Input (from CrossoverNetwork)
    │
    ▼
Sweep Intensity Multiply ◄── Sweep value from SweepProcessor
    │
    ▼
Upsample (intelligent factor based on distortion types)
    │
    ▼
┌─────────────────────────────────┐
│     MorphEngine (this spec)     │
│  ┌─────────────────────────┐   │
│  │ 1. Compute Weights       │   │
│  │    A:0.4 B:0.35 C:0.25   │   │
│  └─────────────────────────┘   │
│         │                       │
│    ┌────┼────┐                  │
│    ▼    ▼    ▼                  │
│  Dist  Dist  Dist               │
│   A     B     C                 │
│    │    │    │                  │
│    └────┼────┘                  │
│         ▼                       │
│  Weighted Sum (Crossfade)       │
└─────────────────────────────────┘
    │
    ▼
Downsample
    │
    ▼
Band Gain/Pan/Mute
    │
    ▼
Band Output (to summation)
```

**Key Architecture Points:**
1. **Sweep applies BEFORE morph** - SweepProcessor modulates input gain before MorphEngine
2. **Upsample wraps MorphEngine** - Distortion processing happens at oversampled rate
3. **MorphEngine is per-band** - Each band has independent morph state and weights
4. **Gain/Pan after morph** - Final level control happens post-distortion

---

## Integration Points

### BandProcessor Integration

MorphEngine will be integrated into BandProcessor following the signal flow above:
1. BandProcessor owns MorphEngine instance
2. BandProcessor applies sweep intensity multiply BEFORE calling MorphEngine
3. BandProcessor calls `morphEngine.process()` at oversampled rate
4. MorphEngine processes audio through weighted distortion blend
5. Output fed to BandProcessor's gain/pan/mute stage AFTER downsampling

### Parameter Flow

```
Host/UI -> Processor atomics -> BandProcessor::update() -> MorphEngine::setMorphPosition()
                                                        -> MorphEngine::setNodes()
                                                        -> MorphEngine::setMode()
```

### Sibling Feature Hooks

- **006-sweep-system**: Will call `morphEngine.getWeights()` for sweep-morph linking
- **007-morph-ui**: Will call `morphEngine.getWeights()` for visualization
- **008-modulation-system**: Will set `morphEngine.setMorphPosition()` as modulation target

---

## Phase Implementation Summary

### Phase 0: Research (Complete)

- Reviewed inverse distance weighting algorithm
- Confirmed equal-power crossfade pattern exists in codebase
- Identified existing components to reuse

### Phase 1: Design

Deliverables:
- `data-model.md`: MorphNode, DistortionFamily, MorphMode definitions
- `contracts/morph_engine_api.md`: Public interface specification
- `contracts/morph_node.h`: Header contract for MorphNode
- `quickstart.md`: Usage examples

### Phase 2: Implementation (via tasks.md)

Task groups per ROADMAP.md T5.1-T5.17:
1. MorphNode and DistortionFamily data structures
2. MorphEngine class with weight computation
3. 1D Linear mode
4. 2D Planar mode
5. 2D Radial mode (P1 priority)
6. Same-family parameter interpolation
7. Cross-family parallel processing with equal-power crossfade
8. Transition zone fade (40-60%)
9. Morph smoothing (0-500ms)
10. BandProcessor integration
11. Global processor cap enforcement
12. Unit tests
13. Approval tests

---

## Risk Assessment (from plans-overview.md)

| Risk | Probability | Impact | Mitigation (this spec) |
|------|-------------|--------|------------------------|
| Morph artifacts | Medium | High | Extensive smoothing (FR-009), automated testing (AP-003), transition zones (FR-008) |
| CPU overload | Medium | High | Global 16-processor cap (FR-019), weight thresholding (FR-015), early profiling |

**Key Mitigations Implemented:**
- **Artifact prevention**: Equal-power crossfade in transition zones (40-60%), configurable smoothing (0-500ms), context-aware smoothing strategy
- **CPU management**: Weight threshold skips low-contribution nodes, global processor cap prevents runaway parallel processing

---

## Complexity Tracking

No constitution violations identified. Design follows all principles:
- Real-time safe (Principle II)
- Layered architecture (Principle IX)
- Pre-allocated buffers (Principle XI)
- Test-first development (Principle XII)
- ODR prevention via codebase research (Principle XIV)
