# Research: Disrumpo Plugin Skeleton

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)
**Date**: 2026-01-27

## Overview

This document captures research findings for the Disrumpo plugin skeleton implementation. Since Iterum provides a complete reference implementation, research is primarily focused on:

1. Confirming patterns from Iterum apply to Disrumpo
2. Documenting the different parameter ID encoding scheme
3. Verifying VST3 SDK requirements

---

## Research Questions

### R1: VST3 Plugin Factory Pattern

**Question**: How should the plugin factory be structured?

**Finding**: The Iterum `entry.cpp` provides the definitive pattern:

```cpp
BEGIN_FACTORY_DEF(
    stringCompanyName,
    stringVendorURL,
    stringVendorEmail
)
    // Processor
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Namespace::kProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        stringPluginName,
        Steinberg::Vst::kDistributable,  // CRITICAL: Enables separation
        Namespace::kSubCategories,
        FULL_VERSION_STR,
        kVstVersionString,
        Namespace::Processor::createInstance
    )

    // Controller
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Namespace::kControllerUID),
        PClassInfo::kManyInstances,
        kVstComponentControllerClass,
        stringPluginName "Controller",
        0,
        "",
        FULL_VERSION_STR,
        kVstVersionString,
        Namespace::Controller::createInstance
    )
END_FACTORY
```

**Decision**: Use identical pattern with Disrumpo namespace.

---

### R2: Parameter ID Encoding Scheme

**Question**: How should parameter IDs be encoded for Disrumpo?

**Finding**: Per [dsp-details.md](../Disrumpo/dsp-details.md), Disrumpo uses a **bit-encoded scheme** that differs from Iterum's sequential 100-gap scheme:

| Range | Purpose | Encoding |
|-------|---------|----------|
| `0x0Fxx` | Global parameters | `(0xF << 8) \| param` |
| `0x0Exx` | Sweep parameters | `(0xE << 8) \| param` |
| Band params | Per-band parameters | `(0xF << 12) \| (band << 8) \| param` |
| Node params | Per-node parameters | `(node << 12) \| (band << 8) \| param` |

**Iterum's scheme** (for comparison):
- Global: 0-99
- Granular: 100-199
- Spectral: 200-299
- etc.

**Decision**: Use bit-encoded scheme per spec. For skeleton:
- kInputGainId = 0x0F00
- kOutputGainId = 0x0F01
- kGlobalMixId = 0x0F02

**Rationale**: The bit-encoded scheme supports the multiband architecture where 8 bands x 4 nodes x 16 param types need unique IDs. The encoding allows extracting band/node indices from the ID at runtime.

---

### R3: State Serialization Format

**Question**: What format should state serialization use?

**Finding**: Per [dsp-details.md](../Disrumpo/dsp-details.md) Section 2:

```cpp
tresult PLUGIN_API Processor::getState(IBStream* state) {
    IBStreamer streamer(state, kLittleEndian);

    // Version for future compatibility
    streamer.writeInt32(kPresetVersion);  // ALWAYS FIRST

    // Parameters in order
    streamer.writeFloat(paramValue1);
    streamer.writeFloat(paramValue2);
    // ...

    return kResultOk;
}
```

**Version handling**:
- `version > kPresetVersion`: Load what we understand, skip unknown
- `version < kPresetVersion`: Apply defaults for missing parameters
- Read fails: Return kResultFalse, plugin uses default values

**Decision**: Implement exactly as specified. Initial kPresetVersion = 1.

---

### R4: Bus Arrangement Support

**Question**: What bus arrangements should be supported?

**Finding**: Spec FR-010 states stereo-only:
> "Processor MUST implement setBusArrangements() accepting stereo (2 channels) only."

From Iterum's processor.cpp pattern:
```cpp
tresult PLUGIN_API Processor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1) {
        if (inputs[0] == SpeakerArr::kStereo &&
            outputs[0] == SpeakerArr::kStereo) {
            return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
        }
    }
    return kResultFalse;  // Reject non-stereo
}
```

**Decision**: Implement stereo-only support per spec.

---

### R5: CMake Integration

**Question**: How should the CMake build be structured?

**Finding**: The root CMakeLists.txt shows the pattern:
```cmake
add_subdirectory(plugins/iterum)
```

The plugin CMakeLists.txt uses:
- `file(READ ... version.json)` for version
- `configure_file()` for version.h generation
- `smtg_add_vst3plugin()` for plugin target
- `target_link_libraries(... sdk vstgui_support KrateDSP)`

**Decision**: Follow identical pattern for Disrumpo.

---

### R6: FUID Generation

**Question**: How should unique FUIDs be generated?

**Finding**: FUIDs must be globally unique. Iterum uses placeholder values (0x12345678...) which should be replaced with real GUIDs in production.

**Decision**: Generate new unique FUIDs for Disrumpo. Use different values than Iterum to ensure they never conflict:
- Processor: Generate new GUID
- Controller: Generate new GUID

**Tool**: https://www.guidgenerator.com/ or Windows `[guid]::NewGuid()` in PowerShell

---

## Technology Decisions

| Decision | Chosen | Alternatives Rejected | Rationale |
|----------|--------|----------------------|-----------|
| Base class for Processor | `AudioEffect` | `AudioEffectX`, `SingleComponentEffect` | Standard VST3 pattern, matches Iterum |
| Base class for Controller | `EditControllerEx1` | `EditController` | Ex1 provides more features (parameters as objects) |
| State format | IBStreamer binary | JSON, XML | Standard VST3 binary format, fastest |
| Parameter encoding | Bit-encoded (0x0Fxx) | Sequential (0-99, 100-199) | Spec requirement, supports band/node structure |
| Endianness | Little endian | Big endian | Standard for x86/x64, ARM supports both |

---

## Dependencies Verified

| Dependency | Version | Source | Status |
|------------|---------|--------|--------|
| VST3 SDK | 3.7.x | extern/vst3sdk/ | Available |
| VSTGUI | 4.12+ | extern/vst3sdk/vstgui4/ | Available |
| KrateDSP | internal | dsp/ | Available |
| Catch2 | 3.4.0 | FetchContent | Available |

---

## Patterns from Iterum (Confirmed Applicable)

### 1. File Organization
```
plugins/disrumpo/
+-- CMakeLists.txt
+-- version.json
+-- src/
|   +-- entry.cpp
|   +-- plugin_ids.h
|   +-- version.h.in
|   +-- processor/processor.{h,cpp}
|   +-- controller/controller.{h,cpp}
+-- resources/
    +-- win32resource.rc.in
```

### 2. Namespace Pattern
All Disrumpo code in `namespace Disrumpo { ... }` to prevent ODR violations.

### 3. Version Management
Single source of truth in `version.json`, CMake generates `version.h`.

### 4. Parameter Storage
Atomic members in Processor class for thread-safe parameter access:
```cpp
std::atomic<float> inputGain_{0.5f};
std::atomic<float> outputGain_{0.5f};
std::atomic<float> globalMix_{1.0f};
```

### 5. Audio Passthrough (Skeleton)
```cpp
// In process():
for (int32 i = 0; i < numSamples; ++i) {
    outputL[i] = inputL[i];
    outputR[i] = inputR[i];
}
```

---

## Risk Mitigations

| Risk | Mitigation |
|------|------------|
| FUID collision with Iterum | Generate completely new GUIDs |
| Parameter ID scheme confusion | Document clearly in plugin_ids.h |
| State corruption handling | Test with truncated/corrupted streams |
| DAW compatibility | Test in Reaper and Ableton per spec |

---

## Open Questions (Resolved)

| Question | Resolution |
|----------|------------|
| ~~Which parameter ranges for gain?~~ | Normalized 0.0-1.0, dB conversion in processing |
| ~~Include KrateDSP headers in skeleton?~~ | Yes, link but don't use yet (placeholder for future) |
| ~~Test infrastructure in skeleton?~~ | No, tests added in later specs |

---

## References

- [Disrumpo dsp-details.md](../Disrumpo/dsp-details.md) - Parameter encoding specification
- [Iterum plugin_ids.h](../../plugins/iterum/src/plugin_ids.h) - Reference implementation
- [Iterum entry.cpp](../../plugins/iterum/src/entry.cpp) - Factory pattern
- [VST3 SDK Documentation](https://steinbergmedia.github.io/vst3_dev_portal/)
- [Constitution](../../.specify/memory/constitution.md) - Development principles
