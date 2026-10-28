# Audit: Freeze mode (template `FreezePanel`)

Sources: `shots/before/editor-freeze.png` (2x, default state, Euclidean pattern visible), `editor.uidesc` lines 555-634,
`inventory-before.json` (FreezePanel: 66 views, 34 bound controls, 0 geometry defects, 6 stacked pairs = the four
pattern fieldsets), `controller.cpp` 904-935 (container visibility), `freeze_params.h`, `plugin_ids.h` 278-319.
Coordinates are template-absolute `x,y,w,h` (the panel is 860 x 400 and sits at 20,60 in the editor).

## How the panel works today

- One title label `PATTERN FREEZE MODE` (10,5,200,22), three always-visible fieldsets in the left column (PATTERN,
  SLICE, ENVELOPE) and **four stacked fieldsets at (300,30)**, of which exactly one is visible, chosen by
  `FreezePatternType` (tag 1015): EUCLIDEAN (0), GRANULAR SCATTER (1), HARMONIC DRONES (2), NOISE BURSTS (3).
- Visibility is `ContainerVisibilityController` (controller.cpp 911-935). It finds each container by a child control
  carrying a given tag and toggles that child's parent. Load-bearing anchors: Euclidean = `FreezeEuclideanSteps` (1020),
  Granular = `FreezeGranularDensity` (1030), Drones = `FreezeDroneVoiceCount` (1040), Noise = `FreezeNoiseColor` (1050).
  Each anchor control must stay inside its own pattern container, and each container must hold all of that pattern's
  controls. The stacked pairs reported by the inventory are intentional, not defects.
- Thresholds are on the normalized value of `FreezePatternType` (0.166 / 0.5 / 0.833).
- Freeze has no delay-time knob, feedback or time/sync toggle (none exist in `plugin_ids.h`), so the time-knob layout
  of other modes cannot be reused. It has a Mix (control-tag `FreezeDryWet`, `kFreezeMixId` 1012).

## Control table

Roles: time, feedback, mix, width, modulation, filter, character, mode-specific, output, navigation.

| Control tag (id) | Label shown | Canonical label | Role | Widget, size, position | Group | Visible when |
|---|---|---|---|---|---|---|
| FreezePatternType (1015) | Type | Pattern | mode-specific | COptionMenu 100x22 @70,38 | PATTERN | always |
| FreezeDryWet (1012) | Dry/Wet | Mix | mix | CSlider 260x20 @20,82 + CParamDisplay 45x16 @235,64 | PATTERN | always |
| FreezeSliceLength (1016) | Length | Slice Length | time | CSlider 100x20 @20,158 + display 45x16 @70,140 | SLICE | always |
| FreezeSliceMode (1017) | Mode | Slice Mode | character | CSegmentButton 150x22 @130,158 (Fixed/Variable) | SLICE | always |
| FreezeEuclideanSteps (1020) | Steps | Steps | mode-specific | CSlider 60x20 @310,58 + display 25x16 @345,40 | EUCLIDEAN | Pattern = Euclidean |
| FreezeEuclideanHits (1021) | Hits | Hits | mode-specific | CSlider 60x20 @375,58 + display 25x16 @405,40 | EUCLIDEAN | same |
| FreezeEuclideanRotation (1022) | Rot | Rotation | mode-specific | CSlider 60x20 @440,58 + display 25x16 @475,40 | EUCLIDEAN | same |
| FreezePatternRate (1023) | Rate | Pattern Rate | time | COptionMenu 55x22 @505,58 | EUCLIDEAN | same |
| FreezeGranularDensity (1030) | Density | Density | mode-specific | CSlider 90x20 @310,58 + display 40x16 @355,40 | GRANULAR SCATTER | Pattern = Granular Scatter |
| FreezeGranularGrainSize (1033) | Grain | Grain Size | mode-specific | CSlider 75x20 @405,58 + display 40x16 @440,40 | GRANULAR SCATTER | same |
| FreezeGranularPositionJitter (1031) | PosJit | Position Jitter | modulation | CSlider 35x20 @485,58, no readout | GRANULAR SCATTER | same |
| FreezeGranularSizeJitter (1032) | SzJit | Size Jitter | modulation | CSlider 35x20 @525,58, no readout | GRANULAR SCATTER | same |
| FreezeDroneVoiceCount (1040) | Voices | Voices | mode-specific | CSlider 65x20 @310,58 + display 25x16 @350,40 | HARMONIC DRONES | Pattern = Harmonic Drones |
| FreezeDroneInterval (1041) | Intrvl | Interval | mode-specific | COptionMenu 60x22 @380,58 | HARMONIC DRONES | same |
| FreezeDroneDrift (1042) | Drift | Drift | modulation | CSlider 55x20 @445,58, no readout | HARMONIC DRONES | same |
| FreezeDroneDriftRate (1043) | Rate | Drift Rate | modulation | CSlider 55x20 @505,58, no readout | HARMONIC DRONES | same |
| FreezeNoiseColor (1050) | Color | Noise Color | character | COptionMenu 60x22 @310,58 | NOISE BURSTS | Pattern = Noise Bursts |
| FreezeNoiseBurstRate (1051) | Rate | Burst Rate | time | COptionMenu 55x22 @375,58 | NOISE BURSTS | same |
| FreezeNoiseFilterType (1052) | Filter | Filter Type | filter | CSegmentButton 240x22 @310,103 (LP,HP,BP) | NOISE BURSTS | same |
| FreezeNoiseFilterCutoff (1053) | Cut | Filter Cutoff | filter | CSlider 113x20 @437,59, no readout | NOISE BURSTS | same |
| FreezeNoiseFilterSweep (1054) | Sweep | Filter Sweep | filter | CSlider 240x20 @310,152, no readout | NOISE BURSTS | same |
| FreezeEnvelopeAttack (1060) | Atk | Attack | character | CSlider 80x20 @20,248 + display 40x16 @60,230 | ENVELOPE | always |
| FreezeEnvelopeRelease (1061) | Rel | Release | character | CSlider 80x20 @105,248 + display 40x16 @145,230 | ENVELOPE | always |
| FreezeEnvelopeShape (1062) | Shape | Envelope Shape | character | CSegmentButton 250x22 @20,294 (Lin,Exp in uidesc) | ENVELOPE | always |

All 24 parameters (1012, 1015-1017, 1020-1023, 1030-1033, 1040-1043, 1050-1054, 1060-1062) have a control today and
must keep one. Gain in the footer is the only output control outside the panel.

## Issues

### High

1. **CSlider colour defect, all 15 sliders.** Inventory indices 6, 10, 16, 19, 22, 28, 31, 33, 35, 39, 43, 45, 54, 56,
   60, 63 (and the Release slider) write `back-color` / `value-color` / `frame-color`; VSTGUI reads `draw-back-color` /
   `draw-value-color` / `draw-frame-color`. In the screenshot every slider is a black track with a white fill and no
   accent. Fix on every slider via XSLT.
2. **Readouts show the raw normalized value, not the parameter value.** Screenshot: Steps `.2000`, Hits `.2857`, Rot
   `.0000`, Slice Length `0.0955`, Atk `0.0200`, Rel `0.0500`. `formatFreezeParam` (freeze_params.h 502-516) handles
   `kFreezeMixId` only, so ids 1016, 1020-1022, 1030, 1033, 1040, 1060, 1061 fall through to the base class and print
   the normalized number (Granular, by comparison, shows "100", "10.0", "+0.0"). The uidesc cannot fix this; it needs
   controller/formatter work (the frozen set is IDs, types, ranges, defaults and state, so a formatter is allowed) or
   those displays must go.
3. **Six sliders have no readout**: PosJit @485,58, SzJit @525,58, Drift @445,58, Drift Rate @505,58, Cut @437,59,
   Sweep @310,152. Density, Grain and Attack, the same kind of parameter, do have one.
4. **Pattern selector is buried in the wrong group.** `FreezePatternType` is at 70,38 inside PATTERN together with the
   global Mix (20,82), while the controls it switches appear at 300,30 with SLICE in the reading order between. The
   group merges unrelated controls (pattern choice, output Mix) and separates related ones (pattern choice, its
   parameters).
5. **Variant fieldsets have four different heights** (EUCLIDEAN and GRANULAR 270x77, DRONES 270x82, NOISE 270x147), so
   the right column's shape changes with the pattern, and the left column does not match any of them.

### Medium

6. **Dead space.** 570-860 x 30-400 is empty in every pattern state, and so is 322-400 on the left, while controls
   are squeezed into 35-60 px sliders.
7. **Slider widths are inconsistent and some are too small**: 260, 240, 113, 100, 90, 80, 75, 65, 60 (x3), 55 (x2),
   35 (x2). The 35x20 jitter sliders @485,58 and @525,58 sit under 40 px labels at 485 and 525 that abut each other.
   Label x positions across the template: 10, 60, 75, 80, 105, 120, 140, 145, 185, 205, 225, so there is no column grid.
8. **Misalignment inside NOISE BURSTS.** `Cut` label is at 437,41 and its slider at 437,59, one pixel lower than the
   Color/Rate row (labels y=40, controls y=58). The `Sweep` label is 40x10 @310,132 while every other label is 16 high.
   Row pitch is irregular: 40 / 83 / 132 label rows (43 and 49 px).
9. **Inconsistent widget choice for the same kind of parameter.** Selectors: Pattern (4 options), Noise Color (3),
   Interval and the two note-value rates are dropdowns, Noise Filter Type (3) is a segment button, Slice Mode and
   Envelope Shape (2 each) are segment buttons of different widths (150, 250). Rates: Pattern Rate and Burst Rate are
   dropdowns, Drift Rate is a slider.
10. **"Rate" is used for three parameters** (Euclidean pattern rate @505,40, Drone drift rate @505,40, Noise burst rate
    @375,40) and the first two share a position but are a dropdown and a slider.
11. **Irregular left column.** PATTERN 280x77 @10,30; SLICE 280x62 @10,130 (23 px gap above); ENVELOPE 280x102 @10,220
    (28 px gap). Heights 77/62/102 and gaps 23/28 follow no rhythm; ENVELOPE spends 102 px on two sliders and one
    selector.
12. **Noise filter is split across the group.** Type (310,103), Cutoff (437,59) and Sweep (310,152) are three
    unrelated rows; Cutoff sits in the Color/Rate row and is 113 wide while Sweep is 240 wide. Related filter controls
    are not grouped.
13. **Abbreviations in labels**: `Atk`, `Rel`, `Rot`, `Intrvl`, `PosJit`, `SzJit`, `Cut`, `Dry/Wet`; uidesc segment names
    `LP,HP,BP` and `Lin,Exp`. The screenshot shows `Linear/Exponential` because the parameter string list overrides the
    uidesc names, so check the parameter's strings (freeze_params.h) before treating the uidesc names as what users see.
14. **Title** `PATTERN FREEZE MODE` @10,5 is flush left, whereas the Granular title is indented, so the title position
    is not shared between modes.
15. **Shared vs pattern-specific is not expressed.** SLICE and ENVELOPE apply to all patterns (`plugin_ids.h`:
    "shared across patterns" for the envelope) but are laid out as peers of the pattern selector, while the
    pattern-specific group floats alone on the right.

### Low

16. Dropdown widths (100, 60, 55) can truncate longer entries (`Granular Scatter`, interval names); 55 px only just
    fits "1/8".
17. Labels are `text-dim` on light grey and small at 100 % scale; low contrast.
18. Once sliders render properly (issue 1) they and the gradient-filled segment buttons will be two different
    control families side by side; plan a shared colour set.

## Constraints for the redesign (Freeze scope)

- Keep all 24 control-tags and the four visibility anchors (tags 1020, 1030, 1040, 1050), each inside its own pattern container.
- No time knob or feedback exists here, so a shared time-knob header cannot apply; the shared layout must tolerate a
  Mix-only output group.
- The readout defect (issue 2) needs formatter work in `freeze_params.h`, not only uidesc edits.
- The inventory `--check` must keep treating the four fieldsets at 300,30 as stacked variants.
