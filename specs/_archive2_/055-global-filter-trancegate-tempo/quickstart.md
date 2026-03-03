# Quickstart: Global Filter Strip & Trance Gate Tempo Sync

**Date**: 2026-02-15
**Spec**: [spec.md](spec.md)
**Plan**: [plan.md](plan.md)

## What This Feature Does

Adds three UI-only changes to the Ruinae synthesizer plugin:

1. **Global Filter Strip** -- A slim 36px horizontal strip between the Timbre row and the Trance Gate row, exposing 4 already-implemented global filter parameters (Enable, Type, Cutoff, Resonance).

2. **Trance Gate Tempo Sync Toggle** -- A toggle button in the Trance Gate toolbar that switches the bottom-row Rate knob to a NoteValue dropdown for tempo-synced operation.

3. **Window Height Increase** -- The editor window grows from 900x830 to 900x866 pixels to accommodate the new filter strip.

## Files to Modify

| File | Changes |
|------|---------|
| `plugins/ruinae/resources/editor.uidesc` | Add color, control-tags, resize window, shift rows, add Global Filter strip, add Sync toggle, add Rate/NoteValue groups |
| `plugins/ruinae/src/controller/controller.h` | Add 2 view pointer declarations |
| `plugins/ruinae/src/controller/controller.cpp` | Add visibility toggle in setParamNormalized, verifyView capture, willClose cleanup |

## No Files to Create

This is purely UI-layer work. No new C++ files, headers, DSP code, parameter registrations, or state persistence changes.

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae
# Plugin at: build/windows-x64-release/VST3/Release/Ruinae.vst3/
# Run pluginval:
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Key Patterns to Follow

The Trance Gate Rate/NoteValue visibility switching follows the exact same pattern as LFO1/LFO2/Chaos/Delay/Phaser sync toggles already in the codebase. See `research.md` R-007 for the complete pattern reference.

## Critical Decision

`kTranceGateNoteValueId` (607) is the tempo-synced rate parameter. The spec initially suggested creating a new parameter, but research confirmed (see `research.md` R-001) that parameter 607 already serves this purpose. Both the toolbar dropdown and the bottom-row dropdown bind to the same control-tag.
