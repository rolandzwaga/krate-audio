# Seraphis

**A spectral organism synthesizer for VST3**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)]()

---

## Overview

Seraphis is the ethereal counterpart to [Ruinae](../ruinae/README.md): where Ruinae is instability,
chaos and aggression, Seraphis is emergence, resonance and weightlessness. Each voice is a 64-partial
harmonic cloud driven by a spectral-evolution layer, resonated through a continuous modal body, with a
parallel granular atmosphere mixed in. A family of slow autonomous *life modulators* — Brownian drift,
breathing, tides, orbits — animates all of it, so nothing is ever static, and an integrated Aether space
engine (infinite FDN reverb, shimmer bloom, spectral diffusion) sits **in** the signal path rather than
behind it. Built with the Steinberg VST3 SDK and VSTGUI on top of the
[KrateDSP library](../../dsp/).

[**Website & Documentation**](https://krateaudio.com/seraphis/) | [**Changelog**](CHANGELOG.md)

## Status — 1.0.0

First general release (see [CHANGELOG.md](CHANGELOG.md) for the full history from the 0.1.0 scaffold).

| Shipping | Not yet |
|---|---|
| Event input + stereo output, 1-16 voices, 1024 samples of reported latency | MPE and per-note expression |
| Every engine parameter: cloud, morph/entropy, life modulators, body, atmosphere, aether, effects | Drawer slide animation; the window is a fixed 1000 × 700 |
| Five macros, wired, with the macro-first interface and the edit drawer | The whole-`process()` optimisation pass against the 25 %-of-one-core target |
| Ten factory spectra and 42 factory presets across seven categories (Bells, Choirs, Cinematic, Drones, Motion, Pads, Textures) | |
| Project state version 4, loading every earlier version | |

## Identity

| | |
|---|---|
| VST3 bundle | `Seraphis.vst3`, bundle base `com.krateaudio.seraphis` |
| AU | type `aumu`, subtype `Srph`, manufacturer `KrAt` |
| Buses | 1 event input, 1 stereo audio output (no audio input) |
| Presets | `Krate Audio/Seraphis/<Category>` — Bells, Choirs, Cinematic, Drones, Motion, Pads, Textures |

## Building

From the repository root. On Windows CMake must be invoked by full path — the Python `cmake` wrapper on
`PATH` does not work.

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"

# Configure once
"$CMAKE" --preset windows-x64-release

# Build just this plugin
"$CMAKE" --build build/windows-x64-release --config Release --target Seraphis
```

The built plugin lands in `build/windows-x64-release/VST3/Release/Seraphis.vst3`. The post-build copy to
`C:/Program Files/Common Files/VST3/` may fail with a permission error; the compile itself still succeeded.

On macOS / Linux use the `macos-release` / `linux-release` presets and the same `--target Seraphis`.

## Testing

```bash
# Build the test target
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target seraphis_tests

# Run it (Catch2 prints the pass/fail summary on the last lines)
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5

# Run a single case by name (positional filter, not -c)
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_ProcessorRendersHeldNote*"
```

## Validation

```bash
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"
```

macOS additionally: `auval -v aumu Srph KrAt`.

Static analysis:

```powershell
./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja
```

```bash
./tools/run-clang-tidy.sh --target seraphis
```

## Plugin structure

```
plugins/seraphis/
├── src/
│   ├── entry.cpp                     # VST3 factory registration
│   ├── plugin_ids.h                  # FUIDs, parameter IDs, state version
│   ├── version.h                     # GENERATED from version.json
│   ├── processor/                    # Audio processing (real-time audio thread)
│   ├── controller/                   # Parameters and UI (main thread)
│   ├── parameters/                   # one header per section: global, macro, cloud, morph,
│   │                                 #   life_mod, body, atmosphere, aether, effects
│   ├── engine/                       # seraphis_engine_config.h (prepare-time config)
│   ├── preset/                       # preset-manager config adapter
│   ├── update/                       # update-checker config adapter
│   └── ui/                           # cloud view, macro ring knob, edit drawer + sub-controller
├── tests/                            # seraphis_tests (unit + integration)
├── docs/                             # website page, manual template, signal-flow graph
└── resources/
    ├── editor.uidesc                 # VSTGUI editor description
    ├── au-info.plist  auv3/          # Audio Unit wrapper configuration
    └── presets/<Category>/           # 42 factory presets in seven categories
```

`src/version.h`, `resources/win32resource.rc` and `resources/auv3/audiounitconfig.h` are generated by
CMake from `version.json`; only `auv3/audiounitconfig.h.in` is authored. A version bump touches
`version.json` and `CHANGELOG.md` and nothing else.

## Real-time safety

`process()` allocates nothing, takes no locks, throws nothing and does no I/O. The engine and the Aether
reverb are constructed once in `initialize()` and prepared in `setupProcessing()`; every scratch buffer is
sized there to a fixed 2048-sample bound, and host blocks larger than that are sub-divided.

## License

MIT — see the [LICENSE](../../LICENSE) file.

## Author

**Roland Zwaga** — [Krate Audio](https://krateaudio.com/)

---

*Part of the [Krate Audio](../../README.md) monorepo*
