# Data Model: Master Section Panel - Wire Voice & Output Controls

**Branch**: `054-master-section-panel` | **Date**: 2026-02-14

## Entities

### GlobalParams (extended)

**Location**: `plugins/ruinae/src/parameters/global_params.h`
**Type**: struct (POD with atomic fields)
**Purpose**: Thread-safe storage for global parameter values, read on the audio thread

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| masterGain | `std::atomic<float>` | 1.0f | 0.0-2.0 | Output gain (linear) |
| voiceMode | `std::atomic<int>` | 0 | 0-1 | 0=Poly, 1=Mono |
| polyphony | `std::atomic<int>` | 8 | 1-16 | Voice count |
| softLimit | `std::atomic<bool>` | true | true/false | Soft limiter on/off |
| **width** | **`std::atomic<float>`** | **1.0f** | **0.0-2.0** | **Stereo width (engine value): 0=mono, 1=natural, 2=extra-wide** |
| **spread** | **`std::atomic<float>`** | **0.0f** | **0.0-1.0** | **Voice spread: 0=center, 1=fully spread** |

**Bold** entries are new in this spec.

### Parameter IDs (extended)

**Location**: `plugins/ruinae/src/plugin_ids.h`
**Section**: Global Parameters (0-99)

| ID | Name | Existing? | Type |
|----|------|-----------|------|
| 0 | kMasterGainId | Yes | Continuous |
| 1 | kVoiceModeId | Yes | StringListParameter (dropdown) |
| 2 | kPolyphonyId | Yes | StringListParameter (dropdown) |
| 3 | kSoftLimitId | Yes | Toggle (stepCount=1) |
| **4** | **kWidthId** | **New** | **Continuous (0-1 normalized)** |
| **5** | **kSpreadId** | **New** | **Continuous (0-1 normalized)** |

## Parameter Pipeline

### Width Parameter Pipeline

```
Host/UI (normalized 0.0-1.0)
    |
    v
handleGlobalParamChange():  value * 2.0 -> GlobalParams.width (0.0-2.0)
    |
    v
Processor forwarding:  engine_.setStereoWidth(globalParams_.width.load())
    |
    v
RuinaeEngine::setStereoWidth(float):  stereoWidth_ = clamp(width, 0, 2)
    |
    v
Mid/Side processing in engine render
```

### Spread Parameter Pipeline

```
Host/UI (normalized 0.0-1.0)
    |
    v
handleGlobalParamChange():  value -> GlobalParams.spread (0.0-1.0, 1:1)
    |
    v
Processor forwarding:  engine_.setStereoSpread(globalParams_.spread.load())
    |
    v
RuinaeEngine::setStereoSpread(float):  stereoSpread_ = clamp(spread, 0, 1); recalculatePanPositions()
    |
    v
Voice pan distribution in engine render
```

### VoiceMode Pipeline (existing, UI added)

```
Host/UI (normalized 0.0 or 1.0)
    |
    v
handleGlobalParamChange():  int(value + 0.5) -> GlobalParams.voiceMode (0 or 1)
    |
    v
Processor forwarding:  engine_.setMode(voiceMode == 0 ? EngineMode::Poly : EngineMode::Mono)
    |                   (already implemented)
    v
Engine mode selection (already implemented)
```

## State Persistence Format

### Current format (4 fields, in order):
1. `float` masterGain (0.0-2.0)
2. `int32` voiceMode (0 or 1)
3. `int32` polyphony (1-16)
4. `int32` softLimit (0 or 1)

### New format (6 fields, in order):
1. `float` masterGain (0.0-2.0)
2. `int32` voiceMode (0 or 1)
3. `int32` polyphony (1-16)
4. `int32` softLimit (0 or 1)
5. **`float` width (0.0-2.0)** -- NEW, EOF-safe (defaults to 1.0 if missing)
6. **`float` spread (0.0-1.0)** -- NEW, EOF-safe (defaults to 0.0 if missing)

### Backward Compatibility

Old presets (4 fields) loaded into new code: Width defaults to 1.0 (natural stereo), Spread defaults to 0.0 (center). No crash, no error.

New presets (6 fields) loaded into old code: Old code reads 4 fields successfully, ignores remaining bytes in stream. No crash, no error (IBStreamer handles this gracefully).

## Display Formatting

| Parameter | Normalized | Display | Formula |
|-----------|-----------|---------|---------|
| Width | 0.0 | "0%" | `int(norm * 200 + 0.5)` + "%" |
| Width | 0.25 | "50%" | |
| Width | 0.5 | "100%" | |
| Width | 1.0 | "200%" | |
| Spread | 0.0 | "0%" | `int(norm * 100 + 0.5)` + "%" |
| Spread | 0.5 | "50%" | |
| Spread | 1.0 | "100%" | |
