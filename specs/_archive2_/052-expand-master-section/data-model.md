# Data Model: Expand Master Section into Voice & Output Panel

**Feature**: 052-expand-master-section
**Date**: 2026-02-14

---

## Entities

### 1. IconStyle Enum (Extended)

**Location**: `plugins/shared/src/ui/toggle_button.h`
**Namespace**: `Krate::Plugins`

| Value | String | Description |
|-------|--------|-------------|
| `kPower` | `"power"` | Existing. IEC 5009 power symbol. |
| `kChevron` | `"chevron"` | Existing. Directional chevron arrow. |
| `kGear` | `"gear"` | **NEW**. Gear/cog icon for settings. |

**Validation**: No additional validation needed beyond the existing string-to-enum conversion.

---

### 2. Voice & Output Panel (UIDESC Layout)

**Location**: `plugins/ruinae/resources/editor.uidesc`, lines ~2602-2651
**Container**: `FieldsetContainer` at origin `(772, 32)`, size `(120, 160)`

#### Control Inventory

| Control | Class | Size | Bound To | Status |
|---------|-------|------|----------|--------|
| Panel title | `FieldsetContainer` `fieldset-title` | -- | -- | RENAME: "MASTER" -> "Voice &amp; Output" |
| Polyphony dropdown | `COptionMenu` | 60x18 | `Polyphony` (ID 2) | REPOSITION + RESIZE (was 80px) |
| "Poly" label | `CTextLabel` | 60x10 | -- | RENAME: "Polyphony" -> "Poly" |
| Gear icon | `ToggleButton` | 18x18 | None (inert) | **NEW** |
| Output knob | `ArcKnob` | 36x36 | `MasterGain` (ID 0) | REPOSITION |
| "Output" label | `CTextLabel` | 52x12 | -- | REPOSITION (height 12px matches font metrics) |
| Width knob | `ArcKnob` | 28x28 | None (placeholder) | **NEW** |
| "Width" label | `CTextLabel` | 36x10 | -- | **NEW** |
| Spread knob | `ArcKnob` | 28x28 | None (placeholder) | **NEW** |
| "Spread" label | `CTextLabel` | 40x10 | -- | **NEW** |
| Soft Limit toggle | `ToggleButton` | 80x16 | `SoftLimit` (ID 3) | REPOSITION |

#### Layout Grid (within 120x160 panel)

```
 x:  0    8   14  20  42  58  62  68  72  90  100 112 120
 y:  +----|----|----|----|----|----|----|----|----|----|----|+
  0  |                   Panel Title                        |
 12  |                                                      |
 14  |    [== Poly dropdown ==]    [gear]                   |
 32  |    "Poly" (label)                                    |
 42  |                                                      |
 48  |              (======OUTPUT======)                    |
 84  |              "Output" (label)                        |
 94  |                                                      |
100  |   (==Width==)        (==Spread==)                   |
128  |   "Width"             "Spread"                      |
138  |                                                      |
142  |      [====Soft Limit====]                            |
158  |                                                      |
160  +------------------------------------------------------+
```

---

### 3. Existing Parameters (Unchanged)

These parameters are NOT modified by this spec. Listed here for reference.

| Parameter | ID | Type | Range | Default | Registration |
|-----------|----|------|-------|---------|-------------|
| Master Gain | 0 | Continuous | 0-200% | 50% (0 dB) | `global_params.h:77-79` |
| Voice Mode | 1 | Dropdown | Poly/Mono | Poly | `global_params.h:82-85` |
| Polyphony | 2 | Dropdown | 1-16 | 8 | `global_params.h:88-94` |
| Soft Limit | 3 | Toggle | On/Off | On | `global_params.h:97-99` |

---

## State Transitions

None. This feature does not introduce new parameters or state. The gear icon is inert (no state). The Width/Spread knobs are unbound (no state persisted).

---

## Relationships

```
FieldsetContainer "Voice & Output"
  |
  +-- COptionMenu (Polyphony, ID 2) -- existing, repositioned
  +-- CTextLabel ("Poly") -- existing, renamed
  +-- ToggleButton (gear, no tag) -- NEW, inert
  +-- ArcKnob (Output, ID 0) -- existing, repositioned
  +-- CTextLabel ("Output") -- existing, repositioned
  +-- ArcKnob (Width, no tag) -- NEW, placeholder
  +-- CTextLabel ("Width") -- NEW
  +-- ArcKnob (Spread, no tag) -- NEW, placeholder
  +-- CTextLabel ("Spread") -- NEW
  +-- ToggleButton (Soft Limit, ID 3) -- existing, repositioned
```
