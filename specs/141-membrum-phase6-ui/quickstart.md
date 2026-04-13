# Phase 6 Quickstart -- Membrum v0.6.0

**Spec**: `specs/141-membrum-phase6-ui/spec.md`
**Date**: 2026-04-12

This quickstart walks through building, running, and exercising every P1 user story for Phase 6.

---

## Build

All commands are Windows-centric; adapt cmake path for macOS/Linux.

```bash
# Set full cmake path (Windows)
CMAKE="C:/Program Files/CMake/bin/cmake.exe"

# Configure release
"$CMAKE" --preset windows-x64-release

# Build Membrum
"$CMAKE" --build build/windows-x64-release --config Release --target Membrum

# Build membrum tests
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests
```

Built plugin bundle: `build/windows-x64-release/VST3/Release/Membrum.vst3/`.

### Debug build with AddressSanitizer (for SC-014 lifecycle check)

```bash
"$CMAKE" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON
"$CMAKE" --build build-asan --config Debug --target membrum_tests
```

---

## Run Tests

```bash
# All Membrum tests
build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5

# Specific test group
build/windows-x64-release/bin/Release/membrum_tests.exe "[macro_mapper]"
build/windows-x64-release/bin/Release/membrum_tests.exe "[phase6_state]"
build/windows-x64-release/bin/Release/membrum_tests.exe "[editor_reachability]"
build/windows-x64-release/bin/Release/membrum_tests.exe "[editor_lifecycle]"

# Run via CTest
ctest --test-dir build/windows-x64-release -C Release --output-on-failure -R membrum

# ASan lifecycle test (SC-014)
build-asan/bin/Debug/membrum_tests.exe "[editor_lifecycle_asan]"
```

---

## Pluginval (SC-010)

```bash
tools/pluginval.exe --strictness-level 5 --validate \
    "build/windows-x64-release/VST3/Release/Membrum.vst3"
```

Expected: `ALL TESTS PASSED`.

---

## macOS AU Validation (SC-011)

```bash
# On macOS after building:
auval -v aumu Mb01 KrAu
```

Expected: `AU VALIDATION SUCCEEDED`.

---

## Clang-Tidy (SC-012)

```powershell
# Run from VS Developer PowerShell after generating compile_commands.json:
./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja 2>&1 | Tee-Object -FilePath clang-tidy-phase6.log
```

Expected: zero new warnings on Phase 6 files.

---

## User Story Walkthroughs

Open Membrum in a VST3 host (Reaper, Bitwig, Ableton Live 11+). Load a MIDI track, route it to the plugin, arm for input.

### US1 -- First-Open Acoustic Mode with Macros (P1)

1. Instantiate a fresh Membrum. Verify window size = 1280x800.
2. Verify the editor shows three regions: 4x8 Pad Grid (left), Selected-Pad Panel (centre), Kit Column (right).
3. Verify the Acoustic/Extended toggle is set to **Acoustic** (default).
4. Verify the Selected-Pad Panel shows: 5 Macro knobs (Tightness, Brightness, Body Size, Punch, Complexity), 5 body knobs (Material, Size, Decay, Strike Position, Level), Pitch Envelope display, Output selector, Choke Group selector, Exciter / Body dropdowns.
5. Verify the Unnatural Zone controls (Mode Stretch, Mode Inject, Decay Skew, Nonlinear Coupling) are NOT visible.
6. Click pad 1 (kick). Automate the Tightness macro from 0.0 to 1.0 over 2 seconds.
7. **Verify**: Tension (material), Damping (decay), and Decay Skew values follow documented macro curves; kick audibly tightens.

### US2 -- Extended Mode Reveals Full Parameter Access (P1)

1. Click the Acoustic/Extended toggle -> `Extended`.
2. Verify the Selected-Pad Panel expands to show: Unnatural Zone (4 controls), raw physics (Tension, Damping, Air Coupling, Nonlinear Pitch if exposed), full Tone Shaper (Filter Type, Cutoff, Resonance, EnvAmount, Drive, Fold, Filter ADSR), complete exciter params, Material Morph XY pad, per-pad Coupling Amount.
3. Verify the 32x32 Coupling Matrix editor is accessible from the Kit Column.
4. Edit Mode Stretch on pad 3 from 1.0 to 1.5. Toggle to Acoustic, then back to Extended.
5. **Verify**: Mode Stretch on pad 3 is still 1.5 (hiding is visual only; no reset).
6. Send host automation on Mode Inject Amount while in Acoustic mode.
7. **Verify**: audio output reflects the automation (hidden parameters still respond).

### US3 -- Pad Grid Trigger, Select, and Visual Feedback (P1)

1. Verify the 4x8 grid displays pads labelled with MIDI notes 36 (bottom-left) through 67 (top-right) and GM names.
2. Send external MIDI note 36 (C1). **Verify**: pad 1 glows, intensity proportional to voice envelope, decays with voice release.
3. Click pad 5. **Verify**: Selected-Pad Panel updates within one frame (~16.7 ms).
4. Shift-click pad 7. **Verify**: pad 7 auditions at velocity 100; pad 5 remains selected.
5. Assign pad 4 to choke group 2 and aux bus 3. **Verify**: pad 4 displays "CG2" and "BUS3" compact indicators.

### US4 -- Kit-Level Controls and Preset Browser (P1)

1. Click the kit Browse button. **Verify**: `PresetBrowserView` modal opens with factory/user kits, search field, category tabs.
2. Load a factory kit. **Verify**: all 32 pads update to the preset's values.
3. Modify pad 7's Decay. Click "Save As". **Verify**: `SavePresetDialogView` appears (no native popup), accepts a name, writes to `C:\ProgramData\Krate Audio\Membrum\Kits\User\`.
4. Select pad 5, open the per-pad preset browser. Load a pad preset.
5. **Verify**: pad 5's sound parameters change, but pad 5's `outputBus` and `couplingAmount` are preserved.
6. Load a malformed JSON preset file. **Verify**: error indicator appears; editor state unchanged; no crash.

### US5 -- Choke Groups and Voice Management UI (P2)

1. Select pad 9 (open hat), set Choke Group = 1. Select pad 10 (closed hat), set Choke Group = 1.
2. Trigger pad 9. During its decay, trigger pad 10. **Verify**: pad 9 is fast-released (grid glow drops rapidly).
3. Set Max Polyphony = 4. Trigger 5 simultaneous notes. **Verify**: active-voices readout shows 4, not 5.
4. Hover the Voice Stealing dropdown. **Verify**: tooltip explains Oldest/Quietest/Priority.

### US6 -- Tier 2 Coupling Matrix Editor (P2)

1. In Extended mode, open the Coupling Matrix editor.
2. **Verify**: 32x32 grid renders, colour intensity maps to `effectiveGain`.
3. Click cell (kick, snare), set to 0.04. **Verify**: `CouplingMatrix::overrideGain[kick][snare] == 0.04`, `hasOverride == true`.
4. Save kit preset, reload. **Verify**: override round-trips.
5. Click Reset on the cell. **Verify**: `hasOverride == false`; cell renders Tier 1 value.
6. During playback, strike kick. **Verify**: (kick, snare) cell outlined with activity highlight.
7. Engage Solo on (kick, snare). Trigger various pads. **Verify**: only (kick, snare) coupling audible in coupling output.
8. Close the matrix editor. **Verify**: Solo auto-disengages.

### US7 -- Output Routing UI (P2)

1. Select pad 3. In the Output selector, choose Aux 2. **Verify**: grid shows "BUS2" on pad 3; pad 3 audio routed to Main + Aux 2.
2. Choose Aux 5 (not host-activated). **Verify**: warning tooltip "Host must activate Aux 5 bus"; pad 3 falls back to Main only.

### US8 -- Pitch Envelope as Primary Voice Control (P2)

1. Select pad 1 (kick archetype). **Verify**: Pitch Envelope display is visible in the Selected-Pad Panel (Acoustic mode, not behind a tab).
2. Drag the End point from 50 Hz to 80 Hz. **Verify**: `kToneShaperPitchEnvEndId` updates in real time; kick's pitch-down span changes.
3. Set Punch macro to 1.0. **Verify**: `tsPitchEnvStart`, `tsPitchEnvTime` update per the mapping; kick gains audible "snap".

---

## Edge-Case Smoke Tests

| Edge case | Verification |
|-----------|--------------|
| Window resize | Click editor-size toggle -> Compact (1024x640). Verify pad grid + macros still visible, no clipping. Toggle back -> Default. |
| DPI scaling | Set OS scale to 1.5x, reopen plugin. Verify all text/knobs legible. Repeat at 2.0x. |
| Extreme macro automation | Automate Tightness macro at audio-block rate for 10 s. Verify no clicks in audio. |
| Simultaneous macro + underlying edit | While Tightness macro is automating, drag Tension knob. Verify most-recent-write semantics; no crash. |
| Preset load with editor open | Load a kit preset. Verify all 32 pads' panels update atomically in one UI frame. |
| External MIDI during selection change | Send MIDI while clicking pads. Verify MIDI triggers correct pad regardless of selection. |
| Host bypass | Bypass plugin. Verify editor still renders; pad glow stops; mode preserved. |
| Matrix editor during preset load | Leave matrix editor open, load kit. Verify matrix re-renders with loaded overrides. |
| Rapid editor open/close | Open/close editor 100 times (via host API or ASan test). Verify no ASan errors. |
| v5 state load | Load a v5 project. Verify 160 macros all set to 0.5; audio matches Phase 5 (<= -120 dBFS delta, SC-006). |
| Automation on Acoustic/Extended | Host-automate `kUiModeId`. Verify visibility switches on UI thread via `IDependent`. |

---

## Directory Layout After Phase 6

```
plugins/membrum/
├── src/
│   ├── ui/                    <-- NEW
│   ├── processor/macro_mapper.{h,cpp}  <-- NEW
│   ├── dsp/pad_glow_publisher.h        <-- NEW
│   ├── dsp/matrix_activity_publisher.h <-- NEW
│   └── ... (existing)
├── resources/
│   └── editor.uidesc          <-- NEW
└── tests/unit/ui/             <-- NEW
```

---

## Canonical Todo Checklist (per spec 135 roadmap)

- [ ] Write failing tests for MacroMapper, state v6, publishers, editor reachability
- [ ] Implement `PadConfig` v6 fields, `plugin_ids.h` Phase 6 IDs
- [ ] Implement `MacroMapper` in Processor
- [ ] Implement `PadGlowPublisher`, `MatrixActivityPublisher`, `MetersBlock` DataExchange
- [ ] Implement state v5 -> v6 migration
- [ ] Implement Controller `createView`, custom views, sub-controller, DataExchange receiver
- [ ] Author `editor.uidesc` with two top-level templates and UIViewSwitchContainer
- [ ] Implement `PadGridView`, `CouplingMatrixView`, `PitchEnvelopeDisplay`
- [ ] Wire kit + per-pad `PresetManager` + `PresetBrowserView` instances
- [ ] Wire kit preset JSON `"uiMode"` + `"macros"` fields
- [ ] Verify zero compiler warnings
- [ ] All tests pass (`membrum_tests`, `dsp_tests`)
- [ ] Pluginval strictness 5 passes
- [ ] `auval -v aumu Mb01 KrAu` passes (macOS)
- [ ] clang-tidy clean
- [ ] ASan lifecycle test passes
- [ ] Fill SC-001..SC-015 compliance table with concrete evidence
- [ ] Update `specs/_architecture_/` with new components
- [ ] Commit per phase as authorised by `tasks.md`
