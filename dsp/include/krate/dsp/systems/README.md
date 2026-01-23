# Layer 3: System Components

This folder contains high-level system components that orchestrate multiple processors and primitives into cohesive audio systems. **Layer 3 depends on Layers 0-2** (core, primitives, and processors).

Systems handle complex multi-component tasks like delay engines, modulation routing, feedback networks, and character processing.

## Files

### Delay Systems

| File | Purpose |
|------|---------|
| `delay_engine.h` | High-level delay wrapper with free/synced time modes, parameter smoothing, and dry/wet mixing. The core building block for delay effects. |
| `tap_manager.h` | Multi-tap delay management with per-tap timing, level, pan, and feedback. Powers multi-tap and tape-style delay effects. |

### Feedback Routing

| File | Purpose |
|------|---------|
| `feedback_network.h` | Basic feedback network for routing delay output back to input with filtering and limiting. |
| `flexible_feedback_network.h` | Advanced feedback routing with cross-channel feedback, processor insertion points, and configurable topologies. |

### Granular

| File | Purpose |
|------|---------|
| `granular_engine.h` | Complete granular synthesis engine managing grain scheduling, playback, and mixing. Core of granular delay effects. |

### Character & Color

| File | Purpose |
|------|---------|
| `character_processor.h` | Analog character emulation with four modes: Clean (bypass), Tape (saturation + wow/flutter + hiss), BBD (bandwidth limiting + clock noise), and DigitalVintage (bit/rate reduction). |
| `tape_machine.h` | Complete tape machine emulation including transport, heads, bias, and degradation modeling. |

### Stereo & Spatial

| File | Purpose |
|------|---------|
| `stereo_field.h` | Stereo field manipulation: width control, mid/side balance, and stereo enhancement. |
| `modulation_matrix.h` | Flexible modulation routing connecting LFOs, envelopes, and other sources to parameters. |

### Distortion Systems

| File | Purpose |
|------|---------|
| `amp_channel.h` | Complete amplifier channel with gain staging, EQ, and speaker simulation. |
| `fuzz_pedal.h` | Complete fuzz pedal emulation with input/output staging and tone control. |
| `distortion_rack.h` | Chainable distortion rack for combining multiple saturation types in series. |

## Usage

Include files using the `<krate/dsp/systems/...>` path:

```cpp
#include <krate/dsp/systems/delay_engine.h>
#include <krate/dsp/systems/character_processor.h>
#include <krate/dsp/systems/tap_manager.h>
```

## Design Principles

- **Orchestration** - systems coordinate multiple lower-level components
- **User-facing parameters** - expose meaningful controls (not raw DSP values)
- **State management** - handle complex internal state and transitions
- **Cross-fade between modes** - smooth transitions when switching configurations
- **Noexcept processing** with all allocation done in prepare()

## Architecture

Systems are designed to be composed by Layer 4 effects:

```
┌─────────────────────────────────────────────────┐
│ Layer 4: Effect (e.g., TapeDelay)               │
│  ┌────────────────┐  ┌────────────────────────┐ │
│  │ TapManager     │  │ CharacterProcessor     │ │
│  │ (Layer 3)      │  │ (Layer 3)              │ │
│  │  ┌──────────┐  │  │  ┌──────────────────┐  │ │
│  │  │DelayLine │  │  │  │SaturationProcessor│ │ │
│  │  │(Layer 1) │  │  │  │(Layer 2)          │ │ │
│  │  └──────────┘  │  │  └──────────────────┘  │ │
│  └────────────────┘  └────────────────────────┘ │
└─────────────────────────────────────────────────┘
```
