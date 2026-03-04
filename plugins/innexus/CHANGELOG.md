# Changelog

All notable changes to Innexus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.0] - 2026-03-04

### Added

- **M3: Live Sidechain Mode** — Real-time continuous analysis from sidechain audio input, enabling performers to route live audio and play MIDI keys that inherit the live source's timbral character
- **Sidechain audio input bus** — Stereo auxiliary input (kAux), automatically downmixed to mono for analysis
- **Input Source parameter** — Switch between Sample and Sidechain modes with 20ms click-free crossfade
- **Latency Mode parameter** — Low Latency (11.6ms, short STFT only) or High Precision (dual-window STFT, detects fundamentals down to 40 Hz)
- **LiveAnalysisPipeline** — Real-time orchestration of PreProcessing, YIN, STFT, PartialTracker, HarmonicModelBuilder, and SpectralCoringEstimator with zero audio-thread allocations
- **SpectralCoringEstimator** (KrateDSP Layer 2) — Lightweight residual estimation by zeroing harmonic bins and measuring inter-harmonic energy, adding zero additional analysis latency
- **Confidence-gated freeze in sidechain mode** — Reuses existing freeze mechanism; activates within 2 STFT hops of silence, recovers with crossfade when signal returns
- **Residual synthesis in sidechain mode** — Existing controls (Harmonic Level, Residual Level, Brightness, Transient Emphasis) work identically in both sample and sidechain modes
- **State version 3** — Persists Input Source and Latency Mode parameters with backward compatibility for v1/v2 presets
- **Early-out optimizations** — Silent sidechain skip (~95% CPU reduction when idle), residual skip when Residual Level is zero (~10% reduction)

### Performance

- Analysis pipeline: < 5% single-core CPU at 44.1 kHz (SC-003)
- Analysis + synthesis: < 8% single-core CPU at 44.1 kHz (SC-004)
- Analysis latency: 11.6ms hop + < 1ms processing overhead (SC-001)
- Source crossfade: < -60 dB discontinuity (SC-005)

## [0.2.0] - 2026-03-04

### Added

- **M1: Core Playable Instrument** — Complete analysis-synthesis pipeline for harmonic resynthesis of audio samples, playable via MIDI
- **Pre-processing pipeline** — DC offset removal (IIR estimator with first-sample init), 4th-order Butterworth HPF at 30 Hz, noise gate (-60 dB default), transient suppression (10:1 ratio)
- **YIN pitch detector** — FFT-accelerated autocorrelation (O(N log N)), CMNDF with parabolic interpolation, confidence gating (0.3 threshold), 2% frequency hysteresis, hold-previous on low confidence
- **Dual-window STFT analysis** — Long window (4096/hop 2048) for frequency resolution, short window (1024/hop 512) for temporal resolution, both BlackmanHarris
- **Partial tracker** — Spectral peak detection with parabolic interpolation, harmonic sieve (sqrt(n) tolerance), frame-to-frame matching by frequency proximity, birth/death with 4-frame grace period, 48-partial hard cap ranked by energy * stability
- **Harmonic model builder** — L2 amplitude normalization, dual-timescale blending (5ms fast / 100ms slow), 5-frame median filter for impulse rejection, spectral centroid/brightness/noisiness descriptors, global amplitude tracking (10ms smoother)
- **48-oscillator harmonic oscillator bank** — Gordon-Smith MCF (Modified Coupled Form), SoA layout with 32-byte alignment, anti-aliasing soft rolloff near Nyquist, phase-continuous frequency updates, 3ms crossfade on pitch jumps > 1 semitone, per-partial amplitude smoothing (~2ms), configurable inharmonicity (0-100%)
- **Sample loading** — WAV/AIFF support via dr_wav, stereo-to-mono downmix, background analysis thread with atomic pointer swap (lock-free, zero audio-thread allocations)
- **MIDI integration** — Note-on/off with monophonic last-note-priority, velocity scaling (global, not per-partial), pitch bend (+/-12 semitones), exponential release envelope with 20ms anti-click minimum, confidence-gated freeze with 7ms recovery crossfade
- **State persistence** — Versioned binary state saves/restores all parameters and sample file path, triggers re-analysis on session reload
- **Plugin parameters** — Release Time (20-5000ms, exponential mapping), Inharmonicity Amount (0-100%)

### Performance

- Oscillator bank: 0.28% CPU at 44.1 kHz stereo, 48 partials (target: < 0.5%)
- Full plugin: < 5% CPU at 44.1 kHz stereo
- Analysis: ~250ms for 10s mono file (target: < 10s)
- YIN pitch error: 0.0% gross error rate (target: < 2%)

## [0.1.0] - 2026-03-03

### Added

- Initial plugin skeleton — VST3 entry point, processor, controller
