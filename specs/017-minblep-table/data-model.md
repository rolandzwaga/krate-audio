# Data Model: MinBLEP Table

**Spec**: 017-minblep-table | **Date**: 2026-02-04

## Entities

### MinBlepTable

**Location**: `dsp/include/krate/dsp/primitives/minblep_table.h`
**Namespace**: `Krate::DSP`
**Layer**: 1 (Primitives)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `table_` | `std::vector<float>` | empty | Flat polyphase table: `length_ * oversamplingFactor_` entries |
| `length_` | `size_t` | 0 | Output-rate table length = `zeroCrossings * 2` |
| `oversamplingFactor_` | `size_t` | 0 | Oversampling factor for sub-sample resolution |
| `prepared_` | `bool` | false | Whether `prepare()` has been called successfully |

**Relationships**:
- Owns `table_` data (value semantics via vector)
- Referenced by `Residual` instances (non-owning `const MinBlepTable*` pointer)

**Validation Rules**:
- `prepare()` requires `oversamplingFactor > 0` AND `zeroCrossings > 0`
- After `prepare()`, `table_` is immutable (read-only for all query methods)
- `length_` always equals `zeroCrossings * 2` after successful prepare
- `prepared_` is true only after successful `prepare()` call

**State Transitions**:
```
[Default/Empty] --prepare(valid)--> [Prepared/Immutable]
[Prepared/Immutable] --prepare(valid)--> [Prepared/Immutable] (re-prepared with new params)
[Default/Empty] --prepare(invalid)--> [Default/Empty] (no change)
[Prepared/Immutable] --prepare(invalid)--> [Default/Empty] (cleared)
```

### MinBlepTable::Residual (Nested Struct)

**Location**: Nested within `MinBlepTable` class
**Namespace**: `Krate::DSP`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `table_` | `const MinBlepTable*` | nullptr | Non-owning pointer to associated table |
| `buffer_` | `std::vector<float>` | empty | Ring buffer of `table_->length()` samples |
| `readIdx_` | `size_t` | 0 | Current read position in ring buffer |

**Relationships**:
- References `MinBlepTable` via non-owning pointer (same pattern as `WavetableOscillator` -> `WavetableData`)
- Table MUST outlive Residual instances

**Validation Rules**:
- Constructed from `const MinBlepTable&` reference
- `buffer_` size equals `table_->length()`
- `readIdx_` always in range `[0, buffer_.size())`
- `addBlep()` accumulates (adds to) existing buffer contents
- `consume()` reads, clears, and advances

**State Transitions**:
```
[Constructed] --addBlep()--> [Active BLEPs]
[Active BLEPs] --consume()--> [Active BLEPs] (one sample consumed)
[Active BLEPs] --consume() x length()--> [Empty] (all corrections consumed)
[Active BLEPs] --reset()--> [Empty]
[Empty] --consume()--> [Empty] (returns 0.0)
[Empty] --addBlep()--> [Active BLEPs]
```

## Internal Data Structures (Not Exposed)

### Polyphase Table Layout

The internal table is stored as a flat `std::vector<float>` with the following access pattern:

```
table_[index * oversamplingFactor_ + subIndex]

where:
  index:    output-rate sample position [0, length_)
  subIndex: oversampled sub-position [0, oversamplingFactor_)
```

For default parameters (oversamplingFactor=64, zeroCrossings=8):
- Total entries: 16 * 64 = 1024
- Memory: 1024 * 4 bytes = 4 KB

### Ring Buffer Layout

The Residual ring buffer is a flat `std::vector<float>` with circular access:

```
buffer_[(readIdx_ + offset) % length]

where:
  readIdx_: current read position
  offset:   forward offset from current position
  length:   buffer_.size() (= table_->length())
```

## Entity Diagram

```
MinBlepTable
  |-- table_: vector<float> [1024 entries for default]
  |-- length_: size_t [16 for default]
  |-- oversamplingFactor_: size_t [64 for default]
  |-- prepared_: bool
  |
  |-- Residual (nested struct, multiple instances)
        |-- table_: const MinBlepTable* (non-owning)
        |-- buffer_: vector<float> [16 entries for default]
        |-- readIdx_: size_t
```

## prepare() Algorithm Pipeline

The `prepare()` method executes a multi-step algorithm. These are the intermediate data structures:

| Step | Input | Output | Storage |
|------|-------|--------|---------|
| 1. Windowed Sinc | parameters | `vector<float>` sinc | `zeroCrossings * oversamplingFactor * 2 + 1` samples |
| 2a. Zero-pad | sinc | `vector<float>` padded | Next power-of-2 FFT size |
| 2b. Forward FFT | padded | `vector<Complex>` spectrum | FFT size / 2 + 1 bins |
| 2c. Log-magnitude | spectrum | `vector<float>` logMag | FFT size / 2 + 1 values |
| 2d. Inverse FFT | logMag | `vector<float>` cepstrum | FFT size samples |
| 2e. Cepstral window | cepstrum | cepstrum (in-place) | Same |
| 2f. Forward FFT | cepstrum | `vector<Complex>` minPhaseSpectrum | FFT size / 2 + 1 bins |
| 2g. Complex exp | minPhaseSpectrum | minPhaseSpectrum (in-place) | Same |
| 2h. Inverse FFT | minPhaseSpectrum | `vector<float>` minPhaseSinc | FFT size samples |
| 3. Integration | minPhaseSinc | `vector<float>` minBlep | Truncated to sinc length |
| 4. Normalize | minBlep | minBlep (in-place) | Same |
| 5. Store | minBlep | `table_` | `length_ * oversamplingFactor_` entries |

All intermediate vectors are allocated and freed within `prepare()`. Only `table_` persists.
