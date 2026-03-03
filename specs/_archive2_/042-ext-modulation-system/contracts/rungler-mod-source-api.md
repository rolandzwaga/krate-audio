# API Contract: Rungler ModulationSource Interface

**Feature**: 042-ext-modulation-system | **Layer**: 2 (Processors)

## Class Declaration Change

### Previous
```cpp
class Rungler {
    // ...existing API...
};
```

### New (FR-017)
```cpp
class Rungler : public ModulationSource {
    // ...existing API unchanged...

    // ModulationSource interface (NEW)
    [[nodiscard]] float getCurrentValue() const noexcept override;
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override;
};
```

## New Method: getCurrentValue()

```cpp
[[nodiscard]] float getCurrentValue() const noexcept override {
    return runglerCV_;
}
```

### Behavior
- Returns the current filtered Rungler CV output
- Range: [0, +1]
- Updated per sample during `process()`
- Returns 0.0 if not prepared

### Thread Safety
- Read-only access to `runglerCV_`
- Safe to call from any thread (float read is atomic on x86/ARM)
- Typically called from the audio thread after `process()`

## New Method: getSourceRange()

```cpp
[[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
    return {0.0f, 1.0f};
}
```

### Behavior
- Returns the unipolar output range of the Rungler CV
- Always returns {0.0, 1.0} regardless of configuration

## Correlation Requirement (SC-007)

The ModulationSource output MUST correlate with the standalone Rungler output:
- Pearson correlation > 0.99 between `getCurrentValue()` and `process().rungler`
- This is trivially satisfied because `getCurrentValue()` returns the exact same `runglerCV_` member that populates `Output::rungler`

## Include Dependency

New include required in `rungler.h`:
```cpp
#include <krate/dsp/core/modulation_source.h>  // Layer 0 - valid for Layer 2
```

## Test Verification Matrix

| Test Case | Setup | Expected |
|-----------|-------|----------|
| getCurrentValue before prepare | Default Rungler | Returns 0.0f |
| getCurrentValue after process | prepare(), process N samples | Returns same as Output::rungler |
| getSourceRange | Any state | Returns {0.0f, 1.0f} |
| Correlation test | Process 44100 samples, compare getCurrentValue() vs Output::rungler | Pearson > 0.99 (trivially 1.0) |
| Works as ModulationSource* | Cast Rungler* to ModulationSource*, call getCurrentValue() | Same result as through Rungler* |
