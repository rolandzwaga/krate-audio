# Changelog

All notable changes to Vorago will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-09-26

The first release you can actually design sounds with. It covers two pieces of
work: the full parameter surface (Phase 12), which had no release of its own,
and the real interface (Phase 13) that replaces the placeholder editor. The
twelve macros now do what their names say, every engine setting worth turning
is exposed and saved, and the editor puts the macros around a live view of the
ecosystem that is making the sound.

### Added

- **Every engine parameter is exposed** — 108 registered parameters, 106 of them saved with the project, across cloud, noise, resonance, ecology, sub, smear, events, ecosystem, body, space, envelope, bloom, ghost and life. Time, frequency and rate controls use a log taper, so the useful range isn't squeezed into the first few degrees of the knob; levels, amounts and bipolar controls stay linear.
- **Seed** — A list of 16 curated seeds for the ecosystem. Changing it while a note sounds reseeds straight away, so you hear the habitat reorganise instead of waiting for the next note. Renders are still a pure function of the saved state and the notes played.
- **Output Saturation** — The amount of the engine's output-stage saturation is now a parameter in the header, instead of a fixed setting.
- **Sustain pedal and channel pressure** — CC64 holds notes until the pedal lifts. Channel pressure is added to the Pressure macro, so leaning on the keys pushes the drone without moving the knob. Neither is saved; they are performance controls.
- **The interface** — A fixed 1100 × 760 window built around the idea, not the engine: the twelve macros as large knobs either side of the ecosystem view, then seven pages (Cloud, Noise, Resonance, Body, Sub / Smear, Space, Life) for everything underneath. Every saved parameter has exactly one control.
- **The ecosystem view** — A live picture of the agents inside the sounding voice. Each agent glows with its energy, the links between agents fade in and out with how much they exchange, and the whole habitat dims as the voice releases. A small count shows how many voices are sounding.
- **Preset browser** — A button in the header opens the preset browser, with its own save dialog, reading the `Drones` category.
- **Gravity tells you when it does nothing** — Gravity only acts when the Resonance Anchor is set to Hybrid. In any other anchor mode the Gravity knob's label and tooltip say it is inert. The knob still moves and is still saved; it is just honest about it.

### Changed

- **The twelve macros are live** — Darkness, Age, Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth and Mass now drive the engine. At their defaults the plugin sounds as 0.1.0 did.
- **State format version 2** — Projects and presets saved with 0.1.0 still load; the new parameters come up at their defaults.

### Known limitations

- No factory presets ship yet; the `Drones` category is still empty. The library is Phase 14.
- The window is fixed at 1100 × 760 and cannot be resized.
- The ecosystem view follows the newest note only. When several notes sound, the older voices keep playing but are not drawn.
- No per-note MPE; channel pressure is the only pressure input.

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
