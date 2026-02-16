# Krate Audio Architecture

Living inventory of components and APIs. Reference before writing specs to avoid duplication.

> **Constitution Principle XIII**: Every spec implementation MUST update this document.

**Last Updated**: 2026-02-16 | **Namespace**: `Krate::DSP` | **Include**: `<krate/dsp/...>`

## Repository Structure

```
dsp/                          # Shared KrateDSP library
├── include/krate/dsp/        # Public headers
│   ├── core/                 # Layer 0: Utilities
│   ├── primitives/           # Layer 1: DSP primitives
│   ├── processors/           # Layer 2: Processors
│   ├── systems/              # Layer 3: Systems
│   └── effects/              # Layer 4: Features
└── tests/                    # DSP tests

plugins/iterum/               # Iterum delay plugin
├── src/                      # Plugin source
├── tests/                    # Plugin tests
└── resources/                # UI, presets
```

## Layer Dependency Rules

```
Layer 4: User Features     ← composes layers 0-3
Layer 3: System Components ← composes layers 0-2
Layer 2: DSP Processors    ← uses layers 0-1
Layer 1: DSP Primitives    ← uses layer 0 only
Layer 0: Core Utilities    ← stdlib only, no DSP deps
```

---

## Architecture Sections

This architecture documentation is split into the following sections:

| Section | Description |
|---------|-------------|
| [Layer 0: Core Utilities](layer-0-core.md) | Mathematical utilities, constants, window functions, PRNG, tempo sync, fast math, interpolation |
| [Layer 1: DSP Primitives](layer-1-primitives.md) | DelayLine, LFO, Biquad, Oversampler, FFT, STFT, DCBlocker, Waveshaper, Wavefolder, LadderFilter |
| [Layer 2: DSP Processors](layer-2-processors.md) | EnvelopeFollower, Saturation, TubeStage, DiodeClipper, WavefolderProcessor, TapeSaturator, FuzzProcessor, BitcrusherProcessor, DynamicsProcessor, FormantPreserver, SpectralFreezeOscillator |
| [Layer 3: System Components](layer-3-systems.md) | DelayEngine, FeedbackNetwork, ModulationMatrix, ModulationEngine, CharacterProcessor, TapManager, GrainCloud, AmpChannel |
| [Layer 4: User Features](layer-4-features.md) | Complete delay modes: Tape, BBD, Digital, PingPong, MultiTap, Reverse, Shimmer, Spectral, Freeze, Ducking, Granular |
| [Plugin Architecture](plugin-architecture.md) | VST3 components, parameter flow, state flow, UI components |
| [Plugin Parameter System](plugin-parameter-system.md) | Parameter pack pattern, macro/rungler parameter flows, denormalization mappings |
| [Plugin State Persistence](plugin-state-persistence.md) | State version history, stream format, ModSource enum migration, backward compatibility |
| [Testing](testing.md) | Testing layers, test helpers infrastructure (artifact detection, signal metrics, golden reference) |
| [Quick Reference](quick-reference.md) | Layer inclusion rules, common include patterns, ODR prevention |

---

## Layer Inclusion Rules (Quick Reference)

| Your Code In | Can Include |
|--------------|-------------|
| Layer 0 | stdlib only |
| Layer 1 | Layer 0 |
| Layer 2 | Layers 0-1 |
| Layer 3 | Layers 0-2 |
| Layer 4 | Layers 0-3 |
| Plugin | All DSP layers |

---

## Before Creating New Components

```bash
# Search for existing implementations (ODR prevention)
grep -r "class YourClassName" dsp/ plugins/
grep -r "struct YourStructName" dsp/ plugins/
```
