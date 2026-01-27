# Data Model: Wavefolder Primitive

**Spec**: 057-wavefolder | **Date**: 2026-01-13

## Entities

### WavefoldType Enumeration

**Purpose**: Selects between three wavefolding algorithms with distinct harmonic characteristics.

```cpp
enum class WavefoldType : uint8_t {
    Triangle = 0,  // Dense odd harmonics, smooth rolloff
    Sine = 1,      // FM-like sparse spectrum (Serge style)
    Lockhart = 2   // Rich even/odd harmonics with spectral nulls
};
```

| Value | Underlying | Harmonic Character | Use Case |
|-------|------------|-------------------|----------|
| Triangle | 0 | Dense odd harmonics | Guitar effects, general distortion |
| Sine | 1 | Sparse FM-like (Bessel) | Serge synthesizer emulation |
| Lockhart | 2 | Even + odd with nulls | Circuit-derived waveshaping |

**Constraints**:
- Underlying type: `uint8_t` (FR-002)
- Values: 0, 1, 2 (contiguous for switch optimization)

### Wavefolder Class

**Purpose**: Unified waveshaping primitive with selectable algorithms and configurable fold intensity.

**Layer**: 1 (Primitives)
**Namespace**: `Krate::DSP`
**Header**: `dsp/include/krate/dsp/primitives/wavefolder.h`

#### Member Variables

| Member | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `type_` | `WavefoldType` | `Triangle` | enum values | Selected algorithm |
| `foldAmount_` | `float` | `1.0f` | [0.0, 10.0] | Fold intensity |

**Size Constraint**: sizeof(Wavefolder) < 16 bytes (SC-007)
- WavefoldType: 1 byte
- float: 4 bytes
- Padding: 3 bytes (typical alignment)
- **Total: 8 bytes** (meets constraint)

#### State Transitions

```
Construction
    |
    v
[Default State]
  type_ = Triangle
  foldAmount_ = 1.0
    |
    +---> setType(type) -----> [Type Changed]
    |                              |
    +---> setFoldAmount(amt) ---> [Amount Changed]
    |                              |
    v                              v
[Ready for Processing] <----------+
    |
    +---> process(x) -----> [Output] (stateless, no state change)
    |
    +---> processBlock(buf, n) --> [Buffer Modified] (stateless)
```

**Key Property**: The class is stateless for processing operations. Both `process()` and `processBlock()` are logically const (process() is marked const, processBlock() modifies the buffer but not internal state).

#### Relationships

```
Wavefolder
    |
    +-- uses --> WavefoldMath::triangleFold() [Layer 0]
    |
    +-- uses --> WavefoldMath::sineFold() [Layer 0]
    |
    +-- uses --> WavefoldMath::lambertW() [Layer 0]
    |
    +-- uses --> FastMath::fastTanh() [Layer 0]
    |
    +-- uses --> detail::isNaN(), detail::isInf() [Layer 0]
```

### Validation Rules

| Property | Rule | Enforcement |
|----------|------|-------------|
| `foldAmount_` | Clamp to [0.0, 10.0] | In `setFoldAmount()` |
| Negative foldAmount | Convert to absolute value | Before clamping |
| type_ enum | No validation needed | enum class prevents invalid values |

### Processing Behavior by Type

#### Triangle Fold

```
Input: x (float), foldAmount
Internal: threshold = 1.0 / max(foldAmount, 0.001f)
Delegate: WavefoldMath::triangleFold(x, threshold)
Output: [-threshold, threshold]

Special Cases:
- NaN input: Propagate NaN
- Infinity input: Return sign(x) * threshold (saturate)
- foldAmount=0: threshold=1000 (very little folding)
```

#### Sine Fold

```
Input: x (float), foldAmount
Delegate: WavefoldMath::sineFold(x, foldAmount)
Output: [-1, 1]

Special Cases:
- NaN input: Propagate NaN
- Infinity input: Return sign(x) * 1.0 (saturate)
- foldAmount=0: Return x (linear passthrough via sineFold)
- foldAmount < 0.001: Return x (linear passthrough)
```

#### Lockhart Fold

```
Input: x (float), foldAmount
Formula: tanh(lambertW(exp(x * foldAmount)))
Output: [-1, 1] (tanh bounded)

Special Cases:
- NaN input: Propagate NaN
- Infinity input: Return NaN (per spec clarification)
- foldAmount=0: exp(0)=1, lambertW(1)~0.567, tanh(0.567)~0.514
- exp() overflow: lambertW handles large inputs gracefully
```

## Memory Layout

```cpp
class Wavefolder {
    // Members (8 bytes total, 4-byte aligned)
    WavefoldType type_;    // offset 0, size 1
    // padding             // offset 1, size 3
    float foldAmount_;     // offset 4, size 4
};
```

## Thread Safety

- **Configuration** (`setType`, `setFoldAmount`): Not thread-safe, caller must synchronize
- **Processing** (`process`, `processBlock`): Thread-safe for concurrent reads (const operations)
- **Per-channel instances**: Designed to be trivially copyable for independent per-channel use

## Performance Characteristics

| Operation | Complexity | Memory | Notes |
|-----------|------------|--------|-------|
| Construction | O(1) | 0 alloc | Default member init |
| setType | O(1) | 0 alloc | Simple assignment |
| setFoldAmount | O(1) | 0 alloc | Clamp + assign |
| process | O(1) | 0 alloc | Algorithm-dependent cycles |
| processBlock | O(n) | 0 alloc | n * process() |
