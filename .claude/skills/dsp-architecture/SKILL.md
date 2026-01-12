---
name: dsp-architecture
description: DSP development patterns for this VST3 plugin. Use when working on audio processing, dsp/ folder, processors, effects, delay algorithms, filters, or any code that runs on the audio thread. Covers real-time safety, layered architecture, interpolation choices, and performance budgets.
allowed-tools: Read, Grep, Glob, Bash
---

# DSP Architecture Guide

This skill provides guidance for DSP development in this VST3 plugin project.

## Quick Reference

- **Real-time safety rules**: See [REALTIME-SAFETY.md](REALTIME-SAFETY.md)
- **Layer architecture & files**: See [LAYERS.md](LAYERS.md)
- **Implementation rules & performance**: See [IMPLEMENTATION.md](IMPLEMENTATION.md)

---

## Layered Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    LAYER 4: USER FEATURES                   │
│            (Complete delay modes, full effects)             │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 3: SYSTEM COMPONENTS                 │
│         (Composed processors, modulation systems)           │
├─────────────────────────────────────────────────────────────┤
│                   LAYER 2: DSP PROCESSORS                   │
│            (Filters, saturators, diffusers)                 │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 1: DSP PRIMITIVES                    │
│         (Delay lines, oscillators, envelopes)               │
├─────────────────────────────────────────────────────────────┤
│                    LAYER 0: CORE UTILITIES                  │
│           (Math, conversions, buffer helpers)               │
└─────────────────────────────────────────────────────────────┘
```

### Dependency Rules

- Each layer can **ONLY** depend on layers below it
- No circular dependencies
- Extract utilities to Layer 0 if used by 2+ Layer 1 primitives

### Layer Include Matrix

| Your Layer | Location | Can Include |
|------------|----------|-------------|
| 0 (core/) | `dsp/include/krate/dsp/core/` | stdlib only |
| 1 (primitives/) | `dsp/include/krate/dsp/primitives/` | Layer 0 |
| 2 (processors/) | `dsp/include/krate/dsp/processors/` | Layers 0, 1 |
| 3 (systems/) | `dsp/include/krate/dsp/systems/` | Layers 0, 1, 2 |
| 4 (effects/) | `dsp/include/krate/dsp/effects/` | Layers 0, 1, 2, 3 |

---

## Critical Rules Summary

### Real-Time Thread Safety

The audio thread (`Processor::process()`) has **hard real-time constraints**.

**FORBIDDEN on audio thread:**
```cpp
new / delete / malloc / free          // Memory allocation
std::vector::push_back()              // May reallocate
std::mutex / std::lock_guard          // Blocking synchronization
throw / catch                         // Exception handling
std::cout / printf / logging          // I/O operations
```

See [REALTIME-SAFETY.md](REALTIME-SAFETY.md) for safe alternatives.

### ODR Prevention (CRITICAL)

Before creating ANY new class/struct, search codebase:
```bash
grep -r "class ClassName" dsp/ plugins/
```

Two classes with same name in same namespace = undefined behavior (garbage values, mysterious test failures). Check `dsp_utils.h` and ARCHITECTURE.md before adding components.

---

## Performance Budgets

| Component | CPU Target | Memory |
|-----------|------------|--------|
| Layer 1 primitive | < 0.1% | Minimal |
| Layer 2 processor | < 0.5% | Pre-allocated |
| Layer 3 system | < 1% | Fixed buffers |
| Full plugin | < 5% | 10s @ 192kHz max |

---

## DSP Implementation Quick Reference

| Use Case | Interpolation |
|----------|---------------|
| Fixed delay in feedback | Allpass |
| LFO-modulated delay | Linear/Cubic (NOT allpass) |
| Pitch shifting | Lagrange/Sinc |

| Technique | When to Apply |
|-----------|---------------|
| Oversampling | Nonlinear processing (saturation, waveshaping) |
| DC Blocking | After asymmetric saturation (~5-20Hz highpass) |
| Feedback Safety | When feedback > 100% (soft-limit with `std::tanh()`) |

See [IMPLEMENTATION.md](IMPLEMENTATION.md) for detailed guidance.

---

## File Organization

```
dsp/                              # Shared KrateDSP library (Krate::DSP namespace)
├── include/krate/dsp/
│   ├── core/                     # Layer 0 - math, conversions, utilities
│   ├── primitives/               # Layer 1 - delay lines, oscillators, envelopes
│   ├── processors/               # Layer 2 - filters, saturators, diffusers
│   ├── systems/                  # Layer 3 - composed processors, mod systems
│   └── effects/                  # Layer 4 - complete delay modes
└── tests/                        # DSP unit tests (mirrors structure above)
```

**Include patterns:**
```cpp
#include <krate/dsp/core/math_utils.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/processors/biquad_filter.h>
```
