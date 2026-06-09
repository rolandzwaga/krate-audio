# Membrum Recipe — Riq / Tambourine Frame Drum (world)

**Body:** Membrane (Bessel circular drumhead) · **Exciter:** Impulse (raised-cosine fingertip click)
**Secondary:** metallic jingle shell (Secondary Enabled, size 0.20 / material 0.85, coupled at 0.40)
**Signature layer:** prominent bright **Violet** noise (mix 0.60, ~9.5 kHz LP) = the broadband jingle shimmer

## Physical model behind the choices

A riq is **two superimposed sources**:

1. **The head** — a small (~9"/23 cm), thin, high-tension fishskin **circular membrane**. Inharmonic Bessel-zero ratios `1 : 1.594 : 2.136 : 2.296 : 2.653 …` (Euphonics 3.6.1; D. Russell, PSU). The doum (center bass stroke) of a small tightly-tuned head sits ~120-220 Hz → **target ~150 Hz**. Hand-damped → short ring (~150-300 ms).
2. **The jingles** — 5 *double* pairs (= 10 small brass/steel discs) that collide repeatedly → a **broadband, HF, noise-like shimmer that predominates and masks the head** (Grokipedia/Tambourine). The canonical synthesis is Perry Cook's **PhISEM** (Csound `tambourine` / STK `Shakers`): ~32 colliding objects driving metal resonances at **2300 Hz (main), 5600 Hz, 8100 Hz**, high damping 0.9985 (sustaining metallic ring). Jingle energy lives in the **3-16 kHz** band (cymbal normal-mode analyses).

No pitch glide (light hand strike → negligible tension modulation). Brass = warmer, steel/German-silver = brighter (Rhythm Tech / Pearl jingle-material notes).

## How that maps to Membrum

| Component | Membrum stage | Setting |
|---|---|---|
| Fishskin head, ~150 Hz doum | Membrane body, Size | Size 0.52 → ~151 Hz; Material 0.40 (warm/woody) |
| Inharmonic, hand-made skin | true Bessel + Mode Scatter | Mode Stretch neutral (0.333); Mode Scatter 0.20 (~3% dither) |
| Hand-damped short ring | Decay + b1/b3 | Decay 0.34; Body Damping b1 0.32 (~16 s⁻¹), b3 0.12 |
| Off-center doum/tek | Strike Position | 0.60 (r/a≈0.54) |
| **Jingle broadband shimmer (dominant)** | Noise layer | Mix 0.60, Cutoff 0.82 (~9.5 kHz LP), Color **Violet** (0.90), Decay 0.50 (~200 ms), Reso 0.30 |
| **Tuned metallic jingle ring** | Secondary shell + coupling | Secondary Enabled 1.0; Secondary Size **0.20** (high f0), Material **0.85** (bright/long); Coupling Strength 0.40 |
| Sharp tek / jingle onset | Click layer | Mix **0.65**, Contact 0.30 (~2.9 ms), Brightness 0.85 (~6.9 kHz) |
| Dense multi-source clatter | Complexity macro | 0.62 (slight thickening) |
| No glide / no morph / clean | PitchEnv Time 0, Morph off, Drive/Fold 0, Tension 0, filter bypassed |

## Key normalized baseline (post-audit semantics)

```
exciterType = 0.0      (Impulse)
bodyModel   = 0.0      (Membrane)
material    = 0.40     brightness 0.40, woody fishskin
size        = 0.52     f0 ≈ 151 Hz (doum)
decay       = 0.34     ~0.55× base (~0.23 s head ring)
strikePos   = 0.60     r/a ≈ 0.54
level       = 0.80
tsFilterCutoff = 1.0   20 kHz → bypassed (stays bright)
modeStretch = 0.333    physical 1.0 (true Bessel)
decaySkew   = 0.55     +0.10 mild high-mode tilt
modeScatter = 0.20     ~3% dither
airLoading  = 0.45     moderate low-mode depression
bodyDampingB1 = 0.32   ~16 s⁻¹
bodyDampingB3 = 0.12   1.2e-4 s
modeInject  = 0.0      OFF (inharmonic, no harmonic stack)
nonlinearCoupling = 0.0
macroComplexity = 0.62
-- jingle noise shimmer --
noiseLayerMix    = 0.60
noiseLayerCutoff = 0.82   ~9.5 kHz LP
noiseLayerReso   = 0.30   Q≈1.7
noiseLayerDecay  = 0.50   ~200 ms
noiseLayerColor  = 0.90   Violet
-- bright tek/jingle onset --
clickLayerMix        = 0.65
clickLayerContactMs  = 0.30   ~2.9 ms
clickLayerBrightness = 0.85   ~6.9 kHz
-- metallic jingle secondary --
secondaryEnabled   = 1.0
couplingStrength   = 0.40
secondarySize      = 0.20    high-f0 metal ring
secondaryMaterial  = 0.85    bright, long metallic highs
-- off --
tsPitchEnvTime = 0.0   (no glide)
morphEnabled   = 0.0
tsDriveAmount  = 0.0 ; tsFoldAmount = 0.0
tensionModAmt  = 0.0
pan            = 0.5   center
```

## Sources
- Riq construction (9", fishskin, 5 double jingle pairs): worldpercussion.net, ethnicmusical.com, salamuzik.com
- Jingle broadband shimmer dominates/masks head: Grokipedia *Tambourine*
- PhISEM tambourine model (32 objects; 2300/5600/8100 Hz; damp 0.9985): csound.com `tambourine`, RTcmix MSHAKERS, STK `Shakers`
- Inharmonic Bessel membrane ratios: Euphonics 3.6.1, D. Russell (PSU)
- Cymbal/jingle 3-16 kHz HF content: Rollins normal-modes-of-cymbals
- Jingle material (brass=warm, steel=bright): Rhythm Tech, Pearl
