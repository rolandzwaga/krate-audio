# Parameter Contracts: Master Section Panel

## New Parameters

### Width (kWidthId = 4)

| Property | Value |
|----------|-------|
| ID | 4 |
| Name | "Width" |
| Unit | "%" |
| Type | Continuous (Parameter) |
| Normalized Range | 0.0 - 1.0 |
| Engine Range | 0.0 - 2.0 |
| Denormalization | `norm * 2.0` |
| Default (normalized) | 0.5 |
| Default (engine) | 1.0 (natural stereo width) |
| Display Format | "%d%%" where value = `int(norm * 200 + 0.5)` |
| Flags | kCanAutomate |
| Engine Method | `RuinaeEngine::setStereoWidth(float)` |
| State Format | float (engine value 0.0-2.0) |

### Spread (kSpreadId = 5)

| Property | Value |
|----------|-------|
| ID | 5 |
| Name | "Spread" |
| Unit | "%" |
| Type | Continuous (Parameter) |
| Normalized Range | 0.0 - 1.0 |
| Engine Range | 0.0 - 1.0 |
| Denormalization | 1:1 (no conversion) |
| Default (normalized) | 0.0 |
| Default (engine) | 0.0 (all voices centered) |
| Display Format | "%d%%" where value = `int(norm * 100 + 0.5)` |
| Flags | kCanAutomate |
| Engine Method | `RuinaeEngine::setStereoSpread(float)` |
| State Format | float (engine value 0.0-1.0) |

## Existing Parameter (UI Binding Only)

### VoiceMode (kVoiceModeId = 1)

| Property | Value |
|----------|-------|
| ID | 1 |
| Name | "Voice Mode" |
| Type | StringListParameter (dropdown) |
| Items | "Polyphonic" (index 0), "Mono" (index 1) |
| Default | 0 (Polyphonic) |
| Flags | kCanAutomate, kIsList |
| Engine Method | `RuinaeEngine::setMode(EngineMode)` |
| Change in this spec | Add uidesc control-tag "VoiceMode" tag="1" and COptionMenu control |
| Registration change | Update first item from "Poly" to "Polyphonic" |

## UI Control Contracts

### Control Tags (editor.uidesc)

| Name | Tag | Control Type | New? |
|------|-----|-------------|------|
| MasterGain | 0 | ArcKnob | No |
| VoiceMode | 1 | COptionMenu | Yes |
| Polyphony | 2 | COptionMenu | No |
| SoftLimit | 3 | ToggleButton | No |
| Width | 4 | ArcKnob | Yes |
| Spread | 5 | ArcKnob | Yes |

### Panel Layout Contract

```
Panel: origin=(772,32), size=(120,160)
Fieldset: "Voice & Output", color="master"

Controls (y positions, all within 120px width):
  y=14:  "Mode" label (28px) + VoiceMode dropdown (56px) + gear icon (18px)
  y=34:  Polyphony dropdown (60px)
  y=56:  Output ArcKnob (36x36, centered at x=42)
  y=92:  "Output" label
  y=104: Width ArcKnob (28x28, x=14) + Spread ArcKnob (28x28, x=62)
  y=132: "Width" label (x=10) + "Spread" label (x=58)
  y=144: Soft Limit toggle (80px wide, centered at x=20)
```

## State Persistence Contract

### Write Order (saveGlobalParams)
1. float: masterGain
2. int32: voiceMode
3. int32: polyphony
4. int32: softLimit
5. float: width (NEW)
6. float: spread (NEW)

### Read Order (loadGlobalParams)
1. float: masterGain (REQUIRED - return false on failure)
2. int32: voiceMode (REQUIRED - return false on failure)
3. int32: polyphony (REQUIRED - return false on failure)
4. int32: softLimit (REQUIRED - return false on failure)
5. float: width (OPTIONAL - default 1.0f on EOF)
6. float: spread (OPTIONAL - default 0.0f on EOF)

### Controller Read Order (loadGlobalParamsToController)
Same as above, but converts back to normalized:
- width: `floatVal / 2.0f` (engine -> normalized)
- spread: `floatVal` (1:1, no conversion)
