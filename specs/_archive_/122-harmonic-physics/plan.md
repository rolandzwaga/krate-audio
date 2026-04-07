# Implementation Plan: Harmonic Physics

**Branch**: `122-harmonic-physics` | **Date**: 2026-03-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/122-harmonic-physics/spec.md`

## Summary

Add a physics-based harmonic processing system to Innexus with three sub-systems: Warmth (tanh soft saturation), Coupling (nearest-neighbor energy sharing), and Dynamics (per-partial agent system with inertia and decay). All three operate as HarmonicFrame transforms between the existing frame source pipeline and `oscillatorBank_.loadFrame()`, modifying only partial amplitudes. Four new parameters (Warmth, Coupling, Stability, Entropy) default to 0.0 for bit-exact bypass.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (internal)
**Storage**: VST3 binary state (IBStreamer), version 7 (currently v6)
**Testing**: Catch2 via `innexus_tests` target *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: VST3 plugin (monorepo)
**Performance Goals**: Combined CPU < 0.5% single core @ 48kHz with 48 partials (SC-006)
**Constraints**: Real-time safe (no allocation/locks/exceptions in audio thread), zero compiler warnings
**Scale/Scope**: ~200 lines of new DSP code, 4 new parameters, 7 insertion points in processor.cpp

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in processFrame() -- uses fixed-size std::array
- [x] No locks or blocking primitives
- [x] No exceptions
- [x] No I/O or system calls
- [x] std::tanh is the only transcendental -- called 48 times per frame (acceptable)

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analyzed -- verdict: NOT BENEFICIAL (see SIMD section below)

**Required Check - Principle X (DSP Processing Constraints):**
- [x] Warmth uses tanh saturation but operates on harmonic amplitudes (not audio samples), so oversampling is NOT required. No DC offset is introduced because tanh(0) = 0 and the formula preserves zero-crossing.
- [x] No feedback loops in the physics chain

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

**Classes/Structs to be created**: HarmonicPhysics, AgentState

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| HarmonicPhysics | `grep -r "class HarmonicPhysics" dsp/ plugins/` | No | Create New |
| AgentState | `grep -r "struct AgentState" dsp/ plugins/` | No | Create New |
| PartialAgent | `grep -r "class PartialAgent\|struct PartialAgent" dsp/ plugins/` | No | Not creating (using AgentState arrays instead) |

**Parameter IDs to be created**: kWarmthId, kCouplingId, kStabilityId, kEntropyId

| Planned ID | Search Command | Existing? | Action |
|------------|----------------|-----------|--------|
| kWarmthId | `grep -r "kWarmthId" plugins/ dsp/` | No | Create New (700) |
| kCouplingId | `grep -r "kCouplingId" plugins/ dsp/` | No | Create New (701) |
| kStabilityId | `grep -r "kStabilityId" plugins/ dsp/` | No | Create New (702) |
| kEntropyId | `grep -r "kEntropyId" plugins/ dsp/` | No | Create New (703) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| HarmonicFrame | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Input/output data structure for all three processors |
| Partial | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Per-partial amplitude field is the primary target |
| kMaxPartials (=48) | `dsp/include/krate/dsp/processors/harmonic_types.h:21` | 2 | Array sizing for agent state |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h:133` | 1 | Smoothing for 4 new parameters |
| applyModulatorAmplitude pattern | `plugins/innexus/src/processor/processor.cpp:1619` | N/A | Insertion point pattern to follow |
| HarmonicModulator pattern | `plugins/innexus/src/dsp/harmonic_modulator.h` | N/A | Class design pattern (header-only, prepare/reset/process) |
| EvolutionEngine pattern | `plugins/innexus/src/dsp/evolution_engine.h` | N/A | Plugin-local DSP class instantiation pattern |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (OnePoleSmoother reused)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 (HarmonicFrame/Partial reused)
- [x] `specs/_architecture_/` - Component inventory checked
- [x] `plugins/innexus/src/dsp/` - No existing harmonic physics classes
- [x] `plugins/innexus/src/plugin_ids.h` - IDs 700-703 confirmed free

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (`HarmonicPhysics`, `AgentState`) are unique and not found anywhere in the codebase. They live in the `Innexus` namespace, further reducing collision risk. The only reused types are `HarmonicFrame` and `Partial` from KrateDSP, which are well-established stable APIs.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| HarmonicFrame | partials | `std::array<Partial, kMaxPartials> partials{}` | Yes |
| HarmonicFrame | numPartials | `int numPartials = 0` | Yes |
| HarmonicFrame | globalAmplitude | `float globalAmplitude = 0.0f` | Yes |
| Partial | amplitude | `float amplitude = 0.0f` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | advanceSamples | `void advanceSamples(size_t numSamples) noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| kMaxPartials | constant | `inline constexpr size_t kMaxPartials = 48` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/harmonic_types.h` - HarmonicFrame, Partial, kMaxPartials
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother full class
- [x] `plugins/innexus/src/plugin_ids.h` - ParameterIds enum
- [x] `plugins/innexus/src/processor/processor.h` - Processor class members
- [x] `plugins/innexus/src/dsp/harmonic_modulator.h` - Design pattern reference
- [x] `plugins/innexus/src/parameters/innexus_params.h` - Parameter registration pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | `snapTo()` sets BOTH current and target | Use for state restore; use `setTarget()` for normal updates |
| OnePoleSmoother | `reset()` zeros both values | Only call on full processor reset, not parameter changes |
| HarmonicFrame | `numPartials` can be 0 | All loops must use `numPartials` as bound, not `kMaxPartials` |
| Partial::amplitude | Linear scale, not dB | No conversion needed; tanh operates on linear values |
| processParameterChanges | Uses atomics (relaxed order) | Store with `std::memory_order_relaxed`, load same |
| State version | Currently 6 (M6) | Must increment to 7, with `if (version >= 7)` guard |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| applyWarmth | Innexus-specific, operates on HarmonicFrame, only 1 consumer |
| applyCoupling | Innexus-specific, only 1 consumer |
| applyDynamics | Stateful, Innexus-specific, only 1 consumer |

**Decision**: No Layer 0 extraction needed. All functions are Innexus-specific HarmonicFrame transforms with a single consumer. If Spec B (Harmonic Space) or Spec C (Emergent Behaviors) need similar transforms, extraction can happen then.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Each partial is independent within a frame (coupling reads from input copy) |
| **Data parallelism width** | 48 partials | Good width but tiny total work |
| **Branch density in inner loop** | LOW | Only bypass checks at function entry |
| **Dominant operations** | transcendental (tanh) | 48 tanh calls per frame in warmth |
| **Current CPU budget vs expected usage** | 0.5% budget vs ~0.01% expected | Massive headroom |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The harmonic physics system processes only 48 floats once per analysis frame (typically every 512-2048 samples). The total computation per frame is ~100 multiply-adds plus 48 tanh calls. At 48kHz with hop size 512, that's ~94 frames/second -- negligible CPU regardless of scalar vs SIMD. The overhead of setting up SIMD registers would dominate the actual computation time.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when param = 0.0 | ~100% for bypass case | LOW | YES (required by FR-002/FR-007/FR-013) |
| Skip dynamics when stability=0 AND entropy=0 | ~33% when dynamics unused | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Plugin-local DSP (Innexus-specific)

**Related features at same layer** (from roadmap):
- Spec B (Harmonic Space): Spatial/stereo processing on HarmonicFrames
- Spec C (Emergent Behaviors): Higher-order emergent patterns building on physics

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Energy conservation normalization | MEDIUM | Spec B (spatial might need energy-preserving transforms) | Keep local |
| AgentState per-partial state pattern | HIGH | Spec C (emergent behaviors will need per-partial state) | Keep local, extract when Spec C starts |
| HarmonicPhysics::processFrame() API | HIGH | Spec C (may extend the physics chain) | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep all code in HarmonicPhysics class | Single consumer (Innexus processor), no proven reuse yet |
| SoA layout for AgentState | Cache-friendly for future SIMD if Spec C needs it; also cleaner code |

### Review Trigger

After implementing **Spec B (Harmonic Space)**, review:
- [ ] Does Spec B need energy conservation normalization? Extract to shared utility
- [ ] Does Spec B use similar per-partial state? Consider shared base

After implementing **Spec C (Emergent Behaviors)**, review:
- [ ] Does Spec C extend AgentState? Extract to shared location
- [ ] Does Spec C add to the physics chain? Consider chain abstraction

## Project Structure

### Documentation (this feature)

```text
specs/122-harmonic-physics/
  plan.md              # This file
  research.md          # Phase 0 output
  data-model.md        # Phase 1 output
  quickstart.md        # Phase 1 output
  contracts/           # Phase 1 output
    harmonic-physics-api.md
  tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
plugins/innexus/
  src/
    dsp/
      harmonic_physics.h           # NEW: HarmonicPhysics class (header-only)
    plugin_ids.h                   # MODIFY: Add kWarmthId(700)-kEntropyId(703)
    processor/
      processor.h                  # MODIFY: Add member, atomics, smoothers
      processor.cpp                # MODIFY: Wire physics at 7 loadFrame sites
    controller/
      controller.cpp               # MODIFY: Register 4 parameters
    parameters/
      innexus_params.h             # MODIFY: Add save/load/handle for 4 params
  tests/
    CMakeLists.txt                 # MODIFY: Register new test files
    unit/processor/
      test_harmonic_physics.cpp    # NEW: Unit tests for all 3 processors
    integration/
      test_harmonic_physics_integration.cpp  # NEW: Pipeline integration tests
    unit/vst/
      test_state_v7.cpp            # NEW: State version 7 persistence tests
```

**Structure Decision**: Follows existing Innexus plugin-local DSP pattern. New DSP code in `src/dsp/`, tests mirroring the structure in `tests/unit/processor/` and `tests/integration/`.

## Detailed File Changes

### New Files

#### 1. `plugins/innexus/src/dsp/harmonic_physics.h`

Header-only class in `Innexus` namespace. Contains:
- `AgentState` struct with 4 arrays of `kMaxPartials` floats
- `HarmonicPhysics` class with:
  - `prepare(double sampleRate, int hopSize)`: compute persistence timing constants
  - `reset()`: clear agent state, set `firstFrame_ = true`
  - `processFrame(HarmonicFrame& frame)`: chain Coupling -> Warmth -> Dynamics
  - `setWarmth/setCoupling/setStability/setEntropy`: parameter setters
  - Private: `applyWarmth`, `applyCoupling`, `applyDynamics`

#### 2. `plugins/innexus/tests/unit/processor/test_harmonic_physics.cpp`

Unit tests for the `HarmonicPhysics` class in isolation. Test cases:
- **Warmth**: Bypass at 0.0, compression behavior, energy non-increase, zero-frame safety, peak-to-average ratio reduction (SC-003)
- **Coupling**: Bypass at 0.0, neighbor energy spread, energy conservation (SC-002), boundary handling, frequency preservation
- **Dynamics**: Bypass at 0.0/0.0, stability inertia (SC-004), entropy decay (SC-005), persistence growth/decay, reset behavior, first-frame initialization, energy budget conservation
- **Combined**: All three active, processing order verification
- **Performance**: Benchmark for SC-006

#### 3. `plugins/innexus/tests/integration/test_harmonic_physics_integration.cpp`

Full-pipeline integration tests using real `Processor`:
- Parameters applied via `processParameterChanges()`
- State save/load roundtrip for v7
- Verify physics transforms apply before loadFrame in actual process() call

#### 4. `plugins/innexus/tests/unit/vst/test_state_v7.cpp`

State persistence tests:
- Save v7, load v7 roundtrip
- Load v6 state into v7 code (backward compatibility, params default to 0.0)

### Modified Files

#### 5. `plugins/innexus/src/plugin_ids.h`

Add after `kBlendLiveWeightId = 649`:
```cpp
// Harmonic Physics (700-703) -- Spec A
kWarmthId = 700,       // 0.0-1.0, default 0.0
kCouplingId = 701,     // 0.0-1.0, default 0.0
kStabilityId = 702,    // 0.0-1.0, default 0.0
kEntropyId = 703,      // 0.0-1.0, default 0.0
```

#### 6. `plugins/innexus/src/processor/processor.h`

Add members:
```cpp
// Harmonic Physics (Spec A)
std::atomic<float> warmth_{0.0f};
std::atomic<float> coupling_{0.0f};
std::atomic<float> stability_{0.0f};
std::atomic<float> entropy_{0.0f};

// Harmonic Physics smoothers
Krate::DSP::OnePoleSmoother warmthSmoother_;
Krate::DSP::OnePoleSmoother couplingSmoother_;
Krate::DSP::OnePoleSmoother stabilitySmoother_;
Krate::DSP::OnePoleSmoother entropySmoother_;

// Harmonic Physics processor
HarmonicPhysics harmonicPhysics_;
```

Add method declaration:
```cpp
void applyHarmonicPhysics() noexcept;
```

#### 7. `plugins/innexus/src/processor/processor.cpp`

**processParameterChanges()**: Add 4 cases in switch statement:
```cpp
case kWarmthId:
    warmth_.store(static_cast<float>(value));
    break;
case kCouplingId:
    coupling_.store(static_cast<float>(value));
    break;
case kStabilityId:
    stability_.store(static_cast<float>(value));
    break;
case kEntropyId:
    entropy_.store(static_cast<float>(value));
    break;
```

**setupProcessing()**: Configure smoothers, prepare physics processor:
```cpp
warmthSmoother_.configure(kDefaultSmoothingTimeMs, sampleRate_);
couplingSmoother_.configure(kDefaultSmoothingTimeMs, sampleRate_);
stabilitySmoother_.configure(kDefaultSmoothingTimeMs, sampleRate_);
entropySmoother_.configure(kDefaultSmoothingTimeMs, sampleRate_);
harmonicPhysics_.prepare(sampleRate_, hopSize);
```

**process() per-block smoother updates**: Set targets from atomics, advance by block size:
```cpp
warmthSmoother_.setTarget(warmth_.load(std::memory_order_relaxed));
couplingSmoother_.setTarget(coupling_.load(std::memory_order_relaxed));
stabilitySmoother_.setTarget(stability_.load(std::memory_order_relaxed));
entropySmoother_.setTarget(entropy_.load(std::memory_order_relaxed));
warmthSmoother_.advanceSamples(numSamples);
couplingSmoother_.advanceSamples(numSamples);
stabilitySmoother_.advanceSamples(numSamples);
entropySmoother_.advanceSamples(numSamples);
```

**applyHarmonicPhysics()**: New method:
```cpp
void Processor::applyHarmonicPhysics() noexcept
{
    harmonicPhysics_.setWarmth(warmthSmoother_.getCurrentValue());
    harmonicPhysics_.setCoupling(couplingSmoother_.getCurrentValue());
    harmonicPhysics_.setStability(stabilitySmoother_.getCurrentValue());
    harmonicPhysics_.setEntropy(entropySmoother_.getCurrentValue());
    harmonicPhysics_.processFrame(morphedFrame_);
}
```

**Insertion at all 7 loadFrame sites**: After each `applyModulatorAmplitude()` call and before `oscillatorBank_.loadFrame()`, add:
```cpp
applyHarmonicPhysics();
```

Lines (approximate): 806, 859, 1017, 1072, 1141, 1457, 1497

**getState()**: Increment version to 7, append 4 floats after existing v6 data.

**setState()**: Add `if (version >= 7)` block to read 4 new params with defaults of 0.0.

**setActive(true)**: Snap physics smoothers and reset physics processor.

**Initialization**: In constructor or setupProcessing, snap smoothers to 0.0.

#### 8. `plugins/innexus/src/controller/controller.cpp`

Add 4 RangeParameter registrations after the blend parameters:
```cpp
// Harmonic Physics (Spec A)
auto* warmthParam = new Steinberg::Vst::RangeParameter(
    STR16("Warmth"), kWarmthId,
    STR16("%"), 0.0, 1.0, 0.0, 0,
    Steinberg::Vst::ParameterInfo::kCanAutomate);
parameters.addParameter(warmthParam);
// ... same for Coupling, Stability, Entropy
```

Add state loading in `setComponentState()` for `version >= 7`.

#### 9. `plugins/innexus/src/parameters/innexus_params.h`

**No changes required.** The harmonic physics parameters are NOT routed through `InnexusParams`. The creative extensions parameters (M6) are handled directly as atomics in `Processor`, and harmonic physics follows the same pattern for consistency: direct atomics in `Processor`, dispatched in `processParameterChanges()` switch, saved/loaded in `getState()`/`setState()`. `innexus_params.h` is unchanged by this feature.

#### 10. `plugins/innexus/tests/CMakeLists.txt`

Add new test files:
```cmake
# Harmonic Physics unit tests (Spec A)
unit/processor/test_harmonic_physics.cpp

# Harmonic Physics integration tests (Spec A)
integration/test_harmonic_physics_integration.cpp

# State v7 tests (Spec A)
unit/vst/test_state_v7.cpp
```

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| 7 insertion points in processor.cpp | MEDIUM | Encapsulate in `applyHarmonicPhysics()` method; all sites identical |
| Dynamics state not reset on note-on | HIGH | Wire `harmonicPhysics_.reset()` into existing note-on handler |
| tanh precision across platforms | LOW | Using std::tanh which is well-defined; tests use Approx().margin() |
| Energy conservation floating-point tolerance | LOW | SC-002 allows 0.001% tolerance; sqrt normalization is numerically stable |
| State version backward compatibility | LOW | Standard pattern: `if (version >= 7)` guard, defaults to 0.0 |
| First-frame initialization after reset | MEDIUM | `firstFrame_` flag copies input to agent state, preventing ramp-from-zero |
| Smoother interaction with frame-rate updates | LOW | Smoothers advance per audio block; physics reads current value at frame update |

## Phase Breakdown

### Phase 1: Milestone A1 - Warmth (Simplest, standalone)

1. Write unit tests for warmth transform (bypass, compression, energy, zero-frame)
2. Implement `HarmonicPhysics::applyWarmth()` and parameter setter
3. Add `kWarmthId` to plugin_ids.h
4. Wire into processor (atomic, smoother, processParameterChanges, applyHarmonicPhysics)
5. Register parameter in controller
6. Add state save/load (version 7)
7. Build, verify zero warnings, run tests

### Phase 2: Milestone A2 - Coupling (Stateless, energy-conserving)

1. Write unit tests for coupling (bypass, energy conservation, boundary, frequency preservation)
2. Implement `HarmonicPhysics::applyCoupling()` and parameter setter
3. Add `kCouplingId` to plugin_ids.h
4. Wire into processor (same pattern as warmth)
5. Register parameter in controller
6. Update state save/load
7. Build, verify zero warnings, run tests

### Phase 3: Milestone A3 - Dynamics (Stateful agent system)

1. Write unit tests for dynamics (bypass, stability inertia, entropy decay, persistence, reset, first-frame, energy budget)
2. Implement `HarmonicPhysics::applyDynamics()` with AgentState
3. Implement `prepare()` and `reset()`
4. Add `kStabilityId` and `kEntropyId` to plugin_ids.h
5. Wire into processor
6. Register parameters in controller
7. Update state save/load
8. Build, verify zero warnings, run tests

### Phase 4: Integration & Polish

1. Write integration tests (full processor pipeline)
2. Write state v7 persistence tests
3. Run pluginval at strictness 5
4. Run clang-tidy
5. Performance benchmark (SC-006)
6. Update architecture documentation
7. Final compliance verification

## Complexity Tracking

No constitution violations. All design decisions align with established patterns.
