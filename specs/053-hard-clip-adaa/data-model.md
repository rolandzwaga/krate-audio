# Data Model: Hard Clip with ADAA

**Feature**: 053-hard-clip-adaa
**Date**: 2026-01-13
**Layer**: 1 (Primitives)
**Namespace**: `Krate::DSP`

---

## 1. Entities

### 1.1 Order Enumeration

```cpp
/// @brief ADAA order selection for aliasing reduction quality vs CPU tradeoff.
enum class Order : uint8_t {
    First = 0,   ///< First-order ADAA: ~12-20 dB aliasing reduction
    Second = 1   ///< Second-order ADAA: ~18-30 dB aliasing reduction
};
```

**Purpose**: Select between first-order (faster, good quality) and second-order (slower, better quality) ADAA algorithms.

| Value | Aliasing Reduction | CPU Cost |
|-------|-------------------|----------|
| First | 12-20 dB vs naive | ~6-8x naive |
| Second | 18-30 dB vs naive | ~12-15x naive |

### 1.2 HardClipADAA Class

```cpp
/// @brief Anti-aliased hard clipping using Antiderivative Anti-Aliasing.
class HardClipADAA {
public:
    // Nested enum
    enum class Order : uint8_t { First, Second };

    // Construction
    HardClipADAA() noexcept;

    // Configuration
    void setOrder(Order order) noexcept;
    void setThreshold(float threshold) noexcept;
    void reset() noexcept;

    // Getters
    [[nodiscard]] Order getOrder() const noexcept;
    [[nodiscard]] float getThreshold() const noexcept;

    // Processing
    [[nodiscard]] float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;

    // Static antiderivative functions
    [[nodiscard]] static float F1(float x, float threshold) noexcept;
    [[nodiscard]] static float F2(float x, float threshold) noexcept;

private:
    // State
    float x1_;                 // Previous input sample
    float D1_prev_;            // Previous first-order result (for ADAA2)
    float threshold_;          // Clipping threshold
    Order order_;              // Selected ADAA order
    bool hasPreviousSample_;   // First sample flag

    // Internal processing
    [[nodiscard]] float processFirstOrder(float x) noexcept;
    [[nodiscard]] float processSecondOrder(float x) noexcept;
};
```

---

## 2. Member Variables

### 2.1 State Variables

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `x1_` | `float` | `0.0f` | Previous input sample for ADAA computation |
| `D1_prev_` | `float` | `0.0f` | Previous first-order ADAA result (second-order only) |
| `hasPreviousSample_` | `bool` | `false` | Flag indicating if previous sample exists |

### 2.2 Configuration Variables

| Member | Type | Default | Valid Range | Description |
|--------|------|---------|-------------|-------------|
| `threshold_` | `float` | `1.0f` | `>= 0.0f` | Clipping threshold (absolute value stored) |
| `order_` | `Order` | `Order::First` | First, Second | ADAA algorithm order |

---

## 3. Relationships

### 3.1 Layer Dependencies

```
Layer 1: HardClipADAA
    |
    +-- Layer 0: core/sigmoid.h (Sigmoid::hardClip)
    +-- Layer 0: core/db_utils.h (detail::isNaN, detail::isInf)
    +-- stdlib: <cmath>, <algorithm>
```

### 3.2 No Composition

HardClipADAA is a self-contained primitive with no composition of other DSP components.

---

## 4. State Transitions

### 4.1 Lifecycle States

```
[Uninitialized] --> construct() --> [Ready, No History]
                                          |
                                    process() (first call)
                                          |
                                          v
                                    [Ready, Has History]
                                          |
                                    reset() |
                                          v
                                    [Ready, No History]
```

### 4.2 State Transition Details

| From State | Trigger | To State | Actions |
|------------|---------|----------|---------|
| (any) | `HardClipADAA()` | Ready, No History | Initialize defaults |
| Ready, No History | `process(x)` | Ready, Has History | Store x as x1_, return naive clip |
| Ready, Has History | `process(x)` | Ready, Has History | Compute ADAA, update state |
| Ready, Has History | `reset()` | Ready, No History | Clear x1_, D1_prev_, hasPreviousSample_ |

---

## 5. Validation Rules

### 5.1 Threshold Validation

- **Input**: Any float value
- **Storage**: `std::abs(threshold)` - negative values become positive
- **Zero handling**: When threshold == 0.0f, process() returns 0.0f always

### 5.2 Input Validation

| Input | Behavior |
|-------|----------|
| Normal float | Process normally |
| NaN | Propagate NaN in output |
| +Infinity | Return threshold |
| -Infinity | Return -threshold |

### 5.3 Epsilon Constant

```cpp
static constexpr float kEpsilon = 1e-5f;  // Near-identical sample detection
```

When `|x[n] - x[n-1]| < kEpsilon`, use fallback formula to avoid division instability.

---

## 6. Invariants

1. **State consistency**: After any operation, object is in a valid state
2. **Threshold non-negative**: `threshold_ >= 0.0f` always
3. **Real-time safety**: All methods are `noexcept`, no allocations
4. **Block equivalence**: `processBlock(buf, N)` produces identical output to N sequential `process()` calls

---

## 7. Memory Layout

```
HardClipADAA (estimated 16 bytes, may pad to 20):
+0:  float x1_              [4 bytes]
+4:  float D1_prev_         [4 bytes]
+8:  float threshold_       [4 bytes]
+12: Order order_           [1 byte]
+13: bool hasPreviousSample_[1 byte]
+14: [padding]              [2 bytes]
```

Note: Actual layout may vary by compiler. All members trivially copyable; class is trivially copyable.

---

## 8. API Contract Summary

### 8.1 Construction

```cpp
HardClipADAA();  // Order::First, threshold 1.0, no history
```

### 8.2 Configuration (All noexcept)

```cpp
void setOrder(Order order);      // Immediate effect
void setThreshold(float t);      // Stores abs(t)
void reset();                    // Clears history, keeps config
```

### 8.3 Getters (All const noexcept)

```cpp
Order getOrder() const;
float getThreshold() const;
```

### 8.4 Processing (All noexcept)

```cpp
float process(float x);                      // Sample-by-sample, updates state
void processBlock(float* buffer, size_t n);  // In-place block processing
```

### 8.5 Static Functions (All noexcept)

```cpp
static float F1(float x, float threshold);  // First antiderivative
static float F2(float x, float threshold);  // Second antiderivative
```

---

## 9. Example State Evolution

Processing sequence with Order::First, threshold 1.0:

| Step | Input | x1_ | hasPreviousSample_ | Output | Notes |
|------|-------|-----|-------------------|--------|-------|
| 0 | - | 0.0 | false | - | Initial state |
| 1 | 0.5 | 0.5 | true | 0.5 | First sample, naive clip |
| 2 | 1.2 | 1.2 | true | 1.0* | ADAA smoothed output |
| 3 | 1.2 | 1.2 | true | 1.0 | Epsilon fallback (same input) |
| 4 | 0.8 | 0.8 | true | 0.87* | ADAA transition out of clip |
| reset | - | 0.0 | false | - | State cleared |

*Approximate ADAA values - actual computation uses antiderivative formulas.
