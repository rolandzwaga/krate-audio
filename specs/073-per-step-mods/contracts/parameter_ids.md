# API Contract: Parameter IDs for Per-Step Modifiers

**Date**: 2026-02-21
**File**: `plugins/ruinae/src/plugin_ids.h`

## New Parameter IDs

```cpp
// --- Modifier Lane (073-per-step-mods, 3140-3172) ---
kArpModifierLaneLengthId = 3140,    // discrete: 1-32 (RangeParameter, stepCount=31)
kArpModifierLaneStep0Id  = 3141,    // discrete: 0-255 (RangeParameter, stepCount=255)
kArpModifierLaneStep1Id  = 3142,
kArpModifierLaneStep2Id  = 3143,
kArpModifierLaneStep3Id  = 3144,
kArpModifierLaneStep4Id  = 3145,
kArpModifierLaneStep5Id  = 3146,
kArpModifierLaneStep6Id  = 3147,
kArpModifierLaneStep7Id  = 3148,
kArpModifierLaneStep8Id  = 3149,
kArpModifierLaneStep9Id  = 3150,
kArpModifierLaneStep10Id = 3151,
kArpModifierLaneStep11Id = 3152,
kArpModifierLaneStep12Id = 3153,
kArpModifierLaneStep13Id = 3154,
kArpModifierLaneStep14Id = 3155,
kArpModifierLaneStep15Id = 3156,
kArpModifierLaneStep16Id = 3157,
kArpModifierLaneStep17Id = 3158,
kArpModifierLaneStep18Id = 3159,
kArpModifierLaneStep19Id = 3160,
kArpModifierLaneStep20Id = 3161,
kArpModifierLaneStep21Id = 3162,
kArpModifierLaneStep22Id = 3163,
kArpModifierLaneStep23Id = 3164,
kArpModifierLaneStep24Id = 3165,
kArpModifierLaneStep25Id = 3166,
kArpModifierLaneStep26Id = 3167,
kArpModifierLaneStep27Id = 3168,
kArpModifierLaneStep28Id = 3169,
kArpModifierLaneStep29Id = 3170,
kArpModifierLaneStep30Id = 3171,
kArpModifierLaneStep31Id = 3172,
// 3173-3179: reserved

// --- Modifier Configuration (073-per-step-mods, 3180-3181) ---
kArpAccentVelocityId     = 3180,    // discrete: 0-127 (RangeParameter, stepCount=127)
kArpSlideTimeId          = 3181,    // continuous: 0-500ms (Parameter, default 60ms)
// 3182-3189: reserved
```

## Parameter Registration Details

| Parameter ID | Type | Range | Default | stepCount | Flags |
|---|---|---|---|---|---|
| kArpModifierLaneLengthId | RangeParameter | 1-32 | 1 | 31 | kCanAutomate |
| kArpModifierLaneStep0..31Id | RangeParameter | 0-255 | 1 (kStepActive) | 255 | kCanAutomate, kIsHidden |
| kArpAccentVelocityId | RangeParameter | 0-127 | 30 | 127 | kCanAutomate |
| kArpSlideTimeId | Parameter (continuous) | 0-1 (maps to 0-500ms) | 0.12 (maps to 60ms) | 0 | kCanAutomate |

## Normalized Value Mapping

| Parameter | Normalized -> Plain | Plain -> Normalized |
|---|---|---|
| Modifier Lane Length | `1 + round(norm * 31)` -> 1-32 | `(plain - 1) / 31` |
| Modifier Lane Steps | `round(norm * 255)` -> 0-255 | `plain / 255` |
| Accent Velocity | `round(norm * 127)` -> 0-127 | `plain / 127` |
| Slide Time | `norm * 500.0` -> 0-500ms | `plain / 500.0` |

## Total Parameter Count

- 1 modifier lane length
- 32 modifier lane steps
- 1 accent velocity
- 1 slide time
- **Total: 35 new parameters** (IDs 3140-3181)
- **Grand total arp parameters**: 11 (base) + 99 (Phase 4 lanes) + 35 (Phase 5) = 145
- All within kArpBaseId (3000) to kArpEndId (3199) range. No sentinel update needed.
