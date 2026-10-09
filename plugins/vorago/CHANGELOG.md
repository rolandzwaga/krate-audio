# Changelog

All notable changes to Vorago will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-10-06

The first public release: the factory library. Every one of the 42 presets was
authored against a measurement — a long render of the preset as stored against
the same preset with its named feature switched off — so a preset that claims
a feature makes that feature audible, and every preset holds its level over a
seven-minute hold. The parameter surface and the editor are the 0.2.0 ones; what
changed is the library, two ecosystem rule knobs, the state format that carries
them, and the plugin telling the host how long its tail is.

Be patient with it. Vorago is slow by design: hold the note. In most presets
the first sound takes several seconds to surface and the drone keeps swelling
for about two and a half minutes (the default attack runs 155 seconds); Sudden
Chasm, Growth Ring and Slow Bloom are the faster ones. A note that starts in
near silence is the instrument working, not a fault.

### Added

- **42 factory presets in seven categories** — Drones, Abyss, Caverns, Organisms, Machines, Textures and Ghosts, at least three per category. Each preset leads with one feature of the engine (a section, a concept macro, a colony route, a body material, a noise model, a growth envelope or an attack span) and carries the others as secondaries. The presets are generated from the plugin's own parameter surface and installed with the plugin.
- **Ecosystem Sync and Self Affinity** — Two rule knobs on the Life page (IDs 901 and 902) that shape the colony itself: how strongly the agents fall into step with one another, and how much each agent keeps to its own kind. Both are saved with the project.
- **Tail length** — The plugin reports its tail to the host (the cavern's decay plus the release), so offline renders and freeze-tail bounces are not cut short.
- **Freeze as a gesture** — Freeze holds the cavern's current field for as long as it is on; play a note, let the drone bloom, engage Freeze and the space hangs while the voice keeps singing into it. Cathedral Void is built around it.

### Changed

- **State format v3** — 436 bytes: the 0.2.0 stream plus the two rule knobs. Projects saved with 0.2.0 and 0.1.0 load unchanged; the new knobs take their defaults.
- **Default ghost level is 0** — The ghost layer is silent unless a preset or the user raises it; presets that depend on it set it explicitly.
- **Engine levels retuned so every feature is audible** (Phases 13b and 13c, measured preset by preset): the ghost layer carries a 21 dB make-up, the feedback ecology a 30 dB wet make-up, the breathing and tidal life lanes a gain of 3 with a 0.8 tidal rate, the colony child gain is 1.5 and Fog now also decoheres the smear; the attack reshapes as a quartic rise so slow swells are heard growing; the ecosystem agent state exposes its sync rate and self affinity. Presets saved with 0.2.0 will sound louder in those layers.
- **Ghost density and event triggers add** — With Ghost Event Triggers on, the triggered grains add on top of the density scheduler instead of replacing it.

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
