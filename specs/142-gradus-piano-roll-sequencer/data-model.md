# Phase 1 Data Model: Gradus Piano-Roll Step Sequencer

**Feature**: 142-gradus-piano-roll-sequencer
**Date**: 2026-05-23
**Status**: Complete (grilling-pass pivot applied 2026-05-23 — Sequencer Note is now lane 10 inside `ArpeggiatorCore`; held notes route through the existing `HeldNoteBuffer`).

This document specifies all entities introduced by this feature: their fields,
validation rules, defaults, state transitions, and where they live in the
codebase.

---

## Entity 1: Source Mode Parameter

| Property | Value |
|----------|-------|
| **Type** | VST3 `StringListParameter` (2 entries) |
| **VST3 ID** | `kArpSourceModeId = 3741` |
| **Internal name** | `ArpSourceMode` (for UIDesc control-tag) |
| **Display name** | "Source" |
| **Values** | `0 = Live`, `1 = Sequencer` |
| **Default** | `0` (Live) — per FR-001, FR-002 |
| **Automatable** | Yes (`kCanAutomate`) |
| **Persisted** | Yes (in state stream as int32, in v3 block) |
| **Atomic storage** | `std::atomic<int> sourceMode{0}` in `ArpeggiatorParams` |
| **Hidden** | No |

**Validation Rules**:
- Range: `[0, 1]`, clamped on load and on `handleArpParamChange`.
- Round-trip: writes 0 or 1 (no fractional); reads tolerate any normalized value
  via `std::clamp(static_cast<int>(std::round(value * 1.0)), 0, 1)`.

**State Transitions** (grilling-pass revised):
- `Live → Sequencer` or `Sequencer → Live`:
  - Audio thread emits note-off for any currently-sounding programmed note via `arpCore_`'s existing panic path.
  - `arpCore_.setSourceMode(newMode)` is called.
  - **Lane playheads are NOT reset** on the toggle (Q5-A). The new source picks up at the current playhead position. Explicit retrigger (`kArpRetriggerId = Note/Beat`) is the only mechanism that resets playheads — toggling Source does not.
  - The `HeldNoteBuffer` inside `arpCore_` is **not** cleared. Held notes continue to serve their role in both modes (arp source in Live; transposition root + base velocity + chord voicing context in Sequencer).
- Pending `midiDelay_` echoes survive the toggle (FR-025) — `MidiNoteDelay` is downstream and source-agnostic.

---

## Entity 2: Sequencer Note Lane (Parameter Group)

A new lane structurally analogous to the existing 9 Gradus arp lanes
(Velocity / Gate / Pitch / Modifier / Ratchet / Condition / Chord / Inversion /
MIDI Delay).

### Fields

| Param | VST3 ID | Type | Range | Default | Hidden | Persisted |
|-------|---------|------|-------|---------|--------|-----------|
| Length | `kArpSequencerNoteLaneLengthId = 3742` | `RangeParameter` | 1..32 (int) | 16 (FR-005) | No | Yes |
| Pitch step 0 | `kArpSequencerNoteLaneStep0Id = 3743` | `RangeParameter` | 0..127 (int) | 60 (FR-006) | Yes | Yes |
| Pitch step 1..30 | `3744..3773` | `RangeParameter` | 0..127 | 60 | Yes | Yes |
| Pitch step 31 | `kArpSequencerNoteLaneStep31Id = 3774` | `RangeParameter` | 0..127 | 60 | Yes | Yes |
| Rest flag step 0 | `kArpSequencerNoteLaneRestStep0Id = 3775` | toggle (`stepCount=1`) | 0..1 | 1 (FR-007) | Yes | Yes |
| Rest flag step 1..30 | `3776..3805` | toggle | 0..1 | 1 | Yes | Yes |
| Rest flag step 31 | `kArpSequencerNoteLaneRestStep31Id = 3806` | toggle | 0..1 | 1 | Yes | Yes |
| Lane Speed | `kArpSequencerNoteLaneSpeedId = 3807` | `Parameter` (discrete via `kLaneSpeedValues` mapping) | 0.25x..4.0x | 1.0x (index 3) | No | Yes |
| Lane Swing | `kArpSequencerNoteLaneSwingId = 3808` | `Parameter` | 0..75% | 0% | No | Yes |
| Lane Length Jitter | `kArpSequencerNoteLaneJitterId = 3809` | `RangeParameter` | 0..4 steps | 0 | No | Yes |
| Lane Speed Curve Depth | `kArpSequencerNoteLaneSpeedCurveDepthId = 3810` | `Parameter` | 0..1 | 0.5 | No | Yes |
| Playhead | `kArpSequencerNoteLanePlayheadId = 3811` | `Parameter` (output-only) | 0..1 (normalized step idx / 32) | 0 | Yes | No (ephemeral) |

**End-of-block sentinel**: `kArpSequencerNoteLaneEndId = 3811`.

**Total new IDs**: 71 (1 + 1 + 32 + 32 + 4 + 1 = 71). All ≥ 3741; none collide with the
3000-3372 Ruinae-shared block, the 3380-3445 / 3446-3495 / 3500-3507 / 3510-3740
existing Gradus blocks, or the 4000-4003 audition block.

### Atomic Storage (in `ArpeggiatorParams`)

```cpp
// --- Sequencer Note Lane ---
std::atomic<int>   sourceMode{0};                              // 0 = Live, 1 = Sequencer
std::atomic<int>   seqNoteLaneLength{16};                      // 1-32
std::array<std::atomic<int>, 32> seqNoteLanePitches{};         // 0-127, default 60
std::array<std::atomic<int>, 32> seqNoteLaneRestFlags{};       // 0/1, default 1 (rest)
std::atomic<float> seqNoteLaneSpeed{1.0f};                     // discrete via kLaneSpeedValues
std::atomic<float> seqNoteLaneSwing{0.0f};                     // 0-75%
std::atomic<int>   seqNoteLaneJitter{0};                       // 0-4 steps
std::atomic<float> seqNoteLaneSpeedCurveDepth{0.5f};           // 0-1
// (Playhead is NOT stored in params — it's emitted from the audio thread per block)

// Constructor initializes pitches[i] = 60, restFlags[i] = 1 for all i
ArpeggiatorParams() {
    // ... existing inits ...
    for (auto& p : seqNoteLanePitches) p.store(60, std::memory_order_relaxed);
    for (auto& r : seqNoteLaneRestFlags) r.store(1, std::memory_order_relaxed);
}
```

### Validation Rules

- `seqNoteLaneLength`: clamped to `[1, 32]` on load and write.
- `seqNoteLanePitches[i]`: clamped to `[0, 127]` on load and write.
- `seqNoteLaneRestFlags[i]`: stored as int `0` or `1` (any positive value → 1).
- `seqNoteLaneSpeed`: snapped to the nearest entry in `kLaneSpeedValues[]` (same
  mechanism as other lane speeds).
- `seqNoteLaneSwing`: clamped to `[0.0, 75.0]`.
- `seqNoteLaneJitter`: clamped to `[0, 4]`.
- `seqNoteLaneSpeedCurveDepth`: clamped to `[0.0, 1.0]`.

### State Transitions

The lane has no explicit lifecycle beyond what `ArpLane<uint8_t>` provides:
- Step advance on each lane-tick boundary, driven by `ArpeggiatorCore::fireStep` polymetric clocking (same infrastructure as the existing 9 lanes — grilling-pass pivot moved Sequencer Note inside `kNumLanes`).
- Length change mid-playback: handled identically to other lanes (next loop iteration wraps to new length per existing `ArpLane<T>::setLength()` behavior).
- Speed-curve table update: arrives via the existing `SpeedCurveTable` IMessage path and is installed into `arpCore_` via `setLaneSpeedCurveTable(9, ...)` (extended call site).
- **Conditional inert in Live mode**: when `sourceMode_ == Live`, `fireStep` skips lane 10's advance, so the lane stays at its last position and consumes zero cycles.

---

## Entity 3: Sequencer Pattern (Runtime Concept)

This is **not** a persisted entity — it's the audio-thread view of the programmed pattern, derived from `seqNoteLanePitches` + `seqNoteLaneRestFlags` + `seqNoteLaneLength` atomics, with pitch storage backed by `ArpeggiatorCore::seqNoteLane_` (an `ArpLane<uint8_t>`).

| Field | Source | Audio-thread access |
|-------|--------|---------------------|
| `length` | `params.seqNoteLaneLength.load()` | Pushed into `arpCore_.seqNoteLane_.setLength()` at the existing `applyParams` sync point |
| `pitches[32]` | `params.seqNoteLanePitches[i].load()` | Pushed into `arpCore_.seqNoteLane_.setStep(i, val)`; read inside `fireStep` via `seqNoteLane_.currentValue()` |
| `restFlags[32]` | `params.seqNoteLaneRestFlags[i].load()` | Pushed into `arpCore_.seqRestFlags_[i]` (atomic array); read inside `fireStep` via `seqRestFlags_[currentStep].load(std::memory_order_relaxed)` |
| `currentStep` | Owned by `arpCore_.seqNoteLane_` (the `ArpLane<uint8_t>` itself) | Mutated by `advanceLaneBySpeed(seqNoteLane_, 9)` inside `fireStep` (Seq mode only) |

**No allocation.** Storage = existing atomics in `ArpeggiatorParams` + the new `seqNoteLane_` (`ArpLane<uint8_t>`) and `seqRestFlags_[32]` (atomic array) inside `ArpeggiatorCore`. The Layer 2 `SequencerNoteSource` wrapper from the original plan has been dropped.

---

## Entity 4: Transposition Root State (Runtime Concept)

**Grilling-Pass Revision (2026-05-23):** No separate `heldKeys_` buffer. Held notes route to `arpCore_.heldNotes_` (the existing `HeldNoteBuffer`) in both modes. Mode divergence happens inside `fireStep`, not at the input stage.

### Fields

| Field | Type | Location | Notes |
|-------|------|----------|-------|
| _(reuses)_ `arpCore_.heldNotes_` | `Krate::DSP::HeldNoteBuffer` (Layer 1 type, existing) | inside `ArpeggiatorCore` | Already tracks held notes in insertion order. `byInsertOrder().back()` is the most-recently-pressed still-held note. Used by Live mode's ArpMode traversal AND by Sequencer mode's transposition formula. |

**Operations** (unchanged from current Gradus):
- Incoming MIDI note-on → `arpCore_.noteOn(pitch, velocity)` in both modes.
- Incoming MIDI note-off → `arpCore_.noteOff(pitch)` in both modes.
- On `sourceMode` toggle: held notes are **NOT** cleared. They keep serving their role in both modes.
- In `fireStep` (Sequencer mode only), to derive transposition root + base velocity:
  ```cpp
  auto keys = heldNotes_.byInsertOrder();
  uint8_t rootNote = 60;   // default = no transposition (FR-016)
  uint8_t baseVel = 100;   // default per FR-025a
  if (!keys.empty()) {
      const auto& mostRecent = keys.back();
      rootNote = mostRecent.note;
      baseVel = mostRecent.velocity;
  }
  int transposeSemis = rootNote - 60;
  ```

**Validation**:
- Capacity is `HeldNoteBuffer::kMaxNotes = 32` (already enforced by the primitive). Excess notes silently ignored — pre-existing behavior.

**Not persisted** — runtime state only. Lost on plugin reload (correct — user isn't holding keys during reload).

---

## Entity 5: Piano-Roll View (UI Entity)

VSTGUI custom view rendering the Sequencer Note lane's pattern.

### Class Layout (`plugins/gradus/src/ui/piano_roll_view.h`)

```cpp
class PianoRollView : public VSTGUI::CView,
                       public Steinberg::FObject  // for IDependent
{
public:
    PianoRollView(const VSTGUI::CRect& size,
                  Steinberg::Vst::EditController* controller);
    ~PianoRollView() override;

    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where,
                                        const VSTGUI::CButtonState& buttons) override;

    // IDependent
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown,
                           Steinberg::int32 message) override;

    // Binding (called after construction, before first draw)
    void bindParameters();

private:
    Steinberg::Vst::EditController* controller_ = nullptr;

    // Per-step parameter cache (read from controller on update())
    struct StepData {
        uint8_t pitch;       // 0-127
        bool isRest;
    };
    std::array<StepData, 32> steps_{};

    int activeLength_ = 16;        // mirrors lane Length param
    int playheadStep_ = -1;        // -1 = none

    // Drag state
    bool dragging_ = false;
    int dragPitch_ = 60;           // pitch row captured at drag start
    int lastPaintedStep_ = -1;
    int dragStartStep_ = -1;       // for distinguishing click-vs-drag

    // Geometry constants
    static constexpr int kMidiLow = 36;   // C2 (FR-028)
    static constexpr int kMidiHigh = 83;  // B5
    static constexpr int kPitchRows = kMidiHigh - kMidiLow + 1;  // 48

    // Geometry helpers
    [[nodiscard]] int stepFromX(VSTGUI::CCoord x) const;     // 0..31 or -1
    [[nodiscard]] int pitchFromY(VSTGUI::CCoord y) const;    // 36..83 or -1
    [[nodiscard]] VSTGUI::CRect cellRect(int step, int pitch) const;
    [[nodiscard]] float colWidth() const;
    [[nodiscard]] float rowHeight() const;

    // Param binding helpers
    void registerDependent(Steinberg::Vst::ParamID id);
    void unregisterDependent(Steinberg::Vst::ParamID id);
    void refreshStepCache();
    void editStep(int stepIdx, int pitch, bool isRest);

    // Rendering helpers
    void drawGrid(VSTGUI::CDrawContext* ctx);
    void drawNotes(VSTGUI::CDrawContext* ctx);
    void drawPlayhead(VSTGUI::CDrawContext* ctx);
};
```

### Mouse Event State Machine

```
State: IDLE
  on mouseDown(buttons=kRButton):
    step = stepFromX(x); if (step in [0, activeLength_-1]):
      editStep(step, current_pitch_for_step, isRest=true)
    return kMouseEventHandled
  on mouseDown(buttons=kLButton):
    state = DRAGGING
    dragStartStep = stepFromX(x)
    dragPitch = pitchFromY(y)   // captures FR-031's "lock-to-start-pitch"
    lastPaintedStep = -1
    return kMouseEventHandled

State: DRAGGING
  on mouseMoved:
    step = clamp(stepFromX(x), 0, activeLength_-1)
    if (step != lastPaintedStep && step != dragStartStep):
      // The drag has actually moved past the start — we're truly dragging
      // (not a single click). Paint:
      editStep(step, dragPitch, isRest=false)
      lastPaintedStep = step
    return kMouseEventHandled
  on mouseUp:
    if (lastPaintedStep == -1):
      // Never moved past start step — treat as single click
      handleSingleClick(dragStartStep, dragPitch)
    state = IDLE
    return kMouseEventHandled

handleSingleClick(step, clickedPitch):
  current = steps_[step]
  if (current.isRest):
    // Rest → play at clicked pitch (FR-030 "click on resting step")
    editStep(step, clickedPitch, isRest=false)
  else if (current.pitch == clickedPitch):
    // Same pitch → toggle to rest (FR-030 "same-pitch click")
    editStep(step, current.pitch, isRest=true)
  else:
    // Different pitch → silent replace (FR-030 "different-pitch click")
    editStep(step, clickedPitch, isRest=false)
```

### Validation Rules

- `stepFromX` returns `-1` when `x` is outside the active range or to the left
  of `originX_`.
- `pitchFromY` returns `-1` when `y` is outside [originY_, originY_ + viewHeight].
- All `editStep` calls validate `step ∈ [0, activeLength_-1]` and `pitch ∈ [0, 127]`.
- Right-click on a step with `isRest=1` is a no-op (already a rest; idempotent).

### State Transitions

- **Mount (`attached`)**: register `IDependent` on the 64 step params (pitches +
  rest flags) + length param + playhead param. Refresh cache. Trigger initial
  draw.
- **Unmount (`removed` / dtor)**: unregister all `IDependent`s. Free resources.
- **Param change** (`IDependent::update`): refresh affected cell in cache, call
  `invalid()` to redraw.
- **Mode change to Live**: view is hidden by `UIViewSwitchContainer`, so
  `removed()` fires → automatically unregisters dependents until shown again.

---

## Entity 6: State Stream v3 Format

See `contracts/state-stream-v3.md` for the exact binary layout. Summary:

- **Header**: `int32 version = 3`
- **v2 block** (unchanged from current Gradus): all existing fields written by
  `saveArpParams()` — 1080+ fields covering 9 lanes + modulators + scale + Markov +
  speed curves + MIDI delay.
- **v3 appendix** (new):
  - `int32 sourceMode`
  - `int32 seqNoteLaneLength`
  - `int32[32] seqNoteLanePitches` (or `float[32]` normalized? — see contract)
  - `int32[32] seqNoteLaneRestFlags`
  - `float seqNoteLaneSpeed`
  - `float seqNoteLaneSwing`
  - `int32 seqNoteLaneJitter`
  - `float seqNoteLaneSpeedCurveDepth`

**Total v3 appendix size**: ≈ 280 bytes (256 + ~24).

### Backward Compatibility Path

| Stream Version | Loaded By | Result |
|----------------|-----------|--------|
| v2 (legacy) | v3 `setState` | `loadArpParams` consumes full v2 block, returns true; subsequent `loadSequencerNoteLaneParams` reads EOF on first field → returns true; all Sequencer params remain at struct defaults. |
| v3 (new) | v3 `setState` | `loadArpParams` consumes v2 block; `loadSequencerNoteLaneParams` reads the appendix. |
| v3 (new) | v2 `setState` (old Gradus binary loading newer preset) | Not a supported direction; users on v3-era Gradus do not roll back. **Defensive guard**: `if (version > kCurrentStateVersion) return kResultFalse` to fail cleanly. |

---

## Entity 7: Default-State Vectors

For `freshly instantiated Gradus` and `v2 preset loaded into v3 binary`:

| Field | Value | Source |
|-------|-------|--------|
| `sourceMode` | `0` (Live) | FR-001 |
| `seqNoteLaneLength` | `16` | FR-005 |
| `seqNoteLanePitches[*]` | `60` (C4) | FR-006 |
| `seqNoteLaneRestFlags[*]` | `1` (rest) | FR-007 |
| `seqNoteLaneSpeed` | `1.0` | "neutral" default per FR-039a |
| `seqNoteLaneSwing` | `0.0` | neutral |
| `seqNoteLaneJitter` | `0` | neutral |
| `seqNoteLaneSpeedCurveDepth` | `0.5` | mirrors existing per-lane default (e.g., `velocityLaneSpeedCurveDepth{0.5f}`) |

This default vector ensures:
- A fresh Gradus (default `sourceMode = 0`) behaves identically to the current
  pre-feature Gradus (SC-004).
- A user toggling to Sequencer mode for the first time sees an empty piano roll
  (all 32 rests) — silent until they click.
- All clicked rows place a played note at the clicked pitch (rest flag clears),
  because all pitches default to 60 (= C4 = clicking on the C4 row matches the
  pitch and would toggle to rest, but since restFlag=1, FR-030 "click on resting
  step" applies: clear rest, set pitch = clicked pitch).

---

## Cross-Entity Invariants

1. **Monophonic invariant** (FR-030): Each step has exactly one
   `(pitch, restFlag)` pair. There is no concept of multiple notes per step in v1.
2. **Active-length invariant** (FR-029): The piano roll only shows / edits the
   first `seqNoteLaneLength` columns. Steps beyond `length` exist in the param
   array but are inactive (not edited, not rendered, not played).
3. **No-arp-mode-in-Sequencer invariant** (FR-022) — grilling-pass revised: When `sourceMode == Sequencer`, `fireStep` takes an early-branch path that reads the source pitch from `seqNoteLane_.currentValue()` + `seqRestFlags_[currentStep]` instead of running ArpMode/Octave traversal on `heldNotes_`. The held-note pool is still populated (it serves transposition root, base velocity, and chord-lane voicing), but ArpMode/Octave/Markov/Euclidean/Pin/Range/ScaleQuantizeInput controls are inert (grayed in UI, ignored in audio thread).
4. **Pitch lane additivity** (FR-021): The pitch lane offset is added on top of the transposed programmed pitch BEFORE the output scale-quantize stage. Implementation: inside `fireStep`, the pre-emission pitch is `programmedPitch + (heldNote - 60) + kArpTranspose`; the pitch lane's `currentValue()` is then added downstream in the existing emission path (unchanged from Live mode). This produces the correct stack per the FR-021 formula `finalPitch = programmedPitch + (heldNote - 60) + kArpTranspose + pitchLaneOffset`, then output scale quantize.
5. **Lane 10 inertness in Live mode** (new — grilling-pass): When `sourceMode == Live`, lane 10's advance and modulator reads are short-circuited in `fireStep`. Lane 10 occupies memory but consumes zero cycles. SC-004 (Gradus Live byte-identical) and SC-004b (Ruinae preset corpus byte-identical) verify this invariant.

---

## Summary

- **1** new top-level param (`kArpSourceModeId`).
- **70** new Sequencer Note lane params (`kArpSequencerNoteLane*Id`).
- **71** total new parameter IDs at 3741-3811.
- **MODIFIED** KrateDSP (`dsp/include/krate/dsp/processors/arpeggiator_core.h`): `kNumLanes` 9→10; new `seqNoteLane_` (`ArpLane<uint8_t>`) + `seqRestFlags_[32]` atomic array + `sourceMode_` + `setSourceMode()` API; `fireStep` gets conditional-inert and source-pitch branches.
- **Ruinae regression-verified** via SC-004b. Ruinae's behavior unchanged (default `sourceMode_ = Live` → lane 10 always inert).
- **0** new standalone Layer 2 DSP components. (`SequencerNoteSource` from the prior plan was dropped — lane lives natively inside `ArpeggiatorCore`.)
- **1** new VSTGUI custom view: `PianoRollView`.
- **0** new audio-thread members in `Processor`. (No `heldKeys_` parallel buffer — held notes flow through `arpCore_.heldNotes_` in both modes.)
- **1** state stream version bump: 2 → 3, with EOF-safe legacy v2 load + checked-in binary fixtures for migration test.
