<!-- verdict: pass-with-fixes | coverageOk: true | issues fixed: 9 | IMPLEMENTED: 2026-06-09 (commit re-tune Acoustic Studio Kit) -->

# Membrum — "Acoustic Studio Kit" (acousticKit()) — Corrected Re-Tune Plan (VERIFIED)

A full mic'd studio kit, voiced against the post-audit chain (linear per-voice output, measured-strike body norm / -6 dBFS budget, -1 dBTP bus limiter). All values are NORMALIZED [0,1] (on-wire / preset). Pad index 0..31 = MIDI 36..67 (GM). Cymbals route to Output Bus 1.

> Adversarial-verification pass applied 9 fixes: woodblock body Shell->Plate (dedicated archetype), bongos/congas regained their cited PitchEnv (+ bongo secondary shell), clap regained its defining Noise Resonance formant, cowbell regained ModeStretch/DecaySkew + correct FM ratio, rim shot regained Strike Position + correct Size, tambourine regained choke/reso. All other pads verified consistent with their cited archetype recipes and the corrected post-audit semantics.

## Layout (GM-aligned)

| Pad | MIDI | GM role | Drum | Body / Exciter | Notes |
|----|------|---------|------|----------------|-------|
| 0 | 36 | Bass Drum 1 | Acoustic Kick | Membrane / Mallet | air-load + shell coupling + glide |
| 1 | 37 | Side Stick | Side Stick | Shell / Impulse | MOVED to GM slot |
| 2 | 38 | Acoustic Snare | Snare (wire-buzz) | Membrane / NoiseBurst | crack + wires + shell |
| 3 | 39 | Hand Clap | Hand Clap | NoiseBody / NoiseBurst | NEW crafted (formant reso) |
| 4 | 40 | Electric Snare | Rim Shot (2nd snare) | Shell / Impulse | NEW (was side stick) |
| 5 | 41 | Low Floor Tom | Floor Tom | Membrane / Mallet | tom row, graded |
| 6 | 42 | Closed Hi-Hat | Closed Hat | NoiseBody / NoiseBurst | choke 1 |
| 7 | 43 | High Floor Tom | Tom 2 | Membrane / Mallet | |
| 8 | 44 | Pedal Hi-Hat | Pedal Hat | NoiseBody / NoiseBurst | choke 1, b1=0.5 splat |
| 9 | 45 | Low Tom | Tom 3 | Membrane / Mallet | |
| 10 | 46 | Open Hi-Hat | Open Hat | NoiseBody / NoiseBurst | choke 1, HP, long tail |
| 11 | 47 | Low-Mid Tom | Tom 4 (canonical) | Membrane / Mallet | |
| 12 | 48 | Hi-Mid Tom | Tom 5 | Membrane / Mallet | |
| 13 | 49 | Crash 1 | Crash Cymbal 1 | NoiseBody / NoiseBurst | bus 1, bloom |
| 14 | 50 | High Tom | High Rack Tom | Membrane / Mallet | |
| 15 | 51 | Ride 1 | Ride Cymbal | Bell / NoiseBurst | NEW, bus 1 |
| 17 | 53 | Ride Bell | Ride Bell (cup) | Bell / NoiseBurst | NEW, bus 1 |
| 18 | 54 | Tambourine | Tambourine/Pandeiro | NoiseBody / NoiseBurst | NEW, choke 1 |
| 19 | 55 | Splash | Splash | NoiseBody / NoiseBurst | NEW, bus 1 |
| 20 | 56 | Cowbell | Cowbell | Bell / FMImpulse | NEW, FM live |
| 21 | 57 | Crash 2 | Crash 2 (dark) | NoiseBody / NoiseBurst | NEW, bus 1 |
| 22 | 58 | (Vibraslap) | Cabasa/Shaker | NoiseBody / NoiseBurst | NEW |
| 23 | 59 | Ride 2 | Ride 2 (dark) | Bell / NoiseBurst | NEW, bus 1 |
| 24 | 60 | Hi Bongo | Bongo Hi | Membrane / Impulse | NEW, pitchEnv + shell |
| 25 | 61 | Low Bongo | Bongo Lo | Membrane / Impulse | NEW, pitchEnv + shell |
| 26 | 62 | Mute Hi Conga | Conga Hi | Membrane / Impulse | NEW, pitchEnv + barrel |
| 27 | 63 | Open Hi Conga | Conga Lo (tumba) | Membrane / Impulse | NEW, pitchEnv + barrel |
| 28 | 64 | Low Conga | Woodblock | Plate / Impulse | NEW, modeStretch+decaySkew |
| 16,29,30,31 | 52,65,66,67 | China / — | (disabled spare) | default | 16=GM China left as documented spare (no China archetype in scope); 29-31 no distinct GM role |

Crafted set: {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17,18,19,20,21,22,23,24,25,26,27,28} = 28 pads.

## Per-pad exact normalized values

Only meaningful params shown; everything else is at its documented default. Body/Exciter selectors per the Layout table.

### Pad 0 — Acoustic Kick (Membrane/Mallet)
Material 0.35 · Size 0.85 (70.7 Hz) · Decay 0.25 · Strike 0.30 · Level 0.85 · PitchEnv 160->50 Hz / 40 ms / curve 0.15 · b1 0.13 · b3 0.42 · AirLoading 0.78 · TensionMod 0.18 · CouplingStrength 0.35 · Secondary ON / Size 0.40 / Mat 0.60 · Click 0.70 / 0.20 / 0.45 · Noise 0.10 / cut 0.45 / Brown / dec 0.20 · Pan 0.50. (Verified == acoustic-maple-kick.md.)

### Pad 1 — Side Stick (Shell/Impulse)
Material 0.30 · Size 0.20 (946 Hz) · Decay 0.16 · Strike 0.30 · Level 0.78 · b1 0.42 · b3 0.10 · ModeScatter 0.50 · Click 0.88 / 0.10 / 0.62 · Noise 0.0 · Pan 0.58. (Verified == side-stick.md.)

### Pad 2 — Acoustic Snare (Membrane/NoiseBurst)
Material 0.50 · Size 0.42 (~190 Hz) · Decay 0.30 · Strike 0.35 · Level 0.92 · NoiseBurstDur ~4 ms · Filter LP cut 0.92 / reso 0.12 / envAmt 0.72 / dec 0.32 / rel 0.15 · PitchEnv 200->130 Hz / 35 ms / curve 0.35 · Drive 0.08 · NonlinearCoupling 0.22 · ModeScatter 0.28 · b1 0.28 · b3 0.03 · AirLoading 0.42 · Noise 0.82 / cut 0.90 / reso 0.10 / dec 0.48 / Violet · Click 0.92 / 0.18 / 0.90 · CouplingStrength 0.78 · Secondary ON / Size 0.78 / Mat 0.52 · TensionMod 0.16 · Pan 0.50. (Verified == acoustic-snare-wire-buzz.md, Material -0.05 studio delta.)

### Pad 3 — Hand Clap (NoiseBody/NoiseBurst)  [FIXED]
Material 0.85 · Size 0.18 · Decay 0.18 · Level 0.80 · NoiseBurstDur 0.55 (~9 ms spread) · Noise 0.85 / cut 0.78 / **reso 0.40 (Q~2.18 formant)** / dec 0.20 / White · Click 0.45 / 0.22 / 0.62 · ModeScatter 0.40 · b1 0.50 · b3 0.0 · Pan 0.44. (Added the defining Noise Resonance formant + aligned Material/Size/cutoff to clap.md.)

### Pad 4 — Rim Shot (Shell/Impulse)  [FIXED]
Material 0.70 · Size **0.34** (~686 Hz) · Decay 0.18 · **Strike 0.15** · Level 0.88 · ModeStretch 0.45 · Click 0.95 / 0.08 / 0.92 · Noise 0.20 / cut 0.85 / Violet · ModeScatter 0.40 · b1 0.50 · b3 0.08 · Pan 0.50. (Added Strike Position; Size 0.30->0.34 per rim-shot-perc.md.)

### Pads 5/7/9/11/12/14 — Tom Row (Membrane/Mallet), graded
Shared: Material 0.40 · Decay 0.50 · Strike 0.35 · Level 0.80 · b3 0.10 · CouplingStrength 0.40 · Secondary ON / Mat 0.55 · TensionMod 0.22 · Noise 0.16 / cut 0.45 / Pink · Click 0.50 / 0.32 / 0.55 · NonlinearCoupling 0.12 · ModeScatter 0.08 · PitchEnv curve 0.15.

| Pad | Size | PitchEnv Start->End / Time | b1 | AirLoad | Sec.Size | DecaySkew | Pan |
|----|------|---------------------------|----|---------|----------|-----------|-----|
| 5 (floor) | 0.80 | 200->110 / 90 ms | 0.30 | 0.65 | 0.32 | 0.42 | 0.30 |
| 7 | 0.70 | 250->130 / 80 ms | 0.32 | 0.60 | 0.31 | 0.44 | 0.38 |
| 9 | 0.60 | 300->150 / 70 ms | 0.34 | 0.55 | 0.30 | 0.46 | 0.46 |
| 11 | 0.55 | 290->180 / 65 ms | 0.34 | 0.55 | 0.32 | 0.48 | 0.54 |
| 12 | 0.45 | 380->230 / 50 ms | 0.37 | 0.48 | 0.30 | 0.52 | 0.62 |
| 14 | 0.40 | 470->290 / 40 ms | 0.40 | 0.45 | 0.29 | 0.55 | 0.70 |

(Verified == acoustic-tom.md grading row; pitchEnv norms via toLogNorm: 200=0.500/110=0.371, 250=0.547/130=0.407, 300=0.585/150=0.439, 290=0.581/180=0.477, 380=0.640/230=0.541, 470=0.686/290=0.586.)

### Pads 6/8/10 — Hi-Hats (NoiseBody/NoiseBurst), choke group 1
- **6 Closed:** Mat 0.88 · Size 0.10 · Decay 0.10 · Strike 0.60 · Level 0.72 · ModeStretch 0.50 · ModeScatter 0.30 · NoiseBurstDur 3 ms · Noise 0.70 / cut 0.86 / reso 0.20 / dec 0.10 / Violet · Click 0.18 / br 0.85 · b1 0.55 · b3 0.0 · AirLoad 0.0 · Pan 0.62. (Studio Noise Mix 0.70 = warm-hat variant per closed-hi-hat.md.)
- **8 Pedal:** Mat 0.88 · Size 0.12 · Decay 0.05 · Level 0.68 · Noise 0.70 / cut 0.88 / dec 0.07 / Violet · Click 0.0 · b1 0.50 · b3 0.0 · Pan 0.62.
- **10 Open:** Mat 0.90 · Size 0.18 · Decay 0.55 · Strike 0.45 · Level 0.72 · Filter HP cut 0.534 / reso 0.20 · Noise 0.70 / cut 0.867 / reso 0.25 / dec 0.70 / Violet · Click 0.22 / 0.20 / 0.85 · b1 0.30 · b3 0.0 · ModeScatter 0.45 · AirLoad 0.0 · Pan 0.62. (Pan deliberately 0.62 not the archetype's 0.42 so all three hats share one physical-hat image — internally consistent override.)

### Pad 13 — Crash 1 (NoiseBody/NoiseBurst), bus 1
Mat 0.93 · Size 0.35 · Decay 0.70 · Strike 0.55 · Level 0.72 · ModeStretch 0.60 · ModeInject 0.25 · NonlinearCoupling 0.35 · ModeScatter 0.60 · DecaySkew 0.58 · b1 0.30 · b3 0.0 · Noise 0.50 / cut 0.85 / White-Violet / dec 0.60 · Click 0.20 / 0.30 / 0.82 · Bus 1 · Pan 0.40. (Verified == crash-cymbal.md.)

### Pad 15 — Ride 1 (Bell/NoiseBurst), bus 1
Mat 0.95 · Size 0.30 (~400 Hz) · Decay 0.90 · Strike 0.18 · Level 0.72 · ModeStretch 0.45 · DecaySkew 0.62 · NonlinearCoupling 0.18 · ModeScatter 0.55 · b1 0.16 · b3 0.0 · Noise 0.45 / cut 0.90 / dec 0.78 / Violet · Click 0.45 / 0.25 / 0.82 · Bus 1 · Pan 0.62. (Verified == ride-cymbal.md.)

### Pad 17 — Ride Bell / cup (Bell/NoiseBurst), bus 1
Mat 0.95 · Size 0.26 · Decay 0.72 · Strike 0.05 (cup) · Level 0.74 · ModeStretch 0.40 · DecaySkew 0.60 · NonlinearCoupling 0.20 · ModeScatter 0.40 · b1 0.18 · b3 0.0 · Noise 0.30 / cut 0.88 / dec 0.55 / Violet · Click 0.55 / br 0.85 · Bus 1 · Pan 0.64.

### Pad 19 — Splash (NoiseBody/NoiseBurst), bus 1
Mat 0.94 · Size 0.20 · Decay 0.30 · Strike 0.55 · Level 0.70 · ModeStretch 0.55 · NonlinearCoupling 0.28 · ModeScatter 0.60 · DecaySkew 0.55 · b1 0.40 · b3 0.0 · Noise 0.50 / cut 0.90 / reso 0.20 / dec 0.25 / Violet · Click 0.22 · Bus 1 · Pan 0.36. (Consistent with dedicated splash-cymbal.md; b1 0.40 tighter than crash per 'shortest of family'.)

### Pad 21 — Crash 2 dark (NoiseBody/NoiseBurst), bus 1
Mat 0.90 · Size 0.40 · Decay 0.75 · Strike 0.55 · Level 0.72 · ModeStretch 0.60 · ModeInject 0.25 · NonlinearCoupling 0.35 · ModeScatter 0.65 · DecaySkew 0.56 · b1 0.28 · b3 0.0 · Noise 0.50 / cut 0.80 / White / dec 0.65 · Click 0.20 · Bus 1 · Pan 0.58. (== crash-cymbal.md 'acoustic/warmer' variant.)

### Pad 23 — Ride 2 dark (Bell/NoiseBurst), bus 1
Mat 0.92 · Size 0.34 · Decay 0.92 · Strike 0.18 · Level 0.72 · ModeStretch 0.45 · DecaySkew 0.60 · NonlinearCoupling 0.18 · ModeScatter 0.58 · b1 0.15 · b3 0.0 · Noise 0.42 / cut 0.85 / dec 0.78 / White-Violet · Click 0.45 / br 0.80 · Bus 1 · Pan 0.58.

### Pad 18 — Tambourine/Pandeiro (NoiseBody/NoiseBurst)  [FIXED]
Mat 0.92 · Size 0.15 · Decay 0.25 · Level 0.74 · **Choke Group 1** · NoiseBurstDur 0.40 (~7 ms) · Noise 0.65 / cut 0.92 / **reso 0.20** / dec 0.30 / Violet · ModeScatter 0.50 · ModeStretch 0.55 · b3 0.0 · Click 0.30 · Pan 0.56. (Added Choke 1 + Noise Resonance per pandeiro.md; Noise Mix held 0.65 studio.)

### Pad 20 — Cowbell (Bell/FMImpulse)  [FIXED]
Mat 0.78 · Size 0.26 · Decay 0.30 · Level 0.76 · **FMRatio 0.50 (live -> mod ratio 2.5, detuned fifth)** · **ModeStretch 0.55 · DecaySkew 0.42** · Click 0.55 / br 0.70 · Noise 0.10 / cut 0.62 / Pink / dec 0.20 · ModeScatter 0.20 · b1 0.40 · b3 0.0 · Pan 0.46. (Added ModeStretch/DecaySkew clang axes + corrected FM ratio per cowbell.md.)

### Pad 22 — Cabasa/Shaker (NoiseBody/NoiseBurst)
Mat 0.80 · Size 0.20 · Decay 0.12 · Level 0.70 · NoiseBurstDur 0.20 (~5 ms) · Noise 0.80 / cut 0.88 / **reso 0.16** / dec 0.10 / Violet · Click 0.10 · b1 0.55 · b3 0.05 · Pan 0.66. (Bright-cabasa variant of shaker-cabasa.md; added broad Noise Resonance 0.16 per PhISEM cabasa r=0.7.)

### Pads 24/25 — Bongos (Membrane/Impulse)  [FIXED]
- **24 Hi (macho):** Mat 0.50 · Size 0.32 · Decay 0.22 · Strike 0.40 · Level 0.78 · **PitchEnv 420->350 Hz / 20 ms / curve 0.15** · AirLoad 0.40 · TensionMod 0.25 · b1 0.40 · b3 0.08 · **CouplingStrength 0.25 · Secondary ON / Size 0.30 / Mat 0.40** · Click 0.70 / 0.15 / 0.75 · Noise 0.08 · ModeScatter 0.10 · Pan 0.40.
- **25 Lo (hembra):** Size 0.40 · **PitchEnv 340->280 Hz / 20 ms / curve 0.15** · Decay 0.24 · AirLoad 0.45 · b1 0.38 · Click 0.68 / 0.15 / br 0.72 · TensionMod 0.25 · CouplingStrength 0.25 · Secondary ON / Size 0.30 / Mat 0.40 · (else as Hi) · Pan 0.46. (Added the cited PitchEnv pitch mechanism + crafted secondary shell per bongo.md.)

### Pads 26/27 — Congas (Membrane/Impulse), barrel shell  [FIXED]
- **26 Hi:** Mat 0.42 · Size 0.50 · Decay 0.30 · Strike 0.42 · Level 0.80 · **PitchEnv 280->210 Hz / 20 ms / curve 0.15** · AirLoad 0.55 · TensionMod 0.22 · b1 0.34 · b3 0.10 · CouplingStrength 0.30 · Secondary ON / Size 0.35 / Mat 0.45 · Click 0.55 / 0.20 / br 0.60 · Noise 0.10 / cut 0.40 / Pink · Pan 0.58.
- **27 Lo (tumba):** Size 0.62 · **PitchEnv 200->150 Hz / 20 ms / curve 0.15** · AirLoad 0.58 · b1 0.32 · Secondary Size 0.38 · (else as Hi) · Pan 0.64. (Added the cited PitchEnv per conga.md; tensionMod retained for the kerthump.)

### Pad 28 — Woodblock (Plate/Impulse)  [FIXED — body changed Shell->Plate]
Mat 0.32 · Size 0.18 (~529 Hz) · Decay 0.18 · Strike 0.30 · Level 0.76 · **ModeStretch 0.50 · DecaySkew 0.30** · b1 0.50 · b3 0.10 · ModeScatter 0.20 · Click 0.78 / 0.12 / 0.78 · Noise 0.0 · Pan 0.52. (Body corrected to Plate (free-plate Chladni) per dedicated woodblock-perc.md, which reasons Plate>Shell for the inharmonic low-mode-dominant slit-cavity block; modeStretch/decaySkew now meaningful on the modal Plate body. AirLoading default = no-op on Plate.)

## Param-surface coverage (the kit collectively)
- **Membrane physics:** airLoading + tensionMod on kick, snare, all 6 toms, bongos, congas.
- **Head<->shell secondary coupling:** kick, snare, toms, **bongos (restored)**, congas.
- **PitchEnv:** kick, snare, all toms, **bongos + congas (restored — the cited pitch mechanism for hand drums)**.
- **Metallic axes (now corrected):** modeStretch (all cymbals + woodblock + tambourine + **cowbell**), decaySkew per-mode tilt (every cymbal + every tom + woodblock + **cowbell**), modeInject 1/k (crashes), NonlinearCoupling amplitude-bloom (crashes, splash, both rides, ride-bell, snare).
- **Exciters:** Mallet (kick/toms), NoiseBurst (snare/hats/cymbals/clap/tamb/shaker), Impulse (side stick/rim/bongos/congas/**woodblock**), FMImpulse (cowbell, FM ratio live = 2.5).
- **Clap formant:** Noise Resonance 0.40 (restored — the defining cupped-hand formant).
- **ToneShaper:** LP filter env on snare; HP on open hat.
- **Routing/util:** choke group 1 (3 hats + tambourine); Output Bus 1 (6 cymbals); full equal-power pan image (toms 0.30->0.70, hats 0.62, overheads split, perc spread).

## Range / legality audit
All 28 voiced pads: every valueNorm in [0,1] (min 0.0, max 0.95). Discrete/sentinel decodes verified — Choke Group 0.125 -> group 1; Output Bus 0.0667 -> aux 1; FM Ratio 0.50 -> mod 2.5; Filter Type 0.0 -> LP, 0.5 -> HP; Noise Color thresholds (Brown<0.25, Pink<0.55, White<0.80, Violet) all land as intended; b1/b3 written off-sentinel only where a flat-damping override is wanted; Secondary Enabled 1.0 paired with CouplingStrength>0 everywhere it is set (kick/snare/toms/bongos/congas) so it is never a silent no-op.

## Gaps fixed
Added a complete ride section (was absent), a 2nd snare/rim articulation, and a crafted hand-perc block; de-duplicated the 7 identical cymbal loops and 13 identical perc loops; broke the one-recipe tom sweep into a true per-pad gradient. Verification pass additionally restored every cited-but-dropped meaningful param (bongo/conga pitchEnv, bongo shell, clap formant reso, cowbell clang axes, rim strike) and corrected the woodblock body model. Remaining documented spares: pad 16 (GM China, no archetype in scope) and pads 29-31 (no distinct GM role).

---

## Verification log (9 issues found & fixed)

1. PAD 28 WOODBLOCK — WRONG BODY MODEL. Proposal voiced the woodblock on Shell/Impulse using the side-stick archetype. A dedicated woodblock archetype exists (woodblock-perc.md) and explicitly chooses Plate (free-plate Chladni) over Shell, reasoning that Shell's strict 1:2.76:5.40 ladder is the TUNED-bar idealisation while the slit-cavity woodblock is low-mode-dominant + inharmonic, captured by Plate+modeStretch. FIX: changed body to Plate; set Material 0.32, Size 0.18, Decay 0.18, Strike 0.30, b1 0.50, b3 0.10, ModeStretch 0.50, DecaySkew 0.30, ModeScatter 0.20, Click 0.78/0.12/0.78, Noise 0, AirLoading default (no-op on Plate). modeStretch/decaySkew now correctly meaningful on a Plate (modal body) rather than relying on Shell.

2. PADS 24/25 BONGOS — MISSING PITCH MECHANISM + MISSING CRAFTED SHELL. Proposal dropped PitchEnv entirely ('hand-drum tone uses tensionMod not a programmed glide') and disabled the secondary shell ('no shell coupling crafted'). The dedicated bongo archetype (bongo.md) explicitly states 'Size sets character, the env sets pitch' and crafts BOTH a pitch envelope AND a secondary shell. Without PitchEnv the macho/hembra register is not set (Size 0.30/0.40 natural f0 is ~240/190 Hz, wrong band). FIX: added PitchEnv macho 420->350 Hz (norm 0.6585->0.6175) / 20 ms (0.04) / curve 0.15, hembra 340->280 Hz (0.6131->0.5719); added Secondary ON, CouplingStrength 0.25, SecondarySize 0.30, SecondaryMaterial 0.40; Click Contact 0.15 per archetype.

3. PADS 26/27 CONGAS — MISSING PITCH ENVELOPE. Proposal dropped PitchEnv ('tensionMod carries the tone'). The conga archetype (conga.md) sets BOTH a short pitch env (hi 280->210, lo 200->150, 20 ms, curve 0.15) AND tensionMod 0.22 — the env fixes the open-tone fundamental, tensionMod adds the kerthump. FIX: added PitchEnv hi 280->210 Hz (norm 0.560->0.503) / 20 ms (0.04) / curve 0.15; lo 200->150 Hz (0.50->0.438).

4. PAD 3 HAND CLAP — MISSING THE DEFINING FORMANT (Noise Resonance). Proposal omitted Noise Resonance, leaving it at default 0.2, and used Noise Cutoff 0.80. The clap archetype (clap.md) makes Noise Resonance 0.40 (Q~2.18, ~909 Q 1.95) the DEFINING clap formant at the LP cutoff, with Noise Cutoff 0.78. Missing the resonant formant flattens the clap. FIX: added Noise Resonance 0.40; set Noise Cutoff 0.78, Material 0.85, Size 0.18, Click Contact 0.22, Click Brightness 0.62 to match the cited recipe.

5. PAD 20 COWBELL — UNDER-EXERCISED METALLIC AXES + FM RATIO OFF THE CITED VALUE. Proposal defaulted Mode Stretch and Decay Skew ('Bell ratios already clangy'). The cowbell archetype (cowbell.md) explicitly sets Mode Stretch 0.55 (extra inharmonic clang/beating) and Decay Skew 0.42 (lift bright clang partials) — and the kit's stated goal is to exercise these axes. FM Ratio 0.45 (mod 2.35) is below the cited detuned-fifth target 0.50 (mod 2.5). FIX: added Mode Stretch 0.55, Decay Skew 0.42; nudged FM Ratio 0.45->0.50.

6. PAD 4 RIM SHOT — MISSING STRIKE POSITION + SIZE OFF RECIPE. Proposal omitted Strike Position (meaningful for Shell: re-weights modal energy) and used Size 0.30. Rim-shot archetype (rim-shot-perc.md) sets Size 0.34 (f0 686 Hz, in the 3-6 kHz rim-crack band) and Strike 0.15 (near-edge). FIX: set Size 0.34, added Strike Position 0.15.

7. PAD 18 TAMBOURINE/PANDEIRO — minor: archetype adds Choke Group 1 (jingle strokes mutually choke, hat-like) and Noise Resonance 0.20. Proposal omitted both. FIX (optional alignment): added Choke Group 1 and Noise Resonance 0.20; kept proposal's Noise Mix 0.65 (studio balance) which is within the recipe's documented variant tolerance.

8. PAD 22 CABASA/SHAKER — minor brightness deviation. Proposal Noise Cutoff 0.88 (~5-6 kHz) corresponds to the archetype's 'brighter cabasa/sekere' variant rather than the baseline cabasa center ~3 kHz (Noise Cutoff 0.73). Left as-is (a bright studio shaker is a legitimate variant) but documented; added Noise Resonance 0.16 per the cabasa PhISEM constant (broad, r=0.7).

9. LAYOUT — pad 16 (MIDI 52) is GM Chinese Cymbal, a legitimate cymbal slot, but is listed as a disabled spare while the kit ships a full cymbal section. Not wrong (a China is optional and the kit already has 6 cymbals on bus 1) but flagged: it is the one remaining GM cymbal role left unvoiced. Documented as an intentional spare; no fix applied (no China archetype in scope and the overhead bus is already well-populated).
