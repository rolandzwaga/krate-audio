# Changelog

All notable changes to Profundum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - Unreleased

First cut of the plugin. Profundum has existed since Phase 1 as a set of KrateDSP
components with unit tests and no way to play them; this release wraps the
finished `ProfundumEngine` in a VST3 you can load, play a bass line on, and hear.
The sound design surface is deliberately not here yet: this is the scaffold the
parameter work (Phase 11), the interface and the factory library are built on.

### Added

- **Profundum is a plugin** — A registered VST3 instrument (`Profundum.vst3`, AU `aumu`/`Prfd`/`KrAt`, bundle `com.krateaudio.profundum`). Every note runs the whole mass-bass engine, and the engine's true-peak limiter is the last stage inside it.
- **One event input, one stereo output** — No audio input and no sidechain bus. The AU declares exactly one channel configuration, 0 in / 2 out.
- **Four global controls** — Output gain (attenuation only, -60 dB to 0 dB with silence at the bottom, smoothed, applied after the limiter so it can never push past the ceiling), voice mode (Mono or Poly), polyphony (1-8, four by default) and output ceiling (-12 to 0 dBTP, -1 by default). All four are saved with the project.
- **Four live Personality macros: Weight, Dark/Bright, Tight/Loose, Clean/Violent** — Each one moves the sound from the first release; the centre position (0.5) is neutral and changes nothing. All four are saved with the project.
- **Host tempo** — When the host reports a tempo, the engine's tempo-synced motion follows it; otherwise it runs at 120 BPM.
- **64 samples of reported latency** — Constant at every sample rate: the output limiter's look-ahead. The plugin reports its delay so the host can compensate.
- **Presets scan the `Sub` category** — The preset browser is live and reads `Krate Audio/Profundum/Sub`. The category ships empty. The name is permanent: later releases add categories beside it and never rename it.
- **Test suite `profundum_tests`** — Bus layout, parameter denormalisation, state round trip (hostile streams included), MIDI event ordering and overflow, lifecycle, editor lifecycle, wrapper transparency, parameter audibility, zero allocation on the audio thread, and a timed CPU case run on its own.
- **Registered across the repository** — CI build and test lanes, release packaging and installers, nightly valgrind, clang-tidy, the CPU, shard and close-lane runners, the changelog and specs-index tools, and the root documentation all know about Profundum.

### Known limitations

- The editor is a placeholder: one panel of stock controls, enough to prove the parameter wiring.
- Only the four global parameters and the four macros are exposed. Mass, Morph position and every engine parameter arrive in Phase 11.
- Host sample rates outside 22050 Hz - 192 kHz are refused; the plugin stays silent at such a rate.
- No pitch bend, sustain pedal or MIDI CC. Only note-on and note-off are read; these arrive in Phase 11.
- No factory presets ship.
