# Research: Global Filter Strip & Trance Gate Tempo Sync

**Date**: 2026-02-15
**Spec**: [spec.md](spec.md)

## R-001: Trance Gate NoteValue Parameter Identity

**Question**: Does the Trance Gate have a separate "tempo-synced rate" parameter distinct from the existing `kTranceGateNoteValueId` (607)?

**Finding**: No. The DSP `TranceGateParams` struct has exactly two timing-related fields:
- `rateHz` (float) -- free-running step rate, mapped to `kTranceGateRateId` (602)
- `noteValue` (NoteValue enum) + `noteModifier` (NoteModifier enum) -- tempo-synced step rate, both derived from `kTranceGateNoteValueId` (607)

The comment "step length" in `plugin_ids.h:179` describes what the parameter does: it sets how long each gate step lasts when in tempo-synced mode. "Step length" and "tempo-synced rate" are the same thing from different perspectives.

**Decision**: Reuse `kTranceGateNoteValueId` (607) for both the toolbar NoteValue dropdown AND the bottom-row NoteValue dropdown. No new parameter ID is needed.

**Rationale**: Creating a new parameter ID would require new DSP wiring, state persistence, and controller handling -- work that contradicts the spec's "purely UI-layer work" scope. The existing parameter 607 already does exactly what is needed.

**Alternatives considered**:
- Create new parameter ID (612+): Rejected -- requires DSP changes, state format changes, and breaks the spec's "no new parameter IDs" constraint.
- Use the toolbar dropdown as-is and skip bottom-row: Rejected -- spec explicitly requires Rate/NoteValue visibility switching in the bottom knob row.

**Impact on spec FR-009**: The spec says "a new control-tag for the tempo-synced rate Note Value dropdown (e.g., TranceGateSyncNoteValue) mapping to the already-registered tempo-synced rate parameter (distinct from the existing tag 607)". Since there IS no separate parameter, the implementation will add a `TranceGateSync` control-tag for parameter 606 and reuse the existing `TranceGateNoteValue` control-tag (607) for the bottom-row dropdown. Two COptionMenu controls bound to the same control-tag is valid in VSTGUI.

---

## R-002: Global Filter Accent Color Selection

**Question**: What color should the "global-filter" accent use?

**Finding**: The existing color palette uses warm/cool section accents:
- `osc-a`: `#64B4FF` (blue)
- `osc-b`: `#FF8C64` (orange)
- `filter`: `#4ECDC4` (teal)
- `distortion`: `#E8644C` (red)
- `trance-gate`: `#DCA83C` (gold)
- `modulation`: `#5AC882` (green)
- `effects`: `#6E7078` (gray)
- `master`: `#C8C8CC` (silver)

The Global Filter strip sits between Timbre/Dynamics (contains filter teal `#4ECDC4` and distortion red `#E8644C`) and Trance Gate (gold `#DCA83C`). It needs to be visually distinct from adjacent sections.

**Decision**: Use a warm pink/rose color `#C8649C` for the global-filter accent. This sits in an unused hue range (pink/magenta), clearly distinguishes from the teal per-voice filter above, the gold trance gate below, and the cool modulation green further down.

**Rationale**: Pink/rose occupies a hue gap in the palette. It reads as "filter" without conflicting with the per-voice filter teal. The warm tone echoes the distortion row above while being clearly distinct.

**Alternatives considered**:
- Purple (`#9C64C8`): Could work but might conflict visually with mod-env purple `#A05AC8`.
- Lighter gold: Too similar to trance-gate gold.
- Cyan variant: Too similar to per-voice filter teal.

---

## R-003: VSTGUI COptionMenu with Same Control-Tag

**Question**: Can two COptionMenu instances bind to the same control-tag in VSTGUI?

**Finding**: Yes. VSTGUI's VST3Editor binds each CControl to the parameter identified by its control-tag. Multiple controls can share the same tag -- they all reflect the same parameter value. When the user changes one, the host notifies all controls bound to that parameter. This is the standard approach for showing the same parameter in multiple places.

**Decision**: The toolbar NoteValue dropdown (existing, at origin 56,14) and the bottom-row NoteValue dropdown (new, in TranceGateNoteValueGroup) will both use `control-tag="TranceGateNoteValue"` (tag 607).

---

## R-004: Row Shifting Strategy for +36px Height Increase

**Question**: What is the most reliable approach for shifting rows 3-5 down by 36px?

**Finding**: The uidesc uses absolute positioning throughout. There are two layers of containers that need updating:

1. **Row containers** (transparent placeholders at lines 1774-1796):
   - Row 3 (Trance Gate): `origin="0, 334"` becomes `"0, 370"`
   - Row 4 (Modulation): `origin="0, 496"` becomes `"0, 532"`
   - Row 5 (Effects): `origin="0, 658"` becomes `"0, 694"`

2. **FieldsetContainers** (the actual visible sections):
   - Trance Gate: `origin="8, 334"` becomes `"8, 370"` (line 2119)
   - Modulation: `origin="8, 496"` becomes `"8, 532"` (line 2243)
   - Effects: `origin="8, 658"` becomes `"8, 694"` (line 2293)

3. **Editor template size**: `"900, 830"` becomes `"900, 866"` (3 attributes: minSize, maxSize, size)

Child controls within FieldsetContainers use relative coordinates, so they do NOT need updating. Only the FieldsetContainer origins and the row container origins need to change.

**Decision**: Update all 6 Y-coordinate values (3 row containers + 3 FieldsetContainers) and the 3 size attributes (minSize, maxSize, size). Total: 9 line edits.

---

## R-005: Trance Gate Toolbar Layout After Adding Sync Toggle

**Question**: What is the current toolbar layout and how should the Sync toggle fit?

**Finding**: Current Trance Gate toolbar row (y=14 within FieldsetContainer):
- `[On/Off toggle]` at origin (8, 14), size (40, 18)
- `[NoteValue dropdown]` at origin (56, 14), size (70, 18)
- `[NumSteps dropdown]` at origin (130, 14), size (56, 18)

The spec says the Sync toggle goes immediately after On/Off (leftmost in toolbar). This means:
- `[On/Off]` stays at (8, 14)
- `[Sync]` is inserted at (56, 14), size ~(50, 18) -- matches other Sync toggles (Delay Sync uses 50x20)
- `[NoteValue dropdown]` shifts right to (110, 14)
- `[NumSteps dropdown]` shifts right to (184, 14)

Since the Sync toggle defaults to ON (1.0), the toolbar NoteValue dropdown controls the step length and should remain visible regardless of sync state (it always applies in the toolbar context).

**Decision**: Insert Sync toggle at (56, 14) with size (50, 18), shift NoteValue to (110, 14), shift NumSteps to (184, 14). Use the `trance-gate` accent color for consistency.

---

## R-006: Global Filter Strip Layout Within 36px Height

**Question**: How to fit Enable toggle, Type dropdown, Cutoff knob + label, Resonance knob + label in a 36px-high strip?

**Finding**: The FieldsetContainer has an internal title area. With `fieldset-font-size="10"` and typical FieldsetContainer padding, the usable area starts at approximately y=10 inside the container. With 36px total height, the usable vertical space is about 26px.

The ArcKnob minimum usable size is 24x24 or 28x28. At 28x28, knobs would exceed the strip height. At 24x24, they fit with labels beside them (to the right, as specified).

Layout plan (all y-coordinates relative to FieldsetContainer):
- On/Off toggle: origin (8, 8), size (40, 18)
- Type dropdown: origin (56, 8), size (90, 18)
- Cutoff knob: origin (200, 4), size (24, 24)
- "Cutoff" label: origin (228, 10), size (40, 12), text-alignment="left"
- Resonance knob: origin (320, 4), size (24, 24)
- "Reso" label: origin (348, 10), size (36, 12), text-alignment="left"

Using 24x24 knobs instead of the standard 28x28 keeps everything within the 36px strip. Labels beside knobs (to the right) uses horizontal space efficiently as specified.

**Decision**: Use 24x24 knobs with labels positioned immediately to the right. All controls centered vertically within the FieldsetContainer's usable area.

---

## R-007: Trance Gate Rate/NoteValue Visibility Group Pattern

**Question**: What exact pattern should the TranceGate Rate/NoteValue groups follow?

**Finding**: All 5 existing implementations (LFO1, LFO2, Chaos, Delay, Phaser) follow this exact pattern:

**uidesc** (two overlapping CViewContainers at the same position):
```xml
<!-- Rate (hidden when sync active) -->
<view class="CViewContainer" origin="X, Y" size="W, H"
      custom-view-name="[Prefix]RateGroup" transparent="true">
    <view class="ArcKnob" ... control-tag="[Prefix]Rate" .../>
    <view class="CTextLabel" ... title="Rate"/>
</view>
<!-- Note Value (visible when sync active) -->
<view class="CViewContainer" origin="X, Y" size="W, H"
      custom-view-name="[Prefix]NoteValueGroup" transparent="true"
      visible="false">
    <view class="COptionMenu" ... control-tag="[Prefix]NoteValue" .../>
    <view class="CTextLabel" ... title="Note"/>
</view>
```

**controller.h** (pointer declarations):
```cpp
VSTGUI::CView* [prefix]RateGroup_ = nullptr;
VSTGUI::CView* [prefix]NoteValueGroup_ = nullptr;
```

**controller.cpp setParamNormalized()** (visibility toggle):
```cpp
if (tag == k[Prefix]SyncId) {
    if ([prefix]RateGroup_) [prefix]RateGroup_->setVisible(value < 0.5);
    if ([prefix]NoteValueGroup_) [prefix]NoteValueGroup_->setVisible(value >= 0.5);
}
```

**controller.cpp verifyView()** (pointer capture + initial visibility sync):
```cpp
else if (*name == "[Prefix]RateGroup") {
    [prefix]RateGroup_ = container;
    auto* syncParam = getParameterObject(k[Prefix]SyncId);
    bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
    container->setVisible(!syncOn);
} else if (*name == "[Prefix]NoteValueGroup") {
    [prefix]NoteValueGroup_ = container;
    auto* syncParam = getParameterObject(k[Prefix]SyncId);
    bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
    container->setVisible(syncOn);
}
```

**controller.cpp willClose()** (pointer cleanup):
```cpp
[prefix]RateGroup_ = nullptr;
[prefix]NoteValueGroup_ = nullptr;
```

**Decision**: Follow this exact pattern for TranceGate with `custom-view-name="TranceGateRateGroup"` and `custom-view-name="TranceGateNoteValueGroup"`.

**Important**: Since the default sync state is ON (1.0), the Rate group starts hidden (`visible="false"` in uidesc, or set by verifyView) and the NoteValue group starts visible.
