# Plugin Parameter Contract: Spice/Dice & Humanize

**Feature**: 077-spice-dice-humanize | **Date**: 2026-02-23

## New Parameters

| Name | ID | Type | Range | Default | Step Count | Flags | Display |
|---|---|---|---|---|---|---|---|
| Arp Spice | 3290 | Continuous | 0.0 - 1.0 | 0.0 | 0 | kCanAutomate | "0%" - "100%" |
| Arp Dice | 3291 | Discrete | 0 - 1 | 0 | 1 | kCanAutomate | "--" / "Roll" |
| Arp Humanize | 3292 | Continuous | 0.0 - 1.0 | 0.0 | 0 | kCanAutomate | "0%" - "100%" |

Reserved for future: IDs 3293-3299 (within existing kArpEndId = 3299 sentinel).

## Parameter Handling

### handleArpParamChange()

| Param ID | Normalized Input | Denormalized Output | Storage |
|---|---|---|---|
| kArpSpiceId (3290) | [0, 1] | `clamp(float(value), 0, 1)` | `params.spice` (atomic float) |
| kArpDiceTriggerId (3291) | 0.0 or 1.0 | `value >= 0.5` -> set true | `params.diceTrigger` (atomic bool) |
| kArpHumanizeId (3292) | [0, 1] | `clamp(float(value), 0, 1)` | `params.humanize` (atomic float) |

### applyParamsToEngine()

```cpp
// Spice: direct float transfer
arpCore_.setSpice(arpParams_.spice.load(std::memory_order_relaxed));

// Dice: atomic compare-exchange for exactly-once consumption
bool expected = true;
if (arpParams_.diceTrigger.compare_exchange_strong(expected, false, std::memory_order_relaxed)) {
    arpCore_.triggerDice();
}

// Humanize: direct float transfer
arpCore_.setHumanize(arpParams_.humanize.load(std::memory_order_relaxed));
```

### formatArpParam()

| Param ID | Value 0.0 | Value 0.5 | Value 1.0 |
|---|---|---|---|
| kArpSpiceId | "0%" | "50%" | "100%" |
| kArpDiceTriggerId | "--" | "Roll" | "Roll" |
| kArpHumanizeId | "0%" | "50%" | "100%" |

## Serialization

### Save Order (appended after Phase 8 fillToggle)

```
[Phase 8 data]
  fillToggle: int32 (0 or 1)
[Phase 9 data - NEW]
  spice: float (0.0 - 1.0)
  humanize: float (0.0 - 1.0)
```

- diceTrigger: NOT serialized (momentary action)
- overlay arrays: NOT serialized (ephemeral)

### Load - Backward Compatibility

| Scenario | EOF at | Behavior |
|---|---|---|
| Phase 8 preset | First Spice float read | `return true` -- Spice=0, Humanize=0 (defaults) |
| Phase 9 preset, corrupt | Humanize float read (after Spice success) | `return false` -- stream corrupt |
| Phase 9 preset, valid | After Humanize read | `return true` -- both values loaded |

### loadArpParamsToController()

| Field | Conversion to Normalized | Param ID |
|---|---|---|
| spice (float) | Direct: `clamp(floatVal, 0, 1)` | kArpSpiceId |
| humanize (float) | Direct: `clamp(floatVal, 0, 1)` | kArpHumanizeId |

## Controller Registration

```cpp
// Spice: Continuous 0-1, no unit suffix, default 0.0
parameters.addParameter(STR16("Arp Spice"), STR16("%"), 0, 0.0,
    ParameterInfo::kCanAutomate, kArpSpiceId);

// Dice: Discrete 2-step, no unit suffix, default 0
parameters.addParameter(STR16("Arp Dice"), STR16(""), 1, 0.0,
    ParameterInfo::kCanAutomate, kArpDiceTriggerId);

// Humanize: Continuous 0-1, no unit suffix, default 0.0
parameters.addParameter(STR16("Arp Humanize"), STR16("%"), 0, 0.0,
    ParameterInfo::kCanAutomate, kArpHumanizeId);
```
