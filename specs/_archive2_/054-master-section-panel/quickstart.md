# Quickstart: Master Section Panel - Wire Voice & Output Controls

**Branch**: `054-master-section-panel` | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## What This Feature Does

Wires the Voice & Output panel with functional controls:
1. Adds a **Voice Mode dropdown** (Polyphonic/Mono) bound to the already-registered `kVoiceModeId` parameter
2. Adds two **new global parameters** (`kWidthId = 4`, `kSpreadId = 5`) for stereo Width and Spread
3. Binds the existing placeholder Width and Spread knobs to these new parameters
4. Forwards the new parameters through the processor to the engine's existing `setStereoWidth()` and `setStereoSpread()` methods

No DSP code changes. This is purely parameter pipeline wiring + UI binding.

## Files to Modify

| File | Change | Why |
|------|--------|-----|
| `plugins/ruinae/src/plugin_ids.h` | Add `kWidthId = 4`, `kSpreadId = 5` | New parameter IDs |
| `plugins/ruinae/src/parameters/global_params.h` | Extend struct + 6 functions | Parameter storage, handling, registration, formatting, persistence |
| `plugins/ruinae/src/processor/processor.cpp` | Add 2 `engine_.set*()` calls | Forward params to engine |
| `plugins/ruinae/resources/editor.uidesc` | Add 3 control-tags + Voice Mode dropdown + wire knobs | UI binding |

## Implementation Order

1. **IDs first**: Add kWidthId and kSpreadId to `plugin_ids.h`
2. **Struct + handlers**: Extend GlobalParams struct and handleGlobalParamChange in `global_params.h`
3. **Registration + formatting**: Extend registerGlobalParams and formatGlobalParam
4. **State persistence**: Extend save/load functions with EOF-safe pattern
5. **Processor forwarding**: Add engine_.setStereoWidth/Spread calls in `processor.cpp`
6. **UI wiring**: Add control-tags and reorganize Voice & Output panel in `editor.uidesc`
7. **Build and verify**: Build, run pluginval, visually verify

## Key Patterns to Follow

### Existing Parameter Pattern (reference: MasterGain)

The Width parameter follows the exact same pattern as MasterGain:

```cpp
// plugin_ids.h
kWidthId = 4,

// GlobalParams struct
std::atomic<float> width{1.0f};

// handleGlobalParamChange - denormalize
case kWidthId:
    params.width.store(std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f),
                       std::memory_order_relaxed);
    break;

// registerGlobalParams
parameters.addParameter(STR16("Width"), STR16("%"), 0, 0.5,
    ParameterInfo::kCanAutomate, kWidthId);

// formatGlobalParam - display as percentage
case kWidthId: {
    int pct = static_cast<int>(value * 200.0 + 0.5);
    char8 text[32];
    snprintf(text, sizeof(text), "%d%%", pct);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// processor.cpp - forward to engine
engine_.setStereoWidth(globalParams_.width.load(std::memory_order_relaxed));
```

### EOF-Safe State Loading Pattern

```cpp
// In loadGlobalParams - after reading the 4 existing fields:
if (streamer.readFloat(floatVal))
    params.width.store(floatVal, std::memory_order_relaxed);
// else: keep default 1.0f -- old preset, no Width data

if (streamer.readFloat(floatVal))
    params.spread.store(floatVal, std::memory_order_relaxed);
// else: keep default 0.0f -- old preset, no Spread data
```

Key difference from existing fields: Do NOT return false on read failure. Width and Spread are optional for backward compatibility.

### COptionMenu for VoiceMode (uidesc)

```xml
<control-tag name="VoiceMode" tag="1"/>

<view class="COptionMenu" origin="32, 14" size="56, 18"
      control-tag="VoiceMode"
      default-value="0"
      font="~ NormalFontSmaller"
      font-color="master"
      back-color="bg-dropdown"
      frame-color="frame-dropdown-dim"
      tooltip="Voice Mode"
      transparent="false"/>
```

The COptionMenu automatically populates its items from the StringListParameter registered for tag 1. No manual menu item addition needed.

### Wiring Placeholder Knobs (uidesc)

```xml
<!-- Before (placeholder): -->
<view class="ArcKnob" origin="14, 114" size="28, 28"
      arc-color="master"
      guide-color="knob-guide"/>

<!-- After (functional): -->
<view class="ArcKnob" origin="14, 114" size="28, 28"
      control-tag="Width"
      default-value="0.5"
      arc-color="master"
      guide-color="knob-guide"/>
```

Just add `control-tag` and `default-value` attributes. Everything else stays the same.

## Gotchas

1. **Width denormalization**: norm * 2.0 = engine value, NOT norm * 200 (that is display only). The engine expects 0.0-2.0, where 1.0 = natural stereo width.

2. **Width controller sync**: When loading state, the stored value is the engine value (0-2). To send to controller, divide by 2.0 to get normalized: `setParam(kWidthId, floatVal / 2.0)`.

3. **Spread has no conversion**: Both normalized and engine values are 0.0-1.0. No multiplication needed.

4. **VoiceMode display text**: The spec says "Polyphonic" and "Mono". The current registration has "Poly" and "Mono". Update the registration from `STR16("Poly")` to `STR16("Polyphonic")` per FR-002.

5. **EOF handling asymmetry**: The existing 4 fields use `if (!read()) return false` (hard failure). The new 2 fields use `if (read()) store()` (soft failure with default). This is intentional -- old presets legitimately lack these fields.

6. **Panel vertical space**: With the Voice Mode row added, all controls must shift down. Use the layout coordinates from the plan (Option A) to ensure everything fits within 160px.

7. **Soft Limit toggle position**: Must be adjusted to y=144 (from y=142) to accommodate the shifted layout. Verify it fits within the 160px panel height.

## Verification

### Manual Checks
- Open plugin UI, verify Voice Mode dropdown shows "Polyphonic" (default)
- Click dropdown, verify "Polyphonic" and "Mono" items appear
- Select "Mono", play multiple notes, verify only one sounds
- Switch back to "Polyphonic", play chord, verify polyphony
- Turn Width knob: minimum = mono, center = natural, maximum = extra-wide
- Turn Spread knob with chord playing: minimum = centered, maximum = spread across stereo field
- Save preset, reload, verify all values restored
- Load old preset (pre-Width/Spread), verify Width=100% and Spread=0% defaults

### Pluginval
```bash
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```
Expected: All tests pass with exit code 0.
