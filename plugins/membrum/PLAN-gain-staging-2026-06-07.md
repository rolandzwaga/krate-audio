# Membrum Holistic Gain-Staging Architecture — Design Plan

> Companion to `AUDIT-signal-path-2026-06-07.md`. Resolves the coupled cluster
> H-1, H-2/C-2, H-4, M-2, M-3 + the missing main-bus limiter as ONE gain
> structure. Code facts verified against source; design choices verified against
> the modal-synthesis / mastering literature (sources at end). 2026-06-07.

---

## 0. Verified code facts (the starting state)

- **Per-voice output is `softClip(shaped * env * level_)` on all three paths**: fast per-sample `drum_voice.h:446`, block fast lane `:769`, block UN lane `:782`, slow path `:885`. `softClip` is the rational tanh in `dsp_utils.h:105-111` (clamped ±1; y(1)=0.778 = −2.1 dB; y(3)=1.0).
- **Body norm = 1/√N**, set at `drum_voice.h:248`; `bodyGainCompensation_` / `secondaryGainCompensation_` hard-set to 1.0 at `:251-252` and only multiply when `!= 1.0f` (`:676-681`, `:701-706`) — dead code. 48-mode membrane: 1/√48 = 0.144.
- **`getInputGainSum()` exists and is correct**: accumulates `Σ|gain_k|` (per-mode amplitude) over active modes (`modal_resonator_bank.h:715,722`) — exactly the coherent in-phase peak bound. Currently **never called**.
- **`applyOutputStage` (`modal_resonator_bank.h:750-757`)**: `modeSum *= outputGain_; if (threshold>0) softClip`. Membrum sets threshold=0 (`drum_voice.h:249`) → bank is linear; the only clip is the voice-level `softClip`.
- **Layer amplitudes**: noise returns `mix*env*f` (`noise_layer.h:147`) × `kStandaloneOutputGain=3.0` (`:218`); click returns `mix*vel*env*f` (`click_layer.h:132`) × `2.0` (`:153`). Click added **twice**: half into excitation (`drum_voice.h:425`) + full into mix (`:435`).
- **Default kit mixes (`default_kit.h`)**: noise 0.50–0.95, click 0.40–0.75 → worst-case layer peaks noise 0.95×3.0=**2.85**, click 0.75×2.0=**1.5**, vs body ≈ Σ|a|/√N. This is the ~17 dB body deficit.
- **VoicePool sum is pure additive, mono, no headroom scaling**: `outL[i]+=scratch[i]` (`voice_pool.cpp:354-355`, `:455-456`, aux `:474-475`). N voices sum linearly.
- **No main-bus limiter exists.** `applyEnergyLimiter` (`processor.h:165-178`, −20 dBFS one-pole) applies **only to the coupling return** (`processor.cpp:791`), not to `outL/outR`. Master gain (−24..+12 dB) is applied after (`:803-814`). The per-voice `softClip` is the only thing bounding the main bus — **remove it and the bus is unbounded**. That is why H-1 cannot be deleted; it must be replaced.

**Literature anchor:** Faust `physmodels.lib` uses `:> /(nModes)` = **1/N**; Smith/CCRMA uses per-mode fitted gains with **no** mode-count divisor; STK Modal applies **only linear gain** to the summed output (no tanh/clip). None put a waveshaper on the bank output — confirming the per-voice `softClip` is non-idiomatic.

---

## 1. Unity reference & gain budget

**Assumption (stated):** EBU-style headroom — unit-velocity single hit peaks well below 0 dBFS. Target **−12 dBFS** single-voice peak.

| Node | Target (unit velocity, 1 voice) | Rationale |
|---|---|---|
| Modal body peak (post-norm) | **−12 dBFS (0.25)** | dominant; carries Material/Size/StrikePos/damping |
| Noise layer peak (mix 1.0) | **−18 dBFS (0.125)**, ~6 dB under body | transient accent |
| Click layer peak (mix 1.0) | **−18 dBFS (0.125)** | transient accent |
| Combined voice peak (typical) | **−9 to −10 dBFS** | body + accents |
| Per-voice ceiling (safety) | **0 dBFS hardClip** | catches pathological voices only; never on musical hits |
| Main bus, N voices | unbounded pre-limiter | sum is linear |
| Master limiter ceiling | **−1.0 dBTP** | EBU R128 / streaming convention |
| Master gain | −24..+12 dB | applied **after** limiter |

Body leads layers by ~6 dB (resolves the masking half of H-2). N-voice headroom is **not** guaranteed pre-limiter (no 1/N in pool — it would crush single hits); the brickwall limiter at −1 dBTP guarantees the ceiling.

---

## 2. Normalization choice

**Decision: replace `1/√N` with `1/getInputGainSum()` (unit-peak `1/Σ|a_k|`), then a fixed make-up to hit −12 dBFS.**

- A strike excites all modes **in phase** → attack peak is the **coherent sum ≈ Σ|a_k|**, not incoherent `√(Σa_k²)`. The code comment (`drum_voice.h:235-246`) describes `1/√N` as steady-state RMS — right description, wrong regime for a transient.
- `1/√N` is **amplitude-blind** (count-only) → normalizes away per-preset amplitude-profile differences (membrane vs bell vs plate) → direct sameness contributor. `Σ|a_k|` is amplitude-aware; the difference survives.
- Plumbing exists (`inputGainSum_` computed at `setModes`, `:722`); just unwired.

```
outputGain = kBodyHeadroom / getInputGainSum()     // kBodyHeadroom = 0.25 (−12 dBFS)
```

Options compared at the body node: `1/√N` (count-only, amplitude-blind — reject); `1/N` Faust (more attenuated, still count-only — reject); `1/Σ|a|` Smith unit-peak (peak≈1.0, amplitude-aware — **accept**, then trim by `kBodyHeadroom`). Collapses the dead `bodyGainCompensation_`/`secondaryGainCompensation_` into `kBodyHeadroom`. Per-mode boosts (decaySkew/modeScatter) change `Σ|a_k|`, so relative tilt is preserved while peak stays unit-bounded — not self-cancelled.

---

## 3. Where saturation and limiting live

### 3a. Remove per-voice `softClip` as a gain stage (H-1)
Voice output becomes **linear**: `voiceOut = shaped * env` (level applied later, §4). Replace with a **per-voice safety hardClip ±1.0** that never engages on a musical hit (catches runaway feedback/NaN only). FeedbackExciter path keeps its documented passivity-loss clip (stability net, not gain stage — justified by Karjalainen); do not extend it to the linear path.

### 3b. Add the missing main-bus true-peak limiter (master-limiter-gap)
New code on the main bus in `processor.cpp`, after voice sum + coupling return, before master gain:
- **Type:** look-ahead true-peak brickwall (transparent, ratio ∞).
- **Ceiling:** **−1.0 dBTP** (0.891 linear) — EBU R128 / streaming convention.
- **True-peak detection:** **4× oversampled** peak detection on the look-ahead buffer only (not the whole path); keeps inter-sample error ~0.1 dB.
- **Look-ahead:** **1.0 ms** (~48 samples @ 48 kHz) — transient-preserving sweet spot for percussion.
- **Attack:** instantaneous (look-ahead is the attack). **Release:** **50–100 ms** program-dependent (start 80 ms).
- **Channel linking:** **stereo-linked** (max|L|,|R|) — future-proofs pan; mono today.
- **RT-safety:** fixed buffers allocated in `prepare()`; zero allocation/locks/`pow`/`log` in `process()` (precompute coeffs).

The existing `applyEnergyLimiter` stays on the coupling return — different node, different job. Add the new limiter on `outL/outR`.

### 3c. Musical saturation → explicit DRIVE, not a hidden curve
No always-on saturation (per STK/Faust/Bilbao). Only intentional saturation is the user-facing ToneShaper **Drive** (§5) — makeup-compensated, off at 0.

---

## 4. Where per-pad Level and master gain sit (H-4)

```
[VOICE]   shaped * env                              (linear, §3a)
        × level_                                    ← per-pad Level, OUTSIDE saturation
        → hardClip(±1.0)                            ← per-voice safety only
[POOL]    Σ voices (linear, additive)               (unchanged)
[BUS]     + coupling return (applyEnergyLimiter)    (unchanged)
        → TruePeakLimiter(−1 dBTP)                  ← NEW, §3b
        × masterGain (−24..+12 dB)                  (moved after limiter)
        → out
```

`level_` now multiplies a **linear** voice before the safety clip → a Level cut is true attenuation (resolves H-4; the "0.10 still clips" symptom disappears). Master gain stays last. **Trade-off (stated):** master gain after the limiter means a +12 dB boost can re-exceed −1 dBTP — acceptable/conventional (master gain is an output trim; the limiter protects the mix). Add a DAC-boundary hardClip later only if hard output protection is required.

---

## 5. ToneShaper Drive (M-2) & NonlinearCoupling (M-3) folded into the budget

- **M-2 Drive** (`tone_shaper.h:198-203,387-394`): maps `[0,1]→[1,10]` into `recipSqrt`, no makeup → ~9× small-signal boost = compressor. **Fix:** add post-shaper makeup ≈ 1/slope-at-zero so level is preserved and only shape changes; dry/wet blend then level-matched; exact dry at amount 0 (`:389`).
- **M-3 NonlinearCoupling** (`nonlinear_coupling.h:92-97`): any `amount!=0` runs the whole sum through `recipSqrt` (~18% cut at 0.7 even when AM≈0). **Fix:** saturate only the **AM-added component** (`bodyOut + recipSqrt(modulated − bodyOut)`) or threshold-gate so steady-state pass-through is unchanged; preserve `amount==0` exact bypass (`:71-72`) and continuity as amount→0.

Both become **level-neutral in their unity region** — timbre, not gain — so they don't fight the limiter or the body/layer budget.

---

## 6. Finding → design element → why it stops fighting

| Finding | Resolved by | Why it no longer fights |
|---|---|---|
| **H-1** softClip = de-facto compressor | §3a linear voice + §3b bus limiter | Velocity dynamics restored; the *bus* limiter bounds the *sum*, not each hit. |
| **H-2/C-2** body 17 dB under layers; dead make-up; `1/√N` | §2 `1/Σ|a_k|`+`kBodyHeadroom`; §1 layer budget | H-2 direction (raise body) is now safe **because** H-1 is gone — raising body no longer worsens clipping. |
| **H-4** Level inside softClip | §4 `level_` × linear voice, pre-safety-clip | Real attenuation; no saturating stage to slide along. |
| **M-2** Drive 9× makeup | §5 unity-region makeup | Level-neutral → no hidden gain into budget/limiter. |
| **M-3** coupling recipSqrt over whole signal | §5 saturate AM-only; continuous bypass | No level theft; `amount==0` bit-identical. |
| **master-limiter-gap** | §3b true-peak brickwall, RT-safe | The single guarantee that N-voice sum ≤ ceiling; lets the voice be linear safely. |

**Keystone: §3a (linear voice).** It makes the H-2 normalization direction unambiguous and *requires* §3b (the bus limiter) to exist.

---

## 7. Test-first implementation sequence (strict TDD, build-before-test, zero warnings)

**Re-baselining note:** this changes the operating point — approval/golden tests pinning absolute levels (the 0.5012 pins, clip-count assertions, `diagnose_orch`) **will fail and must be re-baselined** against new targets. Re-baseline per-step, after the change that sets each level.

1. **Add the bus true-peak limiter (BEFORE removing softClip).** Test: +6 dBFS burst → output ≤ −1 dBTP (4× oversampled); −20 dBFS steady passes ±0.01 dB; zero heap alloc in `process()`. *First, so the bus is never unbounded.*
2. **Linear voice + per-voice safety clip; move `level_` (H-1, H-4).** Test: unit-velocity kick peaks −12 ±1 dBFS; **vel 0.5 peaks ~6 dB lower** (no longer pinned to 0.5012); `level=0.1` attenuates ~−18 dB vs `level=0.8`. Implement all four sites together + fast/slow equivalence test. Re-baseline level goldens here.
3. **Body norm `1/Σ|a_k|` + `kBodyHeadroom`; delete dead make-up (C-2).** Test: body-only unit-velocity peak −12 ±1 dBFS for **every** body type; `bodyGainCompensation_` removed.
4. **Layer re-balance (H-2).** Test: noise-only (mix 1.0) peak −18 ±1.5 dBFS (≥6 dB under body); same for click. Re-derive `kStandaloneOutputGain` constants. (Preset re-tuning is out of scope — Phase 4 of the audit; here only calibration constants.)
5. **ToneShaper Drive makeup (M-2).** Test: −12 dBFS sine in → output within ±0.5 dB at any Drive amount (level-neutral); THD rises with amount; amount 0 bit-identical.
6. **NonlinearCoupling AM-only (M-3).** Test: `amount==0` bit-identical (FR-055); steady input level change <0.5 dB across amount sweep; transient AM still audible.

**Must land before preset re-tuning:** Steps 1–4 (full gain structure + normalization + layer balance). Steps 5–6 before re-voicing any preset using Drive/Coupling.

---

## 8. Risks / equivalence concerns

- **Fast vs slow equivalence:** softClip removal + `level_` move touch `:446`, `:769`, `:782`, `:885` — must stay sample-identical. Add explicit fast-vs-slow equivalence test (closes audit L-9). Block fast-lane vs UN-lane must match when UN off.
- **Default-off bit-identity (FR-055):** UnnaturalZone `amount==0` bit-identical; M-3 fix continuous as amount→0; Drive bypass exact at 0. Justify any deviation in the commit.
- **RT-safety / pluginval:** limiter buffers in `prepare()` only; no alloc/lock/`pow`/`log` in `process()`. Run pluginval + RT-safety harness after Step 1.
- **Limiter latency:** 1.0 ms look-ahead = plugin latency → report via `setLatencySamples` for host PDC; test reported latency == look-ahead samples. (New behavior — main path has no latency today.)
- **Re-baselining surface:** enumerate every absolute-level approval/golden test; re-baseline per-step so each is traceable.
- **Polyphony headroom intentionally not pre-limiter:** document so nobody "fixes" it with 1/N (which re-crushes single-hit dynamics).
- **Master-gain-after-limiter** can re-exceed −1 dBTP at high settings (§4 trade-off) — accepted.

---

## Critical files

- `plugins/membrum/src/dsp/drum_voice.h` — remove softClip / safety clip / move `level_` (446, 769, 782, 885); body norm via `getInputGainSum()`+`kBodyHeadroom`, delete dead make-up (248–252, 676–681, 701–706); layer gains (423, 434).
- `plugins/membrum/src/processor/processor.cpp` — add true-peak bus limiter on `outL/outR` before master gain (~795–815); move master-gain block after it.
- `plugins/membrum/src/processor/processor.h` — limiter member/state, `prepare()` allocation, latency reporting (near `applyEnergyLimiter` 165–178).
- `dsp/include/krate/dsp/processors/modal_resonator_bank.h` — consume `getInputGainSum()` (163–166), `setOutputGain()` (135); no clip on non-feedback path (745–757).
- `plugins/membrum/src/dsp/tone_shaper.h` (M-2, 198–203, 383–394); `plugins/membrum/src/dsp/unnatural/nonlinear_coupling.h` (M-3, 70–98).

---

## Sources

- Faust `physmodels.lib` — `:> /(nModes)` (1/N): https://faustlibraries.grame.fr/libs/physmodels/
- J.O. Smith, *Physical Audio Signal Processing* — Modal Expansion (per-mode fitted gains, no mode-count divisor, no output saturation): https://ccrma.stanford.edu/~jos/pasp/Modal_Expansion.html
- STK `Modal` — linear gain only on summed output, no waveshaper: https://github.com/thestk/stk ; https://ccrma.stanford.edu/software/stk/papers/stkicmc99.pdf
- True-peak limiting, 4× oversample + look-ahead: https://www.fabfilter.com/help/pro-l/using/truepeaklimiting ; EBU R128 −1 dBTP convention.
- Limiter placement / transient preservation: https://www.izotope.com/en/learn/an-introduction-to-limiters-and-how-to-use-them.html
- Modal synthesis overview (body leads excitation): https://nathan.ho.name/posts/exploring-modal-synthesis/ ; https://ccrma.stanford.edu/~jos/jnmr/Body_Resonators.html
