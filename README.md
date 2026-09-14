# Krate Audio

**A monorepo for audio plugins and the KrateDSP library**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)]()

---

## Overview

This monorepo contains the **KrateDSP** shared DSP library and audio plugins built on top of it. The architecture enables code reuse across plugins while maintaining a clean separation between DSP algorithms and plugin-specific implementations.

This project is a personal endeavour to investigate how viable it is to develop a DSP library, and VST3 plugins built on top of this library, using AI assisted development.

## Repository Structure

```
krate-audio/
├── dsp/                      # KrateDSP - Shared DSP library
│   ├── include/krate/dsp/    # Public headers
│   │   ├── core/             # Layer 0: Math utilities, constants
│   │   ├── primitives/       # Layer 1: Basic DSP blocks (filters, delay lines, LFOs)
│   │   ├── processors/       # Layer 2: Composed processors (filters, saturation, dynamics)
│   │   ├── systems/          # Layer 3: Complex systems (feedback networks, voices, organisms)
│   │   └── effects/          # Layer 4: Complete effect algorithms
│   └── tests/                # DSP unit tests (7200+ test cases, one executable per layer)
│
├── plugins/
│   ├── iterum/               # Iterum - Delay plugin with 10 modes
│   ├── disrumpo/             # Disrumpo - Multiband morphing distortion
│   ├── ruinae/               # Ruinae - Chaos/spectral hybrid synthesizer
│   ├── innexus/              # Innexus - Harmonic analysis/resynthesis instrument
│   ├── gradus/               # Gradus - Standalone step arpeggiator
│   ├── membrum/              # Membrum - Physically-modelled drum synthesizer
│   ├── seraphis/             # Seraphis - Spectral organism synthesizer
│   ├── vorago/               # Vorago - Dark ambient drone instrument
│   └── shared/               # Shared plugin infrastructure (UI controls, presets, MIDI, update)
│
├── tools/
│   ├── control_testbench/    # Standalone app for testing custom VSTGUI controls
│   ├── krate-render/         # Offline plugin renderer (audio verification)
│   ├── membrum-fit/          # Offline drum-sample fitter
│   ├── pluginval.exe         # Plugin validation tool
│   ├── run-clang-tidy.*      # Static analysis runners (ps1/sh)
│   ├── *_preset_generator.*  # Factory preset generators
│   ├── lint-*.js             # Architecture/ODR/portability lints (run by CI)
│   └── *.js                  # Dev helpers (crash dump analysis, signal flow, repo maps)
│
├── tests/                    # Shared test helpers and DSP benchmarks
├── extern/vst3sdk/           # Steinberg VST3 SDK (submodule)
└── extern/dr_libs/           # dr_wav single-header WAV loader (vendored, public domain)
```

Every plugin follows the same `src/ tests/ docs/ resources/` skeleton. pffft (SIMD FFT, BSD) and Google Highway (SIMD math, Apache-2.0) are fetched by CMake at configure time and land in `build/_deps/`.

## Plugins

### [Iterum](plugins/iterum/README.md)

A feature-rich VST3/AU delay plugin with 10 distinct delay algorithms - from vintage tape and analog emulations to modern granular and spectral processing.

[**Website & Documentation**](https://krateaudio.com/iterum/) | [**Plugin README**](plugins/iterum/README.md)

### [Disrumpo](plugins/disrumpo/docs/index.html)

A multiband morphing distortion VST3 plugin with a 4-band crossover network, smooth morph transitions between distortion types, and a sweep system with LFO and envelope modes.

[**Website & Documentation**](https://krateaudio.com/disrumpo/) | [**Changelog**](plugins/disrumpo/CHANGELOG.md)

### [Ruinae](plugins/ruinae/README.md)

A chaos/spectral hybrid synthesizer blending 10 oscillator types — from classic PolyBLEP and wavetable to chaos attractors, particle clouds, and spectral freeze — with a deep modulation matrix, full arpeggiator, and a post-voice effects chain.

[**Website & Documentation**](https://krateaudio.com/ruinae/) | [**Plugin README**](plugins/ruinae/README.md)

### [Innexus](plugins/innexus/README.md)

A harmonic analysis and resynthesis VST3/AU instrument. Analyzes audio samples or live sidechain input to extract harmonic content, then resynthesizes it as a playable instrument with independent control over harmonics, residual noise, and transients.

[**Website & Documentation**](https://krateaudio.com/innexus/) | [**Plugin README**](plugins/innexus/README.md)

### [Gradus](plugins/gradus/docs/index.html)

A standalone step arpeggiator with 8 independent polymetric lanes plus a MIDI delay lane, per-lane speed multipliers, Euclidean rhythms, conditional triggers, chord generation, and scale quantization. Runs as a VST3 instrument with MIDI output for driving any synth, plus a built-in audition voice. Shares arp parameter IDs with Ruinae for cross-plugin preset exchange.

[**Website & Documentation**](https://krateaudio.com/gradus/) | [**Changelog**](plugins/gradus/CHANGELOG.md)

### [Membrum](plugins/membrum/docs/index.html)

A physically-modelled drum synthesizer: 32 pads, 6 exciter types, 6 body models, parallel noise and click layers, head/shell coupling, and 20 factory kits.

[**Website & Documentation**](https://krateaudio.com/membrum/) | [**Changelog**](plugins/membrum/CHANGELOG.md)

### [Seraphis](plugins/seraphis/README.md)

A spectral organism synthesizer: each voice is a 64-partial harmonic cloud driven by a spectral-evolution layer, resonated through a continuous modal body, with a parallel granular atmosphere. Slow autonomous life modulators animate everything, and an integrated Aether space engine (infinite FDN reverb, shimmer bloom, spectral diffusion) sits in the signal path.

[**Website & Documentation**](https://krateaudio.com/seraphis/) | [**Plugin README**](plugins/seraphis/README.md)

### Vorago (in development)

A dark ambient drone instrument, Seraphis's subterranean sibling: sound masses grown from emergent agent interaction and slow discrete events rather than authored states. The DSP layer is being built first, phase by phase, as `Krate::DSP` systems (slow event engine, noise organism, resonance drift network, spectral smear, feedback ecology, subharmonic engine); the plugin itself follows once the engine is complete.

[**Roadmap**](specs/Vorago-roadmap.md)

## KrateDSP Library

The KrateDSP library provides reusable DSP components organized in a 5-layer architecture where each layer can only depend on layers below it:

| Layer | Directory | Components | Examples |
|-------|-----------|------------|----------|
| 0 | `core/` | 38 math/utility headers | dB conversion, sigmoid functions, interpolation, window functions, RNG |
| 1 | `primitives/` | 54 basic DSP blocks | Biquad, SVF, ladder filter, delay lines, LFOs, FFT/STFT, oscillators, smoothers |
| 2 | `processors/` | 108 composed processors | Filters (formant, phaser, spectral morph), saturation, resonator bank, particle oscillator, envelope followers |
| 3 | `systems/` | 45 complex systems | Tape machine, amp channel, feedback networks, granular engine, synth voices, modulation matrix, spectral organisms, drone ecosystems (Vorago) |
| 4 | `effects/` | 15 complete algorithms | Tape delay, granular delay, shimmer, spectral, BBD, reverb, flanger |

### Key Features

- **Real-Time Safe** - No allocations in audio processing, lock-free operations
- **Modern C++20** - RAII, constexpr, concepts, value semantics
- **Cross-Platform** - Windows, macOS (Intel & Apple Silicon), Linux
- **Extensively Tested** - 7200+ DSP test cases across 320+ test files (10,800+ across the whole repo), spectral analysis, and approval testing
- **SIMD-Optimized FFT** - pffft backend with SSE (x86/x64) and NEON (ARM) acceleration
- **Composable Anti-Aliasing** - Oversampling applied at appropriate abstraction levels
- **Physical Modeling** - Resonator banks, formant filters, waveguide primitives, modal drum bodies

## Building

### Prerequisites

- CMake 3.20+
- C++20 compiler (MSVC 2022, Clang 14+, GCC 11+)
- VST3 SDK (included as submodule)

### Build Commands

```bash
# Clone with submodules
git clone --recursive https://github.com/rolandzwaga/krate-audio.git
cd krate-audio

# Configure and build (choose your platform)
cmake --preset windows-x64-release && cmake --build --preset windows-x64-release
cmake --preset macos-release && cmake --build --preset macos-release
cmake --preset linux-release && cmake --build --preset linux-release

# Run all tests
ctest --test-dir build/windows-x64-release -C Release
ctest --test-dir build/macos-release -C Release
ctest --test-dir build/linux-release -C Release

# Run DSP tests only (one executable per layer)
cmake --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests
./build/windows-x64-release/bin/Release/dsp_core_tests
```

### Build Outputs

| Target | Location |
|--------|----------|
| Plugins | `build/<preset>/VST3/Release/<Plugin>.vst3` (Iterum, Disrumpo, Ruinae, Innexus, Gradus, Membrum, Seraphis) |
| DSP tests | `build/<preset>/bin/Release/dsp_{core,primitives,processors,systems,effects}_tests` |
| Plugin tests | `build/<preset>/bin/Release/<plugin>_tests` (Iterum: `plugin_tests` and `approval_tests`) |
| Shared infrastructure tests | `build/<preset>/bin/Release/shared_tests` |
| KrateDSP library | `build/<preset>/lib/Release/KrateDSP.lib` |

## Technical Highlights

- **Layered Architecture** - Clean dependency hierarchy prevents circular dependencies
- **Header-Only DSP** - Most DSP code is header-only for inlining and optimization
- **Constitution-Driven** - Development follows documented principles in `.specify/memory/constitution.md`
- **Spec-First Development** - Features are specified before implementation

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Author

**Roland Zwaga** - [Krate Audio](https://krateaudio.com/)

---

*Built with the [Steinberg VST3 SDK](https://steinbergmedia.github.io/vst3_dev_portal/) and [VSTGUI](https://steinbergmedia.github.io/vst3_doc/vstgui/html/)*
