# Quickstart: XYMorphPad Custom Control

**Date**: 2026-02-10
**Spec**: [spec.md](spec.md)

## What This Feature Does

XYMorphPad is a 2D interactive pad control for the Ruinae synthesizer's Oscillator Mixer section. It provides a visual "character space" where the user controls two parameters simultaneously:
- **X axis (horizontal)**: Morph position between OSC A and OSC B [0.0 - 1.0]
- **Y axis (vertical)**: Spectral tilt [-12 to +12 dB/oct]

The pad renders a bilinear color gradient (blue = OSC A, gold = OSC B, dim = dark, bright = bright) with an interactive cursor and modulation visualization.

## Key Files to Create/Modify

### New Files

| File | Purpose |
|------|---------|
| `plugins/shared/src/ui/xy_morph_pad.h` | XYMorphPad CControl + ViewCreator (header-only) |

### Modified Files

| File | Change |
|------|--------|
| `plugins/shared/src/ui/color_utils.h` | Add `bilinearColor()` utility function |
| `plugins/shared/CMakeLists.txt` | Add `xy_morph_pad.h` to source list |
| `plugins/ruinae/src/plugin_ids.h` | Add `kMixerTiltId = 302` |
| `plugins/ruinae/src/parameters/mixer_params.h` | Add tilt parameter (atomic, handler, registration, save/load) |
| `plugins/ruinae/src/controller/controller.h` | Add `xyMorphPad_` pointer member |
| `plugins/ruinae/src/controller/controller.cpp` | Wire XYMorphPad in verifyView(), forward setParamNormalized(), include header |
| `plugins/ruinae/src/processor/processor.h` | Add atomic for tilt parameter |
| `plugins/ruinae/src/processor/processor.cpp` | Handle tilt in processParameterChanges() |
| `plugins/ruinae/resources/editor.uidesc` | Add XYMorphPad view in Oscillator Mixer section |

## Architecture Decisions

1. **Header-only shared control**: Follows ArcKnob, FieldsetContainer, StepPatternEditor pattern. All in `Krate::Plugins` namespace.

2. **Dual-parameter communication**: X axis uses standard CControl tag binding (setValue + valueChanged). Y axis uses direct controller->performEdit() calls on the secondary parameter ID. This is the proven pattern from Disrumpo's MorphPad.

3. **Bilinear gradient**: 4 configurable corner colors interpolated on a square grid (default 24x24). Uses composable `lerpColor()` from color_utils.h plus a new `bilinearColor()` convenience function.

4. **VSTGUI 4.11+ Event API**: All mouse interaction uses the modern event API (onMouseDownEvent, etc.) consistent with Disrumpo MorphPad.

5. **ViewCreator registration**: Inline global variable triggers static registration. Attributes enable per-plugin customization of colors, grid size, and secondary parameter tag from .uidesc XML.

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build Ruinae plugin
"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae

# Run plugin tests (if any)
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Implementation Order

1. **bilinearColor utility** -- Add to color_utils.h, write unit test
2. **XYMorphPad control** -- Header-only class with drawing, interaction, coordinate conversion
3. **ViewCreator** -- Registration struct with all configurable attributes
4. **Parameter plumbing** -- Add kMixerTiltId, extend MixerParams, wire processor
5. **Controller integration** -- Wire in verifyView, forward setParamNormalized
6. **editor.uidesc** -- Place pad in Oscillator Mixer section
7. **Pluginval** -- Validate plugin loads correctly

## Reference Implementations

- **Disrumpo MorphPad**: `plugins/disrumpo/src/controller/views/morph_pad.h/.cpp` -- Primary reference for dual-parameter pattern, cursor rendering, coordinate conversion, fine adjustment
- **StepPatternEditor**: `plugins/shared/src/ui/step_pattern_editor.h` -- Pattern for shared CControl, ViewCreator, ParameterCallback, drawing decomposition
- **ArcKnob**: `plugins/shared/src/ui/arc_knob.h` -- Pattern for ViewCreator with color/float attributes, modulation visualization
