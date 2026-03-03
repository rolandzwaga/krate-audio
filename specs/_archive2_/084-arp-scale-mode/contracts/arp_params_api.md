# API Contract: Arp Parameter Extension

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**File**: `plugins/ruinae/src/plugin_ids.h`
**File**: `plugins/ruinae/src/parameters/dropdown_mappings.h`

## Parameter IDs

```cpp
// plugin_ids.h
kArpScaleTypeId           = 3300,
kArpRootNoteId            = 3301,
kArpScaleQuantizeInputId  = 3302,

kArpEndId = 3302,
kNumParameters = 3303,
```

## ArpeggiatorParams Struct Extension

```cpp
// Appended after ratchetSwing:
std::atomic<int>  scaleType{8};               // ScaleType enum value (default Chromatic=8)
std::atomic<int>  rootNote{0};                // Root note (default C=0)
std::atomic<bool> scaleQuantizeInput{false};  // Input quantize toggle (default OFF)
```

## handleArpParamChange Extension

```cpp
// New cases in the switch statement:
case kArpScaleTypeId: {
    // UI index -> ScaleType enum via display order mapping
    int uiIndex = std::clamp(
        static_cast<int>(value * (kArpScaleTypeCount - 1) + 0.5),
        0, kArpScaleTypeCount - 1);
    params.scaleType.store(kArpScaleDisplayOrder[uiIndex],
        std::memory_order_relaxed);
    return;
}
case kArpRootNoteId:
    params.rootNote.store(
        std::clamp(static_cast<int>(value * (kArpRootNoteCount - 1) + 0.5),
                   0, kArpRootNoteCount - 1),
        std::memory_order_relaxed);
    return;
case kArpScaleQuantizeInputId:
    params.scaleQuantizeInput.store(value >= 0.5, std::memory_order_relaxed);
    return;
```

## registerArpParams Extension

```cpp
// Scale Type: StringListParameter (16 entries), default index 0 = Chromatic
parameters.addParameter(createDropdownParameter(
    STR16("Arp Scale Type"), kArpScaleTypeId,
    {STR16("Chromatic"), STR16("Major"), STR16("Natural Minor"),
     STR16("Harmonic Minor"), STR16("Melodic Minor"),
     STR16("Dorian"), STR16("Phrygian"), STR16("Lydian"),
     STR16("Mixolydian"), STR16("Locrian"),
     STR16("Major Pentatonic"), STR16("Minor Pentatonic"),
     STR16("Blues"), STR16("Whole Tone"),
     STR16("Diminished (W-H)"), STR16("Diminished (H-W)")}));

// Root Note: StringListParameter (12 entries), default index 0 = C
parameters.addParameter(createDropdownParameter(
    STR16("Arp Root Note"), kArpRootNoteId,
    {STR16("C"), STR16("C#"), STR16("D"), STR16("D#"),
     STR16("E"), STR16("F"), STR16("F#"), STR16("G"),
     STR16("G#"), STR16("A"), STR16("A#"), STR16("B")}));

// Scale Quantize Input: Toggle (0 or 1), default off
parameters.addParameter(STR16("Arp Scale Quantize"), STR16(""), 1, 0.0,
    ParameterInfo::kCanAutomate, kArpScaleQuantizeInputId);
```

## Save/Load Extension

```cpp
// saveArpParams: append after ratchetSwing
streamer.writeInt32(params.scaleType.load(std::memory_order_relaxed));
streamer.writeInt32(params.rootNote.load(std::memory_order_relaxed));
streamer.writeInt32(params.scaleQuantizeInput.load(std::memory_order_relaxed) ? 1 : 0);

// loadArpParams: append after ratchetSwing (with backward compat defaults)
if (!streamer.readInt32(intVal)) return true;  // Old preset, keep defaults
params.scaleType.store(std::clamp(intVal, 0, 15), std::memory_order_relaxed);
if (!streamer.readInt32(intVal)) return true;
params.rootNote.store(std::clamp(intVal, 0, 11), std::memory_order_relaxed);
if (!streamer.readInt32(intVal)) return true;
params.scaleQuantizeInput.store(intVal != 0, std::memory_order_relaxed);
```

## Controller State Restore Extension

```cpp
// loadArpParamsToController: append after ratchetSwing
if (streamer.readInt32(iv)) {
    int enumVal = std::clamp(iv, 0, 15);
    int uiIndex = kArpScaleEnumToDisplay[enumVal];
    setParam(kArpScaleTypeId, static_cast<double>(uiIndex) / (kArpScaleTypeCount - 1));
}
if (streamer.readInt32(iv))
    setParam(kArpRootNoteId, static_cast<double>(iv) / (kArpRootNoteCount - 1));
if (streamer.readInt32(iv))
    setParam(kArpScaleQuantizeInputId, iv != 0 ? 1.0 : 0.0);
```
