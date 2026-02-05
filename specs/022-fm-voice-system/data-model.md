# Data Model: FM Voice System

**Feature Branch**: `022-fm-voice-system`
**Date**: 2026-02-05

## Entity Relationship Overview

```
FMVoice
├── operators_: std::array<FMOperator, 4>
│   └── Each FMOperator owns: WavetableData, WavetableOscillator
├── operatorConfigs_: std::array<OperatorConfig, 4>
│   └── Per-operator: mode, ratio, fixedFrequency
├── dcBlocker_: DCBlocker
├── currentAlgorithm_: Algorithm enum
└── References static: kAlgorithmTopologies[8]
```

## Entities

### Algorithm (Enum Class)

Identifies one of the 8 predefined routing topologies.

```cpp
enum class Algorithm : uint8_t {
    Stacked2Op = 0,           // Simple 2->1 stack
    Stacked4Op = 1,           // Full 4->3->2->1 chain
    Parallel2Plus2 = 2,       // Two parallel 2-op stacks
    Branched = 3,             // Multiple mods to single carrier
    Stacked3PlusCarrier = 4,  // 3-op stack + independent carrier
    Parallel4 = 5,            // All 4 as carriers (additive)
    YBranch = 6,              // Mod feeding two parallel stacks
    DeepStack = 7,            // Deep modulation chain
    kNumAlgorithms = 8        // Sentinel for iteration/validation
};
```

**Validation Rules**:
- Value must be < kNumAlgorithms (0-7)
- Invalid values silently ignored (preserve previous)

### OperatorMode (Enum Class)

Distinguishes ratio-tracking from fixed-frequency behavior.

```cpp
enum class OperatorMode : uint8_t {
    Ratio = 0,  // frequency = baseFrequency * ratio (default)
    Fixed = 1   // frequency = fixedFrequency (ignores base)
};
```

### ModulationEdge (Struct)

Single modulation connection between operators.

```cpp
struct ModulationEdge {
    uint8_t source;  // Modulator operator index (0-3)
    uint8_t target;  // Target operator index (0-3)
};
```

**Validation Rules**:
- source and target must be in range [0, 3]
- source != target (no self-modulation edges; feedback is separate)

### AlgorithmTopology (Struct)

Complete routing definition for one algorithm.

```cpp
struct AlgorithmTopology {
    uint8_t carrierMask;           // Bitmask: bit i set = operator i is carrier
    uint8_t feedbackOperator;      // Which operator has self-feedback (0-3)
    uint8_t numEdges;              // Number of valid modulation edges (0-6)
    ModulationEdge edges[6];       // Modulation connections (max 6 for 4 ops)
    uint8_t processOrder[4];       // Operator processing order (modulators first)
    uint8_t carrierCount;          // Precomputed count of carriers (popcount of mask)
};
```

**Invariants**:
- carrierMask has at least one bit set (every algorithm has >= 1 carrier)
- feedbackOperator is in range [0, 3]
- numEdges is in range [0, 6]
- processOrder is a permutation of {0, 1, 2, 3}
- carrierCount == popcount(carrierMask)

### OperatorConfig (Struct)

Per-operator configuration state within FMVoice.

```cpp
struct OperatorConfig {
    OperatorMode mode = OperatorMode::Ratio;
    float ratio = 1.0f;           // Used when mode == Ratio
    float fixedFrequency = 440.0f; // Used when mode == Fixed
};
```

**Validation Rules**:
- ratio clamped to [0.0, 16.0]
- fixedFrequency clamped to [0.0, Nyquist]
- NaN/Inf values ignored (preserve previous)

### FMVoice (Class)

Main system component orchestrating 4 operators with algorithm routing.

**Member Variables**:

```cpp
class FMVoice {
private:
    // Sub-components (owned)
    std::array<FMOperator, 4> operators_;    // The 4 FM operators
    std::array<OperatorConfig, 4> configs_;  // Per-operator configuration
    DCBlocker dcBlocker_;                    // Output DC blocking

    // Parameters
    Algorithm currentAlgorithm_ = Algorithm::Stacked2Op;
    float baseFrequency_ = 440.0f;           // Voice base frequency

    // State
    double sampleRate_ = 0.0;
    bool prepared_ = false;
};
```

**Lifecycle States**:

| State | Condition | process() Behavior |
|-------|-----------|-------------------|
| Unprepared | prepared_ == false | Returns 0.0f |
| Prepared | prepared_ == true | Normal processing |

**State Transitions**:

```
[Unprepared] --prepare()--> [Prepared]
[Prepared] --prepare()--> [Prepared] (re-initializes)
[Prepared] --reset()--> [Prepared] (phases cleared)
```

## Static Data: Algorithm Topology Tables

```cpp
static constexpr AlgorithmTopology kAlgorithmTopologies[8] = {
    // Algorithm 0: Stacked2Op - Simple 2->1 stack
    {
        .carrierMask = 0b0001,      // Operator 0 is carrier
        .feedbackOperator = 1,       // Op 1 has feedback
        .numEdges = 1,
        .edges = {{1, 0}},           // 1 -> 0
        .processOrder = {1, 0, 2, 3},
        .carrierCount = 1
    },
    // Algorithm 1: Stacked4Op - Full 4->3->2->1 chain
    {
        .carrierMask = 0b0001,
        .feedbackOperator = 3,
        .numEdges = 3,
        .edges = {{3, 2}, {2, 1}, {1, 0}},
        .processOrder = {3, 2, 1, 0},
        .carrierCount = 1
    },
    // Algorithm 2: Parallel2Plus2 - Two parallel 2-op stacks
    {
        .carrierMask = 0b0101,      // Operators 0 and 2 are carriers
        .feedbackOperator = 1,
        .numEdges = 2,
        .edges = {{1, 0}, {3, 2}},
        .processOrder = {1, 3, 0, 2},
        .carrierCount = 2
    },
    // Algorithm 3: Branched - Y into carrier (2,1->0)
    {
        .carrierMask = 0b0001,
        .feedbackOperator = 2,
        .numEdges = 2,
        .edges = {{1, 0}, {2, 0}},
        .processOrder = {1, 2, 0, 3},
        .carrierCount = 1
    },
    // Algorithm 4: Stacked3PlusCarrier - 3-stack + carrier
    {
        .carrierMask = 0b0011,      // Operators 0 and 1 are carriers
        .feedbackOperator = 3,
        .numEdges = 2,
        .edges = {{3, 2}, {2, 1}},
        .processOrder = {3, 2, 1, 0},
        .carrierCount = 2
    },
    // Algorithm 5: Parallel4 - All carriers (additive)
    {
        .carrierMask = 0b1111,
        .feedbackOperator = 0,
        .numEdges = 0,
        .edges = {},
        .processOrder = {0, 1, 2, 3},
        .carrierCount = 4
    },
    // Algorithm 6: YBranch - Mod feeding two paths
    {
        .carrierMask = 0b0101,      // Operators 0 and 2 are carriers
        .feedbackOperator = 3,
        .numEdges = 4,
        .edges = {{3, 1}, {3, 2}, {1, 0}, {2, 0}},
        .processOrder = {3, 1, 2, 0},
        .carrierCount = 2
    },
    // Algorithm 7: DeepStack - Deep modulation chain with mid-chain feedback
    // Differs from Stacked4Op: feedback on op 2 (middle) instead of op 3 (top)
    // This creates different harmonics as feedback is further modulated before carrier
    {
        .carrierMask = 0b0001,
        .feedbackOperator = 2,           // Mid-chain feedback (differs from Stacked4Op)
        .numEdges = 3,
        .edges = {{3, 2}, {2, 1}, {1, 0}},
        .processOrder = {3, 2, 1, 0},
        .carrierCount = 1
    }
};
```

## Processing Algorithm

```
process(void) -> float:
    IF not prepared THEN return 0.0f

    topology = kAlgorithmTopologies[currentAlgorithm_]

    // Phase 1: Compute operator frequencies
    FOR i = 0 TO 3:
        IF configs_[i].mode == Ratio:
            freq = baseFrequency_ * configs_[i].ratio
        ELSE:
            freq = configs_[i].fixedFrequency
        operators_[i].setFrequency(freq)

    // Phase 2: Set feedback (only on designated operator)
    // Note: FMOperator feedback is already set via setFeedback()

    // Phase 3: Process operators in dependency order
    float modulation[4] = {0, 0, 0, 0}

    FOR i = 0 TO topology.numEdges - 1:
        edge = topology.edges[i]
        // Accumulate modulation for target from source's raw output
        // (will be applied during process)

    FOR i IN topology.processOrder:
        // Process operator with accumulated modulation
        operators_[i].process(modulation[i])

        // Distribute this operator's output to its targets
        FOR edge IN topology.edges WHERE edge.source == i:
            modulation[edge.target] += operators_[i].lastRawOutput() * operators_[i].getLevel()

    // Phase 4: Sum carriers with normalization
    float carrierSum = 0.0f
    FOR i = 0 TO 3:
        IF topology.carrierMask & (1 << i):
            carrierSum += operators_[i].lastRawOutput() * operators_[i].getLevel()

    output = carrierSum / topology.carrierCount

    // Phase 5: DC blocking and sanitization
    output = dcBlocker_.process(output)
    output = sanitize(output)

    return output
```

## Parameter Ranges

| Parameter | Range | Default | Clamping |
|-----------|-------|---------|----------|
| algorithm | 0-7 | 0 (Stacked2Op) | Invalid -> preserve |
| baseFrequency | 0.0 - Nyquist | 440.0 Hz | NaN/Inf -> 0.0 |
| operatorRatio | 0.0 - 16.0 | 1.0 | Clamp to bounds |
| operatorLevel | 0.0 - 1.0 | 0.0 | Clamp to bounds |
| feedback | 0.0 - 1.0 | 0.0 | Clamp to bounds |
| fixedFrequency | 0.0 - Nyquist | 440.0 Hz | Clamp, NaN -> preserve |

## Memory Layout

```
FMVoice instance (~360 KB total):
├── operators_[0]: FMOperator (~90 KB for wavetable)
├── operators_[1]: FMOperator (~90 KB)
├── operators_[2]: FMOperator (~90 KB)
├── operators_[3]: FMOperator (~90 KB)
├── configs_[4]: OperatorConfig (12 bytes each = 48 bytes)
├── dcBlocker_: DCBlocker (~32 bytes)
├── currentAlgorithm_: uint8_t (1 byte)
├── baseFrequency_: float (4 bytes)
├── sampleRate_: double (8 bytes)
└── prepared_: bool (1 byte + padding)
```

Note: Each FMOperator contains a WavetableData (~90 KB for mipmapped sine table). Total FMVoice instance is approximately 360 KB. This is acceptable for a monophonic voice; polyphony should be implemented with voice sharing or pooling at a higher level.
