# Research: Expand Master Section into Voice & Output Panel

**Feature**: 052-expand-master-section
**Date**: 2026-02-14

---

## Research Questions

### R-001: How to draw a vector gear/cog icon using CGraphicsPath

**Decision**: Draw the gear as a circle with evenly-spaced rectangular teeth around the perimeter, using `beginSubpath`, `addLine`, and `closeSubpath`. No bitmaps. The gear shape consists of:
- An inner circle (the body) rendered as the negative space between teeth
- N teeth (typically 6-8 for an 18x18 icon) drawn as trapezoids radiating outward
- A small center circle hole (optional, for visual clarity)

**Rationale**: CGraphicsPath supports `beginSubpath`, `addLine`, `addArc`, `addEllipse`, and `closeSubpath`. A gear icon at 18x18px needs to be simple enough to render crisply at small sizes. 6 teeth provides good visual recognition at this scale. The approach is identical to how `drawPowerIcon` and `drawChevronIcon` are already implemented in ToggleButton -- pure path operations, no bitmap dependencies, fully cross-platform.

**Algorithm**:
```
For each tooth i in [0, numTeeth):
  angle = i * (2*PI / numTeeth)
  Draw outer rect (tooth) from outerRadius to innerRadius
  Draw inner arc between teeth
```

A simpler approach: generate the gear outline as a polygon where vertices alternate between inner and outer radii, with slight angular offsets to create the tooth shape:
1. For each tooth: 4 vertices (inner-leading, outer-leading, outer-trailing, inner-trailing)
2. Connect all vertices into a single closed subpath
3. Optionally subtract a center circle via `addEllipse` (winding rule)

**Alternatives considered**:
- Bitmap icon: Rejected. Constitution Principle VI requires cross-platform solutions; vector drawing is already the established pattern in ToggleButton (power icon, chevron icon).
- SVG rendering: Rejected. VSTGUI has limited SVG support and it would add complexity for a single icon.
- Unicode glyph (U+2699): Rejected. Font rendering varies across platforms and sizes; not reliable at 18x18.

---

### R-002: ToggleButton extension points for new icon style

**Decision**: Add `kGear` to the `IconStyle` enum and a `drawGearIcon()` private method. The draw dispatch in `draw()` already follows a pattern: check title, then check icon style. Adding `kGear` requires:

1. `IconStyle` enum: Add `kGear` after `kChevron`
2. `iconStyleFromString()`: Add `"gear"` mapping
3. `iconStyleToString()`: Add `kGear -> "gear"` case
4. `draw()` method: Add `else if (iconStyle_ == IconStyle::kGear)` branch
5. `drawIconAndTitle()`: Add gear branch for combined icon+text mode
6. `getPossibleListValues()` for `"icon-style"`: Add `"gear"` string
7. Copy constructor: Already copies `iconStyle_` member -- no change needed.

**Rationale**: This follows the exact pattern used when `kChevron` was added to `kPower`. The ViewCreator system uses string-to-enum conversion and a list of possible values; both need updating. No new members are added to the class (the gear icon uses the same `iconSize_`, `strokeWidth_`, and color members).

**Alternatives considered**: None -- the spec explicitly requires extending ToggleButton with a new icon style.

---

### R-003: VSTGUI tooltip attribute on COptionMenu

**Decision**: The `tooltip` attribute is supported on all CView subclasses via `CView::setTooltipText()`. In uidesc XML, it is set as `tooltip="Polyphony"` directly on the `<view class="COptionMenu" .../>` element.

**Rationale**: Verified by reading VSTGUI source: `CView` has `tooltip` as a built-in attribute handled by the base CViewCreator. COptionMenu inherits from CControl which inherits from CView, so tooltip is available without any custom code.

**Alternatives considered**: None -- this is the standard VSTGUI mechanism.

---

### R-004: ArcKnob without control-tag behavior

**Decision**: An `ArcKnob` in uidesc with no `control-tag` attribute will render normally but not be bound to any parameter. User interaction (mouse drag) will change the internal value visually (the arc moves) but no parameter change is sent to the processor. This is the desired behavior for placeholder knobs.

**Rationale**: VSTGUI's parameter binding is established through the `control-tag` attribute. Without it, the control has tag=-1 (kNoTag) and `VST3Editor::valueChanged()` ignores controls without valid tags. The control still renders and responds to mouse input visually -- it just produces no side effects. This is confirmed by reading `vst3editor.cpp` which checks for valid tag before calling `performEdit()`.

**Alternatives considered**:
- Disabled controls (grayed out): Rejected. Spec explicitly says "visually indistinguishable from functional knobs."
- Custom non-interactive subclass: Rejected. Over-engineering; no-tag behavior is sufficient.

---

### R-005: Layout fitting validation within 120x160px

**Decision**: The proposed layout fits 6 visual elements in 120x160px with the following spacing analysis:

```
Panel: 120x160 (content area starts after fieldset title)
Content area: ~120 x ~148 (fieldset title takes ~12px at top)

Vertical stack (relative to content area top = y 0):
  Row 1: Polyphony dropdown (y=14, h=18) + Gear icon (y=14, h=18)
  Label:  "Poly" (y=32, h=10)
  Gap:    6px
  Row 2: Output knob (y=48, h=36), centered at x=42
  Label:  "Output" (y=84, h=10)
  Gap:    6px
  Row 3: Width knob (y=100, h=28) + Spread knob (y=100, h=28)
  Labels: "Width"/"Spread" (y=128, h=10)
  Gap:    2px
  Row 4: Soft Limit toggle (y=140, h=16)

Bottom edge: 140 + 16 = 156 <= 160 OK
```

All spacing constraints met:
- Polyphony-to-Gear horizontal: dropdown ends at x=68, gear at x=72 = 4px gap (minimum met)
- Width-to-Spread horizontal: Width at x=14+28=42, Spread at x=62 = 20px gap (exceeds minimum)
- All vertical gaps >= 4px

**Rationale**: Direct pixel arithmetic from the proposed layout in the spec. The tightest vertical spacing is between the Width/Spread labels and Soft Limit toggle (2px gap at y=138 to y=140), which is below the 4px minimum. The implementer can adjust: moving the toggle down to y=142 gives a 4px gap and still fits (142+16=158 <= 160).

**Alternatives considered**: Different vertical ordering (e.g., Polyphony below Output) was considered but rejected because the spec's ordering follows the signal flow concept (voice count -> output level -> stereo field -> limiting).

**Cross-reference**: The canonical layout and spacing constraints are defined in [spec.md Proposed Layout](spec.md#proposed-layout-reference). The detailed spacing and boundary verification tables are in [contracts/uidesc-voice-output-panel.md](contracts/uidesc-voice-output-panel.md).

---

### R-006: Impact on existing tests and preset compatibility

**Decision**: No existing tests or presets are affected because:
1. No parameter IDs change (kMasterGainId=0, kPolyphonyId=2, kSoftLimitId=3 remain identical)
2. No parameter registration code changes (global_params.h is untouched)
3. No state save/load format changes
4. The uidesc XML restructuring only affects visual layout, not parameter binding
5. The C++ change (ToggleButton kGear) adds a new enum value at the end -- no ABI break, no existing code affected

**Rationale**: The feature is purely additive in C++ (new enum value + new draw method) and restructuring in XML (repositioning existing controls + adding unbound new controls). This is the safest category of change for backward compatibility.

**Alternatives considered**: None -- this is a factual analysis, not a design choice.
