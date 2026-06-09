# Membrum Recipe — Cowbell (world)

**Archetype:** Clapperless metal bell — the agogo-family clangy inharmonic idiophone. One recipe, three variants: the **CR-78/808 electronic cowbell**, the **vintage-wood (mounted) cowbell**, and the **Latin hi/lo (cencerro) pair**.

**Body:** `Bell` (idx 4) — church-bell Chladni, 16 modes, hum at 0.25·f0, clang partials at 1.5·/2.0·f0. The only Membrum body with a tuned-but-inharmonic partial cluster, i.e. the cowbell's "detuned-fifth, beating" spectrum.
**Exciter:** `FMImpulse` (idx 4) — Chowning bell-FM clang, fmRatio in the detuned-fifth band. (`Impulse` is the alternate for a cleaner vintage-wood stick-on-metal contact.)

## The physics this is voiced against

- Real cowbell: clapperless **steel/iron**, struck with a stick; tuned versions span **F4–C7 (~350–2100 Hz)**; "single note with timbral variations," **noisier than handbells** (Wikipedia). Strongly **inharmonic**, **short/dry** decay (hand/mount damped, ~150–400 ms), hard **stick contact tick**, **no pitch glide**.
- Iconic synthesis (TR-808 / CR-78): **two square waves at 540 Hz + 800 Hz** (ratio **1:1.48**, an out-of-tune perfect fifth — acetateme/Baratatronix); Sound On Sound's reverse-engineering gives **587 Hz + 845 Hz (1:1.44)** and warns "even small deviations from these pitches destroy the cowbell illusion." The inharmonic pair **beats** → metallic dissonance.
- Filtering/envelope: summed pair **band-passed ≈ 2.64 kHz**, 12 dB/oct + mild resonance to accentuate partials near centre; **two-stage amplitude envelope** — a high-amplitude short **impact** then a faster-than-natural clangorous ring-out; **low-level pink noise halo** around the partials during impact (SoS).

## Mapping to Membrum (normalized baseline)

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | **0.80** | FMImpulse | metallic FM clang ≈ the 808 two-tone |
| Body Model | **0.80** | Bell | inharmonic tuned clang cluster |
| Material | **0.78** | brightness≈0.93, base≈1.13 s | bright struck metal |
| Size | **0.22** | f0_nom≈**471 Hz** (clang at 707/942 Hz) | cowbell register; covers 808 540–800 Hz |
| Decay | **0.30** | ~0.5 s realised | short, dry ring (not a church bell) |
| Strike Position | **0.30** | azimuth 0.47 rad | stick on soundbow |
| Level | **0.75** | linear 0.75 | loud accent, headroom under −6 dBFS |
| FM Ratio | **0.50** | mod ratio **2.5** | inharmonic detuned-fifth family |
| Click Mix | **0.55** | stick tick into body + out | defining attack |
| Click Contact | **0.10** | 2.3 ms | sharp tick |
| Click Brightness | **0.72** | ~2.6 kHz BP | ≈ SoS 2.64 kHz center |
| Noise Mix | **0.10** | faint halo | SoS impact-phase noise halo |
| Noise Color | **0.40** | Pink | SoS pink-ish halo |
| Noise Cutoff | **0.62** | ~2.4 kHz LP | halo in the clang band |
| Noise Decay | **0.20** | ~48 ms | impact-only |
| Body Damping b1 | **0.32** | ~16 s⁻¹ | short dry RT60 |
| Body Damping b3 | **0.0** | no f² damping | metal: bright partials sustain |
| Mode Stretch | **0.55** | phys ~1.33 | extra inharmonic clang/beating |
| Mode Scatter | **0.20** | ~3% dither | cast-metal imperfection |
| Decay Skew | **0.42** | −0.16 tilt | lift bright clang partials |
| Macro Brightness | **0.65** | +0.15 | clangy register |
| Filter Cutoff | **1.0** | bypass | body+click already shape |
| PitchEnv Time | **0.0** | disabled | cowbell pitch is static |
| Pan | **0.5** | center | hi/lo can be split in the kit |

## Variant deltas (from the baseline above)

- **CR-78 / 808 cowbell** — baseline as-is. For the lo/main 808 timbre nudge **Size 0.22→0.24** (≈445 Hz) and **fmRatio 0.50→0.55** (closer to the 1.48 fifth). Two-stage envelope is approximated by the hard Click impact + short b1 ring.
- **Vintage-wood (mounted) cowbell** — switch **Exciter → Impulse (0.0)** for a cleaner stick contact (no FM sidebands); raise **Click Mix → 0.62**, **Material → 0.72** (slightly darker, mounted bell), **Decay → 0.34**.
- **Latin hi/lo pair (cencerro / agogô)** — **hi:** Size **0.20** (≈505 Hz), fmRatio **0.55**, Decay **0.30**, optional feedbackAmount-as-edge n/a (FM). **lo:** Size **0.26–0.28** (≈410–383 Hz), fmRatio **0.42** (mod ratio 2.26), Decay **0.40** (a touch longer). Place hi/lo at Pan ~0.4 / ~0.6. Agogô hi can go brighter: Material **0.85**, Size **0.14** (≈579 Hz), fmRatio up to **0.72**.

## Deliberately defaulted (no-op or neutral for this archetype)

ToneShaper filter + Drive/Fold (bypassed — clang comes from body+FM, not saturation); all PitchEnv sub-params (env disabled — static pitch); ModeInject & NonlinearCoupling (exact bypass — preserve the pure inharmonic clang); Material Morph (static timbre); Air Loading & Tension Mod (membrane-only, no-op on Bell); Secondary/coupling stages (single-shell bell has no coupled resonator); Tightness/BodySize/Punch/Complexity macros (neutral 0.5 — explicit params set the voice); NoiseBurst Duration & Friction Pressure (wrong-exciter no-ops); Output Bus 0 (main).