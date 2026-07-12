---
name: testing-dsp-analysis
description: Deep DSP-verification techniques — FFT-based aliasing measurement, THD/SNR and harmonic analysis, click/LPC/spectral-anomaly artifact detection, frequency estimation, and deterministic test-signal generation. Use ONLY when verifying the numerical/spectral correctness of a DSP algorithm (waveshaper aliasing, oscillator anti-aliasing, saturation THD, click/discontinuity hunting, golden spectral references). For general test writing, Catch2 patterns, build-before-test, integration, and VST3 validation, use the broader testing-guide skill instead.
allowed-tools: Read, Grep, Glob, Bash
---

# DSP Analysis Testing

The narrow, deep companion to `testing-guide`. `testing-guide` fires broadly for *any* test work;
this skill fires only when you are **measuring DSP correctness in the frequency/statistical domain** —
the work that needs an FFT, a THD figure, or an artifact detector rather than a `REQUIRE`.

Reach for this when the question is one of:
- *Does this waveshaper/oscillator alias?* → FFT the output, measure energy above Nyquist-image bands.
- *How much harmonic distortion does this saturator add?* → THD / SNR measurement.
- *Did this change introduce clicks or discontinuities?* → click/LPC/spectral-anomaly detectors.
- *Is the spectral output still golden?* → reference spectra + A/B comparison.
- *What's the true fundamental of this output?* → frequency estimation.

## Contents

- [DSP-TESTING.md](DSP-TESTING.md) — test signals, THD measurement, frequency estimation, deterministic RNG
- [SPECTRAL-ANALYSIS.md](SPECTRAL-ANALYSIS.md) — FFT-based aliasing measurement for waveshaper and ADAA tests
- [ARTIFACT-DETECTION.md](ARTIFACT-DETECTION.md) — click detection, LPC analysis, spectral flatness, signal-quality metrics, parameter-sweep testing

## Helpers these techniques use

In `tests/test_helpers/`:
- `spectral_analysis.h` — FFT-based aliasing measurement
- `signal_metrics.h` — SNR, THD, crest factor, kurtosis, ZCR
- `artifact_detection.h` — ClickDetector, LPCDetector, SpectralAnomalyDetector
- `statistical_utils.h` — mean, stddev, median, MAD
- `golden_reference.h` — reference comparison, A/B testing
- `parameter_sweep.h` — automated parameter-range testing

## Non-negotiable

- **Assert the timbre/spectrum you expect, not merely that output changed.** A "the signal is
  different now" check passes even when the change is a regression. Name the band, the centroid, the
  THD threshold. (See `testing-guide/ANTI-PATTERNS.md`.)
- **Never `REQUIRE`/`INFO` inside a sample loop** — collect the metric across the buffer, assert once.
- **Cross-platform tolerances:** MSVC/Clang diverge at the 7th–8th decimal — use `Approx().margin()`,
  never exact equality, on spectral magnitudes.
