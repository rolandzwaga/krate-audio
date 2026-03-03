# Innexus

**A harmonic analysis & synthesis engine for VST3**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)]()

---

## Overview

Innexus is an analysis-driven synthesizer whose oscillator is derived from the harmonic structure of another sound source. It analyzes incoming audio (loaded sample or live sidechain), extracts harmonic information, and uses that data to drive synthesis rather than merely resynthesize audio.

This is **analysis-driven synthesis** in the tradition of Risset [1966] and Serra & Smith [1990]. The source acts as a spectral DNA provider — the oscillator becomes a function of another sound.

## Architecture

```
External Sound → Harmonic Analysis → Harmonic Model → Oscillator Bank → Musical Control → Output
```

## Features (Planned)

- **YIN F0 Tracking** — Real-time fundamental frequency estimation with confidence gating
- **Partial Tracking** — Multi-resolution STFT with harmonic sieve and frame-to-frame matching
- **Gordon-Smith MCF Oscillator Bank** — 48 amplitude-stable oscillators per voice
- **Dual Input Modes** — Sample analysis and live sidechain
- **Residual Model** — Deterministic + stochastic (SMS) decomposition
- **Harmonic Memory** — Snapshot capture, recall, and morphing
- **Musical Control** — Freeze, morph, harmonic filtering, evolution engine
- **Cross-Platform** — Windows, macOS (Intel & Apple Silicon), Linux

## Status

Early development. See `specs/INNEXUS-ROADMAP.md` for the full implementation plan.
