# MultiTap tap editor: design

Written 2026-10-29 from the four research reports (`research/code-facts.md`, `research/prior-art.md`,
`research/critique.md`, `research/dsp-and-state.md`), the six before-shots (`shots/before/`), the view
(`plugins/iterum/src/ui/tap_pattern_editor.{h,cpp}`, `tap_pattern_editor_logic.h`), the controller wiring
(`plugins/iterum/src/controller/controller.cpp`), `multitap_params.h`, `pattern_calculator.h`,
`dsp/include/krate/dsp/effects/multi_tap_delay.h`, `processor.cpp`, and the panel documents
(`specs/iterum-ui-redesign/design-system.md`, `panels/multitap.md`, `xslt/multitap.xsl`). Line numbers are those
of branch `ui/iterum-consistency` on that date. Every number is in VSTGUI points; the view is 852 x 140.
Revised the same day against 21 challenges; section 10 lists each and where it was resolved. Reconciled the same day
against the user's five rulings and the seven findings left open after the second challenge round (`open-items.md`);
section 9 lists each ruling and what it changed. Every SDK, VSTGUI and DSP claim the reconcile restates was re-read
in the source on that date.

Frozen and respected throughout: parameter IDs, types and ranges of every existing parameter; every parameter keeps a
control; saved state stays loadable, new fields go at the very end of the stream; VSTGUI only. Two parameters are
added (section 7); nothing existing changes type, range or ID.

Abbreviations: CF = code-facts.md, PA = prior-art.md, CR = critique.md, DS = dsp-and-state.md, TPE =
`tap_pattern_editor.cpp`, TPL = `tap_pattern_editor_logic.h`, CTL = `controller.cpp`, MTP = `multitap_params.h`,
DSP = `multi_tap_delay.h`, PROC = `processor.cpp`, TM = `tap_manager.h`.

---

## 1. Problems addressed

The two complaints that started this are the floor. Everything else below removes a problem one of the reports
names; nothing is added on speculation.

| # | Problem | Evidence | Fixed by |
|---|---|---|---|
| P1 | Raising Taps stacks the new taps (0.75, 0.75, 1.0, 1.0 ...); nothing visibly changes; stacked taps cannot be grabbed | CF M1/S1, CR H1, `custom-8taps.png`, `custom-16taps.png` | section 3: a tap revealed by a user gesture keeps its stored place when that place is free, else moves to the nearest free grid line below the 5 s ceiling; registered defaults fill the grid coarse to fine (quarters, then odd eighths, then odd sixteenths, ruling 3), so every fresh or revealed tap is distinct and on the default grid or a finer one |
| P2 | The grid does not show the selected timing: background lines are fixed quarters, the ruler has no labels, "1/4" is 1250 ms at every tempo, dotted values are rounded, Snap Off looks like 1/4, several Snap entries are duplicates | CF M4/S2/S3/S4, CR H2, DS H-2, all six shots | section 2: a musical time axis, exact note lengths, labelled ruler, Snap ticks drawn in their own colour |
| P3 | A saved Custom pattern cannot follow the song's tempo; the one musical-looking pattern is a free-time delay | CR H2, DS section 0 | section 2: a Pattern Length parameter; synced lengths follow the host tempo, Free keeps today's 5 s axis |
| P4 | Shrinking then growing the tap count overwrites the taps you placed; growth also rewrites taps in non-Custom patterns and on preset load, and the sound depends on whether the editor was open | CF H2/M2/S7, CR H1/H5 | section 3: decrease writes nothing; increase reveals the stored taps; placement only for the taps one user gesture revealed |
| P5 | Five different "default position" rules; Reset puts tap 1 at 1 ms and the last at the clipped right edge | CF M3/S6, CR M5 | section 3: one ideal position, D(i, n) = (i+1)/n of the playable length, used by Reset, double-click and placement; double-click and placement never land on another tap. The registered defaults of a fresh instance (quarters, odd eighths, odd sixteenths, ruling 3) are a second, count-independent rule; they equal Reset at the default 4 taps and differ from it at other counts (3.1) |
| P6 | No numbers anywhere: no readout while dragging or hovering, no ruler labels; host lanes show Custom Time as "%" | CF S9, CR H4, PA H2 | sections 2, 4, 5: popup readout (grid position, ms, dB), ruler labels, host format "750.0 ms" / "-6.0 dB" |
| P7 | The level axis is linear gain: -12 to -30 dB share the bottom quarter; a level-0 tap draws over the ruler and cannot be grabbed | CF S10/H5, CR M2, `custom-8taps-levels.png` | section 5: a 0 to -48 dB axis with a silence band; labels in dB; every tap, silent ones included, can be moved in time and level (4.1) |
| P8 | Grabbing a tap moves it: absolute-position drag, snap on the first move, cursor says vertical while the handle also moves time | CR H3, CF M10 | section 4: delta drag, dead zone, handle = level only, body = time only; a tap too short for both has one grip whose axis is set by the first move |
| P9 | No edit gesture reaches the host for any write from the view (tag 920 is not a parameter); two `performEdit` per mouse move even when nothing changed | CF H4/S12, CR M1 | section 3.5: begin / perform / end for every path, perform only on change |
| P10 | Taps at ratio 0 and 1 draw half clipped; the left edge is a 1 ms delay; tap numbers sit at the bar's foot and vanish in stacks | CF M7/M8/S8/S14/S15, CR M3/L4/L5 | section 5: the plot is inset by a bar width; numbers above the bar; stacked labels merge; ratio 0 is never a snap or placement target (2.2) |
| P11 | Copy to Custom copies as if the tap count were 2, scales the last tap to 5000 ms, drops the levels | CF H3/M6, CR M4, DS H-1 | section 2.6: Copy reproduces the times and levels that play at the current tempo, computed in milliseconds exactly as the DSP clamps them; a synced Length when every tap lies within 8 beats and no clamp engages at that tempo, Free otherwise |
| P12 | The view never learns new time/level values after creation (automation, undo, preset load); a MultiTap-to-MultiTap preset load orphans the controller's view pointer | CF M9/H1, CR H5 | section 3.6: a parameter-dependent bridge pushes every change to the view synchronously (on the UI thread, where the VST3 contract puts `setParamNormalized`), looking the view up by tag each time; a bulk load pushes once, after the load |
| P13 | Morphing into Custom heads for n x baseTime and then jumps | CF M5, DS M-1 | section 2.5: the processor pushes the custom values before it detects the pattern change, and `calculatePatternTimes` gets a clamped Custom case |
| P14 | 512 `applyCustomTimingPattern` loop bodies per block | CF L2, DS L-1 | section 2.5: setters only store; `process()` applies the custom pattern exactly once per block under Custom (16 loop bodies) |
| P15 | Colours and fonts are hard-coded literals and 8/9 pt Arial; the smallest editor text is 11 pt | CF M8/S11, CR L1 | section 5.7: tokens read from the uidesc, `value-font` |
| P16 | Shift and Ctrl contradict the CSlider conventions; Escape never reaches the view (it never takes focus) | CR M6 | section 4: Shift = fine adjust and snap bypass; Ctrl/Cmd-click resets a tap's level to 0 dB, the reset gesture of every other Iterum control (ruling 5); the view takes focus on press |
| P17 | The lower half of the panel is empty for 19 of 20 patterns; nothing shows what "Golden Ratio" does | CR M9, CF L1, user critique 5, design-system principle 6 | section 5.9: the display shows every pattern, read-only unless Custom, on the axis Copy would give it |
| P18 | A black frame around the "Snap" label | CR L2 | section 6: `style-no-frame="true"` on the title-line labels |
| P19 | Inactive taps are reset locally but not in the parameters | CF L3 | section 3.4: Reset touches active taps only, params and view alike |

**Left alone, and why**

- Per-tap pan (Spatial "Custom" has no editor, CR L6): no pan parameters exist; adding sixteen is a feature, not a fix.
- Typed entry, keyboard nudge, multi-select, rubber-band, value-curve drawing, right-click menus, random / ramp /
  evenly-space actions (PA H3, H4, M2): nothing in the critique or the shots fails because they are missing. Shift
  (section 4.2) is both fine adjust and a snap bypass for the drag, so a flam, a push or a swung echo can be placed off
  the grid with Snap left on; that is the precision CR M6 asks for. The rest is prior art's feature set, not a defect
  of Iterum's.
- Adding or deleting taps by clicking the background (CR M8): Iterum's model is count-driven like Steinberg's and
  Output's (PA M1); the Taps slider stays the one way to change the count, and with P1/P4 fixed it works.
- Taps numbered by index, Cascade pans by index (CR M7): renumbering by time would change which tap a host
  automation lane edits; the numbers stay, and section 5.6 keeps them readable. With the ruling-3 defaults the
  numbers of a fresh pattern above 4 taps do not run left to right either (3.1 states that cost).
- The 1 ms minimum at ratio 0 (`kMinDelayMs`, DSP:167): ratio 0 is excluded from every snap and placement grid (2.2),
  so it is reachable only by an unsnapped drag (Snap Off or Shift) to the left edge; the readout then says "1.0 ms".
- Rhythmic patterns re-read the tempo only while playing (DS L-2): a pre-existing rule of the preset patterns,
  untouched. The custom path reads the tempo in every block (section 2.5).
- Under Custom the custom levels override the Decaying and Flat spatial levels (CR M4 last bullet; the processor
  calls `applySpatialPattern` every block, PROC:726, and the custom apply follows it). That order is kept exactly by
  the once-per-block apply of 2.5, and the editor shows the custom levels, which is what plays. For the other 19
  patterns the spatial level rule does apply, and the read-only display and Copy include it (2.6, 5.9).
- `kMaxDelayMs` stays 5000: raising it would move every mathematical pattern that clamps at 5 s today (Primes with
  16 taps, the factory preset Prime_Numbers), which the state ruling forbids.
- `tools/preset_generator.cpp` and the factory `.vstpreset` files (section 7.2): the generator already omits the
  Tape sync tail, so appending a Length field alone would be read as Tape's time mode; it is left alone.

---

## 2. Time axis

### 2.1 Model: a Pattern Length, synced by default; Free is the legacy axis

Of DS's options, A (keep 5000 ms, draw a tempo grid) leaves P3 unsolved and needs the tempo before the grid can be
right at all; B (tempo-synced span) solves P3 and makes Copy exact and tempo-free, but needs a flag to keep old
patterns; C-bars with "Free" as index 0 is B with the flag folded into one parameter. The design takes the last one.

**New parameter `MultiTap Custom Length`, ID 914 (`kMultiTapCustomLengthId`), StringList, 6 entries, default
index 4 (`1/1`, ruling 1: every tempo-capable mode of this release starts synced), entries per ruling 4:**

| Index | String | Span | At 120 BPM | Exceeds the 5 s ceiling below |
|---|---|---|---|---|
| 0 | Free | 5000 ms, tempo-independent (today's axis) | 5000 ms | never |
| 1 | 1/8 | 0.5 beats | 250 ms | 6 BPM (never; tempo is clamped to 20) |
| 2 | 1/4 | 1 beat | 500 ms | 12 BPM (never) |
| 3 | 1/2 | 2 beats | 1000 ms | 24 BPM |
| 4 | 1/1 | 4 beats | 2000 ms | 48 BPM |
| 5 | 2/1 | 8 beats | 4000 ms | 96 BPM |

"Beats" means quarter notes, exactly as `getBeatsForNote` counts them (`note_value.h:64-101`); no time signature is
read, "1/1" is one whole note (4 beats), the same reading the Tape and Digital note menus use. 4/1 is not offered
(ruling 4): it exceeds the ceiling below 192 BPM.

**Meaning of the stored ratios.** Tap i plays at `ratio_i x span`. In Free the span is 5000 ms, so every pattern
saved before this change (always Free, section 7) plays the same milliseconds as today. In a synced length the span
follows the host tempo, so the pattern follows the song: a tap at ratio 0.25 of 1/1 is one beat, at any tempo.

**x = 0** is the input (the note being delayed); **x = 1** is the end of the length: 5000 ms in Free, a whole note
later (1/1), two whole notes later (2/1), and so on. Every number on the axis is a **delay counted from the input**:
the ruler counts beats of delay (2.2), the readout counts snap units of delay (4.3), the host counts milliseconds of
delay (4.4). Changing Length rescales the whole pattern (ratio 0.5 of 1/1 is 2 beats; of 2/1 it is 4 beats); the taps
jump through the 20 ms per-tap smoother, there is no morph (DS M-1). That is the "stretch" behaviour DS section 4
describes, and the Length menu is the only thing that triggers it.

**The playable fraction.** `c = min(1, 5000 / spanMs)` at the view's tempo: 1 in Free and whenever the span fits
under the ceiling, below 1 for 1/1 under 48 BPM and 2/1 under 96 BPM. Taps right of `c` play at 5000 ms. Reset,
double-click and placement (section 3) only use positions in `(0, c]`.

### 2.2 Grid and ruler

All grid positions are computed by one pure function `gridLines(length, snap, bpm)` in TPL, which returns the lines
as ratios with a kind each; the drawing, the snap and the placement of section 3 all use it, so the three can never
disagree (today they do: CF M4).

- **Beat lines** at every quarter note, **bar lines** at every whole note, from ratio 0 to 1. Synced: beat k is at
  `k / spanBeats`. Free: beat k is at `k x 60000 / bpm / 5000`, so in Free the musical grid moves with the tempo while
  the taps stay in milliseconds (that is what Free means, and the ruler says ms). Beat and bar lines are drawn only in
  the plot; in Free the ruler carries ms ticks only.
- **Snap lines.** Snap index s (1..21) is note-value dropdown index s-1 (`kNoteValueDropdownMapping[s-1]`,
  `note_value.h:136-164`: 1/64T .. 1/1D in the same order as the Snap menu, MTP:199-213), so the unit in beats is
  `u = getBeatsForNote(mapping.note, mapping.modifier)`: 1/8D is 0.75 beats, 1/4T is 0.667 beats, exactly, never a
  rounded column count. The snap lines are the multiples `k x u` with `0 < k x u < spanBeats - 1e-6`, **plus ratio 1
  always** (the end of the length is where D(n-1, n) and Copy put the last tap, so it must be reachable when the unit
  does not divide the span, e.g. 1/8D or 1/4T over 1/2). **Ratio 0 is never a snap line** (it is a 1 ms delay).
  Dotted units do not divide the span evenly and the grid shows exactly that.
- **A unit too long for the length.** When that set holds fewer than two lines, which happens exactly when the unit
  is as long as the span or longer (`u >= spanBeats`: over 1/8 every unit from 1/8 up, 1/8D, 1/4T and the default
  1/4 included; over 1/4 every unit from 1/4 up; over 1/2 from 1/2 up; over 1/1 Snap 1/1 and 1/1D; never over 2/1; in
  Free a unit of 5 s or more at the current tempo), the **effective unit** falls back to 1/16 (0.25 beats).
  The span is at least 0.5 beats, so 1/16 always yields at least two lines (1/8: 0.5 and 1.0). The fallback lines are
  the ones drawn, snapped to and used for placement, so what is drawn is what snaps; the readout names the effective
  unit (4.3). Example: Length 1/8 with the default Snap 1/4 snaps to ratio 0.5 and 1.0 (the two sixteenths), never 0.
- **Snap Off** draws no snap lines and no snap ticks; nothing quantises.
- **Culling** (display only; snapping uses every line): if the pitch between consecutive snap lines is below 4 px,
  draw every k-th line, k = ceil(4 / pitch). 1/64T over 2/1 is 192 lines at 4.2 px, nothing culled; Free at 300 BPM
  with 1/64T is 1.3 px, so every 4th line is drawn.
- **Over the ceiling** (`c < 1`): the plot right of ratio `c` is filled with `background` at 50 % opacity and a
  `text-dim` line is drawn at `c`, with the ruler label "5 s" (it takes priority over any beat label within 4 px of
  it). Taps beyond it are drawn in `text-dim`: they all play at 5000 ms, which is what the DSP does with them (and
  did with every pattern before).
- **Ruler** (y 114..140 of the view). A baseline at y 114 in `slider-frame`. Tick row y 114..122, every tick hanging
  from the baseline and none leaving the row: bar ticks 8 px (114..122) and beat ticks 5 px (114..119) in `text-dim`
  (synced only); snap ticks 3 px (114..117) in `accent`, drawn last, so they show over a coinciding beat tick (this is
  what makes Snap 1/4 look different from Snap Off, 5.3). Label row y 122..138, `value-font`, `text-dim`. Each label
  has one defined slot:
  - **synced**: beat line k (k = 1 .. spanBeats) carries the label "k", the number of beats of delay (for 1/1:
    "1 2 3 4", the "4" at ratio 1; for 2/1: "1 .. 8"). Ratio 0 has no label (it is the input). A label's left edge
    sits 2 px right of its line, except the label at ratio 1, whose right edge sits 2 px left of the line (x <= 842).
    For Length 1/8 (half a beat) the labels sit at the two sixteenth lines: "0.25", "0.5".
  - **Free**: a 3 px tick every 100 ms and an 8 px tick with a label every 500 ms: "0.5", "1.0", ... "4.5", and
    "5.0 s" at ratio 1 (the unit on the last label only), placed by the same left / right-aligned rule.
  - A label is skipped when its box would come within 4 px of a label already placed; the end label and the "5 s"
    ceiling label are placed first, so they always win.
  - **Tempo**: in the gutter under the level labels, x 0..34, right-aligned at x 30: "120" (`"%.0f"`, y 116..127)
    above "BPM" (y 127..138), `text-dim`. It never shares the ruler's right end.
  - The Length string is not drawn: the Length menu on the title line already shows it.

### 2.3 What the Snap menu now means

Snap quantises a dragged tap's time to the snap lines of 2.2 (synced: a note length; Free: that note length at the
current tempo; in both, ratio 1 always included and ratio 0 never, and the 1/16 fallback when the unit is too long),
and the same lines are the placement grid of section 3. Holding Shift during a drag bypasses it (4.2). Snap Off
disables both the quantising and the snap lines. The parameter, its 22 entries and its default (1/4) are unchanged;
only the lines it names move from fractions of 5 s to note lengths. Taps are never re-snapped when Snap or Length
changes.

### 2.4 How the tempo reaches the view, and what it shows before the first value

The controller has no tempo today (DS section 5). The design uses the SDK's own mechanism for processor-to-controller
values, the one the `again` sample uses for its VU meter (`public.sdk/samples/vst/again/source/again.cpp:269-281`),
as ruling 2 chose (no DataExchange block): a read-only output parameter written through
`data.outputParameterChanges`, which the host forwards to `IEditController::setParamNormalized`. That call is marked
`[UI-thread & Connected]` (`ivsteditcontroller.h:521-525`), and the design relies on that contract: the bridge of 3.6
delivers the value to the view synchronously, inside the call, with no queue of its own. It allocates nothing on the
audio thread, needs no `connect`, no message, no timer and no run loop (the Linux harness caveat of DS section 5 does
not apply), and a late-opening editor reads the last value from the parameter.

**New parameter `MultiTap Tempo`, ID 915 (`kMultiTapTempoId`)**: plain `Parameter`, flags
`kIsReadOnly | kIsHidden` (`ivsteditcontroller.h:80,92`), no `kCanAutomate`, units "BPM", normalized
`(bpm - 20) / 280` over the 20..300 BPM range `noteToDelayMs` clamps to (`note_value.h:200-203`), default 120 BPM
(normalized 0.357142857). Not written to the state stream. No control in the uidesc (it is hidden and read-only; the
"every parameter keeps a control" ruling is about the parameters the user had controls for).

**Processor** (`Processor::process`, before the mode switch, in every mode): `tempo = ctx.tempoBPM` (the 120
fallback of PROC:192 applies when the host gives none). When `|tempo - lastSentTempo_| >= 0.05` and at least 0.05 s of
samples have elapsed since the last send (a sample counter, no clock), or on the first block after `setActive(true)`
(`lastSentTempo_ = -1`), add one point to `outputParameterChanges` for 915 as `again.cpp` does. A tempo ramp therefore
sends at most 20 updates per second.

**Controller**: the bridge of 3.6 forwards `20 + v x 280` to the view (`setTempo`) and, for a non-Custom pattern,
recomputes the read-only display (5.9). **Before the first value arrives the view uses the parameter's default,
120 BPM, which is the same fallback the processor plays with until the host provides a tempo**; the gutter shows
"120 BPM" and corrects on the first processed block. No "unknown tempo" state exists: a synced grid is correct in
beats whatever the tempo, so only the ms readouts, the ceiling marker and Free's musical lines depend on the value.

### 2.5 The exact DSP formula

In `MultiTapDelay` (DSP):

```
enum class CustomLength : uint8_t { Free = 0, N8, N4, N2, N1, N2x1 };
static constexpr float kCustomFreeLengthMs = 5000.0f;        // the legacy scale, a literal, not maxDelayMs_ (DS H-3)
static constexpr float kCustomLengthBeats[6] = {0.f, 0.5f, 1.f, 2.f, 4.f, 8.f};

CustomLength customLength_ = CustomLength::Free;               // the DSP's own default; the processor pushes 914
double customTempoBPM_ = 120.0;                                // until the first block with ctx.tempoBPM > 0

float customSpanMs() const noexcept {
    if (customLength_ == CustomLength::Free) return kCustomFreeLengthMs;
    const double bpm = std::clamp(customTempoBPM_, kMinTempoSyncBPM, kMaxTempoSyncBPM);
    return static_cast<float>(kCustomLengthBeats[int(customLength_)] * 60000.0 / bpm);
}

void applyCustomTimingPattern() noexcept {                     // unchanged except the span
    const float span = customSpanMs();
    for (i < activeTapCount_) {
        timeMs  = std::clamp(span * customTimeRatios_[i], kMinDelayMs, maxDelayMs_);
        levelDb = (customLevels_[i] > 0.001f) ? gainToDb(customLevels_[i]) : -96.0f;   // as today, DSP:861-863
        tapManager_.setTapEnabled(i, true); setTapTimeMs(i, timeMs); setTapLevelDb(i, clamp(levelDb, -96, 6));
    }
    for (i in [activeTapCount_, kMaxTaps)) tapManager_.setTapEnabled(i, false);
}
```

**The application rule, exactly.** There is no dirty flag.
- `setCustomLength`, `setCustomTimeRatio` and `setCustomLevelRatio` **only store**; they never apply (P14).
- `process()`: first `customTempoBPM_ = ctx.tempoBPM` whenever `ctx.tempoBPM > 0`, in every transport state (DS L-2).
  Then, **after** `updateMorph` (DSP:639-641), `if (currentTimingPattern_ == Custom && !morphing_)
  applyCustomTimingPattern();` **in every block**, unconditionally. That is 16 loop bodies a block instead of today's
  512 (the 32x saving of P14), and it keeps today's order of application exactly: the processor calls
  `applySpatialPattern` before `process()` in every block (PROC:726), Decaying and Flat rewrite every tap's level
  there (DSP:928-940), and the custom apply that follows puts the custom levels back, as the 32 per-block setter calls
  do today. A saved Custom pattern with Spatial = Decaying or Flat therefore keeps its custom levels in every block.
- **Morph into Custom.** `currentTimingPattern_` becomes Custom only when the morph completes (`updateMorph`,
  DSP:1050-1056, which sets times only). Because the apply runs after `updateMorph` in the same block, the block in
  which the morph completes also applies the custom levels. During the morph the levels are those of the pattern being
  left (with its spatial levels), as today.
- `setBaseTimeMs` (DSP:350-359) no longer re-applies for Custom (the per-block apply covers it; DS M-2: no double
  apply). `loadTimingPattern(Custom, n)` still applies immediately through `applyTimingPattern`'s Custom case.
- `calculatePatternTimes` gains `case TimingPattern::Custom: timeMs = std::clamp(customSpanMs() *
  customTimeRatios_[i], kMinDelayMs, maxDelayMs_);`, so `morphToPattern(Custom)` glides to the custom shape (P13) and
  never toward 0 ms. The target uses the tempo of the previous block; a tempo change inside the morph block is
  corrected by the first per-block apply after completion, through the 20 ms smoother.
- Free reproduces today's `clamp(5000 x ratio, 1, 5000)` bit for bit: `kCustomFreeLengthMs` equals the prepared
  `maxDelayMs_` (PROC:96).

**Processor** (MultiTap branch of `Processor::process`): push the 16 time ratios, the 16 levels and the Length into
the DSP **in every block, before the pattern-change detection** (PROC:697-724), **whatever the current pattern**. The
`currentPattern == 19` guard of PROC:732 goes; the setters only store, so pushing under another pattern changes
nothing audible. Today the push comes after `morphToPattern(Custom)` (PROC:719) and only while the pattern is
already 19 (PROC:732-741), so on the block of the switch the morph target is computed from stale ratios (on a fresh
instance the zero-initialised `customTimeRatios_`, DSP:1138, a 0 ms target). With the push first, both
`morphToPattern(Custom)` and the immediate `loadTimingPattern(Custom, n)` of a simultaneous tap-count change read
the current values.

### 2.6 Copy to Custom

`pattern_calculator.h` is rewritten to mirror the DSP **in milliseconds at a given tempo, with every clamp the DSP
applies, in the order it applies them** (finding 5), for all 16 indices, with levels:

```
struct PatternTap { float ms; float beats; float levelDb; };   // ms: what plays; beats: the unclamped musical value
PatternTap patternTap(int timingPattern, int noteValueIdx, int noteModifierIdx, size_t i, size_t n,
                      double bpm) noexcept;
bool patternClampEngaged(int timingPattern, int noteValueIdx, int noteModifierIdx, size_t n, double bpm) noexcept;
float playedLevelDb(float patternLevelDb, int spatialPatternIdx, size_t i) noexcept;
```

The tempo is first clamped to [20, 300] BPM as the DSP does (`bpm_ = clamp(bpm, kMinTempo, kMaxTempo)`, DSP:451;
`noteToDelayMs` clamps the same range, `note_value.h:200-203`), `q = 60000 / bpm` ms per beat.
- Rhythmic patterns 0-13: base `L_ms = clamp(L x q, 1, 5000)` (DSP:745, and DSP:1002 for the morph target), then
  `ms = min((i+1) x L_ms, 5000)` (TM:664-667 and 705, `loadPatternWithBaseTime` QuarterNote), L = 4, 2, 1, 0.5, 0.25,
  0.125, 3, 1.5, 0.75, 0.375, 4/3, 2/3, 1/3, 1/6 beats (the multipliers of DSP:686-742). `beats = (i+1) x L`.
- Mathematical patterns 14-18: base `B = clamp(noteToDelayMs(note, modifier, bpm), 1, 5000)` from Note Value and
  Modifier (911/912), the clamp of DSP:628-635 (line 631), applied **before** any ratio. Golden: `ms_0 = B`, `ms_i =
  min(ms_(i-1) x phi, 5000)` (TM:683-688, a chain over the clamped previous tap); Fibonacci `min(fib(i+1) x B x 0.25,
  5000)` (TM:694, 705); Exponential `min(B x 2^i, 5000)` (DSP:802-807); Primes `min(B x primeRatios[i], 5000)`
  (DSP:817-825); Linear `B + i x (maxT - B) / (n - 1)` with `maxT = min(B x n, 5000)` (DSP:835-848: the spread is
  clamped at its end, not per tap, so with B x n above 5 s every tap from the second moves earlier). `beats` is the
  same formula on the unclamped base in beats with no clamp anywhere. Example (the case the old formula got wrong):
  Linear, Quarter, 16 taps, 90 BPM: B = 666.7 ms, maxT = 5000, tap 2 = 666.7 + 4333.3 / 15 = 955.6 ms, not 1333 ms.
  Example of the base clamp: Note Value Whole at 30 BPM: raw B = 8000 ms, B = 5000; Primes tap 1 = 5000 x 0.5 =
  2500 ms, Fibonacci taps 1-2 = 1250 ms, Linear every tap 5000 ms (maxT = 5000 = B, step 0).
- `patternClampEngaged` is true when any active tap's `ms` differs from `beats x q` by more than 0.01 ms, i.e. when
  any of the clamps above changed what plays at this tempo.
- The 911/912 index tables now live in one function `multiTapNoteValueFromDropdown(noteIdx, modifierIdx)` in
  `multitap_params.h`, used by PROC:672-694 and by the calculator, so the two cannot drift.
- **Level of the timing pattern**: `-3 dB x i` for every pattern 0-18. The rhythmic patterns get it from
  `TapManager::loadPatternWithBaseTime` (TM:703), the mathematical ones from their own loops (DSP:802-848).
- **Level that plays** (`playedLevelDb`): the processor applies the spatial pattern after the timing pattern in every
  block (PROC:726), so Spatial 4 Decaying gives `-3 dB x i`, Spatial 5 Flat gives `0 dB`, and the pan-only spatial
  patterns 0-3 and 6 leave the timing pattern's level. The mirror test below checks this against the DSP for every
  spatial pattern.

`Controller::copyCurrentPatternToCustom` (bpm = the Tempo parameter 915, the tempo the processor last reported):
1. Count n decoded as `int(2 + norm x 14 + 0.5)` (P11; CTL:1137 used `toPlain` on a plain `Parameter`).
2. `maxBeats = max over i < n of patternTap(p, ..., i, n, bpm).beats`; `clamped = patternClampEngaged(p, ..., n,
   bpm)`.
3. **If `maxBeats <= 8` and `!clamped`**: Length = the smallest synced entry whose beats >= maxBeats; `ratio_i =
   beats_i / spanBeats`. Because no clamp is engaged, `beats_i x q` is what plays, so the copy plays what the source
   plays at the current tempo and keeps following the tempo as the source does (until a tempo at which the source
   would clamp; the source then clamps by its own rules and the copy by `clamp(span x ratio, 1, 5000)`, which agree for
   every pattern whose clamp is per tap, the rhythmic ones, and may differ for Linear and the base clamp).
   **Else** (some tap later than 8 beats: Quarter above 8 taps, Whole above 2, Dotted Half above 2, Golden, Primes and
   Exponential at higher counts; or any clamp engaged at this tempo: a tap at 5 s, Linear with B x n above 5 s, a base
   above 5 s): Length = **Free**, `ratio_i = patternTap(...).ms / 5000` (ms already lies in [1, 5000]). This is exact
   at the current tempo by construction, including the taps the source plays at 5 s, which stack at ratio 1 exactly as
   they stack in the source (and in the read-only display before Copy); the copy **no longer follows later tempo
   changes**. Beats are used only to choose a synced Length, never to compute a time that plays.
4. `level_i = clamp(dbToGain(playedLevelDb(patternTap(...).levelDb, spatial, i)), 0, 1)`.
5. Writes, each as begin / perform / end on `componentHandler` and `setParamNormalized` on the controller: the n
   active times, the n active levels, Length, then Timing Pattern = Custom (2n + 2 ids). Hidden taps (i >= n) are not
   written: they keep their stored values, as every other path keeps them (3.2). The view is refreshed through the
   bridge of 3.6, not by hand.

Under Custom the custom levels override the spatial levels (2.5), so writing the levels that played keeps the sound
of every tap across the Copy, whichever spatial pattern is set. Worked examples at 120 BPM, Spatial Centered:
Quarter with 6 taps -> Length 2/1, ratios 0.125 .. 0.75, levels 0, -3 .. -15 dB. Quarter with 16 taps -> Free, ratios
0.1, 0.2 .. 0.9 and 1.0 for taps 10-16 (4500 ms, then 5000 ms, as the source plays). Whole with 4 taps -> Free, 0.4,
0.8, 1.0, 1.0. Golden with 8 taps, Note Value 1/4 -> Free, 0.1, 0.1618, 0.2618, 0.4236, 0.6854, 1.0, 1.0, 1.0.
Linear with 4 taps, Note Value 1/4 -> no clamp (maxT = 2000 ms), Length 1/1, ratios 0.25, 0.5, 0.75, 1.0. At 90 BPM:
Linear with 16 taps, Note Value 1/4 -> clamp engaged (B x 16 = 10667 ms), Free, ratios 0.1333, 0.1911, 0.2489 ..
1.0 (666.7 ms + i x 288.9 ms). At 30 BPM, Note Value Whole: Primes with 4 taps -> base clamp engaged, Free, 0.5,
0.75, 1.0, 1.0 (2500, 3750, 5000, 5000 ms).

---

## 3. Tap lifecycle

### 3.1 The default rules

`D(i, n) = (i + 1) / n x c` for tap index i (0-based), active count n and the playable fraction c of 2.1: the n taps
divide the playable length evenly and the last tap sits on its end (the end of the length when it fits under 5 s,
the 5 s line when it does not). It is the shape of every rhythmic preset pattern (n x L) and of Copy. Default level
1.0 (0 dB). Used by Reset (every active tap) and as the ideal position of double-click and placement (3.3, 3.4).

**The registered parameter defaults are a second rule** (ruling 3, option C), the only count-independent one. They
fill the length coarse to fine, so each block of taps lands on the gaps the earlier block left:

| Taps | Custom Time default (ratio) | Grid |
|---|---|---|
| 1-4 | 0.25, 0.5, 0.75, 1.0 | quarters of the length (beats 1-4 of 1/1) |
| 5-8 | 0.125, 0.375, 0.625, 0.875 | the odd eighths |
| 9-16 | 0.0625, 0.1875, 0.3125, 0.4375, 0.5625, 0.6875, 0.8125, 0.9375 | the odd sixteenths |

One table, `kDefaultCustomTimeRatios` (a std-only header, row 3), is read by `registerMultiTapParams` (MTP:295-305,
today `(i + 1) / 17.0f`), by the `MultiTapParams` constructor (MTP:62-70, today `(i + 1) / (kCustomPatternMaxTaps +
1)`, the value a state without the spec-046 fields keeps) and by the view's constructor (TPE:14-22, today
`calculateDefaultTapTime(i, 16)`), so the three cannot drift. Registered level defaults stay 1.0.

What it gives: a fresh 4-tap pattern **equals Reset** (`D(i, 4)` at c = 1: Length 1/1, the default, fits under 5 s
from 48 BPM up); a fresh 8 is every eighth; a fresh 16 every sixteenth; all 16 values are distinct, all above 0 and
at least 0.0625 (50 px) apart, so the free test of 3.3 keeps every one of them and nothing a fresh instance reveals
is ever moved or stacked. Every value lies on the default Snap 1/4 grid (taps 1-4) or on a finer one (1/8 for 5-8,
1/16 for 9-16), so a snapped drag of a fresh tap starts from a line: a 3 px body drag of tap 1 stays at 0.25 and never
lands on tap 4 at 1.0 (the case of finding 1, row 9).

**The cost, stated for both orders.** Above 4 taps the tap numbers of a fresh pattern no longer run left to right
(8 taps in time order: 5, 1, 6, 2, 7, 3, 8, 4). What goes by index then no longer follows time order:
- the index-based pans: Cascade (L to R by index), Alternating and Widening Stereo (DSP:897-924) pan a fresh 8-tap
  Custom pattern in index order, so Cascade sweeps L to R across taps 1-4 and again across 5-8 instead of once in
  time. The default Spatial Pattern is Centered (MTP:46, index 2), which is unaffected;
- Decaying Level also goes by index (DSP:928-934), but under Custom the custom levels override it in every block
  (2.5), so it never reaches a Custom pattern; it only shapes the read-only display and Copy of the other 19 patterns,
  whose taps are in time order;
- lowering Taps hides the highest indices first (3.2), which here hides the finest subdivision first (8 -> 4 leaves
  the quarters), a side effect that reads as intended;
- Reset and the ideal of double-click and placement use `D(i, n)`, which is in index order: at 4 taps Reset changes
  nothing on a fresh pattern, at 8 taps it reorders the taps to run left to right (tap 1 to 0.125, tap 5 to 0.625).

The rejected order A (consecutive sixteenths, `(i + 1) / 16`) has the same index-order cost as soon as any tap is
moved, and adds two of its own: its taps 1-3 sit off the default 1/4 grid, so the first snapped drag of tap 1, 2 or
3 below 0.375 lands on 0.25, on top of tap 4; and its 3.3 example (Reset at 4, then 4 -> 8) gave the off-grid time
order 1, 5, 6, 7, 2, 3, 8, 4. B (keep `(i + 1) / 17`) sits off every grid.

### 3.2 Decrease: remember

Lowering Taps writes nothing, from any source, user gestures included. The hidden taps keep their parameters (as
today, CF M2) and the view keeps drawing nothing for them. Reason: the user's complaint P4, and prior art keeps hidden
taps too (Steinberg's automatable Taps control adds and removes repeats without rewriting them, PA M1).

### 3.3 Increase: reveal, and move only what collides

Raising Taps from m to n reveals taps m..n-1 at their stored parameters. Nothing is written by the reveal itself, in
any pattern, from any source (automation, preset load, view creation, UI), so a count change never rewrites a custom
pattern (P4) and the sound no longer depends on whether the editor is open (CR H5).

Placement runs only for **the taps one user gesture on the Taps slider revealed, while the editor shows the Custom
pattern**. A user gesture reaches the controller as `setParamNormalized(902)` followed by `performEdit(902)`
(`vst3editor.cpp:226-232`); automation and bulk loads never call `performEdit`. The pair of counts is handed over
explicitly; the view keeps no reveal state:

- `Controller::setParamNormalized(902)` records `tapCountBeforeSet_ = decode(getParamNormalized(902))` **before** the
  base call (decode = `int(2 + norm x 14 + 0.5)`).
- `Controller::performEdit` (a new override of `EditControllerEx1::performEdit`) calls the base, then, when `id ==
  kMultiTapTapCountId`, `!bulkParamLoad_`, the Timing Pattern is Custom, `tapEditor()` exists and `newCount =
  decode(v) > tapCountBeforeSet_`, calls `tapEditor()->placeRevealedTaps(tapCountBeforeSet_, newCount)`. A decrease,
  or no change, calls nothing.
- `placeRevealedTaps(oldCount, newCount)` sets the view's active count to `newCount` (a no-op in practice: the
  bridge already pushed 902 synchronously inside the preceding `setParamNormalized`, 3.6; the call keeps the view
  correct on its own, for the view tests), then handles exactly the indices `[oldCount, newCount)`.

So a count raised by automation, a preset load or the view's creation is never placed later: automation 4 -> 8
followed by a user 8 -> 9 places only tap 9; an editor that opens at 8 taps and a user 8 -> 7 writes nothing. A slider
drag 4 -> 8 delivers 4 -> 5, 5 -> 6, ... as separate pairs, each placing its own new tap.

For each handled index i in ascending order, with `t = tapTimeRatios_[i]`:

1. **Free test**: t is free when `0 < t <= c` (2.1) and no other active tap j != i has `|tapTimeRatios_[j] - t| <
   kCollisionRatio`, `kCollisionRatio = 8 / 802` (8 px of the 802 px plot: closer than that two bars read as one,
   section 5.5). Free: keep t (remembered), done.
2. Otherwise the tap moves to the **free grid line nearest the ideal** `D(i, n)`, n = the new count. Candidate lines:
   the snap lines of 2.2 (effective unit, ratio 1 included, ratio 0 never) when Snap is on, else the 1/16 lines
   (synced: every 0.25 beats; Free: 1/16 at the current tempo); in both, **only lines in `(0, c]`**, so no candidate
   lies beyond the 5 s ceiling. Order: the nearest line (ties go to the later line), then alternately the next line
   later, the next earlier, two later, two earlier, and so on. A line is free by the test of step 1 against every other
   active tap, including taps placed earlier in this same call.
3. If no line of that grid is free, retry with the 1/16 grid, then the 1/64 grid (both cut at c); if still none, use
   `D(i, n)` itself.
4. Level: never changed by placement (remembered).

The search is one pure function `placeTap(ratios, active, i, ideal, grids, c)` in TPL, shared with double-click
(3.4). A moved tap is written as begin / perform / end on its time parameter (3.5).

Worked examples (Length 1/1 at 120 BPM so c = 1, Snap 1/4, fresh instance, taps 1-4 at 0.25, 0.5, 0.75, 1.0, the
registered defaults of 3.1):
- 4 -> 5 by the slider: tap 5's stored 0.125 is free, it stays; nothing is written. 4 -> 8 the same: 0.125, 0.375,
  0.625, 0.875 all free. 8 -> 16 the same: the odd sixteenths are all free. Every tap is visibly new and on a grid
  (P1).
- After Reset at 4 taps (0.25, 0.5, 0.75, 1.0, which is the fresh pattern), 4 -> 8 in one slider jump (one pair
  4 -> 8): taps 5-8 find their stored odd eighths free and stay; nothing is written. The result is evenly spaced
  eighths, 0.125 apart, in the time order 5, 1, 6, 2, 7, 3, 8, 4.
- The same, stepwise 4 -> 5 -> 6 -> 7 -> 8: identical, each step finds its stored value free.
- A collision, Snap 1/8: from the fresh 4 taps, the user drags tap 2 from 0.5 to 0.625 (the stored place of hidden
  tap 7), then raises 4 -> 8 in one jump. Tap 5 (0.125) and tap 6 (0.375) are free and stay. Tap 7 (0.625) collides
  with tap 2 -> ideal D(6, 8) = 0.875, occupied by tap 8's stored value -> next later 1.0 (tap 4) -> next earlier
  0.75 (tap 3) -> two later: none -> two earlier 0.625 (tap 2) -> three earlier 0.5: free (tap 2 left it) -> tap 7 is
  written at 0.5. Tap 8 (0.875) is free and stays. Result taps 1-8 at 0.25, 0.625, 0.75, 1.0, 0.125, 0.375, 0.5,
  0.875: distinct, on the 1/8 grid, one write.
- Length 2/1 at 60 BPM (span 8000 ms, c = 0.625), Snap 1/4: a colliding revealed tap is placed on a beat line at or
  below 5 beats, never in the shaded region.

When the editor is closed, or the count changes without a user gesture, the stored taps are revealed unmoved; a
stacked pair is possible only if the stored values were stacked (by the user, or by a project saved with the old
editor). That is the whole residual difference between editor open and closed.

### 3.4 Reset and double-click

- **Reset** (button): for every active tap i: time `D(i, n)` (with c, so at sub-ceiling tempos the taps divide the
  playable 5 s instead of piling on it), level 1.0, each written as begin / perform / end on both parameters, in
  index order. The n positions are distinct by construction. Inactive taps are not touched, in the view or in the
  parameters (P19). Snap is not applied (`D` is the even division of the length, which is itself a musical grid).
- **Double-click on a tap** (kept by ruling 5, the time-and-level reset): time `placeTap(..., i, ideal = D(i, n),
  ...)` against the other active taps (the 3.3 search, so it never lands on another tap), level 1.0, both bracketed.
  It moves the tap in time to an index-based position, which can lie far from where it was; the level-only reset is
  Ctrl/Cmd-click (4.2). Examples, Length 1/1, 120 BPM, Snap 1/4: on a fresh 4-tap pattern a double-click on tap 1 has
  ideal D(0, 4) = 0.25, its own place, free of the others: time unchanged, level 0 dB. After tap 1 was dragged onto
  tap 2 at 0.5, the same double-click sends it back to 0.25. On a fresh 8-tap pattern a double-click on tap 5 (at
  0.125) has ideal D(4, 8) = 0.625: the 1/4 lines 0.75, 1.0, 0.5, 0.25 are all taken (taps 3, 4, 2, 1), so the 1/16
  retry starts at 0.625 (tap 7) and lands on 0.6875: a long jump, which is what this gesture does and why
  Ctrl/Cmd-click exists.

### 3.5 Edit gestures

The view no longer calls `CControl::beginEdit/endEdit` on itself (tag 920 is not a parameter, so those were no-ops,
CF H4). It gets three callbacks, installed by `createCustomView`:

```
struct TapEditSink { std::function<void(ParamID)> begin, end; std::function<void(ParamID, float)> perform; };
```

`perform` does `getParameterObject(id)->setNormalized(v)` then `performEdit(id, v)`; `begin` / `end` call
`beginEdit(id)` / `endEdit(id)` on the controller (which forward to the component handler). All of them run on the UI
thread (mouse and button handlers). Per path:

| Path | begin | perform | end |
|---|---|---|---|
| Body drag of tap i (time) | time_i at mouse down | time_i on every change of the (snapped unless Shift) value, never when equal to the last written value | time_i at mouse up |
| Handle drag of tap i (level) | level_i at mouse down | level_i on change | level_i at mouse up |
| Single grip of a short or silent tap (4.1) | time_i and level_i at mouse down | the axis chosen by the first move, on change | both at mouse up |
| Escape / mouse cancel during a drag | (already begun) | the pre-drag value, once | the dragged id(s) |
| Ctrl/Cmd-click tap i (4.2) | level_i | level_i = 1.0, only when it differs | level_i |
| Double-click tap i | time_i, level_i | both | both |
| Reset | per tap: time_i, level_i | both | both |
| Placement (3.3), moved taps only | time_i | once | time_i |
| Copy (controller) | per id, 2n + 2 ids | once each | per id |

A drag that never crosses its dead zone (4.2) still brackets begin / end with no perform: hosts treat it as a touch.

### 3.6 Controller wiring

- **No cached view pointer.** `tapPatternEditor_` (controller.h:187) is removed. `TapPatternEditor*
  Controller::tapEditor()` finds the control with tag 920 under `activeEditor_->getFrame()` and returns null when
  there is no editor or the MultiTap panel is not up. The lookup uses `findControlByTag` (CTL:85-102), which today is
  compiled only inside `#if defined(_DEBUG) && defined(_WIN32)` (CTL:33-105): it is moved out of that guard
  unchanged (it is a plain `ViewIterator` walk with no Win32 dependency); the logging helpers stay inside. The
  `kModeId` nulling at CTL:601-603 goes with the pointer (P12's orphaned pointer, CF H1), as does the `willClose`
  nulling. `tapEditor()` is called only on the UI thread: from the bridge's `update`, from `syncAllViews`, from
  `performEdit` and from the Copy and Reset buttons.
- **The bridge (threading, finding 4 option 1).** Delivery is **synchronous**. `EditController::setParamNormalized`
  calls `Parameter::setNormalized` (`vsteditcontroller.cpp:146-154`), which on a changed value calls
  `FObject::changed()` (`vstparameters.cpp:73-77`), which calls `UpdateHandler::triggerUpdates`
  (`fobject.cpp:196-202`); that runs every dependent's `update` at once, on the calling thread, with no thread check
  (`updatehandler.cpp`, `doTriggerUpdates`). Only an explicit `deferUpdate()` queues. The design therefore relies on
  the VST3 threading contract: `setParamNormalized` is `[UI-thread & Connected]` (`ivsteditcontroller.h:521-525`),
  and every other caller in Iterum (the sink of 3.5, `copyCurrentPatternToCustom`, `editParamWithNotify`) runs on
  the UI thread. A host that breaks the contract would already break every `VisibilityController` and the existing
  direct forwards; the design adds no new exposure and builds no queue. A new `TapEditorParamBridge :
  Steinberg::FObject` (no class of that name exists in `plugins/` or `dsp/`) is built the way `VisibilityController`
  is (`visibility_controller.h:12-60`): it calls `addDependent(this)` on the `Parameter` objects 900, 901, 902, 911,
  912, 913, 914, 915 and 950-981, so each change reaches its `update(changedUnknown, kChanged)` inside the call that
  made it, whoever made it (`Controller::setParamNormalized`, or the sink's direct `Parameter::setNormalized`).
  `update` returns at once while `controller.isBulkParamLoad()` is true (a new const accessor of `bulkParamLoad_`);
  otherwise it looks the view up with `tapEditor()` and pushes that one parameter: 913 Snap, 902 Taps
  (`setActiveTapCount`, reveal only, no writes), 914 Length, 915 Tempo, 950-965 `setTapTimeRatio`, 966-981
  `setTapLevel`, 900 editable or read-only; and for 900, 901, 902, 911, 912 and 915 it recomputes the read-only
  pattern of 5.9 when the pattern is not Custom. While the view is dragging tap k, incoming values of k's own time or
  level are ignored (the sink's own perform, and a host echo, cannot fight the drag). The bridge is created in
  `didOpen` and removes its dependencies in `willClose`, alongside the visibility controllers. Today's direct Snap and
  Taps forwards in `setParamNormalized` (CTL:615-626) are removed: the bridge covers them.
- **Bulk loads.** `setComponentState` (CTL:252-305) and `loadComponentStateWithNotify` (CTL:1233-1283) set
  `bulkParamLoad_` around about 40 MultiTap `setParamNormalized` calls; with per-id pushes those would be 40 view
  pushes and 5 read-only recomputes inside the load. The bridge skips them all while the flag is set (above), and
  `syncAllViews` (CTL:1292-1306) does **one full push after the load**: its tap-editor lines stay, rewritten to use
  `tapEditor()` instead of the removed pointer, and call `pushAllToTapEditor(view)`, the controller function
  `createCustomView` also uses (every id above through the same per-id `pushToTapEditor(id, view)` the bridge calls,
  then the read-only pattern when the pattern is not Custom; it lives on the controller because `createCustomView`
  runs before `didOpen` creates the bridge). Nothing in the bridge ever places or writes a parameter.
- **`createCustomView("TapPatternEditor")`** (UI thread): builds the view, reads the colour and font tokens from the
  `IUIDescription` it receives (5.7), installs the sink, and pushes the full set (`pushAllToTapEditor`: the per-id
  push for every id above, then the read-only pattern). It no longer calls `setActiveTapCount` before loading the values (the load
  order dependency of CTL:755-784 disappears because reveal writes nothing).
- **`resetPatternToDefault`** and the Copy button keep their entry points (`custom_views.h`).

---

## 4. Interaction

### 4.1 Hit areas

Tap bar: 12 px wide, centred on `x(ratio)`, from `y(level)` to the plot bottom y1 = 106 (section 5.2). Hit box 16 px
wide.
- A **tall tap** (bar at least 12 px high: `y1 - y(level) >= 12`, level at or above about -41.7 dB) has two zones:
  - **Handle** = from 3 px above the bar top to 8 px below it (so a 0 dB tap, whose bar top is the plot top, is
    grabbable).
  - **Body** = the rest of the bar, at least 4 px.
- A **short tap** (bar below 12 px, level below about -41.7 dB) and a **silent tap** (level below the display floor,
  5.2, drawn as a 12 x 8 px handle in the silence band) have **one grip**: the 16 px column from 3 px above the bar
  top (silent: the band top, 106) down to the band bottom, 114. Its drag is level or time, decided by the first move
  (4.2). Without it a silent tap could never be moved in time, and a tap below about -44 dB would have no body to grab.

Hit test order: highest index first (as today), handles and grips before bodies.

### 4.2 Drag

- Press on a **body**: time drag. `ratio = preDragRatio + (x - xDown) / 802`, snapped to the snap lines of 2.2 when
  Snap is on, clamped to [0, 1]. Level never changes.
- Press on a **handle**: level drag. `dB = startDb - (y - yDown) x (48 / 92)`, with `startDb = max(gainToDb(level),
  kLevelFloorDb)` (-48 dB), so a silent tap (gainToDb(0) = -144 dB, `db_utils.h:310-319`) leaves the silence band on
  the first upward pixel past the dead zone instead of after 184 px. Mapped by 5.2: dB below -48 is the band, level 0;
  dB at or above 0 is 1.0. Time never changes.
- Press on a **grip** (short or silent tap): nothing moves until the pointer has travelled 2 px; then the axis with
  the larger travel wins for the rest of the drag (`|dx| >= |dy|`: time, as a body drag; else level, as a handle
  drag, from the same `startDb`). Deltas are measured from the press point, as for every drag, so a 10 px upward
  drag raises a silent tap to `-48 + 10 x 48 / 92`, about -42.8 dB.
- **Dead zone**: nothing is written until the pointer has moved 2 px along the drag's axis; a click is never a move
  (P8). Snap applies to the target computed from the delta, so a tap that started off-grid snaps on its first real
  move, which is Steinberg's and Logic's rule (PA H1).
- **Shift** = fine adjust (10:1, the `CSlider` convention, CR M6) **and snap bypass**, for as long as it is held: the
  time target is `preDragRatio + (x - xDown) / 8020` and is not snapped, so a tap can be nudged off the grid (a flam,
  a push, a swung echo) with Snap left on. This is the free move of Steinberg's modifier (PA H1). Pressing or
  releasing Shift during a drag re-bases the drag (`preDragRatio` = the current value, `xDown` = x, and likewise for
  level), so the tap never jumps when the modifier changes. The old axis lock is gone: the handle / body split, and
  the first-move rule of a grip, already fix the axis.
- The view calls `setWantsFocus(true)` in its constructor and `getFrame()->setFocusView(this)` on mouse down, so
  **Escape** reaches `onKeyDown` where the host passes keys; it restores the pre-drag value (one perform, then end).
  `onMouseCancel` does the same. Hosts that take Escape for themselves leave the drag to finish on mouse up.
- **Ctrl / Cmd-click** (ruling 5): resets the tap's **level** to 1.0 (0 dB); time never changes. It is the reset
  gesture of every other Iterum control (`CControl::kDefaultValueModifier = kControl`, `ccontrol.h:108`; VSTGUI maps
  Cmd to `kControl` on macOS). The earlier claim that it "cannot be made to" work was wrong (finding 3):
  `CView::dispatchEvent` is virtual (`cview.h:96`) and calls the view-event listeners with `forEachReverse`, stopping
  at the first that consumes the event (`cview.cpp:666-675`), and `CControl` registers its default-value listener in
  its constructor (`ccontrol.cpp:68-72`), whose test is the static `CControl::CheckDefaultValueEventFunc`
  (`ccontrol.h:98-101`, `ccontrol.cpp:294-302`, which defers to `CheckDefaultValueFunc`, `ccontrol.cpp:280-289`: left
  button, modifiers exactly `kDefaultValueModifier`). A listener registered in the view's own constructor would therefore run
  first; the design takes the simpler of the two equivalent routes and **overrides `dispatchEvent`** in
  `TapPatternEditor`, which runs before any listener: on a `MouseDown` for which
  `CControl::CheckDefaultValueEventFunc(this, event)` is true it consumes the event, calls
  `ignoreFollowUpMoveAndUpEvents(true)` and returns without calling the base. If the view is editable and the press
  hits a tap (any zone of 4.1, the highest index first), it writes begin(level_i), perform(level_i, 1.0) only when the
  level differs, end(level_i) (3.5) and redraws; on empty space, or in read-only mode, it writes nothing. Every other
  event goes to `CControl::dispatchEvent`. So CControl's default-value path (which would `setValue` on tag 920, not a
  parameter) is never reached from this view.
- **Right-click**: ignored, as today (no context menu; the host's parameter menu needs a bound parameter view).
- **Double-click on empty space**: nothing.
- **Mouse wheel**: nothing.

### 4.3 Hover, cursor, readout

- Cursor per zone (finding 2), one static function `TapPatternEditor::cursorForZone(HitZone zone, bool editable)`
  that `updateCursorForPosition` calls: over a **body** `kCursorHSize` (as today, TPE:497-498: the one cue that a
  body drag moves time only); over a **handle** `kCursorVSize` (as today, TPE:494-495), handle drawn in `text-light`;
  over a **grip** (short or silent tap, 4.1) `kCursorSizeAll` (`vstguifwd.h:60`), handle drawn in `text-light`,
  because its axis is chosen by the first move; elsewhere `kCursorDefault`. No hand cursor: it says "grab and move
  anywhere", which neither zone does. In read-only mode (5.9): `kCursorDefault` everywhere.
- **Popup readout** while a tap is hovered or dragged, and in read-only mode while hovered: a `CTextLabel` added to
  the frame and positioned like `ArcKnob`'s value popup (`arc_knob.h:421-477`): 20 px high, centred on the tap,
  6 px above the bar top (above the handle, never under the pointer), clamped inside the frame; `value-font`,
  font colour `text`, background `#1e1e1e` at alpha 220, round radius 4, mouse disabled; removed on mouse exit, on
  mouse up when not hovering, and in `removed()`.
  Text (design-system 6.4 formats): `"<grid> · <time> · <level>"` where
  - `<time>` is the core-time format: `"%.1f ms"` below 1000 ms, `"%.2f s"` from 1000 ms ("750.0 ms", "1.25 s");
    synced time = `ratio x spanMs` at the view's tempo, capped at 5000 ms.
  - `<level>` is `"%.1f dB"` of `gainToDb(level)`, `"-inf dB"` when `level <= 0.001` (the DSP floor, DSP:862).
  - `<grid>` is the tap's delay counted in snap units, in the same system as the ruler (2.1, 2.2: both count delay
    from the input, the ruler in beats, the readout in units): `"k × <unit>"`, k the line index from 1 and `<unit>`
    the effective unit's menu name ("3 × 1/8" = three eighths of delay; "5 × 1/8T"; "4 × 1/4" sits on the ruler label
    "4"). The "×" keeps it from reading as a note length. It is present only when Snap is on, Shift is not held, and
    the tap lies on a snap line (within 1e-4 of ratio); the line at ratio 1 that is not a multiple of the unit prints
    the ruler's end instead ("end"). Otherwise the field and its separator are omitted.
  Examples: "3 × 1/8 · 750.0 ms · -6.0 dB", "2.37 s · -inf dB".

### 4.4 Host-side formats (P6)

`formatMultiTapParam` (MTP:372-382): Custom Time i prints `min(ratio x spanMs, 5000)` in the core-time format, where
spanMs comes from the Length and Tempo parameters (the controller passes their normalized values to the formatter,
the way `formatFreezeParam` receives the Steps value); in Free it is `ratio x 5000`. Only milliseconds, no grid
field: the host string is read in an automation lane without the Snap context that defines the unit, and ms is a
delay counted from the input like every other number on the axis. Custom Level prints `"%.1f dB"` / `"-inf dB"`.
Length prints its string; Tempo prints `"%.1f BPM"`. `getParamValueByString` parses "750.0 ms" and "-6.0 dB" back
through `parseMultiTapParam(id, string, lengthNorm, tempoNorm, out)` beside the formatter: Custom Time = ms / spanMs
clamped to [0, 1] (spanMs from Length and Tempo, 5000 in Free); Custom Level = "-inf" -> 0, else `dbToGain` clamped to
[0, 1]. Today `Controller::getParamValueByString` handles only `kGainId` (CTL:481-512); without the branch a typed
"750 ms" falls through to `Parameter::fromString` and lands on a wrong value.

---

## 5. Visual

### 5.1 Layout of the 852 x 140 view (view-local)

```
x:  0        34   42                                              844   852
y 0   ┌──────────┬───────────── top band (tap numbers) ─────────────┬─────┐
  14  │ level    │ 0 dB ─────────────── plot ───────────────────────│ pad │
      │ labels   │ ...                                               │     │
  106 │          │ -48 dB ───────────────────────────────────────────│     │
  114 ├──────────┼── silence band (level 0 handles) ─────────────────┼─────┤
  122 │    120   │ ruler ticks 114-122 (baseline 114)                │     │
  138 │    BPM   │ ruler labels 122-138, end label right-aligned ≤842│     │
  140 └──────────┴───────────────────────────────────────────────────┴─────┘
```

- Gutter x 0..34: level labels (y 14..114) and the tempo (y 116..138). Plot x0 = 42, x1 = 844 (802 px). Right pad
  844..852, which no label uses. A bar at ratio 0 spans 36..48 and at ratio 1 spans 838..850: both fully inside the
  view (P10).
- Top band y 0..14: tap numbers above full-height bars. Plot y0 = 14 (0 dB) to y1 = 106 (-48 dB), 92 px. Silence band
  106..114. Ruler 114..140: ticks 114..122, labels 122..138.
- `x(ratio) = 42 + ratio x 802`, with `ratio >= 1 -> 844` and `ratio <= 0 -> 42` by branch, not arithmetic.
- Scale: Free 6.2 ms/px; 1/1 at 120 BPM 2.5 ms/px; 1/8 at 120 BPM 0.31 ms/px. Level 0.52 dB/px.

### 5.2 Level axis (P7)

Stored values stay linear gain 0..1 (frozen); only the mapping to y changes.
- `dB = gainToDb(level)`; display floor `kLevelFloorDb = -48`. `y(level) = y0 + (-dB / 48) x 92` for dB in
  [-48, 0], with `level >= 1 -> y0` by branch. Levels below `dbToGain(-48) = 0.00398` are **silent** for display: the
  handle sits in the silence band (y 106..114), no bar.
- Inverse: y in [y0, y1] -> `dB = -48 x (y - y0) / 92`, `level = dbToGain(dB)`; y above y0 -> 1.0; y below y1 -> 0.0.
  A level dragged into the band is exactly 0, which the DSP plays as -96 dB (DSP:862). A drag's own dB arithmetic
  starts from the floor, not from -144 dB (4.2).
- Horizontal grid lines at -6, -12, -24, -36 dB in `border`; the -48 line is the plot bottom in `slider-frame`.
- Gutter labels (`value-font`, `text-dim`, right-aligned at x 30, 14 px high centred on the line): "0 dB" at y0,
  "-12", "-24", "-36", "-48" at their lines.
- Reading of the before-shot levels: 1.0 -> top, 0.7 -> -3.1 dB (6 px down), 0.5 -> -6 dB (11.5 px), 0.35 -> 17.5 px,
  0.25 -> 23 px, 0.1 -> 38 px, 0.05 -> 50 px, 0.0 -> silence band. The quiet taps a producer sets (-12 to -30 dB)
  now span 23 to 57 px instead of 28 px for everything below -12 dB.

### 5.3 Vertical grid (2.2) colours

On the `slider-track` ground (#272730), draw order bottom to top:
1. snap lines, `accent-dim` at 60 % alpha (about 1.5:1, blue);
2. beat lines, `text-dim` at 25 % alpha (about 1.5:1, grey);
3. bar lines, `text-dim` at 45 % alpha (about 2.2:1); the plot's left edge (ratio 0) and right edge (ratio 1) are bar
   lines.

Beat and bar lines sit on top, so the bar / beat hierarchy survives where a snap line coincides with them (every bar
line is a snap line at Snap 1/4 or finer), and with Snap Off the plot still shows a readable beat grid (today's
`border` beat lines were about 1.13:1 and `slider-frame` bar lines about 1.32:1, effectively invisible). Where a snap
line coincides with a beat line (Snap 1/4), the plot does not show it; the ruler's `accent` snap ticks, drawn over
the beat ticks (2.2), do, which is what distinguishes Snap 1/4 from Snap Off. Over-ceiling region: `background` at
50 % with a `text-dim` line (2.2), drawn over the grid.

### 5.4 Taps

- Bar: 12 px wide, `accent` at alpha 170 (so two overlapping bars read darker where they overlap, 5.5), from
  `y(level)` to y1. Dragged: full alpha.
- Handle: the top 6 px of the bar in `text`; hovered or dragged: `text-light`. A short tap's bar is drawn at its true
  height (its grip, 4.1, is larger than what is drawn).
- Silent tap: a 12 x 8 handle at y 106..114 in `text`, no bar.
- Number: `value-font`, `text-dim` (`text-light` while dragged), centred on the bar, in a 14 px band whose bottom is
  the bar top (`labelTop = max(0, y(level) - 14)`); for a silent tap the band is y 92..106.
- Draw order ascending index; the dragged tap is drawn last.

### 5.5 Overlap

Taps whose centres lie within 8 px of each other form a group (`labelGroups` in TPL, the same `kCollisionRatio` as
3.3). A group draws one number label, the indices joined by "+" ("6+7", "7+8+9"), above the tallest bar of the group.
The bars themselves overlap at alpha 170, so a stack reads as a darker bar with a "+" label, never as one tap.

### 5.6 Edge clipping

Solved by geometry (5.1): a tap at ratio 0 or 1 is 6 px inside the plot edge; the plot edges are drawn as bar lines so
the inset reads as the bar line, not as empty space.

### 5.7 Colours and fonts (P15)

The view holds a `Palette` struct filled by `createCustomView` from the `IUIDescription` (`getColor` / `getFont`),
with compiled-in fallbacks equal to the tokens of design-system 5.2:

| Use | Token |
|---|---|
| view background | `slider-track` |
| view border, level grid lines | `border` |
| -48 dB line, ruler baseline | `slider-frame` |
| snap lines (plot) | `accent-dim` at 60 % |
| beat lines (plot) | `text-dim` at 25 % |
| bar lines (plot), plot edges | `text-dim` at 45 % |
| snap ticks (ruler) | `accent` |
| tap bars | `accent` |
| handles, popup text | `text` |
| hovered / dragged handle, dragged number | `text-light` |
| numbers, ruler ticks and labels, gutter labels, tempo, over-ceiling line | `text-dim` |
| read-only taps | `text-dim` at alpha 120 |
| over-ceiling fill | `background` at 50 % |
| every text | `value-font` (11 pt) |

No `CFontDesc` is constructed in `draw`; the font is held by the view.

### 5.8 Hidden and conditional states

Dragged, hovered, silent, short, read-only, over-ceiling: all above. There is no selected state after mouse up and
no disabled state.

### 5.9 The empty-panel question: the display shows every pattern

Decision: the fieldset is always visible and the view shows the taps of the selected pattern: editable for Custom,
read-only for the other 19. It removes P17 (principle 6: empty space sits inside bordered groups, and nothing sits
there for 19 of 20 patterns today), it turns Copy into a visible "edit this", and it costs only what Copy already
needs (the calculator of 2.6).

Read-only mode (`setPresetPattern(ratios[16], levels[16], count, length)`, computed by the controller with the very
function Copy uses, 2.6 steps 1-4, at the Tempo parameter):
- drawn on **the Length Copy would choose** (2.6 step 3): the smallest synced Length that holds every tap, with its
  synced ruler, beat labels and beat / bar lines, when no clamp is engaged; or Free (0..5000 ms, ms ruler, the
  tempo's beat and bar lines in the plot) for a pattern with a tap later than 8 beats or any clamp engaged at the
  current tempo (2.6). The ratios are the ones Copy would write, so pressing Copy changes only the colour and the
  editability: no tap moves on screen. No snap lines; the over-ceiling region is drawn when the chosen synced Length
  exceeds 5 s at the current tempo (2/1 below 96 BPM, e.g. Quarter with 5 taps at 60 BPM, c = 0.625), and no tap lies
  beyond it (a tap beyond it would be clamped, which sends the pattern to Free); in Free the taps the source plays at
  5 s sit stacked at the right edge with a "+" label, which is what plays;
- levels: `playedLevelDb` (2.6), the timing pattern's -3 dB x i after the spatial pattern (Flat: 0 dB), which is what
  plays;
- taps in `text-dim` at alpha 120, no handles, numbers as 5.4, group labels as 5.5;
- hover popup "<time> · <level>" (4.3), no cursor change, no drag, no double-click, no Ctrl/Cmd-click write (it is
  still consumed, 4.2), no key handling;
- recomputed by the bridge on every change of 900, 901, 902, 911, 912 or 915 (3.6).

The read-only picture is the DSP's own formula at the current tempo, timing and spatial pattern together (2.6 mirror
test), so what it shows is what plays. Under Custom the editor shows the custom levels, which override the spatial
levels (2.5), so the same holds there.

---

## 6. Panel change

Against `panels/multitap.md` 2.7-2.8 and design-system 7.9 / 8.2 (ruling 6 fixed the title line; this adds one
label and one menu to it and makes the fieldset permanent). Fieldset-relative coordinates.

| # | View | Origin | Size | control-tag / custom-view-name | Attributes |
|---|---|---|---|---|---|
| 7 | FieldsetContainer | 12, 288 (panel) | 876, 180 | none | 4.11 set; `fieldset-title="TAP PATTERN"` (was CUSTOM PATTERN); **always visible** |
| 7e | CTextLabel "Pattern Length" | 330, 4 | 92, 16 | `MultiTapPatternEditorLabel` (922) | label-font, text-dim, right, `transparent="false" back-color="panel" style-no-frame="true"` |
| 7f | COptionMenu | 426, 0 | 110, 24 | `MultiTapCustomLength` (914) | 4.7 menu set; title line |
| 7a | CTextLabel "Snap" | 548, 4 | 40, 16 | `MultiTapPatternEditorLabel` (922) | as 7e (adds the tag and `style-no-frame`, P18) |
| 7b | COptionMenu | 592, 0 | 110, 24 | `MultiTapSnapDivision` (913) | unchanged |
| 7c | CView Reset | 784, 0 | 80, 24 | `ResetPatternButton` (923) | unchanged |
| 7d | CView editor | 12, 28 | 852, 140 | `TapPatternEditor` (920) | unchanged |
| 8 | CTextLabel caption "Pattern" in the Copy wrapper (a sibling of the fieldset at panel 12, 288, 876 x 24, drawn after it, `multitap.xsl:147-149`) | 558, 4 (wrapper) | 148, 16 | `MultiTapCopyPatternButton` (921, kept) | **changed** (finding 7): `transparent="false" back-color="panel"`, was `transparent="true"` (`multitap.xsl:149`, `editor.uidesc:598`); label-font, text-dim, right as before |
| 8a | CView Copy button | 714, 0 (wrapper) | 150, 24 | `CopyPatternButton` | unchanged |

Title-line x ranges: 330-422, 426-536, 548-588, 592-702, 784-864, pairwise disjoint; the Copy caption (558-706) and
button (714-864) overlap only views that are hidden whenever they are shown.

**The title line under the Copy row** (finding 7). With the fieldset always visible, its top border runs under the
Copy row in 19 of 20 patterns: `FieldsetContainer` strokes the outline at `y = titleFontSize / 2 + lineWidth / 2`,
about 6 px, across the whole width except the title gap (`fieldset_container.h:103-117`). The caption box (y 4..20)
sits on it, so a transparent caption would show the line through the top of "Pattern". The legend rule of the panel
(title-line labels are opaque to mask the border, the xsl comment at `multitap.xsl:133`, design-system 4.10) now
applies to it as it does to 7e and 7a, hence `back-color="panel"`. The Copy button needs no change: it is a
`CTextButton` styled by `styleIterumPatternButton` (`custom_views.h:34-45`) with a solid `slider-track` gradient fill
and a 3 px round radius, filled with `fillLinearGradient` over its round-rect path (`cbuttons.cpp:949-951`), so the
line at y 6, below the 3 px corners, is covered by its opaque fill. The visual review
of the `preset-*.png` shots (row 11) names this title line explicitly: no border stroke visible through "Pattern" or
the Copy button.

Visibility (replaces the 8.2 rows "MultiTap custom pattern" and "MultiTap copy button"):

| Group | Mechanism | Tags | Shown when |
|---|---|---|---|
| Custom controls | `VisibilityController` (per control) | 913, 914, 922, 923 | Timing Pattern >= 0.99 |
| Copy | `VisibilityController` (exists) | 921 | Timing Pattern < 0.99 |
| The fieldset and the editor (920) | none | | always |

The `ContainerVisibilityController` anchored on 920 (CTL:902-910) is removed. A `CTextLabel` carrying tag 922 is a
`CControl` the per-control controller toggles (design-system 8.1); 922 is `kMultiTapPatternEditorLabelTagId`,
defined and unused today (`plugin_ids.h:235`), not a parameter, so VST3Editor binds nothing to it (the Copy caption
with 921 is the precedent, `multitap.xsl:18-19`).

XSLT (`specs/iterum-ui-redesign/xslt/multitap.xsl`): in `control-tags` add `MultiTapCustomLength` 914 and
`MultiTapPatternEditorLabel` 922 (the same guarded pattern as line 23); in the CUSTOM PATTERN section change the title,
insert 7e and 7f before the Snap label, give both labels `control-tag` 922 and `style-no-frame="true"`; in the Copy
wrapper change the caption's `transparent="true"` to `transparent="false" back-color="panel"` (row 8). Regenerate
`editor.uidesc` with `npx xslt3` as 9.2 of the design system prescribes. Structure counts for
`editor_panels_test.cpp` stay 4 fieldsets / 4 plain containers; the MultiTap row table gains `{914, W::M}`; the
conditional case changes to the table above.

---

## 7. State and compatibility

### 7.1 Stream

One field, appended **after** `saveTapeSyncParams` at the very end of `Processor::getState` (PROC:320), never inside
the MultiTap block (DS section 3 explains why: Snap was appended inside the block and the Tape tail follows it):

```
saveMultiTapLengthParams:  writeInt32(customLength)        // 0 Free .. 5 2/1
loadMultiTapLengthParams:  if (!readInt32(v)) customLength = 0 /* Free */; else customLength = clamp(v, 0, 5)
loadMultiTapLengthParamsToController: absent -> setParam(914, 0.0); present -> setParam(914, v / 5.0)
```

Called from `getState`, `setState`, `setComponentState` (via `syncMultiTapLengthParamsToController(streamer, controller)`, the wrapper modelled on `syncTapeSyncParamsToController`, tape_params.h:651) and `loadComponentStateWithNotify`
(CTL:1277), in that last position each time. The Tempo parameter (915) is not stored.

### 7.2 What every saved pattern from before loads as

Free. The field is absent, so Length = Free and every tap plays `ratio x 5000 ms`, the formula of today (CF section
0), bit for bit (2.5). Snap (913) keeps its saved index; its lines now mean note lengths at the host tempo (2.3),
which changes only the picture, never a stored time. The 10 factory MultiTap presets are not Custom
(`tools/preset_generator.cpp:1113-1248`: patterns 2, 3, 4, 14, 15, 16, 17), so their sound cannot change; they load
as Free, and a user who switches one to Custom gets its stored `(i+1)/17` taps on the 5 s axis.

**The generator and the `.vstpreset` files are left alone.** The generator writes no Tape sync tail today: its
stream ends with the MultiTap block (`tools/preset_generator.cpp:485-498`, `writeMultiTapState` last; every MultiTap
and Tape factory preset's Comp chunk is 696 bytes and ends with snapDivision 14). Appending only the Length int32
would make `loadTapeSyncParams` read it as Tape's time mode and leave the Length absent; writing the full tail would
need the Tape presets' real time mode and note value, which is Tape's business, not this change's. Presets made by
the unchanged generator keep loading exactly as now (Tape sync absent -> its defaults; Length absent -> Free). The
generator's `(i+1)/17` custom-time defaults are unused by non-Custom presets.

States that predate the custom-pattern fields of spec 046 carry no ratios at all and keep the `MultiTapParams`
constructor's defaults; those move from `(i+1)/17` to the ruling-3 table (3.1): tap 1 294 -> 1250 ms on the Free
axis, every tap a different time. No shipped state is in that class (the fields were added with the editor), and such
a state only sounds different if it is set to Custom. A fresh instance is not a saved state: it starts at Length 1/1
(ruling 1) on the ruling-3 taps.

### 7.3 What a preset saved after loads as before

An older build ignores the tail and plays the pattern as Free: the stored ratios on a 5 s axis. A pattern saved at
Length 1/1 and 120 BPM (2000 ms span) therefore plays 2.5 x slower in a build without this change, and a Free pattern
plays identically. A newer build loading a state from this build reads the field (it is at the end of the stream and
nothing follows it yet; any later field goes after it).

### 7.4 Parameters

| ID | Name | Type | Range / strings | Default | Flags | Control |
|---|---|---|---|---|---|---|
| 914 | MultiTap Custom Length | StringList | Free, 1/8, 1/4, 1/2, 1/1, 2/1 | 4 (1/1) | kCanAutomate, kIsList (via `createDropdownParameterWithDefault`) | COptionMenu 7f |
| 915 | MultiTap Tempo | Parameter | 20..300 BPM, `(bpm-20)/280` | 120 | kIsReadOnly, kIsHidden | none (hidden, read-only) |

Changed registered defaults: Custom Time 1-16 from `(i+1)/17` to the ruling-3 table `kDefaultCustomTimeRatios`
(quarters, odd eighths, odd sixteenths; 3.1), in the registration and in the `MultiTapParams` constructor alike. Changed display formats: Custom
Time and Level (4.4). Nothing else in `registerMultiTapParams` changes.

---

## 8. Implementation plan

Ordered; each row names the file, the function and the Catch2 case that proves it, written red first. Gates run
sharded: `node tools/run-gate.js --filter '~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]' --out DIR <exe>`;
`multi_tap_delay.h` reaches `dsp_effects_tests`, `plugin_tests` and `approval_tests` (`tools/header-consumers.js`).
Once row 4 compiles `processor.cpp` into `plugin_tests`, the processor test TU `multitap_tempo_output_test.cpp` (and
every later `plugin_tests` TU that includes `processor/processor.h`) is a consumer of `processor.cpp` and
`processor.h` for gate scoping: a change to either runs that TU's cases, which `header-consumers.js` finds through
the `#include` of `processor/processor.h`.
Real-time rules for rows 1-5: no allocation, lock or I/O in `process()`; relaxed atomics; the tempo send uses the
host's `IParameterChanges` as `again.cpp` does; per-block custom work is one `applyCustomTimingPattern` (16 taps)
under Custom and 33 stores in the processor.

**Implementer 1: DSP, processor, parameters, controller**

| # | File / function | Change | Catch2 case (file, tag) |
|---|---|---|---|
| 1 | `dsp/include/krate/dsp/effects/multi_tap_delay.h`: `CustomLength`, `kCustomFreeLengthMs`, `customSpanMs()`, `setCustomLength`, `customLength_` (Free), `customTempoBPM_` (120); `setCustomTimeRatio` / `setCustomLevelRatio` / `setCustomLength` store only; `setBaseTimeMs` no Custom re-apply; `process()` tempo read and the unconditional per-block apply after `updateMorph`; `calculatePatternTimes` clamped Custom case | 2.5 | `dsp/tests/unit/effects/multi_tap_delay_test.cpp` `[effects][multitap][custom]`: "Free length plays ratio x 5000 ms, clamped to 1 and 5000" (getTapTimeMs after one block, ratios 0, 0.0001, 0.25, 1.0); "Length 1/1 at 120 BPM puts ratio 0.25 at 500 ms and 0.5 at 1000 ms, and a block at 90 BPM moves them to 667 and 1333 ms, transport stopped"; "Length 2/1 at 60 BPM clamps every tap beyond ratio 0.625 to 5000 ms"; "setters store only: getTapTimeMs is unchanged until the next block, then reflects all 32 values"; "Custom with Spatial DecayingLevel keeps the custom levels across 10 blocks" and the same for FlatLevel (call `applySpatialPattern` before each `process` as the processor does; getTapLevelDb equals the custom level every block); "custom levels apply in the block a morph into Custom completes" (levels 0.5 set before `morphToPattern(Custom)`; after completion getTapLevelDb == -6.02 dB in that block); "morphToPattern(Custom) targets ratio x span, clamped to [1, 5000]" (ratios 0 and 1.0 at Length 2/1, 60 BPM: targets 1 ms and 5000 ms, never n x baseTime) |
| 2 | `plugins/iterum/src/plugin_ids.h`: `kMultiTapCustomLengthId = 914`, `kMultiTapTempoId = 915` | 7.4 | `multitap_visibility_test.cpp` "MultiTap parameter IDs are correctly defined": add both |
| 3 | `plugins/iterum/src/parameters/multitap_params.h`: `customLength` atomic (default 4); `handleMultiTapParamChange` 914 (`int(v x 5 + 0.5)`); `registerMultiTapParams` 914 and 915 (7.4); new std-only header `plugins/iterum/src/parameters/custom_pattern_defaults.h` with `inline constexpr std::array<float, 16> kDefaultCustomTimeRatios` (3.1, ruling 3; no symbol of that name exists in `plugins/` or `dsp/`), read by the 950-965 registration and the `MultiTapParams` constructor; `formatMultiTapParam` 4.4 with the extra Length/Tempo arguments; `multiTapNoteValueFromDropdown`; `saveMultiTapLengthParams`, `loadMultiTapLengthParams`, `loadMultiTapLengthParamsToController`, `syncMultiTapLengthParamsToController(streamer, controller)` (7.1, modelled on `syncTapeSyncParamsToController`), `parseMultiTapParam(id, string, lengthNorm, tempoNorm, out)` (4.4) | 2.6, 3.1, 4.4, 7.1 | `plugins/iterum/tests/unit/parameters/multitap_params_test.cpp` `[params][multitap]`: "Custom Length normalization" (0, 0.2, 1.0 -> 0, 1, 5); "Custom Length tail: absent loads Free, present round-trips 0..5" (a stream ending after the Tape sync fields, then one with the tail); "Custom Length controller sync" (absent -> setParam(914, 0.0); present v -> setParam(914, v / 5.0), through both `loadMultiTapLengthParamsToController` and the `sync...` wrapper); "Custom Time defaults fill the grid coarse to fine" (the registered default of 950+i and `MultiTapParams{}.customTimeRatios[i]` both equal `kDefaultCustomTimeRatios[i]`: 0.25, 0.5, 0.75, 1.0, then 0.125 .. 0.875, then 0.0625 .. 0.9375; all 16 distinct and > 0; the first 4 equal `(i+1)/4`); "Custom Time formats as core time of the span" (Free 0.15 -> "750.0 ms"; 1/1 at 120 BPM 0.5 -> "1.00 s"; 2/1 at 60 BPM 1.0 -> "5.00 s"); "Custom Level formats in dB" (1.0 -> "0.0 dB", 0.5 -> "-6.0 dB", 0.0005 -> "-inf dB"); "Custom Time parses back" ("750.0 ms" -> 0.15 in Free and 0.375 at 1/1, 120 BPM; "6000 ms" clamps to 1.0); "Custom Level parses back" ("-6.0 dB" -> 0.501 within 1e-3, "-inf dB" -> 0, "0.0 dB" -> 1.0) |
| 4 | `plugins/iterum/src/processor/processor.{h,cpp}`: `getState` / `setState` tail (7.1); MultiTap branch pushes the 16 ratios, 16 levels and `setCustomLength` every block **before** the pattern-change detection, the `== 19` guard removed (2.5); `process()` tempo send (2.4) with `lastSentTempo_`, `samplesSinceTempoSend_`, reset in `setActive(true)`; `processParameterChanges` unchanged (900-999 already route, PROC:446); a test accessor `const Krate::DSP::MultiTapDelay& multiTapDelayForTest() const noexcept { return multiTapDelay_; }` in `processor.h` (the member is `multiTapDelay_`, processor.h:209). `plugins/iterum/tests/CMakeLists.txt` (finding 6): today `plugin_tests` compiles `controller.cpp`, `custom_views.cpp`, `delay_time_sync_controller.cpp`, `tap_pattern_editor.cpp` and `memorystream.cpp` (lines 80-86) but **not** `processor.cpp`, and no test includes `processor/processor.h`; add `${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp` beside `controller.cpp`, and `public.sdk/source/vst/hosting/processdata.cpp` and `parameterchanges.cpp` beside `memorystream.cpp` (KrateDSP is already linked, line 97) | 2.4, 2.5, 7.1 | new `plugins/iterum/tests/unit/processor/multitap_tempo_output_test.cpp` `[iterum][multitap][processor]`, driving `Processor::process` with `Steinberg::Vst::HostProcessData` + `ProcessContext`. Every case **heap-allocates** the processor (`auto proc = Steinberg::owned(new Iterum::Processor());`: it holds every mode's DSP, MultiTapDelay alone two 8192-float dry buffers, and a stack instance risks the Catch2 "Unknown exception" stack overflow), then calls `initialize(nullptr)`, `setupProcessing` (48 kHz, 64-sample blocks, kRealtime) and `setActive(true)` before the first `process`: "writes MultiTap Tempo on the first block and again only when the tempo moves 0.05 BPM" (points at 120 then 121.5; none for 120.02); "sends at most one tempo point per 50 ms" (a 1-sample ramp over 100 blocks of 64 samples at 48 kHz yields <= 3 points); "state round-trip keeps Custom Length and a pre-tail stream loads Free" (getState -> setState; a stream truncated after Tape sync -> Free); "switching Timing Pattern to Custom at the same tap count glides toward ratio x span" (fresh processor, MultiTap, Quarter, 4 taps, custom ratios 0.1 .. 0.4 at Length 1/1 and 120 BPM set as parameters while Quarter plays; then Timing Pattern 19; run blocks through the morph and read the tap times through `multiTapDelayForTest().getTapTimeMs(i)`: every sampled time lies between the Quarter start and ratio x 2000 ms, never below 1 ms, and ends at ratio x 2000 ms) |
| 5 | `plugins/iterum/src/parameters/pattern_calculator.h`: `patternTap(..., bpm)` (milliseconds with every DSP clamp in the DSP's order: tempo to [20, 300], base clamp to [1, 5000] before the ratios, per-tap `min(.., 5000)`, Linear's `maxT = min(B x n, 5000)`), `patternClampEngaged(...)` and `playedLevelDb(...)` (2.6, finding 5); delete `calculatePatternRatios` | 2.6 | `tap_pattern_editor_test.cpp` `[tap_pattern_editor][copy]`: "patternTap and playedLevelDb match MultiTapDelay for every timing and spatial pattern" (for timing patterns 0..18, spatial patterns 0..6, note values Quarter and Eighth, modifiers None/Triplet/Dotted, 4 and 16 taps, at 90 and 140 BPM: prepare a `MultiTapDelay`, `setNoteValue`, `loadTimingPattern`, then per block `applySpatialPattern` and `process` with that tempo playing, as the processor does, and compare `getTapTimeMs(i)` with `patternTap(..., bpm).ms` within 0.5 ms and `getTapLevelDb(i)` with `playedLevelDb` within 0.1 dB, for every i below the count); "patternTap: Linear with 16 taps at 90 BPM follows the clamped spread" (Quarter: tap 2 at 955.6 ms within 0.5 ms, every tap equal to the DSP's, `patternClampEngaged` true); "patternTap: Note Value Whole at 30 BPM applies the base clamp" (Primes, Fibonacci and Linear, 4 and 16 taps, against the DSP within 0.5 ms: Primes tap 1 = 2500 ms, Fibonacci taps 1-2 = 1250 ms, Linear every tap 5000 ms; `patternClampEngaged` true); "patternClampEngaged is false for Quarter with 6 taps at 120 BPM and true for Whole with 4 taps at 120 BPM"; the old `calculatePatternRatios` cases are deleted |
| 6 | `plugins/iterum/src/controller/controller.{h,cpp}`: remove `tapPatternEditor_` and the Snap/Taps forwards of CTL:615-626; move `findControlByTag` out of the `_DEBUG && _WIN32` guard; `tapEditor()`; `TapEditorParamBridge` (new, in `controller/`), created in `didOpen`, detached in `willClose`; `tapCountBeforeSet_` in `setParamNormalized`; `performEdit` override (3.3); `isBulkParamLoad()` const accessor, the bridge's `update` returning early while it is true; `pushToTapEditor(id, view)` and `pushAllToTapEditor(view)`; the tap-editor lines of `syncAllViews` (CTL:1293-1305) rewritten to one `pushAllToTapEditor` on `tapEditor()` (3.6, finding 4); `copyCurrentPatternToCustom` (2.6, with the tempo of 915 passed to `patternTap`); `createCustomView` (3.6, 5.7); read-only pattern push (5.9); `getParamStringByValue` passes Length and Tempo to the formatter; `getParamValueByString` gains the 950-981 branch through `parseMultiTapParam` (4.4); `setComponentState` calls `syncMultiTapLengthParamsToController(streamer, *this)` right after `syncTapeSyncParamsToController` (CTL:299) and `loadComponentStateWithNotify` calls `loadMultiTapLengthParamsToController(streamer, setParamWithNotify)` right after CTL:1277, so the controller's 914 follows the processor on every load (without them an old project plays Free while the menu, the bridge and the grid show 1/1); remove the 920 `ContainerVisibilityController`, add the per-control one for 913/914/922/923 (6) | 2.6, 3.3, 3.6, 5.9, 6 | `plugins/iterum/tests/unit/vst/tap_editor_controller_test.cpp` `[iterum][ui][tap-editor]`, on the `HeadlessEditor` of `editor_panels_test.cpp` (factor its helper into a header if needed) with a recording `IComponentHandler` stub (begin / perform / end per id); no `triggerDeferedUpdates` is needed for the bridge, whose delivery is synchronous (3.6), and the cases below assert exactly that: "Copy of Quarter with 6 taps writes 6 times, 6 levels, Length and Custom, each bracketed, count decoded 2 + 14n" (Spatial Centered: Length 2/1, ratios 0.125, 0.25 .. 0.75, levels dbToGain(-3 x i)); "Copy under Spatial Flat writes level 1.0 for every tap"; "Copy of Exponential keeps -3 dB per tap"; "Copy of Quarter with 16 taps at 120 BPM goes to Free at the played times" (ratios 0.1 .. 0.9, then 1.0 for taps 10-16); "Copy of Whole with 4 taps at 120 BPM" (Free; 0.4, 0.8, 1.0, 1.0); "Copy of Golden with 8 taps at 120 BPM, Note Value 1/4" (Free; 0.1, 0.1618, 0.2618, 0.4236, 0.6854, 1.0, 1.0, 1.0 within 1e-3); "Copy does not move the taps on screen" (the read-only ratios before Copy equal the editable ratios after it, and the view's Length is the copied one); "a set of 950 or 966 is reflected in the view immediately" (`setParamNormalized(950, 0.3)` then, with no deferred-update call in between, `getTapTimeRatio(0) == 0.3`; the same for 966 and `getTapLevel(0)`); "a bulk load pushes once, after the load" (with MultiTap open, `setComponentState` of a stream whose 950-981 differ from the view's: the controller counts its pushes through two test counters, `tapEditorFullPushCountForTest()` incremented by `pushAllToTapEditor` and `tapEditorIdPushCountForTest()` incremented by every `pushToTapEditor` that is not part of a full push; across the call the per-id count is unchanged, the full count rises by exactly 1, and the view shows the loaded values afterwards); "Copy of Linear with 16 taps at 90 BPM goes to Free at the played times" (915 at 90 BPM, Quarter: Length Free, tap 2's ratio = 955.6 / 5000 within 1e-4, every ratio = `patternTap(...).ms / 5000`); "a MultiTap preset loaded while MultiTap is open still drives the view afterwards" (load a component state twice, then set 913 and see `getSnapDivision` follow: the CF H1 probe); "performEdit on Taps after a user-path increase places colliding taps, automation does not" (setParamNormalized(902) alone leaves a colliding stored tap; setParamNormalized + performEdit moves it with begin / perform / end on its time id); "automation 4 -> 8, then a user 8 -> 9 moves only tap 9" (taps 5-8 stored as a stack, tap 9 colliding: only 958's id is written); "a decrease gesture writes nothing, even after a non-gesture increase or editor open" (open at 8 from an initial 4 with stacked stored taps, then user 8 -> 7: no perform); "the tempo parameter reaches the view and the read-only pattern" (set 915 to 90 BPM with pattern Quarter and 4 taps: Length 1/1, `getPresetTapTimeMs(0)` == 667 ms); "Spatial changes recompute the read-only levels" (901 Flat: every preset level 1.0); "setComponentState of a stream without the Length tail sets 914 to 0.0 (Free); a stream with it sets v/5" (and the same through `loadComponentStateWithNotify`); "typed host strings parse on Custom Time and Level" (`getParamValueByString(950, "750.0 ms")` -> 0.15 in Free and 0.375 at 1/1, 120 BPM; `(966, "-6.0 dB")` -> 0.501 within 1e-3) |
| 6a | `.claude/skills/vst-guide/THREAD-SAFETY.md:121` (finding 4): the "How It Works" item 2 says a parameter change "calls `deferUpdate()` internally, scheduling notification on UI thread", and item 3 that `update()` runs at 30 Hz from a `CVSTGUITimer`; both are false for `Parameter::setNormalized`, which calls `changed()` and so runs every dependent's `update` synchronously on the calling thread (`vstparameters.cpp:73-77`, `fobject.cpp:196-202`). Rewrite items 2-4 to: delivery is synchronous; `setParamNormalized` is a `[UI-thread & Connected]` call (`ivsteditcontroller.h:521-525`), which is what makes the `update` safe; only an explicit `deferUpdate()` (as `VisibilityController`'s constructor uses for its first update, `visibility_controller.h:56`) queues to the UpdateHandler's deferred list | 3.6 | none (documentation); the row 6 case "a set of 950 or 966 is reflected in the view immediately" is the executable form of the corrected statement |
| 7 | `custom_views.h`: no change to the buttons (their restyle landed with the UI redesign); the caption beside the Copy button changes in the uidesc only (row 10) | | |

**Implementer 2: the view and the panel**

| # | File / function | Change | Catch2 case |
|---|---|---|---|
| 8 | `plugins/iterum/src/ui/tap_pattern_editor_logic.h`: `CustomLength` + `kCustomLengthBeats` (shared with the DSP through `multi_tap_delay.h` or a tiny core header, one definition); `spanMs(length, bpm)`; `playableFraction(length, bpm)`; `GridLine {float ratio; enum {Beat, Bar, Snap} kind;}` and `gridLines(length, snap, bpm)` (snap lines in (0, 1) plus ratio 1, ratio 0 never, the 1/16 fallback); `effectiveSnapUnit`; `snapToLines`; `defaultTapTime(i, n, c)` (Reset and the ideal; the registered defaults are `kDefaultCustomTimeRatios`, row 3, which the view's constructor reads instead of `calculateDefaultTapTime`); `placeTap(...)` (3.3 steps 1-3, pure, takes the active ratios, the index, the ideal, the grid sets and c); `levelToY` / `yToLevel` in dB (5.2); `levelDragDb(startLevel, dy, fine)` (4.2); `rulerLabels(length, bpm)` (2.2 slots); `labelGroups`; `formatTapReadout`; `gridLabel` (4.3); delete `getSnapDivisions`, `snapToGrid`, `calculateDefaultTapTime`, `determineConstraintAxis`, `applyAxisConstraint`, `kRulerHeight` and the other obsolete constants | 2.1, 2.2, 3.1, 3.3, 4.2, 4.3, 5.2, 5.5 | `tap_pattern_editor_test.cpp` (the snap-table cases at lines 510-635 are deleted; coordinate, hit-test and clamping cases are rewritten to the new geometry): "gridLines: 1/1 with Snap 1/8D has snap lines at 0.1875, 0.375, 0.5625, 0.75, 0.9375 and 1.0 and beat lines at quarters"; "gridLines: Length 1/8 with Snap 1/4 falls back to the 1/16 lines 0.5 and 1.0"; "gridLines: Snap 1/2 over Length 1/4 and Snap 1/1 over Length 1/2 fall back to 1/16"; "gridLines: ratio 1 is a snap line for 1/8D and 1/4T over 1/2"; "gridLines: no snap line at ratio 0 for any length and snap"; "gridLines: Free at 120 BPM has beat lines every 0.1 and bar lines every 0.4"; "gridLines: 1/4T over 1/1 has 6 snap lines, exact"; "snapToLines rounds to the nearest line, ties later, never 0"; "defaultTapTime is (i+1)/n x c" (c = 0.625 at 2/1, 60 BPM); "placeTap keeps a free stored value"; "placeTap moves a colliding tap to the nearest free snap line, later first" (the collision example of 3.3, Snap 1/8: tap 2 dragged to 0.625, then 4 -> 8: tap 7 lands at 0.5, taps 5, 6 and 8 keep 0.125, 0.375, 0.875); "the registered defaults reveal without a move" (`placeTap` on the `kDefaultCustomTimeRatios` table returns the stored value for every i at 4 -> 8 and 8 -> 16, Snap 1/4, Length 1/1, 120 BPM); "placeTap never returns 0 and falls back to the 1/16 then 1/64 grid"; "placeTap skips lines beyond the 5 s ceiling" (2/1 at 60 BPM: no result above 0.625); "levelToY: 1.0 -> 14, 0.5 -> 25.5, dbToGain(-48) -> 106, 0.003 -> silence band; yToLevel inverts within 1e-4 and the band gives 0"; "levelDragDb starts a silent tap at -48 dB" (level 0, dy -1 -> about -47.5 dB, not -143.5); "rulerLabels: 1/1 gives 1..4 with the 4 right-aligned at or left of x 842; Free ends with a right-aligned 5.0 s; no label beyond x 844"; "labelGroups joins taps within 8 px"; "formatTapReadout: 750.0 ms / 1.25 s / -6.0 dB / -inf dB and the grid field 3 × 1/8, 5 × 1/8T, absent off-grid, with Snap Off or with Shift" |
| 9 | `plugins/iterum/src/ui/tap_pattern_editor.{h,cpp}`: `Palette`, `TapEditSink`, `setTempo`, `setCustomLength`, `setActiveTapCount` (reveal only, no reveal state), `placeRevealedTaps(oldCount, newCount)`, `setPresetPattern` / `setEditable`, `resetToDefault` (3.4), double-click through `placeTap` (ruling 5: time and level), `dispatchEvent` override for Ctrl/Cmd-click (4.2, ruling 5: level only, consumed ahead of CControl's default-value listener, on a tap or not), `static CCursorType cursorForZone(HitZone, bool editable)` (4.3, finding 2: body `kCursorHSize`, handle `kCursorVSize`, grip `kCursorSizeAll`, else and read-only `kCursorDefault`) and a public `hitZoneAt(x, y)` returning the zone and tap the hit test of 4.1 finds, drawing (5), mouse (4: zones, grip axis rule, level drag from the floor, Shift fine + bypass with re-base), focus and Escape (4.2), popup (4.3), `removed()` cleanup; the constructor's initial ratios from `kDefaultCustomTimeRatios` (row 3); delete `onPatternChanged`, the axis-lock code, `invalidatePattern` | 3, 4, 5 | `plugins/iterum/tests/unit/ui/tap_pattern_editor_view_test.cpp` `[iterum][ui][tap-editor]`, constructing the view at (0,0,852,140) with a recording sink and driving `onMouseDown` / `onMouseMoved` / `onMouseUp` / `onKeyDown` directly, and `dispatchEvent` with a `MouseDownEvent` for the Ctrl/Cmd cases (no frame; the popup and cursor-setting paths check for a null frame, so the cursor is checked through `cursorForZone`): "fresh pattern, Snap 1/4: a 3 px body drag of tap 1 does not land on tap 4's ratio" (fresh 4 taps from the registered defaults, Length 1/1, 120 BPM: press on tap 1's body, move 3 px right, release: tap 1's time is 0.25, never 1.0, and no perform was written since the value did not change; finding 1); "the cursor per zone" (on a fresh pattern with tap 2 at -46 dB and tap 3 silent: `cursorForZone(hitZoneAt(...))` is `kCursorHSize` over tap 1's body, `kCursorVSize` over tap 1's handle, `kCursorSizeAll` over the grips of taps 2 and 3, `kCursorDefault` over empty plot and over every zone in read-only mode; finding 2); "Ctrl-click on tap i writes level 1.0 only" (tap 2 at level 0.5: one begin, one perform of 1.0 and one end on 967, nothing on 951; time unchanged; finding 3); "Ctrl-click never reaches CControl's default-value path" (the view's CControl value set to 0.3 with default 0.5; a Ctrl-click on a tap and one on empty space each leave `getValue() == 0.3`, mark the event consumed, write nothing for the empty-space click, and the follow-up moves change nothing); "Ctrl-click in read-only mode writes nothing and is consumed"; "setActiveTapCount reveals stored taps and writes nothing"; "placeRevealedTaps(old, new) moves only colliding taps in [old, new), bracketed"; "a body drag changes time only, snapped, with one begin and one end and no perform inside the dead zone"; "a snapped drag with Length 1/8 and Snap 1/4 never lands on ratio 0"; "a handle drag changes level only, never time"; "Shift drag with Snap 1/4 lands off-grid, 10:1" (an 80 px Shift drag of a tap at 0.25 lands at 0.25 + 8/802, not on a line); "pressing Shift mid-drag does not move the tap"; "a silent tap can be raised by a 10 px upward drag" (level becomes dbToGain(about -42.8 dB), time unchanged); "a silent tap can be dragged in time" (horizontal first move); "a -46 dB tap can be dragged in time"; "a click on a tap writes nothing"; "double-click resets to (i+1)/n at 0 dB with both ids bracketed" (Reset positions, no collision); "double-click keeps a free ideal" (fresh 4 taps: a double-click on tap 1 at level 0.5 leaves its time at 0.25 and writes level 1.0, both ids bracketed); "double-click moves a tap through placeTap and never onto another tap" (fresh 8 taps, Snap 1/4: a double-click on tap 5 at 0.125 lands at 0.6875, level 1.0; ruling 5 keeps this time move, 3.4); "Reset at 2/1 and 60 BPM divides the playable 0.625" (8 taps at 0.078 .. 0.625); "Escape restores the pre-drag value with one perform then end"; "a silent tap is hit in the silence band"; "the dragged tap ignores incoming sets of its own ids"; "Reset writes active taps only"; "read-only mode ignores mouse down and double-click" |
| 10 | `specs/iterum-ui-redesign/xslt/multitap.xsl` -> `plugins/iterum/resources/editor.uidesc` (section 6); `specs/iterum-ui-redesign/panels/multitap.md` 2.7 / 2.8 and design-system 7.9 / 8.2 amended to section 6 | 6 | `editor_panels_test.cpp`: MultiTap row table + `{kMultiTapCustomLengthId, W::M}`; "Iterum editor MultiTap: ..." conditional case rewritten: the fieldset (parent of 920) visible at 2/19, 15/19, 19/19; 913, 914, 922, 923 visible only at 19/19; 921 only below; Copy and Reset never both; "the Copy caption masks the fieldset border" (the `CTextLabel` carrying tag 921 has `getTransparency() == false` and back colour `panel`; finding 7). `node tools/uidesc-inventory.js plugins/iterum/resources/editor.uidesc --check --baseline specs/iterum-ui-redesign/inventory-before.json` prints `UIDESC CLEAN` |
| 11 | `plugins/iterum/tests/unit/vst/tap_editor_screenshot_test.cpp` (`Iterum_TapEditorScreenshots`, hidden): shots after the change into `ITERUM_TAP_SHOTS_DIR`: `custom-4taps.png` (fresh, Length 1/1, Snap 1/4: the taps on beats 1-4, identical to Reset), `custom-8taps.png` (4 -> 8 through the user path: `setParamNormalized` + `performEdit` per step: every eighth, nothing written), `custom-16taps.png` (every sixteenth), `custom-8taps-snap8.png`, `custom-8taps-snap-off.png` (the visual review checks that the beat and bar grid is readable with Snap Off), `custom-8taps-levels.png` (the same eight levels through the parameters only, one of them silent and one at -46 dB; the by-hand `setTapLevel` of today's case goes, which proves 3.6), `custom-8taps-free.png` (Length Free), `custom-8taps-2bars-60bpm.png` (Length 2/1, Tempo 60, after Reset: the over-ceiling region and the eight taps dividing the playable 5 s), `custom-length8-snap4.png` (Length 1/8, Snap 1/4: the 1/16 fallback lines), `preset-quarter-8taps.png` (Timing Pattern Quarter, read-only, on 2/1), `preset-quarter-16taps.png` (read-only on Free, taps 10-16 stacked at 5 s), `preset-golden-8taps.png`. The visual review of the three `preset-*.png` shots names the fieldset's title line explicitly: no border stroke through the "Pattern" caption or the Copy button (finding 7, section 6) | visual review | the case itself (bytes > 0 per shot) |
| 12 | `plugins/iterum/CHANGELOG.md`, the open `[1.0.0]` entry: Added "Pattern Length for the custom tap pattern: the custom pattern now follows the song's tempo (1/8 to 2/1), Free keeps the 5 s axis; patterns saved before load as Free and sound the same"; Changed "the tap editor shows a musical grid with labels, the taps of every pattern, dB levels, and the time and level of the tap under the pointer; Shift drags finely and off the grid; Ctrl/Cmd-click resets a tap's level to 0 dB; a new instance's custom pattern starts synced at 1/1 with its taps on the beats, and taps added with the Taps slider fill the eighths, then the sixteenths"; Fixed "raising Taps no longer stacks new taps or overwrites the ones you placed; Copy to Custom reproduces the times and levels the pattern plays at the current tempo; morphing into Custom glides to the custom pattern; tap edits are recorded by host automation" | | |

Verification after both: build `dsp_effects_tests plugin_tests approval_tests`, zero warnings; one `run-gate.js` call for
the three executables with the filter above; `tools/pluginval.exe --strictness-level 5` on Iterum.vst3;
`./tools/run-clang-tidy.ps1 -Target iterum -BuildDir build/windows-ninja`; `node tools/lint-plugin-editor-baseline.js`;
the screenshot case by name for the visual review.

---

## 9. Rulings

The user ruled on 2026-10-29 (`open-items.md`), choosing from multiple-choice options. No open question remains. The
five rulings come first, then the seven challenge findings left open after the second round, each accepted with the
challenger's fix as written. For each one: what was decided, and every section, row and case it changed.

**R1. Default Length of a fresh instance: `1/1` (synced).** Matches the release's rule that every tempo-capable mode
starts synced (Tape, Spectral, Shimmer, BBD, Reverse). Rejected: Free. Changed: 2.1 (914 default index 4, now
attributed), 7.2 (a fresh instance is not a saved state), 7.4 (unchanged default 4), row 12 (CHANGELOG). Saved
states are unaffected: a state without the tail loads Free (7.1, 7.2).

**R2. Tempo transport: the hidden read-only output parameter 915.** Rejected: a DataExchange block. Changed: 2.4
(attributed; and, with F4, delivery to the view is synchronous under the `[UI-thread & Connected]` contract, not
moved to the UI thread by the bridge). Rows 2, 3, 4 and 6 as before. Known cost, accepted: a host that ignores
`kIsHidden` lists a read-only "MultiTap Tempo" in its parameter list.

**R3. Registered Custom Time defaults: option C.** Taps 1-4 at 0.25, 0.5, 0.75, 1.0; taps 5-8 at the odd eighths;
taps 9-16 at the odd sixteenths. A fresh 4-tap pattern equals Reset, a fresh 8 is every eighth, a fresh 16 every
sixteenth, all distinct. Rejected: A, consecutive sixteenths (its first snapped drag of taps 1-3 stacks on tap 4, F1),
and B, keep `(i+1)/17` (off every grid). **The cost, accepted:** tap numbers above 4 no longer run left to right, so
the index-based pans (Cascade, Alternating, Widening Stereo) do not follow time order on a fresh pattern, and Reset /
double-click ideals (`D(i, n)`, index order) differ from the fresh layout at every count but 4. Decaying Level also
goes by index but never reaches a Custom pattern, whose custom levels override it in every block (2.5). Option A pays
the same index-order cost as soon as one tap is moved. Changed: P1 and P5 rows of section 1, 'Left alone' (index
numbering), 3.1 (the table, the one shared `kDefaultCustomTimeRatios`, the cost for C and A stated), 3.3 (worked
examples: fresh reveals move nothing; Reset at 4 then 4 -> 8 gives evenly spaced eighths; a new collision example),
3.4 (double-click examples), 7.2 (pre-spec-046 states), 7.4, rows 3, 8, 9, 11 and 12.

**R4. Length menu: Free, 1/8, 1/4, 1/2, 1/1, 2/1; no 4/1.** Changed: 2.1 (attributed). Nothing else: the design had
this set.

**R5. Per-tap reset gestures: Ctrl/Cmd-click resets the tap's level to 0 dB; double-click keeps the time-and-level
move of 3.4.** Ctrl/Cmd-click is bracketed begin / perform / end on the level id only and is intercepted ahead of
CControl's default-value listener (the view overrides `dispatchEvent`). Double-click still moves the tap in time to
its ideal through `placeTap` and sets 0 dB, so a quick double-click can move a tap far (the 3.4 example: 0.125 ->
0.6875); Ctrl/Cmd-click is the level-only reset that never moves time. Changed: P16 row, 3.4 (double-click stated as
ruled, with examples), 3.5 (a Ctrl/Cmd-click row), 4.2 (the premise corrected, the mechanism), 5.9 (read-only mode
consumes it), rows 9 and 12.

**F1 (design 3.1, 2.2, 4.2, old Q3). A fresh instance contradicted its own default grid.** Resolved by R3. Changed:
3.1 (cost restated for C and for A), 3.3 (the Reset-then-4 -> 8 example is now evenly spaced eighths), row 9 case
"fresh pattern, Snap 1/4: a 3 px body drag of tap 1 does not land on tap 4's ratio", row 8 case "the registered
defaults reveal without a move".

**F2 (4.3). The hand cursor hid the body = time, handle = level split.** Accepted: `kCursorHSize` over a body (as
today), `kCursorVSize` over a handle, `kCursorSizeAll` over a grip, `kCursorDefault` elsewhere and in read-only mode;
no hand. Changed: 4.3 (one `cursorForZone` function), row 9 (`cursorForZone`, `hitZoneAt`, the case "the cursor per
zone").

**F3 (4.2, 3.4). "Ctrl/Cmd-click cannot be made to work" was false.** Accepted, resolved by R5. The premise is
corrected in 4.2 from the source: `CView::dispatchEvent` is virtual (`cview.h:96`), listeners run in reverse
registration order and stop at the first consumer (`cview.cpp:666-675`), CControl registers its listener in its
constructor (`ccontrol.cpp:68-72`), so a listener registered in the view's constructor would run first, and an
override of `dispatchEvent` runs before any listener. Changed: 4.2, 3.5, row 9 cases "Ctrl-click on tap i writes
level 1.0 only", "Ctrl-click never reaches CControl's default-value path", "Ctrl-click in read-only mode writes
nothing and is consumed".

**F4 (3.6, 2.4, row 6). Parameter changes reach the bridge synchronously, not deferred.** Accepted, option (1): rely
on the VST3 threading contract (`setParamNormalized` is `[UI-thread & Connected]`), state that delivery is
synchronous (`setNormalized` -> `changed()` -> `triggerUpdates`, every dependent on the calling thread; only
`deferUpdate` queues), skip the per-id pushes while `bulkParamLoad_` is set, and do one full push from `syncAllViews`
through `tapEditor()`. Changed: P12 row, 2.4, 3.3 (`placeRevealedTaps` no longer waits for a deferred 902), 3.6 (the
bridge and bulk-load bullets rewritten), row 6 (`isBulkParamLoad`, `pushToTapEditor` / `pushAllToTapEditor`,
`syncAllViews` rewritten instead of removed; the case "setParamNormalized does not touch the view before the deferred
update" replaced by "a set of 950 or 966 is reflected in the view immediately", plus "a bulk load pushes once, after
the load"), new row 6a (correct `.claude/skills/vst-guide/THREAD-SAFETY.md:121` in the build stage).

**F5 (2.6, row 5). `patternTap` ignored the DSP's millisecond clamps.** Accepted: `patternTap(..., bpm)` computes
milliseconds exactly as the DSP clamps them (tempo to [20, 300], base to [1, 5000] before any ratio, per-tap
`min(.., 5000)`, Linear's `maxT = min(B x n, 5000)`); a synced Length only when no clamp engages at that tempo,
otherwise Free at the played times. Changed: P11 row, 2.6 (signatures, every formula with its DSP line, the 955.6 ms
and 30 BPM examples, Copy step 3), 5.9 (the read-only Length follows the same rule), rows 5 (the mirror test compares
with `patternTap(...).ms`; cases for Linear with 16 taps at 90 BPM and Note Value Whole at 30 BPM for Primes,
Fibonacci and Linear) and 6 (a Linear Copy case).

**F6 (row 4). The processor test could not link and risked a stack overflow.** Accepted: `processor.cpp` compiled
into `plugin_tests`, a `const MultiTapDelay& multiTapDelayForTest() const` accessor, the processor heap-allocated
(`owned(new Processor)`) and brought up with `initialize`, `setupProcessing`, `setActive(true)`; the test TU is a
consumer of `processor.cpp` / `processor.h` for gate scoping. Changed: row 4, the gate paragraph of section 8.

**F7 (section 6, rows 8 / 8a). The transparent Copy caption would show the fieldset border.** Accepted: the caption
gets `transparent="false" back-color="panel"` and keeps tag 921; `CopyPatternButton` already paints an opaque fill
(`CTextButton` gradient fill, `cbuttons.cpp:949-951`), confirmed; the visual review of the `preset-*.png` shots names
the title line. Changed: section 6 (rows 8 and 8a split, the title-line paragraph, the XSLT step), rows 7, 10 (a
transparency assertion) and 11 (the review note).

---

## 10. Challenges considered

All 21 challenges of the 2026-10-29 review were accepted; none was found wrong. Where a challenge offered options,
the one taken is named.

1. Quiet and silent taps get stuck: level drags start from max(dB, -48) (4.2); short and silent taps have one grip,
   axis by first move (4.1, 4.2); view cases for a 10 px raise and a -46 dB time drag (row 9).
2. Snap unit longer than the Length: ratio 1 is always a snap line, ratio 0 never, a 1/16 fallback below two lines
   (2.2, 2.3); gridLines cases (row 8).
3. Shift did not leave the grid: Shift is fine adjust and snap bypass, with re-base (4.2); 'Left alone' bullet 2
   rewritten; case "Shift drag with Snap 1/4 lands off-grid, 10:1" (row 9).
4. Two counting systems: every number is a delay from the input; ruler in beats from 1 at the first beat line, the
   readout as "k × unit", the host in ms only (2.1, 2.2, 4.3, 4.4).
5. The ruler's right end: tempo in the gutter, end label right-aligned left of ratio 1, Length string dropped, Free
   ruler in ms only, all ticks inside 114..122 (2.2, 5.1).
6. Grid contrast and hierarchy: snap lines drawn first, beat and bar lines on top in `text-dim` 25 % / 45 %; ruler
   accent snap ticks keep Snap 1/4 distinct from Off; visual review of the snap-off shot (5.3, 5.7, row 11).
7. Two default rules: stated honestly (3.1, P5); double-click goes through `placeTap` (3.4); a third registered-
   default option was offered, and ruling R3 chose it (section 9); the double-click cases are now "double-click
   keeps a free ideal" and "double-click moves a tap through placeTap and never onto another tap" (row 9).
8. Ceiling ignored: placement candidates and Reset are limited to the playable fraction c (2.1, 3.1, 3.3, 3.4).
9. revealBase_ consumed by the wrong gesture: removed; the controller hands `placeRevealedTaps(old, new)` the pair
   from setParamNormalized / performEdit (3.3); case "automation 4 -> 8, then a user 8 -> 9 moves only tap 9" (row 6).
10. Spatial levels ignored by the display and Copy: `playedLevelDb` (2.6), used by Copy and the read-only display
    (5.9); 901 added to the recompute triggers; DSP cases for Decaying and Flat under Custom (row 1); the mirror test
    covers every spatial pattern (row 5).
11. Read-only axis differed from Copy's: the read-only pattern is drawn on the Length Copy would choose (5.9).
12. Copy clamped taps past 8 beats to the 2/1 end: such patterns copy to Free at the played times (2.6); the
    exactness claim is restated; Golden 8-tap case (row 6).
13. Dirty flag would lose custom levels to Decaying / Flat and after a morph: no dirty flag; the apply runs in every
    block under Custom after `updateMorph` (2.5); cases for both (row 1).
14. P13 could not work through the plugin path: the processor pushes the custom values every block before the
    pattern-change detection; `customTempoBPM_` starts at 120; the morph target is clamped (2.5); processor case
    (row 4).
15. Copy of long rhythmic patterns stacked at 2/1: the Free option taken, with the loss of tempo-following stated
    (2.6); Quarter 16-tap and Whole 4-tap cases (row 6).
16. Copy levels contradicted the mirror test: Copy writes the played levels (-3 dB x i unless Flat), the Copy case
    expects dbToGain(-3 x i) and a Flat case 1.0 (2.6, 5.9, row 6).
17. View updates from any thread: a `TapEditorParamBridge` FObject with parameter dependencies, as
    `VisibilityController` (3.6). Its premise of deferred delivery was wrong; finding F4 (section 9) replaced it with
    synchronous delivery under the `[UI-thread & Connected]` contract and one push after a bulk load.
18. `findControlByTag` is debug-only: moved out of the guard (3.6, row 6).
19. A decrease gesture could place taps: placement only for `[old, new)` of a growing user gesture (3.2, 3.3); case
    "a decrease gesture writes nothing, even after a non-gesture increase or editor open" (row 6).
20. Same as 2 (drag snap to ratio 0): resolved by the same rule; case "a snapped drag with Length 1/8 and Snap 1/4
    never lands on ratio 0" (row 9).
21. Generator tail: the "state that the generator is left alone" option taken (1 'Left alone', 7.2).
