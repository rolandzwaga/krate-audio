# Data Model: Rungler / Shift Register Oscillator

**Branch**: `029-rungler-oscillator` | **Date**: 2026-02-06

---

## Entities

### Rungler (Top-Level Processor)

**Location**: `dsp/include/krate/dsp/processors/rungler.h`
**Namespace**: `Krate::DSP`
**Layer**: 2 (Processors)

The main processor class that encapsulates two cross-modulating triangle oscillators, an 8-bit shift register with XOR feedback, a 3-bit DAC, and optional CV smoothing. Produces multi-output chaotic stepped sequences.

#### Configuration State (set by user, persisted across reset)

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `osc1BaseFreq_` | `float` | 200.0f | [0.1, 20000] Hz | Oscillator 1 base frequency |
| `osc2BaseFreq_` | `float` | 300.0f | [0.1, 20000] Hz | Oscillator 2 base frequency |
| `osc1RunglerDepth_` | `float` | 0.0f | [0.0, 1.0] | Osc1 modulation depth from Rungler CV |
| `osc2RunglerDepth_` | `float` | 0.0f | [0.0, 1.0] | Osc2 modulation depth from Rungler CV |
| `filterAmount_` | `float` | 0.0f | [0.0, 1.0] | CV smoothing filter amount (0=off, 1=max) |
| `loopMode_` | `bool` | false | true/false | Chaos mode (false) or loop mode (true) |
| `runglerBits_` | `size_t` | 8 | [4, 16] | Shift register length in bits |
| `kModulationOctaves` | `static constexpr float` | 4.0f | compile-time constant | Modulation range in octaves (no setter, fixed at 4 octaves) |

#### Processing State (reset on prepare/reset)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `osc1Phase_` | `float` | 0.0f | Oscillator 1 triangle phase [-1, +1] |
| `osc1Direction_` | `int` | 1 | Oscillator 1 ramp direction (+1 or -1) |
| `osc2Phase_` | `float` | 0.0f | Oscillator 2 triangle phase [-1, +1] |
| `osc2Direction_` | `int` | 1 | Oscillator 2 ramp direction (+1 or -1) |
| `osc2PrevTriangle_` | `float` | 0.0f | Previous osc2 triangle value (for edge detection) |
| `registerState_` | `uint32_t` | (random) | Shift register bit state |
| `runglerCV_` | `float` | 0.0f | Current (filtered) Rungler CV value [0, 1] |
| `rawDacOutput_` | `float` | 0.0f | Unfiltered DAC output [0, 1] |

#### Derived State (computed from configuration + sample rate)

| Field | Type | Description |
|-------|------|-------------|
| `sampleRate_` | `float` | Stored sample rate for increment calculation |
| `registerMask_` | `uint32_t` | Bitmask `(1u << runglerBits_) - 1` |
| `prepared_` | `bool` | Whether prepare() has been called |

#### Internal Components

| Field | Type | Layer | Purpose |
|-------|------|-------|---------|
| `cvFilter_` | `OnePoleLP` | 1 | Low-pass filter for Rungler CV smoothing |
| `rng_` | `Xorshift32` | 0 | PRNG for shift register seeding |

---

### Rungler::Output (Nested Struct)

**Scope**: Public nested struct within `Rungler`

Multi-output sample structure produced by `process()`.

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `osc1` | `float` | [-1.0, +1.0] | Oscillator 1 triangle wave |
| `osc2` | `float` | [-1.0, +1.0] | Oscillator 2 triangle wave |
| `rungler` | `float` | [0.0, +1.0] | Rungler CV (filtered DAC output) |
| `pwm` | `float` | [-1.0, +1.0] | PWM comparator output |
| `mixed` | `float` | [-1.0, +1.0] | Equal mix of osc1 + osc2 (scaled) |

---

## Relationships

```
Rungler (Layer 2)
  |
  +-- Xorshift32 (Layer 0, core/random.h)
  |     Used for: shift register seeding on prepare()/reset()
  |
  +-- OnePoleLP (Layer 1, primitives/one_pole.h)
  |     Used for: CV smoothing filter
  |
  +-- detail::isNaN / detail::isInf (Layer 0, core/db_utils.h)
  |     Used for: input sanitization
  |
  +-- kPi, kTwoPi (Layer 0, core/math_constants.h)
        Used for: (not directly needed -- no trig in this component)
```

Note: `math_constants.h` is not actually required. The Rungler uses no trigonometry. The frequency modulation uses `std::pow(2.0f, ...)` which is from `<cmath>`.

---

## State Transitions

### Oscillator Triangle Phase

```
State: phase in [-1.0, +1.0] (float), direction in {+1, -1} (int)

Each sample:
  phase += direction * phaseIncrement
  if phase >= 1.0:
    phase = 2.0 - phase    // Reflect off upper bound
    direction = -1
  if phase <= -1.0:
    phase = -2.0 - phase   // Reflect off lower bound
    direction = +1

Output:
  triangle = phase          // Already in [-1, +1]
  pulse = (phase >= 0.0) ? +1.0 : -1.0
```

### Shift Register Clock Event

```
Trigger: osc2PrevTriangle_ < 0.0 AND osc2CurrentTriangle >= 0.0

Actions:
  1. Determine data bit:
     if loopMode_:
       dataBit = (registerState_ >> (runglerBits_ - 1)) & 1   // Last bit recycled
     else:
       osc1Pulse = (osc1Phase_ >= 0.0) ? 1 : 0
       lastBit = (registerState_ >> (runglerBits_ - 1)) & 1
       dataBit = osc1Pulse XOR lastBit                         // Chaos mode
  2. Shift register: registerState_ = ((registerState_ << 1) | dataBit) & registerMask_
  3. Compute DAC: Read bits N-1, N-2, N-3 -> rawDacOutput_ = (msb*4 + mid*2 + lsb) / 7.0
```

### Register Length Change

```
Trigger: setRunglerBits(newSize)

Actions:
  1. Clamp newSize to [4, 16]
  2. Update runglerBits_ = newSize
  3. Update registerMask_ = (1u << newSize) - 1
  4. Mask register: registerState_ &= registerMask_   // Truncate to new length
  (No re-seed, no glitch -- just changes the mask and DAC bit positions)
```

---

## Validation Rules

| Parameter | Validation | Sanitization |
|-----------|-----------|--------------|
| osc1Frequency | Clamp [0.1, 20000] | NaN -> 200.0, Inf -> clamp |
| osc2Frequency | Clamp [0.1, 20000] | NaN -> 300.0, Inf -> clamp |
| osc1RunglerDepth | Clamp [0.0, 1.0] | Implicit by clamp |
| osc2RunglerDepth | Clamp [0.0, 1.0] | Implicit by clamp |
| filterAmount | Clamp [0.0, 1.0] | Implicit by clamp |
| runglerBits | Clamp [4, 16] | Implicit by clamp |
| loopMode | Boolean | No sanitization needed |
| seed | uint32_t | 0 replaced by default seed (Xorshift32 handles this) |
