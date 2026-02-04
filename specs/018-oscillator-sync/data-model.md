# Data Model: Oscillator Sync

**Feature**: 018-oscillator-sync | **Date**: 2026-02-04

---

## Entities

### 1. SyncMode Enumeration

**Location**: `dsp/include/krate/dsp/processors/sync_oscillator.h` (file scope, `Krate::DSP` namespace)

| Value | Name | Underlying | Description |
|-------|------|-----------|-------------|
| 0 | Hard | `uint8_t` | Reset slave phase on master wrap |
| 1 | Reverse | `uint8_t` | Reverse slave direction on master wrap |
| 2 | PhaseAdvance | `uint8_t` | Advance slave phase by fractional amount on master wrap |

**Relationships**: Used by `SyncOscillator::setSyncMode()`.

---

### 2. SyncOscillator Class

**Location**: `dsp/include/krate/dsp/processors/sync_oscillator.h`
**Layer**: 2 (processors)
**Namespace**: `Krate::DSP`

#### Fields (Internal State)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `table_` | `const MinBlepTable*` | `nullptr` | Non-owning pointer to shared minBLEP/minBLAMP table (caller owns) |
| `residual_` | `MinBlepTable::Residual` | default | Ring buffer for BLEP/BLAMP corrections |
| `masterPhase_` | `PhaseAccumulator` | `{0.0, 0.0}` | Master phase accumulator (timing only, no waveform) |
| `slave_` | `PolyBlepOscillator` | default | Slave oscillator (generates audible output) |
| `sampleRate_` | `float` | `0.0f` | Cached sample rate |
| `masterFrequency_` | `float` | `0.0f` | Master frequency in Hz |
| `masterIncrement_` | `float` | `0.0f` | Cached master phase increment (freq/sampleRate) |
| `syncMode_` | `SyncMode` | `Hard` | Active sync mode |
| `syncAmount_` | `float` | `1.0f` | Sync intensity [0.0, 1.0] |
| `reversed_` | `bool` | `false` | Direction flag for reverse sync mode |
| `prepared_` | `bool` | `false` | Whether prepare() has been called |

#### Memory Layout

Cache-friendly: hot-path data (phase accumulators, increment) placed first. Configuration (mode, amount) placed after. The `PolyBlepOscillator` is embedded by value (no heap allocation for the slave).

#### Validation Rules

| Field | Rule |
|-------|------|
| `masterFrequency_` | Clamped to [0, sampleRate/2). NaN/Inf treated as 0.0. |
| `syncAmount_` | Clamped to [0.0, 1.0]. |
| `table_` | Must be non-null and prepared before `prepare()` succeeds. |
| Output | Sanitized to [-2.0, 2.0]. NaN replaced with 0.0. |

#### State Transitions

```
                +--------+     prepare(sampleRate)    +----------+
                | Created| --------------------------> | Prepared |
                +--------+                            +----------+
                                                        |       ^
                                                        |       |
                                           process()    |       | reset()
                                        setMaster/Slave |       | (preserves config)
                                        setSyncMode()   |       |
                                        setSyncAmount() |       |
                                                        v       |
                                                      +-----------+
                                                      | Processing|
                                                      +-----------+
```

- **Created**: Default constructor. No sample rate set. `process()` returns 0.0.
- **Prepared**: `prepare()` called. Internal oscillators initialized. Ready for processing.
- **Processing**: Any parameter change or process call. Can return to Prepared via `reset()`.

---

### 3. MinBlepTable Extension (MinBLAMP Support)

**Location**: `dsp/include/krate/dsp/primitives/minblep_table.h` (existing file, extended)

#### New Fields

| Field | Type | Description |
|-------|------|-------------|
| `blampTable_` | `std::vector<float>` | Precomputed minBLAMP table (integral of minBLEP residual) |

#### New Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `sampleBlamp` | `[[nodiscard]] float sampleBlamp(float subsampleOffset, size_t index) const noexcept` | Look up interpolated minBLAMP value |

#### Residual Extension

| Method | Signature | Description |
|--------|-----------|-------------|
| `addBlamp` | `void addBlamp(float subsampleOffset, float amplitude) noexcept` | Stamp minBLAMP correction for derivative discontinuity |

---

## Entity Relationships

```
   +------------------+
   |   MinBlepTable    |<---- const pointer (caller owns)
   | - table_[]        |
   | - blampTable_[]   |      +-------------------+
   | + sample()        |      |  SyncOscillator   |
   | + sampleBlamp()   |----->| - table_*         |
   | + prepare()       |      | - residual_       |
   +------------------+      | - masterPhase_    |
          |                   | - slave_          |
          |                   | - syncMode_       |
          v                   | - syncAmount_     |
   +------------------+      | - reversed_       |
   |     Residual      |      | + process()       |
   | - buffer_[]       |<-----| + processBlock()  |
   | + addBlep()       |      | + setSyncMode()   |
   | + addBlamp()      |      | + setSyncAmount() |
   | + consume()       |      +-------------------+
   | + reset()         |              |
   +------------------+              | composes (by value)
                                      v
                              +-------------------+
                              | PolyBlepOscillator|
                              | - phaseAcc_       |
                              | - waveform_       |
                              | + process()       |
                              | + resetPhase()    |
                              | + phase()         |
                              +-------------------+
```

---

## Key Data Flows

### Per-Sample Processing Pipeline

```
Input: masterFrequency, slaveFrequency, syncMode, syncAmount, waveform
                     |
                     v
     +-------------------------------+
     | 1. Master phase advance       |
     |    masterPhase += increment    |
     |    wrapped = (phase >= 1.0)    |
     +-------------------------------+
                     |
                     v
     +-------------------------------+
     | 2. Sync event (if wrapped)    |
     |    - Compute subsample offset |
     |    - Mode-specific processing |
     |    - Hard: phase reset + BLEP |
     |    - Reverse: direction flip  |
     |      + BLAMP                  |
     |    - PhaseAdvance: partial    |
     |      reset + BLEP            |
     +-------------------------------+
                     |
                     v
     +-------------------------------+
     | 3. Slave output               |
     |    raw = slave_.process()     |
     +-------------------------------+
                     |
                     v
     +-------------------------------+
     | 4. Apply correction           |
     |    output = raw +             |
     |             residual_.consume |
     +-------------------------------+
                     |
                     v
     +-------------------------------+
     | 5. Sanitize output            |
     |    NaN -> 0.0                 |
     |    clamp [-2.0, 2.0]         |
     +-------------------------------+
                     |
                     v
                  Output: float
```
