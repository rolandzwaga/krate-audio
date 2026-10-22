# Changelog

All notable changes to Profundum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - Unreleased

First cut of the plugin. Profundum has existed since Phase 1 as a set of KrateDSP
components with unit tests and no way to play them; this release wraps the
finished `ProfundumEngine` in a VST3 you can load, play a bass line on, and hear.
The full sound design surface is exposed as host parameters; the interface and the
factory library are built on it.

### Added

- **Profundum is a plugin** — A registered VST3 instrument (`Profundum.vst3`, AU `aumu`/`Prfd`/`KrAt`, bundle `com.krateaudio.profundum`). Every note runs the whole mass-bass engine, and the engine's true-peak limiter is the last stage inside it.
- **One event input, one stereo output** — No audio input and no sidechain bus. The AU declares exactly one channel configuration, 0 in / 2 out.
- **Four global controls** — Output gain (attenuation only, -60 dB to 0 dB with silence at the bottom, smoothed, applied after the limiter so it can never push past the ceiling), voice mode (Mono or Poly), polyphony (1-8, four by default) and output ceiling (-12 to 0 dBTP, -1 by default). All four are saved with the project.
- **Four live Personality macros: Weight, Dark/Bright, Tight/Loose, Clean/Violent** — Each one moves the sound from the first release; the centre position (0.5) is neutral and changes nothing. All four are saved with the project.
- **Host tempo** — When the host reports a tempo, the engine's tempo-synced motion follows it; otherwise it runs at 120 BPM.
- **64 samples of reported latency** — Constant at every sample rate: the output limiter's look-ahead. The plugin reports its delay so the host can compensate.
- **Presets scan the `Sub` category** — The preset browser is live and reads `Krate Audio/Profundum/Sub`. The category ships empty. The name is permanent: later releases add categories beside it and never rename it.
- **Test suite `profundum_tests`** — Bus layout, parameter denormalisation, state round trip (hostile streams included), MIDI event ordering and overflow, lifecycle, editor lifecycle, wrapper transparency, parameter audibility, zero allocation on the audio thread, and a timed CPU case run on its own.
- **The full parameter surface: 84 parameters** — Every engine lever is a host parameter, in 100-ID bands: Mass and Morph Position beside the macros (104, 105), Core 200-208, Anchor 300-306, Drop / note behaviour / amp envelope 400-499 (Drop 400-403, note behaviour 410-417, amp envelope 420-426), Fold / Shape 500-501, Character 600-602, Mass dynamics 700-701, Stereo field 800-809, Motion 900-914 and Morph control 1000-1002 (Anchor Morph Mode, Edit Target, Morph Ramp Time).
- **Two endpoints, A and B, with an Edit Target** — The 60 sound parameters each hold an A and a B value and Morph Position moves between them. Edit Target chooses which endpoint you see and edit; switching it never changes the sound. Host automation of a sound parameter writes the endpoint selected by Edit Target when it plays back.
- **Copy and Compare** — Copy A to B / B to A, and Compare (A, A-B, B) to audition either endpoint without moving Morph Position. Both are controller operations without buttons until the interface arrives; Compare is never saved.
- **MIDI controllers** — Mod wheel and channel pressure push Mass up at every Morph Position without changing the saved sound; pitch bend with an adjustable range (Pitch Bend Range); a sustain pedal that holds released notes until it lifts.
- **Larger saved state** — Projects now save both endpoints, the note behaviour, the morph controls, the random seed and the morph's in-flight position (510 bytes, format version 2). Projects saved by the first scaffold build still load: their globals and macros are kept and every new parameter takes its default.
- **Release-aware tail** — The reported tail follows the longest amp release of the two endpoints, as lengthened or shortened by Tight/Loose, so a host never cuts a release short.
- **Registered across the repository** — CI build and test lanes, release packaging and installers, nightly valgrind, clang-tidy, the CPU, shard and close-lane runners, the changelog and specs-index tools, and the root documentation all know about Profundum.

### Known limitations

- The editor is a placeholder: one panel of stock controls, enough to prove the parameter wiring.
- Host sample rates outside 22050 Hz - 192 kHz are refused; the plugin stays silent at such a rate.
- Velocity is not read.
- On Linux, where the SDK has no timer, a value the host changes without the controller seeing it shows on the parameter display only at the next Edit Target switch, state save / load or (de)activation. The sound and the saved state are always current.
- No factory presets ship.
