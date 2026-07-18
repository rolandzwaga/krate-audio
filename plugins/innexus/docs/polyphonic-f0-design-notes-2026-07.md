# Innexus polyphonic F0 — investigation record and design input

**Status:** pre-spec design input. Nothing here has been implemented.
**Date:** 2026-07-18
**Origin:** traced from a failing `[.real]` test while closing out the
2026-07 Innexus audit (`audit-2026-07-implementation-plan.md`).

This document exists so the eventual spec starts from measurement rather than
from a fresh round of guessing. Everything in §2–§4 was measured on this
machine and each number states how it was obtained. §6 records hypotheses that
were tested and **refuted** — they look plausible and will be re-proposed by
anyone who does not know they were already tried.

---

## 1. The presenting symptom

`plugins/innexus/tests/integration/sample_load_e2e_tests.cpp:687`

```cpp
// F0 must be a real musical pitch, not a false subharmonic.
// Guitar fundamentals are typically 80-1200 Hz.  A subharmonic at 56 Hz
// would indicate the analyzer fell into the YIN subharmonic trap.
REQUIRE(midFrame.f0 > 80.0f);   // FAILS: 55.3714
```

Test: `E2E: Polyphonic sample produces pitched resynthesis`, tagged `[.real]`.
Sample: `C:/test/633815__argenisflores__guitar-note-sustained.wav`.

This test has never been enforced anywhere. `[.real]` is a Catch2 hidden tag, and
the sample path is an absolute local path that does not exist on CI runners, so
the test early-returns there. It is a local-only diagnostic.

**The assertion's stated reasoning is wrong for this file**, but the failure is
still pointing at a real defect — just not the one the comment names. Do not
"fix" this by relaxing the threshold.

---

## 2. What the sample actually is

Confirmed by the user by ear: *"it's a guitar chord that does indeed have a very
prominent main note."*

Confirmed independently from the spectrum. Two **independent** harmonic series
are present:

| Series | Freq | Note | Evidence |
|---|---|---|---|
| Low root | ~53.8 Hz | A1 | own series in a 4096 FFT: h1 = 75.1, h2 = 54.7, h3 = 38.1 |
| Prominent | ~139.35 Hz | C#3 | fits the 9 strongest tracked partials with max integer deviation **0.136** |

Fit test of the strongest frame's partials (428.9, 560.6, 713.4, 975.4, 1125.2,
1244.1, 1374.5, 1543.9, 1811.6 Hz) against candidate fundamentals — max
deviation of `partial / f0` from an integer:

| candidate f0 | max deviation |
|---|---|
| **139.35 Hz** | **0.136** |
| 69.4 Hz | 0.280 |
| 110.7 Hz | 0.444 |
| 55.37 Hz | 0.469 |

`53.8 / 139.35 = 0.39` — not an integer ratio in either direction, so these are
two genuinely separate pitches, not one fundamental and its overtones. A + C# is
a major third. A1 at ~55 Hz is *below* standard guitar low E (82.4 Hz), which is
why the test's "guitar fundamentals are 80–1200 Hz" premise does not hold here.

### 2.1 The two tones cross over in prominence

Per-series harmonic energy (4096 FFT, positions shared between the two series
excluded so the comparison is fair):

| t (s) | A1 (53.8 Hz) | C#3 (139.35 Hz) |
|---|---|---|
| 0.28 | 1,438 | **24,369** (17×) |
| 0.37 | 16,659 | **38,226** |
| 0.65 | 23,614 | **42,749** |
| 1.02 | **14,851** | 14,026 ← crossover |
| 1.67 | **3,771** | 2,328 |
| 2.41 | **890** | 562 |

C#3 dominates the attack — that is the "prominent main note" the ear hears — and
decays faster (higher partials decay faster, as expected physically). A1 rings on
and dominates the tail.

**Consequence:** the frame the test inspects (`frames.size()/2`, index 207 of 414,
t ≈ 2.4 s, amplitude 0.0379 = **13% of peak**) sits well past the crossover.
Reporting ~55 Hz *there* is defensible. The test's midpoint-frame methodology is
weak, but fixing only that does not make the test pass: the strongest frame
(index 53, amplitude 0.290) reports **69.4 Hz**, which also fails `> 80`.

---

## 3. Why the analyzer produces 55 Hz

### 3.1 YIN is unstable on this material at every window size

YIN run directly on the raw audio over the sustained region (t = 0.2–1.5 s),
bypassing the sieve/tracker/validator chain entirely:

| window | min | median | max |
|---|---|---|---|
| 2048 | 46.2 | 54.7 | 139.3 |
| 4096 (analyzer's) | 46.3 | 55.1 | 139.3 |
| 8192 | 54.8 | 69.4 | 139.2 |

Early in the note (t = 0.05–0.25 s) YIN reports **139.349 Hz at confidence 0.81** —
correct. It then drifts to 69.4 (= 139.35/2) and ~55 as C#3 decays and A1 takes
over. **Window size is not the lever**; this is not a resolution problem.

### 3.2 The polyphonic re-analysis path never fires

`sample_analyzer.cpp` decides a sample needs a second, sieve-free pass when the
F0 is unstable **or** noisiness is high. Instrumented on this sample:

```
nVoiced=414  medianF0=55.37  q1=54.66  q3=55.53  iqr=0.016
unstable=0   noisiness=0.340  tooNoisy=0  ->  reanalysis=0
```

- `iqr = 0.016` vs `kInstabilityThreshold = 0.1` — F0 is *rock stable* on the low
  root, so the instability check cannot see it. The analyzer is confidently
  wrong, not jittery.
- `noisiness = 0.340` vs `kHighNosinessThreshold = 0.5` — under the gate.

So the harmonic sieve stays ON with F0 ≈ 55 Hz, and the C#3 series (at 2.59× the
assumed fundamental) is forced onto a grid it does not fit.

**This is the actual defect: a genuinely polyphonic sample is not detected as
polyphonic.**

Note this is adjacent to but distinct from WI-24 (committed `de1c4db4`), which
fixed the *opposite* error — noisiness firing when it should not, on low
monophonic material. Here the gate fails to fire when it should.

---

## 4. A measured polyphony signal already exists in the codebase

`MultiPitchDetector` is already constructed in `sample_analyzer.cpp`, but only
consulted *after* the decision to re-analyze has been made. Measured as a
detector instead — fraction of frames reporting ≥ 2 pitches, identical method
across three files:

| sample | frames | ≥2 pitches | rate |
|---|---|---|---|
| guitar **chord** | 414 | 373 | **90.1%** |
| female vocal, single note (mono) | 127 | 14 | 11.0% |
| harmonica, single note (mono) | 179 | 1 | 0.6% |

**90.1% vs 0.6–11% is a clean separation.** The count is a usable polyphony gate.

### 4.1 …but its frequency estimates are not usable

The same runs return pitch values such as 1208, 1356, 1225, 1280, 1416, 1396 Hz —
upper partials, not fundamentals. Neither 53.8 nor 139.35 is ever returned.

Root cause is **QS-3** (documented in `465c75af`): the cancellation step
subtracts a flat 85% of each matched peak instead of Klapuri's spectral-smoothness
envelope, so salience ranking is distorted.

This is the same defect that produced the bogus **708 Hz** reference F0 seen
during the WI-24 investigation on a 41 Hz synthetic tone.

**Design constraint: gate on the count, never source `referenceF0` from this
detector until QS-3 is addressed.**

---

## 5. Design questions the spec must answer

1. **What should F0 be for a chord?** The lowest tone (current behaviour), the
   most prominent one, or should the sieve-off path make the question moot?
   Note the prominence *changes over the note* (§2.1) — any answer based on a
   single whole-file statistic will be wrong for part of the file.

2. **Where does `referenceF0` come from** once polyphony is detected, given
   `MultiPitchDetector`'s frequencies cannot be trusted? Candidates: energy-weighted
   median of per-frame YIN; F0 of the highest-energy frame (= 69.4 here, an octave
   error — so it needs validator help); attack-region F0 (= 139.3 here, correct,
   and matches the prominent note).

3. **Should QS-3 be fixed first?** It blocks question 2, and it independently
   affects any consumer of multi-pitch output. Sequencing it first may be cheaper
   than working around it twice.

4. **Does the polyphony decision have to be whole-file?** The crossover in §2.1
   suggests per-region handling may be more faithful, at the cost of complexity
   and of frames disagreeing about F0.

5. **What should the `[.real]` test assert?** Its current threshold encodes a
   false premise for this file. Once the intended behaviour is decided, the
   assertion should express *that*, with the sample documented as a chord
   containing A1 + C#3.

---

## 6. Hypotheses already tested and REFUTED

Recorded so they are not re-proposed. Each was measured, not reasoned away.

| # | Hypothesis | Prediction | Measured | Verdict |
|---|---|---|---|---|
| 1 | `SubharmonicValidator::kCorrectionThreshold = 1.4` is backwards and blocks the octave-up correction | lowering it fixes the pitch | true f0 **is** the top-scoring candidate (55.46 vs 43.70/43.88/45.46), but "correcting" 55→139 would swap one **real** chord tone for another | **refuted** — would have been tuning a constant against one file |
| 2 | Spectral floor inflates the subharmonic's score | floor subtraction restores the theoretical ~2× ratio | ratio 1.269 → **1.319**, still under 1.4 | refuted |
| 3 | The short (1024) window cannot resolve low harmonics, so score against the long window | long window separates the candidates | ratio **1.045** — *worse* than short | refuted |
| 4 | The strong 69.4 Hz bin is DC/window leakage | magnitude decays monotonically from DC | genuine local peak (bin 2 = 1.6, bin 5 = 75.1) | refuted — this is what revealed the second real series |
| 5 | The analyzer's YIN window (4096) is too small | a larger window recovers 139 Hz | median 54.7 / 55.1 / 69.4 at 2048 / 4096 / 8192 | refuted |

Supporting data for #1, `harmonicScore` at t = 0.51 s, 1024 FFT:

```
cand=55.37  -> 43.88     cand=138.81 -> 55.46   <- true f0, highest
cand=69.40  -> 43.70     cand=139.35 -> 55.43
cand=110.74 -> 45.46     cand=208.2  -> 24.96
                         cand=277.6  -> 30.43
ratio score(2*f0)/score(f0) = 1.269   (kCorrectionThreshold demands > 1.4)
```

Odd/even harmonic magnitudes of the 69.4 Hz candidate (4096 FFT) — the pattern
that proves the C#3 series is real (even positions strong, odd weak) *and* that
h=1 is a real independent peak rather than leakage:

```
h=1  ODD  69.4 Hz  mag=55.43   <- A1, a real independent partial
h=2  even 138.8    mag=24.86
h=3  ODD  208.2    mag= 7.65
h=4  even 277.6    mag=30.48
h=6  even 416.4    mag=44.39
h=12 even 832.8    mag=47.90
```

---

## 7. Relevant constants and code sites

| Item | Location | Value |
|---|---|---|
| polyphony gate: instability | `sample_analyzer.cpp` | `kInstabilityThreshold = 0.1` |
| polyphony gate: noisiness | `sample_analyzer.cpp` | `kHighNosinessThreshold = 0.5` |
| noisiness gate low-f0 cutoff (WI-24) | `sample_analyzer.cpp` | `dualWindowMaxF0` ≈ 172 Hz |
| octave correction margin | `subharmonic_validator.h` | `kCorrectionThreshold = 1.4` |
| octave candidate set | `subharmonic_validator.h` | `{f0/2, f0, f0*2, f0*4}` only (QS-4) |
| harmonics summed per candidate | `subharmonic_validator.h` | `kMaxHarmonics = 12` |
| multi-pitch cancellation | `multi_pitch_detector.h` | flat `kCancellationFraction = 0.85` (QS-3) |
| offline YIN window | `sample_analyzer.cpp` | `kHighPrecisionYinWindowSize` = 4096 (WI-15) |

Related audit items, all committed:

- **WI-24** (`de1c4db4`) — noisiness gate firing wrongly on low monophonic material.
- **WI-15** (`fa2538fb`) — offline YIN window raised to 4096.
- **WI-12** (`cb10e133`) — dual-window crossover raised to 4 short bins (~172 Hz).
- **QS-3 / QS-4** (`465c75af`) — multi-pitch cancellation and validator candidate
  set documented as diverging from their cited theory.

---

## 8. Validation plan for whatever is built

Any change here touches the **shared monophonic analysis path**, which is
load-bearing. Minimum bar:

1. **No monophonic regression.** The vocal (11.0%) and harmonica (0.6%) samples
   must not be re-classified as polyphonic. These two rates are the baseline any
   gate threshold must clear.
2. **Full suites green** — `innexus_tests` (567 cases at time of writing) plus the
   five `dsp_*_tests` layers, since `subharmonic_validator`, `multi_pitch_detector`
   and `partial_tracker` are shared DSP.
3. **`[.real]` suite** — currently 1 passed / 1 failed. The remaining failure is
   the test in §1. `test_real_sample_noise` was made honest in `2c786797`; keep it
   green so a real regression stays visible.
4. **pluginval** strictness 5 and **clang-tidy** at zero warnings for `innexus`.
5. Re-measure the §4 table after the change and record it in the commit, the same
   way the audit work did.

## 9. Reproducing the measurements

All figures came from temporary `[.probe]`-tagged Catch2 tests, since removed;
none are in the tree. To recreate them: load the WAV with `dr_wav` (see
`test_sidechain_wav_quality.cpp` for the pattern), run `STFT` +
`SpectralBuffer` at the FFT size named in each section, and call the component
under test directly. The instrumentation of the re-analysis decision (§3.2) was a
temporary `fprintf` in `sample_analyzer.cpp` at the `needsReanalysis` assignment.
