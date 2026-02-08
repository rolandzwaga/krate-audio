# Quickstart: Extended Modulation System

**Feature**: 042-ext-modulation-system | **Date**: 2026-02-08

## What This Feature Does

Extends the Ruinae synthesizer's modulation system with:
1. **Aftertouch** as a per-voice modulation source
2. **OSC A Level** and **OSC B Level** as per-voice modulation destinations
3. **Global modulation** via existing ModulationEngine (LFO, Chaos, Rungler, Macros, MIDI controllers)
4. **Global-to-voice forwarding** for All Voice Filter Cutoff, All Voice Morph Position, and Trance Gate Rate

## Files to Modify

### Headers (4 files)
```
dsp/include/krate/dsp/systems/ruinae_types.h       # Add enum values
dsp/include/krate/dsp/systems/voice_mod_router.h    # Add aftertouch param + NaN sanitization
dsp/include/krate/dsp/systems/ruinae_voice.h        # Add setAftertouch() + OscLevel application
dsp/include/krate/dsp/processors/rungler.h          # Add ModulationSource inheritance
```

### Tests (4 files, 1 new)
```
dsp/tests/unit/systems/voice_mod_router_test.cpp    # Extend with new source/dest tests
dsp/tests/unit/systems/ruinae_voice_test.cpp        # Extend with OscLevel + aftertouch tests
dsp/tests/unit/processors/rungler_test.cpp          # Add ModulationSource interface tests
dsp/tests/unit/systems/ext_modulation_test.cpp      # NEW: global modulation + forwarding
```

### Build (1 file)
```
dsp/tests/CMakeLists.txt                            # Register new test file
```

## Implementation Order

### Step 1: Enum Extensions (ruinae_types.h)
```cpp
// Add to VoiceModSource (before NumSources):
Aftertouch,     // [0, 1] channel aftertouch

// Add to VoiceModDest (before NumDestinations):
OscALevel,      // OSC A amplitude [0, 1]
OscBLevel,      // OSC B amplitude [0, 1]
```

### Step 2: VoiceModRouter (voice_mod_router.h)

Extend `computeOffsets()` to accept 8 parameters (add `float aftertouch`), add NaN/Inf/denormal sanitization after accumulation loop.

See [contracts/voice-mod-router-api.md](contracts/voice-mod-router-api.md) for exact signature and behavior.

### Step 3: RuinaeVoice (ruinae_voice.h)

Add `setAftertouch(float)` method, apply OscALevel/OscBLevel offsets (computed at block start, pre-VCA) to oscillator buffers before mixing, pass `aftertouch_` to `computeOffsets()`.

See [contracts/ruinae-voice-extensions-api.md](contracts/ruinae-voice-extensions-api.md) for exact formulas and behavior.

### Step 4: Rungler (rungler.h)

Add `ModulationSource` inheritance directly to Rungler class. Implement `getCurrentValue()` (returns `runglerCV_`) and `getSourceRange()` (returns `{0.0f, 1.0f}`).

See [contracts/rungler-mod-source-api.md](contracts/rungler-mod-source-api.md) for exact signatures.

### Step 5: Global Modulation Tests (ext_modulation_test.cpp)

Compose ModulationEngine in test scaffold, test routing from global sources to destinations, test global-to-voice forwarding with two-stage clamping, test Pitch Bend/Mod Wheel normalization.

See [contracts/global-modulation-api.md](contracts/global-modulation-api.md) for destination IDs, forwarding formula, and MIDI normalization.

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only modulation tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[voice_mod_router]"
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ext_modulation]"
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[rungler][modsource]"
```

## Key Design Decisions

1. **Aftertouch is 8th param to computeOffsets()** -- breaking change, all callers updated
2. **OscALevel/OscBLevel base = 1.0 (fixed)** -- not a user parameter
3. **Rungler inherits ModulationSource directly** -- no wrapper class
4. **Pitch Bend/Mod Wheel via Macros** -- interim for test scaffold; Phase 6 may add dedicated enum values
5. **Per-voice amounts NOT smoothed** -- instant application per spec clarification
6. **Global amounts smoothed via existing OnePoleSmoother** (20ms) -- already in ModulationEngine
