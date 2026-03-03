# Contract: Voice Route IMessage Protocol

**Spec**: 049-mod-matrix-grid | **Type**: IMessage Communication

## Overview

Per-voice modulation routes are NOT exposed as VST parameters (FR-041). They use bidirectional IMessage communication between Controller and Processor. The Processor is authoritative -- it sends back full state after any update.

## Messages

### Controller -> Processor: VoiceModRouteUpdate

**Purpose**: User edits a voice route in the ModMatrixGrid Voice tab.

**Message ID**: `"VoiceModRouteUpdate"`

**Attributes**:

| Key | Type | Range | Description |
|---|---|---|---|
| `slotIndex` | int64 | 0-15 | Voice route slot index |
| `source` | int64 | 0-6 | ModSource enum value (voice sources only) |
| `destination` | int64 | 0-6 | ModDestination enum value (voice destinations only) |
| `amount` | double | [-1.0, +1.0] | Bipolar modulation amount |
| `curve` | int64 | 0-3 | 0=Linear, 1=Exponential, 2=Logarithmic, 3=S-Curve |
| `smoothMs` | double | [0.0, 100.0] | Smoothing time in milliseconds |
| `scale` | int64 | 0-4 | 0=x0.25, 1=x0.5, 2=x1, 3=x2, 4=x4 |
| `bypass` | int64 | 0 or 1 | Bypass toggle |
| `active` | int64 | 0 or 1 | Whether slot is occupied |

**Sender**: Controller, when user adds/edits/removes a voice route.

**Receiver**: Processor, applies to VoiceModRouter.

### Controller -> Processor: VoiceModRouteRemove

**Purpose**: User removes a voice route.

**Message ID**: `"VoiceModRouteRemove"`

**Attributes**:

| Key | Type | Range | Description |
|---|---|---|---|
| `slotIndex` | int64 | 0-15 | Voice route slot to deactivate |

**Note**: Equivalent to sending VoiceModRouteUpdate with `active = 0`.

### Processor -> Controller: VoiceModRouteState

**Purpose**: Processor sends authoritative voice route state to Controller for UI sync.

**Message ID**: `"VoiceModRouteState"`

**Attributes**:

| Key | Type | Description |
|---|---|---|
| `routeCount` | int64 | Number of active routes (0-16) |
| `routeData` | binary (void*, size) | Packed array of 16 VoiceModRoute structs (224 bytes) |

**When sent**:
- After receiving a `VoiceModRouteUpdate` or `VoiceModRouteRemove`
- After `setComponentState()` (preset load)
- After state restore from host project

**Binary format** for `routeData`:
```
Offset  Size  Field
0       1     source (uint8_t)
1       1     destination (uint8_t)
2       4     amount (float, little-endian)
6       1     curve (uint8_t)
7       4     smoothMs (float, little-endian)
11      1     scale (uint8_t)
12      1     bypass (uint8_t)
13      1     active (uint8_t)
--- 14 bytes per route, 16 routes = 224 bytes total ---
```

## Sequence Diagrams

### User Adds Voice Route

```
Controller                         Processor
    |                                  |
    |-- VoiceModRouteUpdate ---------->|
    |   slotIndex=3, active=1,         |
    |   source=1, dest=0, amount=0.0   |
    |                                  |
    |<-- VoiceModRouteState -----------|
    |   routeCount=6, routeData=...    |
    |                                  |
    |-- update UI (ModMatrixGrid,      |
    |   ModHeatmap, ModRingIndicator)  |
```

### Host Loads Preset

```
Host                    Processor                    Controller
  |                         |                            |
  |-- setComponentState --->|                            |
  |                         |-- VoiceModRouteState ----->|
  |                         |   (full state from preset) |
  |                         |                            |
  |                         |                    update UI
```

### User Removes Voice Route

```
Controller                         Processor
    |                                  |
    |-- VoiceModRouteUpdate ---------->|
    |   slotIndex=3, active=0          |
    |                                  |
    |<-- VoiceModRouteState -----------|
    |   routeCount=5, routeData=...    |
    |                                  |
    |-- update UI                      |
```

## Error Handling

- **Invalid slot index**: Processor ignores messages with slotIndex >= 16
- **Invalid source/destination**: Processor clamps to valid range
- **Amount out of range**: Processor clamps to [-1.0, +1.0]
- **Missing attributes**: Processor uses defaults (source=0, dest=0, amount=0, etc.)

## Thread Safety

- IMessage send/receive happens on the message thread (not audio thread)
- Processor must use lock-free mechanism to propagate route changes to audio thread
- Controller receives VoiceModRouteState on the message thread and must defer UI updates to the UI thread
