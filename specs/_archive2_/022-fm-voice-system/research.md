# Research: FM Voice System

**Feature Branch**: `022-fm-voice-system`
**Date**: 2026-02-05

## Research Questions Resolved

### Q1: Algorithm Routing Data Structure

**Decision**: Enum-indexed static constexpr adjacency list

**Rationale**:
- Testable independently of audio (validate edges/carriers without DSP)
- Data-driven clarity (algorithms are data, not control flow)
- Extensibility (adding algorithm 9 = one enum + one table entry)
- No runtime allocation
- Enables startup validation and potential visualization/analysis reuse

**Implementation**:
```cpp
// Compile-time algorithm definitions
enum class Algorithm : uint8_t {
    Stacked2Op = 0,
    Stacked4Op,
    Parallel2Plus2,
    Branched,
    Stacked3PlusCarrier,
    Parallel4,
    YBranch,
    DeepStack,
    kNumAlgorithms
};

struct ModulationEdge {
    uint8_t source;  // Modulator index (0-3)
    uint8_t target;  // Target index (0-3)
};

struct AlgorithmTopology {
    uint8_t carrierMask;        // Bitmask: bit i = operator i is carrier
    uint8_t feedbackOperator;   // Which operator has self-feedback (0-3)
    uint8_t numEdges;           // Number of modulation connections
    ModulationEdge edges[6];    // Max edges for 4 operators
    uint8_t processOrder[4];    // Operator processing order (modulators first)
};
```

**Alternatives Considered**:
1. **Runtime-built routing matrix**: More flexible but requires allocation, harder to validate
2. **Virtual function dispatch**: Cleaner syntax but vtable overhead in audio thread
3. **Template-based algorithms**: Maximum performance but code bloat, harder to switch at runtime

### Q2: Processing Order Computation

**Decision**: Precompute in static constexpr tables

**Rationale**:
- Zero runtime overhead during process()
- Compile-time validation ensures no cycles
- Each algorithm has known fixed topology

**Implementation**:
For each algorithm, the `processOrder` array lists operators from pure modulators to carriers:

| Algorithm | Processing Order | Explanation |
|-----------|-----------------|-------------|
| Stacked2Op | [1, 0, 2, 3] | 1 mods 0 (carrier), 2 and 3 unused |
| Stacked4Op | [3, 2, 1, 0] | Chain: 3->2->1->0 |
| Parallel2Plus2 | [1, 3, 0, 2] | 1->0, 3->2 (two parallel chains) |
| Branched | [1, 2, 0, 3] | 1,2 both mod 0, 3 unused |
| Stacked3PlusCarrier | [3, 2, 1, 0] | 3->2->1, 0 independent carrier |
| Parallel4 | [0, 1, 2, 3] | All carriers, any order |
| YBranch | [3, 1, 2, 0] | 3 feeds 1 and 2, both feed 0 |
| DeepStack | [3, 2, 1, 0] | Same topology as Stacked4Op but feedback on op 2 (mid-chain) |

### Q3: Phase Preservation on Algorithm Switch

**Decision**: Always preserve phases (no reset)

**Rationale**:
- Prevents audible clicks/glitches
- Enables real-time algorithm modulation (e.g., mod wheel control)
- Phase continuity is musically coherent (oscillators continue, only routing changes)
- Keeps API simple (no mode parameter)
- Users can call reset() explicitly if hard restart desired

**Implementation**:
`setAlgorithm()` modifies only the active routing topology, not operator state. The `reset()` method exists separately for explicit phase reset when needed (e.g., note-on retrigger).

### Q4: Carrier Output Normalization

**Decision**: Divide by carrier count: `output = sum / N`

**Rationale**:
- Ensures consistent perceived amplitude across all algorithms
- Prevents unexpected volume jumps when switching algorithms
- Maintains headroom and prevents clipping
- Musically stable, predictable, and testable behavior

**Implementation**:
```cpp
// In process():
float carrierSum = 0.0f;
int carrierCount = 0;
for (int i = 0; i < 4; ++i) {
    if (topology.carrierMask & (1 << i)) {
        carrierSum += operators_[i].lastRawOutput() * operators_[i].getLevel();
        ++carrierCount;
    }
}
return (carrierCount > 0) ? carrierSum / static_cast<float>(carrierCount) : 0.0f;
```

### Q5: Fixed Frequency Mode

**Decision**: Per-operator mode enum with both ratio and fixed frequency storage

**Rationale**:
- Each operator needs independent mode selection (FR-013)
- Switching between modes must be glitch-free (FR-014)
- Fixed frequency must be independent of voice base frequency (FR-017)

**Implementation**:
```cpp
enum class OperatorMode : uint8_t {
    Ratio = 0,  // frequency = baseFrequency * ratio
    Fixed = 1   // frequency = fixedFrequency (ignores base)
};

struct OperatorConfig {
    OperatorMode mode = OperatorMode::Ratio;
    float ratio = 1.0f;
    float fixedFrequency = 440.0f;
};
```

### Q6: DC Blocker Placement and Cutoff

**Decision**: Single DC blocker at voice output with 20Hz cutoff

**Rationale**:
- Per-operator DC blocking wastes CPU (only final output matters)
- 20Hz matches spec requirement (FR-028)
- Removes DC offset from asymmetric feedback waveforms
- Standard cutoff for audio DC blocking (below audible range)

**Implementation**:
```cpp
// Member variable
DCBlocker dcBlocker_;

// In prepare():
dcBlocker_.prepare(sampleRate, 20.0f);  // 20Hz cutoff per FR-028

// In process():
float output = ... // normalized carrier sum
return dcBlocker_.process(sanitize(output));
```

### Q7: Performance Optimization Strategies

**Decision**: Minimize branching in inner loop, precompute routing

**Best Practices Found**:

1. **Avoid virtual calls in process()**: Use direct member access to FMOperator instances
2. **Precompute processing order**: Static constexpr arrays indexed by algorithm
3. **Use fixed-size arrays**: `std::array<FMOperator, 4>` not `std::vector`
4. **Batch carrier summation**: Accumulate in single pass
5. **Inline sanitization**: Branchless pattern from UnisonEngine

**Measured FMOperator Performance**:
From existing test (SC-006): ~0.17% CPU for 1 second at 44.1kHz on reference hardware. With 4 operators, expect ~0.68% CPU, leaving margin for DC blocking and routing overhead.

## Component Analysis

### Existing Components to Compose

| Component | Location | Role in FMVoice | Notes |
|-----------|----------|-----------------|-------|
| FMOperator | processors/fm_operator.h | 4 instances as voice operators | Already handles feedback, ratio, level |
| DCBlocker | primitives/dc_blocker.h | Output DC blocking | Use 20Hz cutoff |
| detail::isNaN/isInf | core/db_utils.h | Parameter sanitization | Same pattern as FMOperator |

### Patterns from Layer 3 Systems

**From UnisonEngine** (reference for Layer 3 composition):
1. Fixed-size `std::array` for sub-components
2. `prepare()` / `reset()` lifecycle
3. Inline `sanitize()` helper using `std::bit_cast`
4. NaN/Inf parameter guarding using `detail::isNaN`/`detail::isInf`
5. Single-sample `process()` with block variant

**From FMOperator** (building block API):
1. `process(phaseModInput)` - takes external phase modulation
2. `lastRawOutput()` - pre-level output for modulator chaining
3. Parameter setters with clamping and NaN protection

## Algorithm Topology Details

### Algorithm 0: Stacked2Op (Simple 2->1)

```
Operators: [0:Carrier] [1:Mod] [2:unused] [3:unused]
Routing: 1 -> 0
Carriers: 0
Feedback: 1
```

**Use Case**: Bass, leads, learning FM. Classic 2-op DX7 patches.

### Algorithm 1: Stacked4Op (Full Chain)

```
Operators: [0:Carrier] [1:Mod] [2:Mod] [3:Mod]
Routing: 3 -> 2 -> 1 -> 0
Carriers: 0
Feedback: 3
```

**Use Case**: Rich leads, brass. Maximum harmonic complexity.

### Algorithm 2: Parallel2Plus2 (Two Stacks)

```
Operators: [0:Carrier] [1:Mod] [2:Carrier] [3:Mod]
Routing: 1 -> 0, 3 -> 2
Carriers: 0, 2
Feedback: 1
```

**Use Case**: Organ, pads. Two independent timbres mixed.

### Algorithm 3: Branched (Y into Carrier)

```
Operators: [0:Carrier] [1:Mod] [2:Mod] [3:unused]
Routing: 1 -> 0, 2 -> 0
Carriers: 0
Feedback: 2
```

**Use Case**: Bells, metallic sounds. Multiple modulation sources create complex inharmonic spectra.

### Algorithm 4: Stacked3PlusCarrier

```
Operators: [0:Carrier] [1:Carrier] [2:Mod] [3:Mod]
Routing: 3 -> 2 -> 1
Carriers: 0, 1
Feedback: 3
```

**Use Case**: E-piano, complex tones. 3-op stack mixed with pure carrier.

### Algorithm 5: Parallel4 (Additive)

```
Operators: [0:Carrier] [1:Carrier] [2:Carrier] [3:Carrier]
Routing: none
Carriers: 0, 1, 2, 3
Feedback: 0 (applies to op 0 only)
```

**Use Case**: Additive/organ sounds. Pure sine mixing.

### Algorithm 6: YBranch

```
Operators: [0:Carrier] [1:Mod/Carrier] [2:Carrier] [3:Mod]
Routing: 3 -> 1, 3 -> 2, 1 -> 0, 2 -> 0 (or simplified)
Carriers: 0, 2
Feedback: 3
```

**Use Case**: Complex evolving sounds. Single modulator feeds two paths.

### Algorithm 7: DeepStack

```
Operators: [0:Carrier] [1:Mod] [2:Mod+FB] [3:Mod]
Routing: 3 -> 2 -> 1 -> 0
Carriers: 0
Feedback: 2 (mid-chain feedback - differs from Stacked4Op)
```

**Use Case**: Aggressive leads, noise. Same topology as Stacked4Op but with mid-chain feedback on operator 2 instead of top-chain feedback on operator 3. This creates different harmonics as the feedback-generated partials are further modulated by operator 1 before reaching the carrier.

## Test Strategy

### Unit Tests (per FR-xxx)

1. **Lifecycle**: Default constructor produces silence, prepare() enables processing, reset() clears phases
2. **Algorithm Selection**: All 8 algorithms produce distinct output
3. **Operator Configuration**: Ratio, level, feedback setters work correctly
4. **Fixed Frequency Mode**: Operators track base frequency in ratio mode, ignore in fixed mode
5. **Phase Preservation**: Algorithm switch preserves phases (no clicks)
6. **Output Normalization**: Carrier sum divided by carrier count
7. **DC Blocking**: DC offset reduced by >= 40dB
8. **Stability**: Maximum feedback for 10 seconds produces no NaN/Inf

### Success Criteria Tests (per SC-xxx)

1. **SC-001**: Composition parity with raw FMOperator pair
2. **SC-002**: Algorithm switching < 1 sample latency
3. **SC-003**: Maximum feedback stable for 10 seconds
4. **SC-004**: All 8 algorithms produce distinct spectra
5. **SC-005**: DC offset reduced by >= 40dB
6. **SC-006**: process() < 1 microsecond at 48kHz
7. **SC-007**: Full voice < 0.5% CPU at 44.1kHz

## References

- [Yamaha DX7 chip reverse-engineering, part 4: how algorithms are implemented](http://www.righto.com/2021/12/yamaha-dx7-chip-reverse-engineering.html)
- [Yamaha TX81Z - Wikipedia](https://en.wikipedia.org/wiki/Yamaha_TX81Z)
- [Elektron Digitone Algorithms](https://support.elektron.se/support/solutions/articles/43000566579-algorithms)
- Existing codebase: FMOperator (spec 021), UnisonEngine (spec 020)
