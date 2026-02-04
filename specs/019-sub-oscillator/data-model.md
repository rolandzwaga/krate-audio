# Data Model: Sub-Oscillator (019)

**Date**: 2026-02-04
**Spec**: specs/019-sub-oscillator/spec.md
**Status**: Complete

---

## Entities

### SubOctave (Enumeration)

**Location**: `dsp/include/krate/dsp/processors/sub_oscillator.h`
**Scope**: `Krate::DSP` namespace, file scope (not nested in class)
**Underlying type**: `uint8_t`

| Value | Name | Integer | Description |
|-------|------|---------|-------------|
| Divide-by-2 | `OneOctave` | 0 | Master frequency / 2 |
| Divide-by-4 | `TwoOctaves` | 1 | Master frequency / 4 |

---

### SubWaveform (Enumeration)

**Location**: `dsp/include/krate/dsp/processors/sub_oscillator.h`
**Scope**: `Krate::DSP` namespace, file scope (not nested in class)
**Underlying type**: `uint8_t`

| Value | Name | Integer | Description |
|-------|------|---------|-------------|
| Flip-flop square | `Square` | 0 | Classic analog sub, flip-flop with minBLEP |
| Digital sine | `Sine` | 1 | Pure sine at sub frequency via phase accumulator |
| Digital triangle | `Triangle` | 2 | Triangle at sub frequency via phase accumulator |

---

### SubOscillator (Class)

**Location**: `dsp/include/krate/dsp/processors/sub_oscillator.h`
**Layer**: 2 (Processor)
**Namespace**: `Krate::DSP`

#### Public Interface

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| Constructor | `explicit SubOscillator(const MinBlepTable* table = nullptr) noexcept` | N/A | Accepts shared table pointer |
| prepare | `void prepare(double sampleRate) noexcept` | No | Initialize for sample rate, reset state |
| reset | `void reset() noexcept` | Yes | Reset flip-flops and phase, preserve config |
| setOctave | `void setOctave(SubOctave octave) noexcept` | Yes | Select frequency division mode |
| setWaveform | `void setWaveform(SubWaveform waveform) noexcept` | Yes | Select sub waveform type |
| setMix | `void setMix(float mix) noexcept` | Yes | Set dry/wet balance [0.0, 1.0] |
| process | `[[nodiscard]] float process(bool masterPhaseWrapped, float masterPhaseIncrement) noexcept` | Yes | Generate one sub sample |
| processMixed | `[[nodiscard]] float processMixed(float mainOutput, bool masterPhaseWrapped, float masterPhaseIncrement) noexcept` | Yes | Generate mixed main+sub sample |

#### Internal State

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `table_` | `const MinBlepTable*` | `nullptr` | Shared minBLEP table (caller owns) |
| `residual_` | `MinBlepTable::Residual` | default | Per-instance minBLEP correction buffer |
| `subPhase_` | `PhaseAccumulator` | `{0.0, 0.0}` | Phase accumulator for sine/triangle |
| `sampleRate_` | `float` | `0.0f` | Current sample rate |
| `mix_` | `float` | `0.0f` | Mix parameter [0.0, 1.0] |
| `mainGain_` | `float` | `1.0f` | Cached equal-power gain for main signal |
| `subGain_` | `float` | `0.0f` | Cached equal-power gain for sub signal |
| `flipFlop1_` | `bool` | `false` | First-stage flip-flop (divide-by-2) |
| `flipFlop2_` | `bool` | `false` | Second-stage flip-flop (divide-by-4) |
| `octave_` | `SubOctave` | `OneOctave` | Current octave division setting |
| `waveform_` | `SubWaveform` | `Square` | Current waveform setting |
| `prepared_` | `bool` | `false` | Whether prepare() succeeded |

**Memory footprint estimate** (with standard table config `prepare(64, 8)`, length=16):
- `table_`: 8 bytes (pointer)
- `residual_`: ~40 bytes (vector metadata + 64 bytes heap for 16-float buffer)
- `subPhase_`: 16 bytes (2 doubles)
- `sampleRate_`: 4 bytes
- `mix_`, `mainGain_`, `subGain_`: 12 bytes
- `flipFlop1_`, `flipFlop2_`, `prepared_`: 3 bytes
- `octave_`, `waveform_`: 2 bytes
- Padding: ~3 bytes
- **Total stack size**: ~48 bytes (excluding Residual's heap buffer)
- **Total with heap (standard, length=16)**: ~112 bytes
- **Total with heap (maximum, length=64)**: ~304 bytes (at 300-byte budget limit)
- **Well under 300-byte limit with standard config; at limit with max config**
- Note: The Residual buffer is dynamically sized to `table.length()` via the existing `MinBlepTable::Residual` implementation. The 300-byte limit is the overarching constraint; the standard config provides ample safety margin.

#### State Transitions

```
                  +-------------------+
                  |   Constructed     |
                  | (prepared_=false) |
                  +--------+----------+
                           |
                      prepare(sr)
                           |
                  +--------v----------+
                  |     Prepared      |
                  | (prepared_=true)  |
                  | flip-flops=false  |
                  | subPhase=0.0      |
                  +--------+----------+
                           |
                  +--------v----------+
                  |    Processing     |<---------+
                  |  process() loop   |          |
                  +---+-----+----+----+     reset()
                      |     |    |               |
                 setOctave  | setWaveform   +----+
                 setMix     |               |
                      |     |    |          |
                      +-----v----+----------+
                      |    Processing       |
                      |  (params updated)   |
                      +---------------------+
```

#### Flip-Flop State Machine

**OneOctave mode** (output = flipFlop1_):
```
Master wraps: W1   W2   W3   W4   W5   W6   W7   W8
flipFlop1_:   T    F    T    F    T    F    T    F
Output:       +1   -1   +1   -1   +1   -1   +1   -1
              |    |    |    |    |    |    |    |
              <--------->    <--------->
              1 sub cycle    1 sub cycle
              = 2 master wraps
```

**TwoOctaves mode** (output = flipFlop2_):
```
Master wraps:  W1   W2   W3   W4   W5   W6   W7   W8
flipFlop1_:    T    F    T    F    T    F    T    F
                ^         ^         ^         ^
                |rising   |rising   |rising   |rising
flipFlop2_:    T    T    F    F    T    T    F    F
Output:        +1   +1   -1   -1   +1   +1   -1   -1
               |              |              |
               <------------->
               1 sub cycle = 4 master wraps
```

---

## Dependencies (Layer Compliance)

### Layer 0 (Core)

| Header | Components Used |
|--------|-----------------|
| `core/phase_utils.h` | `PhaseAccumulator`, `wrapPhase()`, `subsamplePhaseWrapOffset()` |
| `core/math_constants.h` | `kTwoPi` |
| `core/crossfade_utils.h` | `equalPowerGains()` |
| `core/db_utils.h` | `detail::isNaN()`, `detail::isInf()` |

### Layer 1 (Primitives)

| Header | Components Used |
|--------|-----------------|
| `primitives/minblep_table.h` | `MinBlepTable`, `MinBlepTable::Residual` |

### Standard Library

| Header | Components Used |
|--------|-----------------|
| `<bit>` | `std::bit_cast` (sanitization) |
| `<cmath>` | `std::sin`, `std::abs` |
| `<cstdint>` | `uint8_t`, `uint32_t` |

### NOT Included (Layer Compliance)

| Header | Why Not |
|--------|---------|
| `primitives/polyblep_oscillator.h` | Not needed. SubOscillator does not compose a PolyBlepOscillator. The OscWaveform enum is not used (SubWaveform is separate). |
| Any Layer 2+ header | Layer compliance: processors may only depend on Layer 0 and Layer 1 |
