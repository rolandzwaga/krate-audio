# Data Model: FM/PM Synthesis Operator

**Feature**: 021-fm-pm-synth-operator
**Date**: 2026-02-05
**Status**: Complete

---

## Entity: FMOperator

A single FM synthesis operator consisting of a sine oscillator with configurable frequency ratio, self-modulation feedback with tanh limiting, external phase modulation input, and level-controlled output.

### Location

`dsp/include/krate/dsp/processors/fm_operator.h`

### Layer

Layer 2 (DSP Processor)

### Dependencies

| Component | Layer | Purpose |
|-----------|-------|---------|
| WavetableOscillator | 1 | Core oscillator engine |
| WavetableData | 0 | Mipmapped sine storage |
| wavetable_generator.h | 1 | Sine table generation |
| fast_math.h | 0 | fastTanh for feedback |
| db_utils.h | 0 | isNaN, isInf for sanitization |
| math_constants.h | 0 | kTwoPi for radians |

---

## State

### Configuration Parameters (Preserved Across reset())

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `frequency_` | `float` | [0, sampleRate/2) | 0.0 | Base frequency in Hz |
| `ratio_` | `float` | [0, 16.0] | 1.0 | Frequency multiplier |
| `feedbackAmount_` | `float` | [0, 1.0] | 0.0 | Self-modulation intensity |
| `level_` | `float` | [0, 1.0] | 0.0 | Output amplitude scaling |
| `sampleRate_` | `double` | > 0 | 0.0 | Current sample rate |

### Internal State (Reset on reset())

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `osc_` | `WavetableOscillator` | (default) | Internal sine oscillator |
| `previousRawOutput_` | `float` | 0.0 | Last raw output for feedback |

### Resources (Regenerated on prepare())

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `sineTable_` | `WavetableData` | ~90 KB | Mipmapped sine wavetable |

### Flags

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `prepared_` | `bool` | false | True after prepare() called |

---

## Validation Rules

### Frequency Validation (FR-004)

```
IF isNaN(frequency) OR isInf(frequency):
    frequency = 0.0
IF frequency < 0:
    frequency = 0.0
IF frequency >= sampleRate/2:
    frequency = sampleRate/2 - epsilon
```

### Ratio Validation (FR-005)

```
IF ratio < 0:
    ratio = 0.0
IF ratio > 16.0:
    ratio = 16.0
// After ratio clamping, effective frequency is Nyquist-clamped:
IF (frequency * ratio) >= sampleRate/2:
    effectiveFreq = sampleRate/2 - epsilon
```

### Feedback Validation (FR-006)

```
IF feedbackAmount < 0:
    feedbackAmount = 0.0
IF feedbackAmount > 1.0:
    feedbackAmount = 1.0
```

### Level Validation (FR-007)

```
IF level < 0:
    level = 0.0
IF level > 1.0:
    level = 1.0
```

### Input Sanitization (FR-013)

All NaN and Infinity inputs to setters are treated as:
- Frequency: 0.0 Hz (silence)
- Ratio: preserved previous value
- Feedback: preserved previous value
- Level: preserved previous value
- phaseModInput: 0.0 radians

---

## State Transitions

### Lifecycle States

```
[UNINITIALIZED] --prepare()--> [PREPARED] --reset()--> [PREPARED]
       |                            |
       v                            v
  process() = 0.0            process() = audio
```

### State Transition: Uninitialized -> Prepared

**Trigger**: `prepare(sampleRate)`

**Actions**:
1. Store sample rate
2. Generate sine wavetable via generateMipmappedFromHarmonics
3. Configure WavetableOscillator with sample rate and table pointer
4. Set prepared_ = true

**Postconditions**:
- osc_ ready to process
- sineTable_ populated with mipmapped sine

### State Transition: Prepared -> Reset

**Trigger**: `reset()`

**Actions**:
1. Reset osc_ phase to 0
2. Clear previousRawOutput_ to 0.0

**Preserved**:
- frequency_, ratio_, feedbackAmount_, level_
- sampleRate_
- sineTable_ (not regenerated)
- prepared_ flag

---

## Processing Pipeline

### process(phaseModInput) Signal Flow

```
                    +----------------+
                    | feedbackAmount |
                    +-------+--------+
                            |
                            v
+--------------------+   +------+   +-------+
| previousRawOutput  |-->| *    |-->| tanh  |--+
+--------------------+   +------+   +-------+  |
                                               |
                         +--------------+      |
                         | phaseModInput|------+---> totalPM (radians)
                         +--------------+              |
                                                       v
+------------+   +--------+   +-------+         +-------------+
| frequency  |-->|   *    |-->| clamp |-------->| WavetableOsc|
+------------+   +--------+   +-------+         | .setFreq()  |
                      ^                         | .setPM()    |
+------------+        |                         +------+------+
| ratio      |--------+                                |
+------------+                                         v
                                                +-----------+
                                                | .process()|
                                                +-----+-----+
                                                      |
                                     rawOutput <------+
                                                      |
                      +----------------+              v
                      | previousRaw =  |<-----[store]
                      +----------------+              |
                                                      v
                                              +-------------+
                                              |    * level  |
                                              +------+------+
                                                     |
                                                     v
                                              +-------------+
                                              |  sanitize   |
                                              +------+------+
                                                     |
                                                     v
                                                  output
```

### Step-by-Step Processing

1. **Compute effective frequency**
   ```cpp
   float effFreq = frequency_ * ratio_;
   // Clamp to Nyquist
   effFreq = clamp(effFreq, 0.0f, sampleRate/2 - epsilon);
   osc_.setFrequency(effFreq);
   ```

2. **Compute feedback contribution**
   ```cpp
   float feedback = FastMath::fastTanh(previousRawOutput_ * feedbackAmount_);
   ```

3. **Combine modulation**
   ```cpp
   float totalPM = phaseModInput + feedback;
   osc_.setPhaseModulation(totalPM);
   ```

4. **Generate sample**
   ```cpp
   float rawOutput = osc_.process();
   ```

5. **Store for feedback**
   ```cpp
   previousRawOutput_ = rawOutput;
   ```

6. **Apply level and sanitize**
   ```cpp
   float output = rawOutput * level_;
   return sanitize(output);
   ```

---

## Memory Layout

### FMOperator Instance (~90 KB)

```
+---------------------------+
| WavetableData sineTable_  |  ~90,112 bytes
| (11 levels x 2052 floats) |
+---------------------------+
| WavetableOscillator osc_  |  ~48 bytes
| - PhaseAccumulator        |
| - sampleRate_, frequency_ |
| - fmOffset_, pmOffset_    |
| - table_ (pointer)        |
| - phaseWrapped_           |
+---------------------------+
| float frequency_          |  4 bytes
| float ratio_              |  4 bytes
| float feedbackAmount_     |  4 bytes
| float level_              |  4 bytes
| float previousRawOutput_  |  4 bytes
| double sampleRate_        |  8 bytes
| bool prepared_            |  1 byte (+padding)
+---------------------------+
Total: ~90,200 bytes
```

---

## Relationships

### FMOperator as Modulator

When FMOperator is used as a modulator in an FM chain:

```
+--------------+     lastRawOutput()    +--------------+
| Modulator    |----------------------->| Carrier      |
| (FMOperator) |    * modulatorLevel    | (FMOperator) |
+--------------+                        +--------------+
      |                                       |
      v                                       v
   (internal                              process(pm)
    feedback)                                 |
                                              v
                                           output
```

### FMOperator in Future FM Voice (Layer 3)

```
+------------------+
|    FM Voice      |
+------------------+
| operators_[0..5] |  6 x FMOperator instances
| algorithm_       |  Routing configuration
| velocityScale_   |  Velocity modulation
+------------------+
```

---

## Test Verification Points

| Field/Behavior | Test Method |
|----------------|-------------|
| frequency_ clamping | Set > Nyquist, verify output frequency |
| ratio_ range | Set 0.5 to 16.0, verify via FFT peak |
| feedbackAmount_ effect | Compare THD at 0, 0.5, 1.0 |
| level_ scaling | Compare amplitude at level 0.5 vs 1.0 |
| previousRawOutput_ feedback | Run feedback=1.0 for 10s, verify stability |
| reset() preserves config | Reset, verify frequency/ratio unchanged |
| prepare() generates table | Verify osc_ produces output after prepare |
