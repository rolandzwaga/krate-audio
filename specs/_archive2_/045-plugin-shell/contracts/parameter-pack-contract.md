# Parameter Pack API Contract

**Feature**: 045-plugin-shell
**Date**: 2026-02-09

## Contract: Parameter Pack Interface

Each parameter pack header MUST implement these 6 functions following the Iterum pattern.

### 1. Atomic Storage Struct

```cpp
namespace Ruinae {

struct SectionNameParams {
    std::atomic<float> paramName{defaultValue};
    std::atomic<int> discreteParam{defaultIndex};
    std::atomic<bool> toggleParam{defaultBool};
    // All fields must have sensible defaults for audible sound on first load
};

} // namespace Ruinae
```

### 2. Change Handler

```cpp
namespace Ruinae {

inline void handleSectionNameParamChange(
    SectionNameParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue)
{
    switch (id) {
        case kSectionParamId:
            // Denormalize from [0,1] to real range
            // Clamp to valid bounds
            // Store with memory_order_relaxed
            params.paramName.store(
                static_cast<float>(/* denormalization */),
                std::memory_order_relaxed);
            break;
    }
}

} // namespace Ruinae
```

**Requirements**:
- MUST denormalize from [0.0, 1.0] to real-world range
- MUST clamp to valid bounds (clamping is implicit in the formula for most cases)
- MUST use `std::memory_order_relaxed` (audio thread)
- MUST be inline (called from process())

### 3. Registration Function

```cpp
namespace Ruinae {

inline void registerSectionNameParams(
    Steinberg::Vst::ParameterContainer& parameters)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Continuous parameters (use Parameter directly with stepCount=0)
    parameters.addParameter(
        STR16("Param Name"),      // display name
        STR16("Hz"),              // unit
        0,                        // stepCount (0 = continuous)
        0.5,                      // default normalized value
        ParameterInfo::kCanAutomate,
        kParamId);

    // Discrete dropdown parameters (MUST use StringListParameter)
    parameters.addParameter(createDropdownParameter(
        STR16("Type"), kTypeId,
        {STR16("Option A"), STR16("Option B")}
    ));

    // Toggle parameters (stepCount=1)
    parameters.addParameter(
        STR16("Enabled"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kEnabledId);
}

} // namespace Ruinae
```

**Requirements**:
- MUST use `StringListParameter` (via `createDropdownParameter`) for all discrete list parameters
- MUST set `kCanAutomate` flag on all parameters
- MUST provide correct default normalized values
- MUST provide correct units (Hz, ms, %, st, dB, ct)

**Note (A15)**: All note value parameters (Trance Gate ID 607, Delay ID 1605) share the same dropdown mapping from note_value_ui.h (whole through 128th triplets).

**Note (A16)**: StringListParameter stores labels at registration time in Controller::initialize(). getParamStringByValue() does NOT need to handle them — the base class returns the stored label.

**Note (A23)**: Parameter = base class. RangeParameter adds min/max/default. StringListParameter adds label list. For continuous params, use Parameter directly with stepCount=0.

### 4. Display Formatter

```cpp
namespace Ruinae {

inline Steinberg::tresult formatSectionNameParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string)
{
    switch (id) {
        case kFrequencyParamId: {
            float hz = /* denormalize */;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f Hz", hz);  // 1 decimal for Hz (A28)
            Steinberg::UString(string, 128).fromAscii(text);
            return Steinberg::kResultOk;
        }
        // StringListParameter types are handled automatically
        // by the base class -- DO NOT add cases for them
    }
    return Steinberg::kResultFalse;
}

} // namespace Ruinae
```

**Requirements**:
- MUST use same denormalization formula as change handler
- MUST include correct units (Hz, ms, %, st, ct, dB)
- MUST NOT handle StringListParameter types (auto-handled by framework)
- MUST return kResultFalse for unhandled IDs

**Display Precision Table (A28)**:
| Unit | Precision | Format |
|------|-----------|--------|
| Hz | 1 decimal | "%.1f Hz" |
| ms | 1 decimal | "%.1f ms" |
| % | 0 decimal | "%.0f%%" |
| st | 0 decimal | "%+.0f st" |
| dB | 1 decimal | "%.1f dB" |
| ct | 0 decimal | "%+.0f ct" |

### 5. Save/Load Functions

```cpp
namespace Ruinae {

inline void saveSectionNameParams(
    const SectionNameParams& params,
    Steinberg::IBStreamer& streamer)
{
    // Write in deterministic order matching struct layout
    streamer.writeFloat(params.paramName.load(std::memory_order_relaxed));
    streamer.writeInt32(params.discreteParam.load(std::memory_order_relaxed));
    streamer.writeInt32(params.toggleParam.load(std::memory_order_relaxed) ? 1 : 0);
}

inline void loadSectionNameParams(
    SectionNameParams& params,
    Steinberg::IBStreamer& streamer)
{
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    streamer.readFloat(floatVal);
    params.paramName.store(floatVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.discreteParam.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.toggleParam.store(intVal != 0, std::memory_order_relaxed);
}

} // namespace Ruinae
```

**Requirements**:
- Save and load MUST use same field order
- Booleans serialized as int32 (0/1)
- Floats use writeFloat/readFloat (4 bytes, IEEE 754)
- Ints use writeInt32/readInt32 (4 bytes, little-endian)

### 6. Controller Sync Template

```cpp
namespace Ruinae {

template<typename SetParamFunc>
inline void loadSectionNameParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    // Read in same order as loadSectionNameParams
    // Convert real values back to normalized for Controller
    if (streamer.readFloat(floatVal)) {
        setParam(kParamId, /* normalize: real -> [0,1] */);
    }
}

// Convenience wrapper
inline void syncSectionNameParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    loadSectionNameParamsToController(streamer, [&](Steinberg::Vst::ParamID id, double val) {
        controller.setParamNormalized(id, val);
    });
}

} // namespace Ruinae
```

**Requirements**:
- Read order MUST match save/load order exactly
- Normalization MUST be exact inverse of denormalization
- Template pattern enables reuse for both setComponentState and preset loading

## Contract: Processor Parameter Routing

```cpp
void Processor::processParameterChanges(IParameterChanges* changes) {
    // For each changed parameter:
    //   Get last value (most recent point)
    //   Route by ID range to appropriate handler:

    if (paramId >= kOscABaseId && paramId <= kOscAEndId) {
        handleOscAParamChange(oscAParams_, paramId, value);
    } else if (paramId >= kOscBBaseId && paramId <= kOscBEndId) {
        handleOscBParamChange(oscBParams_, paramId, value);
    }
    // ... etc for all 19 sections
}
```

**Note (A33)**: Parameter ID ranges are inclusive bounds (e.g., 100-199 means 100 <= id <= 199).

## Contract: State Format

```
Byte offset  Type     Field
0            int32    stateVersion (currently 1)
4            float    masterGain
8            int32    voiceMode
12           int32    polyphony
16           int32    softLimit
20           int32    oscAType
24           float    oscATune
28           float    oscAFine
32           float    oscALevel
36           int32    oscAPhase
...          ...      (continues in data-model.md serialization order)
```

All values are little-endian (IBStreamer with kLittleEndian).

**Note (A34)**: Full byte offset table is derivable from data-model.md serialization order. Low priority to generate complete table.
