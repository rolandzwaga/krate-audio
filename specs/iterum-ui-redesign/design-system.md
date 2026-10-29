# Iterum interface language

One layout, ten delay modes. This document is the authority for the redesign: the ten panel specs
(`panels/*.md`), the global and per-panel XSLT stylesheets and the C++ changes all follow it. Where a panel
spec and this document disagree, this document wins.

Inputs: `audits/*.md` (Spectral and Tape were audited from `inventory-before.json` and the uidesc directly, their
audit files are absent), `shots/before/editor-*.png`, `inventory-before.json`, `plugins/iterum/resources/editor.uidesc`,
`plugins/iterum/src/plugin_ids.h`, `plugins/iterum/src/controller/*`, `plugins/shared/src/ui/*`.

Every number in this document is in VSTGUI points (the uidesc unit). Screenshots render at 2x.

Frozen (user ruling 2026-10-28): parameter IDs, types, ranges, defaults, saved state; every control-tag name and
number in the uidesc; every parameter that has a control today keeps one; exactly one `UpdateBanner` (last view of
the Editor template), one `CheckForUpdatesButton`, one `VersionLabel`; cross-platform VSTGUI only.

---

## 1. Principles

1. **The three things a delay player reaches for first are in the same place in every mode.** Time, Feedback and
   Mix sit in a core strip across the top of every panel, as three knobs in three fixed cells, with the Sync toggle
   beside the Time knob and, in the two modes that can hold their buffer (Granular, Spectral), the Freeze toggle
   beside the Mix knob. Switching modes never moves them. The two modes without a Feedback parameter (Freeze) or
   a time knob (MultiTap) keep the cell and put the mode's defining selector in it, labelled, so the cell is never
   blank and never mislabelled.
2. **One widget per kind of parameter, one size per widget.** A continuous value is a horizontal slider with its
   label above-left and its value above-right. A bipolar value is a centred slider with a 1 pt centre tick. A 2-4 way
   choice is a segment
   button. A longer choice is a menu. An on/off is a power toggle with its name beside it. The only knobs are the
   four always-present controls (Time, Feedback, Mix, Gain). Nothing is a plain CCheckBox or CKnob any more.
3. **Sections read in one order.** Body sections follow one canonical order (primary, secondary, Modulation,
   Filter, Stereo) and fill a three-column grid left to right, top to bottom. A mode without a Modulation section
   simply has one fewer section; what it does have is where the other modes have it.
4. **Full words, real units.** Labels are the canonical names from `plugins/iterum/CLAUDE.md` (Mix, Mod Depth, Mod
   Rate, Width, Age, Era, Filter Cutoff ...), never abbreviations, and every readout shows the parameter's plain
   value with its unit in the one format of section 6.4. Cells are sized for the longest label and the longest value,
   not the other way round.
5. **Conditional groups keep their shape.** A group that appears and disappears (Freeze pattern variants, MultiTap
   note value and custom editor, Digital Age) owns a fixed cell. Nothing else moves when it shows or hides.
6. **The panel fills its frame, the frame fills the window.** No dead band under the panel; sparse modes use tall
   sections so empty space sits inside bordered groups rather than as an unexplained blank.
7. **Dark, like every other Krate editor.** See section 5 for the reasoning.

---

## 2. Editor geometry

### 2.1 Window

| Item | Value |
|---|---|
| Editor template size | **940 x 608** |
| minSize / maxSize | 940 x 608 / 940 x 608 (fixed). The current 900-1200 x 600-900 range only stretched the chrome; nothing in the layout scales. HiDPI is handled by VSTGUI zoom, not by resizing. |
| autosize on the Editor template | none (remove `autosize="left right top bottom"`) |

### 2.2 Regions (editor-absolute, x y w h)

| Region | View | Rect | Notes |
|---|---|---|---|
| Header | CViewContainer, `background-color="header"` | 0 0 940 52 | |
| Title | CTextLabel "ITERUM", `title-font`, `accent`, left | 20 13 100 26 | 100 wide: room for the 20 pt bold text |
| Mode strip | IconSegmentButton, control-tag `Mode` | 130 6 790 40 | 10 segments of 79; right edge 920 = panel right edge |
| Mode panel frame | UIViewSwitchContainer, `background-color="panel"`, `animation-time="0"` | 20 64 900 480 | 12 pt below the header; all ten templates are 900 x 480 |
| Footer | CViewContainer, `background-color="header"` | 0 556 940 52 | 12 pt below the panel |
| Gain knob | ArcKnob, control-tag `Gain` | 20 10 32 32 | footer-relative; `popup-position="top"` |
| Gain label | CTextLabel "Gain", `label-font`, `text-dim`, left | 60 10 80 16 | footer-relative |
| Gain readout | CParamDisplay, control-tag `Gain`, `value-font`, `text`, left | 60 28 80 16 | footer-relative; reads `"%.1f dB"` once the Gain row of 9.3 lands (today the `kGainId` case prints `"%.1f"` with no unit, `controller.cpp:410`) |
| Version | CView `custom-view-name="VersionLabel"` | 430 18 110 16 | footer-relative; the view right-aligns its text, so it ends at 540 |
| Check for updates | `custom-view-name="CheckForUpdatesButton"` | 560 14 120 24 | footer-relative |
| Save preset | `custom-view-name="SavePresetButton"` | 690 14 110 24 | footer-relative |
| Presets | `custom-view-name="PresetBrowserButton"` | 810 14 110 24 | footer-relative; right edge 920 |
| Update banner | `custom-view-name="UpdateBanner"`, **last child of Editor** | 0 0 940 52 | exactly the header: it never touches the panel. While shown it covers the mode strip; that is the shared banner's behaviour in every Krate plugin and is dismissed with its X. Changing it would be a shared-view change, out of scope. This is an **intentional overlay**: it is a plain CView over the header container, so the inventory's overlap rule exempts the view whose `custom-view-name` is `UpdateBanner` (section 8.3, 9.5). |

Right margins are now one number: header strip, panel frame and footer buttons all end at x = 920 (20 pt margin).

### 2.3 Panel grid (template-relative, every mode template is 900 x 480)

```
y   0 ┌──────────────────────────────────────────────────────────────┐
     12│ core strip: [TIME 284] 12 [FEEDBACK 284] 12 [MIX 284]   h 72  │
     96│ body row 1:  [col 1 284] 12 [col 2 284] 12 [col 3 284]  h 180 │
    288│ body row 2:  [col 1 284] 12 [col 2 284] 12 [col 3 284]  h 180 │
    480└──────────────────────────────────────────────────────────────┘
      x=12          x=308         x=604         right edge 888
```

| Grid item | Value |
|---|---|
| Panel margin | 12 on all four sides |
| Columns | 3, each 284 wide, at x = 12, 308, 604 |
| Gutter | 12 |
| Spans | 2 columns = 580 (x = 12 or 308), 3 columns = 876 (x = 12) |
| Core strip | y = 12, height 72; three cells of 284 x 72 at x = 12, 308, 604 |
| Body rows | row 1 at y = 96, row 2 at y = 288, each 180 high |
| Tall section | spans both rows: y = 96, height 372 |
| Section inner padding | left/right 12, first control row at y = 24 (below the fieldset title), bottom pad at least 8 |
| Control rows inside a section | y = 24, 76, 128 (standard) and 180, 232, 284 (tall only); row pitch 52; cell height 44 |
| Cell widths inside a section | inner = section width - 24. Full = inner. Half = (inner - 12) / 2. 284 -> full 260, half 124. 580 -> full 556, half 272. 876 -> full 852, half 420, third 276 (x = 12, 300, 588), quarter 204 (x = 12, 228, 444, 660). |

A standard section (180 high) holds three control rows; a tall one holds six. A control row holds one full cell or
two half cells. Nothing narrower than a half cell exists for a slider: a label plus a readout does not fit.

### 2.4 Section placement rule

Sections are listed in canonical order (section 3) and placed into body cells left to right, row 1 then row 2.
A mode with **three or fewer** body sections uses tall sections (372 high), and its primary section may span two
columns (section 7 says which). A mode with four or five sections uses standard sections; trailing empty cells stay
plain panel background. The exact cell of every section in every mode is fixed in section 7; implementers do not
re-derive it.

Two things override the count-derived placement, because a player finds them by position, not by counting sections:

- **STEREO is pinned to the standard cell (604,288,284,180), column 3 bottom-most, in every mode that has it**
  (Granular, Spectral, Digital, PingPong; ruling 5). It is never placed by the rule above and is never tall: Width is at
  panel y 334 in all four modes. If a tall mode has STEREO and another section in column 3 (PingPong), column 3 is
  split into two standard sections, the other at (604,96,284,180) and STEREO at (604,288,284,180). Cells left empty by
  the pin (Granular (308,288), Spectral (604,96), Digital (12,288) and (308,288)) stay plain panel background.
- **MODULATION is always the standard section (604,96,284,180)** (BBD, Digital, PingPong), so Mod Depth and Mod Rate
  sit at the same panel coordinates in the three modes; BBD's (604,288) below it stays plain panel background (BBD has
  no width parameter).
- **Freeze is mirrored.** Its primary (the four stacked pattern variants) sits under the Pattern selector at columns
  2-3 (308,96,580,372), and column 1 holds SLICE under the Slice Length knob and ENVELOPE below it. This is the one
  documented exception to "primary is always row 1 column 1" (section 7.10).

---

## 3. Section taxonomy and order

### 3.1 The core strip (every mode)

| Cell | x (panel) | Holds | Label | Variant |
|---|---|---|---|---|
| Core 1 | 12 | the mode's time base | "Time" for a delay time; the parameter's own name when it is not a delay time ("Chunk Size", "Slice Length", "Timing Pattern") | knob cell (+ Sync toggle when the mode has a TimeMode parameter); MultiTap: select cell |
| Core 2 | 308 | Feedback | "Feedback" | knob cell; Freeze (no feedback parameter): select cell "Pattern" |
| Core 3 | 604 | Mix | "Mix" | knob cell, every mode; Granular and Spectral add the Freeze toggle in the cell's toggle slot (4.1), the same slot Sync has in core 1 |

Because every mode has its own parameter IDs, the core strip is ten per-mode cells that share one geometry, never
one control. The Time knob cell is the container the `<Mode>TimeSyncController` sub-controller is attached to (section 8).

### 3.2 Body sections, canonical order

| # | Section | Title in the fieldset | Present in | What belongs there |
|---|---|---|---|---|
| 1 | Primary | the mode's own word (GRAIN, SPREAD, PITCH SHIFT, MOTOR, CHIP, CHARACTER, PING PONG, PLAYBACK, PATTERN, EUCLIDEAN / GRANULAR SCATTER / HARMONIC DRONES / NOISE BURSTS) | every mode | the parameters that define this mode's sound; always row 1 column 1, except Freeze, whose variants sit at columns 2-3 under the Pattern selector (section 2.4, 7.10) |
| 2 | Secondary | mode word (PITCH, SPRAY, CHARACTER, WAVEFOLD, SPLICE, FEEDBACK FILTER, NOTE VALUE, ENVELOPE, SLICE, HEADS, CUSTOM PATTERN) | as needed | further mode-specific groups, in the order section 7 gives |
| 3 | MODULATION | MODULATION | BBD, Digital, PingPong | Mod Depth, Mod Rate, Mod Waveform; always the standard section (604,96,284,180) (2.4) |
| 4 | FILTER | FILTER | Shimmer, Reverse | enable toggle, type, cutoff (MultiTap's two cutoffs are its FEEDBACK FILTER secondary section; Freeze's noise filter lives inside the NOISE BURSTS variant) |
| 5 | STEREO | STEREO | Granular, Spectral, Digital, PingPong | Width, and only Width. Pinned to the standard cell (604,288,284,180) in all four modes (section 2.4, ruling 5). (PingPong's L/R Ratio and Cross-Feedback stay in its primary PING PONG section; its Width moves to STEREO.) |

A mode's remaining cells after these are empty panel background. Stereo is last because width is a finishing
control; Modulation precedes Filter because more modes have it.

### 3.3 What a musician finds where

| Looking for | Where, in every mode that has it |
|---|---|
| delay time, sync, note value | core cell 1 (knob, readout, Sync toggle) |
| feedback | core cell 2 |
| dry/wet | core cell 3 |
| output gain | footer, left |
| the mode's own character | body row 1, column 1 (and 2) |
| modulation | the last section of row 1 when the mode has it |
| width | STEREO, the standard cell (604,288) in Granular, Spectral, Digital and PingPong; the slider is at panel y 334 in all four |
| freeze (hold) | core cell 3, the toggle beside the Mix knob, in Granular and Spectral (the same slot Sync has beside Time) |
| presets, save, updates, version | footer, right |

---

## 4. Control vocabulary

Every control kind has one widget, one size and one attribute set. Cell coordinates below are relative to the cell
origin `(x, y)` given by the grid (section 2.3); `w` is the cell width (260 / 124 / 556 / 272 / 852 / 420 / 276 / 204).
Body cells are **not** wrapped in their own CViewContainer: a cell's views are direct children of the section
fieldset (section 8 explains why: the container visibility controllers find a fieldset through the *immediate
parent* of a tagged control).

### 4.1 Knob cell (core strip only): Time, Feedback, Mix; Gain in the footer

Container: `CViewContainer origin="X, 12" size="284, 72" transparent="true"` (the Time cell adds
`sub-controller="<Mode>TimeSyncController"` in modes with a TimeMode parameter).

| Part | View | Cell-relative rect | Attributes |
|---|---|---|---|
| Knob | `ArcKnob` | 8 8 56 56 | `control-tag`, `arc-color="accent"`, `guide-color="slider-frame"`, `arc-width="3"`; no `default-value` |
| Label | `CTextLabel` | 76 10 120 16 | `font="label-font"`, `font-color="text-dim"`, `text-alignment="left"`, `transparent="true"` |
| Readout | `CParamDisplay` | 76 30 120 24 | same `control-tag` as the knob, `font="readout-font"`, `font-color="text"`, `text-alignment="left"`, `back-color="~ TransparentCColor"`, `frame-color="~ TransparentCColor"`, `style-no-frame="true"` |
| Toggle slot: Sync (Time cell, modes with TimeMode); Freeze (Mix cell, Granular and Spectral) | `ToggleButton` | 200 32 76 22 | `control-tag="<Mode>TimeMode"` with `title="Sync"`, or `control-tag="<Mode>Freeze"` with `title="Freeze"`; `title-position="right"`, `icon-style="power"`, `icon-size="0.6"`, `font="label-font"`, `on-color="accent"`, `off-color="text-dim"`; no `default-value` |

The footer Gain knob is the same widget at 32 x 32 (`arc-width="2.5"`), with its label and readout to the right
(section 2.2). ArcKnob already shows a value popup while dragging; the readout is the resting display.

**No `default-value` attribute on any bound control, anywhere in the file.** `VST3Editor` sets every bound control's
default from the parameter's `defaultNormalizedValue` when it binds the control (`vst3editor.cpp:265-287`,
`c->setDefaultValue`), so a uidesc value is overwritten before the first click; today's mixed values (0.25 on a knob
whose parameter registers 0.098) were noise. Modifier-click reset therefore always lands on the registered default.

### 4.2 Select cell (core strip): MultiTap Timing Pattern, Freeze Pattern

Container as 4.1. Label `CTextLabel` at 8 10 200 16 (label-font, text-dim, left); `COptionMenu` at 8 30 200 24
with the menu attribute set (4.7).

### 4.3 Slider cell: every continuous, unipolar parameter

| Part | View | Cell-relative rect | Attributes |
|---|---|---|---|
| Label | `CTextLabel` | 0 0 (w-64) 16 | `font="label-font"`, `font-color="text-dim"`, `text-alignment="left"`, `transparent="true"` |
| Readout | `CParamDisplay` | (w-64) 0 64 16 | same `control-tag`, `font="value-font"`, `font-color="text"`, `text-alignment="right"`, `back-color="~ TransparentCColor"`, `frame-color="~ TransparentCColor"`, `style-no-frame="true"` |
| Slider | `CSlider` | 0 22 w 20 | `control-tag`, `orientation="horizontal"`, `draw-frame="true"`, `draw-back="true"`, `draw-value="true"`, **`draw-frame-color="slider-frame"`, `draw-back-color="slider-track"`, `draw-value-color="accent"`**, `frame-width="1"`, `transparent="true"` |

The three `draw-*-color` names are the ones `CSlider` reads (`vstgui/uidescription/detail/uiviewcreatorattributes.h`
lines 263-265). `back-color`, `value-color` and `frame-color` on a CSlider are the current defect and must not
survive anywhere in the file (the global stylesheet renames them tree-wide; the inventory metrics list the colors
per view, so a leftover is visible).

Readout widths: 64 fits "20.00 kHz", "+24.0 st", "2000 ms", "120%" in value-font (the 6.4 formats). A half cell (124) leaves the
label 60 pt, enough for 10 characters of label-font ("Mod Depth", "LP Cutoff", "Grain Size"); longer labels take a
full cell.

### 4.4 Bipolar cell: Pitch, Fine Pitch, Feedback Tilt, Wavefold Symmetry, Head Pan

Same label and readout as 4.3; the slider is the **same `CSlider` as 4.3** (same rect 0 22 w 20, same `draw-*`
attributes and colours) plus **`draw-value-from-center="true"`** (`kAttrDrawValueFromCenter`,
`sliderviewcreator.cpp:254`; `cslider.cpp:787` fills from the track's centre). Every bipolar parameter in Iterum is
linear with its zero at normalized 0.5 (Pitch +-24 st, Fine Pitch +-100 ct, Feedback Tilt +-1, Wavefold Symmetry
+-100 %, Head Pan L100-R100), so the fill starts at the zero and is empty at the zero.

**Centre tick (ruling 7).** Every bipolar cell adds one more view, declared immediately **after** its slider:

| Part | View | Cell-relative rect | Attributes |
|---|---|---|---|
| Centre tick | `CViewContainer` (no children) | (w/2) 22 1 20 | `background-color="text-dim"`, `transparent="false"`, `mouse-enabled="false"`; no `control-tag` |

Its x is the cell origin plus half the cell width (260 -> 130, 272 -> 136, 276 -> 138), so it stands on the
boundary where `draw-value-from-center` starts the fill. It is drawn over the track, not behind it, because `CSlider`
paints the whole track opaque when `draw-back="true"` (`cslider.cpp:757-770`), and it is mouse-disabled so that
`CViewContainer`'s mouse dispatch skips it (`cviewcontainer.cpp:1042-1043`) and the slider under it takes every click.
The inventory exempts exactly this pair from the overlap rule (9.5, third row). The bipolar cell keeps everything the
unipolar cell has: click-to-position, mouse-wheel stepping (`CSliderBase::onMouseWheelEvent`) and the modifier-click
reset to the parameter default.
`BipolarSlider` (`plugins/shared/src/ui/bipolar_slider.h`) is **not** used: it is a visibly different widget (4 pt
track, 10 pt dot, no frame, relative drag at a fixed 200 px per range, no wheel), which would break principle 2 when
it sits beside a Level CSlider in Tape HEADS, and using it would need plugins/shared changes.

### 4.5 Select cell (body): enum of 2-4 entries

Label `CTextLabel` at 0 0 w 16 (label-font, text-dim, left). Control `CSegmentButton` at 0 20 w 24:
`style="horizontal"`, `selection-mode="kSingle"`, `segment-names` = the parameter's own strings (section 6.3; the
uidesc list must match the StringListParameter so the file is not stale), `font="value-font"`,
`text-color="text"`, `text-color-highlighted="text-light"`, `frame-color="border"`, `frame-width="1"`,
`round-radius="3"`, `gradient="segment-bg"`, `gradient-highlighted="segment-bg-selected"`.
A segment takes the full cell of its row when it is alone on the row, whatever its entry count (260 in a 284-wide
section: Spectral Spread Curve, Freeze Slice Mode and Envelope Shape, Granular Grain Envelope; 556 in a two-column
section: BBD Era). It takes a half cell (272) only where it shares a row in a two-column section (Reverse Playback
Mode, Freeze Filter Type). Every segment is then at least 65 pt wide.

### 4.6 Menu cell (body): enum of 5 or more entries

Label as 4.5. Control `COptionMenu` at 0 20 w 24 with the menu attribute set (4.7). A menu takes the full cell (260)
in a 284-wide section (Granular Pitch Quantize, Digital Mod Waveform, MultiTap Spatial Pattern and Note Value) and a
half cell (272) only where it shares a row in a two-column section (PingPong L/R Ratio, the Freeze variant menus);
272 fits the longest entries ("Golden Ratio", "Exponential", "Dotted 16th", "Triplet 16th", "Perfect 5th"). The
strings are the live parameter's (section 6.3); the Freeze examples come from `freeze_params.h`
(`registerFreezeParams`), never from `pattern_freeze_params.h`, which is dead code (section 9.3).

### 4.7 Menu attribute set

`font="value-font"`, `font-color="text"`, `back-color="slider-track"`, `frame-color="border"`, `frame-width="1"`,
`style-round-rect="true"`, `round-rect-radius="3"`, `text-inset="6, 0"`, `menu-popup-style="true"`,
`mouse-enabled="true"`, `wants-focus="true"`. The generic VSTGUI menu is already enabled in `didOpen`.

### 4.8 Toggle cell: on/off parameters (Freeze, Filter, Splice, Head 1-3)

`ToggleButton` at 0 20 w 24: `control-tag`, `title="<label>"`, `title-position="right"`, `icon-style="power"`,
`icon-size="0.6"`, `font="label-font"`, `on-color="accent"`, `off-color="text-dim"`; no `default-value` (4.1). The
label row (0 0 w 16) is empty: the toggle's title is its label. Half cell. Replaces every `CCheckBox`. The Freeze
toggles of Granular and Spectral are not body toggle cells: they sit in the core-strip toggle slot (4.1).

### 4.9 Time with sync (free/synced pair)

The knob cell of 4.1 with the Sync toggle, inside the sub-controller container. The knob and the readout both carry
the free-time tag; `DelayTimeSyncController::verifyView` collects every control with that tag and the toggle with
the TimeMode tag, and swaps the knob and readout to the NoteValue tag when synced. The NoteValue parameter therefore
has no view of its own in any mode (as today); the readout shows "1/8" while synced and "500.0 ms" while free.
`verifyView` used to read the Sync state through `control->getFrame()`, which is null while the panel is being
built. Corrected 2026-10-28 by the build stage: in the real editor this did not show, because VST3Editor calls the
Sync toggle's `valueChanged()` when it binds it (vst3editor.cpp:350) and the toggle comes after the knob and readout,
so the sub-listener retags them. It does break any cell that declares the toggle first. The 9.3 row "Read the Sync
state at panel build" fixed it anyway (`editController()` resolves through the parent VST3Editor), with a direct unit
case that was red before; the layout does not depend on it. Tape and Freeze have no TimeMode parameter: their core cell 1
has the knob, label and readout only, and no sub-controller.

### 4.10 Custom views

| View | Size | Where |
|---|---|---|
| `TapPatternEditor` (tag 920 set by the controller) | 852 x 140 | inside CUSTOM PATTERN at fieldset (12,28); never below 135 high, the drag travel for tap levels is the one MultiTap gesture that needs fine resolution (today 820 x 135) |
| `ResetPatternButton` (tag 923) | 80 x 24 | CUSTOM PATTERN **title line**, right end: fieldset (784,0,80,24). The Snap label and menu sit on the same line (section 7.9). The fieldset border passes behind these three opaque views, legend-style; the title text at the left is untouched. |
| `CopyPatternButton` (tag 921) | 150 x 24 | inside its own stacked, transparent wrapper, in Reset's slot (section 8.2); Copy and Reset are never visible together |
| `SavePresetButton`, `PresetBrowserButton`, `CheckForUpdatesButton`, `VersionLabel`, `UpdateBanner` | section 2.2 | footer / Editor |

**Stacked wrapper rule.** A container stacked on a sibling at the same origin (the MultiTap Copy wrapper) carries
`transparent="true"` (a `CViewContainer` defaults to an opaque black background, `cviewcontainer.cpp:48`, and an
opaque container ends mouse dispatch to what lies under it, `cviewcontainer.cpp:1086`), and none of its children
intersects a child of the sibling that can be visible at the same time (Copy and Reset share a slot only because
one hides whenever the other shows, section 8.2). The inventory classifies same-origin containers as "stacked" and
cannot see this, so the panel spec gives the wrapper's full attribute set and the visual review checks a Custom and
a non-Custom pattern.

### 4.11 Section container

`FieldsetContainer origin="X, Y" size="W, H" fieldset-title="TITLE" fieldset-color="border"
fieldset-title-color="text-dim" fieldset-font-size="11" fieldset-radius="4" fieldset-line-width="1"
transparent="true"`. Titles are upper case, one or two words, no ampersands (the mode strip uses title case; the
fieldset caps are the only caps in the panel). No panel carries a title label any more ("DIGITAL DELAY" and its
nine siblings go): the highlighted mode segment is the title.

---

## 5. Visual style

### 5.1 Dark, and why

Iterum goes dark. Every other Krate editor is dark (Profundum and Vorago `bg #101014`, Gradus `#1a1a2e`), and the
shared views Iterum must carry are drawn for a dark ground with hard-coded colours: `VersionLabel` and
`CheckForUpdatesButton` paint grey-140 text, the `UpdateBanner` is a `#323237` card, `IconSegmentButton` and the
`OutlineButton` family default to the dark triple; today Iterum overrides the buttons with `kIterumLightButtonColors`
and the strip still renders inverted (chrome audit, issue 1). Going dark removes those overrides instead of adding
more, and the accent blue reads as a value fill on a dark track, which is what the slider defect accidentally
demonstrated. It also makes Iterum sit next to its siblings as one product line.

### 5.2 Colour tokens (`<colors>`; names referenced everywhere, no literal hex in any view attribute)

| Name | rgba | Used for |
|---|---|---|
| `background` | `#121216ff` | editor window |
| `header` | `#1e1e26ff` | header and footer bands |
| `panel` | `#1b1b22ff` | mode panel frame and every panel template |
| `border` | `#30303aff` | fieldset lines, segment/menu frames, mode-strip frame |
| `slider-track` | `#272730ff` | slider/bipolar track, menu background |
| `slider-frame` | `#3a3a46ff` | slider frame, knob guide ring |
| `accent` | `#5aa0e6ff` | value fills, knob arcs, toggles on, title |
| `accent-dim` | `#2e5a88ff` | selected segment / selected mode fill |
| `text` | `#e6e6ecff` | values, menu text, segment text |
| `text-dim` | `#9c9caaff` | labels, fieldset titles, toggles off, unselected mode text |
| `text-light` | `#ffffffff` | text on an accent fill (selected segment, selected mode) |

Removed: `sidebar`, `section`, `segment-normal`, `segment-selected` (unused or replaced). Contrast: `text-dim` on
`panel` is about 6.9:1, `text` on `panel` about 13:1, `text-light` on `accent-dim` about 7:1.

### 5.3 Gradients (`<gradients>`, CSegmentButton only)

| Name | Stops |
|---|---|
| `segment-bg` | `#2a2a33ff` at 0, `#24242cff` at 1 |
| `segment-bg-selected` | `#3568a0ff` at 0, `#2e5a88ff` at 1 |

### 5.4 Fonts (`<fonts>`; Arial family everywhere, as today, for identical fallback on the three platforms)

| Name | Size | Weight | Used for |
|---|---|---|---|
| `title-font` | 20 | bold | "ITERUM" |
| `readout-font` | 15 | bold | core-strip knob readouts |
| `label-font` | 11 | regular | every label, toggle titles, fieldset titles (`fieldset-font-size="11"`), Gain label |
| `value-font` | 11 | regular | readouts, segment text, menu text, Gain readout |

Four fonts replace five; 9 pt and 7 pt (the "sync" toggle) are gone, and the smallest text in the editor is 11 pt.
The mode strip sets `font-size="11"` on the IconSegmentButton. `section-font` and `mode-font` are removed.

### 5.5 Mode strip (IconSegmentButton colour roles, from `icon_segment_button.h`)

`highlight-color` is the fill of the selected segment, `selected-color` the icon and text colour on it,
`unselected-color` the icon and text colour of the other segments, `frame-color` the outline and dividers:
`highlight-color="accent-dim" selected-color="text-light" unselected-color="text-dim" frame-color="border"
round-radius="4" font-size="11" layout-mode="icon-and-text"`. Unselected segments show the header colour through.
Segment names and icons stay as they are (they match the Mode parameter's strings).

### 5.6 States

| State | Appearance |
|---|---|
| Slider value | `accent` fill on `slider-track` from the left (bipolar: from the centre, `draw-value-from-center`, under a 1 pt `text-dim` centre tick, 4.4) |
| Knob value | `accent` arc over a `slider-frame` guide ring; indicator tick |
| Toggle on / off | icon and title `accent` / `text-dim` |
| Segment selected / other | `segment-bg-selected` fill with `text-light` / `segment-bg` with `text` |
| Mode selected / other | `accent-dim` fill with `text-light` / header ground with `text-dim` |
| Outline buttons (Save Preset, Presets) | default `OutlineButtonColors` (frame 64/64/72, hover fill white 8 %, text grey 192) |
| Copy / Reset pattern buttons (CTextButton) | resting: frame `slider-frame`, text `text`, background a flat `slider-track` gradient, round 3; pressed: background `accent-dim`, text `text-light`, frame `accent-dim` (all set in C++, section 9.3; CTextButton's own defaults are a light-grey gradient with white highlighted text, `cbuttons.cpp:736-743`, unreadable on the dark panel) |
| Hidden by a visibility controller | the cell stays empty; neighbours do not move |
| Hover | VSTGUI default for CSlider/CSegmentButton (none); OutlineButton hover fill |

Nothing in this editor needs a disabled drawing state: no parameter is disabled by another, only shown or hidden.

---

## 6. Labels

### 6.1 Rule

Labels are full words from the dictionary below. **No abbreviations.** If a label does not fit its cell, the control
takes a wider cell (half becomes full), never a shorter word. Units live in the readout, not in the label. Labels
are title case ("Mod Depth", "Grain Size"); fieldset titles upper case; toggle titles title case.

### 6.2 Canonical dictionary (shared meanings take the same label and the same slot in every mode)

| Label | Meaning | Tags |
|---|---|---|
| Time | delay time, core cell 1 | GranularDelayTime, SpectralBaseDelay, ShimmerDelayTime, TapeMotorSpeed, BBDDelayTime, DigitalDelayTime, PingPongDelayTime |
| Chunk Size / Slice Length / Timing Pattern | core cell 1 when the time base is not a delay time | ReverseChunkSize, FreezeSliceLength, MultiTapTimingPattern |
| Sync | TimeMode toggle | *TimeMode (7 modes) |
| Feedback | core cell 2 | *Feedback (9 modes) |
| Mix | core cell 3 (never "Dry/Wet"; the tag names `*DryWet` stay) | GranularDryWet, SpectralDryWet, ShimmerDryWet, TapeMix, BBDMix, DigitalMix, PingPongMix, ReverseDryWet, MultiTapDryWet, FreezeDryWet |
| Gain | footer | Gain |
| Mod Depth, Mod Rate, Mod Waveform | MODULATION | BBDModulationDepth/Rate, DigitalModDepth/Rate/Waveform, PingPongModDepth/Rate |
| Width | stereo width; always alone in STEREO, the standard cell (604,288) (section 2.4) | GranularStereoWidth, SpectralStereoWidth, DigitalWidth, PingPongWidth |
| Age, Era | component aging, model selector | BBDAge/Era, DigitalAge/Era, TapeAge |
| Filter, Filter Type, Filter Cutoff | FILTER | ShimmerFilterEnabled/Cutoff, ReverseFilterEnabled/Type/Cutoff, FreezeNoiseFilterType/Cutoff |
| Freeze | hold toggle; always the toggle slot of core cell 3, beside the Mix knob (4.1), the same slot Sync has beside Time | GranularFreeze, SpectralFreeze |
| Diffusion, Diffusion Size | | SpectralDiffusion, ShimmerDiffusionSize |
| Pitch, Fine Pitch, Pitch Blend | | GranularPitch, ShimmerPitchSemitones, ShimmerPitchCents, ShimmerShimmerMix |
| Grain Size, Density | | GranularGrainSize/Density, FreezeGranularGrainSize/Density |
| Pattern, Pattern Rate | | FreezePatternType, FreezePatternRate |

Mode-specific labels are listed per control in section 7. The former abbreviations and their replacements:
Spd -> Time, Iner -> Inertia, Sat -> Saturation, Intens -> Splice Intensity, Dep -> Mod Depth, Rate -> Mod Rate /
Drift Rate / Pattern Rate / Burst Rate, Wid -> Width, XFB -> Cross-Feedback, Semi -> Pitch, Cent -> Fine Pitch,
Shim -> Pitch Blend, Cut -> Filter Cutoff, LP Cut -> LP Cutoff, HP Cut -> HP Cutoff, Pos -> Position Spray,
Rev -> Reverse Probability, Tex -> Texture, Sym -> Wavefold Symmetry, Atk -> Attack, Rel -> Release, Rot -> Rotation,
Intrvl -> Interval, PosJit -> Position Jitter, SzJit -> Size Jitter, FB Tilt -> Feedback Tilt, Play -> Playback Mode,
Enable -> the toggle's own name (Filter, Splice), Note -> Note Value, Modifier -> Note Modifier, Spatial -> Spatial
Pattern, Taps -> Taps, Length -> Slice Length, Mode (Freeze) -> Slice Mode, Type (Freeze) -> Pattern, sync -> Sync.

### 6.3 Segment names (must equal the parameter's strings; the parameter wins at runtime)

| Tag | `segment-names` |
|---|---|
| GranularEnvelopeType | Hann,Trapezoid,Sine,Blackman |
| SpectralFFTSize | 512,1024,2048,4096 |
| SpectralSpreadDirection | Low-&gt;High,High-&gt;Low,Center Out |
| SpectralSpreadCurve | Linear,Logarithmic |
| FreezeSliceMode | Fixed,Variable |
| FreezeEnvelopeShape | Linear,Exponential |
| FreezeNoiseFilterType | LowPass,HighPass,BandPass |
| ReverseFilterType | LowPass,HighPass,BandPass |
| ReversePlaybackMode | Full Reverse,Alternating,Random |
| BBDEra | MN3005,MN3007,MN3205,SAD1024 |
| DigitalEra | Pristine,80s Digital,Lo-Fi |
| DigitalLimiterCharacter | Soft,Medium,Hard |
| DigitalWavefoldType | Simple,Serge,Buchla,Lockhart |
| MultiTapNoteModifier | None,Triplet,Dotted |

Menus (5+ entries) take their strings from the parameter and need no list in the uidesc: GranularPitchQuant (5),
DigitalModWaveform (6), PingPongLRRatio (7), MultiTapTimingPattern (20), MultiTapSpatialPattern (7),
MultiTapNoteValue (10), MultiTapSnapDivision (22), FreezePatternType (4, a menu because it sits in a core select
cell; strings Euclidean, Granular Scatter, Harmonic Drones, Noise Bursts), FreezePatternRate (21),
FreezeNoiseBurstRate (21), FreezeDroneInterval (6: Unison, Minor 3rd, Major 3rd, Perfect 4th, Perfect 5th, Octave),
FreezeNoiseColor (8: White, Pink, Brown, Blue, Violet, Grey, Velvet, Radio; `freeze_params.h` ~428. VST3Editor
rebuilds a CSegmentButton's segments from the StringListParameter, `vst3editor.cpp` ~332-340, so a 3-name segment
list would still render 8 segments of ~34 pt; a menu is the 4.5/4.6 rule for 5 or more entries).

### 6.4 Readouts

Every slider, bipolar and knob has a readout bound to the same tag. The text is the controller's
`getParamStringByValue` string, shown verbatim: `VST3Editor` copies it into the `CParamDisplay`
(`vst3editor.cpp:252-256`) and adds no unit of its own. The six Freeze sliders without a readout today (Position
Jitter, Size Jitter, Drift, Drift Rate, Noise Filter Cutoff, Noise Filter Sweep) get one.

**One format standard, every mode.** The panel specs quote these formats verbatim; a formatter that prints anything
else has a row in 9.3.

| Kind | Format | Examples | Where |
|---|---|---|---|
| Core time (the core-cell-1 knob of Granular, Spectral, Shimmer, Tape, BBD, Digital, PingPong, Reverse; Freeze Slice Length) | `"%.1f ms"` below 1000 ms, `"%.2f s"` from 1000 ms | "499.0 ms", "2.00 s", "10.00 s" | Digital and PingPong today; Tape, BBD, Reverse gain the seconds switch; Granular, Spectral, Shimmer, Freeze gain the unit (9.3) |
| Body milliseconds (Grain Size, Spread, Inertia, Morph Time, Attack, Release) | `"%.0f ms"` | "100 ms", "2000 ms" | Tape, MultiTap today; Granular 100, Spectral 202, Freeze 1033/1060/1061 (9.3) |
| Percent, unipolar (Feedback of its own range, Mix, Width, Age, Mod Depth, sprays, jitters, Diffusion ...) | `"%.0f%%"`, no space | "0%", "50%", "120%", "200%" | six modes today; Granular, Spectral, Shimmer, Freeze (9.3) |
| Percent, bipolar (Feedback Tilt, Wavefold Symmetry) | `"%+.0f%%"` | "-100%", "+0%", "+100%" | Digital today; Spectral 205 (9.3) |
| Semitones (Granular Pitch, Shimmer Pitch) | `"%+.1f st"` | "-24.0 st", "+12.0 st" | Granular 103, Shimmer 301 (9.3) |
| Cents (Shimmer Fine Pitch) | `"%+.0f ct"` | "-100 ct", "+0 ct" | Shimmer 302 (9.3) |
| Frequency, cutoff (20-20000 Hz) | `"%.0f Hz"` below 1000 Hz, `"%.2f kHz"` from 1000 Hz | "20 Hz", "4.00 kHz", "20.00 kHz" | Reverse, MultiTap today; Shimmer 308, Freeze 1053 (9.3) |
| Frequency, rate (0.1-10 Hz: Mod Rate, Drift Rate) | `"%.2f Hz"` | "0.10 Hz", "5.05 Hz" | BBD, Digital, PingPong today; Freeze 1043 (9.3) |
| Frequency, density (grains per second) | `"%.1f Hz"` | "1.0 Hz", "10.0 Hz" | Granular 101, Freeze 1030 (9.3) |
| Decibels (Gain, Head Level) | `"%.1f dB"`; `"-inf dB"` at the silence floor | "0.0 dB", "-6.0 dB", "-inf dB" | Tape heads today; Gain (9.3) |
| Pan (Head Pan) | `"L%.0f"` / `"C"` / `"R%.0f"` | "L100", "C", "R35" | Tape today |
| Plain integers (Taps, Steps, Hits, Rotation, Voices) | `"%d"` of the handler's rounded value | "8", "16" | MultiTap today; Freeze (9.3) |
| On / off (toggles) | "On" / "Off" | | every mode today |
| Enum (segments, menus) | the parameter's own strings | | every mode today (StringListParameter) |

Readout widths: 64 pt (value-font 11) fits "20.00 kHz", "+24.0 st", "2000 ms", "-100 ct", "120%"; 120 pt
(readout-font 15 bold) fits "10.00 s", "499.0 ms", "1/16 dotted". The state today: Tape, BBD, Digital, PingPong,
Reverse and MultiTap print units; Granular, Spectral and Shimmer print bare numbers; Freeze formats only Mix; the
Gain case prints no "dB". Section 9.3 has one row per file.

---

## 7. Per-mode mapping

Every bound control-tag of every mode (from `inventory-before.json` `controlTags`), its section, cell and widget.
Section rects are panel-relative; control rows are fieldset-relative y (24, 76, 128, 180, 232, 284); "full",
"half L", "half R", "third n", "quarter n" are the cell widths of section 2.3 at x = 12 (full / left / third 1 /
quarter 1), x = 12 + half + 12 (half R), x = 300 / 588 (third 2 / 3), x = 228 / 444 / 660 (quarter 2 / 3 / 4).
Widgets: K = knob cell (4.1), SEL = core select cell (4.2), S = slider cell (4.3), B = bipolar cell (4.4),
SEG = segment cell (4.5), M = menu cell (4.6), T = toggle cell (4.8).

Core strip in every mode: cell 1 at (12,12,284,72), cell 2 at (308,12,284,72), cell 3 at (604,12,284,72).

### 7.0 Chrome (template Editor)

| Tag | Label | Widget | Place |
|---|---|---|---|
| Mode (2) | segment names unchanged | IconSegmentButton | header 130,6,790,40 |
| Gain (1) | Gain | ArcKnob 32 + label + readout | footer (section 2.2) |
| VersionLabel, CheckForUpdatesButton, SavePresetButton, PresetBrowserButton, UpdateBanner | | custom views | footer / last view (section 2.2) |

### 7.1 Granular (GranularPanel) - 5 standard sections

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| GranularDelayTime (102) | Time | K + Sync, `sub-controller="GranularTimeSyncController"` | core 1 | |
| GranularTimeMode (113) | Sync | toggle in core 1 | core 1 | |
| GranularNoteValue (114) | (via knob swap) | none | core 1 | |
| GranularFeedback (109) | Feedback | K | core 2 | |
| GranularDryWet (110) | Mix | K | core 3 | |
| GranularFreeze (108) | Freeze | toggle in core 3 (the 4.1 toggle slot, cell (200,32,76,22)) | core 3 | |
| GranularGrainSize (100) | Grain Size | S | GRAIN (12,96,284,180) | 24 full |
| GranularDensity (101) | Density | S | GRAIN | 76 full |
| GranularEnvelopeType (112) | Grain Envelope | SEG 4 | GRAIN | 128 full |
| GranularPitch (103) | Pitch | B | PITCH (308,96,284,180) | 24 full |
| GranularPitchQuant (116) | Pitch Quantize | M 5 | PITCH | 76 full |
| GranularPitchSpray (104) | Pitch Spray | S | PITCH | 128 full |
| GranularPositionSpray (105) | Position Spray | S | SPRAY (604,96,284,180) | 24 full |
| GranularPanSpray (106) | Pan Spray | S | SPRAY | 76 full |
| GranularReverseProb (107) | Reverse Probability | S | SPRAY | 128 full |
| GranularTexture (117) | Texture | S | CHARACTER (12,288,284,180) | 24 full |
| GranularJitter (115) | Timing Jitter | S | CHARACTER | 76 full |
| GranularStereoWidth (118) | Width | S | STEREO (604,288,284,180), pinned (2.4) | 24 full |

Cell (308,288) is empty (STEREO is pinned to column 3, so the gap sits between CHARACTER and STEREO). CHARACTER row
128 is empty: Freeze sits in core cell 3 (6.2). The Pitch cell carries the centre tick of 4.4.

### 7.2 Spectral (SpectralPanel) - 2 tall sections + STEREO (standard, pinned)

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| SpectralBaseDelay (201) | Time | K + Sync, `sub-controller="SpectralTimeSyncController"` | core 1 | |
| SpectralTimeMode (211) | Sync | toggle in core 1 | core 1 | |
| SpectralNoteValue (212) | (via knob swap) | none | core 1 | |
| SpectralFeedback (204) | Feedback | K | core 2 | |
| SpectralDryWet (208) | Mix | K | core 3 | |
| SpectralFreeze (206) | Freeze | toggle in core 3 (the 4.1 toggle slot, cell (200,32,76,22)) | core 3 | |
| SpectralSpread (202) | Spread | S | SPREAD (12,96,284,372) | 24 full |
| SpectralSpreadDirection (203) | Spread Direction | SEG 3 | SPREAD | 76 full |
| SpectralSpreadCurve (209) | Spread Curve | SEG 2 | SPREAD | 128 full |
| SpectralFeedbackTilt (205) | Feedback Tilt | B | CHARACTER (308,96,284,372) | 24 full |
| SpectralDiffusion (207) | Diffusion | S | CHARACTER | 76 full |
| SpectralFFTSize (200) | FFT Size | SEG 4 | CHARACTER | 128 full |
| SpectralStereoWidth (210) | Width | S | STEREO (604,288,284,180), pinned (2.4) | 24 full |

STEREO holds Width only at row 24, so its slider is at panel y 334 exactly as in Granular, Digital and PingPong
(ruling 5); (604,96,284,180) above it is plain panel background. Rows 180-284 of the two tall sections are bordered
empty space (principle 6). Freeze sits beside Mix in core cell 3 (6.2); the Feedback Tilt cell carries the centre tick
of 4.4.

### 7.3 Shimmer (ShimmerPanel) - 3 tall sections

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| ShimmerDelayTime (300) | Time | K + Sync, `sub-controller="ShimmerTimeSyncController"` | core 1 | |
| ShimmerTimeMode (310) | Sync | toggle in core 1 | core 1 | |
| ShimmerNoteValue (311) | (via knob swap) | none | core 1 | |
| ShimmerFeedback (304) | Feedback | K | core 2 | |
| ShimmerDryWet (309) | Mix | K | core 3 | |
| ShimmerPitchSemitones (301) | Pitch | B | PITCH SHIFT (12,96,284,372) | 24 full |
| ShimmerPitchCents (302) | Fine Pitch | B | PITCH SHIFT | 76 full |
| ShimmerShimmerMix (303) | Pitch Blend | S | CHARACTER (308,96,284,372) | 24 full |
| ShimmerDiffusionSize (306) | Diffusion Size | S | CHARACTER | 76 full |
| ShimmerFilterEnabled (307) | Filter | T | FILTER (604,96,284,372) | 24 half L |
| ShimmerFilterCutoff (308) | Filter Cutoff | S | FILTER | 76 full |

The Pitch and Fine Pitch cells carry the centre tick of 4.4.

### 7.4 Tape (TapePanel) - 3 standard sections + a 3-column HEADS section

Tape has no TimeMode parameter: core cell 1 is a knob cell without the Sync toggle and without a sub-controller.

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| TapeMotorSpeed (400) | Time | K (no sync) | core 1 | |
| TapeFeedback (407) | Feedback | K | core 2 | |
| TapeMix (408) | Mix | K | core 3 | |
| TapeMotorInertia (401) | Inertia | S | MOTOR (12,96,284,180) | 24 full |
| TapeWear (402) | Wear | S | MOTOR | 76 full |
| TapeSaturation (403) | Saturation | S | CHARACTER (308,96,284,180) | 24 full |
| TapeAge (404) | Age | S | CHARACTER | 76 full |
| TapeSpliceEnabled (405) | Splice | T | SPLICE (604,96,284,180) | 24 half L |
| TapeSpliceIntensity (406) | Splice Intensity | S | SPLICE | 76 full |
| TapeHead1Enabled (410) | Head 1 | T | HEADS (12,288,876,180) | 24, ToggleButton at fieldset (12,44,120,24) |
| TapeHead1Level (413) | Level | S | HEADS | 24 third 2 (x 300, w 276) |
| TapeHead1Pan (416) | Pan | B | HEADS | 24 third 3 (x 588, w 276) |
| TapeHead2Enabled (411) | Head 2 | T | HEADS | 76, ToggleButton at (12,96,120,24) |
| TapeHead2Level (414) | Level | S | HEADS | 76 third 2 |
| TapeHead2Pan (417) | Pan | B | HEADS | 76 third 3 |
| TapeHead3Enabled (412) | Head 3 | T | HEADS | 128, ToggleButton at (12,148,120,24) |
| TapeHead3Level (415) | Level | S | HEADS | 128 third 2 |
| TapeHead3Pan (418) | Pan | B | HEADS | 128 third 3 |

The three Pan cells carry the centre tick of 4.4 (x = 588 + 138 = 726 inside HEADS).

### 7.5 BBD (BBDPanel) - CHIP (tall, spans two columns) + MODULATION (standard)

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| BBDDelayTime (500) | Time | K + Sync, `sub-controller="BBDTimeSyncController"` | core 1 | |
| BBDTimeMode (507) | Sync | toggle in core 1 | core 1 | |
| BBDNoteValue (508) | (via knob swap) | none | core 1 | |
| BBDFeedback (501) | Feedback | K | core 2 | |
| BBDMix (506) | Mix | K | core 3 | |
| BBDEra (505) | Era | SEG 4 | CHIP (12,96,580,372) | 24 full (556) |
| BBDAge (504) | Age | S | CHIP | 76 half L (272) |
| BBDModulationDepth (502) | Mod Depth | S | MODULATION (604,96,284,180) | 24 full |
| BBDModulationRate (503) | Mod Rate | S | MODULATION | 76 full |

MODULATION is the standard section of 2.4, so Mod Depth and Mod Rate are at the same panel coordinates as in Digital
and PingPong; (604,288,284,180) below it is plain panel background (BBD has no Width). CHIP rows 128-284 are bordered
empty space (principle 6).

### 7.6 Digital (DigitalPanel) - 4 standard sections

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| DigitalDelayTime (600) | Time | K + Sync, `sub-controller="DigitalTimeSyncController"` | core 1 | |
| DigitalTimeMode (601) | Sync | toggle in core 1 | core 1 | |
| DigitalNoteValue (602) | (via knob swap) | none | core 1 | |
| DigitalFeedback (603) | Feedback | K | core 2 | |
| DigitalMix (610) | Mix | K | core 3 | |
| DigitalEra (605) | Era | SEG 3 | CHARACTER (12,96,284,180) | 24 full |
| DigitalAge (606) + DigitalAgeLabel (9902) | Age | S; the label CTextLabel carries `control-tag="DigitalAgeLabel"`, slider and readout carry `DigitalAge` | CHARACTER | 76 full (hidden while Era is Pristine; the row stays empty) |
| DigitalLimiterCharacter (604) | Limiter | SEG 3 | CHARACTER | 128 full |
| DigitalWavefoldAmount (611) | Wavefold Amount | S | WAVEFOLD (308,96,284,180) | 24 full |
| DigitalWavefoldSymmetry (614) | Wavefold Symmetry | B | WAVEFOLD | 76 full |
| DigitalWavefoldType (613) | Wavefold Type | SEG 4 | WAVEFOLD | 128 full |
| DigitalModDepth (607) | Mod Depth | S | MODULATION (604,96,284,180) | 24 full |
| DigitalModRate (608) | Mod Rate | S | MODULATION | 76 full |
| DigitalModWaveform (609) | Mod Waveform | M 6 | MODULATION | 128 full |
| DigitalWidth (612) | Width | S | STEREO (604,288,284,180), pinned (2.4) | 24 full |

Cells (12,288) and (308,288) are empty. The Wavefold Symmetry cell carries the centre tick of 4.4.

### 7.7 PingPong (PingPongPanel) - primary spans two columns (tall); column 3 split into MODULATION and STEREO (standard)

Column 3 is split because STEREO is pinned to column 3 bottom-most and MODULATION also belongs in column 3 (2.4).

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| PingPongDelayTime (700) | Time | K + Sync, `sub-controller="PingPongTimeSyncController"` | core 1 | |
| PingPongTimeMode (701) | Sync | toggle in core 1 | core 1 | |
| PingPongNoteValue (702) | (via knob swap) | none | core 1 | |
| PingPongFeedback (704) | Feedback | K | core 2 | |
| PingPongMix (709) | Mix | K | core 3 | |
| PingPongLRRatio (703) | L/R Ratio | M 7 | PING PONG (12,96,580,372) | 24 half L (272) |
| PingPongCrossFeedback (705) | Cross-Feedback | S | PING PONG | 24 half R (x 296, w 272) |
| PingPongModDepth (707) | Mod Depth | S | MODULATION (604,96,284,180) | 24 full |
| PingPongModRate (708) | Mod Rate | S | MODULATION | 76 full |
| PingPongWidth (706) | Width | S | STEREO (604,288,284,180), pinned (2.4) | 24 full |

### 7.8 Reverse (ReversePanel) - 2 tall sections, primary spans two columns

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| ReverseChunkSize (800) | Chunk Size | K + Sync, `sub-controller="ReverseTimeSyncController"` | core 1 | |
| ReverseTimeMode (808) | Sync | toggle in core 1 | core 1 | |
| ReverseNoteValue (809) | (via knob swap) | none | core 1 | |
| ReverseFeedback (803) | Feedback | K | core 2 | |
| ReverseDryWet (807) | Mix | K | core 3 | |
| ReversePlaybackMode (802) | Playback Mode | SEG 3 | PLAYBACK (12,96,580,372) | 24 half L (272; 90 per segment) |
| ReverseCrossfade (801) | Crossfade | S | PLAYBACK | 24 half R (x 296, w 272) |
| ReverseFilterEnabled (804) | Filter | T | FILTER (604,96,284,372) | 24 half L |
| ReverseFilterCutoff (805) | Filter Cutoff | S | FILTER | 76 full |
| ReverseFilterType (806) | Filter Type | SEG 3 | FILTER | 128 full |

Filter Cutoff is on row 76 under the row-24 toggle, as in Shimmer's FILTER (7.3); the type segment follows on 128.
XML order follows visual order (PLAYBACK before FILTER); the current file declares the middle fieldset first.

### 7.9 MultiTap (MultiTapPanel) - 3 standard sections + conditional 3-column CUSTOM PATTERN + stacked Copy container

Core cell 1 is a select cell (4.2): MultiTap has no time knob; the Timing Pattern menu is its time base. No sub-controller.

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| MultiTapTimingPattern (900) | Timing Pattern | SEL (menu, 20 entries) | core 1 | |
| MultiTapFeedback (905) | Feedback | K | core 2 | |
| MultiTapDryWet (909) | Mix | K | core 3 | |
| MultiTapSpatialPattern (901) | Spatial Pattern | M 7 | PATTERN (12,96,284,180) | 24 full |
| MultiTapTapCount (902) | Taps | S | PATTERN | 76 full |
| MultiTapMorphTime (908) | Morph Time | S | PATTERN | 128 full |
| MultiTapNoteValue (911) | Note Value | M 10 | NOTE VALUE (308,96,284,180), conditional container, anchor tag 911 | 24 full |
| MultiTapNoteModifier (912) | Note Modifier | SEG 3 | NOTE VALUE | 76 full |
| MultiTapFeedbackLPCutoff (906) | LP Cutoff | S | FEEDBACK FILTER (604,96,284,180) | 24 full |
| MultiTapFeedbackHPCutoff (907) | HP Cutoff | S | FEEDBACK FILTER | 76 full |
| MultiTapCustomLength (914) | Pattern Length | M 6 | TAP PATTERN (12,288,876,180), always visible; per-control visibility (Custom only) | **title line**: label CTextLabel (330,4,92,16, tag 922), COptionMenu (426,0,110,24) |
| MultiTapSnapDivision (913) | Snap | M 22 | TAP PATTERN (12,288,876,180), always visible; per-control visibility (Custom only) | **title line**: label CTextLabel (548,4,40,16, tag 922), COptionMenu (592,0,110,24) |
| ResetPatternButton (custom, tag 923) | Reset | custom CView | CUSTOM PATTERN | title line, (784,0,80,24) |
| TapPatternEditor (custom, tag 920) | | custom CView | CUSTOM PATTERN | (12,28,852,140); bottom pad 12 |
| CopyPatternButton (custom, tag 921; **new placement**, the controller already creates it) | Copy to Custom | custom CView at wrapper-relative (714,0,150,24) | its own `CViewContainer origin="12, 288" size="876, 24" transparent="true"`, the last sibling of CUSTOM PATTERN at the same origin (stacked); full attribute set in 8.2 | shown by the controller when the pattern is not Custom; panel-absolute (726,288,150,24), the same title-line slot as Reset (796,288,80,24) |

The NOTE VALUE fieldset is hidden by `ContainerVisibilityController` except for the Mathematical patterns
(timing pattern 14-18); its cell stays empty otherwise. The TAP PATTERN fieldset (CUSTOM PATTERN before specs/iterum-tap-editor/design.md section 6) is always visible: the editor shows every pattern, read-only unless Custom (19); Length, Snap, their labels and Reset show only for Custom.

The Snap label, Snap menu and Reset sit on the fieldset's title line (y 0-24, the border runs at y 5.5 behind
them, legend-style) so the tap editor keeps 140 pt of vertical travel (today 135). The alternative, a Snap/Reset row
below the editor, does not fit: 24 (row 1) + 140 + 24 + 8 pad exceeds the 180 pt section. The Copy button
(panel x 726-876) never intersects the Snap label (560-600) or the Snap menu (604-714; 12 pt gap). It does share
Reset's slot, which the stacked-wrapper rule of 4.10 allows only because Reset hides with the fieldset and Copy
hides on Custom: they are never visible together.

### 7.10 Freeze (FreezePanel) - core select cell, mirrored body: SLICE + ENVELOPE in column 1, 4 stacked tall variants at columns 2-3

Core cell 1 is a knob cell without Sync (no TimeMode parameter); core cell 2 is a select cell for the Pattern
because Freeze has no feedback parameter.

The body is **mirrored** (the documented exception of 2.4): the four stacked variants sit at (308,96,580,372),
directly under the Pattern selector (core 2) and Mix (core 3) that choose and blend them; SLICE sits at
(12,96,284,180) directly under the Slice Length knob (core 1), because Slice Mode and Slice Length are a pair
(Variable mode varies the slice length) and share one SLICE fieldset today; ENVELOPE sits below SLICE at
(12,288,284,180).

| Tag | Label | Widget | Section (rect) | Row / cell |
|---|---|---|---|---|
| FreezeSliceLength (1016) | Slice Length | K (no sync) | core 1 | |
| FreezePatternType (1015) | Pattern | SEL (menu, 4 entries, the parameter's strings: Euclidean, Granular Scatter, Harmonic Drones, Noise Bursts) | core 2 | |
| FreezeDryWet (1012) | Mix | K | core 3 | |
| FreezeHold (1070) | Hold | T (the knob cell's toggle slot, 4.1) | core 3 | toggle at (200,32,76,22) |
| FreezeCaptureLength (1071) | Capture | M 4 (1/4, 1/2, 1/1, 2/1) | CAPTURE (12,96,284,180) | 24 full |
| FreezeSliceMode (1017) | Slice Mode | SEG 2 | CAPTURE | 76 full |
| FreezeDryMode (1072) | Dry | SEG 2 (Mix, Insert) | CAPTURE | 128 full |
| FreezeEnvelopeAttack (1060) | Attack | S | ENVELOPE (12,288,284,180) | 24 full |
| FreezeEnvelopeRelease (1061) | Release | S | ENVELOPE | 76 full |
| FreezeEnvelopeShape (1062) | Envelope Shape | SEG 2 | ENVELOPE | 128 full |
| FreezeEuclideanSteps (1020) | Steps | S | EUCLIDEAN (308,96,580,372), anchor 1020 | 24 half L (272) |
| FreezeEuclideanHits (1021) | Hits | S | EUCLIDEAN | 24 half R (x 296) |
| FreezeEuclideanRotation (1022) | Rotation | S | EUCLIDEAN | 76 half L |
| FreezePatternRate (1023) | Pattern Rate | M 21 | EUCLIDEAN | 76 half R |
| FreezeGranularDensity (1030) | Density | S | GRANULAR SCATTER (308,96,580,372), anchor 1030 | 24 half L |
| FreezeGranularGrainSize (1033) | Grain Size | S | GRANULAR SCATTER | 24 half R |
| FreezeGranularPositionJitter (1031) | Position Jitter | S (readout added) | GRANULAR SCATTER | 76 half L |
| FreezeGranularSizeJitter (1032) | Size Jitter | S (readout added) | GRANULAR SCATTER | 76 half R |
| FreezeDroneVoiceCount (1040) | Voices | S | HARMONIC DRONES (308,96,580,372), anchor 1040 | 24 half L |
| FreezeDroneInterval (1041) | Interval | M 6 (Unison ... Octave) | HARMONIC DRONES | 24 half R |
| FreezeDroneDrift (1042) | Drift | S (readout added) | HARMONIC DRONES | 76 half L |
| FreezeDroneDriftRate (1043) | Drift Rate | S (readout added) | HARMONIC DRONES | 76 half R |
| FreezeNoiseColor (1050) | Noise Color | M 8 (White ... Radio, section 6.3) | NOISE BURSTS (308,96,580,372), anchor 1050 | 24 half L |
| FreezeNoiseBurstRate (1051) | Burst Rate | M 21 | NOISE BURSTS | 24 half R |
| FreezeNoiseFilterType (1052) | Filter Type | SEG 3 | NOISE BURSTS | 76 half L |
| FreezeNoiseFilterCutoff (1053) | Filter Cutoff | S (readout added) | NOISE BURSTS | 76 half R |
| FreezeNoiseFilterSweep (1054) | Filter Sweep | S (readout added) | NOISE BURSTS | 128 half L |

The four variant fieldsets are the same size (580 x 372) at the same origin, so the panel's silhouette does not
change with the pattern. CAPTURE and ENVELOPE apply to every pattern and stay outside the variants. XML order is
CAPTURE, ENVELOPE, then the four variants (column 1 top to bottom, then columns 2-3). The Freeze redesign
(`specs/iterum-freeze/design.md` 3.2) renamed SLICE to CAPTURE and added Hold, Capture and Dry; the paragraphs
above that say SLICE describe the fieldset as it was first placed.

### 7.11 Count check

Bound tags in the inventory: 2 chrome + 17 Granular + 12 Spectral + 10 Shimmer + 18 Tape + 8 BBD + 14 Digital
(+ 9902) + 9 PingPong + 9 Reverse + 11 MultiTap + 27 Freeze = 137 control-tags with a view, plus 7 NoteValue tags
reached through the knob swap, 3 MultiTap custom-view tags (920, 921 new placement, 923) and the shared custom views.
Every row above maps to exactly one of them; the eight centre ticks (Granular 1, Spectral 1, Shimmer 2, Digital 1,
Tape 3) carry no tag and count for nothing here. Two checks in `uidesc-inventory.js --check --baseline inventory-before.json`
enforce it: `lost-binding` (a control-tag or custom-view-name bound in the baseline and bound nowhere now) and
`lost-control` (section 9.5: for every tag that has an **interactive** view in the baseline, any class other than
CParamDisplay or CTextLabel, at least one interactive view with that tag must exist now; the seven NoteValue tags
have no view in the baseline, so they are never in that set). Without the second check a stylesheet that dropped a
CSlider and kept its CParamDisplay readout would pass, because `bindings()` is a set of tag names. Both checks are in
the tool (9.5).

---

## 8. Conditional groups

### 8.1 How the controllers find things (load-bearing)

- `ContainerVisibilityController::findContainerByChildTag` (`visibility_controller.h` 275-305) walks the frame,
  finds the first `CControl` with the anchor tag and toggles **its immediate parent container**. Therefore the
  anchor control is a direct child of the fieldset it must hide, never inside a nested cell container, and every
  control that must hide with it is in that same fieldset. This is why body cells are flat (section 4).
- `VisibilityController` toggles every `CControl` carrying a listed tag, wherever it is. A `CTextLabel` is a
  CControl, which is how the Digital Age label (tag 9902) is hidden together with the Age slider and readout (606).
- `DelayTimeSyncController::verifyView` sees every view created under the container that names it in
  `sub-controller`; it collects all controls with the free-time tag (knob + readout) and the control with the
  TimeMode tag (the Sync toggle). Those three live inside the core cell 1 container.
- Controllers are created in `didOpen` and look controls up on every change, so nothing in the layout may cache
  or depend on a view pointer.

### 8.2 The table

| Group | Container (panel rect) | Anchor / tags | Must contain | Must not contain | Shown when |
|---|---|---|---|---|---|
| Time sync (7 modes) | core cell 1 `CViewContainer` (12,12,284,72) with `sub-controller` | free-time tag, TimeMode tag | ArcKnob + CParamDisplay (free-time tag), Sync ToggleButton (TimeMode tag), the label | anything with another control-tag | always; the knob/readout carry the NoteValue tag while synced, from the panel's creation on (9.3 "Read the Sync state at panel build") and on every toggle |
| Digital Age | none (per-control) | 9902, 606 | label (9902), slider + readout (606) in CHARACTER row 76 | | Era >= 0.25 (80s Digital, Lo-Fi) |
| MultiTap note value | NOTE VALUE fieldset (308,96,284,180) | 911 | 911, 912 and their two labels | anything else | timing pattern 14-18 (normalized 14/19 <= v < 19/19) |
| MultiTap custom controls | none (per-control `VisibilityController`; the TAP PATTERN fieldset and the editor 920 are always visible) | 913, 914, 922, 923 | Length menu (914) at (426,0,110,24) + its label (922) at (330,4,92,16), Snap menu (913) at (592,0,110,24) + its label (922) at (548,4,40,16), ResetPatternButton (923) at (784,0,80,24) | the Copy button (921) | timing pattern Custom (v >= 0.99) |
| MultiTap copy button | `<view class="CViewContainer" origin="12, 288" size="876, 24" transparent="true"/>`, the **last** sibling of the panel, at CUSTOM PATTERN's origin (stacked); no background-color | 921 (set by the controller) | CopyPatternButton only, `custom-view-name="CopyPatternButton" origin="714, 0" size="150, 24"` | anything else | timing pattern not Custom (v < 0.99); toggled per control by tag, the wrapper stays visible and, being transparent, neither paints nor swallows clicks |
| Freeze Euclidean | fieldset (308,96,580,372) | 1020 | 1020, 1021, 1022, 1023 | | pattern < 0.166 |
| Freeze Granular Scatter | fieldset (308,96,580,372) | 1030 | 1030, 1031, 1032, 1033 | | 0.166 <= pattern < 0.5 |
| Freeze Harmonic Drones | fieldset (308,96,580,372) | 1040 | 1040, 1041, 1042, 1043 | | 0.5 <= pattern < 0.833 |
| Freeze Noise Bursts | fieldset (308,96,580,372) | 1050 | 1050, 1051, 1052, 1053, 1054 | | pattern >= 0.833 |

### 8.3 Grid fit and the inventory check

- The four Freeze variants and the MultiTap Copy wrapper are sibling containers at one origin; the inventory
  reports them as "stacked", not as overlaps. The Copy button is wrapped in a container for exactly this reason:
  a bare CView over the hidden fieldset would be an overlap defect. The wrapper is transparent and its button
  lies where CUSTOM PATTERN has nothing that can be visible at the same time (4.10, 7.9).
- The UpdateBanner is a plain CView stacked on the header container (2.2); the inventory cannot classify it as
  stacked (only two containers at one origin are), so **the build stage uses the tool change of 9.5**: the
  overlap rule exempts the view whose `custom-view-name` is `UpdateBanner`. The gate is otherwise strict: it does
  not merely diff against the baseline's 18 defects, every one of which the redesign removes (the knob/Sync
  overlaps, the 30-pt label/readout overlaps, the Reverse container and the MultiTap editor overlaps).
- Hidden groups leave their cell empty rather than collapsing: NOTE VALUE's cell (308,96) is blank for 15 of the 20
  timing patterns, the CUSTOM PATTERN row is blank for 19 of them (only the Copy button shows, at the right end of
  the row), and Digital's Age row is blank for Pristine. Nothing else moves, which is the point (principle 5).
- A readout is a CParamDisplay with the same tag as its control, so it hides and shows with it under
  `VisibilityController` without being listed separately.
- The bipolar centre tick (4.4) is a childless 1 pt `CViewContainer` inside its slider's rect; the inventory exempts
  exactly that pair (9.5, third row). It is drawn over the track and is mouse-disabled, so it neither hides the fill
  nor takes a click.
- The `--check` pass is read per stylesheet for that stylesheet's own template and tags (9.4 step 2): no overlap
  (banner and ticks exempt), no out-of-bounds in that template, no lost binding, no lost control (7.11, 9.5) and no
  changed tag for that mode against `inventory-before.json`. Whole-file `UIDESC CLEAN` is required once, after all
  eleven stylesheets.

---

## 9. Implementation plan

### 9.1 Template structure (all in `plugins/iterum/resources/editor.uidesc`)

```
<colors>      11 tokens of section 5.2
<fonts>       4 fonts of section 5.4
<control-tags> unchanged (names and numbers frozen)
<gradients>   2 gradients of section 5.3
<template Editor 940x608>          header / UIViewSwitchContainer 20,64,900,480 / footer / UpdateBanner (last)
<template GranularPanel 900x480>   core strip (3 cell containers) + 5 fieldsets
<template SpectralPanel 900x480>   core strip + SPREAD, CHARACTER (tall) + STEREO (standard, pinned at 604,288)
<template ShimmerPanel 900x480>    core strip + 3 tall fieldsets
<template TapePanel 900x480>       core strip + 3 fieldsets + HEADS
<template BBDPanel 900x480>        core strip + CHIP (2 col, tall) + MODULATION (standard)
<template DigitalPanel 900x480>    core strip + 4 fieldsets
<template PingPongPanel 900x480>   core strip + PING PONG (2 col, tall) + MODULATION + STEREO (column 3 split)
<template ReversePanel 900x480>    core strip + PLAYBACK (2 col, tall) + FILTER (tall)
<template MultiTapPanel 900x480>   core strip + 3 fieldsets + CUSTOM PATTERN + transparent Copy wrapper (last)
<template FreezePanel 900x480>     core strip + SLICE + ENVELOPE (column 1) + 4 stacked variants (columns 2-3)
```

Every panel template keeps its name, class `CViewContainer`, `background-color="panel"`, `transparent="false"`;
its size becomes 900 x 480. Inside a panel the XML order is the visual order: core cells left to right, then
sections row 1 left to right, then row 2.

### 9.2 XSLT strategy (`specs/iterum-ui-redesign/xslt/`, applied with `npx xslt3 -xsl:FILE.xsl -s:IN -o:OUT`)

1. `00-global.xsl` (identity transform plus):
   - replaces `<colors>`, `<fonts>`, `<gradients>` with sections 5.2-5.4;
   - replaces `template[@name='Editor']` with section 2.2;
   - on every `view[@class='CSlider']`: renames `back-color` -> `draw-back-color`, `value-color` -> `draw-value-color`,
     `frame-color` -> `draw-frame-color` and sets the values of section 4.3, so no slider anywhere keeps the defect
     even before its panel stylesheet runs;
   - removes every `view[@class='CTextLabel'][@font='section-font']` directly under a panel template (the ten
     panel titles) and replaces `font="section-font"` / `font="mode-font"` wherever else they occur with
     `label-font` so no view references a removed font.
2. `cells.xsl`: a module the ten panel stylesheets `<xsl:include>`. Named templates `knob-cell`, `select-cell`,
   `slider-cell`, `bipolar-cell`, `segment-cell`, `menu-cell`, `toggle-cell`, `section` that take
   `x, y, w, tag, label, [segments|sub-controller|toggle-tag|toggle-title]` parameters and emit exactly the attribute
   sets of section 4; `bipolar-cell` emits the slider and then the centre tick of 4.4; `knob-cell` takes an optional
   toggle (tag + title) for the Sync and Freeze slots; no cell template emits `default-value` (4.1). One place
   defines the geometry; ten stylesheets only say which cell goes where. (If an author cannot make the include
   resolve, the named templates are copied verbatim into the panel stylesheet; the attribute sets must still match
   section 4 to the letter.)
3. `<mode>.xsl` x 10: identity transform plus one `xsl:template match="template[@name='<Mode>Panel']"` that
   emits the whole template from section 7 by calling the cell templates. Each is dry-run on a private copy of the
   `00-global.xsl` result and checked with `uidesc-inventory.js --check --baseline inventory-before.json`, read for
   its own template and tags only (9.4 step 2); then all ten are applied in order onto the result of
   `00-global.xsl`, and that file must print `UIDESC CLEAN`.
4. Nothing in the uidesc is edited by hand or by string replacement.

### 9.3 C++ changes

Every row is test-first: the named Catch2 case is written against today's code, seen red, and then the change lands.
Formatter rows change controller display strings only; no parameter ID, range, default, units metadata or saved state
moves (section 10 ruling scope, section 11 "units metadata").

| Change | File | Why |
|---|---|---|
| No new view class | nothing in `entry.cpp` or `editor_screenshot_test.cpp` | the bipolar cell (4.4) is VSTGUI's `CSlider` with `draw-value-from-center="true"` plus a plain `CViewContainer` tick; `BipolarSlider` is not registered and nothing in plugins/shared changes |
| Drop the light button triple | `plugins/iterum/src/controller/custom_views.h`: `PresetBrowserButton` and `SavePresetButton` construct `OutlineButton` with the default `OutlineButtonColors`; delete `kIterumLightButtonColors` (lines 24-30) | dark theme (5.1, ruling 1); the defaults are the Krate dark triple |
| Restyle Copy / Reset pattern buttons | `custom_views.h` `CopyPatternButton` / `ResetPatternButton` constructors (lines 63-100), the full `CTextButton` set: `setFrameColor(CColor(58,58,70))` (slider-frame), `setFrameColorHighlighted(CColor(46,90,136))` (accent-dim), `setTextColor(CColor(230,230,236))` (text), `setTextColorHighlighted(CColor(255,255,255))` (text-light), **`setGradient(VSTGUI::owned(VSTGUI::CGradient::create(0, 1, CColor(39,39,48), CColor(39,39,48))))`** (flat slider-track), **`setGradientHighlighted(VSTGUI::owned(VSTGUI::CGradient::create(0, 1, CColor(46,90,136), CColor(46,90,136))))`** (accent-dim), `setRoundRadius(3)`. `CGradient::create` returns a new `AtomicReferenceCounted` object holding one reference (`cgradient.h:19-22`); `CTextButton::setGradient` assigns it to a `SharedPointer<CGradient>` (`cbuttons.h:267-268`, `cbuttons.cpp:807-818`), which adds a second one, so a bare `create` leaks a gradient per panel instantiation. `owned()` hands the create reference to a temporary `SharedPointer` that converts to the raw pointer for the call and releases on return, exactly as VSTGUI's own constructor does (`cbuttons.cpp:739-740`). Test first: a `[multitap]` Catch2 case constructs both buttons and asserts `getGradient()->getNbReference() == 1` and `getGradientHighlighted()->getNbReference() == 1` (`vstguibase.h:273`), the two colour stops of each gradient, the text colours and the frame colours | they were styled for the light panel; `CTextButton` defaults to a 220-to-180 grey gradient with white highlighted text (`cbuttons.cpp:736-743`), so setting only frame, text and radius would leave near-white text on light grey |
| Read the Sync state at panel build | `plugins/iterum/src/controller/delay_time_sync_controller.cpp` `verifyView` (lines 61-78) and `syncTimeControlValues` (lines 23-34): resolve the edit controller through the parent controller the constructor receives, `dynamic_cast<Steinberg::Vst::VSTGUIEditor*>(controller)->getController()` (`controller` is `DelegationController`'s protected member, `delegationcontroller.h:31`; `Controller::createSubController` passes the `VST3Editor`, `controller.cpp:815-827`), in one private helper used by both functions, instead of `control->getFrame()->getEditor()`. The header keeps its constructor signature. Test first: a `[iterum][ui]` case in `plugins/iterum/tests/unit/vst/editor_panels_test.cpp` (9.4) sets `kGranularTimeModeId` to 1.0 on the controller, opens the editor headlessly and asserts that under the template root exactly two controls carry tag 114 (`kGranularNoteValueId`), an `ArcKnob` and a `CParamDisplay`, and none carries 102; then switches to Digital (its TimeMode registers Synced by default, `digital_params.h`, so no parameter is set) and asserts tag 602 on knob and readout; then turns the Sync toggle off through the control (`setValueNormalized(0)`, `valueChanged()`) and asserts the pair reads 600 again | `verifyView` is called from `UIDescription::createViewFromNode` (`uidescription.cpp:740`) on a view that its parent has not yet added (`addView` runs at line 711, after `createViewFromNode` returns) and whose template is not attached to any frame; `CView::getFrame()` returns the frame set in `attached()` (`cview.cpp:452`), so it is null, the branch never runs and the knob and the new 15 pt readout bind to the free-time parameter although Sync is on. That is the state on every editor open and every mode switch while synced, and the plain default state of Digital and PingPong, whose TimeMode defaults to Synced. (Corrected 2026-10-28 by the build stage: true of `verifyView` alone; in the full editor the Sync toggle's bind-time `valueChanged()` retags the pair, so it only breaks a cell that declares the toggle first. See 4.9.) The readout then shows the free time while the host plays the note value, and the knob edits a hidden parameter until Sync is clicked twice |
| Complete the Granular formatter | `plugins/iterum/src/parameters/granular_params.h` `formatGranularParam` (lines 405-467), to the 6.4 standard: 100 `"%.0f ms"` (10 + n*490); 101 `"%.1f Hz"` (1 + n*99); 102 core time (n*2000: `"%.1f ms"` below 1000, `"%.2f s"` from 1000); 103 `"%+.1f st"`; 104, 105, 106, 107, 110, 115, 117, 118 `"%.0f%%"`; 109 `"%.0f%%"` of n*120. The On/Off and StringList cases stay. Test first: a `[granular][format]` case asserting min / default / max for every id, e.g. 100 "10 ms" / "100 ms" / "500 ms", 101 "1.0 Hz" / "10.0 Hz" / "100.0 Hz", 102 "0.0 ms" / "500.0 ms" / "2.00 s", 103 "-24.0 st" / "+0.0 st" / "+24.0 st", 109 "0%" / "0%" / "120%" (defaults from `registerGranularParams`) | today every Granular readout is a bare number ("500", "80", "12.5") beside modes that show "500.0 ms" and "80%" |
| Complete the Spectral formatter | `plugins/iterum/src/parameters/spectral_params.h` `formatSpectralParam` (lines 307-357): split the shared 201/202 branch; 201 core time (n*2000, seconds from 1000); **202 `"%.0f ms"`** (it is 0-2000 ms, `spectral_params.h:79-83`, not a percentage); 204 `"%.0f%%"` of n*120; 205 `"%+.0f%%"`; 207, 208, 210 `"%.0f%%"`. Test first: `[spectral][format]`, e.g. 201 "0.0 ms" / "250.0 ms" / "2.00 s" (default 0.125), 202 "0 ms" / "0 ms" / "2000 ms", 204 "0%" / "0%" / "120%", 205 "-100%" / "+0%" / "+100%" | as Granular; the Spectral spec's first draft gave 202 a percent format, which would label a time value "%" |
| Complete the Shimmer formatter | `plugins/iterum/src/parameters/shimmer_params.h` `formatShimmerParam` (lines 282-340): 300 core time (10 + n*4990, seconds from 1000); 301 `"%+.1f st"`; 302 `"%+.0f ct"`; 303, 306, 309 `"%.0f%%"`; 304 `"%.0f%%"` of n*120; 308 `"%.0f Hz"` below 1000 Hz, `"%.2f kHz"` from 1000 Hz (the value is the linear 20 + n*19980 the handler uses; only the format changes, replacing `"%.1fk"`). Test first: `[shimmer][format]`, e.g. 300 "10.0 ms" / "499.0 ms" / "5.00 s" (default 0.098), 301 "-24.0 st" / "+12.0 st" / "+24.0 st", 302 "-100 ct" / "+0 ct" / "+100 ct", 304 "0%" / "50%" / "120%", 308 "20 Hz" / "4.00 kHz" / "20.00 kHz" (default 0.199) | as Granular; `"%.1fk"` was a format no other mode uses, and `%.0f` semitones hid the tenths Granular shows for the same label |
| Gain in dB | `plugins/iterum/src/controller/controller.cpp` `getParamStringByValue`, the `kGainId` case (line 410): `"%.1f dB"`. `getParamValueByString` (lines 482-495) parses with `sscanf("%lf")`, which still reads "-6.0 dB". Test first: a `[vst][gain][format]` case on a `Controller` after `initialize`: 0.0 -> "-80.0 dB", 0.5 -> "0.0 dB", 1.0 -> "6.0 dB", and `getParamValueByString("-6.0 dB")` round-trips | the footer Gain readout (2.2) is the only readout outside the panels; it printed "0.0" with no unit while the parameter registers "dB" |
| Core time readouts switch to seconds | `tape_params.h` `formatTapeParam` case 400 (line 350), `bbd_params.h` `formatBBDParam` case 500 (line 218), `reverse_params.h` `formatReverseParam` case 800 (line 219): keep `"%.1f ms"` below 1000 ms and print `"%.2f s"` from 1000 ms, the rule Digital (`digital_params.h:296-298`) and PingPong (`pingpong_params.h:225-227`) already follow. Test first: `[tape][format]`, `[bbd][format]`, `[reverse][format]` cases: 400 "20.0 ms" / "499.2 ms" / "2.00 s" (default 0.242), 500 "20.0 ms" / "300.3 ms" / "1.00 s" (default 0.286), 800 "10.0 ms" / "499.5 ms" / "2.00 s" (default 0.246); the expected strings are computed from each formatter's own mapping in float, as the formatter does | one core time format for the seven time knobs (6.4): "2.00 s" next to Digital's "2.00 s", never "2000.0 ms" beside it |
| Complete the Freeze formatter | `plugins/iterum/src/parameters/freeze_params.h` `formatFreezeParam` (lines 502-517): **the only source of each mapping is `handleFreezeParamChange` in the same file** (the processor's denormalisation, lines 71-256, constants from `dsp/include/krate/dsp/core/pattern_freeze_types.h`); `pattern_freeze_params.h` is dead code (`registerPatternFreezeParams` is never called, only `freeze_params.h` is included and registered, `controller.cpp:109, 206`) and is neither read nor deleted. Cases: 1016 core time (10 + n*1990: `"%.1f ms"` below 1000, `"%.2f s"` from 1000; default (200-10)/1990 reads "200.0 ms"), 1020 (integer 2 + n*30), **1021 (integer 1 + n*(steps-1)) and 1022 (integer n*(steps-1))**, 1030 (`"%.1f Hz"`, 1 + n*49), 1031/1032/1042/1054 (`"%.0f%%"`), 1033 (`"%.0f ms"`, 10 + n*490), 1040 (integer 1 + n*3), 1043 (`"%.2f Hz"`, 0.1 + n*1.9), 1053 (`"%.0f Hz"` below 1000, `"%.2f kHz"` from 1000; 20 * 1000^n), 1060 (`"%.0f ms"`, n*500), 1061 (`"%.0f ms"`, n*2000); integers round with + 0.5 as the handler does and print `"%d"`. Hits and Rotation depend on the current Steps, so `formatFreezeParam` gains a `double stepsNormalized` argument that `Controller::getParamStringByValue` fills from `getParamNormalized(kFreezeEuclideanStepsId)`. Test first: a `[freeze][format]` case asserting the strings for min, default and max of each id, and Hits/Rotation at two Steps values | readouts show raw normalized values today (Freeze audit, issue 2); a formatter is controller code, not frozen state |
| Refresh Hits / Rotation when Steps moves | `controller.cpp` `Controller::setParamNormalized`: after the base-class call (line 580), when `id == kFreezeEuclideanStepsId`, call `deferUpdate()` on `getParameterObject(kFreezeEuclideanHitsId)` and `getParameterObject(kFreezeEuclideanRotationId)` (`fobject.h:99`). Test first: a `[freeze]` case that registers a dependent on 1021, sets 1020, and sees the notification | VST3Editor redraws a CParamDisplay only when its own parameter notifies, so the Hits and Rotation readouts would show stale text after a Steps change |
| Nothing else | `visibility_controller.h`, `tap_pattern_editor.*`, `update_banner_view.h`, `digital_params.h`, `pingpong_params.h`, `multitap_params.h`, everything in `plugins/shared` | the controllers find views by tag and container, which the layout preserves; the CopyPatternButton creation and its visibility controller already exist and only lacked a view; Digital, PingPong and MultiTap already print the 6.4 formats |

No change to `plugin_ids.h`, `parameters/` registration (titles, units strings, defaults), the processor, state, or any control-tag.

### 9.4 Verification

The build stage runs these in this order.

1. **Tool state first.** The 9.5 banner exemption and `lost-control` check are in `tools/uidesc-inventory.js` already
   (2026-10-28): `node tools/uidesc-inventory.js --self-test` prints `uidesc-inventory self-test: 5 cases OK`, and
   `node tools/uidesc-inventory.js plugins/iterum/resources/editor.uidesc --check --baseline specs/iterum-ui-redesign/inventory-before.json`
   on the untouched file prints `UIDESC DEFECTS {"overlap":17}` and exits 1: the baseline's 18 geometry defects minus
   the banner pair (Granular 6, Reverse 3, MultiTap 3, one knob/Sync overlap in each of Spectral, Shimmer, BBD,
   Digital, PingPong), no lost binding, no lost control, no changed tag. So the gate is known to be able to pass once
   the ten panels are rewritten. The centre-tick exemption (9.5, third row) lands, test-first in `--self-test`, before
   the first panel stylesheet with a bipolar cell is dry-run (Granular is the first).
2. **The per-stylesheet gate is scoped to the stylesheet's own templates and control-tags.** `--check` reports the
   whole file and exits 1 on any defect; it has no per-template option and does not need one. The gate reads the
   defect lines instead:
   - after `00-global.xsl`, dry-run on a private copy: no defect line tagged `[Editor]`, no `lost-binding` naming the
     Editor's bindings (`control-tag:Mode`, `control-tag:Gain`, `custom-view:VersionLabel`,
     `custom-view:CheckForUpdatesButton`, `custom-view:SavePresetButton`, `custom-view:PresetBrowserButton`,
     `custom-view:UpdateBanner`), no `changed-tag`. The 17 panel overlaps are expected to remain: 00-global only
     renames the slider colour attributes and removes the panel titles inside the panels.
   - each `<mode>.xsl`, dry-run on a private copy of the 00-global result: no defect line tagged `[<Mode>Panel]`, no
     `lost-binding` or `lost-control` naming one of that mode's control-tags or custom views (section 7, the panel
     spec's mapping table), no `changed-tag`. Defect lines of other templates are those stylesheets' answers, not this
     one's; the fixer loop never edits a panel that another stylesheet replaces.
   - `UIDESC CLEAN` is required **exactly once**: after `00-global.xsl` and all ten panel stylesheets have been applied
     in order to the real file. That run must also print 0 stacked pairs for every panel except Freeze (6) and
     MultiTap (1, the Copy wrapper).
3. **REQUIRED, test-first: the editor panel structure test**, `plugins/iterum/tests/unit/vst/editor_panels_test.cpp`,
   Catch2 cases tagged `[iterum][ui]`, not hidden, in `plugin_tests`. It is modelled on
   `editor_screenshot_test.cpp`: it includes `ui/arc_knob.h`, `ui/fieldset_container.h`, `ui/icon_segment_button.h`
   and `ui/toggle_button.h` so the shared view creators are registered (the plugin registers them in `entry.cpp`,
   which `plugin_tests` does not compile), opens the editor headlessly through `VST3Editor` + `attached`, attaches the
   template root (frame child 0) if `CFrame::open` did not, calls `triggerDeferedUpdates`, finds the
   `UIViewSwitchContainer` and calls `setAnimationTime(0)`, and switches the `Mode` control through all ten modes
   (`setValueNormalized(mode / 9)`, `valueChanged()`, `triggerDeferedUpdates()`). Every search starts at the template
   root, never at the frame: the preset browser overlay is frame child 1 and reuses tags 0-31. Written red against
   today's uidesc first (today's panels have CCheckBoxes, a CKnob and no ArcKnob for Feedback), green after the
   stylesheets. Cases:
   - **Every control of every mode is live and of the designed class.** For each mode, a table of
     `{tag, expected class}` taken from section 7 (K -> `Krate::Plugins::ArcKnob`, S and B -> `VSTGUI::CSlider`
     (B additionally `getDrawStyle() & kDrawValueFromCenter`), SEG -> `VSTGUI::CSegmentButton`, M and SEL ->
     `VSTGUI::COptionMenu`, T and Sync and the core-3 Freeze toggle -> `Krate::Plugins::ToggleButton`, 9902 ->
     `VSTGUI::CTextLabel`): at least one `CControl` with that tag whose `dynamic_cast` to the class succeeds, and for
     every K / S / B row at least one `CParamDisplay` with the same tag (the readout). The NoteValue tags are asserted
     absent while Sync is off. No plain container stands where the uidesc names a registered class: among the
     descendants of the current panel (the switch container's first child, itself a plain `CViewContainer` that is
     not counted) the number of `Krate::Plugins::FieldsetContainer` views equals the
     section count of section 7 (Granular 5, Spectral 3, Shimmer 3, Tape 4, BBD 2, Digital 4, PingPong 3, Reverse 2,
     MultiTap 4, Freeze 6) and the number of views whose dynamic type is exactly `VSTGUI::CViewContainer` equals the
     cell containers plus wrappers plus centre ticks the panel spec lists (Granular 4, Spectral 4, Shimmer 5, Tape 6,
     BBD 3, Digital 4, PingPong 3, Reverse 3, MultiTap 4, Freeze 3). An unregistered class is built as a plain
     `CViewContainer` by the factory (`uidescription.cpp` ~690), which this count catches.
   - **Conditional groups.** Freeze: for `kFreezePatternTypeId` = 0, 1/3, 2/3, 1 (set on the controller, then
     `triggerDeferedUpdates`), exactly one of the four variant fieldsets (the `FieldsetContainer` parent of 1020, 1030,
     1040, 1050) is visible. MultiTap: for `kMultiTapTimingPatternId` = 2/19, 15/19, 19/19, the NOTE VALUE fieldset
     (parent of 911) is visible only at 15/19, the CUSTOM PATTERN fieldset (parent of 920) only at 19/19, the Copy
     button (921) only below 19/19, and Copy (921) and Reset (923) are never both visible. Digital: at
     `kDigitalEraId` = 0 the 606 slider and readout and the 9902 label are hidden, at 0.5 they are visible.
   - **Sync retags at open and on toggle** (the 9.3 `verifyView` row): see that row for the three assertions.
4. Build `plugin_tests`; `node tools/run-gate.js --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' plugin_tests`
   (the new `[iterum][ui]` cases, the `[format]` cases of 9.3, the editor lifecycle and the shared-views cases).
5. `node tools/gen-screenshots.js iterum --out specs/iterum-ui-redesign/shots/after`, then compare with `shots/before`.
   The screenshots render default parameter values, so the Custom-pattern state of MultiTap (Snap, Reset, editor
   visible; Copy hidden) and the Freeze variants other than Euclidean are checked in the visual review by setting the
   parameter, not by the default shot.
6. `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Iterum.vst3`.
7. `node tools/lint-plugin-editor-baseline.js` (one UpdateBanner last, one CheckForUpdatesButton, one VersionLabel).

### 9.5 Tool changes (`tools/uidesc-inventory.js`, Node, test-first in its `--self-test`)

| Change | Where | State / why |
|---|---|---|
| Exempt the update banner from the overlap rule | the overlap loop, line 96: a pair is skipped when either view's `customViewName === 'UpdateBanner'`. Nothing else is exempt; `--check` still exits 1 on any defect | **Done (2026-10-28)**, self-test cases "banner overlay is not a defect" and "other overlaps still are". 2.2 stacks the banner CView on the header container by design, and `stacked` only covers two containers at one origin; `inventory-before.json` lists the pair, so without this the gate could never pass |
| `lost-control` check | `interactiveTags()` (line 146) and `compareToBaseline()` (lines 151-163): from the baseline, every control-tag carried by a view whose class is not `CParamDisplay` and not `CTextLabel`; for each, at least one such view now, else `{ kind: 'lost-control' }` | **Done (2026-10-28)**, self-test cases "readout alone is a lost control", "a different widget keeps the control", "a fully removed tag is lost twice over". The `*NoteValue` exemption the first draft asked for is unnecessary: those seven tags have no view at all in the baseline, so they are never in the baseline's interactive set. `bindings()` is a set of tag names, so a readout alone would keep a tag "bound" after its slider was dropped; the frozen rule is that every parameter that has a control today keeps one (7.11) |
| Exempt the bipolar centre tick from the overlap rule | `buildInventory` records `drawValueFromCenter: v['draw-value-from-center'] === 'true'` per view; the overlap loop skips a pair when one view is a childless `CViewContainer` of width 1 whose rect lies inside the other view's rect, and the other is a `CSlider` with `drawValueFromCenter`. Nothing else is exempt: a tick beside a unipolar slider, a tick wider than 1 pt, or a tick that pokes out of its slider is still an overlap | **To do, before the first bipolar panel stylesheet (9.4 step 1)**, with two self-test cases: "centre tick over a bipolar slider is not a defect", "a tick over a unipolar slider still is". Ruling 7 puts a 1 pt `CViewContainer` over every bipolar track (4.4); without the exemption every bipolar cell is an overlap defect and the gate of 9.4 cannot pass |
| Baseline unchanged | `inventory-before.json` is not regenerated | it is the record of the before state; the exemptions apply when the baseline's own defect list is compared, so the exempt pairs simply no longer count |

---

## 10. Rulings (2026-10-28)

The user's rulings on the four open questions of the design stage and on three unresolved challenges. Each ruling
names what it changed in this document and in the panel specs; the open questions they answer are closed.

1. **Dark theme.** Section 5.1 stands as designed; the 5.2 tokens are final. `kIterumLightButtonColors` is deleted
   (9.3, "Drop the light button triple"); `PresetBrowserButton` and `SavePresetButton` take the default
   `OutlineButtonColors`. Nothing in the layout changed.
2. **Fixed window 940 x 608, min = max.** Section 2.1 stands: no `autosize` on the Editor template, no resize range.
   The Editor stylesheet of 9.2 emits `minSize="940, 608" maxSize="940, 608"`.
3. **Knobs for Time, Feedback, Mix (and Gain); sliders for every other continuous parameter.** Sections 4.1 and 4.3
   stand; principle 2 is final. The ten core strips are knob cells (or the select cells of 4.2 where the mode has no
   time knob / no feedback); the Digital Wavefold Symmetry CKnob becomes a bipolar slider (7.6).
4. **Tape: the `kTapeMotorSpeedId` control is labelled "Time".** 6.2 and 7.4 stand; the Tape spec no longer offers
   "Motor Speed" as an alternative.
5. **Spectral: STEREO is the standard pinned cell (604,288,284,180), not a tall column-3 box; Width sits where it sits
   in every other mode.** Changed: 2.4 (the pin rule no longer has a tall variant), 3.2 #5, 3.3 "width", 6.2 "Width",
   7.2 (header "2 tall sections + STEREO", row 210 at (604,288,284,180) row 24, row 209 Spread Curve "128 full"),
   9.1 (SpectralPanel line), the first bullet of 11; `panels/spectral.md` section 5 no longer "supersedes" anything.
   (604,96,284,180) in Spectral is plain panel background, as the pin leaves cells empty in Granular and Digital.
   The Width slider is at panel y 334 in all four modes that have it.
6. **MultiTap: Snap label, Snap menu and Reset on the CUSTOM PATTERN fieldset title line; the tap editor keeps
   852 x 140.** 4.10, 7.9 and 8.2 stand as written (title line y 0-24: Snap label (548,4,40,16), Snap menu
   (592,0,110,24), Reset (784,0,80,24); editor (12,28,852,140)); `panels/multitap.md` 2.7 and 2.8 stand.
7. **Bipolar sliders get a 1 pt centre tick (a CView behind each bipolar slider).** Changed: principle 2, 4.4 (the
   tick is a childless `CViewContainer` 1 x 20 in `text-dim` at the track's centre), 5.6, 9.2 (`bipolar-cell` emits it),
   9.4 step 1 and 9.5 (the inventory exempts it), the Granular, Spectral, Shimmer, Digital and Tape specs (one tick per
   bipolar cell: Pitch; Feedback Tilt; Pitch and Fine Pitch; Wavefold Symmetry; Head 1-3 Pan). The tick is declared
   **after** the slider and is mouse-disabled rather than behind it: `CSlider` paints its track opaque when
   `draw-back="true"` (`cslider.cpp:757-770`), so a view behind the track is never seen; drawn after the slider it
   reads as the 1 pt mark the ruling asks for, and `CViewContainer` mouse dispatch skips a view whose `mouse-enabled`
   is false (`cviewcontainer.cpp:1042-1043`), so the slider under it takes every click (section 11, "centre tick").

Everything else in this document is the design owner's decision; the decisions taken while reconciling the panel specs
are listed at the end of section 11.

---

## 11. Challenges considered (2026-10-28 review of this document)

Every challenge changed the document except where a line below says otherwise.

- **Width in four slots.** Accepted: STEREO is pinned to column 3 bottom-most (2.4). PingPong is not left as an
  exception: its column 3 is split into standard MODULATION and STEREO sections, so Width is at (604,288) in
  Granular, Digital and PingPong, and, by ruling 5, in Spectral too.
- **Freeze toggle under STEREO.** Accepted in outcome, placement revised by the reconcile stage: the toggle sits in
  core cell 3 beside Mix in both modes (see "Freeze in the core strip" below), not in the last row of CHARACTER.
- **Freeze Slice pair at opposite corners.** Accepted: the body is mirrored (2.4, 7.10, 8.2).
- **BipolarSlider is harder to use.** Accepted in outcome, corrected in one detail: the claim that BipolarSlider has
  no modifier-click reset is wrong. `CControl`'s constructor registers a view event listener (`ccontrol.cpp:23`)
  whose `viewOnEvent` (`ccontrol.cpp:33-49`) resets *any* CControl to `getDefaultValue()` on the default-value
  modifier before the view's own mouse handler runs (`cview.cpp:666-677`), so ctrl/cmd-click already works on it.
  The missing mouse wheel, the relative-only drag and the visibly different widget stand, and the fix chosen is the
  one of the companion challenge: VSTGUI's `CSlider` with `draw-value-from-center="true"` (4.4), now with the centre
  tick of ruling 7. No plugins/shared change is needed, so no scope approval is needed.
- **Copy wrapper paints black and swallows clicks.** Accepted (both reports): `transparent="true"`, Reset's slot,
  full attribute set in 8.2, rule in 4.10.
- **Tap editor shrinks.** Accepted: 852 x 140 with Snap and Reset on the fieldset title line (4.10, 7.9), confirmed by
  ruling 6. The "below the editor" option was rejected because it does not fit in 180 pt.
- **The inventory gate can never pass.** Accepted: the banner is exempted in the tool (9.5), the baseline is not
  regenerated and the gate stays strict (8.3). The "fail only on defects not in the baseline" option was rejected
  because it would let any of the 17 other before-defects survive unnoticed. The second half of the challenge, that
  every per-stylesheet check is red until the tenth panel, is answered by scoping the gate (9.4 step 2), not by a
  `--template` option: the tool reports per-template defect lines already, and `UIDESC CLEAN` is required once, at the
  end.
- **FreezeNoiseColor has 8 entries.** Accepted: `M 8`, half cell (6.3, 7.10).
- **Freeze formatter pointed at dead code.** Accepted: `handleFreezeParamChange` is the only source; Hits and
  Rotation take the current Steps; `deferUpdate()` on 1021/1022 when 1020 changes; 4.6 and 7.10 strings corrected
  (9.3).
- **Copy / Reset buttons unreadable on CTextButton defaults.** Accepted: full gradient and highlighted colour set,
  with a test (5.6, 9.3). The follow-up challenge, that `setGradient(CGradient::create(...))` leaks because the button
  stores a `SharedPointer`, is accepted: both calls wrap the gradient in `VSTGUI::owned()` and the `[multitap]` test
  asserts `getNbReference() == 1` (9.3).
- **Lost-binding check is blind to a dropped control.** Accepted: `lost-control` check (7.11, 9.5), landed.
- **Readouts: "nine modes already formatted" was false.** Accepted. Only Tape, BBD, Digital, PingPong, Reverse and
  MultiTap print units today (`tape_params.h:350-400`, `bbd_params.h:218-245`, `digital_params.h:296-348`,
  `pingpong_params.h:225-261`, `reverse_params.h:219-251`, `multitap_params.h:345-381`); Granular, Spectral and
  Shimmer print bare numbers (`granular_params.h:416-459`, `spectral_params.h:321-349`, `shimmer_params.h:293-334`),
  Gain prints `%.1f` with no "dB" (`controller.cpp:410`), Freeze formats only Mix. 6.4 now states one format standard,
  and 9.3 carries a row for every formatter that misses it, including the seconds switch for Tape, BBD and Reverse.
- **Sync state is read before the view is attached.** Accepted: the `verifyView` row of 9.3 and the
  `[iterum][ui]` retag assertions of 9.4; 4.9 and 8.2 no longer claim `delay_time_sync_controller.*` needs no change.
- **No gate instantiates nine of the ten panels.** Accepted: the required `editor_panels_test.cpp` of 9.4 step 3.

**Decisions of the reconcile stage (2026-10-28)**, taken by the design owner where a panel spec and this document
disagreed or a challenge was left open; both now say the same thing.

- **Freeze in the core strip.** The hold toggle is a live performance control shared by Granular and Spectral, and
  "the last row of CHARACTER" put it at panel (24,436) in Granular and (320,296) in Spectral. It now sits in core cell
  3 beside Mix in both modes, at cell (200,32,76,22), the same slot and attribute set as Sync in core cell 1 (3.1, 3.3,
  4.1, 6.2, 7.1, 7.2, `panels/granular.md`, `panels/spectral.md`). Granular CHARACTER keeps Texture and Timing Jitter;
  its row 128 is empty. Spectral CHARACTER keeps Feedback Tilt, Diffusion and FFT Size.
- **Readout standard (6.4).** Core time readouts switch to seconds at 1000 ms in every mode (Digital's rule), body
  millisecond readouts are whole numbers, percents carry no space, semitones one decimal with sign, cents whole with
  sign, dB one decimal. Granular's Grain Size therefore reads `"%.0f ms"` like Freeze's Grain Size and the other body
  ms sliders, Shimmer's percents lose their space and its Pitch gains a decimal, Spectral's Spread is milliseconds.
- **Units metadata stays.** Granular, Spectral and Shimmer register units strings ("%", "ms", "st", "ct", "Hz") in
  `ParameterInfo`, and so do the six modes that already print units (`digital_params.h:163-274`,
  `tape_params.h:176-302`); Gain registers "dB". The formatted string with its unit is what VST3Editor shows in a
  `CParamDisplay` (`vst3editor.cpp:252-256`) and what every host that reads `getParamStringByValue` shows; a host that
  appends the units string on its own would already double it for Digital and Tape, and none has been reported. The
  registration is frozen metadata (section 10 scope), so nothing is cleared: the three formatters simply join the six.
- **No `default-value` attribute on any bound control.** `VST3Editor` sets every bound control's default from the
  parameter's `defaultNormalizedValue` when it binds it (`vst3editor.cpp:265-287`, `c->setDefaultValue`), so the
  uidesc attribute is overwritten and the ten specs' differing values (today's 0.25, the registered 0.098, none) were
  noise. 4.1 and 4.8 now say "no `default-value`"; the specs drop the attribute and their "as today" / "registered
  default" deviations.
- **MODULATION is always a standard section at (604,96,284,180)** (2.4, 3.2 #3, 7.5, 9.1): Mod Depth and Mod Rate sit
  at the same panel coordinates in BBD, Digital and PingPong. BBD's (604,288) is plain panel background, not STEREO
  (BBD has no width parameter); the BBD spec's "as in Digital and PingPong" claim is gone.
- **BBD CHIP follows 7.5**: Era row 24 full (556), Age row 76 half L (272), the slots Era and Age have in Digital
  CHARACTER (24, 76) and Age in Tape CHARACTER (76). The spec's side-by-side layout is withdrawn.
- **Reverse FILTER rows**: Filter toggle 24, Filter Cutoff 76, Filter Type 128 (7.8 amended to the spec's order):
  Shimmer's FILTER also has its cutoff at row 76 under the row-24 toggle. The spec's Crossfade panel-absolute slider
  rect is (308,142,272,20).
- **Menus take a full cell in a 284-wide section** (4.6, 7.6 row 609): Digital's Mod Waveform is 260 wide, like
  Granular's Pitch Quantize and MultiTap's Spatial Pattern and Note Value; half cells (272) only beside another control
  in a two-column section.
- **Two-entry segments take the full cell when alone on a row** (4.5): Spectral Spread Curve (ruling 5), Freeze Slice
  Mode and Envelope Shape; the 4.5 "may use a half cell" licence is withdrawn.
- **Centre tick z-order** (ruling 7): over the track, declared after the slider, `mouse-enabled="false"`, because the
  opaque track hides anything behind it; see ruling 7 for the sources.
- **Per-stylesheet gate scope** (9.4 step 2): the gate reads the tool's per-template defect lines for the stylesheet's
  own template and tags; whole-file `UIDESC CLEAN` once at the end. No `--template` option.

## 12. After the build (2026-10-28, user rulings on the visual review)

These amend the sections above; where a panel spec under panels/ still shows the old numbers, the stylesheets in
xslt/ are the record (they regenerate editor.uidesc from the committed file exactly).

- **Tape head Level readout** (user ruling: option a). `formatTapeParam` and `loadTapeParamsToController` now use
  the processor's own mapping, -30 to +6 dB (`handleTapeParamChange`); they used -96 to +6. The DSP is unchanged, so
  the default (0.941) now reads its true +3.9 dB, and a reloaded level lands at the knob position it was saved at.
  Test: "Tape head Level readout and state load follow the processor's dB mapping" (red before, green after).
- **Multi-column gutter is 24 pt** (review item 4): half cells in a two-column (580) section are 266 wide, the right
  half at x 302 (12 + 266 + 24 + 266 + 12); this replaces the 272 / 296 of 2.x, 4.x and the panel specs. Tape HEADS:
  toggle 120, Level at 156 and Pan at 522, both 342 wide (12 + 120 + 24 + 342 + 24 + 342 + 12 = 876).
- **Section-opening toggles take the label row** (review item 5): `toggle-cell` gains `opens-section`; Shimmer and
  Reverse FILTER and Tape SPLICE set it (toggle at row y, not y + 20). Toggles that share a row with other cells (Tape
  HEADS) keep the 4.8 placement, level with the neighbouring controls.
- **MultiTap** (review items 1, 2): the Snap label on the CUSTOM PATTERN title line is opaque (`back-color="panel"`) so
  it masks the border like the Snap menu and Reset. The Copy row has a text-dim "Pattern" caption; it carries the
  button's visibility tag 921 (new control-tag name `MultiTapCopyPatternButton`, not a parameter) so the button's
  VisibilityController hides both.
- **Mode strip font** (review item 6): IconSegmentButton (plugins/shared) gains a `font-name` attribute, default
  "Helvetica", so Ruinae and Seraphis draw exactly as before; Iterum sets `font-name="Arial"`. Test:
  plugins/shared/tests/test_icon_segment_button_font.cpp.
