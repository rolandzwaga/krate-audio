# Quickstart: 079-layout-framework

## What This Feature Does

Restructures the Ruinae SEQ tab layout: shrinks the Trance Gate section from ~390px to ~100px, expands the Arpeggiator section, and introduces a multi-lane editing framework (ArpLaneEditor + ArpLaneContainer) for velocity and gate lanes with per-lane playhead visualization.

## Key Files to Create

| File | Description |
|------|-------------|
| `plugins/shared/src/ui/arp_lane_editor.h` | ArpLaneEditor: StepPatternEditor subclass with header, collapse, accent color, range labels |
| `plugins/shared/src/ui/arp_lane_container.h` | ArpLaneContainer: CViewContainer subclass with vertical scroll, lane stacking |

## Key Files to Modify

| File | Description |
|------|-------------|
| `plugins/shared/src/ui/step_pattern_editor.h` | Add `barAreaTopOffset_` member + setter for subclass header support |
| `plugins/ruinae/src/plugin_ids.h` | Add kArpVelocityPlayheadId (3294), kArpGatePlayheadId (3295) |
| `plugins/ruinae/src/parameters/arpeggiator_params.h` | Register playhead parameters (hidden, non-automatable) |
| `plugins/ruinae/src/controller/controller.h` | Add ArpLaneEditor pointers, ArpLaneContainer pointer |
| `plugins/ruinae/src/controller/controller.cpp` | Wire ArpLaneEditor instances in verifyView/didOpen, poll playhead in timer |
| `plugins/ruinae/src/processor/processor.cpp` | Write playhead positions to parameter output |
| `plugins/ruinae/resources/editor.uidesc` | Restructure Tab_Seq: shrink TG, add arp lanes, register colors |

## Key Files to Create (Tests)

| File | Description |
|------|-------------|
| `plugins/shared/tests/test_arp_lane_editor.cpp` | Unit tests for ArpLaneEditor (layout, collapse, color, range) |
| `plugins/shared/tests/test_arp_lane_container.cpp` | Unit tests for ArpLaneContainer (stacking, scroll, layout) |

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Run tests
build/windows-x64-release/bin/Release/shared_tests.exe
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Architecture Diagram

```
Tab_Seq (1400 x 620)
+================================================================+ y=0
|  TRANCE GATE  [ON] Steps:[16] [Presets v] [invert][<-][->]    |
|  Sync:[knob] Rate:[1/16 v] Depth:[knob] Atk:[knob] Rel:[knob]|
|  Phase:[knob] [Eucl] Hits:[knob] Rot:[knob]      ~26px toolbar|
|  +----------------------------------------------------------+  |
|  |  Thin StepPatternEditor bars (~70px)                      |  |
|  +----------------------------------------------------------+  |
+================================================================+ y~=104
|  --- divider ---                                               |
+================================================================+ y~=108
|  ARPEGGIATOR  [ON] Mode:[Up v] Oct:[2] [Seq v]                |
|  Sync:[knob] Rate:[1/16 v] Gate:[knob] Swing:[knob]           |
|  Latch:[Hold v] Retrig:[Note v]                  ~40px toolbar |
+----------------------------------------------------------------+ y~=148
|                                                                |
|  ArpLaneContainer (CViewContainer, ~390px viewport,            |
|  manual scroll offset -- NOT CScrollView, see R-002)          |
|  +----------------------------------------------------------+  |
|  | [v] VEL  [16 v] [copper bars, 0-1 normalized]    ~86px   |  |
|  | (v = expanded; > = collapsed -- both are triangle icons)  |  |
|  | [v] GATE [16 v] [sand bars, 0-200% gate length]  ~86px   |  |
|  |                                                           |  |
|  +----------------------------------------------------------+  |
|  Per-lane playhead highlight (basic, no trail in 11a)          |
+================================================================+ y~=540
  Total arp section height: ~40px toolbar + ~390px lanes = ~430px
  (y~=148 to y~=540; "~510px freed" = space reclaimed from TG)

Class hierarchy:
  StepPatternEditor (existing, modified: +barAreaTopOffset_)
    |
    +-- ArpLaneEditor (new, shared)
          - lane type, accent color, header, collapse, preview
          - per-lane length param; playhead set by controller via setPlaybackStep()

  CViewContainer (VSTGUI)
    |
    +-- ArpLaneContainer (new, shared)
          - vertical stacking, manual scroll offset, left-alignment
          - dynamic height on collapse/expand
```

## StepPatternEditor Modification

The only change to StepPatternEditor is adding a configurable top offset to `getBarArea()`:

```cpp
// New member:
float barAreaTopOffset_ = 0.0f;

// New setter:
void setBarAreaTopOffset(float offset) { barAreaTopOffset_ = offset; }

// Modified getBarArea():
float top = static_cast<float>(vs.top) + kPhaseOffsetHeight + barAreaTopOffset_;
```

This is backward-compatible (default 0.0f preserves existing behavior for Trance Gate).

## Implementation Order

1. Modify StepPatternEditor (add barAreaTopOffset_)
2. Create ArpLaneEditor (header, collapse, colors, range labels)
3. Create ArpLaneContainer (stacking, scroll)
4. Add playhead parameter IDs and registration
5. Restructure editor.uidesc Tab_Seq layout
6. Wire controller (verifyView, setParamNormalized, timer poll)
7. Wire processor playhead output
8. Test everything
