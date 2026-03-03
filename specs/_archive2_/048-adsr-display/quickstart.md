# Quickstart: ADSRDisplay Implementation

**Feature**: 048-adsr-display | **Date**: 2026-02-10

## Overview

This feature adds an interactive ADSR envelope display/editor as a shared VSTGUI custom control. It includes:
1. A new Layer 0 DSP utility (`curve_table.h`) for lookup table generation
2. Modifications to the Layer 1 `ADSREnvelope` for continuous curve support
3. A new shared UI component (`adsr_display.h`) with ViewCreator registration
4. 48 new VST parameters for curve amounts, Bezier control points, and mode flags
5. Controller integration in the Ruinae plugin

## Priority Phases

Implementation is organized by the spec's priority levels:

### Phase A (P1 - Core)
- Curve table utility (Layer 0)
- ADSREnvelope DSP modifications
- ADSRDisplay rendering (filled curve, grid, labels)
- Control point dragging (Peak, Sustain, End)
- Parameter communication and knob synchronization

### Phase B (P2 - Interaction)
- Curve segment dragging (bend curves)
- Fine adjustment (Shift+drag)
- Double-click reset
- Escape cancel

### Phase C (P3 - Pro Features)
- Bezier mode toggle and handles
- Bezier-to-Simple / Simple-to-Bezier conversion
- Playback dot visualization

## Key Files

### New Files
| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/core/curve_table.h` | Layer 0: Curve lookup table generation |
| `dsp/tests/core/curve_table_tests.cpp` | Tests for curve table utility |
| `plugins/shared/src/ui/adsr_display.h` | ADSRDisplay CControl (header-only) |
| `plugins/shared/tests/adsr_display_tests.cpp` | Unit tests for ADSRDisplay logic |

### Modified Files
| File | Changes |
|------|---------|
| `dsp/include/krate/dsp/primitives/adsr_envelope.h` | Add continuous curve support + lookup tables |
| `dsp/tests/primitives/adsr_envelope_tests.cpp` | Add tests for continuous curves |
| `plugins/ruinae/src/plugin_ids.h` | Add 48 new parameter IDs |
| `plugins/ruinae/src/parameters/amp_env_params.h` | Add curve + Bezier fields |
| `plugins/ruinae/src/parameters/filter_env_params.h` | Add curve + Bezier fields |
| `plugins/ruinae/src/parameters/mod_env_params.h` | Add curve + Bezier fields |
| `plugins/ruinae/src/controller/controller.cpp` | Wire ADSRDisplay callbacks in verifyView() |
| `plugins/ruinae/src/controller/controller.h` | Add ADSRDisplay pointer storage |
| `plugins/ruinae/src/processor/processor.cpp` | Send envelope display state via IMessage |
| `plugins/ruinae/src/processor/processor.h` | Add atomic display state fields |
| `plugins/shared/CMakeLists.txt` | Add adsr_display.h to sources |
| `plugins/ruinae/resources/editor.uidesc` | Add 3 ADSRDisplay instances |

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests (curve table + envelope)
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Build full plugin
"$CMAKE" --build build/windows-x64-release --config Release

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Iterum.vst3"
```

## Patterns to Follow

| Pattern | Reference |
|---------|-----------|
| Header-only CControl + ViewCreator | `plugins/shared/src/ui/arc_knob.h` |
| ParameterCallback for multi-param | `plugins/shared/src/ui/step_pattern_editor.h` |
| Fine adjustment + Escape cancel | `plugins/shared/src/ui/xy_morph_pad.h` |
| CVSTGUITimer for playback refresh | `plugins/shared/src/ui/step_pattern_editor.h` (line 163) |
| color_utils.h for color manipulation | `plugins/shared/src/ui/color_utils.h` |
| IMessage for processor-controller comms | `plugins/ruinae/src/processor/processor.cpp` (line 182) |
| verifyView() for wiring custom controls | `plugins/ruinae/src/controller/controller.cpp` (line 346) |

## Architecture Decisions

1. **Callback pattern over direct controller access**: ADSRDisplay uses `ParameterCallback` (like StepPatternEditor) rather than holding a controller pointer (like XYMorphPad). This scales better for 7+ parameters.

2. **Curve table in Layer 0**: The `curve_table.h` utility is pure math with no DSP state, making it suitable for Layer 0. Both ADSREnvelope (Layer 1) and ADSRDisplay (UI) can use it.

3. **Table-based envelope processing**: Replaces the one-pole coefficient approach for shaped curves. The table is the abstraction boundary between Simple and Bezier modes -- the audio thread sees no difference.

4. **Backward-compatible EnvCurve API**: The existing `setAttackCurve(EnvCurve)` overloads are preserved, internally converting to continuous curve amounts. No existing code breaks.
