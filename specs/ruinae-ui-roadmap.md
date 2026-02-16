# Ruinae UI Gaps Roadmap

**Date**: 2026-02-14
**Status**: Approved (In Progress)
**Last Updated**: 2026-02-16
**Context**: [ui-gaps-assessment.md](specs/ui-gaps-assessment.md)

---

## Layout Challenge

The current UI is 900x830px with 5 dense rows and very little free space. Adding ~30+ new parameters requires a layout strategy, not just appending controls.

### Current Layout
```
┌─────────────────────────────────────────────────────────────────┐
│  RUINAE                                    Preset Browser       │ 30px
├──────────┬──────────────────────┬──────────┬─────────┤
│  OSC A   │  Spectral Morph Pad  │  OSC B   │ Master  │ 160px
│  220px   │      300px           │  220px   │ 120px   │
├────────┬─────────┬─────────────────────────────────────┤
│ Filter │ Distor- │        Envelopes (x3)               │ 138px
│ 170px  │ tion    │        544px                         │
│        │ 154px   │                                      │
├─────────────────────────────────────────────────────────┤
│              Trance Gate (full width)                    │ 160px
│              Step sequencer + controls                   │
├─────────────────────────────────────────────────────────┤
│  LFO1/2/Chaos  │  Mod Matrix Grid  │  Mod Heatmap      │ 160px
│  tabs 158px    │  340px            │  360px             │
├─────────────────────────────────────────────────────────┤
│  Effects: Delay / Reverb / Phaser (collapsible strips)  │ 160px
└─────────────────────────────────────────────────────────┘
                          900px
```

### Layout Strategy

Window height may increase modestly (830 → ~866px) to accommodate the Global Filter strip. Otherwise, use patterns already established in the UI:

1. **Embed in existing sections** - Master section has room for Voice Mode + Stereo controls
2. **Conditional visibility** - Mono controls only visible when mono selected
3. **Expandable detail panels** - Like FX strip: collapsed summary, expand for details
4. **Dropdown selector** - Replace mod source tabs with a dropdown to accommodate 5+ sources
5. **Slide-out drawer** - For rarely-changed global settings (tuning, velocity curve, etc.)

---

## Phase 0: Layout Restructuring

Before adding features, restructure the Master section and Modulation row to create slots for new content.

### 0A. Expand Master Section into a "Voice & Output" Panel -- DONE (Spec 052 + 054)

**Current**: Master has only 3 controls (Output knob, Polyphony dropdown, Soft Limit toggle) in 120x160px.

**Proposed**: Reorganize to accommodate Voice Mode, Stereo, and a settings gear icon:

```
┌─ Voice & Output ──────┐
│ [Poly ▼] [⚙]          │  Voice mode dropdown + Settings gear
│                        │
│     (Output)           │  Output knob (centered)
│      36x36             │
│                        │
│  (Width) (Spread)      │  Two 28x28 knobs side by side
│                        │
│  [Soft Limit]          │  Toggle at bottom
└────────────────────────┘
```

This fits within the existing 120x160 footprint by tightening spacing. The `[⚙]` opens a settings overlay (Phase 5).

### 0B. Replace Mod Source Tabs with Dropdown Selector -- DONE (Spec 053)

**Current**: Mod source tabs are `LFO1 | LFO2 | Chaos` sharing a 158x120 view area.

**Problem**: Adding Macros and Rungler makes 5 sources; Phase 6 adds 5 more (10 total). Tabs don't scale.

**Solution**: Replace the tab bar with a `COptionMenu` dropdown selector:

```
┌─ Modulation Source ──────┐
│ [LFO 1            ▼]    │  Dropdown replaces tab bar
│ ┌──────────────────────┐ │
│ │  (Rate)  (Depth)     │ │  View area unchanged (158x120)
│ │  [Shape▼] [Sync]     │ │  Switches content based on selection
│ │  ...                 │ │
│ └──────────────────────┘ │
└──────────────────────────┘
```

**Dropdown items**: LFO 1, LFO 2, Chaos, Macros, Rungler (Phase 4), Env Follower, S&H, Random, Pitch Follower, Transient (Phase 6)

This uses the same `UIViewSwitchContainer` pattern already used for oscillator types and filter types. The dropdown takes ~20px height, leaving ~100px for the source view — comparable to the current tab layout.

### 0C. Add Global Filter Strip to Row 2 -- DONE (Spec 055)

**Current Row 2**: `Filter (170) | Distortion (154) | Envelopes (544)` = 868px

**Option A** - Compress envelopes and add Global Filter:
```
│ Filter │ Distort │  Envelopes (x3)        │ Glob Filt │
│ 160px  │ 144px   │  420px                  │ 144px     │
```

**Option B** - Global Filter as a collapsible strip between Row 2 and Row 3 (adds ~36px height):
```
├─ Global Filter: [On] Type▼  ──(Cutoff)── ──(Resonance)── ─┤ 36px
```

**Decision**: Option B. A slim horizontal strip keeps Row 2 untouched and mirrors the FX strip pattern. The 36px height increase (830 -> 866) is acceptable.

---

## Phase 1: Quick Wins (Minimal Layout Change)

**Goal**: Expose already-registered parameters that just need uidesc controls.

### 1.1 Voice Mode Selector -- DONE (Spec 054)
- **What**: Add Poly/Mono dropdown to Master section
- **Param**: `kVoiceModeId` (1) - already registered
- **Where**: Replace or sit alongside Polyphony dropdown in Master section
- **Effort**: Small (1 uidesc control + visibility wiring)
- **Unlocks**: Access to mono mode
- **Implemented**: COptionMenu with "Polyphonic"/"Mono" items, "Mode" label, control-tag="VoiceMode" tag="1"

### 1.2 Trance Gate Tempo Sync Toggle -- DONE (Spec 055)
- **What**: Add sync toggle button next to the Trance Gate rate/note controls
- **Param**: `kTranceGateTempoSyncId` (606) - already registered
- **Where**: Trance Gate toolbar, next to existing Rate/NoteValue
- **Effort**: Small (1 toggle + rate/note visibility switch — same pattern as LFO sync)
- **Unlocks**: Tempo-synced trance gate
- **Implemented**: Sync toggle in Trance Gate toolbar (leftmost position after On/Off), Rate/NoteValue visibility switching via controller, new NoteValue dropdown with tempo-synced rate values

### 1.3 Stereo Width & Spread -- DONE (Spec 054)
- **What**: Two knobs for stereo field control
- **Params**: `kWidthId` (4), `kSpreadId` (5) - defined in spec 054
- **Where**: Master/Voice section (see Phase 0A layout)
- **Effort**: Medium (2 new param IDs + processor wiring + 2 uidesc knobs)
- **Unlocks**: Stereo field shaping
- **Implemented**: Width (0-200%, default 100%) via `engine_.setStereoWidth()`, Spread (0-100%, default 0%) via `engine_.setStereoSpread()`, EOF-safe state loading for backward compatibility

**Phase 1 total**: ~3-5 controls, minimal layout disruption

---

## Phase 2: Global Filter Section -- DONE (Spec 055)

**Goal**: Expose the fully-implemented global stereo filter.

### 2.1 Global Filter Strip -- DONE (Spec 055)
- **What**: Horizontal strip with Enable toggle, Type dropdown, Cutoff knob, Resonance knob
- **Params**: `kGlobalFilterEnabledId` (1400), `kGlobalFilterTypeId` (1401), `kGlobalFilterCutoffId` (1402), `kGlobalFilterResonanceId` (1403) - all registered
- **Where**: New slim strip between Row 2 and Row 3 (Phase 0C, Option B)
- **Layout**: `[On/Off] [Type ▼]  ─(Cutoff)─  ─(Resonance)─`
- **Effort**: Medium (layout height change + 4 uidesc controls)
- **Dependencies**: Phase 0C layout change
- **Implemented**: 36px-high strip with "global-filter" accent color, On/Off toggle, Type dropdown (LP/HP/BP/Notch), Cutoff and Resonance knobs with labels beside (right), window height 830→866px

---

## Phase 3: Mono Mode Panel -- DONE (Spec 056)

**Goal**: Make mono mode usable once Voice Mode selector exists (Phase 1.1).

### 3.1 Mono Mode Controls (Conditional) -- DONE (Spec 056)
- **What**: Priority, Legato toggle, Portamento Time, Portamento Mode
- **Params**: `kMonoPriorityId` (1800), `kMonoLegatoId` (1801), `kMonoPortamentoTimeId` (1802), `kMonoPortaModeId` (1803) - all registered
- **Where**: Conditionally visible panel that appears when Voice Mode = Mono. Options:
  - **A) Inline in Master section**: Replace polyphony dropdown area (polyphony is irrelevant in mono)
  - **B) Slide-out panel**: Small panel that appears below/beside Master when mono is selected
  - **C) Popover from Voice Mode dropdown**: Click mono triggers a popover with 4 controls
- **Recommendation**: Option A — when Mono is selected, Polyphony dropdown hides and Mono controls appear in the same space. Natural conditional swap.
- **Layout**:
  ```
  Mono mode:                    Poly mode:
  ┌────────────┐                ┌────────────┐
  │ [Mono ▼]   │                │ [Poly ▼]   │
  │ [Legato]   │                │ Voices: [8]│
  │ Priority▼  │                │            │
  │ (Porta)    │                │            │
  │ PortaMode▼ │                │            │
  └────────────┘                └────────────┘
  ```
- **Effort**: Medium (conditional visibility + 4 controls in existing space)
- **Dependencies**: Phase 1.1 (Voice Mode selector must exist)
- **Implemented**: Option A — conditional visibility swap in Master section. Mono mode shows Priority dropdown, Legato toggle, Portamento Time knob, and Portamento Mode dropdown. Poly mode shows Polyphony dropdown. Voice Mode selector switches between the two panels.

---

## Phase 4: Macros & Rungler -- DONE (Spec 057)

**Goal**: Give users direct control over Macros 1-4 and the Rungler.

### 4.1 Define Parameter IDs -- DONE (Spec 057)
- **What**: Create param IDs for Macros 1-4 values, Rungler configuration (osc freqs, depth, filter, bits, loop mode)
- **Scope**: 4 macro params + 6 rungler params = 10 new IDs (Clock Div/Slew deferred)
- **Where**: `plugin_ids.h`, parameter registration in controller, processor wiring
- **Effort**: Medium-Large (full param pipeline for 10 params)
- **Implemented**: Macro IDs 2000-2003, Rungler IDs 2100-2105. MacroParams and RunglerParams structs with full register/handle/format/save/load functions. ModSource::Rungler inserted at enum position 10 with enum migration for backward compatibility. State version bumped to 13.

### 4.2 Macro Knobs View -- DONE (Spec 057)
- **What**: "Macros" view in Modulation row showing 4 macro knobs
- **Where**: Mod source dropdown (Phase 0B)
- **Layout**:
  ```
  ┌─ Macros ─────────────┐
  │ (M1)  (M2)  (M3) (M4)│  4 knobs
  │ Macro1 Macro2 ...     │  Labels
  └───────────────────────┘
  ```
- **Effort**: Small (4 knobs in existing dropdown infrastructure)
- **Dependencies**: Phase 4.1, Phase 0B
- **Implemented**: 4 ArcKnobs (28x28) in ModSource_Macros template at x=4,42,80,118 with M1-M4 labels

### 4.3 Rungler Configuration View -- DONE (Spec 057)
- **What**: Full Rungler configuration accessible via mod source dropdown
- **Where**: Mod source dropdown (Phase 0B)
- **Layout** (6 params exposed, Clock Div/Slew deferred):
  ```
  ┌─ Rungler ─────────────────────┐
  │ (Osc1 Freq) (Osc2 Freq)      │  Oscillator frequencies
  │ (Depth)     (Filter)          │  Modulation depth + filter
  │ (Bits)      [Loop]            │  Shift register bits + loop toggle
  └───────────────────────────────┘
  ```
- **Effort**: Medium (new dropdown view + 6 controls)
- **Dependencies**: Phase 4.1, Phase 0B
- **Implemented**: Row 1: 4 ArcKnobs (Osc1/Osc2/Depth/Filter). Row 2: Bits ArcKnob + Loop ToggleButton. Log frequency mapping (0.1-100 Hz). Rungler integrated into ModulationEngine as active modulation source.

---

## Phase 5: Settings Panel & Mod Matrix Detail

**Goal**: Expose lower-priority global settings and mod matrix per-slot detail parameters.

### 5.1 Settings Drawer
- **What**: A gear icon (in Master section) opens a slide-out drawer from the right edge with:
  - Pitch Bend Range
  - Velocity Curve selector
  - Tuning Reference (A4 Hz)
  - Voice Allocation Mode
  - Voice Steal Mode
  - Gain Compensation toggle
- **Params**: All need new IDs (~6 new params)
- **Design**: Drawer slides in from the right, overlapping part of the main UI. Non-modal — user can close by clicking outside or pressing the gear icon again. Contains a vertical list of labeled controls.
- **Implementation**: VSTGUI `CViewContainer` with animation (`CViewContainer::setViewSize` + timer for slide). Gear icon toggles drawer visibility.
- **Effort**: Medium-Large (drawer infrastructure + 6 new param IDs + wiring)

### 5.2 Mod Matrix Detail Expansion
- **What**: Per-slot access to Curve, Smooth, Scale, Bypass
- **Params**: IDs 1324-1355 - already registered
- **Options**:
  - **A) Expand row on click**: Clicking a mod matrix row reveals detail controls below it
  - **B) Detail panel**: Selecting a slot shows Curve/Smooth/Scale/Bypass in a side panel
  - **C) Right-click context**: Right-click slot for detail popover
- **Recommendation**: Option B — a detail strip below the ModMatrixGrid that updates when a slot is selected. Fits naturally in existing space if the heatmap compresses slightly.
- **Layout**:
  ```
  ┌─────────────────────────────────────┐
  │  Mod Matrix Grid (8 slots)          │
  ├─────────────────────────────────────┤
  │ Slot 3: [Curve▼] (Smooth) (Scale) [Bypass] │  Detail strip
  └─────────────────────────────────────┘
  ```
- **Effort**: Medium (detail strip + selection state + 4 controls per slot)

---

## Phase 6: Additional Modulation Sources

**Goal**: Wire up all 5 remaining mod sources that exist in the architecture but have no implementation. All five are equal priority and will be implemented together.

Each source needs: DSP implementation, parameter IDs, processor wiring, controller registration, and a dropdown view in the mod source selector (Phase 0B infrastructure).

### 6.1 Env Follower
- Tracks amplitude of the input signal for modulation
- **Params**: Sensitivity, Attack, Release (~3 params)
- **DSP**: RMS or peak envelope detection on input buffer
- **View**: 3 knobs in mod source dropdown view

### 6.2 Sample & Hold
- Samples a random value at a configurable rate, holds until next trigger
- **Params**: Rate, Sync, NoteValue, Quantize (~4 params)
- **DSP**: Clock + random generator with optional quantization
- **View**: Rate knob + Sync toggle + NoteValue dropdown + Quantize knob

### 6.3 Random
- Continuous smoothed random modulation (like a very slow noise source)
- **Params**: Rate, Smooth, Range (~3 params)
- **DSP**: Interpolated random walk or filtered noise
- **View**: 3 knobs in mod source dropdown view

### 6.4 Pitch Follower
- Tracks pitch of the input signal, maps to modulation value
- **Params**: Range (semitones), Response speed (~2 params)
- **DSP**: Autocorrelation or YIN pitch detection on input
- **View**: 2 knobs in mod source dropdown view

### 6.5 Transient Detector
- Detects audio transients (onsets) and generates trigger/envelope
- **Params**: Sensitivity, Attack, Release (~3 params)
- **DSP**: Spectral flux or energy-based onset detection
- **View**: 3 knobs in mod source dropdown view

**Total Phase 6**: ~15 new param IDs, ~15 uidesc controls, 5 new DSP processors

**Note**: The mod matrix Source dropdown already lists these sources — they just produce zero signal. Once DSP is wired, they become functional mod sources immediately. The dropdown views (Phase 0B) let users configure their behavior.

---

## Implementation Order Summary

```
Phase 0: Layout prep                          ┐
  0A. Restructure Master → Voice & Output     │ Foundation
      ✅ DONE (Spec 052 layout + Spec 054     │
         wiring)                              │
  0B. Replace mod source tabs with dropdown   │
      ✅ DONE (Spec 053)                      │
  0C. Add Global Filter strip slot             │
      ✅ DONE (Spec 055)                      ┘

Phase 1: Quick wins                           ┐
  1.1 Voice Mode selector                     │
      ✅ DONE (Spec 054)                      │
  1.2 Trance Gate sync toggle                 │
      ✅ DONE (Spec 055)                      │
  1.3 Stereo Width/Spread knobs               │
      ✅ DONE (Spec 054)                      ┘

Phase 2: Global Filter strip                  ┐ Post-mix filter exposed
      ✅ DONE (Spec 055)                      ┘

Phase 3: Mono Mode conditional panel
      ✅ DONE (Spec 056)                          Mono becomes usable

Phase 4: Macros & Rungler                     ┐ New param IDs needed,
  4.1 Define param IDs (10 params)            │ medium-large effort
      ✅ DONE (Spec 057)                      │
  4.2 Macro knobs view                        │
      ✅ DONE (Spec 057)                      │
  4.3 Rungler config view (6 params)          │
      ✅ DONE (Spec 057)                      ┘

Phase 5: Polish & detail                      ┐
  5.1 Settings drawer (slide-out)             │ Lower-priority params,
  5.2 Mod matrix detail strip                 ┘ quality-of-life

Phase 6: All 5 mod sources (equal priority)   ┐ Major DSP + UI work
  6.1 Env Follower                            │ ~15 new params,
  6.2 Sample & Hold                           │ 5 new DSP processors,
  6.3 Random                                  │ 5 new dropdown views
  6.4 Pitch Follower                          │
  6.5 Transient Detector                      ┘
```

### Dependency Graph

```
Phase 0A ✅ ──→ Phase 1.1 ✅ ──→ Phase 3 ✅ (mono needs voice mode selector)
Phase 0A ✅ ──→ Phase 1.3 ✅ (stereo knobs need space in Master)
Phase 0B ✅ ──→ Phase 4.2 ✅, 4.3 ✅ (dropdown needed for new source views)
Phase 0B ✅ ──→ Phase 6.1-6.5 (dropdown needed for new source views)
Phase 0C ✅ ──→ Phase 2 ✅ (filter strip needs layout slot)
Phase 4.1 ✅ ──→ Phase 4.2 ✅, 4.3 ✅ (UI needs param IDs)
Phase 5.1 ──→ (standalone, needs param IDs for settings)
Phase 5.2 ──→ (standalone, params already registered)
Phase 1-5 ──→ Phase 6 (complete existing features before adding new DSP)
```

---

## Effort Estimates by Phase

| Phase | New Param IDs | UIDESC Controls | Layout Changes | Relative Size | Status |
|-------|---------------|-----------------|----------------|---------------|--------|
| 0     | 0             | ~1 (dropdown)   | 3 sections     | Medium        | ✅ All done (0A: 052/054, 0B: 053, 0C: 055) |
| 1     | 2 (stereo)    | ~5              | Minimal        | Small         | ✅ All done (1.1/1.3: 054, 1.2: 055) |
| 2     | 0             | 4               | Height +36px   | Small-Medium  | ✅ Done (055) |
| 3     | 0             | 4               | Conditional    | Medium        | ✅ Done (056) |
| 4     | 10            | 10              | New dropdown views | Large     | ✅ Done (057) |
| 5     | ~6            | ~10             | Drawer + strip | Large         | Pending |
| 6     | ~15           | ~15             | 5 dropdown views + DSP | Very Large | Pending |

---

## Decisions Log

| # | Question | Decision |
|---|----------|----------|
| 1 | Window height | Allow increase: 830 → ~866px for Global Filter strip |
| 2 | Mod source selector | Dropdown (`COptionMenu`) replaces tabs — scales to 10+ sources |
| 3 | Settings panel style | Slide-out drawer from right edge, non-modal |
| 4 | Rungler scope | Full configuration — expose all params (~8 controls) |
| 5 | Phase 6 priority | All 5 mod sources equally — implement together |

---

## Spec Tracking

| Spec | Roadmap Items | Status | Branch |
|------|--------------|--------|--------|
| 052 - Expand Master Section | Phase 0A (layout/placeholders) | Merged | `052-expand-master-section` |
| 053 - Mod Source Dropdown | Phase 0B | Merged | `053-mod-source-dropdown` |
| 054 - Master Section Panel | Phase 0A (wiring) + Phase 1.1 + Phase 1.3 | Merged | `054-master-section-panel` |
| 055 - Global Filter & Trance Gate Tempo | Phase 0C + Phase 1.2 + Phase 2 | Merged | `055-global-filter-trancegate-tempo` |
| 056 - Mono Mode | Phase 3 | Merged | `056-mono-mode` |
| 057 - Macros & Rungler | Phase 4 (4.1 + 4.2 + 4.3) | Complete | `057-macros-rungler` |

### Next Up (unblocked)
- **Phase 5.1**: Settings drawer (standalone, needs param IDs for settings)
- **Phase 5.2**: Mod matrix detail strip (standalone, params already registered)
- **Phase 6**: All 5 mod sources (depends on Phase 1-5 completion — Phase 5 still pending)
