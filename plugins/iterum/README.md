# Iterum

**A feature-rich VST3/AU delay plugin with 10 distinct delay modes**

[![Website](https://img.shields.io/badge/website-iterum-blue)](https://rolandzwaga.github.io/iterum/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)]()

[**Website & Documentation**](https://rolandzwaga.github.io/iterum/)

---

## Overview

Iterum is a professional-grade delay plugin offering 10 unique delay algorithms, from classic tape and analog emulations to modern granular and spectral processing. Built with the Steinberg VST3 SDK and VSTGUI, it delivers high-quality audio processing with a focus on real-time performance and creative flexibility.

## Features

- **10 Delay Modes** - From vintage to experimental
- **Click-Free Mode Switching** - 50ms equal-power crossfade between all modes
- **Tempo Sync** - Lock delay times to host BPM with musical note values
- **Cross-Platform** - Windows, macOS (Intel & Apple Silicon), Linux
- **Multiple Formats** - VST3 and Audio Unit (macOS)

## Delay Modes

| Mode | Description |
|------|-------------|
| **Granular** | Granular processing with pitch/time spray, freeze, and tempo sync |
| **Spectral** | FFT-based per-band delays with frequency spreading |
| **Shimmer** | Pitch-shifted feedback with diffusion for ethereal textures |
| **Tape** | Classic tape echo with wow, flutter, saturation, and multi-head |
| **BBD** | Bucket-brigade analog character (MN3005, MN3007, MN3205, SAD1024) |
| **Digital** | Clean or vintage digital with era modeling (Pristine, 80s, Lo-Fi) |
| **Ping-Pong** | Stereo alternating delays with cross-feedback and width control |
| **Reverse** | Grain-based reverse processing with multiple playback modes |
| **Multi-Tap** | Up to 16 taps with timing/spatial patterns and morphing |
| **Freeze** | Infinite sustain with shimmer and decay control |

## Installation

### Pre-built Releases

Download the latest release from the [Releases](https://github.com/rolandzwaga/iterum/releases) page.

| Platform | Installer | Install Location |
|----------|-----------|------------------|
| **Windows** | `Iterum-x.x.x-Windows-x64.exe` | `C:\Program Files\Common Files\VST3\Krate Audio\Iterum` |
| **macOS** | `Iterum-x.x.x-macOS.pkg` | VST3: `/Library/Audio/Plug-Ins/VST3/` AU: `/Library/Audio/Plug-Ins/Components/` |
| **Linux** | `Iterum-x.x.x-Linux-x64.tar.gz` | Extract to `~/.vst3/` or `/usr/lib/vst3/` |

### Building from Source

See the [main repository README](../../README.md) for build instructions.

Quick start:

```bash
# From repository root
cmake --preset windows-x64-release && cmake --build --preset windows-x64-release
```

The built plugin will be in `build/<preset>/VST3/Release/Iterum.vst3`.

## Plugin Structure

```
plugins/iterum/
├── src/
│   ├── entry.cpp           # VST3 factory registration
│   ├── plugin_ids.h        # Parameter IDs and GUIDs
│   ├── version.h           # Version information
│   ├── processor/          # Audio processing (runs on audio thread)
│   │   ├── processor.h
│   │   └── processor.cpp
│   └── controller/         # UI and parameter handling (runs on UI thread)
│       ├── controller.h
│       └── controller.cpp
├── tests/                  # Plugin-specific tests
│   ├── unit/               # Unit tests
│   ├── integration/        # Integration tests
│   └── approval/           # Approval tests
└── resources/
    ├── editor.uidesc       # VSTGUI editor description
    ├── presets/            # Factory presets
    └── installer/          # Platform installers
```

## Technical Details

### Architecture

Iterum follows the VST3 architecture with strict separation between:

- **Processor** (`IAudioProcessor`) - Handles audio processing on the real-time audio thread
- **Controller** (`IEditController`) - Manages UI and parameters on the main thread

Communication between processor and controller uses VST3's message system, never direct function calls.

### DSP Foundation

Iterum is built on the [KrateDSP library](../../dsp/), which provides:

- Delay lines with multiple interpolation modes
- Feedback networks with saturation and filtering
- Character processors (tape, BBD, digital vintage)
- Spectral processing (FFT-based effects)
- Granular processing engine

### Real-Time Safety

The audio processor follows strict real-time constraints:

- No memory allocation in `process()`
- No locks or mutexes
- No I/O operations
- Bounded execution time

## License

This project is licensed under the MIT License - see the [LICENSE](../../LICENSE) file for details.

## Author

**Roland Zwaga** - [Krate Audio](https://github.com/rolandzwaga)

---

*Part of the [Krate Audio](../../README.md) monorepo*
