# Data Model: ModMatrixGrid -- Modulation Routing UI

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Entities

### ModSource

**Type**: `enum class` (uint8_t)

| Value | Name | Color RGB | Full Name | Abbreviation | Scope |
|---|---|---|---|---|---|
| 0 | Env1 | (80,140,200) | ENV 1 (Amp) | E1 | Voice + Global |
| 1 | Env2 | (220,170,60) | ENV 2 (Filter) | E2 | Voice + Global |
| 2 | Env3 | (160,90,200) | ENV 3 (Mod) | E3 | Voice + Global |
| 3 | VoiceLFO | (90,200,130) | Voice LFO | VLFO | Voice + Global |
| 4 | GateOutput | (220,130,60) | Gate Output | Gt | Voice + Global |
| 5 | Velocity | (170,170,175) | Velocity | Vel | Voice + Global |
| 6 | KeyTrack | (80,200,200) | Key Track | Key | Voice + Global |
| 7 | Macros | (200,100,140) | Macros 1-4 | M1-4 | Global only |
| 8 | ChaosRungler | (190,55,55) | Chaos/Rungler | Chao | Global only |
| 9 | GlobalLFO | (60,210,100) | LFO 1-2 (Global) | LF12 | Global only |

**Validation**: Voice tab shows sources 0-6 (7 sources). Global tab shows sources 0-9 (10 sources).

### ModDestination

**Type**: `enum class` (uint8_t)

| Value | Name | Full Name | Voice Abbr | Global Abbr | Scope | Visible In Tab |
|---|---|---|---|---|---|---|
| 0 | FilterCutoff | Filter Cutoff | FCut | FCut | Voice + Global (forwarded) | Voice + Global |
| 1 | FilterResonance | Filter Resonance | FRes | FRes | Voice + Global (forwarded) | Voice + Global |
| 2 | MorphPosition | Morph Position | Mrph | Mrph | Voice + Global (forwarded) | Voice + Global |
| 3 | DistortionDrive | Distortion Drive | Drv | Drv | Voice + Global (forwarded) | Voice + Global |
| 4 | TranceGateDepth | TranceGate Depth | Gate | Gate | Voice + Global (forwarded) | Voice + Global |
| 5 | OscAPitch | OSC A Pitch | OsA | OsA | Voice + Global (forwarded) | Voice + Global |
| 6 | OscBPitch | OSC B Pitch | OsB | OsB | Voice + Global (forwarded) | Voice + Global |
| 7 | GlobalFilterCutoff | Global Filter Cutoff | -- | GFCt | Global only | Global only |
| 8 | GlobalFilterResonance | Global Filter Resonance | -- | GFRs | Global only | Global only |
| 9 | MasterVolume | Master Volume | -- | Mstr | Global only | Global only |
| 10 | EffectMix | Effect Mix | -- | FxMx | Global only | Global only |

**Validation**: Voice tab shows destinations 0-6 (7 destinations, 7 heatmap columns). Global tab shows destinations 0-10 (11 destinations = 4 global-scope + 7 forwarded voice, 11 heatmap columns). The two lists are cumulative: Global tab includes all voice destinations plus global-only ones.

### ModRoute

**Type**: `struct`
**Location**: `plugins/shared/src/ui/mod_source_colors.h` (alongside color definitions)

| Field | Type | Range | Default | Description |
|---|---|---|---|---|
| source | ModSource | 0-9 | Env1 (0) | Modulation source |
| destination | ModDestination | 0-10 | FilterCutoff (0) | Modulation destination |
| amount | float | [-1.0, +1.0] | 0.0 | Bipolar modulation amount |
| curve | uint8_t | 0-3 | 0 (Linear) | Response curve: 0=Linear, 1=Exponential, 2=Logarithmic, 3=S-Curve |
| smoothMs | float | [0, 100] | 0.0 | Smoothing time in milliseconds |
| scale | uint8_t | 0-4 | 2 (x1) | Scale multiplier: 0=x0.25, 1=x0.5, 2=x1, 3=x2, 4=x4 |
| bypass | bool | true/false | false | Whether route is temporarily bypassed |
| active | bool | true/false | false | Whether slot is occupied |

**Validation**: Duplicate source+destination combinations are allowed (amounts sum). Amount is clamped to [-1.0, +1.0] at the UI level. Effective amount = amount * scaleMultiplier, where scaleMultiplier = {0.25, 0.5, 1.0, 2.0, 4.0}[scale].

### VoiceModRoute

**Type**: `struct` (for IMessage serialization)
**Location**: `plugins/shared/src/ui/mod_source_colors.h`

Identical fields to ModRoute but with fixed-size types for binary serialization:

| Field | Type | Size | Description |
|---|---|---|---|
| source | uint8_t | 1 byte | ModSource value |
| destination | uint8_t | 1 byte | ModDestination value |
| amount | float | 4 bytes | [-1.0, +1.0] |
| curve | uint8_t | 1 byte | 0-3 |
| smoothMs | float | 4 bytes | 0-100 |
| scale | uint8_t | 1 byte | 0-4 |
| bypass | uint8_t | 1 byte | 0 or 1 |
| active | uint8_t | 1 byte | 0 or 1 |

**Total per route**: 14 bytes. **Total for 16 routes**: 224 bytes.

### ModRouteSlot

**Type**: Conceptual (not a separate struct; represented by array index)

| Property | Value |
|---|---|
| Global tab slot count | 8 (max) |
| Voice tab slot count | 16 (max) |
| Active slot | Contains a ModRoute with `active = true` |
| Empty slot | Contains a ModRoute with `active = false`; renders as "[+ Add Route]" button |

### ModSourceInfo

**Type**: `struct` (compile-time registry)
**Location**: `plugins/shared/src/ui/mod_source_colors.h`

| Field | Type | Description |
|---|---|---|
| color | VSTGUI::CColor | Source color (FR-011) |
| fullName | const char* | Full display name |
| abbreviation | const char* | Abbreviated name for heatmap rows |

### ModDestInfo

**Type**: `struct` (compile-time registry)
**Location**: `plugins/shared/src/ui/mod_source_colors.h`

| Field | Type | Description |
|---|---|---|
| fullName | const char* | Full display name |
| voiceAbbr | const char* | Abbreviated name for voice tab heatmap columns |
| globalAbbr | const char* | Abbreviated name for global tab heatmap columns |

## Relationships

```
ModMatrixGrid (1) --contains--> ModRoute[8] (Global slots)
ModMatrixGrid (1) --contains--> ModRoute[16] (Voice slots, cached from IMessage)
ModMatrixGrid (1) --contains--> ModHeatmap (1, child view)
ModMatrixGrid (1) --references--> ModSourceInfo[10] (static color registry)
ModMatrixGrid (1) --references--> ModDestInfo[11] (static name registry)

ModRingIndicator (N) --observes--> ModRoute[8] (Global, via IDependent on params)
ModRingIndicator (N) --observes--> ModRoute[16] (Voice, via controller cache)
ModRingIndicator (1) --targets--> destination ArcKnob (1, overlay relationship)

ModHeatmap (1) --visualizes--> ModRoute[8 or 16] (depending on active tab)
ModHeatmap (1) --references--> ModSourceInfo (for cell colors)

Controller (1) --mediates--> ModRingIndicator <-> ModMatrixGrid (route selection)
Controller (1) --mediates--> ModHeatmap <-> ModMatrixGrid (route selection)
Controller (1) --sends/receives--> Processor (IMessage for voice routes)
```

## State Transitions

### Route Slot State Machine

```
[Empty] --user clicks [+ Add Route]--> [Active, amount=0.0, default source/dest]
[Active] --user adjusts controls--> [Active, updated values]
[Active] --user clicks [x] remove--> [Empty]
[Active] --user toggles Bypass--> [Active, bypass=true, dimmed visuals]
[Active, bypass=true] --user toggles Bypass--> [Active, bypass=false]
[Active] --host loads preset--> [Active or Empty, from saved state]
```

### Tab State Machine

```
[Global tab active] --user clicks Voice tab--> [Voice tab active]
[Voice tab active] --user clicks Global tab--> [Global tab active]
```

Tab switching does NOT affect the other tab's data. Expanded row state is per-tab.

### ModRingIndicator State

ModRingIndicator has no explicit state machine. It is a pure visualization that redraws based on current parameter values:

```
Parameter changes --> IDependent::update() --> rebuild arc list --> setDirty() --> draw()
```

## Parameter Mapping

### Global Route Parameters (VST Parameters, IDs 1300-1355)

| Slot | Source ID | Dest ID | Amount ID | Curve ID | Smooth ID | Scale ID | Bypass ID |
|---|---|---|---|---|---|---|---|
| 0 | 1300 | 1301 | 1302 | 1324 | 1325 | 1326 | 1327 |
| 1 | 1303 | 1304 | 1305 | 1328 | 1329 | 1330 | 1331 |
| 2 | 1306 | 1307 | 1308 | 1332 | 1333 | 1334 | 1335 |
| 3 | 1309 | 1310 | 1311 | 1336 | 1337 | 1338 | 1339 |
| 4 | 1312 | 1313 | 1314 | 1340 | 1341 | 1342 | 1343 |
| 5 | 1315 | 1316 | 1317 | 1344 | 1345 | 1346 | 1347 |
| 6 | 1318 | 1319 | 1320 | 1348 | 1349 | 1350 | 1351 |
| 7 | 1321 | 1322 | 1323 | 1352 | 1353 | 1354 | 1355 |

**Formula for slot N (0-7)**:
- Source ID = 1300 + N * 3
- Destination ID = 1301 + N * 3
- Amount ID = 1302 + N * 3
- Curve ID = 1324 + N * 4
- Smooth ID = 1325 + N * 4
- Scale ID = 1326 + N * 4
- Bypass ID = 1327 + N * 4

### Voice Route Parameters (IMessage, not VST Parameters)

Voice routes use the VoiceModRoute struct serialized via IMessage. 16 slots, indexed 0-15. Not host-automatable.

### Normalized <-> Plain Value Mapping

| Parameter | Type | Normalized Range | Plain Range | Mapping |
|---|---|---|---|---|
| Source | StringListParameter | [0.0, 1.0] | 0-9 (Global) or 0-6 (Voice) | Automatic via stepCount |
| Destination | StringListParameter | [0.0, 1.0] | 0-10 (Global) or 0-6 (Voice) | Automatic via stepCount |
| Amount | RangeParameter | [0.0, 1.0] | [-1.0, +1.0] | `plain = normalized * 2.0 - 1.0` |
| Curve | StringListParameter | [0.0, 1.0] | 0-3 | Automatic via stepCount |
| Smooth | RangeParameter | [0.0, 1.0] | [0.0, 100.0] ms | `plain = normalized * 100.0` |
| Scale | StringListParameter | [0.0, 1.0] | 0-4 | Automatic via stepCount |
| Bypass | Parameter | [0.0, 1.0] | 0 or 1 | `plain = normalized >= 0.5 ? 1 : 0` |
