# Ruinae

**A chaos/spectral hybrid synthesizer for VST3**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)]()

---

## Overview

Ruinae is a polyphonic synthesizer that blends conventional synthesis with experimental sound sources. It pairs 10 oscillator types — from classic PolyBLEP and wavetable to chaos attractors, particle clouds, and spectral freeze — with a deep modulation system, a full arpeggiator, and a post-voice effects chain. Built with the Steinberg VST3 SDK and VSTGUI.

## Features

- **10 Oscillator Types** per voice, with dual oscillators (OSC A + OSC B) and spectral morphing
- **13 Filter Types** including SVF, ladder, formant, comb, envelope filter, and self-oscillating
- **6 Distortion Types** from wavefolder and tape saturation to spectral and granular distortion
- **Full Arpeggiator** with 10 modes, 6 independent lanes, conditional triggers, and euclidean timing
- **Deep Modulation** with 8-slot mod matrix, 2 LFOs, chaos/rungler sources, 4 macros, and per-voice routing
- **4-Effect Chain** — Phaser, Delay (5 types), Harmonizer (up to 4 voices), and Reverb
- **3 ADSR Envelopes** per voice with per-segment curve control and Bezier mode
- **Trance Gate** with step sequencer, euclidean mode, and tempo sync
- **Up to 16 Voices** with configurable allocation, steal modes, and gain compensation
- **Preset Browser** with category filtering and search
- **Cross-Platform** — Windows, macOS (Intel & Apple Silicon), Linux

## Oscillator Types

| Type | Description |
|------|-------------|
| **PolyBLEP** | Anti-aliased digital — Sine, Saw, Square, Pulse, Triangle |
| **Wavetable** | Mipmapped single-cycle wavetable playback |
| **Phase Distortion** | Casio-style PD with 8 waveforms and distortion amount |
| **Oscillator Sync** | Hard/Reverse/Phase Advance sync with configurable master-slave ratio |
| **Additive** | FFT-based harmonic synthesis with up to 128 partials and spectral tilt |
| **Chaos Attractor** | Lorenz, Rossler, Chua, Duffing, Van der Pol as audio-rate oscillators |
| **Particle** | Granular cloud with scatter, density (1-64), lifetime, and drift |
| **Formant** | Vowel synthesis with continuous A-E-I-O-U morphing |
| **Spectral Freeze** | FFT-based with pitch shift, spectral tilt, and formant shift |
| **Noise** | White, Pink, Brown, Blue, Violet, Grey |

## Filter Types

| Type | Notes |
|------|-------|
| SVF LP/HP/BP/Notch/Allpass | 12 or 24 dB/oct, drive 0-24 dB |
| SVF Peak/Low Shelf/High Shelf | Gain +/-24 dB |
| Ladder | 6/12/18/24 dB slopes, drive 0-24 dB |
| Formant | Vowel morph with gender control |
| Comb | With damping control |
| Envelope Filter | LP/BP/HP with sensitivity, depth, attack, release |
| Self-Oscillating | With glide, external mix, and shape |

## Effects Chain

Processing order: **Phaser -> Delay -> Harmonizer -> Reverb**

| Effect | Highlights |
|--------|------------|
| **Phaser** | 2-12 stages, tempo sync, stereo spread, 4 LFO waveforms |
| **Delay** | 5 types: Digital (3 eras), Tape, Ping-Pong, Granular, Spectral |
| **Harmonizer** | Up to 4 harmony voices, chromatic/scalic modes, formant preservation |
| **Reverb** | Dattorro plate with freeze, modulation, and diffusion |

## Arpeggiator

| Feature | Details |
|---------|---------|
| **Modes** | Up, Down, UpDown, DownUp, Converge, Diverge, Random, Walk, AsPlayed, Chord |
| **Lanes** | Velocity, Gate, Pitch, Modifier, Ratchet, Condition (each 1-32 steps, independent length) |
| **Modifiers** | Active, Rest, Tie, Slide, Accent (per step) |
| **Conditions** | 18 trigger types: probability (10%-90%), cycle-based (every Nth), First, Fill |
| **Euclidean** | Configurable hits, steps, and rotation for euclidean rhythm patterns |
| **Extras** | Spice/Dice randomization, humanize, swing, latch (Off/Hold/Add), 1-4 octave range |

## Modulation System

**13 global sources**: LFO 1, LFO 2, Env Follower, Random, Chaos, Rungler, Sample & Hold, Pitch Follower, Transient, Macro 1-4

**8 voice-local sources**: Amp Env, Filter Env, Mod Env, Voice LFO, Gate, Velocity, Key Track, Aftertouch

**8-slot modulation matrix** with per-slot curve (Linear/Exp/Log/S-Curve), smoothing, and scaling

## Signal Flow

```
Per Voice:
  OSC A + OSC B -> Mixer (Crossfade or Spectral Morph)
    -> Filter -> Distortion -> DC Blocker -> Trance Gate -> VCA

Global:
  Voices -> Stereo Pan + Sum -> Stereo Width -> Global Filter
    -> Phaser -> Delay -> Harmonizer -> Reverb
    -> Master Gain (1/sqrt(N) compensation) -> Soft Limiter -> Output
```

## Installation

### Pre-built Releases

Download the latest release from the [Releases](https://github.com/rolandzwaga/iterum/releases) page.

| Platform | Installer | Install Location |
|----------|-----------|------------------|
| **Windows** | `Ruinae-x.x.x-Windows-x64.exe` | `C:\Program Files\Common Files\VST3\Krate Audio\Ruinae` |
| **macOS** | `Ruinae-x.x.x-macOS.pkg` | `/Library/Audio/Plug-Ins/VST3/` |
| **Linux** | `Ruinae-x.x.x-Linux-x64.tar.gz` | Extract to `~/.vst3/` or `/usr/lib/vst3/` |

### Building from Source

See the [main repository README](../../README.md) for build instructions.

Quick start:

```bash
# From repository root
cmake --preset windows-x64-release && cmake --build --preset windows-x64-release
```

The built plugin will be in `build/<preset>/VST3/Release/Ruinae.vst3`.

## Plugin Structure

```
plugins/ruinae/
├── src/
│   ├── entry.cpp             # VST3 factory registration
│   ├── plugin_ids.h          # Parameter IDs and GUIDs
│   ├── version.h             # Version information
│   ├── processor/            # Audio processing (real-time audio thread)
│   │   ├── processor.h
│   │   └── processor.cpp
│   ├── controller/           # UI and parameter handling (UI thread)
│   │   ├── controller.h
│   │   └── controller.cpp
│   ├── engine/               # Synth engine, voice, effects chain
│   │   ├── ruinae_engine.h
│   │   ├── ruinae_voice.h
│   │   └── ruinae_effects_chain.h
│   └── parameters/           # Per-section parameter registration helpers
├── tests/
│   └── unit/                 # Unit and integration tests
└── resources/
    ├── editor.uidesc         # VSTGUI editor description
    └── presets/              # Factory presets
```

## Technical Details

### Architecture

Ruinae follows the VST3 architecture with strict separation between:

- **Processor** (`IAudioProcessor`) — Handles audio processing on the real-time audio thread
- **Controller** (`IEditController`) — Manages UI and parameters on the main thread

Communication between processor and controller uses VST3's message system, never direct function calls.

### DSP Foundation

Ruinae is built on the [KrateDSP library](../../dsp/), which provides:

- Polyphonic voice allocator with configurable steal strategies
- SVF and ladder filters with drive and resonance
- FFT-based spectral processing (additive oscillator, spectral morph, spectral freeze)
- Chaos attractors (Lorenz, Rossler, Chua, Duffing, Van der Pol)
- Particle oscillator for granular cloud synthesis
- Dattorro plate reverb, phaser, and delay lines
- Pitch shifting with granular, phase vocoder, and pitch sync algorithms

### Real-Time Safety

The audio processor follows strict real-time constraints:

- No memory allocation in `process()`
- No locks or mutexes
- No I/O operations
- Bounded execution time
- All oscillator types, filters, and effects pre-allocated at `prepare()` time

## License

This project is licensed under the MIT License - see the [LICENSE](../../LICENSE) file for details.

## Author

**Roland Zwaga** - [Krate Audio](https://github.com/rolandzwaga)

---

*Part of the [Krate Audio](../../README.md) monorepo*
