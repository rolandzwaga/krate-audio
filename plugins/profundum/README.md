# Profundum

**A mass bass instrument for VST3 / AU**

Profundum is a Krate Audio bass instrument built around mass rather than layers: one physically coherent bass
voice that moves from pure sub through resonant and harmonic bass to a distorted speaker-grabber while never
losing its low-frequency centre, steered by four Personality macros (Weight, Dark/Bright, Tight/Loose,
Clean/Violent). It is a thin plugin wrapper around the `ProfundumEngine` / `ProfundumMacroMatrix` DSP in the
[KrateDSP library](../../dsp/); no DSP lives in this plugin. Unreleased at `0.1.0` (the plugin scaffold); see
[CHANGELOG.md](CHANGELOG.md) and `specs/Profundum-roadmap.md`.

## Parameters

84 parameters, numbered in 100-ID bands:

| IDs | Section |
|---|---|
| 0-7 | Global (output gain, voice mode, polyphony, output ceiling) and four hidden performance parameters (Pitch Bend, Sustain Pedal, Mod Wheel, Channel Pressure) |
| 100-105 | Personality macros (Weight, Dark/Bright, Tight/Loose, Clean/Violent), Mass, Morph Position |
| 200-208 | Core |
| 300-306 | Anchor |
| 400-499 | Drop / note behaviour / amp envelope: Drop 400-403, note behaviour 410-417, amp envelope 420-426 |
| 500-501 | Fold / Shape |
| 600-602 | Character |
| 700-701 | Mass dynamics |
| 800-809 | Stereo field |
| 900-914 | Motion |
| 1000-1002 | Morph control (Anchor Morph Mode, Edit Target, Morph Ramp Time) |

**Two endpoints, one Edit Target.** The 60 sound parameters (Core, Anchor, Drop, amp envelope, Fold / Shape, Character,
Mass dynamics, Stereo field, Motion and Mass) each hold two values, endpoint A and endpoint B; Morph Position moves the
sound between them. Edit Target chooses which endpoint the sound parameters show and edit. Switching it never changes
the sound.

**Automation is target-relative.** Host automation of a sound parameter writes the endpoint selected by Edit Target when
it plays back. A lane recorded while editing A writes B if Edit Target is B at playback. Edit Target itself is not
automatable; it is saved with the project, so a reloaded project edits the endpoint it was saved on.

**Copy and Compare are controller operations.** Copy A to B / B to A and Compare (A, A-B, B) exist in the controller
with no buttons until the Phase 12 interface. Compare only changes what you hear: it is never saved and returns to A-B
when a state loads or the plugin is deactivated.

**MIDI.** Mod wheel (CC 1) and channel pressure add to Mass at every Morph Position without changing the saved Mass;
pitch bend follows Pitch Bend Range; the sustain pedal (CC 64) holds released notes. These four arrive through the
host's MIDI-controller mapping and are never saved.

**Linux display sync.** The processor sends endpoint changes to the controller every 50 ms on Windows and macOS. Linux
has no SDK timer: there, a value the host changed without the controller seeing it reaches the controller's display at
the next Edit Target switch (which asks the processor for both endpoints), state save / load or (de)activation. Only the
displayed values can lag; the sound and the saved state are always current. The Phase 12 interface closes this gap.

## Build

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release
"$CMAKE" --build build/windows-x64-release --config Release --target Profundum profundum_tests
```

## Test

```bash
build/windows-x64-release/bin/Release/profundum_tests.exe 2>&1 | tail -5
node tools/run-cpu-tests.js profundum_tests    # timed CPU case; run it alone
```

## Validate

```bash
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Profundum.vst3"
```
