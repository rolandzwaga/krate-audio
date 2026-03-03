# API Contract: Disrumpo Preset Integration

## 1. Disrumpo PresetManager Configuration

```cpp
// plugins/disrumpo/src/preset/disrumpo_preset_config.h
#pragma once
#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"

namespace Disrumpo {

inline Krate::Plugins::PresetManagerConfig makeDisrumpoPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        .processorUID = kProcessorUID,
        .pluginName = "Disrumpo",
        .pluginCategoryDesc = "Distortion",
        .subcategoryNames = {
            "Init", "Sweep", "Morph", "Bass", "Leads",
            "Pads", "Drums", "Experimental", "Chaos", "Dynamic", "Lo-Fi"
        }
    };
}

// Tab labels for the preset browser (includes "All" prefix)
inline std::vector<std::string> getDisrumpoTabLabels() {
    return {
        "All", "Init", "Sweep", "Morph", "Bass", "Leads",
        "Pads", "Drums", "Experimental", "Chaos", "Dynamic", "Lo-Fi"
    };
}

} // namespace Disrumpo
```

## 2. Disrumpo Controller Integration

```cpp
// In plugins/disrumpo/src/controller/controller.h (additions)
class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate {
    // ...existing members...

    // Preset management
    Krate::Plugins::PresetManager* presetManager_ = nullptr;
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;

    // Called when user clicks preset browser button
    void openPresetBrowser();
    void openSavePresetDialog();

    // StateProvider callback: creates MemoryStream with processor state
    Steinberg::IBStream* createComponentStateStream();

    // LoadProvider callback: applies state from stream via performEdit
    bool applyComponentState(Steinberg::IBStream* stream);
};
```

## 3. Disrumpo Serialization (Already Implemented)

The following methods are already implemented in `plugins/disrumpo/src/processor/processor.cpp`:

```cpp
// Processor::getState(IBStream*) - lines 441-712
//   Writes: version(v6), globals, bands(8), crossovers(7), sweep, modulation, morph nodes

// Processor::setState(IBStream*) - lines 714+
//   Reads: version, then conditionally reads each section based on version number
//   v1: globals only
//   v2: + bands + crossovers
//   v4: + sweep
//   v5: + modulation
//   v6: + morph nodes
```

No changes to these methods are required.

## 4. Factory Preset Generator

```cpp
// tools/disrumpo_preset_generator.cpp
//
// Standalone executable that generates .vstpreset files for Disrumpo.
// Follows the pattern established by tools/preset_generator.cpp (Iterum).
//
// Usage: disrumpo_preset_generator [output_directory]
//
// Generates 120 .vstpreset files organized into 11 category subdirectories:
//   Init/         (5 presets)
//   Sweep/        (15 presets)
//   Morph/        (15 presets)
//   Bass/         (10 presets)
//   Leads/        (10 presets)
//   Pads/         (10 presets)
//   Drums/        (10 presets)
//   Experimental/ (15 presets)
//   Chaos/        (10 presets)
//   Dynamic/      (10 presets)
//   Lo-Fi/        (10 presets)

// BinaryWriter class (same as Iterum's preset_generator.cpp)
class BinaryWriter { /* writeInt32, writeFloat, writeInt8 */ };

// DisrumpoPresetState - matches Processor::getState() binary layout
struct DisrumpoPresetState {
    int32_t version = 6;

    // Global (v1)
    float inputGain = 0.5f;     // 0 dB
    float outputGain = 0.5f;    // 0 dB
    float globalMix = 1.0f;     // 100%

    // Band Management (v2)
    int32_t bandCount = 1;
    struct BandState {
        float gainDb = 0.0f;
        float pan = 0.5f;
        bool solo = false;
        bool bypass = false;
        bool mute = false;
    } bands[8];
    float crossoverFreqs[7] = {250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};

    // Sweep (v4) - defaults to off
    // Modulation (v5) - defaults to none
    // Morph Nodes (v6) - defaults per-preset

    void serialize(BinaryWriter& writer) const;
};
```

## 5. Round-Trip Test Interface

```cpp
// plugins/disrumpo/tests/unit/preset/serialization_round_trip_test.cpp
//
// Tests that all ~450 parameters survive save/load cycle
// Method:
//   1. Configure processor with non-default values for ALL parameters
//   2. Call getState() to serialize to MemoryStream
//   3. Call setState() to deserialize from same stream
//   4. Compare all parameter values with margin of 1e-6

// Tests for each version level:
// TEST_CASE("v1 preset loads with defaults for missing params")
// TEST_CASE("v2 preset loads bands, defaults for sweep/mod/morph")
// TEST_CASE("v4 preset loads sweep, defaults for mod/morph")
// TEST_CASE("v5 preset loads modulation, defaults for morph")
// TEST_CASE("v6 preset round-trips all parameters")
// TEST_CASE("Future version (v99) loads known params, ignores trailing data")
// TEST_CASE("Version 0 is rejected")
// TEST_CASE("Empty stream is rejected")
// TEST_CASE("Truncated stream loads partial data with defaults")
```
