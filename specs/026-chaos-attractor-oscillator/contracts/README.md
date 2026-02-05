# API Contracts: Chaos Attractor Oscillator

**Feature**: 026-chaos-attractor-oscillator
**Date**: 2026-02-05

## Status

**N/A** - This is an internal DSP component with no external API contracts.

## Rationale

The ChaosOscillator is a C++ class intended for internal use within the KrateDSP library. It does not expose:
- REST/HTTP endpoints
- GraphQL schemas
- Protocol buffers
- External file formats

## Public Interface

The public C++ API is defined in:
- Specification: `spec.md` (FR-015 to FR-023)
- Data model: `data-model.md`
- Quickstart: `quickstart.md`

The implementation header will be at:
- `dsp/include/krate/dsp/processors/chaos_oscillator.h`
