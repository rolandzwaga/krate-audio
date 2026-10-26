# Core-alone probe behind the 2026-10-13 bank ruling (FR-070, SC-028)

`growl-probe.cpp` renders `ProfundumCore` alone (no Phase 4 component) through a Drop-shaped `f0PerSample` trajectory
(exponential family, k = 8, from +A semitones to the note over T) and measures, after the landing, the aliased power (the Phase 1
SC-009 estimator) and every partial's amplitude against its value before the Drop. Build as `drift-probe` (Phase 3 evidence), run
`growl-probe <fs> <block> <drop|hold|jump> <midi> <T ms> <A st> <growl|saw|mid>`.

**1. The T026 SC-015 readings are not the core's.** Core alone, the `bridge-midfall-up5-A48-T1` cell's trajectory at kGrowl: aliased
−135.8 dBFS (E1, 44.1 kHz, blocks 1 and 512), −125.3 (E1, 48 kHz), −134.0 (C3, 44.1 kHz), −127.8 (C3, 48 kHz), −134.7 / −127.5 (E2);
hold −135.9; a 1 ms +48 st jump −125.2. The test harness read −29.98 dBFS on the same cell at E1 / 44.1 kHz with kGrowl and −130 with
kSawAnchor: the inharmonic energy enters through the composition, not the bank (Defects found, T023).

**2. The bank re-rolled partial amplitudes after every large pitch motion** (before the fix; `harmonic_oscillator_bank.h`
applyExternalFrequencyMultipliers kept (s, c) across the coefficient change):

| core alone, 48 kHz, 512-sample blocks | harmonic energy after − before | worst partial |
|---|---|---|
| C3 kGrowl, A 48 st, T 60 ms | +4.00 dB | h18 +10.7 dB |
| C3 kGrowl, A 48, T 1 ms / 10 / 60 / 200 / 1000 ms | +6.3 / +5.9 / +4.1 / +4.1 / +4.1 dB | — |
| C3 kGrowl, A 24 / 12 / 5 st, T 1 s | +2.1 / +0.3 / 0.0 dB | h45 +6.5 dB (A 24) |
| E1 kGrowl, A 48, T 60 ms | +4.24 dB | h57 +10.9 dB |
| C3 44.1 kHz kGrowl, A 48, T 60 ms | +4.12 dB | h9 +9.5 dB |
| C3 kSawAnchor, A 48, T 60 ms | +0.40 dB (quiet high partials) | h18 +10.7 dB (same per-partial pattern) |

Per-partial changes at C3 kGrowl A 48 T 60 ms: 1:−0.5 2:+1.2 3:−1.2 4:+0.3 5:+1.8 6:−4.8 7:+3.4 8:+1.9 9:+0.9 10:+4.8 11:+3.0 12:+7.0
13:+7.5 14:−4.9 15:+3.9 16:+6.0 18:+10.7 … 64:+5.1 dB — a re-roll, not a level shift.

**Mechanism.** MCF with coefficient eps = 2 sin(w/2): s[n] = A sin(psi), c[n] = A cos(psi + w/2). Keeping (s, c) while eps changes
changes A by up to 1/sqrt(1 − eps²/4) (≈ 3× at the cap, where frozen partials sit at eps ≈ 1.9) and shifts psi. **Fix (opt-in,
`setContinuousFrequencyUpdates`):** at each stereo render the bank compares each active partial's effective coefficient with the
one its state last ran at and rewrites c so A and psi are exact: A cos psi = (c + s·eps1/2)/sqrt(1 − eps1²/4);
c' = A cos psi · sqrt(1 − eps2²/4) − s·eps2/2. Default off: Innexus / Membrum / Seraphis / Vorago bit-identical.

**After the fix:** `HarmonicOscillatorBank_ContinuousFrequencyUpdates` worst partial −0.0001 dB over a ±53 st staircase (control,
default path: −5.82 dB); `ProfundumCore_SpectrumPreservedAcrossDrop` (SC-028) 162 cells, harmonic energy within 0.0007 dB, worst
partial −0.246 dB on a partial 50 dB below h1 (bar 0.1 dB within 40 dB of h1, 0.5 dB below).
