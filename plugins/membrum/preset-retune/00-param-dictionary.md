# Membrum Parameter Dictionary (authoritative)

All values are NORMALIZED [0,1] on-wire/preset representation unless stated. Per-pad VST3 id = `1000 + padIndex*128 + offset`. Globals in 100-325. Proxies forward to the pad selected by `kSelectedPadId` (260).

## Per-pad sound slot (PadParamOffset, pad_config.h)

| Off | Name | norm -> physical (denorm site) | Default | Meaningful for |
|----|------|--------------------------------|---------|----------------|
| 0 | Exciter Type | enum 0..5 Impulse/Mallet/NoiseBurst/Friction/FMImpulse/Feedback | 0 | selector |
| 1 | Body Model | enum 0..5 Membrane/Plate/Shell/String/Bell/NoiseBody | 0 | selector |
| 2 | Material | verbatim; per-body brightness+decay base+stretch tilt | 0.5 | all |
| 3 | Size | f0 = base*0.1^size (base 500/800/1500 Hz) | 0.5 | all |
| 4 | Decay | decayTime *= exp(lerp(ln0.3,ln3,decay)) (10x) | 0.3 | all; overridden by b1 |
| 5 | Strike Position | mode-shape sampling (Bessel/beam/pluck pos) | 0.3 | all |
| 6 | Level | LINEAR final gain out=hardClip(shaped*env*level) | 0.8 | all |
| 7 | Filter Type | int(norm*3) LP/HP/BP | 0 | all (if filter on) |
| 8 | Filter Cutoff | 20*1000^norm Hz [20,20000]; >=20k+amt0 = bypass | 1.0 | all |
| 9 | Filter Resonance | Q 0.707..10 | 0 | all |
| 10 | Filter Env Amount | norm*2-1 [-1,+1]; 2^(3*amt*env) cutoff mod | 0.5 (=0) | all |
| 11 | Drive Amount | drive 1..10, 1/drive makeup; bypass at 0 | 0 | all |
| 12 | Fold Amount | fold 0..pi; bypass at 0 | 0 | all |
| 13 | PitchEnv Start | 20*100^norm Hz [20,2000] | 0.0 | all (Time>0) |
| 14 | PitchEnv End | 20*100^norm Hz | 0.0 | all (Time>0) |
| 15 | PitchEnv Time | norm*500 ms; 0 = pitch env OFF | 0.0 | all; master enable |
| 16 | PitchEnv Curve | norm*2-1 [-1,+1] seg1, 0.5=linear | 0.5 | all (Time>0) |
| 17 | Filter Env Atk | norm^3*500 ms | 0.0 | all |
| 18 | Filter Env Dec | norm^3*2000 ms | 0.1 | all |
| 19 | Filter Env Sus | [0,1] level | 0.0 | all |
| 20 | Filter Env Rel | norm^3*2000 ms | 0.1 | all |
| 21 | Mode Stretch | norm-of[0.5,2.0]: phys=0.5+norm*1.5 | 0.333 (=1.0) | modal only |
| 22 | Decay Skew | norm*2-1 [-1,+1]; bias + per-mode tilt | 0.5 (=0) | modal only |
| 23 | Mode Inject | verbatim; 8 harmonics 1/k; exact bypass 0 | 0.0 | all |
| 24 | Nonlinear Coupling | env-driven AM brightener; exact bypass 0 | 0.0 | all |
| 25 | Morph Enabled | bool >=0.5 | 0.0 | all |
| 26 | Morph Start | Material [0,1] | 1.0 | morph on |
| 27 | Morph End | Material [0,1] | 0.0 | morph on |
| 28 | Morph Duration | 10+norm*1990 ms | 0.0955 (=200ms) | morph on |
| 29 | Morph Curve | bool Lin/Exp | 0.0 | morph on |
| 30 | Choke Group | round(norm*8) [0,8]; 0=none | 0.0 | routing |
| 31 | Output Bus | round(norm*15) [0,15]; 0=main | 0.0 | routing |
| 32 | FM Ratio | 1+3*norm [1,4]; def 0.133->1.4 | 0.133 | FMImpulse only |
| 33 | Feedback Amount | drive floor, *0.85 max | 0.0 | Feedback only |
| 34 | NoiseBurst Duration | 2+norm*13 ms [2,15] | 0.5 | NoiseBurst only |
| 35 | Friction Pressure | +norm*0.5 bow bias | 0.0 | Friction only |
| 36 | Coupling Amount | global sympathetic participation weight | 0.5 | all (Global Coupling>0) |
| 37 | Tightness | macro: material/decay/decaySkew deltas | 0.5 | all |
| 38 | Brightness | macro: cutoff/modeInject deltas | 0.5 | all |
| 39 | Body Size | macro: size/stretch/decay deltas | 0.5 | all |
| 40 | Punch | macro: pitchEnv depth/time, attack deltas | 0.5 | all |
| 41 | Complexity | macro: coupling/nonlinear/modeInject deltas | 0.5 | all |
| 42 | Noise Mix | verbatim; *0.243; 0=off | 0.35 | all |
| 43 | Noise Cutoff | exp log [40,18000] Hz (lowpass) | 0.5 | all |
| 44 | Noise Resonance | 0.3+norm*4.7 [0.3,5] | 0.2 | all |
| 45 | Noise Decay | exp log [20,2000] ms | 0.3 | all |
| 46 | Noise Color | 4 bands Brown/Pink/White/Violet | 0.5 | all |
| 47 | Click Mix | verbatim; *0.445; 0=off; into exc+out | 0.5 | all+all exciters |
| 48 | Click Contact | 2+norm*3 ms [2,5] | 0.3 | all |
| 49 | Click Brightness | exp log [200,12000] Hz | 0.6 | all |
| 50 | Body Damping b1 | SENTINEL -1=derive(Decay); else 0.2+norm*49.8 s^-1 | 0.5* | modal only |
| 51 | Body Damping b3 | SENTINEL -1=derive(Material); else norm*1e-3 s | 0.5* | modal only |
| 52 | Air Loading | Bessel->Rossing freq depression | 0.6 | Membrane-effective |
| 53 | Mode Scatter | [0,15%] freq dither | 0.0 | modal only |
| 54 | Coupling Strength | feedforward head->shell drive | 0.0 | modal + Secondary on |
| 55 | Secondary Enabled | bool >=0.5 | 0.0 | (needs strength>0) |
| 56 | Secondary Size | shell f0 = head*(1-norm*0.75) | 0.5 | secondary on |
| 57 | Secondary Material | shell b1/b3 | 0.4 | secondary on |
| 58 | Tension Mod | energy pitch glide (max +3 st) | 0.0 | MEMBRANE only |
| 59 | Pad Enabled | bool >=0.5; OFF early-returns noteOn | 1.0 | all |
| 60 | PitchEnv Knee | bool >=0.5 (3-point env) | 0.0 | all (Time>0) |
| 61 | PitchEnv Mid | 20*100^norm Hz | 0.5 | knee on |
| 62 | PitchEnv Mid Frac | fraction of total time | 0.5 | knee on |
| 63 | PitchEnv Curve2 | norm*2-1 seg2, 0.5=linear | 0.5 | knee on |
| 64 | Pan | equal-power: gainL=sqrt2*cos(pan*pi/2), R=sin | 0.5 | all |

(*) b1/b3 PadConfig struct default is the -1.0 sentinel; 0.5 is the registered/display default that only takes effect once written.

## Kit-level globals (plugin_ids.h)

| Id | Name | norm -> physical | Default |
|----|------|------------------|---------|
| 250 | Max Polyphony | 4+round(norm*12) [4,16] voices | 8 |
| 251 | Voice Stealing | {Oldest,Quietest,Priority} idx=int(norm*3) | Oldest |
| 270 | Global Coupling | couplingEngine amount [0,1] | 0.0 |
| 271 | Snare Buzz | coupling-matrix weight | 0.0 |
| 272 | Tom Resonance | coupling-matrix weight | 0.0 |
| 273 | Coupling Delay | 0.5+norm*1.5 ms [0.5,2.0] | 1.0 ms |
| 280 | UI Mode | {Simple,Advanced}; session-only, no DSP | 0 |
| 320 | Master Gain | dB=-24+36*norm; gain=10^(dB/20); MAIN bus only | -6 dB |

All other globals (100-252, 282, 290-325) are SELECTED-PAD PROXIES (see per-pad table). 260 = Selected Pad index.