# Quickstart: Specialized Lane Types (080)

**Branch**: `080-specialized-lane-types` | **Date**: 2026-02-24

---

## What This Feature Does

Extends the Ruinae arpeggiator's visual lane editor with 4 new specialized lane types (Pitch, Ratchet, Modifier, Condition), each with unique rendering and interaction paradigms. Integrates all 6 lanes into a single scrollable stacked container. Introduces the `IArpLane` interface for polymorphic lane management and extracts `ArpLaneHeader` as a shared helper.

---

## Architecture Overview

```
IArpLane (interface)
  |
  +-- ArpLaneEditor (bar/bipolar/discrete modes) --- owns ArpLaneHeader
  |     - kVelocity: standard bar chart (Phase 11a)
  |     - kGate: standard bar chart (Phase 11a)
  |     - kPitch: bipolar bars with center line (NEW)
  |     - kRatchet: stacked blocks 1-4 (NEW)
  |
  +-- ArpModifierLane (4-row toggle dot grid) ------- owns ArpLaneHeader
  |     - Rest/Tie/Slide/Accent bitmask per step (NEW)
  |
  +-- ArpConditionLane (enum popup per step) --------- owns ArpLaneHeader
        - 18 TrigCondition values with popup menu (NEW)

ArpLaneContainer
  - holds std::vector<IArpLane*>
  - manages vertical stacking, scroll, collapse/expand
```

---

## File Map

### New Files (plugins/shared/src/ui/)

| File | Purpose |
|------|---------|
| `arp_lane.h` | `IArpLane` pure virtual interface |
| `arp_lane_header.h` | `ArpLaneHeader` shared header helper (non-CView) |
| `arp_modifier_lane.h` | `ArpModifierLane` custom view + ViewCreator |
| `arp_condition_lane.h` | `ArpConditionLane` custom view + ViewCreator |

### Modified Files

| File | Changes |
|------|---------|
| `arp_lane_editor.h` | Add bipolar/discrete modes, implement IArpLane, delegate header to ArpLaneHeader |
| `arp_lane_container.h` | Change from `ArpLaneEditor*` to `IArpLane*`, generalize addLane/removeLane |
| `plugins/ruinae/src/plugin_ids.h` | Add 4 playhead param IDs (3296-3299) |
| `plugins/ruinae/src/parameters/arpeggiator_params.h` | Register playhead params, add playhead handling |
| `plugins/ruinae/src/controller/controller.h` | Add pointers for 4 new lanes |
| `plugins/ruinae/src/controller/controller.cpp` | Construct/wire 4 new lanes, poll playheads |
| `plugins/ruinae/resources/editor.uidesc` | Register 4 new named colors |
| `plugins/shared/CMakeLists.txt` | (No changes needed -- header-only files included via controller) |
| `plugins/shared/tests/CMakeLists.txt` | Add test files for new components |

### New Test Files

| File | Purpose |
|------|---------|
| `plugins/shared/tests/test_arp_modifier_lane.cpp` | ArpModifierLane unit tests |
| `plugins/shared/tests/test_arp_condition_lane.cpp` | ArpConditionLane unit tests |
| `plugins/shared/tests/test_arp_lane_header.cpp` | ArpLaneHeader unit tests |

---

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Run shared tests
build/windows-x64-release/bin/Release/shared_tests.exe

# Run Ruinae tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

---

## Key Integration Points

1. **ArpLaneEditor -> IArpLane**: ArpLaneEditor already has `getExpandedHeight()`, `isCollapsed()`, etc. It needs to formally implement `IArpLane` and add `getView()` (returns `this`), `setPlayheadStep()` (delegates to `setPlaybackStep()`), `setLength()` (delegates to `setNumSteps()`).

2. **ArpLaneContainer -> IArpLane**: The container's `lanes_` vector changes type. The `recalculateLayout()` method already calls the right interface methods. The `addLane()`/`removeLane()` signatures change to accept `IArpLane*`.

3. **Controller Lane Wiring**: The controller creates each lane with `new`, configures accent color, param IDs, and callbacks, then calls `container->addLane()`. The 4 new lanes follow the exact same pattern as velocity/gate.

4. **Playhead Polling**: The existing `playbackPollTimer_` callback polls playhead parameters. 4 new `if (newLane) { ... }` blocks are added following the velocity/gate pattern.

5. **Parameter Sync**: The `setParamNormalized()` method in controller.cpp handles host-to-UI sync. 4 new blocks dispatch to the new lanes.
