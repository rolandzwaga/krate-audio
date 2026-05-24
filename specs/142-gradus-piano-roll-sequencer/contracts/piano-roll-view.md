# Contract: PianoRollView Public API

**Feature**: 142-gradus-piano-roll-sequencer
**File**: `plugins/gradus/src/ui/piano_roll_view.h`

This contract specifies the public API and behavioral guarantees of the
`PianoRollView` VSTGUI custom view.

**Grilling-pass note (2026-05-23):** This contract is unchanged by the audio-side pivot. The view talks to the controller via VST3 parameters (IDs 3741-3811, see `param-ids.md`). The underlying storage moved into `ArpeggiatorCore::seqNoteLane_` + `seqRestFlags_[32]`, but that is transparent to the view — `editParamWithNotify` still routes through the host's parameter queue and reaches the Processor → ArpeggiatorCore via the existing `processParameterChanges` path.

The view is hosted inside a `UIViewSwitchContainer` slot (template-switch-control=`ArpSourceMode`) per Decision 4 in `research.md`: visible only when Source=Sequencer; Live mode shows an empty/placeholder template. Editor window size is unchanged; lane editors remain visible in both modes (per Q7-A).

## Class Signature

```cpp
namespace Gradus {

class PianoRollView : public VSTGUI::CView,
                      public Steinberg::FObject  // for IDependent
{
public:
    /// Construct a PianoRollView bound to a controller. The controller must
    /// outlive the view (standard VSTGUI lifecycle: editor owns view, controller
    /// owns editor lifecycle in willClose).
    /// @param size View bounds in parent-frame coordinates.
    /// @param controller Pointer to the Gradus EditController (used for
    ///                   editParamWithNotify + getParameter).
    PianoRollView(const VSTGUI::CRect& size,
                  Steinberg::Vst::EditController* controller);

    ~PianoRollView() override;

    // --- CView overrides ---
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where,
                                        const VSTGUI::CButtonState& buttons) override;
    bool attached(VSTGUI::CView* parent) override;
    bool removed(VSTGUI::CView* parent) override;

    // --- IDependent override (UI thread, deferred) ---
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown,
                           Steinberg::int32 message) override;

    // --- Configuration ---
    /// Set the accent color used for active notes. Default = Gradus's standard
    /// accent gold (0xD4, 0xA8, 0x56).
    void setAccentColor(VSTGUI::CColor color);

    CLASS_METHODS(PianoRollView, VSTGUI::CView)

private:
    // (private implementation per data-model.md Entity 5)
};

} // namespace Gradus
```

## Behavioral Contract

### Rendering (FR-026, FR-028, FR-034, FR-034a)

1. **Fixed pitch range** (FR-028): View ALWAYS displays exactly the 48 rows for
   MIDI 36 (C2, bottom) through MIDI 83 (B5, top). No scrolling.
2. **Active columns** (FR-029): Only the first `seqNoteLaneLength` columns are
   rendered as "active" (interactive, full color). Steps beyond Length are
   rendered as a desaturated/dimmed grid area (visual cue) and do NOT respond
   to clicks.
3. **Note rendering**: For each active step `i ∈ [0, length-1]`:
   - If `restFlags[i] == 1`: render an empty cell (rest indicator, e.g., a
     small dash or dot at the center).
   - Else: render a filled rectangle at row `pitches[i]` of the step column.
4. **Playhead cursor** (FR-034a): Render a translucent column overlay at the
   step column indicated by the `kArpSequencerNoteLanePlayheadId` parameter
   value (`playheadStep = round(playheadParam * 32) clamped to [0, length-1]`).
   The overlay must not obscure the underlying note rendering — use alpha
   blending (e.g., 0x30 alpha).
5. **External param changes** (FR-034): When ANY of the 64 step params, the
   length param, or the playhead param changes (via host automation, preset
   load, MIDI learn, etc.), the view MUST re-render to reflect the new value.

### Mouse Handling (FR-030, FR-031, FR-032)

#### Single Left-Click (no drag detected)

Define "no drag" as `mouseUp` fired before any `mouseMoved` past a different step column.

| Current cell state | Clicked row pitch == current pitch? | Action (single click) |
|--------------------|--------------------------------------|------------------------|
| `restFlag=1` (rest) | (any)                                | Set `pitch = clickedRowPitch`, `restFlag = 0` (place note) |
| `restFlag=0`, same pitch | Yes                                 | Set `restFlag = 1` (toggle to rest, FR-030) |
| `restFlag=0`, different pitch | No                            | Set `pitch = clickedRowPitch`, keep `restFlag = 0` (silent replace, FR-030) |

#### Left-Drag (mouse-down + mouse-moved past start step + mouse-up)

- Lock the paint pitch to `pitchFromY(mouseDownPos.y)` at the moment of
  `mouseDown` (FR-031 "lock-to-start-pitch").
- For each step column the mouse enters during the drag:
  - Set `pitch[step] = lockedDragPitch`
  - Set `restFlag[step] = 0`
  - (Ignore current state; always paint, per FR-031 clarification.)
- Vertical mouse motion is IGNORED during the drag (paint pitch does not follow
  cursor).
- Steps outside `[0, length-1]` are clamped (no out-of-bounds writes).

#### Right-Click (or platform equivalent)

- On any mouse-down with `buttons & kRButton`:
  - Set `restFlag[clickedStep] = 1` (FR-032).
  - Idempotent (right-click on existing rest = no-op).
- Does NOT enter drag mode.
- Right-click out of bounds = no-op.

### Parameter Binding

The view binds to these parameters of the bound `EditController`:

| Param | ID range | Bound for |
|-------|----------|-----------|
| Sequencer Note pitches | 3743..3774 | Read (refresh on update), write (via editParamWithNotify) |
| Sequencer Note rest flags | 3775..3806 | Read, write |
| Lane Length | 3742 | Read (determines active column count) |
| Playhead | 3811 | Read (drives playhead cursor) |

**Total IDependent registrations**: 64 + 1 + 1 = 66.

Registration MUST happen in `attached()`. Unregistration MUST happen in
`removed()` AND in the destructor (defense in depth — if `removed` is not called,
dtor still cleans up).

### Lifecycle

| Event | Action |
|-------|--------|
| Constructor | Initialize cache to all-defaults (no controller access yet). |
| `attached(parent)` | Acquire controller params, register IDependent on each, refresh cache, request initial draw. Return `true`. |
| `removed(parent)` | Unregister IDependent on each param. Return `true`. |
| Destructor | (Defensive) Unregister any remaining IDependent. |
| `update(param, kChanged)` | If param is a bound step pitch: refresh `steps_[i].pitch`. If a rest flag: refresh `steps_[i].isRest`. If length: refresh `activeLength_`. If playhead: refresh `playheadStep_`. Then call `invalid()` to redraw. |
| `setMouseEnabled(false)` (defense; when Source = Live AND view somehow still attached) | View accepts the call; subsequent mouseDown events return `kMouseEventNotHandled` (let parent handle). |

### Thread Safety

- ALL public methods are called from the UI thread (VSTGUI guarantee).
- `update()` is called from the UI thread via the deferred-update mechanism
  (VST3 SDK's `UpdateHandler` posts the call to the UI thread).
- Internal data (`steps_`, `dragging_`, etc.) is touched only on the UI thread.
- No audio-thread access.
- Param edits go through `Controller::editParamWithNotify`, which is itself
  thread-safe (it routes through the host's parameter changes queue).

### Cross-Platform Compatibility (FR-033)

- ZERO platform-specific code. All drawing uses `CDrawContext` (VSTGUI's
  cross-platform abstraction).
- Mouse buttons (`kLButton`, `kRButton`) use VSTGUI's normalized enum.
- Font rendering uses VSTGUI's `CFontDesc` (no native font handles).
- Colors use `CColor` (no platform color types).

### Edge Cases

| Edge case | Behavior |
|-----------|----------|
| Length changes mid-drag | Drag continues at new length boundary; steps beyond new length not painted. |
| Click out of pitch range (above/below visible grid) | Clamp to nearest valid row, OR `kMouseEventNotHandled` if clearly outside view bounds. Implementation: `pitchFromY` returns -1, click is ignored. |
| Click out of step range (right of active length) | Click is ignored (clipped by `stepFromX` returning `>= length`). |
| Programmatic param change during drag | `update()` refreshes cache and redraws, but does NOT affect the in-flight drag state. The drag continues based on its own `dragPitch_`. |
| `attached()` called twice without intervening `removed()` | Second call is idempotent (uses internal flag to avoid double-registration). |
| `removed()` called before `attached()` | No-op (no dependents to unregister). |
| Controller pointer becomes invalid | Defensive: store as raw pointer; controller's `willClose` MUST null out any controller-side cache before tearing down the editor. View's destructor uses controller only if non-null. |

## Test Coverage

### `piano_roll_view_test.cpp` (NEW, in `tests/unit/ui/`)

Test scenarios (using a mock VST3 controller from `tests/test_helpers/`):

1. **rendersGridWith48Rows** (FR-028): Construct view, mock 32 steps, verify
   `pitchFromY(viewSize.top + 0.5*rowHeight) == 83` and
   `pitchFromY(viewSize.bottom - 0.5*rowHeight) == 36`.

2. **clickOnRestingStepPlacesNote** (FR-030 "click on resting step"): Set
   step 5 to rest, click at row 67 in column 5. Verify `pitches[5] = 67`,
   `restFlags[5] = 0`.

3. **clickOnSamePitchTogglesRest** (FR-030 "same-pitch click"): Set step 5 to
   pitch 67 non-rest. Click at row 67 column 5. Verify `restFlags[5] = 1`,
   `pitches[5] = 67` (unchanged).

4. **clickOnDifferentPitchReplaces** (FR-030 "different-pitch click"): Set step
   5 to pitch 67 non-rest. Click at row 70 column 5. Verify `pitches[5] = 70`,
   `restFlags[5] = 0`.

5. **rightClickSetsRest** (FR-032): Click with kRButton at column 10, any row.
   Verify `restFlags[10] = 1`.

6. **dragLocksPitchToStart** (FR-031): MouseDown at row 67 col 2, MouseMoved
   through cols 3, 4, 5 at row 70 (different row). Verify `pitches[2..5] = 67`
   (locked), `restFlags[2..5] = 0`.

7. **dragNeverTogglesRest** (FR-031): Same as above but col 3 was already
   `pitch=67, rest=0`. Verify col 3 still `pitch=67, rest=0` (no toggle during
   drag).

8. **playheadDrivenByParam** (FR-034a): Set playhead param to 0.5 with length=16.
   Verify `playheadStep_ == 8` (16 * 0.5).

9. **lengthChangeShrinksActiveArea** (FR-029): Set length=8. Click at col 10
   (out of active range). Verify no param write.

10. **externalParamChangeRedraws** (FR-034): Trigger `update()` for a step
    pitch change. Verify cache reflects new value and view marked dirty.

11. **dragOutOfBoundsClamps** (edge case): MouseDown at col 28 row 80, MouseMoved
    to (-50, 50). Verify drag clamped to col 0, paint pitch still 80.

12. **rightClickDuringDrag**: Mid-drag, right-click in a column. Verify rest is
    set in that column but drag's leftClick paint also fires (or whichever the
    impl handles — but no crash and no inconsistent state).

13. **idempotent attached/removed**: Call `attached()` twice, `removed()` twice.
    Verify dependent count stays correct.
