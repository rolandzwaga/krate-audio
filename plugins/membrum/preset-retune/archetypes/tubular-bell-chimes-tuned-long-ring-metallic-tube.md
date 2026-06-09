# Membrum Recipe — Tubular Bell / Chimes

**Body:** String (WaveguideString, Jaffe-Smith) · **Exciter:** Mallet

A long hanging metal tube struck at the top cap. Acoustically a *free-free bar* with strongly **inharmonic** partials (ideal ~1 : 2.76 : 5.40 : 8.93…, real tubes follow a modified Chladni law f∝(2n+c)^p, p≈1.65, c≈0.29 — Hibberts 233-tube study). The musical pitch is a **virtual strike tone**: modes 4-5-6(-7) form a near-harmonic 2:3:4(:5) series and the ear hears a pitch *one octave below P4* (the "missing 1"); P1/P2 are physically very weak (~55 & 158 Hz barely above noise). Bronze/steel tube rings for **several seconds**; HF modes decay faster so the bright strike tone softens into the hum. A 'purer tone than solid chimes' — minimal noise. No pitch glide.

Membrum's **String/waveguide** body is the right physical-model route for the *dominant perceptual feature* — one long, pitched, metallic-bright sustaining ring — even though it does not literally reproduce the free-free P4/P5/P6 triplet (that would need the modal Bell/Shell ratios). The canonical electronic alternative is Chowning FM (DX7 'TUB BELLS', c:m≈1:3.5, long decay/release).

## Parameters (normalized → physical)

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.20 | Mallet | soft beater, like a rawhide chime mallet |
| Body Model | 0.60 | String (waveguide) | single long pitched metal-tube ring |
| Material | 0.85 | loop brightness_ 0.15 (highs sustain) + decay base 3.48 s | bright, sustaining metallic tail |
| Size | 0.55 | f0 ≈ 225 Hz | deep tube ring; strike pitch reads an octave below |
| Decay | 0.92 | ×2.50 → ~8.7 s ring | chimes ring for seconds |
| Strike Position | 0.30 | pick pos 0.30 | near-top strike, mild brightening |
| Level | 0.72 | linear 0.72 | trim — high-Q ring builds energy |
| Click Mix | 0.40 | metallic contact tick | the cap 'tink' the soft exciter lacks |
| Click Contact | 0.20 | 2.6 ms | short sharp metal contact |
| Click Brightness | 0.65 | ~2.4 kHz BP | bright metal-strike tick |
| Noise Mix | 0.10 | light | 'purer tone', breath only |
| Noise Cutoff | 0.50 | ~850 Hz | neutral (near-silent layer) |
| Noise Decay | 0.30 | ~108 ms | short contact noise only |
| modeStretch | 0.50 | phys 1.25 — **no-op (String)** | carried for kit consistency |

## Defaulted (physical reason)
- **Pitch envelope** (Start/End/Time/Curve/Knee/Mid…): chimes have **no glide** — PitchEnv Time=0 disables it.
- **Drive/Fold/ModeInject/NonlinearCoupling/Morph**: a struck chime is clean & linear; all bypassed at 0.
- **ToneShaper filter + filter-env**: transparent (cutoff 1.0); the waveguide loop filter already shapes the tone.
- **String no-ops** (set but inert): Body Damping b1/b3, Air Loading, Mode Scatter, Decay Skew, Tension Mod — all modal/Membrane-only; on the waveguide they do nothing. Decay/material drive the ring instead.
- **Secondary/Coupling**: a tube is one resonator — no shell to couple into.
- **FM Ratio / Feedback / NoiseBurst / Friction**: secondary-exciter params, no-ops with Mallet selected.
- **Choke Group 0**: chimes ring freely. **Pan 0.5**: center. **Pad Enabled 1**.

## Implementation note (preset generator)
This archetype **already exists** as `pad[3]` in `holyMetalsKit()` (or equivalent) at `tools/membrum_preset_generator.cpp:1815-1826`, voiced exactly as above: `exciterType=Mallet, bodyModel=String, material=0.85, size=0.55, decay=0.92, level=0.72, modeStretch=0.50, clickLayerMix=0.40, clickLayerContactMs=0.20, clickLayerBrightness=0.65, noiseLayerMix=0.10, airLoading=0.0, bodyDampingB1=0.30, bodyDampingB3=0.20, decaySkew=0.55`. The b1/b3/airLoading/decaySkew/modeStretch values there are **inert no-ops on the String body** — they neither help nor hurt; the audible voice is set entirely by Material (brightness+decay-base), Size (f0), Decay (×2.5), strikePosition (pick comb), and the Click layer.

## Sources
- [Tubular bells — Wikipedia](https://en.wikipedia.org/wiki/Tubular_bells)
- [Tubular bells — The Sound of Bells (Hibberts)](https://www.hibberts.co.uk/tubular-bells/)
- [Modes of vibration of a tubular bell — Hibberts](https://www.hibberts.co.uk/modes-of-vibration-of-a-tubular-bell/)
- [Strike tone — Wikipedia](https://en.wikipedia.org/wiki/Strike_tone)
- [Tubular Bells — HyperPhysics](http://hyperphysics.phy-astr.gsu.edu/hbase/Music/tbell.html)
- [Ultimate Guide: FM Synthesis — Roland](https://articles.roland.com/ultimate-guide-fm-synthesis/)