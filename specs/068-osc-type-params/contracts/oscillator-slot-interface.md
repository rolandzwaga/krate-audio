# Contract: OscillatorSlot Interface Extension

**Layer**: 3 (Systems)
**File**: `dsp/include/krate/dsp/systems/oscillator_slot.h`

## New Method

```cpp
/// @brief Set a type-specific parameter on this oscillator.
/// @param param The parameter identifier
/// @param value The parameter value (in DSP domain, NOT normalized)
/// @note Base class implementation is an unconditional silent no-op.
///       Subclasses override to handle parameters relevant to their type.
///       Unrecognized OscParam values MUST be silently ignored.
/// @note Must be real-time safe: no allocation, no logging, no assertion.
virtual void setParam(OscParam param, float value) noexcept {
    (void)param; (void)value;
}
```

## Pre-conditions
- `param` may be any `OscParam` value (including values not handled by this slot type)
- `value` is a DSP-domain value (e.g., Hz for frequency, 0-1 for amounts, enum cast for types)

## Post-conditions
- If the adapter handles the param, the underlying oscillator is updated
- If the adapter does NOT handle the param, it is silently discarded (no-op)
- No exceptions thrown
- No memory allocated
- No assertions triggered

## Thread Safety
- Single-threaded (audio thread only)
- No internal synchronization needed
