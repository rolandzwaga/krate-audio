# Membrum Recipe — Shaker / Cabasa

**Archetype:** Shaker family (cabasa baseline; hand shaker / maraca variants)
**Body model:** `NoiseBody`  **Exciter:** `NoiseBurst`

## 1. The real instrument (cited)

A shaker/cabasa has **no pitch and no harmonic mode structure**. The sound is **dense broadband noise** from many independent micro‑collisions of beads/seeds:

- **Cabasa** — loops of steel ball‑chain scraping a serrated cylinder. "Crisp, sharp, metallic sizzle," brighter than a maraca because of the steel beads ([Cabasa, Wikipedia](https://en.wikipedia.org/wiki/Cabasa); [Shekere vs Cabasa](https://muzicalinstruments.com/percussions/shekere-vs-cabasa/)).
- **Maraca** — ~25 dried seeds in a gourd; measured frequency response **~3000–10000 Hz**, no low fundamental ([Maraca, Wikipedia](https://en.wikipedia.org/wiki/Maraca)).
- **Hand shaker** — beads in a tube; similar high‑band noise, slightly darker.

**Canonical synthesis = PhISEM** (Perry Cook, STK Shakers): the sound is a train of **short exponentially‑decaying white‑noise bursts through resonant bandpass "system‑resonance" filters**; object count sets collision density, the resonance center sets timbre ([STK Shakers](https://csound.com/docs/manual/STKShakers.html); [Cook NIME 2001](https://www.nime.org/proceedings/2001/nime2001_003.pdf)).

**PhISEM constants** ([Shakers.cpp](https://raw.githubusercontent.com/thestk/stk/master/src/Shakers.cpp)):

| Instrument | Objects | Center freq | Resonance radius | Sound decay |
|---|---|---|---|---|
| **Cabasa** | 512 (steel beads) | **3000 Hz** | **0.7 (broad)** | 0.96 (fast) |
| Maraca | 25 seeds | 3200 Hz | 0.96 (tight) | 0.95 |
| Sekere | 64 | 5500 Hz | 0.6 | 0.96 |

So cabasa = **dense, near‑continuous, broad** hiss centered ~3 kHz; maraca = fewer beans → **granular, tighter** "shh." Both decay fast (T60 tens of ms). **No transient click, no pitch glide, no tonal partials.**

## 2. Mapping onto Membrum

The perceptual sound is carried by the **always‑on parallel noise layer** (= the PhISEM noise band) plus the **NoiseBurst** exciter's filtered‑noise contact. The `NoiseBody` modal bank is kept as a **thin, short, bright metallic tint** ("b1 partials") under the hiss — never a pitch.

**Baseline (cabasa):**

| Param | Norm | Physical |
|---|---|---|
| Exciter | 0.40 | NoiseBurst |
| Body | 1.00 | NoiseBody |
| Material | 0.85 | very bright (metallic) |
| Size | 0.08 | f0 ≈ 1.2 kHz (tiny vessel) |
| Decay | 0.08 | ~0.34× body ring (near‑dead) |
| Level | 0.62 | accent‑level trim |
| **Noise Mix** | **0.85** | **dominant hiss layer** |
| **Noise Cutoff** | **0.73** | **≈3.4 kHz LP corner** |
| Noise Resonance | 0.16 | q≈1.05 (broad, like cabasa r=0.7) |
| Noise Decay | 0.12 | ~35 ms (fast "tss") |
| Noise Color | 0.75 | White (bright) |
| **Click Mix** | **0.0** | **no click** |
| NoiseBurst Dur | 0.20 | 4.6 ms mid burst |
| Body Damp b1 | 0.55 | ~27 s⁻¹ tight flat damping |
| Body Damp b3 | 0.0 | metallic (no f² roll‑off) |
| Air Loading | 0.0 | n/a (membrane‑only) |
| Pan | 0.5 | center |

## 3. Variants

- **Brighter cabasa / sekere:** Noise Cutoff → 0.80 (≈5–6 kHz), Noise Color → 0.85 (Violet).
- **Maraca (granular, darker):** Noise Resonance → 0.40 (tighter peak), Noise Cutoff → 0.65 (≈2 kHz), Material → 0.78, Noise Decay → 0.18, Mode Scatter → 0.3.
- **Hand shaker:** Material 0.85, NoiseBurst Dur → 0.25, Noise Cutoff → 0.70.

## 4. Deliberately defaulted

PitchEnv (no pitch/glide), Morph, Drive/Fold (noise + saturation = no benefit), ToneShaper filter (bypassed — all shaping is in the noise layer), Mode Inject / Nonlinear Coupling (no tonal series to brighten), all coupling/secondary/tension (no resonant shell, no glide), FM/Feedback/Friction (wrong‑exciter no‑ops), and all five macros (neutral). See `defaultedParams` for one‑line physical reasons.

---
*Voiced against the post‑audit (2026‑06‑07) corrected signal path: linear voice + measured‑strike body norm, noise/click re‑balanced (~−18 dBFS), bus true‑peak limiter at −1 dBTP. The shaker leans on the noise layer, which the audit recalibrated against the actual lowpass response (M‑7).*