# D7 — Accessibility and scaling

**Status: RATIFIED by the user, 2026-10-23 (spec Clarifications "Session 2026-10-23 (12a ratification)").**

Deliverable D7 of Phase 12 (FR-010, FR-014, FR-076; plan §3.1 "D7 values", §5.6). Every value below is a named literal
that `Profundum_UiLayout` (SC-019) and `Profundum_UiZoom` (SC-032) gate. Until the user ratifies this document in the spec's
Clarifications section, every value is a proposal.

## 1. Designed size and resize policy (FR-010, FR-014)

| Literal | Value | Gated by |
|---|---|---|
| `kDesignedWidth` | 960 px (at 100 %) | SC-019 |
| `kDesignedHeight` | 600 px (at 100 %) | SC-019 |
| `kZoomSteps` | 1.0 / 1.25 / 1.5 / 2.0 (100 / 125 / 150 / 200 %) | SC-019, SC-032 |
| Free resize | **none**: the template sets `minSize = maxSize = 960, 600` | SC-032 |

- At 200 % the window is 1920 × 1200 px.
- The editor constrains its size with `setEditorSizeConstrains({960,600},{960,600})`, scaled by the zoom. Real signature:
  `bool setEditorSizeConstrains (const CPoint& newMinimumSize, const CPoint& newMaximumSize);`
  (`extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.h:123`).
- The zoom is applied with `void setZoomFactor (double factor);` (`vst3editor.h:127`). The allowed set is the four steps
  above (`void setAllowedZoomFactors (std::vector<double> zoomFactors)`, `vst3editor.h:130`); the editor offers no size other
  than these four (SC-032).
- The zoom is chosen by the UI-only zoom segment, **session tag 9300** (plan §4.7, `plan.md:1234`). It is session state: not
  a registered parameter, not in the plugin state, bound to no parameter ID; `getState` bytes are identical before and after a
  zoom change, and the registered parameter count stays 84 (FR-014, SC-032).

Zoom control wireframe (four segment cells, each at least the segment-cell minimum of §2):

```
 +------+------+------+------+
 | 100% | 125% | 150% | 200% |   zoom segment -- UI-only (session tag 9300)
 +------+------+------+------+
```

It sits in the header (D1) and in the Advanced SETTINGS tab `tab-settings` (D2); both instances carry session tag 9300.

## 2. Minimum text and hit-target sizes (FR-010)

All sizes are at 100 % zoom. Zoom scales every rect and font by the same factor, so a control that meets the minimum at
100 % meets it, scaled, at every step; SC-019 nevertheless measures at every step.

| Literal | Value (at 100 %) | Applies to |
|---|---|---|
| `kMinFontPx` | 10 px | every `CTextLabel`, readout (`CParamDisplay`), menu (`COptionMenu`) and segment-cell font |
| `kMinHitTargetPx` | 20 × 20 px | every bound control's hit rect (knobs, sliders, buttons, menus, custom views that write a parameter) |
| `kMinSegmentCellPx` | 24 × 24 px | each cell of a segment control: voice mode 1, Motion 900, Anchor Morph Mode 1000, Edit Target 1001, compare UI-only (session tags 9200 / 9203), page strip UI-only (session tag 9100), tab strip UI-only (session tag 9110), zoom UI-only (session tag 9300) |

SC-019 checks, at every zoom step: every hit rect ≥ `kMinHitTargetPx × zoom` (segment cells ≥ `kMinSegmentCellPx × zoom`),
every font ≥ `kMinFontPx × zoom`, every bound rect inside the frame, and no overlap of bound controls on a visible page.
Readouts at the registered extremes (Morph Ramp 5 / 2000 ms, Drop Amount 48 st, portamento 0 / 10000 ms) fit their label
rects at every step (spec Edge Cases, `spec.md:907–908`).

## 3. Textual names (FR-010)

SC-019 accepts a non-empty tooltip or an adjacent label. D7 rules the stricter form: **every bound control carries a
non-empty tooltip**; a visible label is additional. The tooltip strings themselves are ruled in D1 (Main control table) and
D2 (Advanced sections).

## 4. Contrast (FR-010; plan §5.6)

| Literal | Value | Applies to |
|---|---|---|
| `kMinContrastText` | 4.5 : 1 | all text |
| `kMinContrastLargeBold` | 3.0 : 1 | bold text of 14 px or more (at 100 %) |

Method (WCAG 2.x relative luminance, plan §5.6):

- Linearise each sRGB channel C ∈ [0, 1]: C ≤ 0.04045 → C / 12.92; otherwise ((C + 0.055) / 1.055)^2.4.
- Relative luminance L = 0.2126·R + 0.7152·G + 0.0722·B over the linearised channels.
- Ratio = (L₁ + 0.05) / (L₂ + 0.05), L₁ the lighter colour.

The test reads the text colour, and the background colour of the nearest opaque ancestor, from the built view tree, and
computes the ratio from the uidesc colour table (SC-019).

## 5. Colour tokens

The four Phase 10 tokens keep their values (`plugins/profundum/resources/editor.uidesc:11–14`: `bg #101014`,
`text #D8D8E0`, `track #22222A`, `accent #7A8CA8`). Five are new (plan §4.12 names the nine tokens).

| Token | Proposed `#RRGGBB` | Relative luminance L | Role |
|---|---|---|---|
| `bg` | `#101014` | 0.0053 | editor background (Phase 10 value) |
| `panel` | `#1A1A20` | 0.0106 | section / group container background |
| `track` | `#22222A` | 0.0165 | unfilled knob arcs and slider tracks, segment-cell background (Phase 10 value) |
| `text` | `#D8D8E0` | 0.6909 | primary text: labels, readouts, section titles (Phase 10 value) |
| `text-dim` | `#9A9AA8` | 0.3281 | secondary text: captions, units, dimmed Anchor edits held at A (FR-044) |
| `accent` | `#7A8CA8` | 0.2572 | knob value arc, selected segment-cell fill, accent text (Phase 10 value) |
| `highlight` | `#E0B060` | 0.4774 | lit compare indicator, highlight state |
| `anchor` | `#D08A5C` | 0.3236 | ANCHOR lane of the spectrum, Anchor group labels |
| `mass` | `#7FB0D8` | 0.4052 | spectrum bars (Sub / Body / Presence shading derives from it), THE MASS group labels |

## 6. Computed contrast of every text-on-background pair the pages use

Computed with the §4 method from the §5 values (no rounding before the final ratio; ratios shown to two decimals).

| Text colour | Background | Ratio | Bound | Where used |
|---|---|---|---|---|
| `text` | `bg` | 13.40 : 1 | 4.5 | header title, page strip labels |
| `text` | `panel` | 12.22 : 1 | 4.5 | control labels and readouts in every section |
| `text` | `track` | 11.14 : 1 | 4.5 | unselected segment-cell labels |
| `text-dim` | `bg` | 6.84 : 1 | 4.5 | header captions |
| `text-dim` | `panel` | 6.24 : 1 | 4.5 | units, endpoint captions (session tags 9220 / 9221), dimmed held-at-A values |
| `text-dim` | `track` | 5.68 : 1 | 4.5 | disabled segment-cell labels |
| `accent` | `bg` | 5.55 : 1 | 4.5 | header accent text |
| `accent` | `panel` | 5.07 : 1 | 4.5 | section titles |
| `accent` | `track` | 4.62 : 1 | 4.5 | accent text on segment cells |
| `highlight` | `panel` | 8.70 : 1 | 4.5 | compare-indicator text |
| `highlight` | `track` | 7.93 : 1 | 4.5 | lit compare-cell label |
| `anchor` | `panel` | 6.16 : 1 | 4.5 | ANCHOR lane label, Anchor group title |
| `anchor` | `track` | 5.62 : 1 | 4.5 | Anchor segment-cell label |
| `mass` | `panel` | 7.51 : 1 | 4.5 | THE MASS group title |
| `mass` | `track` | 6.84 : 1 | 4.5 | mass-coloured segment-cell label |
| `bg` | `accent` | 5.55 : 1 | 4.5 | label of the selected segment cell (dark text on accent fill) |
| `bg` | `highlight` | 9.54 : 1 | 4.5 | label of the lit compare cell (dark text on highlight fill) |

Every pair meets the 4.5 : 1 bound for normal text, so no pair proposed here relies on the 3 : 1 bold-text allowance. A pair
not in this table may not carry text until its row (with a ratio at or above its bound) is added to D7.

## 7. No run loop → empty spectrum (FR-076)

The spectrum frames cross from processor to controller by DataExchange, the Vorago / Membrum mechanism. In a host without the
DataExchange API the SDK fallback queue opens only when `Timer::create` succeeds: `openQueue` returns false when the timer is
null (`extern/vst3sdk/public.sdk/source/vst/utility/dataexchange.cpp:77–85`), and `Timer::create` returns nullptr when no
create-timer function is injected (`extern/vst3sdk/base/source/timer.cpp:347–352`). On Linux the SDK factory injects one
whenever the host passes `Linux::IRunLoop` through `setHostContext`
(`extern/vst3sdk/public.sdk/source/main/pluginfactory.cpp:316–319`), so real Linux hosts open the queue. The controller decodes
the fallback IMessage as Vorago does (`plugins/vorago/src/controller/controller.cpp:330–337`).

**Documented note (shown in the Advanced SETTINGS tab, D2):** "No host run loop: the spectrum view stays empty." In a host
that provides neither the DataExchange API nor a run loop (for example a bare test harness), the queue stays closed and both
`HarmonicBarView` instances draw empty. Nothing works around this. The morph component, every parameter and the
controller-computed SPREAD curve (FR-080) still work.

## 8. Values awaiting ratification

| Item | Proposed value |
|---|---|
| Designed size | 960 × 600 px, no free resize (`minSize = maxSize`) |
| Zoom steps | 1.0 / 1.25 / 1.5 / 2.0 |
| Minimum font | 10 px at 100 % |
| Minimum hit target | 20 × 20 px; segment cells 24 × 24 px |
| Contrast | ≥ 4.5 : 1; ≥ 3 : 1 for bold text ≥ 14 px |
| Tooltips | every bound control |
| Colour tokens | the nine values of §5 |
| Empty-spectrum note | the §7 string |
