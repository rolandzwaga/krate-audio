# Bottom Bar Layout Contract

## Position

Below ArpLaneContainer, within the SEQ tab's arpeggiator section.
Height: ~80px. Full width of the arpeggiator area.

## Control Layout (left to right)

```
|  EUCLIDEAN SECTION (~200px)  |  GENERATIVE SECTION (~200px)  |  PERFORMANCE (~80px) |
|                              |                                |                      |
| [ON] Hits:[knob] Steps:[knob] Rot:[knob] [dot-display]  | Hmz:[knob] Spice:[knob] RatSwg:[knob] | [Dice] [Fill] |
```

## Euclidean Section

| Control | Type | Param ID | Size | Notes |
|---------|------|----------|------|-------|
| Enable toggle | ToggleButton | kArpEuclideanEnabledId (3230) | 20x20 | power icon |
| Hits knob | ArcKnob | kArpEuclideanHitsId (3231) | 32x32 | 0-32 discrete |
| Steps knob | ArcKnob | kArpEuclideanStepsId (3232) | 32x32 | 2-32 discrete |
| Rotation knob | ArcKnob | kArpEuclideanRotationId (3233) | 32x32 | 0-31 discrete |
| Dot display | EuclideanDotDisplay | (none, display-only) | 60x60 | Circular dots |

## Generative Section

| Control | Type | Param ID | Size | Notes |
|---------|------|----------|------|-------|
| Humanize knob | ArcKnob | kArpHumanizeId (3292) | 32x32 | 0-100% |
| Spice knob | ArcKnob | kArpSpiceId (3290) | 32x32 | 0-100% |
| Ratchet Swing | ArcKnob | kArpRatchetSwingId (3293) | 32x32 | 50-75% |

## Performance Section

| Control | Type | Param ID | Size | Notes |
|---------|------|----------|------|-------|
| Dice button | ActionButton | kArpDiceTriggerId (3291) | 24x24 | regen icon, momentary |
| Fill toggle | ToggleButton | kArpFillToggleId (3280) | 24x24 | power icon, latching |

## Color Scheme

- All knobs: neutral accent color #606068 (FR-041)
- Toggle button on-color: lane-agnostic neutral tone
- ActionButton color: neutral gray matching bottom bar theme
- Background: same as arp area background (#19191C)
- Labels: dimmed white (#A0A0A5), 9pt Arial

## Visibility Rules

- Euclidean section knobs + dot display: visible only when kArpEuclideanEnabledId = 1.0
- All other controls: always visible when arp section is visible

## uidesc Integration

All controls defined in editor.uidesc within the SEQ tab arpeggiator view.
The Euclidean section uses a CViewContainer with visibility toggled by the controller based on the kArpEuclideanEnabledId parameter value (same pattern as arpRateGroup_/arpNoteValueGroup_ toggle).
