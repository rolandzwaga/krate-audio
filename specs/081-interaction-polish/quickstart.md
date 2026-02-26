# Quickstart: Arpeggiator Interaction Polish (081)

## What This Spec Implements

Seven interconnected UI features for the Ruinae arpeggiator:

1. **Playhead Trail** - Fading 3-step trail behind the playhead in all 6 lanes
2. **Skip Step Indicators** - X overlay when steps are skipped (condition, probability, rest)
3. **Transform Buttons** - Invert, Shift L/R, Randomize in each lane header
4. **Copy/Paste** - Right-click lane header for cross-type pattern clipboard
5. **Euclidean Dot Display** - Circular visualization of E(k,n) pattern
6. **Bottom Bar Controls** - Euclidean section, Humanize, Spice, Ratchet Swing, Dice, Fill
7. **Color Scheme** - Final color pass across all visual elements

## Architecture Overview

```
PROCESSOR (audio thread)                    CONTROLLER (UI thread)
+-------------------------+                 +----------------------------------+
| ArpeggiatorCore         |                 | Trail Timer (30fps CVSTGUITimer) |
|   processBlock()        |                 |   polls 6 playhead params        |
|   -> ArpEvent[kSkip]    |   IMessage      |   shifts trail buffers           |
|   -> sendSkipEvent()  ------"ArpSkip"---->|   handleArpSkipEvent()           |
|                         |                 |   invalidates dirty lanes        |
+-------------------------+                 +----------------------------------+
                                            | LaneClipboard (copy/paste)       |
                                            | EuclideanDotDisplay              |
                                            | Bottom Bar Controls              |
                                            +----------------------------------+
```

## Key Files to Modify

### Shared UI Components (`plugins/shared/src/ui/`)

| File | Changes |
|------|---------|
| `arp_lane.h` | Extend IArpLane interface with trail, skip, transform, copy/paste methods |
| `arp_lane_header.h` | Add transform button drawing + right-click context menu |
| `arp_lane_editor.h` | Implement trail/skip rendering, transform logic for bar lanes |
| `arp_modifier_lane.h` | Implement trail/skip rendering, transform logic for bitmask lane |
| `arp_condition_lane.h` | Implement trail/skip rendering, transform logic for condition lane |
| `arp_lane_container.h` | No changes needed (container is type-agnostic) |
| `euclidean_dot_display.h` | **NEW** - Circular E(k,n) CView |

### Ruinae Plugin (`plugins/ruinae/src/`)

| File | Changes |
|------|---------|
| `controller/controller.h` | Add clipboard, trail timer, Euclidean display, bottom bar pointers |
| `controller/controller.cpp` | Wire trail timer, handle skip events, wire bottom bar, transforms |
| `processor/processor.h` | Add pre-allocated skip IMessages, editorOpen flag |
| `processor/processor.cpp` | Send skip events from arp engine, handle editor open/close |
| `resources/editor.uidesc` | Bottom bar layout with all controls |

### DSP Layer (potential)

| File | Changes |
|------|---------|
| `dsp/include/krate/dsp/processors/arpeggiator_core.h` | Add kSkip event type if not already present |

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build Ruinae plugin
"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae

# Build and run shared tests
"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests
build/windows-x64-release/bin/Release/shared_tests.exe

# Build and run Ruinae tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Implementation Order

The recommended implementation order follows dependency chains:

1. **IArpLane interface extension** (needed by everything else) -- Phase 1
2. **Foundation: trail timer member, skip IMessage pool, clipboard struct, ArpLaneHeader scaffold** -- Phase 2
3. **Trail rendering in all 6 lane types** + trail timer in controller -- Phase 3 (US1, P1)
4. **Skip event: processor sends, controller receives** (IMessage) + skip overlay in all 6 lane types -- Phase 4 (US2, P1)
5. **ArpLaneHeader transform buttons** (drawing + hit detection) + **Transform logic per lane type** -- Phase 5 (US3, P1)
6. **Copy/paste clipboard + context menu** -- Phase 6 (US4, P2)
7. **EuclideanDotDisplay** (standalone CView) -- Phase 7 (US5, P2)
8. **Bottom bar layout** (uidesc + controller wiring) -- Phase 8 (US6, P2; depends on Phase 7)
9. **Color scheme verification pass** -- Phase 9 (US7, P3; depends on Phases 3-8 complete)
10. **Pluginval + clang-tidy + ASan** -- Phase 10
11. **Architecture documentation updates** -- Phase 11
12. **Honest completion verification** -- Phase 12

Note: This ordering matches the task phase dependency graph in `tasks.md`. Phases 3, 5, 6, and 7 are independent after Phase 2 and can run in parallel; Phase 4 (US2) shares `PlayheadTrailState` with Phase 3 (US1) and is safest done sequentially after it; Phase 8 depends on Phase 7 (EuclideanDotDisplay must exist first).

## Key Patterns to Follow

### CVSTGUITimer (trail timer)
Reference: `StepPatternEditor` lines 168-169 for timer creation pattern.

### IMessage pre-allocation (skip events)
Reference: `Processor::initialize()` in processor.cpp lines 383-447 for existing IMessage patterns.

### Parameter edit protocol (transforms)
```cpp
beginEdit(paramId);
performEdit(paramId, newNormalized);
setParamNormalized(paramId, newNormalized);
endEdit(paramId);
```

### COptionMenu popup (copy/paste context menu)
Reference: `ArpLaneHeader::openLengthDropdown()` for existing popup pattern.

### ActionButton icon styles
```cpp
ActionIconStyle::kInvert     // Two opposing arrows
ActionIconStyle::kShiftLeft  // Left arrow
ActionIconStyle::kShiftRight // Right arrow
ActionIconStyle::kRegen      // Circular refresh (for Randomize and Dice)
```
