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
│   │   ├── systems/          # Layer 3: Complex systems (feedback networks, character)
│   │   └── effects/          # Layer 4: Complete effect algorithms
│   └── tests/                # DSP unit tests (5400+ test cases)
│
├── plugins/
│   ├── iterum/               # Iterum - Delay plugin with 11 modes
│   │   ├── src/              # Plugin source (processor, controller, UI)
│   │   ├── tests/            # Plugin-specific tests
│   │   └── resources/        # UI assets, presets, installers
│   ├── disrumpo/             # Disrumpo - Multiband morphing distortion
│   │   ├── src/              # Plugin source (processor, controller, UI)
│   │   ├── tests/            # Plugin-specific tests
│   │   └── resources/        # UI assets, presets
│   ├── ruinae/               # Ruinae - Chaos/spectral hybrid synthesizer
│   │   ├── src/              # Plugin source (engine, processor, controller)
│   │   ├── tests/            # Plugin-specific tests
│   │   └── resources/        # UI assets, presets, installers
│   └── shared/               # Shared plugin components (UI controls, presets, MIDI)
│       ├── src/ui/           # Reusable VSTGUI controls
│       └── tests/            # Shared component tests
│
├── tools/
│   └── control_testbench/    # Standalone app for testing custom VSTGUI controls
│
├── tests/                    # Shared test infrastructure
├── extern/vst3sdk/           # Steinberg VST3 SDK (submodule)
└── extern/pffft/             # SIMD-optimized FFT library (BSD license)
```

## Plugins

### [Iterum](plugins/iterum/README.md)

A feature-rich VST3/AU delay plugin with 11 distinct delay algorithms - from vintage tape and analog emulations to modern granular and spectral processing.

[**Website & Documentation**](https://rolandzwaga.github.io/krate-audio/iterum//) | [**Plugin README**](plugins/iterum/README.md)

### [Disrumpo](plugins/disrumpo/README.md)

A multiband morphing distortion VST3 plugin with a 4-band crossover network, smooth morph transitions between distortion types, and a sweep system with LFO and envelope modes.

[**Website & Documentation**](https://rolandzwaga.github.io/krate-audio/disrumpo//) | [**Plugin README**](plugins/disrumpo/README.md)

### Ruinae

A chaos/spectral hybrid synthesizer VST3 plugin featuring a modular synthesis engine with modulation matrix, multiple oscillator types, and an effects chain.

## KrateDSP Library

The KrateDSP library provides reusable DSP components organized in a 5-layer architecture where each layer can only depend on layers below it:

| Layer | Directory | Components | Examples |
|-------|-----------|------------|----------|
| 0 | `core/` | Math utilities, constants | dB conversion, sigmoid functions, interpolation, window functions |
| 1 | `primitives/` | 49 basic DSP blocks | Biquad, SVF, ladder filter, delay lines, LFOs, FFT/STFT, oscillators |
| 2 | `processors/` | 68 composed processors | Filters (formant, phaser, spectral morph), saturation, resonator bank, particle oscillator |
| 3 | `systems/` | 29 complex systems | Tape machine, amp channel, feedback networks, granular engine, synth voice, modulation matrix |
| 4 | `effects/` | 13 complete algorithms | Tape delay, granular delay, shimmer, spectral, BBD, reverb |

### Key Features

- **Real-Time Safe** - No allocations in audio processing, lock-free operations
- **Modern C++20** - RAII, constexpr, concepts, value semantics
- **Cross-Platform** - Windows, macOS (Intel & Apple Silicon), Linux
- **Extensively Tested** - 6500+ test cases across 300+ test files, spectral analysis, and approval testing
- **SIMD-Optimized FFT** - pffft backend with SSE (x86/x64) and NEON (ARM) acceleration
- **Composable Anti-Aliasing** - Oversampling applied at appropriate abstraction levels
- **Physical Modeling** - Resonator banks, formant filters, waveguide primitives

## Building

### Prerequisites

- CMake 3.20+
- C++20 compiler (MSVC 2022, Clang 14+, GCC 11+)
- VST3 SDK (included as submodule)

### Build Commands

```bash
# Clone with submodules
git clone --recursive https://github.com/rolandzwaga/iterum.git
cd iterum

# Configure and build (choose your platform)
cmake --preset windows-x64-release && cmake --build --preset windows-x64-release
cmake --preset macos-release && cmake --build --preset macos-release
cmake --preset linux-release && cmake --build --preset linux-release

# Run all tests
ctest --preset windows-x64-release
ctest --preset macos-release
ctest --preset linux-release

# Run DSP tests only
cmake --build build/windows-x64-release --target dsp_tests
./build/windows-x64-release/dsp/tests/Release/dsp_tests
```

### Build Outputs

| Target | Location |
|--------|----------|
| Iterum plugin | `build/<preset>/VST3/Release/Iterum.vst3` |
| Disrumpo plugin | `build/<preset>/VST3/Release/Disrumpo.vst3` |
| Ruinae plugin | `build/<preset>/VST3/Release/Ruinae.vst3` |
| DSP tests | `build/<preset>/dsp/tests/Release/dsp_tests` |
| KrateDSP library | `build/<preset>/dsp/Release/KrateDSP.lib` |

## Technical Highlights

- **Layered Architecture** - Clean dependency hierarchy prevents circular dependencies
- **Header-Only DSP** - Most DSP code is header-only for inlining and optimization
- **Constitution-Driven** - Development follows documented principles in `.specify/memory/constitution.md`
- **Spec-First Development** - Features are specified before implementation

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Author

**Roland Zwaga** - [Krate Audio](https://github.com/rolandzwaga)

---

*Built with the [Steinberg VST3 SDK](https://steinbergmedia.github.io/vst3_dev_portal/) and [VSTGUI](https://steinbergmedia.github.io/vst3_doc/vstgui/html/)*
