# Changelog

All notable changes to Profundum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - Unreleased

First cut of the plugin. Profundum has existed since Phase 1 as a set of KrateDSP
components with unit tests and no way to play them; this release wraps the
finished `ProfundumEngine` in a VST3 you can load, play a bass line on, and hear.
The full sound design surface is exposed as host parameters and has its own editor;
the factory library is built on it.

### Added

- **Profundum is a plugin** — A registered VST3 instrument (`Profundum.vst3`, AU `aumu`/`Prfd`/`KrAt`, bundle `com.krateaudio.profundum`). Every note runs the whole mass-bass engine, and the engine's true-peak limiter is the last stage inside it.
- **One event input, one stereo output** — No audio input and no sidechain bus. The AU declares exactly one channel configuration, 0 in / 2 out.
- **Four global controls** — Output gain (attenuation only, -60 dB to 0 dB with silence at the bottom, smoothed, applied after the limiter so it can never push past the ceiling), voice mode (Mono or Poly), polyphony (1-8, four by default) and output ceiling (-12 to 0 dBTP, -1 by default). All four are saved with the project.
- **Four live Personality macros: Weight, Dark/Bright, Tight/Loose, Clean/Violent** — Each one moves the sound from the first release; the centre position (0.5) is neutral and changes nothing. All four are saved with the project.
- **Host tempo** — When the host reports a tempo, the engine's tempo-synced motion follows it; otherwise it runs at 120 BPM.
- **64 samples of reported latency** — Constant at every sample rate: the output limiter's look-ahead. The plugin reports its delay so the host can compensate.
- **Presets scan the `Sub` category** — The preset browser is live and reads `Krate Audio/Profundum/Sub`. The name is permanent: later releases add categories beside it and never rename it.
- **Test suite `profundum_tests`** — Bus layout, parameter denormalisation, state round trip (hostile streams included), MIDI event ordering and overflow, lifecycle, editor lifecycle, wrapper transparency, parameter audibility, zero allocation on the audio thread, and a timed CPU case run on its own.
- **The full parameter surface: 84 parameters** — Every engine lever is a host parameter, in 100-ID bands: Mass and Morph Position beside the macros (104, 105), Core 200-208, Anchor 300-306, Drop / note behaviour / amp envelope 400-499 (Drop 400-403, note behaviour 410-417, amp envelope 420-426), Fold / Shape 500-501, Character 600-602, Mass dynamics 700-701, Stereo field 800-809, Motion 900-914 and Morph control 1000-1002 (Anchor Morph Mode, Edit Target, Morph Ramp Time).
- **Two endpoints, A and B, with an Edit Target** — The 60 sound parameters each hold an A and a B value and Morph Position moves between them. Edit Target chooses which endpoint you see and edit; switching it never changes the sound. Host automation of a sound parameter writes the endpoint selected by Edit Target when it plays back.
- **Copy and Compare** — Copy A to B / B to A, and Compare (A, A-B, B) to audition either endpoint without moving Morph Position. Both have buttons in the editor's morph section; Compare is never saved.
- **MIDI controllers** — Mod wheel and channel pressure push Mass up at every Morph Position without changing the saved sound; pitch bend with an adjustable range (Pitch Bend Range); a sustain pedal that holds released notes until it lifts.
- **Larger saved state** — Projects now save both endpoints, the note behaviour, the morph controls, the random seed and the morph's in-flight position (510 bytes, format version 2). Projects saved by the first scaffold build still load: their globals and macros are kept and every new parameter takes its default.
- **Release-aware tail** — The reported tail follows the longest amp release of the two endpoints, as lengthened or shortened by Tight/Loose, so a host never cuts a release short.
- **The editor: a Main page and an Advanced page** — Main holds the controls that shape a bass in seconds: voice mode, Weight, Dark/Bright and Clean/Violent, Mass, Bass Morph, Depth, Body and Edge, the Anchor waveform and level, Drop, Glide, Fold, the Motion source, Morph Behaviour and Edit Target. Advanced holds the rest in three tabs: Sections, Modulation and Settings. The window is a fixed 960 x 600 and every control has a tooltip.
- **A live harmonic spectrum** — The bars show the harmonics of the note you are playing, as you play it, with the Anchor shown on its own. When you play chords it shows the most recent note while that note sounds, then falls back to the one before. The display never changes the sound. In a host that offers neither VST3 data exchange nor a run loop (a bare test harness, for example) the spectrum stays empty.
- **Bass Morph in the editor** — Compare buttons (A, A-B, B) audition either endpoint without moving Morph Position, and Copy A to B / B to A copies one endpoint over the other. Each endpoint can be given a name (up to 31 bytes of text: 31 plain letters, fewer accented or non-Latin ones), saved with the project, and shows three words describing its shape, colour and force, such as Round, Warm, Clean. When Anchor Morph Mode is Hold and you edit B, the Anchor controls dim and read "Held at A": they still edit B, but the Anchor you hear stays at A.
- **Macro highlight and rings** — Hovering over or moving a Personality macro highlights the controls it acts on. A ring around each of those knobs shows how far the macro pushes it; the knob itself stays where you set it.
- **Zoom** — 100, 125, 150 or 200 %, chosen in the header or in the Settings tab. The zoom is kept for the session and is not saved with the project.
- **Endpoint names are saved** — The saved state grows to 574 bytes (format version 3) to hold the two names. Projects saved by earlier builds still load, with the endpoints named A and B.
- **Registered across the repository** — CI build and test lanes, release packaging and installers, nightly valgrind, clang-tidy, the CPU, shard and close-lane runners, the changelog and specs-index tools, and the root documentation all know about Profundum.
- **53 factory presets in six categories** — Sub (12), Resonant (7), Harmonic (8), Driven (11), Plucked (7) and Animated (8). Each preset leads with one lever of the engine and was authored against a measurement: a render of the preset as stored against the same preset with that lever switched off, so a preset that claims a feature makes it audible. Every preset is checked for mono compatibility, for staying under the output ceiling at full Mass, and for a level within 4 dB of the library median. The presets are generated from the plugin's own parameter surface and installed with the plugin.
- **Preset browser** — A PRESETS button in the header opens the preset browser, with an All tab, one tab per category and its own save dialog. Loading a preset restores the whole saved sound, both endpoints, the endpoint names and the random seed included, also while a note is held.
- **User manual** — `MANUAL.md`, with an HTML page under `docs/`, covers the signal flow, every control by its on-screen name, the Anchor / Mass split, Bass Morph with Edit Target and Compare, the Personality macros, and the factory library by category with a one-line description of each preset.

### Known limitations

- Host sample rates outside 22050 Hz - 192 kHz are refused; the plugin stays silent at such a rate.
- Velocity is not read.
- In a Linux host that offers no run loop (so the SDK timer cannot run), a value the host changes without the controller seeing it shows on the parameter display only at the next Edit Target switch, state save / load or (de)activation. Hosts that provide the run loop refresh it as on the other systems. The sound and the saved state are always current.
