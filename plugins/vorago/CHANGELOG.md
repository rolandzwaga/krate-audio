# Changelog

All notable changes to Vorago will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-09-24

First cut of the plugin. Vorago has existed since Phase 1 as a set of KrateDSP
components with unit tests and no way to play them; this release wraps the
finished engine in a VST3 you can load, hold a note on, and hear. The sound
design surface is deliberately not here yet — this is the scaffold the parameter
work (Phase 12), the interface (Phase 13) and the factory library (Phase 14) are
built on.

### Added

- **Vorago is a plugin** — A registered VST3 instrument (`Vorago.vst3`, AU `aumu`/`Vrgo`/`KrAt`, bundle `com.krateaudio.vorago`) with one event input and one stereo output. Holding a note runs the whole drone engine per voice, then the cavern reverb, then the engine's output stage, so the limiter is the last thing the audio meets and the cavern tail is inside the instrument rather than bolted on behind it.
- **Two global controls** — Master gain (smoothed, so moving it never zippers) and voice count (1-6, four by default). Both are saved with the project.
- **Twelve macros: Darkness, Age, Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass** — Present, saved and restored, and shown in the placeholder editor, but **inert in this release**: moving them changes nothing you can hear. They ship now so that the parameter IDs, the defaults and the saved state are fixed before anything depends on them; Phase 12 wires them to the engine.
- **Presets scan the `Drones` category** — The preset browser is live and reads `Krate Audio/Vorago/Drones`. The category ships empty; the factory library is Phase 14. The name is permanent — later releases add categories beside it and never rename it, because a rename orphans every preset saved against it.
- **3072 samples of reported latency** — Constant at every sample rate: the engine's spectral smear (2048) plus the cavern's spectral diffusion (1024). The plugin reports its delay so the host can compensate.

### Known limitations

- The editor is a placeholder: one panel of stock controls, enough to prove the parameter wiring. The real interface is Phase 13.
- Only the two global parameters and the twelve macros are exposed. Every engine parameter is Phase 12.
- No output soft-limit switch; the engine's limiter is always on.
- No MPE, channel pressure or sustain pedal (CC64). Only note-on and note-off are read; these arrive in Phase 12.
- No factory presets ship.
