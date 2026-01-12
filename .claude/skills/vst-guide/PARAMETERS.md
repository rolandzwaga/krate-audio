# Parameter Types and Helpers

## The Problem with toPlain()

The base `Parameter` class does NOT scale normalized values in `toPlain()`:

```cpp
// From vstparameters.cpp - Parameter::toPlain()
ParamValue Parameter::toPlain(ParamValue normValue) const {
    return normValue;  // Just returns the input unchanged!
}
```

This means if you create a parameter with `parameters.addParameter()` and expect `toPlain(0.5)` to return `5.0` for a 0-10 range, **it won't work**.

---

## Parameter Type Selection

Use the appropriate parameter type for your use case:

| Use Case | Parameter Type | toPlain() Behavior |
|----------|---------------|-------------------|
| Continuous knob (0-1 range) | `Parameter` | Returns normalized value |
| Continuous range (e.g., 20Hz-20kHz) | `RangeParameter` | Scales to min/max range |
| Discrete list (e.g., modes, types) | `StringListParameter` | Returns 0, 1, 2, ... stepCount |

---

## StringListParameter

For discrete mode selection (dropdowns, segmented buttons):

```cpp
auto* modeParam = new Steinberg::Vst::StringListParameter(
    STR16("Mode"),             // title
    kModeId,                   // parameter ID
    nullptr,                   // units
    Steinberg::Vst::ParameterInfo::kCanAutomate |
    Steinberg::Vst::ParameterInfo::kIsList
);
modeParam->appendString(STR16("Granular"));
modeParam->appendString(STR16("Spectral"));
// ... add all options
parameters.addParameter(modeParam);
```

### Automatic toString Handling

When you use `StringListParameter`, you don't need to override `getParamStringByValue()` - the class handles it:

```cpp
// From vstparameters.cpp - StringListParameter::toString()
void StringListParameter::toString(ParamValue _valueNormalized, String128 string) const {
    int32 index = static_cast<int32>(toPlain(_valueNormalized));
    if (const TChar* valueString = strings.at(index)) {
        UString(string, str16BufferSize(String128)).assign(valueString);
    }
}
```

---

## RangeParameter

For continuous parameters with specific ranges:

```cpp
auto* freqParam = new Steinberg::Vst::RangeParameter(
    STR16("Frequency"),        // title
    kFreqId,                   // parameter ID
    STR16("Hz"),              // units
    20.0,                      // minPlain
    20000.0,                   // maxPlain
    1000.0,                    // defaultPlain
    0,                         // stepCount (0 = continuous)
    Steinberg::Vst::ParameterInfo::kCanAutomate
);
parameters.addParameter(freqParam);
```

---

## Dropdown Parameter Helper

### The Problem

Creating `StringListParameter` correctly requires multiple lines:

```cpp
auto* param = new Steinberg::Vst::StringListParameter(
    STR16("Mode"),
    kModeId,
    nullptr,
    Steinberg::Vst::ParameterInfo::kCanAutomate |
    Steinberg::Vst::ParameterInfo::kIsList
);
param->appendString(STR16("Option1"));
param->appendString(STR16("Option2"));
param->appendString(STR16("Option3"));
parameters.addParameter(param);
```

This is verbose and easy to get wrong (forgetting `kIsList` flag, using wrong parameter type).

### The Solution

Use the `createDropdownParameter` helper from `src/controller/parameter_helpers.h`:

```cpp
#include "controller/parameter_helpers.h"

// Single line, impossible to get wrong
parameters.addParameter(createDropdownParameter(
    STR16("Mode"), kModeId,
    STR16("Option1"), STR16("Option2"), STR16("Option3")
));
```

### Helper Functions

**`createDropdownParameter`** - Default index 0:
```cpp
parameters.addParameter(createDropdownParameter(
    STR16("FFT Size"), kFFTSizeId,
    STR16("512"), STR16("1024"), STR16("2048"), STR16("4096")
));
```

**`createDropdownParameterWithDefault`** - Custom default index:
```cpp
parameters.addParameter(createDropdownParameterWithDefault(
    STR16("FFT Size"), kFFTSizeId,
    1,  // default to index 1 (1024)
    STR16("512"), STR16("1024"), STR16("2048"), STR16("4096")
));
```

### Implementation

The helper uses C++17 fold expressions:

```cpp
template<typename... Strings>
inline Steinberg::Vst::StringListParameter* createDropdownParameter(
    const Steinberg::Vst::TChar* title,
    Steinberg::Vst::ParamID id,
    Strings... options)
{
    auto* param = new Steinberg::Vst::StringListParameter(
        title, id, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );
    (param->appendString(options), ...);  // C++17 fold expression
    return param;
}
```

### Benefits

1. **Impossible to create wrong type** - Always creates `StringListParameter`
2. **Impossible to forget flags** - `kCanAutomate` and `kIsList` are always set
3. **Less code** - Single line vs 7+ lines
4. **Self-documenting** - All options visible at registration point
5. **Compile-time checked** - Wrong argument types caught by compiler

### When to Use

**Always use for dropdowns.** There is no reason to manually create `StringListParameter` anymore.

| Scenario | Use |
|----------|-----|
| Mode selector (3-20 options) | `createDropdownParameter` |
| FFT size selection | `createDropdownParameterWithDefault` (if default != 0) |
| Filter type (LP/HP/BP) | `createDropdownParameter` |
| Any discrete list | `createDropdownParameter` |

---

## COptionMenu and Discrete Parameters

### Menu Population

VSTGUI's VST3Editor automatically populates COptionMenu items from the parameter's `stepCount` and calls `getParamStringByValue()` for each index.

### COptionMenu Value Range

COptionMenu stores values as integers (0, 1, 2, ..., nbEntries-1). When bound to a parameter:
- `getValue()` returns the integer index
- `getValueNormalized()` returns index / (nbEntries - 1)

---

## Key Insight

**Never use custom formulas like `value * 10.0 + 0.5` to convert normalized values.** Always use the SDK's `param->toPlain(normalized)` after ensuring you're using the correct parameter type.
