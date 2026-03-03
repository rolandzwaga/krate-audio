# API Contract: Plugin Integration -- Ratchet Parameters

**Date**: 2026-02-22

## Parameter IDs (plugin_ids.h)

```cpp
// Ratchet Lane (074-ratcheting)
kArpRatchetLaneLengthId  = 3190,    // discrete: 1-32 (RangeParameter, stepCount=31)
kArpRatchetLaneStep0Id   = 3191,    // discrete: 1-4 (RangeParameter, stepCount=3, default 1)
kArpRatchetLaneStep1Id   = 3192,
kArpRatchetLaneStep2Id   = 3193,
kArpRatchetLaneStep3Id   = 3194,
kArpRatchetLaneStep4Id   = 3195,
kArpRatchetLaneStep5Id   = 3196,
kArpRatchetLaneStep6Id   = 3197,
kArpRatchetLaneStep7Id   = 3198,
kArpRatchetLaneStep8Id   = 3199,
kArpRatchetLaneStep9Id   = 3200,
kArpRatchetLaneStep10Id  = 3201,
kArpRatchetLaneStep11Id  = 3202,
kArpRatchetLaneStep12Id  = 3203,
kArpRatchetLaneStep13Id  = 3204,
kArpRatchetLaneStep14Id  = 3205,
kArpRatchetLaneStep15Id  = 3206,
kArpRatchetLaneStep16Id  = 3207,
kArpRatchetLaneStep17Id  = 3208,
kArpRatchetLaneStep18Id  = 3209,
kArpRatchetLaneStep19Id  = 3210,
kArpRatchetLaneStep20Id  = 3211,
kArpRatchetLaneStep21Id  = 3212,
kArpRatchetLaneStep22Id  = 3213,
kArpRatchetLaneStep23Id  = 3214,
kArpRatchetLaneStep24Id  = 3215,
kArpRatchetLaneStep25Id  = 3216,
kArpRatchetLaneStep26Id  = 3217,
kArpRatchetLaneStep27Id  = 3218,
kArpRatchetLaneStep28Id  = 3219,
kArpRatchetLaneStep29Id  = 3220,
kArpRatchetLaneStep30Id  = 3221,
kArpRatchetLaneStep31Id  = 3222,

// Updated sentinels
kArpEndId = 3299,        // Was 3199
kNumParameters = 3300,   // Was 3200
```

## ArpeggiatorParams Extension

```cpp
// Ratchet Lane (074-ratcheting)
std::atomic<int> ratchetLaneLength{1};
std::atomic<int> ratchetLaneSteps[32];  // Default 1 each (set in constructor)
```

Constructor addition:
```cpp
// Initialize ratchet lane steps to default value 1
for (auto& step : ratchetLaneSteps) {
    step.store(1, std::memory_order_relaxed);
}
```

## handleArpParamChange Extension

### Length parameter (switch case):
```cpp
case kArpRatchetLaneLengthId:
    // RangeParameter: 0-1 -> 1-32 (stepCount=31)
    params.ratchetLaneLength.store(
        std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
        std::memory_order_relaxed);
    break;
```

### Step parameters (default block, range check):
```cpp
// Ratchet lane steps: 3191-3222
else if (id >= kArpRatchetLaneStep0Id && id <= kArpRatchetLaneStep31Id) {
    // RangeParameter: 0-1 -> 1-4 (stepCount=3)
    int ratchet = std::clamp(
        static_cast<int>(1.0 + std::round(value * 3.0)), 1, 4);
    params.ratchetLaneSteps[id - kArpRatchetLaneStep0Id].store(
        ratchet, std::memory_order_relaxed);
}
```

## registerArpParams Extension

### Length parameter:
```cpp
// Ratchet lane length: RangeParameter 1-32, default 1, stepCount 31
parameters.addParameter(
    new RangeParameter(STR16("Arp Ratch Lane Len"), kArpRatchetLaneLengthId,
                      STR16(""), 1, 32, 1, 31,
                      ParameterInfo::kCanAutomate));
```

### Step parameters:
```cpp
// Ratchet lane steps: loop 0-31, RangeParameter 1-4, default 1, stepCount 3
for (int i = 0; i < 32; ++i) {
    char name[48];
    snprintf(name, sizeof(name), "Arp Ratch Step %d", i + 1);
    Steinberg::Vst::String128 name16;
    Steinberg::UString(name16, 128).fromAscii(name);
    parameters.addParameter(
        new RangeParameter(name16,
            static_cast<ParamID>(kArpRatchetLaneStep0Id + i),
            STR16(""), 1, 4, 1, 3,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
}
```

## formatArpParam Extension

### Length parameter (switch case):
```cpp
case kArpRatchetLaneLengthId: {
    char8 text[32];
    int len = std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
    snprintf(text, sizeof(text), "%d steps", len);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
```

### Step parameters (default block, range check):
```cpp
// Ratchet lane steps: display as "Nx"
if (id >= kArpRatchetLaneStep0Id && id <= kArpRatchetLaneStep31Id) {
    char8 text[32];
    int ratchet = std::clamp(
        static_cast<int>(1.0 + std::round(value * 3.0)), 1, 4);
    snprintf(text, sizeof(text), "%dx", ratchet);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
```

## saveArpParams Extension

After existing `streamer.writeFloat(params.slideTime...)`:
```cpp
// --- Ratchet Lane (074-ratcheting) ---
streamer.writeInt32(params.ratchetLaneLength.load(std::memory_order_relaxed));
for (int i = 0; i < 32; ++i) {
    streamer.writeInt32(params.ratchetLaneSteps[i].load(std::memory_order_relaxed));
}
```

## loadArpParams Extension

After existing slide time read:
```cpp
// --- Ratchet Lane (074-ratcheting) ---
// EOF-safe: if ratchet data is missing entirely (Phase 5 preset), keep defaults.
if (!streamer.readInt32(intVal)) return true;  // EOF at first ratchet field = Phase 5 compat
params.ratchetLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

// From here, EOF signals a corrupt stream (length present but steps not)
for (int i = 0; i < 32; ++i) {
    if (!streamer.readInt32(intVal)) return false;  // Corrupt
    params.ratchetLaneSteps[i].store(
        std::clamp(intVal, 1, 4), std::memory_order_relaxed);
}
```

## applyParamsToEngine Extension (processor.cpp)

After existing modifier lane application:
```cpp
// --- Ratchet Lane (074-ratcheting) ---
{
    const auto ratchLen = arpParams_.ratchetLaneLength.load(std::memory_order_relaxed);
    arpCore_.ratchetLane().setLength(32);  // Expand first
    for (int i = 0; i < 32; ++i) {
        int val = std::clamp(
            arpParams_.ratchetLaneSteps[i].load(std::memory_order_relaxed), 1, 4);
        arpCore_.ratchetLane().setStep(
            static_cast<size_t>(i), static_cast<uint8_t>(val));
    }
    arpCore_.ratchetLane().setLength(static_cast<size_t>(ratchLen));  // Shrink to actual
}
```

## loadArpParamsToController Extension

Follow the same pattern as existing lanes -- sync normalized values from loaded state back to controller parameters using `setParamNormalized()`.
