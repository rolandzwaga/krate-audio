# SC-003(e)(ii) drift attribution probe (T023 red, 2026-10-12)

`drift-probe.cpp` renders the SC-003(e) chain (ProfundumCore mid grid, excitation 1/1/1, 48 kHz, block 512) for T + 1 s and
measures, with the chain test's BH7 single-bin DFT over ceil(f₀) periods centred at 1 s and T:
(D) the core's h1 against an ideal phasor; (A) the core's partials h2…h6 relative to n·h1; (B) the Fold-100 output's h1
relative to the core's h1; (C) the same Fold fed a perfectly periodic double-synthesised copy of the core's 1 s spectrum.

Build (MSVC, the dynamic runtime the static libs use):

```
cl /nologo /std:c++20 /O2 /fp:fast /EHsc /MD /arch:AVX2 /I dsp\include drift-probe.cpp /Fe:drift-probe.exe ^
   /link build\windows-x64-release\lib\Release\KrateDSP.lib build\windows-x64-release\lib\Release\hwy.lib build\windows-x64-release\lib\Release\pffft.lib
drift-probe.exe <midi> <T seconds>
```

Output (2026-10-12; midi 48 / 60 s, 40 / 60 s, 16 / 60 s, 48 / 10 s):

```
midi 48 f0 130.8128 Hz, T 60 s, window 48069 samples
(D) core h1 vs ideal phasor: 1 s +125.5088 deg, 60 s +93.0206 deg, drift -32.4883 deg
(A) core h2 - 2*h1: 1 s +42.3112 deg, 60 s +42.3106 deg, drift -0.0006 deg
(A) core h3 - 3*h1: 1 s -164.1953 deg, 60 s -164.7217 deg, drift -0.5264 deg
(A) core h4 - 4*h1: 1 s -43.0383 deg, 60 s -43.5745 deg, drift -0.5362 deg
(A) core h5 - 5*h1: 1 s +97.3790 deg, 60 s +105.7316 deg, drift +8.3526 deg
(A) core h6 - 6*h1: 1 s -136.0919 deg, 60 s -138.5567 deg, drift -2.4647 deg
(B) Fold100(core) h1 - core h1: 1 s -3.0564 deg, 60 s -2.4936 deg, drift +0.5628 deg
(C) Fold100(periodic synth) h1 - synth h1: 1 s -3.1065 deg, 60 s -3.1065 deg, drift -0.0001 deg
rms dBFS: core -11.90/-11.90, synth -11.99/-11.99, folded -11.33/-11.34

midi 40 f0 82.4069 Hz, T 60 s, window 48345 samples
(D) core h1 vs ideal phasor: 1 s -166.3329 deg, 60 s -161.5017 deg, drift +4.8312 deg
(A) core h2 - 2*h1: 1 s +38.2521 deg, 60 s +38.1441 deg, drift -0.1081 deg
(A) core h3 - 3*h1: 1 s -158.1295 deg, 60 s -158.0834 deg, drift +0.0461 deg
(A) core h4 - 4*h1: 1 s -29.9644 deg, 60 s -29.8191 deg, drift +0.1453 deg
(A) core h5 - 5*h1: 1 s +118.0286 deg, 60 s +119.6313 deg, drift +1.6028 deg
(A) core h6 - 6*h1: 1 s -128.9085 deg, 60 s -128.4904 deg, drift +0.4181 deg
(B) Fold100(core) h1 - core h1: 1 s +3.0250 deg, 60 s +3.0516 deg, drift +0.0266 deg
(C) Fold100(periodic synth) h1 - synth h1: 1 s +2.8221 deg, 60 s +2.8220 deg, drift -0.0000 deg
rms dBFS: core -12.35/-12.35, synth -12.46/-12.46, folded -11.89/-11.90

midi 16 f0 20.6017 Hz, T 60 s, window 48928 samples
(D) core h1 vs ideal phasor: 1 s -23.0230 deg, 60 s -21.8341 deg, drift +1.1889 deg
(A) core h2 - 2*h1: 1 s +46.2638 deg, 60 s +46.3142 deg, drift +0.0504 deg
(A) core h3 - 3*h1: 1 s -168.6322 deg, 60 s -168.6154 deg, drift +0.0168 deg
(A) core h4 - 4*h1: 1 s -24.0523 deg, 60 s -24.0044 deg, drift +0.0479 deg
(A) core h5 - 5*h1: 1 s +73.9720 deg, 60 s +74.1085 deg, drift +0.1365 deg
(A) core h6 - 6*h1: 1 s +152.7287 deg, 60 s +152.8816 deg, drift +0.1530 deg
(B) Fold100(core) h1 - core h1: 1 s +7.6454 deg, 60 s +7.6432 deg, drift -0.0022 deg
(C) Fold100(periodic synth) h1 - synth h1: 1 s +6.1025 deg, 60 s +6.1026 deg, drift +0.0001 deg
rms dBFS: core -12.53/-12.53, synth -12.60/-12.60, folded -14.38/-14.45

midi 48 f0 130.8128 Hz, T 10 s, window 48069 samples
(D) core h1 vs ideal phasor: 1 s +125.5088 deg, 10 s -7.5837 deg, drift -133.0926 deg
(A) core h2 - 2*h1: 1 s +42.3112 deg, 10 s +42.3140 deg, drift +0.0028 deg
(A) core h3 - 3*h1: 1 s -164.1953 deg, 10 s -164.2724 deg, drift -0.0771 deg
(A) core h4 - 4*h1: 1 s -43.0383 deg, 10 s -43.1129 deg, drift -0.0747 deg
(A) core h5 - 5*h1: 1 s +97.3790 deg, 10 s +98.9586 deg, drift +1.5796 deg
(A) core h6 - 6*h1: 1 s -136.0919 deg, 10 s -136.4663 deg, drift -0.3744 deg
(B) Fold100(core) h1 - core h1: 1 s -3.0564 deg, 10 s -2.9750 deg, drift +0.0814 deg
(C) Fold100(periodic synth) h1 - synth h1: 1 s -3.1065 deg, 10 s -3.1064 deg, drift +0.0000 deg
rms dBFS: core -11.90/-11.90, synth -11.99/-11.99, folded -11.33/-11.33
```

Reading: (C) the Fold's own h1 phase on a stationary input does not move (≤ 0.0001°); (A) the core's partials walk relative to
h1 by up to 8.35° in 60 s at C3 (each partial's own frequency error is within Phase 1's 0.1 cent bar; h1's absolute walk at
C3 is 32.5° / 60 s = 0.02 cent); (B) the folded h1's phase follows those inter-partial phases through the nonlinearity:
+0.56° at C3 (the chain test's 0.59°), +0.03° at E2, −0.002° at E0. The Anchor is not in any of these measurements; its own
drift against the excited core's h1 is ≤ 0.03° (chain test, SC-003(e)(i)).
