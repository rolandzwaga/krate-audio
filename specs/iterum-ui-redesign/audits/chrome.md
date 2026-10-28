# Iterum editor chrome audit

Scope: template `Editor` (900 x 600; min 900 x 600, max 1200 x 900), editor.uidesc lines 630-646, plus `<colors>`, `<fonts>`, `<gradients>`, `<bitmaps>`. Evidence: `specs/iterum-ui-redesign/shots/before/editor-*.png` (2x renders, so 1800 x 1200 px; coordinates below are in editor points), `inventory-before.json`, `controller.cpp`.

The inventory reports no geometry defects for template `Editor` and no stacked pairs. All ten panel overlaps and out-of-bounds reports belong to the panels, not the chrome.

## Layout map (template-absolute rects)

| Region | View | Rect (x, y, w, h) | Notes |
|---|---|---|---|
| Header | CViewContainer, bg `header` | 0,0,900,50 | autosize left right |
| Title | CTextLabel "ITERUM", `title-font` (Arial 18 bold), `accent` | 15,10,80,30 | |
| Mode strip | IconSegmentButton, tag `Mode` (2) | 100,5,790,40 | 10 segments of 79 pt, icon-and-text |
| Panel frame | UIViewSwitchContainer, bg `panel`, switch on `Mode` | 20,60,860,400 | 10 templates, each 860 x 400 |
| Dead band | nothing | 0,460,900,100 | empty in every mode |
| Footer | CViewContainer, bg `header` | 0,560,900,40 | autosize left right bottom |
| Version label | CView `VersionLabel` | 0,12,60,16 (abs 0,572) | shared view |
| Gain knob | ArcKnob, tag `Gain` (1) | 62,2,36,36 (abs 62,562) | `accent` arc, width 2, popup top |
| Gain caption | CTextLabel "Gain", `label-font` (9), `text-dim` | 100,12,30,16 (abs 100,572) | |
| Update button | CView `CheckForUpdatesButton` | 580,7,110,26 (abs 580,567) | shared view |
| Save | CView `SavePresetButton` | 700,7,85,26 (abs 700,567) | controller.cpp:697 |
| Presets | CView `PresetBrowserButton` | 795,7,90,26 (abs 795,567) | controller.cpp:686 |
| Update banner | CView `UpdateBanner` | 0,0,900,60 | last child; paints over header and the top 10 pt of the panel frame when shown |

## Control table (bound controls in the chrome)

| Tag (control-tag) | Label shown today | Canonical label | Role | Widget, size | Group | Visibility condition |
|---|---|---|---|---|---|---|
| Mode (2) | segment names "Granular ... Freeze" with icons | Mode | navigation | IconSegmentButton 790x40 | header | always visible; also drives the UIViewSwitchContainer (`template-switch-control="Mode"`) |
| Gain (1) | "Gain" (caption, 9 pt) | Gain, or "Output Gain" (role: output) | output | ArcKnob 36x36 | footer | always visible; value formatted in dB by controller.cpp:401 |
| none (custom-view-name VersionLabel) | "v0.15.2" | Version | navigation | shared view 60x16 | footer | always visible |
| none (custom-view-name CheckForUpdatesButton) | "Check for Updates" | Check for Updates | navigation | shared view 110x26 | footer | always visible |
| none (custom-view-name SavePresetButton) | "Save Preset" | Save Preset | navigation | custom view 85x26 | footer | always visible |
| none (custom-view-name PresetBrowserButton) | "Presets" | Presets | navigation | custom view 90x26 | footer | always visible |
| none (custom-view-name UpdateBanner) | banner text | n/a | navigation | shared view 900x60 | overlay | shown by the update checker only |

Controller notes for this scope:
- `Mode` is a StringListParameter (controller.cpp:176-181). `setParamNormalized(kModeId)` at 512-540 and 586-625 still carries legacy "Mode COptionMenu" lookups via `findControlByTag(frame, kModeId)`; the editor has no COptionMenu for Mode, so those lookups find the IconSegmentButton. Keep the tag and the control class compatible.
- Shared views are produced by `createSharedEditorView` at controller.cpp:791. Exactly one of each must stay (UpdateBanner last, one CheckForUpdatesButton, one VersionLabel).
- Frozen: `Gain` tag 1 and `Mode` tag 2, and the saved mode normalisation `mode / 9.0` (controller.cpp:280).

## Issues by severity

### High

1. **Mode strip draws inverted and dark in a light UI.** IconSegmentButton at 100,5,790,40 sets `selected-color="#1e1e22ff"` (a literal hex) and `unselected-color`/`highlight-color` = `slider-track` (#ccc). In every screenshot the strip body is near-black and the selected mode is the light grey segment, so the active mode reads as the dimmed one. The rest of the editor is light (`background` #f8f8f8, `header` #e8e8e8). Fix: choose a single polarity (selected = filled `accent` or `segment-selected`, unselected = `segment-normal`), using named colors only.
2. **Panel frame leaves a 100 pt dead band.** The switch container ends at y = 460 and the footer starts at y = 560 (0,460,900,100). The panels themselves use less than the 400 pt they get (Digital ends around y = 410 of 460 abs; Freeze around y = 385). In Freeze, Digital, Tape and several other modes the lower right half of the panel is empty as well. Fix: grow the panel frame to 20,60,860,490 or shrink the editor, and place controls on a common grid so every mode fills the same area.
3. **Panel frame is not resizable although the editor is.** The Editor template allows 900-1200 x 600-900 with `autosize left right top bottom`, but the UIViewSwitchContainer at 20,60,860,400 has no `autosize`. Growing the window only enlarges the header and footer backgrounds and the empty band, leaving the panel at 860 x 400.
4. **CSlider color defect (global, 83 of 83 sliders).** Every `CSlider` sets `back-color` / `value-color` / `frame-color`; VSTGUI reads `draw-back-color`, `draw-value-color`, `draw-frame-color`. Every slider draws a black track and a white fill (visible in every screenshot, for example Digital Feedback at 105,18+ and Freeze Euclidean Steps). The chrome's colors (`slider-track`, `accent`, `border`) are never applied to them. Fix by XSLT across the uidesc (rename the attributes), then use `accent` fill on `slider-track`.
5. **Update banner covers header and panel top.** `UpdateBanner` is 900 x 60 at 0,0, which is 10 pt taller than the header (50). When visible it hides the ITERUM title and the whole mode strip and the first 10 pt of the panel frame (starts at y = 60 so no overlap, but the banner has no gap to the panel). The user cannot switch modes while the banner is up. Fix: reserve a banner row in the layout (or size it to the header) so it does not hide navigation.

### Medium

6. **Section title position differs from the panel frame grid.** Every panel title (`CTextLabel`, `section-font` Arial 14 bold, `accent`) is at panel-relative 10,5,200,22. That is abs 30,65. The header title ITERUM is at 15,10 (18 pt) so two accent-colored bold titles sit at different x origins (15 vs 30) and sizes. The title text is centered, not left-aligned, inside its 200 pt box in the screenshots ("DIGITAL DELAY" starts at x = 78, "PATTERN FREEZE MODE" at x = 45), so the panel title starts at a different x in every mode. Fix: set `text-alignment="left"` and align to the fieldset left edge (x = 10).
7. **Panel titles duplicate the mode strip.** "DIGITAL DELAY" restates the highlighted Digital segment; the inconsistent wording ("PATTERN FREEZE MODE" vs "... DELAY", "MULTI-TAP DELAY" vs strip "MultiTap", "PING PONG DELAY" vs "PingPong") adds noise. Either remove the title, or reuse that row for a shared per-mode subtitle with one naming scheme.
8. **Header title box is tight and mode strip has no gap.** ITERUM label is 80 pt wide at 18 pt bold; the mode strip starts at x = 100 so there are only 5 pt of slack (the label's text is 70 pt wide in the 2x render). Localised or wider fonts would clip. Mode strip right edge is 890 (10 pt margin) while the panel frame and footer buttons use a 20 pt / 15 pt margin (frame right edge 880; Presets right edge 885): three different right margins (880, 885, 890).
9. **Footer elements are not on one baseline or grid.** Gain knob 62,2,36,36 (centre y = 20), caption at y = 12..28 (centre 20), version label at y = 12..28 (centre 20), buttons at y = 7..33 (centre 20). Centres agree, but the knob is almost the full footer height (36 of 40) and is crowded: version text "v0.15.2" ends at about x = 60, the knob starts at 62 and the caption starts at 100 (screenshot: "v0.15.2" touches the knob). Gain is the only always-visible control and is the smallest and least discoverable one.
10. **Footer button widths are inconsistent.** Check for Updates 110, Save Preset 85, Presets 90; gaps are 10, 10 (580-690, 700-785, 795-885). Check for Updates is drawn as plain text with no button frame in the screenshots while Save Preset and Presets have frames and a bevel: three buttons, two visual styles. Labels use different font sizes from the 9 pt `label-font` captions.
11. **Footer caption is tiny.** The "Gain" caption uses `label-font` at 9 pt, `text-dim` (#666 on #e8e8e8, contrast about 4.1:1, under 4.5:1 for small text). The same dim-on-light combination is used for 115 labels across the panels.
12. **Gain label abbreviation and missing value readout.** Gain has no value display; the dB value only shows in the knob popup (`popup-position="top"`).

### Low

13. **Dead palette entries.** `<colors>`: `section` (#eee), `segment-normal`, `segment-selected` and `mode-font` (Arial 12) are defined and never referenced; `sidebar` is referenced only by the 13 panel containers' fieldset or bitmap-less views, never by the chrome. `text-light` is used 15 times.
14. **Hard-coded colors bypass the palette.** Literal hex in attributes: `#1e1e22ff` (Mode strip selected-color) is not a palette color; `#2a2a2aff`, `#1a1a1aff`, `#e8e8e8ff`, `#d8d8d8ff` appear only inside the `segment-bg` / `segment-bg-selected` gradients and duplicate `header`, `segment-selected` and `text`. Add named colors and reference them.
15. **Font scale is irregular.** Five fonts exist (Arial 18 bold, 14 bold, 12, 10, 9); `label-font` 9 pt is used 122 times and `value-font` 10 pt 106 times (the 9 vs 10 pt distinction is hard to see). `mode-font` is unused (the strip uses the control's own font). `title-font` is used once. All fonts are the Arial family, so rendering differs per platform. Fix: define a small scale (title 18, section 14, label 11, value 11).
16. **No `<bitmaps>` section at all** (grep finds none); the mode icons live inside IconSegmentButton, so the chrome's only imagery is code-drawn. The panel container uses the flat `panel` color (#f4f4f4) on `background` (#f8f8f8): about 4 points of luminance difference, so the 860 x 400 panel frame barely separates from the window (visible in the screenshots as a faint rectangle). Fix: either border it with `border` or fill with a clearly different tone.
17. **Header and footer use the same color (`header` #e8e8e8)** and the same autosize behaviour, but the header has no divider line to the panel and the footer has no top border; the regions are only separated by 4-10 luminance points.
18. **Gradients are declared once and referenced 15 times each** (`segment-bg`, `segment-bg-selected`), always with the dark `segment-bg-selected` for the active state, which is the dark-selected polarity that conflicts with issue 1 on the strip.

## Cross-mode consistency check (chrome only)

- All ten templates are 860 x 400 with the title label at 10,5 (identical).
- The first fieldset row starts at y = 30 in all panels and the first fieldset left edge at x = 10, so the chrome-to-panel grid is consistent; what varies is how far down and across the content extends, which is the panels' own audit.
- Footer and header are identical across modes (they sit outside the switch container).
